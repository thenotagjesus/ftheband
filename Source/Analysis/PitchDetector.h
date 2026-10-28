// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <vector>
#include <cmath>

namespace sj {
/** Monophonic YIN pitch detector (de Cheveigne & Kawahara 2002).
    Feed mono samples; call detect() on a full analysis window.
    Status: functional baseline, no octave-error smoothing yet (milestone M3). */
class PitchDetector {
public:
    void prepare (double sr, int windowSize = 2048) { sampleRate = sr; W = windowSize; diff.assign ((size_t) W / 2, 0.0f); }
    /** returns Hz or -1 if unvoiced */
    float detect (const float* x, float threshold = 0.15f) {
        const int half = W / 2;
        const int minTau = (int) (sampleRate / 1400.0), maxTau = std::min (half - 1, (int) (sampleRate / 70.0));
        for (int tau = 1; tau < half; ++tau) {
            float s = 0; for (int i = 0; i < half; ++i) { float d = x[i] - x[i + tau]; s += d * d; } diff[(size_t) tau] = s;
        }
        diff[0] = 1.0f; float run = 0;
        for (int tau = 1; tau < half; ++tau) { run += diff[(size_t) tau]; diff[(size_t) tau] = run > 0 ? diff[(size_t) tau] * (float) tau / run : 1.0f; }
        for (int tau = std::max (2, minTau); tau < maxTau; ++tau) {
            if (diff[(size_t) tau] < threshold) {
                while (tau + 1 < maxTau && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
                float a = diff[(size_t) tau - 1], b = diff[(size_t) tau], c = diff[(size_t) tau + 1];
                float den = a + c - 2 * b, shift = std::abs (den) > 1e-9f ? 0.5f * (a - c) / den : 0.0f;
                lastConfidence = 1.0f - b;
                return (float) (sampleRate / (tau + shift));
            }
        }
        lastConfidence = 0; return -1.0f;
    }
    static int hzToMidi (float hz) { return hz <= 0 ? -1 : (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0)); }
    float lastConfidence = 0;
    int windowSize() const { return W; }
private:
    double sampleRate = 48000; int W = 2048; std::vector<float> diff;
};
} // namespace sj
