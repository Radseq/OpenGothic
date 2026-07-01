# 07 Save Checkpoint Step94

This is the first runtime implementation slice from the save-to-server roadmap.
It does not replace the native Gothic `.sav` writer. It adds a server-bound
checkpoint manifest that runs only when `-mmo-client-server` is active.

Implemented surfaces:
- client emits `save_checkpoint_manifest` after `GameSession::save` writes the
  native save and after the normal character checkpoint hook;
- C++ UDP server handles this action directly through
  `mmo_create_save_checkpoint_manifest`;
- Step93 replaces `mmo_update_character_quest` with a UTF-8/idempotent quest
  journal path and `character_quest_audit`;
- Step94 adds `mmo_save_checkpoint_manifests` and the manifest creation
  procedure;
- bootstrap/live JSON snapshots include `mover_state` and
  `server_checkpoint_manifest`;
- client parses/logs those sections but does not yet drive `MoveTrigger` from
  DB mover state.

Rules:
- Do not make `.sav` authority for MMO state. Treat it as compatibility/debug
  until DB-only restore parity is proven.
- Do not persist camera, renderer/audio state, animation frame, pointer queues,
  raw fight queues or raw AI queues in the manifest.
- Keep all new behavior under server-bound mode.

Next work:
1. Add safe mover materialization API that sets mover state from DB without
   firing local trigger chains.
2. Build DB-only boot: load baseline ZEN, request server snapshot, apply durable
   facts, then let server-controlled systems start.
3. Extract snapshot SQL/read-model logic from `mmo_udp_server.cpp`.
