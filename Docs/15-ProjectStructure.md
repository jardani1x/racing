# Project structure and conventions

**Module granularity decided 2026-08-12** by the project owner, resolving a
contradiction between this document and `Docs/Tickets.md` (finding N-2). The layout is
**two modules**: one runtime module carrying the five gameplay layers as folders, and a
separate test module.

- `RacingSim` — `Runtime`. Contains `Core/`, `Vehicle/`, `Race/`, `UI/`, `Streaming/`.
- `RacingSimTests` — `UncookedOnly`. Contains all automation specs.

Rationale: the five gameplay layers are folder conventions, not link-enforced
boundaries, because the boundaries are not yet proven by real code and premature
modularization creates churn when they shift. Splitting them into separate modules
later is a mechanical change. `Tests` **is** a real module, because that is a
correctness requirement rather than a preference — an `UncookedOnly` module cannot be
built into a packaged Game target, so test code physically cannot ship.

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
      RacingSimTypes.h
      RacingSimSettings.*
      RacingTelemetry.*
    Vehicle/
      RacingVehiclePawn.*
      RacingVehicleMovementFacade.*
      VehicleInputComponent.*
      VehicleTelemetryComponent.*
      VehicleResetComponent.*
      CarSpecDataAsset.*
    Race/
      RaceDirector.*
      RaceClock.*
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
