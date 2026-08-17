# Project structure and conventions

**Module granularity decided 2026-08-12** by the project owner, resolving a
contradiction between this document and `Docs/Tickets.md` (finding N-2). The layout is
**two modules**: one runtime module carrying the five gameplay layers as folders, and a
separate test module.

- `RacingSim` — `Runtime`. Contains `Core/`, `Vehicle/`, `Race/`, `UI/`, `Streaming/`.
- `RacingSimTests` — `UncookedOnly`. Contains **all** automation specs, and is the only
  place an automation-test macro may appear. Enforced, not just stated — see
  *Test module and its enforcement* below.

Rationale: the five gameplay layers are folder conventions, not link-enforced
boundaries, because the boundaries are not yet proven by real code and premature
modularization creates churn when they shift. `Tests` **is** a real module, because
that is a correctness requirement rather than a preference — an `UncookedOnly`
module cannot be built into a packaged Game target.

**Wording corrected at TEST-001.** This paragraph previously ended "so test code
physically cannot ship". That was true of code *in that module* and false as a general
claim, which is CORE-001 finding N-2: `WITH_DEV_AUTOMATION_TESTS` is `1` in a
Development Game target, so an `IMPLEMENT_SIMPLE_AUTOMATION_TEST` written under
`Source/RacingSim/` compiles into `RacingSim.exe`, and the `.target`-receipt and
staging-manifest checks would not notice — they assert which *modules* were compiled,
and `RacingSim` is supposed to be one. The module type is a necessary control, not a
sufficient one.

**Finality (CORE-002, closes N-3):** this decision was reopenable only until the
first real `UObject` existed to exercise it. `URacingSimSettings`
(`Core/RacingSimSettings.h`) is that `UObject`, so the two-module layout above is now
final for the gameplay layers — a later split into separate modules is no longer a
"mechanical change" available on demand: it means moving a `UDeveloperSettings`
subclass and everything that depends on it (including `RacingSimTests`) across a
module boundary, which is a real migration, not a folder rename. If the layers are
ever promoted to real modules, treat it as its own ticket with its own review.

Consequence to accept knowingly: **architecture boundaries between the five gameplay
layers are not compile-enforced.** Nothing stops `Streaming/` from including a `Race/`
header. `CLAUDE.md` states that no race truth lives in `Streaming`; under this layout
that rule is enforced by review, not by the linker. If it is violated in practice,
promote the layers to real modules.

```text
Source/
  RacingSim/                      <- Runtime module
    RacingSim.Build.cs
    Core/
      RacingSimLog.*                <- CORE-001
      RacingSimTypes.h              <- CORE-002: shared enums, penalty summary
      RacingSimUnits.h              <- CORE-002: cm/SI conversion policy, header-only
      RacingSimBuildId.*            <- CORE-002: build ID + version stamp contract
      RacingSimSettings.*           <- CORE-002: UDeveloperSettings, config=Game
      RacingTelemetry.*             <- CORE-002: telemetry data contracts
    Vehicle/
      RacingVehiclePawn.*
      RacingVehicleMovementFacade.*
      VehicleInputComponent.*
      VehicleTelemetryComponent.*
      VehicleResetComponent.*
      CarSpecDataAsset.*
    Race/
      RaceClock.*                   <- RACE-001: monotonic timestamp-subtraction clock
      RaceStateMachine.*             <- RACE-001: authored transition graph, track-agnostic
      RaceRulesetDataAsset.*         <- RACE-001: countdown length + other state-machine tunables
      RaceDirector.*                 <- planned, not yet implemented. Will own a URaceStateMachine
                                         (Docs/01-Architecture.md's "session orchestration" role);
                                         RACE-001's header documents the split in detail.
      TrackDefinitionActor.*
      TrackCheckpoint.*
      RaceProgressComponent.*
      RaceResult.*
    UI/
      RaceHUDViewModel.*
      RaceHUDController.*
    Streaming/
      RacingStreamingBridge.*
      StreamingTelemetry.*

  RacingSimTests/                 <- UncookedOnly module, never packaged.
    RacingSimTests.Build.cs          The ONLY location an automation-test macro may
    RacingSimTests.cpp               appear. See "Test module and its enforcement".
    RacingSimTestsLog.h
    Core/                         <- mirrors the runtime layer folders
      RacingSimLogSpec.cpp             <- CORE-001: category names/distinctness
      RacingSimLogSuppressionSpec.cpp  <- TEST-001: categories reachable by name
                                          through the log suppression system
      RacingSimUnitsSpec.cpp           <- CORE-002
      RacingSimVersionSpec.cpp         <- CORE-002: build ID + version stamp
      RacingSimSettingsSpec.cpp        <- CORE-002
      RacingTelemetrySpec.cpp          <- CORE-002
    Race/
      RaceClockSpec.cpp                <- RACE-001
      RaceStateMachineSpec.cpp         <- RACE-001
    Tests/                        <- TEST-001: tests about the test infrastructure
      AutomationTestPlacementSpec.cpp  <- no RacingSim.* test outside this module
      NonShippingArtifactSpec.cpp      <- .target receipts + DirectoriesToNeverCook
    Vehicle/
    UI/
    Streaming/

Content/                          <- cooked package root /Game
  Cars/Prototype/
    Data/
    Meshes/
    Materials/
    Blueprints/
    Audio/
  Tracks/Prototype/
    Maps/
    Geometry/
    Materials/
    PCG/
    Data/
  UI/
  Tests/          # /Game/Tests -- NEVER COOKED. TEST-001 added
    README.md     # DirectoriesToNeverCook for this path in
    Maps/         # Config/DefaultGame.ini. Exists, empty of assets today.
  Developer/      # /Game/Developer -- also in DirectoriesToNeverCook.
                  # "never referenced by shipping content" is now enforced, not asserted.

Plugins/RacingAutomation/
  Source/
  Content/

Scripts/
  Editor/
  Build/
  Test/
    Check-NonShippingArtifacts.ps1   <- TEST-001. Receipt/config/pak checks.
  Deploy/

Web/PixelStreamingFrontend/
  src/
  tests/

Docs/
  ADR/
  Reports/
```

## Test module and its enforcement

Added at `TEST-001`, closing `CORE-001` findings `N-2` and `N-4`.

`RacingSimTests` is `UncookedOnly` and is declared as such in `RacingSim.uproject`;
`Source/RacingSim.Target.cs` does not list it, and `Source/RacingSimEditor.Target.cs`
does. Its `Build.cs` depends privately on `RacingSim`. **Nothing in `RacingSim` may ever
depend on `RacingSimTests`.**

Every test carries `EAutomationTestFlags::SmokeFilter` plus `EditorContext` and
`CommandletContext`. That is a rule, not a habit: the project's only documented
automation command is `Automation RunFilter Smoke` (`Docs/Environment.md`), so a test on
another filter sits in the tree, is counted as coverage by later tickets, and never runs.
Prove discoverability with a `RunFilter` log line, never with `RunTests <name>`.

### The three enforcement layers

| Layer | Mechanism | Fails as |
|---|---|---|
| Source scan | `EnforceNoAutomationTestsInRuntimeModule()` in `Source/RacingSim/RacingSim.Build.cs` — scans every `.h/.cpp/.inl` under `Source/RacingSim/` for automation-test-declaring macros, after stripping comments | `BuildException`, on **both** Editor and Game targets |
| Registry check | `RacingSim.Tests.AutomationTestPlacement` — walks `FAutomationTestFramework::GetValidTestNames` and asserts every `RacingSim.*` test's `__FILE__` is under `Source/RacingSimTests/` | automation test failure |
| Artifact check | `RacingSim.Tests.NonShippingArtifacts` and `Scripts/Test/Check-NonShippingArtifacts.ps1` — `.target` receipts and `DirectoriesToNeverCook`, plus pak/binary byte search in the script | test failure, and script exit 1 |

The source scan adds every file it reads to `ModuleRules.ExternalDependencies`
(`ModuleRules.cs:1437`, consumed at `UEBuildTarget.cs:3460`), so editing any runtime-module
file invalidates UBT's makefile and re-runs the scan. Without that line the scan would
only re-run when the module's *file list* changed, and adding a banned macro to an
existing file would slip through — the check would silently stop checking, which is
precisely the decay `CORE-001`'s reviewer predicted for the hand-typed receipt check.

Each layer has a stated blind spot and they do not overlap: the scan cannot see a test
registered without a macro; the registry check cannot see a test whose flags exclude it
from the current application context; the artifact check is about modules and content,
not about code placement. `Docs/01-Architecture.md` carries the same table with the
reasoning.

### Test-only content

`Content/Tests/` (`/Game/Tests`) and `Content/Developer/` (`/Game/Developer`) are listed
in `DirectoriesToNeverCook` in `Config/DefaultGame.ini`. That setting excludes them
*even when shipping content references them*, which is the case that matters: a
functional-test map legitimately references real gameplay assets.

**Honest limit, recorded rather than glossed:** no cooked asset exists under `/Game/Tests`
yet, so the pak-side check currently verifies an exclusion with no subject. Its positive
control (a known string that must be found) passes, so the search itself is proven — but
the first ticket to add a functional-test map must re-run
`Scripts/Test/Check-NonShippingArtifacts.ps1 -Mode Pak` against a fresh package, because
that is the first run capable of failing.

## Naming

Use Unreal conventions consistently, for example:

- `BP_` Blueprint classes;
- `DA_` DataAssets;
- `M_` master materials, `MI_` instances;
- `T_` textures;
- `SM_` static meshes, `SK_` skeletal meshes;
- `WBP_` widgets;
- `SFX_` sound effects;
- `MAP_` maps;
- `FT_` functional test actors/assets where useful.

Do not encode manufacturer names in generic gameplay class names. A licensed vehicle should be data/content, not a new vehicle code fork.

## Versioning

Every competitive result records:

- game build ID;
- engine patch;
- track definition/version hash;
- car spec/tune version;
- physics policy version;
- assist preset;
- input type;
- validity/penalty state.

This prevents leaderboard comparisons across incompatible builds.

**Implemented by CORE-002** as `FRacingSimVersionStamp` in
`Source/RacingSim/Core/RacingSimBuildId.h` — one struct carrying all eight items, so a
result cannot be written with half of them. `MakeCurrent()` fills only what this build
genuinely knows (build ID, engine patch and changelist, physics policy version, assist
preset); track, car, ruleset, input type and validity stay unpopulated until
`TRACK-001`, `VEH-003`, `RACE-001`, `VEH-001` and `RACE-002` fill them, and
`IsPublishable()` refuses a stamp that still has holes.

Game build ID scheme (`ERacingBuildIdScheme`):

- `Derived` — `<channel>-<projectversion>-<engineversion>+<changelist>-<config>-<targettype>`,
  e.g. `dev-0.1.0-5.8.1+56057345-Development-Editor`. Always available, never
  authoritative: two different local source trees on one engine patch produce the same
  string.
- `Explicit` — stamped by CI into `URacingSimSettings::ExplicitBuildId`. The only scheme
  a published leaderboard may accept (`FRacingSimBuildId::bIsAuthoritative`).

Also implemented: the **units policy** in `Source/RacingSim/Core/RacingSimUnits.h`.
Storage and simulation are Unreal centimetres everywhere; SI and display units exist
only at presentation and at authored-data load. Variables not in Unreal units carry the
unit in the name.
