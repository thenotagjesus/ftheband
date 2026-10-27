# Credits and sample licences

Every sample the app downloads or bundles allows **commercial use and redistribution**. Nothing here is NC, ND or
"personal use only". The licences were checked on 2026-10-25 against each repository's LICENSE/README, and every
download is pinned to the commit below. The full file list is in `assets/samples.manifest`, and the files go to
`assets/samples/` (see README "Sample library").

| Part | Asset | Licence | Source (pinned commit) | Obligation |
|---|---|---|---|---|
| Drums: kick, snare, side stick, hi-hats, toms (front mic, all velocity layers, RR 1-4; toms RR 1-2) | **Gogodze Phu Vol. II** by Karoryfer Samples | **CC0 1.0** (LICENSE file: "CC0 1.0 Universal") | https://github.com/sfzinstruments/karoryfer.gogodze-phu-vol-ii @ 69a0274cdc39 | None. Credit given as a courtesy. |
| Crash cymbals (suspended cymbal 1/2, stick hits) | **VCSL – Versilian Community Sample Library** by Versilian Studios LLC | **CC0 1.0** (README: "under a Creative Commons 0 license … even make commercial software") | https://github.com/sgossner/VCSL @ c1ea7bcc3c73 | None |
| Bass: "darkblack" fingered, 8 notes × 3 velocity layers × 2 RR | **Black And Blue Basses** by Karoryfer Samples | **CC0 1.0** (GitHub licence: CC0-1.0) | https://github.com/sfzinstruments/karoryfer.black-and-blue-basses @ 6e7d674cdb41 | None |
| Keys / EP: Wurlitzer EP200, all notes/layers (FLAC) | **Greg Sullivan E-Pianos** (samples by Greg Sullivan, http://www.sullivang.net/; SFZ mapping by kinwie) | **CC BY 3.0 Unported** (README "License" section → https://creativecommons.org/licenses/by/3.0/) | https://github.com/sfzinstruments/GregSullivan.E-Pianos @ 8c3e581acda3 | **Attribution required**: keep this credit in the app's About box, README and any download page, link the licence, and say the samples were trimmed and resampled. |

The Organ, Pad/Strings and Rhythm Guitar chairs are synthesised in code (`Source/Engine/PolySynth.h`), so they
use no third-party assets. The test guitar used by `--eval` and `--render-demo` is synthesised as well
(`Source/Test/GuitarSynth.h`).

User recordings (for example `user-takes/`) belong to the user. They are never bundled or zipped.

## Attribution text to ship (required by CC BY 3.0)
> Wurlitzer EP200 samples by Greg Sullivan (sullivang.net), SFZ mapping by kinwie, licensed under CC BY 3.0
> (https://creativecommons.org/licenses/by/3.0/). Trimmed and resampled for Session Jam.
> Drum, cymbal and bass samples: Karoryfer Samples (Gogodze Phu Vol. II, Black And Blue Basses) and
> Versilian Studios (VCSL), CC0.

## Downloadable sample packs

# Credits – additions for downloadable packs (batch 1)

Checked 2026-10-26 against each upstream LICENSE/README at the pinned commit. Every pack is CC0 1.0, so no attribution is legally required; credit is given as a courtesy (and VCSL's organ Info.txt asks for credit to the original author and Versilian).

| Pack | Asset | Licence | Evidence (upstream file, pinned) | Source |
|---|---|---|---|---|
| `karoryfer-big-rusty-drums` (drums2) | Big Rusty Drums (close mics) | **CC0-1.0** | LICENSE line 3: "CC0 1.0 Universal" – [LICENSE](https://raw.githubusercontent.com/sfzinstruments/karoryfer.big-rusty-drums/f07ce00df34a46b6b08375be56fe116cf15782bc/LICENSE) | https://github.com/sfzinstruments/karoryfer.big-rusty-drums/tree/f07ce00df34a46b6b08375be56fe116cf15782bc |
| `vcsl-pipe-organ` (organ) | VCSL Pipe Organ (Manual III, quiet) | **CC0-1.0** | README.md line 6: "This collection is under a Creative Commons 0 license. Essentially it's Public Domain- you can do whatever you want with these sounds (even make commercial software), no royalties, no credit, no special terms." (LICENSE line 3: "CC0 1.0 Universal"); Pipe Organ/Info.txt: "Bearer is granted right to redistribute this sample set by Versilian Studios LLC." – [README.md](https://raw.githubusercontent.com/sgossner/VCSL/c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e/README.md) | https://github.com/sgossner/VCSL/tree/c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e/Aerophones/Edge-blown%20Aerophones/Pipe%20Organ |
| `karoryfer-zinc-strings` (pads) | String Cyborgs: Zinc string machine | **CC0-1.0** | LICENSE line 3: "CC0 1.0 Universal"; readme.txt: "This instrument is open source, and you can do whatever you want with it" – [LICENSE](https://raw.githubusercontent.com/sfzinstruments/karoryfer.string-cyborgs/f2238b3e36ae64c6383356221dc89c9c476c7b11/LICENSE) | https://github.com/sfzinstruments/karoryfer.string-cyborgs/tree/f2238b3e36ae64c6383356221dc89c9c476c7b11 |
| `karoryfer-green-guitar` (guitar) | Black And Green Guitars: Green (clean electric, twang) | **CC0-1.0** | LICENSE line 3: "CC0 1.0 Universal"; readme.txt: "Royalty-free for all commercial and non-commercial use." – [LICENSE](https://raw.githubusercontent.com/sfzinstruments/karoryfer.black-and-green-guitars/b3b3249d37dc977a1a297bd2dc053e6d9b6b805c/LICENSE) | https://github.com/sfzinstruments/karoryfer.black-and-green-guitars/tree/b3b3249d37dc977a1a297bd2dc053e6d9b6b805c |
| `vcsl-hand-percussion` (perc) | VCSL Hand Percussion (tambourine, shaker, cowbell, claves, congas, bongos) | **CC0-1.0** | README.md line 6: "This collection is under a Creative Commons 0 license. ... (even make commercial software), no royalties, no credit, no special terms." (LICENSE line 3: "CC0 1.0 Universal") – [README.md](https://raw.githubusercontent.com/sgossner/VCSL/c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e/README.md) | https://github.com/sgossner/VCSL/tree/c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e |
| `osiris-piano` (keys) | Osiris Piano (upright, mic A) | **CC0-1.0** | LICENSE line 3: "CC0 1.0 Universal" – [LICENSE](https://raw.githubusercontent.com/sfzinstruments/Osiris_Piano/18c6afccb60cff458edbf7c394571783e074e1e9/LICENSE) | https://github.com/sfzinstruments/Osiris_Piano/tree/18c6afccb60cff458edbf7c394571783e074e1e9 |

## Credit lines to ship (About box / download page)
- Big Rusty Drums by Karoryfer Samples (CC0 1.0).
- Pipe Organ sampled by Simon Dalzell (Ivy Audio), VCSL by Versilian Studios LLC (CC0 1.0).
- String Cyborgs (Zinc) by Karoryfer Samples (CC0 1.0).
- Black And Green Guitars by Karoryfer Samples, recorded by Brian Wood (CC0 1.0).
- Percussion from VCSL by Versilian Studios LLC (CC0 1.0).
- Osiris Piano by Karoryfer Samples (CC0 1.0).

Subsets: Big Rusty Drums uses the close mics only (kick, snare centre+side stick top, hi-hat tip closed/open, toms 14"/18", crash 17", ride 22"). It keeps every velocity layer and round robin. Green guitar uses ord/twang rr1, every 3rd semitone E2–C#6 (MIDI 40–85), at p/mf/f. Osiris uses mic A with both velocity layers. Organ uses the quiet Manual III take (21 notes). Zinc uses the full-length samples, A1–A5. The percussion pack uses selected VCSL hits.
