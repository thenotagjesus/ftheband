// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <thread>
#include "DriftResampler.h"
#include <JuceHeader.h>
#include "Engine/Band.h"
#include "Analysis/ChordTracker.h"
#include "Music/InputInstrument.h"
#include "Recording/StemRecorder.h"
#include "Engine/PluginChain.h"
#include "Engine/StemDelay.h"

namespace sj {

/** Lock-free single-producer/single-consumer "latest value" exchange (triple buffer). Never blocks, never tears. */
template <typename T> class TripleBuffer {
public:
    /** Producer: write into back(), then publish(). */
    T& back() { return slots[(size_t) backIdx]; }
    void publish() { backIdx = mid.exchange (backIdx | dirtyBit, std::memory_order_acq_rel) & idxMask; }
    /** Consumer: returns true if a newer value was swapped into front(). */
    bool consume() {
        if ((mid.load (std::memory_order_relaxed) & dirtyBit) == 0) return false;
        frontIdx = mid.exchange (frontIdx, std::memory_order_acq_rel) & idxMask; return true;
    }
    const T& front() const { return slots[(size_t) frontIdx]; }
private:
    static constexpr int dirtyBit = 4, idxMask = 3;
    std::array<T, 3> slots {}; int backIdx = 0, frontIdx = 1; std::atomic<int> mid { 2 };
};

/** One band chair as the UI/session sees it. */
struct ChairSpec { ChairType type = ChairType::Drums; int role = 0; float gain = 0.8f; bool mute = false, solo = false; };

/** What the analysis thread knows (published after every analysed block). Times are in the engine's session clock. */
struct EngineStatus {
    ChordLabel chord; KeyEstimate key; std::array<float, 12> chroma {};
    double bpm = 0; bool beatLocked = false; int64_t beatPhase = 0; uint32_t lockGeneration = 0;
    bool countIn = true, silent = true; float intensity = 0, rmsDb = -100, gateDb = -50, floorDb = -100;
    int64_t lastOnset = -1000000, lastActive = -1000000; int onsetCount = 0; int bassMidi = -1;
    bool strangleDetected = false; float lowRatioDb = 0;
    int nNotches = 0; float notchHz[ChordTracker::maxNotches] {}; bool noiseLearned = false;
    uint32_t session = 0;
};

/** Audio callback + analysis thread.
    Audio thread (never locks or allocates): mono input ch 0 -> GuitarDI bus; placeholder drive + 4 VST inserts -> GuitarFX;
    the Band renders one bus per chair from the latest FollowerState; stereo mix out; every bus goes to the stem recorder.
    Blocks are processed in MaxChunk pieces, so any device buffer size works (nothing is dropped).
    Analysis thread: polls a lock-free FIFO (no signalling from the audio thread), runs ChordTracker and publishes
    EngineStatus through triple buffers (one for the audio thread, one for the UI).
    Session: start() resets band + tracker, learns ~1 s of noise (gate floor + hum notches), then the count-in. */
class AudioEngine : public juce::AudioIODeviceCallback, private juce::Thread {
public:
    static constexpr int MaxChunk = 512;
    AudioEngine();
    ~AudioEngine() override;

    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    /** Same as audioDeviceAboutToStart without a device (offline engine test). */
    void prepareOffline (double sampleRate, int blockSize);
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;

    // ---- controls (message thread writes, audio/analysis threads read)
    std::atomic<int> style { 0 };
    std::atomic<float> fallbackBpm { 100 };          // band tempo until a count-in locks
    std::atomic<bool> monitorGuitar { true };
    std::atomic<bool> placeholderDrive { false };   // tanh(4x) soft-clip ahead of the inserts: OFF by default (clean DI until the player asks for it)
    std::atomic<float> guitarGain { 1.0f }, masterGain { 0.8f };
    std::atomic<int> instrument { (int) InputInstrument::BassVI }, tuning { (int) Tuning::Standard };
    std::atomic<float> customCents { 0 };

    // ---- split input (separate input device/clock, see SplitInput.h + DriftResampler.h)
    DriftResampler driftIn;
    /** Message thread. false: returns only once the audio thread is guaranteed not to be inside driftIn.pull(),
        so driftIn may then be prepared. true: the DI comes from driftIn instead of the output device's input. */
    void setSplitInputActive (bool on)
    {
        splitActive.store (on);
        if (! on) while (inPull.load()) std::this_thread::yield();
    }
    bool isSplitInputActive() const { return splitActive.load(); }

    // ---- microphone (a second input-only device on its own clock, own DriftResampler). Recorded as the "Mic" stem
    //      (last stem); never fed to the chord follower. Monitor is off by default (feedback / doubled voice).
    DriftResampler driftMic;
    void setMicInputActive (bool on)
    {
        micActive.store (on);
        if (! on) while (inPull.load()) std::this_thread::yield();
    }
    bool isMicInputActive() const { return micActive.load(); }
    std::atomic<float> micGain { 1.0f }, micPeak { 0 };
    std::atomic<bool> micMonitor { false };
    /** Message thread: start a take with the current stems (+ "Mic" while a mic input is open). */
    bool startRecording (const juce::File& folder);

    // ---- latency compensation (message thread; see updateStemDelays)
    /** Paths in samples: outLatency = output device latency; guitarIn / micIn = device input latency + drift FIFO
        target (0 FIFO for a combined device). trim = user offset added to the round trip (calibration). */
    void setLatencyPaths (int outLatency, int guitarIn, int micIn);
    std::atomic<int> latencyTrimSamples { 0 };
    void updateStemDelays();
    std::atomic<int> appliedPdcSamples { 0 }, appliedRtSamples { 0 }, appliedMicSkew { 0 };
    std::atomic<int> holdBars { 4 };
    std::atomic<int> startMode { 0 };          // 0 start when I play (armed), 1 start on button
    std::atomic<bool> countInClick { true };   // button mode: 1-bar click count-in
    /** Message thread: start/stop the BAND (session keeps listening). Stop is musical (next downbeat, final hit, ring-out). */
    void bandStart() { bandStartReq = true; }
    void bandStop()  { band.requestStop(); }
    std::atomic<bool> bandStartReq { false };   // bars of player silence the band grooves through before its 2-bar fade-out (1..16)
    std::atomic<bool> strangle { false }, muteBassChair { false }, bassGiveWay { false };   // bass-chair options, off by default
    std::atomic<float> learnSeconds { 1.0f };

    /** Message thread. Rebuilds the band (prepared off the audio thread, swapped in atomically). */
    void setChairs (const std::vector<ChairSpec>&);
    /** Message thread: hot-swap a sample library loaded off-thread. Rebuilds the chairs; the audio thread picks the
        new config up on its next block (lock-free). The old library is freed later by collectLibraries(). */
    void installLibrary (std::shared_ptr<SampleLibrary> lib);
    /** Message thread (UI timer): frees retired libraries once two consecutive ticks saw no pending swap. */
    void collectLibraries();
    std::shared_ptr<const SampleLibrary> currentLibrary() const { return library; }
    std::vector<ChairSpec> getChairs() const;
    void setChairGain (int i, float g)  { if (i >= 0 && i < MaxChairs) chairGain[(size_t) i] = g; }
    void setChairMute (int i, bool m)   { if (i >= 0 && i < MaxChairs) chairMute[(size_t) i] = m; }
    void setChairSolo (int i, bool s)   { if (i >= 0 && i < MaxChairs) chairSolo[(size_t) i] = s; }

    enum class Phase { Idle, Learning, Listening };   // Listening = count-in / jamming (see status().countIn)
    void startSession();      // reset, learn noise, then wait for the count-in
    void stopSession();
    void learnNoiseNow();     // 'Learn noise' button: capture learnSeconds of input now (play nothing)
    Phase phase() const { return (Phase) phaseAtomic.load(); }
    float learnProgress() const { return learnProgressAtomic.load(); }

    // ---- telemetry
    std::atomic<float> inputPeak { 0 }, inputRms { 0 };
    std::atomic<int> bandState { 0 }, bandChairs { 0 };
    std::atomic<int64_t> analysisDropped { 0 };
    // ---- glitch telemetry (ASIO4ALL reports no xruns, so measure them ourselves)
    std::atomic<int64_t> cbCount { 0 }, cbLate { 0 }, cbGaps { 0 }, nanEvents { 0 }, clipSamples { 0 };
    std::atomic<int>     cbMaxUs { 0 }, cbBudgetUs { 0 }, nanSource { 0 };   // nanSource bit: 0 fx, 1 mic, 2+c chair c
    std::atomic<float>   cbMaxLoad { 0 };   // worst duration / budget since last takeCbMax()
    int  takeCbMaxUs() { return cbMaxUs.exchange (0); }
    int64_t glitches() const { return cbLate.load() + cbGaps.load() + nanEvents.load(); }
    int64_t lastCbTicks = 0; bool nanResetPending = false; float limG = 1.0f;    // samples the analysis FIFO had no room for
    /** UI thread only. */
    const EngineStatus& status() { uiStatus.consume(); return uiStatus.front(); }

    juce::StringArray stemNames() const;
    StemRecorder recorder;
    PluginChain plugins;    // 4 inserts on GuitarFX
    double getSampleRate() const { return sampleRate.load(); }
    int getDeviceBlockSize() const { return blockSize.load(); }

private:
    void run() override;
    void processChunk (const float* in, const float* mic, float* const* out, int numOut, int n);
    void rebuildBand();
    std::unique_ptr<BandConfig> makeConfig (const std::vector<ChairSpec>&);

    std::atomic<double> sampleRate { 48000 }; std::atomic<int> blockSize { 512 };
    std::shared_ptr<SampleLibrary> library;
    std::vector<std::shared_ptr<SampleLibrary>> oldLibraries;   // kept until no band config can still point into them
    int libraryIdleTicks = 0;
    Band band;
    mutable juce::CriticalSection chairLock; std::vector<ChairSpec> chairs;   // message thread + device start
    std::array<std::atomic<float>, MaxChairs> chairGain; std::array<std::atomic<bool>, MaxChairs> chairMute, chairSolo;

    // audio-thread state
    std::atomic<bool> splitActive { false }, inPull { false }; std::vector<float> splitBuf = std::vector<float> (MaxChunk);
    std::atomic<bool> micActive { false }, recordMic { false }; std::vector<float> micBuf = std::vector<float> (MaxChunk);
    static constexpr int MicBus = 2 + MaxChairs, NumBuses = MicBus + 1;
    juce::AudioBuffer<float> buses;     // 0 DI, 1 FX, 2.. chairs, MicBus mic; MaxChunk samples, pre-sized
    std::array<StemDelay, NumBuses> stemDelay; juce::AudioBuffer<float> recTap;   // delay-compensated recorder tap
    std::atomic<int> outLatSmp { 0 }, guitarInSmp { 0 }, micInSmp { 0 }, followerRt { 0 };
    FollowerState follower; uint32_t seenLock = 0, seenSession = 0;
    int64_t sessionClock = 0;           // samples since the current session started (== band clock)
    std::atomic<uint32_t> sessionRequest { 0 }; uint32_t sessionApplied = 0;

    // audio -> analysis (lock-free; the analysis thread polls, the audio thread never signals)
    juce::AbstractFifo fifo { 1 << 17 };
    std::vector<float> fifoBuf;
    std::atomic<int64_t> fifoWritten { 0 };          // total samples pushed (audio thread)
    std::atomic<int64_t> sessionStartAt { -1 };      // fifo index where the current session begins
    std::atomic<uint32_t> sessionStartGen { 0 };
    std::atomic<bool> learnRequest { false }, stopRequest { false };
    std::atomic<int> phaseAtomic { (int) Phase::Idle };
    std::atomic<float> learnProgressAtomic { 0 };

    // analysis-thread state
    ChordTracker tracker;
    TripleBuffer<EngineStatus> audioStatus, uiStatus;
};
} // namespace sj
