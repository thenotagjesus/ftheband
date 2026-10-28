// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <chrono>
#include <array>

namespace sj {

/** Bridges two audio devices running on independent clocks (split-input mode: Real Tone cable on its own WASAPI
    input stream, output on the AudioDeviceManager). Design: Aura review doc 01, with these changes:
      - PI loop tuned as a critically damped 2nd-order loop (wn = 2*pi*0.02 rad/s): converges in ~40 s, rejects
        callback jitter, error at 300 ppm stays < ~1 ms. Conditional-integration anti-windup at the +-500 ppm clamp.
      - NO ZEROS on underrun/overrun: the read head jumps back/forward to the target fill and the jump is hidden by an
        equal-power crossfade between the old and new heads (history behind the read head is kept for back-jumps).
        Only if the input has truly stopped (no new samples) do we fade out to silence and re-prime with a fade-in.
      - 6-point, 5th-order Lagrange interpolation.
      - Phase-independent fill measurement: push() timestamps each block, pull() interpolates the writer's position
        to "now" (w - inBlock + elapsed * rate), so the slowly sliding phase between the two callbacks (a beat with a
        period of blockTime/ppm, e.g. 33 s at 300 ppm with 10 ms blocks) does not leak into the ratio.
      - Recovery jumps are phase-aligned (WSOLA-style correlation search over +-1 period of 50 Hz) and crossfaded
        over ~5 ms, so even a pure sine shows no discontinuity.
    Threading: push() = input-device thread only, pull() = output-device thread only, wait-free; telemetry atomics
    may be read from any thread. prepare()/reset() allocate or clear and must only be called while both devices are
    stopped (or neither thread is inside push/pull). */
class DriftResampler {
public:
    static constexpr int    Taps = 6;
    static constexpr double MaxPpm = 500.0;

    struct Telemetry {
        std::atomic<float> ppm { 0 };            // current correction: >0 = input clock faster than output
        std::atomic<float> fill { 0 };           // smoothed fill (samples)
        std::atomic<float> target { 0 };
        std::atomic<float> fillMin { 1e9f }, fillMax { 0 };   // raw fill extrema since last takeMinMax()
        std::atomic<int>   underruns { 0 };      // low-water back-jumps (crossfaded)
        std::atomic<int>   overruns { 0 };       // high-water forward-jumps (crossfaded)
        std::atomic<int>   stalls { 0 };         // input stopped -> faded out
        std::atomic<int>   pushDrops { 0 };      // ring full, input block dropped (output stopped / hard fault)
        std::atomic<int>   holds { 0 };          // emergency head freeze (should stay 0)
        std::atomic<bool>  primed { false };
        std::atomic<int>   retargets { 0 };      // adaptive target changes applied by a (phase-aligned) jump
        std::atomic<float> jitter { 0 };         // learnt fill excursion below the mean (samples), drives the target
        std::atomic<float> targetMin { 1e9f };   // lowest target used since prepare/reset
    };

    /** adaptive = true: start at the safe fixed target (below), learn the real fill excursion (callback bursts +
        scheduling jitter) for learnSeconds after priming, then jump once to lowWater + excursion + safety and keep
        tracking it: raised at once when the jitter grows (or on any underrun), lowered by <= 1 sample/s. */
    void prepare (double sampleRateIn, int outBlockMax, int inBlockMax, int crossfadeSamples = 128, bool adaptiveTarget = true)
    {
        sr = sampleRateIn; outBlock = std::max (1, outBlockMax); inBlock = std::max (1, inBlockMax);
        xfLen = std::max (16, crossfadeSamples); adaptive = adaptiveTarget;
        // safe fixed target, ~2.5 blocks: one input block + one output block (the two bursts) + one more of the larger
        // as jitter margin, plus the crossfade reserve and interpolator taps. Adaptive mode starts here and never exceeds it.
        startTarget = (double) (inBlock + outBlock + std::max (inBlock, outBlock) + xfLen + Taps);
        maxTarget = startTarget + (adaptive ? 2.0 * std::max (inBlock, outBlock) + 0.010 * sr : 0.0);   // adaptive may grow up to here
        targetFill = maxTarget;
        lowWaterExtra = (double) (xfLen + Taps);
        lowThresh = (double) outBlock + Taps + 2 + lowWaterExtra;        // fill (after a pull) below which the next pull underruns
        safety = std::max (64.0, 0.003 * sr);                            // 3 ms on top of the worst excursion seen
        highWater = targetFill + 2.0 * std::max (inBlock, outBlock) + xfLen;   // sizes the ring for the largest target
        searchLag = (int) (sr / 50.0);   // +-20 ms covers one period of anything >= 50 Hz
        histKeep = (int64_t) targetFill + xfLen + Taps + 8 + searchLag;
        size_t need = (size_t) (highWater + histKeep + 4 * inBlock + 4 * outBlock + 64);
        size_t cap = 8192; while (cap < 2 * need) cap <<= 1;
        ring.assign (cap, 0.0f); mask = (int64_t) cap - 1;
        const double wn = 2.0 * 3.14159265358979 * 0.02, zeta = 1.0;
        kp = 2.0 * zeta * wn / sr; ki = wn * wn / sr;
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        write.store (0); readIndexInt.store (0); pushTime.store (-1.0);
        read = oldRead = 0; ratio = 1; integ = 0; fillLp = 0; mode = Mode::Unprimed; primeBase = 0; xfPos = 0;
        lastRecoveryW = -1;
        // while learning, sit high (old fixed formula or block-size headroom + 8 ms, whichever is larger), then jump down once
        targetFill = adaptive ? std::min (maxTarget, std::max (startTarget, lowThresh + safety + 2.0 * inBlock + 0.008 * sr)) : startTarget;
        highWater = targetFill + 2.0 * std::max (inBlock, outBlock) + xfLen;
        winMin = 1e18; winN = 0; primedSamples = 0; dPeak = 0; learnt = false; integGood = 0; haveGood = false;
        bucket.fill (0.0); bucketIdx = 0; bucketN = 0;
        tel.ppm = 0; tel.fill = 0; tel.target = (float) targetFill; tel.primed = false; tel.retargets = 0; tel.jitter = 0; tel.targetMin = (float) targetFill;
        tel.underruns = 0; tel.overruns = 0; tel.stalls = 0; tel.pushDrops = 0; tel.holds = 0; takeMinMax();
    }

    // ---------------------------------------------------------------- producer (input device thread)
    static double nowSeconds() noexcept
    { return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count(); }
    void push (const float* x, int n) noexcept { push (x, n, nowSeconds()); }
    void pull (float* out, int n) noexcept     { pull (out, n, nowSeconds()); }

    void push (const float* x, int n, double tNow) noexcept
    {
        const int64_t w = write.load (std::memory_order_relaxed);
        const int64_t oldest = readIndexInt.load (std::memory_order_acquire);
        if (w + n - oldest > (int64_t) ring.size()) { tel.pushDrops.fetch_add (1, std::memory_order_relaxed); return; }
        for (int i = 0; i < n; ++i) ring[(size_t) ((w + i) & mask)] = x != nullptr ? x[i] : 0.0f;
        pushTime.store (tNow, std::memory_order_relaxed);
        write.store (w + n, std::memory_order_release);
    }

    // ---------------------------------------------------------------- consumer (output device thread)
    void pull (float* out, int n, double tNow) noexcept
    {
        const int64_t w = write.load (std::memory_order_acquire);
        const double tPush = pushTime.load (std::memory_order_relaxed);
        const double dt = (double) n / sr;

        if (mode == Mode::Unprimed) {
            if (w - primeBase >= (int64_t) targetFill + Taps) {
                read = (double) (w - (int64_t) targetFill); fillLp = targetFill;
                mode = Mode::FadeIn; xfPos = 0; tel.primed = true;
            } else {
                std::fill (out, out + n, 0.0f);
                readIndexInt.store (std::max<int64_t> (0, w - histKeep), std::memory_order_release);
                return;
            }
        }

        double fill = (double) w - read;
        const double need = ratio * n + Taps + 2;
        if (mode == Mode::Normal) {
            if (fill < need + lowWaterExtra) {
                if (adaptive && learnt) bumpTarget();
                if (w != lastRecoveryW && (double) w - targetFill > read - (double) histKeep + Taps) {
                    startJump (read + (fillLp - targetFill), w); tel.underruns.fetch_add (1, std::memory_order_relaxed);   // land the smoothed (phase-free) fill on target
                } else {   // the input stopped: fade out, and drop whatever the PI learnt while starving
                    mode = Mode::FadeOut; xfPos = 0; tel.stalls.fetch_add (1, std::memory_order_relaxed);
                    integ = haveGood ? integGood : 0.0;
                }
                lastRecoveryW = w;
            } else if (fill > highWater) {
                startJump (read + (fillLp - targetFill), w); tel.overruns.fetch_add (1, std::memory_order_relaxed);   // land the smoothed (phase-free) fill on target
                lastRecoveryW = w;
            }
        }

        const double maxRead = (double) w - (Taps / 2 + 1);   // never interpolate past written data
        const float halfPi = 1.57079632679f;
        int i = 0;
        for (; i < n; ++i) {
            if (read > maxRead) { read = maxRead; tel.holds.fetch_add (1, std::memory_order_relaxed); }
            float y = lag (read);
            if (mode == Mode::Crossfade) {
                if (oldRead > maxRead) oldRead = maxRead;
                const float g = (float) xfPos / (float) xfLen;
                // aligned (correlated) material: linear fade keeps the level; uncorrelated: equal-power
                const float gl = linXf ? g : std::sin (g * halfPi), go = linXf ? 1.0f - g : std::cos (g * halfPi);
                y = go * lag (oldRead) + gl * y;
                oldRead += ratio;
                if (++xfPos >= xfLen) mode = Mode::Normal;
            } else if (mode == Mode::FadeIn) {
                y *= std::sin ((float) xfPos / (float) xfLen * halfPi);
                if (++xfPos >= xfLen) mode = Mode::Normal;
            } else if (mode == Mode::FadeOut) {
                y *= std::cos ((float) xfPos / (float) xfLen * halfPi);
                if (++xfPos >= xfLen) { mode = Mode::Unprimed; tel.primed = false; out[i++] = y; break; }
            }
            out[i] = y;
            read += ratio;
        }
        if (mode == Mode::Unprimed) {   // faded out because the input stopped: silence until it comes back
            std::fill (out + i, out + n, 0.0f);
            primeBase = w; ratio = 1.0;
            readIndexInt.store (std::max<int64_t> (0, w - histKeep), std::memory_order_release);
            return;
        }

        // ---- PI on the low-passed, phase-independent fill
        fill = (double) w - read;
        double fillC = fill;
        if (tPush > 0) {
            const double el = std::clamp (tNow - tPush, 0.0, 2.0 * inBlock / sr);
            fillC = fill - inBlock + el * sr * ratio + 0.5 * inBlock;   // continuous writer position, centred like the raw mean
        }
        tel.fillMin = std::min (tel.fillMin.load (std::memory_order_relaxed), (float) fill);
        tel.fillMax = std::max (tel.fillMax.load (std::memory_order_relaxed), (float) fill);
        if (mode == Mode::Normal || mode == Mode::FadeIn) {
            const double a = 1.0 - std::exp (-2.0 * 3.14159265358979 * 0.2 * dt);
            fillLp += a * (fillC - fillLp);
            const double err = fillLp - targetFill;
            const double lim = MaxPpm * 1e-6;
            // anti-windup: the I term (the clock-offset estimate) is itself clamped to +-500 ppm, so beyond the limit
            // it parks at the limit and the ratio stays there across recovery jumps
            // also frozen while the input is starving (late/stopped input is not a clock offset; integrating it
            // slams the ratio to the limit - the "ppm +-500, stalls 1" pattern in the rig's drift.log)
            const bool starving = tPush > 0 && tNow - tPush > 3.0 * inBlock / sr + 0.005;
            if (! starving) integ = std::clamp (integ + err * dt * sr, -lim * sr / ki, lim * sr / ki);   // samples*samples
            ratio = 1.0 + std::clamp (kp * err + ki * integ / sr, -lim, lim);
            if (! starving && mode == Mode::Normal && std::abs (err) < 0.5 * safety) {   // remember a healthy clock estimate
                integGood = haveGood ? integGood + 0.001 * (integ - integGood) : integ; haveGood = true; }
        }
        if (adaptive) adapt (fill, n);
        tel.ppm.store ((float) ((ratio - 1.0) * 1e6), std::memory_order_relaxed);
        tel.fill.store ((float) fillLp, std::memory_order_relaxed);
        const double oldest = std::min (read, mode == Mode::Crossfade ? oldRead : read) - (double) histKeep;
        readIndexInt.store (std::max<int64_t> (0, (int64_t) std::floor (oldest)), std::memory_order_release);
    }

    /** Raw fill extrema since the previous call (any thread). */
    std::pair<float, float> takeMinMax() noexcept { return { tel.fillMin.exchange (1e9f), tel.fillMax.exchange (0.0f) }; }
    double targetFillSamples() const noexcept { return targetFill; }
    int64_t samplesWritten() const noexcept { return write.load (std::memory_order_relaxed); }   // any thread (stall watchdog)
    double latencyMs() const noexcept { return targetFill / sr * 1000.0; }
    double maxLatencyMs() const noexcept { return startTarget / sr * 1000.0; }   // the old fixed-formula value, for comparison
    bool   isAdaptive() const noexcept { return adaptive; }
    static constexpr double learnSeconds = 8.0;
    bool   isPrimed() const noexcept { return tel.primed.load(); }

    Telemetry tel;

private:
    enum class Mode { Unprimed, FadeIn, Normal, Crossfade, FadeOut };

    void startJump (double newRead, int64_t w) noexcept
    {
        // phase-align: pick the new position near newRead whose next L samples best match the old head's
        const int L = 256;
        const double hiLim = (double) w - (Taps / 2 + 2) - xfLen * 1.01 - L;   // candidate must have data for the fade
        const double loLim = std::max (read - (double) histKeep + Taps + 2,                  // history still in the ring
                                       (double) w - highWater + inBlock);                    // and not straight into an overrun
        const double hiLim2 = (double) w - (ratio * outBlock + Taps + 2 + lowWaterExtra + outBlock);  // nor an underrun
        double best = newRead, bestC = -1e30; bestRho = 0;
        float ref[256]; double e0 = 1e-12;
        for (int k = 0; k < L; ++k) { ref[k] = lag (read + k * ratio); e0 += (double) ref[k] * ref[k]; }
        for (int d = -searchLag; d <= searchLag; ++d) {
            const double c0 = std::floor (newRead) + d + (read - std::floor (read));
            if (c0 > hiLim || c0 > hiLim2 || c0 < loLim) continue;
            const int64_t ci = (int64_t) std::floor (c0);
            double xy = 0, e1 = 1e-12;
            for (int k = 0; k < L; ++k) { const float v = at (ci + k); xy += (double) ref[k] * v; e1 += (double) v * v; }
            const double c = xy / std::sqrt (e0 * e1) - 0.15 * std::abs ((double) d) / searchLag;  // prefer small shifts  // prefer the nominal spot on ties
            if (c > bestC) { bestC = c; best = c0; bestRho = xy / std::sqrt (e0 * e1); }
        }
        linXf = bestRho > 0.5;
        oldRead = read; read = best; mode = Mode::Crossfade; xfPos = 0;
        fillLp += (double) w - best - ((double) w - oldRead);   // shift the estimate by the jump; keep the integrator (it holds the clock-offset estimate)
    }

    // ---- adaptive target (consumer thread)
    void setTarget (double t) noexcept
    {
        targetFill = std::clamp (t, lowThresh + 16.0, maxTarget);
        highWater = targetFill + 2.0 * std::max (inBlock, outBlock) + xfLen;
        tel.target.store ((float) targetFill, std::memory_order_relaxed);
        if ((float) targetFill < tel.targetMin.load (std::memory_order_relaxed)) tel.targetMin.store ((float) targetFill, std::memory_order_relaxed);
    }
    void bumpTarget() noexcept   // an underrun: the learnt excursion was too small
    {
        dPeak += std::max (64.0, 0.5 * inBlock);
        bucket[(size_t) bucketIdx] = std::max (bucket[(size_t) bucketIdx], dPeak);
        setTarget (lowThresh + dPeak + safety);
        tel.jitter.store ((float) dPeak, std::memory_order_relaxed);
    }
    void adapt (double fillAfterPull, int n) noexcept
    {
        if (mode != Mode::Normal) { winMin = 1e18; winN = 0; return; }   // crossfades/fades distort the window
        primedSamples += n;
        winMin = std::min (winMin, fillAfterPull); winN += n;
        const int64_t win = (int64_t) (0.5 * sr);
        if (winN < win) return;
        const double d = std::max (0.0, fillLp - winMin);   // how far the raw fill dips below its smoothed mean
        winMin = 1e18; winN = 0;
        if (primedSamples < (int64_t) (2.0 * sr)) return;   // let the PI settle first
        const double desiredOf = lowThresh + safety;
        if (! learnt) {
            dPeak = std::max (dPeak, d); bucket[0] = std::max (bucket[0], dPeak);
            if (primedSamples >= (int64_t) ((2.0 + learnSeconds) * sr)) {
                learnt = true;
                const double t = std::clamp (desiredOf + dPeak, lowThresh + 16.0, maxTarget);
                if (t > targetFill) setTarget (t);
                if (t < targetFill - 8.0) {   // one phase-aligned forward jump straight onto the new target
                    const double oldT = targetFill; setTarget (t);
                    if (lastRecoveryW != write.load (std::memory_order_relaxed)) {
                        startJump (read + (fillLp - targetFill), write.load (std::memory_order_acquire));
                        tel.retargets.fetch_add (1, std::memory_order_relaxed);
                    } else setTarget (oldT);
                }
            }
        } else {
            // memory: the worst excursion of the last ~10 min (20 x 30 s buckets) is a hard floor, so a quiet stretch
            // cannot walk the target down until the next DPC burst underruns (= a click every few minutes)
            bucket[(size_t) bucketIdx] = std::max (bucket[(size_t) bucketIdx], d);
            if (++bucketN >= 60) { bucketN = 0; bucketIdx = (bucketIdx + 1) % NumBuckets; bucket[(size_t) bucketIdx] = 0; }
            double floor10 = 0; for (double b : bucket) floor10 = std::max (floor10, b);
            dPeak = std::max ({ d, floor10, dPeak - 0.5 });   // above the floor: forgets slowly (~1 sample/s)
            const double t = desiredOf + dPeak;
            if (t > targetFill + 1.0) setTarget (t);           // grow at once: the PI slows the reader
            else if (t < targetFill - 16.0) setTarget (targetFill - 0.25);   // hysteresis, ~0.5 sample/s: invisible to the PI
        }
        tel.jitter.store ((float) dPeak, std::memory_order_relaxed);
    }

    float at (int64_t i) const noexcept { return ring[(size_t) (i & mask)]; }
    float lag (double pos) const noexcept
    {
        const int64_t i = (int64_t) std::floor (pos);
        const float t = (float) (pos - (double) i);
        const float ym2 = at (i - 2), ym1 = at (i - 1), y0 = at (i), y1 = at (i + 1), y2 = at (i + 2), y3 = at (i + 3);
        // 6-point Lagrange, nodes -2..3
        const float tp2 = t + 2, tp1 = t + 1, tm1 = t - 1, tm2 = t - 2, tm3 = t - 3;
        return ym2 * (-t * tp1 * tm1 * tm2 * tm3 / 120.0f)
             + ym1 * (tp2 * t * tm1 * tm2 * tm3 / 24.0f)
             + y0  * (-tp2 * tp1 * tm1 * tm2 * tm3 / 12.0f)
             + y1  * (tp2 * tp1 * t * tm2 * tm3 / 12.0f)
             + y2  * (-tp2 * tp1 * t * tm1 * tm3 / 24.0f)
             + y3  * (tp2 * tp1 * t * tm1 * tm2 / 120.0f);
    }

    std::vector<float> ring; int64_t mask = 0;
    std::atomic<int64_t> write { 0 }, readIndexInt { 0 }; std::atomic<double> pushTime { -1.0 };
    int searchLag = 960;
    double sr = 48000; int outBlock = 512, inBlock = 480, xfLen = 128;
    double targetFill = 1024, lowWaterExtra = 134, highWater = 2048; int64_t histKeep = 1200;
    double maxTarget = 1024, startTarget = 1024, lowThresh = 300, safety = 96; bool adaptive = true;
    double winMin = 1e18, dPeak = 0; int64_t winN = 0, primedSamples = 0; bool learnt = false;
    double kp = 0, ki = 0, integGood = 0; bool haveGood = false;
    static constexpr int NumBuckets = 20; std::array<double, NumBuckets> bucket {}; int bucketIdx = 0, bucketN = 0;
    // consumer-only state
    double bestRho = 0; bool linXf = false;
    double read = 0, oldRead = 0, ratio = 1, integ = 0, fillLp = 0;
    Mode mode = Mode::Unprimed; int64_t primeBase = 0, lastRecoveryW = -1; int xfPos = 0;
};

} // namespace sj
