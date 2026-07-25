# Full Client Architecture Decisions

Read only the ADR governing the boundary being changed. ADRs contain durable
decisions and trade-offs; current activation belongs in
`../llm/current-state.md`, and work order belongs in `../llm/next-work.md`.

- [0001 — Client is an input and presentation adapter](0001-client-is-input-and-presentation-adapter.md) — authority and presentation ownership.
- [0002 — Preserve native mode and fail closed](0002-preserve-native-mode-and-fail-closed.md) — explicit native/server-bound mode separation.
- [0003 — Typed facade mailbox is the engine boundary](0003-typed-facade-mailbox-is-the-engine-boundary.md) — the single production communication seam.
- [0004 — Presentation is route-scoped and generation-safe](0004-route-scoped-generation-safe-presentation.md) — exact identity, replacement and bootstrap rules.
- [0005 — Server-revisioned UI has no optimistic authority](0005-server-revisioned-ui-has-no-optimistic-authority.md) — read models, pending UX and revision semantics.
- [0006 — Diagnostics and local storage are tools](0006-diagnostics-and-local-storage-are-tools.md) — optional diagnostics cannot activate gameplay.

One ADR owns one decision. Supersede an accepted ADR explicitly instead of
silently changing its meaning. Do not append implementation progress, test logs
or current-state claims to ADRs.
