// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Session Jam contributors
#pragma once
#include <JuceHeader.h>

namespace sj {
/** Records N mono stems to separate 24-bit WAVs, sample-aligned by construction.
    The audio thread reserves ONE frame block (all stems interleaved) in a single lock-free FIFO: either the whole
    block goes in for every stem, or (FIFO full) it is dropped from every stem and counted — stems never drift apart.
    A background thread drains the FIFO into plain AudioFormatWriters. No locks on the audio thread. */
class StemRecorder : private juce::Thread {
public:
    static constexpr int MaxStems = 16;
    StemRecorder();
    ~StemRecorder() override;
    /** Message thread. Creates <folder>/<take>_<stem>.wav for each stem name. */
    bool start (const juce::File& folder, const juce::StringArray& stemNames, double sampleRate);
    void stop();
    bool isRecording() const { return active.load(); }
    /** Audio thread. channels[i] = stem i (mono), all numSamples long. */
    void write (const float* const* channels, int numStems, int numSamples);
    juce::File lastFolder;
    int64_t samplesWritten() const { return written.load(); }
    std::atomic<int> droppedBlocks { 0 };      // whole blocks lost on every stem (disk too slow)
    std::atomic<int64_t> droppedSamples { 0 };
private:
    void run() override;
    void drain();
    static constexpr int FifoFrames = 1 << 16;  // ~1.4 s at 48 kHz, per stem
    juce::AbstractFifo fifo { FifoFrames };
    std::vector<float> buf;                     // FifoFrames * numStems, interleaved by frame
    std::vector<std::unique_ptr<juce::AudioFormatWriter>> writers;
    juce::AudioBuffer<float> deint;
    std::atomic<int> nStems { 0 };
    std::atomic<bool> active { false };
    std::atomic<int> inWrite { 0 };             // audio thread is inside write(): stop() waits for 0
    std::atomic<int64_t> written { 0 };
    juce::CriticalSection drainLock;            // drain thread vs stop() (never the audio thread)
};
} // namespace sj
