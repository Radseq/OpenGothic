# API Contracts — Full Client MMO Boundary

The engine-facing input boundary starts in `mmoclientadapter.h`. Domain systems
submit synchronous DTO views that do not expose packet kinds, route headers,
sequence allocation, codecs, sockets or ASIO types.

Implemented request families:

- `ClientBootstrapRequest`;
- `ClientMovementIntent` for movement proposals and character checkpoints;
- `ClientInteractionRequest` with a typed verb and optional generation-safe
  target handle;
- `ClientInventoryRequest` with typed actions and checked unsigned-to-wire
  narrowing;
- `ClientWeaponStateRequest`;
- `ClientCombatRequest`;
- `ClientDialogChoiceRequest` with expected revision and client choice sequence.

`submitClient*` functions validate and synchronously copy requests into the
current compatibility facade contract. Character stats, authoritative state
changes, wallet values, NPC death state and other server-owned results are
structurally absent from these DTOs.

The engine-facing presentation types live in
`mmoserverentitypresentationtypes.h`:

- `ServerEntityHandle` is `(id, generation)`;
- `ServerPresentationRouteView` is `(local world generation, server world
  instance)`;
- `ServerEntityKind` distinguishes local player, remote player and NPC;
- `ServerEntityTransformObservation` is the protocol-independent transform
  boundary.

`ServerEntityPresentationRegistry` owns the only handle-to-local-object map.
Lookups, invalidation and despawn require the exact handle and local world
generation. A newer entity generation releases the previous binding and cannot
reuse it implicitly. Route reset returns all released bindings so the engine can
remove replica presentation safely.

`ServerMovementCorrectionBoundary` owns pending correction values and accepts
only the currently bound local-player handle on the active route with a strictly
newer server tick. Application/reconciliation is intentionally separate and
waits for typed Protocol V2 correction delivery.

The typed presentation event boundary lives in
`mmoserverpresentationevents.h`; `mmoserverpresentationstate.h` owns its
bounded state machine. Both are independent of facade and wire types:

- `ServerPresentationRouteIdentity` is `(connection id, route epoch, server
  world id, server world generation)`;
- `ServerPresentationBootstrap` carries a world descriptor, entity roster, NPC
  states and interactive/mover baselines under one baseline;
- `ServerPresentationEvent` is the closed variant consumed by the full-client
  state machine;
- `ServerPresentationApplyResult` reports the engine mutation, released exact
  entity and whether a correction requires hard snap;
- `ServerPresentationState` is bounded, installs bootstrap atomically and never
  parses JSON.

Facade F should map its domain DTOs into this boundary in one direction. It must
not make `GameSession` depend on Protocol V2 wire structs.

`mmoclientbridge.h` remains the transitional integration boundary around
`ClientRuntimeFacade`. Its stable responsibilities are:

- configure/start/stop the facade from command-line MMO mode;
- submit adapter-produced compatibility intents while facade V2 is integrated;
- drain server presentation/bootstrap events and diagnostics;
- never expose transport ownership to gameplay/UI systems.

Wire schema, endpoint retry, logical-session continuity, sequencing and receipts
are sandbox/facade concerns, not full-client API concerns.
