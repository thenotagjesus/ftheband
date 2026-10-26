# Session Jam — Design

A JUCE 8 (C++17) desktop app in the spirit of Rocksmith's Session Mode. You play (Bass VI by default; guitar or bass
also work) and a sampled band of up to 8 chairs follows your chords, tempo and intensity. Every bus can be recorded
as a sample-aligned stem. AGPL-3.0-only.

## 1. Threads
| Thread | Work |
|---|---|
| Audio (`AudioEngine::audioDeviceIOCallbackWithContext`) | Splits each device block into chunks of at most 512 samples (`processChunk`). Each chunk runs: input gain → placeholder drive → `PluginChain` → band → mix with equal-power pans → `StemRecorder::write()` → push the raw input to the analysis FIFO. There is no allocation and no blocking lock; the buses are pre-sized in `prepareOffline`. |
| Analysis (`AudioEngine::run`) | **Polls** a lock-free `AbstractFifo` every 2 ms (an atomic flag and polling, never a signal from the audio thread). Feeds `ChordTracker` and publishes `EngineStatus` through a triple buffer. |
| Stem writer (`StemRecorder`, a `juce::Thread`) | Drains one interleaved lock-free FIFO into per-stem 24-bit WAV writers. |
| Message (UI) | Tabs, a 30 Hz status timer, chair edits (a new `BandConfig` is built here and swapped in atomically), and the plug-in editors. |

**Overflow policy.** Both FIFOs reserve a **whole block or nothing**. A block that does not fit is dropped and
counted: analysis drops in samples, recorder drops in blocks and samples. The Jam tab shows both counts. A partial write would
misalign the stems, so it is never done.

**Session start (learn noise → count-in → play).** Start session sets `learnRequest` and bumps `sessionRequest`.
The audio thread stamps `sessionStartAt` as a FIFO position. The analysis thread splits its chunk exactly there, then
collects about 1 s (`learnSeconds`) of input and calls `tracker.learnNoiseFrom()`, which sets the gate floor and up to 4
hum notches. The **Learn noise** button reruns that step alone. Every status carries the session generation, so the UI and band
never act on a previous session's data. When the tracker reports a **locked** tempo, `lockGen` is bumped. The audio side then sets
`band.bpm` and `alignBeatTo(beatPhase)`, so the band's grid lands on the player's beat. Until a lock, the band uses the fallback BPM.

## 2. Audio I/O
- The target hardware is a USB guitar interface (class-compliant cable, mono, 48 kHz) as input and onboard Realtek as output.
- Device policy (`MainComponent::setupAudioDevices`): first the saved state in `session.xml`. Then, **Windows only (`#if JUCE_WINDOWS`)**,
  ASIO4ALL at 48 kHz/64–128 samples, then WASAPI shared with the USB guitar interface (auto-detected by a device name containing "Rocksmith"). The last resort is the platform default (ALSA/JACK).
- **Not implemented (deliberately, review item 02):** latency compensation. (Item 01, the drift resampler, is now implemented; see the end of this file.)
  of the stems. The stems stay aligned *with each other* because every stem is written from the same callback.

## 3. Analysis (`Source/Analysis/ChordTracker.h`)
- **Input profile** (`Music/InputInstrument.h`): the instrument plus a **Tuning** (Standard, −1, −2, Drop D, Custom cents) set the
  multi-pitch search range and the FFT size (16384 for the low register). **Tuning is display and range only.** Chord and key
  detection work on sounding pitch and are tuning-agnostic. The Jam tab adds "Shape as fingered (tuning)" by shifting the
  sounding chord by the tuning offset.
- **Learn noise:** about 1 s with nothing played. It finds narrow, stationary spectral lines between 35 and 160 Hz and notches them (Q 30).
  It then measures the floor after the notches and sets the gate. Finding on the user's take: a **48.2 / 96.5 / 145 Hz** series was present
  in the silence before the first note. That is not 50/60 Hz mains, and nothing was played, but it sits close to G1/G2/D3 and pulled the
  chroma and key towards G. With the notches, the take reads **F minor**. Its chords (G#5, A#5, C#5, F5) also fit A♭ major/F minor. If the
  instrument is tuned down a semitone, that is G minor as fingered. Key detection is left on sounding pitch on purpose.
- **Chords:** a chroma FFT plus multi-pitch salience with leaky evidence integration, reset on onsets. Then hysteresis, frame confirmation (longer for the
  low register unless the onset lands on a beat), and a hold during decays.
- **Bass VI strangle:** a toggle, plus auto-detection from the energy ratio below 100 Hz against 100–400 Hz on note frames. When it is active, the
  root evidence leans on the 2nd/3rd harmonics.
- **Tempo (`BeatTracker`):** the first four evenly spaced onsets (within 8 %) are the count-in. They set the tempo and phase and are never committed as chords.
  Four even eighth-note strums used to lock at double tempo (202 BPM on the user's take), so tempos above 160 BPM are now halved. The phase stays on a beat.
  After the lock, onsets near the beat refine the tempo slowly.
- **Key:** Krumhansl-style correlation on long-term chroma (τ 20 s), with a 4 s hold before a key change.

## 4. Band (`Source/Engine/Band.h`)
- A sample-accurate 16th clock. Each 16th is planned one step ahead, so humanised timing can fall early as well as late. Chord changes land on
  the next beat (or the next half bar). Fills are cued by sharp intensity changes. When the player stops, the band breaks down and then drops out. It re-enters on the next downbeat
  with a crash.
- **Chairs** (max 8): Drums, Bass, Keys/EP (samples, see CREDITS.md), plus Organ, Pad and Rhythm guitar (synthesised). Each chair has a type, role, gain, mute and solo.
  A whole prepared `BandConfig` is swapped through an atomic pointer, and the old one is deleted on the message thread.
- The bass chair plays normally for every instrument, Bass VI included (a Bass VI is treated as a super-low guitar: low pitch range for detection, chords and power chords as well as riffs). Two opt-in toggles, both off by default: "Bass chair octave up" (sparse roots and fifths an octave up) and "Mute bass chair".

## 5. Plug-ins (`PluginChain`, `UI/PluginUI`)
- VST3 is always available. VST2/VST1 are available with `ENABLE_VST2` and a user-supplied SDK.
- There are 4 insert slots on the GuitarFX bus, with state saved in session.xml.
- The audio thread only *try*-locks the slots. On contention (a swap in progress), it outputs the dry signal from a ring buffer, **delayed by the
  last reported plug-in latency**, so a swap does not jump in time.
- Plug-ins are prepared for the 512-sample chunk size.

## 6. Mix and stems
- **Buses:** GuitarDI (dry, always), GuitarFX, and one per chair. Pans are equal-power. The master has a clip guard.
- **Stem files:** `StemRecorder` writes `<take>_<Stem>.wav` files (24-bit) with identical lengths. `stop()` waits for any in-flight `write()`, drains the
  FIFO, then closes the files.

## 7. UI
- **Size:** 1280x720 logical pixels, which is 3840x2160 at 300 % (4K). Global scale is `uiScale` from session.xml, or automatic (0.5–1.0 for smaller screens).
- **Tabs:** Jam, Band, Inserts, Audio, About. Each is a viewport with a minimum size, so a small window scrolls instead of clipping.
- **About tab:** version, AGPL notice and link, the CC BY attribution, an optional source URL (`SESSIONJAM_SOURCE_URL`), and a **Donate** link that
  appears only when `donateUrl` is set in session.xml. It is empty by default.
- **`--screens <dir>`:** renders every tab at 1280x720, 1280x720@3x (Jam), 1920x1080 and 960x600, with preview data.

## 8. session.xml
`<Session instrument tuning cents strangle muteBassChair bassGiveWay style fallbackBpm monitor inputGain masterGain uiScale donateUrl>`
with these children: `<Chairs><Chair type role gain mute solo/>…</Chairs>`, `<DEVICESETUP/>`, and `<PluginChain>`. It lives in the user app-data
folder under `SessionJam/`.

## 9. Verification (all offline)
| Check | Command | Latest result |
|---|---|---|
| Chord follow on synthesised guitar, 1134 changes | `SessionJamCli --eval` | 85.8 % maj/min, 86.7 % root, median latency 1.23 beats (`eval/eval-summary.txt`) |
| Quick eval incl. Bass VI (+ strangle) | `SJ_QUICK=1 SessionJamCli --eval eval/eval-quick.csv` | maj/min 78.2 % overall: standard 86.6 %, Bass VI 69.2 %, Bass VI strangled 66.0 %; on the user take with a 150 Hz high-pass, the strangle path agrees 73 % with the unfiltered take (the guitar profile agrees 2.6 %) (`eval/eval-quick-summary.txt`) |
| Real engine, real take, block 480 and 2048 | `"Session Jam" --engine-test` | 0 analysis drops, 0 recorder drops, 5 stems of identical length, tempo lock 101.5 BPM (CLI estimate 100.2), F minor |
| Mix sanity on the demos and the user-take jam | `tools/check_mix.py` | 0 clicks; drums 88–97 % within 20 ms of the grid (median 1.6–2.4 ms); bass fits the sounding chord in 94.7–100 % of notes |
| Band chord accuracy vs truth | `tools/follow_check.py` | see `demos/*.log.txt` |

**Known weaknesses:** power chords are the weakest class (48.6 % exact). Key estimates on the user take wander between
F minor, A♭ major and G minor in the first 30 s (they are relative or neighbouring keys). In the live engine, learn noise takes a fixed 1 s, so playing during
it raises the gate (−52.8 dBFS against −60.5 when only silence is learned).

## Split input / drift (Aura 01)
`SplitInput` (WASAPI input-only, own thread) → `DriftResampler` (SPSC ring, PI on phase-corrected fill,
ωn=2π·0.02, ζ=1, I term clamped ±500 ppm, 6-pt Lagrange, WSOLA-aligned crossfaded jumps on low/high water,
fade-out/fade-in on input stall) → `AudioEngine` output callback. `SessionJamCli --drift-test` simulates it offline.
