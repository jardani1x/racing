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

- `ARaceDirector`: session orchestration. Owns a `URaceStateMachine` (see below) rather
  than being the state machine itself — RACE-001 split ownership from transition logic
  for testability (a plain `UObject` can be exhaustively tested under `-nullrhi` in a
  commandlet without a `UWorld`; an `AActor` cannot). Not yet implemented; the property
  that will hold the `URaceStateMachine` must be a `UPROPERTY` (a `UObject` is not
  GC-rooted by its outer alone).
- `URaceStateMachine` (**RACE-001, shipped**): the authoritative transition graph and
  clock owner. `Source/RacingSim/Race/RaceStateMachine.h` carries the full design
  rationale for the split from `ARaceDirector`.
- `URaceRulesetDataAsset` (**RACE-001, shipped**): typed countdown length and other
  state-machine tunables, per `CLAUDE.md`'s "no magic numbers" rule.
- `ATrackDefinitionActor`: centerline spline, track length, sectors, start/finish, grid, reset samples.
- `ATrackCheckpoint`: ordered trigger gate with crossing direction and width/height.
- `URaceProgressComponent`: expected checkpoint, lap, spline distance, progress, validity, penalties.
- `FRaceClock` (**RACE-001, shipped**): monotonic timing via timestamp subtraction, not
  accumulation — see `Source/RacingSim/Race/RaceClock.h`. Shipped as a plain struct, not
  a `UObject`/`URaceClock`: it has a mutating accessor and is internal race truth, not a
  Blueprint-facing contract; `URaceStateMachine` exposes the two numbers Blueprint/UMG
  need.
- `URaceResult`: immutable finish result and validation metadata.

State machine (**RACE-001, shipped** — collapses `Boot`/`Loading`/`Grid` into a single
`PreRace`, since none of the three yet has an owner or a distinct entry action; split
them out only when one gets real content, per `ERaceState`'s doc comment in
`RacingSimTypes.h`):

```text
PreRace -> Countdown -> Racing -> Finished -> Results
   ^           |           |          |          |
   +-----------+-- Restart (legal from every state, always lands in PreRace) --+
```

`Restart` is deliberately one edge from all five states rather than only from
`Results`: a restart legal only from the terminal state would leave a player stuck in
`Racing` after an abandon, and every caller would grow its own ad-hoc reset path. See
`ERaceTransition::Restart`'s doc comment for the full reasoning.

Every transition has preconditions, one owner, and one entry action, and every
transition has automation coverage. **No exit actions** (contrary to the "one exit
action" this section originally specified): `URaceStateMachine::CommitTransition`
implements entry actions only, so clock teardown (e.g. stopping the countdown clock)
is duplicated into each destination's entry action instead. `code-reviewer` flagged
this as a real but currently-harmless coupling risk (finding M5, RACE-001 review) —
the first new edge leaving `Countdown` must remember to stop the countdown clock in
its own entry action, since nothing will do it on the way out. Revisit if that
duplication grows past two call sites.

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
