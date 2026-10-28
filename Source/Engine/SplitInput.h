// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <JuceHeader.h>
#include "AudioEngine.h"

namespace sj {

/** Split-input mode: an input-only device (the USB guitar interface, or a microphone) runs on its own thread and
    clock (Windows: WASAPI, 48 kHz); the output stays on the AudioDeviceManager (ASIO4ALL with only the Realtek
    enabled, or WASAPI). A DriftResampler in the engine bridges the two clocks. Message thread only.
    Latency: when a low-latency type is offered (Windows: "Windows Audio (Low Latency Mode)", IAudioClient3), it is
    tried first at the smallest period >= 64 samples; on any failure it falls back to the plain type at the device's
    default period (10 ms on WASAPI shared). */
class SplitInput : private juce::AudioIODeviceCallback {
public:
    enum class Target { Guitar, Mic };
    SplitInput (AudioEngine& e, Target t = Target::Guitar) : engine (e), target (t) {}
    ~SplitInput() override { close(); }

    DriftResampler& drift() { return target == Target::Guitar ? engine.driftIn : engine.driftMic; }

    /** (Re)open `inName` at the output's rate. outBlock = output device buffer size. lowLatency (may be null) is tried
        first. Returns an error string ("" = OK). Safe to call while the output device is running. */
    juce::String open (juce::AudioIODeviceType& type, const juce::String& inName, double outRate, int outBlock,
                       juce::AudioIODeviceType* lowLatency = nullptr)
    {
        close();
        juce::String firstErr;
        if (lowLatency != nullptr && lowLatency != &type) {
            firstErr = tryOpen (*lowLatency, inName, outRate, true);
            if (firstErr.isEmpty()) return start (outRate, outBlock);
        }
        auto err = tryOpen (type, inName, outRate, false);
        if (err.isNotEmpty()) return firstErr.isNotEmpty() ? err + " (low-latency mode: " + firstErr + ")" : err;
        lowLatencyFallback = firstErr;
        return start (outRate, outBlock);
    }
    void close()
    {
        activate (false);
        if (device != nullptr) { device->stop(); device->close(); device.reset(); }
    }
    bool isOpen() const { return device != nullptr && device->isOpen(); }
    bool isPlaying() const { return device != nullptr && device->isPlaying(); }
    int  xruns() const { return device != nullptr ? juce::jmax (0, device->getXRunCount()) : 0; }
    int  inputBlock() const { return device != nullptr ? device->getCurrentBufferSizeSamples() : 0; }
    int  inputLatency() const { return device != nullptr ? device->getInputLatencyInSamples() : 0; }
    juce::String typeName, deviceName, lowLatencyFallback;   // fallback = why low-latency mode was not used ("" = it was, or not offered)
    std::atomic<bool> errored { false };

private:
    juce::String tryOpen (juce::AudioIODeviceType& type, const juce::String& inName, double outRate, bool smallest)
    {
        type.scanForDevices();
        if (! type.getDeviceNames (true).contains (inName)) return "input device not found: " + inName;
        device.reset (type.createDevice ({}, inName));
        if (device == nullptr) return "could not create " + inName;
        int bs = device->getDefaultBufferSize();
        if (smallest) for (int s : device->getAvailableBufferSizes()) if (s >= 64) { bs = s; break; }   // sorted ascending
        juce::BigInteger inCh; inCh.setBit (0);
        auto err = device->open (inCh, {}, outRate, bs);
        if (err.isEmpty() && std::abs (device->getCurrentSampleRate() - outRate) > 1.0)
            err = "input runs at " + juce::String (device->getCurrentSampleRate()) + " Hz, output at " + juce::String (outRate)
                + " Hz; set both to 48 kHz in Windows Sound settings";
        if (err.isNotEmpty()) { device->close(); device.reset(); return err; }
        typeName = type.getTypeName(); deviceName = inName;
        return {};
    }
    juce::String start (double outRate, int outBlock)
    {
        // both sides quiet: input not started, engine not pulling
        drift().prepare (outRate, outBlock, device->getCurrentBufferSizeSamples());
        activate (true);
        device->start (this);
        return {};
    }
    void activate (bool on) { if (target == Target::Guitar) engine.setSplitInputActive (on); else engine.setMicInputActive (on); }
    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
    void audioDeviceError (const juce::String&) override { errored = true; }
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        juce::ScopedNoDenormals nd;
        drift().push (numIn > 0 ? in[0] : nullptr, n);
        for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);
    }
    AudioEngine& engine;
    Target target;
    std::unique_ptr<juce::AudioIODevice> device;
};

} // namespace sj
