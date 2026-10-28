# Code signing

## Code signing policy

> Free code signing provided by [SignPath.io](https://about.signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).

This is the policy for the official Windows build of FtheBand (`FtheBand.exe` inside `FtheBand-<version>-win-x64.zip`).

**What is signed.** Only `FtheBand.exe`, built from the source in this repository by the GitHub Actions workflow
[`.github/workflows/release.yml`](../.github/workflows/release.yml) on a GitHub-hosted runner. The signing request is submitted by that workflow and
SignPath verifies where the file came from (repository, commit, workflow run) before it signs. Third-party files in the zip are **not** signed by us and are
included unmodified: the FlexASIO installer (MIT, SHA-256 pinned in `tools/package-win.ps1`) and the CC0 sample packs (SHA-256 verified against `catalog/packs.json`).
Only FtheBand itself is ever signed; no other project's binaries are signed with this certificate.

**Team roles.** FtheBand is currently a one-person project, so one person holds every role. The account uses multi-factor authentication on both GitHub and SignPath.

| Role | Who |
| --- | --- |
| Author (may change the source without a second review) | [thenotagjesus](https://github.com/thenotagjesus) |
| Reviewer (reviews changes proposed by people who are not committers, e.g. pull requests) | [thenotagjesus](https://github.com/thenotagjesus) |
| Approver (approves every signing request; every release needs a manual approval) | [thenotagjesus](https://github.com/thenotagjesus) |

Pull requests from anyone else are reviewed before they are merged. Build scripts and the workflow are part of the source and get the same care as code, because SignPath's signature
means "this binary is an automated build of this repository".

**Privacy.** This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it.
The only network use is the **Get more sounds** tab: when you press *Refresh catalog* or *Install*, FtheBand downloads the pack catalog
(`https://raw.githubusercontent.com/thenotagjesus/ftheband/main/catalog/packs.json`) and the sample files it lists (also from `raw.githubusercontent.com`, SHA-256 checked).
Nothing is sent about you, there is no telemetry, no account and no auto-update. The zip is portable: there is no installer, nothing is written outside the unzipped folder
except your own settings and downloaded packs in `%APPDATA%\Ftheband`, and deleting the folder (and that data folder) uninstalls it. The optional FlexASIO driver is a separate installer
that you run yourself; FtheBand never runs it.

## Why we sign

Unsigned executables are blocked by Windows Smart App Control and flagged by SmartScreen. Authenticode signing fixes the "unknown publisher / blocked" part. Note that even a
validly signed file can show a SmartScreen warning for a while: reputation builds up with download history over days or weeks. Signing is still required for Smart App Control to allow the app.

## What the release workflow does

`release.yml` runs on a tag `v<version>` (the tag must equal `FTHEBAND_DISPLAY_VERSION` in `CMakeLists.txt`, for example `v0.1.0-beta`) or by hand (Actions -> release -> Run workflow).

1. Builds Release with `build.ps1`: ASIO **on** (Steinberg ASIO SDK 2.3.4 downloaded, SHA-256 checked, and its `LICENSE.txt` must still offer the GPLv3 option), VST2 **off**.
2. Checks that `FtheBand.exe` has product name `FtheBand` and product version equal to the CMake `project(VERSION)` (SignPath enforces both).
3. Signs `FtheBand.exe` (SignPath, or Azure if you switched it on; nothing if neither is configured), then verifies the Authenticode signature and timestamp.
4. Runs `tools/package-win.ps1 -SkipBuild`: base samples, the 6 CC0 packs (SHA-256 verified), FlexASIO 1.10b (pinned SHA-256), licence files, zip.
5. Verifies the zip (6 packs, FlexASIO pin, no ASIO4ALL, signed exe inside) and uploads `FtheBand-<version>-win-x64.zip` plus a `.sha256` file as a workflow artifact
   (named `...-signed` or `...-unsigned`). It never publishes a GitHub Release by itself.

### How signing is selected

| Repo variable `SIGNING_PROVIDER` | Behaviour |
| --- | --- |
| empty (default) | SignPath if `SIGNPATH_API_TOKEN` (secret) and `SIGNPATH_ORGANIZATION_ID` (variable) exist, otherwise an **unsigned** build with a warning |
| `signpath` | SignPath; the run fails if its secret/variables are missing |
| `azure` | Azure Artifact Signing (optional, never used unless you set this); fails if its secrets/variables are missing |
| `none` | Always unsigned |

The manual run also has a `signing: none` choice to force an unsigned test build.

## Option A (primary, free): SignPath Foundation

### Eligibility (checked 2026-10-28, from https://signpath.org/terms.html)

| SignPath condition | FtheBand |
| --- | --- |
| OSI-approved licence, "without commercial dual-licensing for all components" | AGPL-3.0-only. **Watch out:** the build depends on JUCE (AGPLv3 / commercial dual licence, used under AGPLv3) and the ASIO SDK (GPLv3 / proprietary dual licence, used under GPLv3). FtheBand itself is not dual-licensed. Be ready to explain this in the application. |
| No proprietary code | VST2 is off, so no Steinberg VST2 SDK. Everything bundled is AGPL/GPL/MIT/CC0. ASIO4ALL is not bundled. |
| Actively maintained | Yes (see the commit history). |
| Already released in the form to be signed | **Needs a first public release** (a tagged GitHub Release with the zip), see "Before you apply". |
| Functionality described on its download page | README and the release notes; add a short description to the Release page. |
| Code signing policy on the home page and release pages, using the words "Code signing policy" | This page, linked from README.md and the landing page. |
| MFA for all members on GitHub and SignPath | Turn it on for both before you apply. |
| Roles: authors / reviewers / approvers | Table above. |
| Artifact configuration with product name and product version enforced | `.signpath/artifact-configurations/default.xml`. |
| Built from source in a verifiable way, every release manually approved | GitHub-hosted runner, `release.yml`, manual approval in the signing policy. |
| Reputation: "for executable programs we require a certain verifiable reputation" | **The main risk.** SignPath says it will not sign "source code that nobody knows". The repository is new with few or no stars, forks or releases. Public reports from 2026 show similar young projects being declined for this reason. Build visible reputation first (a release, an issue tracker in use, a project page, some users or links, ideally a few months of history) and say honestly where the project stands. |

Other points on that page: the certificate is issued to SignPath Foundation (so the publisher shown in Windows is "SignPath Foundation"), only your own binaries may be signed
(upstream binaries such as the FlexASIO installer may ride along unsigned), and SignPath may decline without giving reasons and without arbitration.

### Cost and timing

Free. The application is reviewed by hand; expect days to a couple of weeks for an answer (SignPath does not publish a service level), then about an hour to set up the project, then about 10 minutes per release
(build + your approval click). Applications can be re-submitted later if declined.

### Step by step

1. **Push everything in the checklist** in `docs/signpath-application.md` ("Before you apply") to the public repository, then publish a first GitHub Release (it can be an unsigned beta zip built by `release.yml` with `signing: none`). Mention in its notes that a signed build is planned.
2. Turn on MFA for your GitHub account and make sure the repository's README links to this page ("Code signing policy").
3. Open https://signpath.org/apply and fill in the form with the answers drafted in `docs/signpath-application.md`. Submit it yourself. Wait for the e-mail.
4. When accepted you get a SignPath.io organization. Sign in (with MFA) at https://app.signpath.io and note the **Organization ID** (shown in the organization's details and in the URL of the web app).
5. Create the project: Projects -> Add project -> "FtheBand", slug **`ftheband`** (or set your own slug in the `SIGNPATH_PROJECT_SLUG` variable). Repository URL `https://github.com/thenotagjesus/ftheband`.
6. Add a **trusted build system**: Trusted build systems -> link **GitHub.com** (follows the SignPath docs for the "SignPath Connector" GitHub App: install https://github.com/apps/signpath on the repository). Add the trusted build system to the project.
7. Artifact configuration: in the project, add an artifact configuration with slug `default` and paste `.signpath/artifact-configurations/default.xml`. Make it the default.
8. Signing policy: create **`release-signing`** using the certificate "SignPath Foundation". Enable **"Approval required"**, set yourself as approver, and (if offered) restrict origin to the repository's `v*` tags / `main` branch and require the trusted build system. Create `test-signing` too if you want trial runs with the test certificate (then pass its slug in `SIGNPATH_SIGNING_POLICY_SLUG`).
9. Create an API user and token: Users -> add a CI user, grant it **Submitter** on the project's signing policy, copy the API token.
10. GitHub repository -> Settings -> Secrets and variables -> Actions:
    - Secret `SIGNPATH_API_TOKEN` = the token.
    - Variable `SIGNPATH_ORGANIZATION_ID` = the organization ID.
    - Variables `SIGNPATH_PROJECT_SLUG` (default `ftheband`) and `SIGNPATH_SIGNING_POLICY_SLUG` (default `release-signing`) only if you used different slugs.
    - Optional: variable `SIGNING_PROVIDER` = `signpath` (so a missing secret fails the run instead of silently building unsigned).
11. Release: bump the version in `CMakeLists.txt`, commit, `git tag v<version>` and push the tag. The workflow builds, waits for SignPath, you approve the request in the SignPath web UI (or from the e-mail), the workflow continues. Download the artifact, test it, attach it to a GitHub Release.

Things to confirm with SignPath's setup UI/support the first time (not verifiable from the outside): whether tag-triggered runs satisfy the "allowed branches" origin restriction of the policy (if not, add the tag pattern or sign from `main` via manual dispatch);
and the exact wording of the application form (the form is rendered by JavaScript).

## Option B (optional, off by default): Azure Artifact Signing (formerly Trusted Signing)

### Eligibility and price (checked 2026-10-28)

- The service was renamed from "Trusted Signing" to **Artifact Signing**; the GitHub action is now `azure/artifact-signing-action` (the old `azure/trusted-signing-action` name redirects).
- **Individual developers** can use it in the **United States and Canada** (organizations: USA, Canada, EU, UK). Source: https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart (prerequisites) and https://learn.microsoft.com/en-us/azure/artifact-signing/faq.
  Canada is listed as eligible. One Microsoft Q&A answer from March 2026 claimed that individual onboarding was paused; I could not find that on any official page, so treat it as unconfirmed and check the portal when you create the identity validation.
- Needs a **paid** Azure subscription (free, trial and sponsored subscriptions do not work), plus identity validation with a government photo ID, Microsoft Authenticator (Verified ID + Face Check) and a billing account of type *Individual* whose legal name and sold-to address match your ID.
- Price: **Basic plan about US$9.99 per month** (5,000 signatures per month; Premium has a 100,000 quota and costs more). Billing starts as soon as the account is created and is not pro-rated. Sources: https://azure.microsoft.com/en-us/pricing/details/artifact-signing/ (the page renders its price table with JavaScript, so the dollar figure comes from Microsoft's announcement and third-party write-ups; check it in the portal before you create the account).
- The certificate subject will be **your own legal name** (individual validation), not a project name. Certificates are short-lived (3 days) and renewed by the service; the signature is timestamped, so it stays valid.
- Timing: identity validation takes about 1 to 20 business days (support cannot expedite it); you get 3 attempts.

### Step by step

1. Create an Azure account and a pay-as-you-go subscription (a card is needed).
2. In the portal: Subscriptions -> your subscription -> Resource providers -> register **Microsoft.CodeSigning**.
3. Create an **Artifact Signing account** (pick the region, note its endpoint, e.g. `https://eus.codesigning.azure.net`) on the Basic SKU.
4. Assign yourself the **Artifact Signing Identity Verifier** role, then Identity validation -> New -> **Individual**; finish the Verified ID / Face Check steps on your phone. Wait for "Completed".
5. Create a **Public Trust certificate profile** from that validation. Note the account name and the profile name.
6. Create an app registration (Entra ID) for GitHub: add a **federated credential** for GitHub Actions on `thenotagjesus/ftheband`, entity type *Environment* or *Tag* (`v*`) (so no client secret is stored).
   Give it the role **Artifact Signing Certificate Profile Signer** on the signing account.
7. GitHub repository -> Settings -> Secrets and variables -> Actions:
    - Secrets `AZURE_CLIENT_ID`, `AZURE_TENANT_ID`, `AZURE_SUBSCRIPTION_ID`.
    - Variables `AZURE_SIGNING_ENDPOINT`, `AZURE_SIGNING_ACCOUNT`, `AZURE_CERT_PROFILE`.
    - Variable `SIGNING_PROVIDER` = `azure`.
8. Tag a release as above. No approval step is needed; the workflow signs and packages straight away.

Azure does not remove the SmartScreen reputation warm-up either, but its certificates chain to a root Windows trusts for Smart App Control.
If the SignPath application is declined and you want a signed build soon, this is the fallback (about US$10/month).
