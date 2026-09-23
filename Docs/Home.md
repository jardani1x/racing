---
title: RacingSim documentation home
type: index
tags:
  - index
  - racingsim
---

# RacingSim — documentation home

Entry point for the `Docs/` vault. This folder is an Obsidian vault **and** a
normal tracked part of the repository, so every note here is plain CommonMark
that reads correctly on GitHub, in an editor, and in Obsidian alike. See
[[Vault]] for the conventions that keep it that way.

The authority for what is true about the project is the repository, not this
note. Where the two disagree, the repository wins and this note is stale — say
so and fix it.

---

## Start here

| If you want to | Read |
|---|---|
| Understand the whole plan | [[00-ExecutivePlan]] |
| Understand the module boundaries | [[01-Architecture]] · [[15-ProjectStructure]] |
| Pick up the next piece of work | [[Tickets]] |
| Know what "done" means | [[07-QualityGates]] |
| Set up a machine | [[Environment]] |
| Know what may and may not be imported | [[08-LegalLicensing]] · [[13-AssetLicenseLedger]] |
| See how agents are supposed to work | [[06-AgentWorkflow]] |

The project contract itself lives at `CLAUDE.md` in the repository root, outside
this vault. It is short and it overrides everything written here.

---

## The four domains

Mirrors the architecture boundaries in `CLAUDE.md`, so a note about a subsystem
sits under the same heading as the code that implements it.

### Vehicle
[[02-VehiclePhysics]] — Chaos Vehicles baseline, tune data, assists, reset,
telemetry, failure detection.
Code: `Source/RacingSim/Vehicle/`. Tests: `Source/RacingSimTests/Vehicle/`.

### Race
[[03-TrackRaceUI]] — track definition, ordered checkpoints, lap validation,
timing, race state machine, results.
Code: `Source/RacingSim/Race/`.

### Rendering and content
[[04-VisualPipeline]] · [[14-VisualReferenceBrief]] — Nanite, Lumen, VSM, TSR,
World Partition, materials, lighting presets, benchmark scene.

### Streaming
[[05-PixelStreaming]] · [[17-BrowserQA]] — Pixel Streaming 2, browser frontend,
signalling, session lifecycle, WebRTC telemetry. No race truth lives here.

---

## Process

- [[06-AgentWorkflow]] — which specialist owns which ticket, and the mandatory
  implement → review → test cycle.
- [[07-QualityGates]] — the gates a ticket must clear before it can be called
  done.
- [[10-SourceControl]] — branching, LFS, binary-asset locking.
- [[12-TicketTemplate]] — the shape every ticket takes.
- [[09-UnrealMCP]] — editor automation, loopback only, excluded from shipping.

## Decisions and evidence

- [[ADR/README|Architecture decision records]] — one file per decision, never
  edited after acceptance; superseded by a later ADR instead.
  - [[ADR-0001-pixel-streaming-2-and-session-scaling]]
  - [[ADR-0002-msvc-toolchain-selection]]
  - [[ADR-0003-reference-gpu-worker-deferral]]
  - [[ADR-0004-source-control-topology]]
- [[Reports/README|Reports]] — milestone reports and decision sheets.
  - [[Phase0-Report]] · [[M0-DecisionSheet]]

## Backlog and history

- [[Tickets]] — the ordered ticket set, with per-ticket acceptance criteria,
  findings registers and evidence. This is the long one; use the outline pane.
- [[16-InitialBacklog]] — what [[Tickets]] was derived from.
- [[11-Roadmap]] — milestone ordering.
- [[18-ResearchReferences]] — external reading.

---

## Where the project stands

Epics 0 through 3 (environment, core, vehicle, track/race) are complete; UI is in progress (`UI-001` and `UI-002` done), streaming has not started; the operations epic is
blocked on hardware.

| Epic | State | Note |
|---|---|---|
| Environment and governance (`ENV`, `LEGAL`, `ARCH`) | Complete | |
| Core (`CORE`, `TEST`) | Complete | |
| Track and race (`TRACK`, `RACE`) | Complete | `RACE-006` driver reset through the race director closed 2026-09-23 on local `main`, **not pushed**. It also fixed `VEH-010` (Chaos cannot wake a non-skeletal chassis) and opened `VEH-011` and `VEH-012`; see [[Tickets]] |
| Vehicle (`VEH`) | Complete | `VEH-006` closed 2026-09-15; `VEH-007` (VEH-006 finding 2, suppression counter split) closed 2026-09-18; `VEH-010` (chassis wake) closed 2026-09-23 inside the `RACE-006` branch; `VEH-011` (re-apply the `NeverSleep` pin after a reset) and `VEH-012` (tolerance drift guard) open; remaining follow-ups tracked in [[Tickets]] |
| UI (`UI-001`..`UI-004`) | In progress | `UI-001` HUD data contract and `UI-002` native HUD widget closed 2026-09-18; `RACE-005` race session composition closed 2026-09-18. Next: `TRACK-003` input/lighting content (needs an editor session), then `STREAM-001` |
| Streaming (`STREAM-001`..`003`) | Not started | |
| Streaming infrastructure (`STREAM-004`..`006`), operations | Blocked | `BLOCKER-001`: no reference GPU worker |

`BLOCKER-001` is the one thing no amount of local work clears. It gates external
STUN/TURN testing, the session-broker spike, WebRTC latency measurement, and the
whole operations epic. See [[ADR-0003-reference-gpu-worker-deferral]].

**This table is written by hand and goes stale.** [[Tickets]] is the source of
truth; when the two disagree, believe [[Tickets]] and correct this one.

---

## Running things

Every command below is run from the repository root. Counts are read from
`index.json`, never from a process exit code — see [[Environment]] for why.

| Task | Script |
|---|---|
| Build the editor target | `Scripts/Test/Build-Target.ps1` |
| Smoke suite (fast, runs in `PreInit`) | `Scripts/Test/Run-Smoke.ps1` |
| One filter or an explicit test list | `Scripts/Test/Run-AutomationFilter.ps1` |
| Thirty-minute vehicle soak | `Scripts/Test/Run-Soak.ps1` |

Test evidence logs are committed under `Scripts/Test/*.log` on purpose: a gate
result that cannot be re-read later is not evidence.

Which of this can run in a cloud Claude Code session and which needs this machine is
in [[CloudAndLocalWork]].
