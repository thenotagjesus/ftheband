// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Session Jam contributors
#pragma once
#include <JuceHeader.h>
#include <random>
#include "Engine/Sampler.h"
#include "Engine/PolySynth.h"
#include "Engine/Grooves.h"
#include "Music/Theory.h"

namespace sj {

/** What the band knows about the player this block (from ChordTracker, possibly via atomics). */
struct FollowerState {
    ChordLabel chord;
    int keyTonic = -1; bool keyMinor = false;
    float intensity = 0;
    bool silent = true;
    bool countIn = false;                 // tracker is inside a count-in: stay out
    int64_t lastOnsetSample = -1000000;   // in the band's sample clock
    int64_t lastActiveSample = -1000000;  // last time the player was audibly sounding (hysteretic presence gate), same clock
};

enum class ChairType { Drums, Bass, Keys, Organ, RhythmGuitar, Pad, Percussion, Num };
inline juce::StringArray chairTypeNames() { return { "Drums", "Bass", "Keys/EP", "Organ", "Rhythm Guitar", "Pad/Strings", "Percussion" }; }
inline juce::StringArray chairRoleNames() { return { "Style", "Sparse", "Busy", "High" }; }

/** One band member: type, role, mixer state (atomics: UI edits never need a config swap) and its own voice engine. */
struct Chair {
    ChairType type = ChairType::Drums; int role = 0; juce::String name;
    std::atomic<float> gain { 0.8f }; std::atomic<bool> mute { false }, solo { false };
    std::unique_ptr<Instrument> inst;
    // generator state (audio thread only)
    int held[8] {}; int nHeld = 0; int voicing[8] {}; int nVoices = 0; int prev[8] { 55,60,64,67 }; int nPrev = 4;
};
/** Immutable chair list built + prepared on the message thread, then swapped into the audio thread. */
struct BandConfig { std::vector<std::unique_ptr<Chair>> chairs; };
static constexpr int MaxChairs = 8;

inline std::unique_ptr<Instrument> makeInstrument (ChairType t, SampleLibrary* lib) {
    auto sampler = [] (const ZoneSet& z, float rel) { auto s = std::make_unique<Sampler> (&z); s->releaseSeconds = rel; return s; };
    const bool have = lib != nullptr && lib->loaded > 0;
    switch (t) {
        case ChairType::Drums: case ChairType::Percussion:
            if (have) { auto s = sampler (lib->drums, 0.08f); s->chokeGroup = [] (int n) { return (n == 42 || n == 44 || n == 46) ? 1 : 0; }; return s; }
            return std::make_unique<PolySynth> (PolySynth::Pluck);
        case ChairType::Bass:  if (have) return sampler (lib->bass, 0.05f); return std::make_unique<PolySynth> (PolySynth::Pluck);
        case ChairType::Keys:  if (have) return sampler (lib->keys, 0.25f); return std::make_unique<PolySynth> (PolySynth::Organ);
        case ChairType::Organ: return std::make_unique<PolySynth> (PolySynth::Organ);
        case ChairType::Pad:   return std::make_unique<PolySynth> (PolySynth::Pad);
        case ChairType::RhythmGuitar: return std::make_unique<PolySynth> (PolySynth::Pluck);
        default: return nullptr;
    }
}

/** The adaptive backing band. Sample-accurate clock, plans each 16th one step ahead (humanised timing can be
    early as well as late), lands chord changes on the next beat (or half bar), cues fills on sharp intensity
    changes, breaks down / drops out when the player stops and re-enters on the next downbeat with a crash.
    Chairs are swapped in as a whole prepared BandConfig through an atomic pointer; the old one is handed back
    for deletion on the message thread. process() never allocates or locks. */
class Band {
public:
    enum State { Out, Playing, Breakdown, CountIn, Ending };
    struct LogEntry { int64_t sample; int kind; int a, b; };   // 0 chord(root,type) 1 state 2 fill(bar) 3 note(chair,midi)
    int style = 0; double bpm = 100; bool halfBarQuantize = false;
    /** Optional, both off by default (a Bass VI is played as a low guitar, so the bass chair plays normally):
        playerIsBass = "bass chair gives way": up an octave, sparse root (bar) + fifth (half bar);
        muteBassForBassPlayer = silence the bass chair entirely. Independent of the selected input instrument. */
    std::atomic<bool> playerIsBass { false }, muteBassForBassPlayer { false };
    int keyTonic = -1; bool keyMinor = false;
    State state = Out; ChordLabel bandChord; int tier = 0; int64_t step = 0;
    std::vector<LogEntry>* log = nullptr;    // offline only
    /** Bars of player silence the band keeps grooving through (pauses, held notes) before it starts a 2-bar
        fade-out (Breakdown) and drops out. Settings: Band tab, persisted. */
    std::atomic<int> holdBars { 4 };
    static constexpr int FadeBars = 2;
    /** How the band starts. 0 = "Start when I play": armed while Out, comes in on the downbeat after the player starts
        (and re-arms after every fade-out). 1 = "Start on button": waits for requestStart(), optional 1-bar click count-in. */
    std::atomic<int> startMode { 0 };
    std::atomic<bool> countInClick { true };
    void requestStart() { startReq.store (true); }
    void requestStop()  { stopReq.store (true); }      // musical stop: play to the next downbeat, final hit, ring out
    bool isActive() const { return state != Out; }

    ~Band() { delete cfg; delete pending.exchange (nullptr); delete retired.exchange (nullptr); }
    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    double sampleRate() const { return sr; }
    void reset() { if (cfg) for (auto& ch : cfg->chairs) { ch->nHeld = 0; if (ch->inst) ch->inst->allNotesOff (0.3f); }
        startReq = false; stopReq = false; playStart = 0; velScale = 1.0f; breakdownStart = 0; clock = 0; nextStepAt = 0; step = 0; state = Out; nEvents = 0; bandChord = {}; tier = 0; fillBar = -1; slowInt = fastInt = 0; lastFillBar = -100; }

    /** Message thread: hand over a fully prepared config. Returns false if a previous swap is still pending. */
    bool submit (std::unique_ptr<BandConfig> c) {
        collectGarbage();
        if (retired.load() != nullptr) return false;
        for (auto& ch : c->chairs) if (ch->inst) ch->inst->prepare (sr);
        BandConfig* expected = nullptr;
        BandConfig* raw = c.release();
        if (! pending.compare_exchange_strong (expected, raw)) { delete raw; return false; }
        return true;
    }
    /** Message thread: delete configs the audio thread has retired. */
    void collectGarbage() { delete retired.exchange (nullptr); }
    /** Message thread: true when no swap is in flight (the running config is the last one submitted). */
    bool swapIdle() { collectGarbage(); return pending.load() == nullptr && retired.load() == nullptr; }
    /** Offline/single-threaded convenience. */
    void setConfigNow (std::unique_ptr<BandConfig> c) { for (auto& ch : c->chairs) if (ch->inst) ch->inst->prepare (sr); delete cfg; cfg = c.release(); nEvents = 0; }
    const BandConfig* current() const { return cfg; }   // audio thread / offline only
    int numChairs() const { return cfg ? (int) cfg->chairs.size() : 0; }

    double stepLen() const { return sr * 60.0 / bpm / 4.0; }
    int64_t now() const { return clock; }
    /** Re-phase the grid so a beat lands on sample t; if asDownbeat that beat starts a bar. */
    void alignBeatTo (int64_t t, bool asDownbeat) {
        const double beat = 4 * stepLen();
        double bt = (double) t; while (bt < (double) clock) bt += beat; while (bt - beat >= (double) clock) bt -= beat;
        nextStepAt = bt; if (asDownbeat) step = ((step + 15) / 16) * 16;
    }

    /** outs[i] = mono buffer for chair i (numOuts >= numChairs()), cleared by the caller. */
    void process (const FollowerState& f, float* const* outs, int numOuts, int n) {
        if (auto* p = pending.exchange (nullptr)) {
            if (cfg) for (auto& ch : cfg->chairs) if (ch->inst) ch->inst->allNotesOff (0.01f);
            BandConfig* old = cfg; cfg = p; nEvents = 0;
            if (old) retired.store (old);   // submit() guarantees the slot is empty
        }
        if (! cfg) return;
        const int nc = juce::jmin (numOuts, (int) cfg->chairs.size());
        int pos = 0;
        while (pos < n) {
            if ((double) clock >= nextStepAt) { onStep (f); ++step; nextStepAt += stepLen(); continue; }
            while (nEvents > 0 && events[0].t <= clock) { fire (events[0]); popFront(); }
            int64_t until = juce::jmin (clock + (n - pos), (int64_t) std::ceil (nextStepAt));
            if (nEvents > 0) until = juce::jmin (until, events[0].t);
            if (until <= clock) until = clock + 1;
            const int len = (int) (until - clock);
            for (int c = 0; c < nc; ++c) if (auto* in = cfg->chairs[(size_t) c]->inst.get()) in->render (outs[c] + pos, len);
            pos += len; clock += len;
        }
        bool anySolo = false; for (int c = 0; c < nc; ++c) anySolo |= cfg->chairs[(size_t) c]->solo.load();
        for (int c = 0; c < nc; ++c) {
            auto& ch = *cfg->chairs[(size_t) c];
            const float g = (ch.mute.load() || (anySolo && ! ch.solo.load())) ? 0.0f : ch.gain.load();
            juce::FloatVectorOperations::multiply (outs[c], g, n);
        }
    }

private:
    struct Event { int64_t t; uint8_t chair, on, note, vel; };
    static constexpr int maxEvents = 512;
    std::array<Event, maxEvents> events; int nEvents = 0;
    double sr = 48000, nextStepAt = 0; int64_t clock = 0;
    BandConfig* cfg = nullptr; std::atomic<BandConfig*> pending { nullptr }, retired { nullptr };
    std::mt19937 rng { 12345 };
    int fillBar = -1; float slowInt = 0, fastInt = 0; int64_t lastFillBar = -100;
    float velScale = 1.0f; int64_t breakdownStart = 0, playStart = 0, endingStart = 0;
    std::atomic<bool> startReq { false }, stopReq { false };

    void push (int64_t t, int chair, bool on, int note, int vel) {
        if (nEvents >= maxEvents) return;
        int i = nEvents++;
        while (i > 0 && events[(size_t) i - 1].t > t) { events[(size_t) i] = events[(size_t) i - 1]; --i; }
        if (on) vel = (int) std::lround ((float) vel * velScale);
        events[(size_t) i] = { t, (uint8_t) chair, (uint8_t) on, (uint8_t) juce::jlimit (0, 127, note), (uint8_t) juce::jlimit (1, 127, vel) };
    }
    void popFront() { for (int i = 1; i < nEvents; ++i) events[(size_t) i - 1] = events[(size_t) i]; --nEvents; }
    void fire (const Event& e) {
        if (e.chair >= cfg->chairs.size()) return;
        auto* in = cfg->chairs[e.chair]->inst.get(); if (! in) return;
        if (e.on) in->noteOn (e.note, e.vel, 0); else in->noteOff (e.note);
        if (log && e.on) log->push_back ({ clock, 3, e.chair * 1000 + e.note, e.vel });
    }
    float jitterMs (float sd) { std::normal_distribution<float> g (0, sd); return juce::jlimit (-2.5f * sd, 2.5f * sd, g (rng)); }
    int humanVel (int v, float spread = 0.08f) { std::uniform_real_distribution<float> u (1 - spread, 1 + spread); return (int) ((float) v * u (rng)); }
    void setState (State s) { if (s == state) return; state = s; if (log) log->push_back ({ clock, 1, (int) s, 0 }); }
    struct StepCtx { int64_t s; int inBar, q; int64_t bar; double t0, L; bool chordChange, entering, inFill, ending; const Groove* g; };
    bool pendingEnter = false, wantEnd = false;
    const int* drumChair() { static thread_local int idx; if (! cfg) return nullptr;
        for (size_t i = 0; i < cfg->chairs.size(); ++i) if (cfg->chairs[i]->type == ChairType::Drums || cfg->chairs[i]->type == ChairType::Percussion) { idx = (int) i; return &idx; }
        return nullptr; }
    /** Ending downbeat: crash + kick, and every harmonic chair re-strikes the current chord and HOLDS it (no note-off
        scheduled) so it rings for the Ending bar; ringOut() then releases with a long envelope. Never a hard cut. */
    void finalHit (const StepCtx& c) {
        if (! cfg) return;
        velScale = std::max (velScale, 0.6f);
        for (size_t i = 0; i < cfg->chairs.size(); ++i) {   // old notes end exactly where the final chord starts (no gap)
            auto& ch = *cfg->chairs[i]; for (int k = 0; k < ch.nHeld; ++k) push ((int64_t) c.t0, (int) i, false, ch.held[k], 1); ch.nHeld = 0; }
        for (size_t i = 0; i < cfg->chairs.size(); ++i) {
            auto& ch = *cfg->chairs[i];
            if (ch.type == ChairType::Drums || ch.type == ChairType::Percussion) {
                push ((int64_t) c.t0, (int) i, true, 49, humanVel (112, 0.05f)); push ((int64_t) c.t0, (int) i, true, 36, humanVel (105, 0.05f)); continue; }
            if (! bandChord.valid()) continue;
            if (ch.nVoices == 0) buildVoicing (ch);
            const bool mono = ch.type == ChairType::Bass;
            const int nv = mono ? 1 : ch.nVoices;
            for (int v = 0; v < nv && ch.nHeld < 8; ++v) {
                const int note = mono ? 28 + ((bandChord.root + 12 - 4) % 12) : ch.voicing[v];   // bass: root, E1..D#2
                push ((int64_t) c.t0 + v * 40, (int) i, true, note, humanVel (90)); ch.held[ch.nHeld++] = note; }
        }
    }
    void ringOut() {
        if (! cfg) return;
        nEvents = 0;
        for (auto& ch : cfg->chairs) { ch->nHeld = 0; if (ch->inst && ch->type != ChairType::Drums && ch->type != ChairType::Percussion) ch->inst->allNotesOff (1.5f); }
    }
    void releaseAll() {
        for (size_t c = 0; c < cfg->chairs.size(); ++c) { auto& ch = *cfg->chairs[c]; for (int i = 0; i < ch.nHeld; ++i) push (clock + 1, (int) c, false, ch.held[i], 1); ch.nHeld = 0; }
    }

    // ------------------------------------------------------------------------------------------------ planning

    void onStep (const FollowerState& f) {
        StepCtx c {};
        if (startReq.exchange (false) && (state == Out)) {
            // button start: snap the grid so the coming step is a downbeat
            step = ((step + 1 + 15) / 16) * 16 - 1;
            if (countInClick.load()) { setState (CountIn); playStart = clock; }
            else pendingEnter = true;
        }
        if (stopReq.exchange (false)) { if (state == CountIn) setState (Out); else if (state == Playing || state == Breakdown) wantEnd = true; }
        c.s = step + 1; c.inBar = (int) (c.s % 16); c.q = c.inBar % 4; c.bar = c.s / 16;
        c.g = &groove (style); c.L = stepLen();
        c.t0 = nextStepAt + c.L;
        if (c.q == 2) c.t0 += c.g->swing8 * 2 * c.L; else if (c.q == 1 || c.q == 3) c.t0 += c.g->swing16 * c.L + (c.q == 3 ? c.g->swing8 * c.L : 0.0);

        fastInt += (f.intensity - fastInt) * 0.25f;
        slowInt += (f.intensity - slowInt) * 0.02f;
        const bool sharp = std::abs (fastInt - slowInt) > 0.15f;
        if (state == Playing && sharp && fillBar < c.bar && c.bar - lastFillBar >= 2 && c.inBar <= 9) { fillBar = (int) c.bar; lastFillBar = c.bar; if (log) log->push_back ({ clock, 2, (int) c.bar, 0 }); }
        if (c.inBar == 0) tier = f.intensity < 0.33f ? 0 : f.intensity < 0.62f ? 1 : 2;

        const double beat = 4 * c.L;
        // silence is measured from the last moment the player was SOUNDING (presence gate), not the last pick attack,
        // so held notes and their decay keep the band in; the band holds the groove for holdBars bars of real
        // silence, then fades over FadeBars bars (Breakdown) and drops out. Any new note during the fade = back in on the beat.
        const double bar = 4 * beat;
        const int64_t lastSound = std::max ({ f.lastOnsetSample, f.lastActiveSample, playStart });
        const double silentFor = f.silent ? (double) (clock - lastSound) : 0.0;
        const bool recentOnset = clock - f.lastOnsetSample < (int64_t) (beat * 1.5);
        const double hold = bar * juce::jlimit (1, 16, holdBars.load (std::memory_order_relaxed));
        if (state == CountIn) {
            if (c.inBar == 0 && clock - playStart > (int64_t) (2 * c.L)) { setState (Playing); c.entering = true; velScale = 1.0f; playStart = clock; }
            else if (c.q == 0) { if (auto* d = drumChair()) push ((int64_t) c.t0, *d, true, c.inBar == 0 ? 37 : 42, c.inBar == 0 ? 112 : 90); }   // click: side-stick on 1, hat on 2-4
        }
        if (pendingEnter && c.inBar == 0) { pendingEnter = false; setState (Playing); c.entering = true; velScale = 1.0f; playStart = clock; }
        if (wantEnd && c.inBar == 0) { wantEnd = false; setState (Ending); endingStart = clock; c.ending = true; }
        else if (state == Ending && clock - endingStart >= (int64_t) (16 * c.L) - (int64_t) (0.5 * c.L)) {   // last chord has rung a bar: long release
            setState (Out); ringOut(); velScale = 1.0f; }
        if (state == Ending) { if (c.ending) finalHit (c); return; }
        if (state == Out && startMode.load() == 0 && ! f.silent && ! f.countIn && f.chord.valid() && recentOnset && c.inBar == 0) { setState (Playing); c.entering = true; velScale = 1.0f; playStart = clock; }
        else if (state == Breakdown && ! f.silent && recentOnset && c.q == 0) { setState (Playing); velScale = 1.0f; }
        else if (state == Playing && silentFor > hold && c.q == 0) { setState (Breakdown); breakdownStart = clock; }
        else if (state == Breakdown && (double) (clock - breakdownStart) >= bar * FadeBars - 0.5 * c.L && c.inBar == 0) { setState (Ending); endingStart = clock; finalHit (c); return; }
        if (state == Breakdown)   // linear fade 0.85 -> 0.35 across the fade bars
            velScale = (float) juce::jlimit (0.35, 0.85, 0.85 - 0.5 * (double) (clock - breakdownStart) / (bar * FadeBars));

        const bool boundary = halfBarQuantize ? (c.inBar % 8 == 0) : (c.q == 0);
        if (boundary && f.chord.valid() && (! bandChord.sameChord (f.chord) || bandChord.bass != f.chord.bass)) {
            c.chordChange = true; bandChord = f.chord; keyTonic = f.keyTonic; keyMinor = f.keyMinor;
            if (log) log->push_back ({ (int64_t) c.t0, 0, bandChord.root, bandChord.type });
            for (auto& ch : cfg->chairs) buildVoicing (*ch);
        }
        if (state == Out || state == CountIn) return;
        const bool drumsOnly = ! bandChord.valid();   // button start before the first chord: groove on drums until one is heard
        c.inFill = fillBar >= 0 && c.bar == fillBar && state == Playing;
        for (size_t i = 0; i < cfg->chairs.size(); ++i) {
            auto& ch = *cfg->chairs[i];
            if (drumsOnly && ch.type != ChairType::Drums && ch.type != ChairType::Percussion) continue;
            if (ch.nVoices == 0 && ! drumsOnly) buildVoicing (ch);
            switch (ch.type) {
                case ChairType::Drums: drums (c, (int) i); break;
                case ChairType::Percussion: perc (c, (int) i, ch); break;
                case ChairType::Bass: bass (c, (int) i, ch); break;
                default: comp (c, (int) i, ch); break;
            }
        }
    }
    int tierFor (const Chair& ch) const {
        if (state == Breakdown) return 0;
        return juce::jlimit (0, 2, tier + (ch.role == 1 ? -1 : ch.role == 2 ? 1 : 0));
    }
    int64_t tm (const StepCtx& c, float ms) const { return (int64_t) (c.t0 + ms * 0.001 * sr); }
    static char at (const std::string& p, int64_t s) { return p[(size_t) (s % (int64_t) p.size())]; }

    void drums (const StepCtx& c, int ci) {
        const auto& tr = c.g->tiers[tierFor (*cfg->chairs[(size_t) ci])];
        bool fillHere = false;
        if (c.inFill) for (auto& fp : c.g->fill) if (fp.first == c.inBar && (c.inBar >= 12 || tier == 2)) {
            push (tm (c, jitterMs (2.0f)), ci, true, fp.second, humanVel (100 + 3 * (c.inBar - 8))); fillHere = true; }
        if (c.entering || (c.inBar == 0 && fillBar == (int) c.bar - 1 && state == Playing))
            push (tm (c, jitterMs (1.0f)), ci, true, 49, humanVel (118, 0.05f));
        if (state == Breakdown) {
            if (c.q == 0) push (tm (c, jitterMs (3.0f)), ci, true, 42, humanVel (40));
            if (c.inBar == 0) push (tm (c, jitterMs (1.5f)), ci, true, 36, humanVel (60));
            return;
        }
        if (c.inFill && c.inBar >= 12 && fillHere) return;
        auto vel = [] (char ch) { return ch == 'X' ? 118 : ch == 'x' ? 92 : ch == 'g' ? 34 : 0; };
        if (int v = vel (at (tr.kick, c.s))) push (tm (c, jitterMs (1.5f)), ci, true, 36, humanVel (v));
        if (int v = vel (at (tr.snare, c.s))) push (tm (c, jitterMs (2.0f) + (v < 40 ? 4.0f : 0.0f)), ci, true, 38, humanVel (v, v < 40 ? 0.25f : 0.06f));
        const char h = at (tr.hat, c.s);
        const int hv = h == 'X' ? 105 : h == 'x' ? (c.q == 0 ? 82 : 62) : h == 'o' ? 80 : h == 'O' ? 105 : 0;
        if (hv) push (tm (c, jitterMs (3.0f)), ci, true, (h == 'o' || h == 'O') ? 46 : c.g->hatNote, humanVel (hv, 0.12f));
    }
    void perc (const StepCtx& c, int ci, const Chair& ch) {
        if (state == Breakdown) return;
        const int t = tierFor (ch);
        // shaker-like 16ths on the closed hat (soft, accented off-beats), side-stick backbeat, low tom pickup when busy
        const int v = c.q == 2 ? 52 : c.q == 0 ? 30 : 38;
        if (t >= 1 || c.q % 2 == 0) push (tm (c, jitterMs (4.0f) + 6.0f), ci, true, 44, humanVel (v, 0.2f));
        if ((c.inBar == 4 || c.inBar == 12) && t >= 1) push (tm (c, jitterMs (2.5f)), ci, true, 37, humanVel (80));
        if (t == 2 && c.inBar == 14) push (tm (c, jitterMs (2.5f)), ci, true, 41, humanVel (70));
    }
    void bass (const StepCtx& c, int ci, Chair& ch) {
        const auto& tr = c.g->tiers[tierFor (ch)];
        const float lb = c.g->laidBackMs;
        auto offAll = [&] (float ms) { for (int i = 0; i < ch.nHeld; ++i) push (tm (c, ms), ci, false, ch.held[i], 1); ch.nHeld = 0; };
        if (muteBassForBassPlayer.load()) { if (ch.nHeld) offAll (lb); return; }
        if (playerIsBass.load()) {
            const bool hit = c.inBar == 0 || (c.inBar == 8 && tierFor (ch) > 0 && state != Breakdown) || (c.chordChange && c.q == 0);
            if (! hit) return;
            const int midi = bassNoteFor (c.inBar == 8 && ! c.chordChange ? '5' : 'R') + 12;
            offAll (lb - 3);
            push (tm (c, lb + jitterMs (2.5f)), ci, true, midi, humanVel (state == Breakdown ? 55 : 78));
            ch.held[0] = midi; ch.nHeld = 1; return;
        }
        char k = state == Breakdown ? (c.inBar == 0 ? 'R' : '_') : at (tr.bass, c.s);
        if (c.chordChange && (k == '_' || k == '-')) k = 'R';
        auto off = [&] (float ms) { for (int i = 0; i < ch.nHeld; ++i) push (tm (c, ms), ci, false, ch.held[i], 1); ch.nHeld = 0; };
        if (k == '-') { off (lb - 8); return; }
        if (k == '_') return;
        const int midi = bassNoteFor (k) + (ch.role == 3 ? 12 : 0);
        int v = k == 'g' ? 45 : (c.q == 0 ? 100 : 86); if (state == Breakdown) v = 60;
        off (lb + jitterMs (2) - 3);
        push (tm (c, lb + jitterMs (2.5f)), ci, true, midi, humanVel (v));
        if (k == 'g') push (tm (c, lb + 60), ci, false, midi, 1); else { ch.held[0] = midi; ch.nHeld = 1; }
    }
    std::string compPattern (ChairType t, int tierIdx) const {
        const auto& g = groove (style);
        switch (t) {
            case ChairType::Organ: return tierIdx == 2 ? "C_______C___C___" : "C_______________";
            case ChairType::Pad:   return "C_______________________________";
            case ChairType::RhythmGuitar:
                if (style == 1) return tierIdx == 0 ? "s-----s-s-----s-" : "s-ss-s-ss-ss-s-s";
                if (style == 2) return tierIdx == 0 ? "C-------C-------" : "C-c-C-c-C-c-C-c-";
                if (style == 3) return tierIdx == 0 ? "C_______________" : "C___c___C___c___";
                return tierIdx == 0 ? "C_______C_______" : tierIdx == 1 ? "C_c_C_c_C_c_C_c_" : "CcCcCcCcCcCcCcCc";
            default: return g.tiers[tierIdx].keys;
        }
    }
    void comp (const StepCtx& c, int ci, Chair& ch) {
        const int ti = tierFor (ch);
        char k = state == Breakdown ? ((c.inBar == 0 && ch.type != ChairType::RhythmGuitar) ? 'c' : '_') : at (compPattern (ch.type, ti), c.s);
        if (c.chordChange && (k == '_' || k == '-') && (state == Playing || ch.type != ChairType::RhythmGuitar)) k = 'c';
        const float lb = c.g->laidBackMs + 2.0f;
        const bool strum = ch.type == ChairType::RhythmGuitar;
        auto off = [&] (float ms) { for (int i = 0; i < ch.nHeld; ++i) push (tm (c, ms), ci, false, ch.held[i], 1); ch.nHeld = 0; };
        if (k == '-') { off (lb - 10); return; }
        if (k == 'C' || k == 'c' || k == 's') {
            off (lb - 4);
            const int base = k == 'C' ? 92 : k == 's' ? 84 : 62;
            const bool up = strum && (c.s % 2 == 1);
            for (int i = 0; i < ch.nVoices; ++i) {
                const int vi = up ? ch.nVoices - 1 - i : i;
                const float spread = strum ? 7.0f : 4.0f;
                push (tm (c, lb + (float) i * spread + jitterMs (1.5f)), ci, true, ch.voicing[vi], humanVel (base - i * 3));
                ch.held[ch.nHeld++] = ch.voicing[vi];
            }
            if (k == 's') off (lb + (float) (c.L / sr * 1000.0 * 0.9));
        } else if (k >= '1' && k <= '4' && ch.nVoices > 0) {
            off (lb + 30);
            const int note = ch.voicing[(k - '1') % ch.nVoices];
            push (tm (c, lb + jitterMs (2)), ci, true, note, humanVel (70)); ch.held[0] = note; ch.nHeld = 1;
        }
    }
    int bassNoteFor (char k) const {
        const int bassPc = bandChord.bass >= 0 ? bandChord.bass : bandChord.root;
        const auto& iv = chordIntervals (bandChord.type);
        int pc = bassPc;
        switch (k) {
            case '5': pc = bandChord.root + 7; break;
            case '3': pc = bandChord.root + (bandChord.type == Power || bandChord.type == Sus2 || bandChord.type == Sus4 ? 7 : iv[1]); break;
            case '7': pc = bandChord.root + ((bandChord.type == Dom7 || bandChord.type == Min7 || bandChord.minorish()) ? 10 : 9); break;
            default: break;
        }
        pc = ((pc % 12) + 12) % 12;
        const int rootMidi = 28 + (((bassPc - 4) % 12) + 12) % 12;      // E1..D#2
        if (k == 'R' || k == 'g') return rootMidi;
        if (k == '8') return rootMidi + 12;
        if (k == 'a') return rootMidi - 1 < 28 ? rootMidi + 11 : rootMidi - 1;
        return rootMidi + ((pc - rootMidi % 12) % 12 + 12) % 12;
    }
    void buildVoicing (Chair& ch) {
        if (! bandChord.valid()) { ch.nVoices = 0; return; }
        int pcs[6] {}; int n = 0;
        for (int i : chordIntervals (bandChord.type)) pcs[n++] = (bandChord.root + i) % 12;
        if (bandChord.type == Power) {
            // power chord from the player: keys/pads pick the diatonic third from the key so the band sounds major or minor
            if (keyTonic >= 0 && ch.type != ChairType::RhythmGuitar) {
                static const bool majD[12] = { 1,0,1,0,1,1,0,1,0,1,0,1 }, minD[12] = { 1,0,1,1,0,1,0,1,1,0,1,0 };
                const int m3 = (bandChord.root + 3) % 12, d = ((m3 - keyTonic) % 12 + 12) % 12;
                pcs[n++] = (keyMinor ? minD[d] : majD[d]) ? m3 : (bandChord.root + 4) % 12;
            } else pcs[n++] = bandChord.root;
        }
        int lo = 52, hi = 74;
        if (ch.type == ChairType::RhythmGuitar) { lo = 40; hi = 66; }
        if (ch.type == ChairType::Pad || ch.role == 3) { lo += 12; hi += 12; }
        int bestCost = 1 << 30; int best[6] {};
        for (int inv = 0; inv < n; ++inv)
            for (int base = lo; base <= lo + 10; ++base) {
                int v[6] {}; int prev = base - 1;
                for (int i = 0; i < n; ++i) { int pc = pcs[(i + inv) % n]; int m = prev + 1; while (m % 12 != pc) ++m; v[i] = m; prev = m; }
                if (v[n - 1] > hi) continue;
                int cost = std::abs (v[0] - (lo + 5)) / 2;
                for (int i = 0; i < n; ++i) cost += std::abs (v[i] - ch.prev[juce::jmin (i, ch.nPrev - 1)]);
                if (cost < bestCost) { bestCost = cost; std::copy (v, v + n, best); }
            }
        std::copy (best, best + n, ch.voicing); ch.nVoices = n; std::copy (best, best + n, ch.prev); ch.nPrev = n;
    }
};
} // namespace sj
