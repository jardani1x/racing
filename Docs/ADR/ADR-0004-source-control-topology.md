# ADR-0004: Source-control topology and binary-asset serialization

- Status: Accepted, with a known enforcement gap
- Date: 2026-08-10
- Deciders: human project owner
- Related: `Docs/10-SourceControl.md`, `Docs/Environment.md` BLOCKER-002, hard constraint #7

## Context

Hard constraint #7 forbids parallel edits to `.uasset` and `.umap` files, and
`CLAUDE.md` forbids attempting text merges of Unreal binary assets. Enforcing
that normally means exclusive checkout.

At the start of Phase 0 there was **no source control at all** — neither
`racing/` nor any parent directory was a git repository. Available tooling:
git 2.53.0.windows.2 and git-lfs 3.7.1. Perforce was not installed (`p4` absent).

## Decision

**git with Git LFS, local-only, no remote.**

- Branch `main`.
- Unreal-standard `.gitignore`: `Binaries/`, `Build/`, `Intermediate/`, `Saved/`,
  `DerivedDataCache/`, IDE files, packaged output.
- `.gitattributes` marks `*.uasset` and `*.umap` — plus `*.fbx`, `*.blend`,
  `*.psd`, `*.tga`, `*.wav`, `*.ogg` — as `filter=lfs … -text lockable`.
- Pixel Streaming Infrastructure is **ignored, not vendored**. It is fetched by
  cloning the commit pinned in `Docs/Environment.md`.
- Unreal MCP's generated `.mcp.json` is ignored as machine-local and loopback-only.

## Known enforcement gap

**`lockable` does not work in this configuration.** This was tested, not assumed:

```text
$ git lfs lock Test.uasset
hint: The remote resolves to a file:// URL, which can only work with a
hint: standalone transfer agent.
Locking Test.uasset failed: missing protocol: "file:///C:/Users/.../racing/.git"

$ git lfs lock Docs/Test.uasset          # in the origin repo, which has no remote
Locking Docs/Test.uasset failed: missing protocol: ""
```

git-lfs lock enforcement requires an HTTP LFS server implementing the locking API.
A local path or `file://` remote cannot provide one. The `lockable` attributes are
therefore **inert** — they are recorded correctly and will activate the moment an
LFS-capable remote is added, but today they enforce nothing.

**Consequence: hard constraint #7 rests on process discipline alone.** Content
changes must be serialized in the integration checkout by human/agent convention.
Concretely:

- Only one actor edits `.uasset`/`.umap` at a time, in the integration checkout.
- Code-only subagents may use worktrees (they touch no binary assets).
- Any ticket touching content declares explicit asset ownership before starting.

Tracked as BLOCKER-002.

## Alternatives rejected

**git + LFS with a GitHub remote.** Would make locking real. Rejected for now:
adds a hosting dependency during Phase 0, and Unreal binary assets consume LFS
storage and bandwidth quota quickly. Remains the cheapest path to real locks and
should be reconsidered before content work begins.

**Perforce (Helix Core).** The Unreal-native answer — first-class exclusive
checkout, built for large binaries. Rejected for Phase 0 as the heaviest setup
(server install and administration) for a project with zero binary assets so far.

**No source control.** Rejected. Leaves every ticket without rollback.

## Verification

- Baseline commit `266dc32`, 39 files.
- Clean-clone test passed: cloned to a scratch path, 39 tracked files restored,
  HEAD matched, 8 `lockable` patterns present in the clone's `git lfs track`.
- Project shell committed as `f37938f`; `Web/PixelStreamingInfrastructure` was
  correctly excluded by `.gitignore`, confirming the ignore rules work.
- Lock round trip **failed as documented above** — this is the gap, not a defect
  in the configuration.

## Revisit when

Any of: a second person or parallel agent begins content work; the first
`.uasset` is committed; or Epic 6 starts. At that point the advisory-lock gap
becomes a real collision risk and a remote or Perforce should be adopted.
