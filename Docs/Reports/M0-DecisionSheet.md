# M0 decision sheet — Phase 0 sign-off

- Date prepared: 2026-08-12
- Prepared by: technical director (Claude Opus 5)
- Commit under review: `a96187c`
- Signing authority: **human project owner.** No agent may sign this sheet.

---

## Owner direction of 2026-08-12 — recorded, not a signature

On 2026-08-12 the project owner directed: *"fix anything that is left until it is fixed
and move into next phase."*

That is recorded here as an instruction to proceed to Epic 1, and `CORE-001` is
unblocked on its strength. It is **not** a disposition of D-1 through D-9, and this
document does not treat it as one. Several of those items commit real money (D-1),
change source-control tooling (D-2), buy hardware (D-3), or take a legal position
(D-4, D-5, D-9). No agent may answer them, and inferring an answer from a general
instruction to proceed would be exactly the self-certification
`PROMPT_TO_START.md` constraint 8 forbids.

Each item below therefore keeps its blank decision line. Items the technical director
could close by investigation rather than decision **have been closed**, and say so.

## Why this sheet exists

`PROMPT_TO_START.md` constraint 8 forbids self-certification. `Docs/Tickets.md`
requires human approval of M0 before Epic 1 may start. `CLAUDE.md` forbids the
same agent implementing and approving a change.

Independent agents can close the **evidence** gap. They cannot close the
**decision** gap. Everything below is a funding, legal, or risk-acceptance call
that only the project owner can make. The verification results that inform these
decisions are in `Docs/Reports/Phase0-Report.md`.

Approving this sheet authorises exactly one thing: starting `CORE-001`. It is not
a Gate A pass, not a claim that any gameplay works, and not legal clearance.

---

## D-1 — Reference GPU worker

**Question.** Fund a named reference GPU worker, or accept that Gates D, E and F
cannot be measured?

**Evidence.** BLOCKER-001. Gates D/E/F specify thresholds on a named reference
worker; none exists. The local RTX 3050 6 GB / 15.7 GB RAM is designated
development-only by ADR-0003 and cannot meet Gate E. Blocks Epic 6, Epic 7, and
`STREAM-004`/`005`/`006`.

**Narrowed 2026-08-12.** This was an open research task; it is now a yes/no question. A
full requirement specification is in `Docs/Environment.md` under BLOCKER-001 — sustained
60 fps at 1080p, frame p95 ≤ 16.67 ms, hardware NVENC/AMF encode, **two concurrent
encode sessions minimum**, VRAM above the known-fail 6 GB, ≥ 32 GB system RAM, and a
region inside the Gate F latency envelope.

**Answer these three first — they constrain the purchase more than the GPU does:**
1. Is there a target user region? Gate F's thresholds are regional and meaningless
   without one.
2. Cloud instance or physical box? A physical box removes per-hour cost and fixes region.
3. How many concurrent sessions must one worker carry? A product question, not an
   engineering one.

**Verify before buying:** the concurrent NVENC session cap on the specific GPU and
driver, and whether a virtualised provider GPU exposes hardware encode at all — some do
not. This is the most likely thing to invalidate an otherwise correct choice.

**Options.**
1. Fund a worker against the specification above.
2. Defer, and accept the project stops after M1 (graybox) rather than drifting
   into Epic 6 with no way to validate the output.
3. Defer with no stop condition.

**Recommendation.** Option 1 or 2. Option 3 is the failure mode ADR-0003 was
written to prevent: hero-quality art produced without a measurement baseline may
have to be rebuilt.

**Decision:** ☐ fund ☐ defer, stop after M1 ☐ defer, no stop condition
**Signed / date:** ______________________

---

## D-2 — Binary asset serialization

**Question.** Add an LFS-capable remote, adopt Perforce, or formally accept
process-only serialization?

**Evidence.** BLOCKER-002. `lockable` attributes are configured and a clean-clone
test restored 8 `lockable` patterns, but without an LFS server the locks are
inert. Hard constraint 7 (no parallel `.uasset`/`.umap` edits) currently rests on
process discipline alone.

**Mitigated 2026-08-12 — the risk is now controlled, the decision is not made.**
`.githooks/pre-commit` blocks any staged `.uasset`/`.umap` that is unclaimed or claimed
by another owner, against `Docs/AssetOwnership.tsv`. Verified against all three cases:
unclaimed blocks, wrong-owner blocks, correct-owner passes. Activate with
`git config core.hooksPath .githooks`.

It is **not** locking: it cannot coordinate across machines or clones, and
`--no-verify` bypasses it. But it converts "process discipline" into something that
fails loudly at the moment of the mistake, on the clone where the work happens.

**Impact if deferred.** Now tolerable rather than acute. Still becomes real the moment
the project has two machines or a remote, because the hook cannot see another clone.

**Recommendation.** The mitigation buys time through M1. Decide before the project gains
a second machine or a remote — not before `CORE-002`.

**Decision:** ☐ LFS remote ☐ Perforce ☐ accept process-only ☐ defer to M1
**Signed / date:** ______________________

---

## D-3 — Workstation memory

**Question.** Is 15.7 GB acceptable for the graybox milestone?

**Evidence, corrected 2026-08-12 — the original premise was stale.** BLOCKER-003 recorded
that UnrealBuildTool requested 14 parallel actions and was granted **1**, with ~321 MB
free. **That is not what happens now.** Across this session's builds, Unreal Build
Accelerator was granted **6, 7, 10 and 11** parallel actions on the same machine, with
free RAM as low as 0.58 GB. The single-action result was a point-in-time measurement,
not a property of the hardware.

Measured today: editor incremental builds 34–48 s; full `BuildCookRun` including cook,
pak and stage 147–210 s. UBA reports 40 GB of disk-backed storage capacity, so it
degrades throughput under memory pressure rather than serialising.

**Recommendation.** This is no longer a blocker to graybox work — price it as "slower
builds", not "blocked". The machine is still genuinely memory-tight and that will bite
during real shader compilation and content work, so it remains a live risk for Epic 6.
Worth an explicit answer, but not one that gates Epics 1–5.

**Decision:** ☐ accept for graybox ☐ upgrade before Epic 1 ☐ upgrade before Epic 6
**Signed / date:** ______________________

---

## D-4 — Generative-AI provenance policy

**Question.** May generative-AI tooling author project art, and under what
provenance record?

**Evidence.** Open legal question 4 in `Docs/13-AssetLicenseLedger.md`, currently
**undecided**. While undecided, no AI-generated asset may be imported, which blocks
`ART-001`, which blocks all of Epic 6.

**Recommendation.** Answer before Epic 6 planning, not necessarily before M0. But
answer it deliberately — an undecided policy silently becomes a "no" the first time
someone needs an asset.

**Decision:** ☐ permitted, with recorded provenance ☐ prohibited ☐ defer to Epic 6
**Signed / date:** ______________________

---

## D-5 — AGPL exposure in the streaming dependency chain

**Question.** Accept, replace, or isolate `ua-parser-js@2.0.10`
(`AGPL-3.0-or-later`)?

**Evidence.** Open legal question 5 in `Docs/13-AssetLicenseLedger.md`. Independently
confirmed by `ip-compliance-auditor` during this M0 pass, two ways: the installed
package declares `"license": "AGPL-3.0-or-later"` and carries the full AGPL text, and
the `package-lock.json` entry at the pinned PSI commit `48bff3b7` agrees.

**The dependency chain, corrected.** The ledger says it "hangs off a *peer*
dependency." The precise chain is worse in one direction and better in another:

- `SFU/package.json` → `@epicgames-ps/mediasoup-sdp-bridge` (production dependency)
- the bridge declares `mediasoup-client` in both `devDependencies` and
  `peerDependencies`
- `mediasoup-client@3.10.1` declares `ua-parser-js` in **`dependencies`** — an
  unconditional runtime dependency, not a peer

**Why the exposure is nonetheless low — this reverses an earlier assessment.** The
only module that requires the AGPL package is `mediasoup-client/lib/Device.js`. The
bridge's compiled `lib/` imports only `handlers/sdp/commonUtils`,
`handlers/sdp/RemoteSdp`, `handlers/sdp/unifiedPlanUtils` and `ortc`; each one's
require list was traced and **none reaches `Device.js`**. The `mediasoup-client/types`
import is type-only and is erased from the compiled output. The SFU process therefore
never loads AGPL code, and no frontend workspace depends on mediasoup-client at all,
so nothing AGPL reaches the browser bundle.

The exposure is **conveyance, not §13**. AGPL §13 attaches to users interacting with
the covered work remotely; a file present on disk but never loaded is not being
interacted with. There is no linking or combination, so copyleft does not reach the
SFU, the frontend, or the Unreal application — this is aggregation of an unmodified
work. Residual risk: shipping an SFU container image with `node_modules` conveys an
unmodified AGPL work, discharged by a §6 offer of that work's corresponding source
(trivial, since it is public and unmodified) plus the notice.

**Caveat on that evidence.** It is a static require-graph trace. Per D-6,
`mediasoup`'s postinstall was skipped and the SFU has never been executed on this
machine, so nothing was confirmed at runtime.

**An earlier draft of this sheet called this "the single highest-consequence legal
item currently open." That was an overstatement and is withdrawn.** The
highest-consequence open items are the notices obligations (Gate H) and the Unreal
EULA royalty question (D-1 sheet item 1 in the ledger).

**Options.** Install the SFU with `--omit=dev` and assert `node_modules/ua-parser-js`
is absent as a deployment gate check · do not deploy an SFU at all (it is not required
for single-viewer Pixel Streaming sessions) · accept conveyance and discharge with a
§6 source offer plus notice · replace the package upstream.

**Recommendation.** `--omit=dev` plus a deployment assertion. It removes the risk
entirely at near-zero cost and needs no legal opinion. Record the disposition either
way — deferring the answer is fine; leaving the risk unnamed is not.

**Decision:** ☐ `--omit=dev` + gate check ☐ no SFU ☐ accept conveyance, §6 offer
☐ replace ☐ defer
**Signed / date:** ______________________

---

## D-6 — Node runtime pin for deployed workers

**Question.** Pin worker images to `v22.14.0`?

**Evidence.** ASSUMPTION-001, resolved with a caveat. Upstream pins `NODE_VERSION`
= `v22.14.0`. The local box runs **v24.18.0**, two majors ahead. Under v24.18.0:
`npm ci` exit 0 (1,373 packages), `npm run build:all:cjs` exit 0, signalling server
listened on 8888/8889/8080, a packaged Unreal streamer connected and joined, and the
player page served HTTP 200. Nothing exercised the SFU (`mediasoup`'s postinstall was
skipped, so its native worker was never built) and nothing exercised a browser
WebRTC peer.

**Recommendation.** Divergence is fine on a development box and should not be fine on
a worker image. Pin deployed workers to v22.14.0. Follow-up owner:
`pixel-streaming-engineer`, at `STREAM-001`.

**Decision:** ☐ pin workers to v22.14.0 ☐ standardise on v24.x and re-verify upstream
☐ defer to `STREAM-001`
**Signed / date:** ______________________

---

## D-7 — `-nocleanstage` waiver — **CLOSED 2026-08-12, no decision needed**

**This item is withdrawn. The blocker it existed to waive has been fixed.**

BLOCKER-006 is resolved by staging outside `Documents\`:
`-stagingdirectory="$env:LOCALAPPDATA\RacingSimStage"`. `BuildCookRun` now succeeds
with the stage-directory cleanup **enabled**, so `-nocleanstage` has been removed from
the canonical command entirely.

Proven twice — once into an empty staging directory, then again into that same
directory holding 52 files. The second run is the one that counts, because a populated
staging directory is precisely the state that failed three times under `Documents\`.
Both returned `BUILD SUCCESSFUL`, `ExitCode=0`.

The stale-archive risk this item was created to price is also gone: all five pak and
container files now carry the timestamp of the run that produced them, verified within
35 seconds of the build completing.

No system-level security setting was changed. The Defender exclusion suggested earlier
was never tested and proved unnecessary.

**No signature required. Gate A's packaging leg no longer rests on a workaround.**

<details>
<summary>Original decision item, retained for history</summary>

**Question.** Accept packaging via the `-nocleanstage` workaround, and under what
expiry condition?

**Evidence.** BLOCKER-006. `BuildCookRun` fails after a successful cook and pak:

```text
Stage Failed. Failed to delete staging directory
  C:\Users\jun yi\Documents\central-command\racing\Saved\StagedBuilds\Windows
AutomationTool exiting with ExitCode=102 (Error_FailedToDeleteStagingDirectory)
```

Reproduced deliberately as part of this M0 pass. Not transient. The stale-partial-tree
hypothesis is disproven — the directory was verified empty, deleted cleanly with no
lock, and the next run failed identically. Root cause remains unidentified; untested
hypotheses in order of likelihood are Windows Defender real-time scanning, Unreal's
`DirectoryWatcher` (the cook log shows `ReadDirectoryChangesW failed ... GetLastError
code [6]`), or a file-sync/indexer agent on `Documents\`.

**New evidence, 2026-08-12 — the stale-file risk is no longer theoretical.** An earlier
draft of this item said the waiver was safe because "the project has no content, so
there is nothing stale to retain." **`test-engineer` disproved that by direct
observation.** In the verified `-nocleanstage` run:

- `Packaged/Windows/RacingSim.exe` was rewritten — `LastWriteTime` 2026-08-12 10:51:43
- every `.pak`, `.ucas` and `.utoc` under `Packaged/Windows/RacingSim/Content/Paks`
  still carried `LastWriteTime` 2026-08-10 22:12–22:13

The cook step ran and reprocessed all 586 packages that day. **The resulting archive
does not contain that cook's output.** The archive's pak content is two days older than
the cook that supposedly produced it. Anyone treating a `-nocleanstage` archive as "this
cook's output" is wrong today, not merely at some future point when content exists.

**Refined BLOCKER-006 signature.** The failure is specifically at
`Saved\StagedBuilds\Windows\Engine\Binaries\ThirdParty\DbgHelp`, with
`Access to the path ... is denied` after 10 delete attempts — a sharing-violation or
permission signature, consistent with the Defender hypothesis. Separately, the
`ReadDirectoryChangesW ... GetLastError code [6]` line was searched for in the fresh
cook log and **not found**, so that hypothesis is not corroborated and should be
demoted.

**Recommendation, revised.** Do not treat this as a low-cost waiver. The workaround
does not merely risk staleness — it demonstrably produces it. Either fix the root cause
before relying on any packaged artifact for evidence, or accept the waiver with the
explicit condition that **no `-nocleanstage` archive may be cited as evidence for any
gate**. The two experiments remain: exclude the project directory from Defender
real-time protection, then if that fails use `-stagingdirectory=` to a path outside
`Documents\`.

**Gate A is NOT claimed** under this waiver. `test-engineer` returned an explicit
**FAIL** verdict for Gate A.

**Decision:** superseded — root cause fixed, no waiver taken.

</details>

---

## D-8 — Subagent protocol

**Question.** Is the mandatory implement → review → test protocol operational?

**Evidence.** BLOCKER-004. Previously all eight project agents failed to resolve
(`Agent type 'code-reviewer' not found`), which halted every implementation ticket.
Three project agents — `code-reviewer`, `test-engineer`, `ip-compliance-auditor` —
were dispatched during this M0 pass. Disposition recorded in
`Docs/Reports/Phase0-Report.md` §4.

**Recommendation.** If the dispatch succeeded, close BLOCKER-004 and require no
waiver. If it did not, M0 cannot be signed without an explicit waiver, and the
protocol must be formally re-specified against available agents — a governance
change requiring approval, not a silent substitution. `ecc:code-reviewer` is a
different agent with different instructions and is not a drop-in replacement.

**Decision:** ☐ operational, blocker closed ☐ waive and proceed ☐ hold M0
**Signed / date:** ______________________

---

## D-9 — Secret material in git history

**Question.** Rewrite git history to remove two in-history AndroidFileServer security
tokens, or formally accept them?

**Evidence.** Raised by `code-reviewer` during this M0 pass; it was not surfaced in any
earlier document, which is itself the reason it appears here. Two distinct
`SecurityToken` values occur across commits `d9ac6c3`, `8efbfd5`, `c3ab1a7` and
`46e1b25`. HEAD is clean, and `Config/DefaultEngine.ini:27-45` discloses the situation
openly.

**Why the tokens are inert today.** The `AndroidFileServer` plugin is disabled at
`RacingSim.uproject:62-64`, Android is not a build target, and the repository has no
remote — so the history has never left this machine.

**Why it still needs a decision.** `Docs/07-QualityGates.md:70` (Gate G) requires that
secrets are not in source control. Rewriting history costs almost nothing right now:
9 commits, no remote, no collaborators. It becomes expensive and disruptive the moment
a remote is added or anyone else clones. Signing a Gate-G-adjacent milestone without
recording a disposition would leave the decision to be made later under worse
conditions.

**Correction required either way.** `Config/DefaultEngine.ini:40` states that three
tokens reached git history. Independent search of the object graph found **two**.

**Recommendation.** Rewrite now, before a remote exists. If accepted instead, attach
the explicit condition: **must be resolved before any remote is added.**

**Decision:** ☐ rewrite history now ☐ accept, resolve before adding a remote
☐ accept unconditionally
**Signed / date:** ______________________

---

## Carry-forward to Gate G — not a decision, an instruction

`SEC-001` must assert that no `[HTTPServer.Listeners]` section exists in project
configuration.

Unreal MCP was verified bound to `127.0.0.1:8000` only, with connection attempts
refused on all seven non-loopback IPv4 addresses. But that binding comes from the
engine default `BindAddress = FString("localhost")` at
`Engine/Source/Runtime/Online/HTTPServer/Private/HttpServerConfig.h:13`. A single
`[HTTPServer.Listeners]` ini entry overrides it — and produces **no differing log
output**, so the exposure would be silent. The loopback guarantee is a default, not
an enforcement.

Also for the Gate G exposure record: `UbaServer` binds `0.0.0.0:1345` during builds
(NOTE-001). Build-time only, never shipped, but it is a non-loopback listener on a
development machine.

---

## M0 sign-off

I have read `Docs/Reports/Phase0-Report.md`, recorded a disposition for D-1 through
D-9 above, and understand that this approval authorises the start of `CORE-001` only.
It is not a Gate A pass, not a gameplay claim, and not legal clearance.

**Name:** ______________________
**Date:** ______________________
**Signature:** ______________________
