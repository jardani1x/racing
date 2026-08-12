# Phase 0 report — inspect and plan

- Date: 2026-08-12 (supersedes the 2026-08-10 edition)
- Author: technical director (Claude Opus 5)
- Status: **submitted for human approval**
- Commit reviewed: `a96187c`
- Commits covered: `266dc32`, `f37938f`, `b631c1b`, `08131b1`, `8efbfd5`, `c3ab1a7`,
  `46e1b25`, `a96187c`, plus the corrections commit carrying this rewrite
- Companion document: `Docs/Reports/M0-DecisionSheet.md` — D-1 through D-9

---

## 1. Outcome and confidence

**Outcome: Phase 0's investigative work is complete and has now been independently
reviewed. Gate A is NOT passed. M0 is a decision for the human owner, not a result
this report can claim.**

Every Epic 0 ticket is closed. The environment, toolchain, commands, licence
position and MCP security posture are recorded with running evidence. What changed
since the 2026-08-10 edition is that three independent read-only agents re-examined
that evidence, and **they found the self-assessment wrong in one place that matters**.

Confidence is **high** on the corrected record, because the corrections came from
agents that could not approve their own work and that re-ran commands rather than
reading the record. Confidence is **withheld** wherever this report says UNVERIFIED
or NOT RUN; those are unknowns, not gaps filled with plausible defaults.

The three findings that most change the picture since the last edition:

1. **BLOCKER-004 is closed.** Project subagents dispatch. Three of the eight ran.
2. **The Gate G plugin-exclusion evidence was false as written**, and two independent
   agents confirmed it from the artifact. The MCP conclusion survives; the sentence
   used as proof did not.
3. **`-nocleanstage` produces stale archives in practice, not just in theory** —
   directly observed, and it invalidates the "harmless while the project has no
   content" reasoning that the packaging waiver rested on.

## 2. Acceptance criteria status

| Phase 0 requirement (from `PROMPT_TO_START.md`) | Status |
|---|---|
| Verify repo state, engine association, toolchain, OS, GPU, source-control mode, plugins, disk/RAM | **DONE** |
| Verify Unreal version and Pixel Streaming Infrastructure branch match | **DONE** — UE 5.8.1 (CL 56057345); PSI `UE5.8` pinned at `48bff3b7`, checkout HEAD independently confirmed |
| Verify Unreal MCP setup without exposing beyond loopback | **DONE** — ENV-005; loopback-only confirmed by probe, write test deliberately skipped |
| Inventory all assets and licenses; quarantine anything without provenance | **DONE** — LEGAL-001, re-inventoried and then independently audited; nothing quarantined; 5 open legal questions |
| Discover and record actual build/test/cook/package commands | **DONE** — ENV-004; every command re-run by `test-engineer`. Packaging still requires the `-nocleanstage` workaround |
| Create ADR for Pixel Streaming 2 and scaling | **DONE** — ADR-0001, plus 0002/0003/0004 |
| Turn backlog into ordered tickets, dependencies, milestone gates | **DONE** — `Docs/Tickets.md` |
| Identify assumptions needing a human answer | **DONE** — 9 decisions in `Docs/Reports/M0-DecisionSheet.md` |

**Gate A: NOT PASSED.** `test-engineer` returned an explicit FAIL verdict on two
grounds — `BuildCookRun` fails without `-nocleanstage` (exit 102), and the packaged
Game target mounts two editor-only plugins at runtime. Neither is waived by this
report.

## 3. Files and assets changed

No binary assets are tracked. Everything committed is text.

**Created across Phase 0:** `.gitignore`, `.gitattributes`, `RacingSim.uproject`,
`Source/RacingSim.Target.cs`, `Source/RacingSimEditor.Target.cs`,
`Source/RacingSim/{RacingSim.Build.cs,RacingSim.h,RacingSim.cpp}`,
`Config/{DefaultEngine.ini,DefaultGame.ini,DefaultInput.ini}`,
`Samples/PixelStreaming2/WebServers/get_ps_servers.{bat,sh}`,
`Docs/ADR/ADR-0001..0004`, `Docs/Tickets.md`, `Docs/Reports/M0-DecisionSheet.md`,
this report.

**Rewritten:** `Docs/Environment.md` (was an empty template).
**Extended:** `Docs/13-AssetLicenseLedger.md` (template preserved, real entries appended).

**Fetched, not committed:** `Web/PixelStreamingInfrastructure` at
`48bff3b751f91f735b50c90b2a7fec5ceb2a440f` — gitignored by design.

**Untracked artifacts on disk:** `.mcp.json` (gitignored, `.gitignore:52`),
`Web/PixelStreamingInfrastructure/node_modules` (~1,373 packages),
`Packaged/`, `Saved/StagedBuilds/`.

## 4. Review findings and disposition

**Independent review was performed.** This supersedes the previous edition's
statement that none had been. Three read-only agents were dispatched against
`a96187c`: `code-reviewer`, `test-engineer`, `ip-compliance-auditor`. None could
approve its own work; none holds write access.

### Blocking findings, and what was done

| # | Finding | Disposition |
|---|---|---|
| B-1 | `Docs/Environment.md:197-212` claimed "zero matches" for six editor-only plugins in the packaged tree. **False for two of them.** `PythonScriptPlugin` and `EditorScriptingUtilities` are staged and mounted at runtime | **Corrected.** Section rewritten; the plugin table's "In shipping" column restated as a module-binary claim; recorded in `Docs/Tickets.md` under B-1 |
| B-2 | `Docs/Tickets.md` carried two contradictory ENV-005 acceptance blocks — one all `[x]`, one all `[ ]` | **Corrected.** Duplicate deleted |
| B-2b | Ticket line claimed 6 open legal questions; five are open (#3 struck closed). ENV-003 line said the manifest check was outstanding when it had been done | **Corrected** |
| B-3 | This report was stale — it described a project state that no longer existed and was the document the sign-off sheet tells the signer to read | **Corrected** by this rewrite |
| B-4 | Two AndroidFileServer `SecurityToken` values in git history across `d9ac6c3`, `8efbfd5`, `c3ab1a7`, `46e1b25`, never surfaced to the signer. `Config/DefaultEngine.ini:40` said three | **Surfaced as D-9** in the decision sheet; count corrected to two |
| F-1 | `Docs/13-AssetLicenseLedger.md` claimed the project contains zero binary assets, unqualified. True of the git index only; the working tree stages a font and ~30 third-party DLLs. The supporting reasoning was invalid — `git status --untracked-files=all` does not list ignored paths | **Corrected.** Claim scoped to the tracked tree; `ASSET-0007` opened |
| F-2 | Ledger said "this project has no notices file." False — `Packaged/Windows/NOTICES.txt` exists and is **one line** | **Corrected**, with a concrete statement of what compliance requires |

### Findings that lowered a recorded risk

- **AGPL exposure downgraded.** `ua-parser-js@2.0.10` is confirmed AGPL-3.0-or-later,
  and the chain is *harder* than recorded — the final hop is a `dependencies` entry,
  not a peer. But the require graph was traced: the only module needing it is
  `mediasoup-client/lib/Device.js`, and the SFU's compiled import path never reaches
  it. **The SFU never loads AGPL code and nothing AGPL reaches the browser bundle.**
  Exposure is conveyance, not §13. The earlier "single highest-consequence legal item"
  framing is withdrawn. Cheapest complete fix: `--omit=dev` plus a deployment
  assertion.
- **`spawndamnit@3.0.1` is verbatim MIT.** Finding closed.
- **All three "genuinely unlicensed" packages are MIT** — a legacy `licenses` array, a
  wrong lockfile entry, and a British-spelled `LICENCE`. Zero genuinely unlicensed
  third-party packages.
- **The licence census is sound.** Reproduced exactly, then strengthened: all 1,349
  installed packages compared against lockfile metadata, two mismatches, both benign.
  The original methodological caveat is discharged.
- **The four deferred `.gitignore`/`.gitattributes` findings are all still deferrable.**
  None is a live defect. Ticket citation corrected: `*.pdb` is `.gitignore:33`, not `:37`.
- **Branded content: PASS.** Zero manufacturer, model, circuit, sponsor or series marks
  across all 56 tracked files, except prohibitions and legal citations.

### Findings routed to later tickets

- **N-2 — `CORE-001` had two incompatible acceptance definitions. RESOLVED 2026-08-12
  by the project owner.** `Docs/15-ProjectStructure.md` showed folders inside one
  module; `Docs/Tickets.md` required one module per layer. The decision is **two
  modules**: `RacingSim` (`Runtime`) carrying the five gameplay layers as folders, and
  `RacingSimTests` (`UncookedOnly`) so test code physically cannot ship. All three
  documents that disagreed — `Docs/15-ProjectStructure.md`, `Docs/Tickets.md`,
  `README.md` — now agree. Accepted consequence: boundaries between the five gameplay
  layers are enforced by review, not by the linker.
- **N-1** — `EngineAssociation: "5.8"` cannot express the patch in a launcher install;
  fold an engine-CL assertion into the `CORE-002` build-ID scheme.
- **F-4/F-5** — `Samples/` scripts are staged into the distributable despite being
  marked reference-only, and carry a UE 5.8 fallthrough to branch `UE5.7`.
  `ASSET-0006` status qualified.
- **F-6** — the PSI default frontend serves Epic showcase art; scope item for
  `STREAM-002`.
- **N-3** — `CookRule=AlwaysCook` is inert under `bIsEditorOnly=True`; cosmetic.
- **The `-log=<name>.log` flag in the automation command does not work.** Only
  `Saved/Automation/Report/index.json` is authoritative evidence from that command.

### Self-identified errors from earlier in Phase 0

Retained because the evidence policy requires it: a failed build nearly reported as
passing (exit code came from a shell pipeline while the log read `'C:\Program' is not
recognized`); a misread MSVC ban that triggered an unnecessary toolchain install,
cancelled; and a "JSON parse error" that was an unquoted path splitting at
`C:\Users\jun`. Three failures from paths containing spaces. Standing hazard note in
`Docs/Tickets.md` and `Docs/Environment.md`.

## 5. Commands run and result locations

All re-run by `test-engineer` on 2026-08-12 and verified by reading logs, not exit codes.

| Command | Result |
|---|---|
| `Build.bat RacingSimEditor Win64 Development` | **Exit 0, zero warnings.** But the log reads `Target is up to date` / `0 action(s)` — a **no-op**. This run did not exercise the compiler. The 179.60 s full compile stands only as a historical record |
| `UnrealEditor-Cmd … -ExecCmds="Automation RunFilter Smoke; Quit"` | **426 succeeded / 0 failed / 0 notRun**, `reportCreatedOn 2026.08.12-02.41.42`. Read from `Saved/Automation/Report/index.json`. These are **engine** tests; the project has none |
| `RunUAT BuildCookRun … -cook -stage -pak -archive` (no `-nocleanstage`) | **FAILED, exit 102**, reproduced a third time. Cook succeeded (586 packages), UnrealPak exit 0, staging failed at `Saved\StagedBuilds\Windows\Engine\Binaries\ThirdParty\DbgHelp` — `Access to the path … is denied` after 10 attempts |
| same, `-nocleanstage` | **Exit 0**, 167.48 s, 586 packages. Archive `Packaged/Windows/RacingSim.exe` (171,520 bytes), pak 11,170,982 bytes, tree ~1.03 GiB / 55 files. **But see §7 — the archive's pak content is stale** |
| Packaged plugin manifest | `ModelContextProtocol`, `ModelContextProtocolEngine`, `AllToolsets`, `ToolsetRegistry`, `AndroidFileServer` — **zero matches** in all three manifests, absent from `Engine/Plugins`, absent from ~190 `Mounting Engine plugin` lines. **`PythonScriptPlugin` and `EditorScriptingUtilities` are present and mounted** (`Manifest_UFSFiles_Win64.txt:843,894,1696`) |
| Listener check, ports 8000/8080/8888/8889 | **Nothing listening**, checked twice |
| `git clone` clean-clone test | Passed — 39 files, HEAD matched, 8 `lockable` patterns restored |
| `git lfs lock Test.uasset` | Failed as expected — `missing protocol`; BLOCKER-002 |
| `npm ci` / `npm run build:all:cjs` / signalling start / packaged PS2 launch | All succeeded on 2026-08-10; recorded verbatim in `Docs/Environment.md`. **Not re-run**, and not reproducible from the repository alone since PSI is gitignored |
| Agent dispatch — `code-reviewer`, `test-engineer`, `ip-compliance-auditor` | **All three resolved and returned reports.** BLOCKER-004 closed |

**Not run:** any Shipping-configuration build; an off-host probe of MCP port 8000; any
MCP write tool; a browser client; the SFU; TURN.

## 6. Visual and performance evidence

**None, and none is claimable.** No content exists and no frame has reached a viewer.
Per ADR-0003 the development laptop is not the reference GPU worker, so no number
produced here could serve as a Gate D/E/F result even if measured.

One performance-relevant environmental fact stands: UnrealBuildTool requested 14
parallel compile actions and was granted **1**, with ~321 MB free physical memory on a
15.7 GB machine (BLOCKER-003).

One artifact-level fact worth recording, which nobody had written down: **the packaged
build actually runs.** `Packaged/Windows/RacingSim/Saved/Logs/RacingSim.log` has zero
`Error:` lines, no `ensure`, no load failures, and two benign warnings. That is the
strongest available evidence that the `bIsEditorOnly=True` AssetManager fix is correct
— demonstrated empirically rather than reasoned from engine source.

## 7. Open risks, strongest counterargument, rollback

### Blockers

- **BLOCKER-001 — no reference GPU worker.** Gates D/E/F unmeasurable; Epics 6–7 blocked. **D-1.**
- **BLOCKER-002 — LFS locks inert.** Hard constraint #7 rests on process discipline. **D-2.**
- **BLOCKER-003 — memory pressure.** Builds effectively single-threaded. **D-3.**
- **BLOCKER-006 — staging delete fails.** Reproduced three times. Root cause still
  unidentified, but the signature sharpened: a single directory
  (`Engine\Binaries\ThirdParty\DbgHelp`) and `Access to the path … is denied`. Windows
  Defender is now the leading hypothesis; the `DirectoryWatcher` hypothesis is
  **demoted** — `ReadDirectoryChangesW … GetLastError code [6]` was searched for in the
  fresh cook log and not found. **D-7.**
- **BLOCKER-004 — CLOSED.** Subagents dispatch.
- **BLOCKER-005 — CLOSED.** AssetManager rule added, `bIsEditorOnly=True` verified, and
  the packaged build's clean runtime log demonstrates it.

### The two risks that materialised during this pass

**`-nocleanstage` produces stale archives — observed, not theorised.** In the verified
run, `RacingSim.exe` was rewritten at 2026-08-12 10:51:43 while every `.pak`, `.ucas`
and `.utoc` under `Content/Paks` still carried 2026-08-10 22:12–22:13. The cook ran and
reprocessed all 586 packages. **The archive does not contain that cook's output.** The
prior reasoning — safe because the project has no content — is wrong. No `-nocleanstage`
archive should be cited as gate evidence while BLOCKER-006 is open.

**Editor-only plugins reach the shipped target.** `PythonScriptPlugin` and
`EditorScriptingUtilities` are staged and mounted at runtime. Cause:
`FPluginReferenceDescriptor::IsEnabledForTarget` is evaluated per reference, so
`TargetAllowList` suppresses only the project's own reference while other enabled engine
plugins re-enable these two. Gate A's "disallowed editor-only plugins are excluded from
shipping" cannot be called passing. The MCP exclusion itself is real and confirmed three
ways.

A consequence nobody had recorded: because editor plugins load in the cook commandlet,
**which editor plugins are enabled changes the shipped bytes**. Disabling `AllToolsets`
later will alter `global.ucas`.

### Legal position

Five open questions, all needing a human legal owner: Unreal EULA royalty treatment for
a streamed product; third-party redistribution terms for a worker fleet (the question
names NVENC/AMF, but the actual staged set is different and the question should be
rewritten against `Manifest_NonUFSFiles_Win64.txt`); generative-AI provenance
(**blocks ART-001, therefore all of Epic 6**); AGPL conveyance in the SFU image; and
notices obligations in aggregate. None blocks M0.

### Strongest counterargument to approval

*This evidence package is honest about everything expensive and was wrong about the one
thing that was cheap to check.* The blockers, the packaging waiver, the AGPL exposure,
the missing GPU worker, the skipped write test, the node-version correction — all
disclosed, several against the author's own interest. But the single claim stated as
VERIFIED and used as Gate G proof failed on the first independent look at the artifact
it cited, and the report the signer was told to read described a project that no longer
existed. A milestone whose evidence package is internally inconsistent should not be
signed on the strength of its candour elsewhere.

**Response, and its limit.** Every defect found was in *text*, not in the build. The
packaged build is clean, the MCP exclusion is real, the AssetManager fix is empirically
demonstrated. All the repairs are documentation, and they are in this commit. The limit
is real though: the corrections were found because three agents were pointed at the
work, and that is now the minimum standard for every subsequent ticket — not an optional
extra when convenient.

### Rollback

Every change is text on a local `main` with no remote. `git reset --hard 266dc32`
returns to documentation-only state; deleting the repository,
`Web/PixelStreamingInfrastructure`, `Packaged/` and `Saved/` returns to pre-Phase-0.
Nothing was installed and no system setting was changed. The one process-level action —
terminating a running editor — was authorised, and those processes had already exited.

## 8. Next unblocked ticket

**`CORE-001`, gated on one remaining thing:**

**Human signature on `Docs/Reports/M0-DecisionSheet.md`** (D-1 through D-9). No agent
may sign it; `PROMPT_TO_START.md` constraint 8 and `CLAUDE.md` both forbid
self-certification.

N-2 — the contradictory `CORE-001` acceptance definitions — was **resolved on
2026-08-12** by the project owner. The ticket now has a single reviewable acceptance
checklist in `Docs/Tickets.md`, and `Docs/15-ProjectStructure.md` and `README.md` were
updated to match. Nothing else blocks the ticket.

Decisions needed from the human owner are enumerated in the decision sheet: reference
GPU worker funding, binary-asset serialization, workstation memory, generative-AI
provenance, AGPL disposition, node pin, the `-nocleanstage` waiver, the subagent
protocol, and the in-history security tokens.

---

**This project is not production-ready and no gate has been claimed as passed.**
Gates A–H remain open. **Gate A is explicitly NOT passed** — `test-engineer` returned a
FAIL verdict on the packaging workaround and the plugin-exclusion gap. `production-ready`
may not appear in any report before M4, per `Docs/07-QualityGates.md`.
