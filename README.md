# Session Jam

A Rocksmith-Session-Mode-style jam app with an adaptive backing band and per-track stem recording. See DESIGN.md for the architecture and roadmap.

## Build (Windows 11, VS Build Tools 2022, CMake, Ninja, git)
```powershell
.\build.ps1            # Release, ASIO ON (Steinberg ASIO SDK auto-downloaded), log in build.log
.\build.ps1 -NoAsio    # WASAPI/DirectSound only
.\build.ps1 -AsioSdk C:\sdk\ASIOSDK   # use a local SDK
```
The output is `build\SessionJam_artefacts\Release\Session Jam.exe`. JUCE 8.0.8 is fetched by CMake FetchContent. The ASIO SDK comes from Steinberg's official download (https://www.steinberg.net/asiosdk, dual GPLv3/proprietary licence), pinned to 2.3.4 with a SHA-256 check.

### Plug-in hosting (VST3 / VST2 / VST1)
- **VST3** hosting is always built in, on Windows and Linux.
- **VST2** hosting is optional because Steinberg no longer distributes the VST2 SDK. If you have a copy, use `.\build.ps1 -Vst2Sdk C:\path\to\vst2sdk`, or `-DENABLE_VST2=ON -DVST2_SDK_DIR=...` with CMake. The folder must contain `pluginterfaces/vst2.x/aeffect.h`.
- **VST1** plug-ins are loaded through the same VST2 host, since VST 1.x is a subset of the 2.x ABI. They therefore need `ENABLE_VST2` too.

## Build (Linux)
```bash
sudo apt install build-essential cmake libasound2-dev libjack-jackd2-dev libfreetype6-dev libfontconfig1-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libgl1-mesa-dev libcurl4-openssl-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```
ASIO is always off on Linux.

## ASIO4ALL setup (recommended on Windows)
1. Download and install **ASIO4ALL v2** from https://asio4all.org.
2. Close any other app that uses audio, then launch Session Jam. It picks the "ASIO4ALL v2" device automatically at 48 kHz with a 64–128 sample buffer (Windows only; ASIO code is behind `JUCE_WINDOWS`).
3. Open the ASIO4ALL panel: Session Jam → **Audio** tab → **Control Panel**, or the green ASIO4ALL tray icon.
4. Click the wrench icon to switch to *Advanced view*, then:
   - Enable (power button on) your **USB guitar interface** (e.g. a Real Tone-style cable) and expand it. Enable its **input** (mono, 48 kHz).
   - Enable **Realtek(R) Audio** and enable only its **outputs** (speakers/headphones). Disable Realtek inputs so the mic doesn't take input channel 1.
   - Disable every other device.
5. Set **ASIO Buffer Size** to 128 samples (try 64 if you hear no crackles). If you get crackles, raise it to 256. Leave "Allow Pull Mode" on, and try "Always Resample 44.1kHz <-> 48kHz" off.
6. On Session Jam's **Audio** tab, make sure **input channel 1** is the USB guitar interface and outputs 1+2 are the Realtek.

The cable and the Realtek have separate clocks, so expect a possible occasional click on long takes. FlexASIO is an alternative. Without ASIO4ALL, the app falls back to **WASAPI shared** with the USB guitar interface as input and the default Realtek output.

## Amp sims and effects
1. On the **Inserts** tab, click **Plug-ins...**, then **Options... → Scan for new or updated VST3 plug-ins**. The scan starts with the default folders (`C:\Program Files\Common Files\VST3`; on Linux `~/.vst3`, `/usr/lib/vst3`, `/usr/local/lib/vst3`). Add your own folders in that prompt; they are remembered. On a VST2 build there is also a VST scan option, so you can add your `VstPlugins` folder.
2. The plug-in list is saved to `%APPDATA%\SessionJam\plugins.xml`. A plug-in that crashes the scanner is blacklisted on the next scan.
3. Click an **Insert 1–4** slot, choose **Load plug-in**, and pick your amp sim. Its editor opens in its own window; use **Edit** to reopen it. Each slot has a **Bypass** toggle.
4. The signal path is: guitar → placeholder drive → Insert 1 → 2 → 3 → 4 → GuitarFX fader → *GuitarFX stem* and speakers. The **GuitarDI stem is always dry**, so you can re-amp it later.
5. Total plug-in latency (`getLatencySamples` of the active slots) is shown above the slots. While a plug-in is being swapped, the dry signal is delayed by that same latency, so the sound does not jump. The stems are not latency-compensated: GuitarFX lags the DI and band by that amount.
6. Loaded plug-ins, their full state and bypass settings are saved with the device setup in `%APPDATA%\SessionJam\session.xml` and restored at launch.

## Use
**Jam tab.** Pick your instrument (**Bass VI** is the default; also Guitar, Guitar drop D and 4-string bass) and your
**Tuning** (Standard, -1 semitone, -2 semitones, Drop D, or Custom cents). Then press **Start session**:
1. **Learn noise** (about 1 s): keep your hands off the strings. The app measures the noise floor and sets the noise gate
   and up to 3 hum notches (mains hum and its harmonics). You can rerun this any time with **Learn noise**.
2. **Count in:** play four even strums or hits. The band locks to that tempo and phase (tempos above 160 BPM are
   read as eighth notes and halved). If you don't count in, the band uses the **fallback tempo** from the Band tab.
3. Play. The band follows your chords (it lands changes on the next beat), your tempo and your intensity. It breaks
   down when you stop and comes back in on the next downbeat.

- **Tuning** only sets the display ("Shape as fingered" names the chord shape you are fingering) and the pitch-search
  range. Chord and key detection always use the *sounding* pitch, so a mis-set tuning never changes what the band plays.
- **Strangle** (Bass VI only) is for heavy low-string playing; it is also auto-detected and the status line shows the
  result.
- **Bass VI** is treated as a super-low *guitar*: the low pitch range is used for detection, and chords, low power chords
  and riffs are all expected. The band's bass chair plays normally by default. Two optional toggles, both off by default:
  **Bass chair octave up** (the bass moves up an octave and plays sparse roots and fifths) and **Mute bass chair**.

**Band tab:** this holds the style, fallback tempo, master and input level, and up to 8 **chairs** (Drums, Bass, Keys/EP, Organ, Pad,
Rhythm guitar), each with a role, gain, mute and solo. **Inserts tab:** amp sims (see above). **Audio tab:** device setup.
**About tab:** version, licence, credits, and a Donate link if one is configured.

**Record stems** writes aligned 24-bit WAVs (GuitarDI, GuitarFX and one per chair) to `Music/SessionJam Takes`.
All settings are saved in `session.xml` (`%APPDATA%\SessionJam\` on Windows, `~/.config/SessionJam/` on Linux):
instrument, tuning, cents, strangle, mute-bass-chair, style, fallback BPM, monitor, gains, UI scale, the chairs,
the audio device and the plug-in chain. To show a Donate link on the About tab, set `donateUrl="https://..."` on the root
`<Session>` element. It is empty (hidden) by default.

**4K / scaling:** the window is 1280x720 logical pixels, which is 3840x2160 at 300 %. Every tab scrolls rather than
clipping when the window is smaller. Set `uiScale="0.8"` in session.xml to force a scale (0 = automatic).

## Sample library
CMake downloads the band samples at configure time from pinned commits (see CREDITS.md) into `assets/samples/`. If the
download fails, the band falls back to synthesised voices.

## Command-line tools (offline, no audio device)
| Command | What it does |
|---|---|
| `SessionJamCli --eval [csv]` | Chord-follow evaluation on synthesised guitar (tunings x tempos x noise/hum); `SJ_QUICK=1` runs a small subset. |
| `SessionJamCli [--instrument bassvi] [--tuning down1\|--cents -30] --analyze take.wav` | Report on a real take: level, learned noise and hum notches, strangle detection, tempo, key, and the chord timeline. |
| `SessionJamCli --render-demo <style> <bpm> <key> <sec> out.wav` | Render a synthesised player and the band, plus `.log.txt`, `.truth.txt` and stems. |
| `SessionJamCli --instrument bassvi --jam take.wav <style> out.wav [bpm]` | Render the band jamming to a real take. |
| `"Session Jam" --engine-test take.wav outdir <block>` | Stream a take through the **real** audio callback (AudioEngine) at the given block size; writes stems and `mix.wav` and reports drops. |
| `"Session Jam" --screens dir` | Render every tab to PNG at 1280x720, 1280x720@3x, 1920x1080 and 960x600 (use `xvfb-run -a` on headless Linux). |
| `tools/check_mix.py mix.wav stems/ log.txt [--key "F minor"]` | Objective checks: clicks, drum timing vs the grid, and bass notes vs the sounding chord (and key). |
| `tools/follow_check.py demos/demo-rock 120` | Band chord accuracy and latency vs the truth file of a rendered demo. |

## Licence
AGPL-3.0-only (see LICENSE and LICENSING.md). Sample credits are in CREDITS.md and on the About tab.

## Windows: USB guitar interface + ASIO4ALL (clock drift)

The Real Tone cable and the PC's Realtek output run on separate clocks. If one ASIO4ALL device aggregates
both, the clocks drift apart and after a few minutes you hear static. SessionJam avoids this by keeping
the two devices separate:

* **Output:** ASIO4ALL, output only. In the ASIO4ALL panel, **enable Realtek only and disable the USB guitar interface
  cable**. (WASAPI output works too, with more latency.)
* **Input:** the Real Tone cable opens as its own WASAPI input-only device (shared mode, 48 kHz) on its own
  thread ("split input"). A drift-tracking resampler bridges it to the output clock. It uses a FIFO
  at about 2-3 blocks, a PI-controlled ratio within ±500 ppm, 6-point Lagrange interpolation and
  phase-aligned crossfades for recovery. It never inserts silence.
* Split input turns on automatically whenever the input and output are different physical devices
  (Settings → Audio → "Split input (auto)"). The status line shows the FIFO fill, the ppm estimate and the xrun count.
* The drift log is at `%APPDATA%\SessionJam\drift.log` (1 line/s; it rotates at 4 MB). A steady ppm of a few
  tens to hundreds is normal. A warning appears if the estimate stays above 450 ppm for more than 10 s.
* The split input adds about 27-35 ms of latency (offline test: 28 ms at a 128-sample output block).

## Latency and microphone (v0.4)

* Split-input FIFO is adaptive: it starts safe, learns the input jitter for ~8 s, then settles at the smallest safe target
  (e.g. ~17 ms instead of 28 ms with a 480-sample WASAPI input). Audio tab: "Low-latency input mode" tries WASAPI
  IAudioClient3 at the smallest period first (falls back automatically); "Output buffer" picks the ASIO/WASAPI buffer (64-128 recommended).
* The Jam tab shows FIFO + driver in/out + insert latency = total. Recorded stems are delay-compensated (PDC + round trip);
  use "Recording trim" if your takes are still offset.
* Microphone: pick it on the Audio tab; gain, meter and "Monitor mic" (default off) on the Jam tab. It is recorded as its own
  sample-aligned "Mic" stem and never feeds the chord/tempo follower.
