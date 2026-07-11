# Durable Client Decisions

- The server is MMO gameplay authority.
- `src/client_sandbox` owns client-side MMO communication.
- Native single-player behavior remains available and unchanged by default.
- Full-client MMO code is a thin input/presentation adapter.
- Stable server IDs and generations outrank names, array order and local object
  addresses.
- JSON/SQLite local tooling is diagnostic/offline only, never gameplay control.
- Typed binary bootstrap and live events replace historical in-memory JSON
  bodies; no filesystem side channel returns.
- Full MMO UX is integrated only after the equivalent headless scenario works.
