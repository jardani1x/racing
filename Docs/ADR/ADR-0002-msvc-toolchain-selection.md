# ADR-0002: MSVC toolchain selection for UE 5.8.1

- Status: Accepted
- Date: 2026-08-10
- Deciders: technical director
- Related: `Docs/Environment.md`, `CLAUDE.md` ("No warning suppression without a documented reason")

## Context

Two Visual Studio Build Tools installations exist on the development machine, and
neither carries a Visual Studio IDE:

| Install | Version | MSVC directory |
|---|---|---|
| Build Tools 2022 | 17.14.36811.4 | `VC\Tools\MSVC\14.44.35207` |
| Build Tools 2026 | 18.7.11925.98 | `VC\Tools\MSVC\14.51.36231` |

UE 5.8.1 constrains compilers in `Engine/Config/Windows/Windows_SDK.json`:

```json
"PreferredVisualCppVersions": [ "14.50.35717-14.50.99999", "14.44.35207-14.44.99999" ],
"BannedVisualCppVersions":    [ "14.50.0-14.50.35722", "14.44.0-14.44.35210",
                                "14.40.0-14.43.99999", "14.39.0-14.39.99999" ],
"MinimumVisualCppVersion":    "14.38.33130"
```

UE 5.8 supports VS2026 explicitly — `WindowsCompiler.VisualStudio2026` exists and
`WindowsCompiler.VisualStudio` defaults to it — so both installs were candidates.

## The trap

Reading the *directory name* `14.44.35207` against `BannedVisualCppVersions`
suggests the VS2022 toolchain is banned, since `14.44.35207` falls inside
`14.44.0-14.44.35210`. Acting on that reading would have triggered an unnecessary
multi-gigabyte toolchain install requiring administrator rights.

**The directory name is not the compiler version.** UnrealBuildTool reads the real
version from the toolchain itself. During a successful build it reported:

```text
Using Visual Studio 14.44.35222 toolchain
  (C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207)
  and Windows 10.0.26100.0 SDK
```

The actual compiler is **14.44.35222** — above the ban ceiling of `14.44.35210`,
and inside the preferred range `14.44.35207-14.44.99999`.

## Decision

**Use MSVC 14.44.35222 from Visual Studio Build Tools 2022, with Windows SDK
10.0.26100.0.** UnrealBuildTool selects this automatically; no override flag,
no additional install, no ADR waiver.

The VS2026 toolchain (14.51.36231) is left installed but unused. It is above the
minimum and not banned, but sits outside every preferred range, so it would build
with a non-preferred-toolchain warning — which `CLAUDE.md` forbids leaving
undocumented. Preferring 14.44.35222 avoids the warning entirely.

Windows SDK 10.0.26100.0 is within the engine's `MinVersion 10.0.19041.0` …
`MaxVersion 10.9.99999.0` band.

## Consequences

- Editor target builds clean: exit 0, no warnings, 179.60 s (evidence below).
- If Build Tools 2022 is uninstalled or downgraded below 14.44.35211, the build
  breaks and this ADR must be revisited.
- If a future engine patch changes the preferred ranges, re-read
  `Engine/Config/Windows/Windows_SDK.json` rather than assuming this still holds.
- **Verify compiler versions from UBT output, never from directory names.** This
  applies to any future toolchain decision on this project.

## Evidence

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' `
    RacingSimEditor Win64 Development `
    -project="C:\Users\jun yi\Documents\central-command\racing\RacingSim.uproject" `
    -waitmutex
```

Result: `Succeeded`, exit code 0, output `UnrealEditor-RacingSim.dll`.
Grep of the full log for `warning|error|banned|not.*preferred` returned nothing.
UBT log retained at `C:\Users\jun yi\AppData\Local\UnrealBuildTool\Log.txt`.
