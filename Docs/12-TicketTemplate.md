# Ticket template

## ID and title

`RACE-000: concise outcome`

## Outcome

One sentence describing the user-visible or system result.

## Owner / agents

- Parent integrator:
- Implementation subagent:
- Review subagent: `code-reviewer`
- Validation subagent: `test-engineer`
- Additional gate owner:

## Preconditions and dependencies

- engine/build version:
- required assets/data/licenses:
- preceding tickets:
- content packages locked by this ticket:

## Scope

- included:
- excluded:

## Design

- affected modules/classes/assets:
- data contracts:
- state transitions:
- units and conversions:
- failure/rollback behavior:

## Acceptance criteria

Write binary or measurable statements. Example:

- [ ] Forward finish crossing after all ordered checkpoints increments the lap exactly once.
- [ ] Reverse crossing does not increment the lap.
- [ ] Automation report contains 100 passing valid/invalid traversal cases.
- [ ] Packaged build passes the relevant functional test map.

## Required tests and evidence

- low-level/automation:
- functional:
- screenshot:
- performance/Insights:
- packaged/Gauntlet/soak:
- browser/network:
- output/report paths:

## Risk and counter-case

- highest-risk assumption:
- evidence that would show the design is wrong:
- strongest alternative:

## Repair loop

- cycle 1 result:
- cycle 2 result:
- cycle 3 result:
- blocker escalation if still failing:

## Completion

- commits/changelists:
- files/assets changed:
- review findings closed:
- tests passed:
- license ledger updated:
- rollback:
- remaining limitations/follow-ups:
