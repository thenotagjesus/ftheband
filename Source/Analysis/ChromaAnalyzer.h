// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Session Jam contributors
#pragma once
#include <JuceHeader.h>
#include "Music/Theory.h"

namespace sj {
/** FFT -> 12-bin chroma -> template chord match (maj/min triads) + Krumhansl-Schmuckler key
    detection over an exponentially-decayed long-term chroma. Runs on the analysis thread. */
class ChromaAnalyzer {
public:
    static constexpr int order = 13, N = 1 << order; // 8192 @48k -> 5.9 Hz bins
    ChromaAnalyzer() : fft (order), window ((size_t) N, juce::dsp::WindowingFunction<float>::hann) {}
    void prepare (double sr) { sampleRate = sr; longChroma.fill (0); }

    void process (const float* x) {   // x has N samples
        std::copy (x, x + N, buf.begin()); std::fill (buf.begin() + N, buf.end(), 0.0f);
        window.multiplyWithWindowingTable (buf.data(), (size_t) N);
        fft.performFrequencyOnlyForwardTransform (buf.data());
        std::array<float,12> c {};
        for (int k = 1; k < N / 2; ++k) {
            double f = k * sampleRate / N; if (f < 60 || f > 2000) continue;
            int pc = ((int) std::lround (12.0 * std::log2 (f / 440.0)) + 69) % 12;
            c[(size_t) pc] += buf[(size_t) k] * buf[(size_t) k];
        }
        float mx = *std::max_element (c.begin(), c.end());
        if (mx < 1e-6f) { chroma.fill (0); return; }
        for (int i = 0; i < 12; ++i) { chroma[(size_t) i] = c[(size_t) i] / mx; longChroma[(size_t) i] = 0.97f * longChroma[(size_t) i] + 0.03f * chroma[(size_t) i]; }
        chord = matchChord (chroma); key = krumhansl (longChroma);
    }
    static ChordEstimate matchChord (const std::array<float,12>& ch) {
        ChordEstimate best; float bestS = -1e9f;
        for (int r = 0; r < 12; ++r) for (int m = 0; m < 2; ++m) {
            int third = m ? 3 : 4;
            float s = ch[(size_t) r] + ch[(size_t) ((r + third) % 12)] + ch[(size_t) ((r + 7) % 12)];
            for (int i = 0; i < 12; ++i) if (i != r && i != (r + third) % 12 && i != (r + 7) % 12) s -= 0.33f * ch[(size_t) i];
            if (s > bestS) { bestS = s; best.root = r; best.minor = m; }
        }
        best.confidence = juce::jlimit (0.0f, 1.0f, bestS / 3.0f); return best;
    }
    static KeyEstimate krumhansl (const std::array<float,12>& ch) {
        static const float maj[12] = { 6.35f,2.23f,3.48f,2.33f,4.38f,4.09f,2.52f,5.19f,2.39f,3.66f,2.29f,2.88f };
        static const float mnr[12] = { 6.33f,2.68f,3.52f,5.38f,2.60f,3.53f,2.54f,4.75f,3.98f,2.69f,3.34f,3.17f };
        KeyEstimate best; best.correlation = -2;
        for (int mode = 0; mode < 2; ++mode) for (int t = 0; t < 12; ++t) {
            const float* p = mode ? mnr : maj; float mx = 0, mp = 0;
            for (int i = 0; i < 12; ++i) { mx += ch[(size_t) i]; mp += p[i]; } mx /= 12; mp /= 12;
            float num = 0, dx = 0, dp = 0;
            for (int i = 0; i < 12; ++i) { float a = ch[(size_t) ((i + t) % 12)] - mx, b = p[i] - mp; num += a * b; dx += a * a; dp += b * b; }
            float r = (dx > 0 && dp > 0) ? num / std::sqrt (dx * dp) : 0;
            if (r > best.correlation) { best.correlation = r; best.tonic = t; best.minor = mode; }
        }
        return best;
    }
    std::array<float,12> chroma {}, longChroma {};
    ChordEstimate chord; KeyEstimate key;
private:
    juce::dsp::FFT fft; juce::dsp::WindowingFunction<float> window;
    std::array<float, 2 * N> buf {}; double sampleRate = 48000;
};
} // namespace sj
