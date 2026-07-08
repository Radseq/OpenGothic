# Step125 - MMO default session and DB character menu

Goal: server-bound MMO mode should not require manual character/session flags for
normal local-dev use, and the menu should stop presenting native `.sav` slots as
the persistence model.

Implemented:

- client default MMO action session now matches the C++ server default:
  `local-dev-PC_HERO_TEST`;
- client default periodic character checkpointing is enabled:
  - interval: 5000 ms;
  - force interval: 30000 ms;
  - minimum distance: 50 world units;
  - minimum yaw: 5 degrees;
- `Save` menu entries are disabled in server-bound mode;
- `MENU_SAVEGAME` is no longer treated as DB Continue;
- `Load` slots are populated from `character_list` in the latest server
  bootstrap snapshot;
- selecting a Load slot switches `CommandLine::mmoCharacterKey()` at runtime and
  loads DB Continue for that character;
- New Game in server-bound mode generates a new DB `character_key`, asks the
  server to create/select it, and starts a fresh local baseline without applying
  an old DB snapshot;
- the C++ UDP server adds `character_list` to bootstrap/live snapshots;
- the C++ UDP server uses a DB session key derived from client session key plus
  character key, so multiple characters do not fight over one `server_sessions`
  row;
- the C++ UDP server auto-creates a missing character from the `PC_HERO`
  template by copying position, stats, and script state.

Current model:

- local Gothic script/player instance remains `PC_HERO`;
- DB identity is `character_key`;
- Load menu selects DB identity, not a `.sav` file;
- server persistence comes from direct semantic DB actions and periodic character
  checkpoints, not from menu Save.

Still not final:

- New Game currently auto-generates a technical display name; there is no
  character naming UI yet;
- auto-create deliberately does not copy `item_instances`, because those are
  unique rows with ownership constraints;
- full periodic DB-save-checkpoint manifests are still separate from lightweight
  character checkpoints.

Minimal client command after this step:

```bash
./build/opengothic/Gothic2Notr \
  -g "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  -g2 \
  -mmo-client-server 127.0.0.1:29777
```

Minimal server command:

```bash
./build/mmo_cpp_server/mmo_udp_server \
  --bind 127.0.0.1:29777 \
  --mysql-url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean"
```




