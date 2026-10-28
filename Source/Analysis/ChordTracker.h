// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include "Music/InputInstrument.h"
#include <JuceHeader.h>
#include <deque>
#include "Music/Theory.h"

namespace sj {

/** Tempo/phase from strums. Count-in: the first 4 evenly spaced onsets set BPM and the downbeat phase.
    Afterwards onsets close to a predicted beat nudge phase (10%) and tempo (bounded ±4%). */
class BeatTracker {
public:
    double bpm = 0; int64_t beatPhase = 0; bool locked = false; double sr = 48000;
    void prepare (double s) { sr = s; reset(); }
    void reset() { locked = false; bpm = 0; count.clear(); }
    /** Returns true when the count-in has just locked. */
    bool onOnset (int64_t t) {
        if (! locked) {
            count.push_back (t); if (count.size() > 4) count.erase (count.begin());
            if (count.size() == 4) {
                double i1 = (double) (count[1] - count[0]), i2 = (double) (count[2] - count[1]), i3 = (double) (count[3] - count[2]);
                const double mean = (i1 + i2 + i3) / 3;
                const double dev = juce::jmax (std::abs (i1 - mean), std::abs (i2 - mean), std::abs (i3 - mean));
                double b = 60.0 * sr / mean;
                if (b > 160.0 && b * 0.5 >= 50.0) b *= 0.5;   // four even 8th-note strums: count quarters (phase stays on a beat)
                if (dev < 0.08 * mean && b >= 50 && b <= 220 && 60.0 * sr / mean <= 440) { bpm = b; baseBpm = b; beatPhase = count[3]; locked = true; return true; }
            }
            return false;
        }
        const double period = 60.0 * sr / bpm;
        const double k = std::round ((double) (t - beatPhase) / period);
        const double err = (double) (t - beatPhase) - k * period;
        if (std::abs (err) < 0.12 * period) {      // only on-beat strums steer
            beatPhase += (int64_t) (0.1 * err);
            bpm = juce::jlimit (baseBpm * 0.96, baseBpm * 1.04, bpm * (1.0 + 0.02 * err / period));
        }
        return false;
    }
private:
    std::vector<int64_t> count; double baseBpm = 0;
};
/** Real-time player follower: onsets, harmonic-weighted multi-pitch chroma, bass note, chord templates with
    hysteresis smoothing, slow key estimate and intensity. Not real-time safe (FFTs): run it on an analysis thread,
    or inline when rendering offline. Feed with process(); all times are in absolute input samples. */
class ChordTracker {
public:
    int N = 8192;                           // chroma FFT (8192 guitar ≈171 ms, 16384 bass ≈341 ms at 48 kHz)
    InputInstrument instrument = InputInstrument::GuitarStandard; InputProfile profile = inputProfile (InputInstrument::GuitarStandard);
    bool strangleHint = false;              // Bass VI 'strangle' (low-cut) switch is on: fundamentals are weak
    bool strangleDetected = false;          // auto: energy <100 Hz vs 100-400 Hz on note frames
    float lowRatioDb = 0;                   // smoothed 10*log10(E<100 / E100-400)
    bool strangleActive() const { return profile.lowRegister && (strangleHint || strangleDetected); }
    /** Call before prepare(). Low-register profiles use a 16384 FFT, a lower pitch range, stronger root/bass
        evidence, key context and a longer confirmation so chromatic passing notes do not flip the chord. */
    /** Tuning only widens/narrows the search range (and is used for display). Key/chord detection stays in sounding pitch. */
    void setInstrument (InputInstrument i, Tuning t = Tuning::Standard, float customCents = 0) {
        instrument = i; profile = inputProfile (i, t, customCents); N = 1 << profile.fftOrder;
        if (profile.lowRegister) params.confirmSeconds = 0.12f;
    }
    static constexpr int hopC = 1024;       // chroma frame hop
    static constexpr int NO = 1024;         // onset FFT
    static constexpr int hopO = 256;        // onset hop (5.3 ms)

    struct Params {
        float evidenceTau = 0.30f;   // s, leaky evidence integration
        float onsetReset = 0.25f;    // evidence multiplier on each new onset (fresh strum = fresh evidence)
        float hysteresis = 0.20f;    // candidate must beat current by this fraction
        int   confirmFrames = 2;     // consecutive frames candidate must lead
        float confirmSeconds = 0.0f; // low register: candidate must lead this long unless its onset lands on a beat
        float gateDb = -50.0f;       // input gate
        float keyTau = 20.0f;        // s, long-term chroma for key
        float keyHoldSeconds = 4.0f; // new key must lead this long to replace the current one
        bool  countInGate = true;    // first 4 evenly spaced onsets = count-in: never committed as chords
        float holdAfterOnset = 1.5f; // s without a new onset => hold the chord (decaying tails / gaps never flip it)
    } params;

    struct Note { int midi; float salience; };

    // --------------------------------------------------------------- outputs (read by owner on same thread)
    ChordLabel chord;                       // committed (smoothed) chord
    ChordLabel frameChord;                  // raw per-frame best
    int64_t chordChangedAt = -1;            // sample where committed chord last changed
    int bassMidi = -1;                      // lowest strong pitch in last frame
    KeyEstimate key;                        // slow key
    std::array<float, 12> chroma {};        // last frame (normalised)
    float intensity = 0, rmsDb = -100, onsetRate = 0;
    int64_t lastOnset = -1000000;           // sample of last onset
    int64_t totalSamples = 0;
    int onsetCount = 0;
    BeatTracker beat;                       // count-in lock + tempo/phase
    /** True while the tracker is still inside a possible count-in (band should stay out, chords are not committed). */
    bool countInActive() const { return params.countInGate && countInOpen; }
    juce::dsp::IIR::Filter<float> inHp;
    /** Hum notches, set by learnNoiseFrom(): narrow, stationary peaks (35-160 Hz: hum and its first harmonics; higher lines sit on top of played notes, e.g. 244 Hz next to B3) in ~1 s of input captured before the
        count-in. The user's take had a 48.2 Hz series (48/96/145 Hz) in the silence before the first note: not mains,
        not a string (nothing was played), and close enough to G1/G2/D3 to bias chroma/key towards G. */
    static constexpr int maxNotches = 4; juce::dsp::IIR::Filter<float> notch[maxNotches]; int nNotches = 0; float notchHz[maxNotches] {};
    bool noiseLearned = false;
    /** Analyse a stretch of input with nothing played (0.3-2 s): sets the gate's noise floor and the hum notches.
        Not real-time safe (allocates, FFT); call on the analysis thread or offline, after prepare(). */
    void learnNoiseFrom (const float* x, int n) {
        if (n < (int) (0.25 * sr)) return;
        std::vector<float> y ((size_t) n);
        { juce::dsp::IIR::Filter<float> hp; hp.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 30.0f, 0.7071f);
          for (int i = 0; i < n; ++i) y[(size_t) i] = hp.processSample (x[i]); }
        // Welch power spectrum, ~1.5 Hz resolution when 0.7 s+ is available
        int order = 15; while (order > 12 && (1 << order) > n) --order;
        const int L = 1 << order; juce::dsp::FFT f (order); std::vector<float> buf ((size_t) L * 2), P ((size_t) L / 2, 0.0f); int segs = 0;
        for (int st = 0; st + L <= n; st += L / 4) { std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < L; ++i) buf[(size_t) i] = y[(size_t) (st + i)] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (i + 0.5f) / L));
            f.performFrequencyOnlyForwardTransform (buf.data(), true); for (int k = 0; k < L / 2; ++k) P[(size_t) k] += buf[(size_t) k] * buf[(size_t) k]; ++segs; }
        if (segs == 0) return;
        const float bin = (float) (sr / L);
        const int k0 = juce::jmax (3, (int) (35 / bin)), k1 = juce::jmin (L / 2 - 2, (int) (160 / bin)), w = juce::jmax (6, (int) (15 / bin)), ex = juce::jmax (2, (int) (2.5f / bin));
        std::vector<std::pair<float,int>> pk;
        for (int k = k0; k < k1; ++k) {
            const float v = P[(size_t) k]; if (v < P[(size_t) k - 1] || v < P[(size_t) k + 1]) continue;
            std::vector<float> nb; for (int j = k - w; j <= k + w; ++j) if (std::abs (j - k) > ex && j > 0 && j < L / 2) nb.push_back (P[(size_t) j]);
            std::nth_element (nb.begin(), nb.begin() + (long) nb.size() / 2, nb.end());
            const float ratio = v / (nb[nb.size() / 2] + 1e-20f);
            if (ratio > 15.8f) pk.push_back ({ ratio, k });            // > 12 dB above its +-15 Hz neighbourhood: a line, not noise
        }
        std::sort (pk.begin(), pk.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        nNotches = 0;
        for (auto& [ratio, k] : pk) { if (nNotches >= maxNotches) break;
            const float a = std::log (P[(size_t) k - 1] + 1e-20f), b = std::log (P[(size_t) k] + 1e-20f), c = std::log (P[(size_t) k + 1] + 1e-20f);
            const float hz = ((float) k + 0.5f * (a - c) / (a - 2 * b + c - 1e-9f)) * bin;
            notchHz[nNotches] = hz; notch[nNotches].coefficients = juce::dsp::IIR::Coefficients<float>::makeNotch (sr, hz, 30.0f); notch[nNotches].reset(); ++nNotches; }
        std::sort (notchHz, notchHz + nNotches);
        for (int i = 0; i < nNotches; ++i) { notch[i].coefficients = juce::dsp::IIR::Coefficients<float>::makeNotch (sr, notchHz[i], 30.0f); notch[i].reset(); }
        for (int i = 0; i < nNotches; ++i) { juce::dsp::IIR::Filter<float> nf; nf.coefficients = notch[i].coefficients; for (auto& v : y) v = nf.processSample (v); }
        // floor (after the notches, as the tracker will see it): median RMS of 2048-sample blocks, first one skipped
        std::vector<float> dbs; for (int i = 2048; i + 2048 <= n; i += 2048) { double e = 0; for (int k = 0; k < 2048; ++k) e += (double) y[(size_t) (i + k)] * y[(size_t) (i + k)];
            dbs.push_back ((float) (10.0 * std::log10 (e / 2048 + 1e-12))); }
        if (! dbs.empty()) { std::nth_element (dbs.begin(), dbs.begin() + (long) dbs.size() / 2, dbs.end()); noiseFloorDb = dbs[dbs.size() / 2]; }
        noiseLearned = true;
    }
    bool countInOpen = true; float frameDb = -100; int64_t countInLockedAt = -1;
    std::vector<int64_t> recentOnsets;      // last ~4 s

    void prepare (double sampleRate) {
        sr = sampleRate;
        hist.assign ((size_t) histSize, 0.0f);
        fft = std::make_unique<juce::dsp::FFT> (profile.fftOrder); fftO = std::make_unique<juce::dsp::FFT> (10);
        work.assign ((size_t) N * 2, 0.0f); workO.assign ((size_t) NO * 2, 0.0f);
        prevLogMag.assign (NO / 2 + 1, 0.0f);
        winO.resize (NO); for (int i = 0; i < NO; ++i) winO[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / NO);
        beat.prepare (sr);
        nNotches = 0; noiseLearned = false;
        reset();
        // candidate pitch table (E1..E6), harmonic search ranges
        binHz = (float) (sr / N);
    }
    void reset() {
        std::fill (hist.begin(), hist.end(), 0.0f); totalSamples = 0; sinceC = sinceO = 0;
        inHp.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 30.0f, 0.7071f); inHp.reset();   // learnt notches/floor survive a reset; prepare() clears them
        for (int k = 0; k < nNotches; ++k) notch[k].reset();
        evidence.fill (0); chord = {}; frameChord = {}; chordChangedAt = -1; countInOpen = true; countInLockedAt = -1; beat.reset(); key = {}; keyCand = -1; keyCandSince = 0;
        longChroma.fill (0); odfHist.clear(); noiseFloorDb = noiseLearned ? noiseFloorDb : -100.0f; peakDb = -100; noiseFrames = 0; noiseSpec.clear(); lastOnset = -1000000; recentOnsets.clear(); onsetCount = 0; present = false; lastActive = -1000000;
        intensity = 0; rmsDb = -100; candidate = -1; candFrames = 0; std::fill (prevLogMag.begin(), prevLogMag.end(), 0.0f);
        odf1 = odf2 = 0; rmsAcc = 0; rmsCnt = 0;
    }

    /** Push input; runs onset frames every hopO and chroma frames every hopC. Returns true if the committed chord changed. */
    bool process (const float* x, int n) {
        bool changed = false;
        for (int i = 0; i < n; ++i) {
            // -80 dBFS dither: real inputs always carry some noise; exact digital silence made the
            // noise model degenerate (clean eval scored 46% vs 86% with a 60 dB SNR floor).
            ditherState = ditherState * 1664525u + 1013904223u;
            float xi = x[i] + 1.0e-4f * ((float) (ditherState >> 8) * (1.0f / 8388608.0f) - 1.0f);
            xi = inHp.processSample (xi);
            for (int k = 0; k < nNotches; ++k) xi = notch[k].processSample (xi);   // input conditioning: 30 Hz HP (DC, rumble, thumps) + learnt hum notches
            hist[(size_t) (totalSamples & (histSize - 1))] = xi;
            rmsAcc += xi * xi; ++rmsCnt;
            ++totalSamples;
            if (++sinceO >= hopO) { sinceO = 0; onsetFrame(); }
            if (++sinceC >= hopC) { sinceC = 0; changed |= chromaFrame(); }
        }
        return changed;
    }

    double seconds (int64_t samples) const { return (double) samples / sr; }
    float gate() const { return juce::jmax (params.gateDb, noiseFloorDb + 6.0f, peakDb - 20.0f); }
    float peakDb = -100; uint32_t ditherState = 22222u;
    float noiseFloorDb = -100;
    float presenceOnDb() const { return juce::jmax (params.gateDb, noiseFloorDb + 6.0f) - (strangleActive() ? 3.0f : 0.0f); }
    float presenceOffDb() const { return juce::jmax (presenceOnDb() - 6.0f, noiseFloorDb + 3.0f); }
    bool present = false; int64_t lastActive = -1000000;   // last frame end the presence gate was open
    bool isSilent() const { return rmsDb < gate() && totalSamples - lastOnset > (int64_t) (0.5 * sr); }
    std::vector<Note> notes;                // last frame's multi-pitch notes

private:
    static constexpr int histSize = 1 << 15;
    double sr = 48000; float binHz = 5.86f;
    std::vector<float> hist, work, workO, prevLogMag, winO;
    std::unique_ptr<juce::dsp::FFT> fft, fftO;
    int sinceC = 0, sinceO = 0;
    float rmsAcc = 0; int rmsCnt = 0;
    std::deque<float> odfHist; float odf1 = 0, odf2 = 0;
    std::array<float, 12 * NumChordTypes> evidence {};
    int candidate = -1, candFrames = 0;
    std::array<float, 12> longChroma {};
    int keyCand = -1; int64_t keyCandSince = 0;

    float sampleAt (int64_t t) const { return t < 0 ? 0.0f : hist[(size_t) (t & (histSize - 1))]; }

    // ------------------------------------------------------------------ onsets: log-magnitude spectral flux
    void onsetFrame() {
        const int64_t end = totalSamples;
        for (int i = 0; i < NO; ++i) workO[(size_t) i] = sampleAt (end - NO + i) * winO[(size_t) i];
        std::fill (workO.begin() + NO, workO.end(), 0.0f);
        fftO->performFrequencyOnlyForwardTransform (workO.data(), true);
        float flux = 0;
        const int kMax = (int) (8000.0 / (sr / NO));
        for (int k = 2; k < juce::jmin (kMax, NO / 2); ++k) {
            float lm = std::log1p (100.0f * workO[(size_t) k]);
            flux += juce::jmax (0.0f, lm - prevLogMag[(size_t) k]);
            prevLogMag[(size_t) k] = lm;
        }
        // level / intensity bookkeeping
        const float rms = std::sqrt (rmsAcc / (float) juce::jmax (1, rmsCnt)); rmsAcc = 0; rmsCnt = 0;
        const float db = juce::Decibels::gainToDecibels (rms, -100.0f); frameDb = db;
        rmsDb += (db - rmsDb) * (db > rmsDb ? 0.3f : 0.02f);
        peakDb = db > peakDb ? db : peakDb - 0.02f; // ~2 dB/s release at 100 fps
        // presence (is the player still sounding?) for the band's hold timer: hysteretic and WITHOUT the peak-20 dB
        // term, so a decaying Bass VI sustain (or a strangled, fundamental-less note) counts as playing all the way down
        if (! present && db > presenceOnDb()) present = true;
        else if (present && db < presenceOffDb()) present = false;
        if (present) lastActive = end;
        // noise floor: fast down, very slow up (≈ +1 dB per 4 s) so sustained playing is not mistaken for noise
        // Only learn the floor away from notes (before the first onset or >1.2 s after the last one), so a
        // decaying string is never mistaken for noise.
        const bool awayFromNotes = onsetCount == 0 || end - lastOnset > (int64_t) (1.2 * sr);
        if (awayFromNotes) {
            if (noiseFloorDb < -99.0f) noiseFloorDb = db;
            else if (db < noiseFloorDb) noiseFloorDb += (db - noiseFloorDb) * 0.2f;
            else noiseFloorDb += (db - noiseFloorDb) * 0.01f;
        }
        quietFrame = awayFromNotes && db < noiseFloorDb + 3.0f;

        // peak picking on previous frame with adaptive median threshold
        odfHist.push_back (flux); if (odfHist.size() > 40) odfHist.pop_front();
        std::vector<float> sorted (odfHist.begin(), odfHist.end());
        std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
        const float med = sorted[sorted.size() / 2];
        const float thresh = med * 1.5f + 4.0f;
        const int64_t t = end - hopO;   // time of odf1
        if (odf1 > thresh && odf1 >= odf2 && odf1 >= flux && db > gate() && t - lastOnset > (int64_t) (0.07 * sr)) {
            lastOnset = t; ++onsetCount; recentOnsets.push_back (t);
            if (countInOpen) {
                if (beat.onOnset (t)) { evidence.fill (0); candidate = -1; candFrames = 0; countInLockedAt = t; }   // count-in heard: forget it
                else if (beat.locked && t > countInLockedAt + (int64_t) (0.75 * 60.0 * sr / beat.bpm)) { countInOpen = false; evidence.fill (0); }   // first strum on/after the downbeat that follows the count-in   // first strum after the count-in: fresh evidence
                else if (! beat.locked && onsetCount >= 4) countInOpen = false;      // no count-in: start following now
            } else beat.onOnset (t);
            if (notes.size() >= 3) for (auto& e : evidence) e *= params.onsetReset;   // single notes / double-stops: keep integrating (riffs)
            candFrames = 0;
        }
        while (! recentOnsets.empty() && recentOnsets.front() < end - (int64_t) (2.0 * sr)) recentOnsets.erase (recentOnsets.begin());
        odf2 = odf1; odf1 = flux;

        // intensity = onset density (0..1 over 0.5..6 onsets/s) and loudness (-45..-12 dBFS)
        onsetRate = (float) recentOnsets.size() / 2.0f;
        const float dens = juce::jlimit (0.0f, 1.0f, (onsetRate - 0.5f) / 5.5f);
        const float loud = juce::jlimit (0.0f, 1.0f, (rmsDb + 45.0f) / 33.0f);
        const float target = 0.5f * dens + 0.5f * loud;
        const float a = target > intensity ? 0.02f : 0.004f;      // ~0.25 s up, ~1.3 s down at 5.3 ms hops
        intensity += (target - intensity) * a;
    }

    // ------------------------------------------------------------------ chroma / chord frame
    bool chromaFrame() {
        if (rmsDb < gate()) { if (quietFrame) learnNoise(); return false; }   // hold chord during silence
        const int64_t end = totalSamples;
        int64_t start = juce::jmax (end - N, lastOnset + (int64_t) (0.012 * sr));   // onset-aligned window
        const int len = (int) (end - start);
        if (len < profile.minWindow) return false;                                     // wait for enough post-onset audio
        std::fill (work.begin(), work.end(), 0.0f);
        for (int i = 0; i < len; ++i) {
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (i + 0.5f) / len);
            work[(size_t) i] = sampleAt (start + i) * w;
        }
        fft->performFrequencyOnlyForwardTransform (work.data(), true);
        std::vector<float> X (work.begin(), work.begin() + N / 2);
        if (profile.lowRegister && end - lastOnset < (int64_t) (0.5 * sr)) {     // strangle auto-detect on note frames
            double lo = 1e-12, mid = 1e-12; const int k100 = (int) (100 / binHz), k400 = (int) (400 / binHz), k30 = (int) (30 / binHz);
            for (int k = k30; k < k400 && k < (int) X.size(); ++k) (k < k100 ? lo : mid) += (double) X[(size_t) k] * X[(size_t) k];
            lowRatioDb += ((float) (10.0 * std::log10 (lo / mid)) - lowRatioDb) * 0.05f;
            if (lowRatioDb < -7.0f) strangleDetected = true; else if (lowRatioDb > -3.0f) strangleDetected = false;
        }
        for (auto& v : X) v = std::sqrt (v);                              // compress (magnitude^0.5)
        // subtract learnt stationary noise (e.g. cable hum), scaled for this window length
        if (noiseFrames > 0) { const float sc = std::sqrt ((float) len / N) * 1.5f;
            for (size_t k = 0; k < X.size(); ++k) X[k] = juce::jmax (0.0f, X[k] - sc * noiseSpec[k]); }

        multiPitch (X, len);
        std::array<float, 12> c {}; float tot = 0;
        int lowest = 1000; float maxSal = 0;
        for (auto& nt : notes) maxSal = juce::jmax (maxSal, nt.salience);
        for (auto& nt : notes) {
            c[(size_t) (nt.midi % 12)] += nt.salience; tot += nt.salience;
            if (nt.salience > 0.3f * maxSal) lowest = juce::jmin (lowest, nt.midi);
        }
        if (tot <= 0) return false;
        float nrm = 0; for (float v : c) nrm += v * v; nrm = std::sqrt (nrm);
        for (int i = 0; i < 12; ++i) chroma[(size_t) i] = c[(size_t) i] / nrm;
        bassMidi = lowest < 1000 ? lowest : -1;
        const int bassPc = bassMidi >= 0 ? bassMidi % 12 : -1;

        // long-term chroma for key
        const float kd = std::exp (-(float) hopC / (float) (params.keyTau * sr));
        for (int i = 0; i < 12; ++i) longChroma[(size_t) i] = longChroma[(size_t) i] * kd + chroma[(size_t) i];
        updateKey (end);

        // template scores
        std::array<float, 12 * NumChordTypes> score {};
        int best = 0;
        for (int r = 0; r < 12; ++r)
            for (int t = 0; t < NumChordTypes; ++t) {
                float dot = 0, tn = 0;
                const auto& iv = chordIntervals (t);
                for (size_t j = 0; j < iv.size(); ++j) {
                    const float w = iv[j] == 7 ? 0.85f : (iv[j] == 10 ? 0.8f : 1.0f);
                    dot += w * chroma[(size_t) ((r + iv[j]) % 12)]; tn += w * w;
                }
                float s = dot / std::sqrt (tn);
                if (profile.lowRegister) {
                    // bass register: the lowest note is the strongest chord evidence; the slow key breaks ties
                    if (bassPc >= 0) s += bassPc == r ? 0.08f : (chordContains (r, t, bassPc) ? 0.0f : -0.12f);
                    if (key.tonic >= 0) { const int d = ((r - key.tonic) % 12 + 12) % 12;
                        static const bool majD[12] = { 1,0,1,0,1,1,0,1,0,1,0,1 }, minD[12] = { 1,0,1,1,0,1,0,1,1,0,1,0 };
                        s += (key.minor ? minD[d] : majD[d]) ? 0.015f : -0.015f; }
                } else if (bassPc >= 0) s += bassPc == r ? 0.06f : (chordContains (r, t, bassPc) ? 0.0f : -0.10f);
                static const float prior[] = { 0.0f, 0.0f, -0.02f, -0.02f, -0.03f, -0.03f, 0.0f };
                s += prior[t];
                score[(size_t) (r * NumChordTypes + t)] = s;
                if (s > score[(size_t) best]) best = r * NumChordTypes + t;
            }
        frameChord = { best / NumChordTypes, best % NumChordTypes, bassPc, score[(size_t) best] };

        // evidence integration: softmax posterior weighted by window length (longer window = better resolution)
        const float decay = std::exp (-(float) hopC / (float) (params.evidenceTau * sr));
        const float wlen = (float) len / N;
        float mx = score[(size_t) best], z = 0;
        std::array<float, 12 * NumChordTypes> p {};
        for (size_t i = 0; i < p.size(); ++i) { p[i] = std::exp ((score[i] - mx) / 0.04f); z += p[i]; }
        for (size_t i = 0; i < p.size(); ++i) evidence[i] = evidence[i] * decay + wlen * p[i] / z;

        // hold: no commits during the count-in, below the gate, or long after the last strum (decay / gap)
        if (countInActive() || frameDb < gate() + (profile.lowRegister ? 0.0f : 4.0f) || end - lastOnset > (int64_t) (params.holdAfterOnset * sr)) { candidate = -1; candFrames = 0; return false; }
        int top = 0; for (int i = 1; i < (int) evidence.size(); ++i) if (evidence[(size_t) i] > evidence[(size_t) top]) top = i;
        const int cur = chord.valid() ? chord.root * NumChordTypes + chord.type : -1;
        if (top == cur) { candidate = -1; candFrames = 0; chord.bass = bassPc >= 0 && chordContains (chord.root, chord.type, bassPc) ? bassPc : chord.root; return false; }
        if (top != candidate) { candidate = top; candFrames = 0; }
        ++candFrames;
        const float curE = cur >= 0 ? evidence[(size_t) cur] : 0.0f;
        // passing tones: in the bass register a new chord must be held (confirmSeconds) unless its strum fell on a beat
        int need = params.confirmFrames;
        if (params.confirmSeconds > 0) { need = juce::jmax (need, (int) (params.confirmSeconds * sr / hopC));
            if (beat.locked) { const double per = 60.0 * sr / beat.bpm; const double ph = std::fmod ((double) (lastOnset - beat.beatPhase), per);
                if (juce::jmin (ph, per - ph) < 0.1 * per) need = params.confirmFrames; } }
        // cap on 'self-transition': a candidate that has led for a full beat commits even inside the hysteresis band
        const double beatSamples = beat.locked ? 60.0 * sr / beat.bpm : 0.6 * sr;
        const bool overdue = ! profile.lowRegister && (double) candFrames * hopC >= beatSamples;   // bass register: passing tones must not force a change
        if (candFrames >= need && (overdue || evidence[(size_t) top] > curE * (1.0f + params.hysteresis))) {
            chord = { top / NumChordTypes, top % NumChordTypes, -1, evidence[(size_t) top] };
            chord.bass = bassPc >= 0 && chordContains (chord.root, chord.type, bassPc) ? bassPc : chord.root;
            chordChangedAt = end; candidate = -1; candFrames = 0;
            return true;
        }
        return false;
    }

    /** Iterative harmonic-summation multi-pitch estimate with spectral-smoothness subtraction (Klapuri-style). */
    void multiPitch (std::vector<float>& X, int windowLen) {
        notes.clear();
        const int lo = profile.loMidi, hi = profile.hiMidi;
        float first = 0;
        const float minF = (float) sr / windowLen * 1.2f; // fundamental must be resolvable by this window
        const bool low = profile.lowRegister;
        const int nH = low ? 16 : 10; const float wDecay = low ? 0.88f : 0.82f;
        const int maxIter = low ? 5 : 6; const float stopRel = low ? 0.28f : 0.18f;
        for (int iter = 0; iter < maxIter; ++iter) {
            int bestP = -1; float bestS = 0;
            for (int p = lo; p <= hi; ++p) {
                const float f0 = 440.0f * std::pow (2.0f, (p - 69) / 12.0f);
                if (f0 < minF * 0.5f) continue;
                float s = 0, w = 1;
                for (int h = 1; h <= nH; ++h, w *= wDecay) {
                    const float f = f0 * h; if (f > 5000.0f) break;
                    s += w * peakNear (X, f);
                }
                // a real fundamental needs energy at f0 or 2*f0 (rejects sub-harmonic ghosts)
                const float base = strangleActive() ? juce::jmax (peakNear (X, f0), peakNear (X, 2 * f0), peakNear (X, 3 * f0))
                                                    : juce::jmax (peakNear (X, f0), peakNear (X, 2 * f0));
                if (base < (strangleActive() ? 0.08f : 0.15f) * s) continue;
                if (profile.lowRegister) {
                    // subharmonic-ghost / octave guard: a true f0 has odd partials (1,3,5); the octave-below ghost
                    // only "sees" the even ones. Works when the fundamental itself is filtered away.
                    const float odd = juce::jmax (peakNear (X, f0), peakNear (X, 3 * f0), peakNear (X, 5 * f0));
                    if (odd < 0.12f * s) continue;
                }
                if (s > bestS) { bestS = s; bestP = p; }
            }
            if (bestP < 0) break;
            if (low) {
                // octave/fifth-error guard: is bestP really the 2nd (or 3rd) partial of a lower string? Then the lower
                // note's odd partials (which bestP cannot explain) are present. Works with or without a fundamental.
                const float fb = 440.0f * std::pow (2.0f, (bestP - 69) / 12.0f), ref = peakNear (X, fb) + 1e-9f;
                for (int dn : { 12, 19 }) {
                    const int q = bestP - dn; if (q < lo) continue;
                    const float fq = fb / (dn == 12 ? 2.0f : 3.0f);
                    const float odd = dn == 12 ? 0.5f * (peakNear (X, 3 * fq) + peakNear (X, 5 * fq))
                                               : 0.5f * (peakNear (X, 2 * fq) + peakNear (X, 4 * fq));
                    if (odd > 0.3f * ref) { bestP = q; break; }
                }
            }
            if (iter == 0) first = bestS; else if (bestS < stopRel * first) break;
            bool dup = false; for (auto& nt : notes) if (nt.midi == bestP) dup = true;
            if (! dup) notes.push_back ({ bestP, bestS });
            // subtract this note's partials (smoothness-limited so shared partials survive)
            const float f0 = 440.0f * std::pow (2.0f, (bestP - 69) / 12.0f);
            float A[22] {};
            for (int h = 1; h <= nH + 1; ++h) A[h] = peakNear (X, f0 * h);
            for (int h = 1; h <= nH; ++h) {
                const float est = h == 1 ? A[1] : juce::jmin (A[h], 0.5f * (A[h - 1] + A[h + 1]));
                const float f = f0 * h; if (f > 5000.0f) break;
                const float half = juce::jmax (1.5f, f * 0.03f / binHz);
                const int k0 = juce::jmax (1, (int) std::floor (f / binHz - half)), k1 = juce::jmin ((int) X.size() - 1, (int) std::ceil (f / binHz + half));
                for (int k = k0; k <= k1; ++k) X[(size_t) k] = juce::jmax (0.0f, X[(size_t) k] - est);
            }
        }
    }
    float peakNear (const std::vector<float>& X, float f) const {
        const float half = juce::jmax (1.0f, f * 0.025f / binHz);   // ±~0.4 semitone
        const int k0 = juce::jmax (1, (int) std::floor (f / binHz - half)), k1 = juce::jmin ((int) X.size() - 1, (int) std::ceil (f / binHz + half));
        float m = 0; for (int k = k0; k <= k1; ++k) m = juce::jmax (m, X[(size_t) k]);
        return m;
    }

    bool quietFrame = false; int noiseFrames = 0; std::vector<float> noiseSpec;
    void learnNoise() {
        const int64_t end = totalSamples;
        std::fill (work.begin(), work.end(), 0.0f);
        for (int i = 0; i < N; ++i) work[(size_t) i] = sampleAt (end - N + i) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (i + 0.5f) / N));
        fft->performFrequencyOnlyForwardTransform (work.data(), true);
        if (noiseSpec.empty()) noiseSpec.assign (N / 2, 0.0f);
        const float a = noiseFrames < 10 ? 1.0f / (float) (noiseFrames + 1) : 0.05f;
        for (int k = 0; k < N / 2; ++k) noiseSpec[(size_t) k] += (std::sqrt (work[(size_t) k]) - noiseSpec[(size_t) k]) * a;
        ++noiseFrames;
    }

    void updateKey (int64_t now) {
        static const float majP[] = { 6.35f,2.23f,3.48f,2.33f,4.38f,4.09f,2.52f,5.19f,2.39f,3.66f,2.29f,2.88f };
        static const float minP[] = { 6.33f,2.68f,3.52f,5.38f,2.60f,3.53f,2.54f,4.75f,3.98f,2.69f,3.34f,3.17f };
        float sum = 0; for (float v : longChroma) sum += v;
        if (sum < 20.0f) return;                                  // ~1 s of playing before any key guess
        auto corr = [&] (const float* prof, int tonic) {
            float mx = 0, mp = 0; for (int i = 0; i < 12; ++i) { mx += longChroma[(size_t) i]; mp += prof[i]; } mx /= 12; mp /= 12;
            float a = 0, b = 0, c = 0;
            for (int i = 0; i < 12; ++i) { float x = longChroma[(size_t) ((i + tonic) % 12)] - mx, y = prof[i] - mp; a += x * y; b += x * x; c += y * y; }
            return a / std::sqrt (b * c + 1e-9f);
        };
        int best = 0; float bc = -2;
        for (int k = 0; k < 24; ++k) { float c = corr (k < 12 ? majP : minP, k % 12); if (c > bc) { bc = c; best = k; } }
        const int cur = key.tonic < 0 ? -1 : key.tonic + (key.minor ? 12 : 0);
        if (cur < 0) { key = { best % 12, best >= 12, bc }; return; }
        if (best == cur) { key.correlation = bc; keyCand = -1; return; }
        if (best != keyCand) { keyCand = best; keyCandSince = now; }
        const float curC = corr (key.minor ? minP : majP, key.tonic);
        if (now - keyCandSince > (int64_t) (params.keyHoldSeconds * sr) && bc > curC + 0.05f) { key = { best % 12, best >= 12, bc }; keyCand = -1; }
        else key.correlation = curC;
    }
};

} // namespace sj
