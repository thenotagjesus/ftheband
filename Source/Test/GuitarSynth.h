// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Session Jam contributors
#pragma once
#include <JuceHeader.h>
#include <random>
#include "Music/Theory.h"

namespace sj {
/** Test guitar: Karplus-Strong strings (fractional delay, loss filter, pick-position comb, body resonances),
    chord voicing search on any tuning, strums with stagger/direction/velocity, palm-mutes and noise. */
class GuitarSynth {
public:
    struct Tuning { const char* name; std::array<int, 6> open; };   // low E .. high e (MIDI)
    static std::vector<Tuning> tunings() {
        return { { "standard", { 40,45,50,55,59,64 } }, { "eb-standard", { 39,44,49,54,58,63 } },
                 { "drop-d", { 38,45,50,55,59,64 } }, { "d-standard", { 38,43,48,53,57,62 } },
                 { "bassvi", { 28,33,38,43,47,52 } }, { "bassvi-strangle", { 28,33,38,43,47,52 } } };
    }
    explicit GuitarSynth (double sampleRate, uint32_t seed = 1) : sr (sampleRate), rng (seed) {}

    /** Find a playable voicing (MIDI notes, low to high) of root/type on tuning. Power chords use the two or three
        lowest strings. `variant` picks among valid positions; root is always the lowest note unless invert. */
    std::vector<int> voicing (const Tuning& tu, int root, int type, int variant, bool invert = false) {
        std::vector<std::vector<int>> found;
        if (type == Power) {
            for (int s = 0; s < 2; ++s)
                for (int f = 0; f <= 9; ++f)
                    if ((tu.open[(size_t) s] + f) % 12 == root) {
                        std::vector<int> v { tu.open[(size_t) s] + f, tu.open[(size_t) s] + f + 7 };
                        if (variant % 2 == 0) v.push_back (v[0] + 12);
                        found.push_back (v);
                    }
        } else {
            for (int pos = 0; pos <= 8; ++pos) {
                for (int lowStr = 0; lowStr <= 2; ++lowStr) {
                    std::vector<int> v; std::array<bool, 12> have {};
                    bool ok = true;
                    for (int s = lowStr; s < 6; ++s) {
                        int pick = -1;
                        for (int f = (pos == 0 ? 0 : pos); f <= pos + 3; ++f) {
                            const int m = tu.open[(size_t) s] + f;
                            if (chordContains (root, type, m % 12)) { pick = m; break; }
                        }
                        if (pick < 0) { if (s == lowStr) { ok = false; break; } continue; }
                        v.push_back (pick); have[(size_t) (pick % 12)] = true;
                    }
                    if (! ok || v.size() < 4) continue;
                    if (! invert && v[0] % 12 != root) continue;
                    if (invert && v[0] % 12 == root) continue;
                    bool all = true; for (int iv : chordIntervals (type)) all &= have[(size_t) ((root + iv) % 12)];
                    if (all) found.push_back (v);
                }
            }
        }
        if (found.empty()) return {};
        return found[(size_t) variant % found.size()];
    }

    /** Add a strum to `out` starting at sample `at`. dir: +1 down (low->high), -1 up. vel 0..1. mute: palm mute. */
    void strum (std::vector<float>& out, int64_t at, const std::vector<int>& notes, int dir, float vel, bool mute, double ringSeconds) {
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        const double spread = (0.006 + 0.02 * u (rng)) * (mute ? 0.5 : 1.0);
        for (size_t i = 0; i < notes.size(); ++i) {
            const size_t idx = dir > 0 ? i : notes.size() - 1 - i;
            if (dir < 0 && idx < notes.size() / 2 && u (rng) < 0.5f) continue;     // up-strums often skip bass strings
            const int64_t t = at + (int64_t) (sr * spread * (double) i / juce::jmax<size_t> (1, notes.size() - 1));
            pluck (out, t, notes[idx], vel * (0.75f + 0.25f * u (rng)), mute, ringSeconds);
        }
    }

    void pluck (std::vector<float>& out, int64_t at, int midi, float vel, bool mute, double ringSeconds) {
        std::uniform_real_distribution<float> u (-1.0f, 1.0f);
        const double f0 = 440.0 * std::pow (2.0, (midi - 69) / 12.0) * (1.0 + 0.0015 * u (rng));   // slight detune
        const double d = sr / f0 - 0.5;                     // loop delay (one-pole loss adds ~0.5)
        const int L = (int) d; const float frac = (float) (d - L);
        std::vector<float> line ((size_t) L + 2);
        // excitation: filtered noise burst, brighter for harder picks, with pick-position comb
        const float bright = 0.35f + 0.5f * vel;
        float lp = 0;
        for (auto& s : line) { lp += bright * (u (rng) - lp); s = lp; }
        const int pp = juce::jmax (1, (int) (L * (0.12f + 0.06f * (u (rng) + 1))));
        for (int i = (int) line.size() - 1; i >= pp; --i) line[(size_t) i] -= line[(size_t) (i - pp)];
        const float loss = mute ? 0.90f : 0.9985f - 0.0000015f * (float) f0;
        const float damp = mute ? 0.6f : 0.35f;             // one-pole loss filter coefficient
        const int64_t len = (int64_t) (sr * (mute ? 0.15 : ringSeconds));
        const size_t M = 8192; std::vector<float> buf (M, 0.0f);
        for (size_t i = 0; i < line.size(); ++i) buf[i] = line[i];
        size_t w = line.size() - 1;                         // excitation occupies the most recent L samples
        const double delay = d;                             // total loop delay incl. filter (~0.5)
        float prev = 0;
        for (int64_t n = 0; n < len && at + n < (int64_t) out.size(); ++n) {
            const double rp = (double) w - delay + (double) M;
            const size_t i0 = (size_t) rp % M, i1 = (i0 + 1) % M; const float fr = (float) (rp - std::floor (rp));
            const float x = buf[i0] * (1 - fr) + buf[i1] * fr;
            const float y = loss * ((1 - damp) * x + damp * prev); prev = x;
            w = (w + 1) % M; buf[w] = y;
            float env = n < 24 ? (float) n / 24.0f : 1.0f;
            if (n > len - 480) env *= (float) (len - n) / 480.0f;
            if (at + n >= 0) out[(size_t) (at + n)] += 0.22f * vel * env * y;
        }
    }

    /** Simple body: two resonant peaks + gentle high shelf cut, applied in place. */
    void body (std::vector<float>& x) {
        juce::dsp::IIR::Filter<float> a, b, c;
        a.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 110.0f, 1.2f, 1.6f);
        b.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 220.0f, 1.5f, 1.4f);
        c.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 4000.0f, 0.7f, 0.5f);
        for (auto& s : x) s = c.processSample (b.processSample (a.processSample (s)));
    }

    void addNoise (std::vector<float>& x, float snrDb, bool hum) {
        double p = 0; for (float s : x) p += (double) s * s; p /= juce::jmax<size_t> (1, x.size());
        const float nAmp = (float) std::sqrt (p / std::pow (10.0, snrDb / 10.0));
        std::normal_distribution<float> g (0.0f, 1.0f);
        float pink = 0;
        for (size_t i = 0; i < x.size(); ++i) {
            pink = 0.97f * pink + 0.03f * g (rng) * 5.0f;
            float h = hum ? 0.5f * std::sin ((float) (juce::MathConstants<double>::twoPi * 60.0 * (double) i / sr))
                          + 0.25f * std::sin ((float) (juce::MathConstants<double>::twoPi * 180.0 * (double) i / sr)) : 0.0f;
            x[i] += nAmp * (0.7f * pink + 0.3f * g (rng) + h);
        }
    }
    std::mt19937& random() { return rng; }
private:
    double sr; std::mt19937 rng;
};
} // namespace sj
