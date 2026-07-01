# 12 DB Continue Pre-World Step99

Purpose: remove another `.sav` dependency from the DB-native Continue path.

Before Step99, `-mmo-db-continue-without-native-save` could bootstrap a baseline
ZEN world when the requested native save was missing, but the world came from a
local/default guess unless `-mmo-db-bootstrap-world` was passed explicitly.

Step99 keeps old single-player save/load untouched and changes only this guarded
mode:

- active only with `-mmo-client-server` and `-mmo-db-continue-without-native-save`
  when the requested native `.sav` file is missing;
- if `-mmo-db-bootstrap-world` is explicitly provided, that override still wins;
- otherwise the client sends a pre-world `client_bootstrap_request` through the
  existing semantic action sink;
- the existing C++ UDP server responds with the normal chunked
  `mmo_bootstrap_snapshot_v1`;
- the client validates the snapshot and reads `world_name` from the DB save
  checkpoint snapshot before constructing `GameSession(world)`;
- if strict mode is enabled, the pre-world snapshot must have
  `snapshot_source=db_save_checkpoint_v1`.

This is still a development bridge, not the final production login/realm
handshake. The important improvement is that DB-native Continue no longer has to
guess the baseline world for the missing native save case.

Manual smoke after a real DB save checkpoint exists:

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

Expected client evidence:

```text
MMO DB continue pre-world snapshot selected world=...
  snapshot_source=db_save_checkpoint_v1 manifest=...
MMO DB continue: native save is missing, bootstrapping baseline world ...
```
