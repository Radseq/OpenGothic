# Next Work — Full OpenGothic Client

Last updated: 2026-07-12.

The global roadmap currently prioritizes Protocol V2, typed replication and
binary bootstrap before broad MMO UX. Client changes should connect those
contracts to the prepared domain/presentation boundaries without inventing a
second transport.

## 1. Replace compatibility mapping with facade V2

Implemented independently of facade V2:

- engine hooks submit domain bootstrap, movement/checkpoint, interaction,
  inventory, weapon, combat and dialog requests;
- packet construction is isolated in `mmoclientadapterdetail.h` and the bridge;
- validation is fail-closed and does not submit local gameplay results;
- native single-player behavior remains behind existing mode checks.

After client-sandbox facade C lands:

- map the existing request DTOs to `authenticate`, `listCharacters`,
  `createCharacter`, `selectCharacter`, `enterWorld`, `requestMovement`,
  `requestInteract` and `requestDialogChoice`;
- remove compatibility packet mapping from the full client;
- keep sequence/idempotency/receipt/retry/reconnect ownership inside the facade.

## 2. Connect facade F to the typed presentation state

Implemented independently of facade F:

- protocol-independent DTOs cover route/world descriptor, bootstrap roster,
  movement correction, entity lifecycle/transform, NPC, dialog/busy,
  interactive and mover presentation;
- `ServerPresentationState` rejects stale route epochs/world generations,
  installs a bootstrap atomically and activates its baseline only after the
  complete roster and world-object baseline are valid;
- generation replacement and despawn are exact-handle; route replacement clears
  entities, world objects, dialogs and pending corrections;
- the current transform path materializes unknown remote players/NPCs as
  MMO-owned proxies and removes their interpolation/presentation bindings on
  release without aliasing pre-existing world NPCs;
- server replicas do not execute local insertion scripts, AI or perception.

Facade-domain mapping now implemented:

- `mmoserverpresentationfacadeadapter.h` maps completed bootstraps and every
  typed S2C mailbox into `ServerPresentationEvent`/`ServerPresentationBootstrap`;
- the adapter validates route/baseline/enum/flag invariants, drops malformed
  records, and restores global event order with `streamSequence`;
- `mmoclientbridge` exposes a single protocol-independent typed batch; no wire
  struct reaches `GameSession`.

Next integration:

- replace the bounded direct `ArchetypeId -> script symbol` compatibility rule
  with a production client resource catalog for hashed/catalog
  `PresentationId`/`ArchetypeId` values;
- extend the typed dialog presentation contract or catalog lookup so numeric
  line IDs resolve to subtitle/audio metadata and awaiting-choice updates carry
  the actual revisioned choice list;
- add engine-level integration fixtures for route replacement, bootstrap
  materialization, mover/interactive application and dialog UI lifecycle in a
  complete OpenGothic checkout with third-party dependencies.

## 3. Classify remaining semantic hooks

For every callback in `mmosemantichooks.*`, retain exactly one role:

- valid client intent routed through the adapter;
- non-authoritative local diagnostic;
- native single-player-only behavior;
- obsolete migration hook to delete.

Do not send before/after stats, damage, wallet, quest, NPC death or world-state
results as MMO truth.

## 4. MMO character UX

- New Game creates a server character;
- Continue/Load lists server characters without selecting local save files;
- Save is disabled/replaced in MMO mode;
- remove hardcoded development character/session identities;
- world transitions use server approval and loading presentation.

## Acceptance

- no full-client MMO socket, codec, retry or bootstrap assembler;
- no semantic hook or UI constructs client wire packets;
- no local gameplay result can be submitted as authority;
- stale route/entity generations cannot mutate or despawn a replacement;
- replicated NPCs never run local authoritative AI;
- the same behavior is proven first in the headless sandbox;
- native single-player remains unchanged when MMO mode is off.
