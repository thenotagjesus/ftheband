// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include "Music/Theory.h"
#include "Engine/Synths.h"

namespace sj {
enum class Style { Rock, Funk, Blues, Ballad };

/** 16-step drum patterns; intensity (0..1, from guitar RMS) adds hats/ghost notes and velocity. */
class DrumGenerator {
public:
    void onStep (int64_t step, Style style, float intensity, DrumSynth& d) {
        const int s = (int) (step % 16);
        static const char* kick[]  = { "x.......x.x.....", "x..x..x...x..x..", "x.....x.x.....x.", "x.........x....." };
        static const char* snare[] = { "....x.......x...", "....x..x.x..x...", "....x.......x...", "........x......." };
        const int idx = (int) style;
        float v = 0.6f + 0.4f * intensity;
        if (kick[idx][s] == 'x') d.trigger (DrumSynth::Kick, v);
        if (snare[idx][s] == 'x') d.trigger (DrumSynth::Snare, v * 0.8f);
        bool hat = intensity > 0.5f ? true : (s % 2 == 0);
        if (style == Style::Ballad && intensity < 0.3f) hat = (s % 4 == 0);
        if (hat) d.trigger (DrumSynth::Hat, (s % 4 == 0 ? 0.7f : 0.45f) * v);
    }
};

/** Bass follows the detected chord root (snapped to the chosen key/scale), else the key tonic. */
class BassGenerator {
public:
    void onStep (int64_t step, Style style, int chordRoot, int keyRoot, ScaleType scale, float intensity, BassSynth& b) {
        const int s = (int) (step % 16);
        int root = chordRoot >= 0 ? snapToScale (chordRoot, keyRoot, scale) : keyRoot;
        int midi = 36 + root; if (midi > 45) midi -= 12;     // E1..A2-ish range
        bool hit = (s % 4 == 0) || (intensity > 0.6f && s % 2 == 0) || (style == Style::Funk && (s == 3 || s == 10));
        if (hit) {
            int n = midi;
            if (style == Style::Blues && s == 8) n = midi + 7;          // fifth on beat 3
            if (intensity > 0.75f && s == 14) n = midi + 12;            // octave pickup
            b.noteOn (n, 0.6f + 0.3f * intensity);
        } else if (s % 4 == 3) b.noteOff();
    }
};

/** Keys: STUB for v1 (M4). Will voice diatonic triads on chord changes. */
class KeysGenerator { public: void onStep (int64_t, int, int, ScaleType) {} };
} // namespace sj
