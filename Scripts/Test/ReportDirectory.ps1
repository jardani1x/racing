# Copyright RacingSim. All Rights Reserved.
#
# Shared report-directory handling for Scripts/Test/Run-Smoke.ps1,
# Scripts/Test/Run-AutomationFilter.ps1 and Scripts/Test/Run-Soak.ps1.
#
# Dot-sourced, not a module, because these are three plain scripts run by hand and by
# CLAUDE.md's gate workflow; a module would need installing or a path, and the whole point
# of this file is that it cannot be forgotten. Every function is prefixed RacingSim so a
# dot-source into a caller's scope cannot collide with anything.
#
# WHY THIS EXISTS. All three scripts opened with the same line:
#
#     if (Test-Path $ReportDir) { Remove-Item -Recurse -Force $ReportDir }
#
# which is a recursive force-delete of any path the caller names, under
# $ErrorActionPreference = 'Continue', with no check that it worked. Three separate
# problems live in that one line:
#
#   IT DELETES ANYTHING.   A mistyped -ReportDir naming a real directory -- a source tree,
#                          a Saved folder, a home directory -- was destroyed without a
#                          word. Nothing about the path was validated first.
#   IT DELETES SILENTLY.   Remove-Item failing is non-terminating here, so the run carried
#                          on into a directory still holding the PREVIOUS index.json, and
#                          the summary read that stale report as this run's result. That
#                          is not hypothetical: it was observed on an 8.3 short path that
#                          Test-Path could resolve and Remove-Item could not.
#   IT DELETES UNDERNEATH  Two runs pointed at one report directory raced, and the second
#   A LIVE RUN.            wiped the first one's output mid-run.
#
# The rule enforced below is narrow on purpose: a directory may be cleared only when it is
# empty, or when it looks like a report this suite produced, and only when no live run
# still owns it.

# Every marker name this suite has ever written. Ownership is recognised from ANY of them,
# so pointing a different script at an existing report directory still works, while the
# name a given script WRITES stays its own business.
$script:RacingSimReportMarkerNames = @('.racingsim-report', '.racingsim-soak-report')

# WHY REFUSAL MESSAGES ARE STORED RATHER THAN PRINTED.
#
# The obvious shape -- Write-Output the reason, then return $false -- does not work in
# PowerShell and fails in the worst possible direction. A function emits EVERYTHING it
# writes to the output stream, so `Write-Output "reason"; return $false` returns a
# two-element array. `if (-not (Test-... ))` then negates a non-empty array, which is
# truthy, so the test is never true and THE GUARD NEVER FIRES. The caller reads as though
# it refuses and does not.
#
# Written down because it was not caught by reading: the first version of this file did
# exactly that, and the unit exercise in Docs/Tickets.md caught it only because it asserted
# on the boolean rather than on the printed text. Anything added here must keep the same
# discipline -- one value out, reasons via Add-RacingSimRefusal.
$script:RacingSimRefusals = @()

function Add-RacingSimRefusal
{
    param([Parameter(Mandatory = $true)][string]$Message)
    $script:RacingSimRefusals += $Message
}

function Get-RacingSimRefusals
{
    # Comma prefix so a single reason survives pipeline unrolling and the caller can always
    # foreach over the result.
    return , $script:RacingSimRefusals
}

function Clear-RacingSimRefusals
{
    $script:RacingSimRefusals = @()
}

function Write-RacingSimRefusals
{
    # The callers all print to stdout and are read from a captured transcript, so the
    # reasons have to land on the same stream as the rest of the harness output.
    foreach ($Message in $script:RacingSimRefusals) { Write-Output $Message }
    Clear-RacingSimRefusals
}

function Test-RacingSimAbsoluteReportDir
{
    param(
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$ReportDir
    )

    # [System.IO.Path]::IsPathRooted IS NOT AN ABSOLUTE-PATH TEST, which is what these
    # scripts used to rely on. "C:report" is rooted and DRIVE-RELATIVE: it resolves
    # against the current directory of drive C:, which is per-process state this script
    # does not set. "\report" is rooted too and resolves against the current drive. Both
    # pass IsPathRooted, and both are then resolved by UnrealEditor-Cmd against the ENGINE
    # directory rather than against anything here -- which is the precise failure the
    # rooted check was added to prevent, still getting through.
    #
    # Matching the two shapes that are genuinely absolute is a smaller and more honest
    # test than trying to normalise: drive-qualified with a separator, or UNC.
    if ($ReportDir -match '^[A-Za-z]:[\\/]' -or $ReportDir -match '^[\\/][\\/]')
    {
        return $true
    }

    Add-RacingSimRefusal "ReportDir must be an ABSOLUTE path -- drive-qualified such as C:\reports\run, or UNC such as \\server\share\run. UnrealEditor-Cmd resolves -ReportExportPath against the engine directory, not the working directory, and a drive-relative path such as C:reports is not absolute however rooted it looks. Got: $ReportDir"
    return $false
}

function Test-RacingSimReportDirIsOurs
{
    param(
        [Parameter(Mandatory = $true)][string]$ReportDir
    )

    foreach ($MarkerName in $script:RacingSimReportMarkerNames)
    {
        if (Test-Path -LiteralPath (Join-Path $ReportDir $MarkerName) -PathType Leaf)
        {
            return $true
        }
    }

    # index.json ALONE is not an ownership test, and treating it as one is how this guard
    # stayed weaker than it read. Any tool can write a file called index.json -- a web
    # build, a package manifest directory, a documentation site -- and the old check would
    # then recursively force-delete that directory on the strength of the name. Require it
    # to parse AND to carry the two fields an Unreal automation report always has, so the
    # test is about the content rather than the filename.
    #
    # The marker above is still the primary signal, because the run that fails hardest
    # writes NO index.json at all and would otherwise lock its own directory out of every
    # future run -- the guard at its most obstructive exactly where it was meant to help.
    $IndexPath = Join-Path $ReportDir 'index.json'
    if (-not (Test-Path -LiteralPath $IndexPath -PathType Leaf))
    {
        return $false
    }

    try
    {
        $Probe = Get-Content -LiteralPath $IndexPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    }
    catch
    {
        return $false
    }

    if ($null -eq $Probe) { return $false }
    return ($null -ne $Probe.reportCreatedOn) -and ($null -ne $Probe.tests)
}

function Get-RacingSimLiveReportOwner
{
    param(
        [Parameter(Mandatory = $true)][string]$ReportDir
    )

    # Returns the process id of a run that still owns this directory, or $null.
    foreach ($MarkerName in $script:RacingSimReportMarkerNames)
    {
        $MarkerPath = Join-Path $ReportDir $MarkerName
        if (-not (Test-Path -LiteralPath $MarkerPath -PathType Leaf)) { continue }

        try { $Lines = @(Get-Content -LiteralPath $MarkerPath -ErrorAction Stop) }
        catch { continue }

        $OwnerProcessId = $null
        $OwnerStartedUtc = $null
        foreach ($Line in $Lines)
        {
            if ($Line -match '^\s*OwnerProcessId=(\d+)\s*$')
            {
                $OwnerProcessId = [int]$Matches[1]
            }
            elseif ($Line -match '^\s*OwnerStartedUtc=(\S+)\s*$')
            {
                try
                {
                    $OwnerStartedUtc = [datetime]::Parse(
                        $Matches[1],
                        [cultureinfo]::InvariantCulture,
                        [System.Globalization.DateTimeStyles]::RoundtripKind)
                }
                catch { $OwnerStartedUtc = $null }
            }
        }

        # A marker with no owner line is an older one, from before this was recorded. It
        # still proves ownership; it just cannot prove liveness, so it does not block.
        if ($null -eq $OwnerProcessId) { continue }
        if ($OwnerProcessId -eq $PID) { continue }

        try { $Owner = Get-Process -Id $OwnerProcessId -ErrorAction Stop }
        catch { continue }

        # PID REUSE is the entire reason the timestamp is written alongside the id. Windows
        # recycles process ids freely, so a marker left by a run that died last week can
        # name an id some unrelated process holds today. Refusing on the id alone would
        # lock the report directory permanently, and the operator's only way out would be
        # deleting it by hand -- the same dead end this guard exists to avoid. A live owner
        # must therefore ALSO have started no later than the marker says it did.
        if ($null -ne $OwnerStartedUtc)
        {
            try
            {
                if ($Owner.StartTime.ToUniversalTime() -gt $OwnerStartedUtc.AddMinutes(1))
                {
                    continue
                }
            }
            catch
            {
                # StartTime is not readable for every process. Unknown is not proof of a
                # live owner, and refusing on an unprovable claim is the obstructive
                # failure again.
                continue
            }
        }

        return $OwnerProcessId
    }

    return $null
}

function Reset-RacingSimReportDirectory
{
    param(
        [Parameter(Mandatory = $true)][string]$ReportDir,
        [Parameter(Mandatory = $true)][string]$MarkerName,
        [Parameter(Mandatory = $true)][string]$OwnerScript
    )

    # Returns $true when the directory is ready to be written into, $false after printing
    # the reason it is not. Callers exit 1 on $false; nothing here exits on the caller's
    # behalf, so the refusal reads at the call site rather than three files away.
    if (Test-Path -LiteralPath $ReportDir)
    {
        if (-not (Test-Path -LiteralPath $ReportDir -PathType Container))
        {
            Add-RacingSimRefusal "ReportDir exists and is not a directory: $ReportDir"
            return $false
        }

        # CHECKED, because $ErrorActionPreference is Continue in every caller. An
        # unreadable directory returning no children silently looks exactly like an empty
        # one, and an empty one is cleared without further questions.
        try
        {
            $Existing = @(Get-ChildItem -LiteralPath $ReportDir -Force -ErrorAction Stop)
        }
        catch
        {
            Add-RacingSimRefusal "FAILED to inspect $ReportDir : $($_.Exception.Message)"
            Add-RacingSimRefusal 'Refusing to continue: a directory that cannot be listed cannot be shown to be safe to delete.'
            return $false
        }

        if ($Existing.Count -gt 0 -and -not (Test-RacingSimReportDirIsOurs -ReportDir $ReportDir))
        {
            Add-RacingSimRefusal "REFUSING to delete $ReportDir : it is not empty, carries none of this suite's markers ($($script:RacingSimReportMarkerNames -join ', ')), and holds no parseable automation index.json, so it is not a report directory this suite produced. Point -ReportDir at a new or previously used report directory."
            return $false
        }

        $LiveOwner = Get-RacingSimLiveReportOwner -ReportDir $ReportDir
        if ($null -ne $LiveOwner)
        {
            Add-RacingSimRefusal "REFUSING to clear $ReportDir : process $LiveOwner is still running and its marker says it owns this directory. Two runs sharing one report directory means the second wipes the first one's output partway through and then reads whatever survives. Wait for it, or use a different -ReportDir."
            return $false
        }

        # CHECKED for the same reason. A failed delete here is silent, and the run would
        # then carry on into a directory still holding the PREVIOUS index.json -- which
        # the summary would read as this run's result. A thirty-minute soak reported green
        # on evidence produced by an entirely different build.
        try
        {
            Remove-Item -Recurse -Force -LiteralPath $ReportDir -ErrorAction Stop
        }
        catch
        {
            Add-RacingSimRefusal "FAILED to clear $ReportDir : $($_.Exception.Message)"
            Add-RacingSimRefusal 'Refusing to continue: a stale index.json left in place would be read as this run.'
            return $false
        }
    }

    try
    {
        New-Item -ItemType Directory -Force -Path $ReportDir -ErrorAction Stop | Out-Null
    }
    catch
    {
        Add-RacingSimRefusal "FAILED to create $ReportDir : $($_.Exception.Message)"
        return $false
    }

    # Written FIRST, before anything that can crash. A marker written at the end would be
    # absent from precisely the runs that need it -- the ones that die partway and leave
    # the directory non-empty.
    try
    {
        Set-Content -LiteralPath (Join-Path $ReportDir $MarkerName) -Encoding utf8 -ErrorAction Stop -Value @(
            "Written by $OwnerScript.",
            "OwnerProcessId=$PID",
            "OwnerStartedUtc=$((Get-Date).ToUniversalTime().ToString('o'))",
            'Its presence marks this directory as a report directory this suite owns and may',
            'clear on the next run. While the process named above is still running, another run',
            'pointed here refuses rather than deleting underneath it. Deleting this file breaks',
            'nothing; it only makes the next run fall back to inspecting index.json.'
        )
    }
    catch
    {
        Add-RacingSimRefusal "FAILED to write the ownership marker in $ReportDir : $($_.Exception.Message)"
        Add-RacingSimRefusal 'Refusing to continue: without the marker the next run cannot tell this directory apart from one it must not delete.'
        return $false
    }

    return $true
}

function Test-RacingSimReportHasPositiveProof
{
    param(
        [Parameter(Mandatory = $true)][AllowNull()]$Report,
        [Parameter(Mandatory = $false)][string[]]$RequiredTestNames = @()
    )

    # POSITIVE PROOF, not the absence of a negative -- the distinction these scripts were
    # missing. Every check they made could only FAIL a run; none could pass one. A report
    # holding zero tests has failed=0 and notRun=0, so a decision made from those two
    # fields alone calls it green: an editor that started, wrote an empty report and quit,
    # a filter that matched nothing, a test name with a typo in it. All green. And these
    # scripts never called exit at all after their NO_INDEX_JSON branch, so even a report
    # full of genuine failures left the process exiting 0.
    #
    # Prints every reason it found rather than the first, because a run that produced no
    # tests usually also produced no passes, and reporting one of those makes the operator
    # re-run to discover the other.
    $Problems = @()

    if ($null -eq $Report)
    {
        return , @('the report could not be read at all')
    }

    $Passed = [int]$Report.succeeded + [int]$Report.succeededWithWarnings
    $Tests = @($Report.tests)

    if ($Tests.Count -lt 1) { $Problems += 'the report contains no tests' }
    if ($Passed -lt 1) { $Problems += "no test passed (succeeded=$($Report.succeeded), succeededWithWarnings=$($Report.succeededWithWarnings))" }
    if ([int]$Report.failed -gt 0) { $Problems += "$($Report.failed) test(s) failed" }
    if ([int]$Report.notRun -gt 0) { $Problems += "$($Report.notRun) test(s) did not run" }

    foreach ($Name in $RequiredTestNames)
    {
        $Matching = @($Tests | Where-Object { $_.fullTestPath -eq $Name })
        if ($Matching.Count -lt 1)
        {
            $Problems += "the report does not contain $Name"
        }
        elseif ($Matching.Count -gt 1)
        {
            $Problems += "the report contains $($Matching.Count) entries for $Name"
        }
        elseif ($Matching[0].state -ne 'Success')
        {
            $Problems += "$Name is $($Matching[0].state), not Success"
        }
    }

    # Comma prefix so a single-element array survives PowerShell's pipeline unrolling. An
    # empty result means proof was found.
    return , $Problems
}
