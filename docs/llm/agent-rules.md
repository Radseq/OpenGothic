# Full Client Agent Rules

- Preserve native single-player behavior by default.
- In MMO mode, the server owns gameplay truth and the sandbox owns transport.
- Do not add sockets, endpoint parsing, packet codecs, retry/ACK state or
  bootstrap assembly to `src/client`.
- Do not send client-calculated damage, economy, quest, NPC or world mutation
  results.
- Keep presentation bindings generation-safe and fail closed on stale IDs.
- Gate optional tooling explicitly; diagnostics must not become production
  control flow.
- Inspect engine lifetime/thread constraints before retaining references across
  facade drains.
- Validate with the headless sandbox before claiming full-client integration.
