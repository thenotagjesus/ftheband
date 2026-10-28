# FtheBand sound pack catalog

`packs.json` is fetched at runtime by the in-app **Get more sounds** browser from
`https://raw.githubusercontent.com/thenotagjesus/ftheband/main/catalog/packs.json`.

**Only licence-checked CC0 or CC-BY packs are allowed.** No NC (non-commercial), no ND (no-derivatives), no
"personal use only", no unknown licences. (CC-BY-SA is also rejected by the app.) Check the licence in the source
repository's LICENSE/README, pin every URL to a commit, and for CC-BY give the exact credit line: it is shown in the
app's About tab.

Format: a JSON array of packs with `id` (folder name, `[a-z0-9-_]`), `name`, `category` (drums*/perc feed the
Drums chair, bass the Bass chair, anything else the Keys chair), `licence` (e.g. `CC0-1.0`, `CC-BY-4.0`),
`licence_url`, `licence_quote`, `credit`, `upstream_url`, `size_bytes`, `file_count` and `entries`:
`{url, dest, root_note, velocity, keytrack, sha256, size}` with `dest` = `packs/<id>/[<category>/]file`.
Every entry needs an https URL pinned to a commit and its sha256; the app rejects the pack otherwise. An optional
pack-level `sha256` (all files concatenated in entry order) is checked too. The `*.manifest` files are the same
entries in `assets/samples.manifest` format, and `CREDITS-additions.md` holds the matching CREDITS.md rows.
