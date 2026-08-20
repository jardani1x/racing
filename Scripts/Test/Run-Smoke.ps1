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

if (Test-Path $ReportDir) { Remove-Item -Recurse -Force $ReportDir }

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

$Report = Get-Content -LiteralPath $IndexPath -Raw | ConvertFrom-Json
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
