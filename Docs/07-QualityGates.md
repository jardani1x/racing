# Quality gates

These are project acceptance targets, not claims about Unreal or a cloud provider. The reference hardware, region, browser, network profile, build configuration, and exact measurement method must be recorded before a result is accepted.

## Gate A: repository and build health

- Clean checkout resolves all required dependencies.
- C++ build succeeds with no newly introduced warnings.
- required plugins load; disallowed editor-only plugins are excluded from shipping.
- all target maps and assets cook/package successfully.
- no missing redirectors, broken references, load errors, or unlicensed assets.
- build is reproducible from documented commands.

## Gate B: gameplay correctness

- 100 automated valid-lap runs count exactly once.
- 100 skipped/out-of-order/reverse/double-cross scenarios never produce a valid lap.
- countdown, start, finish, results, and restart transitions are deterministic and idempotent.
- timer is monotonic and independent of render frame rate.
- reset cannot award progress or create an immediate duplicate checkpoint.
- HUD data equals authoritative race state.
- results include build, track, car/tune, assist, and validity metadata.

## Gate C: vehicle stability

- no NaN, infinity, explosive energy, persistent penetration, or unbounded wheel state in automated manoeuvres.
- stable launch, braking, steering, curb impact, spin, reverse, off-track, and reset.
- 30-minute recorded-input soak completes without crash or unrecovered simulation failure.
- behavior remains within approved telemetry envelopes across supported render rates.
- an experienced human reviewer signs off that the prototype is credible enough for the milestone.

## Gate D: visual quality

- fixed benchmark cameras have approved baseline images.
- screenshot comparison detects unintended regressions within an agreed tolerance.
- no visible LOD/HLOD pop, missing mesh, light leak, shadow failure, reflection discontinuity, texture-streaming failure, or collision/visual mismatch in the hero route.
- automotive paint, carbon, glass, rubber, metal, asphalt, kerbs, vegetation, and barriers pass the art checklist.
- camera, motion blur, suspension, wheel motion, tire contact, and audio agree with vehicle motion.
- art director or designated human owner approves the hero car and circuit sector.

## Gate E: runtime performance

Initial target on the named reference GPU worker, packaged build, 1920x1080 stream:

- sustained 60 fps target after a 60-second warm-up;
- Unreal frame p95 at or below 16.67 ms and p99 at or below 22 ms on the benchmark lap;
- no post-warm-up hitch above 100 ms without an accepted cause;
- no monotonic memory growth greater than 5% across a 30-minute repeatable soak;
- physics step misses and game-thread stalls are surfaced as test failures;
- encoder time, bitrate, dropped frames, RTT, jitter, and packet loss are recorded, not guessed.

If the spike proves a threshold unrealistic on the chosen worker, change the hardware/quality tier or document a human-approved threshold change; do not silently lower the bar.

## Gate F: browser and streaming quality

Initial regional target:

- input-to-photon latency p50 at or below 80 ms and p95 at or below 120 ms on the agreed nearby-network profile;
- no unrecovered media/input stall in a 30-minute session;
- TURN-only connection works;
- keyboard/gamepad, focus loss, reconnect, fullscreen, audio, and termination work on the supported matrix;
- 100 start/stop cycles leave no orphan worker and no cross-session state;
- degraded network produces controlled quality reduction and visible diagnostics rather than a silent failure.

## Gate G: security and operations

- Unreal MCP is disabled/not reachable in packaged production workers.
- only approved public endpoints and ports are exposed.
- session tokens and TURN credentials are short-lived; secrets are not in source control or logs.
- workers are isolated and scrubbed or destroyed between users.
- rate limits, idle timeouts, cost caps, logs, metrics, alerts, crash capture, rollout, and rollback are tested.
- dependency and container/host vulnerability checks meet the release policy.

## Gate H: legal and provenance

- every external asset, scan, texture, font, audio file, model, plugin, and code dependency has a ledger entry.
- no real brand, model, logo, distinctive vehicle shape, livery, sponsor, track name/layout/signage, or proprietary recording is present without written approval.
- all required credit, notice, territory, platform, term, marketing, and approval obligations are implemented.
- legal owner signs off before any branded build is shared publicly.

## Production-grade declaration

The words `production-ready` or `production-grade` may appear in a release report only when Gates A-H pass, evidence is linked, all critical/high findings are closed, waivers are explicit, and the required human owners sign the report.
