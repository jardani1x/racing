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
| TEST-001 | Test module and first smoke test | race-systems-engineer (impl) + test-engineer (validation) | CORE-001 | A | **IMPLEMENTED, GATES PENDING** 2026-08-17 — branch `worktree-agent-a489f0e1e76ec91e0`, `40508ac..c5d0960`. Both targets `Result: Succeeded` with 0 `warning|error` matches; 445/0/0 automation Smoke; package `ExitCode=0`; N-2 enforcement driven to a real failure twice and reverted. **Not merged**; `code-reviewer` and `test-engineer` have not run |
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

Reported line numbers are approximate (`around line N`) because comments are stripped
before matching and block-comment stripping is not line-preserving. The file name is the
actionable part.

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

#### Check results

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

- Rollback is `git revert` of the range `40508ac..c5d0960` on this branch; no engine
  source, no `.uasset`, no shared config outside `Config/DefaultGame.ini` was touched.
- The `Build.cs` scan reads every runtime-module source file on each makefile
  regeneration. Measured cost is inside the 2.34 s failure path above, i.e. negligible at
  today's ~20 files. If the module grows to thousands, re-measure rather than assume.
- The comment stripper in the scan does not understand string literals. A banned macro
  name inside a string literal followed by `(` would false-positive. No plausible code
  contains that, and the error direction is safe (false positive, never false negative).

---

### CORE-003 — acceptance criteria, opened 2026-08-14

Scope per this row: `DataAsset validation framework`. Owner `race-systems-engineer`.
Gate A. Depends on `CORE-002` (DONE). Read "### CORE-003 — findings inherited from
CORE-002" immediately below **first** — it names two concrete defects this ticket
must close, not just a framework to build in the abstract.

- [ ] A reusable, reflection-driven validation pass that re-applies a `UPROPERTY`'s
      `ClampMin`/`ClampMax` metadata after config/ini load — closing the gap that
      metadata only constrains the Details panel today, not an
      `-ini:Game:[...]:Field=value` override.
- [ ] `URacingSimSettings` calls this pass (e.g. from a config-load hook) so an
      out-of-range ini value (`TelemetrySampleRateHz=1e6`, negative
      `PhysicsPolicyVersion`, etc.) is clamped or rejected rather than loading
      unchallenged.
- [ ] `URaceRulesetDataAsset::Validate()` (RACE-001) is evaluated for reuse under this
      same framework rather than staying a one-off pattern — either adopt it as the
      framework's shape, or state in this ticket why it stays separate.
- [ ] M-5 (pass 1, CORE-002) closed — see the inherited-findings table below.
- [ ] MEDIUM-1 (pass 2, CORE-002) closed: `FRacingSimBuildId::Current()`'s Explicit
      branch stops marking a sanitisation-mutated stamp `bIsAuthoritative = true`;
      adopt the same rule already used one branch below in the same function for the
      empty-stamp case (`Result.bIsAuthoritative = (Stamped == Trimmed)`).
- [ ] MEDIUM-2 (pass 2, CORE-002) closed: either add `+` to `SanitiseComponent`'s
      allow-list, or document explicitly why Derived may embed `+` and Explicit may
      not.
- [ ] `RacingSimTests` gains automation coverage: an out-of-range ini value is
      clamped/rejected for at least two distinct properties, plus tests for both
      MEDIUM-1 and MEDIUM-2's fixed behavior.
- [ ] Editor **and** Game targets build with zero new warnings.

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
| TRACK-001 | Original circuit graybox and spline centerline | race-systems-engineer | CORE-001 | B | OPEN |
| TRACK-002 | Ordered checkpoint gates and crossing direction | race-systems-engineer | TRACK-001 | B | OPEN |
| RACE-001 | Race state machine and monotonic clock | race-systems-engineer | CORE-002 | B | **DONE** 2026-08-14 — `code-reviewer` approved across two passes at `7832d0a`; `test-engineer` independently confirmed both targets build clean from a from-scratch rebuild and 442/442 automation Smoke tests pass. Merged to `main` at `2c41989`. Two findings (M4, M1's accepted risk) tracked forward into `RACE-002` |
| RACE-002 | Lap/sector/progress/validity logic | race-systems-engineer | TRACK-002, RACE-001 | B | OPEN |
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

### TRACK-001 — acceptance criteria, opened 2026-08-14

Scope per this row: `Original circuit graybox and spline centerline`. Owner
`race-systems-engineer`. Gate B. Depends on `CORE-001` (DONE). **Original circuit
only** — no real track name, layout, signage, or venue (`CLAUDE.md` non-negotiable
decisions; `LEGAL-001`'s ledger governs any external reference art).

**Deliberately checkpoint-agnostic**, mirroring RACE-001's split: ordered checkpoint
gates and crossing-direction validation are `TRACK-002`, not this ticket. TRACK-001 is
the centerline/track-identity contract everything else attaches to.

- [ ] `ATrackDefinitionActor` (`Source/RacingSim/Race/`, per `Docs/01-Architecture.md`'s
      proposed types) exposes: centerline spline (closed loop), track length in
      centimetres (with a documented cm→SI conversion, per `CLAUDE.md` units rule and
      `RacingSimUnits.h`'s existing conversion policy), sector boundary markers along
      the spline, start/finish transform, grid slot transforms, and reset sample points
      (nearest-valid-track-point candidates for `VEH-005`'s safe reset, later).
- [ ] Spline-distance and nearest-point queries are exposed as a typed, testable API
      (not raw `USplineComponent` calls scattered across callers) — this is the surface
      `RACE-002`'s progress/lap logic and `VEH-005`'s reset will consume.
- [ ] Track identity uses `FRacingContentVersion` (`CORE-002`, `RacingSimBuildId.h`) —
      `GetContentVersion()`/content-hash pattern, matching `URaceRulesetDataAsset`'s
      precedent from `RACE-001` — so `FRacingSimVersionStamp::TrackVersion` (reserved by
      CORE-002) can be populated from a real asset instead of staying empty.
- [ ] A minimal graybox test level (`Content/Tracks/Prototype/Maps/`, per
      `Docs/15-ProjectStructure.md`'s planned tree) containing one closed-loop
      `ATrackDefinitionActor` instance, using only primitive/placeholder geometry — no
      final art, no license-ledger-requiring external asset. Sufficient for `TRACK-002`
      and `RACE-002`'s automation to exercise real checkpoint/lap logic against.
- [ ] `RacingSimTests` gains automation coverage for the centerline/spline-query API
      (distance along spline, nearest point, sector boundaries) that does **not**
      require the test map — testable against a procedurally-constructed spline in a
      transient world/commandlet, matching RACE-001's testability-first design so the
      cheap, certain work doesn't block on the level-authoring step.
- [ ] Editor **and** Game targets build with zero new warnings.

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
