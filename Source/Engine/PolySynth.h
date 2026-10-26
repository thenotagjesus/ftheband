// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Session Jam contributors
#pragma once
#include "Engine/Sampler.h"

namespace sj {
/** Small RT-safe polyphonic synth for chairs without a sample set:
    Organ (drawbar additive 16'/8'/5 1/3'/4' with key click + slow leslie-ish AM),
    Pad (3 detuned saws -> one-pole LP, slow attack/release),
    Pluck (Karplus-Strong rhythm guitar with loss filter; fixed buffers, no allocation). */
class PolySynth : public Instrument {
public:
    enum Mode { Organ, Pad, Pluck };
    explicit PolySynth (Mode m) : mode (m) {}
    void prepare (double s) override { sr = s; for (auto& v : voices) v.active = false; }
    void noteOn (int note, int vel, int) override {
        Voice* v = nullptr;
        for (auto& c : voices) if (! c.active) { v = &c; break; }
        if (! v) { v = &voices[0]; for (auto& c : voices) if (c.age > v->age) v = &c; }
        *v = Voice(); v->active = true; v->note = note; v->vel = (float) vel / 127.0f;
        v->f = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        for (int i = 0; i < 3; ++i) v->ph[i] = 0.31 * i;
        if (mode == Pluck) {
            v->L = juce::jlimit (8, (int) ksMax - 2, (int) (sr / v->f));
            uint32_t seed = (uint32_t) note * 2654435761u + (uint32_t) counter++ * 40503u; float lp = 0;
            for (int i = 0; i < v->L; ++i) { seed = seed * 1664525u + 1013904223u; float r = (float) (seed >> 9) / 4194304.0f - 1.0f; lp += (0.4f + 0.4f * v->vel) * (r - lp); v->ks[(size_t) i] = lp; }
        }
    }
    void noteOff (int note) override { for (auto& v : voices) if (v.active && v.note == note) v.rel = true; }
    void allNotesOff (float) override { for (auto& v : voices) v.rel = true; }
    void render (float* out, int n) override {
        const double tw = juce::MathConstants<double>::twoPi;
        const float atk = mode == Pad ? (float) (1.0 / (0.6 * sr)) : (float) (1.0 / (0.004 * sr));
        const float rel = mode == Pad ? (float) (1.0 / (0.9 * sr)) : mode == Organ ? (float) (1.0 / (0.03 * sr)) : (float) (1.0 / (0.08 * sr));
        for (auto& v : voices) {
            if (! v.active) continue;
            for (int i = 0; i < n; ++i) {
                if (v.rel) { v.env -= rel; if (v.env <= 0) { v.active = false; break; } }
                else if (v.env < 1) v.env = juce::jmin (1.0f, v.env + atk);
                float s = 0;
                if (mode == Organ) {
                    const double p = v.ph[0]; v.ph[0] += v.f / sr; if (v.ph[0] > 1) v.ph[0] -= 1;
                    s = (float) (0.5 * std::sin (tw * p * 0.5) + 0.8 * std::sin (tw * p) + 0.4 * std::sin (tw * p * 1.5) + 0.35 * std::sin (tw * p * 2) + 0.12 * std::sin (tw * p * 4));
                    lfo += 6.0 / sr / (double) voices.size(); s *= 0.85f + 0.15f * (float) std::sin (tw * lfo);
                    if (v.age < 96) s += 0.3f * (float) ((v.age * 7919) % 97 - 48) / 48.0f * (1.0f - (float) v.age / 96.0f);   // key click
                    s *= 0.22f;
                } else if (mode == Pad) {
                    for (int k = 0; k < 3; ++k) { const double det = 1.0 + (k - 1) * 0.004; v.ph[k] += v.f * det / sr; if (v.ph[k] > 1) v.ph[k] -= 1; s += (float) (2 * v.ph[k] - 1); }
                    v.lp += 0.06f * (s - v.lp); s = v.lp * 0.18f;
                } else {
                    const int r = v.idx, r1 = (v.idx + 1) % v.L;
                    const float y = 0.996f * 0.5f * (v.ks[(size_t) r] + v.ks[(size_t) r1]);
                    s = v.ks[(size_t) r]; v.ks[(size_t) r] = y; v.idx = r1; s *= 0.5f;
                }
                out[i] += s * v.env * (0.35f + 0.65f * v.vel);
                ++v.age;
            }
        }
    }
private:
    static constexpr size_t ksMax = 2048;
    struct Voice { bool active = false, rel = false; int note = 0, L = 0, idx = 0; float vel = 0, env = 0, lp = 0; double f = 0; double ph[3] {}; int64_t age = 0; std::array<float, ksMax> ks {}; };
    Mode mode; double sr = 48000, lfo = 0; uint32_t counter = 0;
    std::array<Voice, 16> voices;
};
} // namespace sj
