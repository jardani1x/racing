# ADR-0003: Reference GPU worker deferral

- Status: Accepted
- Date: 2026-08-10
- Deciders: human project owner
- Related: `Docs/07-QualityGates.md` Gates D/E/F, `Docs/Environment.md` BLOCKER-001/003, ADR-0001

## Context

`Docs/07-QualityGates.md` opens by stating that the reference hardware, region,
browser, network profile, build configuration and measurement method **must be
recorded before a result is accepted**. Gate E then sets concrete targets on that
worker: sustained 60 fps at 1920x1080 in a packaged build, Unreal frame p95
≤ 16.67 ms and p99 ≤ 22 ms, no post-warm-up hitch above 100 ms.

The only hardware available is the development laptop:

- NVIDIA GeForce RTX 3050 **6 GB** Laptop GPU (GA107)
- Intel Core Ultra 5 125H, 14 physical / 18 logical cores
- **15.7 GB** system RAM
- Hybrid graphics with an Intel Arc iGPU as the default adapter

This does not meet the visual milestone's needs:

1. **6 GB VRAM.** Nanite + Lumen + Virtual Shadow Maps + TSR at 1080p, with
   hero-quality car paint, carbon, glass and a detailed circuit, will not hold
   16.67 ms frame times on a GA107 laptop part.
2. **No AV1 encode.** GA107 NVENC does H.264 and HEVC only. The AV1 arm of the
   codec comparison required by `Docs/05-PixelStreaming.md` cannot be run here.
3. **16 GB RAM is already the binding constraint.** During the Phase 0 editor
   build, UnrealBuildTool requested 14 parallel actions and was limited to **1**,
   reporting ~321 MB physical memory free. Shader compilation and cooking are
   substantially heavier than that build.
4. **A laptop is not a deployment target.** Thermal throttling, hybrid-GPU
   switching and background load make it unreproducible as a measurement baseline.

## Decision

**The laptop is the development machine only. It is explicitly not the reference
GPU worker. No reference worker is named at this time.**

Consequently:

- **Gates D, E and F are unmeasurable and remain open.** No performance,
  visual-quality or streaming-latency result may be recorded as a gate pass until
  a reference worker exists. Numbers produced on the laptop are diagnostic only
  and must be labelled as such.
- **Epics 1–5 (graybox) proceed on this hardware.** Project skeleton, input,
  Chaos Vehicles prototype, graybox circuit, race state machine, lap timing, HUD
  and automation tests are all logic-correctness work. They are gated by Gate A
  (build health), Gate B (gameplay correctness) and Gate C (vehicle stability),
  none of which depend on the reference worker.
- **Epic 6 (visual vertical slice) is blocked** until a worker is named. Starting
  hero-quality art without a measurement baseline would produce content that
  cannot be validated and may have to be rebuilt.
- Tracked as BLOCKER-001 in `Docs/Environment.md`.

## Alternatives rejected

**Declare the laptop the reference worker and lower Gate E.** Rejected. The
prompt forbids silently weakening thresholds, and a 6 GB laptop baseline would
set a quality bar far below the product's premise. If the project later chooses
this path it requires explicit written approval and a rewritten Gate E, not a
quiet redefinition.

**Provision a cloud worker during Phase 0.** Rejected for now on cost grounds.
Nothing in Epics 1–5 needs it, so the spend can be deferred until the graybox
passes its gates.

## Consequences

- The graybox milestone can be fully delivered and validated before any cloud
  spend begins.
- Local Pixel Streaming (`STREAM-001`) can still prove connection, input routing
  and session telemetry. It cannot prove latency or quality targets.
- When a worker is named, `Docs/Environment.md`'s *Reference GPU worker* and
  *Browser/network support matrix* sections must be filled before any Gate D/E/F
  measurement is accepted, and this ADR superseded.
- Risk accepted: art direction decisions made before profiling may need revision
  once real budgets are known. Mitigated by keeping Epic 6 blocked rather than
  starting it speculatively.
