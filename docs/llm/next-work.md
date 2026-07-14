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

- extend inventory/equipment/container/trade hooks with V2 item/entity handles,
  generations and expected revisions;
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

Facade-domain mapping now implemented:

- `mmoserverpresentationfacadeadapter.h` maps completed bootstraps and every
  typed S2C mailbox into `ServerPresentationEvent`/`ServerPresentationBootstrap`;
- the adapter validates route/baseline/enum/flag invariants, drops malformed
  records, and restores global event order with `streamSequence`;
- `mmoclientbridge` exposes a single protocol-independent typed batch; no wire
  struct reaches `GameSession`.

Next integration:

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

## 3. Classify remaining semantic hooks

For every callback in `mmosemantichooks.*`, retain exactly one role:

- valid client intent routed through the adapter;
- non-authoritative local diagnostic;
- native single-player-only behavior;
- obsolete migration hook to delete.

Do not send before/after stats, damage, wallet, quest, NPC death or world-state
results as MMO truth.

## 4. MMO character UX

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
