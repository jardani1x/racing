# Executive plan

## Decision

The project should be a cloud-rendered Unreal Engine 5.8.x application delivered through Pixel Streaming 2. It should not begin as a native browser-rendered game. Unreal remains responsible for rendering, vehicle physics, race state, audio, and the in-game UI; the browser is a WebRTC player/input client plus an optional web shell for authentication, settings, session allocation, and connection telemetry.

## Reality check

A Gran Turismo-class product is primarily a content, licensing, validation, and operations program, not just a coding project. Claude Code can accelerate scaffolding, C++, test automation, editor scripts, technical documentation, and controlled editor actions. It cannot replace:

- manufacturer and circuit licensing;
- licensed CAD/scan/source data;
- senior vehicle-dynamics calibration;
- hero vehicle/environment artists;
- audio recording and mix;
- sustained QA on real hardware and networks;
- legal and release approval.

The achievable agent-led goal is a polished, evidence-backed vertical slice and a production pipeline that can later scale with a real team and licensed content.

## Product definition for the vertical slice

- Original unbranded GT prototype.
- Original 3-5 km closed circuit with elevation, fast/slow corners, kerbs, runoff, barriers, timing gates, and one pit/start area.
- Single-player time trial; optional ghost or AI only after core correctness.
- Dry daytime preset.
- Desktop browser, keyboard and gamepad.
- 1080p60 target on a named reference GPU worker.
- Full race loop and polished HUD.
- Pixel Streaming 2 deployment with STUN/TURN, session allocation, observability, and cost controls.
- Repeatable build, package, automation, visual regression, performance, and soak tests.

## Design pillars

1. **Driving credibility**: stable, tunable vehicle behavior with measured telemetry and no frame-rate dependence.
2. **Photographic coherence**: correct scale, materials, exposure, lighting, reflections, shadows, motion, camera, and surface detail working together.
3. **Race correctness**: no skipped checkpoints, false laps, reverse finish counts, timer drift, or broken restart paths.
4. **Streaming responsiveness**: browser input remains usable under the target network envelope and reports connection quality.
5. **Evidence over claims**: every quality assertion points to a build, test report, screenshot comparison, trace, or human approval.
6. **License-ready architecture**: branded content can replace placeholders without rewriting gameplay systems.

## Non-goals for the first slice

- Dozens of branded cars.
- A copied real circuit.
- Online wheel-to-wheel human multiplayer.
- Dynamic weather, night/day transitions, damage simulation, career mode, economy, or live-service backend.
- Native client-side browser rendering.
- Console certification.

## Exit condition

The vertical slice exits only when `Docs/07-QualityGates.md` passes and the designated human owners approve visual quality, handling, legal status, and deployment readiness.
