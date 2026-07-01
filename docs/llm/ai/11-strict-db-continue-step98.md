# 11 Strict DB Continue Step98

Purpose: make DB-native Continue/restores testable without accidentally passing because the server silently fell back to live projections or because the local native `.sav` still contained the wanted state.

Step97 made DB save checkpoints an actual bootstrap source. Step98 adds strict validation and clearer source metadata.

Implemented surfaces:
- Bootstrap snapshots produced from live current projections now include `snapshot_source=current_projections_v1`.
- DB save checkpoint snapshots already include `snapshot_source=db_save_checkpoint_v1` and `db_save_checkpoint_manifest_uuid`.
- The client parses and logs `source`, `snapshot_source` and the DB save checkpoint manifest UUID.
- New client guard: `-mmo-require-db-save-checkpoint-restore` / `-mmo-strict-db-continue`. In this mode the client rejects a downloaded bootstrap snapshot unless `snapshot_source=db_save_checkpoint_v1`.
- New server guard: `mmo_udp_server --require-db-save-checkpoint-restore`. In this mode bootstrap is NACKed if no DB save checkpoint snapshot can be exported.
- Live movement-triggered world refreshes no longer use the DB save checkpoint exporter. They explicitly use current projections only, so an old save checkpoint cannot overwrite a live nearby-item refresh.
- SQL validation function/procedure/view:
  - `mmo_validate_latest_save_checkpoint_restore_v1(session_id)`
  - `mmo_assert_latest_save_checkpoint_restore_v1(session_id, OUT validation_json)`
  - `v_mmo_latest_save_checkpoint_strict_restore`

Validation:

```bash
python3 tools/check_mmo_step98_strict_db_continue_restore.py \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --smoke \
  --output runtime/step98_strict_db_continue_restore/check.json
```

After a real in-game save, validate existing state without creating a smoke checkpoint:

```bash
python3 tools/check_mmo_step98_strict_db_continue_restore.py \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --require-existing \
  --output runtime/step98_strict_db_continue_restore/check_existing.json
```

Strict manual test after a real DB checkpoint exists:

```bash
./build/mmo_cpp_server/mmo_udp_server \
  --bind 127.0.0.1:29777 \
  --mysql-url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --session-key local-dev-PC_HERO_TEST \
  --character-key PC_HERO \
  --require-db-save-checkpoint-restore

./build/opengothic/Gothic2Notr \
  -g "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  -g2 \
  -mmo-client-server 127.0.0.1:29777 \
  -mmo-action-session-key local-dev-PC_HERO_TEST \
  -mmo-require-db-save-checkpoint-restore
```

Expected evidence:

```text
[bootstrap_db_save_checkpoint_restore] bytes=...
MMO server snapshot restore source: source=mmo_udp_server_cpp_db_save_checkpoint snapshot_source=db_save_checkpoint_v1 db_checkpoint=1 manifest_uuid=...
```

Limits:
- This still uses the same bootstrap JSON contract and load-time materialization.
- It does not yet remove native `.sav` menu UX or implement full production shard-memory restore.
- It is a strict evidence guard for the save-file-to-DB migration path.
