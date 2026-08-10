# Environment record

Phase 0 record. Values marked `VERIFIED` were produced by running the command or
reading the file on this machine. Values marked `UNVERIFIED` have an entry in
*Known blockers and assumptions* and must not be treated as fact.

Recorded: 2026-08-10.

## Project

- Project name/path: `RacingSim` — `C:\Users\jun yi\Documents\central-command\racing` (VERIFIED)
- Repository URL/branch/commit: local-only git, branch `main`, no remote (VERIFIED — see ADR-0004)
- Unreal Engine version and exact patch: **5.8.1**, Changelist **56057345**, CompatibleChangelist 55116800, BranchName `++UE5+Release-5.8` (VERIFIED — `Engine/Build/Build.version`)
- Engine source or launcher build: **Launcher build**, `IsPromotedBuild: 1`. `GenerateProjectFiles.bat` is absent, which is expected; UnrealBuildTool C# source *is* shipped (VERIFIED)
- Project association: `"EngineAssociation": "5.8"` in `RacingSim.uproject` (VERIFIED — editor target builds against `C:\Program Files\Epic Games\UE_5.8`)
- Build ID/versioning method: UNVERIFIED — no build-ID scheme yet; delivered by `CORE-002`

## Development workstation

- OS/version: Windows 11 Pro 10.0.26200, build 26200 (VERIFIED)
- CPU/RAM: Intel Core Ultra 5 125H — 14 physical / 18 logical cores; **15.7 GB RAM** (VERIFIED)
- GPU/VRAM/driver:
  - NVIDIA GeForce RTX 3050 **6 GB** Laptop GPU, driver 32.0.16.1047 (VERIFIED)
  - Intel Arc iGPU, driver 31.0.101.5125 (VERIFIED) — hybrid graphics; Unreal must be pinned to the NVIDIA adapter or Pixel Streaming loses NVENC
- storage/free space: C: 275 GB free · X: 183 GB · Y: 64 GB · G/H/Z: 261 GB (VERIFIED)
- compiler/IDE/toolchain: **MSVC 14.44.35222** from Visual Studio Build Tools 2022 17.14.36811.4, with **Windows SDK 10.0.26100.0** (VERIFIED — reported by UBT during a successful build). Also installed but unused: VS Build Tools 2026 18.7.11925.98 (MSVC 14.51.36231). No Visual Studio IDE. See ADR-0002.
- source-control client/version: git 2.53.0.windows.2, git-lfs 3.7.1 (VERIFIED)
- Git LFS/Perforce status and lock test: LFS initialized; `*.uasset`/`*.umap` marked `lockable`. **Lock test FAILED** — `Locking Test.uasset failed: missing protocol` because lock enforcement requires an HTTP LFS server and this repository is local-only. Locks are **advisory**. Perforce not installed (`p4` absent). (VERIFIED — see ADR-0004)

Other tooling: node **v24.18.0**, npm **11.16.0**, python 3.13.14 (VERIFIED). No .NET SDK on PATH; UE's bundled `DotNet/10.0/win-x64` is used by UBT and is sufficient (VERIFIED).

## Reference GPU worker

**UNVERIFIED — no reference worker exists.** Decision recorded in ADR-0003: this
laptop is the *development machine only* and is explicitly **not** the reference
worker. Gates D, E and F cannot be measured until a worker is named.

- provider/region or host identifier: UNVERIFIED
- OS/image: UNVERIFIED
- CPU/RAM: UNVERIFIED
- GPU/VRAM/driver: UNVERIFIED
- hardware encoder and supported codecs: UNVERIFIED for the worker. On the local
  RTX 3050 (GA107) NVENC supports H.264 and HEVC; **AV1 encode is not available**
  on this generation, so the AV1 arm of the codec comparison in
  `Docs/05-PixelStreaming.md` cannot be run locally.
- Unreal process/session limit: UNVERIFIED
- firewall/public/private ports: UNVERIFIED

## Browser/network support matrix

UNVERIFIED — no stream has been established yet. To be filled by `STREAM-001`.

- browser/version: UNVERIFIED
- OS/version: UNVERIFIED
- controller: UNVERIFIED
- target region: UNVERIFIED
- test RTT/jitter/loss/bandwidth profile: UNVERIFIED
- stream resolution/frame rate/codec/bitrate policy: UNVERIFIED

## Plugins

Enabled in `RacingSim.uproject` and confirmed present in the engine install.
Editor-only plugins carry `"TargetAllowList": ["Editor"]`, which keeps them out
of Game/Client/Server targets per hard constraint #6 and Gate G. The field name
and its `EBuildTargetType` values were verified against
`Engine/Source/Runtime/Projects/Private/PluginReferenceDescriptor.cpp`.

| Plugin | Engine path | Runtime/editor | Why required | In shipping |
|---|---|---|---|---|
| Chaos Vehicles | `Engine/Plugins/Experimental/ChaosVehiclesPlugin` | Runtime | Mandated vehicle simulation (hard constraint #2) | Yes |
| Enhanced Input | `Engine/Plugins/EnhancedInput` | Runtime | Keyboard/gamepad input layer | Yes |
| Pixel Streaming 2 | `Engine/Plugins/Media/PixelStreaming2` | Runtime | Browser delivery (hard constraint #4) | Yes |
| Functional Testing Editor | `Engine/Plugins/Tests/FunctionalTestingEditor` | Editor | Functional/automation test authoring | No |
| Python Editor Script | `Engine/Plugins/Experimental/PythonScriptPlugin` | Editor | Editor/content automation only, never gameplay | No |
| Editor Scripting Utilities | `Engine/Plugins/Editor/EditorScriptingUtilities` | Editor | Batch asset operations | No |
| Model Context Protocol | `Engine/Plugins/Experimental/ModelContextProtocol` | Editor | Experimental editor automation, loopback only | No |
| All Toolsets | `Engine/Plugins/Experimental/Toolsets/AllToolsets` | Editor | MCP toolset registry dependency | No |

Available but **not** enabled: `ChaosModularVehicle`, `Gauntlet`, `PixelStreaming` (v1).
PixelStreaming2 pulls `NVCodecs` and `AMFCodecs` transitively, giving the hardware encode path.

Exact plugin versions: UNVERIFIED — all ship with UE 5.8.1 and carry no independent version; treat the engine changelist as their version.

## Pixel Streaming Infrastructure

- repository source: `https://github.com/EpicGamesExt/PixelStreamingInfrastructure` (VERIFIED)
- exact UE-matching branch/tag/commit: branch **`UE5.8`**, commit **`48bff3b751f91f735b50c90b2a7fec5ceb2a440f`** (2026-08-04), `RELEASE_VERSION` **0.1.0**, `SignallingWebServer` package version 3.0.0 (VERIFIED — cloned and checked out detached at that commit)
- local path: `Web/PixelStreamingInfrastructure` — **gitignored, not vendored**. Re-fetch by cloning the pinned commit.
- Node version from the repository: **v22.14.0**, from a root `NODE_VERSION` file (VERIFIED 2026-08-10).
  **This corrects an earlier entry in this document**, which claimed no version was
  pinned upstream. That claim was drawn from the absence of `.nvmrc` and of an
  `engines` field — both genuinely absent — but the check missed the `NODE_VERSION`
  file, which is what Epic's own tooling reads. Local node is **v24.18.0**, two major
  versions ahead of the pin. See ASSUMPTION-001 for what was then actually tested.
- frontend/signalling modifications: none
- STUN/TURN implementation: UNVERIFIED

**Caution:** the shipped `get_ps_servers.bat` in UE 5.8.1 has **no `5.8` case** in
its version table and silently defaults to branch `UE5.7`. It also writes into
`Program Files`, requiring elevation. It was not used. Fetch by explicit clone of
the pinned commit, or `get_ps_servers.bat /b UE5.8` from an elevated shell.

## Discovered commands

Recorded only after executing and verifying on this environment.

Note: paths contain a space (`Program Files`, `jun yi`). Invoking these through a
POSIX shell with forward slashes silently fails with
`'C:\Program' is not recognized`. Use PowerShell's call operator, as shown.

### Generate project/build files

```text
UNVERIFIED
```
Not required for the current workflow — UBT builds directly from the `.uproject`.
A launcher build has no `GenerateProjectFiles.bat`; the equivalent is
`Build.bat -projectfiles -project=<uproject> -game -engine`, which has not been run.

### Compile editor target

```powershell
# VERIFIED 2026-08-10 - exit 0, no warnings, 179.60s
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' `
    RacingSimEditor Win64 Development `
    -project="C:\Users\jun yi\Documents\central-command\racing\RacingSim.uproject" `
    -waitmutex
```
Produces `UnrealEditor-RacingSim.dll`. UBT log: `C:\Users\jun yi\AppData\Local\UnrealBuildTool\Log.txt`.

**Precondition:** no Unreal Editor or `LiveCodingConsole` process may be running,
or the build aborts with `Unable to build while Live Coding is active` and exit code 6.

### Run automation tests

**VERIFIED 2026-08-10 — 426 tests, 426 passed, 0 failed, 0 not run, 8.70 s.**

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
    "C:\Users\jun yi\Documents\central-command\racing\RacingSim.uproject" `
    -ExecCmds="Automation RunFilter Smoke; Quit" `
    -ReportExportPath="C:\Users\jun yi\Documents\central-command\racing\Saved\Automation\Report" `
    -unattended -nopause -nosplash -nullrhi -stdout -utf8output -log=AutomationSmoke.log
```

Report: `Saved/Automation/Report/{index.json,index.html}`. Counts must be read from
`index.json` (`succeeded` / `failed` / `notRun`), **not** from the process exit code.

Command syntax was read from engine source, not guessed:

- Subcommands, `AutomationCommandline.cpp:570-740`: `List`, `RunTests`/`RunTest`,
  `RunFilter`, `SetFilter`, `RunAll`, `Now`, `SetPriority`, `SetMinimumPriority`,
  `Quit`, `SoftQuit`.
- Filter names, `AutomationCommandline.cpp:95-102`: `Engine`, `Smoke`, `Stress`,
  `Perf`, `Product`, `Standard`, `Negative`, `All`.
- Report flag, `AutomationControllerManager.cpp:213-219`: `-ReportExportPath=`.
  `-ReportOutputPath=` still parses but logs a deprecation warning.
- Text inside `-ExecCmds` is split on `;`, so `"Automation RunFilter Smoke; Quit"`
  is parsed as two Automation subcommands.

`Smoke` was chosen deliberately over `All`: it is the fast subset, and this machine
is memory-constrained (BLOCKER-003). Widening the filter is a later decision, not a
default.

**These are engine tests, not project tests.** The project has no tests yet —
`TEST-001` is unstarted. This run proves the harness, the command form and report
export work on this machine; it says nothing about RacingSim's correctness.

`-nullrhi` skips GPU work. Any future rendering or screenshot test must drop it.

### Cook/package

**VERIFIED 2026-08-10 — `BUILD SUCCESSFUL`, `AutomationTool exiting with
ExitCode=0 (Success)`, 194.83 s.** Requires `-nocleanstage`; see the caveat below.

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat' `
    BuildCookRun `
    -project="C:\Users\jun yi\Documents\central-command\racing\RacingSim.uproject" `
    -platform=Win64 -clientconfig=Development `
    -build -cook -stage -nocleanstage -pak -archive `
    -archivedirectory="C:\Users\jun yi\Documents\central-command\racing\Packaged" `
    -nop4 -utf8output -unattended
```

Artifacts produced and inspected:

- `Packaged/Windows/RacingSim.exe`
- `Packaged/Windows/RacingSim/Content/Paks/RacingSim-Windows.pak` (11.2 MB)
- 1.1 GB staged total

**`-nocleanstage` is a workaround, not a fix.** It skips the stage-directory
cleanup that fails in BLOCKER-006. Without it this command fails at exit 102 —
reproduced twice, including after manually deleting the directory first. The root
cause is still unidentified. Do not present this as a clean Gate A pass.

#### Packaged plugin manifest — Gate G evidence (`ENV-003`)

Searched the packaged tree for editor-only plugin artifacts. **None present:**
`ModelContextProtocol`, `AllToolsets`, `ToolsetRegistry`, `PythonScriptPlugin`,
`EditorScriptingUtilities`, `AndroidFileServer` — zero matches.
`Packaged/Windows/Engine/Plugins` contains only `NNE`.

This proves hard constraint #6 by inspection of the artifact rather than by reading
`.uproject` intent. `TargetAllowList: ["Editor"]` is confirmed effective for build
and stage — independently corroborated in engine source at `PluginManager.cpp:2425`,
`UEBuildTarget.cs:5835`, `CopyBuildToStagingDirectory.Automation.cs:884`.

`PixelStreaming2` **is** staged (`Packaged/Windows/RacingSim/Samples/PixelStreaming2`),
as required. Chaos Vehicles and Enhanced Input produced no separate plugin
directories — expected, since they link into the target binaries — so their presence
is **not** independently confirmed by this check.

**Every path argument must be quoted.** An unquoted `-project=` splits at the
space in `jun yi`; UAT then tries to parse `C:\Users\jun` and reports it as a
bogus JSON syntax error in the `.uproject`.

What worked: the Game target compiled with the verified toolchain, and the cooker
processed **all 586 packages** (`LogCook: Display: Done!`) in 145.92 s,
PeakPhysMemoryMB 1397.

What failed — 2 errors, cook exit 1, UAT exit 25:

```text
LogGameFeatures: Error: Asset manager settings do not include a rule for assets of
  type GameFeatureData, which is required for game feature plugins to function
LoadErrors: Error: Asset Manager settings do not include an entry for assets of type
  GameFeatureData ... Add entry to PrimaryAssetTypesToScan?
```

Diagnosis: `AllToolsets` (enabled for Unreal MCP) pulls in `GameFeaturesToolset`,
which loads the `GameFeatures` plugin. The cooker runs `UnrealEditor-Cmd`, so
editor plugins load during cook despite the `TargetAllowList: ["Editor"]`
restriction — that field governs *packaged targets*, not the cook commandlet.
The project **had no `Config/` directory at all**, so AssetManager had no
`PrimaryAssetTypesToScan` rule for `GameFeatureData`. (Historical diagnosis —
`Config/` now exists; see the RESOLVED block below.)

**RESOLVED 2026-08-10 — Option 1 implemented. The AssetManager error is gone.**

Two options were considered:

1. **Chosen.** Add `Config/DefaultGame.ini` with an AssetManager
   `PrimaryAssetTypesToScan` entry for `GameFeatureData`.
2. Rejected. Drop `AllToolsets` and enable only the toolsets Unreal MCP needs.

**Why Option 1 over Option 2**, reversing the earlier note that favoured Option 2:
`Docs/09-UnrealMCP.md` step 2 explicitly instructs enabling **Unreal MCP and All
Toolsets**. Dropping `AllToolsets` would contradict the project's own documented
MCP setup and require first determining, then pinning, the exact toolset subset MCP
depends on — an experimental, engine-patch-sensitive surface. Option 1 is a
four-line config entry with a documented engine-source justification and no
behavioural cost, since this project has no game feature plugins. Option 2's
supposed advantage — narrowing the editor-only surface for Gate G — is better
served by the ENV-005 packaged-manifest inspection, which proves exclusion directly
rather than by omission.

Result of the re-run: the Game target compiled, **all 586 packages cooked**, and
`UnrealPak executed in 3.917860 seconds ... ExitCode=0`. The GameFeatures error
does not appear.

The run nonetheless **failed at a later, unrelated stage** — see BLOCKER-006.
Gate A therefore remains unclaimed.

### Install and build the Pixel Streaming Infrastructure

**VERIFIED 2026-08-10 — `npm ci` exit 0, 1,373 packages, 4 min; `build:all:cjs` exit 0.**

```powershell
cd "C:\Users\jun yi\Documents\central-command\racing\Web\PixelStreamingInfrastructure"
npm ci --no-audit --no-fund
npm run build:all:cjs     # Common -> Signalling -> SignallingWebServer -> Frontend
```

`npm ci` was chosen over `npm install` so the tree matches `package-lock.json` at the
pinned commit exactly — the same lockfile the licence census in
`Docs/13-AssetLicenseLedger.md` (ASSET-0005) was taken from.

Two warnings that are not failures but are worth knowing: two deprecations
(`whatwg-encoding`, `node-domexception`), and three packages whose install scripts npm
did **not** run — `mediasoup@3.15.5` (postinstall), `pre-commit@1.2.2`,
`spawn-sync@1.0.15`. `mediasoup` needs its postinstall to build the SFU worker binary,
so **the SFU is not usable until those scripts are approved**. The signalling server and
frontend do not need them, which is why the run below still works.

### Start signalling/frontend/TURN

**VERIFIED 2026-08-10 — listeners up in under 5 s; player page HTTP 200.**

```powershell
cd "C:\Users\jun yi\Documents\central-command\racing\Web\PixelStreamingInfrastructure\SignallingWebServer"
node ./dist/index.js --serve --console_messages verbose --player_port 8080 `
    --http_root "C:\Users\jun yi\Documents\central-command\racing\Web\PixelStreamingInfrastructure\SignallingWebServer\www"
```

Observed listeners: streamer `8888`, SFU `8889`, HTTP `8080`.

**`--http_root` is mandatory on this checkout, and the reason is a defect upstream.**
`SignallingWebServer/config.json` at commit `48bff3b7` ships

```json
"http_root": "D:\\PixelStreamingInfrastructure\\SignallingWebServer\\www",
```

— an absolute path leaked from Epic's build machine. It overrides the sane default at
`src/index.ts:120` (`path.resolve(__dirname, '..', 'www')`), so without the flag every
page 404s with `error: Unable to locate file player.html`. **Verified twice**: 404
before the override, HTTP 200 (1,409 bytes) after.

**The space hazard bit for a third time here.** `Start-Process -ArgumentList` joins its
array with spaces and does *not* quote, so `--http_root C:\Users\jun yi\...` arrived as
`C:\Users\jun` and still 404'd. The value must carry embedded quotes. The confirming
evidence is the server's own config echo, which prints the path it actually resolved —
check that line, not the exit code.

TURN: **still UNVERIFIED.** No STUN/TURN server was configured or started; a local
loopback stream does not need one. That belongs to `STREAM-004`, which is blocked on
BLOCKER-001.

### Run packaged Pixel Streaming build

**VERIFIED 2026-08-10 — packaged build connected to the signalling server, joined the
room, and published video and audio tracks.**

```powershell
& "C:\Users\jun yi\Documents\central-command\racing\Packaged\Windows\RacingSim.exe" `
    -PixelStreamingConnectionURL=ws://127.0.0.1:8888 `
    -RenderOffScreen -ForceRes -ResX=1280 -ResY=720 -Unattended -stdout -AudioMixer
```

Evidence from `Packaged/Windows/RacingSim/Saved/Logs/RacingSim.log`:

```text
LogPixelStreaming2EpicRtc: Conference::CreateSession. Id=[DefaultStreamer], Url=[ws://127.0.0.1:8888]
LogEpicRtcWebsocket: Websocket connection made to: ws://127.0.0.1:8888
LogPixelStreaming2EpicRtc: RoomSignallingContextObserver::OnJoined. ... state=[Joined]
LogPixelStreaming2RTC: FEpicRtcStreamer::OnVideoTrackUpdate(Participant [DefaultStreamer], VideoTrack [0], IsRemote[false])
LogPixelStreaming2RTC: FEpicRtcStreamer::OnAudioTrackUpdate(Participant [DefaultStreamer], AudioTrack [1], IsRemote [false])
```

Matching signalling-server side:

```text
info: New streamer connection: ::ffff:127.0.0.1
info: < UnknownStreamer :: {"type":"config","peerConnectionOptions":{},"protocolVersion":"1.3.0"}
info: > UnknownStreamer :: {"id":"DefaultStreamer","protocolVersion":"1.1.0","type":"endpointId"}
info: < DefaultStreamer :: {"type":"endpointIdConfirm","committedId":"DefaultStreamer"}
```

Then 30-second `ping`/`pong` keepalives for the remainder of the run.

**Reconnect works, and was observed rather than assumed.** The signalling server was
killed and restarted while the game kept running; the streamer re-registered
**0.5 s** after the new server came up, logging `Streamer reconnecting... Attempt 2`,
`Attempt 3`, then `Websocket connection made`.

#### What this does and does not prove

It proves the packaged build accepts PS2 arguments, reaches signalling, negotiates an
endpoint id, creates tracks, and survives a signalling restart. **It does not prove a
browser ever received a frame** — no browser client connected, no WebRTC peer
connection was established, no encoder was exercised. That is `STREAM-001`, and it
stays open.

#### Pixel Streaming 2 flag names, derived from source

PS2 does not hard-code a flag list. Every `PixelStreaming2.*` CVar gets a command-line
form by a mechanical transform at
`PixelStreaming2PluginSettings.cpp:93-104`: **delete the dots, then rewrite the
`PixelStreaming2` prefix as `PixelStreaming`.** So `PixelStreaming2.ConnectionURL`
becomes `-PixelStreamingConnectionURL=…`, and `PixelStreaming2.Encoder.Codec` becomes
`-PixelStreamingEncoderCodec=…`. This is why PS1 documentation is misleading here: the
*shape* of the flags survived, the CVars behind them did not.

| Flag | CVar | Notes |
|---|---|---|
| `-PixelStreamingConnectionURL=` | `PixelStreaming2.ConnectionURL` | Default empty. `(protocol)://(host):(port)`. **The one flag that matters.** |
| `-PixelStreamingID=` | `PixelStreaming2.ID` | Streamer id; defaulted to `DefaultStreamer` in this run |
| `-PixelStreamingEncoderCodec=` | `PixelStreaming2.Encoder.Codec` | H.264/HEVC on this GPU; no AV1 encode on GA107 |
| `-PixelStreamingWebRTCFps=` | `PixelStreaming2.WebRTC.Fps` | |
| `-PixelStreamingEncoderTargetBitrate=` | `PixelStreaming2.Encoder.TargetBitrate` | |
| `-PixelStreamingInputController=` | `PixelStreaming2.InputController` | Which peer owns input |

`PixelStreaming2.AutoStartStream` defaults to **true** outside the editor
(`PixelStreaming2PluginSettings.cpp:360-364`), which is why the packaged build began
streaming with no explicit start call.

Deprecated, still parsed, do not use in new work: `PixelStreaming2.SignallingURL`,
`.URL`, `.IP`, `.Port` all now alias `ConnectionURL`
(`PixelStreaming2PluginSettings.cpp:241-244`); supplying `IP`/`Port` logs a conversion
warning. Unrecognised `-PixelStreaming*` arguments are not silently ignored — 
`ValidateCommandLineArgs()` logs `Unknown PixelStreaming command line arg: {0}`, which
is the cheapest way to catch a typo'd flag.

#### Protocol version mismatch — watch this

The signalling server announces `protocolVersion 1.3.0`; the UE 5.8.1 streamer replies
`1.1.0`. The handshake completed anyway, so it is not currently breaking anything, but
it is a version skew between two things this project pins separately. Record it in
`STREAM-002` and re-check after any engine or PSI bump.

#### Security note for Gate G / SEC-001

The signalling server binds **`::` (all interfaces)** on ports 8080, 8888 and 8889 —
not loopback. That is upstream's default and is correct for a real deployment, but on a
development machine it means the streamer and player endpoints are reachable from the
LAN. This is the opposite posture to Unreal MCP (loopback-only) and must be handled
explicitly in the worker image and firewall rules, not assumed. Related: NOTE-001
(`UbaServer` on `0.0.0.0:1345`).

## Unreal MCP

**ENV-005 VERIFIED 2026-08-10.** Every line below was produced by starting the
server and probing it, not by reading documentation.

- endpoint confirmed loopback-only: **VERIFIED** — `Get-NetTCPConnection -LocalPort 8000`
  returned exactly one row: `LocalAddress 127.0.0.1`, `State Listen`,
  `OwningProcess 46376` (the editor). **No `0.0.0.0` and no `::` row exists.**
- LAN reachability: **VERIFIED refused.** A TCP connect to port 8000 was attempted
  against all seven of the machine's non-loopback IPv4 addresses. The three routable
  ones — `192.168.88.7` (LAN), `172.19.176.1` (WSL/Hyper-V), `100.115.135.126` —
  each returned `No connection could be made because the target machine actively
  refused it`. The four `169.254.*` link-local addresses returned
  `A socket operation was attempted to an unreachable network`. Nothing connected.
  Caveat: these probes originated on the host itself. They prove the socket is not
  bound to those interfaces; a genuinely off-host probe is still worth doing once a
  second machine is available.
- generated `.mcp.json` inspected: **VERIFIED** — written to the project root by
  `ModelContextProtocol.GenerateClientConfig ClaudeCode`. Contents:

  ```json
  { "mcpServers": { "unreal-mcp": { "type": "http", "url": "http://127.0.0.1:8000/mcp" } } }
  ```

  Confirmed gitignored: `git check-ignore -v .mcp.json` → `.gitignore:52`.
- read-only tool discovery test: **VERIFIED.** `initialize` returned HTTP 200,
  `protocolVersion 2025-06-18`, and a session id. `tools/list` returned HTTP 200 with
  exactly three tools — `list_toolsets`, `describe_toolset`, `call_tool` — which is
  the `bEnableToolSearch = true` surface, not full native registration. A
  `list_toolsets` call then enumerated the toolsets, including
  `AutomationTestToolset`, `EditorAppToolset`, `PluginToolset`, `ConfigSettingsToolset`
  and **`GameFeaturesToolset`** — independent corroboration of the BLOCKER-005
  diagnosis that `AllToolsets` drags `GameFeatures` in.
- write test and rollback: **NOT RUN, deliberately.** ENV-005 requires a read-only
  call to succeed *before any write is permitted*. No write tool was invoked. A write
  test belongs to the first ticket that actually needs editor automation, with a
  rollback path defined in that ticket.
- shipping exclusion verified: **VERIFIED** — see the packaged plugin manifest check
  under *Cook/package*. Neither `ModelContextProtocol` nor `AllToolsets` appears
  anywhere in the packaged tree.

### How the server is started, and why it is off by default

The server does **not** start on its own. `UModelContextProtocolSettings::bAutoStartServer`
defaults to `false` (`ModelContextProtocolSettings.h:42`), and the setting lives in
`EditorPerProjectUserSettings` — a machine-local file, not project config, so it cannot
be switched on for everyone by a commit.

```powershell
# VERIFIED 2026-08-10 - listener came up 70 s after launch
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
    "C:\Users\jun yi\Documents\central-command\racing\RacingSim.uproject" `
    -ModelContextProtocolStartServer `
    -ExecCmds="ModelContextProtocol.GenerateClientConfig ClaudeCode" `
    -nullrhi -unattended -nopause -nosplash -stdout -utf8output -log=McpLoopback.log
```

The process stays resident; stop it explicitly (`Stop-Process`) when finished. The
listener disappeared immediately on shutdown, which was confirmed.

Flags read from engine source, not guessed:

- `-ModelContextProtocolStartServer` — `ModelContextProtocolSettings.cpp:33`.
  `-StartModelContextProtocolServer` still works but logs a deprecation warning.
- `-ModelContextProtocolPort=<1-65535>` — `ModelContextProtocolSettings.cpp:18`,
  overrides the setting; out-of-range values warn and fall back.
- `ModelContextProtocol.GenerateClientConfig <ClaudeCode|Cursor|VSCode|Gemini|Codex|All>`
  — `ModelContextProtocolEngineModule.cpp:41`, registered `ECVF_Cheat`. In a launcher
  build the file lands in `FPaths::ProjectDir()`; in a source build it would land in
  `FPaths::RootDir()` (`ModelContextProtocolClientConfig.cpp:160-163`).

### Why the binding is loopback, and what would break it

The bind address is **not** an MCP setting. MCP registers a route on the engine's
shared `HTTPServer` module, whose listener config defaults to
`BindAddress = "localhost"` (`HttpServerConfig.h:13`), which `HttpListener.cpp:68-70`
resolves through `SetLoopbackAddress()`. Loopback is therefore a *default*, not a
hard-coded guarantee.

**It is overridable, and that is the risk to watch.** Setting
`[HTTPServer.Listeners] DefaultBindAddress` in `Engine.ini` — or a per-port
`ListenerOverrides` entry containing `BindAddress=` (`HttpServerConfig.cpp:60,97`) —
moves the socket to any interface, with `any` meaning `0.0.0.0`. Neither
`BaseEngine.ini` nor this project's `Config/` contains an `[HTTPServer.Listeners]`
section (VERIFIED by grep), so nothing overrides it today. **Gate G / SEC-001 must
assert that this section stays absent**, because the exposure would be silent: the
MCP plugin logs nothing different when the socket moves.

Second-layer defence, and not a substitute: `ModelContextProtocolServer.cpp:112`
rejects requests whose `Origin` header is not `localhost`, `127.0.0.1` or `[::1]`.
That only blocks browser-driven requests — a non-browser client sends no `Origin` and
is allowed through — so it does not protect a wrongly-bound socket.

### Licensing note

`ModelContextProtocol.uplugin` declares **`"NoRedist": true`** and
`"IsExperimentalVersion": true`. It must never be redistributed, which the packaged
manifest check confirms it is not. Note that its two core modules
(`ModelContextProtocol`, `ModelContextProtocolEngine`) are typed **`Runtime`**, not
`Editor` — so keeping it out of a shipping build rests entirely on the
`TargetAllowList` in `RacingSim.uproject`, not on module types.

## Known blockers and assumptions

### BLOCKER-001 — no reference GPU worker

- blocker: Gates D, E and F specify thresholds on a named reference worker. None exists.
- owner: human project owner
- evidence needed: provider, region, instance type, GPU/VRAM/driver, encoder codec support, sessions-per-GPU, and a measured benchmark lap.
- decision date: open
- note: the local RTX 3050 6 GB / 15.7 GB RAM cannot meet Gate E and is designated development-only (ADR-0003).

### BLOCKER-002 — LFS locking is inert

- blocker: `lockable` attributes cannot be enforced without an LFS server, so hard constraint #7 rests on process discipline alone.
- owner: human project owner
- evidence needed: a decision to either add an LFS-capable remote, adopt Perforce, or formally accept process-only serialization.
- decision date: open

### BLOCKER-003 — memory pressure limits build throughput

- blocker: UBT requested 14 parallel actions and was limited to **1** — only ~321 MB physical RAM was free at build time on a 15.7 GB machine.
- owner: human project owner
- evidence needed: whether 16 GB is accepted for the graybox milestone, or the workstation is upgraded before content work begins.
- decision date: open
- note: this worsens sharply once shader compilation and cooking start.

### ASSUMPTION-001 — node v24.18.0 runs the pinned signalling server — **RESOLVED, with a caveat**

- original wording said "upstream pins no node version". **That was wrong**: the
  repository root carries `NODE_VERSION` = `v22.14.0`. The local runtime is
  **v24.18.0**, two majors ahead.
- **Tested anyway, and it works.** Under v24.18.0: `npm ci` exit 0 (1,373 packages),
  `npm run build:all:cjs` exit 0, signalling server started and listened on 8888/8889/8080,
  a packaged UE streamer connected and joined, and the player page served HTTP 200.
- caveat, and the reason this is not simply closed: running two majors above a pin is
  an accepted risk, not a proven equivalence. Nothing here exercised the SFU
  (`mediasoup`'s postinstall was skipped, so its native worker was never built) and
  nothing exercised a browser WebRTC peer.
- **Recommendation:** pin the toolchain to v22.14.0 for anything deployed. Divergence
  is fine on a development box and should not be fine on a worker image.
- follow-up owner: `pixel-streaming-engineer`, at `STREAM-001`.

### NOTE-002 — upstream `config.json` contains a foreign absolute path

`SignallingWebServer/config.json` at pinned commit `48bff3b7` sets
`"http_root": "D:\\PixelStreamingInfrastructure\\SignallingWebServer\\www"` — a path
from Epic's build machine. It overrides the correct relative default and 404s every
static page until `--http_root` is passed. Any deployment automation must either pass
the flag or patch the config; patching means carrying a local modification of a
gitignored dependency, which is itself a thing to track.

### BLOCKER-004 — project subagents do not dispatch

- blocker: the mandatory ticket protocol in `CLAUDE.md` and `PROMPT_TO_START.md`
  requires implementation, then `code-reviewer`, then `test-engineer`, with the
  rule that no agent approves its own change. **None of the eight agents in
  `.claude/agents/` resolve.** Tested 2026-08-10:

  ```text
  Agent type 'code-reviewer' not found.
  ```

  The registered roster contains only `ecc:*`, `caveman:*` and built-ins.
  `ecc:code-reviewer` is a different agent with different instructions and is not
  a substitute for the project's tailored reviewer.

- Affected: `code-reviewer`, `test-engineer`, `vehicle-physics-engineer`,
  `race-systems-engineer`, `pixel-streaming-engineer`, `rendering-tech-artist`,
  `performance-engineer`, `ip-compliance-auditor`.
- The agent definitions themselves are well-formed: correct `name`/`description`/
  `tools`/`model` frontmatter, read-only tool sets on the two reviewer agents,
  `isolation: worktree` on the code-only implementers. The defect is registration,
  not authoring.
- owner: human project owner
- evidence needed: one successful `code-reviewer` dispatch returning a report.
- likely fix: restart Claude Code with `racing/` as the working directory so the
  project agent directory is enumerated at startup. If that fails, the protocol
  must be formally re-specified against available agents — which is a governance
  change requiring approval, not a silent substitution.
- **Consequence: no implementation ticket may start.** Every ticket from CORE-001
  onward requires independent review and test agents.

### BLOCKER-005 — cook failed on a missing AssetManager rule — **RESOLVED**

- blocker: `BuildCookRun` cooked all 586 packages then failed with
  `LogGameFeatures: Error: Asset manager settings do not include a rule for assets
  of type GameFeatureData`, UAT exit 25.
- cause: `AllToolsets` (required by `Docs/09-UnrealMCP.md`) pulls
  `GameFeaturesToolset` → `GameFeatures`. The cooker runs `UnrealEditor-Cmd`, so
  editor plugins load during cook; `TargetAllowList` governs packaged targets only.
  The project had no `Config/` directory at all.
- resolution: `Config/DefaultGame.ini` AssetManager `PrimaryAssetTypesToScan` entry
  for `GameFeatureData`, `bIsEditorOnly=True`. Verified: cook completes, UnrealPak
  exits 0, error absent.
- follow-up caught in review: the entry was first written with
  `bIsEditorOnly=False`, which would have made packaged Game/Client targets try to
  resolve a class absent from those targets and trip
  `ensureMsgf(AssetBaseClassLoaded, ...)` at `AssetManagerTypes.cpp:82` on startup.
  Corrected before any packaged build existed.

### BLOCKER-006 — staging fails, cook and pak succeed

- blocker: `BuildCookRun` now fails after a successful cook and pak:

  ```text
  Stage Failed. Failed to delete staging directory
    C:\Users\jun yi\Documents\central-command\racing\Saved\StagedBuilds\Windows
  AutomationTool exiting with ExitCode=102 (Error_FailedToDeleteStagingDirectory)
  ```

- **Reproduced twice. Not transient.** The first hypothesis — that the cook's own
  process held the handle and a stale partial tree was to blame — was **wrong**.
  `Saved/StagedBuilds` was verified empty (0 files), deleted cleanly and instantly
  with no lock, and the very next run failed identically at the same step. Whatever
  holds the directory does so reliably during the run.
- workaround: `-nocleanstage` skips the failing cleanup. With it the package
  succeeds (exit 0) and produces a valid archive. Flag confirmed at
  `ProjectParams.cs:2196` ("skip cleaning the stage directory").
- **Root cause remains unidentified.** Untested hypotheses, in order of likelihood:
  Windows Defender real-time scanning holding freshly written files; UE's
  `DirectoryWatcher` (the cook log shows `ReadDirectoryChangesW failed ... GetLastError
  code [6]` on a project path); or a file-sync/indexer agent on `Documents\`.
- owner: technical director
- evidence needed: re-run **without** `-nocleanstage` after (a) excluding the project
  directory from Defender real-time protection, then (b) if that fails, using
  `-stagingdirectory=` to a path outside `Documents\`. Record which one changes the
  outcome. `handle.exe`/Process Explorer would identify the holder directly.
- risk if left: `-nocleanstage` reuses stale staged files, so a deleted or renamed
  asset can persist into a package and mask a packaging regression. Acceptable for a
  project with no content; **not** acceptable once real assets exist.
- **Gate A is NOT claimed.** A packaged build now exists, but only via a workaround
  whose side effect is stale-file retention.

### NOTE-001 — UnrealBuildTool opens a non-loopback listener

- `UbaServer - Listening on 0.0.0.0:1345` during builds (Unreal Build Accelerator).
- Build-time only and never shipped, but it is a non-loopback listener on the development machine and belongs in the Gate G security record.
