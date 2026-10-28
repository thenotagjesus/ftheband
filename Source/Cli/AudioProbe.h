// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <JuceHeader.h>
#include "../Engine/AudioEngine.h"
#include "../Engine/SplitInput.h"
#include "../Engine/DevicePolicy.h"
#include "../Net/SamplePacks.h"

namespace sj::probe {

/** `FtheBand.exe --audio-probe <list|shared|lowlat|exclusive|asio|auto> [seconds=4] [--split] [--out file]`
    Opens the output with the SAME code the app uses (DevicePolicy.h), runs the real AudioEngine callback for <seconds> with the
    output forced to SILENCE (nothing is audible), optionally opens the guitar (default capture device) as a split input with its own
    clock + drift resampler, and prints one machine-readable line "PROBE {...}" with the reported latency, buffer, callback timing, xruns
    and drift telemetry. It never reads or writes session.xml / settings. */
struct ZeroingCallback : juce::AudioIODeviceCallback {
    explicit ZeroingCallback (sj::AudioEngine* e) : eng (e) {}
    sj::AudioEngine* eng;
    std::atomic<int64_t> callbacks { 0 }, late { 0 }; std::atomic<double> maxGapMs { 0 }, sumGapMs { 0 };
    double last = 0, period = 0; std::atomic<int> block { 0 };
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override { block = d->getCurrentBufferSizeSamples(); period = 1000.0 * block / d->getCurrentSampleRate(); if (eng) eng->audioDeviceAboutToStart (d); }
    void audioDeviceStopped() override { if (eng) eng->audioDeviceStopped(); }
    void audioDeviceIOCallbackWithContext (const float* const* in, int ni, float* const* out, int no, int n, const juce::AudioIODeviceCallbackContext& c) override {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (last > 0 && callbacks.load() > 20) {   // skip start-up
            const double gap = now - last; sumGapMs = sumGapMs.load() + gap;
            if (gap > maxGapMs.load()) maxGapMs = gap;
            if (gap > 2.5 * period) ++late;
        }
        last = now; ++callbacks;
        if (eng) eng->audioDeviceIOCallbackWithContext (in, ni, out, no, n, c);
        for (int ch = 0; ch < no; ++ch) if (out[ch] != nullptr) juce::FloatVectorOperations::clear (out[ch], n);   // never audible
    }
};

inline juce::String q (const juce::String& s) { return juce::JSON::toString (s); }

inline int run (const juce::StringArray& args, int at)
{
    const juce::String mode = args[at + 1].isEmpty() ? "auto" : args[at + 1];
    double seconds = 4; if (args[at + 2].getDoubleValue() > 0) seconds = args[at + 2].getDoubleValue();
    const bool doSplit = args.contains ("--split");
    const int oi = args.indexOf ("--out"); const juce::File outFile = oi >= 0 ? juce::File::getCurrentWorkingDirectory().getChildFile (args[oi + 1]) : juce::File();
    juce::String report;
    auto emit = [&] (const juce::String& s) { std::cout << s << std::endl; report << s << "\n"; };
    auto log = [&] (const juce::String& m) { emit ("LOG " + m); };

    juce::AudioDeviceManager dm; dm.initialise (0, 2, nullptr, false);   // no device is opened here
    if (mode == "list") {
        for (auto* t : dm.getAvailableDeviceTypes()) {
            t->scanForDevices();
            const auto outs = t->getDeviceNames (false), ins = t->getDeviceNames (true);
            emit ("TYPE " + t->getTypeName() + " | outputs: " + outs.joinIntoString (" ; ") + " | default output: #" + juce::String (t->getDefaultDeviceIndex (false)) + " = \"" + outs[t->getDefaultDeviceIndex (false)]
                  + "\" | inputs: " + ins.joinIntoString (" ; ") + " | default input: #" + juce::String (t->getDefaultDeviceIndex (true)) + " = \"" + ins[t->getDefaultDeviceIndex (true)] + "\"");
        }
        if (outFile != juce::File()) outFile.replaceWithText (report);
        return 0;
    }

    sj::AudioEngine engine;
    ZeroingCallback cb (doSplit ? &engine : nullptr);
    std::unique_ptr<sj::SplitInput> split;
    juce::Array<int> ladder; sj::devpolicy::Opened r;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    if (mode == "shared")         r = sj::devpolicy::openShared (dm, false, log);
    else if (mode == "lowlat")    r = sj::devpolicy::openLowLatency (dm, sj::devpolicy::LowLatType, log, &ladder);
    else if (mode == "exclusive") r = sj::devpolicy::openLowLatency (dm, sj::devpolicy::ExclusiveType, log, &ladder);
    else if (mode == "asio")      r = sj::devpolicy::openAsio4All (dm, log);
    else {   // auto = the app's startup policy when nothing is saved
        r = sj::devpolicy::openAsio4All (dm, log);
        if (! r.ok) r = sj::devpolicy::openBuiltIn (dm, true, true, log, &ladder);
    }
    const double openMs = juce::Time::getMillisecondCounterHiRes() - t0;
    juce::String splitInfo = "null";
    if (! r.ok) {
        emit ("PROBE {\"mode\":" + q (mode) + ",\"ok\":false,\"how\":" + q (r.how) + ",\"tried\":" + q (r.tried.joinIntoString (" || ")) + "}");
        if (outFile != juce::File()) outFile.replaceWithText (report);
        return 2;
    }
    auto* d = dm.getCurrentAudioDevice();
    const double sr = d->getCurrentSampleRate(); const int bufSz = d->getCurrentBufferSizeSamples();
    if (doSplit) {
        auto* sh = sj::devpolicy::findType (dm, sj::devpolicy::SharedType);
        const auto in = r.input.isNotEmpty() ? r.input : (sh != nullptr ? sj::devpolicy::defaultInputName (*sh) : juce::String());
        if (r.input.isNotEmpty()) splitInfo = "{\"note\":\"combined input on the output device\"}";
        else if (sh != nullptr && in.isNotEmpty()) {
            split = std::make_unique<sj::SplitInput> (engine);
            auto* low = sj::devpolicy::findType (dm, sj::devpolicy::LowLatType);
            const auto err = split->open (*sh, in, sr, bufSz, low);
            splitInfo = err.isEmpty() ? "{\"opened\":true,\"device\":" + q (in) + ",\"type\":" + q (split->typeName) + ",\"inputBlock\":" + juce::String (split->inputBlock()) + "}"
                                      : "{\"opened\":false,\"error\":" + q (err) + "}";
        } else splitInfo = "{\"opened\":false,\"error\":\"no input device\"}";
    }
    dm.addAudioCallback (&cb);
    const double runStart = juce::Time::getMillisecondCounterHiRes();
    while (juce::Time::getMillisecondCounterHiRes() - runStart < seconds * 1000.0) juce::Thread::sleep (20);
    const double ran = (juce::Time::getMillisecondCounterHiRes() - runStart) / 1000.0;
    const int outLat = d->getOutputLatencyInSamples(), inLat = d->getInputLatencyInSamples();
    const int xr = d->getXRunCount();
    juce::String drift = "null";
    if (split != nullptr && split->isOpen()) {
        auto& tl = engine.driftIn.tel;
        drift = "{\"active\":" + juce::String (engine.isSplitInputActive() ? "true" : "false") + ",\"ppm\":" + juce::String (tl.ppm.load(), 1) + ",\"fill\":" + juce::String (tl.fill.load(), 1)
              + ",\"target\":" + juce::String (tl.target.load(), 1) + ",\"primed\":" + juce::String (tl.primed.load() ? "true" : "false") + ",\"underruns\":" + juce::String (tl.underruns.load())
              + ",\"overruns\":" + juce::String (tl.overruns.load()) + ",\"stalls\":" + juce::String (tl.stalls.load()) + ",\"inputXruns\":" + juce::String (split->xruns())
              + ",\"inputLatencySmp\":" + juce::String (split->inputLatency()) + ",\"samplesWritten\":" + juce::String ((juce::int64) engine.driftIn.samplesWritten()) + "}";
    }
    dm.removeAudioCallback (&cb);
    if (split != nullptr) split->close();
    const double expectCb = ran * sr / bufSz;
    juce::String o;
    o << "PROBE {\"mode\":" << q (mode) << ",\"ok\":true,\"type\":" << q (r.type) << ",\"output\":" << q (r.output) << ",\"how\":" << q (r.how)
      << ",\"rateHz\":" << juce::String (sr, 0) << ",\"bufferSmp\":" << bufSz << ",\"bufferMs\":" << juce::String (1000.0 * bufSz / sr, 2)
      << ",\"outLatencySmp\":" << outLat << ",\"outLatencyMs\":" << juce::String (1000.0 * outLat / sr, 2)
      << ",\"inLatencySmp\":" << inLat << ",\"openMs\":" << juce::String (openMs, 0)
      << ",\"seconds\":" << juce::String (ran, 1) << ",\"callbacks\":" << (juce::int64) cb.callbacks.load() << ",\"expectedCallbacks\":" << juce::String (expectCb, 0)
      << ",\"maxCallbackGapMs\":" << juce::String (cb.maxGapMs.load(), 2) << ",\"lateCallbacks\":" << (juce::int64) cb.late.load() << ",\"xruns\":" << xr
      << ",\"engineGlitches\":" << (juce::int64) engine.glitches() << ",\"silentOutput\":true,\"largerBuffers\":" << q (juce::String (ladder.size()) + " step-up sizes") << ",\"split\":" << splitInfo << ",\"drift\":" << drift
      << ",\"fallbackTried\":" << q (r.tried.joinIntoString (" || ")) << "}";
    emit (o);
    dm.closeAudioDevice();
    if (outFile != juce::File()) outFile.replaceWithText (report);
    return 0;
}

/** `FtheBand.exe --samples-info [--out file]`: where the samples are loaded from and how many zones came from where. */
inline int samplesInfo (const juce::StringArray& args)
{
    juce::String report;
    auto emit = [&] (const juce::String& s) { std::cout << s << std::endl; report << s << "\n"; };
    const auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    emit ("EXE " + juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName());
    for (auto& p : sj::SampleLibrary::searchPaths()) emit ("SEARCH " + p.getFullPathName() + (p.isDirectory() ? "" : "  (missing)"));
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    sj::SampleLibrary lib; const bool ok = lib.load (false);   // bundled only
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    const bool nextToExe = lib.folder != juce::File() && lib.folder.getParentDirectory() == exeDir;
    emit ("BUNDLED folder=" + lib.folder.getFullPathName() + " nextToExe=" + (nextToExe ? "true" : "false") + " ok=" + (ok ? "true" : "false")
          + " zonesTotal=" + juce::String (lib.loaded) + " base=" + juce::String (lib.loadedBase) + " packs=" + juce::String (lib.loadedBundledPacks)
          + " packCount=" + juce::String (lib.bundledPackIds.size()) + " drums=" + juce::String ((int) lib.drums.zones.size()) + " bass=" + juce::String ((int) lib.bass.zones.size())
          + " keys=" + juce::String ((int) lib.keys.zones.size()) + " loadMs=" + juce::String (ms, 0));
    for (auto& p : sj::SamplePacks::installed())
        emit ("PACK " + p.id + " installed=" + (sj::SamplePacks::isInstalled (p.id) ? "true" : "false") + " bundled=" + (sj::SamplePacks::isBundled (p.id) ? "true" : "false")
              + " licence=" + p.licence + " name=\"" + p.name + "\"");
    sj::SampleLibrary all; all.load (true);
    emit ("WITH_USER zonesTotal=" + juce::String (all.loaded) + " user=" + juce::String (all.loadedUser) + " (user copies of bundled packs are skipped)");
    emit ("CREDITS\n" + sj::SamplePacks::attributionText());
    const int oi = args.indexOf ("--out");
    if (oi >= 0) juce::File::getCurrentWorkingDirectory().getChildFile (args[oi + 1]).replaceWithText (report);
    return ok ? 0 : 2;
}

} // namespace sj::probe
