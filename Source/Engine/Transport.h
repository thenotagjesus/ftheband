// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <atomic>
#include <cstdint>

namespace sj {
/** Sample-accurate transport. Calls onStep(stepIndex, sampleOffset) for every 16th-note boundary inside a block. */
class Transport {
public:
    std::atomic<float> bpm { 100.0f };
    std::atomic<bool> playing { false };
    void prepare (double sr) { sampleRate = sr; reset(); }
    void reset() { samplesToNextStep = 0; step = 0; }
    template <typename Fn> void advance (int numSamples, Fn&& onStep) {
        if (! playing.load()) return;
        const double stepLen = sampleRate * 60.0 / bpm.load() / 4.0;
        int pos = 0;
        while (pos < numSamples) {
            if (samplesToNextStep <= 0.0) { onStep (step, pos); ++step; samplesToNextStep += stepLen; }
            int adv = (int) std::min<double> (numSamples - pos, std::ceil (samplesToNextStep));
            if (adv <= 0) adv = 1;
            samplesToNextStep -= adv; pos += adv;
        }
    }
    int64_t currentStep() const { return step; }
private:
    double sampleRate = 48000, samplesToNextStep = 0; int64_t step = 0;
};
} // namespace sj
