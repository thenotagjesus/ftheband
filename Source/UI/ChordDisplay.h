// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <array>
#include "Music/Theory.h"

namespace sj {

/** UI-only smoothing of the chord label shown on the Jam page (the detector and the band are untouched).
    - Minimum hold: a label stays on screen for at least MinHoldMs once shown.
    - Confidence + hysteresis: over the last WindowMs of UI ticks, each reported label gets a share of the votes
      (weighted by ChordLabel::confidence when the detector fills it in, else 1). A new label replaces the shown one
      only when its share reaches SwitchShare AND the shown label's share has fallen to KeepShare or below.
      Going blank ("--") needs ClearShare of the window, so short dropouts between picks don't blank the display.
    A slash-bass change (C -> C/E) counts as a different label. Call update() once per UI tick, read shown(). */
class ChordDisplayHold {
public:
    static constexpr double MinHoldMs = 250.0, WindowMs = 300.0;
    static constexpr float SwitchShare = 0.6f, KeepShare = 0.4f, ClearShare = 0.8f;

    const ChordLabel& shown() const { return disp; }
    void reset() { n = 0; head = 0; disp = {}; shownAt = -1.0e9; }

    void update (const ChordLabel& in, double nowMs) {
        hist[(size_t) head] = { in, in.confidence > 0.0f ? in.confidence : 1.0f, nowMs };
        head = (head + 1) % Cap; if (n < Cap) ++n;
        if (same (in, disp)) { disp.confidence = in.confidence; return; }
        if (nowMs - shownAt < MinHoldMs) return;
        const float cand = share (in, nowMs), cur = share (disp, nowMs);
        const float need = in.valid() ? SwitchShare : ClearShare;
        if (cand >= need && cur <= KeepShare) { disp = in; shownAt = nowMs; }
    }

private:
    struct Vote { ChordLabel c; float w; double t; };
    static constexpr int Cap = 32;
    std::array<Vote, Cap> hist {}; int n = 0, head = 0;
    ChordLabel disp; double shownAt = -1.0e9;

    static bool same (const ChordLabel& a, const ChordLabel& b) {
        if (! a.valid() || ! b.valid()) return a.valid() == b.valid();
        return a.sameChord (b) && a.bass == b.bass;
    }
    float share (const ChordLabel& c, double nowMs) const {
        float hit = 0, tot = 0;
        for (int i = 0; i < n; ++i) { const auto& v = hist[(size_t) i]; if (nowMs - v.t > WindowMs) continue; tot += v.w; if (same (v.c, c)) hit += v.w; }
        return tot > 0 ? hit / tot : 0.0f;
    }
};
} // namespace sj
