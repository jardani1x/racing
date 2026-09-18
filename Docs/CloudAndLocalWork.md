---
tags: [process, workflow]
updated: 2026-09-18
---

# Cloud and local work split

Which backlog work can run in a **cloud Claude Code session** (claude.ai/code, a Linux
container with a clone of `origin`) and which must run on **this Windows machine**.
Written so a remote session can be started with a one-line prompt whenever the local
session is unavailable, and the local session picks up the verification afterwards.

Related: [[Tickets]], [[Environment]], [[06-AgentWorkflow]], [[07-QualityGates]].

---

## The one rule that decides everything

A cloud session has **no Unreal Engine 5.8, no Windows, no GPU, no Visual Studio
toolchain, and no Windows PowerShell 5.1**. It cannot build `RacingSimEditor`, run an
automation test, run the soak, cook, package, or start Pixel Streaming.

Under `CLAUDE.md`'s evidence policy a change is not done until its build and tests have
actually run. So:

- **Cloud can author, review, and document.** Its output is a branch on `origin`
  marked *implemented, unverified*.
- **Local must build, test, gate, and merge.** Nothing written in the cloud is DONE until
  a local session has compiled it, run its tests, and passed `test-engineer`.

A cloud session must never mark a ticket DONE, never cite a build or test it did not
run, and never merge to `main`.

---

## Prerequisites before any cloud session is useful

| # | Prerequisite | Current state (2026-09-15) | Who |
|---|---|---|---|
| 1 | The branch the cloud works from exists on `origin` | 2026-09-18: local `main` is **7 commits ahead** of `origin/main` (VEH-007 merge `aa8f125` not pushed). Cloud sessions must base on `main` only after that push, or they miss VEH-007 | Owner must explicitly ask for a push; sessions do not push unasked |
| 2 | The cloud session's GitHub access to `jardani1x/racing` | Not verified | Owner |
| 3 | `Web/PixelStreamingInfrastructure` | **Gitignored, not on `origin`.** A cloud session must clone `EpicGamesExt/PixelStreamingInfrastructure` at commit `48bff3b751f91f735b50c90b2a7fec5ceb2a440f` (branch `UE5.8`) itself — see [[Environment]] | Cloud session, per task |
| 4 | Binary content | `Content/` is 93 KB and no `.uasset`/`.umap` is tracked yet, so nothing is lost today. Once assets exist, the cloud must not touch them (no LFS server, locks are local-only — BLOCKER-002) | — |

---

## Work by location

### Cloud-safe (author now, verify locally later)

| Work | What the cloud does | What local still owes |
|---|---|---|
| **Ticket writing** for `UI-002`..`UI-004`, `STREAM-001`..`003` | Full ticket from `Docs/12-TicketTemplate.md` with measurable acceptance criteria, scope boundaries, test names | Nothing beyond normal review |
| **`UI-002`..`UI-003` C++ side** | Formatting/logic helpers (speed, RPM, gear, lap, delta, countdown), restart-flow state logic, their specs | Same as above, **plus all UMG widget `.uasset` work** (local only) |
| **`STREAM-002` browser frontend shell** | TypeScript frontend and versioned custom-message schema inside a fresh clone of the pinned Pixel Streaming Infrastructure; `npm` install, build, lint and unit tests *can* run in the container | Wire into Unreal, run against a packaged build (`STREAM-001`), browser QA |
| **VEH-006 follow-ups, code only** | ~~Production finding 2 (split carried ceiling counter from per-arm floor counter) with CASE 9; findings 3–8 comment/doc fixes~~ done locally as `VEH-007` 2026-09-18; spec S-L2..S-L4 remain | Build, CASE 9 revert proof, full manoeuvre set, Smoke |
| **VEH-006 harness follow-ups** (`H-M1`..`H-L6`) | Edit `Scripts/Test/*.ps1` | Must be *exercised* on Windows PowerShell 5.1 with failing-case logs (junction and `Remove-Item` behaviour differs from `pwsh` on Linux) |
| **Static code review** | `code-reviewer` over a pushed diff, for findings only | A review of uncompiled code is advisory; re-review is required after local build fixes change the diff |
| **Docs and vault** | [[Home]], ADRs, [[Tickets]] prose, the roadmap, research notes | — |

### Local only

| Work | Why it cannot move |
|---|---|
| Any build (`Scripts/Test/Build-Target.ps1`) | Needs UE 5.8 + MSVC on Windows |
| Automation, Smoke, named filters, soak (`Run-Smoke.ps1`, `Run-AutomationFilter.ps1`, `Run-Soak.ps1`) | Launch `UnrealEditor-Cmd.exe` |
| Revert proofs | They are builds plus test runs |
| `test-engineer` validation gate | Its job is to run the above |
| Merging to `main`, flipping tickets to DONE | Only after both gates pass on real evidence |
| `.uasset`/`.umap` content, UMG widgets, levels | Unreal binary assets; serialized, owned in `Docs/AssetOwnership.tsv`, pre-commit hook is local |
| Unreal MCP editor automation | Loopback only, by contract never exposed |
| `UI-004` HUD functional and screenshot tests | Rendering |
| `STREAM-001` packaged Pixel Streaming connection, `STREAM-003` gamepad/focus/reconnect | Packaged build, GPU, NVENC, real browser |
| Cook and package | Toolchain and memory (BLOCKER-003) |

### Blocked everywhere

`STREAM-004`, `STREAM-005`, `STREAM-006` and Gates D/E/F need a reference GPU worker
(BLOCKER-001). Neither cloud nor this laptop clears it; it needs the owner's hardware
decision.

---

## Current queue, in order

1. ~~**Local** — close `VEH-006` (gates, commit, merge).~~ Done 2026-09-15.
2. ~~**Cloud/Local** — write `UI-001`, implement it, gate and merge.~~ Done locally 2026-09-18.
3. ~~**Cloud** — write ticket `UI-002`; carry `UI-001`'s forwarded risks (L5, L7, N3, telemetry timestamp doc) into it.~~ Done 2026-09-18.
4. ~~**Local** — `UI-002` UMG widgets bound to `FRacingHudViewModel`.~~ Done locally 2026-09-18 (native C++ tree; no `.uasset`). `RACE-005` race session composition done locally 2026-09-18. Next: `TRACK-003` input assets and lighting (editor/`.uasset` work), which `STREAM-001` browser QA needs to drive the car.
5. ~~**Cloud** — VEH-006 production finding 2 + CASE 9~~ done locally as `VEH-007`, 2026-09-18.
   Harness `H-*` edits moved to cloud task C1 below.
6. **Local** — `RACE-006` driver reset through the director, with the VEH-007 reset
   cooldown and its test. In progress 2026-09-18.
7. **Cloud, in parallel with 6** — tasks C1–C3 below. None touches `Source/RacingSim/Race/`
   or `Source/RacingSim/Game/`, so none collides with `RACE-006`.
8. **Local** — verify and merge each `cloud/*` branch; then `TRACK-003` (editor session),
   `UI-003` widgets, `UI-004`, `STREAM-001`, `STREAM-003`.
9. **Cloud, after `RACE-006` merges and is pushed** — task C4 (`UI-003` C++ logic), which
   touches the restart flow the director owns.

---

## Phone-ready cloud tasks (2026-09-18)

Start each from claude.ai/code on the phone, repository `jardani1x/racing`, base branch
`main` **after** local `main` has been pushed. One session per task. Each task only
appends its own section and table row to [[Tickets]], so the text merge with local
edits stays clean.

**C1 — `VEH-008` test-harness and soak follow-ups.** Scripts only.

> Work on new ticket VEH-008 in jardani1x/racing from branch main. Cloud rules in
> Docs/CloudAndLocalWork.md: author only, no build or test claims, commit to branch
> cloud/veh-008, push that branch. Scope: VEH-006 harness findings H-M1..H-L6 and the
> soak memory slope/drift assertions (test M-1/M-2) listed in Docs/Tickets.md under
> VEH-006, plus Scripts/Test/Build-Target.ps1 writing its BUILD_EXITCODE / RESULT_LINE /
> WARNING_ERROR_MATCHES markers into -OutFile. Write the VEH-008 ticket (template
> Docs/12-TicketTemplate.md, UI-002 style: checkbox criteria, Deliberately excluded) as a
> new section plus a table row after VEH-007, status IMPLEMENTED (cloud) — awaiting
> local build and gates. Scripts must stay Windows PowerShell 5.1 compatible (no &&, ??,
> ?:, ternaries). List every failing-case log the local session must produce.

**C2 — `VEH-009` VEH-006 spec follow-ups `S-L2`..`S-L4`.** Test code only.

> Work on new ticket VEH-009 in jardani1x/racing from branch main. Cloud rules in
> Docs/CloudAndLocalWork.md: author only, no build or test claims, commit to branch
> cloud/veh-009, push that branch. Scope: VEH-006 spec findings S-L2..S-L4 in
> Docs/Tickets.md. Touch only Source/RacingSimTests/ (and a test helper if unavoidable),
> never Source/RacingSim/Race or Source/RacingSim/Game. Write the ticket as a new section
> plus row after VEH-007, status IMPLEMENTED (cloud). In the handoff, list every Unreal
> API assumption the compiler has not checked and the exact test names to run locally.

**C3 — `STREAM-002` ticket and browser frontend groundwork.** TypeScript; `npm` build,
lint and unit tests can really run in the container, and may be cited as run.

> Work on STREAM-002 in jardani1x/racing from branch main. Cloud rules in
> Docs/CloudAndLocalWork.md; commit to branch cloud/stream-002 and push it. First write
> the STREAM-002 acceptance criteria in Docs/Tickets.md (versioned custom-message schema
> between browser and Unreal, frontend shell, what STREAM-001 must supply). Clone
> EpicGamesExt/PixelStreamingInfrastructure at commit
> 48bff3b751f91f735b50c90b2a7fec5ceb2a440f (branch UE5.8) outside the repo per
> Docs/Environment.md; do not commit that clone. Put project-owned frontend code in a new
> tracked folder and document how it builds against the pinned clone. Run npm build, lint
> and tests and cite their real output. Nothing Unreal-side: no race truth in Streaming.

**C4 — `UI-003` C++ logic** (only after `RACE-006` is on `origin/main`).

> Work on UI-003 in jardani1x/racing from branch main. Cloud rules in
> Docs/CloudAndLocalWork.md; commit to branch cloud/ui-003 and push it. Write the UI-003
> criteria (input prompts, settings, restart flow, accessibility baseline; inherit the
> UI-002 L2 note in its row), then implement only the C++ view-model/state logic and its
> specs. No .uasset/UMG assets. Status IMPLEMENTED (cloud).

**Ticket-writing only, any time.** Criteria for `UI-004`, `STREAM-001`, `STREAM-003`,
`TRACK-003`: same prompt shape, branch `cloud/tickets-<name>`, docs only.

**Not for the phone.** Anything in "Local only" above, `RACE-006` itself (in progress
locally), and merging any `cloud/*` branch.

---

## Handoff protocol

**Starting a cloud session.** Prompt shape:

> Work on `<ticket>` in `jardani1x/racing`, from branch `<base>`. Cloud rules in
> `Docs/CloudAndLocalWork.md`: author only, no build or test claims, commit to branch
> `cloud/<ticket>`, record the handoff in `Docs/Tickets.md`, push that branch.

**What the cloud session leaves behind.**

- Branch `cloud/<ticket-lowercase>` on `origin`, based on the named base branch.
- The ticket status set to `IMPLEMENTED (cloud) — awaiting local build and gates`.
- A handoff note in the ticket: files changed, test names added, what was deliberately
  not done, and every assumption about engine APIs that the compiler has not checked.

**Resuming locally.** Prompt shape:

> Pick up `cloud/<ticket>`: fetch, build, run its tests and Smoke, then run the normal
> review and test gates.

A compile fix, a failing test, or a review finding against cloud-authored code counts as a
normal implement-review-test repair cycle, capped at three as usual.
