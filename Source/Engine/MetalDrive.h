// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>

namespace sj {

/** Allocation-free RBJ biquad (Direct Form II transposed). Coefficients are recomputed on the audio thread,
    so this deliberately avoids juce::dsp::IIR::Coefficients (reference-counted, allocates). */
struct DriveBiquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    float process (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2) {
        b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0); a1 = (float) (na1 / na0); a2 = (float) (na2 / na0); }
    void highpass (double sr, double f, double q) { const double w = juce::MathConstants<double>::twoPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al); }
    void lowpass (double sr, double f, double q) { const double w = juce::MathConstants<double>::twoPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al); }
    void peak (double sr, double f, double q, double gainDb) { const double A = std::pow (10.0, gainDb / 40), w = juce::MathConstants<double>::twoPi * f / sr,
        c = std::cos (w), al = std::sin (w) / (2 * q); set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A); }
};

/** Tight metal drive for the guitar input (ahead of the VST3 inserts). OFF by default; Inserts page toggle + knobs.
      input HPF (tightens the low end before clipping) -> mid push (TS-style, ~750 Hz) -> pre gain
      -> 8x oversampled asymmetric clipper (two stages, juce::dsp::Oversampling, polyphase IIR)
      -> DC blocker -> post EQ (low-mid scoop, tone-controlled presence) -> cab-ish low-pass (2 x 12 dB/oct) -> level.
    Controls are 0..1 atomics written by the message thread; process() never allocates or locks. */
class MetalDrive {
public:
    std::atomic<float> gain { 0.6f }, tone { 0.5f }, level { 0.5f };

    void prepare (double sampleRate, int maxBlock) {
        sr = sampleRate;
        os = std::make_unique<juce::dsp::Oversampling<float>> (1, OversampleStages, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        os->initProcessing ((size_t) maxBlock);
        lastGain = lastTone = -1; updateCoefficients();
        reset();
    }
    void reset() {
        for (auto* f : { &hpf1, &hpf2, &push, &dc, &scoop, &presence, &cab1, &cab2 }) f->reset();
        if (os) os->reset();
        lvlSmoothed = targetLevel();
    }
    /** Oversampler group delay at the base rate (fractional, a few samples; reported, not compensated). */
    float latencySamples() const { return os ? os->getLatencyInSamples() : 0.0f; }

    /** In-place on one mono block (n <= maxBlock given to prepare). */
    void process (float* x, int n) {
        if (! os || n <= 0) return;
        updateCoefficients();
        const float g = preGain;
        for (int i = 0; i < n; ++i) x[i] = push.process (hpf2.process (hpf1.process (x[i]))) * g;

        float* chans[] = { x };
        juce::dsp::AudioBlock<float> block (chans, 1, (size_t) n);
        auto up = os->processSamplesUp (block);
        float* u = up.getChannelPointer (0);
        for (size_t i = 0; i < up.getNumSamples(); ++i) u[i] = clip2 (clip1 (u[i]));
        os->processSamplesDown (block);

        const float target = targetLevel();
        for (int i = 0; i < n; ++i) {
            lvlSmoothed += (target - lvlSmoothed) * 0.002f;
            float y = dc.process (x[i]);
            y = cab2.process (cab1.process (presence.process (scoop.process (y))));
            x[i] = y * lvlSmoothed;
        }
    }

    // exposed for the offline check
    static float clip1 (float v) { constexpr float b = 0.3f; return std::tanh (v + b) - std::tanh (b); }       // asymmetric: positive side clips harder than the negative
    static float clip2 (float v) { v *= 2.2f; return v >= 0 ? v / (1.0f + v) : std::tanh (v * 0.8f) * 1.05f; }   // second, differently-shaped stage (even + odd harmonics)

private:
    static constexpr int OversampleStages = 3;   // 2^3 = 8x (4x measured -32 dB inharmonic at 3.1 kHz full gain)
    double sr = 48000;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    DriveBiquad hpf1, hpf2, push, dc, scoop, presence, cab1, cab2;
    float lastGain = -1, lastTone = -1, preGain = 1, makeup = 1, lvlSmoothed = 1;

    float targetLevel() const { return makeup * juce::Decibels::decibelsToGain (-24.0f + 30.0f * juce::jlimit (0.0f, 1.0f, level.load (std::memory_order_relaxed))); }   // -24..+6 dB
    void updateCoefficients() {
        const float gv = juce::jlimit (0.0f, 1.0f, gain.load (std::memory_order_relaxed)), tv = juce::jlimit (0.0f, 1.0f, tone.load (std::memory_order_relaxed));
        if (gv == lastGain && tv == lastTone) return;
        if (gv != lastGain) {
            preGain = juce::Decibels::decibelsToGain (6.0f + 42.0f * gv);          // +6..+48 dB into the clipper
            makeup = juce::Decibels::decibelsToGain (-6.0f - 6.0f * gv);           // rough loudness match across the gain range
            hpf1.highpass (sr, 90.0 + 60.0 * gv, 0.707);                           // more gain = tighter low end
            hpf2.highpass (sr, 40.0, 0.707);
            push.peak (sr, 750.0, 0.8, 3.0 + 3.0 * gv);
            dc.highpass (sr, 25.0, 0.707);
        }
        scoop.peak (sr, 420.0, 0.9, -5.0);
        presence.peak (sr, 2600.0, 0.7, -6.0 + 12.0 * tv);                         // tone: -6..+6 dB presence
        cab1.lowpass (sr, 3800.0 + 3200.0 * tv, 0.707);                            // tone also opens the cab LPF: 3.8..7 kHz
        cab2.lowpass (sr, 3800.0 + 3200.0 * tv, 0.9);
        lastGain = gv; lastTone = tv;
    }
};
} // namespace sj
