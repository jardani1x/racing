# Copyright RacingSim. All Rights Reserved.
#
# VEH-006 soak gate.
#
# ---------------------------------------------------------------------------
# Why this is a separate script rather than a -TestNames call
# ---------------------------------------------------------------------------
#
# VEH-006's design section settled this before implementation: "a 30-minute soak on the
# per-ticket gate makes the gate unusable ... It gets its own script and its own report
# directory, and its wall-clock cost is measured and written down rather than guessed."
#
# Three things follow from that, and each is a reason this cannot just be
# Run-AutomationFilter.ps1 with a longer name:
#
#   1. The soak is StressFilter, so it is invisible to both Run-Smoke.ps1 (Smoke) and the
#      Product gate. Running it requires asking for it BY NAME, deliberately.
#   2. Its own report directory, so a soak run never overwrites the gate evidence that a
#      reviewer is reading, and vice versa.
#   3. Its cost is the output. This script prints the soak's own summary line -- step
#      count, simulated duration, wall-clock duration, memory delta -- out of the report,
#      because the ticket requires those three reported, and a pass/fail line does not
#      report them.
#
# ---------------------------------------------------------------------------
# Traps this script exists to avoid, all previously paid for
# ---------------------------------------------------------------------------
#
# ABSOLUTE -ReportDir. UnrealEditor-Cmd resolves -ReportExportPath relative to the ENGINE
# directory, not to the working directory, so a relative path writes the report somewhere
# nobody looks and this script then reports NO_INDEX_JSON. Same trap as
# Run-AutomationFilter.ps1's; recorded there too.
#
# `succeeded` IS NOT THE PASS COUNT. A test that passes every assertion but emits one
# UE_LOG(Warning) lands in `succeededWithWarnings` instead. The pass/fail decision is
# `failed` and `notRun`; both success buckets are printed so a warning never looks like a
# regression. Same convention as Run-Smoke.ps1 and Run-AutomationFilter.ps1.
#
# NO INDEX MEANS HARNESS FAILURE, NOT PASS. A run that dies mid-test writes no index.json,
# and an exit code of 0 from a crashed editor is not evidence of anything. Reported as a
# failure, explicitly.
#
# `SetFilter Stress` IS MANDATORY AND MUST COME FIRST. A StressFilter test cannot be found
# by name until the filter is widened, because the automation controller's default is
#
#     RequestedTestFlags = SmokeFilter | EngineFilter | ProductFilter | PerfFilter;
#
# (AutomationControllerManager.cpp:498 and :1009) -- Stress is deliberately absent, and the
# controller asks the worker for tests matching those flags, so a Stress test is never even
# enumerated. Without the SetFilter this script fails as
#
#     LogAutomationCommandLine: Error: No automation tests matched
#     'RacingSim.Vehicle.Soak.ThirtyMinuteDrive'
#
# with PROCESS_EXITCODE=255 and no report, which looks exactly like a crash and is not one.
# `Automation` takes a semicolon-separated list of sub-commands and processes them in order,
# so SetFilter lands before RunTests requests the test list.

param(
    [Parameter(Mandatory = $true)][string]$ProjectPath,
    [Parameter(Mandatory = $true)][string]$ReportDir,

    # Overridable so a smaller soak can be run while iterating, but the DEFAULT is the
    # ticket's test. Anything else is not the gate and must not be reported as it.
    [string]$TestName = 'RacingSim.Vehicle.Soak.ThirtyMinuteDrive'
)

$ErrorActionPreference = 'Continue'

if (-not [System.IO.Path]::IsPathRooted($ReportDir)) {
    Write-Output "ReportDir must be an ABSOLUTE path; UnrealEditor-Cmd resolves -ReportExportPath against the engine directory, not the working directory. Got: $ReportDir"
    exit 1
}

$Cmd = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

if (Test-Path $ReportDir) { Remove-Item -Recurse -Force $ReportDir }

Write-Output "TESTNAME=$TestName"

$StartedAt = Get-Date
Write-Output "SOAK_STARTED=$($StartedAt.ToString('s'))"

& $Cmd $ProjectPath `
    -ExecCmds="Automation SetFilter Stress; RunTests $TestName; Quit" `
    -ReportExportPath="$ReportDir" `
    -unattended -nopause -nosplash -nullrhi -stdout -utf8output | Out-Null

$ExitCode = $LASTEXITCODE
$Elapsed = (Get-Date) - $StartedAt

# The harness's own wall clock, measured outside the editor. The test reports its own
# figure for the driving loop alone; this one includes editor startup and shutdown, and
# the two are not the same number. Both are useful and neither substitutes for the other.
Write-Output "PROCESS_EXITCODE=$ExitCode"
Write-Output "HARNESS_WALLCLOCK_SECONDS=$([math]::Round($Elapsed.TotalSeconds, 1))"

$IndexPath = Join-Path $ReportDir 'index.json'
if (-not (Test-Path $IndexPath)) {
    Write-Output 'NO_INDEX_JSON -- the run produced no report; treat as a harness failure, not a pass.'
    exit 1
}

$Report = Get-Content -LiteralPath $IndexPath -Raw | ConvertFrom-Json
Write-Output "reportCreatedOn=$($Report.reportCreatedOn)"

$Passed = $Report.succeeded + $Report.succeededWithWarnings
Write-Output "succeeded=$($Report.succeeded) succeededWithWarnings=$($Report.succeededWithWarnings) passedTotal=$Passed failed=$($Report.failed) notRun=$($Report.notRun)"
Write-Output "testsInReport=$(@($Report.tests).Count)"
Write-Output "totalDuration=$($Report.totalDuration)"

# The soak's own summary line, which is the actual deliverable of this script: the ticket
# requires simulated duration, wall-clock duration and step count reported. It is a
# Display-severity log entry, so it arrives in the report as an Info event and would
# otherwise be filtered out with every other Info line below.
Write-Output '--- soak summary ---'
$SummaryFound = $false
foreach ($T in @($Report.tests)) {
    foreach ($E in $T.entries) {
        if ($E.event.message -like '*VEH-006 soak:*') {
            Write-Output "  $($E.event.message)"
            $SummaryFound = $true
        }
    }
}
if (-not $SummaryFound) {
    # Deliberately does NOT claim a cause. This used to read "the soak did not reach its
    # summary line", which was wrong: the first passing run emitted the summary and the
    # report simply did not carry it, because a UE_LOG(Display) inside a test reaches
    # index.json only when the framework dumps its captured log on a FAILING test. The spec
    # now emits the summary through AddInfo as well, so absence here is a genuine defect in
    # one of the two -- and which one is a question for the editor log, not for this script.
    Write-Output '  (none -- no VEH-006 soak summary entry in the report; check the run state above and Saved/Logs)'
}

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

if ($Report.failed -gt 0 -or $Report.notRun -gt 0) { exit 1 }
exit 0
