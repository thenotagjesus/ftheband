// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <JuceHeader.h>

namespace sj {
/** Per-stem integer delay line for the recorder tap (plugin delay compensation + round-trip alignment).
    Allocated in prepare(); process() is real-time safe. A delay change crossfades old tap -> new tap over `xfade`
    samples instead of jumping. The lines run all the time (not only while recording), so a take starts with real
    audio, not with `delay` samples of zeros. */
class StemDelay {
public:
    static constexpr int maxDelay = 1 << 16;            // 1.36 s @ 48k: large lookahead plugins + round trip
    void prepare (int maxBlock) { buf.assign ((size_t) (maxDelay + maxBlock + 1), 0.0f); size = (int) buf.size(); w = 0; cur = prev = target.load(); fadePos = xfade; }
    void setDelay (int d) noexcept { target.store (juce::jlimit (0, maxDelay, d)); }   // any thread
    int  getDelay() const noexcept { return cur; }
    /** In place: x becomes x delayed by the current delay. */
    void process (float* x, int n) noexcept {
        if (size <= 1) return;
        const int t = target.load (std::memory_order_relaxed);
        if (t != cur && fadePos >= xfade) { prev = cur; cur = t; fadePos = 0; }
        for (int i = 0; i < n; ++i) {
            buf[(size_t) w] = x[i];
            const float a = tap (cur);
            if (fadePos < xfade) { const float g = (float) fadePos++ / (float) xfade; x[i] = a * g + tap (prev) * (1.0f - g); }
            else x[i] = a;
            if (++w == size) w = 0;
        }
    }
private:
    static constexpr int xfade = 256;
    std::vector<float> buf; int size = 1, w = 0, cur = 0, prev = 0, fadePos = xfade;
    std::atomic<int> target { 0 };
    float tap (int d) const noexcept { int r = w - d; if (r < 0) r += size; return buf[(size_t) r]; }
};
} // namespace sj
