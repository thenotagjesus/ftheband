// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Session Jam contributors
#include <JuceHeader.h>
#include "MainComponent.h"
#include "Util/ProcStats.h"

class SessionJamApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void initialise (const juce::String&) override {
        auto args = getCommandLineParameterArray();
        const int si = args.indexOf ("--screens");
        if (si >= 0) {   // render every tab to PNGs and quit (CI / layout review)
            juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (args[si + 1].isNotEmpty() ? args[si + 1] : "screens");
            MainComponent mc (false);
            for (auto& f : mc.renderScreens (dir)) std::cout << f << std::endl;
            setApplicationReturnValue (0); quit(); return;
        }
        const int ei = args.indexOf ("--engine-test");
        if (ei >= 0) { setApplicationReturnValue (engineTest (args[ei + 1], args[ei + 2], args[ei + 3].getIntValue())); quit(); return; }
        const int ki = args.indexOf ("--soak");
        if (ki >= 0) { setApplicationReturnValue (soakTest (args[ki + 1].getDoubleValue(), args[ki + 2], args[ki + 3].getIntValue(), args[ki + 4].getDoubleValue())); quit(); return; }
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
        Window() : DocumentWindow ("Session Jam", juce::Colour (0xff15171c), allButtons) {
            setUsingNativeTitleBar (true);
            auto* mc = new MainComponent();
            // Logical 1280x720 (3840x2160 at 300 % OS scaling). Only shrink when the screen can't hold it.
            juce::Rectangle<int> area (1280, 720);
            if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) area = d->userArea;
            const float s = mc->uiScale > 0 ? mc->uiScale : MainComponent::autoScaleFor (area.reduced (0, 40));
            if (std::abs (s - 1.0f) > 0.01f) juce::Desktop::getInstance().setGlobalScaleFactor (s);
            setContentOwned (mc, true);
            setResizable (true, true); setResizeLimits (480, 320, 10000, 10000);
            centreWithSize (MainComponent::LogicalW, MainComponent::LogicalH); setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION (SessionJamApp)
