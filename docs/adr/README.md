# Full Client Architecture Decisions

Read only the ADR governing the boundary being changed.

- [0001 — Client is an input and presentation adapter](0001-client-is-input-and-presentation-adapter.md)
- [0002 — Preserve native mode and fail closed in server-bound mode](0002-preserve-native-mode-and-fail-closed.md)
- [0003 — Typed facade mailbox is the engine boundary](0003-typed-facade-mailbox-is-the-engine-boundary.md)
- [0004 — Presentation is route-scoped and generation-safe](0004-route-scoped-generation-safe-presentation.md)
- [0005 — Server-revisioned UI has no optimistic authority](0005-server-revisioned-ui-has-no-optimistic-authority.md)
- [0006 — Diagnostics and local storage are tools](0006-diagnostics-and-local-storage-are-tools.md)

ADRs capture durable decisions, not current completion. Implementation status
belongs in `../llm/current-state.md`; work order belongs in
`../llm/next-work.md`. Supersede an ADR explicitly instead of silently changing
its meaning.
