# `Content/Tracks/` — circuit content

Cooked package path: `/Game/Tracks`. **This directory ships.** It is not excluded by
`DirectoriesToNeverCook` and must not be — the graybox circuit is real game content, not
test content. Test-only assets belong in `Content/Tests/` (`/Game/Tests`).

## `Prototype/Maps/L_Meridian_Graybox.umap`

The graybox test level, authored by **TRACK-002** (a director-approved deferral from
TRACK-001, which specified the level but did not author it).

Contents, in full:

| Actor | Why |
|---|---|
| `Track_Meridian_Graybox` (`ATrackDefinitionActor`) | The deliverable. One closed-loop centerline, three sectors, six ordered checkpoint gates, generated grid and reset poses. |
| `Sun_Graybox` (`ADirectionalLight`) | So the map is not black when a human opens it. |
| `SkyLight_Graybox` (`ASkyLight`) | Same. |

**Nothing else, and no asset references at all** — no static meshes, no materials, no
textures, not even `/Engine/BasicShapes`. That is a deliberate license posture rather than
an omission: TRACK-002's acceptance criterion is "no license-ledger-requiring external
asset", and a level whose only references are its own actors' native classes cannot
acquire one by accident. There is consequently **no road surface mesh**; a spline-mesh
road belongs to the track-art ticket that owns `Docs/13-AssetLicenseLedger.md` entries.

That is sufficient for what the level is for. `RACE-002`'s checkpoint/lap logic drives
analytic positions along the baked centerline, not a car over a mesh.

## Do not hand-edit the map

`.claude/rules/content-safety.md` forbids editing `.uasset`/`.umap` bytes with text tools,
and a binary map cannot be reviewed or merged. The **reviewable artifact for this level is
the script that generates it**, not the map:

```powershell
pwsh -File Scripts/Content/Author-PrototypeGrayboxLevel.ps1
```

`Scripts/Content/Author-PrototypeGrayboxLevel.py` holds every authored number — the
centerline control points, the gate distances, the sector splits, the minimum corner
radius — in reviewable text. It is idempotent: it rebuilds the level from scratch and
overwrites. Two consecutive runs produced an identical 324,466.9 cm centerline and
identical gate distances.

If you change the layout, change the script and re-run it. Do not open the map and drag
the spline, or the script and the asset will disagree and the next regeneration will
silently discard your work.

## Ownership

`Docs/AssetOwnership.tsv` claims `Content/Tracks/Prototype/*` for `junyi` under
`TRACK-002`. `.githooks/pre-commit` blocks any staged `.uasset`/`.umap` that is unclaimed
or claimed by someone else. Claim before editing — `CLAUDE.md` hard-constraint #7 forbids
parallel edits to binary assets, and BLOCKER-002 means LFS locking cannot enforce it.

## Originality

`CLAUDE.md` forbids copying a real circuit's geometry, name, signage or venue. The
centerline is an invented layout drawn to satisfy `Docs/03-TrackRaceUI.md`'s brief (3-5 km,
elevation change, a start/finish straight, mixed corner speeds). It is not traced from,
measured against, or derived from any real venue, map, screenshot or commercial game
asset. The corner names in the script are invented. Keep that true.

## Tests

`Source/RacingSimTests/Race/TrackPrototypeLevelSpec.cpp` loads this map and asserts the
track baked correctly from `PostLoad()` alone. Those tests are **`ProductFilter`, not
`SmokeFilter`**, and must stay that way — see the file's header and `Docs/Environment.md`
for why a `SmokeFilter` test cannot touch an actor in this harness.

```powershell
pwsh -File Scripts/Test/Run-AutomationFilter.ps1 -Filter Product `
    -ProjectPath <uproject> -ReportDir <dir>
```
