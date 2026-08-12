# Ordered ticket set

Derived from `Docs/16-InitialBacklog.md` on 2026-08-10. Format follows
`Docs/12-TicketTemplate.md`. Gates refer to `Docs/07-QualityGates.md`.

Every ticket runs the mandatory protocol from `CLAUDE.md`: implement with the
named specialist, then `code-reviewer` (read-only), then `test-engineer`
(read-only), maximum three repair cycles, then a blocker report. **No agent
reviews or tests its own change.**

## Status legend

`DONE` · `OPEN` · `BLOCKED` · `DEFERRED`

---

## Epic 0 — environment and governance

| ID | Title | Owner | Depends on | Gate | Status |
|---|---|---|---|---|---|
| ENV-001 | Record engine/toolchain/worker/browser matrix | director | — | A | **DONE** |
| ENV-002 | Source control, LFS/locks, ignore rules, clean-clone test | director | — | A | **DONE** (locks inert — BLOCKER-002) |
| ENV-003 | Enable/verify plugins and production exclusions | director | ENV-001 | A, G | **DONE** — manifest checked 2026-08-10; conclusion **corrected 2026-08-12**, see B-1 below |
| ENV-004 | Discover build/test/cook/package commands | director | ENV-003 | A | **DONE** — all five criteria met; BLOCKER-006 **resolved** 2026-08-12, packaging no longer uses a workaround |
| ENV-005 | Verify local Unreal MCP, generate client config | director | ENV-003 | G | **DONE** — loopback-only confirmed by probe; write test deliberately deferred |
| LEGAL-001 | Inventory/quarantine assets, initialize ledger | ip-compliance-auditor | — | H | **DONE** — re-inventoried 2026-08-10; audited 2026-08-12; ledger holds 6 assets and **5** open legal questions (#3 closed) |
| ARCH-001 | Pixel Streaming 2 + scaling ADR | director | ENV-001 | — | **DONE** (ADR-0001..0004) |

### ENV-004 — acceptance criteria, closed 2026-08-10

- [x] `RunUAT BuildCookRun` produces a staged, paked, archived Win64 Development build; archive path recorded. **Caveat lifted 2026-08-12:** BLOCKER-006 is resolved by `-stagingdirectory` outside `Documents\`; the clean-stage path now succeeds and `-nocleanstage` has been removed from the canonical command.
- [x] Automation test command runs and reports pass/fail counts; log path recorded. 426/426 engine Smoke tests at close; **427/427 as of 2026-08-12**, the extra one being `RacingSim.Core.LogCategories` from `CORE-001`.
- [x] Packaged build launches with Pixel Streaming 2 arguments; flag names derived from the PS2 CVar-to-arg transform in source. Streamer connected, joined, published video and audio tracks, and survived a signalling restart.
- [x] Signalling server starts under node v24.18.0 at the pinned PSI commit. ASSUMPTION-001 resolved — **and corrected**: upstream *does* pin a version (`NODE_VERSION` = `v22.14.0`); v24.18.0 works but is two majors ahead.
- [x] Every verified command pasted into `Docs/Environment.md`; failures recorded as blockers (BLOCKER-006) and notes (NOTE-002).

Not covered by ENV-004 and still open: no browser client has connected, so no frame has
reached a viewer — that is `STREAM-001`. TURN is untested (`STREAM-004`, blocked). The
SFU cannot run until `mediasoup`'s skipped postinstall is approved.

### ENV-005 — acceptance criteria, closed 2026-08-10

- [x] `ModelContextProtocol` + `AllToolsets` enabled; editor started with `-ModelContextProtocolStartServer` (the server does **not** auto-start; `bAutoStartServer` defaults false).
- [x] `ModelContextProtocol.GenerateClientConfig ClaudeCode` run; `.mcp.json` inspected; `git check-ignore` confirms `.gitignore:52`.
- [x] Listener bound to `127.0.0.1:8000` — single row, no `0.0.0.0`, no `::`.
- [x] Connection attempts to all seven non-loopback IPv4 addresses refused.
- [x] Read-only discovery succeeded (`initialize`, `tools/list`, `list_toolsets`); **no write tool invoked**.
- [x] Packaged Game target contains neither plugin (evidence under ENV-003).

Carried forward: the loopback binding comes from the engine `HTTPServer` default
(`BindAddress = "localhost"`), which an `[HTTPServer.Listeners]` ini entry can silently
override. **SEC-001 must assert that section stays absent.**

**Standing hazard:** every path on this machine contains a space (`Program Files`,
`jun yi`). This has already produced two silent failures — `'C:\Program' is not
recognized`, and a `-project=` argument split at `C:\Users\jun` that surfaced as a
bogus JSON parse error. Quote every path; verify by reading logs, never exit codes.

### B-1 — Gate G plugin-exclusion evidence was wrong, corrected 2026-08-12

Raised by `code-reviewer` in the M0 verification pass. `Docs/Environment.md:197-212`
claimed a search of the packaged tree returned "zero matches" for six editor-only
plugins. That is false for two of them. The artifact shows:

- `Packaged/Windows/Manifest_UFSFiles_Win64.txt` lists `PythonScriptPlugin.uplugin`,
  `EditorScriptingUtilities.uplugin` and `DefaultEditorScriptingUtilities.ini`;
- `Packaged/Windows/RacingSim/Saved/Logs/RacingSim.log` records
  `Mounting Engine plugin EditorScriptingUtilities` and
  `Mounting Engine plugin PythonScriptPlugin`;
- `global.ucas` carries the cooked script-object names `/Script/ModelContextProtocol`
  (47), `/Script/ToolsetRegistry` (10), `/Script/PythonScriptPlugin` (5),
  `/Script/EditorScriptingUtilities` (3).

Cause: `FPluginReferenceDescriptor::IsEnabledForTarget`
(`PluginReferenceDescriptor.cpp:64-85`) is evaluated **per reference**, applied at
`PluginManager.cpp:2425`. The `.uproject` `TargetAllowList` suppresses only the
project's own reference; other enabled engine plugins reference `PythonScriptPlugin`
and `EditorScriptingUtilities` with no allowlist and re-enable them.

**What still holds:** `ModelContextProtocol`, `AllToolsets` and `ToolsetRegistry` are
genuinely absent as descriptors, binaries and mounts. The MCP exclusion is real. Only
the blanket "zero matches" sentence, and the "In shipping: No" column for those two
engine plugins, were wrong.

**Root cause established 2026-08-12, and there is no project-level fix.** The engine
plugins that re-enable the two, filtered to `"EnabledByDefault": true`, are `Bridge`,
`PluginUtils`, `ChaosEditor`, `Fab`, **`Niagara`**, `MetaHumanSDK`, **`PCG`**,
**`RigVM`** and `InterchangeTests`. Niagara, PCG and RigVM are core plugins this project
will need. Suppressing the leak means disabling them, which is not an option. This is
stock UE 5.8.1 behaviour; `RacingSim.uproject`'s `TargetAllowList` entries are correct
and cannot suppress a reference declared by another enabled plugin. **`SEC-001` and Gate
G must assert the narrow, true property** — that `ModelContextProtocol`, `AllToolsets`
and `ToolsetRegistry` are absent — not a blanket "no editor-only plugins are staged",
which is unachievable on a stock engine install.

**Consequence nobody had recorded:** because editor plugins load in the cook
commandlet, *which editor plugins are enabled changes the shipped bytes*. Disabling
`AllToolsets` later will alter `global.ucas`. This belongs in the rollback notes for
any decision to narrow the MCP toolset surface.

**Untested:** no Shipping-configuration build has ever been produced. The exclusion is
confirmed for Game/Development only; `Docs/07-QualityGates.md:9` says "shipping", and
that word remains untested. Gating is on `EBuildTargetType` and orthogonal to
`EBuildConfiguration`, so the inference is strong — but it is an inference.

---

## Epic 1 — project skeleton

| ID | Title | Owner | Depends on | Gate | Status |
|---|---|---|---|---|---|
| CORE-001 | Module/folder structure and logging categories | director | ENV-004 | A | **IMPLEMENTED 2026-08-12, all 8 criteria evidenced — awaiting `code-reviewer` gate before `DONE`** |
| CORE-002 | Settings, build ID, units, telemetry contracts | race-systems-engineer | CORE-001 | A, B | OPEN |
| TEST-001 | Test module and first smoke test | test-engineer + implementer | CORE-001 | A | OPEN |
| CORE-003 | DataAsset validation framework | race-systems-engineer | CORE-002 | A | OPEN |

**CORE-001 is the next ticket on the critical path**, gated only on the M0 signature
(`Docs/Reports/M0-DecisionSheet.md`).

### CORE-001 — acceptance criteria

Module granularity was contradictory between `Docs/15-ProjectStructure.md` and this
file (finding N-2, `code-reviewer` 2026-08-12). **Resolved 2026-08-12 by the project
owner: two modules.**

- [x] `Source/RacingSim/` remains a single `Runtime` module and gains the folders
      `Core/`, `Vehicle/`, `Race/`, `UI/`, `Streaming/`. Folders only — these five are
      **not** separate modules.
- [x] A new `Source/RacingSimTests/` module is added, typed **`UncookedOnly`**, with
      its own `RacingSimTests.Build.cs`. It must not appear in a packaged Game target.
- [x] `RacingSim.uproject` `Modules` array lists both modules with correct types.
- [x] `Source/RacingSim.Target.cs` and `Source/RacingSimEditor.Target.cs` add the
      modules each target needs. The Game target must **not** pull `RacingSimTests`.
- [x] `IMPLEMENT_PRIMARY_GAME_MODULE` stays in exactly one module
      (`Source/RacingSim/RacingSim.cpp:6`); `RacingSimTests` uses `IMPLEMENT_MODULE`.
- [x] One declared logging category per layer, and one for the test module.
      `LogRacingCore`/`Vehicle`/`Race`/`UI`/`Streaming` in
      `Source/RacingSim/Core/RacingSimLog.h`, plus `LogRacingTests` in
      `Source/RacingSimTests/RacingSimTestsLog.h`. Asserted by
      `RacingSim.Core.LogCategories`, including distinctness and non-collision.
- [x] Editor builds with **zero new warnings** — `Result: Succeeded`, output filtered on
      `warning|error` matched nothing, confirmed in
      `%LOCALAPPDATA%\UnrealBuildTool\Log.txt`.
      **Game target: `ExitCode=0` from `BuildCookRun`; warning count NOT separately
      verified.** The original wording claimed zero warnings for both targets on the
      strength of an exit code, which `BuildCookRun` returns with warnings present.
      Narrowed to what was measured after `code-reviewer` flagged it — the same shape as
      the false Gate G "zero matches" claim corrected earlier this session. Filtered
      Game-target warning output is owed at `TEST-001`.
- [x] **Verification that the test module does not ship** — see the evidence block below.

### CORE-001 — verification evidence, 2026-08-12

**Two real defects were found by building, neither visible by inspection:**

1. `fatal error C1083: Cannot open include file: 'Core/RacingSimLog.h'`.
   `DefaultBuildSettings = V7` sets `bLegacyPublicIncludePaths = false`, so UBT does not
   put the module root on the include path — only a `Public/` folder, which this module
   deliberately does not have. `RacingSim.h` had resolved only because it sits at the
   module root. Fixed with `PublicIncludePaths.Add(ModuleDirectory)` rather than
   restructuring into `Public/`+`Private/`, which preserves the agreed flat layer layout.
2. `LNK2001: unresolved external symbol "LogRacingCore"` ×5, in `RacingSimTests`.
   `DECLARE_LOG_CATEGORY_EXTERN` emits a plain `extern`, which does not cross a DLL
   boundary. Fixed with `RACINGSIM_API`, matching
   `CORE_API DECLARE_LOG_CATEGORY_EXTERN(LogHAL, Log, All)` in `CoreGlobals.h`.
   **This link error is the module split proving itself** — it is compile-time evidence
   that `RacingSimTests` is a genuinely separate binary.

**Test:** `RacingSim.Core.LogCategories` — 1 succeeded, 0 failed, 0 notRun.
Report `Saved/Automation/CoreReport/index.json`.

**Artifact check.** A Development Game target is **monolithic**, so modules link into
`RacingSim.exe` rather than shipping as DLLs. Manifest absence alone therefore proves
nothing, and the binary itself was searched in both ASCII and UTF-16. The check carries
its own positive control: the five runtime categories are found, so a null result means
absence rather than a broken search.

| symbol | in `RacingSim.exe` | expected |
|---|---|---|
| `RacingSim.Core.LogCategories` | absent | absent |
| `FRacingSimLogCategoriesTest` | absent | absent |
| `RacingSimTests` | absent | absent |
| `LogRacingTests` | absent | absent |
| `LogRacingCore` … `LogRacingStreaming` | **present** (UTF-16) | present |

`RacingSimTests` is also absent from all three staging manifests. `UncookedOnly` holds
even in a monolithic target.

**Path precision, per `code-reviewer` N-4.** Two files are named `RacingSim.exe`.
`Packaged/Windows/RacingSim.exe` (171,520 bytes) is the **bootstrap launcher** and
contains none of the needles — including the positive controls, so a check run there
returns all-absent and looks like a pass. The binary that matters is
`Packaged/Windows/RacingSim/Binaries/Win64/RacingSim.exe` (354,528,256 bytes). Always
cite the full path, and always keep the positive control.

**Stronger gate available, adopt at `TEST-001`:** `Binaries/Win64/RacingSim.target`
contains **0** occurrences of `RacingSimTests` while `RacingSimEditor.target` contains
**2**. That is UnrealBuildTool stating what it compiled, which beats string presence.

**Mechanism, verified in engine source rather than assumed.**
`ModuleDescriptor.cs:792-793` — `case ModuleHostType.UncookedOnly: return
!bBuildRequiresCookedData;` and `TargetRules.cs:1190-1195` —
`bBuildRequiresCookedData => bBuildRequiresCookedDataOverride ?? (Type == Game ||
Client || Server)`. The exclusion keys off `bBuildRequiresCookedData`, **not**
`TargetType` directly, and that is a settable override. So the guarantee rests on two
one-line invariants that nothing enforces: nobody sets
`bBuildRequiresCookedDataOverride = false` on a Game target, and nobody adds
`RacingSimTests` to `RacingSim.Target.cs`. Both hold today.

### CORE-001 — review findings assigned to later tickets

Raised by `code-reviewer` at `3bbd9ca`. Assignment is required before CORE-001 closes;
closure is not.

| # | Finding | Ticket |
|---|---|---|
| N-1 | `Source/RacingSim/Core/RacingSimLog.h:16-18` — the logging-policy comment is **wrong**. It names the second macro parameter as the compile-time strip; the strip tests the **third** (`CompileTimeVerbosity`), which is `All` here, so **nothing is compiled out in any configuration**. Only `NO_LOGGING` removes these, and it would strip `Log` too. Set `CompileTimeVerbosity` deliberately and correct the comment | `CORE-002` |
| N-2 | "test code physically cannot ship" is true only of code **in that module**. `WITH_DEV_AUTOMATION_TESTS` is 1 in a Development Game target, so an `IMPLEMENT_SIMPLE_AUTOMATION_TEST` written inside `Source/RacingSim/` compiles into the shipped exe and the CORE-001 gate would not see it. Add a rule that automation tests live only in `RacingSimTests` | `TEST-001` |
| N-3 | "splitting into modules later is a mechanical change" is true **only until the first `UObject` exists**. UObject paths are `/Script/<Module>.<Class>`, so moving a UClass breaks every Blueprint, DataAsset and map reference without authored `CoreRedirects`. Record that cost and make the granularity decision final before the first UObject ships | `CORE-002` |
| N-4 | Adopt the `.target` receipt check as the primary non-shipping gate; keep the string search as corroboration. Also cover **test content** — `Content/Tests/Maps/` cooks into the pak, which neither current check inspects. Needs `DirectoriesToNeverCook` plus a pak-side check | `TEST-001` |
| N-6 | `Docs/15-ProjectStructure.md` test-module tree omits `Core/`, which the implementation added | `CORE-002` |
| N-7 | `.gitignore` — the `Samples/` comment block visually captures unrelated `Archive/` and `StagedBuilds/` entries | `CORE-002` |

**Reviewer's assessment of the test itself, recorded rather than argued away.** The
genuine guarantee is that `RacingSimTests` links against and loads the runtime module
and that exported categories are reachable across a DLL boundary — which is exactly what
the `LNK2001` episode was about. But the count, distinctness and exact-name assertions
**cannot fail**: `LogCategory.h:123` derives the category name from the identifier via
`TEXT(#CategoryName)`, so a duplicate name is a duplicate-symbol link error rather than a
silent merge, and a rename is already a compile error in the spec file. The claim that
the test catches "distinctness and non-collision" overstates it. Not blocking — CORE-001
delivers no behaviour — but `TEST-001` should add assertions that can actually fail,
starting with whether the categories are registered with the log suppression system.

**Known consequence, accepted deliberately.** Boundaries between the five gameplay
layers are **not compile-enforced** under this layout. Nothing prevents `Streaming/`
from including a `Race/` header, and `CLAUDE.md` requires that no race truth lives in
`Streaming`. That rule is enforced by review here, not by the linker. If it is violated
in practice, promote the five layers to real modules — a mechanical change.

**Packaging note.** BLOCKER-006 was resolved in the same commit, so the package is
produced with the clean-stage path and `-stagingdirectory` outside `Documents\`.
`-nocleanstage` is no longer used and the stale-archive risk it carried is gone.

Nothing in the current stub obstructs this. `Source/RacingSim/RacingSim.Build.cs:14-23`
is minimal and correct, and both targets have current receipts.

`CORE-002` must define the units policy (Unreal centimetres, documented SI
conversions) and the build-ID scheme that `Docs/15-ProjectStructure.md` requires on
every competitive result. This closes the `Build ID/versioning method: UNVERIFIED`
field in `Docs/Environment.md`.

**Deferred review findings CORE-001 must also close — ALL FOUR CLOSED 2026-08-12.**

Raised by `code-reviewer` against the Phase 0 shell on 2026-08-10, deferred as
over-exclusions on a stub. A second `code-reviewer` pass at `3bbd9ca` correctly blocked
CORE-001 for leaving this block open while the status line read "all 8 criteria
evidenced" — the checkboxes were done, the ticket as written was not.

Closure verified by `git check-ignore` with both controls, so the test discriminates:

- **still ignored** (6/6): `Binaries/Win64/UnrealEditor-RacingSim.pdb`, the matching
  `.lib`, `Build/Windows/FileOpenOrder/CookerOpenOrder.log`, `Saved/Logs/*.log`,
  `Packaged/Windows/RacingSim.exe`, root `CMakeLists.txt`
- **now trackable** (6/6, previously dropped): `Build/Windows/Resources/Icon.ico`,
  `Build/Windows/Application.manifest`, `Build/Windows/PakBlacklist-Shipping.txt`,
  `Source/ThirdParty/Foo/x64/foo.lib`, `Source/ThirdParty/Foo/CMakeLists.txt`,
  `Source/ThirdParty/Foo/Makefile`

The original findings, retained for the record:

- `.gitignore:3` — bare `Build/` ignores the whole Unreal `Build/` tree at any
  depth, which will silently drop files that must be tracked later:
  `Build/Windows/Resources/*.ico`, `Application.manifest`, `Build/*/PakBlacklist*.txt`.
  Switch to Epic's pattern: `Build/*` plus `!Build/*/` and explicit un-ignores.
- `.gitignore:37` — `*.lib` and `*.pdb` are ignored at any depth while
  `.gitattributes` LFS-tracks `*.dll`/`*.so`. A future
  `Source/ThirdParty/**/x64/*.lib` would be dropped while its sibling DLL commits.
  Anchor the artifact ignores to generated locations.
- `.gitignore:27-29` — `CMakeLists.txt`, `Makefile`, `compile_commands.json`
  ignored at any depth; would drop a legitimate vendored third-party
  `CMakeLists.txt`. Anchor to repo root.
- `Config/DefaultGame.ini` — the `bShould*` / `bOnlyCookProductionAssets` values
  restate engine defaults (`AssetManagerSettings.h:73-76`), adding drift risk with
  no behavioural change. Already removed in Phase 0; do not reintroduce.

---

## Epic 2 — vehicle graybox

| ID | Title | Owner | Depends on | Gate | Status |
|---|---|---|---|---|---|
| VEH-001 | Keyboard/gamepad input mappings | vehicle-physics-engineer | CORE-001 | B | OPEN |
| VEH-002 | Prototype chassis/wheels/collision, Chaos baseline | vehicle-physics-engineer | VEH-001 | C | OPEN |
| VEH-003 | Engine/transmission/diff/brakes/steering/suspension tune data | vehicle-physics-engineer | VEH-002, CORE-003 | C | OPEN |
| VEH-004 | Telemetry and failure detection | vehicle-physics-engineer | VEH-002 | C | OPEN |
| VEH-005 | Camera and safe reset | vehicle-physics-engineer | VEH-002 | B, C | OPEN |
| VEH-006 | Recorded manoeuvre tests and 30-minute soak | test-engineer + implementer | VEH-003..005 | C | OPEN |

Chaos Vehicles is mandatory (hard constraint #2). No Unity-style WheelCollider
architecture. Tunables live in typed DataAssets, never as magic numbers in `Tick`.

`VEH-004` must detect NaN, infinity, explosive energy, persistent penetration and
unbounded wheel state — Gate C treats these as test failures, not warnings.

---

## Epic 3 — track and race

| ID | Title | Owner | Depends on | Gate | Status |
|---|---|---|---|---|---|
| TRACK-001 | Original circuit graybox and spline centerline | race-systems-engineer | CORE-001 | B | OPEN |
| TRACK-002 | Ordered checkpoint gates and crossing direction | race-systems-engineer | TRACK-001 | B | OPEN |
| RACE-001 | Race state machine and monotonic clock | race-systems-engineer | CORE-002 | B | OPEN |
| RACE-002 | Lap/sector/progress/validity logic | race-systems-engineer | TRACK-002, RACE-001 | B | OPEN |
| RACE-003 | Results, restart, metadata | race-systems-engineer | RACE-002 | B | OPEN |
| RACE-004 | Shortcut/reverse/double-trigger/reset automation matrix | test-engineer + implementer | RACE-003 | B | OPEN |

Gate B is unusually explicit and these tickets inherit it verbatim: 100 automated
valid laps count exactly once; 100 skipped/out-of-order/reverse/double-cross
scenarios never produce a valid lap; the timer is monotonic and independent of
render frame rate; reset can never award progress or duplicate a checkpoint.

The circuit must be **original**. No real track name, layout, signage or venue.

---

## Epic 4 — HUD

| ID | Title | Owner | Depends on | Gate | Status |
|---|---|---|---|---|---|
| UI-001 | HUD view model and data contract | race-systems-engineer | RACE-003 | B | OPEN |
| UI-002 | Speed/RPM/gear/lap/time/delta/countdown/results | race-systems-engineer | UI-001 | B | OPEN |
| UI-003 | Input prompts, settings, restart flow, accessibility baseline | race-systems-engineer | UI-002 | B | OPEN |
| UI-004 | HUD functional and screenshot tests | test-engineer + implementer | UI-003 | B, D | OPEN |

HUD reads authoritative race state; it never computes race truth. Position is shown
only when opponents exist. Text must stay readable at the lowest supported stream
tier — a Pixel Streaming constraint, not a normal UI one.

---

## Epic 5 — Pixel Streaming

| ID | Title | Owner | Depends on | Gate | Status |
|---|---|---|---|---|---|
| STREAM-001 | Local packaged PS2 connection | pixel-streaming-engineer | ENV-004, UI-002 | F | OPEN |
| STREAM-002 | Browser frontend shell, versioned custom messages | pixel-streaming-engineer | STREAM-001 | F, G | OPEN |
| STREAM-003 | Gamepad/focus/reconnect tests | test-engineer + implementer | STREAM-002 | F | OPEN |
| STREAM-004 | External STUN/TURN test | pixel-streaming-engineer | STREAM-002 | F | **BLOCKED** — BLOCKER-001 |
| STREAM-005 | Session broker/worker lifecycle spike | pixel-streaming-engineer | STREAM-002 | G | **BLOCKED** — BLOCKER-001 |
| STREAM-006 | WebRTC telemetry and latency measurement | performance-engineer | STREAM-002 | E, F | **BLOCKED** — BLOCKER-001 |

`STREAM-001` through `STREAM-003` are provable locally: connection, input routing,
reconnect, session telemetry. **Latency and quality targets are not** — Gate F
thresholds are regional measurements requiring the reference worker (ADR-0003).

Custom messages are versioned, size-limited, validated, authenticated by session
context, and never authoritative over race state.

---

## Epic 6 — visual vertical slice

**Entire epic BLOCKED on BLOCKER-001** (no reference GPU worker). Per ADR-0003,
starting hero-quality art without a measurement baseline risks producing content
that cannot be validated and may need rebuilding.

`ART-001` hero car source and provenance · `ART-002` car material family and
turntable benchmark · `ART-003` hero circuit sector and environment materials ·
`ART-004` lighting/post/camera benchmark · `ART-005` audio vertical slice ·
`PERF-001` reference trace, budgets, optimization · `VIS-001` screenshot baseline
and human art gate.

Owners: `rendering-tech-artist`, with `performance-engineer` at PERF-001 and
`ip-compliance-auditor` gating ART-001. Gates D, E, H.

`ART-001` cannot start until the generative-AI provenance question in
`Docs/13-AssetLicenseLedger.md` is answered by the legal owner.

---

## Epic 7 — hardening and deployment

**Blocked on BLOCKER-001.** `OPS-001` worker image · `OPS-002` auth, rate limit,
timeout, cost guardrails · `OPS-003` metrics/logs/crashes/alerts · `OPS-004`
100-cycle session test · `SEC-001` exposure and secret audit · `REL-001` full
A–H gate report and rollback drill.

`SEC-001` must confirm Unreal MCP is unreachable on production workers, and should
also account for `UbaServer` binding `0.0.0.0:1345` during builds (NOTE-001) —
build-time only, but it belongs in the exposure audit.

---

## Milestone gates

| Milestone | Contents | Gates | Human approval |
|---|---|---|---|
| M0 Phase 0 | Epic 0 | A (partial) | **Required before Epic 1** |
| M1 Graybox | Epics 1–4 | A, B, C | Required |
| M2 Local streaming | Epic 5 (001–003) | F (partial) | Required |
| M3 Visual slice | Epic 6 | D, E | Required — art director |
| M4 Release readiness | Epic 7 | A–H | Required — legal + ops + art |

`production-ready` may not appear in any report before M4, per
`Docs/07-QualityGates.md`.

## Critical path

`ENV-004` → `CORE-001` → `CORE-002` → `RACE-001` → `TRACK-001` → `TRACK-002` →
`RACE-002` → `RACE-003` → `UI-001` → `UI-002` → `STREAM-001`

Vehicle work (Epic 2) parallelises with track work (Epic 3) after `CORE-002`,
provided the two owners do not touch the same content assets — which, given
BLOCKER-002, is a process guarantee rather than a tooling one.
