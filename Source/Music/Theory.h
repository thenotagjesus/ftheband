// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <array>
#include <string>
#include <vector>

namespace sj {
enum class ScaleType { Major, NaturalMinor, MinorPentatonic, MajorPentatonic, Blues, Dorian, Mixolydian };

inline const char* noteName (int pc) {
    static const char* n[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    return n[((pc % 12) + 12) % 12];
}

inline std::array<bool,12> scaleMask (ScaleType t) {
    std::array<bool,12> m {};
    auto set = [&] (std::initializer_list<int> iv) { for (int i : iv) m[(size_t) i] = true; };
    switch (t) {
        case ScaleType::Major:           set ({0,2,4,5,7,9,11}); break;
        case ScaleType::NaturalMinor:    set ({0,2,3,5,7,8,10}); break;
        case ScaleType::MinorPentatonic: set ({0,3,5,7,10}); break;
        case ScaleType::MajorPentatonic: set ({0,2,4,7,9}); break;
        case ScaleType::Blues:           set ({0,3,5,6,7,10}); break;
        case ScaleType::Dorian:          set ({0,2,3,5,7,9,10}); break;
        case ScaleType::Mixolydian:      set ({0,2,4,5,7,9,10}); break;
    }
    return m;
}

/** Snap a pitch class to the nearest pitch class in key+scale. */
inline int snapToScale (int pc, int keyRoot, ScaleType t) {
    auto m = scaleMask (t);
    for (int d = 0; d < 6; ++d)
        for (int s : { -d, d }) {
            int c = ((pc + s) % 12 + 12) % 12;
            if (m[(size_t) (((c - keyRoot) % 12 + 12) % 12)]) return c;
        }
    return pc;
}

enum ChordType { Maj, Min, Dom7, Min7, Sus2, Sus4, Power, NumChordTypes };

/** Intervals (semitones above root) of each chord type. */
inline const std::vector<int>& chordIntervals (int type) {
    static const std::vector<int> iv[NumChordTypes] = { {0,4,7}, {0,3,7}, {0,4,7,10}, {0,3,7,10}, {0,2,7}, {0,5,7}, {0,7} };
    return iv[type];
}
inline const char* chordSuffix (int type) {
    static const char* s[] = { "", "m", "7", "m7", "sus2", "sus4", "5" };
    return s[type];
}
inline bool chordContains (int root, int type, int pc) {
    for (int i : chordIntervals (type)) if ((root + i) % 12 == ((pc % 12) + 12) % 12) return true;
    return false;
}
/** A detected chord: root pitch class, ChordType, lowest sounding pitch class (for inversions). */
struct ChordLabel {
    int root = -1, type = Maj, bass = -1; float confidence = 0;
    bool valid() const { return root >= 0; }
    bool sameChord (const ChordLabel& o) const { return root == o.root && type == o.type; }
    bool minorish() const { return type == Min || type == Min7; }
    std::string name() const {
        if (root < 0) return "N";
        std::string s = std::string (noteName (root)) + chordSuffix (type);
        if (bass >= 0 && bass != root) s += std::string ("/") + noteName (bass);
        return s;
    }
};

struct ChordEstimate { int root = -1; bool minor = false; float confidence = 0.0f; };
struct KeyEstimate   { int tonic = -1; bool minor = false; float correlation = 0.0f; };

inline std::string chordName (const ChordEstimate& c) { return c.root < 0 ? "--" : std::string (noteName (c.root)) + (c.minor ? "m" : ""); }
inline std::string keyName   (const KeyEstimate& k)   { return k.tonic < 0 ? "--" : std::string (noteName (k.tonic)) + (k.minor ? " minor" : " major"); }
} // namespace sj
