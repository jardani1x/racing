# Copyright RacingSim. All Rights Reserved.
#
# TRACK-002 build helper.
#
# Two reasons this exists rather than a one-line Build.bat invocation:
#
#   1. Every path on this machine contains a space (`Program Files`, `jun yi`).
#      Docs/Environment.md records that invoking Build.bat through a POSIX shell with
#      forward slashes fails with `'C:\Program' is not recognized`. PowerShell's call
#      operator with a quoted path is the form that works.
#   2. The build result must be read from THIS command's own captured output.
#      %LOCALAPPDATA%\UnrealBuildTool\Log.txt is shared machine-wide across every
#      project and worktree on this host and is overwritten by other agents' concurrent
#      builds, so it cannot be cited as evidence for a particular build.

param(
    [Parameter(Mandatory = $true)][string]$Target,
    [Parameter(Mandatory = $true)][string]$OutFile,
    [Parameter(Mandatory = $true)][string]$ProjectPath
)

$ErrorActionPreference = 'Continue'

$BuildBat = 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat'

$OutDir = Split-Path -Parent $OutFile
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

& $BuildBat $Target Win64 Development -project="$ProjectPath" -waitmutex *>&1 |
    Tee-Object -FilePath $OutFile | Out-Null

$Code = $LASTEXITCODE

$Text = Get-Content -LiteralPath $OutFile
$WarnErr = @($Text | Select-String -Pattern 'warning|error' -CaseSensitive:$false)

Write-Output "BUILD_EXITCODE=$Code"
Write-Output "RESULT_LINE=$(@($Text | Select-String -Pattern '^Result:') -join '; ')"
Write-Output "WARNING_ERROR_MATCHES=$($WarnErr.Count)"
foreach ($Line in $WarnErr) { Write-Output "  MATCH: $Line" }
