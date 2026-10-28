// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <JuceHeader.h>

namespace sj {
/** Minimal built-in drum synth: kick (pitch-swept sine), snare (noise+tone), closed hat (HP noise). */
class DrumSynth {
public:
    enum Voice { Kick, Snare, Hat, NumVoices };
    void prepare (double sr) { sampleRate = sr; }
    void trigger (Voice v, float vel) { auto& s = st[v]; s.env = vel; s.phase = 0; s.t = 0; }
    void render (float* out, int n) {
        for (int i = 0; i < n; ++i) {
            float y = 0;
            auto& k = st[Kick];  if (k.env > 1e-4f) { float f = 45.0f + 120.0f * std::exp (-k.t * 30.0f); k.phase += f / (float) sampleRate; y += k.env * std::sin (juce::MathConstants<float>::twoPi * k.phase); k.env *= dec (0.25f); k.t += 1.0f / (float) sampleRate; }
            auto& s = st[Snare]; if (s.env > 1e-4f) { s.phase += 190.0f / (float) sampleRate; y += s.env * (0.6f * noise() + 0.4f * std::sin (juce::MathConstants<float>::twoPi * s.phase)); s.env *= dec (0.12f); }
            auto& h = st[Hat];   if (h.env > 1e-4f) { float nz = noise(); float hp = nz - hatPrev; hatPrev = nz; y += 0.35f * h.env * hp; h.env *= dec (0.04f); }
            out[i] += 0.7f * y;
        }
    }
private:
    struct S { float env = 0, phase = 0, t = 0; } st[NumVoices];
    float dec (float seconds) const { return std::exp (-1.0f / (seconds * (float) sampleRate * 0.25f)); }
    float noise() { return rng.nextFloat() * 2.0f - 1.0f; }
    juce::Random rng; float hatPrev = 0; double sampleRate = 48000;
};

/** Mono bass: band-limited-ish saw + one-pole lowpass + AR envelope with glide. */
class BassSynth {
public:
    void prepare (double sr) { sampleRate = sr; }
    void noteOn (int midi, float vel) { target = 440.0f * std::pow (2.0f, (midi - 69) / 12.0f); if (freq <= 0) freq = target; amp = vel; gate = true; }
    void noteOff() { gate = false; }
    void render (float* out, int n) {
        for (int i = 0; i < n; ++i) {
            freq += (target - freq) * 0.002f;
            phase += freq / (float) sampleRate; if (phase >= 1) phase -= 1;
            float saw = 2 * phase - 1;
            env += ((gate ? amp : 0.0f) - env) * (gate ? 0.01f : 0.0008f);
            lp += 0.08f * (saw - lp);
            out[i] += 0.5f * env * lp;
        }
    }
private:
    double sampleRate = 48000; float freq = 0, target = 0, phase = 0, env = 0, amp = 0, lp = 0; bool gate = false;
};
} // namespace sj
