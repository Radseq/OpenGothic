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

## 2. Connect typed S2C presentation

The local presentation core is prepared with route-scoped handle binding,
entity kinds, safe despawn/rebind, interpolation and movement-correction
validation. After shared contract A lands:

- translate typed `WorldDescriptor` and route generation into
  `ServerPresentationRoute`;
- consume `EntitySpawn`, `EntityDespawn`, `EntityTransform`, `NpcState`,
  dialog, interactive and mover events;
- instantiate remote-player/NPC presentation objects when no local object is
  available;
- feed local-player `MovementCorrection` into the correction boundary and apply
  snap/reconciliation only after validation;
- clear pending route commands and presentation state on route replacement.

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
