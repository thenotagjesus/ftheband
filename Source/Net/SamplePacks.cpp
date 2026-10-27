// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Ftheband contributors
#include "Net/SamplePacks.h"

namespace sj {
static constexpr juce::int64 MaxPackBytes = (juce::int64) 256 * 1024 * 1024;   // hard cap whatever the catalog says (also the in-memory verify limit)

juce::var PackInfo::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", id); o->setProperty ("name", name); o->setProperty ("licence", licence);
    o->setProperty ("credit", credit); o->setProperty ("description", description);
    o->setProperty ("size", size); o->setProperty ("sha256", sha256);
    o->setProperty ("category", category); o->setProperty ("licence_url", licenceUrl); o->setProperty ("upstream_url", upstreamUrl);
    return juce::var (o);
}

namespace SamplePacks {
bool licenceAllowed (const juce::String& licIn)
{
    auto l = licIn.toUpperCase().removeCharacters (" _.");
    if (l.contains ("NC") || l.contains ("ND") || l.contains ("NONCOMMERCIAL") || l.contains ("NODERIV")) return false;
    if (l.startsWith ("CC0") || l == "PUBLICDOMAIN" || l == "PD") return true;
    // CC-BY 3.0 / 4.0 (attribution only). CC-BY-SA is not on the allow-list (copyleft on samples; ask first).
    if (l.startsWith ("CC-BY") || l.startsWith ("CCBY")) return ! l.contains ("SA");
    return false;
}

juce::String sanitiseId (const juce::String& s)
{
    auto r = s.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789-_").substring (0, 48);
    return (r == "." || r == "..") ? juce::String() : r;
}

static bool safeDest (const juce::String& d)
{
    if (d.isEmpty() || d.startsWithChar ('/') || d.containsChar ('\\') || d.contains ("..") || d.containsChar (':')) return false;
    auto ext = d.fromLastOccurrenceOf (".", false, false).toLowerCase();
    return ext == "wav" || ext == "flac" || ext == "aif" || ext == "aiff" || ext == "ogg";
}

juce::String setForCategory (const juce::String& c)
{
    auto l = c.toLowerCase();
    if (l.startsWith ("drum") || l.startsWith ("perc")) return "drums";
    if (l.startsWith ("bass")) return "bass";
    return "keys";
}

static juce::var prop (const juce::var& v, std::initializer_list<const char*> names)
{
    for (auto* n : names) if (v.hasProperty (n)) return v.getProperty (n, {});
    return {};
}

std::vector<PackInfo> parseCatalog (const juce::String& json, juce::StringArray& warnings)
{
    std::vector<PackInfo> out;
    auto root = juce::JSON::parse (json);
    auto* packs = root.isArray() ? root.getArray() : root.getProperty ("packs", {}).getArray();
    if (packs == nullptr) { warnings.add ("Catalog has no pack list"); return out; }
    for (auto& v : *packs) {
        PackInfo p;
        p.name = prop (v, { "name" }).toString().trim();
        p.id = sanitiseId (v.getProperty ("id", p.name.replaceCharacter (' ', '-')).toString());
        p.licence = prop (v, { "licence", "license" }).toString().trim();
        p.licenceUrl = prop (v, { "licence_url", "license_url" }).toString();
        p.upstreamUrl = prop (v, { "upstream_url" }).toString();
        p.credit = prop (v, { "credit" }).toString().trim();
        p.category = prop (v, { "category" }).toString().trim();
        p.description = prop (v, { "description" }).toString();
        p.size = (juce::int64) prop (v, { "size_bytes", "size" });
        p.sha256 = prop (v, { "sha256" }).toString().trim().toLowerCase();
        if (p.name.isEmpty() || p.id.isEmpty()) { warnings.add ("Skipped a pack with no name/id"); continue; }
        if (! licenceAllowed (p.licence)) { warnings.add (p.name + ": licence \"" + p.licence + "\" is not CC0/CC-BY, skipped"); continue; }
        if (p.requiresAttribution() && p.credit.isEmpty()) { warnings.add (p.name + ": CC-BY pack without a credit line, skipped"); continue; }
        if (p.sha256.isNotEmpty() && p.sha256.length() != 64) { warnings.add (p.name + ": bad pack sha256, skipped"); continue; }
        const juce::String prefix = "packs/" + p.id + "/";
        bool bad = false;
        if (auto* es = prop (v, { "entries" }).getArray())
            for (auto& e : *es) {
                PackEntry pe;
                if (e.isString()) {
                    auto t = juce::StringArray::fromTokens (e.toString().trim(), "|", "");
                    if (t.size() < 5) { bad = true; break; }
                    pe.url = t[0].trim(); pe.dest = t[1].trim(); pe.note = t[2].getIntValue(); pe.vel = t[3].getIntValue();
                    pe.keytrack = t[4].getIntValue() != 0; if (t.size() > 5) pe.sha256 = t[5].trim().toLowerCase();
                } else {
                    pe.url = prop (e, { "url" }).toString().trim(); pe.dest = prop (e, { "dest" }).toString().trim();
                    pe.note = (int) prop (e, { "root_note", "note" }); pe.vel = (int) prop (e, { "velocity", "vel" });
                    pe.keytrack = (int) prop (e, { "keytrack" }) != 0;
                    pe.sha256 = prop (e, { "sha256" }).toString().trim().toLowerCase(); pe.size = (juce::int64) prop (e, { "size" });
                }
                if (pe.dest.startsWith (prefix)) pe.dest = pe.dest.substring (prefix.length());   // stored under <userSamples>/<id>/
                if (! pe.url.startsWithIgnoreCase ("https://") || ! safeDest (pe.dest) || pe.sha256.length() != 64) { bad = true; break; }
                p.entries.push_back (pe);
            }
        if (bad || p.entries.empty()) { warnings.add (p.name + ": malformed entries (https URL, safe dest and sha256 required), skipped"); continue; }
        if (p.size <= 0) for (auto& e : p.entries) p.size += e.size;
        out.push_back (std::move (p));
    }
    return out;
}

bool isInstalled (const juce::String& id)
{ return SampleLibrary::userSamplesDir().getChildFile (id).getChildFile ("pack.map").existsAsFile(); }

std::vector<PackInfo> installed()
{
    std::vector<PackInfo> out;
    for (auto& d : SampleLibrary::userSamplesDir().findChildFiles (juce::File::findDirectories, false)) {
        if (! d.getChildFile ("pack.map").existsAsFile()) continue;
        auto v = juce::JSON::parse (d.getChildFile ("pack.json"));
        PackInfo p; p.id = d.getFileName();
        p.name = v.getProperty ("name", p.id).toString(); p.licence = v.getProperty ("licence", "").toString();
        p.credit = v.getProperty ("credit", "").toString(); p.size = (juce::int64) v.getProperty ("size", 0);
        p.category = v.getProperty ("category", "").toString(); p.licenceUrl = v.getProperty ("licence_url", "").toString();
        out.push_back (p);
    }
    std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.id < b.id; });
    return out;
}

int regenerateMap()
{
    auto root = SampleLibrary::userSamplesDir(); root.createDirectory();
    juce::String map ("# Generated by Ftheband from installed packs (<pack>/<dest>|note|vel|keytrack). Do not edit.\n");
    int n = 0;
    for (auto& p : installed()) {
        juce::StringArray lines; root.getChildFile (p.id).getChildFile ("pack.map").readLines (lines);
        for (auto& l : lines) if (l.isNotEmpty() && ! l.startsWith ("#")) { map << p.id << "/" << l << "\n"; ++n; }
    }
    auto target = root.getChildFile ("samples.map");
    juce::TemporaryFile tmp (target);
    if (tmp.getFile().replaceWithText (map)) tmp.overwriteTargetFileWithTemporary();   // atomic replace
    return n;
}

juce::String attributionText()
{
    juce::StringArray s;
    for (auto& p : installed())
        s.add (p.name + ": " + (p.credit.isNotEmpty() ? p.credit : juce::String ("-")) + " (" + p.licence + ")"
               + (p.licenceUrl.isNotEmpty() ? "  " + p.licenceUrl : juce::String()));
    return s.joinIntoString ("\n");
}

juce::String sha256OfFile (const juce::File& f)
{
    juce::FileInputStream in (f);
    return in.openedOk() ? juce::SHA256 (in, -1).toHexString().toLowerCase() : juce::String();
}

juce::String formatSize (juce::int64 b)
{
    if (b <= 0) return "?";
    if (b < 1024 * 1024) return juce::String ((double) b / 1024.0, 0) + " KB";
    return juce::String ((double) b / (1024.0 * 1024.0), 1) + " MB";
}
} // namespace SamplePacks

// ------------------------------------------------------------------------------------------------ catalog
void CatalogFetcher::fetch (Callback cb, juce::String url)
{
    if (isThreadRunning()) return;
    callback = std::move (cb); fetchUrl = std::move (url);
    startThread();
}

void CatalogFetcher::run()
{
    int status = 0; juce::String body, err;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs (10000).withStatusCode (&status).withNumRedirectsToFollow (3);
    if (auto in = juce::URL (fetchUrl).createInputStream (opts)) {
        if (status != 0 && status != 200) err = "Catalog server answered HTTP " + juce::String (status);
        else body = in->readEntireStreamAsString();
    } else err = "Could not reach the sound catalog (offline?). The built-in sounds still work.";
    std::vector<PackInfo> packs; juce::StringArray warnings; bool ok = err.isEmpty();
    if (ok) {
        if (juce::JSON::parse (body).isVoid()) { ok = false; err = "The catalog could not be read (bad JSON)."; }
        else { packs = SamplePacks::parseCatalog (body, warnings); err = warnings.joinIntoString ("; "); }   // warnings are informational
    }
    auto cb = callback; auto flag = alive;
    juce::MessageManager::callAsync ([cb, flag, ok, packs, err] { if (flag->load() && cb) cb (ok, packs, err); });
}

// ------------------------------------------------------------------------------------------------ install
bool PackInstaller::start (const PackInfo& p, Done d)
{
    if (isThreadRunning()) return false;
    pack = p; done = std::move (d); removing = false; prog = 0; setStatus ("Starting");
    startThread(); return true;
}
bool PackInstaller::startRemove (const juce::String& id, Done d)
{
    if (isThreadRunning() || SamplePacks::sanitiseId (id) != id || id.isEmpty()) return false;
    pack = {}; pack.id = id; done = std::move (d); removing = true; prog = 0; setStatus ("Removing");
    startThread(); return true;
}

static bool download (const juce::String& url, const juce::File& out, juce::int64 budget, juce::Thread& t,
                      std::function<void (juce::int64)> onBytes, juce::String& err)
{
    int status = 0;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs (15000).withStatusCode (&status).withNumRedirectsToFollow (3);
    auto in = juce::URL (url).createInputStream (opts);
    if (in == nullptr) { err = "network error"; return false; }
    if (status != 0 && status != 200) { err = "HTTP " + juce::String (status); return false; }
    out.getParentDirectory().createDirectory();
    juce::FileOutputStream os (out);
    if (! os.openedOk()) { err = "cannot write " + out.getFullPathName(); return false; }
    os.setPosition (0); os.truncate();
    juce::HeapBlock<char> buf (65536); juce::int64 total = 0;
    for (;;) {
        if (t.threadShouldExit()) { err = "cancelled"; return false; }
        const int n = in->read (buf, 65536);
        if (n < 0) { err = "read error"; return false; }
        if (n == 0) { if (in->isExhausted()) break; continue; }
        total += n; if (total > budget) { err = "file larger than the catalog says"; return false; }
        if (! os.write (buf, (size_t) n)) { err = "disk full?"; return false; }
        onBytes (n);
    }
    os.flush();
    if (total == 0) { err = "empty download"; return false; }
    return true;
}

bool PackInstaller::install (juce::String& msg)
{
    auto root = SampleLibrary::userSamplesDir();
    if (! root.createDirectory()) { msg = "Cannot create " + root.getFullPathName(); return false; }
    auto staging = root.getChildFile ("." + pack.id + ".partial");
    staging.deleteRecursively(); staging.createDirectory();
    const juce::int64 budget = juce::jmin (MaxPackBytes, pack.size > 0 ? pack.size + pack.size / 4 + 65536 : MaxPackBytes);
    juce::int64 got = 0; juce::String map;
    for (size_t i = 0; i < pack.entries.size(); ++i) {
        auto& e = pack.entries[i];
        setStatus ("Downloading " + juce::String ((int) i + 1) + "/" + juce::String ((int) pack.entries.size()) + ": " + e.dest);
        auto f = staging.getChildFile (e.dest);
        if (! f.isAChildOf (staging)) { msg = "Bad path in catalog: " + e.dest; return false; }
        juce::String err;
        const juce::int64 fileBudget = e.size > 0 ? e.size : budget - got;
        if (! download (e.url, f, juce::jmin (fileBudget, budget - got), *this, [&] (juce::int64 n) {
                got += n; prog = pack.size > 0 ? juce::jlimit (0.0f, 0.95f, (float) got / (float) pack.size)
                                                : 0.95f * (float) i / (float) pack.entries.size(); }, err)) {
            msg = "Download failed (" + err + "): " + e.dest; return false;
        }
        if (e.size > 0 && f.getSize() != e.size) { msg = "Size mismatch: " + e.dest; return false; }
        if (SamplePacks::sha256OfFile (f) != e.sha256) { msg = "Checksum mismatch: " + e.dest; return false; }
        map << e.dest << "|" << e.note << "|" << e.vel << "|" << (e.keytrack ? 1 : 0) << "|" << SamplePacks::setForCategory (pack.category) << "\n";
    }
    if (pack.sha256.isNotEmpty()) {
        setStatus ("Verifying pack sha256");   // optional: SHA-256 over all files concatenated in entry order
        juce::MemoryOutputStream concat;
        juce::int64 totalBytes = 0;
        for (auto& e : pack.entries) totalBytes += staging.getChildFile (e.dest).getSize();
        juce::String h;
        if (totalBytes <= MaxPackBytes) {
            for (auto& e : pack.entries) { juce::FileInputStream in (staging.getChildFile (e.dest)); concat.writeFromInputStream (in, -1); }
            h = juce::SHA256 (concat.getData(), concat.getDataSize()).toHexString().toLowerCase();
        } else { msg = "Pack too large to verify"; return false; }
        if (h != pack.sha256) { msg = "Pack checksum mismatch (expected " + pack.sha256.substring (0, 12) + "..., got " + h.substring (0, 12) + "...)"; return false; }
    }
    staging.getChildFile ("pack.map").replaceWithText ("# dest|note|vel|keytrack\n" + map);
    staging.getChildFile ("pack.json").replaceWithText (juce::JSON::toString (pack.toVar()));
    auto dest = root.getChildFile (pack.id);
    dest.deleteRecursively();
    if (! staging.moveFileTo (dest)) { msg = "Could not move the pack into place"; return false; }
    return true;
}

void PackInstaller::run()
{
    bool ok = true; juce::String msg;
    auto root = SampleLibrary::userSamplesDir();
    if (removing) {
        auto d = root.getChildFile (pack.id);
        ok = d.isAChildOf (root) && d.deleteRecursively();
        msg = ok ? "Removed " + pack.id : "Could not remove " + pack.id;
    } else {
        ok = install (msg);
        if (! ok) root.getChildFile ("." + pack.id + ".partial").deleteRecursively();
        else msg = "Installed " + pack.name;
    }
    std::shared_ptr<SampleLibrary> lib;
    if (ok && ! threadShouldExit()) {
        setStatus ("Updating samples.map"); SamplePacks::regenerateMap();
        setStatus ("Loading samples"); prog = 0.97f;
        lib = std::make_shared<SampleLibrary>();
        if (! lib->load()) { lib.reset(); msg << " (no samples could be loaded)"; }
    }
    prog = 1.0f; setStatus (msg);
    auto d = done; auto flag = alive;
    juce::MessageManager::callAsync ([d, flag, ok, msg, lib] { if (flag->load() && d) d (ok, msg, lib); });
}
} // namespace sj
