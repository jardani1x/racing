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
| CORE-001 | Module/folder structure and logging categories | director | ENV-004 | A | **DONE** 2026-08-12 — `code-reviewer` approved at `9a7d5d4` after two blockers were closed. Ticket-level DONE only; **not** M0 sign-off |
| CORE-002 | Settings, build ID, units, telemetry contracts | race-systems-engineer | CORE-001 | A, B | **DONE** 2026-08-13 — `code-reviewer` approved across two passes at `f84300c`; `test-engineer` independently confirmed both targets build clean and 432/432 automation Smoke tests pass. Merged to `main` at `358848b`. Two MEDIUM findings (build-ID authority/sanitisation edge cases) tracked forward into `CORE-003` |
| TEST-001 | Test module and first smoke test | race-systems-engineer (impl) + test-engineer (validation) | CORE-001 | A | **DONE** 2026-08-18 — `code-reviewer` approved across two passes; `test-engineer` independently confirmed both targets build clean and 445/445 automation Smoke tests pass, plus ran the packaging script directly. Merged to `main` at `5506dcf`. Closes CORE-001's deferred N-2/N-4 findings. One follow-up (no freshness guard on the `.rsp` receipt check) tracked forward into `TRACK-002`/`RACE-002` |
| CORE-003 | DataAsset validation framework | race-systems-engineer | CORE-002 | A | **DONE** 2026-08-18 — `code-reviewer` approved across three passes (2 repair cycles); `test-engineer` independently confirmed both targets build clean from forced real recompilation and 445/445 automation Smoke tests pass. Merged to `main` at `7596b9e`. Closes CORE-002's MEDIUM-1/MEDIUM-2 findings. Seven findings (C3-1..C3-7) tracked forward into `RACE-002` |

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

> **Correction to N-4, made at `TEST-001` repair cycle 1 (finding `T-1`).** The finding's
> own prescription — "adopt the `.target` receipt check as the primary non-shipping
> gate" — **cannot work for the Game target, and the first implementation of `TEST-001`
> inherited that error.** A Development Game target is monolithic, so its `.target`
> receipt lists build products, not modules. Measured on this tree,
> `Binaries/Win64/RacingSim.target` contains **0** occurrences of `RacingSimTests` — and
> **0** of `InputCore`, `CoreUObject` and `SlateCore`, which are all certainly linked
> into that executable. The receipt reads 0 for every module, so it cannot distinguish
> "not linked" from "this file never names modules", and it would still have read 0 with
> `RacingSimTests` compiled in. It therefore caught **neither** of the two regressions
> CORE-001 named (adding the module to `RacingSim.Target.cs`, or setting
> `bBuildRequiresCookedDataOverride = false`).
>
> The gate now reads the Game target's **linker response file**,
> `Intermediate/Build/Win64/x64/RacingSim/Development/RacingSim.exe.rsp`, which names
> every module actually linked into the monolithic executable (1122 object inputs, ~500
> distinct modules). The Editor `.target` receipt is retained as the positive control —
> that half was never broken, because the Editor target is modular. Proven by a
> negative-control probe below.
| N-6 | `Docs/15-ProjectStructure.md` test-module tree omits `Core/`, which the implementation added | `CORE-002` |
| N-7 | `.gitignore` — the `Samples/` comment block visually captures unrelated `Archive/` and `StagedBuilds/` entries | `CORE-002` |
| NEW-1 | **Fixed immediately, not deferred.** The B-1 anchoring left `*.obj` unanchored one line above `*.lib`/`*.pdb`. `.obj` is both an MSVC object file and Wavefront OBJ, so `Source/Art/**/*.obj` and `Content/Raw/**/*.obj` were silently dropped — an original source mesh that never commits is an asset whose provenance cannot be demonstrated, while the author's clone looks fine. Anchored to `Intermediate/**` and `Binaries/**`; verified trackable at three source paths and still ignored at three build paths | closed in `CORE-001` |
| NEW-2 | Six anchored `*.pdb`/`*.lib` patterns cannot match — their parent directories are excluded wholesale and git does not descend into an excluded directory. Harmless, but a reader would think them load-bearing. Commented as belt-and-braces rather than deleted | closed in `CORE-001` |

**Reviewer's counterargument to approval, recorded because it is the real residual
risk.** The non-shipping guarantee is the ticket's most consequential deliverable, and
it is verified only for a Development Game target on one machine, resting on two
unenforced one-line invariants. If someone sets `bBuildRequiresCookedDataOverride =
false` or adds `RacingSimTests` to `RacingSim.Target.cs`, **no build fails and no test
fails.** Approval was given because the mechanism is verified in engine source and the
`.target` receipt corroborates it — but **`TEST-001` must add the automated check, or
this decays into a one-time manual result.**

**Decision owed at `CORE-002`, not inherited by default:** whether to keep
`PublicIncludePaths.Add(ModuleDirectory)` or restructure to `Public/`+`Private/`. The
reviewer would have chosen the split — the current fix publishes the whole module tree
to every dependent, leaving no way to mark a header private later without the
restructure that was avoided. Reversible at near-zero cost today; the cost rises with
every header, and `CORE-002` adds headers.

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

### CORE-002 — acceptance criteria, opened 2026-08-12

Scope per this row: `Settings, build ID, units, telemetry contracts`. Owner
`race-systems-engineer`. Gates A, B. Depends on `CORE-001` (DONE).

**Include-layout decision (project owner, 2026-08-12):** keep
`PublicIncludePaths.Add(ModuleDirectory)` from `CORE-001`. Do **not** restructure to
`Public/`+`Private/` in this ticket.

- [x] `Core/RacingSimSettings.h/.cpp` — `URacingSimSettings` (`UDeveloperSettings`,
      `config="Game"`), editable in Project Settings. This is the module's **first
      UObject**, so it is also the moment `N-3` (module-granularity finality) is tested
      for real rather than in the abstract.
- [x] Build ID scheme defined as a data contract (not populated with real track/car
      values yet — those land with `TRACK-001`/`VEH-003`) covering every field
      `Docs/15-ProjectStructure.md` "Versioning" section requires: game build ID, engine
      patch, track definition/version hash, car spec/tune version, physics policy
      version, assist preset, input type, validity/penalty state.
- [x] Units policy: Unreal-internal values stay centimetres; SI conversion constants/
      functions (cm↔m, cm/s↔km/h or m/s as needed) live in `Core/` with unit tests.
      Every conversion site documents cm vs SI in a comment per `CLAUDE.md`.
- [x] `Core/RacingTelemetry.h/.cpp` — shared telemetry data-contract structs consumed by
      `Vehicle`/`Race`/`UI`/`Streaming`. Contracts only, no gameplay logic, matching
      `Core`'s architecture role (`CLAUDE.md` Architecture boundaries).
- [x] `N-1` closed: `RacingSimLog.h`'s `CompileTimeVerbosity` third parameter and its
      comment are corrected to match the mechanism actually verified in
      `LogMacros.h`/`LogVerbosity.h` — not asserted from memory. Current code claims
      Verbose/VeryVerbose compile out of Shipping; the parameter as written (`All`)
      does not do that.
- [x] `N-3` closed: record here that the two-module decision
      (`Docs/15-ProjectStructure.md`, 2026-08-12) is now exercised by a real UObject and
      stands as final — no further deferral.
- [x] `N-6` closed: `Docs/15-ProjectStructure.md`'s `RacingSimTests/` tree adds `Core/`
      (already present on disk at `Source/RacingSimTests/Core/RacingSimLogSpec.cpp`).
- [x] `N-7` closed: `.gitignore` re-scoped so the `Samples/PixelStreaming2/WebServers/`
      rationale comment (lines 80-84) no longer visually reads as covering the unrelated
      `Archive/`/`StagedBuilds/` lines beneath it.
- [x] Editor **and** Game targets build with zero new warnings — `Result: Succeeded`
      plus filtered `warning|error` output inspected for both, closing the Game-target
      gap CORE-001 left open.
- [x] `RacingSimTests` gains automation coverage for unit-conversion correctness and
      settings default values.
- [x] `Docs/Environment.md`'s `Build ID/versioning method: UNVERIFIED` field is closed
      with the implemented scheme.

### CORE-002 — verification evidence, 2026-08-13

- Editor (`RacingSimEditor Win64 Development`): `Result: Succeeded`, 0 `warning|error`
  matches in the filtered UBT log.
- Game (`RacingSim Win64 Development`): `Result: Succeeded`, 0 `warning|error` matches
  in the filtered UBT log.
- Automation `Smoke` filter, this worktree: `Saved/Automation/Report/index.json` —
  `succeeded: 432, failed: 0, notRun: 0`.
- `code-reviewer` pass 1: 2 HIGH, 8 MEDIUM, 7 LOW. Verdict "changes requested, no
  blockers." Independently verified the build/test evidence above rather than trusting
  the report.

### CORE-002 — review findings, pass 1

| ID | Finding | Disposition |
| --- | --- | --- |
| H-1 | `IsPublishable()` did not check `bIsAuthoritative`; a complete-but-Derived stamp would pass | Fixed — `IsPublishable()` now rejects a non-authoritative `GameBuildId`; test added (`RacingSimVersionSpec.cpp`) |
| H-2 | `SanitiseComponent` can silently mutate a stamped Explicit ID (e.g. `"1.4.0+4417"`, `"feature/x"`), losing traceability while still marked authoritative | Fixed — `Current()` now warns with both raw and sanitised values when they differ; still authoritative (CI's responsibility to supply a clean stamp, now surfaced loudly); tests added for `+` and `/` cases |
| M-1 | `RacingSimSettings.h` comment falsely claimed `-`/`+` are stripped from a derived ID | Fixed — comment corrected; derived ID declared opaque/never-parsed; hyphenated-channel test added |
| M-2 | `TelemetryStaleAfterSeconds = 0` disables staleness checking on a comment citing a convention documented on a different field | Fixed — "0 disables" documented on the field itself in `RacingSimSettings.h`, with a warning against leaving it there in a built config |
| M-3 | `USTRUCT(BlueprintType)` contracts (telemetry conversions, `ToString()`, `IsStaleAt`, etc.) have no `UFUNCTION`-callable equivalents, so Blueprint/UMG cannot actually reach them despite the HUD being UMG per `CLAUDE.md` | **Deferred to `UI-001`** — needs a `URacingTelemetryFunctionLibrary` with `BlueprintPure` wrappers; out of scope for a Core-only contract ticket |
| M-4 | `FRacingTelemetryFrame` embeds `TArray`-bearing structs, so copying "the one frame the HUD may read" heap-allocates; `CLAUDE.md` forbids per-frame allocation | **Deferred to `UI-001`** alongside M-3 — needs either a documented pass-by-`const&` contract or a `TInlineAllocator` |
| M-5 | `ClampMin`/`ClampMax` metadata is not enforced on `-ini:` config overrides or on `BlueprintReadOnly`-only telemetry fields | **Deferred to `CORE-003`** (DataAsset/config validation framework) — the natural home for a `PostInitProperties` range-clamp pass |
| M-6 | `Docs/15-ProjectStructure.md` still said "splitting later is a mechanical change" after `N-3` should have closed that claim | Fixed — finality recorded in the document itself, not only in a C++ comment |
| M-7 | Game-target build evidence predated the final source revision | Fixed — both targets rebuilt at the final revision (see verification evidence above) |
| M-8 | `Docs/Environment.md`'s build-ID description was inaccurate (claimed 2 components from `FEngineVersion`/`FApp`; actually 5, one of which reads `GConfig`) | Fixed — description rewritten to the actual 5-component format with the `GetProjectVersion()` `0.0.0`-fallback caveat noted |
| L-1..L-5, L-7 | Comment/trait/NaN/log-cadence nits | **Batched forward** — reviewer confirmed not required for re-review |
| L-6 | This acceptance-criteria block existed only on `main`, uncommitted, absent from this branch | Fixed by this edit |

### CORE-002 — review findings, pass 2 (re-review)

Verdict: **approved for merge**, conditional on the three items below — no further review
cycle required once applied. Confirmed the M-3/M-4/M-5 deferrals to `UI-001`/`CORE-003`
(M-4 partially — see below). Independently re-verified all pass-1 fixes against the
post-fix worktree, including a rebuild at the exact final revision.

| ID | Finding | Disposition |
| --- | --- | --- |
| MEDIUM-1 | H-2's fix still marks a sanitisation-mutated Explicit ID `bIsAuthoritative = true`, despite the fix's own comment stating this breaks CI traceability and uniqueness | **Batched forward to `CORE-003`** (see below) — no production caller of `IsPublishable` exists yet; tracked rather than fixed blind |
| MEDIUM-2 | `SanitiseComponent` rejects `+`, but the Derived-scheme composer embeds a literal `+` itself (engine changelist separator) — inconsistent, and the H-2 warning fires on every standard semver-style CI stamp (`1.4.0+4417`) | **Batched forward to `CORE-003`** alongside MEDIUM-1 |
| MEDIUM-3 | The two H-2 tests registered the identical `AddExpectedMessagePlain` pattern string; `TSet`-keyed dedup makes the second registration silently replace the first, so the test passes even if only one case actually warns | **Fixed** — patterns now include the raw stamped value (`"...stamped \"1.4.0+4417\""` / `"...stamped \"feature/x\""`), making them distinct and provably tied to the right input |
| MEDIUM-4 | M-3/M-4/M-5 dispositions lived only in this ticket's closed history, not in the receiving tickets' own bodies | **Fixed** — see `### CORE-003 — findings inherited from CORE-002` and `### UI-001 — findings inherited from CORE-002` below |
| MEDIUM-5 | `RacingSimLog.h`'s Shipping/Test `CompileTimeVerbosity = Log` branch has no compile evidence (only Development built) | **Batched forward** — engine's own `static_assert` (`LogCategory.h:121-122`) is satisfied by inspection; no Shipping/Test config exists yet for this project |
| LOW-1..LOW-3 | Doc-comment/asymmetry nits | **Batched forward** |
| LOW-4 | Entire CORE-002 change set was uncommitted at review time — no revision to pin evidence to | **Fixed** — committed before dispatching `test-engineer` (see commit referenced in the completion report) |

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

### TEST-001 — acceptance criteria, opened 2026-08-17

Scope per this row: `Test module and first smoke test`. Gate A. Depends on `CORE-001`
(DONE). **Ownership note:** the table's `test-engineer + implementer` owner reuses a
pattern that also appears on `VEH-006`/`RACE-004`/`UI-004`/`STREAM-003` — `test-engineer`
is read-only validation per `CLAUDE.md` and cannot write code. Implementation here is
`race-systems-engineer` (continuity with `CORE-001`/`CORE-002`); `test-engineer` runs the
standard validation gate afterward, same as every other ticket.

`CORE-001` already delivered a `RacingSimTests` module and a first spec
(`RacingSim.Core.LogCategories`, 1/1 passing) — the literal title is technically met.
This ticket exists to close what `CORE-001`'s own review left open (`N-2`, `N-4`) rather
than to build the module from scratch: it converts a one-time manual verification into
something the project asserts automatically, every build.

- [x] `N-2` closed: a written rule (`Docs/01-Architecture.md` or equivalent) that
      `IMPLEMENT_SIMPLE_AUTOMATION_TEST`/spec files may live only under
      `Source/RacingSimTests/`, plus a check that fails the build or a test if an
      automation-test macro appears inside `Source/RacingSim/`.
- [x] `N-4` (receipt check) closed: the `.target`-file check demonstrated manually at
      `CORE-001` (`Binaries/Win64/RacingSim.target` contains 0 occurrences of
      `RacingSimTests`; `RacingSimEditor.target` contains it) is automated — runnable
      from a script or test, not re-typed by hand at every future ticket.
- [x] `N-4` (test content) closed: any test-only content under `Content/Tests/` is
      excluded from a packaged Game build via `DirectoriesToNeverCook` (or equivalent),
      verified by a pak-side check — `CORE-001`'s binary-search method covered code, not
      cooked content, and nothing today checks the latter.
      **Closed with a stated limit:** the mechanism, the package and the check are all
      real, but `/Game/Tests` holds no cooked asset yet, so the check cannot currently
      fail on this criterion's actual subject. See *The one criterion that is closed but
      not yet proven* below.
- [x] The non-shipping guarantee gains an assertion that can actually fail. Per
      `CORE-001`'s own reviewer note, `RacingSim.Core.LogCategories` cannot fail on a
      duplicate or rename — that is already a link/compile error, not a test outcome.
      Add coverage that is meaningfully falsifiable, e.g. that every declared category is
      reachable through the log-suppression system at runtime.
- [x] Editor **and** Game targets build with zero new warnings.
- [x] `Docs/15-ProjectStructure.md`'s test-module description matches the final tree and
      names the enforcement mechanism added here.

### TEST-001 — verification evidence, 2026-08-17

Implemented on branch `worktree-agent-a489f0e1e76ec91e0`. **Not merged.** `code-reviewer`
and `test-engineer` gates have not run; nothing below is a review approval.

**Worktree note.** The director's brief named the worktree `test-001-retry`. The agent
harness isolates this agent to `.claude/worktrees/agent-a489f0e1e76ec91e0` and refuses git
operations against any other worktree, so the work is on the branch above, cut from the
same base commit `340c9ea`. Not a scope decision — a sandbox constraint, recorded so the
merge targets the right branch.

#### Files changed

| File | Change |
|---|---|
| `Source/RacingSim/RacingSim.Build.cs` | `EnforceNoAutomationTestsInRuntimeModule()` — build-time source scan (N-2) |
| `Source/RacingSimTests/Tests/AutomationTestPlacementSpec.cpp` | new — `RacingSim.Tests.AutomationTestPlacement` (N-2, runtime layer) |
| `Source/RacingSimTests/Tests/NonShippingArtifactSpec.cpp` | new — `RacingSim.Tests.NonShippingArtifacts` (N-4, inside the Smoke gate) |
| `Source/RacingSimTests/Core/RacingSimLogSuppressionSpec.cpp` | new — `RacingSim.Core.LogCategoryRegistration` (falsifiable log coverage) |
| `Scripts/Test/Check-NonShippingArtifacts.ps1` | new — receipt / config / pak+binary checks, `-Mode Receipt|Config|Pak|All` |
| `Config/DefaultGame.ini` | `[/Script/UnrealEd.ProjectPackagingSettings]` `DirectoriesToNeverCook` for `/Game/Tests`, `/Game/Developer` |
| `Content/Tests/README.md`, `Content/Tests/Maps/.gitkeep` | new — the excluded directory, and what it is for |
| `Docs/01-Architecture.md` | the written N-2 rule and the three-layer enforcement table |
| `Docs/15-ProjectStructure.md` | test-module description, real file tree, enforcement mechanism; corrects the "test code physically cannot ship" overclaim |

#### Builds — both targets, zero warnings

Command form is the verified one in `Docs/Environment.md` under *Compile editor target*,
with the target name substituted for the Game build.

| Target | Result | `warning|error` matches in that command's own stdout |
|---|---|---|
| `RacingSimEditor Win64 Development` | `Result: Succeeded`, 607.99 s (from scratch, no makefile) | **0** |
| `RacingSim Win64 Development` | `Result: Succeeded`, 232.47 s (from scratch) | **0** |
| `RacingSimEditor Win64 Development` (final, after the probe revert) | `Result: Succeeded`, 44.55 s | **0** |
| `RacingSim Win64 Development` (final, after the probe revert) | `Result: Succeeded`, 58.69 s | **0** |

**Warning counts are taken from each command's own captured stdout, not from
`%LOCALAPPDATA%\UnrealBuildTool\Log.txt`.** That file is machine-global and was observed
mid-session containing a *different* worktree's build
(`agent-aa457d3da1306fb55`, `Log started at 08/17/2026 14:12:51`) while this ticket's
builds were running. It is not usable as per-build evidence while agents build
concurrently, and any future ticket citing it should check whose build it holds first.

#### Package

Verified `BuildCookRun` from `Docs/Environment.md`, staging outside `Documents\` per
BLOCKER-006 (`%LOCALAPPDATA%\RacingSimStage-a489f0e1`):

```text
LogCook: Display: Done!
LogPakFile: Display: UnrealPak executed in 4.048256 / 1.913048 / 4.285396 seconds
BUILD SUCCESSFUL
AutomationTool exiting with ExitCode=0 (Success)
```

#### Automation

`Automation RunFilter Smoke`, exactly the recorded command, report at
`Saved/Automation/Report/index.json`, `reportCreatedOn 2026.08.17-06.45.43`:

**succeeded = 445, succeededWithWarnings = 0, failed = 0, notRun = 0**, 8.81 s.

Baseline at `RACE-001` was 442. The three added tests account for the delta exactly:
`RacingSim.Core.LogCategoryRegistration`, `RacingSim.Tests.AutomationTestPlacement`,
`RacingSim.Tests.NonShippingArtifacts`. All 19 `RacingSim.*` tests report `Success`;
no pre-existing suite regressed.

Filter choice is not incidental — all three carry `SmokeFilter`, so the project's one
documented command discovers them. A test the documented gate cannot see is not coverage.

#### Negative controls — the checks were made to fail on purpose

A gate that has never failed is a gate nobody has tested. Both N-2 layers were driven to
a real failure and then reverted; the working tree is clean and neither probe is committed.

**Probe 1 — new file.** `IMPLEMENT_SIMPLE_AUTOMATION_TEST` in a new
`Source/RacingSim/Core/ZZTempViolationProbe.cpp`:

```text
Invalidating makefile for RacingSimEditor (source file added)
Unable to instantiate module 'RacingSim': RacingSim: automation-test macros are not
permitted in the runtime module.
...
  ...\Source\RacingSim\Core\ZZTempViolationProbe.cpp (around line 4): IMPLEMENT_SIMPLE_AUTOMATION_TEST
Result: Failed (RulesError)      -- 2.34 s
```

**Probe 2 — existing file, which is the one that matters.** The same macro appended to
`Source/RacingSim/Core/RacingSimLog.cpp`. The module's *file list* is unchanged here, so
this is the case a naive scan would miss once UBT cached its makefile:

```text
Invalidating makefile for RacingSimEditor (RacingSimLog.cpp modified)
Unable to instantiate module 'RacingSim': ...
  ...\Source\RacingSim\Core\RacingSimLog.cpp (around line 13): IMPLEMENT_SIMPLE_AUTOMATION_TEST
Result: Failed (RulesError)      -- 3.97 s
```

That first line is the evidence for the design choice: the scan registers every file it
reads in `ModuleRules.ExternalDependencies` (`ModuleRules.cs:1437`, consumed at
`UEBuildTarget.cs:3460`), so an in-file edit invalidates the makefile and re-runs the
check. Without it the check would run once and then silently stop checking — exactly the
decay `CORE-001`'s reviewer predicted for the hand-typed receipt check.

Reported line numbers were approximate (`around line N`) in the version these two probes
ran against, because block-comment stripping was not line-preserving. **Repair cycle 1
(`T-2`) replaced the stripper**, and the replacement preserves both length and newlines,
so the message is now an exact `(line N)`. The probe transcripts above are kept verbatim
as the record of what was run at the time.

**Probe 3 — found by accident, and the most useful result here.** The first version of
the receipt check looked for a `Modules` key in the `.target` JSON. **UE 5.8.1 `.target`
files have no such key** — the top-level keys are `TargetName`, `Platform`,
`Configuration`, `BuildSettingsVersion`, `TargetBuildEnvironment`, `TargetType`,
`IsTestTarget`, `Architecture`, `Project`, `Launch`, `[LaunchCmd]`, `Version`,
`BuildProducts`, `RuntimeDependencies`, `BuildPlugins`, `AdditionalProperties`. The
parser therefore read an empty module list for both receipts — and **the assertion this
ticket is about, "`RacingSimTests` is not among the Game target's modules", would have
passed vacuously on that empty list.**

It did not become a false pass only because the Editor-side positive control asserted a
known-true fact and failed first. That is the argument for every control in this ticket,
and the reason none of them should be tidied away as redundant. It is also the same shape
as the false Gate G "zero matches" claim corrected in `Docs/Environment.md`: a search that
is narrower than the sentence describing it reads as a clean result.

---

### TEST-001 — repair cycle 1, review findings T-1..T-9, 2026-08-18

Branch `worktree-agent-a2fd151063067095a`, cut from `0576ec4`. **Not merged, not pushed.**

**Worktree note, again a sandbox constraint.** The brief named worktree
`agent-a489f0e1e76ec91e0` at `0576ec4`. The harness isolated this agent to
`agent-a2fd151063067095a`, which was sitting at `9208586` and refuses git operations
against another worktree. `0576ec4` was reachable in the shared object store, so the
branch was fast-forwarded onto it and the repair work sits on top. Same code, different
branch name.

| Finding | Severity | Action |
|---|---|---|
| `T-1`/`T-1a`/`T-1b` | HIGH | Game-side data source switched from the `.target` receipt to the linker response file; false claims corrected; negative control run (below) |
| `T-2` | MEDIUM | Regex comment stripper replaced with a position-preserving scanner |
| `T-4` | MEDIUM | Placement spec filters by source path, not test name |
| `T-5` | MEDIUM | Editor-receipt half degrades to a warning like the Game half |
| `T-6` | LOW | `LogRacingTests` now really is asserted separately |
| `T-9` | LOW | `CORE-002 finding N-1` → `CORE-001 finding N-1, fixed at CORE-002` |
| `T-3`, `T-7`, `T-8` | — | Accepted as-is per the review |

#### T-1: why the old check could not fail, measured

`Binaries/Win64/RacingSim.target`, in the **known-good** state, occurrence counts:

```text
RacingSimTests  0        InputCore    0
CoreUObject     0        SlateCore    0
```

`InputCore`, `CoreUObject` and `SlateCore` are certainly linked into `RacingSim.exe`. A
monolithic Game receipt lists build products, not modules, so it reads 0 for *every*
module. The old assertion was measuring monolithic-vs-modular linkage and would have read
0 with the test module compiled in.

`Intermediate/Build/Win64/x64/RacingSim/Development/RacingSim.exe.rsp` does name them:
1122 `.obj` inputs, **all 1122** matching `/Development/<Module>/<file>.obj`, 0 containing
a backslash, yielding 446 distinct modules. The 111 `.lib` inputs are third-party
(`BLAKE3.lib`, `OpenEXR-3_4.lib`, `Secur32.lib`), not UBT modules, so parsing `.obj`
paths is exhaustive for module membership.

#### T-1b: negative control — the fixed check driven to a real failure

`Source/RacingSim.Target.cs` temporarily gained `ExtraModuleNames.Add("RacingSimTests")`,
then `RacingSim Win64 Development` was rebuilt.

**The build itself succeeded with zero warnings** — `Result: Succeeded`, 141.97 s, 0
`warning|error` matches. That is CORE-001's reviewer's "no build fails and no test fails"
reproduced exactly, and it is why a check is needed at all.

Script, `-Mode Receipt` (**exit 1**):

```text
[PASS] Link: response file parsed into a module list (control)
       1133 object inputs -> 447 distinct modules linked into RacingSim.exe
[PASS] Link: known-linked modules are named in the response file (control)
       Found all of: RacingSim, Core, CoreUObject, Engine, InputCore
[PASS] Link: matcher is sound (negative control)
[FAIL] Link: RacingSimTests is NOT linked into RacingSim.exe
       Linked as a module: True; raw occurrences in the response file: 12. The
       UncookedOnly test module reached the Game executable. Check RacingSim.Target.cs
       ExtraModuleNames, and any bBuildRequiresCookedDataOverride on the Game target --
       ModuleDescriptor.cs:792 keys UncookedOnly exclusion off bBuildRequiresCookedData,
       not off TargetType.
       FYI, not an assertion: 'RacingSimTests' occurs 0 time(s) in RacingSim.target.

Summary: 5 passed, 1 failed, 0 skipped
RESULT: FAILED
```

**The single most important line is the `FYI`.** The regression is present — the test
module *is* linked into the executable — and the old check's data source *still reads 0*.
That is the direct demonstration that the `.target` receipt could never have caught this,
and that the `.rsp` does.

The documented automation gate catches it too, which matters because the script is not a
required step and `Automation RunFilter Smoke` is. Same probe, Smoke filter,
`reportCreatedOn 2026.08.18-02.06.06`: **succeeded 444, failed 1, notRun 0**:

```text
RacingSim.Tests.NonShippingArtifacts                 Fail
  [Error] Expected 'RacingSimTests is NOT linked into RacingSim.exe -- the UncookedOnly
          test module was not compiled into the Game target' to be false.
  [Error] Expected ''RacingSimTests' does not appear anywhere in the Game linker
          response file' to be false.
```

Exactly one test failed; the other 18 `RacingSim.*` tests and all engine suites stayed
green, so the check is specific and not merely noisy.

**Reverted and rebuilt clean.** `Source/RacingSim.Target.cs` is byte-identical to its
committed state (`git diff` empty). `RacingSim Win64 Development` — `Result: Succeeded`,
43.42 s, 0 `warning|error` matches. Script back to **6 passed, 0 failed, exit 0**, module
count back from 447 to 446:

```text
[PASS] Link: response file parsed into a module list (control)
       1122 object inputs -> 446 distinct modules linked into RacingSim.exe
[PASS] Link: RacingSimTests is NOT linked into RacingSim.exe
       Absent from 446 linked modules, and 0 raw occurrences anywhere in the response file.
```

#### T-2: the false-negative the old stripper allowed, demonstrated

The old header comment claimed "a false negative is not reachable this way, and that is
the direction that matters". Running the *old* expressions (`/\*.*?\*/` Singleline, block
comments stripped before line comments) over this input:

```cpp
// TODO: the /* form is deprecated, use // instead
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShippedByAccident, "Rogue.Test", Flags)
bool FShippedByAccident::RunTest(const FString& P) { return true; }

/* an ordinary later block comment */
void RealCode() {}
```

strips to — note the macro is **gone** while `RealCode()` survives:

```text
 
#include "Misc/AutomationTest.h"

 
void RealCode() {}
--- banned macro detected: False
```

A `/*` inside a `//` comment opened a match that ran to the next `*/` anywhere later in
the file. One line of prose disables the gate, which makes it a plausible accident rather
than only a deliberate bypass. The replacement is a single left-to-right scanner that
models what the C++ lexer does, also understands string/char/raw-string literals (removing
the old false-*positive* caveat), and preserves length and newlines so reported line
numbers are exact.

#### Check results

> **Superseded by repair cycle 1 for the Receipt block.** The `Receipt:` lines below are
> the *original* implementation's output and are retained only as the record of what was
> reported at `0576ec4`. The `Receipt: RacingSimTests is NOT in RacingSim.target` line is
> the assertion `T-1` found to be structurally incapable of failing — see the repair-cycle
> section above for the replacement and its negative control. The `Config:`, `Pak:` and
> `Binary:` lines are unaffected by `T-1`.

`Scripts/Test/Check-NonShippingArtifacts.ps1 -Mode All` (receipt + config) and
`-Mode Pak` against the package above:

```text
[PASS] Receipt: both receipts parsed (control)            Editor 1330 build products, Game 10
[PASS] Receipt: RacingSimTests IS a build product of RacingSimEditor.target (control)
[PASS] Receipt: RacingSim.target is the Game target and names RacingSim.exe (control)
[PASS] Receipt: RacingSimTests is NOT in RacingSim.target
       CORE-001 recorded these by hand: RacingSim.target 0 occurrence(s),
       RacingSimEditor.target 2. Measured now: 0 and 2.
[PASS] Config: /Game/Tests is in DirectoriesToNeverCook
[PASS] Config: /Game/Developer is in DirectoriesToNeverCook
[PASS] Config: matcher is sound (negative control)

[PASS] Pak: byte search works (control: /Script/Engine found)   5 containers
[PASS] Pak: '/Game/Tests/' absent from packaged containers
[PASS] Pak: '/Game/Developer/' absent from packaged containers
[PASS] Binary: control symbol LogRacingCore found in packaged exe
[PASS] Binary: 'LogRacingTests' absent from packaged exe
[PASS] Binary: 'RacingSim.Core.LogCategories' absent from packaged exe
[PASS] Binary: 'RacingSim.Tests.AutomationTestPlacement' absent from packaged exe
```

The automated numbers reproduce `CORE-001`'s hand-typed ones exactly (0 and 2), which is
the whole point of the ticket: the same fact, now asserted by something that runs.

The binary check searches
`Packaged/Windows/RacingSim/Binaries/Win64/RacingSim.exe` (354,582,016 bytes), not the
171 KB bootstrap launcher at `Packaged/Windows/RacingSim.exe` — the trap `CORE-001`
recorded, where the wrong file returns all-absent including the controls and looks like a
pass. The control (`LogRacingCore` found) is what distinguishes the two.

#### The one criterion that is closed but not yet proven

**`/Game/Tests` contains no cooked asset**, so the pak-side check currently verifies an
exclusion whose subject does not exist. What *is* proven: the setting is present and
resolves through `GConfig` as the cooker reads it; a package was produced; the byte search
works (its positive control found `/Script/Engine`); and `/Game/Tests/` is absent. What is
**not** proven is that `DirectoriesToNeverCook` would actually stop a real cooked asset —
that requires an asset to exist and be referenced.

Creating one would mean authoring a `.uasset`, which needs an `Docs/AssetOwnership.tsv`
claim and serialized binary-asset ownership under hard constraint #7 — out of scope for a
test-infrastructure ticket. Recorded in `Content/Tests/README.md` and
`Docs/15-ProjectStructure.md` as an obligation on the first ticket to add a functional-test
map (`TRACK-002` or `RACE-002`): re-run `-Mode Pak` against a fresh package, because that
is the first run capable of failing.

#### Strongest counter-case to accepting this ticket

**The three-layer enforcement protects the runtime module and nothing else.** Every check
is hard-coded to `Source/RacingSim/`. The instant a fourth module appears — and
`Docs/15-ProjectStructure.md` already anticipates `Plugins/RacingAutomation/Source/`, plus
the layer-promotion path it says to treat as its own ticket — that module is unguarded,
and it is unguarded *silently*: no build fails, no test fails, and this evidence section
still reads green. The failure mode is identical to the one N-2 was raised about, just one
level up. Nothing in this ticket makes adding a module force a decision about it.

Second, weaker but real: `RacingSim.Tests.AutomationTestPlacement` depends on
`FAutomationTestFramework::GetValidTestNames`, which filters by application context
(`AutomationTest.cpp:800-845`). A test flagged `ClientContext`-only in the runtime module
is invisible to it. That specific gap is covered by the build scan, which is why both
layers exist — but the coverage argument holds only while both layers are maintained, and
the build scan is the one a future author is most likely to find annoying and weaken.

#### Remaining risks and rollback

- Rollback is `git revert` of the range `40508ac..5105f95` on this branch (corrected
  2026-08-18, pass 2 `P2-3` — previously stated `40508ac..c5d0960`, which predates and
  would leave behind the two repair-cycle commits `34f46ce`/`5105f95`); no engine
  source, no `.uasset`, no shared config outside `Config/DefaultGame.ini` was touched.
- The `Build.cs` scan reads every runtime-module source file on each makefile
  regeneration. Measured cost is inside the 2.34 s failure path above, i.e. negligible at
  today's ~20 files. If the module grows to thousands, re-measure rather than assume.
- **Corrected 2026-08-18, pass 2 `P2-2`.** This previously read "The comment stripper in
  the scan does not understand string literals. A banned macro name inside a string
  literal followed by `(` would false-positive." That was fixed at `T-2` (repair commit
  `34f46ce`) — the stripper is now a literal-aware left-to-right scanner that correctly
  handles string/char/raw-string literals — and this line was left stale, contradicting
  the N-2 enforcement section above which already states the fix. No open risk remains
  here.
- **Follow-up owed, pass 2 `P2-1` (MEDIUM, not blocking).** The `.rsp`-based receipt
  check (`T-1`'s fix) has no freshness guard: if `RacingSimTests` is added to
  `RacingSim.Target.cs` and only the documented gate (`Automation RunFilter Smoke`) is
  run — which does not rebuild the Game target — the on-disk `.rsp` is stale and the
  check reports a confident, wrong pass. Absence of the `.rsp` degrades to a visible
  warning (`T-5`); *staleness* does not. Recorded as an obligation on the ticket that
  next touches this check (`TRACK-002`/`RACE-002`, alongside the `/Game/Tests` pak
  obligation already assigned there): compare the `.rsp`'s `LastWriteTime` against
  `Source/RacingSim.Target.cs` and `Source/RacingSim/RacingSim.Build.cs`, and warn or
  fail if the receipt predates its inputs.

---

### CORE-003 — acceptance criteria, opened 2026-08-14

Scope per this row: `DataAsset validation framework`. Owner `race-systems-engineer`.
Gate A. Depends on `CORE-002` (DONE). Read "### CORE-003 — findings inherited from
CORE-002" immediately below **first** — it names two concrete defects this ticket
must close, not just a framework to build in the abstract.

> **Note on this block, 2026-08-14.** These criteria existed only in the `main`
> working tree, uncommitted, and were absent from the implementation branch —
> exactly the CORE-002 `LOW-4` failure repeating. Copied here verbatim before
> implementation started, then checked off in place.


- [x] A reusable, reflection-driven validation pass that re-applies a `UPROPERTY`'s
      `ClampMin`/`ClampMax` metadata after config/ini load — closing the gap that
      metadata only constrains the Details panel today, not an
      `-ini:Game:[...]:Field=value` override.
      → `Source/RacingSim/Core/RacingSimValidation.h/.cpp`,
      `namespace RacingSim::Validation`. `EnforceRanges(UObject*, TConstArrayView<FRacingPropertyRange>)`
      resolves each declared range by name to an `FProperty`, reads/writes through
      `FNumericProperty`, and clamps. No dependency on `URacingSimSettings`; it lives in
      `Core/` and takes any `UObject`.
- [x] `URacingSimSettings` calls this pass (e.g. from a config-load hook) so an
      out-of-range ini value (`TelemetrySampleRateHz=1e6`, negative
      `PhysicsPolicyVersion`, etc.) is clamped or rejected rather than loading
      unchallenged.
      → `PostInitProperties()`, `PostReloadConfig()` and (editor) `PostEditChangeProperty()`
      all call `ValidateConfiguredRanges()`. Ordering verified in engine source, not
      assumed: `UObjectGlobals.cpp:4274` runs `LoadConfig`, `:4320` runs
      `PostInitProperties`, so the ini and any `-ini:` override are already applied.
- [x] `URaceRulesetDataAsset::Validate()` (RACE-001) is evaluated for reuse under this
      same framework rather than staying a one-off pattern — either adopt it as the
      framework's shape, or state in this ticket why it stays separate.
      → **Decision: they stay separate, and they compose.** See "CORE-003 — decision:
      `URaceRulesetDataAsset::Validate()` stays separate" below.
- [x] M-5 (pass 1, CORE-002) closed — see the inherited-findings table below.
      → `RacingSim.Core.SettingsIniOverrideClamp` drives `LapTimeFractionalDigits=99`
      and `TelemetrySampleRateHz=1000000.0` through `GConfig` + `UObject::ReloadConfig`
      and asserts they arrive as `3` and `240`.
- [x] MEDIUM-1 (pass 2, CORE-002) closed: `FRacingSimBuildId::Current()`'s Explicit
      branch stops marking a sanitisation-mutated stamp `bIsAuthoritative = true`;
      adopt the same rule already used one branch below in the same function for the
      empty-stamp case (`Result.bIsAuthoritative = (Stamped == Trimmed)`).
      → Applied verbatim at `RacingSimBuildId.cpp:162`. `FRacingSimBuildId::bIsAuthoritative`'s
      doc comment updated to state the new condition.
- [x] MEDIUM-2 (pass 2, CORE-002) closed: either add `+` to `SanitiseComponent`'s
      allow-list, or document explicitly why Derived may embed `+` and Explicit may
      not.
      → **Decision: `+` added to the allow-list**, which is now `[A-Za-z0-9._+-]`.
      Rationale and the rejected alternative are recorded in the code at
      `RacingSimBuildId.cpp` `SanitiseComponent` and summarised below.
- [x] `RacingSimTests` gains automation coverage: an out-of-range ini value is
      clamped/rejected for at least two distinct properties, plus tests for both
      MEDIUM-1 and MEDIUM-2's fixed behavior.
      → `Source/RacingSimTests/Core/RacingSimValidationSpec.cpp` (3 new tests) and
      updates to `RacingSimVersionSpec.cpp`. See the verification evidence below.
- [x] Editor **and** Game targets build with zero new warnings.
      → See "CORE-003 — verification evidence" below.

### CORE-003 — verification evidence, 2026-08-14

Run in this ticket's worktree
(`.claude/worktrees/agent-aa457d3da1306fb55`), so paths below are worktree-relative.

- Editor (`RacingSimEditor Win64 Development`): `Result: Succeeded`, `EXITCODE=0`,
  219.14 s. Filtered for `warning|error` (case-insensitive) over the full UBT
  transcript: **0 matches**.
- Game (`RacingSim Win64 Development`): `Result: Succeeded`, `EXITCODE=0`, 300.71 s,
  output `Binaries/Win64/RacingSim.exe`. Filtered the same way: **0 matches**.
  Note the Game target compiles `RacingSim` only — `RacingSimTests` is absent from it,
  which is the module split doing its job.
- Automation `Smoke` filter: `Saved/Automation/Report/index.json` —
  **`succeeded: 435, failed: 0, notRun: 0`**, `reportCreatedOn 2026.08.14-07.06.02`.
  Baseline at CORE-002 was 432; the three new tests are the difference.
- All nine project tests `Success`: `RacingSim.Core.BuildId`, `.LogCategories`,
  `.RangeEnforcement`, `.SettingsDefaults`, `.SettingsIniOverrideClamp`,
  `.SettingsRangeMetadata`, `.Telemetry`, `.Units`, `.VersionStamp`.

**Post-merge re-verification, 2026-08-17.** After merging `main` (which has `RACE-001`)
into this branch at `9aa018d`, both targets were rebuilt and the Smoke filter re-run in
this worktree: **`succeeded: 445, failed: 0, notRun: 0`**, `reportCreatedOn
2026.08.17-06.20.20`. The rise from 435 to 445 is RACE-001's 10 `RacingSim.Race.*`
suites joining the same run, not a change to CORE-003 itself — the nine project tests
listed above are still exactly nine and still all `Success`. Both targets: `Result:
Succeeded`, zero real `warning|error` matches (confirmed from each build command's own
captured output, not the shared `%LOCALAPPDATA%\UnrealBuildTool\Log.txt`, which is
overwritten by concurrent builds from other worktrees on this machine and is not
reliable evidence when other tickets are being worked in parallel).

**Pass-2 repair re-verification, 2026-08-17.** After `f35bd83` (closing `code-reviewer`
pass-2 findings M2-1, M2-2, M2-5, M2-7, M2-8), both targets rebuilt clean and the Smoke
filter re-run: **`succeeded: 445, failed: 0, notRun: 0`**, `reportCreatedOn
2026.08.17-06.50.13`, all 9 `RacingSim.Core.*` and all 10 `RacingSim.Race.*` suites
`Success`, `0` warnings/`0` errors each, including `RacingSim.Core.RangeEnforcement`.
This is the run current as of the ticket's final commit.

**The first Smoke run failed, and that is the most useful evidence here.**
`succeeded: 434, failed: 1` at `reportCreatedOn 2026.08.14-07.03.12`.
`RacingSim.Core.SettingsIniOverrideClamp` reported the clamp working correctly —

```text
Range validation corrected a configured value: RacingSimSettings::LapTimeFractionalDigits
  loaded as 99, which is outside the declared range [0, 3]; corrected to 3.
Range validation corrected a configured value: RacingSimSettings::TelemetrySampleRateHz
  loaded as 1e+06, which is outside the declared range [0, 240]; corrected to 240.
```

— while failing on its own `AddExpectedMessagePlain` patterns, which had been written
`URacingSimSettings::...`. `UClass::GetName()` returns the reflected name without the
C++ prefix. The patterns were wrong; the code was not. This is worth recording because
it demonstrates the new spec file is genuinely discovered by the `RunFilter Smoke`
gate and can actually fail — the property `Docs/Environment.md` insists on, and the
thing a green-on-first-run suite never proves.

**Revision integrity.** `RacingSimBuildId.cpp` (MEDIUM-1 + MEDIUM-2) was last written
13:15:27; `Binaries/Win64/UnrealEditor-RacingSim.dll` was produced 13:48:38 and
`Binaries/Win64/RacingSim.exe` 14:53:15, both after it. The test run that produced the
435/0/0 report used `UnrealEditor-RacingSimTests.dll` built 15:04:30. Both fixes are in
the tested binaries.

**What was NOT done, stated plainly:** no packaged (`BuildCookRun`) run and no
Shipping-configuration build. `WITH_METADATA == 0` in the Game target is established
from engine source (`CoreMiscDefines.h:31`, `TargetRules.cs:1203`,
`UEBuildTarget.cs:6556`) and from `RacingSim.Target.cs` declaring `TargetType.Game`
with no `bBuildWithEditorOnlyData` override — but the metadata-absent path has not
been *executed*, only reasoned about and compiled. See the counter-case below.

### CORE-003 — review findings, pass 1

Verdict: **changes required** — 1 HIGH, 5 MEDIUM, several LOW.

| ID | Finding | Disposition |
| --- | --- | --- |
| HIGH-1 | The non-finite/below-min replacement used `Range.Min` as "the safe end". For `TelemetryStaleAfterSeconds` (`ClampMin = 0.0`) that replaces a broken value with exactly the sentinel `FRacingTelemetryFrame::IsStaleAt` reads as "staleness checking disabled" — turning an obviously broken config into a silently permissive one, and falsifying the header's own "every range has its safe end at the minimum" claim | **Fixed** — added `FRacingPropertyRange::WithReplacement()`; `TelemetryStaleAfterSeconds` declares `0.5` (its class default) and any correction on it now uses that instead of a bound. New `ERangeAction::ReplacedOutOfRange`. The policy paragraph in `RacingSimValidation.h` was rewritten and now names this as the counter-example. The NaN test no longer asserts "finite" — it calls the real `IsStaleAt` and asserts a 100 s old frame still reads stale, plus a new negative-value case, which is the one a plain clamp gets wrong |
| MEDIUM-1 | `FInt64Property` is accepted with double-typed bounds, losing precision above 2^53 — inconsistent with rejecting `FUInt64Property` | **Documented** (reviewer's suggested option) — precision limit and the asymmetry's reasoning recorded on `FRacingPropertyRange` in `RacingSimValidation.h`. No int64 `UPROPERTY` exists in the codebase |
| MEDIUM-2 | `VerifyRangesMatchMetadata`'s direction-2 sweep filters on `CPF_Config`, so it checks nothing for a `UDataAsset` — undermining the very reuse CORE-003 recommends | **Documented as a blocking precondition** — see `C3-2` in "### RACE-002 — findings inherited from CORE-003". Not fixed here: parameterising the filter without a real DataAsset consumer to test it against would be speculative, and RACE-001 owns the first consumer |
| MEDIUM-3 | The RACE-002 handoff lived only in CORE-003's own body — CORE-002 `MEDIUM-4` again | **Fixed** — added "### RACE-002 — findings inherited from CORE-003" (4 rows) and added `CORE-003` to RACE-002's `Depends on` column |
| MEDIUM-4 | The range table itself was unvalidated: no `Min <= Max` check, and `ContainerPtrToValuePtr` addresses element 0 only, so `ArrayDim > 1` silently validated one element | **Fixed** — `IsRangeSelfConsistent()` rejects an inverted range and a replacement value outside its own range; `ResolveNumericProperty` rejects `ArrayDim > 1`. All three report as `Failed`/Error. **Two** new test cases (inverted range, replacement-outside-range) — the `ArrayDim > 1` rejection guards a shape no `UPROPERTY` in the codebase currently has, and stays untested; the guard itself is a straight-line check on `FProperty::ArrayDim`, low risk, but exercising it needs a purpose-built reflected test fixture that was judged not worth adding for dead-code coverage alone. Correction made 2026-08-17, `code-reviewer` pass 2 (`M2-2`): this row previously overstated the count as three |
| MEDIUM-5 | Nothing told a future CI author that `SettingsRangeMetadata` is load-bearing | **Fixed** — call-out block added to `Docs/Environment.md` under "Run automation tests" |
| LOW-1..LOW-7 | Float-bound re-logging noise, double-logging in `ValidateConfiguredRanges`, misleading `IsEnum` comment, `IsA` vs `CastField` style, whitespace-only stamp authority edge case, "no ini file written" test-comment overstatement, build-ID format compatibility note for RACE-003 | **Batched forward**, except the last, which is recorded as `C3-4` in the RACE-002 inherited-findings table because it changes a written result format |

### CORE-003 — counter-case: the strongest argument against this design

The range table duplicates the `ClampMin`/`ClampMax` metadata, and duplication is the
thing this ticket was supposed to eliminate. A reviewer is entitled to say: read the
metadata, delete the table, done.

That version does not work, and the reason is the interesting part. `FField`'s metadata
accessors are compiled out unless `WITH_METADATA` is 1, and `WITH_METADATA` *is*
`WITH_EDITORONLY_DATA` (`CoreMiscDefines.h:31`), which UBT sets to 0 for any target
that is not Editor or Program (`TargetRules.cs:1203`, `UEBuildTarget.cs:6556`). So a
metadata-reading pass would enforce ranges in the editor, enforce nothing in the
packaged Game build a CI `-ini:` override actually ships to, and **pass every test** —
because automation runs in the editor. It would be a worse defect than M-5, delivered
with green evidence.

The residual weakness of the design actually chosen is honest and narrower: the table
and the metadata are two statements of one fact, kept in agreement by
`RacingSim.Core.SettingsRangeMetadata` rather than by construction. That test checks
both directions and carries three negative controls (a wrong bound, a missing entry, a
dropped `ClampMax`), plus an assertion that `WITH_METADATA != 0` so it cannot pass
vacuously. If it is ever weakened or skipped, the duplication becomes a real hazard.

### CORE-003 — decision: `URaceRulesetDataAsset::Validate()` stays separate

`URaceRulesetDataAsset` does not exist on `main`; it is being written concurrently
under `RACE-001` (which is still `OPEN`) in a different worktree. It was read there
rather than imagined, and this decision is written against that code — **it is a
recommendation to `RACE-001`, not a change made to it.** CORE-003 deliberately does
not touch a file owned by an in-flight ticket.

**Decision: do not reshape `Validate()` into the range framework. Keep both, and have
`Validate()` be the caller.**

Reasoning, from what the two things actually check:

1. **They answer different questions.** `EnforceRanges` answers "is this number
   inside its declared bounds", mechanically, from reflection. `Validate()` answers
   "is this asset fit to produce a publishable result" — `RulesetId.IsNone()` is not a
   range, and `CountdownSeconds == 0.0` is explicitly *legal* (automation uses it to
   skip the wait) while still being refused for a published run. A range framework
   cannot express "legal, but not for this purpose", and widening it until it could
   would turn a 30-line reflection pass into a rules engine.
2. **They differ on clamp-vs-reject, correctly.** The config pass clamps, because a
   settings CDO has no last-known-good value and a Pixel Streaming worker should not
   refuse to boot over an ini typo. `Validate()` rejects and returns a reason, because
   an authored asset has an author who can fix it, and silently clamping a designer's
   countdown would hide the mistake. Merging them would force one policy on both.
3. **`Validate()` is a `bool` + reason; the framework returns a structured result.**
   Callers of `Validate()` are gates ("may this run start?"). Callers of
   `EnforceRanges` are load hooks that must repair and continue.

**What `RACE-001` should adopt instead**, and the only part of this that is a real
handoff: `URaceRulesetDataAsset` should declare a range table exactly as
`URacingSimSettings::GetValidatedPropertyRanges()` does, call `EnforceRanges` from
`PostLoad()`, and have `Validate()` call it first and fail if `NumFailed() > 0`. That
gets the DataAsset the same "the `ClampMin` you wrote is actually enforced" guarantee
without collapsing the two functions. The framework was written to `UObject*` and a
range table precisely so it can be reused this way — it has no knowledge of
`URacingSimSettings`.

**Counter-case, recorded rather than hidden:** this leaves the project with two
validation entry points, and a future reader may reasonably ask which to call. The
mitigation is a naming and ownership rule, not a merge: `EnforceRanges` is a *load
repair* and always runs automatically from a load hook; `Validate` is a *gate* and is
always called explicitly by something that is about to trust the data. If a third
pattern appears, this decision should be revisited at `RACE-002`.

### CORE-003 — decision: MEDIUM-2, `+` added to the allow-list

`SanitiseComponent`'s allow-list is now `[A-Za-z0-9._+-]`.

**Why this direction.** The Derived composer already embeds a literal `+` as the
engine-changelist separator (`5.8.1+56057345`), so the allow-list rejecting `+` made
the two schemes contradict each other, and it made the H-2 sanitisation warning fire
on every conventionally formatted CI stamp — semver build metadata is `1.4.0+4417`. A
warning that fires on correct input is a warning that gets ignored, which would then
hide the real cases (`feature/x`).

**The alternative that was rejected**, and its cost: removing `+` from the *Derived*
composer instead (e.g. `5.8.1.56057345`) would have preserved a stricter
"URL-safe-everywhere" property. It was rejected because `+` is only ambiguous inside
an `application/x-www-form-urlencoded` **query string**, where it decodes to a space —
and derived IDs already carried that exposure, so allowing `+` on the Explicit path
makes an existing hazard consistent and testable rather than introducing a new one. It
also avoids changing an already-documented, already-tested output format for no
behavioural gain.

**The rule this places on consumers:** percent-encode a build ID before putting it in
a query string. `RacingSim.Core.BuildId` now asserts that every ID *either* scheme
produces matches `[A-Za-z0-9._+-]`, so that rule has exactly one character to worry
about. That assertion is new and deliberately covers the Derived composer, which
inserts its separators *after* sanitising its components and is therefore not covered
by `SanitiseComponent` at all.

**Interaction with MEDIUM-1, which is the point of doing both together:** MEDIUM-2
removes the false positives, and MEDIUM-1 makes the remaining true positives
non-authoritative. `1.4.0+4417` now round-trips verbatim and stays authoritative;
`feature/x` still warns, and now correctly produces an unpublishable result.

### CORE-003 — findings inherited from CORE-002

Raised by `code-reviewer` against CORE-002 (`Source/RacingSim/Core/RacingSimSettings.h`,
`RacingSimBuildId.cpp`, `RacingTelemetry.cpp`), deferred here because CORE-003 (this
ticket) is the DataAsset/config validation framework and these are all "unenforced range
or invariant" problems, not contract-shape problems. Read before writing CORE-003's
acceptance criteria — do not rediscover these from scratch:

| ID | Finding | What CORE-003 must do |
| --- | --- | --- |
| M-5 (pass 1) | `ClampMin`/`ClampMax` metadata on `URacingSimSettings` properties (e.g. `TelemetrySampleRateHz`, `PhysicsPolicyVersion`) is enforced only in the details panel, not on `-ini:Game:...=` config overrides. `LapTimeFractionalDigits=99`, `TelemetrySampleRateHz=1e6`, or a negative `PhysicsPolicyVersion` load unchallenged from an ini | Add a `PostInitProperties`/config-load validation pass that re-applies the same range constraints the `UPROPERTY` metadata declares, and a test that an out-of-range ini value is clamped or rejected |
| MEDIUM-1 (pass 2) | `FRacingSimBuildId::Current()` (`RacingSimBuildId.cpp`, Explicit-scheme branch) marks a sanitisation-mutated stamped ID `bIsAuthoritative = true` even though the fix's own comment states this breaks CI traceability and the uniqueness guarantee that `bIsAuthoritative` promises | Decide and implement the safer rule already used one branch below in the same function for the empty-stamp case: `Result.bIsAuthoritative = (Stamped == Trimmed);` — i.e. a mutated stamp is not authoritative |
| MEDIUM-2 (pass 2) | `SanitiseComponent`'s allow-list rejects `+`, but the Derived-scheme ID composer embeds a literal `+` itself (`RacingSimBuildId.cpp`, changelist separator) — inconsistent, and it means the H-2 warning fires on every conventionally-formatted CI stamp (semver build metadata, `1.4.0+4417`) | Either add `+` to the allow-list, or document explicitly why Derived may use `+` and Explicit may not |

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
| TRACK-001 | Original circuit graybox and spline centerline | race-systems-engineer | CORE-001 | B | **DONE** 2026-08-18 — `code-reviewer` approved across two passes (1 repair cycle, plus two disputed findings independently verified against actual UE 5.8 engine source and upheld); `test-engineer` independently confirmed both targets build clean from forced real recompilation and 452/452 automation Smoke tests pass (run twice, no flakiness). Merged to `main` at `5d44744`. Graybox test level deferred into `TRACK-002`'s scope by director ruling. Findings tracked forward into `TRACK-002`, `RACE-003`, `VEH-005`, `UI-001`, `TEST-001` |
| TRACK-002 | Ordered checkpoint gates and crossing direction | race-systems-engineer | TRACK-001 | B | OPEN |
| RACE-001 | Race state machine and monotonic clock | race-systems-engineer | CORE-002 | B | **DONE** 2026-08-14 — `code-reviewer` approved across two passes at `7832d0a`; `test-engineer` independently confirmed both targets build clean from a from-scratch rebuild and 442/442 automation Smoke tests pass. Merged to `main` at `2c41989`. Two findings (M4, M1's accepted risk) tracked forward into `RACE-002` |
| RACE-002 | Lap/sector/progress/validity logic | race-systems-engineer | TRACK-002, RACE-001, CORE-003 | B | OPEN |
| RACE-003 | Results, restart, metadata | race-systems-engineer | RACE-002 | B | OPEN |
| RACE-004 | Shortcut/reverse/double-trigger/reset automation matrix | test-engineer + implementer | RACE-003 | B | OPEN |

Gate B is unusually explicit and these tickets inherit it verbatim: 100 automated
valid laps count exactly once; 100 skipped/out-of-order/reverse/double-cross
scenarios never produce a valid lap; the timer is monotonic and independent of
render frame rate; reset can never award progress or duplicate a checkpoint.

The circuit must be **original**. No real track name, layout, signage or venue.

### RACE-001 — acceptance criteria, opened 2026-08-13

Scope per this row: `Race state machine and monotonic clock`. Owner
`race-systems-engineer`. Gate B. Depends on `CORE-002` (DONE).

**Deliberately track-agnostic.** `TRACK-001`/`TRACK-002` (checkpoints, centerline) and
lap/sector validation (`RACE-002`) are later, separate tickets. RACE-001 is the state
machine skeleton and the clock everything else attaches to — it must not reference a
checkpoint, a lap, or a track asset.

- [x] Race state enum (e.g. `ERaceState`: PreRace, Countdown, Racing, Finished, Results)
      lives in `Core/RacingSimTypes.h` alongside the project's other shared vocabulary
      (`ERacingRunValidity`, `ERacingInputDeviceType`) — `UI/` needs to read it for the
      HUD later without depending on `Race/`.
- [x] The state machine itself (transition logic, clock ownership) lives in `Race/`, not
      `Core/` — it is race truth, not a shared contract. `CLAUDE.md`: "No race truth
      lives in `Streaming`" implies the inverse too — race truth lives in `Race/`.
- [x] Only the authored transition graph is legal (e.g. PreRace→Countdown→Racing→
      Finished→Results, Results→PreRace on restart). An illegal transition attempt is
      rejected and logged, never silently applied and never a crash.
- [x] Countdown, start, finish, results, and restart transitions are deterministic and
      idempotent — calling the same transition twice produces no additional effect
      (Gate B, verbatim).
- [x] Race clock is monotonic and independent of render frame rate (Gate B, verbatim):
      elapsed time is derived from a monotonic time source (e.g. `FPlatformTime::Seconds()`),
      not accumulated from per-tick `DeltaTime`, so it cannot drift under frame-rate
      variance or a paused/hitched frame.
- [x] Clock is a single server-side authority — nothing client-side interpolates or
      guesses race time; this is the source `RACE-002` will time laps against.
- [x] Restart/reset can never award progress: restarting mid-race returns the state
      machine to `PreRace`/`Countdown` with the clock re-zeroed, never to a state that
      preserves partial progress (Gate B, verbatim — the checkpoint half of this rule is
      `RACE-002`'s, the state/clock half is this ticket's).
- [x] No per-frame allocations, no broad actor searches, no synchronous asset loads in
      the state machine's `Tick` or transition paths (`CLAUDE.md` coding rules).
- [x] `RacingSimTests` gains automation coverage: every legal transition, every illegal
      transition attempt (rejected, not crashed), idempotency of each transition called
      twice, and clock monotonicity under a simulated variable/dropped frame rate.
- [x] Editor **and** Game targets build with zero new warnings.

### RACE-001 — verification evidence, 2026-08-13

- Editor (`RacingSimEditor Win64 Development`): `Result: Succeeded`, 0 `warning|error`
  matches in the filtered UBT log.
- Game (`RacingSim Win64 Development`): `Result: Succeeded`, 0 `warning|error` matches
  in the filtered UBT log.
- Automation `Smoke` filter, this worktree: `Saved/Automation/Report/index.json` —
  `succeeded: 442, failed: 0, notRun: 0`, all 10 `RacingSim.Race.*` suites
  (`ClockMonotonic`, `ClockUnderStates`, `Countdown`, `Idempotency`,
  `PlatformTimeSource`, `Reentrancy`, `RestartAwardsNoProgress`, `Ruleset`,
  `StateMachineSemantics`, `TransitionGraph`) present and `Success`.
- One real bug found and fixed by the director during verification: the automation
  run's first pass (`442` total, `1` failed) found `RaceClockSpec.cpp`'s "A 4-second
  stall is counted in full" test used a `1e-12` tolerance on a `TestEqual` at
  `Epoch = 987654.5` (~1e6 magnitude, double ULP ~1.2e-10 there) comparing against a
  non-exactly-representable literal (`4.016`) — tighter than double precision allows.
  Fixed by widening to `1e-9` with a comment explaining why the whole/half-integer
  cases elsewhere in the same file are safe at `0.0` tolerance and this one is not.
  This is very likely what the implementing agent's cut-off final message ("Let me fix
  several floating-point-exactness and API risks I spotted before building") was about
  to address before its run ended.
- `race-systems-engineer`'s implementation additionally includes
  `Source/RacingSim/Race/RaceRulesetDataAsset.h/.cpp` (ruleset id, countdown seconds,
  content version/hash, validation) — not explicitly named in the acceptance criteria
  above but a reasonable supporting type for `PollAutoTransitions()`'s automatic
  countdown; flagged for `code-reviewer` to judge as in-scope or split out.

### RACE-001 — review findings, pass 1

`code-reviewer` verdict: **approve with conditions** (no BLOCKER, no HIGH). Confirmed
`URaceRulesetDataAsset` is in-scope (not scope creep) and confirmed the plain-`UObject`
design (vs. `AActor`/subsystem) holds up. Independently verified the clock arithmetic
numerically and the track-agnostic constraint by grep. Did not run the build or tests
itself — build/test evidence above is director-provided.

| ID | Finding | Disposition |
| --- | --- | --- |
| M1 | `HasRaceAuthority` fails open on a null `UWorld` (commandlet/automation), which is also reachable from some legitimate client-side outers — no automation coverage of either branch | Fixed — direct tests added for `HasRaceAuthority(nullptr)` and the world-less-context branch; the untested net-client branch is recorded as accepted risk in this section (below) rather than faked with a synthetic PIE world |
| M2 | `URaceRulesetDataAsset::Validate()` has no runtime caller; a NaN or negative `CountdownSeconds` reaches `GetCountdownRemainingSeconds()` (`BlueprintCallable`) as NaN, or causes instant release | Fixed — `CreateWithTimeSource` now rejects non-finite/negative `CountdownSeconds` at construction (falls back to manual countdown, logged). Deliberately NOT full `Validate()`: that also rejects `CountdownSeconds == 0.0`, which the project's own automation intentionally relies on for an instant-release countdown — `Validate()` remains a publish-time content check, not a construction-time gate. Tests added for both NaN and negative cases |
| M3 | The production `Create()` path (real `PlatformMonotonicSeconds` source) is never exercised — every test uses the fake time source | Fixed — a smoke test now exercises `Create()` end-to-end through `BeginCountdown`/`StartRace`, asserting only finiteness and non-decrease (no wall-clock duration assertion) |
| M4 | `CommitTransition` discards `FRaceClock::Start/Stop`'s `bool` return; a refused `Start` (non-finite reading) would still enter `Racing` and freeze a 0.000 result with no invalidity marker | **Deferred to `RACE-002`** — the fix requires plumbing `ERacingRunValidity` (Core, reserved by CORE-002) into a result, which is RACE-002/RACE-003's job, not this ticket's. Unreachable with the shipped platform source today |
| M5 | No exit actions (contrary to `Docs/01-Architecture.md`'s original "one entry action, one exit action" line) — countdown-clock teardown is duplicated into two entry actions instead | **Batched forward, documented** — `Docs/01-Architecture.md` now states the deviation explicitly and flags it as the first thing a new edge leaving `Countdown` must remember. Reviewer confirmed no re-review needed |
| M6 | `RaceStateMachineSpec.cpp`'s countdown-boundary test used non-binary-exact deltas (`2.9 + 0.1`) landing on a zero-margin `TestTrue`/`TestFalse` pair that passes only by rounding luck at this epoch | Fixed — replaced with binary-exact `2.875 + 0.125`, matching the discipline already used elsewhere in the same file |
| M7 | `Docs/15-ProjectStructure.md` and `Docs/01-Architecture.md` not updated: stale `Race/` file list, stale state diagram (`Boot->Loading->Grid->Countdown`, Restart landing in `Countdown`), `URaceClock` proposed as a `UObject` vs. shipped `FRaceClock` struct | Fixed — both docs updated to match shipped reality |
| L1 | Ticket's own evidence section said "8" `RacingSim.Race.*` suites; actual count is 10 | Fixed |
| L2 | `GetTransitionTarget(PreRace, Restart, ...)` returns `true` (static graph query) while `RequestTransition` returns `Redundant` for the same case (instance behaviour) — a caller using only the static helper could disagree with the object | **Batched forward** — reviewer did not require a fix for re-review; both behaviours are individually correct and tested, the disagreement is between two different questions ("is this an edge" vs. "would calling it do anything") |
| L3 | `Restart` from `PreRace` bumps no session id and broadcasts nothing | **Batched forward**, noted for `UI-001`/`RACE-002` |
| L4 | `CurrentState`/`SessionId` are non-`Transient` `UPROPERTY`s while `FRaceClock` is not a `UPROPERTY` at all — a duplicate/save could restore `Racing` with a zeroed clock | **Batched forward** |
| L5 | Test lambdas bound to `OnRaceStateChanged` are never explicitly unbound (safe today, since nothing broadcasts after the owning `TStrongObjectPtr` goes out of scope) | **Batched forward** |
| L6 | `ComputeContentHash`'s "any added field MUST be hashed" comment is unenforced by any guard | **Batched forward** |
| L7 | The tolerance fix (`1e-12` -> `1e-9`) was the looser of two valid repairs; a binary-exact fixture value would have preserved `0.0` tolerance | Not changed — reviewer noted this is a preference, not an objection; `1e-9` at a 4-second interval is 1 ns and cannot mask a real defect |
| L8 | No runtime `checkSlow(IsInGameThread())` guard on `FRaceClock::Sample()`'s mutating path | **Batched forward** |

**Accepted risk (M1, net-client branch):** `HasRaceAuthority`'s net-client-rejection
branch (`World->GetNetMode() != NM_Client`) has no automation coverage — constructing a
real networked `UWorld` under `-nullrhi Automation RunFilter Smoke` is out of proportion
for this ticket. The first ticket that constructs a `URaceStateMachine` inside a real
PIE/networked session (`RACE-002` or later, once `ARaceDirector` exists) must add that
coverage before this project ships with online play.

### RACE-002 — findings inherited from RACE-001

Raised by `code-reviewer` against RACE-001 (`Source/RacingSim/Race/RaceStateMachine.cpp`),
deferred here because closing them requires the lap/result plumbing RACE-002 owns. Read
before writing RACE-002's acceptance criteria:

| ID | Finding | What RACE-002 must do |
| --- | --- | --- |
| M4 (RACE-001 pass 1) | `URaceStateMachine::CommitTransition` discards `FRaceClock::Start()`/`Stop()`'s `bool` return. If `Start` ever refuses a reading (non-finite — unreachable with the shipped platform source, but reachable once a real result is written), the machine still enters `Racing` and later freezes a silent `0.000` result with nothing marking the run invalid | Check the return value; on refusal, mark the run's `ERacingRunValidity` (Core, reserved by CORE-002) invalid rather than letting a zero-duration result reach a leaderboard |
| M1's accepted risk (RACE-001 pass 1) | `URaceStateMachine::HasRaceAuthority`'s net-client-rejection branch has no automation coverage | Once `ARaceDirector` (or equivalent) constructs a `URaceStateMachine` inside a real PIE/networked session, add a test exercising the net-client rejection path |

### RACE-002 — findings inherited from CORE-003

Raised during `CORE-003` (the config/DataAsset range-validation framework) and
recorded here rather than only in CORE-003's own body — the same mistake CORE-002's
`MEDIUM-4` already committed this project to not repeating. `RACE-002` gains a
`CORE-003` dependency in the table above because of the first row.

| ID | Finding | What RACE-002 must do |
| --- | --- | --- |
| C3-1 | `URaceRulesetDataAsset::Validate()` (RACE-001) stays a hand-written gate; `CORE-003` deliberately did not reshape it, and did not touch the file because RACE-001 was in flight. But its `ClampMin`/`ClampMax` metadata has exactly the CORE-002 M-5 problem: it constrains the Details panel and nothing else | Declare a range table as `URacingSimSettings::GetValidatedPropertyRanges()` does, call `RacingSim::Validation::EnforceRanges` from `PostLoad()`, and have `Validate()` call it first and fail if `NumFailed() > 0`. Keep the two functions separate — `EnforceRanges` is *load repair*, `Validate` is a *gate*. Reasoning in "CORE-003 — decision: `URaceRulesetDataAsset::Validate()` stays separate" |
| C3-2 | **`VerifyRangesMatchMetadata()`'s direction-2 sweep filters on `CPF_Config`**, so for a `UDataAsset` (whose properties are `EditAnywhere`, not `config`) it checks nothing. That is the direction that catches "a new clamped property was added and nobody updated the table" — i.e. the reuse recommended in C3-1 silently loses the guarantee that makes the duplication safe | Before reusing the framework on a DataAsset, either parameterise the property filter in `RacingSimValidation.cpp` (pass the required `EPropertyFlags`, defaulting to `CPF_Config`) or add an equivalent explicit coverage test for the asset. **Do not reuse the framework on a DataAsset without closing this** — the range table would be unguarded against drift |
| C3-3 | A property's `ClampMin` is not always its safe value. `TelemetryStaleAfterSeconds` clamping to its minimum (0.0) *disables* staleness checking. `CORE-003` added `FRacingPropertyRange::WithReplacement()` for this | When declaring ranges for lap/sector tolerances, check each bound: if the extreme value means "off" or "unbounded" rather than "least", declare a `WithReplacement()` and assert the resulting behaviour, not just that the field is in range |
| C3-4 | The derived build-ID format now embeds `+` on both schemes (`[A-Za-z0-9._+-]`). Results written by `RACE-003` must percent-encode a build ID before putting it in a URL query string, where `+` decodes to a space | Carry this into the results/metadata format at `RACE-003`; `RacingSim.Core.BuildId` asserts the character set |
| C3-5 | `IsRangeSelfConsistent()` validates the *declared double* bounds, not the *effective integer* bounds after inward rounding (`Ceil`/`Floor`). An int property with fractional-looking bounds (e.g. `Between("SomeInt", 0.2, 0.8)`) would pass self-consistency, then produce `MinInt = 1, MaxInt = 0` — an inverted effective range the guard was built to catch, missed because it checks the wrong space. No int property with this shape exists today, so unreachable in practice, but the guard's coverage claim is narrower than it reads | If a future range declares fractional bounds on an integer property, round first, then self-consistency-check the rounded bounds — not the declared doubles |
| C3-6 | Same rounding asymmetry on the replacement path: range bounds round inward (`Ceil`/`Floor`), but `Range.ReplacementValue` rounds unconditionally with `FloorToInt64`, so a declared replacement can land below the effective integer minimum after passing the double-space self-consistency check | Round `ReplacementValue` the same directionally-safe way bounds are rounded, or validate the rounded replacement against the rounded bounds |
| C3-7 | `bIsAuthoritative`'s doc comment says a stamped build ID must have "survived sanitisation byte-for-byte" to stay authoritative, but the actual check (`Stamped == Trimmed`) only compares against the *trimmed* string — a stamp with leading/trailing whitespace is silently trimmed and still counted authoritative. The code is arguably fine (whitespace trimming is not the traceability-losing mutation the guard exists to catch); the doc overstates the guarantee | Either tighten the check to reject whitespace-only differences too, or soften the doc comment to describe what's actually asserted |

### TRACK-001 — acceptance criteria, opened 2026-08-14

Scope per this row: `Original circuit graybox and spline centerline`. Owner
`race-systems-engineer`. Gate B. Depends on `CORE-001` (DONE). **Original circuit
only** — no real track name, layout, signage, or venue (`CLAUDE.md` non-negotiable
decisions; `LEGAL-001`'s ledger governs any external reference art).

**Deliberately checkpoint-agnostic**, mirroring RACE-001's split: ordered checkpoint
gates and crossing-direction validation are `TRACK-002`, not this ticket. TRACK-001 is
the centerline/track-identity contract everything else attaches to.

- [x] `ATrackDefinitionActor` (`Source/RacingSim/Race/`, per `Docs/01-Architecture.md`'s
      proposed types) exposes: centerline spline (closed loop), track length in
      centimetres (with a documented cm→SI conversion, per `CLAUDE.md` units rule and
      `RacingSimUnits.h`'s existing conversion policy), sector boundary markers along
      the spline, start/finish transform, grid slot transforms, and reset sample points
      (nearest-valid-track-point candidates for `VEH-005`'s safe reset, later).
- [x] Spline-distance and nearest-point queries are exposed as a typed, testable API
      (not raw `USplineComponent` calls scattered across callers) — this is the surface
      `RACE-002`'s progress/lap logic and `VEH-005`'s reset will consume.
- [x] Track identity uses `FRacingContentVersion` (`CORE-002`, `RacingSimBuildId.h`) —
      `GetContentVersion()`/content-hash pattern, matching `URaceRulesetDataAsset`'s
      precedent from `RACE-001` — so `FRacingSimVersionStamp::TrackVersion` (reserved by
      CORE-002) can be populated from a real asset instead of staying empty.
- [~] A minimal graybox test level (`Content/Tracks/Prototype/Maps/`, per
      `Docs/15-ProjectStructure.md`'s planned tree) containing one closed-loop
      `ATrackDefinitionActor` instance, using only primitive/placeholder geometry — no
      final art, no license-ledger-requiring external asset. Sufficient for `TRACK-002`
      and `RACE-002`'s automation to exercise real checkpoint/lap logic against.
      **DIRECTOR-APPROVED DEFERRAL, moved into `TRACK-002`'s scope, 2026-08-17.** The
      level is not dropped and is not optional; it is reassigned to the ticket that
      actually consumes it. `TRACK-002` must place checkpoint gates in a level, so it
      owns the level's authoring, ownership serialisation and license posture as a
      single unit rather than inheriting a half-specified map from here. This deferral
      is only defensible because TRACK-001's testability-first design (criterion below)
      already proves the spline/centerline query API works **without** a placed level:
      every shipped test runs against a procedurally-constructed spline, not against
      the map, so no TRACK-001 correctness claim depends on the deferred artifact. See
      `### TRACK-002 — findings inherited from TRACK-001` below, which records this as
      a `TRACK-002` obligation. **This is a director ruling and supersedes the
      implementer's earlier self-certified "DEFERRED, not attempted" reclassification,
      which was written into a duplicate acceptance-criteria block and has been
      deleted.** An implementer may not reclassify its own acceptance criteria.
- [x] `RacingSimTests` gains automation coverage for the centerline/spline-query API
      (distance along spline, nearest point, sector boundaries) that does **not**
      require the test map — testable against a procedurally-constructed spline in a
      transient world/commandlet, matching RACE-001's testability-first design so the
      cheap, certain work doesn't block on the level-authoring step.
- [x] Editor **and** Game targets build with zero new warnings.

### TRACK-001 — verification evidence

Original implementation evidence, recorded by the implementer at 2026-08-14 (worktree
`.claude/worktrees/agent-aad8b1bd0b3efd1e5`):

- Editor (`RacingSimEditor Win64 Development`): `Result: Succeeded`, 0 `warning|error`
  matches in the filtered UBT output.
- Game (`RacingSim Win64 Development`): `Result: Succeeded`, 0 `warning|error` matches.
- Automation `Smoke`: `succeeded: 441, failed: 0, notRun: 0` (CORE-002's 432 baseline
  plus 9 new suites).

New suites, all `Success`: `RacingSim.Race.CenterlineBuild`,
`CenterlineDistanceDomain`, `CenterlineQueries`, `CenterlineAmbiguity`, `TrackBake`,
`TrackSectors`, `TrackPoses`, `TrackVersion`, `TrackValidation`.

**Repair cycle 1 evidence, 2026-08-18.** Run from worktree
`.claude/worktrees/agent-a2388f8dc9169fc61` (see the worktree note at the end of this
section). Build results were read from each command's **own captured stdout**, not from
`%LOCALAPPDATA%\UnrealBuildTool\Log.txt`, which is shared machine-wide and was being
overwritten by other agents' concurrent builds during this cycle.

- Editor (`RacingSimEditor Win64 Development`): `Result: Succeeded`,
  `BUILD_BAT_EXITCODE=0`, 31 actions, 0 `warning`/`error` lines in the command's own
  output. Incremental re-build after the test fix: `Result: Succeeded`, 4 actions.
- Game (`RacingSim Win64 Development`): `Result: Succeeded`, `BUILD_BAT_EXITCODE=0`,
  24 actions, 0 `warning`/`error` lines. Re-run after the final revision: `Result:
  Succeeded`, 0 actions (runtime sources unchanged; only `RacingSimTests` moved, and
  the Game target excludes that module per CORE-001).
- Automation `Smoke`: `Saved/Automation/Report/index.json` —
  **`succeeded: 452, failed: 0, notRun: 0`**, `reportCreatedOn 2026.08.18-01.55.23`,
  process exit code 0. Counts read from `index.json`, never from the exit code.

That is 441 at the end of implementation plus this cycle's 3 new suites, plus 8 from
other tickets that landed on this branch's base. All 22 `RacingSim.*` suites report
`Success`, including the three added here: `RacingSim.Race.TrackFailedBakeIsNotRetried`,
`RacingSim.Race.TrackHintReseed`, `RacingSim.Race.TrackFixtureRestore`.

**One real defect was found by running, not by inspection.** The first Smoke run of this
cycle returned `succeeded: 452, failed: 1`. `RacingSim.Race.TrackValidation` failed
because the new M6 cases used the fixture's `RestoreSpline()` as their mid-test restore
— but that restores the *CDO's original* spline (`USplineComponent`'s two-point
default), not the fixture's 12-point circle. Every later case then failed on "fewer than
3 spline points" for the wrong reason. Fixed at `27ac97f` by extracting
`AuthorSpecCircle()`; `RestoreSpline()` is left to its actual job. The distinction is
now documented in the helper, because it is not visible at the call site.

> **Worktree note.** This repair cycle was directed at worktree
> `agent-a2415c035e0b2a559`, but the executing agent was hard-isolated by the harness to
> `agent-a2388f8dc9169fc61` and its git operations against the other worktree were
> refused. The six TRACK-001 source files were therefore brought across and verified
> **byte-identical to `b4988b0`** by comparing `git hash-object` output against
> `git ls-tree` for that commit before any edit was made. The repair commits are
> `110ee3b`, `971265f`, `27ac97f` on branch `worktree-agent-a2388f8dc9169fc61`.

**Two real defects were found by building and running, neither visible by inspection:**

1. `error C2665: 'GetTypeHash'` — there is no usable `GetTypeHash` for `FVector` or
   `FTransform` here. Fixed by hashing components as `double`, which is also more
   correct: a memory-based hash would fold in struct padding and make the content hash
   depend on compiler layout rather than on the authored numbers.
2. `error C2666: 'TestNearlyEqual'` ambiguous — `USplineComponent::GetSplineLength()`
   returns **`float`**, not `double`. The engine's whole spline distance API is
   float-based. Fixed with an explicit cast and a documented precision boundary
   (~0.03 cm on a 5 km lap, three orders of magnitude below the 100 cm sample spacing).

**Test-harness finding worth more than the tests themselves.** Actors cannot be
instantiated in this project's automation harness by either obvious route. Both were
tried and both **crashed `UnrealEditor-Cmd`**, killing the whole run and producing no
`index.json`:

- `NewObject<ATrackDefinitionActor>(GetTransientPackage())` — the pattern RACE-001 uses
  for `URaceStateMachine` — dies with
  `Assertion failed: RegisteredElementType [TypedElementRegistry.h:536]` from inside
  `CreateDefaultSubobject`. The Typed Element Framework requires an actor to be created
  through the spawn path.
- `UWorld::CreateWorld(EWorldType::Game)` + `SpawnActor` dies with a bare
  `Fatal error!` (access violation, no assertion) inside `CreateWorld` itself, before
  any actor is spawned.

The fixture is therefore the **class default object**, which already exists and needs
neither a world nor an object-creation path. Each test snapshots and restores every
authored property so the tests stay order-independent. **This is a project-wide
constraint, not a TRACK-001 one** — every future ticket with an Actor (VEH-002,
TRACK-002, RACE-003) hits it. `TEST-001` should own the fix (a functional-test map, or
the correct world-bootstrap incantation for this harness) and record it in
`Docs/Environment.md`.

### TRACK-001 — known gaps

- **`bTrackDataBuilt` is a "has been baked" flag, not a dirty flag.** Authored data
  changed at runtime without a rebuild leaves the previous bake in place. Safe in the
  editor (`PostEditChangeProperty`) and for a placed actor
  (`OnConstruction`/`PostLoad`/`BeginPlay`). The behaviour is **pinned by an assertion**
  in `RacingSim.Race.TrackBake`, so a future ticket adding dirty tracking will see it
  fail and update it deliberately. Repair cycle 1 added a second, independent flag
  (`bBakeAttempted`) that tracks *attempts* rather than *successes*, so a failed bake is
  no longer retried per query — see pass 1, H1.
- **`GetSectorIndexAtDistanceCm` assumes its array is sorted.** `Validate()` enforces
  that; the query itself does not re-check per call. A caller that mutates
  `SectorStartDistancesCm` without validating gets a wrong sector, not a crash.
- **Content-hash collision risk is accepted**, exactly as `FRacingContentVersion`
  already documents: `uint32` detects accidental drift, it is not a signature.

**Strongest counter-case against this ticket, recorded rather than argued away.** The
centerline model is a **polyline**, not the spline. Every query is internally consistent
and the round trip is exact to 1e-4 cm, but positions between samples sit inside the
true curve by roughly `Spacing^2 / (8 * radius)`. At the default 100 cm spacing that is
sub-centimetre and irrelevant — *until* `TRACK-002` places gate geometry using these
transforms and `RACE-002` compares a car's lateral offset against a track-limits
threshold. At that point a systematic sub-centimetre inward bias is being compared
against an authored limit, and nothing currently tests the interaction. The alternative
(querying `USplineComponent` directly at runtime) was rejected because its accuracy is a
per-asset authored property rather than a code property, which is worse — but "worse" is
not "harmless", and `TRACK-002` should assert the bias explicitly rather than inherit it
silently.

### TRACK-001 — review findings, pass 1

`code-reviewer` verdict: **CHANGES REQUIRED** — 3 HIGH, 8 MEDIUM, 6 LOW. Repair cycle 1
closed H1, H2, H3/M8, M1-M4, M6 and L1; the remaining LOW/MEDIUM findings are batched
forward into the inherited-findings sections below rather than fixed here.

**Count correction, pass 2 (`M6-B`).** This section originally stated "7 LOW" but only
ever carried six LOW rows (L1-L5, L7 — no L6 was ever recorded here, distinct from the
unrelated `L6` in RACE-001's own pass-1 table above). Corrected to 6; the seventh LOW
this ticket's pass-1 review may have raised could not be reconstructed from the
repository and is not fabricated here.

| ID | Finding | Disposition |
| --- | --- | --- |
| H1 | `EnsureTrackDataBuilt()` gated only on `!bTrackDataBuilt`, so a **failed** bake (null/degenerate spline component, or a spline collapsed to near-zero length — correction, pass 2 `L-a`: not "a freshly placed actor", whose default two-point ~100cm spline actually bakes successfully at 3 samples) was retried on **every** query call, each retry running a full sample loop with heap allocations plus a fresh `UE_LOG(Warning)` | Fixed — a separate `bBakeAttempted` flag now records that a bake was *attempted*, distinct from `bTrackDataBuilt` which records that one *succeeded*. A failed bake is attempted once and the failure cached; only an explicit `RebuildTrackData()` (or `OnConstruction`/`PostLoad`/`BeginPlay`/`PostEditChangeProperty`) retries. Failure logging is one-shot per attempt sequence. Proven by `RacingSim.Race.TrackFailedBakeIsNotRetried`, which counts bake attempts via `GetBakeAttemptCount()` rather than timing anything |
| H2 | No arc-length accessor for reset samples or grid slots. After a reset or at race start — the two moments a search hint is guaranteed stale — a caller had no way to re-seed `FindNearestNear` except calling global `FindNearest`, which this ticket's own `CenterlineAmbiguity` test proves snaps to the wrong hairpin leg | Fixed — added `GetResetSampleDistanceCm(int32)`, `GetGridSlotDistanceCm(int32)`, and distance-returning overloads `GetGridSlotTransformAndDistance` / `GetResetTransformAtOrBeforeDistanceCm(..., double& OutDistanceCm)`. Covered by `RacingSim.Race.TrackHintReseed` |
| H3 / M8 | The implementer wrote an **unauthorised second** `### TRACK-001 — acceptance criteria` block (duplicate heading, every box `[x]`) that self-certified the ticket done and unilaterally reclassified the required graybox level as "DEFERRED, not attempted" | Fixed by **director ruling**, 2026-08-17 — the duplicate block is deleted. Only the director's original block above survives. The graybox-level criterion is marked director-approved-deferred *into `TRACK-002`'s scope*, with the obligation recorded in `### TRACK-002 — findings inherited from TRACK-001`. An implementer may not reclassify the criteria it is being measured against |
| M1 | `ComputeContentHash` hashed the **authored** `CenterlineSampleSpacingCm`, but the bake substitutes 100 cm for a non-finite/≤0 value and clamps the sample count at `MaxGeneratedSamples`. Two builds with different **effective** resolution could hash identically | Fixed — the hash now covers the effective step and effective sample count actually used by the bake (`GetEffectiveSampleCount()`/`GetEffectiveStepCm()`), in addition to the authored field. Covered by `RacingSim.Race.TrackVersion` |
| M2 | `FindNearestNear` silently falls back to the global (wrong-leg-prone) search when `SearchWindowCm * 2 >= TotalLengthCm`, but the header listed only "non-positive window" and "non-finite hint" as fallback triggers — implying "bigger window = safer" when the opposite is true past half a lap | Fixed — documented explicitly at the declaration in `TrackCenterline.h` and at the `ATrackDefinitionActor::FindNearestCenterlinePointNear` call site, with the safe upper bound stated. Covered by `RacingSim.Race.CenterlineAmbiguity` (correction, pass 2 `L-b`: not `CenterlineQueries`) |
| M3 | `GetCenterline()` is `const` but lazily mutates several `TArray`s through `const_cast`, gated only by a comment saying "game thread only" | Fixed — `check(IsInGameThread())` added to `EnsureTrackDataBuilt()` and `RebuildTrackData()` |
| M4 | The `TrackDefinitionActorSpec.cpp` fixture mutates the CDO and restored only spline point **locations** — not tangents or point types — and carried `EditorContext` | **Half fixed, half disputed with evidence.** The data-loss half is fixed: the fixture now snapshots and restores arrive tangent, leave tangent and point type alongside location, and `RacingSim.Race.TrackFixtureRestore` verifies it from an independent witness. The `EditorContext` half is **declined**, because complying would silently delete this file's coverage. `Engine/Source/Runtime/Core/Private/Misc/AutomationTest.cpp:800-870` (`GetValidTestNames`) computes `bRunningEditor = GIsEditor && !IsRunningCommandlet()` and admits a suite only when `!CurTestApplicationFlags \|\| (CurTestApplicationFlags & ApplicationSupportFlags)`. This project's only recorded gate (`UnrealEditor-Cmd.exe ... -ExecCmds="Automation RunFilter Smoke"`, `Docs/Environment.md`) passes no `-run=`, so `IsRunningCommandlet()` is false and `ApplicationSupportFlags` is `EditorContext` **alone** — a `CommandletContext`-only suite would AND to zero and never be collected. It would not fail, it would cease to exist, and the gate would keep reporting green. `Docs/Environment.md` already records that exact failure mode as a CORE-001 blocker: "A test the documented gate cannot see is not coverage." Confirmed empirically: with `EditorContext` retained, all three new suites appear in the run. The residual risk — mutating a CDO at all — is batched forward to `TEST-001`, which owns the harness fix; the correct end state is "do not mutate the CDO", not "hide the suite from the only gate that runs it". Rationale is recorded in the spec file header so the next reviewer does not re-raise it blind |
| M6 | Four `Validate()` rejection branches had no test: `< 3` spline points, `NumSegments() < 3`, the bake-failure branch, and negative `GridSlotLateralOffsetCm` | Fixed for three; the fourth is proven unreachable **for a bake consistent with the spline `Validate()` reads** rather than faked (corrected 2026-08-18, pass 2 `M6-A` — originally overstated as unconditionally unreachable). `< 3` spline points, negative *and* non-finite `GridSlotLateralOffsetCm`, and the bake-failure branch (with an assertion that the reason names the bake, not a downstream symptom) are all now in `RacingSim.Race.TrackValidation`. For a closed loop baked from the spline `Validate()` is currently reading, `NumSegments() < 3` cannot fire: `Validate()` rejects an open loop several branches earlier, `RebuildTrackData` floors a closed-loop bake at `MinSamples = 3`, and `FTrackCenterline::NumSegments() == NumSamples()` for a closed loop. But `bBakeAttempted` is a has-been-baked flag, not a dirty flag (see `M5` below), so the bake and the spline `Validate()` reads can disagree — bake an open, short spline (`NumSegments() == 1`), then call `SetClosedLoop(true)` without rebuilding, and the guard reaches and correctly fires on `NumSegments() == 1`. This is live code on a stale-bake path, not dead defence in depth. The test asserts the narrower true claim: the guard does not false-positive at the coarsest bake a *consistent* spline/bake pair can produce (ten-lap spacing still floors at 3 samples/3 segments) |
| L1 | `"Track.Prototype.NorthLoop"` as the example asset id is the literal English rendering of *Nordschleife* | Fixed — replaced with `"Track.Prototype.Meridian"` in `TrackDefinitionActor.h` and in both `Core/RacingSimBuildId.h` sites it was inherited from (CORE-002) |
| M5 | `PostLoad()` bakes from the spline before component `PostLoad` ordering is guaranteed relative to `USplineComponent::PostLoad`; the spawn/load path is untested | **Batched forward to `TRACK-002`** — closing it needs the placed level TRACK-002 now owns |
| M7 | Bake and `Validate()` disagree (the bake substitutes fallbacks for values `Validate()` rejects outright), and there is no cheap cached-validity flag a race director can check before starting a session. Same family, added pass 2 (`L-c`): `ComputeContentHash()` still returns a hash when the bake **failed** (`EffectiveSampleCount == 0`, `EffectiveStepCm == 0`), so `GetContentVersion()` yields a version whose `IsPopulated()` is `true` for an unbakeable track — an unbaked track can stamp a result | **Batched forward to `RACE-003`** — the cached-validity flag work must cover the failed-bake-still-hashes case explicitly, not just the bake/`Validate()` disagreement |
| L2 | `GetSampleSpacingCm()` returns the **average** (`TotalLengthCm / NumSegments`), not "the spacing samples were baked at" as its comment claims — non-uniform sampling is legal per `Build()` | **Batched forward to `TRACK-002`** |
| L3 | `FTrackCenterline`/`FTrackCenterlineQuery` are `USTRUCT(BlueprintType)` with plain-C++ member functions, so none of the query API is Blueprint-reachable — the same class of gap as CORE-002's M-3 | **Batched forward to `UI-001`** |
| L4 | Integer-cast edge case on absurd `CenterlineSampleSpacingCm` values: degrades safely, but not by the mechanism the comment documents | **Batched forward to `TRACK-002`** |
| L5 | Reset poses use a fixed `PoseHeightOffsetCm` above the spline with no ground trace, so a reset on a crested or banked section can place a car in the air or in the road | **Batched forward to `VEH-005`** |
| L7 | A degenerate vertical tangent silently yields a zero lateral offset in `ProjectOntoSegments` instead of an invalid-query signal; the behaviour is documented for the sibling function but not this one | **Batched forward to `TRACK-002`** |

### TEST-001 — findings inherited from TRACK-001

| ID | Finding | What TEST-001 must do |
| --- | --- | --- |
| M4 residual (TRACK-001 pass 1) | `TrackDefinitionActorSpec.cpp` mutates the `ATrackDefinitionActor` **class default object**, process-wide, because neither obvious way to instantiate an actor works in this harness: `NewObject<AActor>(GetTransientPackage())` dies in `CreateDefaultSubobject` on `Assertion failed: RegisteredElementType [TypedElementRegistry.h:536]`, and `UWorld::CreateWorld(EWorldType::Game)` dies with a bare access violation inside `CreateWorld`. The restore is now exhaustive and verified by `RacingSim.Race.TrackFixtureRestore`, but the right answer is not to mutate the CDO at all | Provide a working actor-instantiation path for automation (a functional-test map, or the correct world bootstrap for `-nullrhi` + `RunFilter Smoke`) and record it in `Docs/Environment.md`. Every future ticket with an Actor — VEH-002, TRACK-002, RACE-003 — hits this same wall |
| M4 disputed half (TRACK-001 pass 1) | `code-reviewer` asked for `EditorContext` to be dropped from the CDO-mutating suites. Declined with engine-source evidence: the project's only gate runs with `GIsEditor && !IsRunningCommandlet()`, so a `CommandletContext`-only suite is never collected and would vanish silently rather than fail | Once a non-CDO fixture exists the flags can be revisited **together with** the harness fix. Until then, do not drop `EditorContext` from any suite in this project without first proving, with a `RunFilter` log line, that the suite is still collected |

### TRACK-002 — findings inherited from TRACK-001

Read before writing TRACK-002's acceptance criteria.

| ID | Finding | What TRACK-002 must do |
| --- | --- | --- |
| Graybox level (director ruling, 2026-08-17) | The minimal graybox test level in `Content/Tracks/Prototype/Maps/` with one placed closed-loop `ATrackDefinitionActor` was an acceptance criterion of TRACK-001 and was **not** authored. The director deferred it **into TRACK-002's scope** rather than dropping it, because TRACK-002 must place checkpoint gates in a level anyway and should own the map's authoring, binary-asset ownership serialisation and license posture as one unit | **Author the level as part of TRACK-002's own scope.** Primitive/placeholder geometry only, no license-ledger-requiring external asset, one placed `ATrackDefinitionActor` instance. Take explicit asset ownership before touching any `.umap`/`.uasset` (`CLAUDE.md`: content changes are serialised) |
| M5 (TRACK-001 pass 1) | `ATrackDefinitionActor::PostLoad()` calls `RebuildTrackData()`, which reads `USplineComponent` geometry, but component `PostLoad` ordering relative to the owning actor's is not guaranteed by the engine. With no placed instance in the repo, the load path has never run | Once the level exists, add a functional test that loads it and asserts the track baked correctly from `PostLoad` alone (no `OnConstruction`, no `BeginPlay` rebuild). If ordering proves unsafe, move the bake to `PostRegisterAllComponents` or a deferred tick |
| L2 (TRACK-001 pass 1) | `FTrackCenterline::GetSampleSpacingCm()` returns `TotalLengthCm / NumSegments()` — the **average** segment length — while its comment says "spacing the samples were baked at". `Build()` explicitly permits non-uniform sample distances, so the two differ for any centerline not baked by `RebuildTrackData` | Either rename to `GetAverageSampleSpacingCm()` or add a true max-segment-length accessor. TRACK-002 needs the **maximum** segment length, not the mean, to bound gate-placement error |
| L4 (TRACK-001 pass 1) | `FMath::CeilToInt32(SplineLengthCm / SpacingCm)` overflows for absurd authored spacings. The result degrades safely (clamped by `MaxGeneratedSamples`), but by integer wraparound plus `FMath::Max`, not by the mechanism the comment claims | Tighten the guard to compute the sample count in `double` and compare before casting, so the documented mechanism is the real one |
| L7 (TRACK-001 pass 1) | `FTrackCenterline::ProjectOntoSegments` computes `Right = Up x Forward`; for an exactly vertical segment this normalises to zero and the query returns `LateralOffsetCm == 0` with `bValid == true` — indistinguishable from a car dead on the centerline. `GetTransformAtDistanceCm` documents this degenerate case; `ProjectOntoSegments` does not | Either signal the degenerate case (`bValid = false`, or a dedicated flag) or document it identically. This matters once track-limits checks read `LateralOffsetCm` as truth |
| Polyline bias (TRACK-001 counter-case) | Baked-polyline positions sit inside the true spline curve by roughly `Spacing^2 / (8 * radius)`. Sub-centimetre at the default 100 cm spacing, but systematic and one-directional | Assert the bias explicitly against a closed-form circle before comparing a car's lateral offset to an authored track-limits threshold, rather than inheriting it silently |

### RACE-003 — findings inherited from TRACK-001

| ID | Finding | What RACE-003 must do |
| --- | --- | --- |
| M7 (TRACK-001 pass 1) | `ATrackDefinitionActor::RebuildTrackData()` and `Validate()` disagree by design: the bake substitutes fallbacks (100 cm spacing, 800 cm grid spacing, 2500 cm reset spacing, clamped sample count) for exactly the values `Validate()` rejects outright. A track can therefore bake "successfully" and still be unpublishable, and there is no cheap cached-validity flag — `Validate()` re-runs the whole check, allocates `FString`s and is not callable per frame | Cache the validation result (with the content hash it was computed against) so a race director can cheaply refuse to start a session on an invalid track, instead of either re-validating per frame or trusting `IsTrackDataBuilt()`, which is a strictly weaker claim |

### VEH-005 — findings inherited from TRACK-001

| ID | Finding | What VEH-005 must do |
| --- | --- | --- |
| L5 (TRACK-001 pass 1) | `ATrackDefinitionActor::MakePoseAtDistance` lifts every reset pose by a fixed `PoseHeightOffsetCm` along the pose's own up axis, with **no ground trace**. On a crested, banked or elevation-changing section the pose can sit well above the road (car drops and is damaged/destabilised) or, if the polyline chord cuts under a crest, inside it | Ground-trace from the returned reset transform before teleporting, and treat `ATrackDefinitionActor`'s pose as a *seed* rather than a final placement. `GetResetSampleDistanceCm()` (added in TRACK-001 repair cycle 1, H2) gives the arc-length distance to re-seed `FindNearestNear` after the teleport, so the post-reset progress hint is not stale |

### UI-001 — findings inherited from TRACK-001

| ID | Finding | What UI-001 must do |
| --- | --- | --- |
| L3 (TRACK-001 pass 1) | `FTrackCenterline` and `FTrackCenterlineQuery` (`Source/RacingSim/Race/TrackCenterline.h`) are `USTRUCT(BlueprintType)` but every query is a plain-C++ member function, and a `USTRUCT` member function cannot be a `UFUNCTION` — so none of the centerline query API is reachable from Blueprint or UMG. Same class of gap as CORE-002's M-3 | Extend the same fix: a `BlueprintPure` function library wrapping the query surface the HUD actually needs (progress distance, lateral offset, sector). Keep it beside the type it wraps (`Source/RacingSim/Race/`), not in `UI/`, for the reason CORE-002's M-3 row gives |

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

### UI-001 — findings inherited from CORE-002

Raised by `code-reviewer` against CORE-002 (`Source/RacingSim/Core/RacingTelemetry.h`,
`RacingSimUnits.h`), deferred here because no Blueprint/UMG consumer existed yet at
CORE-002 to validate the wrapper shape against. Read before writing UI-001's acceptance
criteria:

| ID | Finding | What UI-001 must do |
| --- | --- | --- |
| M-3 (pass 1) | `RacingTelemetry.h`/`RacingSimUnits.h` structs are `USTRUCT(BlueprintType)` but their member functions (`GetForwardSpeedKph/Mph/MetresPerSecond`, `IsStaleAt`, `AreSectorsConsistent`, `IsComplete`, `IsPopulated`, `ToString`, unit conversions) are plain C++ — a `USTRUCT` member function cannot be a `UFUNCTION`, so none of this is reachable from Blueprint/UMG despite the HUD being UMG per `CLAUDE.md` | Add a `URacingTelemetryFunctionLibrary` with `BlueprintPure` wrappers. Keep it in `Source/RacingSim/Core/` even though this ticket owns the work — it wraps *Core* contracts (unit conversions, staleness), and putting it in `UI/` would give CORE-002's unit-conversion policy a second home |
| M-4 (pass 1) | `FRacingTelemetryFrame` (`RacingTelemetry.h`) embeds two `FRacingLapTiming`, each carrying a `TArray<double> SectorDurationsSeconds` — copying "the single frame the HUD is allowed to read" heap-allocates twice per copy, and `CLAUDE.md` forbids per-frame allocation | Either document that frames are passed by `const&` and never copied per tick, or replace the `TArray` with a `TArray<double, TInlineAllocator<N>>` sized to the real sector count once track data exists |

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
