# 14 DB Continue End-To-End Step101

Purpose: make the current DB-native Continue work testable against a real save
checkpoint, not only against smoke rows created by tools.

State before Step101:
- Step96 creates normalized DB save checkpoint snapshot tables.
- Step97 exports the latest checkpoint as `mmo_bootstrap_snapshot_v1`.
- Step98 adds strict DB checkpoint restore guards on client and server.
- Step99 can choose the baseline world before constructing `GameSession` when
  the native `.sav` file is missing.
- Step100 reuses that pre-world snapshot so strict DB Continue does not
  immediately download the same checkpoint twice.

Step101 adds `tools/check_mmo_step101_db_continue_end_to_end.py`.

The checker does not create a fake save. It inspects the latest real checkpoint
for a session/character and reports:
- required routines/views for DB checkpoint export and strict validation;
- the latest active session;
- `v_mmo_latest_save_checkpoint_strict_restore`;
- `mmo_validate_latest_save_checkpoint_restore_v1`;
- exported bootstrap metadata from
  `mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1`;
- ready/not-ready verdict;
- exact strict server/client commands to run next.

Use after a real in-game save:

```bash
cd ~/Desktop/OpenGothic

python3 tools/check_mmo_step101_db_continue_end_to_end.py \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --session-key local-dev-PC_HERO_TEST \
  --character-key PC_HERO \
  --assert-ready \
  --output runtime/step101_db_continue_end_to_end/check.json
```

Ready means:
- latest session exists;
- latest checkpoint validates with `strict_restore_ok=true`;
- exported bootstrap is non-empty;
- exported `snapshot_source` is exactly `db_save_checkpoint_v1`;
- strict restore view agrees with the procedure result.

If ready is false, inspect `runtime/step101_db_continue_end_to_end/check.json`
before changing runtime code. The likely causes are:
- no real in-game save happened after Step96+;
- checkpoint has no character snapshot row;
- export function is missing or returns empty JSON;
- strict mode would fall back to current projections, which is forbidden.

Manual strict test after readiness passes:

```bash
./build/mmo_cpp_server/mmo_udp_server \
  --bind 127.0.0.1:29777 \
  --mysql-url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --session-key local-dev-PC_HERO_TEST \
  --character-key PC_HERO \
  --require-db-save-checkpoint-restore
```

```bash
./build/opengothic/Gothic2Notr \
  -g "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  -g2 \
  -save 99 \
  -mmo-client-server 127.0.0.1:29777 \
  -mmo-action-session-key local-dev-PC_HERO_TEST \
  -mmo-db-continue-without-native-save \
  -mmo-require-db-save-checkpoint-restore
```

Expected evidence:

```text
MMO DB continue pre-world snapshot selected world=...
  snapshot_source=db_save_checkpoint_v1 manifest=...
MMO DB continue pre-world snapshot reuse enabled ...
MMO server snapshot restore scheduled ... reuse_existing_snapshot=1 ...
MMO server snapshot restore source: ... snapshot_source=db_save_checkpoint_v1 ...
```

There should be no silent fallback to `current_projections_v1` in strict mode.
