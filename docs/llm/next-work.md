# Next Work

## Immediate Step

Step275 should add a guarded no-execute execution/result boundary around the
Step274 persistence preview bridge. It may validate statement ordering, output
slots and failure classes, but must keep `runMysql`, UDP replay send, client
dialog UI/audio and `mark_applied` disabled.

Recommended order:

1. Keep Step274 as the only C++ surface that builds Step273 SQL/procedure
   previews from typed runtime state.
2. Add a separate executor-policy/result-parser boundary that proves how calls
   would be sequenced later, without opening a MySQL connection.
3. Defer executable SQL/tools changes to the final DB/tools batch.

## Acceptance Checks

- full `server/cpp` build passes when server C++ changes are touched;
- full `Gothic2Notr` build passes when client/game changes are touched;
- LLM DB ledger tools still pass for the historical planned-only entries;
- Step273 validation reports all new DB surfaces present when DB work is
  intentionally run;
- Step274 preview reports `execute_mysql=off` and `mutated_db=0`;
- no automatic scheduler, real dialog UI/audio, movement replication, combat
  execution, durable retry worker or `mark_applied` path is enabled by default.

## Defer

- automatic world-instance AI scheduling in `mmo_udp_server`;
- full Daedalus VM execution;
- live NPC movement/path/routine replication;
- real late-observer replay UDP send;
- client subtitle/audio application from server dialog intents;
- combat/spell/projectile server authority;
- marking AI actions applied before durable ACK/apply/dead-letter storage is
  proven.


