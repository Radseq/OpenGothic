# Full OpenGothic Client Context

Last verified: 2026-07-11.

The full client preserves native single-player behavior. In MMO mode it is an
input and presentation adapter around the public client-sandbox facade.

## Read order

1. `current-state.md`.
2. `next-work.md`.
3. `agent-rules.md`.
4. `repo-map.md`.
5. `architecture.md`, `api-contracts.md` and `testing.md` as needed.
6. Actual engine source before editing.

Global ordering is in `docs/llm/gothic-mmo-roadmap.md`. The client-local
`roadmap.md` is only a compact projection and may not reorder global phases.

## Non-negotiable boundary

The full client must not own MMO endpoint parsing, UDP sockets, packet codecs,
retry/ACK state or bootstrap assembly. Those belong to `src/client_sandbox`.
The client may predict/interpolate presentation but cannot commit gameplay
facts.
