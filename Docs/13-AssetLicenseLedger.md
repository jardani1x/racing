# Asset and license ledger

No external asset may be imported until this record is created. No restricted/brand-facing asset may enter a public build unless `Status` is `APPROVED` and the evidence has been reviewed by the legal owner.

## Status values

- `PROPOSED`
- `QUARANTINED`
- `REVIEWING`
- `APPROVED_INTERNAL_ONLY`
- `APPROVED`
- `REJECTED`
- `EXPIRED`

## Entry template

### ASSET-0000: descriptive name

- Status:
- Category: model / texture / material / scan / audio / font / plugin / code / brand / vehicle / circuit / livery / other
- Supplier/creator:
- Licensee legal entity:
- Source/proof location:
- Date acquired:
- Exact license/agreement/version:
- Permitted project/builds:
- Commercial use:
- Modification rights:
- Redistribution/source restrictions:
- Platforms and cloud/streaming rights:
- Territories:
- Term/expiration:
- Attribution/notices:
- Marketing/trailer/screenshot rights:
- Manufacturer/venue/sponsor approvals:
- Generative-AI restrictions:
- Internal package path:
- Reviewer and review date:
- Notes/obligations:

## Current inventory

Initialized `LEGAL-001`, 2026-08-10. Inventory of the entire working tree at that date.
**Re-inventoried 2026-08-10 (second pass)** after `Config/`, `Samples/` and a
packaged build appeared.

**The project contains zero binary art, audio, model, texture or font assets.**

Re-verified by extension census over `git ls-files` (VERIFIED, second pass):
43 `.md`, 3 `.ini`, 3 `.cs`, 2 extensionless, 1 `.uproject`, 1 `.bat`, 1 `.sh`,
1 `.cpp`, 1 `.h`. A filter for `uasset|umap|fbx|obj|png|jpg|tga|exr|hdr|psd|wav|
mp3|ogg|ttf|otf|dll|so|lib|pdb|mp4|blend|glb|gltf` returned **zero tracked files**.
`Content/` contains no files at all. `git status --untracked-files=all` is clean,
so nothing unprovenanced sits outside the index either.

The second pass found **one previously unrecorded external asset**: the two
Epic-authored `get_ps_servers` scripts vendored under `Samples/` — now
`ASSET-0006`. The `.uproject`, the three `Config/*.ini` files and the gitignored
`Packaged/` build output introduce no third-party content of their own.

### ASSET-0001: Unreal Engine 5.8.1

- Status: `APPROVED`
- Category: code / engine
- Supplier/creator: Epic Games, Inc.
- Source/proof location: Launcher install, `C:\Program Files\Epic Games\UE_5.8`
- Date acquired: pre-existing on the development machine
- Exact license/agreement/version: Unreal Engine EULA (Publishing/Creators licence per Epic account). **Exact accepted EULA variant: UNVERIFIED** — needs confirmation from the Epic account holder.
- Commercial use: per EULA, subject to Epic's royalty terms
- Platforms and cloud/streaming rights: EULA permits server-side/streamed delivery; the specific royalty treatment of a Pixel-Streamed product must be confirmed before commercial launch
- Attribution/notices: Unreal Engine trademark attribution required in credits and marketing
- Internal package path: n/a — engine, not project content
- Notes/obligations: **Open question for the legal owner** — royalty basis for a browser-streamed product where the user never downloads the binary. Must be settled before any public/commercial build.

### ASSET-0002: Engine plugins enabled in RacingSim.uproject

- Status: `APPROVED`
- Category: plugin / code
- Supplier/creator: Epic Games, Inc.
- Covered: ChaosVehiclesPlugin, EnhancedInput, PixelStreaming2 (and its transitive PixelCapture, AVCodecsCore, NVCodecs, AMFCodecs, LibVpxCodecs, WebSocketNetworking, WmfMedia, XRBase, MediaIOFramework), FunctionalTestingEditor, PythonScriptPlugin, EditorScriptingUtilities, ModelContextProtocol, AllToolsets
- Exact license/agreement/version: ships with UE 5.8.1; covered by ASSET-0001. No independent plugin version exists — treat engine CL 56057345 as their version.
- Notes/obligations: NVCodecs and AMFCodecs wrap vendor SDKs (NVIDIA NVENC, AMD AMF) with their own redistribution terms. **UNVERIFIED** — must be reviewed before a public build ships to a GPU worker fleet.

### ASSET-0003: Pixel Streaming Infrastructure

- Status: `APPROVED_INTERNAL_ONLY`
- Category: code
- Supplier/creator: Epic Games / `EpicGamesExt`
- Source/proof location: `https://github.com/EpicGamesExt/PixelStreamingInfrastructure`, branch `UE5.8`, commit `48bff3b751f91f735b50c90b2a7fec5ceb2a440f`
- Date acquired: 2026-08-10
- Exact license/agreement/version: **VERIFIED 2026-08-10** — `LICENSE.md` at the pinned commit is the **verbatim MIT licence text**, `Copyright Epic Games, Inc.`, with no additional clauses. The file is not titled "MIT" but the grant, condition and warranty-disclaimer paragraphs match MIT word for word. Permits use, copy, modify, merge, publish, distribute, sublicense and sell, conditioned only on reproducing the copyright and permission notice.
- Attribution/notices: the copyright and permission notice must ship in any distribution or substantial portion — including a hosted frontend bundle. **Obligation is open**: no notices file exists in this project yet.
- Internal package path: `Web/PixelStreamingInfrastructure` (gitignored, not vendored)
- Notes/obligations: the repository's own licence is now settled; **its npm dependency tree is inventoried under `ASSET-0005` and is not clean.** Status stays `APPROVED_INTERNAL_ONLY` until `ASSET-0005`'s AGPL question is answered and a notices file exists.
- Correction to an earlier record: `Docs/Environment.md` previously stated that upstream pins no Node version, based on the absence of `.nvmrc` and an `engines` field. **That was wrong.** The repository root contains a `NODE_VERSION` file reading `v22.14.0` (VERIFIED). See `ASSUMPTION-001`.

### ASSET-0004: Supplied racing screenshots

- Status: `QUARANTINED`
- Category: brand / reference
- Supplier/creator: third-party commercial racing titles (unidentified)
- Permitted use: **visual direction reference only, viewed by humans.**
- Prohibited, without exception: copying, tracing, scraping, ripping, decompiling, photogrammetry, texture extraction, colour-picking for brand-identifying liveries, or use as training/generative input for any project asset.
- Internal package path: **must never enter `Content/` or the repository.**
- Notes/obligations: enforces the art-direction clause of `PROMPT_TO_START.md` and the "Never copy…" clause of `CLAUDE.md`. Any asset whose provenance traces to these images is `REJECTED` on sight.

### ASSET-0005: npm dependency tree of the Pixel Streaming Infrastructure

- Status: `REVIEWING`
- Category: code
- Supplier/creator: ~1,389 npm publishers
- Source/proof location: `Web/PixelStreamingInfrastructure/package-lock.json` at commit `48bff3b7`, `lockfileVersion: 3`
- Date acquired: 2026-08-10
- Method: licence fields read directly out of the lockfile. **`npm install` was not run**, so this is the declared licence set at the pinned commit, not an inspection of installed package files. Any package whose lockfile metadata is wrong is not caught by this pass.

Declared licence census (VERIFIED, 1,389 package entries):

| Count | Licence |
|---|---|
| 1154 | MIT |
| 65 | Apache-2.0 |
| 62 | ISC |
| 28 | BSD-3-Clause |
| 27 | BSD-2-Clause |
| 14 | BlueOak-1.0.0 |
| 6 | (MIT OR CC0-1.0) |
| 2 | (Apache-2.0 AND BSD-3-Clause) |
| 2 | 0BSD |
| 1 each | CC BY-SA 4.0 · MIT-0 · Python-2.0 · CC-BY-4.0 · `["MIT","Apache2"]` · `SEE LICENSE IN LICENSE` · CC-BY-3.0 · CC0-1.0 · **AGPL-3.0-or-later** |
| 19 | no `license` field at all |

Permissive licences dominate and raise no obstacle beyond attribution. Four findings need a decision:

1. **`ua-parser-js@2.0.10` — `AGPL-3.0-or-later`. The one serious finding.**
   Reached by: `SFU` (production workspace) → `@epicgames-ps/mediasoup-sdp-bridge`
   → `mediasoup-client` (declared there as both a `devDependency` and a
   `peerDependency` `>=3.10.0 <3.11.0`) → `ua-parser-js`. The workspace lockfile
   marks the installed copy `dev: true`, and a source grep found **no `ua-parser-js`
   import in any workspace source file**, so it does not currently reach the browser
   frontend bundle. But it hangs off a *peer* dependency of a package the SFU
   depends on in production, and npm 7+ installs peer dependencies — a standalone
   SFU install can therefore pull it as a runtime dependency. AGPL §13 attaches
   obligations to *network-served* software, which is exactly this product's shape.
   **Legal decision required before the SFU is deployed.** Note also that
   `ua-parser-js` 2.x is dual-licensed AGPL/commercial by its author.
2. **`spawndamnit@3.0.1` — `SEE LICENSE IN LICENSE`.** No SPDX identifier; the
   actual terms are only in a file inside the package tarball, which has not been
   fetched. Dev-only (tooling). UNVERIFIED.
3. **Attribution-only licences** — `CC BY-SA 4.0` (`@cspell/dict-en-common-misspellings`),
   `CC-BY-4.0` (`caniuse-lite`), `CC-BY-3.0` (`spdx-exceptions`), `Python-2.0`
   (`argparse`). All dev-only. They carry notice obligations if their content is
   redistributed; CC BY-SA additionally carries share-alike, which is why it is
   listed rather than waved through.
4. **19 entries with no declared licence.** 13 are the repo's own
   `@epicgames-ps/*` workspace packages and local links, covered by `ASSET-0003`.
   The genuinely unlicensed third-party ones are `exit@0.1.2`, `map-stream@0.1.0`
   and `os-shim@0.1.3` — all dev-only, all unmaintained. UNVERIFIED.

Production-reachable non-MIT-family entries worth naming: `BlueOak-1.0.0` on
`glob`, `minimatch`, `minipass`, `path-scurry`, `lru-cache`, `tar`, `yallist`,
`chownr`. Blue Oak 1.0.0 is permissive and OSI-approved; no obstacle.

- Notes/obligations: re-run this census on every PSI bump. It is a lockfile read,
  so it is cheap and must not be skipped.

### ASSET-0006: `get_ps_servers.bat` / `get_ps_servers.sh` vendored under `Samples/`

- Status: `APPROVED`
- Category: code / script
- Supplier/creator: Epic Games, Inc. (`@Rem Copyright Epic Games, Inc. All Rights Reserved.`)
- Source/proof location: byte-identical copies of
  `C:\Program Files\Epic Games\UE_5.8\Engine\Plugins\Media\PixelStreaming2\Resources\WebServers\get_ps_servers.{bat,sh}`
  (VERIFIED — `diff` reported no differences for either file)
- Date acquired: entered the repository in commit `d9ac6c3`, 2026-08-10
- Exact license/agreement/version: engine content shipped with UE 5.8.1; covered by `ASSET-0001`
- Internal package path: `Samples/PixelStreaming2/WebServers/`
- Notes/obligations: these are the **only tracked files in the repository not authored for this project**, which is why they get their own entry rather than being folded into `ASSET-0001`. They also carry the UE 5.8.1 defect recorded in `Docs/Environment.md`: no `5.8` case in the version table, so they silently fetch branch `UE5.7`. They are retained as a reference copy and **must not be executed** as-is.

## Quarantine policy

Any asset arriving without complete provenance is placed in
`Content/Developer/Quarantine/` — which is never referenced by shipping content —
and given a `QUARANTINED` ledger entry before anything else happens to it. It may
not be referenced by a map, material, Blueprint or data asset while quarantined.
It leaves quarantine only by becoming `APPROVED` or `APPROVED_INTERNAL_ONLY` with
evidence recorded, or by being deleted.

The originating ticket is blocked, not weakened, while an asset sits in quarantine.

## Open legal questions

These need a human legal owner and are **not** resolvable by this project team:

1. Royalty treatment of a Pixel-Streamed product under the Unreal EULA (ASSET-0001).
2. NVENC/AMF redistribution terms for a GPU worker fleet (ASSET-0002).
3. ~~Pixel Streaming Infrastructure licence~~ — **CLOSED 2026-08-10**: verbatim MIT, Copyright Epic Games, Inc. (ASSET-0003). The *dependency* half of this question is now question 5.
4. Whether any generative-AI tooling may be used to author project art, and under what provenance record — currently **undecided**, so no AI-generated asset may be imported.
5. **New, 2026-08-10 — AGPL exposure in the SFU dependency chain.** `ua-parser-js@2.0.10`
   is `AGPL-3.0-or-later` and is reachable from the production `SFU` workspace through
   a peer dependency of `mediasoup-sdp-bridge` (ASSET-0005, finding 1). It is currently
   dev-marked and unused in project source, so nothing is shipping today. A decision is
   needed **before the SFU is deployed**: pin the dependency out, take the author's
   commercial licence, or accept AGPL §13 network-use obligations. Deploying an SFU
   without answering this is the one licence risk in the tree that is not merely a
   notices obligation.
6. Attribution/notices obligations are currently **unmet in aggregate**: MIT (ASSET-0003),
   Apache-2.0, BSD, CC-BY family and Unreal trademark attribution all require notices,
   and this project has no notices file. Needed before any public build, not before
   further development.

## Build audit requirements

The release pipeline should fail when:

- an imported external package has no ledger ID;
- a package marked `QUARANTINED`, `REJECTED`, or `EXPIRED` is referenced;
- a restricted package enters a build variant not named in its approval;
- required credit/notice metadata is missing;
- the license proof or approval record cannot be located.
