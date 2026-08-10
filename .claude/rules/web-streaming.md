---
paths:
  - "Web/**"
  - "Source/**/Streaming/**"
  - "Scripts/Deploy/**"
---

- Use Pixel Streaming 2 and the infrastructure branch matching the pinned Unreal version.
- Treat browser input/custom messages as untrusted and versioned.
- Core race HUD and authority remain in Unreal.
- Never expose Unreal MCP/editor/debug/worker-control ports or commit secrets.
- Test STUN/TURN, reconnect, focus loss, stuck input, gamepad disconnect, version mismatch, token expiry, degraded network, and worker cleanup.
- Record codec, encoder, browser, worker, network, bitrate, WebRTC, and latency evidence.
