# Browser and controller QA matrix

## Define the support promise before testing

For the first slice, prefer a narrow desktop target such as current stable Chrome and Edge on Windows with keyboard and standard gamepad. Add other browsers/OS/mobile only after passing the primary matrix. Record exact versions because browser WebRTC and controller behavior changes over time.

## Matrix fields

- browser and version;
- OS and version;
- display resolution/refresh rate;
- keyboard layout;
- controller make/model, connection type, browser mapping;
- network route/region, RTT, jitter, packet loss, available bandwidth;
- codec, resolution, frame rate, bitrate policy;
- GPU worker type, driver, encoder session count;
- result/build ID.

## Test cases

### Session

- sign in, allocate, queue, cancel, connect, disconnect, reconnect;
- stale/expired token;
- worker crash and controlled recovery;
- tab reload/back/close and idle timeout;
- two tabs attempting the same session;
- game build/frontend version mismatch.

### Input

- key down/up, simultaneous throttle/steer/brake, focus loss with held key;
- gamepad connect before/after stream, disconnect/reconnect, analog range/dead zone;
- browser shortcuts and pointer/fullscreen transitions;
- input after reconnect;
- no stuck throttle/steering when packets or focus are lost.

### Media

- video start, audio start/mute, resolution change, fullscreen;
- low bandwidth, burst loss, sustained loss, jitter, high RTT;
- codec fallback;
- background tab and display sleep;
- A/V and HUD synchronization.

### Race integrity

- network degradation cannot alter authoritative timing or checkpoint validation;
- reconnect policy does not create an invalid leaderboard result;
- session termination persists or discards results according to explicit rules.

## Evidence

Store WebRTC stats, Unreal telemetry, browser console/network logs, worker logs, build ID, and a timestamped result. A subjective "felt okay" is not a latency test.
