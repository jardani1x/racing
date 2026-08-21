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
- `ATrackDefinitionActor` (**TRACK-001/TRACK-002, shipped**): centerline spline, track
  length, sectors, start/finish, grid, reset samples, and the baked ordered gate set.
- `FRacingCheckpointGate` / `FRacingCheckpointGateSet` (**TRACK-002, shipped** — this
  section previously specified an `ATrackCheckpoint` actor; corrected at RACE-002,
  closing TRACK-002 finding M5): a checkpoint gate is a **world-free `USTRUCT`**, not an
  actor with a trigger volume. It is a bounded rectangle standing across the track at a
  fixed arc length, with a plane normal derived from the centerline's direction of travel
  and an authored legal crossing direction (`ERacingGateDirection`). Crossings are tested
  as a **segment-versus-plane intersection** over the vehicle's previous/current position
  (`FRacingCheckpointGateSet::EvaluateCrossings`), never as a volume overlap — at 300 km/h
  a car covers 139 cm per 60 Hz tick and over 800 cm in a 100 ms hitch, so a gate thin
  enough to be a line is a gate an overlap steps straight over. The extent test is applied
  to the intersection point, which is what distinguishes "went through the gate" from
  "went round the outside of it" (`ERacingGateCrossing::OutsideExtent`). The set publishes
  the ORDER; it holds no per-car cursor. See
  `Source/RacingSim/Race/TrackCheckpointGate.h`.
- `URaceLapTracker` (**RACE-002, shipped** — sketched in earlier drafts of this document
  as `URaceProgressComponent`): expected checkpoint, lap count, sector splits, spline
  progress and per-lap validity, one instance per competitor. A plain `UObject` rather
  than a `UActorComponent`, for the reason `URaceStateMachine` gives and one more:
  `Docs/Environment.md` records that a `SmokeFilter` automation test in this project
  cannot construct a non-template actor at all, so a component would have made every
  lap-ordering rule testable only through a `Product` gate that cannot complete on the
  reference machine. It consumes a **copy** of the baked gate set taken once at
  configuration, so per-tick evaluation never re-enters the track actor's
  game-thread-only lazy bake. See `Source/RacingSim/Race/RaceLapTracker.h`.
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

#### Rule: automation tests live only in `RacingSimTests`

**Every automation-test-declaring macro — `IMPLEMENT_SIMPLE_AUTOMATION_TEST`,
`IMPLEMENT_COMPLEX_AUTOMATION_TEST`, `DEFINE_SPEC`, `BEGIN_DEFINE_SPEC`,
`IMPLEMENT_BDD_AUTOMATION_TEST`, `IMPLEMENT_NETWORKED_AUTOMATION_TEST`, their
`_CUSTOM_`/`_PRIVATE` variants, and `DEFINE_LATENT_AUTOMATION_COMMAND*` — may appear
only under `Source/RacingSimTests/`. Never under `Source/RacingSim/`.**

Test-only content follows the same rule and lives under `Content/Tests/`
(`/Game/Tests`), which `Config/DefaultGame.ini` lists in `DirectoriesToNeverCook`.

**Why the module boundary is not enough.** `RacingSimTests` is `UncookedOnly`, so it is
not built into a cooked Game or Client target. That guarantee is about *that module*, not
about test code in general. `WITH_DEV_AUTOMATION_TESTS` is `1` in a **Development Game**
target, so an `IMPLEMENT_SIMPLE_AUTOMATION_TEST` written inside `Source/RacingSim/`
compiles straight into `RacingSim.exe` — and every check CORE-001 used would still pass,
because they assert which *modules* were compiled and `RacingSim` is supposed to be one
of them. (CORE-001 finding N-2.)

**Enforcement.** Three layers, deliberately different in kind so they do not share a
blind spot:

| Layer | Where | Catches | Blind spot |
|---|---|---|---|
| Source scan, fails the build | `Source/RacingSim/RacingSim.Build.cs`, `EnforceNoAutomationTestsInRuntimeModule()` | any banned macro in any `.h/.cpp/.inl` under `Source/RacingSim/`, on **both** Editor and Game targets | a test registered without a macro; token pasting |
| Registry check, fails a test | `RacingSim.Tests.AutomationTestPlacement` | any registered `RacingSim.*` test whose source file is not under `Source/RacingSimTests/`, including hand-rolled registrations | tests whose flags exclude them from the current application context |
| Receipt + pak check | `RacingSim.Tests.NonShippingArtifacts`, `Scripts/Test/Check-NonShippingArtifacts.ps1` | the test **module** entering the Game target; test **content** entering the pak | code moved into the runtime module, which is layer 1's job |

The source scan registers every file it reads in `ModuleRules.ExternalDependencies`, so
editing any runtime-module file invalidates UBT's makefile and re-runs the scan. Without
that, the check would only run when the module's file list changed, and adding a macro to
an existing file would slip past — a check that silently stops checking.

**If you need a test helper in the runtime module**, put the helper in
`Source/RacingSim/` and the test that drives it in `Source/RacingSimTests/`. The helper
must be code the shipping game would be willing to carry; if it would not be, it is a
test and belongs in the test module.

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

Vehicle position (previous, current)
    -> FRacingCheckpointGateSet::EvaluateCrossings   (direction, per gate)
    -> URaceLapTracker                                (order, lap, sector, validity)
    -> URaceStateMachine's monotonic clock            (durations only, never wall time)
    -> immutable result
    -> HUD view model + backend submission
```

Two rules constrain that chain and neither is negotiable. **Ordered checkpoint gates
plus a valid crossing direction authorise a lap; continuous spline distance never
does** — distance ranks cars, drives a progress bar and splits sectors inside a lap the
gates already authorised, and a car that is reset, teleported or driven backwards past
the line will wrap its distance without having driven a lap. And **durations are
stored, never formatted**: `Race/` produces seconds as `double` from one monotonic
source, and `UI/` is the only layer that turns 83.456 into "1:23.456".

## Future multiplayer boundary

Do not add human multiplayer to the first slice. A future design would separate an authoritative dedicated race server from per-player Pixel Streaming render workers and would require network prediction, reconciliation, anti-cheat, and a materially different scaling model.
