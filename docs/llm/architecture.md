# Architecture

## Core Principle

In MMO mode, the server owns gameplay truth. The client remains presentation,
input capture, local prediction and native single-player runtime compatibility.

## Modes

### Native Single-Player

- Default OpenGothic/Gothic behavior.
- Native saves/new game/loading remain unchanged.
- No server dependency.
- No MMO packet path unless explicit flags are passed.

### MMO Server-Bound Mode

Enabled by `-mmo-client-server host:port` and related MMO flags.

- Client sends typed intents/evidence to C++ MMO server.
- Server validates, journals and mutates authoritative state.
- Server sends bootstrap/materialization data and later live deltas.
- MySQL stores runtime state, event journal, current projections and read models.

## Authority Ownership

| Domain | Authority |
| --- | --- |
| Native single-player state | Existing client/runtime |
| MMO player character state | C++ server + runtime DB |
| MMO inventory/equipment | C++ server + runtime DB |
| MMO NPC decisions/perception/dialog | C++ server |
| Static Gothic content | Content build pipeline + approved content revision |
| Runtime read-model cache | C++ server |
| Diagnostics/bootstrap JSON | Server-produced artifact, not hot authority |

## Content Build vs Runtime

Content build parses static Gothic content:

```text
VDF/MOD/extracted root -> ZenKit/Daedalus/OU parsing -> mmo_content_build -> runtime read-model export
```

Runtime consumes approved content:

```text
runtime read-model export -> C++ RuntimeReadModel -> world_instance content cache -> future NPC/script/perception tick
```

Do not parse client-local Gothic files as MMO truth at runtime. The server must
select and validate the content revision it uses.

## Bootstrap vs Live Replication

Bootstrap is load-time materialization. It answers: what should the client see
at start or after interest change?

Live replication is not the same thing. Future live replication should use typed
server-owned updates/deltas, not raw save or debug JSON as the hot contract.

## Identity

Durable identity must be based on stable keys:

- content revision;
- world instance key;
- persistent entity id;
- symbol/name from parsed content where stable;
- VOB key/path where stable;
- server-generated UUID.

Avoid relying on:

- display names;
- `PC_HERO`/`Ja` labels;
- array order;
- current position alone;
- mutable amount/HP/state.
