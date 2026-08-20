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
| TRACK-002 | Ordered checkpoint gates and crossing direction | race-systems-engineer | TRACK-001 | B | **DONE** 2026-08-20 — `code-reviewer` returned CHANGES REQUESTED against `dc96061` (1 HIGH + 7 MEDIUM + 5 LOW, four blocking); repair cycle 1 closed all four blocking findings (H1: `MinCheckpointGateCount = 4` enforced in `Validate()` against both the generated and hand-authored gate paths; M6, L1, L3: documentation/process corrections); re-review returned APPROVED WITH FOLLOW-UPS. `test-engineer` independently confirmed both targets build clean with zero warning/error matches, Smoke `passedTotal=466, failed=0, notRun=0` across 40 `RacingSim.*` suites, and all three level tests pass (3/0/0). Merged to `main` at `00ad83b` (merge of `5c3165b`). Non-blocking findings (M2–M5, M7, L2, L4–L6) and two open risks (gate-order floor bounds "no order" but not shortcut-proofing; graybox level has no drivable surface) tracked forward into `RACE-002`, `RACE-003`, `VEH-002`, `TEST-001` |
| RACE-001 | Race state machine and monotonic clock | race-systems-engineer | CORE-002 | B | **DONE** 2026-08-14 — `code-reviewer` approved across two passes at `7832d0a`; `test-engineer` independently confirmed both targets build clean from a from-scratch rebuild and 442/442 automation Smoke tests pass. Merged to `main` at `2c41989`. Two findings (M4, M1's accepted risk) tracked forward into `RACE-002` |
| RACE-002 | Lap/sector/progress/validity logic | race-systems-engineer | TRACK-002, RACE-001, CORE-003 | B | **IMPLEMENTED 2026-08-20, awaiting review + validation** — all 15 acceptance criteria met on branch `worktree-agent-a2dcff27414a9c965`; both targets build with zero warning/error matches; Smoke `passedTotal=471, failed=0, notRun=0` (5 new suites); the 3 placed-level tests pass 3/0/0. Closes RACE-001 `M4`; TRACK-002 `M1`, `M3`, `M5`, `L2`, `L4`, `L5`, `R1-M2`, `R1-L1`, `R1-L2`, `R1-L3`; CORE-003 `C3-7`. `code-reviewer` and `test-engineer` have NOT run and it is NOT merged. See "RACE-002 — verification evidence, 2026-08-20" |
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
| M4 residual (TRACK-001 pass 1) | `TrackDefinitionActorSpec.cpp` mutates the `ATrackDefinitionActor` **class default object**, process-wide, because neither obvious way to instantiate an actor works in this harness: `NewObject<AActor>(GetTransientPackage())` dies in `CreateDefaultSubobject` on `Assertion failed: RegisteredElementType [TypedElementRegistry.h:536]`, and `UWorld::CreateWorld(EWorldType::Game)` dies with a bare access violation inside `CreateWorld`. The restore is now exhaustive and verified by `RacingSim.Race.TrackFixtureRestore`, but the right answer is not to mutate the CDO at all | **CAUSE FOUND AND LARGELY CLOSED BY `TRACK-002`, 2026-08-19.** The wall was never "this harness cannot make actors" — it was a **filter/phase** artifact. `FEngineLoop::PreInit` runs every `SmokeFilter` test itself (`LaunchEngineLoop.cpp:4376`), and `RegisterEngineElements()` — which registers the `Components` typed-element type — is not called until `UEngine::Init` (`UnrealEngine.cpp:2399`), *after* `PreInit` returns. `UActorComponent::PostInitProperties` creates an editor component element for every **non-template** component (`ActorComponent.cpp:588`), so a `SmokeFilter` test constructing any actor asserts. That is one cause for all three of TRACK-001's crashes, and it explains why the CDO worked: CDO subobjects are templates, which the element path skips. **A `ProductFilter` test runs after full init and can load a real actor.** `Source/RacingSimTests/Race/TrackPrototypeLevelSpec.cpp` does exactly that against the graybox level, and `Scripts/Test/Run-AutomationFilter.ps1 -Filter Product` is the second recorded gate. Recorded in `Docs/Environment.md`. **Residual:** this yields a *package-resident, read-only* actor, not a spawnable world — `UWorld::CreateWorld` was not retried and remains presumed broken. A ticket needing a **mutable** instance (`VEH-002`) should duplicate the loaded object, or re-test `CreateWorld` from a `ProductFilter` test now that the cause is understood |
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

### TRACK-002 — acceptance criteria, opened 2026-08-18

Scope per this row: `Ordered checkpoint gates and crossing direction`. Owner
`race-systems-engineer`. Gate B. Depends on `TRACK-001` (DONE). Read "### TRACK-002 —
findings inherited from TRACK-001" immediately above **first** — six concrete
obligations, not just a checkpoint system to build in the abstract.

**Deliberately lap-agnostic**, mirroring RACE-001/TRACK-001's split: lap counting,
sector timing, and validity/progress state are `RACE-002`, a later, separate ticket
that depends on this one. TRACK-002 defines what a checkpoint gate *is* and whether a
given crossing satisfies it — it must not count laps, own a timer, or reference
`ERaceState`.

> **Process correction, repair cycle 1 (code-reviewer finding L3).** On `main` all ten of
> these boxes are `[ ]`; the implementation commit ticked all ten itself. Per this
> project's own established ruling (`TRACK-001` finding H3, "an implementer may not
> reclassify its own acceptance criteria"), that is a process violation regardless of
> whether the underlying work was done — and here it was not entirely: criterion #1 was
> ticked while the gate contract still accepted a one-gate set that cannot enforce any
> order (finding H1). Criterion #1 is therefore reverted to `[]`. **Ticking it is the
> re-review's call, not the implementer's**, and it should be ticked only once H1's fix is
> independently verified. Criteria #2–#10 are left `[x]` because `code-reviewer`
> independently verified those against evidence in pass 1 — that is the gate exercising
> its own authority, which is exactly the distinction this correction is about.
>
> **Re-review, repair cycle 1.** `code-reviewer` independently verified H1's fix — the
> `MinCheckpointGateCount` floor is enforced in `Validate()` on both the generated and
> hand-authored entrances, closing the one-gate-validates-green defect — and marked H1
> CLOSED. Criterion #1 is ticked below on the re-review's authority, not the
> implementer's, per the distinction this correction exists to draw.

- [x] A typed, ordered checkpoint-gate contract (e.g. `FRacingCheckpointGate` /
      `UTrackCheckpointSet` — director's naming call, implementer may propose) built on
      `TRACK-001`'s typed centerline/query API (`ATrackDefinitionActor`,
      `FTrackCenterline`), never raw `USplineComponent` calls. Each gate has a position
      (arc-length distance along the centerline, consistent with `GetGridSlotDistanceCm`/
      `GetResetSampleDistanceCm`'s precedent), a width/extent, and a legal crossing
      direction.
- [x] Crossing-direction validation: given a gate and a crossing (e.g. two consecutive
      world positions, or a signed velocity), determine forward vs. reverse and report
      which — a reverse crossing must be distinguishable from a forward one, not merely
      rejected silently. `.claude/rules/race-tests.md`: "Checkpoint order plus crossing
      direction authorizes laps; spline distance alone never does" — this ticket owns
      the crossing-direction half of that rule; `RACE-002` owns the ordering half (it
      consumes this ticket's per-gate direction result to build lap validity).
- [x] Gate-placement geometry uses the **maximum** segment length, not
      `GetSampleSpacingCm()`'s average (`TRACK-001` `L2`), to bound placement error — add
      a true max-segment-length accessor to `FTrackCenterline` (or rename the existing
      one and add the real one; director's call which).
- [x] The polyline-vs-true-spline bias (`TRACK-001` counter-case, immediately above) is
      asserted explicitly against a closed-form circle fixture, not inherited silently —
      needed before any gate-crossing tolerance is chosen, since the bias is systematic
      and one-directional.
- [x] `TRACK-001` `L4` closed: `FMath::CeilToInt32(SplineLengthCm / SpacingCm)`'s
      sample-count guard computes in `double` and compares before casting, so the
      documented overflow-safety mechanism is the real one.
- [x] `TRACK-001` `L7` closed: `FTrackCenterline::ProjectOntoSegments`'s degenerate
      vertical-tangent case (`Right = Up x Forward` normalises to zero) either signals
      invalidity (`bValid = false` or a dedicated flag) or is documented identically to
      `GetTransformAtDistanceCm`'s handling of the same case — this ticket's own gate
      geometry is the first real consumer of `LateralOffsetCm` as ground truth.
- [x] A minimal graybox test level (`Content/Tracks/Prototype/Maps/`, director-approved
      deferral from `TRACK-001`) containing one closed-loop `ATrackDefinitionActor`
      instance and the checkpoint gates this ticket defines, using only
      primitive/placeholder geometry — no final art, no license-ledger-requiring
      external asset. Explicit asset ownership taken before touching any
      `.umap`/`.uasset` (`CLAUDE.md`: content changes are serialised). Sufficient for
      `RACE-002`'s automation to exercise real checkpoint/lap logic against.
- [x] `TRACK-001` `M5` closed: once the level exists, a functional test loads it and
      asserts the track baked correctly from `PostLoad()` alone (no `OnConstruction`, no
      `BeginPlay` rebuild) — the spawn/load path has never run because no placed
      instance existed before this ticket.
- [x] `RacingSimTests` gains automation coverage for crossing-direction validation
      (forward, reverse, tangential/grazing, high-speed single-tick crossing) that does
      **not** require the level — testable against a procedurally-constructed gate set
      in a transient world/commandlet, matching `TRACK-001`'s testability-first design.
      `.claude/rules/race-tests.md`: "Test reverse crossings, skipped gates, double
      overlaps, spins at gates, high-speed crossings, reset/teleport, and restart" — the
      skipped-gate/ordering and reset/restart cases are `RACE-002`'s to test against
      this ticket's contract, but reverse/double/spin/high-speed crossing-direction
      detection is this ticket's own.
- [x] Editor **and** Game targets build with zero new warnings.

**Known risk, not yet resolved.** `TEST-001`'s findings-inherited table records that no
project ticket has yet provided a working non-CDO actor-instantiation path for
automation (`NewObject<AActor>` and `UWorld::CreateWorld` both crash in this harness) —
`TEST-001` did not fix this; it was outside that ticket's N-2/N-4 scope. This ticket's
own functional-test-map criterion (`M5`, above) may resolve it naturally by providing a
real loaded level with a real placed actor instead of a CDO-mutating fixture — if so,
record that as the fix `TEST-001`'s finding was waiting for. If the functional-test
infrastructure itself hits the same wall, escalate rather than reintroducing a
CDO-mutating fixture for checkpoint-gate tests.

> **Resolved, 2026-08-19.** It did hit the same wall, and the wall turned out to have a
> door. Cause and fix are in the verification-evidence section below and in
> `Docs/Environment.md`; the `TEST-001` inherited-findings row above is updated. Short
> version: the crash is a **filter-phase** artifact, not a harness limitation —
> `SmokeFilter` tests run inside `FEngineLoop::PreInit`, before `UEngine::Init` registers
> the typed-element types that every non-template `UActorComponent` requires. A
> `ProductFilter` test runs after full init and loads a real placed actor without
> incident. No CDO-mutating fixture was introduced.

### TRACK-002 — verification evidence

Recorded by the implementer, 2026-08-19, from worktree
`.claude/worktrees/agent-a1bb698af8574b0ce` (branch `worktree-agent-a1bb698af8574b0ce`,
fast-forwarded onto the prior implementation commit `ed35d74` rather than reimplemented).

Build results were read from **each command's own captured stdout**, never from
`%LOCALAPPDATA%\UnrealBuildTool\Log.txt`, which is shared machine-wide and is overwritten
by other agents' concurrent builds. Automation counts were read from `index.json`, never
from a process exit code — which mattered twice here, since two runs exited non-zero with
no report at all.

#### Files changed

**Corrected in repair cycle 1 (code-reviewer finding M6).** The table first published here
was the **last commit's** file list presented as the whole ticket's, and silently omitted
seven files — including two new scripts, `Scripts/Test/Run-Smoke.ps1` and
`Scripts/Test/Build-Target.ps1`, that the evidence section cites by name while claiming
`Run-Smoke.ps1` was "deliberately untouched" (it did not previously exist; it was *created*
by this ticket). The list below is the ticket's full file set across all five
implementation commits, `f38e063`..`dc96061`, taken from
`git diff --name-status 4f4f0d1 dc96061`, plus the repair-cycle-1 commit.

| File | Status | Change |
|---|---|---|
| `Content/Tracks/Prototype/Maps/L_Meridian_Graybox.umap` | **New** | The graybox level. First binary asset in the repository; tracked by Git LFS. |
| `Content/Tracks/README.md` | **New** | What the level contains, its reference posture, how to regenerate it, ownership and originality. |
| `Scripts/Content/Author-PrototypeGrayboxLevel.py` | **New** | Idempotent editor-Python authoring script; the reviewable source of every authored number in the level. |
| `Scripts/Content/Author-PrototypeGrayboxLevel.ps1` | **New** | Wrapper; self-verifying against the script's own `AUTHORING_OK` marker. |
| `Scripts/Test/Build-Target.ps1` | **New** | Builds one target and greps **its own** stdout for warnings/errors, so a build result never comes from the machine-wide `UnrealBuildTool\Log.txt`. Omitted from the original table. |
| `Scripts/Test/Run-Smoke.ps1` | **New** | Gate 1. Runs the Smoke filter and reports from `index.json`, not from the exit code. Omitted from the original table, which wrongly described it as pre-existing and untouched. Repair cycle 1 **added** `succeededWithWarnings`/`passedTotal`/`testsInReport` output; no existing field removed or changed. |
| `Scripts/Test/Run-AutomationFilter.ps1` | **New** | Gate 2 (`-Filter`, or `-TestNames` for the actor-touching level tests). Same reporting addition in repair cycle 1. |
| `Source/RacingSim/Race/TrackCheckpointGate.h` | **New** | `FRacingCheckpointGateSpec`, `FRacingCheckpointGate`, `FRacingCheckpointGateSet`, `ERacingGateDirection`, `FRacingGateCrossingResult`. The ticket's core contract. Omitted from the original table. |
| `Source/RacingSim/Race/TrackCheckpointGate.cpp` | **New** | Gate bake (`Build`) and the crossing-direction query. Omitted from the original table. |
| `Source/RacingSim/Race/TrackCenterline.h` | Modified | `GetMaxSegmentLengthCm()`, `GetAverageSegmentLengthCm()` (renamed from the misleading `GetSampleSpacingCm`), `GetSagittaBoundCm()`, and the L7 degenerate-lateral-axis flag. Omitted from the original table. |
| `Source/RacingSim/Race/TrackCenterline.cpp` | Modified | Implementations of the above; max-segment cached at `Build()`. Omitted from the original table. |
| `Source/RacingSim/Race/TrackDefinitionActor.h` | Modified | Authored gate fields (`CheckpointGateSpecs`, `NumGeneratedCheckpointGates`, `GeneratedGateHalfWidth/HeightCm`, `MinCornerRadiusCm`), the gate query surface, `TrackSchemaVersion` 1 → 2, the `PostLoad` bake latch, and (repair cycle 1) `MinCheckpointGateCount` + `GetGeneratedGateClampNote()`. |
| `Source/RacingSim/Race/TrackDefinitionActor.cpp` | Modified | Gate bake and generator, gate hashing in `ComputeContentHash`, `Validate()`'s gate rules, `PostLoad()` latch, the `double`-domain sample-count guard (L4), and (repair cycle 1) the generator clamp's log/note plus `Validate()`'s gate-count floor. |
| `Source/RacingSimTests/Race/TrackCheckpointGateSpec.cpp` | **New** | Crossing-direction, gate-build and curved-track coverage. Omitted from the original table. |
| `Source/RacingSimTests/Race/TrackPrototypeLevelSpec.cpp` | **New** | Three `ProductFilter` tests against the placed, loaded track. |
| `Source/RacingSimTests/Race/TrackCenterlineSpec.cpp` | Modified | Max-segment, sagitta-bound and polyline-bias coverage. Omitted from the original table. |
| `Source/RacingSimTests/Race/TrackDefinitionActorSpec.cpp` | Modified | Gate fields added to the CDO fixture snapshot/restore; the coarse-bake block updated; (repair cycle 1) `RacingSim.Race.TrackCheckpointGateOrderFloor` added. Omitted from the original table. |
| `Docs/AssetOwnership.tsv` | Modified | Claims `Content/Tracks/Prototype/*` for `junyi` under TRACK-002. |
| `Docs/Environment.md` | Modified | The `Product` gate; the `SmokeFilter`-cannot-touch-actors rule; three editor-Python invocation traps; the config-rewrite hazard. |
| `Docs/Tickets.md` | Modified | This section, the checkboxes, and the `TEST-001` inherited-finding resolution. |

`Config/DefaultGame.ini` was **reverted, not committed** — see "Hazards" below.

#### Rollback

Reverting this ticket is **not** a plain `git revert` of the source commits. Two things
outlive the C++:

1. **`TrackSchemaVersion` 1 → 2** (`Source/RacingSim/Race/TrackDefinitionActor.h`). A
   revert takes it back to `1`, and any result already published carrying
   `SchemaVersion == 2` becomes unreadable-by-contract rather than merely stale: version 1
   and version 2 are declared non-comparable *because the gates decide which laps count*.
   Nothing publishes results yet (`RACE-003` is `OPEN`), so today the blast radius is
   zero — but a revert **after** `RACE-003` ships must either keep the version at 2 with a
   documented "2 means no gates again" note, or bump to 3. Do not silently reuse 1.
   The content hash also changes on revert (the gate fields drop out of
   `ComputeContentHash`), so every stored track hash is invalidated regardless.
2. **The `.umap` and its Git LFS object.**
   `Content/Tracks/Prototype/Maps/L_Meridian_Graybox.umap` is the repository's first binary
   asset. Confirmed LFS-tracked: `.gitattributes` carries
   `*.umap filter=lfs diff=lfs merge=lfs -text lockable`, and `git lfs ls-files` lists the
   file under oid `3c212e2fc0`. `git revert` removes the **pointer file** from the working
   tree; it does **not** remove the LFS object from `.git/lfs/objects`, and it does not
   release the `Docs/AssetOwnership.tsv` claim on `Content/Tracks/Prototype/*`. A full
   rollback therefore needs two steps the source revert will not do: (a) drop the ownership
   row from `Docs/AssetOwnership.tsv`, and (b) run `git lfs prune` if the object must not
   persist locally. This repository is local-only per `Docs/ADR/ADR-0004`, so there is no
   remote LFS store to clean today — once one exists, note that removing an object from it
   is a history rewrite and is **not** something a revert can or should do.
   The level can be regenerated exactly from
   `Scripts/Content/Author-PrototypeGrayboxLevel.ps1`, which is why the `.umap` is
   recoverable rather than precious; the authoring script, not the binary, is the source of
   truth. Verified idempotent: two consecutive runs produced identical length and gate
   distances.

Everything else — the new `Source/RacingSim/Race/TrackCheckpointGate.*` pair, the
centerline accessors, the test specs and the `Scripts/Test/*.ps1` harness — reverts
cleanly, with one ordering constraint: `Scripts/Test/Run-Smoke.ps1` and
`Scripts/Test/Build-Target.ps1` are **new in this ticket**, so reverting it removes the
project's only scripted build/test entry points. Any later ticket that starts citing them
must not be reverted across this one.

#### Binary-asset ownership

Taken before any `.umap` existed, per `CLAUDE.md` hard constraint #7. `find` across all 17
worktrees returned no `.uasset`/`.umap` anywhere, so no other agent held Unreal content.
Claim added to `Docs/AssetOwnership.tsv`; `.githooks/pre-commit` is active
(`core.hooksPath` set) and `git config user.name` is `junyi`, matching the claim.

#### Commands run, verbatim

```powershell
# Level authoring (run twice; second run is the recorded one)
pwsh -File Scripts\Content\Author-PrototypeGrayboxLevel.ps1

# Builds
pwsh -File Scripts\Test\Build-Target.ps1 -Target RacingSimEditor `
    -OutFile "<worktree>\Saved\TRACK002\build-editor-4.log" `
    -ProjectPath "<worktree>\RacingSim.uproject"
pwsh -File Scripts\Test\Build-Target.ps1 -Target RacingSim `
    -OutFile "<worktree>\Saved\TRACK002\build-game.log" `
    -ProjectPath "<worktree>\RacingSim.uproject"

# Gate 1 - Smoke
pwsh -File Scripts\Test\Run-Smoke.ps1 `
    -ProjectPath "<worktree>\RacingSim.uproject" `
    -ReportDir "<worktree>\Saved\Automation\Report"

# Gate 2 - the actor-touching level tests
pwsh -File Scripts\Test\Run-AutomationFilter.ps1 `
    -TestNames "RacingSim.Race.TrackPrototypeLevelPostLoad+RacingSim.Race.TrackPrototypeLevelIdentity+RacingSim.Race.TrackPrototypeLevelGates" `
    -ProjectPath "<worktree>\RacingSim.uproject" `
    -ReportDir "<worktree>\Saved\Automation\LevelReport"
```

#### Build results

- Editor (`RacingSimEditor Win64 Development`), final build `build-editor-4.log`:
  `BUILD_EXITCODE=0`, `Result: Succeeded`, **`WARNING_ERROR_MATCHES=0`**.
- Game (`RacingSim Win64 Development`), `build-game.log`: `BUILD_EXITCODE=0`,
  `Result: Succeeded`, **`WARNING_ERROR_MATCHES=0`**, 26 compile actions — a real
  recompile of the runtime module, not a no-op.
- Four editor builds were run in total (`build-editor-1..4.log`), all `Succeeded` with
  zero warning/error matches. Build 1 also proves the inherited `ed35d74` code compiles
  clean in a fresh worktree.

#### Automation results

- **Smoke** — `Saved/Automation/Report/index.json`:
  **`succeeded=462, failed=0, notRun=0`**, `reportCreatedOn=2026.08.19-06.29.07`,
  `PROCESS_EXITCODE=0`, `NON_SUCCESS_COUNT=0`. All **39** `RacingSim.*` suites `Success`,
  including this ticket's `GateCrossingDirection`, `GateSetBuild`, `GateCurvedTrack`,
  `CenterlinePolylineBias` and `CenterlineLateralAxis`.
- **Level tests** — `Saved/Automation/LevelReport/index.json`:
  **`succeeded=3, failed=0, notRun=0`**, `reportCreatedOn=2026.08.19-06.27.51`,
  `PROCESS_EXITCODE=0`. `TrackPrototypeLevelPostLoad`, `TrackPrototypeLevelIdentity`,
  `TrackPrototypeLevelGates`, all `Success`.

The level tests are **absent from the Smoke report by design** — they are `ProductFilter`.
Their collection under that filter was proven separately by a real
`Automation RunFilter Product` run, whose log shows the controller dispatching all three
(`Sending RunTest TrackPrototypeLevelGates`, `...Identity`, `...PostLoad`) — i.e. the
discoverability claim rests on a `RunFilter` line, per `Docs/Environment.md`'s rule, and
only the repeatable pass/fail evidence uses `RunTests`.

#### The authored level

`/Game/Tracks/Prototype/Maps/L_Meridian_Graybox`, 76,652 bytes. From the authoring
script's own report in `Saved/Logs/RacingSim.log`:

```text
[TRACK-002] Centerline spline length: 324466.9 cm (3.245 km)
[TRACK-002] Baked gates: 6
[TRACK-002]   gate 0 at 0.0 cm      gate 3 at 162233.5 cm
[TRACK-002]   gate 1 at 54077.8 cm  gate 4 at 216311.3 cm
[TRACK-002]   gate 2 at 108155.6 cm gate 5 at 270389.1 cm
[TRACK-002] Sectors: 3   Grid slots: 8   Reset samples: 129
```

3.245 km is inside `Docs/03-TrackRaceUI.md`'s 3-5 km brief. Two consecutive runs produced
**identical** length and gate distances, which is the idempotency claim actually
exercised rather than asserted. Contents: one `ATrackDefinitionActor`, one
`ADirectionalLight`, one `ASkyLight`, and **no third-party or licence-ledger-requiring
asset references** — no meshes, materials or textures, not even `/Engine/BasicShapes`.

> **Corrected in repair cycle 1 (code-reviewer finding L1).** This originally read "**no
> asset references of any kind**", which is not literally true. The `.umap` byte stream
> does reference `/Engine/EditorResources/LightIcons/S_LightError` (the editor billboard
> sprite the engine attaches to a light component) and
> `/Engine/Maps/Templates/OpenWorld` (the template the map was created from). Both are
> Epic engine content, both are editor-only, and neither is licensable third-party art —
> so the **licensing conclusion is unchanged**: no `Docs/13-AssetLicenseLedger.md` entry is
> required. The categorical claim was simply stronger than the evidence, and a categorical
> claim is exactly the kind that a later reader relies on without re-checking.

No license-ledger entry is required, and the level cannot acquire one by accident from
authored content. There is consequently no road
surface mesh; that is a deliberate deferral to the track-art ticket, approved by the
director before authoring.

#### Two real defects, both found by running

1. **`SmokeFilter` cannot touch an Actor — and this is TEST-001's open finding, solved.**
   The spec was written `SmokeFilter` and crashed the whole run on
   `Assertion failed: RegisteredElementType [TypedElementRegistry.h:536] Element type
   'Components' has not been registered!` — `PROCESS_EXITCODE=3`, **no `index.json` at
   all**. Re-running the single test alone by name reproduced it, ruling out cross-test
   contamination. Cause, from engine source: `FEngineLoop::PreInit` runs every
   `SmokeFilter` test itself (`LaunchEngineLoop.cpp:4376`), while `RegisterEngineElements()`
   is not called until `UEngine::Init` (`UnrealEngine.cpp:2399`); every non-template
   `UActorComponent` acquires an editor element in `PostInitProperties`
   (`ActorComponent.cpp:588`). One cause for all three of TRACK-001's crashes, and it
   explains why the CDO fixture worked — CDO subobjects are templates, which that path
   skips. Fix: `ProductFilter` + a second recorded gate.
2. **The original M5 assertion was unsound, and passing it would have been worse than
   failing.** The test first asserted `World->IsInitialized() == false` and
   `GetBakeAttemptCount() == 1`. Both failed: loading a map package **in the editor**
   initialises the world, registers components and re-runs `OnConstruction`, so the track
   bakes twice. Simply "correcting" the expectation to 2 would have produced a green test
   that asserted nothing about `PostLoad` — a failed `PostLoad` bake is silently repaired
   by the `OnConstruction` bake microseconds later, and every observable an outside test
   can reach reports the *second* bake. That is precisely the hazard M5 describes. So
   `ATrackDefinitionActor` now latches its load-time result in `PostLoad` itself, and the
   test asserts `DidPostLoadBakeSucceed()` with `GetPostLoadBakeAttemptIndex() == 1`.
   **M5's actual answer: the ordering is safe** — `FSplineCurves::ReparamTable` is a
   serialised `UPROPERTY` (`SplineComponent.h:95`) restored during `Serialize`, strictly
   before any `PostLoad`, and `USplineComponent::PostLoad` is `Super::PostLoad()` only
   (`SplineComponent.cpp:672`). No move to `PostRegisterAllComponents` was needed.

#### Hazards found, recorded in `Docs/Environment.md`

- `-ExecCmds="py <path with spaces>"` silently truncates at the first space; the 8.3 short
  path fixes that and breaks the `.py` extension match, which is case-sensitive. Both
  produce the same misleading `SyntaxError`. Fix: short-path the **directory**, keep the
  real filename.
- A bare `Quit` in `-ExecCmds` does not exit the editor (unlike `Automation ...; Quit`,
  where both halves are Automation subcommands). Two runs hung and were killed by PID; the
  script now calls `unreal.SystemLibrary.quit_editor()` in a `finally`.
- `unreal.log()` emits at **Log** verbosity and `-stdout` forwards **Display** and above,
  so the authoring script's entire report never reaches captured stdout. The first
  wrapper read stdout only and reported "the map was not authored" for a run that had
  authored it perfectly.
- **The headless editor rewrote `Config/DefaultGame.ini` on shutdown**, adding placeholder
  `GeneralProjectSettings` keys and **deleting the ~30-line AssetManager comment block**
  documenting BLOCKER-005 and why `bIsEditorOnly=True` is load-bearing. The functional
  settings survived, so the diff reads like harmless tool noise and would very plausibly
  be committed. Reverted with `git checkout --`; the restored file is what is committed.

#### Open risks

- **`Automation RunFilter Product` cannot complete on this machine.** It reaches
  `System.Plugins.PixelStreaming2.FPS2DataChannelEchoTest`, which under `-nullrhi` reports
  "No streamer factory implementation for DefaultRtc found" and then dies on
  `Assertion failed: IsValid() [Templates/SharedPointer.h:1133]`, producing no report. The
  crash is an engine/plugin defect in a headless configuration, unrelated to this ticket,
  but it means the Product gate must name its tests until PixelStreaming's suite is fixed
  or excluded. `STREAM-001` should be aware that these tests do not pass headless today.
- **No negative control for the M5 failure mode.** `DidPostLoadBakeSucceed()` returns
  true, but no test forces a `PostLoad` bake to fail, so the assertion's teeth are
  argued from the engine-source reading above rather than demonstrated. Injecting that
  failure needs a spline whose `PostLoad` is deliberately deferred, which the engine does
  not offer a hook for.
- **The level tests are read-only by necessity.** A package-loaded actor is shared and
  stays resident, so mutating it would leak across tests. A ticket needing a mutable
  instance (`VEH-002`) must duplicate the loaded object, or re-test `UWorld::CreateWorld`
  from a `ProductFilter` test — plausibly fixed by the same phase change, but untested.
- **Content-hash coverage of the new latch.** `PostLoadBakeAttemptIndex` and
  `bPostLoadBakeSucceeded` are `Transient` and derived, so they are correctly outside
  `ComputeContentHash()`; no schema bump was needed. `TrackSchemaVersion` stays at 2.
- **Two gates now exist and both must be run.** A future ticket reporting only Smoke
  counts is reporting a subset of this project's tests.

### TRACK-002 — repair cycle 1, `code-reviewer` pass 1 blocking findings

`code-reviewer` returned **CHANGES REQUESTED** against `dc96061`. This cycle closes the
four blocking findings and **nothing else**: M2–M5, M7, L2, L4–L6 were marked non-blocking
and stay open, batched forward to `RACE-002`/`RACE-003`/`VEH-002`/`TEST-001`.

| ID | Severity | Status |
|---|---|---|
| H1 | HIGH | **Closed.** A gate set too small to enforce order is now refused by `Validate()`. See below. |
| M6 | Docs, blocking | **Closed.** "Files changed" table corrected to the ticket's real file set; rollback section added. |
| L1 | Docs, blocking | **Closed.** The "no asset references of any kind" claim narrowed in `Docs/Tickets.md` and `Content/Tracks/README.md`. |
| L3 | Process, blocking | **Closed.** Acceptance criterion #1 reverted to `[ ]`; ticking it is the re-review's call. |

#### How H1 was closed

**The defect.** `Validate()`'s only gate-count check was
`FRacingCheckpointGateSet::IsValid()`, which means "at least one gate". A one-gate track
therefore validated green. With one gate there is no order to be out of and no shortcut is
detectable, so `CLAUDE.md`'s "ordered checkpoint gates plus a valid crossing direction" had
lost its ordering half entirely — and the track looked perfectly healthy from a HUD.

**It was reachable without anyone authoring it.** `MakeGeneratedGateSpecs` clamps its gate
count down to `floor(L / (2 * MaxSegment))`. At the coarsest bake `Validate()` permits
(`CenterlineSampleSpacingCm >= L/3`, which floors the bake at three samples and therefore
`L/3` segments) that expression is `floor(1.5) == 1`. A large `MinCornerRadiusCm` shrinks
the sagitta below the gate half-width so the one gate bakes cleanly. The reviewer's repro
was **already in the test suite**: `RacingSim.Race.TrackValidation`'s coarse-bake block
sets exactly that pair and asserted `Validate() == true`.

**Approach taken: reject in `Validate()`, and make the clamp testify.** Both halves were
needed, and the choice is not arbitrary:

- *Why not "make the bake fail" alone.* The generator is only one of two entrances. An
  author writing a two-gate `CheckpointGateSpecs` array by hand never invokes the
  generator, so no amount of generator hardening rejects that track — and it is the same
  defect. Only a floor on the **baked** set covers both. This is asserted directly by the
  new test's negative control, which authors two geometrically impeccable gates and
  requires the rejection to name the order rule rather than a bake failure.
- *Why the bake still must not fail.* This file's established design is that a bake never
  fails on a value `Validate()` will reject, so progress and ranking survive a
  mis-authored gate set. Making the generator refuse to emit would take the centerline
  down with it.
- *Why the floor is 4, not 3.* Two thresholds exist and they differ: `>= 2` is where an
  order exists at all, `>= 4` is where a shortcut across the middle of a circuit becomes
  detectable (with gates only at `0` and `L/2`, a car can reach the far gate, turn round
  across the infield, cross the line forwards and be credited a lap half the circuit's
  length). No finite count forbids every shortcut, so the floor is a policy choice —
  and **4 is the number `ATrackDefinitionActor.h` already documented** for
  `NumGeneratedCheckpointGates` and ships as its default. Enforcing 4 removes a
  contradiction between the header and the code; picking 3 would have added a third
  number.

Changes, all in `Source/RacingSim/Race/TrackDefinitionActor.{h,cpp}`:

1. `static constexpr int32 MinCheckpointGateCount = 4`, with the reasoning above recorded
   on it, including why the floor is enforced in `Validate()` (a race rule) and not in
   `FRacingCheckpointGateSet::Build()` (geometry) — the same split the Reverse-only
   start/finish check already uses.
2. `Validate()` rejects `BakedCheckpointGates.NumGates() < MinCheckpointGateCount`, after
   the bake-error branch so a set that failed to build still reports the build reason.
3. `Validate()`'s `NumGeneratedCheckpointGates` check moved from `< 1` to
   `< MinCheckpointGateCount`, and scoped to the case where the generator is actually in
   use — rejecting an inert field would be a false failure.
4. **The clamp now logs and records.** `MakeGeneratedGateSpecs` became non-const and sets
   `GeneratedGateClampNote` (new `Transient` member, new
   `GetGeneratedGateClampNote()` accessor) whenever it reduces the count, naming the
   requested count, the produced count, the max segment, the lap length and the effective
   step. It also emits one `LogRacingRace` warning per bake. `Validate()` appends the note
   to its failure reason, so the message names the **coarse bake** as the cause rather
   than blaming a count the author never typed.
5. `NumGeneratedCheckpointGates`'s `ClampMin`/`UIMin` metadata raised from `1` to `4`, so
   the editor cannot author below the floor in the first place.

**Test changes.**

- `RacingSim.Race.TrackValidation`'s coarse-bake block previously asserted
  `GetNumCheckpointGates() >= 1 && < NumGeneratedCheckpointGates`, which **passes on
  exactly one gate** — it pinned the bug as correct. It now asserts the count is
  *exactly* 1 (a range that includes the broken value is how this survived review once),
  that the clamp note is populated and names the requested count, that `Validate()`
  **fails**, and that the reason contains both the gate floor and the clamp's explanation.
  The block's original purpose is preserved and strengthened: the `NumSegments() < 3`
  guard is now asserted not to fire by checking the reason does **not** contain
  `"too coarse to query"`, which is a stronger statement than the old `Validate() == true`
  (that assertion would have gone green for any reason at all).
- A positive control was added to the same block: at the authored spacing the generator
  places every requested gate, clamps nothing, and the track validates. Without it the two
  rejections would be satisfied by a `Validate()` that had simply stopped returning true.
- **New test `RacingSim.Race.TrackCheckpointGateOrderFloor`** (`SmokeFilter`), covering
  both entrances: the generator-knob path, the hand-authored two-gate negative control
  (asserts the set *builds*, reports two gates, nothing clamped — and is refused anyway,
  on the order rule and not on geometry), the one-gate case finding H1 reported, and the
  exactly-at-the-floor case, which must be **accepted** so the check is a floor and not a
  ban. The floor is read from `ATrackDefinitionActor::MinCheckpointGateCount` rather than
  hard-coded, so editing the constant to 1 fails the test instead of silently passing it.

**Not changed, deliberately.** `TrackSchemaVersion` stays at **2**. `MinCheckpointGateCount`
is a compile-time validation threshold, not authored data — it is absent from
`ComputeContentHash()` for the same reason `Validate()`'s other thresholds are, and no
existing track's hash moves. The graybox level authors six gates and is unaffected.

#### A defect this cycle introduced and then caught by running

The first draft of the H1 fix logged the clamp **unconditionally**, and the Smoke run
reported **nine** suites as `succeededWithWarnings` where the baseline had three (four
warnings across those three suites). The message was the new clamp warning at a
**199.999985 cm lap** — i.e.
`USplineComponent`'s default **two-point, 200 cm** spline, which is what a freshly placed
`ATrackDefinitionActor` has and what the CDO fixture restores on teardown.

That bake **succeeds** (a closed loop floors at three samples), so `LogBakeFailure`'s
one-shot suppression never sees it, and 200 cm of track supports
`floor(200 / (2 * 66.7)) == 1` gate. So the clamp fired on every `OnConstruction` and every
`PostEditChangeProperty` for as long as it takes somebody to draw a circuit — reintroducing,
in a new place, exactly the log-flood defect `TRACK-001` finding H1 fixed for bake failures.

Fixed by routing the log through the same discipline as `bBakeFailureLogged`: a new
`bGateClampLogged` one-shot, re-armed by a bake that places every requested gate.
**`GeneratedGateClampNote` is still recorded on every bake**, so `Validate()` loses no
information — only the repeated log line is suppressed, which is the documented reason
`CLAUDE.md`'s "no warning suppression without a documented reason" asks for. Verified by
re-running: `TrackValidation` 6 → 4 warnings, `TrackFixtureRestore` 2 → 1. The residual
warnings are one per genuine full-set → clamped-set transition, which is the intended
behaviour.

#### Repair cycle 1 — commands run, verbatim

```powershell
# Builds. This worktree was FRESH (no Binaries/, no Intermediate/), so both builds are
# from scratch by construction rather than by a -Clean flag.
powershell -NoProfile -ExecutionPolicy Bypass -File "Scripts\Test\Build-Target.ps1" `
    -Target RacingSimEditor -OutFile "<worktree>\Saved\TRACK002R1\build-editor-2.log" `
    -ProjectPath "<worktree>\RacingSim.uproject"
powershell -NoProfile -ExecutionPolicy Bypass -File "Scripts\Test\Build-Target.ps1" `
    -Target RacingSim -OutFile "<worktree>\Saved\TRACK002R1\build-game-2.log" `
    -ProjectPath "<worktree>\RacingSim.uproject"

# Gate 1 - Smoke
powershell -NoProfile -ExecutionPolicy Bypass -File "Scripts\Test\Run-Smoke.ps1" `
    -ProjectPath "<worktree>\RacingSim.uproject" `
    -ReportDir "<worktree>\Saved\Automation\Report"

# Gate 2 - the actor-touching level tests
powershell -NoProfile -ExecutionPolicy Bypass -File "Scripts\Test\Run-AutomationFilter.ps1" `
    -TestNames "RacingSim.Race.TrackPrototypeLevelPostLoad+RacingSim.Race.TrackPrototypeLevelIdentity+RacingSim.Race.TrackPrototypeLevelGates" `
    -ProjectPath "<worktree>\RacingSim.uproject" `
    -ReportDir "<worktree>\Saved\Automation\LevelReport"
```

`pwsh` (PowerShell 7) is **not installed on this host**; only Windows PowerShell 5.1
(`C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe`) is present. The earlier
evidence section's `pwsh -File ...` lines therefore cannot be replayed verbatim; the
scripts themselves are 5.1-compatible and were run as above. Recorded so a re-reviewer does
not waste a cycle on a command that cannot run here.

#### Repair cycle 1 — build results

Read from each command's own captured stdout, never from the machine-wide
`%LOCALAPPDATA%\UnrealBuildTool\Log.txt`.

| Target | Log | Exit | Result | Warning/error matches | Compile actions |
|---|---|---|---|---|---|
| `RacingSimEditor Win64 Development` | `Saved/TRACK002R1/build-editor.log` | 0 | `Result: Succeeded` | **0** | 32 |
| `RacingSim Win64 Development` | `Saved/TRACK002R1/build-game.log` | 0 | `Result: Succeeded` | **0** | 14 |
| `RacingSimEditor Win64 Development` (after the log fix) | `Saved/TRACK002R1/build-editor-2.log` | 0 | `Result: Succeeded` | **0** | — |
| `RacingSim Win64 Development` (after the log fix) | `Saved/TRACK002R1/build-game-2.log` | 0 | `Result: Succeeded` | **0** | — |

The **Game** target genuinely recompiled the changed runtime sources rather than no-opping —
its log contains `[22/26] Compile [x64] TrackCheckpointGate.cpp` and
`[23/26] Compile [x64] TrackDefinitionActor.cpp`, plus
`[Adaptive Build] Excluded from RacingSim unity file: TrackDefinitionActor.cpp`. That was
the reviewer's specific concern about this file living in the Game target.

#### Repair cycle 1 — automation results

- **Smoke** — `Saved/Automation/Report/index.json`, `reportCreatedOn=2026.08.19-10.35.52`:
  **`succeeded=457, succeededWithWarnings=9, passedTotal=466, failed=0, notRun=0`**,
  `testsInReport=466`, `PROCESS_EXITCODE=0`, `NON_SUCCESS_COUNT=0`. All 466 tests report
  state `Success`. **40** `RacingSim.*` suites (39 before; `+1` is this cycle's new
  `RacingSim.Race.TrackCheckpointGateOrderFloor`), all `Success` — including
  `TrackValidation`, whose coarse-bake block was rewritten.
- **Level tests** — `Saved/Automation/LevelReport/index.json`,
  `reportCreatedOn=2026.08.19-10.34.34`: **`succeeded=3, failed=0, notRun=0`**,
  `PROCESS_EXITCODE=0`. `TrackPrototypeLevelPostLoad`, `TrackPrototypeLevelIdentity`,
  `TrackPrototypeLevelGates`, all `Success` — so the placed six-gate graybox track still
  validates under the new floor.

**On the count moving 462 → 457, which is not a regression.** `succeeded` is *not* the pass
count: the report splits passes into `succeeded` and `succeededWithWarnings`, and a test
that passes every assertion but emits one `UE_LOG(Warning)` moves between the two buckets.
The baseline total was **465** (462 + 3); this cycle's total is **466** (457 + 9) — up by
exactly one, which is `RacingSim.Race.TrackCheckpointGateOrderFloor`, the one new test this
cycle added. `failed` and `notRun` are both **0** in both runs, and every test's `state` is
`Success`. `Scripts/Test/Run-Smoke.ps1` and `Scripts/Test/Run-AutomationFilter.ps1`
printed only `succeeded`, which is what made a bucket shift look like lost coverage; both
now also print `succeededWithWarnings`, `passedTotal` and `testsInReport`. No existing field
was removed or changed meaning, so evidence recorded by CORE-002/TRACK-001/TEST-001 against
`Run-Smoke.ps1` stays verifiable.

#### Open risks from this cycle

- **The floor is a policy number, and it is the strongest counter-case against this fix.**
  Four gates do not make a circuit shortcut-proof; they bound the longest undetectable cut
  to roughly the chord across one quarter-lap arc, which on the 3.245 km graybox is still
  about 800 m of infield. `MinCheckpointGateCount` buys "order is enforceable at all", not
  "shortcuts are impossible". A track wanting real shortcut resistance needs gate spacing
  derived from its own geometry — which is `RACE-002`'s problem once it owns the ordering
  half, and this constant should be revisited there rather than treated as settled.
- **Raising `ClampMin` to 4 does not retro-fix existing content.** `ClampMin` constrains
  the editor UI only; a `.umap` already saved with a lower value, or a value set from
  Python via `set_editor_property`, still loads. `Validate()` is the real guard. No such
  content exists today (the only placed track authors six gates), but the metadata should
  not be mistaken for enforcement.

### RACE-002 — findings inherited from TRACK-002

`code-reviewer`'s two TRACK-002 passes (pass 1 against `dc96061`, re-review against
`ab94902`) marked these non-blocking and routed them here. This table is the merge
condition the re-review named as "must be recorded as inherited findings rather than
dropped" — read before writing `RACE-002`'s acceptance criteria.

| ID | Finding | What RACE-002 must do |
| --- | --- | --- |
| M1 (pass 1) | `FRacingCheckpointGateSet::Build()`'s "two gates cannot share a segment" invariant checks separation only against `InSpecs[Index-1]`, not across the loop closure — nothing stops an authored last gate from sitting one centimetre from gate 0, the near-identical-plane / coin-flip ordering case the check exists to prevent, at exactly the lap boundary. `Centerline.IsClosedLoop()` is already available at the call site. No test covers it today (the fixture is an open straight; the circle fixture is evenly spaced) | Add the wraparound check (cheap — a few lines), or accept it explicitly if `RACE-002`'s own gate-spacing derivation (see the H1 residual risk above) makes it moot by construction |
| M3 (pass 1) | `FindFirstGateCrossing` returns the earliest **plane** crossing, including `OutsideExtent` — a car that goes wide past gate 1 (`OutsideExtent`) then cleanly through gate 2 makes this Blueprint-exposed single-call API return gate 1, not the through-gate crossing. `EvaluateCrossings` is documented as the correct multi-gate answer, but `FindFirstGateCrossing` is the more tempting call for a first pass | Either add a `FindFirstThroughGateCrossing` variant, or make sure lap/order logic reaches for `EvaluateCrossings` and never `FindFirstGateCrossing` for anything order-relevant |
| M5 (pass 1) | `Docs/01-Architecture.md:75` still specifies `ATrackCheckpoint`: "ordered trigger gate with crossing direction and width/height", and `Docs/15-ProjectStructure.md:69` still lists `TrackCheckpoint.*`. TRACK-002 deliberately and correctly built a world-free struct (`FRacingCheckpointGate`) instead, per the ticket's own naming latitude — but neither doc was updated | **Must close before `RACE-002` reads either doc as the contract.** Update both to describe `FRacingCheckpointGate`/`FRacingCheckpointGateSet` before starting implementation |
| L2 (pass 1) | The graybox level (`L_Meridian_Graybox.umap`) has no geometry at all — no road surface, no collision. Correct and sufficient for TRACK-002 (every assertion there is an analytic centerline position), but `RACE-002`'s own automation is likely fine on the same basis; a Chaos-vehicle consumer is not | If `RACE-002`'s tests stay analytic (position/gate queries, no physics), this is a non-issue. Record explicitly if that assumption changes — this is a shared obligation with `VEH-002`, whichever ticket first needs a car to actually stand on the track |
| L4 (pass 1) | `ERacingGateCrossing::OutsideExtent` carries no forward/reverse — a near-miss is the shortcut signature this project's telemetry wants to act on, but the enum value alone doesn't say which direction it was. Recoverable from `SignedDistanceFromCm`/`SignedDistanceToCm` on the crossing result, so this is ergonomics, not missing data | Read direction off the signed-distance pair when logging/acting on an `OutsideExtent` result, or add a convenience accessor if that pattern recurs |
| L5 (pass 1) | `GetCheckpointGates()` → `EnsureTrackDataBuilt()` → `check(IsInGameThread())`, so every gate query through the actor is game-thread-only — even though `FRacingCheckpointGateSet` itself is world-free and safe to read from anywhere. Fine today (the segment-plane crossing test doesn't tunnel, so game-thread-per-frame is sufficient), but would assert if `RACE-002`/vehicle code ever evaluates crossings from a Chaos async/substep context | If crossing evaluation moves off the game thread, read gates via `FRacingCheckpointGateSet` directly rather than the actor accessor, or document the thread constraint at the call site |
| R1-M2 (repair cycle 1 re-review) | The automation warning baseline grew from 3 suites / 4 warnings to 9 suites / 13 warnings, purely as a CDO-fixture artifact: `FTrackSpecCircleFixture`'s ctor/dtor re-bakes a clean circle then the 200 cm default spline on every suite that uses it, and each transition fires the one-shot clamp log again because the one-shot re-arms on every full-set bake. Nothing fails, but every future reviewer re-triages nine warnings | Add an `IsTemplate()`/`HasAnyFlags(RF_ClassDefaultObject)` guard to suppress the clamp log for CDO/template instances, or demote that specific line to `Verbose` while keeping `GeneratedGateClampNote` (the structured note, not the log) at its current visibility |
| R1-L1 (repair cycle 1 re-review) | `MinCheckpointGateCount = 4` is not test-pinned at exactly 4 — the new test only asserts the floor is `>= 2`, so lowering the constant to 3 would still pass every existing case (1 and 2 are caught) | Add an assertion that the floor is specifically `>= 4`, with the shortcut-detectability rationale from the H1 residual-risk note above, or accept the gap explicitly if `RACE-002` replaces the constant with geometry-derived spacing anyway |
| R1-L2 (repair cycle 1 re-review) | `Validate()`'s `NumGeneratedCheckpointGates` check was relaxed from unconditional `< 1` to `CheckpointGateSpecs.Num() == 0 && < MinCheckpointGateCount` (correct — don't fail on an inert field when authored specs win), but the field still feeds `ComputeContentHash()` with no validation at all on the authored-specs path — an authored track can now carry an arbitrary or negative `NumGeneratedCheckpointGates` into the content hash | Either exclude the field from the hash when authored specs are in use, or keep a finiteness/`>= 1` sanity check on the inert path so the hash never covers an unvalidated value |
| R1-L3 (repair cycle 1 re-review) | `bGateClampLogged` is re-armed only by `MakeGeneratedGateSpecs`, which is skipped entirely on the authored path. A generated → authored → generated round trip leaves a second clamp unlogged. Benign (`GeneratedGateClampNote` is still recorded unconditionally and `Validate()` quotes it — only the repeated log line is affected), but the header's re-arm contract doesn't mention this case | One sentence in the `TrackDefinitionActor.h` re-arm contract documenting the gap, or reset the flag in `RebuildCheckpointGates()` alongside the note so the contract and the code agree |

### RACE-003 — findings inherited from TRACK-002

| ID | Finding | What RACE-003 must do |
| --- | --- | --- |
| M2 (pass 1) | `MinCornerRadiusCm` is a safety-critical authored number that is never cross-checked against the actual spline curvature, and is non-monotonic in its own guard: below `R = MaxSegmentLengthCm / π` the capped sagitta bound *decreases* as `R` shrinks, so an author setting a very small radius gets the **weakest** possible placement check, not the strictest. Two in-repo comments disagree about which direction is "safe" (`Author-PrototypeGrayboxLevel.py` vs. `TrackDefinitionActor.h`) | Reconcile the two comments, state the monotonic range explicitly, and raise `ClampMin` or add a `Validate()` check that `MinCornerRadiusCm > MaxSegmentLengthCm / π`. Ideally derive the minimum radius from the baked polyline itself and warn on disagreement with the authored value |
| M4 (pass 1) | `RebuildTrackData()` returns `true` even when the **gate** bake fails — `RebuildCheckpointGates()` records `CheckpointGateBakeError` and returns void, so the load-time bake-outcome latch TRACK-002 added for M5 (`DidPostLoadBakeSucceed()`) does not cover a gate-bake failure from `PostLoad`. Combined with this ticket's own still-open `M7` (a track can bake "successfully" and still be unpublishable, with no cheap cached-validity flag), the blast radius grew: gates now decide which laps count | Fold this into the existing `M7` fix: the cached-validity flag `RACE-003` builds must cover the gate-bake-failed case, not just the centerline-bake-failed case it was originally scoped for |

**M7 (TRACK-002 pass 1), routed to `TEST-001`'s follow-up:** `Scripts/Test/Run-Smoke.ps1`
and `Scripts/Test/Run-AutomationFilter.ps1` only `exit 1` on a missing `index.json` —
a run with real test failures still exits 0. Low cost, not yet fixed; a caller that
trusts the exit code over the parsed counts sees false green.

**L6 (TRACK-002 pass 1), no ticket owner yet:** `FRacingCheckpointGate::IsWithinExtent()`
is dead code — `EvaluateCrossing` re-implements the same comparison inline
(`TrackCheckpointGate.cpp`) to reuse already-computed offsets. Fix on any future touch
of that file: either make the inline path call the helper with the precomputed values,
or delete the helper.

### RACE-002 — acceptance criteria, opened 2026-08-20

Scope per this row: `Lap/sector/progress/validity logic`. Owner `race-systems-engineer`.
Gate B. Depends on `TRACK-002` (DONE), `RACE-001` (DONE), `CORE-003` (DONE) — unblocked.
Read the two "findings inherited" tables immediately above, and
`### RACE-002 — findings inherited from RACE-001` / `### RACE-002 — findings inherited
from CORE-003` earlier in this file, **first** — this ticket inherits eleven concrete
obligations across three prior tickets, not just lap logic to build in the abstract.

**Deliberately the ordering-and-timing half only**, consuming what TRACK-002 and
RACE-001 already built rather than re-implementing either. TRACK-002 defines what a
checkpoint gate *is* and whether a single crossing satisfies it; RACE-001 owns the
session state machine and the monotonic clock. RACE-002 is the layer that turns a
stream of per-gate crossing results into "is this lap valid, and how long did it take."
`RACE-003` (results, restart, metadata) and `UI-001`/`UI-002` (display) are later,
separate tickets that depend on this one.

- [x] A typed lap/progress tracker (director's naming call, implementer may propose —
      e.g. `URaceLapTracker`) consumes `FRacingCheckpointGateSet::EvaluateCrossings`
      for all order-relevant logic — **never `FindFirstGateCrossing`**, which returns
      the earliest plane crossing including `OutsideExtent` and would silently treat a
      near-miss as the through-gate event (closes TRACK-002 `M3`).
- [x] Forward crossing of the finish gate, after every ordered checkpoint gate has been
      crossed forward in sequence since the last finish crossing, increments the lap
      count exactly once.
- [x] A finish crossing with one or more ordered gates not yet crossed forward invalidates
      that lap (does not increment), and the invalidity reason names the specific
      skipped gate — not a generic "invalid lap" — per `.claude/rules/race-tests.md`:
      "Checkpoint order plus crossing direction authorizes laps; spline distance alone
      never does." This is the ordering half of that rule; TRACK-002 already closed the
      per-gate crossing-direction half.
- [x] Reverse crossing of the finish gate does not increment the lap and is reported as
      a distinct, named invalidity — not collapsed into the same reason as a skipped gate.
- [x] Re-crossing an already-satisfied gate before completing the next one in order does
      not advance progress or count twice (double-trigger case from `race-tests.md`).
- [x] A spin at a gate — alternating forward/reverse crossings on the same plane — nets
      to at most one forward advance of progress, consistent with TRACK-002's own
      `GateCurvedTrack` spin test (`no two consecutive crossings share a direction and
      the net is exactly one forward pass`); lap logic built on top must not re-derive a
      different, inconsistent net from the same crossing stream.
- [x] Sector timing: sector-boundary crossings (from `ATrackDefinitionActor`'s TRACK-001
      sector markers) are timed against `RACE-001`'s `FRaceClock` (monotonic,
      server-side per `CLAUDE.md`), producing per-sector durations that populate
      `FRacingLapTiming::SectorDurationsSeconds` (`CORE-002`) — `AreSectorsConsistent()`
      must hold for every completed lap this ticket produces.
- [x] Reset/teleport: after a reset, this ticket's progress state is reseeded using
      `GetResetSampleDistanceCm()` (TRACK-001) rather than trusting stale
      pre-reset progress, and the policy for whether a reset invalidates the
      **current in-progress lap** is decided and documented (not left implicit) —
      `race-tests.md` lists reset/teleport as a required test case.
- [x] Restart: a session restart clears lap/sector/progress state cleanly, with no stale
      gate-crossed flags surviving into the new session — `race-tests.md` lists restart
      as a required test case; `RACE-001`'s `L3` (`Restart` from `PreRace` bumps no
      session id) is relevant context, not this ticket's to fix, but the new session's
      lap state must not depend on that id changing.
- [x] `RACE-001` `M4` closed: `FRaceClock::Start()`/`Stop()`'s `bool` return is checked
      wherever this ticket calls it; a refused `Start` marks the run's
      `ERacingRunValidity` (Core, `CORE-002`) invalid rather than allowing a silent
      `0.000` result to reach a result later.
- [x] `Docs/01-Architecture.md` and `Docs/15-ProjectStructure.md` updated to describe
      `FRacingCheckpointGate`/`FRacingCheckpointGateSet` in place of the stale
      `ATrackCheckpoint`/`TrackCheckpoint.*` description — closes TRACK-002 `M5`, and
      must land before/at the start of implementation since this ticket reads those docs
      as the contract.
- [x] TRACK-002 `M1` (loop-closure gate-separation check missing in
      `FRacingCheckpointGateSet::Build()`) is either fixed as prep work for this ticket,
      or explicitly accepted with a written reason if this ticket's own gate usage makes
      the gap moot by construction — not silently inherited a second time.
- [x] Any new range-validated tunable this ticket adds (lap/sector tolerances, timing
      windows) follows `CORE-003`'s `EnforceRanges`/`Validate()` split (`C3-1`); if reused
      on a `UDataAsset` rather than a `config` object, the property-flag filter in
      `RacingSimValidation.cpp` is parameterised first (`C3-2` — **do not reuse the
      framework on a DataAsset without closing this**); any bound whose extreme value
      means "off"/"unbounded" rather than "least" is declared with `WithReplacement()`
      and the resulting behaviour is asserted, not just the field's range (`C3-3`).
- [x] Automation coverage (level-free where possible, matching TRACK-002's
      testability-first design) for the ordering/reset/restart cases
      `.claude/rules/race-tests.md` assigns to this ticket: skipped gates, double
      overlaps, spins at gates, reset/teleport, and restart. Reverse/grazing/high-speed
      per-gate crossing-direction detection is already covered by TRACK-002 and is not
      re-tested here.
- [x] Editor **and** Game targets build with zero new warnings.

**Deliberately excluded from this ticket's scope**, tracked forward rather than silently
assumed: results screen/metadata format and restart-flow UX (`RACE-003`); lap/sector/time
display (`UI-001`/`UI-002`); the net-client-authority automation test blocked on
`ARaceDirector` not existing yet (`RACE-001`'s `M1` accepted risk); and geometry-derived
checkpoint-gate spacing for real shortcut resistance (TRACK-002's `H1` residual risk —
this ticket may need to know the current floor is a policy constant, not shortcut-proof,
but redesigning gate placement is not in scope unless it blocks a criterion above).

### RACE-002 — verification evidence, 2026-08-20

Implementation branch `worktree-agent-a2dcff27414a9c965`. **Not merged**; `code-reviewer`
and `test-engineer` have not run. Every number below was read out of the artifact named
beside it, never from a process exit code (Docs/Environment.md).

**Files changed**

| File | Change |
| --- | --- |
| `Source/RacingSim/Race/RaceLapTracker.h` | NEW. `URaceLapTracker`, `ERaceLapInvalidReason`, `FRaceLapInvalidity`, `FRaceLapTrackerUpdate` |
| `Source/RacingSim/Race/RaceLapTracker.cpp` | NEW. Ordering, lap open/close, sector splits, reset policy, plausibility guard |
| `Source/RacingSimTests/Race/RaceLapTrackerSpec.cpp` | NEW. Five `SmokeFilter` suites |
| `Source/RacingSim/Race/RaceStateMachine.h/.cpp` | RACE-001 `M4`: `FRaceClock::Start()`/`Stop()` returns checked; `HasRaceClockFault()` latch, cleared by `Restart` |
| `Source/RacingSim/Race/RaceRulesetDataAsset.h/.cpp` | `bResetInvalidatesLap`; `RulesetSchemaVersion` 1 → 2; field added to `ComputeContentHash()` |
| `Source/RacingSim/Race/TrackCheckpointGate.h/.cpp` | TRACK-002 `M1`: loop-closure separation check in `Build()` |
| `Source/RacingSim/Race/TrackDefinitionActor.h/.cpp` | TRACK-002 `R1-M2` (no clamp log for a template/CDO), `R1-L2` (`NumGeneratedCheckpointGates >= 1` on the authored path), `R1-L3` (re-arm on the authored path + header contract) |
| `Source/RacingSim/Core/RacingSimBuildId.h` | CORE-003 `C3-7`: doc corrected to what the check actually asserts |
| `Source/RacingSimTests/Race/TrackCheckpointGateSpec.cpp` | Loop-closure case + open-centerline control for `M1` |
| `Source/RacingSimTests/Race/TrackDefinitionActorSpec.cpp` | `R1-L1`: floor pinned at `>= 4`, not `>= 2` |
| `Docs/01-Architecture.md`, `Docs/15-ProjectStructure.md` | TRACK-002 `M5`: `ATrackCheckpoint`/`TrackCheckpoint.*` replaced by the real types |

No `Content/` change, so no `Docs/AssetOwnership.tsv` claim was needed or taken.

**Commands run, and what they returned**

```powershell
Scripts/Test/Build-Target.ps1 -Target RacingSimEditor   # BUILD_EXITCODE=0, Result: Succeeded, WARNING_ERROR_MATCHES=0
Scripts/Test/Build-Target.ps1 -Target RacingSim         # BUILD_EXITCODE=0, Result: Succeeded, WARNING_ERROR_MATCHES=0
Scripts/Test/Run-Smoke.ps1                              # passedTotal=471 failed=0 notRun=0
Scripts/Test/Run-AutomationFilter.ps1 -TestNames <3 level tests>   # 3/0/0
```

Logs: `Saved/BuildLogs/RACE-002-Editor-final.log`,
`Saved/BuildLogs/RACE-002-Game-final.log`. Reports:
`Saved/Automation/RACE-002-Smoke-final/index.json`
(`reportCreatedOn 2026.08.20-06.24.33`),
`Saved/Automation/RACE-002-Level-final/index.json`
(`reportCreatedOn 2026.08.20-06.25.32`).

**These four artifacts were produced from the committed tree**, after `726c290`, not
from the working copy that first went green — an earlier passing run predated a
comment-only edit, and re-running was cheaper than reporting a number whose source had
since been touched. The earlier artifacts (`-2`, `-1`) are still on disk and agree.

`WARNING_ERROR_MATCHES=0` is the script's own grep for `warning|error` over the full
captured build output, so it covers UHT and the linker as well as the compiler.

**Smoke: 471 tests, 469 succeeded + 2 succeededWithWarnings, 0 failed, 0 notRun.**
Up from TRACK-002's 466 by the five new suites.
`RacingSim.Race.LapTrackerConfiguration`, `LapCleanLap`, `LapOrdering`,
`LapResetAndRestart`, `LapClockFault` all `Success` with `warnings=0 errors=0`.

**The warning baseline went DOWN, which is `R1-M2` measured rather than asserted:**
TRACK-002 left 9 suites / 13 warnings; this run has **2 suites / 3 warnings**
(`TrackFailedBakeIsNotRetried` 1, `TrackValidation` 2), both pre-existing and both
deliberate.

**Two defects the new tests found in the first implementation, fixed before this report**

1. `FRaceLapTrackerUpdate::GatesAdvanced` was clamped at zero per step, so a spin's
   rewind could not cancel its advance: `RacingSim.Race.LapOrdering` measured a net of
   **3** for a triple spin where TRACK-002's own crossing stream nets **1**. The field is
   now signed. This is exactly the "lap logic must not re-derive a different net" clause,
   caught by the test rather than by inspection.
2. The unannounced-teleport guard compared a **straight-line** step against **half a
   lap**. On the circular fixture the largest chord available is the diameter `2R`, which
   is `2/π ≈ 0.64` of that bound, so the guard could never fire — a car teleported to the
   exact opposite side of the circuit passed it. It now tests **arc travel > ¼ lap OR
   chord > ½ lap**, and the configuration suite pins `2R < chord bound` so a future edit
   cannot collapse the two tests back into one.

**How each criterion was met**

1. `URaceLapTracker::Advance` calls `FRacingCheckpointGateSet::EvaluateCrossings` and
   nothing else; `FindFirstCrossing`/`FindFirstGateCrossing` appear nowhere in
   `RaceLapTracker.cpp`. TRACK-002 `M3` closed.
2. `RacingSim.Race.LapCleanLap`: a clean lap closes once, counts once, opens the next
   once; a second clean lap takes the count to 2.
3. `RacingSim.Race.LapOrdering` drives wide of gate 2 (`OutsideExtent`, so not crossed),
   then crosses gate 3: `FRaceLapInvalidity{MissedCheckpoint, GateIndex 2, GateId
   "Gate.02"}`, `ToDebugString()` contains `Gate.02`, lap closes uncounted.
4. Reverse finish is `ERaceLapInvalidReason::ReverseFinishCrossing` →
   `InvalidReverseCrossing`, and first-fault-wins keeps it even though the same lap then
   also fails the all-gates check. Asserted distinct from the skip reason.
5. Double trigger: exercised through the reset path (reset behind a satisfied gate, drive
   over it again) — `GatesAdvanced == 0`, no lap closed, cursor unchanged.
6. Spin: five alternating crossings net exactly `+1`, gate satisfied, next gate expected,
   lap still clean and still countable.
7. Sectors: three splits per lap, all `> 0`, summing to `LapDurationSeconds` within
   `1e-9`; `AreSectorsConsistent()` asserted on every valid lap the suite produces.
   Boundary times are interpolated inside the step from `CrossingAlpha`-equivalent
   arc-length fractions against `FRaceClock`'s elapsed reading, so the splits telescope
   to the lap by construction.
8. Reset: `NotifyVehicleReset(pose, GetResetSampleDistanceCm()-style distance)` re-seeds
   both the previous position (so the next step cannot sweep gates across the teleport)
   and the arc-length hint. Policy is `URaceRulesetDataAsset::bResetInvalidatesLap`,
   **default true**, documented at the field and at the call site, and both settings are
   tested.
9. Restart: `ResetForNewSession()` clears every counter, flag, split and cached lap and
   is idempotent; the gate-flag array is cleared element-wise rather than reallocated. A
   restart the owner forgets to announce is caught by the session-id watch. Both paths
   asserted in `RacingSim.Race.LapResetAndRestart`.
10. RACE-001 `M4`: `CommitTransition` now separates "already running" from "refused" via
    `IsRunning()`, latches `bRaceClockFaulted`, and the tracker turns that into
    `ERacingRunValidity::InvalidIncomplete` for the run and every lap.
    `RacingSim.Race.LapClockFault` drives it with a NaN time source: a gate-perfect lap
    closes **uncounted** with a `0.000` duration and `IsComplete() == false`.
11. Docs updated (TRACK-002 `M5`); `Docs/01-Architecture.md` also gained the corrected
    data-flow chain and the two rules that constrain it.
12. TRACK-002 `M1` **fixed**, not accepted: `Build()` checks the wrap gap on a closed
    loop, with a test for the rejection, a boundary control and an open-centerline
    control proving the check does not apply where there is no wrap.
13. **No range-validated tunable was added**, so `C3-1`/`C3-2`/`C3-3` are not triggered —
    stated rather than assumed. The one new tunable is a `bool`, which has no range;
    the plausibility bounds are **derived from track length**, deliberately, so no
    authored number and no range table exist to drift. `C3-2`'s blocking precondition
    (the `CPF_Config` filter in `RacingSimValidation.cpp`) is therefore untouched and
    still open for whichever ticket first adds a *numeric* ruleset field.
14. Five level-free `SmokeFilter` suites cover skipped gates, double overlaps, spins,
    reset/teleport and restart, plus configuration guards and the clock fault.
15. Both targets, zero warning/error matches.

**Open risks**

- **A small unannounced teleport is not caught.** Under ¼ lap of arc and ½ lap of chord,
  an unannounced jump is indistinguishable from a hitch. `NotifyVehicleReset()` is the
  contract; the guard is defence in depth. `VEH-005` must call it.
- **Sector splits are detected in arc-length space**, not by a plane crossing. They can
  never authorise a lap (gates do that, and the suite proves distance alone counts
  nothing), but a step that crosses the start/finish line *and* a sector boundary in that
  order will drop that boundary's split; the lap then closes with an incomplete split set
  and is marked `TimingUnavailable` rather than silently reporting wrong splits. Needs a
  step of roughly a third of a lap, i.e. already teleport territory.
- **`ATrackDefinitionActor` has no sector-gate bake**, so sector timing inherits the
  arc-length caveat above rather than the segment/plane guarantee gates enjoy.
- **`MinCheckpointGateCount = 4` remains a policy constant, not shortcut-proofing**
  (TRACK-002 `H1`, out of scope here). `R1-L1` is now pinned at 4 by test.
- **No `ARaceDirector` exists**, so nothing yet owns a `URaceLapTracker` in a real
  session and RACE-001 `M1`'s net-client test is still blocked, as scoped.
- **`ERacingRunValidity` has no timing-fault enumerator**; a refused clock maps to
  `InvalidIncomplete` with the precise cause in `ERaceLapInvalidReason`. If `RACE-003`
  wants the distinction on a published result, that is a Core enum change it owns.

**Strongest counter-case against this design.** The finish line closes the lap in
progress *unconditionally* on a legal forward crossing, valid or not. A driver who cuts
the last corner therefore gets a lap boundary, a fresh timer and a clean next lap — and a
driver who spins on the line gets that lap voided by `ReverseFinishCrossing` even though
the spin netted no progress. The opposite policy (refuse to close an invalid lap) was
rejected because it produces one endless lap with a running timer and a HUD that never
advances, which is worse in the common case; but it means "laps completed" and "valid
laps completed" are two different numbers that both have to reach the UI, and `RACE-003`
must not conflate them.

**Rollback.** Every change is additive except the four inherited-finding fixes. Reverting
the branch restores TRACK-002's behaviour exactly; reverting only `RaceLapTracker.*` plus
the `RaceStateMachine`/`RaceRulesetDataAsset` edits leaves the `M1`/`R1-*`/`C3-7` fixes
standing, since they are independent commits-worth of change in separate files.

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
