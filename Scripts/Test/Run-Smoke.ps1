# Copyright RacingSim. All Rights Reserved.
#
# Runs this worktree's automation Smoke filter and reports the counts from
# Saved/Automation/Report/index.json.
#
# Docs/Environment.md: counts must be read from index.json (succeeded/failed/notRun),
# NEVER from the process exit code, and -log=<name>.log does not work on this engine
# build. The report is therefore the only authoritative artifact this command produces.
#
# TRACK-002 repair cycle 1 ADDED output fields here. Run-AutomationFilter.ps1's header
# states this script is "deliberately left alone" because past evidence (CORE-002,
# TRACK-001, TEST-001) cites it by name, and a script that changes behaviour under a cited
# name makes old evidence unverifiable. That policy is respected rather than overridden:
# no existing field was removed and none changed meaning -- `succeeded=` still prints
# exactly $Report.succeeded. The additions exist because printing that field ALONE was
# actively misleading (see the comment at the print site).

param(
    [Parameter(Mandatory = $true)][string]$ProjectPath,
    [Parameter(Mandatory = $true)][string]$ReportDir
)

$ErrorActionPreference = 'Continue'

$Cmd = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

. (Join-Path $PSScriptRoot 'ReportDirectory.ps1')

# VEH-006 repair cycle 3. This used to be one line:
#
#     if (Test-Path $ReportDir) { Remove-Item -Recurse -Force $ReportDir }
#
# a recursive force-delete of whatever path the caller named, unchecked, under
# $ErrorActionPreference = 'Continue'. See ReportDirectory.ps1 for what that cost and
# what the replacement refuses to do. The header policy above -- no existing output
# field removed or changed in meaning -- still holds: every Write-Output below is
# untouched, and everything added here either refuses before the run or appends after it.
if (-not (Test-RacingSimAbsoluteReportDir -ReportDir $ReportDir)) { Write-RacingSimRefusals; exit 1 }
if (-not (Reset-RacingSimReportDirectory -ReportDir $ReportDir -MarkerName '.racingsim-report' -OwnerScript 'Run-Smoke.ps1')) { Write-RacingSimRefusals; exit 1 }

& $Cmd $ProjectPath `
    -ExecCmds="Automation RunFilter Smoke; Quit" `
    -ReportExportPath="$ReportDir" `
    -unattended -nopause -nosplash -nullrhi -stdout -utf8output | Out-Null

Write-Output "PROCESS_EXITCODE=$LASTEXITCODE"

$IndexPath = Join-Path $ReportDir 'index.json'
if (-not (Test-Path $IndexPath)) {
    Write-Output 'NO_INDEX_JSON -- the run produced no report; treat as a harness failure, not a pass.'
    exit 1
}

# CHECKED. Unchecked, a truncated or malformed index.json -- the shape an editor killed
# partway through leaves behind -- made ConvertFrom-Json emit a non-terminating error under
# $ErrorActionPreference = 'Continue' and left $Report null. Every field below then
# expanded to the empty string, `failed` compared as 0, and the run reported green on a
# report it could not read.
try
{
    $Report = Get-Content -LiteralPath $IndexPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
}
catch
{
    Write-Output "UNREADABLE_INDEX_JSON -- $IndexPath exists but could not be parsed: $($_.Exception.Message)"
    Write-Output 'Treat as a harness failure, not a pass.'
    exit 1
}

Write-Output "reportCreatedOn=$($Report.reportCreatedOn)"

# `succeeded` IS NOT THE PASS COUNT. The report splits passing tests into `succeeded` and
# `succeededWithWarnings`; a test that passes every assertion but emits one UE_LOG(Warning)
# moves from the first bucket to the second. Printing only `succeeded` therefore reports a
# DROP when nothing regressed -- TRACK-002 repair cycle 1 hit exactly that: 462 -> 457, total
# 465 -> 466 (up by one, the cycle's one new test), because a new (correct) warning moved
# six suites into the warnings bucket. Both buckets and the total are printed so a later
# ticket cannot misread a bucket shift as lost coverage. The pass/fail decision is `failed`
# and `notRun`, not either bucket.
$Passed = $Report.succeeded + $Report.succeededWithWarnings
Write-Output "succeeded=$($Report.succeeded) succeededWithWarnings=$($Report.succeededWithWarnings) passedTotal=$Passed failed=$($Report.failed) notRun=$($Report.notRun)"
Write-Output "testsInReport=$(@($Report.tests).Count)"
Write-Output "totalDuration=$($Report.totalDuration)"

$Failed = @($Report.tests | Where-Object { $_.state -ne 'Success' })
Write-Output "NON_SUCCESS_COUNT=$($Failed.Count)"
foreach ($T in $Failed) {
    Write-Output "  FAIL: $($T.fullTestPath) => $($T.state)"
    foreach ($E in $T.entries) {
        if ($E.event.type -ne 'Info') { Write-Output "     $($E.event.type): $($E.event.message)" }
    }
}

Write-Output '--- RacingSim.* suites ---'
foreach ($T in @($Report.tests | Where-Object { $_.fullTestPath -like 'RacingSim.*' } | Sort-Object fullTestPath)) {
    Write-Output "  $($T.fullTestPath) => $($T.state)"
}

# THE SCRIPT USED TO END HERE, exiting 0 whatever the report said. That is the M7 residual
# recorded in Docs/Tickets.md: the only non-zero exit was the missing-index.json branch, so
# a run with failing tests, or with no tests at all, was indistinguishable from a clean one
# to anything reading the exit code -- CI, a wrapper script, or a gate report written from
# the outside. The counts were printed correctly the whole time; nothing was ever obliged
# to read them.
#
# Nothing above this line changed, so every past evidence citation of this script's output
# stays verifiable. What is added is the exit status those citations always implied.
$Problems = Test-RacingSimReportHasPositiveProof -Report $Report
if ($Problems.Count -gt 0)
{
    Write-Output '--- GATE FAILED ---'
    foreach ($Problem in $Problems) { Write-Output "  $Problem" }
    exit 1
}

Write-Output "GATE_PASSED passedTotal=$Passed"
exit 0
