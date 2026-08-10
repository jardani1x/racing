# ADR-0001: Pixel Streaming 2 and one-GPU-process-per-independent-session scaling

- Status: Accepted
- Date: 2026-08-10
- Deciders: human project owner (pending sign-off), technical director
- Related: `Docs/05-PixelStreaming.md`, `Docs/07-QualityGates.md` Gates E/F/G, ADR-0003

## Context

The product is a photorealistic racing simulation delivered to a desktop browser.
Two delivery architectures were available: render in the browser (WebGL/WebGPU),
or render on a GPU host and stream pixels over WebRTC.

Hard constraint #4 of `PROMPT_TO_START.md` fixes this decision: browser delivery
is Pixel Streaming 2, and the project may not be redesigned as a native
WebGL/WebGPU build. This ADR records *why* that constraint is sound and what it
commits us to, rather than re-opening it.

## Decision

**Render on a GPU host and stream via Pixel Streaming 2. Allocate one Unreal
process per independent player session.**

Verified available in this environment: `Engine/Plugins/Media/PixelStreaming2`
(UE 5.8.1), with `NVCodecs` and `AMFCodecs` pulled transitively for hardware
encode. Signalling and frontend come from `PixelStreamingInfrastructure` branch
`UE5.8`, pinned at commit `48bff3b751f91f735b50c90b2a7fec5ceb2a440f`.

### Why not browser-native rendering

Nanite, Lumen, Virtual Shadow Maps, TSR and the automotive material stack the
visual milestone requires have no equivalent in a browser runtime. Targeting
WebGL/WebGPU would mean rebuilding the renderer and abandoning the fidelity that
is the product's entire premise.

### Why one process per session

A single Unreal process simulates one world. Multiple WebRTC viewers can observe
that process through an SFU, but they share the same simulation — same car, same
lap, same clock. That is spectating, not independent play.

Since each player needs their own authoritative lap timing, checkpoint state and
vehicle physics, each player needs their own process. This is stated in
`Docs/05-PixelStreaming.md` and is adopted here as binding.

## Consequences

### Accepted costs

- **Cost scales linearly with concurrent players**, not with total users. Every
  concurrent session holds a GPU slice for its whole duration. This is the
  dominant economic fact of the product and must drive session timeouts, queueing
  and cost caps (`OPS-002`).
- **Latency becomes a correctness concern, not just polish.** Input-to-photon
  now includes encode, network and decode. Gate F's p50 ≤ 80 ms / p95 ≤ 120 ms
  targets are the product's real responsiveness budget.
- **The signalling server is not a product.** The reference implementation is a
  starting point; authentication, session brokering, worker lifecycle, TURN with
  short-lived credentials, and rollout/rollback are all our responsibility
  (`Docs/05-PixelStreaming.md`, Epic 7).
- **Sessions-per-GPU is a measured quantity, not an assumption.** It depends on
  VRAM, encoder session limits and the quality tier, and is UNVERIFIED until a
  reference worker exists.

### Constraints this imposes

- Race truth — lap timing, checkpoint order, validity — lives in the `Race`
  layer and never in `Streaming`. Browser-to-Unreal custom messages are
  versioned, size-limited, validated, and never authoritative (`CLAUDE.md`).
- Worker state must be scrubbed or the worker destroyed between users (Gate G).
- Unreal MCP, editor ports and debug consoles are never reachable from a worker's
  public surface (hard constraint #6, Gate G).

### Deferred

Session broker, autoscaling and warm-pool design are a spike (`STREAM-005`), not
part of the vertical slice. The slice proves one local session end to end.

## Status of evidence

Pixel Streaming has **not yet been run** in this project. No stream has been
established, no latency measured, no codec compared. Everything in the
"consequences" section above is architectural reasoning, not measurement.

Local hardware limits what can ever be measured here: the RTX 3050 (GA107)
supports H.264 and HEVC encode but **not AV1**, so the AV1 arm of the codec
comparison in `Docs/05-PixelStreaming.md` requires the reference worker
(ADR-0003, BLOCKER-001).
