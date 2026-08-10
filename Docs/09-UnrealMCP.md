# Unreal MCP and editor automation

## Purpose

Unreal Engine 5.8 includes an experimental Unreal MCP plugin that allows an MCP-compatible agent such as Claude Code to drive supported Unreal Editor functions through a local HTTP connection. It can expose tools for actor creation, lighting, material instances, Slate inspection, automation tests, and other registered toolsets.

It is incomplete, subject to change, unauthenticated, and not safe for remote exposure. Use it to accelerate editor work, not as a runtime or shipping dependency.

## Setup

1. Open **Edit > Plugins**.
2. Enable **Unreal MCP** and **All Toolsets**. Their registry dependency is enabled automatically.
3. Restart the editor.
4. In **Editor Preferences > General > Model Context Protocol**, enable auto-start if desired.
5. The default local endpoint is `http://127.0.0.1:8000/mcp`.
6. Open the Unreal console and run:

```text
ModelContextProtocol.GenerateClientConfig ClaudeCode
```

7. Confirm that Unreal generated/merged `.mcp.json` in the project root.
8. Start Claude Code from that project root.
9. Verify tool discovery with a read-only/editor-inspection action before allowing writes.

Do not manually expose or proxy port 8000. Keep the listener loopback-only.

## Operating policy

- The parent Claude session owns MCP execution.
- Execute one editor-changing action at a time; do not issue overlapping tool calls.
- Save assets explicitly and capture a before/after change manifest.
- Use source-controlled Python/Editor Utility/C++ tooling for repeatable bulk operations, with MCP invoking those tools where appropriate.
- Prefer idempotent operations that can be re-run safely.
- Validate generated assets for path, class, naming, references, collision, materials, and cookability.
- Take benchmark screenshots and run relevant automation immediately after editor changes.
- Disable or remove the MCP/editor-only setup from packaged production configurations.

## Python editor scripting

Python is useful for asset import, validation, material-instance generation, level layout, metadata, and batch operations. It is editor-only and must not be used as gameplay scripting. Store scripts under a source-controlled project path and make them report precise failures instead of silently continuing.

## Fallback

Because MCP is experimental, every critical content workflow must have a documented manual or commandlet/script path. If an engine patch changes the MCP API, the project should still build, test, cook, and ship without it.
