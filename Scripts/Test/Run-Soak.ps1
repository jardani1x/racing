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

. (Join-Path $PSScriptRoot 'ReportDirectory.ps1')

# IsPathRooted was the check here, and it is not an absolute-path test: "C:report" and
# "\report" both pass it and both resolve against per-process state this script does not
# set. See Test-RacingSimAbsoluteReportDir.
if (-not (Test-RacingSimAbsoluteReportDir -ReportDir $ReportDir)) { Write-RacingSimRefusals; exit 1 }

# A non-default -TestName is legitimate -- iterating on a smaller soak is why the parameter
# exists -- but its output must not be mistaken for the gate afterwards, and a transcript
# read a week later has nothing in it to say which was run. Say so in the output itself.
if ($TestName -ne 'RacingSim.Vehicle.Soak.ThirtyMinuteDrive') {
    Write-Output "NOT_THE_GATE -- TestName is $TestName, not the ticket's RacingSim.Vehicle.Soak.ThirtyMinuteDrive. This run is an iteration aid; do not cite its output as VEH-006 soak evidence."
}

# Guards the one shape that fails LATE and confusingly. `Automation RunTests <name>` with
# an empty or whitespace name requests the empty test list, and the editor then exits
# cleanly having run nothing, producing a report with zero tests -- which the positive
# proof below correctly rejects, but only after the operator has waited out an editor
# start-up for a mistake visible before it.
if ([string]::IsNullOrWhiteSpace($TestName)) {
    Write-Output 'TestName must be a non-empty automation test path.'
    exit 1
}

$Cmd = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

# REFUSE rather than delete anything that is not obviously a previous report of ours.
# The only validation this had was IsPathRooted, so a mistyped -ReportDir naming a real
# directory was recursively force-deleted without a word. A stale report has to go,
# because index.json from a previous run would otherwise be read as this run's result,
# so the rule is narrow: empty, ours, or already looking like an automation report.
#
# OURS is the marker file, and it is not decoration. index.json alone is not a workable
# ownership test, because the run that fails hardest is the one that writes no index.json
# at all -- and it still leaves the editor log behind, which makes the directory
# non-empty. A crashed soak would therefore lock its own report directory out of every
# future run, and the operator's only route back would be to delete it by hand: the
# guard would be at its most obstructive in exactly the situation it was added to help
# with. The marker is written by this script and by nothing else, so it says what
# index.json cannot -- that whatever state this directory is in, we are the ones who
# put it there.
#
# The rule this file introduced is now shared with the two sibling scripts, which had no
# guard at all, and it gained three things it was missing here:
#
#   - index.json ALONE no longer licenses the delete. Mere existence of a file with that
#     name was enough, and any tool can write one; it must now parse and carry the fields
#     an automation report has.
#   - New-Item and Set-Content are CHECKED. Both ran unchecked under
#     $ErrorActionPreference = 'Continue', so a read-only or otherwise unwritable
#     ReportDir produced no marker, no directory and no complaint, and the run went on to
#     fail later for a reason that had nothing to do with the cause.
#   - A live owner is REFUSED. Two runs pointed at one report directory used to race, the
#     second deleting the first one's output partway through its thirty minutes.
$OwnershipMarkerName = '.racingsim-soak-report'
if (-not (Reset-RacingSimReportDirectory -ReportDir $ReportDir -MarkerName $OwnershipMarkerName -OwnerScript 'Run-Soak.ps1')) {
    Write-RacingSimRefusals
    exit 1
}

# Editor stdout goes to a FILE, not to Out-Null. When the NO_INDEX_JSON branch below
# fires it means the editor produced no report at all, which is exactly the case where
# the reason is only in the editor output -- and that output was being discarded, so
# the harness reported a failure with no evidence attached to it.
$EditorLog = Join-Path $ReportDir 'soak-editor-stdout.log'

Write-Output "TESTNAME=$TestName"

$StartedAt = Get-Date
Write-Output "SOAK_STARTED=$($StartedAt.ToString('s'))"

# 2>&1 MERGES STDERR IN. Without it the log captured stdout only, and the failures worth
# capturing are the ones that write to stderr: a fatal assertion, a missing DLL, an access
# violation handler. The NO_INDEX_JSON branch below then printed forty lines of ordinary
# start-up chatter and called it the evidence, with the actual message discarded.
#
# The cost, stated because it bites in this specific shell: PowerShell 5.1 wraps each
# stderr line from a native executable in an ErrorRecord and sets $? to $false, so
# $LASTEXITCODE is read into $ExitCode immediately below and NOT inferred from $?.
& $Cmd $ProjectPath `
    -ExecCmds="Automation SetFilter Stress; RunTests $TestName; Quit" `
    -ReportExportPath="$ReportDir" `
    -unattended -nopause -nosplash -nullrhi -stdout -utf8output 2>&1 |
    Tee-Object -FilePath $EditorLog | Out-Null

$ExitCode = $LASTEXITCODE
$Elapsed = (Get-Date) - $StartedAt

# The harness's own wall clock, measured outside the editor. The test reports its own
# figure for the driving loop alone; this one includes editor startup and shutdown, and
# the two are not the same number. Both are useful and neither substitutes for the other.
Write-Output "PROCESS_EXITCODE=$ExitCode"
Write-Output "HARNESS_WALLCLOCK_SECONDS=$([math]::Round($Elapsed.TotalSeconds, 1))"

Write-Output "EDITOR_STDOUT_LOG=$EditorLog"

$IndexPath = Join-Path $ReportDir 'index.json'
if (-not (Test-Path -LiteralPath $IndexPath)) {
    Write-Output 'NO_INDEX_JSON -- the run produced no report; treat as a harness failure, not a pass.'

    # The tail is the evidence. Without it this branch says only that something went
    # wrong, which is the least useful thing a harness failure can say.
    if (Test-Path -LiteralPath $EditorLog) {
        Write-Output '--- last 40 lines of editor stdout ---'
        Get-Content -LiteralPath $EditorLog -Tail 40 | ForEach-Object { Write-Output "  $_" }
    }
    else {
        Write-Output 'No editor stdout was captured either; the process may not have started.'
    }

    exit 1
}

# CHECKED. Unchecked, this was the widest false-green path left in the script: a truncated
# index.json -- the shape an editor killed during the write leaves behind -- made
# ConvertFrom-Json emit a non-terminating error under $ErrorActionPreference = 'Continue'
# and left $Report null. $Report.failed then compared as 0, $Report.notRun as 0, and the
# final decision below exited 0. A thirty-minute soak whose report could not be read at
# all was reported as a passing gate.
try
{
    $Report = Get-Content -LiteralPath $IndexPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
}
catch
{
    Write-Output "UNREADABLE_INDEX_JSON -- $IndexPath exists but could not be parsed: $($_.Exception.Message)"
    if (Test-Path -LiteralPath $EditorLog) {
        Write-Output '--- last 40 lines of editor stdout ---'
        Get-Content -LiteralPath $EditorLog -Tail 40 | ForEach-Object { Write-Output "  $_" }
    }
    Write-Output 'Treat as a harness failure, not a pass.'
    exit 1
}

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

# Both signals fail the run; the ORDER decides only which one gets named. The test
# result is checked first because it is the more specific diagnosis: an editor that runs
# a failing test and then exits non-zero because of it is an ordinary red gate, and
# announcing EDITOR_EXITED_NONZERO for that sends the reader hunting a crash that never
# happened. A non-zero exit alongside a CLEAN report is the interesting case -- the
# editor died after writing it, an assertion or an ensure on shutdown -- and reporting
# that as a passing thirty-minute soak is the false green this gate exists to prevent.
#
# The condition was `$Report.failed -gt 0 -or $Report.notRun -gt 0`, which can only ever
# FAIL a run and can never pass one. A report containing NO TESTS satisfies it -- failed=0
# and notRun=0 are exactly what an empty report holds -- and every one of these produces
# an empty report: a misspelled -TestName, the Stress filter not being set so RunTests
# finds nothing, a build in which the soak spec was compiled out, an editor that started
# and quit before running anything. All of them exited 0 and were reportable as a green
# thirty-minute soak.
#
# Test-RacingSimReportHasPositiveProof inverts that: the run passes only if the report
# holds tests, at least one passed, none failed, none were skipped, and $TestName is
# present in it with state Success. The last clause is the one that matters most here,
# because this script names a single test and its whole purpose is that that test ran.
$Problems = Test-RacingSimReportHasPositiveProof -Report $Report -RequiredTestNames @($TestName)
if ($Problems.Count -gt 0) {
    Write-Output '--- GATE FAILED ---'
    foreach ($Problem in $Problems) { Write-Output "  $Problem" }

    if ($ExitCode -ne 0) {
        Write-Output "PROCESS_EXITCODE=$ExitCode, consistent with the failures above."
    }
    else {
        # NAMED, because a clean editor exit next to a bad report is the confusing case and
        # was previously left to the reader to notice. The editor did what it was told and
        # the report still does not prove the test ran, so the fault is in what it was
        # told -- the test name, the filter, the build -- not in the run.
        Write-Output 'PROCESS_EXITCODE=0 -- the editor exited cleanly and the report still does not prove the named test passed. Suspect the test name, the automation filter, or a build that does not contain the test, rather than a crash.'
    }

    if (Test-Path -LiteralPath $EditorLog) {
        Write-Output '--- last 40 lines of editor stdout ---'
        Get-Content -LiteralPath $EditorLog -Tail 40 | ForEach-Object { Write-Output "  $_" }
    }

    exit 1
}

# A non-zero exit alongside a report that DOES prove the test passed is the remaining
# interesting case: the editor died after writing it, on an assertion or an ensure during
# shutdown. Reporting that as a passing soak is the other false green this gate exists to
# prevent, so it fails even though every test succeeded.
if ($ExitCode -ne 0) {
    Write-Output "EDITOR_EXITED_NONZERO=$ExitCode -- every test passed but the editor did not exit cleanly; treat as a failure."
    exit 1
}

Write-Output "GATE_PASSED $TestName passedTotal=$Passed"
exit 0
