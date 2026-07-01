# 22 MMO Menu DB Continue - Step109

Purpose: remove the fake local save-slot UX from the normal server-bound client
flow.

Before Step109, strict DB-only testing required commands such as `-save 99` and
`-mmo-db-continue-without-native-save`. That proved the DB checkpoint restore
path, but it was not a real MMO player flow.

Step109 rule:

- Without `-mmo-client-server`, native New Game/Load/Save behavior is unchanged.
- With `-mmo-client-server`, local `.sav` is treated as compatibility/debug
  cache, not the normal authority path.
- In server-bound mode, DB Continue is enabled by default.
- The out-of-game menu redirects New Game/Load-style actions to DB Continue, so
  an already-created server character enters through the server snapshot instead
  of choosing a local save file.

Changed files:

- `game/commandline.cpp/.h`
- `game/mainwindow.cpp`
- `game/ui/gamemenu.cpp`

Normal manual flow after this step:

```bash
./build/mmo_cpp_server/mmo_udp_server \
  --bind 127.0.0.1:29777 \
  --mysql-url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --session-key local-dev-PC_HERO_TEST \
  --character-key PC_HERO

./build/opengothic/Gothic2Notr \
  -g "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  -g2 \
  -mmo-client-server 127.0.0.1:29777 \
  -mmo-action-session-key local-dev-PC_HERO_TEST
```

Strict DB checkpoint flags remain useful for checkers and diagnostics, but they
are no longer the intended everyday player startup command.
