# SignPath Foundation application: draft answers

**Draft only. Nothing has been submitted.** Submit it yourself at https://signpath.org/apply after the checklist below is done.
I could not read the live form (it is rendered by JavaScript), so the headings below follow SignPath's published conditions
(https://signpath.org/terms.html) and what other applicants report. Paste each answer into the matching field and adjust the wording to what the form actually asks.
Keep every answer true on the day you submit: SignPath checks the repository and the release page.

## Before you apply: everything that must be public first

SignPath looks at the public repository, its home page and its release page. Push (and publish) these first. This list is from the working tree in `/workspace/ftheband`; nothing is committed yet.

**New files (untracked, must be added):**
1. `.github/workflows/release.yml`: the CI build and signing workflow.
2. `.signpath/artifact-configurations/default.xml`: SignPath artifact configuration (product name and version enforced).
3. `docs/CODE-SIGNING.md`: the code signing policy page SignPath requires.
4. `docs/signpath-application.md`: this file (optional to publish; delete it from the public repo if you prefer).
5. `tools/package-win.ps1` and `tools/FlexASIO-LICENSE.txt`: the packaging script the workflow runs. Without them the build fails.
6. `THIRD-PARTY-NOTICES.md`: the zip packaging step requires it.
7. `Source/Cli/AudioProbe.h`, `Source/Engine/DevicePolicy.h`, `Source/Util/Migration.h`: new headers the modified sources include; the build fails on a clean checkout without them.

**Modified files that matter for this application:** `README.md` (adds the "Code signing policy" section), `index.html` (footer link to the policy), `LICENSING.md` (corrects the "no network features" wording to match the sample-pack downloader), `CMakeLists.txt`.
The other ~40 modified source files are your pending feature work in the mirror; they all need to be pushed together with the new headers so that `main` builds.

**Then, on GitHub:**
- Confirm the repository is public, has a short About description and topics, and that Issues are enabled.
- Turn on MFA for your GitHub account (also required on SignPath later).
- Make sure `main` builds: run the workflow once by hand with `signing: none` (Actions -> release -> Run workflow) and fix anything it reports.
- Publish a **first GitHub Release** (tag `v0.1.0-beta`, matches `FTHEBAND_DISPLAY_VERSION`) with the unsigned zip, a one-paragraph description of what FtheBand does, the AGPL licence note and the link to the Code signing policy. SignPath's terms require the project to be "already released in the form that should be signed" and its functionality "described on its download page".
- The README (project home page) and the Release notes must both contain the heading/link **"Code signing policy"**. Add that line to the release text.

## Honest warning

SignPath's terms say that for executables it requires "a certain verifiable reputation" and that it cannot sign "source code that nobody knows". New repositories with no stars, forks, releases or users have been declined in 2026.
It also asks for "no commercial dual-licensing for all components"; FtheBand itself is AGPL-only, but its build uses JUCE and the ASIO SDK under their open-source (AGPLv3 / GPLv3) options. Say that openly (see answers) rather than leaving them to find it.
If you are declined, you can re-apply later; the fallback is Azure Artifact Signing (about US$10 per month, see `docs/CODE-SIGNING.md`).

## Application answers

**Project name:** FtheBand

**Project / repository URL:** https://github.com/thenotagjesus/ftheband

**Project home page:** https://github.com/thenotagjesus/ftheband (README). Landing page: the Pages site served from `index.html` ("FtheBand"), if you publish it, link it too.

**Download / release page:** https://github.com/thenotagjesus/ftheband/releases

**Code signing policy URL:** https://github.com/thenotagjesus/ftheband/blob/main/docs/CODE-SIGNING.md (also summarised in the README under "Code signing policy")

**Licence (OSI-approved):** GNU Affero General Public License v3.0 only (AGPL-3.0-only), https://github.com/thenotagjesus/ftheband/blob/main/LICENSE

**Short description:**
FtheBand is a free Windows jam app for guitarists and bass players. It listens to the instrument, follows the chords and tempo you play, and plays a follower band
(drums, bass, keys) in real time, with a small DAW-style track view, sample packs and plug-in hosting. It runs fully offline with built-in sounds; extra CC0 sample packs are optional downloads.
Built with C++ and JUCE. Standalone Windows x64 zip, no installer.

**Why do you need code signing?**
Windows Smart App Control blocks unsigned executables outright, and SmartScreen warns on them, so players cannot run the app even though the source is public and auditable.
A signature from a recognised publisher is the only way to let ordinary users run it.

**Maintainers / team and roles:**
Solo maintainer: 666 o'Colock With No Tag Jesus (GitHub: thenotagjesus). Author, reviewer and approver. MFA is on for GitHub and will be on for SignPath. Outside pull requests are reviewed before merging. (Fill in a second approver here if you get one.)

**What would be signed:** `FtheBand.exe` only, our own binary built from this repository. The FlexASIO installer (MIT, unmodified, pinned SHA-256) and CC0 sample files in the zip are upstream and are not signed by us.

**Release process / build system:**
Releases are built by GitHub Actions on GitHub-hosted `windows-latest` runners from a version tag (`.github/workflows/release.yml`). The workflow downloads the Steinberg ASIO SDK under its GPLv3 option and verifies its SHA-256,
builds Release with CMake/Ninja (ASIO on, VST2 off), uploads the unsigned `FtheBand.exe` as a workflow artifact, submits it to SignPath with `signpath/github-action-submit-signing-request`, waits for the manual approval, downloads the signed file,
checks the signature, then runs `tools/package-win.ps1` to add the licence files, base samples, six CC0 sample packs (every file SHA-256 verified against `catalog/packs.json`) and FlexASIO, and uploads the zip. The zip is attached to a GitHub Release by hand.
The artifact configuration (`.signpath/artifact-configurations/default.xml`) sets product name `FtheBand` and a fixed product version, taken from the CMake project version. Every release needs the approver's manual approval.

**Third-party components and licences:**
JUCE 8 (AGPLv3 option, fetched at build time), Steinberg ASIO SDK 2.3.4 (GPLv3 option), optional FlexASIO driver (MIT, not installed by the app), CC0 sample packs. Full list: `LICENSING.md`, `THIRD-PARTY-NOTICES.md`, `CREDITS.md`.
The VST2 SDK is not used (VST2 support is off in release builds). No proprietary code is included. ASIO4ALL is not bundled.
JUCE and the ASIO SDK are dual-licensed by their authors; FtheBand itself is not dual-licensed and uses them only under their open-source licences.

**Privacy / user data:**
No telemetry, no account, no auto-update. "This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it." The only network use is the optional *Get more sounds* tab (user presses Refresh catalog or Install),
which downloads a JSON catalog and sample files from raw.githubusercontent.com and checks their SHA-256. Portable zip, no installer; deleting the folder and `%APPDATA%\Ftheband` uninstalls it. It makes no system configuration changes.

**Maintenance status:** Actively developed; see the commit history and releases. (Add the date of the latest commit and release when you submit.)

**Project reputation / community links:** (Fill in honestly: the repo is new. Mention anything real: stars, forks, forum posts, videos, users, mentions, the project page. Do not inflate.)

**Anything else:**
FtheBand is a hobby project by one developer in Canada. I read and accept SignPath Foundation's conditions and the technical constraints (verifiable CI builds, manual approval of every release, no workarounds).
