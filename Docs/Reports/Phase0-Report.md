# Phase 0 report — inspect and plan

- Date: 2026-08-10
- Author: technical director (Claude Opus 5)
- Status: **submitted for human approval**
- Commits: `266dc32`, `f37938f`, `b631c1b`

---

## 1. Outcome and confidence

**Outcome: Phase 0 substantially complete, with four open blockers, one of which
prevents any implementation ticket from starting.**

Confidence is **high** on everything marked VERIFIED — each was produced by
running a command and reading its output, not by inference. Confidence is
deliberately **withheld** on everything marked UNVERIFIED; those are recorded as
unknowns rather than filled with plausible defaults.

The single most important finding: **BLOCKER-004 — the project's eight subagents
do not dispatch.** The mandatory implement → review → test protocol cannot be
executed. Phase 1 cannot begin until this is fixed.

## 2. Acceptance criteria status

| Phase 0 requirement (from `PROMPT_TO_START.md`) | Status |
|---|---|
| Verify repo state, engine association, toolchain, OS, GPU, source-control mode, plugins, disk/RAM | **DONE** |
| Verify Unreal version and Pixel Streaming Infrastructure branch match | **DONE** — UE 5.8.1; PSI `UE5.8` pinned at `48bff3b7` |
| Verify Unreal MCP setup without exposing beyond loopback | **NOT DONE** — ENV-005 open |
| Inventory all assets and licenses; quarantine anything without provenance | **DONE** — nothing to quarantine; 4 legal questions escalated |
| Discover and record actual build/test/cook/package commands | **PARTIAL** — editor compile verified; cook/package in progress; automation and streaming not started |
| Create ADR for Pixel Streaming 2 and scaling | **DONE** — ADR-0001, plus 0002/0003/0004 |
| Turn backlog into ordered tickets, dependencies, milestone gates | **DONE** — `Docs/Tickets.md` |
| Identify assumptions needing a human answer | **DONE** — 4 blockers, 1 assumption, 1 note |

## 3. Files and assets changed

No binary assets exist. Everything below is text.

**Created:** `.gitignore`, `.gitattributes`, `RacingSim.uproject`,
`Source/RacingSim.Target.cs`, `Source/RacingSimEditor.Target.cs`,
`Source/RacingSim/{RacingSim.Build.cs,RacingSim.h,RacingSim.cpp}`,
`Docs/ADR/ADR-0001..0004`, `Docs/Tickets.md`, this report.

**Rewritten:** `Docs/Environment.md` (was an empty template).
**Extended:** `Docs/13-AssetLicenseLedger.md` (template preserved, real entries appended).

**Fetched, not committed:** `Web/PixelStreamingInfrastructure` at commit
`48bff3b751f91f735b50c90b2a7fec5ceb2a440f` — gitignored by design.

## 4. Review findings and disposition

**No independent review was performed, because the reviewer agent does not exist
(BLOCKER-004).** Everything in this report is self-assessed, which is exactly the
condition the mandatory protocol is designed to prevent. Treat this report as
unreviewed.

Self-identified errors made and corrected during Phase 0, recorded because the
evidence policy requires it:

1. **Nearly reported a failed build as passing.** The first build returned exit
   code 0 while the log read `'C:\Program' is not recognized`. The exit code came
   from a shell pipeline, not the compiler. Caught by reading the log. This is the
   precise failure mode `CLAUDE.md`'s evidence policy exists to prevent.
2. **Misread the MSVC ban.** I matched the toolchain *directory name*
   (`14.44.35207`) against the engine's banned range and concluded an
   administrator-elevated toolchain install was required. UBT then reported the
   real compiler as `14.44.35222` — not banned, and preferred. The install was
   unnecessary and was cancelled. Recorded in ADR-0002.
3. **A "JSON parse error" that was an argument-quoting bug.** `BuildCookRun`
   failed claiming the `.uproject` was invalid JSON. The real cause was the
   unquoted path splitting at the space in `jun yi`, so UAT tried to parse
   `C:\Users\jun`. The `.uproject` was valid throughout.

The common thread — three failures from paths containing spaces — is now a
standing hazard note in `Docs/Tickets.md` and `Docs/Environment.md`.

## 5. Commands run and result locations

| Command | Result |
|---|---|
| `Build.bat RacingSimEditor Win64 Development` | **Succeeded**, exit 0, no warnings, 179.60 s. Output `UnrealEditor-RacingSim.dll`. Log: `%LOCALAPPDATA%\UnrealBuildTool\Log.txt` |
| `git clone` → scratch, clean-clone test | **Passed** — 39 files, HEAD matched, 8 `lockable` patterns restored |
| `git lfs lock Test.uasset` | **Failed as expected** — `missing protocol`; see BLOCKER-002 |
| `git clone -b UE5.8 …PixelStreamingInfrastructure` | **Succeeded** — pinned `48bff3b7`, RELEASE_VERSION 0.1.0 |
| `RunUAT BuildCookRun … -cook -stage -pak -archive` | **In progress at time of writing** — passed the Game-target build with the verified toolchain and entered asset cooking. Log: `scratchpad/cook2.log` |
| Agent dispatch `code-reviewer` | **Failed** — `Agent type 'code-reviewer' not found`. BLOCKER-004 |

Not yet run: automation tests, packaged Pixel Streaming launch, signalling server
start, Unreal MCP loopback verification.

## 6. Visual and performance evidence

**None, and none is claimable.** No content exists, no stream has been
established, no frame has been rendered. Per ADR-0003 the development laptop is
not the reference GPU worker, so no performance number produced here could serve
as a Gate D/E/F result even if measured.

One performance-relevant environmental fact: UnrealBuildTool requested 14 parallel
compile actions and was granted **1**, reporting ~321 MB free physical memory on a
15.7 GB machine. This is BLOCKER-003 and will worsen during shader compilation.

## 7. Open risks, strongest counterargument, rollback

### Blockers

- **BLOCKER-004 — subagents do not dispatch.** Halts all implementation.
  Likely fixed by restarting Claude Code with `racing/` as working directory.
- **BLOCKER-001 — no reference GPU worker.** Gates D/E/F unmeasurable; Epics 6–7 blocked.
- **BLOCKER-002 — LFS locks inert.** Constraint #7 rests on process discipline.
- **BLOCKER-003 — memory pressure.** Builds run single-threaded.

Plus ASSUMPTION-001 (node v24.18 vs the pinned signalling server, unproven) and
NOTE-001 (`UbaServer` binds `0.0.0.0:1345` during builds).

### Strongest counterargument to this plan

*The hardware cannot deliver the product, so sequencing graybox work first is
sunk cost.* A 6 GB laptop GPU cannot host a photorealistic racing sim, and the
16 GB RAM ceiling already throttles a trivial build. If the reference worker is
never provisioned, Epics 1–5 produce a correct simulation nobody can see at the
intended fidelity.

**Response, and its limit:** Epics 1–5 are logic — lap validity, checkpoint
ordering, state machines, physics stability — validated by Gates A/B/C, none of
which reference the worker. That work is portable to any hardware and is not
wasted. But the counterargument holds for *scheduling*: if the worker is never
funded, the project should stop after M1 rather than proceed to M3. The decision
point is real and should be taken deliberately, not drifted past.

### Rollback

Every change is text, in three commits, on a local `main` with no remote.
`git reset --hard 266dc32` returns to documentation-only state; deleting the repo
and `Web/PixelStreamingInfrastructure` returns to pre-Phase-0. Nothing was
installed, no system setting changed, no external service touched. The one
process-level action — terminating the running editor — was authorised, and those
processes had in fact already exited on their own before the command ran.

## 8. Next unblocked ticket

**There is none.** `CORE-001` is next on the critical path, but BLOCKER-004
prevents it: it cannot be reviewed or tested by independent agents, and starting
it anyway would violate the mandatory protocol.

Required before Phase 1:

1. Resolve BLOCKER-004 (restart Claude Code from `racing/`, then re-run the
   `code-reviewer` dispatch test).
2. Finish `ENV-004` — cook/package result, automation command, streaming launch.
3. Complete `ENV-005` — Unreal MCP loopback verification.
4. Human approval of this report.

Decisions needed from the human owner: whether to fund a reference GPU worker
(BLOCKER-001), whether to add an LFS remote or accept process-only serialization
(BLOCKER-002), whether 16 GB is acceptable for the graybox milestone
(BLOCKER-003), and answers to the four legal questions in
`Docs/13-AssetLicenseLedger.md`.

---

**This project is not production-ready and no gate has been claimed as passed.**
Gates A–H remain open. Gate A is partially evidenced (clean editor build, no
warnings); the cook/package leg was still running when this report was written and
its result must be appended before Gate A is even partially claimed.
