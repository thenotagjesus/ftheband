// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <JuceHeader.h>

namespace sj {
/** What the player plugs in. Sets detector ranges and how the band's bass chair behaves. */
enum class InputInstrument { GuitarStandard, GuitarDropD, BassVI, Bass4, Num };
inline juce::StringArray inputInstrumentNames() { return { "Guitar (standard)", "Guitar (drop D)", "Bass VI", "Bass (4-string)" }; }
inline juce::StringArray inputInstrumentIds()   { return { "guitar", "guitar-dropd", "bassvi", "bass4" }; }
inline InputInstrument inputInstrumentFromId (const juce::String& s) {
    const int i = inputInstrumentIds().indexOf (s.trim().toLowerCase()); return i < 0 ? InputInstrument::BassVI : (InputInstrument) i;
}
inline bool isBassRange (InputInstrument i) { return i == InputInstrument::BassVI || i == InputInstrument::Bass4; }

struct InputProfile {
    int loMidi, hiMidi;      // multi-pitch search range
    int fftOrder;            // chroma FFT size (2^order)
    int minWindow;           // shortest onset-aligned window analysed
    float yinMinHz, highPassHz;
    bool lowRegister;        // bass-register heuristics: root/bass evidence, passing-tone hold
};
inline InputProfile baseProfile (InputInstrument i) {
    switch (i) {
        case InputInstrument::GuitarDropD: return { 37, 88, 13, 2048, 70.0f, 60.0f, false };
        case InputInstrument::BassVI:      return { 28, 76, 14, 4096, 35.0f, 25.0f, true };   // E1..E4 (nothing below the open low E: anything lower is a subharmonic error)
        case InputInstrument::Bass4:       return { 23, 67, 14, 4096, 28.0f, 20.0f, true };   // B0..G4
        default:                           return { 38, 88, 13, 2048, 75.0f, 70.0f, false };  // E2..E6
    }
}

/** How the instrument is tuned. Reference only: it lowers/raises the detector's range limit and lets the UI show the
    fingered shape next to the sounding chord. Key and chord detection always work in sounding pitch (tuning-agnostic). */
enum class Tuning { Standard, Down1, Down2, DropD, Custom, Num };
inline juce::StringArray tuningNames() { return { "Standard", "-1 semitone", "-2 semitones", "Drop D", "Custom (cents)" }; }
inline juce::StringArray tuningIds()   { return { "standard", "down1", "down2", "dropd", "custom" }; }
inline Tuning tuningFromId (const juce::String& s) { const int i = tuningIds().indexOf (s.trim().toLowerCase()); return i < 0 ? Tuning::Standard : (Tuning) i; }
/** Whole-instrument shift in semitones (sounding - written): used to name the fingered shape. */
inline float tuningShiftSemis (Tuning t, float cents) {
    switch (t) { case Tuning::Down1: return -1; case Tuning::Down2: return -2; case Tuning::Custom: return cents / 100.0f; default: return 0; }
}
/** Shift of the lowest string (drop D lowers only the low E). */
inline float lowestStringShift (Tuning t, float cents) { return t == Tuning::DropD ? -2.0f : tuningShiftSemis (t, cents); }
inline InputProfile inputProfile (InputInstrument i, Tuning t = Tuning::Standard, float cents = 0) {
    auto p = baseProfile (i);
    float lo = lowestStringShift (t, cents);
    if (i == InputInstrument::GuitarDropD && t == Tuning::DropD) lo = 0;    // the profile already allows drop D
    p.loMidi = juce::jlimit (12, 60, p.loMidi + (int) std::floor (lo + 0.001f));
    const float hi = tuningShiftSemis (t, cents); p.hiMidi += (int) std::ceil (hi - 0.001f);
    p.yinMinHz *= std::pow (2.0f, juce::jmin (0.0f, lo) / 12.0f);
    return p;
}
/** Pitch class as fingered (what the player's hands play) for a sounding pitch class. */
inline int fingeredPc (int soundingPc, Tuning t, float cents) {
    if (soundingPc < 0) return soundingPc;
    const int sh = juce::roundToInt (tuningShiftSemis (t, cents)); return ((soundingPc - sh) % 12 + 12) % 12;
}
} // namespace sj
