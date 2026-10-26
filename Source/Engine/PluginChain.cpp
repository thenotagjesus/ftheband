// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Session Jam contributors
#include "Engine/PluginChain.h"

namespace sj {
static constexpr int maxPluginChannels = 8;

PluginChain::PluginChain()
{
    formatManager.addDefaultFormats();  // VST3 always; VST (2.x, which also loads VST1) when JUCE_PLUGINHOST_VST=1
    scratch.setSize (maxPluginChannels, 512);
    midi.ensureSize (256);
    dry.assign ((size_t) DryLen, 0.0f);
}
PluginChain::~PluginChain() { for (int i = 0; i < NumSlots; ++i) unload (i); }

juce::String PluginChain::slotName (int s) const
{ auto* p = getInstance (s); return p ? p->getName() : juce::String ("(empty)"); }

void PluginChain::prepareInstance (juce::AudioPluginInstance& p)
{
    // Prefer stereo in/out (most amp sims), fall back to mono, else whatever the plugin defaults to.
    for (auto set : { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono() }) {
        auto layout = p.getBusesLayout();
        if (layout.inputBuses.isEmpty() || layout.outputBuses.isEmpty()) break;
        layout.inputBuses.getReference (0) = set; layout.outputBuses.getReference (0) = set;
        if (p.setBusesLayout (layout)) break;
    }
    p.setRateAndBufferSizeDetails (sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);
}

bool PluginChain::loadPlugin (int s, const juce::PluginDescription& d, juce::String& error)
{
    auto inst = formatManager.createPluginInstance (d, sampleRate, blockSize, error);
    if (inst == nullptr) return false;
    if (inst->getTotalNumInputChannels() > maxPluginChannels || inst->getTotalNumOutputChannels() > maxPluginChannels) {
        error = "Plugin has too many channels"; return false;
    }
    if (prepared) prepareInstance (*inst);
    inst->addListener (this);
    std::unique_ptr<juce::AudioPluginInstance> old;
    if (onSlotWillChange) onSlotWillChange (s);
    { const juce::SpinLock::ScopedLockType sl (lock); old = std::move (slots[(size_t) s].instance); slots[(size_t) s].instance = std::move (inst); }
    if (old != nullptr) { old->removeListener (this); if (prepared) old->releaseResources(); }
    latencyDirty = true;
    return true;
}

void PluginChain::unload (int s)
{
    if (slots[(size_t) s].instance == nullptr) return;
    if (onSlotWillChange) onSlotWillChange (s);
    std::unique_ptr<juce::AudioPluginInstance> old;
    { const juce::SpinLock::ScopedLockType sl (lock); old = std::move (slots[(size_t) s].instance); }
    old->removeListener (this);
    if (prepared) old->releaseResources();
    latencyDirty = true;
}

int PluginChain::getTotalLatencySamples() const
{
    int total = 0;
    for (auto& s : slots) if (s.instance && ! s.bypassed.load()) total += s.instance->getLatencySamples();
    return total;
}

void PluginChain::prepare (double sr, int bs)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    sampleRate = sr; blockSize = bs;
    scratch.setSize (maxPluginChannels, bs, false, true, true);   // == prepared block size
    for (auto& s : slots) if (s.instance) { if (prepared) s.instance->releaseResources(); prepareInstance (*s.instance); }
    prepared = true;
}

void PluginChain::release()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    if (! prepared) return;
    for (auto& s : slots) if (s.instance) s.instance->releaseResources();
    prepared = false;
}

void PluginChain::process (float* mono, int n)
{
    // keep a dry history (for the delay-matched pass-through)
    for (int i = 0; i < n; ++i) dry[(size_t) ((dryPos + i) & (DryLen - 1))] = mono[i];
    const int startPos = dryPos; dryPos = (dryPos + n) & (DryLen - 1);

    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (sl.isLocked() && prepared) {
        int lat = 0; for (auto& s : slots) if (s.instance && ! s.bypassed.load()) lat += s.instance->getLatencySamples();
        lastLatency = juce::jlimit (0, DryLen / 2, lat);
        const int chunk = scratch.getNumSamples();   // any n: process in scratch-sized pieces, never skip
        for (int pos = 0; pos < n; pos += chunk) runSlots (mono + pos, juce::jmin (chunk, n - pos));
        return;
    }
    // swap in progress: dry signal delayed by the latency the chain had, so timing does not jump
    for (int i = 0; i < n; ++i) mono[i] = dry[(size_t) ((startPos + i - lastLatency) & (DryLen - 1))];
}

void PluginChain::runSlots (float* mono, int n)
{
    for (auto& s : slots) {
        auto* p = s.instance.get();
        if (p == nullptr || s.bypassed.load()) continue;
        const int nIn = p->getTotalNumInputChannels(), nOut = p->getTotalNumOutputChannels();
        const int nCh = juce::jmax (nIn, nOut, 1);
        for (int c = 0; c < nCh; ++c)
            if (c < juce::jmax (nIn, 1)) scratch.copyFrom (c, 0, mono, n); else scratch.clear (c, 0, n);
        juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), nCh, n);
        midi.clear();
        p->processBlock (view, midi);
        if (nOut <= 0) continue;
        juce::FloatVectorOperations::copy (mono, view.getReadPointer (0), n);
        for (int c = 1; c < nOut; ++c) juce::FloatVectorOperations::add (mono, view.getReadPointer (c), n);
        if (nOut > 1) juce::FloatVectorOperations::multiply (mono, 1.0f / (float) nOut, n);
    }
}

std::unique_ptr<juce::XmlElement> PluginChain::createStateXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("PluginChain");
    for (int i = 0; i < NumSlots; ++i) {
        auto* e = xml->createNewChildElement ("Slot");
        e->setAttribute ("index", i);
        e->setAttribute ("bypassed", slots[(size_t) i].bypassed.load());
        if (auto* p = slots[(size_t) i].instance.get()) {
            if (auto d = p->getPluginDescription().createXml()) e->addChildElement (d.release());
            juce::MemoryBlock mb; p->getStateInformation (mb);
            e->createNewChildElement ("State")->addTextElement (mb.toBase64Encoding());
        }
    }
    return xml;
}

void PluginChain::restoreFromXml (const juce::XmlElement& xml, juce::StringArray& errors)
{
    for (auto* e : xml.getChildWithTagNameIterator ("Slot")) {
        const int i = e->getIntAttribute ("index", -1);
        if (i < 0 || i >= NumSlots) continue;
        setBypassed (i, e->getBoolAttribute ("bypassed"));
        auto* descXml = e->getChildByName ("PLUGIN");
        juce::PluginDescription d;
        if (descXml == nullptr || ! d.loadFromXml (*descXml)) continue;
        juce::String err;
        if (! loadPlugin (i, d, err)) { errors.add (d.name + ": " + err); continue; }
        if (auto* st = e->getChildByName ("State")) {
            juce::MemoryBlock mb;
            if (mb.fromBase64Encoding (st->getAllSubText())) getInstance (i)->setStateInformation (mb.getData(), (int) mb.getSize());
        }
    }
}
} // namespace sj
