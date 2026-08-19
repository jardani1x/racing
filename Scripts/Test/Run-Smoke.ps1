# Copyright RacingSim. All Rights Reserved.
#
# Runs this worktree's automation Smoke filter and reports the counts from
# Saved/Automation/Report/index.json.
#
# Docs/Environment.md: counts must be read from index.json (succeeded/failed/notRun),
# NEVER from the process exit code, and -log=<name>.log does not work on this engine
# build. The report is therefore the only authoritative artifact this command produces.

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
Write-Output "succeeded=$($Report.succeeded) failed=$($Report.failed) notRun=$($Report.notRun)"
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
