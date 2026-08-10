# Pixel Streaming 2 deployment guide

## Why this is the browser architecture

The Unreal application runs on a GPU host. Pixel Streaming encodes its frames and audio, sends them to a modern browser over WebRTC, and routes browser input back to Unreal. This retains Unreal's rendering and simulation fidelity. It also creates a server-cost and latency model very different from a downloaded game.

## Development setup

- Pin Unreal Engine 5.8.x.
- Enable Pixel Streaming 2 in the project.
- Use the official Pixel Streaming Infrastructure branch that matches the engine version.
- Run a packaged or standalone build, not only PIE, for meaningful latency/performance checks.
- Validate keyboard and gamepad first; define touch separately.
- Use a GPU with supported hardware video encoding.
- Record browser, OS, GPU, driver, codec, resolution, target bitrate, and launch arguments in `Docs/Environment.md`.

## Production services

The reference signalling/web server is a starting point, not a complete production platform. Add:

- authenticated HTTPS entry point;
- session broker and allocation API;
- GPU-worker autoscaling and warm pool;
- health checks and crash recovery;
- STUN/TURN with short-lived credentials;
- queue/wait-state UX;
- idle/session timeout and cost guardrails;
- regional placement near users;
- logs, metrics, WebRTC stats, crash reports, and alerting;
- rate limiting, abuse prevention, and secret management;
- versioned frontend and game-build rollout/rollback.

## Session model

Use one Unreal process per independent player session for the initial product. Multiple viewers may observe one process, but they would share that same simulation. Do not treat a one-to-many SFU setup as independent multiplayer.

A worker lifecycle should be:

```text
Provision or claim warm worker
  -> start exact packaged build
  -> register healthy streamer
  -> authenticate/attach one player
  -> run session
  -> persist result/telemetry
  -> disconnect and scrub state
  -> terminate or return a verified clean worker
```

## Networking

- Direct WebRTC connectivity can fail behind NAT, enterprise firewalls, or mobile networks; deploy TURN and test from real external networks.
- Keep the signalling layer and TURN endpoints internet-reachable through the approved ports only.
- Never expose Unreal MCP, editor ports, debug consoles, or worker management ports publicly.
- Test packet loss, jitter, bandwidth reduction, RTT, tab backgrounding, focus loss, gamepad disconnect, and reconnect behavior.

## Codec and quality policy

Hardware support and concurrency vary by GPU. During the platform spike:

- compare H.264, HEVC where supported, and AV1 where the full path supports it;
- measure encode time, quality, bitrate, browser compatibility, and sessions per GPU;
- use TSR/internal-resolution scaling before blindly increasing bitrate;
- choose a conservative default plus adaptive quality tiers;
- keep UI text readable at the lowest supported stream tier.

## Browser frontend

Keep the web shell narrow:

- sign-in/session queue;
- region selection;
- stream connection and reconnect UI;
- fullscreen, mute, controller help, quality preset;
- connection telemetry and support code;
- privacy/terms and session termination.

Core race HUD remains in UMG. Browser-to-Unreal custom messages are versioned, authenticated by session context, size-limited, validated, and never treated as authoritative race events.

## Acceptance tests

- successful connect/input/audio/video on every supported browser/OS pair;
- reconnect from a dropped signalling or media path;
- TURN-only connection from a restrictive network;
- gamepad connect/disconnect and focus recovery;
- 30-minute stream soak with recorded racing input;
- 100 session start/stop cycles with no orphan workers;
- version mismatch gives a controlled error;
- expired/invalid session token is rejected;
- measured end-to-end latency and frame delivery meet `Docs/07-QualityGates.md` in the target region.
