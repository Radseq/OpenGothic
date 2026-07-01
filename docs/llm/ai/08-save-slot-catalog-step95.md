# 08 Save Slot Catalog Step95

Step95 moves the save-to-DB work from a generic checkpoint manifest toward a real
DB-backed save-slot catalog and a controlled `.sav`-free continue bridge.

Implemented:

- `GameSession::recordMmoSaveSlot` now emits `save_checkpoint_manifest` in
  server-bound mode, after the native save has completed. This gives the server
  both the native slot path/key and the display name instead of only a generic
  manifest key.
- `mmo_save_checkpoint_manifests` is extended with:
  - `save_slot_key`;
  - `native_save_path`;
  - `display_name`;
  - `client_world_name`;
  - `native_save_present`.
- `mmo_create_save_checkpoint_manifest` is replaced with a Step95-compatible
  version that still has the same public procedure signature, so the C++ server
  direct DB call stays stable.
- `v_mmo_latest_save_checkpoint_manifests` exposes the latest DB save catalog
  rows per character/slot and keeps `BIN_TO_UUID(...,1)` readable for admin
  inspection. This is an admin/readiness view, not the final hot-path production
  read model.
- The bootstrap snapshot `server_checkpoint_manifest` now includes the slot key,
  display name, native save path, client world name and native-save presence.
- `-mmo-db-continue-without-native-save` / `-mmo-db-continue` is a guarded dev
  bridge: if `-mmo-client-server` is active and the requested native `.sav` file
  is missing, the client creates a baseline world and applies the server snapshot
  instead of failing immediately on local file open.

Important constraints:

- Old single-player save/load is unchanged without `-mmo-client-server`.
- This is not the final production DB-only Continue. It still needs a pre-world
  server/session metadata handshake so the client can choose the authoritative
  world before constructing the baseline ZEN. For now it uses
  `-mmo-db-bootstrap-world` or the default world.
- Native `.sav` remains a compatibility/debug cache. DB truth remains current
  projections plus the event journal.

Validation targets:

```bash
python3 tools/check_mmo_step95_save_slot_catalog_db_continue_bridge.py \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --smoke
```

Inspect latest DB save catalog rows:

```sql
SELECT character_key, save_slot_key, display_name, client_world_name,
       latest_checkpoint_tick, recent_event_seq, inventory_rows, quest_rows,
       known_dialog_rows, world_item_rows, interactive_rows, mover_rows, created_at
  FROM v_mmo_latest_save_checkpoint_manifests
 ORDER BY character_rank, created_at DESC
 LIMIT 20;
```


## Step96 supersedes the catalog-only direction

The slot/catalog fields are not a gameplay objective. They are kept as minimal
metadata so save checkpoints can be identified, debugged and later exposed in a
menu. Actual save migration continues in Step96 through structured snapshot
tables generated from DB current projections.
