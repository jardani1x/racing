# RacingSim project contract

## Goal
Create a high-fidelity browser-delivered racing vertical slice in Unreal Engine 5.8.x. The Unreal application runs on a GPU host and reaches the browser through Pixel Streaming 2.

## Non-negotiable decisions
- Runtime gameplay is C++ first. Use Blueprint for assembly, tuning, presentation, and content workflows; use UMG for the in-game HUD.
- Use Chaos Vehicles as the Phase 1 baseline. Unreal does not use Unity-style WheelColliders.
- Use an original unbranded prototype car and an original circuit until written licenses are recorded in `Docs/13-AssetLicenseLedger.md`.
- Never copy, trace, scrape, rip, decompile, or redistribute assets from Gran Turismo, Forza, manufacturer configurators, commercial games, or the supplied screenshots.
- Use the supplied images only as visual-direction references.
- Unreal MCP is editor automation only: loopback machine, serialized calls, never exposed publicly, and excluded from shipping builds.
- Python may automate editor/content work, but it is not runtime gameplay code.
- Do not edit `.uasset` or `.umap` files concurrently. Do not attempt text merges of Unreal binary assets.
- Pin the Unreal patch and the matching Pixel Streaming Infrastructure branch.

## Required workflow for every ticket
1. Write or update a ticket using `Docs/12-TicketTemplate.md`.
2. Define measurable acceptance criteria before implementation.
3. Delegate implementation to the relevant specialist subagent.
4. Delegate review to `code-reviewer`; it is read-only and cannot approve its own code.
5. Delegate execution/validation to `test-engineer`; it is read-only and cannot silently repair failures.
6. Run at most three implement-review-test repair cycles.
7. If still failing, stop and produce a blocker report with evidence and a recommended decision.
8. Merge only after review and test gates pass. Content changes are serialized and require explicit asset ownership.

## Evidence policy
Never report a command, build, cook, test, package, screenshot, trace, or deployment as successful unless it actually ran and its result was inspected. Every completion report includes:
- files changed;
- commands run;
- test names and pass/fail counts;
- artifact/report paths;
- screenshots or comparison reports when visual output changed;
- Unreal Insights or equivalent metrics when performance changed;
- remaining risks and rollback steps.

## Architecture boundaries
- `Core`: shared types, settings, logging, telemetry, and state contracts.
- `Vehicle`: vehicle pawn, input, tune data, physics, assists, camera hooks, telemetry.
- `Race`: track definition, ordered checkpoints, lap validation, timing, race state machine, results.
- `UI`: HUD view models/widgets, menus, countdown, results, input prompts.
- `Streaming`: Pixel Streaming integration, browser messages, connection telemetry. No race truth lives here.
- `Tests`: automation specs, functional maps, screenshot tests, soak controllers.

## Coding rules
- Prefer small Unreal modules/components with explicit ownership over monolithic actors.
- Put tunable vehicle and race parameters in typed DataAssets or config, not magic numbers in Tick.
- Avoid per-frame allocations, broad actor searches, and synchronous asset loads during a race.
- Treat units explicitly: Unreal distance is centimeters; document conversions to SI.
- Use a monotonic server-side time source for lap timing.
- Guard every state transition and reject invalid checkpoint order, reverse finish crossings, and shortcut laps.
- Keep gameplay independent from frame rate; establish and test a fixed/substepped physics policy.
- Add automation coverage with every gameplay system.
- No warning suppression without a documented reason.

## Content and rendering rules
- Establish a reference GPU worker and locked benchmark scene before quality tuning.
- Use Nanite where it is supported and measured; do not enable it blindly.
- Use Lumen, Virtual Shadow Maps, TSR, physically based materials, Clear Coat/dual normals for paint/carbon, World Partition, HLOD, and PCG only within measured budgets.
- Runtime gameplay uses real-time rendering. Path tracing may create offline reference frames only.
- Start with one fixed dry lighting preset. Dynamic weather and time of day are later milestones.
- Every external asset needs a license-ledger entry before import.

## Definition of done
A ticket is done only when its stated tests pass in a packaged build where applicable, no higher-severity review findings remain, performance stays within the current budget, documentation is updated, and no licensing or binary-asset ownership issue is open.
