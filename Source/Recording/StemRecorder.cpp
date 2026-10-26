// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Session Jam contributors
#include "Recording/StemRecorder.h"

namespace sj {
StemRecorder::StemRecorder() : juce::Thread ("Stem writer")
{
    buf.assign ((size_t) FifoFrames * MaxStems, 0.0f); deint.setSize (MaxStems, 4096);
    startThread (juce::Thread::Priority::normal);
}
StemRecorder::~StemRecorder() { stop(); stopThread (2000); }

bool StemRecorder::start (const juce::File& folder, const juce::StringArray& names, double sr)
{
    stop();
    if (names.isEmpty() || names.size() > MaxStems) return false;
    folder.createDirectory();
    auto take = juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S");
    juce::WavAudioFormat wav;
    std::vector<std::unique_ptr<juce::AudioFormatWriter>> fresh;
    for (auto& n : names) {
        auto f = folder.getChildFile (take + "_" + n + ".wav");
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
        if (os == nullptr) return false;
        auto* w = wav.createWriterFor (os.get(), sr, 1, 24, {}, 0);
        if (w == nullptr) return false;
        os.release();
        fresh.emplace_back (w);
    }
    const juce::ScopedLock sl (drainLock);
    writers = std::move (fresh); fifo.reset(); nStems = names.size();
    lastFolder = folder; written = 0; droppedBlocks = 0; droppedSamples = 0;
    active = true;
    return true;
}

void StemRecorder::stop()
{
    active = false;
    while (inWrite.load() != 0) juce::Thread::yield();   // the audio thread finishes the block it is writing
    const juce::ScopedLock sl (drainLock);
    drain();
    writers.clear();     // flush + close
    nStems = 0;
}

void StemRecorder::write (const float* const* ch, int numStems, int n)
{
    inWrite.fetch_add (1);
    if (active.load()) {
        const int ns = juce::jmin (numStems, nStems.load());
        const int total = nStems.load();
        if (fifo.getFreeSpace() < n) { ++droppedBlocks; droppedSamples += n; }   // all stems lose the same block
        else {
            auto w = fifo.write (n);
            auto put = [&] (int start, int len, int off) {
                for (int i = 0; i < len; ++i) { float* f = buf.data() + (size_t) (start + i) * MaxStems;
                    for (int s = 0; s < total; ++s) f[s] = s < ns ? ch[s][off + i] : 0.0f; } };
            put (w.startIndex1, w.blockSize1, 0); put (w.startIndex2, w.blockSize2, w.blockSize1);
            written += n;
        }
    }
    inWrite.fetch_sub (1);
}

void StemRecorder::drain()   // caller holds drainLock
{
    const int ns = nStems.load(); if (ns == 0 || writers.empty()) return;
    for (;;) {
        const int k = juce::jmin (fifo.getNumReady(), deint.getNumSamples()); if (k <= 0) return;
        { auto r = fifo.read (k);
          auto get = [&] (int start, int len, int off) {
              for (int i = 0; i < len; ++i) { const float* f = buf.data() + (size_t) (start + i) * MaxStems;
                  for (int s = 0; s < ns; ++s) deint.setSample (s, off + i, f[s]); } };
          get (r.startIndex1, r.blockSize1, 0); get (r.startIndex2, r.blockSize2, r.blockSize1); }
        for (int s = 0; s < ns && s < (int) writers.size(); ++s) { const float* p = deint.getReadPointer (s); writers[(size_t) s]->writeFromFloatArrays (&p, 1, k); }
    }
}

void StemRecorder::run()
{
    while (! threadShouldExit()) {
        { const juce::ScopedLock sl (drainLock); drain(); }
        wait (20);
    }
}
} // namespace sj
