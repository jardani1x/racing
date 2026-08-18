<#
.SYNOPSIS
    Asserts that test-only code and test-only content are absent from a packaged
    RacingSim Game build.

.DESCRIPTION
    TEST-001, closing CORE-001 finding N-4.

    CORE-001 established the non-shipping guarantee by hand: someone opened
    Binaries/Win64/RacingSim.target, counted occurrences of "RacingSimTests", found
    zero, opened RacingSimEditor.target, found two, and wrote the numbers into
    Docs/Tickets.md. That is a real result and it was correct. It is also a result
    that decays the moment nobody retypes it, which is what CORE-001's own reviewer
    said when approving: "TEST-001 must add the automated check, or this decays into
    a one-time manual result."

    This script is that check. Three modes, run independently or together:

      Receipt  Asserts RacingSimTests is built for the Editor target and is NOT
               linked into the Game executable.

               The Game half reads the LINKER RESPONSE FILE,
               Intermediate/Build/Win64/x64/RacingSim/Development/RacingSim.exe.rsp,
               not the .target receipt. See the long comment on
               Test-TargetReceipts for why the receipt cannot answer this
               question for a monolithic target -- it was the HIGH finding
               (T-1) against the first version of this script.

      Config   Asserts Config/DefaultGame.ini still declares the
               DirectoriesToNeverCook entries that keep test content out of a cook.
               Cheap, needs no build, and catches the most likely regression: a
               later ticket rewriting DefaultGame.ini and dropping the section.

      Pak      Searches the packaged pak/ucas/utoc containers for cooked test-content
               path prefixes. Requires a completed BuildCookRun.

    Every mode carries a positive control -- an assertion that the search finds
    something it should find. Without one, a broken path, an empty file or a
    mistyped needle produces "not found" and reads as a pass. CORE-001 was bitten by
    exactly this: the Gate G "zero matches" claim in Docs/Environment.md was false
    because the search had been narrower than the sentence describing it, and the
    packaged-binary check would have returned all-absent against the 171 KB
    bootstrap launcher while the real 354 MB binary sat elsewhere.

.PARAMETER ProjectRoot
    Repository root. Defaults to two directories above this script.

.PARAMETER Mode
    Receipt, Config, Pak, or All. Defaults to All. Pak is skipped with an explicit
    SKIP result (not a pass) when no packaged build is present.

.PARAMETER PackagedRoot
    Packaged output root. Defaults to <ProjectRoot>/Packaged/Windows.

.EXAMPLE
    pwsh -File Scripts/Test/Check-NonShippingArtifacts.ps1 -Mode Receipt

.NOTES
    Exit code 0 = all executed checks passed. 1 = at least one failed. 2 = the
    script could not run a check it was asked to run (missing input).

    Read the printed FAIL lines, not just the exit code. CLAUDE.md's evidence policy
    requires the result to be inspected.
#>

[CmdletBinding()]
param(
    [string] $ProjectRoot,
    [ValidateSet('Receipt', 'Config', 'Pak', 'All')]
    [string] $Mode = 'All',
    [string] $PackagedRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Resolved in the body rather than as a param default. $PSScriptRoot is not reliably
# populated in a param default block across PowerShell hosts -- under Windows
# PowerShell 5.1 invoked via -File it came back empty, and Split-Path then threw before
# the script ran at all. $MyInvocation.MyCommand.Path is populated in both 5.1 and 7.x.
if (-not $ProjectRoot) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    # Scripts/Test/<this file> -> repository root is two levels up.
    $ProjectRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)
}

if (-not $PackagedRoot) {
    $PackagedRoot = Join-Path $ProjectRoot 'Packaged\Windows'
}

$script:Failures = 0
$script:Passes = 0
$script:Skips = 0

function Write-Result {
    param(
        [ValidateSet('PASS', 'FAIL', 'SKIP')] [string] $Status,
        [string] $Check,
        [string] $Detail
    )
    switch ($Status) {
        'PASS' { $script:Passes++;   $colour = 'Green' }
        'FAIL' { $script:Failures++; $colour = 'Red' }
        'SKIP' { $script:Skips++;    $colour = 'Yellow' }
    }
    Write-Host ("[{0}] {1}" -f $Status, $Check) -ForegroundColor $colour
    if ($Detail) {
        Write-Host ("       {0}" -f $Detail)
    }
}

# -----------------------------------------------------------------------------
# Receipt check
# -----------------------------------------------------------------------------
#
# ============================================================================
# T-1: why the Game half reads the linker response file, not the .target
# ============================================================================
#
# The first version of this check asserted "the string RacingSimTests appears 0
# times in Binaries/Win64/RacingSim.target" and presented that as proof the test
# module was not linked into the Game executable. It is not proof of anything.
#
# A .target receipt lists BUILD PRODUCTS -- the files UBT produced -- not modules.
# For a modular target (the Editor) each module is its own DLL, so module names do
# appear, as UnrealEditor-<Module>.dll. A Development GAME target is MONOLITHIC:
# every module is compiled into the single RacingSim.exe, so NO module appears as a
# build product and NO module name appears in the receipt at all. Measured on this
# repository, in the known-good state:
#
#     Binaries/Win64/RacingSim.target       RacingSimTests  0 occurrences
#                                           InputCore       0
#                                           CoreUObject     0
#                                           SlateCore       0
#
# InputCore, CoreUObject and SlateCore are unquestionably linked into that exe. The
# receipt reads 0 for them anyway. So "RacingSimTests: 0" was measuring
# monolithic-vs-modular linkage, not module membership, and would still read 0 if
# RacingSimTests really were compiled in. The check could not fail on its subject:
# it caught neither of the two regressions CORE-001 named (adding RacingSimTests to
# RacingSim.Target.cs ExtraModuleNames, or setting bBuildRequiresCookedDataOverride
# = false on the Game target).
#
# The linker response file does answer the question. UBT writes every link input to
#     Intermediate/Build/Win64/x64/RacingSim/Development/RacingSim.exe.rsp
# and for a monolithic target that is one .obj per translation unit per module,
# grouped under a per-module directory:
#
#     ".../Intermediate/Build/Win64/x64/UnrealGame/Development/InputCore/Module.InputCore.cpp.obj"
#     ".../Intermediate/Build/Win64/x64/UnrealGame/Development/RacingSim/RacingSimLog.cpp.obj"
#
# Measured on this repository: 1122 .obj inputs, 1122 of which match
# /Development/<Module>/<file>.obj, 0 containing a backslash -- so the directory
# immediately above the object file is the module name, with no ambiguity. That
# yields ~500 module names including RacingSim, Core, Engine, InputCore,
# CoreUObject, SlateCore, EnhancedInput and DeveloperSettings, and NOT
# RacingSimTests. If the test module were linked in, its objects would appear under
# .../Development/RacingSimTests/ and the assertion below fails.
#
# The .lib inputs (111) are third-party static libraries -- BLAKE3.lib,
# OpenEXR-3_4.lib, Secur32.lib -- not UBT modules, so parsing .obj paths is
# exhaustive for module membership. A raw substring search over the whole file is
# asserted as well, so a module arriving in some other form is still caught.
#
# The Editor receipt check below is unchanged and is still the positive control:
# it proves RacingSimTests is a real module that really is built somewhere, so
# "absent from the Game link" means absent rather than nonexistent.
function Test-TargetReceipts {
    $binaries = Join-Path $ProjectRoot 'Binaries\Win64'
    $editorReceipt = Join-Path $binaries 'RacingSimEditor.target'

    # The Game-side source of truth. Path is target/platform/architecture/config
    # specific; this is the one the documented build command produces.
    $gameLinkRsp = Join-Path $ProjectRoot 'Intermediate\Build\Win64\x64\RacingSim\Development\RacingSim.exe.rsp'

    # -------------------------------------------------------------------------
    # Editor receipt -- positive control.
    # -------------------------------------------------------------------------
    if (-not (Test-Path -LiteralPath $editorReceipt)) {
        Write-Result -Status 'FAIL' -Check 'Receipt: Editor receipt present (control)' `
            -Detail ("Missing {0}. Build RacingSimEditor Win64 Development first -- an absent receipt is not a pass." -f $editorReceipt)
        return
    }

    $editorJson = Get-Content -LiteralPath $editorReceipt -Raw | ConvertFrom-Json
    $editorProducts = @()
    if ($editorJson.PSObject.Properties.Name -contains 'BuildProducts') {
        $editorProducts = @($editorJson.BuildProducts | ForEach-Object { $_.Path })
    }

    if ($editorProducts.Count -gt 0) {
        Write-Result -Status 'PASS' -Check 'Receipt: Editor receipt parsed (control)' `
            -Detail ("{0} build products" -f $editorProducts.Count)
    } else {
        Write-Result -Status 'FAIL' -Check 'Receipt: Editor receipt parsed (control)' `
            -Detail 'Zero build products parsed; the control below would be meaningless.'
        return
    }

    $editorHasTestModule = @($editorProducts | Where-Object { $_ -match 'RacingSimTests' }).Count -gt 0
    if ($editorHasTestModule) {
        Write-Result -Status 'PASS' -Check 'Receipt: RacingSimTests IS a build product of RacingSimEditor.target (control)' `
            -Detail 'The test module exists and is really built -- so its absence from the Game link is meaningful.'
    } else {
        Write-Result -Status 'FAIL' -Check 'Receipt: RacingSimTests IS a build product of RacingSimEditor.target (control)' `
            -Detail 'The test module is not built for the editor, so the Game-link assertion proves nothing.'
    }

    # -------------------------------------------------------------------------
    # Game link inputs -- the assertion this ticket is actually about.
    # -------------------------------------------------------------------------
    if (-not (Test-Path -LiteralPath $gameLinkRsp)) {
        Write-Result -Status 'FAIL' -Check 'Link: Game linker response file present' `
            -Detail ("Missing {0}. Build 'RacingSim Win64 Development' first -- an absent response file is not a pass." -f $gameLinkRsp)
        return
    }

    $rspText = Get-Content -LiteralPath $gameLinkRsp -Raw

    # Module name = the directory immediately containing each linked object file.
    $objMatches = [regex]::Matches($rspText, '/Development/(?<mod>[A-Za-z0-9_]+)/[^/"]+\.obj')
    $linkedModules = @($objMatches | ForEach-Object { $_.Groups['mod'].Value } | Sort-Object -Unique)

    # Control D: the response file really was parsed into a module list. Without
    # this, a path change or a schema change yields an empty list and the assertion
    # below passes vacuously -- the exact failure mode T-1 was raised about, and the
    # same one CORE-001 hit with the "Modules" key that does not exist.
    if ($linkedModules.Count -gt 0) {
        Write-Result -Status 'PASS' -Check 'Link: response file parsed into a module list (control)' `
            -Detail ("{0} object inputs -> {1} distinct modules linked into RacingSim.exe" -f $objMatches.Count, $linkedModules.Count)
    } else {
        Write-Result -Status 'FAIL' -Check 'Link: response file parsed into a module list (control)' `
            -Detail ("Parsed 0 modules from {0}. Every assertion below would be vacuous." -f $gameLinkRsp)
        return
    }

    # Control E: known-linked modules are present. This is the control the .target
    # receipt could never provide -- it read 0 for all of these. If this passes and
    # RacingSimTests is absent, the absence is a real measurement.
    $expectedModules = @('RacingSim', 'Core', 'CoreUObject', 'Engine', 'InputCore')
    $missingExpected = @($expectedModules | Where-Object { $linkedModules -notcontains $_ })
    if ($missingExpected.Count -eq 0) {
        Write-Result -Status 'PASS' -Check 'Link: known-linked modules are named in the response file (control)' `
            -Detail ("Found all of: {0}" -f ($expectedModules -join ', '))
    } else {
        Write-Result -Status 'FAIL' -Check 'Link: known-linked modules are named in the response file (control)' `
            -Detail ("Missing {0}. The parser is not reading module names; absences prove nothing." -f ($missingExpected -join ', '))
        return
    }

    # Control F: negative control for the matcher itself.
    if ($linkedModules -contains 'RacingSimNotARealModule') {
        Write-Result -Status 'FAIL' -Check 'Link: matcher is sound (negative control)' `
            -Detail 'A module that does not exist was reported as linked.'
    } else {
        Write-Result -Status 'PASS' -Check 'Link: matcher is sound (negative control)'
    }

    # THE assertion.
    $testModuleLinked = $linkedModules -contains 'RacingSimTests'
    $rawHits = ([regex]::Matches($rspText, 'RacingSimTests')).Count

    if (-not $testModuleLinked -and $rawHits -eq 0) {
        Write-Result -Status 'PASS' -Check 'Link: RacingSimTests is NOT linked into RacingSim.exe' `
            -Detail ("Absent from {0} linked modules, and 0 raw occurrences anywhere in the response file." -f $linkedModules.Count)
    } else {
        Write-Result -Status 'FAIL' -Check 'Link: RacingSimTests is NOT linked into RacingSim.exe' `
            -Detail ("Linked as a module: {0}; raw occurrences in the response file: {1}. The UncookedOnly test module reached the Game executable. Check RacingSim.Target.cs ExtraModuleNames, and any bBuildRequiresCookedDataOverride on the Game target -- ModuleDescriptor.cs:792 keys UncookedOnly exclusion off bBuildRequiresCookedData, not off TargetType." -f $testModuleLinked, $rawHits)
    }

    # Recorded for continuity with CORE-001, and explicitly labelled as NOT evidence
    # so nobody reinstates it as the assertion. See the T-1 comment above.
    $gameReceipt = Join-Path $binaries 'RacingSim.target'
    if (Test-Path -LiteralPath $gameReceipt) {
        $gameReceiptHits = ([regex]::Matches((Get-Content -LiteralPath $gameReceipt -Raw), 'RacingSimTests')).Count
        Write-Host ("       FYI, not an assertion: 'RacingSimTests' occurs {0} time(s) in RacingSim.target. A monolithic Game receipt names no modules at all (InputCore/CoreUObject/SlateCore also read 0), so this number cannot distinguish linked from not-linked. It is printed only because CORE-001 recorded it by hand." -f $gameReceiptHits)
    }
}

# -----------------------------------------------------------------------------
# Config check
# -----------------------------------------------------------------------------
#
# Runs without a build, so it is the one check that can gate a pull request on a
# machine with no engine installed.
function Test-NeverCookConfig {
    $ini = Join-Path $ProjectRoot 'Config\DefaultGame.ini'
    if (-not (Test-Path -LiteralPath $ini)) {
        Write-Result -Status 'FAIL' -Check 'Config: DefaultGame.ini present' -Detail $ini
        return
    }

    $text = Get-Content -LiteralPath $ini -Raw

    # Strip ini comments before matching, so the explanatory comment block above the
    # section cannot satisfy the check on its own. Same reasoning as the comment
    # stripping in RacingSim.Build.cs.
    $stripped = ($text -split "`n" | Where-Object { $_.TrimStart() -notmatch '^[;#]' }) -join "`n"

    if ($stripped -notmatch '\[/Script/UnrealEd\.ProjectPackagingSettings\]') {
        Write-Result -Status 'FAIL' -Check 'Config: packaging settings section present' `
            -Detail 'No [/Script/UnrealEd.ProjectPackagingSettings] section in Config/DefaultGame.ini.'
        return
    }

    foreach ($path in @('/Game/Tests', '/Game/Developer')) {
        $needle = '\+DirectoriesToNeverCook\s*=\s*\(Path\s*=\s*"' + [regex]::Escape($path) + '"\)'
        if ($stripped -match $needle) {
            Write-Result -Status 'PASS' -Check ("Config: {0} is in DirectoriesToNeverCook" -f $path)
        } else {
            Write-Result -Status 'FAIL' -Check ("Config: {0} is in DirectoriesToNeverCook" -f $path) `
                -Detail 'Test-only content would cook into the packaged pak.'
        }
    }

    # Negative control for the matcher: a path that is deliberately not configured
    # must not match. Without this, a regex that matched anything would report both
    # entries present regardless of the file's contents.
    $bogus = '\+DirectoriesToNeverCook\s*=\s*\(Path\s*=\s*"' + [regex]::Escape('/Game/NotConfigured') + '"\)'
    if ($stripped -match $bogus) {
        Write-Result -Status 'FAIL' -Check 'Config: matcher is sound (negative control)' `
            -Detail 'An unconfigured path matched, so the assertions above prove nothing.'
    } else {
        Write-Result -Status 'PASS' -Check 'Config: matcher is sound (negative control)'
    }
}

# -----------------------------------------------------------------------------
# Pak check
# -----------------------------------------------------------------------------
#
# Cooked package paths are stored as text inside the container files, so a byte
# search for "/Game/Tests" is a direct read of what shipped. Searched in both ASCII
# and UTF-16LE for the reason CORE-001 recorded: UE stores some name tables narrow
# and some wide, and searching only one encoding produces a confident false negative.
function Test-PakContents {
    $paks = Join-Path $PackagedRoot 'RacingSim\Content\Paks'
    if (-not (Test-Path -LiteralPath $paks)) {
        Write-Result -Status 'SKIP' -Check 'Pak: test content absent from packaged containers' `
            -Detail ("No packaged build at {0}. Run the BuildCookRun command in Docs/Environment.md, then re-run with -Mode Pak. This is a SKIP, not a PASS." -f $paks)
        return
    }

    $containers = @(Get-ChildItem -LiteralPath $paks -File | Where-Object { $_.Extension -in @('.pak', '.ucas', '.utoc') })
    if ($containers.Count -eq 0) {
        Write-Result -Status 'FAIL' -Check 'Pak: containers present' -Detail $paks
        return
    }

    # Chunked search. The first implementation compared bytes in a nested PowerShell
    # loop; against a 217 MB .ucas and a 354 MB .exe that did not finish in ten
    # minutes, which makes a check nobody will run. Rewritten to stream 8 MB chunks
    # and let .NET's String.IndexOf do the scanning.
    #
    # Latin-1 is the encoding used for the conversion because it is the only one that
    # maps every byte 0x00-0xFF to exactly one char and back. That makes an arbitrary
    # binary buffer searchable as a string without loss.
    #
    # The UTF-16LE case falls out of the same trick: "abc" encoded UTF-16LE is
    # 61 00 62 00 63 00, which read as Latin-1 is "a\0b\0c\0". So the wide needle is
    # just the narrow one with NULs interleaved, and one search routine covers both.
    # Both encodings are searched for the reason CORE-001 recorded: UE stores some
    # name tables narrow and some wide, and searching one encoding only produces a
    # confident false negative.
    #
    # Chunks overlap by (needle length - 1) so a match spanning a boundary is not
    # missed -- the classic off-by-one that makes a chunked scanner silently unsound.
    function Test-BytesContain {
        param([string] $File, [string] $Needle)

        $narrow = $Needle
        $wide = -join ($Needle.ToCharArray() | ForEach-Object { "$_`0" })
        $needles = @($narrow, $wide)

        $maxNeedle = ($needles | Measure-Object -Property Length -Maximum).Maximum
        $overlap = $maxNeedle - 1
        $chunkSize = 8MB

        $stream = [System.IO.File]::OpenRead($File)
        try {
            $buffer = New-Object byte[] ($chunkSize + $overlap)
            $carry = 0
            while ($true) {
                $read = $stream.Read($buffer, $carry, $chunkSize)
                if ($read -le 0) { break }
                $valid = $carry + $read
                # GetEncoding(28591) rather than ::Latin1 -- the named property only
                # exists on .NET 5+, so it would break this script under Windows
                # PowerShell 5.1, which is the shell available by default here.
                $text = [System.Text.Encoding]::GetEncoding(28591).GetString($buffer, 0, $valid)
                foreach ($n in $needles) {
                    if ($text.IndexOf($n, [System.StringComparison]::Ordinal) -ge 0) { return $true }
                }
                # Carry the tail forward so a boundary-spanning match still matches.
                if ($valid -ge $overlap -and $overlap -gt 0) {
                    [System.Array]::Copy($buffer, $valid - $overlap, $buffer, 0, $overlap)
                    $carry = $overlap
                } else {
                    $carry = 0
                }
            }
        } finally {
            $stream.Dispose()
        }
        return $false
    }

    # Positive control. /Script/Engine is present in global.ucas in every cook, so if
    # this comes back absent the byte search is broken and the null results below are
    # worthless. This is the guard CORE-001's Gate G claim lacked.
    $controlFound = $false
    foreach ($container in $containers) {
        if (Test-BytesContain -File $container.FullName -Needle '/Script/Engine') { $controlFound = $true; break }
    }
    if ($controlFound) {
        Write-Result -Status 'PASS' -Check 'Pak: byte search works (control: /Script/Engine found)' `
            -Detail ("{0} container(s) searched under {1}" -f $containers.Count, $paks)
    } else {
        Write-Result -Status 'FAIL' -Check 'Pak: byte search works (control: /Script/Engine found)' `
            -Detail 'Control string not found. Treat every "absent" result below as unproven.'
        return
    }

    foreach ($needle in @('/Game/Tests/', '/Game/Developer/')) {
        $hits = @()
        foreach ($container in $containers) {
            if (Test-BytesContain -File $container.FullName -Needle $needle) { $hits += $container.Name }
        }
        if ($hits.Count -gt 0) {
            Write-Result -Status 'FAIL' -Check ("Pak: '{0}' absent from packaged containers" -f $needle) `
                -Detail ("Found in: {0}. DirectoriesToNeverCook did not take effect." -f ($hits -join ', '))
        } else {
            Write-Result -Status 'PASS' -Check ("Pak: '{0}' absent from packaged containers" -f $needle)
        }
    }

    # Test-module code must not be in the shipped executable either. CORE-001 did this
    # by hand and recorded the trap: two files are named RacingSim.exe and the small
    # one is a bootstrap launcher containing none of the needles, including the
    # controls -- so checking it returns all-absent and looks like a pass. Use the
    # full path, and keep a control.
    $exe = Join-Path $PackagedRoot 'RacingSim\Binaries\Win64\RacingSim.exe'
    if (-not (Test-Path -LiteralPath $exe)) {
        Write-Result -Status 'SKIP' -Check 'Binary: test symbols absent from packaged exe' -Detail $exe
        return
    }

    if (Test-BytesContain -File $exe -Needle 'LogRacingCore') {
        Write-Result -Status 'PASS' -Check 'Binary: control symbol LogRacingCore found in packaged exe'
        foreach ($needle in @('LogRacingTests', 'RacingSim.Core.LogCategories', 'RacingSim.Tests.AutomationTestPlacement')) {
            if (Test-BytesContain -File $exe -Needle $needle) {
                Write-Result -Status 'FAIL' -Check ("Binary: '{0}' absent from packaged exe" -f $needle) -Detail $exe
            } else {
                Write-Result -Status 'PASS' -Check ("Binary: '{0}' absent from packaged exe" -f $needle)
            }
        }
    } else {
        Write-Result -Status 'FAIL' -Check 'Binary: control symbol LogRacingCore found in packaged exe' `
            -Detail ("Control absent from {0}. Wrong file, or the search is broken; absences prove nothing." -f $exe)
    }
}

# -----------------------------------------------------------------------------

Write-Host ''
Write-Host 'RacingSim non-shipping artifact checks (TEST-001, closes CORE-001 N-2/N-4)'
Write-Host ("Project root : {0}" -f $ProjectRoot)
Write-Host ("Mode         : {0}" -f $Mode)
Write-Host ''

if ($Mode -eq 'All' -or $Mode -eq 'Receipt') { Test-TargetReceipts }
if ($Mode -eq 'All' -or $Mode -eq 'Config')  { Test-NeverCookConfig }
if ($Mode -eq 'All' -or $Mode -eq 'Pak')     { Test-PakContents }

Write-Host ''
Write-Host ("Summary: {0} passed, {1} failed, {2} skipped" -f $script:Passes, $script:Failures, $script:Skips)

if ($script:Failures -gt 0) {
    Write-Host 'RESULT: FAILED' -ForegroundColor Red
    exit 1
}
if ($script:Passes -eq 0) {
    Write-Host 'RESULT: INCONCLUSIVE -- nothing was actually checked' -ForegroundColor Yellow
    exit 2
}
Write-Host 'RESULT: PASSED' -ForegroundColor Green
exit 0
