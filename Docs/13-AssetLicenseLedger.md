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

**The project contains zero binary art, audio, model, texture or font assets.**
Everything tracked is Markdown documentation, C# build scripts, C++ source, and a
JSON project descriptor, all authored for this project. This is the cleanest
possible starting position: there is nothing unprovenanced to quarantine.

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
- Exact license/agreement/version: **UNVERIFIED** — repository licence file not yet read and recorded
- Internal package path: `Web/PixelStreamingInfrastructure` (gitignored, not vendored)
- Notes/obligations: pulls a large npm dependency tree that is **not yet inventoried**. Transitive npm licences must be enumerated before any public deployment. Status is deliberately `APPROVED_INTERNAL_ONLY` until that is done.

### ASSET-0004: Supplied racing screenshots

- Status: `QUARANTINED`
- Category: brand / reference
- Supplier/creator: third-party commercial racing titles (unidentified)
- Permitted use: **visual direction reference only, viewed by humans.**
- Prohibited, without exception: copying, tracing, scraping, ripping, decompiling, photogrammetry, texture extraction, colour-picking for brand-identifying liveries, or use as training/generative input for any project asset.
- Internal package path: **must never enter `Content/` or the repository.**
- Notes/obligations: enforces the art-direction clause of `PROMPT_TO_START.md` and the "Never copy…" clause of `CLAUDE.md`. Any asset whose provenance traces to these images is `REJECTED` on sight.

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
3. Pixel Streaming Infrastructure licence and its full npm dependency licence set (ASSET-0003).
4. Whether any generative-AI tooling may be used to author project art, and under what provenance record — currently **undecided**, so no AI-generated asset may be imported.

## Build audit requirements

The release pipeline should fail when:

- an imported external package has no ledger ID;
- a package marked `QUARANTINED`, `REJECTED`, or `EXPIRED` is referenced;
- a restricted package enters a build variant not named in its approval;
- required credit/notice metadata is missing;
- the license proof or approval record cannot be located.
