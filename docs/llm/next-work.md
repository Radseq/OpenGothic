# Next Work — Full OpenGothic Client

Last updated: 2026-07-14.

The global roadmap currently prioritizes Protocol V2, typed replication and
binary bootstrap before broad MMO UX. Client changes should connect those
contracts to the prepared domain/presentation boundaries without inventing a
second transport.

## 1. Complete the V2-only engine adapter

Implemented:

- engine hooks submit domain bootstrap, movement/checkpoint, interaction,
  inventory, weapon, combat and dialog requests;
- compatibility packet construction and bridge submission are removed;
- validation is fail-closed and does not submit local gameplay results;
- native single-player behavior remains behind existing mode checks.

Movement input, interaction handles, weapon/combat actions and numeric dialog
choices map directly to `ClientRuntimeFacade`. Implemented in the graphical
client:

- full hello/authenticate/list/create/select/enter-world lifecycle;
- typed roster-backed New Game and Continue/Load;
- normalized movement axes, movement mode, input flags, predicted pose and
  acknowledged server tick;
- exact NPC/interactive reverse lookup for Talk/Loot/Use;
- typed draw/holster/primary/secondary/parry submission and local-damage
  suppression for server replicas.

Next:

- extend container and trade hooks with exact V2 handles and revisions;
- enrich numeric dialog identities and choice metadata from typed presentation state;
- keep sequence/idempotency/receipt/retry/reconnect ownership inside the facade;
- do not restore packet-shaped compatibility submission while these hooks are
  incomplete.

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
- protocol-independent S6 fake-event contracts and read models now cover
  equipment slots, weapon mode, combat action timelines/results, authoritative
  damage/HP, hit reaction and life state;
- the graphical materializer resolves weapon visuals, maintains hand/back
  attachments, presents combat/life animations, VFX/SFX/knockback and local-only
  camera shake, and corrects rejected presentation prediction without applying
  local damage.

Facade-domain mapping now implemented:

- `mmoserverpresentationfacadeadapter.h` maps completed bootstraps and every
  typed S2C mailbox into `ServerPresentationEvent`/`ServerPresentationBootstrap`;
- the adapter validates route/baseline/enum/flag invariants, drops malformed
  records, and restores global event order with `streamSequence`;
- `mmoclientbridge` exposes a single protocol-independent typed batch; no wire
  struct reaches `GameSession`.

Next integration:

- map the shared S0/S5 facade mailboxes for equipment, weapon mode, combat,
  damage, hit reaction and death into the prepared S6 records; preserve server
  ordering/revisions and do not infer hits or HP from local animation;
- deploy the importer-generated shared binary presentation catalog next to the
  client build, then pass `-mmo-client-presentation-catalog` together with the
  admitted `-mmo-client-presentation-manifest-id`; the runtime consumer and exact
  `(ArchetypeId, PresentationId) -> Daedalus instance` lookup are implemented;
- extend the typed dialog presentation contract or catalog lookup so numeric
  line IDs resolve to subtitle/audio metadata and awaiting-choice updates carry
  the actual revisioned choice list;
- add engine-level integration fixtures for route replacement, bootstrap
  materialization, mover/interactive application and dialog UI lifecycle in a
  complete OpenGothic checkout with third-party dependencies.

## 3. Complete server-backed inventory presentation

Implemented:

- bootstrap inventory/equipment revisions and stack bindings reach the full
  client through the sandbox facade;
- `ServerInventoryReadModel`, `ServerEquipmentReadModel` and bounded pending
  command state are generation-safe and reject stale/malformed snapshots and
  deltas;
- `InventoryMenu` uses only the server read model in MMO mode and performs no
  optimistic quantity or equipment mutation;
- equip, unequip, use, drop, split and merge actions submit typed Protocol V2
  intents and wait for authoritative revisions after `Applied` receipts.
- live inventory/equipment events update the read models in `GameSession`, item
  display names resolve through the presentation catalog, and revisioned world
  items materialize with exact handles used by pickup intents.

Next:

- resolve item presentation IDs to icons without treating `ArchetypeId` as a
  Daedalus symbol index;
- add server-backed container, loot and trade pages;
- add a graphical smoke test in a complete checkout.

## 4. Classify remaining semantic hooks

For every callback in `mmosemantichooks.*`, retain exactly one role:

- valid client intent routed through the adapter;
- non-authoritative local diagnostic;
- native single-player-only behavior;
- obsolete migration hook to delete.

Do not send before/after stats, damage, wallet, quest, NPC death or world-state
results as MMO truth.

## 5. MMO character UX

Implemented:

- New Game creates a server character;
- Continue/Load lists server characters without selecting local save files;
- Save is disabled in MMO mode;
- command-line character/session identities are configurable;
- `-nomenu` can auto-enter a server session, and the launcher starts a local
  server plus graphical client.

Next:

- replace development guest identity with production account login/token UX;
- add character deletion/rename/appearance selection;
- make world transitions use server approval plus explicit loading
  presentation;
- present reconnect/recovery state in UI instead of logs only.

## Acceptance

- no full-client MMO socket, codec, retry or bootstrap assembler;
- no semantic hook or UI constructs client wire packets;
- no local gameplay result can be submitted as authority;
- stale route/entity generations cannot mutate or despawn a replacement;
- replicated NPCs never run local authoritative AI;
- the same behavior is proven first in the headless sandbox;
- native single-player remains unchanged when MMO mode is off.
