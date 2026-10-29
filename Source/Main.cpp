// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#include <JuceHeader.h>
#include "MainComponent.h"
#include "Util/ProcStats.h"
#include "Util/Migration.h"
#include "Cli/AudioProbe.h"
#include "Setup/FirstRunSetup.h"

#ifndef FTHEBAND_DISPLAY_VERSION
 #define FTHEBAND_DISPLAY_VERSION "0.1.0-beta"
#endif

class FthebandApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return FTHEBAND_DISPLAY_VERSION; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void initialise (const juce::String&) override {
        auto args = getCommandLineParameterArray();
        const int si = args.indexOf ("--screens");
        if (si >= 0) {   // render every tab to PNGs and quit (CI / layout review)
            juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (args[si + 1].isNotEmpty() ? args[si + 1] : "screens");
            MainComponent mc (false);
            if (const int ski = args.indexOf ("--skin"); ski >= 0) { if (args[ski + 1] == "roundtrip") { mc.setSkin (1); mc.setSkin (0); } else mc.setSkin (args[ski + 1] == "page" ? 1 : 0); }   // --skin page|default|roundtrip (roundtrip = on then off again: must equal default)
            for (auto& f : mc.renderScreens (dir)) std::cout << f << std::endl;
            setApplicationReturnValue (0); quit(); return;
        }
        if (args.contains ("--setup-check")) {   // list what the first-run setup would show; never installs. --verify-flex-download [bad] fetches the FlexASIO installer INTO MEMORY only and checks its SHA-256. --out <file> writes there too.
            juce::String t; juce::StringArray none;
            const auto pr = sj::setup::detect (none);
            t << "windows=" << (int) pr.windows << " asio4all=" << (int) pr.asio4all << " flexasio=" << (int) pr.flexasio << " samples=" << (int) pr.samples << " cable=" << (int) pr.cableSeen << " smartAppControl=" << pr.smartAppControl << "\n";
            for (auto& it : sj::setup::buildItems (pr)) t << (it.state == sj::setup::State::Ok ? "[ok]      " : it.state == sj::setup::State::Missing ? "[missing] " : "[note]    ") << it.id << " | " << it.title << "\n";
            if (const int vi = args.indexOf ("--verify-flex-download"); vi >= 0) {
                const bool bad = args[vi + 1] == "bad"; juce::MemoryBlock mb; juce::String err;
                const bool ok = sj::setup::FlexAsioInstaller::fetchVerified (sj::setup::FlexAsioUrl, sj::setup::FlexAsioPrefix, sj::setup::FlexAsioMaxSize,
                                                                              bad ? juce::String ("00") + juce::String (sj::setup::FlexAsioSha256).substring (2) : juce::String (sj::setup::FlexAsioSha256), mb, err);
                t << "flexasio download check" << (bad ? " (wrong hash, must fail)" : "") << ": " << (ok ? "VERIFIED" : "REJECTED") << ", " << (juce::int64) mb.getSize() << " bytes " << err << "\n";
                juce::MemoryBlock dummy; juce::String err2;
                const bool ok2 = sj::setup::FlexAsioInstaller::fetchVerified ("http://github.com/dechamps/FlexASIO/releases/download/x/y.exe", sj::setup::FlexAsioPrefix, 1000, "00", dummy, err2);
                t << "plain-http url: " << (ok2 ? "ACCEPTED (bug)" : "REJECTED") << " (" << err2 << ")\n";
                const bool ok3 = sj::setup::FlexAsioInstaller::fetchVerified ("https://example.com/FlexASIO.exe", sj::setup::FlexAsioPrefix, 1000, "00", dummy, err2);
                t << "foreign host: " << (ok3 ? "ACCEPTED (bug)" : "REJECTED") << " (" << err2 << ")\n";
            }
            std::cout << t << std::endl;
            if (const int oi = args.indexOf ("--out"); oi >= 0) juce::File::getCurrentWorkingDirectory().getChildFile (args[oi + 1]).replaceWithText (t);
            setApplicationReturnValue (0); quit(); return;
        }
        const int pi = args.indexOf ("--audio-probe");
        if (pi >= 0) { setApplicationReturnValue (sj::probe::run (args, pi)); quit(); return; }
        if (args.contains ("--samples-info")) { setApplicationReturnValue (sj::probe::samplesInfo (args)); quit(); return; }
        const int ei = args.indexOf ("--engine-test");
        if (ei >= 0) { setApplicationReturnValue (engineTest (args[ei + 1], args[ei + 2], args[ei + 3].getIntValue())); quit(); return; }
        const int ki = args.indexOf ("--soak");
        if (ki >= 0) { setApplicationReturnValue (soakTest (args[ki + 1].getDoubleValue(), args[ki + 2], args[ki + 3].getIntValue(), args[ki + 4].getDoubleValue())); quit(); return; }
        sj::migrateLegacyUserData();   // one-time: copy settings/session/log from the old SessionJam folder
        window.reset (new Window());
    }
    /** Live-engine smoke test: stream a WAV through AudioEngine's real callback (threads, FIFO, triple buffers, recorder)
        at ~4x real time with the given device block size; writes the stereo mix + stems, prints status once a second. */
    static int engineTest (const juce::String& inPath, const juce::String& outDir, int block) {
        if (block <= 0) block = 480;
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File::getCurrentWorkingDirectory().getChildFile (inPath)));
        if (! r) { std::cerr << "cannot read " << inPath << std::endl; return 1; }
        juce::AudioBuffer<float> in (1, (int) r->lengthInSamples); r->read (&in, 0, in.getNumSamples(), 0, true, false);
        auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (outDir); dir.createDirectory();
        sj::AudioEngine e; e.prepareOffline (r->sampleRate, block);
        e.startSession();
        e.recorder.start (dir.getChildFile ("stems"), e.stemNames(), r->sampleRate);
        juce::AudioBuffer<float> out (2, in.getNumSamples()); out.clear();
        const auto t0 = juce::Time::getMillisecondCounterHiRes(); int lastSec = -1;
        for (int pos = 0; pos < in.getNumSamples(); pos += block) {
            const int n = juce::jmin (block, in.getNumSamples() - pos);
            const float* ins[1] = { in.getReadPointer (0, pos) }; float* outs[2] = { out.getWritePointer (0, pos), out.getWritePointer (1, pos) };
            e.audioDeviceIOCallbackWithContext (ins, 1, outs, 2, n, {});
            const double due = t0 + 1000.0 * (pos + n) / r->sampleRate / 4.0;
            while (juce::Time::getMillisecondCounterHiRes() < due) juce::Thread::sleep (1);
            const int sec = (int) ((pos + n) / r->sampleRate);
            if (sec != lastSec) { lastSec = sec; const auto& s = e.status();
                std::cout << "t=" << sec << "s phase=" << (int) e.phase() << " chord=" << s.chord.name() << " key=" << sj::keyName (s.key)
                          << " bpm=" << s.bpm << (s.beatLocked ? " locked" : "") << " band=" << e.bandState.load() << " countIn=" << s.countIn
                          << " notches=" << s.nNotches << " floor=" << s.floorDb << std::endl; }
        }
        e.recorder.stop();
        std::cout << "analysis dropped " << e.analysisDropped.load() << " samples, recorder dropped blocks " << e.recorder.droppedBlocks.load() << std::endl;
        juce::WavAudioFormat wav; auto f = dir.getChildFile ("mix.wav"); f.deleteFile();
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), r->sampleRate, 2, 24, {}, 0));
        if (w) w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
        return 0;
    }
    /** --soak <minutes> <outDir> [block=128] [speed=4]: memory/CPU soak. Loops the bundled user take through the live
        engine for <minutes> of audio time WITH the split-input drift path (input pushed at +300 ppm in 480-sample
        blocks, as on the rig) and stem recording to disk, polling status like the UI does. Prints RSS/private/CPU every
        audio minute (CSV) so growth over time is visible. */
    static int soakTest (double minutes, const juce::String& outDir, int block, double speed) {
        if (minutes <= 0) minutes = 60; if (block <= 0) block = 128; if (speed <= 0) speed = 4;
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        juce::File take;
        for (auto d = juce::File::getSpecialLocation (juce::File::currentExecutableFile); d.getParentDirectory() != d; d = d.getParentDirectory())
            if (d.getChildFile ("user-takes").isDirectory()) { take = d.getChildFile ("user-takes").findChildFiles (juce::File::findFiles, false, "*.wav")[0]; break; }
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (take));
        if (! r) { std::cerr << "no user-takes/*.wav found near the executable" << std::endl; return 1; }
        juce::AudioBuffer<float> in (1, (int) r->lengthInSamples); r->read (&in, 0, in.getNumSamples(), 0, true, false);
        const double sr = r->sampleRate; const int inBlock = 480;
        auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (outDir); dir.createDirectory();
        sj::AudioEngine e; e.prepareOffline (sr, block);
        e.driftIn.prepare (sr, block, inBlock); e.setSplitInputActive (true);
        e.startSession();
        e.recorder.start (dir.getChildFile ("stems"), e.stemNames(), sr);
        std::vector<float> o0 ((size_t) block), o1 ((size_t) block);
        const int64_t total = (int64_t) (minutes * 60.0 * sr);
        const double inRate = sr * (1.0 + 300e-6);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        int64_t outPos = 0, inPos = 0; int lastMin = -1; double lastCpu = 0;
        std::cout << "minute,rss_mb,private_mb,peak_mb,cpu_pct_of_core,recorded_s,rec_dropped_blocks,analysis_dropped,fifo_underruns,stalls,cb_max_us,cb_budget_us,cb_late,nan,clip_samples" << std::endl;
        while (outPos < total) {
            // input device side: whole 480-sample blocks, on its own (+300 ppm) clock
            while ((double) (inPos + inBlock) / inRate <= (double) (outPos + block) / sr) {
                const int off = (int) (inPos % (in.getNumSamples() - inBlock));
                e.driftIn.push (in.getReadPointer (0, off), inBlock); inPos += inBlock;
            }
            float* outs[2] = { o0.data(), o1.data() };
            e.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, block, {});
            outPos += block;
            if ((outPos / block) % 64 == 0) { const auto& s = e.status(); juce::ignoreUnused (s); }   // UI-style polling
            const double due = t0 + 1000.0 * (double) outPos / sr / speed;
            while (juce::Time::getMillisecondCounterHiRes() < due) juce::Thread::sleep (1);
            const int m = (int) (outPos / (60.0 * sr));
            if (m != lastMin) { lastMin = m;
                const auto p = sj::ProcStats::now();
                const double wall = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
                const double cpu = m == 0 ? 0.0 : (p.cpuSeconds - lastCpu) / (60.0 / speed) * 100.0; lastCpu = p.cpuSeconds; juce::ignoreUnused (wall);
                std::cout << m << "," << juce::String (p.workingSetMB, 2) << "," << juce::String (p.privateMB, 2) << "," << juce::String (p.peakMB, 2) << "," << juce::String (cpu, 1)
                          << "," << juce::String ((double) e.recorder.samplesWritten() / sr, 1) << "," << e.recorder.droppedBlocks.load() << "," << e.analysisDropped.load()
                          << "," << e.driftIn.tel.underruns.load() << "," << e.driftIn.tel.stalls.load() << "," << e.takeCbMaxUs() << "," << e.cbBudgetUs.load() << "," << e.cbLate.load() << "," << e.nanEvents.load() << "," << e.clipSamples.load() << std::endl; }
        }
        e.recorder.stop(); e.setSplitInputActive (false);
        return 0;
    }
    void shutdown() override { window = nullptr; }
    void systemRequestedQuit() override { quit(); }
private:
    struct Window : juce::DocumentWindow {
        Window() : DocumentWindow (juce::String ("FtheBand ") + FTHEBAND_DISPLAY_VERSION, juce::Colour (0xff15171c), allButtons) {
            setUsingNativeTitleBar (true);
            auto* mc = new MainComponent();
            // Logical 1280x720 (3840x2160 at 300 % OS scaling). Only shrink when the screen can't hold it.
            juce::Rectangle<int> area (1280, 720);
            if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) area = d->userArea;
            const float s = mc->uiScale > 0 ? mc->uiScale : MainComponent::autoScaleFor (area.reduced (0, 40));
            if (std::abs (s - 1.0f) > 0.01f) juce::Desktop::getInstance().setGlobalScaleFactor (s);
            setBackgroundColour (MainComponent::windowBackground (mc->getSkin()));
            setContentOwned (mc, true);
            setResizable (true, true); setResizeLimits (480, 320, 10000, 10000);
            centreWithSize (MainComponent::LogicalW, MainComponent::LogicalH); setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION (FthebandApp)
