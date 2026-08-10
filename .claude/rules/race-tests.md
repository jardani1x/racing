---
paths:
  - "Source/**/Race/**"
  - "Source/**/Tests/**"
  - "Content/Tests/**"
---

- Checkpoint order plus crossing direction authorizes laps; spline distance alone never does.
- Race timing uses a monotonic authoritative clock.
- Test reverse crossings, skipped gates, double overlaps, spins at gates, high-speed crossings, reset/teleport, and restart.
- Tests must leave disk/world state clean and must not depend on execution order.
- Do not weaken a failing assertion or screenshot tolerance without an approved ticket change.
