# Domain Glossary

Use only when terminology is unclear. Do not treat this as architecture policy.

## Server-Bound Mode

Client mode enabled by `-mmo-client-server`. The client sends gameplay intents to
the C++ MMO server and applies server bootstrap materialization.

## Semantic Action

Client-side structured gameplay intent/evidence emitted by hooks. In the MMO
path, semantic actions should become typed server packets and durable events.

## Event Journal

Append-only durable gameplay ledger. Accepted server mutations should be
explainable through journal/projection state.

## Current Projection

Typed DB table/view representing current live state derived from events and
server-owned transactions.

## Content Pack

Server-owned set of Gothic content files for a target revision: ZEN, DAT, OU,
VDF/MOD or extracted root, manifest and hashes.

## Content Revision

Stable key identifying the server's active game/mod content version. Clients may
declare a manifest hash; the server validates it.

## `mmo_content_build`

Build-time MySQL schema containing parsed ZEN/DAT/OU content for a content revision.

## Runtime Read-Model

Generated server-cache artifact exported from `mmo_content_build`. Current schema:
`mmo.content_build_runtime_read_model.v1`.

## `world_instance`

Authoritative server simulation boundary. It should load one content read-model/cache
and tick NPC/script/perception logic for all active players in that instance.

## NPC Perception

Server-side decision domain for NPC reactions to players/NPCs. Runtime decisions
belong in `mmo_ai_runtime`.

## Routine

Content-derived NPC schedule/action hint from Daedalus. A routine is server
context until server tick materializes and evaluates it.

## Dialog Info

Daedalus dialog metadata: condition/information functions, permanence,
importance and trade flags.

## Output Unit / OU

Compiled dialog output data: text, audio refs and output names.

## VOB

Gothic world object from ZEN. Can represent items, NPC spawn hints, triggers,
movers, sounds, lights, waypoints and other world entities.

## Stable Key

Durable identity string or DB UUID that survives save/load/runtime drift. It
must not depend on mutable state such as position, HP, amount or array order.

## Bootstrap Snapshot

Server-generated load-time JSON snapshot sent after bootstrap ACK. It is current
materialization evidence, not live replication.

## Outbox

Fallback/debug server action queue. It is not the preferred path for new
authoritative gameplay.
