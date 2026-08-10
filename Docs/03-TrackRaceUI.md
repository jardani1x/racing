# Track, checkpoints, laps, game loop, and UI

## Original circuit design

Create a fictional circuit rather than reproducing Spa or another licensed venue. The reference images suggest elevation, long sight lines, fast commitment corners, substantial safety infrastructure, and dense trackside detail. Capture those qualities without copying the geometry, name, signage, paint scheme, or landmarks.

The first circuit should include:

- approximately 3-5 km of centerline;
- meaningful elevation change;
- a start/finish straight and grid;
- a mix of low-, medium-, and high-speed corners;
- legal racing surface, kerbs, runoff, grass/gravel, walls, barriers, fencing, marshal points, drainage, and reset zones;
- a hero sector for early visual validation;
- deterministic benchmark cameras.

## Track representation

`ATrackDefinitionActor` owns:

- a centerline spline with monotonically increasing arc length;
- track width samples or left/right boundary splines;
- sector boundaries;
- ordered checkpoint IDs;
- start/finish plane and valid crossing direction;
- grid and pit/start poses;
- safe reset poses sampled along the legal route;
- surface metadata and track-limit zones;
- total length and version hash.

Road rendering may use spline meshes, authored modular pieces, or a DCC-generated continuous mesh. Collision should be simplified and tested separately from visual detail.

## Checkpoint and lap algorithm

Each vehicle tracks `ExpectedCheckpointId`.

1. A checkpoint trigger evaluates the vehicle's previous/current position against its plane.
2. Crossing must be in the allowed direction.
3. Only the expected checkpoint advances progress.
4. A skipped or out-of-order gate invalidates the current lap and records a reason.
5. The finish line counts a lap only after the final required checkpoint and a valid forward crossing.
6. Nearest centerline spline distance supplies continuous progress for ranking; it never replaces ordered checkpoint validation.
7. Race progress is ordered by `lap * trackLength + splineDistance`, with safeguards for pit/reset/teleport cases.
8. Reset returns the car to the most recent safe valid sample and may invalidate or penalize the lap according to the ruleset.

Test fast crossings, wide crossings, reverse crossings, spins on the line, teleport/reset, simultaneous triggers, and high-latency input.

## Timing

- Use an authoritative monotonic clock inside Unreal.
- Store raw duration with high precision; format only in UI.
- Capture start, finish, sector splits, best lap, personal best, and delta.
- Pause/menu behavior must be explicit; a streamed public session should not accidentally pause shared process timing.
- Results include track version, car tune version, assist state, validity, and build ID.

## Race state machine

### Loading
Validate map, track definition, car data, and required assets.

### Grid
Spawn/possess the vehicle, lock controls, reset timing and HUD.

### Countdown
Show 3-2-1-Go, pre-warm required streaming/render assets, ignore drive input until the configured release moment.

### Racing
Enable drive input, start the authoritative clock, validate progress and track limits.

### Finished
Freeze the result once, disable further lap counting, allow a controlled coast/finish presentation.

### Results
Show final/best time, validity, splits, restart and exit controls.

### Restart
Perform a complete state reset without reusing stale timers, delegates, input, or checkpoint state.

## HUD

Required in-stream UMG elements:

- speed with configurable km/h or mph;
- gear and RPM/tachometer;
- current lap / total laps or time-trial status;
- current, best, and last lap;
- live delta to best/ghost when available;
- countdown and finish banner;
- position only when opponents exist;
- assist and warning indicators;
- restart/pause/results menu;
- optional compact network-quality icon sourced from the streaming telemetry layer.

Keep the race HUD in Unreal to avoid video/UI desynchronization. The browser shell can own login, queue, region, stream resolution, bitrate preset, fullscreen, and troubleshooting panels.

## Functional tests

- valid ordered lap increments exactly once;
- skipped checkpoint never yields a valid lap;
- reverse finish crossing never increments;
- spinning across a gate cannot double-trigger;
- restart restores all state and input;
- timer never moves backward and does not drift with render rate;
- HUD view model matches authoritative state;
- result submission rejects invalid build/track/tune metadata;
- reset chooses a safe legal pose and prevents immediate duplicate gate triggers.
