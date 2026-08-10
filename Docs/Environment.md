# Environment record

Phase 0 record. Values marked `VERIFIED` were produced by running the command or
reading the file on this machine. Values marked `UNVERIFIED` have an entry in
*Known blockers and assumptions* and must not be treated as fact.

Recorded: 2026-08-10.

## Project

- Project name/path: `RacingSim` — `C:\Users\jun yi\Documents\central-command\racing` (VERIFIED)
- Repository URL/branch/commit: local-only git, branch `main`, no remote (VERIFIED — see ADR-0004)
- Unreal Engine version and exact patch: **5.8.1**, Changelist **56057345**, CompatibleChangelist 55116800, BranchName `++UE5+Release-5.8` (VERIFIED — `Engine/Build/Build.version`)
- Engine source or launcher build: **Launcher build**, `IsPromotedBuild: 1`. `GenerateProjectFiles.bat` is absent, which is expected; UnrealBuildTool C# source *is* shipped (VERIFIED)
- Project association: `"EngineAssociation": "5.8"` in `RacingSim.uproject` (VERIFIED — editor target builds against `C:\Program Files\Epic Games\UE_5.8`)
- Build ID/versioning method: UNVERIFIED — no build-ID scheme yet; delivered by `CORE-002`

## Development workstation

- OS/version: Windows 11 Pro 10.0.26200, build 26200 (VERIFIED)
- CPU/RAM: Intel Core Ultra 5 125H — 14 physical / 18 logical cores; **15.7 GB RAM** (VERIFIED)
- GPU/VRAM/driver:
  - NVIDIA GeForce RTX 3050 **6 GB** Laptop GPU, driver 32.0.16.1047 (VERIFIED)
  - Intel Arc iGPU, driver 31.0.101.5125 (VERIFIED) — hybrid graphics; Unreal must be pinned to the NVIDIA adapter or Pixel Streaming loses NVENC
- storage/free space: C: 275 GB free · X: 183 GB · Y: 64 GB · G/H/Z: 261 GB (VERIFIED)
- compiler/IDE/toolchain: **MSVC 14.44.35222** from Visual Studio Build Tools 2022 17.14.36811.4, with **Windows SDK 10.0.26100.0** (VERIFIED — reported by UBT during a successful build). Also installed but unused: VS Build Tools 2026 18.7.11925.98 (MSVC 14.51.36231). No Visual Studio IDE. See ADR-0002.
- source-control client/version: git 2.53.0.windows.2, git-lfs 3.7.1 (VERIFIED)
- Git LFS/Perforce status and lock test: LFS initialized; `*.uasset`/`*.umap` marked `lockable`. **Lock test FAILED** — `Locking Test.uasset failed: missing protocol` because lock enforcement requires an HTTP LFS server and this repository is local-only. Locks are **advisory**. Perforce not installed (`p4` absent). (VERIFIED — see ADR-0004)

Other tooling: node **v24.18.0**, npm **11.16.0**, python 3.13.14 (VERIFIED). No .NET SDK on PATH; UE's bundled `DotNet/10.0/win-x64` is used by UBT and is sufficient (VERIFIED).

## Reference GPU worker

**UNVERIFIED — no reference worker exists.** Decision recorded in ADR-0003: this
laptop is the *development machine only* and is explicitly **not** the reference
worker. Gates D, E and F cannot be measured until a worker is named.

- provider/region or host identifier: UNVERIFIED
- OS/image: UNVERIFIED
- CPU/RAM: UNVERIFIED
- GPU/VRAM/driver: UNVERIFIED
- hardware encoder and supported codecs: UNVERIFIED for the worker. On the local
  RTX 3050 (GA107) NVENC supports H.264 and HEVC; **AV1 encode is not available**
  on this generation, so the AV1 arm of the codec comparison in
  `Docs/05-PixelStreaming.md` cannot be run locally.
- Unreal process/session limit: UNVERIFIED
- firewall/public/private ports: UNVERIFIED

## Browser/network support matrix

UNVERIFIED — no stream has been established yet. To be filled by `STREAM-001`.

- browser/version: UNVERIFIED
- OS/version: UNVERIFIED
- controller: UNVERIFIED
- target region: UNVERIFIED
- test RTT/jitter/loss/bandwidth profile: UNVERIFIED
- stream resolution/frame rate/codec/bitrate policy: UNVERIFIED

## Plugins

Enabled in `RacingSim.uproject` and confirmed present in the engine install.
Editor-only plugins carry `"TargetAllowList": ["Editor"]`, which keeps them out
of Game/Client/Server targets per hard constraint #6 and Gate G. The field name
and its `EBuildTargetType` values were verified against
`Engine/Source/Runtime/Projects/Private/PluginReferenceDescriptor.cpp`.

| Plugin | Engine path | Runtime/editor | Why required | In shipping |
|---|---|---|---|---|
| Chaos Vehicles | `Engine/Plugins/Experimental/ChaosVehiclesPlugin` | Runtime | Mandated vehicle simulation (hard constraint #2) | Yes |
| Enhanced Input | `Engine/Plugins/EnhancedInput` | Runtime | Keyboard/gamepad input layer | Yes |
| Pixel Streaming 2 | `Engine/Plugins/Media/PixelStreaming2` | Runtime | Browser delivery (hard constraint #4) | Yes |
| Functional Testing Editor | `Engine/Plugins/Tests/FunctionalTestingEditor` | Editor | Functional/automation test authoring | No |
| Python Editor Script | `Engine/Plugins/Experimental/PythonScriptPlugin` | Editor | Editor/content automation only, never gameplay | No |
| Editor Scripting Utilities | `Engine/Plugins/Editor/EditorScriptingUtilities` | Editor | Batch asset operations | No |
| Model Context Protocol | `Engine/Plugins/Experimental/ModelContextProtocol` | Editor | Experimental editor automation, loopback only | No |
| All Toolsets | `Engine/Plugins/Experimental/Toolsets/AllToolsets` | Editor | MCP toolset registry dependency | No |

Available but **not** enabled: `ChaosModularVehicle`, `Gauntlet`, `PixelStreaming` (v1).
PixelStreaming2 pulls `NVCodecs` and `AMFCodecs` transitively, giving the hardware encode path.

Exact plugin versions: UNVERIFIED — all ship with UE 5.8.1 and carry no independent version; treat the engine changelist as their version.

## Pixel Streaming Infrastructure

- repository source: `https://github.com/EpicGamesExt/PixelStreamingInfrastructure` (VERIFIED)
- exact UE-matching branch/tag/commit: branch **`UE5.8`**, commit **`48bff3b751f91f735b50c90b2a7fec5ceb2a440f`** (2026-08-04), `RELEASE_VERSION` **0.1.0**, `SignallingWebServer` package version 3.0.0 (VERIFIED — cloned and checked out detached at that commit)
- local path: `Web/PixelStreamingInfrastructure` — **gitignored, not vendored**. Re-fetch by cloning the pinned commit.
- Node version from the repository: **none pinned upstream** — no `.nvmrc` and no `engines` field in the root, `Signalling`, or `SignallingWebServer` `package.json` (VERIFIED). Local node is v24.18.0; compatibility is UNVERIFIED until the signalling server actually runs.
- frontend/signalling modifications: none
- STUN/TURN implementation: UNVERIFIED

**Caution:** the shipped `get_ps_servers.bat` in UE 5.8.1 has **no `5.8` case** in
its version table and silently defaults to branch `UE5.7`. It also writes into
`Program Files`, requiring elevation. It was not used. Fetch by explicit clone of
the pinned commit, or `get_ps_servers.bat /b UE5.8` from an elevated shell.

## Discovered commands

Recorded only after executing and verifying on this environment.

Note: paths contain a space (`Program Files`, `jun yi`). Invoking these through a
POSIX shell with forward slashes silently fails with
`'C:\Program' is not recognized`. Use PowerShell's call operator, as shown.

### Generate project/build files

```text
UNVERIFIED
```
Not required for the current workflow — UBT builds directly from the `.uproject`.
A launcher build has no `GenerateProjectFiles.bat`; the equivalent is
`Build.bat -projectfiles -project=<uproject> -game -engine`, which has not been run.

### Compile editor target

```powershell
# VERIFIED 2026-08-10 - exit 0, no warnings, 179.60s
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' `
    RacingSimEditor Win64 Development `
    -project="C:\Users\jun yi\Documents\central-command\racing\RacingSim.uproject" `
    -waitmutex
```
Produces `UnrealEditor-RacingSim.dll`. UBT log: `C:\Users\jun yi\AppData\Local\UnrealBuildTool\Log.txt`.

**Precondition:** no Unreal Editor or `LiveCodingConsole` process may be running,
or the build aborts with `Unable to build while Live Coding is active` and exit code 6.

### Run automation tests

```text
UNVERIFIED
```

### Cook/package

```text
UNVERIFIED
```

### Run packaged Pixel Streaming build

```text
UNVERIFIED
```

### Start signalling/frontend/TURN

```text
UNVERIFIED
```

## Unreal MCP

- endpoint confirmed loopback-only: UNVERIFIED
- generated `.mcp.json` inspected: UNVERIFIED — not yet generated; `.gitignore` already excludes it as machine-local
- read-only tool discovery test: UNVERIFIED
- write test and rollback: UNVERIFIED
- shipping exclusion verified: **partially** — `ModelContextProtocol` and `AllToolsets` carry `"TargetAllowList": ["Editor"]` in `RacingSim.uproject` (VERIFIED by inspection). Not yet proven by inspecting a packaged Game target's plugin manifest.

## Known blockers and assumptions

### BLOCKER-001 — no reference GPU worker

- blocker: Gates D, E and F specify thresholds on a named reference worker. None exists.
- owner: human project owner
- evidence needed: provider, region, instance type, GPU/VRAM/driver, encoder codec support, sessions-per-GPU, and a measured benchmark lap.
- decision date: open
- note: the local RTX 3050 6 GB / 15.7 GB RAM cannot meet Gate E and is designated development-only (ADR-0003).

### BLOCKER-002 — LFS locking is inert

- blocker: `lockable` attributes cannot be enforced without an LFS server, so hard constraint #7 rests on process discipline alone.
- owner: human project owner
- evidence needed: a decision to either add an LFS-capable remote, adopt Perforce, or formally accept process-only serialization.
- decision date: open

### BLOCKER-003 — memory pressure limits build throughput

- blocker: UBT requested 14 parallel actions and was limited to **1** — only ~321 MB physical RAM was free at build time on a 15.7 GB machine.
- owner: human project owner
- evidence needed: whether 16 GB is accepted for the graybox milestone, or the workstation is upgraded before content work begins.
- decision date: open
- note: this worsens sharply once shader compilation and cooking start.

### ASSUMPTION-001 — node v24.18.0 runs the pinned signalling server

- Upstream pins no node version. Compatibility is assumed and **not yet proven**.
- evidence needed: `npm install` plus a signalling server start against commit `48bff3b7`.

### BLOCKER-004 — project subagents do not dispatch

- blocker: the mandatory ticket protocol in `CLAUDE.md` and `PROMPT_TO_START.md`
  requires implementation, then `code-reviewer`, then `test-engineer`, with the
  rule that no agent approves its own change. **None of the eight agents in
  `.claude/agents/` resolve.** Tested 2026-08-10:

  ```text
  Agent type 'code-reviewer' not found.
  ```

  The registered roster contains only `ecc:*`, `caveman:*` and built-ins.
  `ecc:code-reviewer` is a different agent with different instructions and is not
  a substitute for the project's tailored reviewer.

- Affected: `code-reviewer`, `test-engineer`, `vehicle-physics-engineer`,
  `race-systems-engineer`, `pixel-streaming-engineer`, `rendering-tech-artist`,
  `performance-engineer`, `ip-compliance-auditor`.
- The agent definitions themselves are well-formed: correct `name`/`description`/
  `tools`/`model` frontmatter, read-only tool sets on the two reviewer agents,
  `isolation: worktree` on the code-only implementers. The defect is registration,
  not authoring.
- owner: human project owner
- evidence needed: one successful `code-reviewer` dispatch returning a report.
- likely fix: restart Claude Code with `racing/` as the working directory so the
  project agent directory is enumerated at startup. If that fails, the protocol
  must be formally re-specified against available agents — which is a governance
  change requiring approval, not a silent substitution.
- **Consequence: no implementation ticket may start.** Every ticket from CORE-001
  onward requires independent review and test agents.

### NOTE-001 — UnrealBuildTool opens a non-loopback listener

- `UbaServer - Listening on 0.0.0.0:1345` during builds (Unreal Build Accelerator).
- Build-time only and never shipped, but it is a non-loopback listener on the development machine and belongs in the Gate G security record.
