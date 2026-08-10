# Source control and Unreal binary assets

## Recommended approach

For a serious art/content team, use Perforce with exclusive checkout/locking for Unreal binary assets. Unreal has built-in Perforce support. A small prototype may use Git LFS, but it must configure file locking and team discipline before multiple agents or people edit content.

## File classes

Text-mergeable:

- C/C++ source and headers;
- `.Build.cs`, `.Target.cs`;
- config/INI;
- web frontend;
- Python/editor scripts;
- Markdown/design docs;
- automation scripts.

Binary/non-mergeable:

- `.uasset`;
- `.umap`;
- many imported source/baked artifacts.

Do not resolve Unreal binary conflicts with line-oriented Git merges.

## Parallel work

- Claude worktrees are allowed for code/config/web tickets.
- One owner at a time edits a map or content package.
- Content agents submit a change manifest listing every package they intend to touch before editing.
- Parent/integrator checks locks, performs or authorizes MCP/editor changes, saves, validates, and submits.
- Avoid broad "save all" operations that modify unrelated packages.

## World Partition and OFPA

Use World Partition and One File Per Actor for the circuit to reduce contention. They improve collaboration but do not remove the need for source control, ownership, and integration testing. Shared assets, data layers, level instances, materials, and project settings can still conflict.

## Git LFS prototype patterns

If Git is used, establish LFS and locking patterns before importing content. At minimum consider Unreal packages, maps, source DCC files, high-resolution textures, audio, and scan data. Verify that fresh clones actually materialize LFS objects and that CI has credentials/access.

## Derived data

Do not commit local Derived Data Cache, Intermediate, Saved, generated solution files, local editor settings, credentials, or packaged outputs unless the build pipeline explicitly publishes them as artifacts.

## Integration checks

Every content change should run:

- asset reference/load validation;
- redirector cleanup under controlled ownership;
- map load and collision smoke;
- cook/package check;
- screenshot comparison if visible output changed;
- license-ledger audit.
