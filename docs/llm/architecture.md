# Architecture

## Authority Layers

```text
content build
-> runtime read-model
-> server world_instance cache
-> server validation/tick
-> durable events/current projections
-> client replication/materialization
```

The client collects input and presents state. The C++ MMO server owns MMO truth.

## Client Boundary

The client owns:

- input collection;
- local presentation;
- native single-player behavior when no MMO flags are active;
- applying server bootstrap materialization after opt-in.

The client does not own MMO truth for world item state, NPC lifecycle/AI/dialog,
perception, authoritative movement, quest/progression effects or server script
decisions.

Compatibility rule:

- old client behavior must remain unchanged;
- new server/MMO behavior is active only when explicit flags select that path.

## Server Boundary

The C++ MMO server owns:

- bootstrap ACK/NACK/diagnostic;
- direct DB procedure calls for accepted gameplay intents;
- session recovery after clean DB rebuilds;
- current bootstrap snapshot generation;
- future runtime world instance simulation.

Existing server flow:

```text
UDP binary packet
-> packet decode
-> identity/session/content checks
-> direct DB procedure call
-> ACK/NACK/diagnostic
```

`mmo_server_action_outbox` is fallback/debug only.

## Content Build Boundary

Content build is read-only source processing:

```text
Gothic content sources
-> ZenKit importer/probes
-> parser_snapshot.json
-> mmo_content_build
-> runtime read-model export
-> C++ read-model indexes/cache
-> C++ server startup-loaded world_instance content cache
-> C++ NPC/perception candidate policy
-> C++ AI runtime recording adapter/probe
```

`mmo_content_build` is not live gameplay state. Runtime decisions belong in the
runtime MMO DB or `mmo_ai_runtime`, depending on domain.

## NPC/Script Direction

Target model:

```text
one content revision
-> one server read-model cache built from Step226 indexes
-> one Step227 world_instance content cache
-> Step228 server startup/session integration
-> Step229 cache-backed NPC perception candidate assessment
-> Step230 explicit mmo_ai_runtime recording probe
-> one authoritative world_instance
-> server NPC/script/perception tick over active players and NPCs
-> durable decisions + fan-out packets
```

Do not run separate script truth per player.

## Data Model Principles

- Baseline content is immutable input.
- Runtime projections are live truth.
- Event journal explains divergence from baseline.
- Generated reports and JSONL are evidence, not authority.
- Stable identity uses world/content revision plus persistent/template ids, not
  display labels.
