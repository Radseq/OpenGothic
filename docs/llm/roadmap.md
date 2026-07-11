# Full Client MMO Roadmap Projection

The canonical project roadmap is `docs/llm/gothic-mmo-roadmap.md`. This file
contains only client-facing milestones and cannot reorder global work.

## Established

- MMO mode is opt-in and native single-player remains available.
- client-side communication moved behind the client-sandbox facade.
- in-memory bootstrap ACK/snapshot handoff exists.
- replicated NPC authority guards and transform presentation exist.

## Required before complete MMO UX

1. Protocol V2 domain facade and typed binary bootstrap.
2. Typed entity lifecycle/live replication and resync.
3. Inventory, equipment, combat, dialog, quest and world-event presentation.
4. server character create/select/load and MMO save replacement.
5. complete reconnect/world-transition UX.
6. multi-client headless proof followed by full-client proof.

Network scaling is not a client milestone until the global playable 1:N UDP
gate passes.
