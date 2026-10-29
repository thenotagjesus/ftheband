// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#include "Engine/AudioEngine.h"

namespace sj {
static constexpr int NumFixedStems = 2;   // GuitarDI, GuitarFX

AudioEngine::AudioEngine() : juce::Thread ("Analysis")
{
    fifoBuf.resize ((size_t) fifo.getTotalSize());
    buses.setSize (NumBuses, MaxChunk); recTap.setSize (NumBuses, MaxChunk);
    for (auto& d : stemDelay) d.prepare (MaxChunk);
    for (int i = 0; i < MaxChairs; ++i) { chairGain[(size_t) i] = 0.8f; chairMute[(size_t) i] = false; chairSolo[(size_t) i] = false; }
    library = std::make_shared<SampleLibrary>(); library->load();
    band.prepare (sampleRate.load());
    setChairs ({ { ChairType::Drums, 0, 0.9f }, { ChairType::Bass, 0, 0.95f }, { ChairType::Keys, 0, 0.6f } });
    tracker.setInstrument ((InputInstrument) instrument.load()); tracker.prepare (sampleRate.load());
    startThread (juce::Thread::Priority::high);
}
AudioEngine::~AudioEngine() { stopThread (2000); }

// ------------------------------------------------------------------------------------------------ band chairs
std::unique_ptr<BandConfig> AudioEngine::makeConfig (const std::vector<ChairSpec>& specs)
{
    auto cfg = std::make_unique<BandConfig>(); auto names = chairTypeNames();
    for (size_t i = 0; i < specs.size() && i < (size_t) MaxChairs; ++i) {
        auto c = std::make_unique<Chair>(); c->type = specs[i].type; c->role = specs[i].role; c->name = names[(int) specs[i].type];
        c->inst = makeInstrument (specs[i].type, library.get());
        c->gain = chairGain[i].load(); c->mute = chairMute[i].load(); c->solo = chairSolo[i].load();
        cfg->chairs.push_back (std::move (c));
    }
    return cfg;
}
void AudioEngine::setChairs (const std::vector<ChairSpec>& specs)
{
    { const juce::ScopedLock sl (chairLock); chairs = specs; if (chairs.size() > (size_t) MaxChairs) chairs.resize ((size_t) MaxChairs); }
    for (size_t i = 0; i < specs.size() && i < (size_t) MaxChairs; ++i) { chairGain[i] = specs[i].gain; chairMute[i] = specs[i].mute; chairSolo[i] = specs[i].solo; }
    rebuildBand();
}
std::vector<ChairSpec> AudioEngine::getChairs() const
{
    const juce::ScopedLock sl (chairLock); auto c = chairs;
    for (size_t i = 0; i < c.size(); ++i) { c[i].gain = chairGain[i].load(); c[i].mute = chairMute[i].load(); c[i].solo = chairSolo[i].load(); }
    return c;
}
void AudioEngine::rebuildBand()
{
    std::vector<ChairSpec> c; { const juce::ScopedLock sl (chairLock); c = chairs; }
    // The audio thread swaps the config in on its next block and retires the old one; retry while a swap is in flight.
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (band.submit (makeConfig (c))) return;
        juce::Thread::sleep (10);
    }
    jassertfalse;   // audio thread not running: the next device start installs the chairs with setConfigNow()
}
void AudioEngine::installLibrary (std::shared_ptr<SampleLibrary> lib)
{
    if (lib == nullptr) return;
    oldLibraries.push_back (library); library = std::move (lib); libraryIdleTicks = 0;
    rebuildBand();
}
void AudioEngine::collectLibraries()
{
    if (oldLibraries.empty()) return;
    if (! band.swapIdle()) { libraryIdleTicks = 0; return; }
    if (++libraryIdleTicks >= 2) oldLibraries.clear();   // the running config was built from `library`
}
juce::StringArray AudioEngine::stemNames() const
{
    juce::StringArray s { "GuitarDI", "GuitarFX" }; auto names = chairTypeNames();
    std::map<int, int> seen; for (auto& c : getChairs()) { auto n = names[(int) c.type].replaceCharacters ("/ ", "--"); const int k = ++seen[(int) c.type]; s.add (k > 1 ? n + juce::String (k) : n); }
    if (micActive.load()) s.add ("Mic");
    return s;
}
bool AudioEngine::startRecording (const juce::File& folder)
{
    const auto names = stemNames();
    recordMic.store (names.contains ("Mic"));    // fixed for the whole take (stem count must not change mid-take)
    return recorder.start (folder, names, getSampleRate());
}

// ------------------------------------------------------------------------------------------------ latency compensation
void AudioEngine::setLatencyPaths (int outLatency, int guitarIn, int micIn)
{
    outLatSmp = juce::jmax (0, outLatency); guitarInSmp = juce::jmax (0, guitarIn); micInSmp = juce::jmax (0, micIn);
    updateStemDelays();
}
/** Recorder-tap delays, all relative to "when it was played":
      GuitarFX has the insert latency L baked in; DI gets L so it lines up with FX.
      Band stems: what the band played at t reached the player at t + out, and the player's answer reached us
      out + in later: delay them by L + RT (RT = out + guitar input path + trim).
      Mic: its own input path; whichever input is faster is delayed to the slower one (P = max of the two). */
void AudioEngine::updateStemDelays()
{
    const int L = plugins.getTotalLatencySamples();
    const int g = guitarInSmp.load(), mIn = micActive.load() ? micInSmp.load() : g, P = juce::jmax (g, mIn);
    const int rt = juce::jmax (0, outLatSmp.load() + g + latencyTrimSamples.load());
    stemDelay[0].setDelay (L + (P - g));
    stemDelay[1].setDelay (P - g);
    for (int c = 0; c < MaxChairs; ++c) stemDelay[(size_t) (2 + c)].setDelay (L + rt + (P - g));
    stemDelay[(size_t) MicBus].setDelay (L + (P - mIn));
    appliedPdcSamples = L; appliedRtSamples = rt; appliedMicSkew = mIn - g; followerRt = rt;
}

// ------------------------------------------------------------------------------------------------ session control
void AudioEngine::startSession() { stopRequest = false; bandStartReq = startMode.load() == 1; learnRequest = true; sessionRequest.fetch_add (1); }
void AudioEngine::stopSession()  { stopRequest = true; bandStartReq = false; band.requestStop(); }
void AudioEngine::learnNoiseNow() { learnRequest = true; }

// ------------------------------------------------------------------------------------------------ device
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* dev)
{
    prepareOffline (dev->getCurrentSampleRate(), dev->getCurrentBufferSizeSamples());
}
void AudioEngine::prepareOffline (double sr, int bs)
{
    sampleRate = sr; blockSize = bs;
    buses.setSize (NumBuses, MaxChunk, false, true, true); recTap.setSize (NumBuses, MaxChunk, false, true, true);
    plugins.prepare (sampleRate.load(), MaxChunk);    // the chain only ever sees MaxChunk-sized pieces (no extra block of delay)
    updateStemDelays();
    band.collectGarbage();
    band.prepare (sampleRate.load());
    { std::vector<ChairSpec> c; { const juce::ScopedLock sl (chairLock); c = chairs; } band.setConfigNow (makeConfig (c)); }
    sessionRequest.fetch_add (1);                    // fresh session clock for the new device (tracker re-prepared at the new rate)
}
void AudioEngine::audioDeviceStopped() { recorder.stop(); plugins.release(); }

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                                    int n, const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals nd;
    const int64_t t0 = juce::Time::getHighResolutionTicks();
    const double tps = (double) juce::Time::getHighResolutionTicksPerSecond();
    const double budget = n / juce::jmax (1.0, sampleRate.load());          // seconds of audio this callback must deliver
    if (lastCbTicks != 0 && cbCount.load (std::memory_order_relaxed) > 50 && (double) (t0 - lastCbTicks) / tps > 1.8 * budget)
        cbGaps.fetch_add (1);                                               // driver called us late / skipped a period
    lastCbTicks = t0;
    const float* input = (numIn > 0) ? in[0] : nullptr;
    float* outs[2] = { numOut > 0 ? out[0] : nullptr, numOut > 1 ? out[1] : nullptr };
    for (int c = 2; c < numOut; ++c) if (out[c]) juce::FloatVectorOperations::clear (out[c], n);
    inPull.store (true);                                    // Dekker handshake with setSplitInputActive (false)
    const bool split = splitActive.load(), mic = micActive.load();
    for (int pos = 0; pos < n; pos += MaxChunk) {           // any device block size: process in pre-sized chunks
        const int len = juce::jmin (MaxChunk, n - pos);
        float* o[2] = { outs[0] ? outs[0] + pos : nullptr, outs[1] ? outs[1] + pos : nullptr };
        if (mic) driftMic.pull (micBuf.data(), len);
        if (split) { driftIn.pull (splitBuf.data(), len); processChunk (splitBuf.data(), mic ? micBuf.data() : nullptr, o, 2, len); }
        else processChunk (input ? input + pos : nullptr, mic ? micBuf.data() : nullptr, o, 2, len);
    }
    inPull.store (false);
    const double dur = (double) (juce::Time::getHighResolutionTicks() - t0) / tps;
    cbCount.fetch_add (1);
    if (dur > 0.7 * budget) cbLate.fetch_add (1);                           // >70 % of the period: the driver's own work will push it over
    const int us = (int) (dur * 1e6); if (us > cbMaxUs.load()) cbMaxUs.store (us);
    cbBudgetUs.store ((int) (budget * 1e6));
}

void AudioEngine::processChunk (const float* in, const float* mic, float* const* out, int numOut, int n)
{
    // ---- session (re)start: band clock and analysis stream restart on the same sample
    const uint32_t req = sessionRequest.load();
    if (req != sessionApplied) {
        sessionApplied = req; sessionClock = 0; follower = {}; seenLock = 0;
        band.reset(); band.bpm = fallbackBpm.load();
        sessionStartAt.store (fifoWritten.load()); sessionStartGen.store (req);
    }
    buses.clear (0, n);
    float* di = buses.getWritePointer (0); float* fx = buses.getWritePointer (1);
    if (in != nullptr) juce::FloatVectorOperations::copy (di, in, n);

    float peak = 0, sumSq = 0; const bool drv = placeholderDrive.load();
    for (int i = 0; i < n; ++i) { const float x = di[i]; peak = juce::jmax (peak, std::abs (x)); sumSq += x * x;
        fx[i] = drv ? std::tanh (4.0f * x) * 0.5f : x; }          // optional placeholder drive (amp sim goes in the inserts); clean DI by default
    inputPeak = juce::jmax (peak, inputPeak.load() * 0.95f); inputRms = std::sqrt (sumSq / (float) juce::jmax (1, n));
    plugins.process (fx, n);

    // ---- feed the analysis thread: lock-free, no wake-up call (the analysis thread polls every 2 ms)
    if (fifo.getFreeSpace() >= n) {
        auto w = fifo.write (n);
        if (w.blockSize1 > 0) juce::FloatVectorOperations::copy (fifoBuf.data() + w.startIndex1, di, w.blockSize1);
        if (w.blockSize2 > 0) juce::FloatVectorOperations::copy (fifoBuf.data() + w.startIndex2, di + w.blockSize1, w.blockSize2);
        fifoWritten.fetch_add (n);
    } else analysisDropped.fetch_add (n);   // the whole block is skipped; the analysis clock notices via fifoWritten

    // ---- latest follower state; on a fresh count-in lock, re-phase the band so bar 1 lands on the next downbeat
    audioStatus.consume();
    const int64_t rtNow = followerRt.load (std::memory_order_relaxed);
    const auto& s = audioStatus.front();
    if (s.session == sessionApplied) {
        follower.chord = s.chord; follower.keyTonic = s.key.tonic; follower.keyMinor = s.key.minor; follower.intensity = s.intensity;
        follower.silent = s.silent; follower.countIn = s.countIn; follower.lastOnsetSample = s.lastOnset - rtNow; follower.lastActiveSample = s.lastActive - rtNow;   // input time -> when it was played
        if (s.beatLocked && s.lockGeneration != seenLock && s.bpm > 0) {
            seenLock = s.lockGeneration; band.bpm = s.bpm;
            band.alignBeatTo (s.beatPhase - rtNow + (int64_t) std::llround (60.0 * sampleRate.load() / s.bpm), true);   // band lands on the beat as heard
        }
    } else { follower.silent = true; follower.countIn = true; }
    if (! s.beatLocked || s.session != sessionApplied) band.bpm = fallbackBpm.load();

    // ---- band
    band.style = style.load();
    band.playerIsBass = bassGiveWay.load(); band.holdBars = holdBars.load(); band.startMode = startMode.load(); band.countInClick = countInClick.load();
    if (phaseAtomic.load() == (int) Phase::Listening && bandStartReq.exchange (false)) band.requestStart();
    band.muteBassForBassPlayer = muteBassChair.load();
    const int nc = juce::jmin (band.numChairs(), MaxChairs);
    if (auto* cfg = band.current())
        for (int c = 0; c < nc; ++c) { auto& ch = *cfg->chairs[(size_t) c];
            ch.gain = chairGain[(size_t) c].load(); ch.mute = chairMute[(size_t) c].load(); ch.solo = chairSolo[(size_t) c].load(); }
    float* chairOut[MaxChairs]; for (int c = 0; c < MaxChairs; ++c) chairOut[c] = buses.getWritePointer (NumFixedStems + c);
    band.process (follower, chairOut, MaxChairs, n);
    bandState = (int) band.state; bandChairs = band.numChairs();
    sessionClock += n;

    // ---- stereo mix (equal-power pans), master gain, hard ceiling
    static constexpr float pans[] = { 0.0f, 0.0f, 0.25f, -0.35f, 0.4f, -0.15f, 0.3f, -0.3f };
    float* L = out[0]; float* R = numOut > 1 ? out[1] : nullptr;
    if (L) juce::FloatVectorOperations::clear (L, n); if (R) juce::FloatVectorOperations::clear (R, n);
    auto add = [&] (const float* src, float pan, float g) {
        const float a = (pan + 1) * juce::MathConstants<float>::halfPi * 0.5f;
        if (L) juce::FloatVectorOperations::addWithMultiply (L, src, std::cos (a) * g * 1.4142f, n);
        if (R) juce::FloatVectorOperations::addWithMultiply (R, src, std::sin (a) * g * 1.4142f, n); };
    if (monitorGuitar.load()) add (fx, -0.2f, guitarGain.load());
    // the band's latest output pre-gain is already in its bus (Band applies chair gain/mute/solo)
    buses.applyGain (1, 0, n, guitarGain.load());   // GuitarFX stem = what you hear (DI stays raw)
    for (int c = 0; c < nc; ++c) add (chairOut[c], pans[c], 1.0f);
    // ---- microphone: own bus (Mic stem), never into the analysis FIFO; monitored only on request
    float* mb = buses.getWritePointer (MicBus);
    if (mic != nullptr) {
        juce::FloatVectorOperations::copyWithMultiply (mb, mic, micGain.load(), n);
        float mp = 0; for (int i = 0; i < n; ++i) mp = juce::jmax (mp, std::abs (mb[i]));
        micPeak = juce::jmax (mp, micPeak.load() * 0.95f);
        if (micMonitor.load()) add (mb, 0.0f, 1.0f);
    } else micPeak = micPeak.load() * 0.95f;
    // ---- NaN/inf guard: one bad sample would poison every filter/reverb downstream (constant static or silence)
    {
        auto bad = [n] (const float* p) { float s = 0; for (int i = 0; i < n; ++i) s += p[i] * 0.0f; return ! std::isfinite (s); };
        int src = 0;
        if (bad (fx)) src |= 1;
        if (mic != nullptr && bad (mb)) src |= 2;
        for (int c = 0; c < nc; ++c) if (bad (chairOut[c])) src |= 4 << c;
        if ((L && bad (L)) || (R && bad (R)) || src) {
            nanEvents.fetch_add (1); nanSource.store (nanSource.load() | src | (src == 0 ? 1 << 30 : 0));
            if (L) juce::FloatVectorOperations::clear (L, n); if (R) juce::FloatVectorOperations::clear (R, n);
            for (int b = 0; b < NumBuses; ++b) { auto* p = buses.getWritePointer (b); for (int i = 0; i < n; ++i) if (! std::isfinite (p[i])) p[i] = 0; }
            band.reset();                                                     // flush poisoned voice state
        }
    }
    const float mg = masterGain.load();
    for (auto* ch : { L, R }) if (ch) juce::FloatVectorOperations::multiply (ch, mg, n);
    // ---- safety limiter (was a hard clip at +-1: monitored guitar + band + inserts summed past 0 dBFS = crackle on every peak)
    {   const float ceil = 0.94f, rel = 1.0f - std::exp (-1.0f / (0.15f * (float) sampleRate.load())); int64_t overs = 0;
        for (int i = 0; i < n; ++i) {
            const float pk = juce::jmax (L ? std::abs (L[i]) : 0.0f, R ? std::abs (R[i]) : 0.0f);
            if (pk > 1.0f) ++overs;
            if (pk * limG > ceil) limG = ceil / pk; else limG += (1.0f - limG) * rel;
            if (L) L[i] *= limG; if (R) R[i] *= limG; }
        if (overs) clipSamples.fetch_add (overs, std::memory_order_relaxed);
        for (auto* ch : { L, R }) if (ch) juce::FloatVectorOperations::clip (ch, ch, -1.0f, 1.0f, n); }

    // ---- stems: all buses of this chunk in one call (the recorder keeps them sample-aligned)
    // delay-compensated tap: every line runs every chunk so it is always full; the monitor mix above is undelayed
    for (int b = 0; b < NumBuses; ++b) { recTap.copyFrom (b, 0, buses, b, 0, n); stemDelay[(size_t) b].process (recTap.getWritePointer (b), n); }
    const float* ptrs[NumBuses]; int ns = 0;
    for (int i = 0; i < NumFixedStems + nc; ++i) ptrs[ns++] = recTap.getReadPointer (i);
    if (recordMic.load (std::memory_order_relaxed)) ptrs[ns++] = recTap.getReadPointer (MicBus);
    recorder.write (ptrs, ns, n);
}

// ------------------------------------------------------------------------------------------------ analysis thread
void AudioEngine::run()
{
    juce::ScopedNoDenormals nd;
    std::vector<float> chunk (4096), learnBuf, lastLearn;
    int64_t readTotal = 0;            // fifo index of the next sample we read
    uint32_t gen = 0;                 // session generation in effect
    int64_t sessIdx = 0, trackerOffset = 0;   // session-relative sample index; session index of the tracker's sample 0
    bool tracking = false, learning = false, learningMidSession = false;
    uint32_t lockGen = 0; bool wasLocked = false;
    int cInst = -1, cTun = -1; float cCents = 0; bool cStr = false; double cSr = 0;

    auto configure = [&] {
        cInst = instrument.load(); cTun = tuning.load(); cCents = customCents.load(); cStr = strangle.load(); cSr = sampleRate.load();
        tracker.setInstrument ((InputInstrument) cInst, (Tuning) cTun, cCents); tracker.strangleHint = cStr; tracker.prepare (cSr);
        if (! lastLearn.empty()) tracker.learnNoiseFrom (lastLearn.data(), (int) lastLearn.size());
        trackerOffset = sessIdx; wasLocked = false;
    };
    auto publish = [&] {
        for (auto* tb : { &audioStatus, &uiStatus }) {
            auto& s = tb->back();
            s.chord = tracker.chord; s.key = tracker.key; s.chroma = tracker.chroma;
            s.bpm = tracker.beat.bpm; s.beatLocked = tracker.beat.locked; s.beatPhase = tracker.beat.beatPhase + trackerOffset; s.lockGeneration = lockGen;
            s.countIn = ! tracking || tracker.countInActive(); s.silent = ! tracking || tracker.isSilent();
            s.intensity = tracker.intensity; s.rmsDb = tracker.rmsDb; s.gateDb = tracker.gate(); s.floorDb = tracker.noiseFloorDb;
            s.lastOnset = tracker.lastOnset + trackerOffset; s.lastActive = tracker.lastActive + trackerOffset; s.onsetCount = tracker.onsetCount; s.bassMidi = tracker.bassMidi;
            s.strangleDetected = tracker.strangleDetected; s.lowRatioDb = tracker.lowRatioDb;
            s.nNotches = tracker.nNotches; for (int k = 0; k < tracker.nNotches; ++k) s.notchHz[k] = tracker.notchHz[k];
            s.noiseLearned = tracker.noiseLearned; s.session = gen;
            tb->publish();
        }
    };
    auto finishLearning = [&] {
        lastLearn = learnBuf; learnBuf.clear();
        tracker.learnNoiseFrom (lastLearn.data(), (int) lastLearn.size());
        learning = learningMidSession = false; learnProgressAtomic = 1.0f;
    };

    while (! threadShouldExit()) {
        const int ready = fifo.getNumReady();
        if (ready == 0) { wait (2); continue; }      // polling: the audio thread never signals (no mutex in the callback)
        const int k = juce::jmin (ready, (int) chunk.size());
        { auto r = fifo.read (k);
          if (r.blockSize1 > 0) std::copy_n (fifoBuf.data() + r.startIndex1, r.blockSize1, chunk.data());
          if (r.blockSize2 > 0) std::copy_n (fifoBuf.data() + r.startIndex2, r.blockSize2, chunk.data() + r.blockSize1); }
        int pos = 0;
        // session boundary inside (or before) this chunk?
        const uint32_t g = sessionStartGen.load();
        if (g != gen) {
            const int64_t at = sessionStartAt.load();
            if (at < readTotal + k) {
                pos = (int) juce::jlimit<int64_t> (0, k, at - readTotal);
                gen = g; sessIdx = 0; lockGen = 0; tracking = true;
                learning = learnRequest.exchange (false); learningMidSession = false; learnBuf.clear(); learnProgressAtomic = 0;
                configure();
                phaseAtomic = (int) (learning ? Phase::Learning : Phase::Listening);
            }
        }
        if (stopRequest.exchange (false)) { tracking = false; learning = false; phaseAtomic = (int) Phase::Idle; }
        if (! learning && learnRequest.exchange (false)) { learning = learningMidSession = true; learnBuf.clear(); learnProgressAtomic = 0; }
        if (instrument.load() != cInst || tuning.load() != cTun || customCents.load() != cCents || strangle.load() != cStr) configure();

        const int need = (int) (learnSeconds.load() * cSr);
        while (pos < k) {
            int m = k - pos;
            if (learning) {
                m = juce::jmin (m, need - (int) learnBuf.size());
                learnBuf.insert (learnBuf.end(), chunk.begin() + pos, chunk.begin() + pos + m);
                learnProgressAtomic = (float) learnBuf.size() / (float) juce::jmax (1, need);
                if (! learningMidSession) { trackerOffset = sessIdx + m; }   // session start: the tracker begins after the learnt second
                if ((int) learnBuf.size() >= need) { finishLearning(); phaseAtomic = (int) (tracking ? Phase::Listening : Phase::Idle); }
                if (learningMidSession && tracking) tracker.process (chunk.data() + pos, m);
            } else if (tracking) tracker.process (chunk.data() + pos, m);
            pos += m; sessIdx += m;
        }
        if (tracker.beat.locked && ! wasLocked) ++lockGen;
        wasLocked = tracker.beat.locked;
        readTotal += k;
        publish();
    }
}
} // namespace sj
