// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#include "Cli/Cli.h"
#include "Analysis/ChordTracker.h"
#include "Analysis/Loudness.h"
#include "Engine/Band.h"
#include "Test/GuitarSynth.h"
#include <cstdio>
#include <map>

#include "../Engine/DriftResampler.h"
namespace sj {
namespace {
constexpr double SR = 48000.0;
static void out (const juce::String& s) { std::printf ("%s\n", s.toRawUTF8()); std::fflush (stdout); }

bool readMono (const juce::File& f, std::vector<float>& x, double& sr) {
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (! r) return false;
    sr = r->sampleRate;
    juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
    x.assign ((size_t) b.getNumSamples(), 0.0f);
    for (int c = 0; c < b.getNumChannels(); ++c) juce::FloatVectorOperations::addWithMultiply (x.data(), b.getReadPointer (c), 1.0f / (float) b.getNumChannels(), b.getNumSamples());
    if (std::abs (sr - SR) > 1) {   // resample to 48k
        juce::LagrangeInterpolator li; const double ratio = sr / SR;
        std::vector<float> y ((size_t) ((double) x.size() / ratio));
        li.process (ratio, x.data(), y.data(), (int) y.size()); x.swap (y); sr = SR;
    }
    return true;
}
bool writeWav (const juce::File& f, const std::vector<const float*>& ch, size_t n) {
    f.deleteFile(); f.getParentDirectory().createDirectory();
    juce::WavAudioFormat wav;
    auto os = f.createOutputStream(); if (! os) return false;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), SR, (unsigned) ch.size(), 24, {}, 0));
    if (! w) return false; os.release();
    return w->writeFromFloatArrays (ch.data(), (int) ch.size(), (int) n);
}

// ---------------------------------------------------------------------------------------------- tempo (offline)
struct Tempo { double bpm = 0; int64_t phase = 0; };
Tempo estimateTempo (const std::vector<int64_t>& onsets, size_t len, double lo = 60, double hi = 180) {
    Tempo t; if (onsets.size() < 6) return t;
    const double fps = 200; std::vector<float> env ((size_t) ((double) len / SR * fps) + 1, 0.0f);
    for (auto o : onsets) { size_t i = (size_t) ((double) o / SR * fps); for (int d = -2; d <= 2; ++d) if (i + d < env.size()) env[i + d] += 1.0f - 0.3f * std::abs (d); }
    double best = -1;
    for (double bpm = lo; bpm <= hi; bpm += 0.25) {
        const double per = fps * 60 / bpm; double s = 0;
        for (int m = 1; m <= 4; ++m) { const double lag = per * m; const double w = (m == 1 || m == 2 || m == 4) ? 1.0 : 0.5;
            for (size_t i = 0; i + (size_t) lag + 1 < env.size(); ++i) { double fr = lag - std::floor (lag); s += w * env[i] * (env[i + (size_t) lag] * (1 - fr) + env[i + (size_t) lag + 1] * fr); } }
        s *= 1.0 + 0.15 * std::exp (-std::pow (std::log2 (bpm / 110.0), 2) / 0.5);   // mild prior around 110
        if (s > best) { best = s; t.bpm = bpm; }
    }
    const double per = SR * 60 / t.bpm; double bestP = -1;
    for (int k = 0; k < 64; ++k) { const double ph = per * k / 64; double s = 0;
        for (auto o : onsets) { double e = std::fmod ((double) o - ph, per); if (e < 0) e += per; e = juce::jmin (e, per - e); s += std::exp (-e * e / (2 * std::pow (0.03 * SR, 2))); }
        if (s > bestP) { bestP = s; t.phase = (int64_t) ph; } }
    return t;
}

// ---------------------------------------------------------------------------------------------- tracking pass
struct TrackRow { int64_t t; ChordLabel chord, frame; float intensity, rms; bool silent; };
struct TrackResult { std::vector<TrackRow> rows; std::vector<int64_t> onsets; KeyEstimate key; std::vector<std::pair<int64_t, ChordLabel>> changes; float floorDb = 0; };
// global CLI options: --instrument <guitar|guitar-dropd|bassvi|bass4>  --strangle
InputInstrument gInstrument = InputInstrument::GuitarStandard; bool gStrangle = false, gMuteBassChair = false, gBassGiveWay = false;
int gStartMode = 0; bool gCountInClick = true; double gStartAt = -1, gStopAt = -1;
Tuning gTuning = Tuning::Standard; float gCents = 0; bool gLearnNoise = true;   // --tuning <id> --cents <c> --no-learn-noise
/** Offline 'learn noise': the quiet lead-in before the first note (what the app captures before the count-in).
    Returns its length in samples (0 if there is less than 0.3 s of it). Capped at 2 s. */
int leadingQuiet (const std::vector<float>& x) {
    const int B = 1024; std::vector<float> db;
    for (size_t p = 0; p + B <= x.size() && p < (size_t) (SR * 4); p += B) { double e = 0; for (int i = 0; i < B; ++i) e += (double) x[p + (size_t) i] * x[p + (size_t) i]; db.push_back ((float) (10 * std::log10 (e / B + 1e-12))); }
    if (db.size() < 8) return 0;
    const float base = *std::min_element (db.begin(), db.begin() + 8);
    size_t k = 0; while (k < db.size() && db[k] < base + 10.0f) ++k;
    const int len = juce::jmin ((int) (2 * SR), (int) k * B - 2048);
    return len >= (int) (0.3 * SR) ? len : 0;
}
struct NoiseInfo { int samples = 0; float floorDb = -100; juce::StringArray notches; };
NoiseInfo gLastNoise;
void learnNoise (ChordTracker& tr, const std::vector<float>& x) {
    gLastNoise = {}; if (! gLearnNoise) return;
    if (const int q = leadingQuiet (x)) { tr.learnNoiseFrom (x.data(), q); gLastNoise.samples = q; gLastNoise.floorDb = tr.noiseFloorDb;
        for (int k = 0; k < tr.nNotches; ++k) gLastNoise.notches.add (juce::String (tr.notchHz[k], 1)); }
}
void highPass2 (std::vector<float>& x, float hz) {   // 2nd-order Butterworth (12 dB/oct): strangle-switch simulation
    juce::dsp::IIR::Filter<float> f; f.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (SR, hz, 0.7071f);
    for (auto& s : x) s = f.processSample (s);
}
struct TrackStats { bool strangleDetected = false; float lowRatioDb = 0; };
TrackStats gLastTrack;
TrackResult track (const std::vector<float>& x, int block = 256, bool countInGate = true, InputInstrument inst = gInstrument, bool strangle = gStrangle) {
    ChordTracker tr; tr.setInstrument (inst, gTuning, gCents); tr.strangleHint = strangle; tr.prepare (SR); tr.params.countInGate = countInGate; learnNoise (tr, x); TrackResult r; int lastCount = 0;
    for (size_t p = 0; p < x.size(); p += (size_t) block) {
        const int n = (int) juce::jmin<size_t> ((size_t) block, x.size() - p);
        if (tr.process (x.data() + p, n)) r.changes.push_back ({ (int64_t) (p + (size_t) n), tr.chord });
        if (tr.onsetCount != lastCount) { lastCount = tr.onsetCount; r.onsets.push_back (tr.lastOnset); }
        r.rows.push_back ({ (int64_t) p, tr.chord, tr.frameChord, tr.intensity, tr.rmsDb, tr.isSilent() });
    }
    r.key = tr.key; r.floorDb = tr.noiseFloorDb; gLastTrack = { tr.strangleDetected, tr.lowRatioDb }; return r;
}

// ---------------------------------------------------------------------------------------------- band jam
struct JamOptions { int style = 0; double bpm = 100; int64_t beatPhase = -1; bool downbeat = true; bool halfBar = false; float guitarGain = 1.0f; std::vector<ChairType> chairs { ChairType::Drums, ChairType::Bass, ChairType::Keys }; InputInstrument instrument = gInstrument; bool strangle = gStrangle; bool muteBassChair = gMuteBassChair; bool bassGiveWay = gBassGiveWay; };
struct JamResult { std::vector<float> L, R; std::vector<std::vector<float>> stems; std::vector<Band::LogEntry> log; std::vector<juce::String> names; TrackResult tr; double lufs = 0, peakDb = 0, gain = 1; };

SampleLibrary& library() { static SampleLibrary lib; static bool done = false; if (! done) { done = true; lib.load(); } return lib; }

JamResult jam (const std::vector<float>& gtr, const JamOptions& o) {
    JamResult j; Band band; band.prepare (SR); band.style = o.style; band.bpm = o.bpm; band.halfBarQuantize = o.halfBar; band.log = &j.log;
    band.startMode = gStartMode; band.countInClick = gCountInClick; band.playerIsBass = o.bassGiveWay; band.muteBassForBassPlayer = o.muteBassChair;
    auto cfg = std::make_unique<BandConfig>();
    auto names = chairTypeNames();
    for (auto t : o.chairs) { auto c = std::make_unique<Chair>(); c->type = t; c->name = names[(int) t]; c->inst = makeInstrument (t, &library());
        c->gain = t == ChairType::Drums ? 0.9f : t == ChairType::Bass ? 0.95f : t == ChairType::Percussion ? 0.5f : t == ChairType::Pad ? 0.35f : 0.6f; cfg->chairs.push_back (std::move (c)); }
    const int nc = (int) cfg->chairs.size();
    band.setConfigNow (std::move (cfg));
    if (o.beatPhase >= 0) band.alignBeatTo (o.beatPhase, o.downbeat);
    ChordTracker tr; tr.setInstrument (o.instrument, gTuning, gCents); tr.strangleHint = o.strangle; tr.prepare (SR); learnNoise (tr, gtr);
    const size_t n = gtr.size() + (size_t) (SR * 2);
    j.stems.assign ((size_t) nc, std::vector<float> (n, 0.0f));
    std::vector<float*> ptrs ((size_t) nc); int lastCount = 0;
    const int B = 256; std::vector<float> zero ((size_t) B, 0.0f);
    for (size_t p = 0; p < n; p += B) {
        const int len = (int) juce::jmin<size_t> (B, n - p);
        const float* in = p < gtr.size() ? gtr.data() + p : zero.data();
        const int inLen = p < gtr.size() ? (int) juce::jmin<size_t> ((size_t) len, gtr.size() - p) : len;
        if (tr.process (in, inLen)) j.tr.changes.push_back ({ (int64_t) p, tr.chord });
        if (tr.onsetCount != lastCount) { lastCount = tr.onsetCount; j.tr.onsets.push_back (tr.lastOnset); }
        FollowerState f; f.chord = tr.chord; f.keyTonic = tr.key.tonic; f.keyMinor = tr.key.minor; f.intensity = tr.intensity; f.silent = tr.isSilent(); f.countIn = tr.countInActive(); f.lastOnsetSample = tr.lastOnset; f.lastActiveSample = tr.lastActive;
        for (int c = 0; c < nc; ++c) ptrs[(size_t) c] = j.stems[(size_t) c].data() + p;
        if (gStartAt >= 0 && (double) p >= gStartAt * SR) { gStartAt = -1; band.requestStart(); }
        if (gStopAt >= 0 && (double) p >= gStopAt * SR) { gStopAt = -1; band.requestStop(); }
        band.process (f, ptrs.data(), nc, len);
    }
    j.tr.key = tr.key;
    const float pans[] = { 0.0f, 0.0f, 0.25f, -0.35f, 0.4f, -0.15f, 0.3f, -0.3f };
    j.L.assign (n, 0); j.R.assign (n, 0);
    auto addPan = [&] (const float* s, size_t len, float pan, float g) {
        const float a = (pan + 1) * juce::MathConstants<float>::halfPi * 0.5f; const float gl = std::cos (a) * g * 1.4142f, gr = std::sin (a) * g * 1.4142f;
        for (size_t i = 0; i < len; ++i) { j.L[i] += s[i] * gl; j.R[i] += s[i] * gr; } };
    // guitar: DI with a little drive/brightness so it sits in a mix
    std::vector<float> g (n, 0.0f); std::copy (gtr.begin(), gtr.end(), g.begin());
    { juce::dsp::IIR::Filter<float> hp, pk; hp.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (SR, inputProfile (o.instrument).highPassHz); pk.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (SR, 2500.0f, 0.8f, 1.6f);
      double pw = 0; for (float s : gtr) pw += (double) s * s; const float rms = (float) std::sqrt (pw / juce::jmax<size_t> (1, gtr.size())) + 1e-9f;
      const float pre = (isBassRange (o.instrument) ? 0.06f : 0.12f) / rms;
      for (auto& s : g) s = std::tanh (pk.processSample (hp.processSample (s)) * pre * 1.2f) * 0.5f; }
    addPan (g.data(), n, -0.2f, o.guitarGain);
    for (int c = 0; c < nc; ++c) { j.names.push_back (names[(int) o.chairs[(size_t) c]]); addPan (j.stems[(size_t) c].data(), n, pans[c % 8], 1.0f); }
    j.stems.insert (j.stems.begin(), g); j.names.insert (j.names.begin(), "GuitarDI");
    // loudness normalise to -14 LUFS, then a look-ahead peak limiter at -1 dBFS
    double lufs = integratedLufs (j.L.data(), j.R.data(), n, SR);
    j.gain = std::pow (10.0, (-14.0 - lufs) / 20.0);
    const float ceiling = std::pow (10.0f, -1.0f / 20.0f); const int la = 48; std::vector<float> gr (n, 1.0f);
    for (size_t i = 0; i < n; ++i) { const float pk = juce::jmax (std::abs (j.L[i]), std::abs (j.R[i])) * (float) j.gain; gr[i] = pk > ceiling ? ceiling / pk : 1.0f; }
    std::vector<float> env (n, 1.0f); float e = 1.0f;          // min over look-ahead window, then smooth release
    { std::deque<size_t> dq; for (size_t i = 0; i < n + (size_t) la; ++i) { if (i < n) { while (! dq.empty() && gr[dq.back()] >= gr[i]) dq.pop_back(); dq.push_back (i); }
        if (i >= (size_t) la) { const size_t k = i - (size_t) la; while (dq.front() + (size_t) la < k) dq.pop_front(); (void) k; }
        if (i >= (size_t) la) { const size_t k = i - (size_t) la; float m = gr[dq.front()]; for (size_t q : dq) if (q >= k && q <= k + (size_t) la) { m = juce::jmin (m, gr[q]); } env[k] = m; } } }
    for (size_t i = 0; i < n; ++i) { const float tgt = env[i]; e = tgt < e ? tgt : e + (tgt - e) * 0.0005f; const float gg = (float) j.gain * e; j.L[i] *= gg; j.R[i] *= gg; }
    for (auto& s : j.stems) for (auto& v : s) v *= (float) j.gain;
    j.lufs = integratedLufs (j.L.data(), j.R.data(), n, SR);
    float pk = 0; for (size_t i = 0; i < n; ++i) pk = juce::jmax (pk, std::abs (j.L[i]), std::abs (j.R[i])); j.peakDb = 20 * std::log10 (pk + 1e-12);
    return j;
}
juce::String fmtTime (int64_t s) { return juce::String ((double) s / SR, 2) + "s"; }

void writeJam (const JamResult& j, const juce::File& outFile, const JamOptions& o) {
    writeWav (outFile, { j.L.data(), j.R.data() }, j.L.size());
    auto stemDir = outFile.getSiblingFile (outFile.getFileNameWithoutExtension() + "_stems");
    for (size_t i = 0; i < j.stems.size(); ++i) writeWav (stemDir.getChildFile (juce::String ((int) i) + "_" + j.names[i].replace ("/", "-").replace (" ", "") + ".wav"), { j.stems[i].data() }, j.stems[i].size());
    juce::String log; log << "style " << o.style << " bpm " << o.bpm << " LUFS " << j.lufs << " peak " << j.peakDb << " dBFS\n";
    for (auto& e : j.log) {
        if (e.kind == 0) log << fmtTime (e.sample) << " BAND CHORD " << ChordLabel { e.a, e.b, -1, 0 }.name() << "\n";
        else if (e.kind == 1) log << fmtTime (e.sample) << " STATE " << (e.a == 0 ? "Out" : e.a == 1 ? "Playing" : e.a == 2 ? "Breakdown" : e.a == 3 ? "CountIn" : "Ending") << "\n";
        else if (e.kind == 2) log << fmtTime (e.sample) << " FILL bar " << e.a << "\n";
        else if (e.kind == 3 && e.a / 1000 == 1) log << fmtTime (e.sample) << " bass " << (e.a % 1000) << "\n";
    }
    for (auto& c : j.tr.changes) log << fmtTime (c.first) << " DETECTED " << c.second.name() << "\n";
    outFile.withFileExtension (".log.txt").replaceWithText (log);
}

// ---------------------------------------------------------------------------------------------- synthetic guitar parts
struct Seg { int64_t start, end; ChordLabel truth; };
/** Strummed part: chords[i] for barsPer bars each; per-bar strum pattern with dynamics; optional silent gap (in bars). */
std::vector<float> synthPart (GuitarSynth& gs, const GuitarSynth::Tuning& tu, double bpm, const std::vector<ChordLabel>& chords, int barsPer,
                              std::vector<Seg>& truth, int gapAfterIdx = -1, int gapBars = 0, float noiseSnr = 0, bool hum = false, int64_t lead = 0) {
    const double beat = SR * 60 / bpm; int64_t t = lead;
    size_t total = (size_t) (lead + beat * 4 * (barsPer * (double) chords.size() + gapBars + 2));
    std::vector<float> x (total, 0.0f);
    std::uniform_real_distribution<float> u (0, 1);
    for (size_t i = 0; i < chords.size(); ++i) {
        const auto& ch = chords[i];
        const bool inv = ch.bass >= 0 && ch.bass != ch.root;
        auto v = gs.voicing (tu, ch.root, ch.type, (int) i, inv);
        if (v.empty()) v = gs.voicing (tu, ch.root, ch.type, (int) i, false);
        const float dyn = 0.35f + 0.6f * (float) (0.5 + 0.5 * std::sin ((double) i * 1.3));
        const int64_t start = t;
        for (int b = 0; b < barsPer * 4; ++b) {
            const int64_t bt = t + (int64_t) (b * beat + (u (gs.random()) - 0.5f) * 0.01f * SR);
            gs.strum (x, bt, v, +1, dyn * (b % 4 == 0 ? 1.0f : 0.8f), false, 2.0);
            if (dyn > 0.6f) gs.strum (x, bt + (int64_t) (beat / 2), v, -1, dyn * 0.6f, false, 1.0);
        }
        t += (int64_t) (barsPer * 4 * beat);
        truth.push_back ({ start, t, ch });
        if ((int) i == gapAfterIdx) { truth.push_back ({ t, t + (int64_t) (gapBars * 4 * beat), {} }); t += (int64_t) (gapBars * 4 * beat); }
    }
    gs.body (x);
    float pk = 0; for (float s : x) pk = juce::jmax (pk, std::abs (s)); for (auto& s : x) s *= 0.4f / (pk + 1e-9f);
    if (noiseSnr > 0) gs.addNoise (x, noiseSnr, hum);
    x.resize ((size_t) t + (size_t) (beat * 4)); return x;
}
ChordLabel C (int root, int type, int bass = -1) { return { root % 12, type, bass < 0 ? root % 12 : bass % 12, 1 }; }
int majminClass (const ChordLabel& c) { if (! c.valid()) return -1; return c.root * 2 + (c.minorish() ? 1 : 0); }

// ---------------------------------------------------------------------------------------------- --eval
int runEval (const juce::File& reportFile) {
    struct Agg { int frames = 0, exact = 0, mm = 0, rootOk = 0, bassFrames = 0, bassOk = 0; std::vector<double> lat; int missed = 0; };
    std::map<std::string, Agg> by; Agg all;
    juce::String report; report << "case,tuning,bpm,noise,exact,majmin,bass,median_latency_beats,p90_latency_beats,missed\n";
    std::vector<std::pair<std::string, std::vector<ChordLabel>>> progs = {
        { "pop-I-vi-IV-V", { C(7,Maj), C(4,Min), C(0,Maj), C(2,Maj), C(7,Maj), C(4,Min), C(0,Maj), C(2,Maj) } },
        { "minor-i-VI-III-VII", { C(9,Min), C(5,Maj), C(0,Maj), C(7,Maj), C(9,Min), C(5,Maj), C(0,Maj), C(7,Maj) } },
        { "blues-7ths", { C(9,Dom7), C(2,Dom7), C(9,Dom7), C(4,Dom7), C(2,Dom7), C(9,Dom7) } },
        { "jazzish-m7", { C(2,Min7), C(7,Dom7), C(0,Maj), C(9,Min7), C(2,Min7), C(7,Dom7) } },
        { "sus", { C(2,Sus4), C(2,Maj), C(2,Sus2), C(7,Maj), C(9,Sus4), C(9,Maj) } },
        { "power", { C(4,Power), C(7,Power), C(9,Power), C(2,Power), C(4,Power), C(0,Power) } },
        { "inversions", { C(0,Maj), C(0,Maj,4), C(9,Min), C(5,Maj), C(7,Maj,11), C(0,Maj) } },
        { "unexpected", { C(4,Min), C(4,Min), C(9,Min), C(3,Maj), C(4,Min), C(1,Maj) } },
    };
    auto tunings = GuitarSynth::tunings();
    const double tempos[] = { 72, 100, 132 };
    const float noises[] = { 0, 30, 18 };
    uint32_t seed = 7;
    for (auto& [name, prog] : progs)
        for (auto& tu : tunings)
            for (double bpm : tempos)
                for (float nz : noises) {
                    if (std::string (tu.name) != "standard" && (nz == 18 || nz == 60)) continue;
                    static const bool quick = juce::SystemStats::getEnvironmentVariable ("SJ_QUICK", {}).isNotEmpty();
                    if (quick && std::string (tu.name) != "standard" && ! juce::String (tu.name).startsWith ("bassvi")) continue;
                    static const juce::String only = juce::SystemStats::getEnvironmentVariable ("SJ_ONLY", {});
                    if (only.isNotEmpty() && ! juce::String (tu.name).startsWith (only)) continue;
                    GuitarSynth gs (SR, seed++);
                    std::vector<Seg> truth;
                    auto x = synthPart (gs, tu, bpm, prog, 1, truth, -1, 0, nz, nz > 0, (int64_t) (0.5 * SR));
                    const bool bvi = juce::String (tu.name).startsWith ("bassvi");
                    if (std::string (tu.name) == "bassvi-strangle") highPass2 (x, 150.0f);
                    static const bool one = juce::SystemStats::getEnvironmentVariable ("SJ_ONE", {}).isNotEmpty();
                    if (one) { for (auto& sg : truth) std::fprintf (stderr, "TRUTH %.2f %s\n", sg.start / SR, sg.truth.name().c_str()); }
                    auto r = track (x, 256, false, bvi ? InputInstrument::BassVI : InputInstrument::GuitarStandard, false);
                    Agg a; const double beat = SR * 60 / bpm;
                    for (auto& row : r.rows) {
                        const Seg* sg = nullptr; for (auto& s : truth) if (row.t >= s.start && row.t < s.end) { sg = &s; break; }
                        if (! sg || ! sg->truth.valid()) continue;
                        if (row.t < sg->start + (int64_t) beat && sg != &truth.front()) continue;   // allow 1 beat to react
                        if (sg == &truth.front() && row.t < sg->start + (int64_t) beat) continue;
                        ++a.frames;
                        a.exact += row.chord.sameChord (sg->truth);
                        const bool pw = sg->truth.type == Power;
                        a.mm += pw ? row.chord.root == sg->truth.root : majminClass (row.chord) == majminClass (sg->truth);
                        a.rootOk += row.chord.root == sg->truth.root;
                        ++a.bassFrames; a.bassOk += row.chord.bass == sg->truth.bass;
                    }
                    for (size_t k = 1; k < truth.size(); ++k) {
                        if (! truth[k].truth.valid() || truth[k].truth.sameChord (truth[k - 1].truth)) continue;
                        double lat = -1;
                        for (auto& row : r.rows) if (row.t >= truth[k].start && row.t < truth[k].end && row.chord.sameChord (truth[k].truth)) { lat = (double) (row.t - truth[k].start) / beat; break; }
                        if (lat < 0) { ++a.missed; ++all.missed; ++by[name].missed; } else { a.lat.push_back (lat); all.lat.push_back (lat); by[name].lat.push_back (lat); }
                    }
                    auto add = [&] (Agg& d) { d.frames += a.frames; d.exact += a.exact; d.mm += a.mm; d.rootOk += a.rootOk; d.bassFrames += a.bassFrames; d.bassOk += a.bassOk; };
                    add (all); add (by[name]);
                    if (one) return 0;
                    auto lats = a.lat; std::sort (lats.begin(), lats.end());
                    const double med = lats.empty() ? -1 : lats[lats.size() / 2], p90 = lats.empty() ? -1 : lats[(size_t) (lats.size() * 0.9)];
                    report << name << "," << tu.name << "," << bpm << "," << nz << "," << (double) a.exact / juce::jmax (1, a.frames) << "," << (double) a.mm / juce::jmax (1, a.frames)
                           << "," << (double) a.bassOk / juce::jmax (1, a.bassFrames) << "," << med << "," << p90 << "," << a.missed << "\n";
                }
    auto pct = [] (int a, int b) { return juce::String (100.0 * a / juce::jmax (1, b), 1) + "%"; };
    auto line = [&] (const std::string& n, Agg& a) {
        std::sort (a.lat.begin(), a.lat.end());
        const double med = a.lat.empty() ? -1 : a.lat[a.lat.size() / 2], p90 = a.lat.empty() ? -1 : a.lat[(size_t) (a.lat.size() * 0.9)];
        out (juce::String (n).paddedRight (' ', 20) + " exact " + pct (a.exact, a.frames) + "  majmin " + pct (a.mm, a.frames) + "  root " + pct (a.rootOk, a.frames)
             + "  bass " + pct (a.bassOk, a.bassFrames) + "  latency median " + juce::String (med, 2) + " beats, p90 " + juce::String (p90, 2) + "  missed " + juce::String (a.missed) + "/" + juce::String (a.missed + (int) a.lat.size()));
    };
    out ("Chord-follow evaluation (Karplus-Strong guitar, 4 tunings x 3 tempos x clean/30dB/18dB+hum; frames scored 1 beat after each change)");
    for (auto& [n, a] : by) line (n, a);
    line ("ALL", all);
    out ("by tuning (majmin):");
    { std::map<std::string, std::pair<double,int>> t; juce::StringArray rows; rows.addLines (report); rows.remove (0);
      for (auto& l : rows) { auto c = juce::StringArray::fromTokens (l, ",", ""); if (c.size() < 6) continue; auto& e = t[c[1].toStdString()]; e.first += c[5].getDoubleValue(); e.second++; }
      for (auto& [k, v] : t) out ("  " + juce::String (k).paddedRight (' ', 18) + juce::String (100.0 * v.first / v.second, 1) + "%  (" + juce::String (v.second) + " cases)"); }
    // real-world strangle test: the user's Bass VI take, unfiltered vs a 2nd-order 150 Hz high-pass
    auto take = juce::File::getCurrentWorkingDirectory().getChildFile (juce::SystemStats::getEnvironmentVariable ("SJ_STRANGLE_TAKE", "user-takes/20261025_224630_GuitarDI.wav"));
    std::vector<float> ux; double usr = 0;
    if (take.existsAsFile() && readMono (take, ux, usr)) {
        auto ref = track (ux, 256, true, InputInstrument::BassVI, false); const auto refStats = gLastTrack;
        auto hp = ux; highPass2 (hp, 150.0f);
        auto aut = track (hp, 256, true, InputInstrument::BassVI, false); const auto autStats = gLastTrack;
        auto hint = track (hp, 256, true, InputInstrument::BassVI, true);
        auto agree = [&] (const TrackResult& a, const TrackResult& b, int& n, int& chordOk, int& rootOk, int& bassOk) {
            n = chordOk = rootOk = bassOk = 0;
            for (size_t k = 0; k < juce::jmin (a.rows.size(), b.rows.size()); ++k) { if (! a.rows[k].chord.valid()) continue; ++n;
                chordOk += a.rows[k].chord.sameChord (b.rows[k].chord); rootOk += a.rows[k].chord.root == b.rows[k].chord.root; bassOk += a.rows[k].chord.bass == b.rows[k].chord.bass; } };
        out ("strangle test on " + take.getFileName() + " (Bass VI profile; reference = unfiltered take, low/mid ratio " + juce::String (refStats.lowRatioDb, 1) + " dB, auto-strangle " + (refStats.strangleDetected ? "ON" : "off") + ")");
        int n, c, r, b;
        agree (ref, aut, n, c, r, b);
        out ("  HP150 2nd-order, auto-detect: low/mid ratio " + juce::String (autStats.lowRatioDb, 1) + " dB, strangle detected " + (autStats.strangleDetected ? "YES" : "no")
             + " | agreement with unfiltered: chord " + pct (c, n) + "  root " + pct (r, n) + "  bass " + pct (b, n) + "  key " + keyName (aut.key) + " vs " + keyName (ref.key));
        agree (ref, hint, n, c, r, b);
        out ("  HP150 2nd-order, Strangle toggle ON:                         | agreement with unfiltered: chord " + pct (c, n) + "  root " + pct (r, n) + "  bass " + pct (b, n) + "  key " + keyName (hint.key));
        auto noFix = track (hp, 256, true, InputInstrument::GuitarStandard, false);
        agree (ref, noFix, n, c, r, b);
        out ("  HP150 through the GUITAR profile (no bass/strangle handling):   | agreement with unfiltered: chord " + pct (c, n) + "  root " + pct (r, n) + "  bass " + pct (b, n));
    } else out ("strangle test skipped (no " + take.getFullPathName() + ")");
    reportFile.getParentDirectory().createDirectory(); reportFile.replaceWithText (report);
    out ("per-case CSV: " + reportFile.getFullPathName());
    return 0;
}

// ---------------------------------------------------------------------------------------------- --analyze
int runAnalyze (const juce::File& f) {
    std::vector<float> x; double sr = 0;
    if (! readMono (f, x, sr)) { out ("cannot read " + f.getFullPathName()); return 1; }
    float pk = 0; for (float s : x) pk = juce::jmax (pk, std::abs (s));
    int clips = 0; for (float s : x) clips += std::abs (s) > 0.999f;
    std::vector<float> blk; for (size_t p = 0; p + 4800 <= x.size(); p += 4800) { double e = 0; for (size_t i = 0; i < 4800; ++i) e += (double) x[p + i] * x[p + i]; blk.push_back ((float) (10 * std::log10 (e / 4800 + 1e-20))); }
    auto sorted = blk; std::sort (sorted.begin(), sorted.end());
    out ("file " + f.getFullPathName() + "  " + juce::String ((double) x.size() / SR, 2) + " s");
    out ("peak " + juce::String (20 * std::log10 (pk + 1e-12), 1) + " dBFS, clipped samples " + juce::String (clips) + ", 100ms-RMS p5/p50/p95 " + juce::String (sorted[sorted.size() / 20], 1) + "/" + juce::String (sorted[sorted.size() / 2], 1) + "/" + juce::String (sorted[sorted.size() * 19 / 20], 1) + " dBFS");
    auto r = track (x);
    auto tp = estimateTempo (r.onsets, x.size());
 out ("learn noise: " + (gLastNoise.samples > 0 ? "first " + juce::String (gLastNoise.samples / SR, 2) + " s, floor " + juce::String (gLastNoise.floorDb, 1) + " dBFS, hum notches " + (gLastNoise.notches.isEmpty() ? juce::String ("none") : gLastNoise.notches.joinIntoString (", ") + " Hz") : juce::String (gLearnNoise ? "no quiet lead-in found" : "off")));
    out ("tuning (reference only): " + tuningNames()[(int) gTuning] + (gTuning == Tuning::Custom ? " " + juce::String (gCents, 0) + " cents" : juce::String()));
    out ("instrument " + inputInstrumentNames()[(int) gInstrument] + ", strangle toggle " + (gStrangle ? "ON" : "off") + ", auto-strangle " + (gLastTrack.strangleDetected ? "detected" : "not detected") + " (E<100 Hz / E100-400 Hz " + juce::String (gLastTrack.lowRatioDb, 1) + " dB)");
    out ("noise floor (tracker) " + juce::String (r.floorDb, 1) + " dBFS, onsets " + juce::String ((int) r.onsets.size()) + ", tempo ~" + juce::String (tp.bpm, 1) + " BPM (beat phase " + fmtTime (tp.phase) + "), key " + keyName (r.key));
    // timeline: committed chord, silence rows hold the previous chord, bass-only changes ignored,
    // segments shorter than half a beat merged into the previous segment
    struct TSeg { int64_t a, b; ChordLabel c; };
    std::vector<TSeg> segs;
    for (auto& row : r.rows) { ChordLabel c = row.chord;
        if (segs.empty() || ! c.sameChord (segs.back().c)) segs.push_back ({ row.t, row.t, c }); segs.back().b = row.t + 256; }
    const double beatS = SR * 60 / tp.bpm;
    for (bool again = true; again;) { again = false;
        for (size_t k = 0; k < segs.size(); ++k) if (segs.size() > 1 && (double) (segs[k].b - segs[k].a) < 0.5 * beatS) {
            if (k > 0) segs[k - 1].b = segs[k].b; else segs[k + 1].a = segs[k].a;
            segs.erase (segs.begin() + (long) k); again = true; break; }
        for (size_t k = 1; k < segs.size(); ++k) if (segs[k].c.sameChord (segs[k - 1].c)) { segs[k - 1].b = segs[k].b; segs.erase (segs.begin() + (long) k); again = true; break; } }
    out ("chord timeline (committed, <0.5-beat blips merged, bass-only changes ignored):");
    for (auto& g : segs) out ("  " + fmtTime (g.a).paddedLeft (' ', 7) + " - " + fmtTime (g.b).paddedLeft (' ', 7) + "  " + juce::String (g.c.valid() ? g.c.name() : "N") + "  (" + juce::String ((double) (g.b - g.a) / beatS, 1) + " beats)");
    // per-bar summary from the estimated grid: chord covering most of each bar
    out ("per-bar summary (4/4 at ~" + juce::String (tp.bpm, 1) + " BPM, bar 1 at beat phase):");
    const int64_t ph = tp.phase; juce::String line;
    for (int bar = 0;; ++bar) { const int64_t a = ph + (int64_t) (bar * 4 * beatS), b = a + (int64_t) (4 * beatS); if (a >= (int64_t) x.size()) break;
        std::map<std::string, int64_t> cov;
        for (auto& g : segs) { const int64_t o = juce::jmin (b, g.b) - juce::jmax (a, g.a); if (o > 0) cov[g.c.valid() ? g.c.name() : "N"] += o; }
        std::string bestN = "N"; int64_t bv = 0; for (auto& kv : cov) if (kv.second > bv) { bv = kv.second; bestN = kv.first; }
        line << "| " << juce::String (bestN).paddedRight (' ', 7);
        if (bar % 4 == 3) { out ("  bars " + juce::String (bar - 2).paddedLeft (' ', 2) + "-" + juce::String (bar + 1).paddedLeft (' ', 2) + "  " + line + "|"); line.clear(); } }
    if (line.isNotEmpty()) out ("  last bars  " + line + "|");
    return 0;
}

// ---------------------------------------------------------------------------------------------- --render-demo / --jam
int styleIndex (juce::String s) { s = s.toLowerCase(); return s == "funk" ? 1 : s == "blues" ? 2 : s == "ballad" ? 3 : 0; }
int keyIndex (juce::String k) { for (int i = 0; i < 12; ++i) if (k.equalsIgnoreCase (noteName (i))) return i; const char* fl[] = { "C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B" }; for (int i = 0; i < 12; ++i) if (k.equalsIgnoreCase (fl[i])) return i; return 0; }
std::vector<ChairType> parseChairs (const juce::String& s) {
    std::vector<ChairType> v; auto names = juce::StringArray::fromTokens (s, ",", "");
    for (auto& n : names) { const auto t = n.trim().toLowerCase();
        if (t == "drums") v.push_back (ChairType::Drums); else if (t == "bass") v.push_back (ChairType::Bass); else if (t == "keys") v.push_back (ChairType::Keys);
        else if (t == "organ") v.push_back (ChairType::Organ); else if (t == "guitar") v.push_back (ChairType::RhythmGuitar); else if (t == "pad") v.push_back (ChairType::Pad); else if (t == "perc") v.push_back (ChairType::Percussion); }
    return v;
}
void mp3 (const juce::File& wav) {
    juce::ChildProcess p; p.start (juce::StringArray { "ffmpeg", "-y", "-loglevel", "error", "-i", wav.getFullPathName(), "-codec:a", "libmp3lame", "-b:a", "192k", wav.withFileExtension (".mp3").getFullPathName() });
    p.waitForProcessToFinish (120000);
}
int runRenderDemo (const juce::StringArray& a, int i) {
    if (a.size() < i + 6) { out ("usage: --render-demo <rock|funk|blues|ballad> <bpm> <key> <seconds> <out.wav> [chairs]"); return 1; }
    JamOptions o; o.style = styleIndex (a[i + 1]); o.bpm = a[i + 2].getDoubleValue(); const int key = keyIndex (a[i + 3]); const double secs = a[i + 4].getDoubleValue();
    juce::File outF (a[i + 5]); if (a.size() > i + 6) { auto c = parseChairs (a[i + 6]); if (! c.empty()) o.chairs = c; }
    const double barSec = 240.0 / o.bpm; const int bars = juce::jmax (8, (int) (secs / barSec));
    // I vi IV V ..., then an unexpected bVI, a silent gap (2 bars), then I vi IV V again
    std::vector<ChordLabel> prog; const int degrees[] = { 0, 9, 5, 7 }; const int types[] = { Maj, Min, Maj, Maj };
    const int before = (bars - 3) / 2;
    for (int b = 0; b < before; ++b) prog.push_back (C (key + degrees[b % 4], b == before - 1 ? Maj : types[b % 4]));
    prog.back() = C (key + 8, Maj);                                      // unexpected: bVI
    for (int b = 0; b < bars - 3 - before; ++b) prog.push_back (C (key + degrees[b % 4], types[b % 4]));
    GuitarSynth gs (SR, 99); std::vector<Seg> truth;
    const int64_t lead = (int64_t) (SR * 60 / o.bpm * 4);   // one bar of count-in clicks (muted strums) so the band can lock
    auto x = synthPart (gs, GuitarSynth::tunings()[0], o.bpm, prog, 1, truth, before - 1, 2, 40, false, lead);
    for (int b = 0; b < 4; ++b) gs.strum (x, (int64_t) (b * SR * 60 / o.bpm), gs.voicing (GuitarSynth::tunings()[0], key, Maj, 0), +1, 0.5f, true, 0.1);
    o.beatPhase = 0; o.downbeat = true;
    auto j = jam (x, o);
    writeJam (j, outF, o); mp3 (outF);
    juce::String plan; for (auto& s : truth) plan << fmtTime (s.start) << " " << s.truth.name() << "  ";
    out ("wrote " + outF.getFullPathName() + "  LUFS " + juce::String (j.lufs, 1) + "  peak " + juce::String (j.peakDb, 1) + " dBFS");
    out ("played: " + plan);
    outF.withFileExtension (".truth.txt").replaceWithText (plan);
    return 0;
}
int runJam (const juce::StringArray& a, int i) {
    if (a.size() < i + 4) { out ("usage: --jam <in.wav> <style> <out.wav> [bpm] [chairs]"); return 1; }
    std::vector<float> x; double sr; if (! readMono (juce::File (a[i + 1]), x, sr)) { out ("cannot read input"); return 1; }
    JamOptions o; o.style = styleIndex (a[i + 2]); juce::File outF (a[i + 3]);
    auto r = track (x); auto tp = estimateTempo (r.onsets, x.size());
    o.bpm = a.size() > i + 4 && a[i + 4].getDoubleValue() > 0 ? a[i + 4].getDoubleValue() : (tp.bpm > 0 ? tp.bpm : 100.0);   // no tempo found (noise, button start): fallback 100
    if (a.size() > i + 5) { auto c = parseChairs (a[i + 5]); if (! c.empty()) o.chairs = c; }
    // downbeat: the beat (of the estimated grid) where most chord changes land, among the first 4 beats after the first onset
    const double per = SR * 60 / o.bpm; int64_t ph = tp.phase; if (! r.onsets.empty()) { while (ph > r.onsets.front()) ph -= (int64_t) per; while (ph + (int64_t) (per * 0.5) < r.onsets.front()) ph += (int64_t) per; }
    int bestK = 0; double bestS = -1;
    for (int k = 0; k < 4; ++k) { double s = 0; for (auto& c : r.changes) { const double beats = (double) (c.first - (ph + (int64_t) (k * per))) / per; const double frac = std::fmod (beats, 4.0); const double d = juce::jmin (std::fmod (frac + 4, 4.0), 4 - std::fmod (frac + 4, 4.0)); s += std::exp (-d * d / 0.5); } if (s > bestS) { bestS = s; bestK = k; } }
    o.beatPhase = std::max<int64_t> (0, ph + (int64_t) (bestK * per)); o.downbeat = true;
    auto j = jam (x, o); writeJam (j, outF, o); mp3 (outF);
    out ("tempo " + juce::String (o.bpm, 1) + " BPM, downbeat at " + fmtTime (o.beatPhase) + ", key " + keyName (j.tr.key));
    out ("wrote " + outF.getFullPathName() + "  LUFS " + juce::String (j.lufs, 1) + "  peak " + juce::String (j.peakDb, 1) + " dBFS");
    return 0;
}
} // namespace


// ---------------------------------------------------------------------------------------------- drift test (offline)
/** --drift-test <ppm> <minutes> <outdir> [inBlock outBlock] [take.wav]
    Two simulated device clocks: input (WASAPI-like, inBlock per callback, rate 48k*(1+ppm)) and output (outBlock per
    callback, 48k), each with scheduling jitter, events processed in time order through the real DriftResampler.
    Signals: a 220 Hz sine (exact click metric) and a looped real take (written for check_mix.py --clicks). */
int runDriftTest (const juce::StringArray& a, int i) {
    if (a.size() < i + 4) { out ("usage: --drift-test <ppm> <minutes> <outdir> [inBlock outBlock] [take.wav]"); return 2; }
    const double ppm = a[i + 1].getDoubleValue(), minutes = a[i + 2].getDoubleValue();
    const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (a[i + 3]); dir.createDirectory();
    int inBlock = 480, outBlock = 128; juce::String takePath;
    if (a.size() > i + 5 && a[i + 4].containsOnly ("0123456789")) { inBlock = a[i + 4].getIntValue(); outBlock = a[i + 5].getIntValue(); }
    for (int k = i + 4; k < a.size(); ++k) if (a[k].endsWithIgnoreCase (".wav")) takePath = a[k];
    std::vector<float> take; double tsr = SR;
    if (takePath.isNotEmpty() && ! readMono (juce::File::getCurrentWorkingDirectory().getChildFile (takePath), take, tsr)) { out ("cannot read " + takePath); return 2; }
    if (! take.empty()) {   // seamless loop: 50 ms equal-power crossfade of the tail into the head
        const size_t xf = (size_t) (0.05 * SR), L = take.size() - xf;
        for (size_t k = 0; k < xf; ++k) { const double g = (double) k / (double) xf * juce::MathConstants<double>::halfPi;
            take[k] = (float) (take[k] * std::sin (g) + take[L + k] * std::cos (g)); }
        take.resize (L);
    }
    const double srOut = SR, srIn = SR * (1.0 + ppm * 1e-6);
    const int64_t totalOut = (int64_t) (minutes * 60.0 * srOut);
    DriftResampler rsSine, rsTake;
    bool adaptive = true, stress = false; for (int k = i + 4; k < a.size(); ++k) { if (a[k] == "--fixed") adaptive = false; if (a[k] == "--stress") stress = true; }
    // --stress: Windows-like trouble on top of the normal jitter: an 8-20 ms input DPC burst every ~90 s, 45 ms every ~15 min (late block,
    // then the backlog arrives at once), a 6-12 ms output hiccup every ~2 min, and a full 1.5 s input stall
    // (device stops delivering, then resumes) every 20 min.
    int clicksAudible = 0; double lastClickT = -1; int stallsSim = 0; double worstRecover = 0, stallEnd = -1; bool waitRecover = false, stallSkip = false;
    rsSine.prepare (srOut, outBlock, inBlock, 128, adaptive); rsTake.prepare (srOut, outBlock, inBlock, 128, adaptive);
    juce::Random rnd (12345);
    const double A = 0.3, f = 220.0;
    std::vector<float> inBuf ((size_t) inBlock), inBufT ((size_t) inBlock), oS ((size_t) outBlock), oT ((size_t) outBlock);
    std::vector<float> wavT; wavT.reserve ((size_t) totalOut);
    int64_t inIdx = 0, outIdx = 0, kIn = 0, kOut = 0; double lastInT = 0;
    auto nextInTime = [&] { double t = (double) (kIn + 1) * inBlock / srIn + rnd.nextDouble() * 0.001;
        if (rnd.nextDouble() < 0.001) t += 0.004;                          // rare 4 ms scheduling hiccup
        if (stress) {
            const double blkT = (double) inBlock / srIn;
            if (rnd.nextDouble() < blkT / 90.0) t += 0.008 + 0.012 * rnd.nextDouble();     // 8-20 ms DPC burst, ~every 90 s
            if (rnd.nextDouble() < blkT / 900.0) t += 0.045;                               // 45 ms, ~every 15 min
            const double ts = std::fmod (t, 1200.0);
            if (t > 300 && ts >= 600.0 && ts < 601.5) { t = t - ts + 601.5; ++stallsSim; stallEnd = t; waitRecover = true; stallSkip = true; }   // stall: nothing until 601.5
        }
        return lastInT = std::max (lastInT, t); };
    auto nextOutTime = [&] { double t = (double) kOut * outBlock / srOut + rnd.nextDouble() * 0.0003;
        if (stress && rnd.nextDouble() < (double) outBlock / srOut / 120.0) t += 0.006 + 0.006 * rnd.nextDouble();
        return t; };
    double tIn = nextInTime(), tOut = nextOutTime();
    // metrics
    const double settle = 120.0;
    double worstClickDb = -300, worstClickT = 0; double fillSum = 0, fillSq = 0, ppmSum = 0, ppmSq = 0; int64_t nStat = 0;
    float rawMin = 1e9f, rawMax = 0; double lockT = -1; double y1 = 0, y2 = 0;
    while (outIdx < totalOut) {
        if (tIn <= tOut) {
            for (int k = 0; k < inBlock; ++k) {
                const int64_t j = inIdx + k;
                inBuf[(size_t) k] = (float) (A * std::sin (2.0 * juce::MathConstants<double>::pi * f * (double) (j % (int64_t) std::llround (srIn * 100)) / srIn));
                inBufT[(size_t) k] = take.empty() ? 0.0f : take[(size_t) (j % (int64_t) take.size())];
            }
            // (j mod 100 s keeps the phase argument small; 100 s * f is an integer number of cycles, so no discontinuity)
            rsSine.push (inBuf.data(), inBlock, tIn); if (! take.empty()) rsTake.push (inBufT.data(), inBlock, tIn);
            inIdx += inBlock; ++kIn; tIn = nextInTime();
            if (stallSkip) { stallSkip = false; while ((double) (kIn + 1) * inBlock / srIn < tIn - 0.002) ++kIn; }   // blocks lost in a stall are skipped; a DPC burst's backlog still arrives
        } else {
            rsSine.pull (oS.data(), outBlock, tOut); if (! take.empty()) { rsTake.pull (oT.data(), outBlock, tOut); wavT.insert (wavT.end(), oT.begin(), oT.end()); }
            const double ratio = 1.0 + rsSine.tel.ppm.load() * 1e-6;
            const double w = 2.0 * juce::MathConstants<double>::pi * f / srIn * ratio, c2 = 2.0 * std::cos (w);
            const double t = (double) outIdx / srOut;
            if (rsSine.isPrimed() && t > 1.0)
                for (int k = 0; k < outBlock; ++k) {
                    const double y = oS[(size_t) k], r = std::abs (y - c2 * y1 + y2) / (A * w * w);
                    if (r > 0) { const double db = 20.0 * std::log10 (r);
                        const bool nearStall = stallEnd > 0 && t > stallEnd - 2.0 && t < stallEnd + 3.0;   // stall fade out/in is expected
                        if (db > 0 && ! nearStall && t + k / srOut - lastClickT > 0.05) { ++clicksAudible; lastClickT = t + k / srOut; }
                        if (! nearStall && db > worstClickDb) { worstClickDb = db; worstClickT = t + k / srOut; } }
                    y2 = y1; y1 = y;
                }
            else for (int k = 0; k < outBlock; ++k) { y2 = y1; y1 = oS[(size_t) k]; }
            const float fl = rsSine.tel.fill.load(), pp = rsSine.tel.ppm.load();
            if (waitRecover && t > stallEnd && rsSine.isPrimed() && std::abs (pp - juce::jlimit (-500.0, 500.0, ppm)) < 30) { worstRecover = std::max (worstRecover, t - stallEnd); waitRecover = false; }
            if (lockT < 0 && t > 1 && std::abs (fl - rsSine.targetFillSamples()) < 16 && std::abs (pp - juce::jlimit (-500.0, 500.0, ppm)) < 20) lockT = t;
            if (t > settle) {
                fillSum += fl; fillSq += (double) fl * fl; ppmSum += pp; ppmSq += (double) pp * pp; ++nStat;
                auto mm = rsSine.takeMinMax(); rawMin = std::min (rawMin, mm.first); rawMax = std::max (rawMax, mm.second);
            } else rsSine.takeMinMax();
            outIdx += outBlock; ++kOut; tOut = nextOutTime();
        }
    }
    const double fm = fillSum / (double) nStat, fs = std::sqrt (std::max (0.0, fillSq / (double) nStat - fm * fm));
    const double pm = ppmSum / (double) nStat, ps = std::sqrt (std::max (0.0, ppmSq / (double) nStat - pm * pm));
    auto& tl = rsSine.tel;
    juce::String r;
    r << "drift-test ppm " << ppm << " minutes " << minutes << " inBlock " << inBlock << " outBlock " << outBlock
      << " | target " << juce::String (rsSine.targetFillSamples(), 0) << " smp = " << juce::String (rsSine.latencyMs(), 2) << " ms added"
      << " (start " << juce::String (rsSine.maxLatencyMs(), 2) << " ms, min " << juce::String (rsSine.tel.targetMin.load() / SR * 1000.0, 2) << " ms, "
      << (rsSine.isAdaptive() ? "adaptive" : "fixed") << ", learnt excursion " << juce::String (rsSine.tel.jitter.load(), 0) << " smp, retarget jumps " << rsSine.tel.retargets.load() << ")"
      << " | lock " << juce::String (lockT, 1) << " s"
      << " | after " << settle << " s: fill mean " << juce::String (fm, 1) << " std " << juce::String (fs, 2) << " rawMin " << juce::String (rawMin, 0) << " rawMax " << juce::String (rawMax, 0)
      << " | ppm est " << juce::String (pm, 2) << " +- " << juce::String (ps, 2)
      << " | underXfades " << tl.underruns.load() << " overXfades " << tl.overruns.load() << " stalls " << tl.stalls.load()
      << " drops " << tl.pushDrops.load() << " holds " << tl.holds.load()
      << (stress ? " | stress: sim stalls " + juce::String (stallsSim) + " worst ppm-recovery " + juce::String (waitRecover ? -1.0 : worstRecover, 1) + " s" : juce::String())
      << " | audible clicks (>0 dB, outside stalls) " << clicksAudible
      << " | sine click score worst " << juce::String (worstClickDb, 1) << " dB @ " << juce::String (worstClickT, 2) << " s (0 dB = the sine's own curvature; a click is >> 0 dB)";
    out (r);
    dir.getChildFile ("drift_" + juce::String (ppm, 0) + "ppm.txt").replaceWithText (r + "\n");
    if (! wavT.empty()) {
        auto wf = dir.getChildFile ("drift_" + juce::String (ppm, 0) + "ppm_take.wav");
        const float* ch = wavT.data(); writeWav (wf, { ch }, wavT.size()); out ("wrote " + wf.getFullPathName());
        if (rsTake.tel.underruns.load() != tl.underruns.load() || rsTake.tel.overruns.load() != tl.overruns.load()) out ("note: take/sine recovery counts differ");
    }
    return 0;
}

int runCli (const juce::StringArray& args) {
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == "--instrument" && args.size() > i + 1) gInstrument = inputInstrumentFromId (args[i + 1]);
        if (args[i] == "--strangle") gStrangle = true;
        if (args[i] == "--mute-bass-chair") gMuteBassChair = true;
        if (args[i] == "--bass-octave-up") gBassGiveWay = true;
        if (args[i] == "--tuning" && args.size() > i + 1) gTuning = tuningFromId (args[i + 1]);
        if (args[i] == "--cents" && args.size() > i + 1) { gTuning = Tuning::Custom; gCents = (float) args[i + 1].getDoubleValue(); }
        if (args[i] == "--no-learn-noise") gLearnNoise = false;
        if (args[i] == "--start-on-button") gStartMode = 1;
        if (args[i] == "--no-click") gCountInClick = false;
        if (args[i] == "--start-at" && args.size() > i + 1) gStartAt = args[i + 1].getDoubleValue();
        if (args[i] == "--stop-at" && args.size() > i + 1) gStopAt = args[i + 1].getDoubleValue();
    }
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == "--eval") return runEval (juce::File (args.size() > i + 1 ? args[i + 1] : "eval.csv").getFullPathName().isEmpty() ? juce::File::getCurrentWorkingDirectory().getChildFile ("eval.csv") : juce::File::getCurrentWorkingDirectory().getChildFile (args.size() > i + 1 ? args[i + 1] : "eval.csv"));
        if (args[i] == "--analyze" && args.size() > i + 1) return runAnalyze (juce::File::getCurrentWorkingDirectory().getChildFile (args[i + 1]));
        if (args[i] == "--render-demo") return runRenderDemo (args, i);
        if (args[i] == "--jam") return runJam (args, i);
        if (args[i] == "--drift-test") return runDriftTest (args, i);
    }
    return -1;
}
} // namespace sj
