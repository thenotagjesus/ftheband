// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Session Jam contributors
#pragma once
#include <JuceHeader.h>
#include <random>

namespace sj {
/** Multisample instrument: zones keyed by (root note, velocity centre), round-robin among zones sharing both,
    nearest-zone fallback with repitching (keytracked instruments), velocity-to-gain, choke groups and
    short release fades (no clicks on retrigger / note-off). Voices are pre-allocated; render is RT-safe. */
/** Common voice-engine interface for band chairs. All methods are RT-safe after prepare(). */
struct Instrument {
    virtual ~Instrument() = default;
    virtual void prepare (double sampleRate) = 0;
    virtual void noteOn (int note, int velocity, int offset) = 0;
    virtual void noteOff (int note) = 0;
    virtual void allNotesOff (float seconds) = 0;
    virtual void render (float* out, int n) = 0;
};

struct SampleZone { juce::AudioBuffer<float> data; int root = 60, vel = 64; bool keytrack = true; double sr = 44100; int group = 0; };
/** Immutable set of zones shared by any number of Sampler instances (owned by SampleLibrary). */
struct ZoneSet {
    std::vector<std::unique_ptr<SampleZone>> zones; int numGroups = 0;
    void finalise() {   // assign round-robin groups: zones with identical (root, vel)
        std::map<std::pair<int,int>, int> g;
        for (auto& z : zones) { auto k = std::make_pair (z->root, z->vel); if (! g.count (k)) g[k] = (int) g.size(); z->group = g[k]; }
        numGroups = (int) g.size();
    }
};

class Sampler : public Instrument {
public:
    using Zone = SampleZone;
    explicit Sampler (const ZoneSet* set = nullptr) { setZones (set); }
    void setZones (const ZoneSet* set) { zoneSet = set; rr.assign (set ? (size_t) set->numGroups + 1 : 1, 0); }
    bool empty() const { return zoneSet == nullptr || zoneSet->zones.empty(); }
    void prepare (double sampleRate) override { sr = sampleRate; for (auto& v : voices) v.active = false; }
    float releaseSeconds = 0.08f;
    float gainTrim = 1.0f;
    std::function<int (int)> chokeGroup;   // note -> group (0 = none); new note kills same-group voices

    void noteOn (int note, int velocity, int offset) override {
        const Zone* z = pick (note, velocity); if (z == nullptr) return;
        if (chokeGroup) { const int g = chokeGroup (note); if (g != 0) for (auto& v : voices) if (v.active && v.group == g) v.release (sr, 0.02f); }
        Voice* v = nullptr;
        for (auto& c : voices) if (! c.active) { v = &c; break; }
        if (v == nullptr) { v = &voices[0]; for (auto& c : voices) if (c.age > v->age) v = &c; }   // steal oldest
        v->zone = z; v->note = note; v->pos = 0; v->active = true; v->releasing = false; v->age = 0; v->delay = offset;
        v->group = chokeGroup ? chokeGroup (note) : 0;
        v->inc = z->sr / sr * (z->keytrack ? std::pow (2.0, (note - z->root) / 12.0) : 1.0);
        const float vr = (float) velocity / (float) juce::jmax (1, z->vel);
        v->gain = gainTrim * std::pow ((float) velocity / 127.0f, 0.6f) * juce::jlimit (0.5f, 1.6f, std::pow (vr, 0.7f)) * 1.0f;
        v->env = 1.0f; v->relStep = 0;
    }
    void noteOff (int note) override { for (auto& v : voices) if (v.active && v.note == note && ! v.releasing) v.release (sr, releaseSeconds); }
    void allNotesOff (float seconds) override { for (auto& v : voices) if (v.active) v.release (sr, seconds); }

    void render (float* out, int n) override {
        for (auto& v : voices) {
            if (! v.active) continue;
            const float* d = v.zone->data.getReadPointer (0); const int len = v.zone->data.getNumSamples();
            int i = 0;
            if (v.delay > 0) { i = juce::jmin (n, v.delay); v.delay -= i; if (i >= n) continue; }
            for (; i < n; ++i) {
                const int p = (int) v.pos; if (p + 1 >= len) { v.active = false; break; }
                const float fr = (float) (v.pos - p);
                float s = d[p] + fr * (d[p + 1] - d[p]);
                // 1.5 ms fade-in removes any DC step at sample start
                const float fade = v.pos < 72 ? (float) v.pos / 72.0f : 1.0f;
                out[i] += s * v.gain * v.env * fade;
                v.pos += v.inc;
                if (v.releasing) { v.env -= v.relStep; if (v.env <= 0) { v.active = false; break; } }
            }
            v.age += n;
        }
    }
private:
    struct Voice {
        const Zone* zone = nullptr; int note = 0, delay = 0, group = 0; double pos = 0, inc = 1; float gain = 1, env = 1, relStep = 0;
        bool active = false, releasing = false; int64_t age = 0;
        void release (double sr, float s) { if (releasing) return; releasing = true; relStep = 1.0f / juce::jmax (1.0f, (float) (s * sr)); }
    };
    std::array<Voice, 48> voices;
    const ZoneSet* zoneSet = nullptr; std::vector<int> rr;
    double sr = 48000;

    const Zone* pick (int note, int vel) {
        if (empty()) return nullptr;
        // nearest note first (exact for drums), then nearest velocity layer; round robin within the chosen group
        float best = 1e9f; const Zone* first = nullptr;
        for (auto& z : zoneSet->zones) {
            if (! z->keytrack && z->root != note) continue;
            const float sc = (float) std::abs (z->root - note) * 40.0f + (float) std::abs (z->vel - vel);
            if (sc < best - 0.01f) { best = sc; first = z.get(); }
        }
        if (first == nullptr) return nullptr;
        int count = 0; for (auto& z : zoneSet->zones) if (z->group == first->group) ++count;
        int idx = rr[(size_t) first->group]++ % count;
        for (auto& z : zoneSet->zones) if (z->group == first->group && idx-- == 0) return z.get();
        return first;
    }
};

/** Loads samples.map (dest|note|vel|keytrack) from the first existing bundled samples folder, then merges the
    per-user downloaded packs (userSamplesDir()/samples.map, paths "<pack>/drums/x.wav" relative to that folder).
    The bundled set loads first and on its own, so a missing or broken user map never costs the built-in sounds.
    load() does file I/O only (no shared state): safe to run on a background thread, then hand the finished
    library to AudioEngine::installLibrary() on the message thread. */
struct SampleLibrary {
    ZoneSet drums, bass, keys; juce::File folder; int loaded = 0, loadedUser = 0;
    /** Per-user download root: <userApplicationData>/Ftheband/samples (one sub-folder per pack). */
    static juce::File userSamplesDir() {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Ftheband").getChildFile ("samples");
    }
    static juce::Array<juce::File> searchPaths() {
        juce::Array<juce::File> p;
        auto env = juce::SystemStats::getEnvironmentVariable ("SESSIONJAM_SAMPLES", {});
        if (env.isNotEmpty()) p.add (juce::File (env));
        auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        p.add (exeDir.getChildFile ("samples"));
        p.add (exeDir.getParentDirectory().getChildFile ("samples"));
       #ifdef SESSIONJAM_SAMPLES_DIR
        p.add (juce::File (SESSIONJAM_SAMPLES_DIR));
       #endif
        return p;
    }
    /** "drums/x.wav" or "<pack>/drums/x.wav" -> its ZoneSet (default keys). */
    ZoneSet& setFor (const juce::String& rel) {
        auto parts = juce::StringArray::fromTokens (rel.replaceCharacter ('\\', '/'), "/", "");
        for (int i = 0; i < parts.size() - 1 && i < 2; ++i) {
            if (parts[i] == "drums") return drums;
            if (parts[i] == "bass")  return bass;
            if (parts[i] == "keys")  return keys;
        }
        return keys;
    }
    int loadMap (const juce::File& dir, juce::AudioFormatManager& fm) {
        int n = 0;
        juce::StringArray lines; dir.getChildFile ("samples.map").readLines (lines);
        for (auto& l : lines) {
            auto t = juce::StringArray::fromTokens (l, "|", ""); if (t.size() < 4 || l.startsWith ("#")) continue;
            auto file = dir.getChildFile (t[0]);
            if (! file.isAChildOf (dir)) continue;   // no "../" escapes from a downloaded map
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
            if (r == nullptr || r->lengthInSamples <= 1) continue;
            auto z = std::make_unique<SampleZone>();
            const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 6));
            z->data.setSize (1, len + 1); z->data.clear();
            juce::AudioBuffer<float> tmp ((int) r->numChannels, len); r->read (&tmp, 0, len, 0, true, true);
            for (int c = 0; c < tmp.getNumChannels(); ++c) z->data.addFrom (0, 0, tmp, c, 0, len, 1.0f / (float) tmp.getNumChannels());
            z->root = juce::jlimit (0, 127, t[1].getIntValue()); z->vel = juce::jlimit (1, 127, t[2].getIntValue());
            z->keytrack = t[3].getIntValue() != 0; z->sr = r->sampleRate;
            // trim leading silence (keeps hits on the grid)
            const float* d = z->data.getReadPointer (0); int s0 = 0; while (s0 < len - 1 && std::abs (d[s0]) < 0.003f) ++s0;
            s0 = juce::jmax (0, s0 - 16);
            if (s0 > 0) { juce::AudioBuffer<float> b (1, len + 1 - s0); b.copyFrom (0, 0, z->data, 0, s0, len + 1 - s0); z->data = std::move (b); }
            setFor (t[0]).zones.push_back (std::move (z)); ++n;
        }
        return n;
    }
    bool load (bool includeUserPacks = true) {
        for (auto& f : searchPaths()) if (f.getChildFile ("samples.map").existsAsFile()) { folder = f; break; }
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        if (folder != juce::File()) loaded += loadMap (folder, fm);
        if (includeUserPacks && userSamplesDir().getChildFile ("samples.map").existsAsFile()) {
            loadedUser = loadMap (userSamplesDir(), fm); loaded += loadedUser;
        }
        for (auto* s : { &drums, &bass, &keys }) s->finalise();
        return loaded > 0;
    }
};
} // namespace sj
