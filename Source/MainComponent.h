// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <JuceHeader.h>
#include "Engine/AudioEngine.h"
#include "Engine/SplitInput.h"
#include "Engine/DevicePolicy.h"
#include "UI/PluginUI.h"
#include "UI/PageSkin.h"

/** Main window content. Layout is designed for 1280x720 logical pixels (= 3840x2160 at 300 % scaling) and every tab
    sits in a Viewport, so smaller windows scroll instead of clipping. */
class MainComponent : public juce::Component, private juce::Timer, private juce::ChangeListener {
public:
    explicit MainComponent (bool openAudio = true);
    ~MainComponent() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;   // Space = Start/Stop (unless typing in a text box)
    std::function<void()> onStartStop;

    /** --screens: render every tab at several window sizes to PNGs. Returns the files written. */
    juce::StringArray renderScreens (const juce::File& dir);
    static constexpr int LogicalW = 1280, LogicalH = 720;
    static float autoScaleFor (juce::Rectangle<int> userArea);   // <1 only when the screen is smaller than 1280x720 logical
    float uiScale = 0;   // 0 = auto (saved in session.xml)

    /** Look: 0 = default (original dark-blue), 1 = optional "FtheBand page" skin (matches the landing page; UI/PageSkin.h). Saved in session.xml. */
    void setSkin (int newSkin);
    int getSkin() const { return skin; }
    static juce::Colour windowBackground (int skinId) { return skinId == 1 ? juce::Colour (0xff070707) : juce::Colour (0xff15171c); }

    struct Page;
private:
    int skin = 0;
    std::unique_ptr<sj::PageSkin> skinLnf;
    void enableSkinGlobals (bool page);
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void setupAudioDevices();
    void loadSession();
    void saveSession();
    static juce::File settingsFile();
    static juce::File recordingsFolder();
    static juce::File driftLogFile();
    // split input
    bool openSplitInput (const juce::String& typeName, const juce::String& inName);   // output must already be open
    void closeSplitInput();
    void reopenSplitInputForOutput();             // after the output device (re)started
    void maybeAutoSplit();                         // policy: split whenever input and output are different devices
    juce::StringArray splitCandidates() const;
    /** Why the guitar has no input right now (empty text = it is fine) and the one-click fix for it (fix 0 = none). */
    struct InputIssue { juce::String text, action; int fix = 0; bool ok = false; };
    InputIssue inputIssue() const;
    void applyInputFix (int fix);                  // 1 = guitar through the ASIO driver channel, 2 = cable as split input, 3 = retry the split input
    bool inRateRetry = false; int previewIssue = 0;
    void driftTick();                              // 1 Hz: status readout + drift.log + input watchdog + 30 s mem/CPU line
    void logEvent (const juce::String& text);      // timestamped line in drift.log (opens/rotates it; always works, split or not)
    juce::String deviceSummary();                  // "ASIO ASIO4ALL v2 | in: - | out: ... | 48 kHz / 128"
    /** Watchdog for one split input (guitar or mic): no samples for 2 s, a device error, or a failed open -> reopen,
        retrying every 2, 4, 8 .. 30 s. Returns true when it is healthy. */
    struct Watch { int64_t lastW = -1; int stuck = 0, retryIn = 0, backoff = 0, healthy = 0, attempts = 0; };
    bool watchInput (Watch& w, bool isMic);
    juce::String splitStatusText() const;
    juce::AudioIODeviceType* findType (const juce::String& name) const;
    juce::AudioIODeviceType* lowLatencyTypeFor (const juce::AudioIODeviceType& t) const;   // "Windows Audio" -> its Low Latency Mode twin
    // microphone (second split input, own drift resampler; recorded as its own stem, never feeds the chord follower)
    bool openMicInput (const juce::String& inName);
    void closeMicInput();
    juce::StringArray micCandidates() const;
    // built-in low latency (WASAPI exclusive / IAudioClient3 + split input): our own "ASIO alternative", no driver needed
    bool isBuiltInLowLatency() const;
    juce::String setBuiltInLowLatency (bool on);   // Audio tab toggle; returns a one-line result
    void startBufferTuning (const juce::Array<int>& ladder);
    void tuneTick();
    void showSetupDialog();                        // setup check window (Audio tab button, and once at first start)
    void maybeShowSetupAtStart();
    juce::Component::SafePointer<juce::Component> setupWindow;
    bool tuneActive = false; int64_t tuneStart = 0; int tuneBase = -1; juce::Array<int> tuneLadder;
    // latency: push the current path estimates into the engine (PDC + follower), readout text
    void updateLatency (bool force = false);
    juce::String latencyText (bool shortForm) const;

    bool audioOpen;
    juce::AudioDeviceManager deviceManager;
    sj::AudioEngine engine;
    sj::PluginLibrary pluginLibrary { engine.plugins };
    std::unique_ptr<juce::XmlElement> savedSession;
    juce::String donateUrl;
    int focusTick = 0;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::OwnedArray<Page> pages;
    juce::Label statusBar;
    std::unique_ptr<sj::SplitInput> split;
    juce::String splitType, splitDevice;          // wanted split input ("" = combined device); persisted <SplitInput>
    bool splitAuto = false, reconfiguring = false;
    juce::String splitError, deviceText;
    std::unique_ptr<sj::SplitInput> mic;
    juce::String micDevice, micError;             // persisted <Mic device gain monitor/>
    bool preferLowLatency = true;                 // persisted <Latency lowLatencyInput trimMs/>
    float trimMs = 0;
    int lastOutLat = -1, lastGtrIn = -1, lastMicIn = -1;
    std::unique_ptr<juce::FileOutputStream> driftLog;
    Watch splitWatch, micWatch; int64_t secs = 0; double lastCpu = -1, startMB = -1; juce::String lastDeviceSummary;
    int tickCount = 0, ppmHighSeconds = 0, lastOutBlock = 0; double lastOutRate = 0; juce::String splitStatus;
    sj::EngineStatus preview; bool usePreview = false;   // --screens: deterministic sample state
    friend struct JamPage; friend struct BandPage; friend struct InsertsPage; friend struct AudioPage; friend struct AboutPage; friend struct SoundsPage;
};
