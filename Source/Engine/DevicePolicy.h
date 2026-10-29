// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <JuceHeader.h>

namespace sj::devpolicy {

/** How FtheBand opens its audio output on Windows (header-only so the app and `FtheBand.exe --audio-probe` run the SAME code).
      shared     "Windows Audio"                      WASAPI shared mode (works everywhere, ~10 ms periods)
      lowlat     "Windows Audio (Low Latency Mode)"   WASAPI shared, IAudioClient3 small period
      exclusive  "Windows Audio (Exclusive Mode)"     WASAPI exclusive: the device is ours alone, buffers down to ~3 ms; no ASIO driver needed
    The built-in low-latency chain is exclusive -> low-latency shared -> plain shared, each one tried only if the one before failed.
    ("ASIO" below only names the third-party driver interface; FtheBand is not ASIO-certified. ASIO is a trademark and software of
    Steinberg Media Technologies GmbH.) */
using LogFn = std::function<void (const juce::String&)>;
inline const juce::String SharedType = "Windows Audio", ExclusiveType = "Windows Audio (Exclusive Mode)", LowLatType = "Windows Audio (Low Latency Mode)";

struct Opened {
    bool ok = false;
    juce::String type, output, input, how;   // how = one line for the log / the UI
    juce::StringArray tried;                 // every failed attempt, in order
};

inline juce::AudioIODeviceType* findType (juce::AudioDeviceManager& dm, const juce::String& name)
{
    for (auto* t : dm.getAvailableDeviceTypes()) if (t->getTypeName() == name) return t;
    return nullptr;
}

/** HDMI / DisplayPort / GPU audio endpoints: usually a monitor with no speakers, often not openable. Tried after everything else. */
inline bool looksLikeDisplayAudio (const juce::String& n)
{
    return n.containsIgnoreCase ("HDMI") || n.containsIgnoreCase ("DisplayPort") || n.containsIgnoreCase ("NVIDIA")
        || n.containsIgnoreCase ("Display Audio") || n.containsIgnoreCase ("AMD High Definition");
}

/** Output names in the order to try: the real Windows default endpoint first (JUCE's getDefaultDeviceIndex = the default render endpoint,
    which its scan puts at index 0), then the other outputs, speakers/headphones before display audio. Logs the whole list. */
inline juce::StringArray outputOrder (juce::AudioIODeviceType& t, const LogFn& log)
{
    t.scanForDevices();
    const auto names = t.getDeviceNames (false);
    const int di = t.getDefaultDeviceIndex (false);
    juce::StringArray order;
    juce::String def;
    if (di >= 0 && di < names.size()) { def = names[di]; order.add (def); }
    for (auto& n : names) if (! order.contains (n) && ! looksLikeDisplayAudio (n)) order.add (n);
    for (auto& n : names) if (! order.contains (n)) order.add (n);
    if (log) log ("outputs of '" + t.getTypeName() + "': Windows default = " + (def.isEmpty() ? juce::String ("(none)") : "'" + def + "' (index " + juce::String (di) + ")")
                  + " | try order: " + order.joinIntoString (" > "));
    return order;
}

/** USB guitar cables (Rocksmith Real Tone cable, "USB Guitar Adapter", ...). On a fresh Windows PC the default capture device is the
    laptop's built-in microphone array, not the cable, so the cable must be picked by name. */
inline bool looksLikeGuitarCable (const juce::String& n)
{
    return n.containsIgnoreCase ("Rocksmith") || n.containsIgnoreCase ("Real Tone") || n.containsIgnoreCase ("RealTone")
        || n.containsIgnoreCase ("Guitar Adapter") || n.containsIgnoreCase ("USB Guitar");
}

/** The guitar input to open: a plugged-in guitar cable if there is one, else the Windows default capture device. */
inline juce::String defaultInputName (juce::AudioIODeviceType& t)
{
    t.scanForDevices();
    const auto names = t.getDeviceNames (true); const int di = t.getDefaultDeviceIndex (true);
    for (auto& n : names) if (looksLikeGuitarCable (n)) return n;
    return di >= 0 && di < names.size() ? names[di] : juce::String();
}

/** Open one specific output (+ optional input channel 0) of `typeName`. Returns "" on success. */
inline juce::String applySetup (juce::AudioDeviceManager& dm, const juce::String& typeName, const juce::String& out, const juce::String& in,
                                double rate, int buffer, bool defaultOutChannels = false)
{
    if (dm.getCurrentAudioDeviceType() != typeName) dm.setCurrentAudioDeviceType (typeName, true);
    juce::AudioDeviceManager::AudioDeviceSetup s;
    s.outputDeviceName = out; s.inputDeviceName = in; s.sampleRate = rate; s.bufferSize = buffer;
    s.inputChannels.clear(); if (in.isNotEmpty()) s.inputChannels.setBit (0);
    s.useDefaultInputChannels = false;
    s.outputChannels.clear(); if (! defaultOutChannels) s.outputChannels.setRange (0, 2, true);
    s.useDefaultOutputChannels = defaultOutChannels;
    auto err = dm.setAudioDeviceSetup (s, true);
    if (err.isEmpty() && dm.getCurrentAudioDevice() == nullptr) err = "device did not open";
    return err;
}

/** Plain WASAPI shared output: the real default endpoint first (48 kHz / 480 smp, then the device's own rate and period), and if that
    cannot be opened EVERY other output in turn until one opens. withInput = also open the default capture device as input 1 (combined
    device; when that fails the same output is retried output-only). Every attempt is logged. */
inline Opened openShared (juce::AudioDeviceManager& dm, bool withInput, const LogFn& log)
{
    Opened r; auto* t = findType (dm, SharedType);
    if (t == nullptr) { r.how = "'" + SharedType + "' is not offered on this system"; return r; }
    const auto order = outputOrder (*t, log);
    const auto in = withInput ? defaultInputName (*t) : juce::String();
    struct Variant { bool input; double rate; int buf; bool defCh; const char* label; };
    const Variant variants[] = { { true, 48000, 480, false, "48 kHz / 480 smp + input" }, { true, 0, 0, false, "device rate/period + input" },
                                 { false, 48000, 480, false, "48 kHz / 480 smp, output only" }, { false, 0, 0, false, "device rate/period, output only" },
                                 { false, 0, 0, true, "device defaults incl. channels, output only" } };
    int attempt = 0;
    for (auto& out : order) {
        for (auto& v : variants) {
            if (v.input && in.isEmpty()) continue;
            ++attempt;
            const auto err = applySetup (dm, SharedType, out, v.input ? in : juce::String(), v.rate, v.buf, v.defCh);
            if (err.isEmpty()) {
                r.ok = true; r.type = SharedType; r.output = out; r.input = v.input ? in : juce::String();
                r.how = "WASAPI shared on '" + out + "' (" + v.label + ")" + (out == order[0] ? " = the Windows default output" : " - the default output could not be opened, this was the first one that did");
                if (log) log ("shared WASAPI: OPENED " + r.how + " after " + juce::String (attempt) + " attempt(s)");
                return r;
            }
            const auto line = "'" + out + "' " + v.label + ": " + err;
            r.tried.add (line); if (log) log ("shared WASAPI: failed " + line);
        }
    }
    r.how = "no output device could be opened (" + juce::String (order.size()) + " tried)";
    return r;
}

/** Built-in low latency ("our ASIO alternative"): WASAPI exclusive (or IAudioClient3 low-latency shared) OUTPUT ONLY at 48 kHz with the
    smallest buffer >= 128 samples that the device offers (128..512; below 128 only if the device cannot do more), real default endpoint
    first, then the other outputs. The guitar input is NOT opened here: it runs as a split input on its own WASAPI stream, bridged by the
    drift resampler (output and input are different devices with different clocks). On return `ladder` holds the larger sizes to step up to
    if the first one glitches. */
inline Opened openLowLatency (juce::AudioDeviceManager& dm, const juce::String& typeName, const LogFn& log, juce::Array<int>* ladder = nullptr,
                              int minBuffer = 128, int maxBuffer = 512)
{
    Opened r; auto* t = findType (dm, typeName);
    if (t == nullptr) { r.how = "'" + typeName + "' is not offered on this system"; return r; }
    for (auto& out : outputOrder (*t, log)) {
        std::unique_ptr<juce::AudioIODevice> probe (t->createDevice (out, {}));   // query only, nothing is opened
        if (probe == nullptr) { r.tried.add ("'" + out + "': cannot create the device"); if (log) log (typeName + ": cannot create '" + out + "'"); continue; }
        const auto sizes = probe->getAvailableBufferSizes(); const auto rates = probe->getAvailableSampleRates();
        bool has48 = false; for (double x : rates) if (std::abs (x - 48000.0) < 1.0) has48 = true;
        juce::String sl; for (int b : sizes) sl << b << " ";
        if (log) log (typeName + ": '" + out + "' offers buffers [" + sl.trim() + "] and " + (has48 ? "supports" : "does NOT support") + " 48 kHz");
        probe.reset();
        if (! has48) { r.tried.add ("'" + out + "': no 48 kHz"); continue; }
        juce::Array<int> lad;
        for (int b : sizes) if (b >= minBuffer && b <= maxBuffer) lad.add (b);
        if (lad.isEmpty()) for (int b : sizes) if (b > 0 && b <= maxBuffer) { lad.add (b); break; }   // device cannot do >= minBuffer: its smallest
        if (lad.isEmpty()) { r.tried.add ("'" + out + "': no usable buffer size"); continue; }
        for (int k = 0; k < lad.size() && k < 3; ++k) {   // the 3 smallest candidates
            const auto err = applySetup (dm, typeName, out, {}, 48000, lad[k]);
            if (err.isEmpty()) {
                r.ok = true; r.type = typeName; r.output = out;
                auto* d = dm.getCurrentAudioDevice();
                r.how = typeName + " on '" + out + "' at 48 kHz / " + juce::String (d != nullptr ? d->getCurrentBufferSizeSamples() : lad[k]) + " smp";
                if (ladder != nullptr) { ladder->clear(); for (int b : sizes) if (b > (d != nullptr ? d->getCurrentBufferSizeSamples() : lad[k]) && b <= 1024) ladder->add (b); }
                if (log) log (typeName + ": OPENED " + r.how);
                return r;
            }
            const auto line = "'" + out + "' " + juce::String (lad[k]) + " smp: " + err;
            r.tried.add (line); if (log) log (typeName + ": failed " + line);
        }
    }
    r.how = typeName + " could not be opened";
    return r;
}

/** Exclusive -> low-latency shared -> plain shared. `lowLatencyWanted` false skips straight to plain shared. */
inline Opened openBuiltIn (juce::AudioDeviceManager& dm, bool lowLatencyWanted, bool sharedWithInput, const LogFn& log, juce::Array<int>* ladder = nullptr)
{
    juce::StringArray why;
    if (lowLatencyWanted) {
        for (auto& tn : { ExclusiveType, LowLatType }) {
            auto r = openLowLatency (dm, tn, log, ladder);
            if (r.ok) { r.how += why.isEmpty() ? juce::String() : " (after: " + why.joinIntoString ("; ") + ")"; return r; }
            why.add (tn + " failed: " + (r.tried.isEmpty() ? r.how : r.tried[0]));
        }
        if (log) log ("built-in low latency: both low-latency modes failed, falling back to shared WASAPI");
    }
    auto r = openShared (dm, sharedWithInput, log);
    if (! why.isEmpty()) r.how += " [FALLBACK from low latency: " + why.joinIntoString ("; ") + "]";
    return r;
}

/** ASIO4ALL (only if installed): output + input channel 0 at 48 kHz, buffer 128 then raised to >= 256 when the driver allows. */
inline Opened openAsio4All (juce::AudioDeviceManager& dm, const LogFn& log)
{
    Opened r; auto* asio = findType (dm, "ASIO");
    if (asio == nullptr) { r.how = "ASIO is not available in this build"; return r; }
    asio->scanForDevices();
    for (auto& n : asio->getDeviceNames()) if (n.containsIgnoreCase ("ASIO4ALL")) {
        auto err = applySetup (dm, "ASIO", n, n, 48000, 128);
        if (err.isEmpty()) {
            if (auto* d = dm.getCurrentAudioDevice()) {   // safe default: 256 (5.3 ms) if the driver allows, else the smallest size >= 256, else the largest it offers
                int want = 0; for (int b : d->getAvailableBufferSizes()) if (b >= 256) { want = b; break; }
                if (want == 0 && ! d->getAvailableBufferSizes().isEmpty()) want = d->getAvailableBufferSizes().getLast();
                if (want > 0 && want != d->getCurrentBufferSizeSamples()) { auto cur = dm.getAudioDeviceSetup(); cur.bufferSize = want; dm.setAudioDeviceSetup (cur, true); }
            }
            r.ok = dm.getCurrentAudioDevice() != nullptr; r.type = "ASIO"; r.output = r.input = n; r.how = n + " via ASIO";
            if (log) log ("ASIO: OPENED " + r.how);
            return r;
        }
        r.tried.add (n + ": " + err); if (log) log ("ASIO: " + n + " failed: " + err);
    }
    r.how = "ASIO4ALL not installed or not openable";
    return r;
}

} // namespace sj::devpolicy
