// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <JuceHeader.h>

namespace sj {
/** 4-slot insert chain hosted on the GuitarFX track (after the placeholder amp, before the GuitarFX stem tap).
    Slots are swapped on the message thread under a SpinLock; the audio thread only try-locks, so a load/unload
    never blocks audio: that block passes through dry, delayed by the chain's last reported latency so the
    signal never jumps in time during a swap. Mono track: fed to every plugin input channel,
    plugin output channels are averaged back to mono. */
class PluginChain : private juce::AudioProcessorListener {
public:
    static constexpr int NumSlots = 4;
    PluginChain();
    ~PluginChain() override;

    // ---- message thread
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    bool loadPlugin (int slot, const juce::PluginDescription&, juce::String& error);
    void unload (int slot);
    juce::AudioPluginInstance* getInstance (int slot) const { return slots[(size_t) slot].instance.get(); }
    juce::String slotName (int slot) const;
    void setBypassed (int slot, bool b) { slots[(size_t) slot].bypassed = b; latencyDirty = true; }
    /** Reported latency of one slot (0 when empty); counted in the total only when not bypassed. */
    int slotLatencySamples (int slot) const { auto* p = getInstance (slot); return p ? p->getLatencySamples() : 0; }
    /** Message thread: called after anything that may change getTotalLatencySamples(). */
    std::function<void()> onLatencyChanged;
    /** Message thread (poll from a Timer): fires onLatencyChanged if a slot changed, was (un)bypassed or a plugin
        reported a new latency (that notification may arrive on the audio thread, so it only sets a flag). */
    bool pollLatencyChange() { if (! latencyDirty.exchange (false)) return false; if (onLatencyChanged) onLatencyChanged(); return true; }
    bool isBypassed (int slot) const { return slots[(size_t) slot].bypassed.load(); }
    /** Sum of getLatencySamples() over active (loaded, non-bypassed) slots. */
    int getTotalLatencySamples() const;

    std::unique_ptr<juce::XmlElement> createStateXml() const;
    void restoreFromXml (const juce::XmlElement&, juce::StringArray& errors);

    // ---- device lifecycle (called from AudioEngine's audioDeviceAboutToStart / audioDeviceStopped)
    void prepare (double sampleRate, int maxBlockSize);
    void release();

    // ---- audio thread
    void process (float* mono, int numSamples);

    /** Called (message thread) before a slot's instance is deleted, so the UI can close its editor window. */
    std::function<void (int)> onSlotWillChange;

private:
    struct Slot { std::unique_ptr<juce::AudioPluginInstance> instance; std::atomic<bool> bypassed { false }; };
    std::array<Slot, NumSlots> slots;
    juce::SpinLock lock;
    juce::AudioBuffer<float> scratch;
    static constexpr int DryLen = 1 << 16;          // power of two; > max plugin latency + block
    std::vector<float> dry; int dryPos = 0; int lastLatency = 0;   // audio thread only
    void runSlots (float* mono, int n);
    juce::MidiBuffer midi;
    double sampleRate = 48000; int blockSize = 512; bool prepared = false;
    void prepareInstance (juce::AudioPluginInstance&);
    std::atomic<bool> latencyDirty { false };
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override { if (d.latencyChanged) latencyDirty = true; }
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
};
} // namespace sj
