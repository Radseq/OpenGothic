# Current State - Full OpenGothic Client

Last verified: 2026-07-11, typed client runtime phase 2.

## Role

The full client owns engine input and presentation. In server-bound mode it is
not a transport implementation and not gameplay authority.

## MMO boundary

The client depends only on:

```text
<gothic/mmo/client_runtime_facade.h>
```

It no longer contains ASIO, endpoint resolution, UDP workers, packet codecs,
bootstrap chunk assembly, retry/ACK state, or generic JSON packet builders.

Input hooks submit typed requests/proposals. Completed gameplay claims are
suppressed from the network and may be recorded only as local diagnostics.

## Bootstrap

Completed server bootstrap snapshots arrive through an in-memory bounded
mailbox. The obsolete local JSON file restore/apply path was removed. The
current snapshot body is still the server's existing JSON schema, parsed from
memory; migration to a versioned binary body remains server/shared work.

## NPC presentation

Authoritative entity transform deltas are interpolated and mapped using server
entity ID, generation and stable key. Local NPC identity is revalidated before
application. Replicated NPCs are marked `mmoServerReplica`, which disables local
routine, perception, regeneration and combat authority while preserving visual,
audio and animation presentation.

## Dialog

Server-published revision and choices drive the UI. The client submits only a
typed choice intent and does not execute local dialog effects as MMO truth.

## Tooling

SQLite capture/restore lives under `src/client/tools/mmo` and is excluded from
production by default. JSONL is diagnostics only.



## Typed bootstrap status - 2026-07-11

The menu consumes bootstrap acceptance/rejection through the in-memory
`ServerBootstrapStatus` bridge backed by `ServerAckKind::Bootstrap`. It no
longer creates, polls or deletes `runtime/mmo_server_bootstrap_reject.json`.
The bootstrap snapshot body remains the historical JSON schema in memory until
the versioned binary bootstrap-section migration is completed.
