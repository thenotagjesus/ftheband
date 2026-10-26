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
