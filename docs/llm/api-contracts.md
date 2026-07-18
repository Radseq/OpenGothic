# Full Client Contract Summary

Source declarations are canonical. This file records only cross-boundary
semantics that are easy to misuse.

- The full client consumes the public `client_runtime_facade` API; private
  sandbox modules and protocol packets must not escape into engine/UI code.
- Engine submissions use domain DTOs. A disabled MMO bridge may report a
  harmless native-mode no-op; code that needs proof of network submission must
  check the explicit submitted/token state, not only generic acceptance.
- Movement is normalized input, not a claimed transform. Interaction and item
  commands require exact server identity; missing generation or revision is
  invalid.
- Presentation delivery is one optional route replacement, bootstraps, then
  live events. The consumer must preserve that order.
- Bootstrap installation is atomic. Invalid records leave the previous valid
  projection intact or force an explicit reset; partial activation is invalid.
- Inventory/equipment UI changes become visible from authoritative snapshots or
  deltas. Completion receipts describe command outcome and do not authorize
  speculative quantity mutation.
- Protocol evolution starts in shared/server/sandbox. The full client receives
  a protocol-independent projection and must tolerate unknown valid families
  until a presentation component is added.

See ADRs 0003–0005 for the binding decisions.
