# Step124 - MMO menu without local `.sav` persistence

Goal: in server-bound MMO mode the client must stop treating local save slots as
the persistence source. The server/DB owns persistence; the local Gothic runtime
only materializes a world and emits semantic actions.

Implemented in this patch:

- menu Save entries are disabled in `-mmo-client-server` mode;
- menu Save execution is ignored with an explicit log line if triggered anyway;
- F5 quick save is ignored in MMO mode;
- F9 quick load is redirected to DB Continue;
- load slot titles in MMO mode show the selected DB character label instead of
  reading `save_slot_N.sav`;
- preview/header reading for `.sav` slots is skipped in MMO mode;
- bootstrap, movement, checkpoint, resource, damage, and progression semantic
  payloads use `CommandLine::mmoCharacterKey()` instead of hard-coded
  `PC_HERO`;
- server snapshot validation now checks the configured MMO character key;
- new client args:
  - `-mmo-character-key <key>`
  - `-mmo-character-name <display name>`
  - `-mmo-character-display-name <display name>`

Important model decision:

`PC_HERO` remains the local engine/script avatar. Gothic scripts, dialogs,
Daedalus references, and many runtime assumptions are coupled to the player
instance being the hero symbol. MMO identity must be separated from that:

- local runtime/script identity: `PC_HERO`;
- DB identity: `character_key`;
- player-visible label: `display_name`.

This lets multiple DB characters use the same Gothic player template without
rewriting game scripts. Renaming the actual script instance away from `PC_HERO`
is not the right path for the MMO layer.

Still needed for the final UX:

- server-side create-character command/procedure;
- server-side list-character command/procedure;
- client request/response transport for the character catalog;
- real menu page that shows DB characters instead of native save slots;
- New Game should call create-character and then start a fresh baseline under
  the new `character_key`;
- Load should select an existing `character_key`, then run DB Continue.

Current test path after this patch:

Run server and client with the same key. If omitted, both default to `PC_HERO`.

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
  -mmo-action-session-key local-dev-PC_HERO_TEST \
  -mmo-character-key PC_HERO \
  -mmo-character-name Ja
```




