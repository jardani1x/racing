# Proposed project structure and conventions

```text
Source/RacingSim/
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
  Tests/
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
