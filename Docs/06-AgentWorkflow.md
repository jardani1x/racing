# Claude Code agent workflow

## Roles

The parent Claude Code session is the technical director and integrator. Specialist subagents live under `.claude/agents/`.

- `vehicle-physics-engineer`: vehicle C++, tuning data, telemetry, physics tests.
- `race-systems-engineer`: track progression, checkpoints, laps, state machine, timing, HUD data.
- `pixel-streaming-engineer`: streaming frontend, signalling integration, browser messages, deployment and network tests.
- `rendering-tech-artist`: reproducible editor scripts/manifests, materials, world-building, content validation and benchmark setup.
- `performance-engineer`: traces, budgets, render/physics/stream analysis, optimization evidence.
- `code-reviewer`: read-only design/code/security/reliability review.
- `test-engineer`: read-only build/test/package/soak execution and evidence.
- `ip-compliance-auditor`: read-only provenance and license audit.

## Ticket loop

```text
Parent defines ticket and acceptance criteria
        |
        v
Specialist implements in a code worktree or serialized content checkout
        |
        v
Code reviewer reports severity-ranked findings (read-only)
        |
        v
Test engineer builds/runs required gates (read-only)
        |
        +-- pass --> parent integrates and records evidence
        |
        +-- fail --> specialist repairs, maximum three cycles
                       |
                       +-- still failing --> blocker report + human decision
```

No agent approves its own work. A passing compilation is not a passing ticket.

## Worktree policy

Use `isolation: worktree` for source/config/web changes when the repository and Git LFS setup support it. Do not use parallel worktrees for changes that create or modify `.uasset`, `.umap`, derived DDC state, or shared project settings without explicit ownership. Unreal binary content is integrated serially in the main checkout.

## Unreal MCP policy

- The parent session owns Unreal MCP calls.
- Subagents may prepare a change manifest, editor script, expected output, and validation plan.
- The parent executes one serialized batch, captures output, saves assets, and asks the test/review agents to inspect evidence.
- Never issue overlapping MCP tool calls because editor/game-thread actions are serialized.
- Never expose the MCP listener outside loopback or include it in production deployment.

## Repair-loop discipline

A repair cycle may fix the implementation, not weaken the requirement. Changing an acceptance threshold, deleting a failing test, suppressing a warning, or replacing a real test with a mock requires a documented human decision.

After three failed cycles, the blocker report contains:

- exact failed criterion;
- smallest reproducible case;
- logs, traces, screenshots, and test artifacts;
- root-cause hypothesis and confidence;
- alternatives with cost/risk;
- recommended decision;
- safe rollback.

## Completion evidence

Every integrated ticket records:

- ticket ID and commit/change identifiers;
- implementation agent and review/test agents;
- changed source/assets;
- commands actually executed;
- test output and report paths;
- screenshot comparison results when applicable;
- performance trace and hardware when applicable;
- license-ledger changes;
- known limitations and follow-up tickets.

## Human gates

Human approval is mandatory for:

- handling feel and vehicle target envelopes;
- hero vehicle and environment quality;
- use of any real brand, vehicle, circuit, livery, sponsor, scan, or recording;
- production cloud architecture/security;
- final quality-gate waiver;
- public release.
