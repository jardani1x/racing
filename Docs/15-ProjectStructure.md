# Project structure and conventions

**Module granularity decided 2026-08-12** by the project owner, resolving a
contradiction between this document and `Docs/Tickets.md` (finding N-2). The layout is
**two modules**: one runtime module carrying the five gameplay layers as folders, and a
separate test module.

- `RacingSim` — `Runtime`. Contains `Core/`, `Vehicle/`, `Race/`, `UI/`, `Streaming/`.
- `RacingSimTests` — `UncookedOnly`. Contains all automation specs.

Rationale: the five gameplay layers are folder conventions, not link-enforced
boundaries, because the boundaries are not yet proven by real code and premature
modularization creates churn when they shift. `Tests` **is** a real module, because
that is a correctness requirement rather than a preference — an `UncookedOnly`
module cannot be built into a packaged Game target, so test code physically cannot
ship.

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

  RacingSimTests/                 <- UncookedOnly module, never packaged
    RacingSimTests.Build.cs
    RacingSimTestsLog.h
    Core/                         <- mirrors the runtime layer folders
    Vehicle/
    Race/
    UI/
    Streaming/

Content/
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
  Tests/Maps/
  Developer/  # never referenced by shipping content

Plugins/RacingAutomation/
  Source/
  Content/

Scripts/
  Editor/
  Build/
  Test/
  Deploy/

Web/PixelStreamingFrontend/
  src/
  tests/

Docs/
  ADR/
  Reports/
```

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
