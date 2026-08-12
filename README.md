# Unreal Racing Simulation + Claude Code Starter Pack

This pack turns the racing-game brief into a staged, testable Unreal Engine project that Claude Code can help implement.

## The architectural decision

Build a packaged Unreal Engine 5.8.x application on a GPU server and stream it to a browser with Pixel Streaming 2. The browser receives video and audio over WebRTC and returns keyboard, mouse, touch, or gamepad input. It does not run the Unreal renderer or vehicle simulation locally.

That choice preserves Unreal's high-end rendering stack, but it also means that each independent player session normally consumes a GPU-backed Unreal process. Treat cloud capacity, TURN networking, stream latency, and cost as first-class systems.

## Scope that can actually reach a high standard

The first production target is a vertical slice, not a full Gran Turismo-scale product:

- One original, unbranded GT-style prototype car.
- One original closed circuit, approximately 3-5 km, with elevation and varied corners.
- One daytime lighting preset and dry weather.
- Single-player time trial, optionally with an AI opponent or ghost after the core loop is stable.
- Browser delivery at 1920x1080 and 60 fps on a defined reference GPU worker.
- Keyboard and gamepad support.
- Complete race loop: load, grid, countdown, race, checkpoints, laps, finish, results, restart.
- Instrumented build, automated tests, visual regression checks, performance traces, and a deployment runbook.

Real Ferrari, Lamborghini, Bugatti, Ford, Porsche, or other branded vehicles must remain out of the repository until written permissions cover the name, marks, vehicle shape, model data, livery, audio, territories, platforms, marketing, and approval process. Use placeholders throughout the prototype.

## Quick start

1. Install and pin one Unreal Engine 5.8.x patch. Do not upgrade the engine mid-milestone.
2. Create a blank C++ Games project named `RacingSim`.
3. Copy this pack into the project root, beside `RacingSim.uproject`.
4. Enable the runtime/editor plugins described in `Docs/09-UnrealMCP.md` and `Docs/05-PixelStreaming.md`.
5. Generate the Unreal MCP client configuration from the Unreal Editor rather than hand-writing it.
6. Start Claude Code from the project root.
7. Paste the contents of `PROMPT_TO_START.md` into Claude Code.
8. Require the agent to complete Phase 0 and present an environment report before it edits gameplay code or assets.

## Recommended repository layout

```text
RacingSim/
  RacingSim.uproject
  Source/
    RacingSim/          # Runtime module
      Core/
      Vehicle/
      Race/
      UI/
      Streaming/
    RacingSimTests/     # UncookedOnly module - never packaged
  Plugins/RacingAutomation/
  Content/
    Cars/Prototype/
    Tracks/Prototype/
    Materials/
    UI/
    Tests/
  Config/
  Build/
  Scripts/
  Web/PixelStreamingFrontend/
  Docs/
  .claude/
    agents/
    rules/
  CLAUDE.md
```

## Pack contents

- `CLAUDE.md`: always-on project contract, intentionally compact.
- `PROMPT_TO_START.md`: the initial orchestration prompt.
- `.claude/agents/`: specialist implementation, review, testing, performance, streaming, art-pipeline, and licensing subagents.
- `.claude/rules/`: path-scoped code and asset rules.
- `Docs/`: architecture, physics, track/race/UI, rendering, streaming, agent workflow, acceptance gates, licensing, source control, backlog, and visual brief.

## What the agent must never claim

The agent must not say a build is "production grade" merely because it compiles or looks good in one screenshot. Production readiness requires all gates in `Docs/07-QualityGates.md`, evidence from a packaged build on the reference hardware, and human sign-off for visual quality, handling, legal clearance, and release operations.
