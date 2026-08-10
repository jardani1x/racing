# Initial dependency-ordered backlog

## Epic 0: environment and governance

- `ENV-001` Record exact Unreal/toolchain/reference worker/browser matrix.
- `ENV-002` Configure source control, LFS/locks or Perforce, ignore rules, and clean clone test.
- `ENV-003` Enable/verify required plugins and production plugin exclusions.
- `ENV-004` Discover and document build/test/cook/package commands.
- `ENV-005` Verify local Unreal MCP and generate Claude Code config.
- `LEGAL-001` Inventory/quarantine assets and initialize license ledger.
- `ARCH-001` Record Pixel Streaming 2 architecture and scaling ADR.

## Epic 1: project skeleton

- `CORE-001` Create module/folder structure and logging categories.
- `CORE-002` Add settings, build ID, units, and telemetry contracts.
- `TEST-001` Create test plugin/module and first smoke test.
- `CORE-003` Add DataAsset validation framework.

## Epic 2: vehicle graybox

- `VEH-001` Input mappings for keyboard/gamepad.
- `VEH-002` Prototype chassis/wheels/collision and Chaos baseline.
- `VEH-003` Engine/transmission/differential/brakes/steering/suspension tune data.
- `VEH-004` Telemetry and failure detection.
- `VEH-005` Camera and safe reset.
- `VEH-006` Recorded manoeuvre tests and 30-minute soak controller.

## Epic 3: track and race

- `TRACK-001` Original circuit graybox and centerline definition.
- `TRACK-002` Ordered checkpoint gates and crossing direction.
- `RACE-001` Race state machine and monotonic clock.
- `RACE-002` Lap/sector/progress/validity logic.
- `RACE-003` Results, restart, and metadata.
- `RACE-004` Shortcut/reverse/double-trigger/reset automation matrix.

## Epic 4: HUD

- `UI-001` HUD view model and data contract.
- `UI-002` Speed/RPM/gear/lap/time/delta/countdown/results.
- `UI-003` Input prompts, settings, restart flow, accessibility baseline.
- `UI-004` HUD functional and screenshot tests.

## Epic 5: Pixel Streaming

- `STREAM-001` Local packaged Pixel Streaming 2 connection.
- `STREAM-002` Browser frontend shell and versioned custom messages.
- `STREAM-003` Gamepad/focus/reconnect tests.
- `STREAM-004` External STUN/TURN test.
- `STREAM-005` Session broker/worker lifecycle spike.
- `STREAM-006` WebRTC telemetry and latency measurement.

## Epic 6: visual vertical slice

- `ART-001` Hero car source/provenance and import pipeline.
- `ART-002` Car material family and turntable benchmark.
- `ART-003` Hero circuit sector and environment material family.
- `ART-004` Lighting/post/camera benchmark.
- `ART-005` Audio vertical slice.
- `PERF-001` Reference trace, budgets, and optimization pass.
- `VIS-001` Screenshot baseline and human art gate.

## Epic 7: hardening and deployment

- `OPS-001` Repeatable worker image/deployment.
- `OPS-002` Auth, rate limit, session timeout, cost guardrails.
- `OPS-003` Metrics/logs/crashes/alerts.
- `OPS-004` 100-cycle session test and cleanup.
- `SEC-001` production exposure and secret audit.
- `REL-001` full A-H quality-gate report and release rollback drill.
