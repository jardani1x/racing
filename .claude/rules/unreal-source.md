---
paths:
  - "Source/**"
  - "Plugins/**/Source/**"
  - "**/*.Build.cs"
  - "**/*.Target.cs"
---

- Follow the architecture boundaries in `Docs/01-Architecture.md`.
- Runtime truth, validation, timing, and simulation are C++ first.
- Make Unreal-centimeter/SI conversions explicit and tested.
- Do not allocate, search the entire world, synchronously load assets, or log noisily every frame.
- Treat object lifetime, delegates, timers, async callbacks, and game-thread access explicitly.
- Put tuning in typed DataAssets/config with validation.
- Add tests with behavior changes and report commands actually run.
- No engine-source modification without an approved ADR and rollback plan.
