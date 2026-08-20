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
- Build ID/versioning method: **VERIFIED — CORE-002.** `FRacingSimBuildId::Current()` (`Source/RacingSim/Core/RacingSimBuildId.cpp`) resolves via `URacingSimSettings.BuildIdScheme`. `Derived` composes five components, hyphen-separated: `<channel>-<projectversion>-<engineversion>+<changelist>-<config>-<targettype>` (e.g. `dev-0.1.0-5.8.1+56057345-Development-Editor`), and is never authoritative. Note: `<projectversion>` reads `ProjectVersion` from `Config/DefaultGame.ini` via `GConfig`/`GGameIni`, not from `FEngineVersion`/`FApp`; the key is absent from this project's ini today, so this component is always the literal fallback `0.0.0`. `Explicit` uses a CI-stamped `ExplicitBuildId` (through `SanitiseComponent`, which allows `[A-Za-z0-9._+-]` and warns if sanitisation changes the value) and is authoritative only when the sanitised result is non-empty **and identical to the stamped value**, else warns and falls back to `Derived`. **Both of those were changed by `CORE-003`** (closing CORE-002 findings MEDIUM-2 and MEDIUM-1 respectively): `+` was added to the allow-list so a semver-style CI stamp such as `1.4.0+4417` round-trips verbatim and stops firing a spurious sanitisation warning — the `Derived` composer already embeds a literal `+` — and a stamp that sanitisation *does* mutate (e.g. `feature/x` → `featurex`) is now marked non-authoritative, because a lossy rewrite cannot honour the uniqueness/traceability promise `bIsAuthoritative` makes and `IsPublishable()` reads. Consumers must percent-encode a build ID before placing it in a URL query string, where `+` decodes to a space; `RacingSim.Core.BuildId` asserts that every ID either scheme produces matches `[A-Za-z0-9._+-]`. Full per-result contract is `FRacingSimVersionStamp` (build ID, engine patch/changelist, track/car-spec/ruleset content versions, physics policy version, assist preset, input device type, validity/penalties); `IsPublishable()` also requires an authoritative build ID. Contract only — track/car fields populate at `TRACK-001`/`VEH-003`. Covered by `RacingSim.Core.BuildId` and `RacingSim.Core.VersionStamp` (`Source/RacingSimTests/Core/RacingSimVersionSpec.cpp`).

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
| Python Editor Script | `Engine/Plugins/Experimental/PythonScriptPlugin` | Editor | Editor/content automation only, never gameplay | No module binaries — **but descriptor is staged and mounted**, see Gate G section |
| Editor Scripting Utilities | `Engine/Plugins/Editor/EditorScriptingUtilities` | Editor | Batch asset operations | No module binaries — **but descriptor is staged and mounted**, see Gate G section |
| Model Context Protocol | `Engine/Plugins/Experimental/ModelContextProtocol` | Editor | Experimental editor automation, loopback only | No |
| All Toolsets | `Engine/Plugins/Experimental/Toolsets/AllToolsets` | Editor | MCP toolset registry dependency | No |

Available but **not** enabled: `ChaosModularVehicle`, `Gauntlet`, `PixelStreaming` (v1).
PixelStreaming2 pulls `NVCodecs` and `AMFCodecs` transitively, giving the hardware encode path.

Exact plugin versions: UNVERIFIED — all ship with UE 5.8.1 and carry no independent version; treat the engine changelist as their version.

## Pixel Streaming Infrastructure

- repository source: `https://github.com/EpicGamesExt/PixelStreamingInfrastructure` (VERIFIED)
- exact UE-matching branch/tag/commit: branch **`UE5.8`**, commit **`48bff3b751f91f735b50c90b2a7fec5ceb2a440f`** (2026-08-04), `RELEASE_VERSION` **0.1.0**, `SignallingWebServer` package version 3.0.0 (VERIFIED — cloned and checked out detached at that commit)
- local path: `Web/PixelStreamingInfrastructure` — **gitignored, not vendored**. Re-fetch by cloning the pinned commit.
- Node version from the repository: **v22.14.0**, from a root `NODE_VERSION` file (VERIFIED 2026-08-10).
  **This corrects an earlier entry in this document**, which claimed no version was
  pinned upstream. That claim was drawn from the absence of `.nvmrc` and of an
  `engines` field — both genuinely absent — but the check missed the `NODE_VERSION`
  file, which is what Epic's own tooling reads. Local node is **v24.18.0**, two major
  versions ahead of the pin. See ASSUMPTION-001 for what was then actually tested.
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

**VERIFIED 2026-08-10 — 426 tests, 426 passed, 0 failed, 0 not run, 8.70 s.**

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
    "C:\Users\jun yi\Documents\central-command\racing\RacingSim.uproject" `
    -ExecCmds="Automation RunFilter Smoke; Quit" `
    -ReportExportPath="C:\Users\jun yi\Documents\central-command\racing\Saved\Automation\Report" `
    -unattended -nopause -nosplash -nullrhi -stdout -utf8output -log=AutomationSmoke.log
```

Report: `Saved/Automation/Report/{index.json,index.html}`. Counts must be read from
`index.json` (`succeeded` / `failed` / `notRun`), **not** from the process exit code.

> **`-log=AutomationSmoke.log` does not work — verified 2026-08-12.** On re-run,
> `Saved/Logs/AutomationSmoke.log` was **not** updated; the transcript landed in the
> default `Saved/Logs/RacingSim.log` instead, and the engine silently fell back rather
> than erroring. The log's own echo of the command line renders the flag as
> `-log=AutomationSmoke .log`, with a space inserted before the extension — that is the
> engine's internal reconstruction, not a defect in the invocation, which was verified
> byte-for-byte. **Treat `Saved/Automation/Report/index.json` as the only authoritative
> evidence from this command, and do not rely on a distinctly named log file.**
> Re-run confirmed 426 succeeded / 0 failed / 0 notRun,
> `reportCreatedOn 2026.08.12-02.41.42`.

Command syntax was read from engine source, not guessed:

- Subcommands, `AutomationCommandline.cpp:570-740`: `List`, `RunTests`/`RunTest`,
  `RunFilter`, `SetFilter`, `RunAll`, `Now`, `SetPriority`, `SetMinimumPriority`,
  `Quit`, `SoftQuit`.
- Filter names, `AutomationCommandline.cpp:95-102`: `Engine`, `Smoke`, `Stress`,
  `Perf`, `Product`, `Standard`, `Negative`, `All`.
- Report flag, `AutomationControllerManager.cpp:213-219`: `-ReportExportPath=`.
  `-ReportOutputPath=` still parses but logs a deprecation warning.
- Text inside `-ExecCmds` is split on `;`, so `"Automation RunFilter Smoke; Quit"`
  is parsed as two Automation subcommands.

> **`RacingSim.Core.SettingsRangeMetadata` is load-bearing, not decorative — do not
> skip it in CI.** `URacingSimSettings`' `ClampMin`/`ClampMax` ranges are enforced at
> runtime from a hand-declared table in C++, *not* from the `UPROPERTY` metadata,
> because `WITH_METADATA` is `WITH_EDITORONLY_DATA` (`CoreMiscDefines.h:31`) and is
> therefore **0 in the packaged Game build** — the exact build a CI `-ini:` override
> ships to. That test is the only thing asserting the table and the metadata still
> agree, in both directions, including that a newly added clamped config property was
> not forgotten. Skip it and the packaged-build validation guarantee degrades silently,
> with the editor still behaving correctly. See `CORE-003` in `Docs/Tickets.md`.

`Smoke` was chosen deliberately over `All`: it is the fast subset, and this machine
is memory-constrained (BLOCKER-003). Widening the filter is a later decision, not a
default.

**Corrected 2026-08-12.** This previously read "These are engine tests, not project
tests. The project has no tests yet — `TEST-001` is unstarted." That stopped being true
when `CORE-001` landed.

The project now owns one test, `RacingSim.Core.LogCategories`
(`Source/RacingSimTests/Core/RacingSimLogSpec.cpp`). It is **`SmokeFilter`**, so the
command above discovers and runs it alongside the engine suite.

**Proven, not assumed** — `RunFilter Smoke` on 2026-08-12 returned
**succeeded=427, failed=0, notRun=0** (`reportCreatedOn 2026.08.12-07.13.48`), with
`RacingSim.Core.LogCategories => Success` present in the results. The count rose from
426 to 427, which is the project test being discovered by the filter rather than
invoked by name.

That filter choice is deliberate and was a `code-reviewer` blocker. The test was first
written `ProductFilter`, which meant the only recorded automation command in this file
could not run it: the suite would report 426/426 green while the project's own test
never executed, and every later ticket would inherit that as "tests pass". A test the
documented gate cannot see is not coverage. **Any new test must carry a filter that a
recorded command actually uses**, and discoverability must be proven with a
`RunFilter` log line — never with `RunTests <name>`, which bypasses filters entirely.

Beyond that one test, the engine tests still say nothing about RacingSim's correctness;
they prove the harness, command form and report export work on this machine.

`-nullrhi` skips GPU work. Any future rendering or screenshot test must drop it.

### Run automation tests — the `Product` filter, for anything that touches an Actor

**VERIFIED 2026-08-19 (TRACK-002).** Second gate, alongside `Smoke`. It exists for one
reason, and the reason resolves a finding that had been open across TRACK-001, TEST-001
and TRACK-002.

```powershell
# VERIFIED 2026-08-19 - see the TRACK-002 verification-evidence section in Docs/Tickets.md
pwsh -File Scripts/Test/Run-AutomationFilter.ps1 `
    -Filter Product `
    -ProjectPath "<...>\RacingSim.uproject" `
    -ReportDir "<...>\Saved\Automation\ProductReport"
```

Same rules as `Smoke`: counts come from `index.json`, never from the exit code.

#### A `SmokeFilter` test cannot create an Actor in this project. Ever.

TRACK-001 recorded, as a brute fact with no explanation, that actors cannot be
instantiated in automation here — `NewObject<AActor>(GetTransientPackage())` dies on

```text
Assertion failed: RegisteredElementType
[Elements/Framework/TypedElementRegistry.h:536]
Element type 'Components' has not been registered!
```

and `UWorld::CreateWorld(EWorldType::Game)` dies inside `CreateWorld`. Both kill
`UnrealEditor-Cmd` outright and produce **no `index.json`**, so the gate reports nothing
at all rather than a failure. TEST-001 inherited the finding and did not close it; every
Actor-bearing ticket was expected to hit the same wall.

**The wall is a timing artifact, and it is avoidable.** Three engine facts, read from
source and then confirmed by running:

1. `FEngineLoop::PreInit` runs the smoke tests itself —
   `FAutomationTestFramework::Get().RunSmokeTests()`,
   `Runtime/Launch/Private/LaunchEngineLoop.cpp:4376`. **Every `SmokeFilter` test in the
   process executes there**, during `PreInit`. The `-ExecCmds` line is not what runs
   them; it is processed later, on the first engine tick.
2. The typed-element types `Object`/`Actor`/`Components`/`SMInstance` are registered by
   `RegisterEngineElements()`, called from `UEngine::Init` —
   `Runtime/Engine/Private/UnrealEngine.cpp:2399` — i.e. **after `PreInit` returns**.
3. `UActorComponent::PostInitProperties` calls
   `UEngineElementsLibrary::CreateEditorComponentElement` for every **non-template**
   component, under `WITH_EDITOR`, on the game thread —
   `Runtime/Engine/Private/Components/ActorComponent.cpp:588`. That reaches
   `UTypedElementRegistry::CreateElementImpl`, which `checkf`s that the element type is
   registered.

So a `SmokeFilter` test runs in a window where constructing any non-template actor
component is a hard crash. This explains all three of TRACK-001's failures with one
cause, including why the CDO fixture worked: CDO subobjects are templates, and the
element path skips templates.

`ProductFilter` tests are **not** run by `RunSmokeTests()`. They run only from the
deferred `Automation RunFilter Product` command, which executes after `UEngine::Init`
has completed. Same process, same flags, roughly fifteen seconds later in boot — and
actors work.

#### Proven, not inferred

`Source/RacingSimTests/Race/TrackPrototypeLevelSpec.cpp` was written `SmokeFilter`
first. It crashed with exactly that assertion, from `LoadPackage` on a map containing one
placed `ATrackDefinitionActor`:

- under the full `Automation RunFilter Smoke` gate — `PROCESS_EXITCODE=3`, no
  `index.json`;
- and again when invoked alone by name (`Automation RunTests
  RacingSim.Race.TrackPrototypeLevelPostLoad`) — same assertion, `EXIT=3`. The isolation
  run is what rules out cross-test contamination and pins it to the harness phase.

Switching the same three tests to `ProductFilter`, with no other change, made them pass.

#### The rule this creates

- **A test that constructs, loads or spawns an Actor or an `UActorComponent` must be
  `ProductFilter`, and must be gated by `Run-AutomationFilter.ps1 -Filter Product`.**
- A test that does not touch actors should stay `SmokeFilter` — it is the faster gate and
  it runs earlier.
- Both gates must be run and reported. `Smoke` alone no longer covers this project's
  tests, and a ticket that reports only `Smoke` counts is reporting a subset.
- The `EditorContext` requirement is unchanged and still applies to both filters: the
  gates run with `GIsEditor` true and `IsRunningCommandlet()` false, so a
  `CommandletContext`-only suite is never collected.

#### What is still not solved

This gives a **loaded, package-resident** actor instance. It does **not** give a
spawnable world: `UWorld::CreateWorld` was not retried and is still presumed broken, and
nothing here lets a test spawn a fresh actor and mutate it. A package-loaded actor is
shared and stays resident, so tests using it must be **read-only** — see the header of
`TrackPrototypeLevelSpec.cpp`. A ticket needing a mutable actor instance (`VEH-002`)
should duplicate the loaded object, or re-open the `CreateWorld` question now that the
element-registration cause is understood: a `ProductFilter` test may well be able to
create a world where a `SmokeFilter` one could not.

### Author the graybox level (editor Python)

**VERIFIED 2026-08-19 (TRACK-002).**

```powershell
pwsh -File Scripts/Content/Author-PrototypeGrayboxLevel.ps1
```

Three non-obvious failures were hit and fixed getting this to work headlessly. All three
produce misleading messages, and each one only becomes visible after the previous is
fixed, so they are recorded in order:

1. **`-ExecCmds="py <path with spaces>"` silently truncates.** The engine's ExecCmds
   tokenizer splits on whitespace, so `py "C:\Users\jun yi\...\x.py"` arrives at Python as
   `C:\Users\jun`. Not ending in `.py`, it is evaluated as literal source and fails with
   `SyntaxError: unexpected character after line continuation character (<string>, line 1)`
   — a message naming neither the path nor the cause. `FPythonScriptPlugin`'s own comment
   (`PythonScriptPlugin.cpp:1813-1822`) advertises support for pathnames with spaces; that
   support is real but sits downstream of the ExecCmds split, so it never sees the path.
2. **The 8.3 short path fixes the spaces and breaks the extension.** Short names are
   uppercase (`AUTHOR~1.PY`), and the file-versus-snippet decision is
   `UE::String::FindFirst(Command, TEXT(".py"))`, which defaults to
   `ESearchCase::CaseSensitive` (`Core/Public/String/Find.h:23`). Identical error message,
   different cause. **Fix: short-path the parent directory only and re-join the real
   filename** — no spaces, lowercase extension.
3. **`; Quit` does not quit, and `unreal.log()` does not reach stdout.** `Quit` after
   `Automation RunFilter Smoke` works because *both* halves are `Automation` subcommands
   and the automation controller quits; as a bare console command it is not handled, and
   the editor sat in its tick loop forever under `-unattended`, producing no output and no
   exit (killed by PID twice). The script now calls `unreal.SystemLibrary.quit_editor()`
   in a `finally`. Separately, `unreal.log()` emits at **Log** verbosity and `-stdout`
   forwards **Display** and above, so the script's entire report reaches
   `Saved/Logs/RacingSim.log` and never reaches captured stdout. The first version of the
   wrapper read stdout only and printed "the map was not authored" for a run that had
   authored it perfectly. **Evidence for this command comes from
   `Saved/Logs/RacingSim.log`, not from stdout and not from the exit code.**

#### The headless editor REWRITES `Config/*.ini` on shutdown, and strips your comments

**Check `git status` after every editor run, and revert config churn you did not
author.** TRACK-002's authoring runs silently rewrote `Config/DefaultGame.ini`:

- the entire `[/Script/EngineSettings.GeneralProjectSettings]` block was expanded with
  ~18 placeholder defaults, including
  `CopyrightNotice=Fill out your copyright notice in the Description page of Project
  Settings.` and `ProjectID=00000000000000000000000000000000`;
- **and the ~30-line AssetManager comment block was deleted** — the one documenting
  BLOCKER-005, why the `GameFeatureData` rule exists at all, and why `bIsEditorOnly=True`
  is load-bearing rather than cosmetic (a `code-reviewer` finding from CORE-001).

The functional settings survived; only the reasoning was destroyed. That is the
dangerous shape of this failure. The config still works, the diff looks like harmless
tool noise, and the next person to touch that rule has lost the explanation of why
changing it breaks a packaged build. It would very plausibly be committed by accident.

Unreal's config writer does not preserve comments — it serialises the in-memory config
object. Nothing in the project can prevent it. The control is procedural: after any
`UnrealEditor-Cmd` run that is not read-only, diff `Config/` and `git checkout --` any
file the ticket did not intend to change.

### Cook/package

**VERIFIED 2026-08-12 — `BUILD SUCCESSFUL`, `AutomationTool exiting with
ExitCode=0 (Success)`, 181.79 s. No workaround. This is the canonical command.**

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat' `
    BuildCookRun `
    -project="C:\Users\jun yi\Documents\central-command\racing\RacingSim.uproject" `
    -platform=Win64 -clientconfig=Development `
    -build -cook -stage -pak -archive `
    -archivedirectory="C:\Users\jun yi\Documents\central-command\racing\Packaged" `
    -stagingdirectory="$env:LOCALAPPDATA\RacingSimStage" `
    -nop4 -utf8output -unattended
```

**`-stagingdirectory` outside `Documents\` is mandatory, and it is what fixed
BLOCKER-006.** Staging into the default `Saved/StagedBuilds` under
`Documents\central-command\racing` fails at `ExitCode=102` on the stage-directory
cleanup, reproducibly. Staging to `%LOCALAPPDATA%\RacingSimStage` succeeds with the
cleanup enabled. Proven twice: once into an empty directory, then again into the same
directory populated with 52 files — the second run is the one that matters, because it
exercises the delete path that failed. See BLOCKER-006.

`-nocleanstage` is **no longer used and must not be reintroduced.** It masked the
failure at the cost of retaining stale staged files, which was observed producing an
archive whose paks predated its own cook by two days.

Artifacts produced and inspected (2026-08-12 run):

- `Packaged/Windows/RacingSim.exe`
- `Packaged/Windows/RacingSim/Content/Paks/RacingSim-Windows.pak` (11,170,981 bytes)
- `RacingSim-Windows.ucas` 216,920,032 bytes; `global.ucas` 3,392,064 bytes
- **All five pak/container files carry the timestamp of the run that produced them** —
  verified within 35 seconds of the build completing. The staleness defect is gone.

#### Packaged plugin manifest — Gate G evidence (`ENV-003`)

> **Corrected 2026-08-12 after independent review.** The original wording of this
> section claimed a search of the packaged tree returned "zero matches" for all six
> editor-only plugins. **That was false for two of them.** What had actually been
> inspected was a directory listing of `Packaged/Windows/Engine/Plugins`, which is a
> much narrower search than the sentence implied. The corrected findings follow.

**Genuinely absent** — no descriptor, no binary, no mount:
`ModelContextProtocol`, `AllToolsets`, `ToolsetRegistry`, `AndroidFileServer`.
Confirmed three ways: absent from every staging manifest, absent from the pak file
index, and absent from all ~190 `Mounting Engine plugin` lines in
`Packaged/Windows/RacingSim/Saved/Logs/RacingSim.log`. Since MCP's own reference is
suppressed, its declared `"Plugins"` dependencies are not followed either, which is
why `ToolsetRegistry` never appears. **The MCP exclusion is real.**

**Present, contrary to the "In shipping: No" column above** — `PythonScriptPlugin`
and `EditorScriptingUtilities`:

- `Packaged/Windows/Manifest_UFSFiles_Win64.txt` lists
  `Engine/Plugins/Experimental/PythonScriptPlugin/PythonScriptPlugin.uplugin`,
  `Engine/Plugins/Editor/EditorScriptingUtilities/EditorScriptingUtilities.uplugin`
  and `Engine/Plugins/Editor/EditorScriptingUtilities/Config/DefaultEditorScriptingUtilities.ini`;
- the runtime log records `Mounting Engine plugin EditorScriptingUtilities` and
  `Mounting Engine plugin PythonScriptPlugin`.

**Cause.** `FPluginReferenceDescriptor::IsEnabledForTarget`
(`PluginReferenceDescriptor.cpp:64-85`, applied at `PluginManager.cpp:2425`) is
evaluated **per reference**. `TargetAllowList` suppresses only the project's own
reference; other enabled engine plugins reference these two with no allowlist and
re-enable them. Corroborated inside the pak by other `.uplugin` descriptors declaring
`"Name": "EditorScriptingUtilities", "Enabled": true`.

**Which plugins, established 2026-08-12 — and why this is not fixable here.** Every
`.uplugin` in the engine referencing `EditorScriptingUtilities` or `PythonScriptPlugin`
was enumerated, then filtered to those with `"EnabledByDefault": true`. Nine remain:

`Bridge`, `Developer/PluginUtils`, `Experimental/ChaosEditor`, `Fab`, **`FX/Niagara`**,
`MetaHuman/MetaHumanSDK`, **`PCG`**, **`Runtime/RigVM`**, `Tests/InterchangeTests`.

`Niagara`, `PCG` and `RigVM` are core engine plugins enabled by default in every
project. Suppressing the leak would mean disabling them, which is not an option for a
project that will need Niagara and PCG.

**Conclusion: this is stock UE 5.8.1 behaviour, not a defect in this project's
configuration.** The `TargetAllowList` entries in `RacingSim.uproject` are correct and
do what they can; they simply cannot suppress a reference declared by a different
enabled plugin. What was wrong was the *claim* made about the result, not the setting.
No project-level fix exists. Gate G must therefore assert the narrower, true property —
that `ModelContextProtocol`, `AllToolsets` and `ToolsetRegistry` are absent — rather
than a blanket "no editor-only plugins are staged", which is unachievable on a stock
engine install.

The "In shipping" column above should therefore be read as a claim about **module
binaries** — no Editor-type module is built into the Game target — not as a claim
that the plugin is absent from the staged tree.

**Cooked script-object residue.** `Packaged/Windows/RacingSim/Content/Paks/global.ucas`
contains the global script-object names `/Script/ModelContextProtocol` (47
occurrences), `/Script/ToolsetRegistry` (10), `/Script/PythonScriptPlugin` (5),
`/Script/EditorScriptingUtilities` (3). Names only — no code, no descriptor, no mount.
Expected, since the cook runs under `UnrealEditor-Cmd` with editor plugins loaded. Not
a Gate G failure, but a `NoRedist` plugin leaving name traces in a shipped artifact
belongs in the licensing record.

**Consequence not previously recorded:** because editor plugins load in the cook
commandlet, *which editor plugins are enabled changes the shipped bytes*. Disabling
`AllToolsets` later will alter `global.ucas`. This belongs in the rollback and
compatibility notes for any decision to narrow the MCP toolset surface.

**What this section does and does not prove.** It proves, by artifact inspection
rather than `.uproject` intent, that the two MCP plugins are excluded from a
Game/Development target. `TargetAllowList: ["Editor"]` is effective for build and
stage — corroborated at `PluginManager.cpp:2425`, `UEBuildTarget.cs:5835`,
`CopyBuildToStagingDirectory.Automation.cs:884`. Gating is on `EBuildTargetType` and
is orthogonal to `EBuildConfiguration`, so a Shipping Game target is excluded on the
same code path. **But no Shipping-configuration build has ever been produced here.**
`Docs/07-QualityGates.md:9` says "excluded from shipping"; that word remains untested,
and the inference — though strong — is an inference.

`PixelStreaming2` **is** staged (`Packaged/Windows/RacingSim/Samples/PixelStreaming2`),
as required. Chaos Vehicles and Enhanced Input produced no separate plugin
directories — expected, since they link into the target binaries — so their presence
is **not** independently confirmed by this check.

**Every path argument must be quoted.** An unquoted `-project=` splits at the
space in `jun yi`; UAT then tries to parse `C:\Users\jun` and reports it as a
bogus JSON syntax error in the `.uproject`.

What worked: the Game target compiled with the verified toolchain, and the cooker
processed **all 586 packages** (`LogCook: Display: Done!`) in 145.92 s,
PeakPhysMemoryMB 1397.

What failed — 2 errors, cook exit 1, UAT exit 25:

```text
LogGameFeatures: Error: Asset manager settings do not include a rule for assets of
  type GameFeatureData, which is required for game feature plugins to function
LoadErrors: Error: Asset Manager settings do not include an entry for assets of type
  GameFeatureData ... Add entry to PrimaryAssetTypesToScan?
```

Diagnosis: `AllToolsets` (enabled for Unreal MCP) pulls in `GameFeaturesToolset`,
which loads the `GameFeatures` plugin. The cooker runs `UnrealEditor-Cmd`, so
editor plugins load during cook despite the `TargetAllowList: ["Editor"]`
restriction — that field governs *packaged targets*, not the cook commandlet.
The project **had no `Config/` directory at all**, so AssetManager had no
`PrimaryAssetTypesToScan` rule for `GameFeatureData`. (Historical diagnosis —
`Config/` now exists; see the RESOLVED block below.)

**RESOLVED 2026-08-10 — Option 1 implemented. The AssetManager error is gone.**

Two options were considered:

1. **Chosen.** Add `Config/DefaultGame.ini` with an AssetManager
   `PrimaryAssetTypesToScan` entry for `GameFeatureData`.
2. Rejected. Drop `AllToolsets` and enable only the toolsets Unreal MCP needs.

**Why Option 1 over Option 2**, reversing the earlier note that favoured Option 2:
`Docs/09-UnrealMCP.md` step 2 explicitly instructs enabling **Unreal MCP and All
Toolsets**. Dropping `AllToolsets` would contradict the project's own documented
MCP setup and require first determining, then pinning, the exact toolset subset MCP
depends on — an experimental, engine-patch-sensitive surface. Option 1 is a
four-line config entry with a documented engine-source justification and no
behavioural cost, since this project has no game feature plugins. Option 2's
supposed advantage — narrowing the editor-only surface for Gate G — is better
served by the ENV-005 packaged-manifest inspection, which proves exclusion directly
rather than by omission.

Result of the re-run: the Game target compiled, **all 586 packages cooked**, and
`UnrealPak executed in 3.917860 seconds ... ExitCode=0`. The GameFeatures error
does not appear.

The run nonetheless **failed at a later, unrelated stage** — see BLOCKER-006.
Gate A therefore remains unclaimed.

### Install and build the Pixel Streaming Infrastructure

**VERIFIED 2026-08-10 — `npm ci` exit 0, 1,373 packages, 4 min; `build:all:cjs` exit 0.**

```powershell
cd "C:\Users\jun yi\Documents\central-command\racing\Web\PixelStreamingInfrastructure"
npm ci --no-audit --no-fund
npm run build:all:cjs     # Common -> Signalling -> SignallingWebServer -> Frontend
```

`npm ci` was chosen over `npm install` so the tree matches `package-lock.json` at the
pinned commit exactly — the same lockfile the licence census in
`Docs/13-AssetLicenseLedger.md` (ASSET-0005) was taken from.

Two warnings that are not failures but are worth knowing: two deprecations
(`whatwg-encoding`, `node-domexception`), and three packages whose install scripts npm
did **not** run — `mediasoup@3.15.5` (postinstall), `pre-commit@1.2.2`,
`spawn-sync@1.0.15`. `mediasoup` needs its postinstall to build the SFU worker binary,
so **the SFU is not usable until those scripts are approved**. The signalling server and
frontend do not need them, which is why the run below still works.

### Start signalling/frontend/TURN

**VERIFIED 2026-08-10 — listeners up in under 5 s; player page HTTP 200.**

```powershell
cd "C:\Users\jun yi\Documents\central-command\racing\Web\PixelStreamingInfrastructure\SignallingWebServer"
node ./dist/index.js --serve --console_messages verbose --player_port 8080 `
    --http_root "C:\Users\jun yi\Documents\central-command\racing\Web\PixelStreamingInfrastructure\SignallingWebServer\www"
```

Observed listeners: streamer `8888`, SFU `8889`, HTTP `8080`.

**`--http_root` is mandatory on this checkout, and the reason is a defect upstream.**
`SignallingWebServer/config.json` at commit `48bff3b7` ships

```json
"http_root": "D:\\PixelStreamingInfrastructure\\SignallingWebServer\\www",
```

— an absolute path leaked from Epic's build machine. It overrides the sane default at
`src/index.ts:120` (`path.resolve(__dirname, '..', 'www')`), so without the flag every
page 404s with `error: Unable to locate file player.html`. **Verified twice**: 404
before the override, HTTP 200 (1,409 bytes) after.

**The space hazard bit for a third time here.** `Start-Process -ArgumentList` joins its
array with spaces and does *not* quote, so `--http_root C:\Users\jun yi\...` arrived as
`C:\Users\jun` and still 404'd. The value must carry embedded quotes. The confirming
evidence is the server's own config echo, which prints the path it actually resolved —
check that line, not the exit code.

TURN: **still UNVERIFIED.** No STUN/TURN server was configured or started; a local
loopback stream does not need one. That belongs to `STREAM-004`, which is blocked on
BLOCKER-001.

### Run packaged Pixel Streaming build

**VERIFIED 2026-08-10 — packaged build connected to the signalling server, joined the
room, and published video and audio tracks.**

```powershell
& "C:\Users\jun yi\Documents\central-command\racing\Packaged\Windows\RacingSim.exe" `
    -PixelStreamingConnectionURL=ws://127.0.0.1:8888 `
    -RenderOffScreen -ForceRes -ResX=1280 -ResY=720 -Unattended -stdout -AudioMixer
```

Evidence from `Packaged/Windows/RacingSim/Saved/Logs/RacingSim.log`:

```text
LogPixelStreaming2EpicRtc: Conference::CreateSession. Id=[DefaultStreamer], Url=[ws://127.0.0.1:8888]
LogEpicRtcWebsocket: Websocket connection made to: ws://127.0.0.1:8888
LogPixelStreaming2EpicRtc: RoomSignallingContextObserver::OnJoined. ... state=[Joined]
LogPixelStreaming2RTC: FEpicRtcStreamer::OnVideoTrackUpdate(Participant [DefaultStreamer], VideoTrack [0], IsRemote[false])
LogPixelStreaming2RTC: FEpicRtcStreamer::OnAudioTrackUpdate(Participant [DefaultStreamer], AudioTrack [1], IsRemote [false])
```

Matching signalling-server side:

```text
info: New streamer connection: ::ffff:127.0.0.1
info: < UnknownStreamer :: {"type":"config","peerConnectionOptions":{},"protocolVersion":"1.3.0"}
info: > UnknownStreamer :: {"id":"DefaultStreamer","protocolVersion":"1.1.0","type":"endpointId"}
info: < DefaultStreamer :: {"type":"endpointIdConfirm","committedId":"DefaultStreamer"}
```

Then 30-second `ping`/`pong` keepalives for the remainder of the run.

**Reconnect works, and was observed rather than assumed.** The signalling server was
killed and restarted while the game kept running; the streamer re-registered
**0.5 s** after the new server came up, logging `Streamer reconnecting... Attempt 2`,
`Attempt 3`, then `Websocket connection made`.

#### What this does and does not prove

It proves the packaged build accepts PS2 arguments, reaches signalling, negotiates an
endpoint id, creates tracks, and survives a signalling restart. **It does not prove a
browser ever received a frame** — no browser client connected, no WebRTC peer
connection was established, no encoder was exercised. That is `STREAM-001`, and it
stays open.

#### Pixel Streaming 2 flag names, derived from source

PS2 does not hard-code a flag list. Every `PixelStreaming2.*` CVar gets a command-line
form by a mechanical transform at
`PixelStreaming2PluginSettings.cpp:93-104`: **delete the dots, then rewrite the
`PixelStreaming2` prefix as `PixelStreaming`.** So `PixelStreaming2.ConnectionURL`
becomes `-PixelStreamingConnectionURL=…`, and `PixelStreaming2.Encoder.Codec` becomes
`-PixelStreamingEncoderCodec=…`. This is why PS1 documentation is misleading here: the
*shape* of the flags survived, the CVars behind them did not.

| Flag | CVar | Notes |
|---|---|---|
| `-PixelStreamingConnectionURL=` | `PixelStreaming2.ConnectionURL` | Default empty. `(protocol)://(host):(port)`. **The one flag that matters.** |
| `-PixelStreamingID=` | `PixelStreaming2.ID` | Streamer id; defaulted to `DefaultStreamer` in this run |
| `-PixelStreamingEncoderCodec=` | `PixelStreaming2.Encoder.Codec` | H.264/HEVC on this GPU; no AV1 encode on GA107 |
| `-PixelStreamingWebRTCFps=` | `PixelStreaming2.WebRTC.Fps` | |
| `-PixelStreamingEncoderTargetBitrate=` | `PixelStreaming2.Encoder.TargetBitrate` | |
| `-PixelStreamingInputController=` | `PixelStreaming2.InputController` | Which peer owns input |

`PixelStreaming2.AutoStartStream` defaults to **true** outside the editor
(`PixelStreaming2PluginSettings.cpp:360-364`), which is why the packaged build began
streaming with no explicit start call.

Deprecated, still parsed, do not use in new work: `PixelStreaming2.SignallingURL`,
`.URL`, `.IP`, `.Port` all now alias `ConnectionURL`
(`PixelStreaming2PluginSettings.cpp:241-244`); supplying `IP`/`Port` logs a conversion
warning. Unrecognised `-PixelStreaming*` arguments are not silently ignored — 
`ValidateCommandLineArgs()` logs `Unknown PixelStreaming command line arg: {0}`, which
is the cheapest way to catch a typo'd flag.

#### Protocol version mismatch — watch this

The signalling server announces `protocolVersion 1.3.0`; the UE 5.8.1 streamer replies
`1.1.0`. The handshake completed anyway, so it is not currently breaking anything, but
it is a version skew between two things this project pins separately. Record it in
`STREAM-002` and re-check after any engine or PSI bump.

#### Security note for Gate G / SEC-001

The signalling server binds **`::` (all interfaces)** on ports 8080, 8888 and 8889 —
not loopback. That is upstream's default and is correct for a real deployment, but on a
development machine it means the streamer and player endpoints are reachable from the
LAN. This is the opposite posture to Unreal MCP (loopback-only) and must be handled
explicitly in the worker image and firewall rules, not assumed. Related: NOTE-001
(`UbaServer` on `0.0.0.0:1345`).

## Unreal MCP

**ENV-005 VERIFIED 2026-08-10.** Every line below was produced by starting the
server and probing it, not by reading documentation.

- endpoint confirmed loopback-only: **VERIFIED** — `Get-NetTCPConnection -LocalPort 8000`
  returned exactly one row: `LocalAddress 127.0.0.1`, `State Listen`,
  `OwningProcess 46376` (the editor). **No `0.0.0.0` and no `::` row exists.**
- LAN reachability: **VERIFIED refused.** A TCP connect to port 8000 was attempted
  against all seven of the machine's non-loopback IPv4 addresses. The three routable
  ones — `192.168.88.7` (LAN), `172.19.176.1` (WSL/Hyper-V), `100.115.135.126` —
  each returned `No connection could be made because the target machine actively
  refused it`. The four `169.254.*` link-local addresses returned
  `A socket operation was attempted to an unreachable network`. Nothing connected.
  Caveat: these probes originated on the host itself. They prove the socket is not
  bound to those interfaces; a genuinely off-host probe is still worth doing once a
  second machine is available.
- generated `.mcp.json` inspected: **VERIFIED** — written to the project root by
  `ModelContextProtocol.GenerateClientConfig ClaudeCode`. Contents:

  ```json
  { "mcpServers": { "unreal-mcp": { "type": "http", "url": "http://127.0.0.1:8000/mcp" } } }
  ```

  Confirmed gitignored: `git check-ignore -v .mcp.json` → `.gitignore:52`.
- read-only tool discovery test: **VERIFIED.** `initialize` returned HTTP 200,
  `protocolVersion 2025-06-18`, and a session id. `tools/list` returned HTTP 200 with
  exactly three tools — `list_toolsets`, `describe_toolset`, `call_tool` — which is
  the `bEnableToolSearch = true` surface, not full native registration. A
  `list_toolsets` call then enumerated the toolsets, including
  `AutomationTestToolset`, `EditorAppToolset`, `PluginToolset`, `ConfigSettingsToolset`
  and **`GameFeaturesToolset`** — independent corroboration of the BLOCKER-005
  diagnosis that `AllToolsets` drags `GameFeatures` in.
- write test and rollback: **NOT RUN, deliberately.** ENV-005 requires a read-only
  call to succeed *before any write is permitted*. No write tool was invoked. A write
  test belongs to the first ticket that actually needs editor automation, with a
  rollback path defined in that ticket.
- shipping exclusion verified: **VERIFIED** — see the packaged plugin manifest check
  under *Cook/package*. Neither `ModelContextProtocol` nor `AllToolsets` appears
  anywhere in the packaged tree.

### How the server is started, and why it is off by default

The server does **not** start on its own. `UModelContextProtocolSettings::bAutoStartServer`
defaults to `false` (`ModelContextProtocolSettings.h:42`), and the setting lives in
`EditorPerProjectUserSettings` — a machine-local file, not project config, so it cannot
be switched on for everyone by a commit.

```powershell
# VERIFIED 2026-08-10 - listener came up 70 s after launch
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
    "C:\Users\jun yi\Documents\central-command\racing\RacingSim.uproject" `
    -ModelContextProtocolStartServer `
    -ExecCmds="ModelContextProtocol.GenerateClientConfig ClaudeCode" `
    -nullrhi -unattended -nopause -nosplash -stdout -utf8output -log=McpLoopback.log
```

The process stays resident; stop it explicitly (`Stop-Process`) when finished. The
listener disappeared immediately on shutdown, which was confirmed.

Flags read from engine source, not guessed:

- `-ModelContextProtocolStartServer` — `ModelContextProtocolSettings.cpp:33`.
  `-StartModelContextProtocolServer` still works but logs a deprecation warning.
- `-ModelContextProtocolPort=<1-65535>` — `ModelContextProtocolSettings.cpp:18`,
  overrides the setting; out-of-range values warn and fall back.
- `ModelContextProtocol.GenerateClientConfig <ClaudeCode|Cursor|VSCode|Gemini|Codex|All>`
  — `ModelContextProtocolEngineModule.cpp:41`, registered `ECVF_Cheat`. In a launcher
  build the file lands in `FPaths::ProjectDir()`; in a source build it would land in
  `FPaths::RootDir()` (`ModelContextProtocolClientConfig.cpp:160-163`).

### Why the binding is loopback, and what would break it

The bind address is **not** an MCP setting. MCP registers a route on the engine's
shared `HTTPServer` module, whose listener config defaults to
`BindAddress = "localhost"` (`HttpServerConfig.h:13`), which `HttpListener.cpp:68-70`
resolves through `SetLoopbackAddress()`. Loopback is therefore a *default*, not a
hard-coded guarantee.

**It is overridable, and that is the risk to watch.** Setting
`[HTTPServer.Listeners] DefaultBindAddress` in `Engine.ini` — or a per-port
`ListenerOverrides` entry containing `BindAddress=` (`HttpServerConfig.cpp:60,97`) —
moves the socket to any interface, with `any` meaning `0.0.0.0`. Neither
`BaseEngine.ini` nor this project's `Config/` contains an `[HTTPServer.Listeners]`
section (VERIFIED by grep), so nothing overrides it today. **Gate G / SEC-001 must
assert that this section stays absent**, because the exposure would be silent: the
MCP plugin logs nothing different when the socket moves.

Second-layer defence, and not a substitute: `ModelContextProtocolServer.cpp:112`
rejects requests whose `Origin` header is not `localhost`, `127.0.0.1` or `[::1]`.
That only blocks browser-driven requests — a non-browser client sends no `Origin` and
is allowed through — so it does not protect a wrongly-bound socket.

### Licensing note

`ModelContextProtocol.uplugin` declares **`"NoRedist": true`** and
`"IsExperimentalVersion": true`. It must never be redistributed, which the packaged
manifest check confirms it is not. Note that its two core modules
(`ModelContextProtocol`, `ModelContextProtocolEngine`) are typed **`Runtime`**, not
`Editor` — so keeping it out of a shipping build rests entirely on the
`TargetAllowList` in `RacingSim.uproject`, not on module types.

## Known blockers and assumptions

### BLOCKER-001 — no reference GPU worker

- blocker: Gates D, E and F specify thresholds on a named reference worker. None exists.
- owner: human project owner
- decision date: open
- note: the local RTX 3050 6 GB / 15.7 GB RAM cannot meet Gate E and is designated development-only (ADR-0003).

**Requirement specification, derived 2026-08-12.** This blocker was previously "evidence
needed: provider, region, instance type…", which is an open research task rather than a
decision. The requirements below are derived from the gates themselves, so the owner's
question narrows to *"can we buy something that meets this, yes or no"*.

These are **requirements, not measurements.** Nothing here has been benchmarked. Each
becomes a measured field only after a worker exists.

| Requirement | Derived from | Why this number |
|---|---|---|
| Sustained 60 fps at 1920×1080, packaged build | Gate E | Stated target |
| Unreal frame p95 ≤ 16.67 ms, p99 ≤ 22 ms on the benchmark lap | Gate E | 16.67 ms is the 60 fps budget |
| Hardware H.264 encode, NVENC or AMF | ADR-0001, PS2 | PixelStreaming2 pulls `NVCodecs`/`AMFCodecs`; software encode will not hold p95 |
| **Two independent encode sessions minimum** | ADR-0003 | One GPU process per session. Consumer GeForce drivers historically cap concurrent NVENC sessions — verify the cap on the exact SKU before buying, it is the single most likely thing to invalidate a choice |
| VRAM sized for Lumen + VSM + Nanite at 1080p | Gate D, `Docs/04-VisualPipeline.md` | The local 6 GB is already the reason this machine is dev-only. Treat 6 GB as a known-fail data point |
| ≥ 32 GB system RAM | BLOCKER-003 | 15.7 GB is measurably tight for cook and shader compilation |
| Region within the target latency envelope | Gate F | Input-to-photon p50 ≤ 80 ms, p95 ≤ 120 ms is **regional**; a worker in the wrong region fails Gate F regardless of GPU |

**What the owner must decide, in order:**

1. Is there a target user region? Gate F's thresholds are meaningless without one, and
   region constrains provider before GPU does.
2. Cloud instance or a physical box? A physical box removes per-hour cost and the
   NVENC session cap question if a professional card is used; it fixes the region.
3. How many concurrent sessions must one worker carry? This drives VRAM and the encoder
   cap, and it is a product question, not an engineering one.

**Verify before purchase, because it invalidates otherwise-correct choices:** the
concurrent NVENC session limit for the specific GPU and driver, and whether the
provider's virtualised GPU exposes hardware encode at all — some do not.

**What stays blocked until then:** Gates D, E, F; Epics 6 and 7; `STREAM-004`,
`STREAM-005`, `STREAM-006`. Epics 1–5 are unaffected — they are logic, validated by
Gates A/B/C, none of which reference the worker.

### BLOCKER-002 — LFS locking is inert — **MITIGATED 2026-08-12, decision still open**

- blocker: `lockable` attributes cannot be enforced without an LFS server, so hard constraint #7 rested on process discipline alone.
- **Mitigation implemented, because "process discipline" is not a control.**
  `.githooks/pre-commit` blocks any staged `.uasset`/`.umap` that is unclaimed, or
  claimed by someone other than the committer, against `Docs/AssetOwnership.tsv`.
  Activate with `git config core.hooksPath .githooks`.
- **Verified against all three cases, not just the happy path:**
  - unclaimed asset → `BLOCKED -- Content/Test/Probe.uasset has no ownership claim`
  - claimed by another → `BLOCKED -- ... is claimed by 'someone-else' (VEH-002), you are 'junyi'`
  - claimed by committer → commit succeeds
  The probe asset and its commit were removed afterwards; `git ls-files` still reports
  **0** tracked `.uasset`/`.umap`.
- **What this does not do.** It is not locking. It cannot coordinate across machines or
  clones, and `git commit --no-verify` bypasses it. It is a real gate on this clone,
  which is where the project works today (single machine, no remote). It converts an
  honour system into something that fails loudly at the moment of the mistake.
- **Side effect, caught and verified — `core.hooksPath` relocates the git-lfs hooks.**
  Setting `core.hooksPath=.githooks` moves hook resolution away from `.git/hooks`, where
  git-lfs installs `post-checkout`, `post-commit`, `post-merge` and `pre-push`. git-lfs
  reinstalled them into `.githooks/`, so they are now **tracked in the repository**.
  Each was inspected: all four are the stock two-line LFS shims
  (`git lfs <hook> "$@"` behind a `command -v git-lfs` guard) and nothing else. The
  project's own `pre-commit` was not touched — LFS does not install one.
  Consequence to know: **deleting `.githooks/` now also disables git-lfs.** If
  `core.hooksPath` is ever unset, re-run `git lfs install` so the hooks return to
  `.git/hooks`.
- owner: human project owner — **the decision is still yours.**
- evidence needed: a decision to add an LFS-capable remote, adopt Perforce, or formally
  accept process-only serialization now that it is at least enforced locally. See D-2.
- decision date: open

### BLOCKER-003 — memory pressure limits build throughput — **DOWNGRADED 2026-08-12, premise was stale**

- original blocker: UBT requested 14 parallel actions and was limited to **1** — only ~321 MB physical RAM was free at build time on a 15.7 GB machine.
- **That is no longer what happens.** Across this session's builds, Unreal Build
  Accelerator was granted **6, 7, 10 and 11** parallel actions on the same machine, with
  the same 15.7 GB physical and free RAM as low as 0.58 GB. The `1 action` observation
  was a point-in-time measurement taken when something else held memory, not a standing
  property of the hardware.
- **Why it works anyway:** UBA reports `UBA Storage capacity 40 GB` and backs its
  scheduling with disk, so it degrades throughput under pressure rather than serialising.
- **Real measured cost today:** editor incremental builds complete in 34–48 s; a full
  `BuildCookRun` including cook, pak and stage completes in 147–210 s. Neither is a
  blocker to graybox work.
- **What remains true:** the machine is genuinely memory-tight, and this will get worse
  with real shader compilation and content. It is a throughput risk for Epic 6, not an
  obstacle to Epics 1–5.
- owner: human project owner
- evidence needed: still worth an explicit answer on whether 16 GB is accepted through
  the graybox milestone — but this should be priced as "slower builds", not "blocked".
  See D-3.
- decision date: open

### ASSUMPTION-001 — node v24.18.0 runs the pinned signalling server — **RESOLVED, with a caveat**

- original wording said "upstream pins no node version". **That was wrong**: the
  repository root carries `NODE_VERSION` = `v22.14.0`. The local runtime is
  **v24.18.0**, two majors ahead.
- **Tested anyway, and it works.** Under v24.18.0: `npm ci` exit 0 (1,373 packages),
  `npm run build:all:cjs` exit 0, signalling server started and listened on 8888/8889/8080,
  a packaged UE streamer connected and joined, and the player page served HTTP 200.
- caveat, and the reason this is not simply closed: running two majors above a pin is
  an accepted risk, not a proven equivalence. Nothing here exercised the SFU
  (`mediasoup`'s postinstall was skipped, so its native worker was never built) and
  nothing exercised a browser WebRTC peer.
- **Recommendation:** pin the toolchain to v22.14.0 for anything deployed. Divergence
  is fine on a development box and should not be fine on a worker image.
- follow-up owner: `pixel-streaming-engineer`, at `STREAM-001`.

### NOTE-002 — upstream `config.json` contains a foreign absolute path

`SignallingWebServer/config.json` at pinned commit `48bff3b7` sets
`"http_root": "D:\\PixelStreamingInfrastructure\\SignallingWebServer\\www"` — a path
from Epic's build machine. It overrides the correct relative default and 404s every
static page until `--http_root` is passed. Any deployment automation must either pass
the flag or patch the config; patching means carrying a local modification of a
gitignored dependency, which is itself a thing to track.

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

### BLOCKER-005 — cook failed on a missing AssetManager rule — **RESOLVED**

- blocker: `BuildCookRun` cooked all 586 packages then failed with
  `LogGameFeatures: Error: Asset manager settings do not include a rule for assets
  of type GameFeatureData`, UAT exit 25.
- cause: `AllToolsets` (required by `Docs/09-UnrealMCP.md`) pulls
  `GameFeaturesToolset` → `GameFeatures`. The cooker runs `UnrealEditor-Cmd`, so
  editor plugins load during cook; `TargetAllowList` governs packaged targets only.
  The project had no `Config/` directory at all.
- resolution: `Config/DefaultGame.ini` AssetManager `PrimaryAssetTypesToScan` entry
  for `GameFeatureData`, `bIsEditorOnly=True`. Verified: cook completes, UnrealPak
  exits 0, error absent.
- follow-up caught in review: the entry was first written with
  `bIsEditorOnly=False`, which would have made packaged Game/Client targets try to
  resolve a class absent from those targets and trip
  `ensureMsgf(AssetBaseClassLoaded, ...)` at `AssetManagerTypes.cpp:82` on startup.
  Corrected before any packaged build existed.

### BLOCKER-006 — staging fails, cook and pak succeed — **RESOLVED 2026-08-12**

**Fix: stage outside `Documents\`.** Adding
`-stagingdirectory="$env:LOCALAPPDATA\RacingSimStage"` makes `BuildCookRun` succeed
with the stage-directory cleanup **enabled**. `-nocleanstage` is no longer required and
has been removed from the canonical command.

Evidence, and why it is conclusive:

- Run 1, into a fresh empty `%LOCALAPPDATA%\RacingSimStage`: `BUILD SUCCESSFUL`,
  `ExitCode=0`, 195.12 s. Not conclusive on its own — an empty directory barely
  exercises the delete path.
- Run 2, into that **same directory now holding 52 files**: `BUILD SUCCESSFUL`,
  `ExitCode=0`, 181.79 s. This is the conclusive one. A populated staging directory is
  exactly the state that failed three times under `Documents\`.
- Freshness re-verified: all five files under
  `Packaged/Windows/RacingSim/Content/Paks/` carry the timestamp of the run that
  produced them. The stale-pak side effect of `-nocleanstage` is gone.

**The variable is the path, not the cook.** Identical project, identical cook, identical
586 packages; only the staging root changed. That narrows the cause to something
scanning or holding files under `Documents\central-command\racing\Saved\` — a
file-sync or indexing agent on `Documents\`, or Defender scoped to that tree. The
precise agent was never identified and does not need to be: the fix does not depend on
knowing which one it was.

Residual note: the leading hypothesis is now **path-scoped**, not Defender-global. The
earlier suggestion to add a Defender exclusion was never tested and is unnecessary — no
system-level security setting was changed to resolve this.

**Gate A's packaging leg is now clean.** It no longer rests on a workaround.

---

#### Original blocker record, retained for history

- blocker: `BuildCookRun` now fails after a successful cook and pak:

  ```text
  Stage Failed. Failed to delete staging directory
    C:\Users\jun yi\Documents\central-command\racing\Saved\StagedBuilds\Windows
  AutomationTool exiting with ExitCode=102 (Error_FailedToDeleteStagingDirectory)
  ```

- **Reproduced twice. Not transient.** The first hypothesis — that the cook's own
  process held the handle and a stale partial tree was to blame — was **wrong**.
  `Saved/StagedBuilds` was verified empty (0 files), deleted cleanly and instantly
  with no lock, and the very next run failed identically at the same step. Whatever
  holds the directory does so reliably during the run.
- workaround: `-nocleanstage` skips the failing cleanup. With it the package
  succeeds (exit 0) and produces a valid archive. Flag confirmed at
  `ProjectParams.cs:2196` ("skip cleaning the stage directory").
- **Reproduced a third time on 2026-08-12 by `test-engineer`, with a sharper
  signature.** UAT exit 102 again, elapsed 231.14 s. The cook succeeded (586 packages,
  `LogCook: Display: Done!`) and UnrealPak/IoStore exited 0; only staging failed. The
  precise failure point, not previously recorded:

  ```text
  Cleaning Stage Directory: ...\Saved\StagedBuilds\Windows
  Failed to delete directory ...\Saved\StagedBuilds\Windows\Engine\Binaries\ThirdParty\DbgHelp in 10 attempts.
  Exception in System.Private.CoreLib: Access to the path '\\?\...\ThirdParty\DbgHelp' is denied.
  ```

  A single named subdirectory — `Engine\Binaries\ThirdParty\DbgHelp` — and an
  access-denied signature rather than a generic failure. That is a sharing-violation or
  permission pattern, which fits the Defender hypothesis.
  Caveat on this particular run: `Saved/StagedBuilds` was **not** empty beforehand
  (52 files, ~1.08 GB, dated 2026-08-10), unlike the earlier investigation.
- **Root cause remains unidentified.** Hypotheses, reordered by the 2026-08-12 evidence:
  1. Windows Defender real-time scanning holding freshly written files — **now the
     leading candidate**, on the access-denied signature at a single third-party
     binary directory.
  2. A file-sync or indexer agent on `Documents\`.
  3. UE's `DirectoryWatcher` — **demoted.** The `ReadDirectoryChangesW failed ...
     GetLastError code [6]` line was searched for in the fresh cook log
     (`Cook-2026.08.12-10.47.07.txt`, 2,493 lines) and **not found**. Only benign
     DLL-load `GetLastError=126` lines and a normal module-load line appear. Absence
     here does not disprove the hypothesis, but it was never confirmed present in the
     first place and must not be treated as corroborated.
- owner: technical director
- evidence needed: re-run **without** `-nocleanstage` after (a) excluding the project
  directory from Defender real-time protection, then (b) if that fails, using
  `-stagingdirectory=` to a path outside `Documents\`. Record which one changes the
  outcome. `handle.exe`/Process Explorer would identify the holder directly.
- risk if left — **observed, not theoretical, as of 2026-08-12.** The earlier wording
  of this line said the workaround was "acceptable for a project with no content."
  That was wrong. In the verified `-nocleanstage` run, `Packaged/Windows/RacingSim.exe`
  was rewritten (`LastWriteTime` 2026-08-12 10:51:43) while every `.pak`, `.ucas` and
  `.utoc` under `Packaged/Windows/RacingSim/Content/Paks` still carried
  `LastWriteTime` 2026-08-10 22:12–22:13. The cook step ran and reprocessed all 586
  packages that day; **the archive does not contain that cook's output.** A
  `-nocleanstage` archive is therefore not evidence of the cook that produced it, and
  must not be cited as gate evidence while this blocker is open.
- **Gate A is NOT claimed.** A packaged build now exists, but only via a workaround
  whose side effect is stale-file retention.

### NOTE-003 — subagent reports do not survive a single dispatch reliably

Observed 2026-08-12 across four dispatches of the mandatory review/test protocol.
**Three of the four returned a fragment instead of a report** — an opening line
("I'll start by reading the contract documents…"), a mid-sentence status
("Now let's check the archive artifacts."), or a hook-compliance preamble — despite
having done 39–60 tool calls and 7–19 minutes of real work each.

The work is not lost. Sending the agent a follow-up message asking it to write up what
it already gathered produces the full report without re-running the investigation.

Practical consequence for anyone running the ticket protocol: **budget two round trips
per agent**, and never interpret a fragment as "the agent found nothing". Re-dispatching
from scratch would repeat expensive cook and package runs for no reason.

The `pre:edit-write` and `pre:bash` fact-forcing gates fire inside subagents as well as
in the main session, which is the likely mechanism for at least the preamble case.

### NOTE-001 — UnrealBuildTool opens a non-loopback listener

- `UbaServer - Listening on 0.0.0.0:1345` during builds (Unreal Build Accelerator).
- Build-time only and never shipped, but it is a non-loopback listener on the development machine and belongs in the Gate G security record.
