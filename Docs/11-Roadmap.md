# Milestone roadmap

## Phase 0: feasibility, legal, and environment

Exit criteria:

- exact Unreal 5.8.x patch and toolchain recorded;
- reference GPU worker and target browser/network matrix selected;
- source control and binary-lock policy operational;
- Pixel Streaming 2 local spike connects with input;
- one packaged blank scene streams successfully;
- Unreal MCP local-only setup verified or documented as unavailable;
- asset provenance inventory completed;
- branded content explicitly blocked;
- build/test/cook/package commands discovered and recorded;
- backlog and architecture decisions approved.

## Phase 1: graybox driving slice

- original proxy car with Chaos Vehicles;
- input, camera, reset, telemetry;
- original graybox circuit and centerline;
- checkpoint/lap/timing/race-state systems;
- functional HUD;
- automation and packaged smoke tests.

Exit only when gameplay correctness and vehicle stability gates pass.

## Phase 2: browser vertical slice

- Pixel Streaming 2 frontend and session flow;
- hardware encoding and codec spike;
- browser input/gamepad;
- STUN/TURN external test;
- streaming metrics and reconnect;
- repeatable GPU-worker deployment.

Exit only when the browser matrix and latency/reliability targets pass for the agreed region.

## Phase 3: hero visual slice

- final-quality original car exterior and cockpit;
- final-quality hero circuit sector;
- car/environment master materials;
- final camera/audio direction;
- benchmark cameras and screenshot baselines;
- performance budget and trace review.

Exit only after human art approval and measured performance.

## Phase 4: full-lap content pass

- extend hero quality around the full circuit;
- World Partition/HLOD/PCG integration;
- complete collision, reset, track-limit, surface, audio, and lighting pass;
- full-lap visual and performance validation.

## Phase 5: hardening

- automation expansion;
- 30-minute and repeated-session soaks;
- crash recovery and telemetry;
- security, rate limits, cost limits, rollout/rollback;
- accessibility and controller UX;
- release documentation and operations drills.

## Phase 6: licensed content expansion

Only after signed agreements:

- import one licensed manufacturer vehicle as a separate content package;
- calibrate approved specs and audio;
- complete manufacturer approval loop;
- repeat visual, physics, performance, legal, and marketing gates;
- add more cars/tracks only after the first licensed pipeline is proven.
