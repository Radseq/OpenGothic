# Full Client MMO Roadmap Projection

Last updated: 2026-07-12. The canonical order is
`docs/llm/gothic-mmo-roadmap.md`. This projection cannot create client
ownership or reorder global phases.

## Established

- MMO mode is opt-in and native single-player remains available.
- Client communication is behind the `client_sandbox` facade.
- In-memory bootstrap ACK/snapshot handoff exists.
- Protocol V2 now has a route-bound binary bootstrap manifest, fixed 1200-byte
  chunk frames, per-section schema/hash descriptors and typed initial sections
  for world clock, controlled character and entity roster. The assembler lives
  in `client_sandbox`, accepts reorder/duplicate delivery, rejects stale routes
  and emits a bounded typed mailbox plus completion ACK.
- `client_sandbox` has Protocol V2 capability negotiation, route/header/receipt
  state and typed builders/codecs for all eight headed session commands and all
  twenty-five gameplay intent families; the full client does not include those
  wire types.
- the real UDP loopback drives negotiation, an authentication frame, heartbeat,
  reroute, resync and gameplay through server admission/dispatch and verifies
  applied, rejected and idempotent replay receipts with revision propagation.
- Replicated NPC authority guards and transform presentation exist.
- The server has a composed trigger/fanout/mover/entity runtime capable of
  deterministic typed internal outputs and restoring pending work.
- The server also has a directory foundation for several isolated mutable world
  instances sharing one immutable content revision, plus a strict server-owned
  story policy that derives safe realm/party/character/explicit cohorts and
  commits membership reroutes atomically.

The last two items are server foundations only. They are not client wire
contracts and must not be consumed by including server headers or duplicating
server routing/story logic.

## Required before complete MMO UX

1. Lift the completed Protocol V2 session/gameplay builders and typed
   bootstrap mailbox into the stable non-wire full-client facade.
2. Complete the remaining binary bootstrap section codecs for inventory,
   equipment, quests/dialog, script scope, world objects and durable NPC state,
   then replace the historical string snapshot producer/consumer.
3. A route-bound world-instance identity in admission, bootstrap, deltas,
   corrections, transitions and resync.
4. Typed entity lifecycle/live replication, revisions and resync.
5. Thin presentation adapters for committed world-logic outputs: mover
   transforms, interaction state, transitions, scripts/presentation and
   rejection/correction paths.
6. Inventory, equipment, combat, dialog, quest and world-event presentation.
7. Server character create/select/load and MMO save replacement.
8. Complete reconnect, world-transition and instance-reroute UX.
9. Multi-client headless proof followed by full-client proof.

## Parallel-world client contract

The full client must not calculate, select or persist authoritative story
projection or instance placement.

- the server assigns the authenticated session to an instance;
- bootstrap names the exact world/content/instance revision being presented;
- all live deltas and receipts are rejected locally when their route identity is
  stale or belongs to another instance;
- a story choice is sent only as an intent; the client never supplies a
  `partitionsWorld` flag, cohort or target instance;
- any required fork/reroute is a committed server result containing the new
  route identity and revision;
- reroute is handled as a bounded transition: stop old-instance application,
  acknowledge the transition, install the new bootstrap/snapshot, then resume;
- local caches are keyed by content revision plus route-bound instance identity;
- NPC alive/dead, routine location, passage, trigger and mover truth always
  comes from the assigned instance;
- two visually identical worlds may still have different instance identities,
  and the client must not merge their mutable state.

Character-only quest/journal/UI state can coexist with a shared world instance,
but that distinction is received from typed server projections rather than
inferred in rendering code.

## Client boundary for the next server integrations

- input produces intents only;
- `client_sandbox` owns transport, codecs, retry and session lifecycle;
- the full client consumes typed snapshots/deltas and performs rendering,
  interpolation, animation, audio, camera and UI;
- local prediction must be reversible and cannot commit trigger, mover,
  interaction, inventory, combat, quest, script or instance-routing state;
- no direct dependency on `src/server`, ZenKit server loaders, DB or ASIO is
  allowed in the full-client adapter.

Network scaling is not a client milestone until the global playable 1:N UDP
gate passes.




### Server-side parallel-instance foundation

The server now owns process-level story routing, authoritative player entities and one NPC/world authority graph per instance. The full client remains presentation-only and receives route-bound sandbox state.
