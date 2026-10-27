// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Ftheband contributors
#pragma once
#include <JuceHeader.h>
#include "Engine/Sampler.h"

namespace sj {
/** Runtime sample packs ("Get more sounds").
    Catalog: JSON at CatalogUrl (see catalog/README.md): a top-level array (or {"packs": [...]}) of packs with id, name,
    category, licence, licence_url, credit, upstream_url, size_bytes, file_count and entries, each either an object
    {url, dest, root_note, velocity, keytrack, sha256, size} or a manifest string "url|dest|note|vel|keytrack|sha256".
    dest is "packs/<id>/[<category>/]file". Every file's sha256 is required and verified; an optional pack-level
    "sha256" (all files concatenated in entry order) is verified too. Packs install into SampleLibrary::userSamplesDir()/<pack id>/ as:
        <files, dest minus "packs/<id>/">, pack.map (dest|note|vel|keytrack|set), pack.json (catalog metadata, for credits)
    and userSamplesDir()/samples.map is regenerated from every installed pack.map ("<id>/<dest>|note|vel|keytrack|set", set = drums/bass/keys chair). */
static constexpr const char* CatalogUrl = "https://raw.githubusercontent.com/thenotagjesus/ftheband/main/catalog/packs.json";

struct PackEntry { juce::String url, dest, sha256; int note = 60, vel = 64; bool keytrack = true; juce::int64 size = 0; };
struct PackInfo {
    juce::String id, name, licence, credit, sha256, description, category, licenceUrl, upstreamUrl; juce::int64 size = 0;
    std::vector<PackEntry> entries;
    bool requiresAttribution() const { return licence.containsIgnoreCase ("BY"); }
    juce::var toVar() const;
};

namespace SamplePacks {
    /** Only CC0 / CC-BY family (and public domain) licences are accepted; NC, ND, SA and unknown licences are rejected. */
    bool licenceAllowed (const juce::String& licence);
    /** Safe folder name for a pack id ([a-z0-9-_], max 48). Empty if unusable. */
    juce::String sanitiseId (const juce::String&);
    /** Parse catalog JSON. Packs with bad licences or malformed entries are skipped (reported in `warnings`). */
    std::vector<PackInfo> parseCatalog (const juce::String& json, juce::StringArray& warnings);
    /** Pack category -> sample set feeding a band chair: drums*, perc -> drums; bass -> bass; everything else -> keys. */
    juce::String setForCategory (const juce::String&);
    bool isInstalled (const juce::String& id);
    /** Installed packs' metadata (from each pack.json). */
    std::vector<PackInfo> installed();
    /** Rebuild userSamplesDir()/samples.map from installed packs. Returns the number of lines written. */
    int regenerateMap();
    /** Credit lines for every installed pack (CC-BY ones are mandatory; CC0 shown as a courtesy). */
    juce::String attributionText();
    juce::String sha256OfFile (const juce::File&);
    juce::String formatSize (juce::int64 bytes);
}

/** Fetches the catalog on a background thread; result delivered on the message thread. */
class CatalogFetcher : private juce::Thread {
public:
    using Callback = std::function<void (bool ok, std::vector<PackInfo>, juce::String error)>;
    CatalogFetcher() : juce::Thread ("CatalogFetch") {}
    ~CatalogFetcher() override { alive->store (false); stopThread (12000); }
    void fetch (Callback cb, juce::String url = CatalogUrl);
    bool busy() const { return isThreadRunning(); }
private:
    void run() override;
    Callback callback; juce::String fetchUrl;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};

/** Downloads + verifies + installs one pack, regenerates samples.map and loads a fresh SampleLibrary, all on its own
    thread. onDone (message thread) receives the new library (nullptr on failure) for AudioEngine::installLibrary(). */
class PackInstaller : private juce::Thread {
public:
    using Done = std::function<void (bool ok, juce::String message, std::shared_ptr<SampleLibrary> lib)>;
    PackInstaller() : juce::Thread ("PackInstall") {}
    ~PackInstaller() override { alive->store (false); cancel(); stopThread (20000); }
    bool start (const PackInfo& p, Done d);
    void cancel() { signalThreadShouldExit(); }
    bool busy() const { return isThreadRunning(); }
    float progress() const { return prog.load(); }
    juce::String status() const { const juce::ScopedLock sl (lock); return stat; }
    /** Remove an installed pack and reload (synchronous file ops, library load on a thread). */
    bool startRemove (const juce::String& id, Done d);
private:
    void run() override;
    bool install (juce::String& msg);
    void setStatus (const juce::String& s) { const juce::ScopedLock sl (lock); stat = s; }
    PackInfo pack; Done done; bool removing = false;
    std::atomic<float> prog { 0 }; juce::CriticalSection lock; juce::String stat;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
} // namespace sj
