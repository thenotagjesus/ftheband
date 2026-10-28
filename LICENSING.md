# Licensing: FtheBand is free software under the GNU AGPL v3 (optional donations)

FtheBand is licensed under the **GNU Affero General Public License, version 3 only** (`SPDX-License-Identifier:
AGPL-3.0-only`). The full text is in `LICENSE`, copied verbatim from https://www.gnu.org/licenses/agpl-3.0.txt.
Every C++/CMake/PowerShell source file has an SPDX header.

I checked the sources below on 2026-10-25. These notes are not legal advice, so re-check them before each release.

## Why AGPLv3 works with every dependency
| Component | Licence used by FtheBand | Official source | Compatible? |
|---|---|---|---|
| JUCE 8.0.8 | **AGPLv3** option of its dual licence | JUCE `LICENSE.md` (https://github.com/juce-framework/JUCE/blob/8.0.8/LICENSE.md): "dual-licensed under the AGPLv3 and the commercial JUCE licence" | Yes: same licence. Choosing AGPL means the JUCE EULA (and its Starter $20k revenue cap, which counts donations) **does not apply**. |
| Steinberg ASIO SDK 2.3.4 (fetched at build time) | **GPLv3** option | `ASIOSDK/LICENSE.txt` in ASIO-SDK_2.3.4_2025-10-15.zip: "licensed under the terms of the Steinberg ASIO License, or alternatively under the terms of the General Public License (GPL) Version 3"; https://www.steinberg.net/developers/ ("ASIO – Open Source variant" under GNU) | Yes. GPLv3 §13 ("Use with the GNU Affero General Public License") and AGPLv3 §13 both explicitly permit combining GPLv3 and AGPLv3 code into one work and conveying it. Each part keeps its own licence, and the network clause of AGPL §13 applies to the combination. |
| VST3 SDK bundled in JUCE 8.0.8 (`modules/juce_audio_processors/format_types/VST3_SDK/LICENSE.txt`) | **GPLv3** option | That file: "Steinberg VST3 License, or alternatively … GPL Version 3" | Yes, for the same reason as the ASIO SDK. |
| Upstream VST3 SDK (newer than the copy in JUCE 8.0.8) | MIT | https://github.com/steinbergmedia/vst3sdk/blob/master/LICENSE.txt ("MIT License, Copyright (c) 2026, Steinberg"); steinberg.net/developers: "available Open Source under the MIT license (VST)" | Yes. MIT is AGPL-compatible. Keep its copyright notice if you upgrade. |
| VST2 SDK (optional `ENABLE_VST2`, default OFF) | Proprietary, no longer licensed by Steinberg, **not GPL-compatible** | (Steinberg discontinued VST2 licensing in 2018) | **No.** Never distribute binaries built with ENABLE_VST2. It exists only for private local builds by people who already hold a VST2 licence. |
| Samples | CC0 / CC BY 3.0 | see CREDITS.md | Yes. Both allow redistribution and commercial use. CC BY needs credit (shown in About box, README, CREDITS.md). Nothing NC, ND or personal-use-only is allowed in this repo. |

Trademarks are separate from copyright. **ASIO is a trademark and software of Steinberg Media Technologies GmbH.** FtheBand says "ASIO" only to name the driver type it can host, never in its own name or logo, never as "ASIO certified/compatible/approved", and states the trademark line in the README, the About tab and THIRD-PARTY-NOTICES.md. The bundled optional FlexASIO installer (MIT) and the non-bundled ASIO4ALL are independent products; see THIRD-PARTY-NOTICES.md.

"ASIO" and "VST" are Steinberg trademarks, and their logos follow
Steinberg's usage guidelines (`VST3_Usage_Guidelines.pdf`, and "Trademark and Logo Usage" in the ASIO README).
Don't use the logos unless you follow those guidelines. Don't use "Rocksmith" (Ubisoft) in the product name or
branding. Describing what the app is "like" in plain text is a different matter.

## What you must do when distributing binaries (AGPLv3 §4–6, plus GPLv3 for the SDK parts)
1. **Ship or offer the Corresponding Source** with every binary (zip, installer, or a download page). That means
   the complete source of FtheBand *and* the source of the linked libraries as used: the JUCE 8.0.8 sources,
   the ASIO SDK `common/` files, and the bundled VST3 SDK. The simplest compliant ways are to:
   - put the source zip next to the binary on the same download page; or
   - give a written offer valid for 3 years (§6b); or
   - (§6d) offer the source "from the same place" at no further charge.
   The CMake build fetches JUCE and the ASIO SDK at pinned versions/hashes. That makes builds reproducible, but
   on its own it does not satisfy §6 for a binary you distribute. Include those sources, or a mirror under your
   control, alongside the binary.
2. Include `LICENSE` (the AGPLv3 text) and keep the copyright and licence notices. The About box shows the
   licence and a link to the source.
3. **Modified versions** must carry notices saying they were modified, and when (§5a), and stay under AGPLv3.
4. Do not add terms that restrict users further (§10). In particular, no "non-commercial only" clause.
5. **Network use (AGPL §13)**: if you (or anyone) modify FtheBand and let users interact with it **remotely over
   a network**, for example a hosted web/jam-server version, those remote users must be offered the corresponding
   source of the modified version from a network server at no charge. The desktop app as shipped is not a
   network service: its only network use is the optional, user-initiated sample-pack download (catalog and files
   from raw.githubusercontent.com), so in practice §13 only matters if a networked or online variant is ever built.
6. Assets (samples) are redistributed under their own licences (CC0 / CC BY 3.0). Keep CREDITS.md and the CC BY
   attribution text with every binary that includes or downloads them.

## Donations and selling are allowed
- The GPL family does not forbid charging. AGPLv3 §4: "You may charge any price or no price for each copy that
  you convey, and you may offer support or warranty protection for a fee." The FSF's "Selling Free Software"
  (https://www.gnu.org/philosophy/selling.html) says the same.
- So you may accept donations ("donate what you like"), sell copies, or sell support. What you **cannot** do is
  stop recipients from sharing, modifying, or redistributing it (even for free), or charge separately for the
  source to people who received the binary (§6: the source must be available at no more than the cost of
  physically performing the transfer).
- The About box has an optional **Donate** link. It is empty by default and is configured through
  `session.xml` → `<Session donateUrl="https://…">` or the CMake option `FTHEBAND_DONATE_URL`. When empty, no
  link is shown.

## Release checklist
- [ ] `LICENSE` (AGPLv3) and `CREDITS.md` are in the binary package
- [ ] A source zip for the exact tag (including JUCE 8.0.8, the ASIO SDK common/ files, and the VST3 SDK) sits on
      the same download page
- [ ] Built with `ENABLE_VST2=OFF`
- [ ] The About box shows licence, source URL, sample credits and (optionally) the donate link
