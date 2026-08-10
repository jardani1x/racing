---
paths:
  - "Content/**"
  - "Config/**"
  - "Scripts/Editor/**"
---

- Never edit `.uasset` or `.umap` bytes with text/file tools.
- Declare package ownership and serialize editor/MCP changes.
- Every external asset requires an entry in `Docs/13-AssetLicenseLedger.md` before import.
- Do not add real car/track/brand/livery/sponsor content without written `APPROVED` status.
- Use idempotent source-controlled editor scripts/manifests for repeatable changes.
- Validate scale, pivots, transforms, normals/tangents, UVs, collision, physical materials, references, cookability, and render cost.
- Avoid broad Save All and unrelated package churn.
