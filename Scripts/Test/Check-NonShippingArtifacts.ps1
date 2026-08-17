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

      Receipt  Reads the UnrealBuildTool .target receipts and asserts RacingSimTests
               is in the Editor target's module list and absent from the Game
               target's. This is UBT stating what it actually compiled, which is
               stronger evidence than searching the binary for a string.

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
# A .target file is JSON that UBT writes after a successful build, listing the
# modules that went into the binary. Parsing it beats grepping the .exe: the binary
# search depends on how the compiler happened to encode a string (CORE-001 had to
# search both ASCII and UTF-16 to get a true answer), whereas the receipt is UBT's
# own statement of what it compiled.
#
# The Editor assertion is the positive control and is not optional decoration. If
# both receipts came back with zero occurrences -- because the JSON shape changed,
# or the module list moved to another key -- the Game assertion alone would pass and
# the check would be silently dead.
function Test-TargetReceipts {
    $binaries = Join-Path $ProjectRoot 'Binaries\Win64'
    $gameReceipt   = Join-Path $binaries 'RacingSim.target'
    $editorReceipt = Join-Path $binaries 'RacingSimEditor.target'

    foreach ($receipt in @($gameReceipt, $editorReceipt)) {
        if (-not (Test-Path -LiteralPath $receipt)) {
            Write-Result -Status 'FAIL' -Check 'Receipt: input present' `
                -Detail ("Missing {0}. Build the target first -- an absent receipt is not a pass." -f $receipt)
            return
        }
    }

    # ---------------------------------------------------------------------
    # Receipt schema, established by reading the files rather than assumed.
    #
    # A UE 5.8.1 .target has no "Modules" key. The first version of this script
    # looked for one, found nothing, and reported an empty module list for both
    # receipts -- at which point the *Game* assertion ("RacingSimTests is not in
    # this list") would have passed on an empty list, which is a false pass.
    #
    # It did not become a false pass, because the Editor positive control failed
    # first and said so. That is the whole argument for keeping controls: the bug
    # was in the checker, the checker was wrong in the safe direction only because
    # something asserted a known-true fact. Do not remove them.
    #
    # The real schema is:
    #   TargetName, Platform, Configuration, BuildSettingsVersion,
    #   TargetBuildEnvironment, TargetType, IsTestTarget, Architecture, Project,
    #   Launch, [LaunchCmd], Version, BuildProducts, RuntimeDependencies,
    #   BuildPlugins, AdditionalProperties
    #
    # Module membership shows up in BuildProducts as per-module DLL/PDB paths for a
    # modular (Editor) target:
    #   { "Path": "$(ProjectDir)/Binaries/Win64/UnrealEditor-RacingSimTests.dll",
    #     "Type": "DynamicLibrary" }
    #
    # A Development *Game* target is monolithic, so its modules do not appear as
    # separate build products at all -- which is exactly why the correct assertion
    # for the Game receipt is "the string RacingSimTests appears nowhere in it",
    # and why that assertion needs an independent control proving the file was read.
    # ---------------------------------------------------------------------

    function Get-ReceiptBuildProductPaths {
        param([string] $Path)
        $json = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
        if ($json.PSObject.Properties.Name -notcontains 'BuildProducts') { return @() }
        return @($json.BuildProducts | ForEach-Object { $_.Path })
    }

    $editorProducts = Get-ReceiptBuildProductPaths -Path $editorReceipt
    $gameProducts   = Get-ReceiptBuildProductPaths -Path $gameReceipt

    $gameText   = Get-Content -LiteralPath $gameReceipt -Raw
    $editorText = Get-Content -LiteralPath $editorReceipt -Raw
    $gameHits   = ([regex]::Matches($gameText,   'RacingSimTests')).Count
    $editorHits = ([regex]::Matches($editorText, 'RacingSimTests')).Count

    # Control A: the receipts parsed and are non-trivial.
    if ($editorProducts.Count -gt 0 -and $gameProducts.Count -gt 0) {
        Write-Result -Status 'PASS' -Check 'Receipt: both receipts parsed (control)' `
            -Detail ("Editor {0} build products, Game {1}" -f $editorProducts.Count, $gameProducts.Count)
    } else {
        Write-Result -Status 'FAIL' -Check 'Receipt: both receipts parsed (control)' `
            -Detail ("Editor {0} build products, Game {1}. A zero here makes every assertion below meaningless." -f $editorProducts.Count, $gameProducts.Count)
        return
    }

    # Control B: the test module really is built for the editor. If this fails, the
    # Game assertion below is not evidence of anything -- it would also pass if the
    # module had simply stopped existing.
    $editorHasTestModule = @($editorProducts | Where-Object { $_ -match 'RacingSimTests' }).Count -gt 0
    if ($editorHasTestModule) {
        Write-Result -Status 'PASS' -Check 'Receipt: RacingSimTests IS a build product of RacingSimEditor.target (control)' `
            -Detail ("{0} raw occurrence(s) of the string in the Editor receipt" -f $editorHits)
    } else {
        Write-Result -Status 'FAIL' -Check 'Receipt: RacingSimTests IS a build product of RacingSimEditor.target (control)' `
            -Detail 'The test module is not built for the editor, so the Game-target assertion proves nothing.'
    }

    # Control C: the Game receipt is the Game target's, and mentions the runtime
    # module. Proves the file was read and that a "RacingSim*" string can be found
    # in it at all -- so a null result for RacingSimTests means absence.
    $gameMentionsRuntime = $gameText -match '"TargetName"\s*:\s*"RacingSim"' -and
                           @($gameProducts | Where-Object { $_ -match 'RacingSim\.exe' }).Count -gt 0
    if ($gameMentionsRuntime) {
        Write-Result -Status 'PASS' -Check 'Receipt: RacingSim.target is the Game target and names RacingSim.exe (control)' `
            -Detail ("TargetType {0}" -f (($gameText | Select-String -Pattern '"TargetType"\s*:\s*"([^"]+)"').Matches[0].Groups[1].Value))
    } else {
        Write-Result -Status 'FAIL' -Check 'Receipt: RacingSim.target is the Game target and names RacingSim.exe (control)' `
            -Detail 'Could not confirm the Game receipt was read; absences below are unproven.'
    }

    # The assertion the ticket is actually about. Both forms, because CORE-001
    # recorded the raw occurrence count by hand and this is the automation of it.
    if ($gameHits -eq 0) {
        Write-Result -Status 'PASS' -Check 'Receipt: RacingSimTests is NOT in RacingSim.target' `
            -Detail ("0 occurrences of 'RacingSimTests' in the Game receipt ({0} build products scanned)" -f $gameProducts.Count)
    } else {
        Write-Result -Status 'FAIL' -Check 'Receipt: RacingSimTests is NOT in RacingSim.target' `
            -Detail ("{0} occurrence(s). The UncookedOnly test module reached the Game target. Check RacingSim.Target.cs ExtraModuleNames, and any bBuildRequiresCookedDataOverride on the Game target -- ModuleDescriptor.cs:792 keys UncookedOnly exclusion off bBuildRequiresCookedData, not off TargetType." -f $gameHits)
    }

    Write-Host ("       CORE-001 recorded these by hand: RacingSim.target {0} occurrence(s), RacingSimEditor.target {1}. Measured now: {0} and {1}." -f $gameHits, $editorHits)
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

    function Test-BytesContain {
        param([string] $File, [string] $Needle)
        $bytes = [System.IO.File]::ReadAllBytes($File)
        $ascii = [System.Text.Encoding]::ASCII.GetBytes($Needle)
        $wide  = [System.Text.Encoding]::Unicode.GetBytes($Needle)
        foreach ($pattern in @($ascii, $wide)) {
            $limit = $bytes.Length - $pattern.Length
            for ($i = 0; $i -le $limit; $i++) {
                $match = $true
                for ($j = 0; $j -lt $pattern.Length; $j++) {
                    if ($bytes[$i + $j] -ne $pattern[$j]) { $match = $false; break }
                }
                if ($match) { return $true }
            }
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
