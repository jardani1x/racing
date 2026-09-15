# Copyright RacingSim. All Rights Reserved.
#
# Runs one automation filter and reports the counts from Saved/Automation/Report/index.json.
#
# This is Run-Smoke.ps1 generalised over the filter name. Run-Smoke.ps1 is deliberately
# left alone: it is cited by name in the verification-evidence sections of CORE-002,
# TRACK-001 and TEST-001, and a script whose behaviour changes under a name that past
# evidence refers to is a script that makes old evidence unverifiable.
#
# WHY A SECOND FILTER EXISTS AT ALL (TRACK-002):
#
#   FEngineLoop::PreInit runs every SmokeFilter test itself
#   (Runtime/Launch/Private/LaunchEngineLoop.cpp:4376), BEFORE UEngine::Init calls
#   RegisterEngineElements() (Runtime/Engine/Private/UnrealEngine.cpp:2399). So a
#   SmokeFilter test executes in a window where constructing any non-template
#   UActorComponent hits
#
#       Assertion failed: RegisteredElementType [TypedElementRegistry.h:536]
#       Element type 'Components' has not been registered!
#
#   and kills the whole run with no report. That is the single cause behind all three
#   actor-instantiation crashes TRACK-001 recorded.
#
#   ProductFilter tests are not run by RunSmokeTests(); they run only from the deferred
#   `Automation RunFilter Product` console command, which is processed on the first
#   engine tick -- after full initialisation. Any test that touches an actor must
#   therefore be ProductFilter and must be gated by THIS script.
#
# Docs/Environment.md: counts must be read from index.json (succeeded/failed/notRun),
# NEVER from the process exit code, and -log=<name>.log does not work on this engine
# build. The report is the only authoritative artifact this command produces.

# WHY -TestNames EXISTS, AND WHY IT IS NOT A SHORTCUT (TRACK-002):
#
#   `Automation RunFilter Product` CANNOT COMPLETE ON THIS MACHINE. It reaches
#   System.Plugins.PixelStreaming2.FPS2DataChannelEchoTest, which under -nullrhi logs
#   "No streamer factory implementation for DefaultRtc found" and then dies on
#   `Assertion failed: IsValid() [Templates/SharedPointer.h:1133]`, killing the run and
#   producing NO index.json. Verified 2026-08-19: the run got through 1300+ tests,
#   including all three RacingSim level tests, and then crashed in the Pixel Streaming
#   suite. That is an engine/plugin defect in a headless configuration, not a project
#   one, and this project cannot fix it.
#
#   So the repeatable gate for the actor-touching tests names them. Docs/Environment.md
#   warns that `RunTests <name>` bypasses filters and must never be used to PROVE a test
#   is discoverable -- that rule is respected: discoverability was proven separately with
#   a real `RunFilter Product` run whose log shows the automation controller collecting
#   and dispatching all three (`Sending RunTest TrackPrototypeLevel...`). -TestNames is
#   used for the repeatable pass/fail evidence, not for the collection claim.
#
#   If the Pixel Streaming tests are ever fixed or excluded, switch this gate back to
#   -Filter Product, which is strictly better.

param(
    [Parameter(Mandatory = $true)][string]$ProjectPath,
    [Parameter(Mandatory = $true)][string]$ReportDir,
    [ValidateSet('Engine', 'Smoke', 'Stress', 'Perf', 'Product', 'Standard', 'Negative', 'All')]
    [string]$Filter,
    [string]$TestNames
)

$ErrorActionPreference = 'Continue'

if (-not $Filter -and -not $TestNames) {
    Write-Output 'Specify exactly one of -Filter or -TestNames.'
    exit 1
}
if ($Filter -and $TestNames) {
    Write-Output 'Specify exactly one of -Filter or -TestNames, not both.'
    exit 1
}

$Cmd = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

. (Join-Path $PSScriptRoot 'ReportDirectory.ps1')

# VEH-006 repair cycle 3. See ReportDirectory.ps1: this was an unchecked recursive
# force-delete of any path the caller named.
if (-not (Test-RacingSimAbsoluteReportDir -ReportDir $ReportDir)) { Write-RacingSimRefusals; exit 1 }
if (-not (Reset-RacingSimReportDirectory -ReportDir $ReportDir -MarkerName '.racingsim-report' -OwnerScript 'Run-AutomationFilter.ps1')) { Write-RacingSimRefusals; exit 1 }

if ($Filter) {
    Write-Output "FILTER=$Filter"
    $AutomationCmd = "Automation RunFilter $Filter; Quit"
} else {
    Write-Output "TESTNAMES=$TestNames"
    $AutomationCmd = "Automation RunTests $TestNames; Quit"
}

& $Cmd $ProjectPath `
    -ExecCmds="$AutomationCmd" `
    -ReportExportPath="$ReportDir" `
    -unattended -nopause -nosplash -nullrhi -stdout -utf8output | Out-Null

Write-Output "PROCESS_EXITCODE=$LASTEXITCODE"

$IndexPath = Join-Path $ReportDir 'index.json'
if (-not (Test-Path $IndexPath)) {
    Write-Output 'NO_INDEX_JSON -- the run produced no report; treat as a harness failure, not a pass.'
    exit 1
}

# CHECKED. See Run-Smoke.ps1: unchecked, a truncated index.json left $Report null and the
# whole report read as zeroes, which is indistinguishable from a clean run.
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

# See Run-Smoke.ps1: `succeeded` is NOT the pass count. A test that passes every assertion
# but emits a UE_LOG(Warning) is counted in `succeededWithWarnings` instead, so reporting
# `succeeded` alone shows a drop where nothing regressed. Both buckets, their sum and the
# report's own test count are printed. The pass/fail decision is `failed` and `notRun`.
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

# THE SCRIPT USED TO END HERE, exiting 0 whatever the report said -- the M7 residual in
# Docs/Tickets.md. Nothing above this line changed, so past evidence citations stay
# verifiable; what is added is the exit status they always implied.
#
# -TestNames gets a STRONGER check than -Filter can have. A filter's expected test set is
# not knowable from here, so all that can be demanded of it is that something ran and
# passed. A name list IS the expected set: `Automation RunTests A+B+C` silently drops a
# name it cannot resolve -- a typo, a renamed spec, a test excluded by the build -- and
# the report then comes back green with fewer tests in it than were asked for. Requiring
# each name to be present AND Success is the difference between "the tests I named passed"
# and "some tests passed".
$RequiredTestNames = @()
if ($TestNames)
{
    # Unreal accepts + and , as separators in RunTests. Split on both rather than assuming.
    $RequiredTestNames = @($TestNames -split '[+,]' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}

$Problems = Test-RacingSimReportHasPositiveProof -Report $Report -RequiredTestNames $RequiredTestNames
if ($Problems.Count -gt 0)
{
    Write-Output '--- GATE FAILED ---'
    foreach ($Problem in $Problems) { Write-Output "  $Problem" }
    exit 1
}

Write-Output "GATE_PASSED passedTotal=$Passed requiredNamesChecked=$($RequiredTestNames.Count)"
exit 0
