# Next Work

## Immediate Step

Step243 follow-up: keep SQLite out of the active server-content path and design
the next disabled-by-default boundary after Step242 fanout planning.

Step242 now passes on a session-bound action by creating a synthetic NPC
decision whose target identity comes from a real active runtime player. The plan
returns `built=true`, `planned_datagrams=1`, `fits_single_datagram=true`, while
all send/fan-out/UI/audio/apply booleans stay `false`.

Recommended next target:

- keep server content/read-model/AI validation on C++ + MySQL, not SQLite;
- keep SQLite tools only for legacy baseline/oracle and old clean rebuild flows;
- add a no-send fanout adapter probe or ACK contract before any real packet send;
- continue to skip cleanup claimed test actions.

## Acceptance Checks

- full `server/cpp` build passes;
- read-model/cache/policy probes still pass;
- Step212/213 DB checks pass;
- action queue is empty after any claim/skip cleanup;
- no automatic scheduler, packet send, dialog UI/audio, movement replication,
  combat execution or mark-applied path is enabled.

## Defer

Do not implement yet:

- automatic AI scheduling in `mmo_udp_server`;
- full Daedalus VM execution;
- per-player script VMs;
- live NPC movement/path replication;
- dialog UI/audio fan-out;
- marking AI action rows applied as gameplay effects;
- final DB/storage rewrite.
