# System architecture

## Deployment topology

```text
Desktop browser
  - Web/TypeScript shell
  - WebRTC video/audio player
  - keyboard/gamepad input
  - auth, session status, connection metrics
           |
           | HTTPS / WebRTC
           v
Edge/API gateway
  - authentication and rate limits
  - session broker
  - short-lived TURN credentials
           |
           +------ Signalling service ------ STUN/TURN
           |
           v
GPU worker allocated to one independent player session
  - packaged Unreal Engine 5.8.x application
  - Pixel Streaming 2 plugin
  - vehicle simulation and race authority
  - audio and UMG HUD
  - telemetry exporter
           |
           +------ profiles / leaderboards / replay metadata
           +------ logs / metrics / traces / crash reports
```

A single Unreal instance can serve multiple viewers of one shared scene, but an independent racing session normally needs a separate Unreal process. Design the broker and autoscaler around that constraint.

## Unreal module boundaries

### Core

- shared enums and structs;
- project settings;
- logging categories and structured events;
- unit conversion helpers;
- telemetry contracts;
- save/profile interfaces.

### Vehicle

Proposed types:

- `ARacingVehiclePawn`: possession, visual assembly, components, reset interface.
- `URacingVehicleMovementFacade`: stable project-facing interface over Chaos/configuration.
- `UCarSpecDataAsset`: mass, dimensions, torque curve, gearing, differential, brakes, suspension, steering, tires, aero, assists.
- `UVehicleInputComponent`: Enhanced Input mapping and normalized commands.
- `UVehicleTelemetryComponent`: speed, wheel speeds, slips, loads, engine, gear, controls, position, and health flags.
- `UVehicleResetComponent`: safe reset to the most recent valid track point.

Do not let race or UI code depend directly on volatile Chaos internals. The facade and telemetry contracts make a later tire/physics upgrade possible.

### Race

Proposed types:

- `ARaceDirector`: authoritative state machine and session orchestration.
- `ATrackDefinitionActor`: centerline spline, track length, sectors, start/finish, grid, reset samples.
- `ATrackCheckpoint`: ordered trigger gate with crossing direction and width/height.
- `URaceProgressComponent`: expected checkpoint, lap, spline distance, progress, validity, penalties.
- `URaceClock`: monotonic timing and split/delta calculations.
- `URaceResult`: immutable finish result and validation metadata.

State machine:

```text
Boot -> Loading -> Grid -> Countdown -> Racing -> Finished -> Results
                                      ^                  |
                                      +----- Restart ----+
```

Every transition has preconditions, one owner, one entry action, one exit action, and automation coverage.

### UI

- UMG is the source of truth for race HUD and timing because it is rendered in the same Unreal frame as the game.
- Use MVVM or a small explicit view-model layer; widgets do not query the world every frame.
- Browser HTML may own account, region, quality settings, queue/session state, and connection diagnostics, but never authoritative race timing.

### Streaming

- Wrap Pixel Streaming messages behind a project interface.
- Keep input normalization in the Vehicle module.
- Expose connection state, selected codec, bitrate, packet loss, RTT, and reconnect state to telemetry.
- Never let a browser message bypass race-state validation.

### Tests

- low-level tests for pure calculations;
- automation specs for state and validation;
- functional tests for maps, input, checkpoints, UI, and reset;
- screenshot tests for benchmark cameras;
- Gauntlet or equivalent orchestration for packaged sessions;
- soak/performance tests with recorded input.

## C++ / Blueprint split

Use C++ for simulation, data validation, timekeeping, checkpoint order, race state, telemetry, browser-message validation, and automation. Use Blueprint for vehicle assembly, camera rigs, VFX, audio routing, environment assembly, art tuning, and UMG layout. Parameters live in typed DataAssets or config so art and handling work does not require recompilation.

## Data flow

```text
Input device/browser
    -> normalized input command
    -> vehicle movement facade
    -> Chaos/custom vehicle simulation
    -> telemetry component
    -> race progress + HUD view model + recorder

Track trigger/spline
    -> race progress validation
    -> race clock/state machine
    -> immutable result
    -> HUD + backend submission
```

## Future multiplayer boundary

Do not add human multiplayer to the first slice. A future design would separate an authoritative dedicated race server from per-player Pixel Streaming render workers and would require network prediction, reconciliation, anti-cheat, and a materially different scaling model.
