# `Content/Tests/` — test-only content

Cooked package path: `/Game/Tests`.

Functional-test maps and any other asset that exists only to be tested against live
here, per `Docs/15-ProjectStructure.md`. Nothing here ships.

## The rule

`Config/DefaultGame.ini` lists `/Game/Tests` in `DirectoriesToNeverCook`, so the cooker
skips this directory **even when shipping content references it**
(`ProjectPackagingSettings.h:570-575`). That last clause is the whole point: a
functional-test map legitimately references real gameplay assets, and a reference in the
other direction — or an AssetManager primary-asset rule that scans too broadly — is how
test content gets dragged into a cook without anyone deciding it should.

## Why this directory exists while empty

TEST-001 created the exclusion before the first test asset, deliberately. Adding the
config entry at the same commit as the first functional-test map means the map is
authored, cooked and packaged at least once before anyone thinks about excluding it, and
by then "does test content ship" is a question about an artifact that already shipped.

An empty directory also keeps the check honest about what it currently proves. See the
TEST-001 evidence section in `Docs/Tickets.md`: the pak-side check is real and its
positive control passes, but with no cooked asset under `/Game/Tests` it is at present
verifying an exclusion whose subject does not yet exist. The first ticket to add a
functional-test map (`TRACK-002` or `RACE-002`) must re-run
`Scripts/Test/Check-NonShippingArtifacts.ps1 -Mode Pak` against a fresh package and
record the result, because that run is the first one that can actually fail.

## Checks

```powershell
# Config + .target receipts. No package required.
pwsh -File Scripts/Test/Check-NonShippingArtifacts.ps1 -Mode Receipt
pwsh -File Scripts/Test/Check-NonShippingArtifacts.ps1 -Mode Config

# Pak-side. Requires a completed BuildCookRun (Docs/Environment.md).
pwsh -File Scripts/Test/Check-NonShippingArtifacts.ps1 -Mode Pak
```

The same config assertion also runs inside the automation suite as
`RacingSim.Tests.NonShippingArtifacts`, so it is covered by the project's documented
`Automation RunFilter Smoke` gate rather than only by a script someone has to remember.
