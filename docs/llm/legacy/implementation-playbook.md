# Implementation Playbook

## Start Here

1. Przeczytaj `docs/llm/legacy/task-brief.md`.
2. Przeczytaj `docs/llm/legacy/code-map.md`.
3. Sprawdz aktualny `git status`.
4. Nie usuwaj lokalnych zmian uzytkownika.
5. Zanim edytujesz save/load, znajdz dokladne miejsca przez `rg`.

## Recommended First Implementation

Dodaj nowy modul eksportera, zamiast dopisywac logike do `Npc::save` albo `WorldObjects::save`.

Proponowane pliki:

- `game/game/worldstateexporter.h`
- `game/game/worldstateexporter.cpp`

Alternatywnie, jesli zaleznosci beda prostsze:

- `game/world/worldstateexporter.h`
- `game/world/worldstateexporter.cpp`

Eksporter powinien byc jawnie wywolany w trybie debug/CLI, a nie zawsze przy save.

## CLI / Trigger

Dodaj opcje command line, np.

```text
-dump-initial-world <directory>
```

Aktualny pierwszy krok jest juz podpiety. Przy starcie nowej gry `MainWindow::startGame` tworzy `GameSession`, a potem, jesli flaga jest ustawiona, wywoluje:

```cpp
WorldStateExporter::exportInitialState(*w, CommandLine::inst().dumpInitialWorld());
```

Eksport trafia do:

```text
<directory>/<target>/<world>/
```

Pliki V1:

- `manifest.json`
- `npcs.jsonl`
- `items.jsonl`
- `npc_inventory.jsonl`
- `mobsi.jsonl`
- `mobsi_inventory.jsonl`

Schema `2` dodaje `stable_key`, `owner_stable_key`, liczniki itemow/inventory w manifescie i target `g2notr` dla `VersionInfo.patch >= 5`.

Po rozszerzeniu exportera schema `2` eksportuje tez mobsi/interactives:

- `mobsi.jsonl`: tag, focus name, display name, owner, scheme, state, typ container/door/ladder, lock/cracked/key/picklock
- `mobsi_inventory.jsonl`: inventory containerow i innych interactive z inventory

Schema `3` dodaje `persistent_id` dla itemow i usuwa `slot_id` z identity itemow swiata. Wersja save `57` zapisuje `Item::persistentId()`.

Schema `4` dodaje:

- `quests.jsonl`: stan quest/topic loga.
- `known_dialogs.jsonl`: zapamietane pary NPC/info z dialogow.

Schema `5` dodaje:

- `script_globals.jsonl`: zmienne globalne Daedalusa typu INT/FLOAT/STRING, z kategoria po nazwie symbolu. To lapie ukryte flagi dialogow, przeczytanych ksiazek, przyznanego EXP/bonusow i innych one-shot efektow.

Schema `6` dodaje:

- `npc_stats.jsonl`: level, exp, exp_next, LP, attributes, protection, damage, hit chance, talent skill/value, mission slots i aivar dla NPC/gracza. To pokazuje realny efekt ksiazek, nauczycieli, bonusow, walki i skryptowych zmian statystyk.

Po zmianie eksportera zawsze uruchom dump ponownie; stare pliki schema `1` nie maja stable key, stare schema `2` maja niestabilne klucze itemow swiata, schema `3` nie ma jeszcze quest/dialog exportu, schema `4` nie ma jeszcze script globals, a schema `5` nie ma jeszcze npc stats.

## Later State Snapshots

Do porownania initial state z gra po kilku minutach/godzinach dodana jest flaga:

```text
-dump-save-world <directory>
```

Jesli gracz zrobi normalny save, exporter zapisze snapshot do:

```text
<directory>/<target>/<world>/snapshots/tick_<tick>/
```

Typowy run do badania delta:

```text
Gothic2Notr.exe -g "<gothic path>" -g2 -nomenu -dump-initial-world exports -dump-save-world exports
```

Potem:

```powershell
powershell -ExecutionPolicy Bypass -File tools/compare_world_dumps.ps1 `
  -Baseline exports/g2notr/newworld.zen `
  -Snapshot exports/g2notr/newworld.zen/snapshots/tick_<tick>
```

Ten diff porownuje JSONL po `stable_key` i raportuje added/removed/changed per plik. To jest pierwszy krok do event/delta modelu.

Do czytelniejszej analizy gameplay eventow uzyj:

```powershell
powershell -ExecutionPolicy Bypass -File tools/summarize_world_events.ps1 `
  -Baseline exports/g2notr/newworld.zen `
  -Snapshot exports/g2notr/newworld.zen/snapshots/tick_<tick> `
  -Limit 30
```

Raport rozbija zmiany na NPC, inventory gracza, itemy swiata, inventory innych NPC, mobsi oraz inventory mobsi.
Od schema `4` raport pokazuje tez nowe/zmienione questy i znane dialogi.
Od schema `5` raport pokazuje tez dodane/usuniete/zmienione globale skryptowe.
Od schema `6` raport pokazuje tez zmiany statystyk gracza/NPC.

Po zwyklym tescie mozna uzyc wrappera, ktory sam bierze najnowszy snapshot:

```powershell
powershell -ExecutionPolicy Bypass -File tools/compare_latest_world_dump.ps1 `
  -WorldDir exports/g2notr/newworld.zen `
  -Limit 30
```

Jesli chcesz od razu zapisac eventy JSONL pod import/staging DB:

```powershell
powershell -ExecutionPolicy Bypass -File tools/compare_latest_world_dump.ps1 `
  -WorldDir exports/g2notr/newworld.zen `
  -Limit 30 `
  -WriteEvents
```

To zapisze `world_events.jsonl` w najnowszym katalogu `snapshots/tick_*`.

Jesli chcesz od razu zrobic lokalny import DB staging:

```powershell
powershell -ExecutionPolicy Bypass -File tools/compare_latest_world_dump.ps1 `
  -WorldDir exports/g2notr/newworld.zen `
  -Limit 30 `
  -WriteEvents `
  -ImportSqlite
```

To zapisze `world_staging.sqlite` w najnowszym katalogu `snapshots/tick_*` i wypisze walidacje licznikow z manifestu.

Jesli chcesz od razu zbudowac lokalna baze przypominajaca docelowa baze serwera MMO:

```powershell
powershell -ExecutionPolicy Bypass -File tools/compare_latest_world_dump.ps1 `
  -WorldDir exports/g2notr/newworld.zen `
  -Limit 30 `
  -WriteEvents `
  -ImportSqlite `
  -BuildMmoDb
```

To zapisze:

- `world_events.jsonl`
- `world_staging.sqlite`
- `gothic_mmo.sqlite`

`gothic_mmo.sqlite` jest zbudowane z replay/canonical state, a nie z raw snapshotu. Ma oddzielne grupy tabel dla account/realm/content/characters/world/event journal.

Importer tworzy teraz dwie warstwy:

- raw/staging: `baseline_*`, `world_delta_events`, `import_validation`;
- MMO projection: `mmo_*` tabele i widoki.

Najwazniejsze tabele MMO projection:

- `mmo_world_templates`: baseline jako statyczny template swiata;
- `mmo_world_instances`: lokalna instancja/shard ze snapshotu;
- `mmo_world_entities`: aktualny stan NPC, itemow swiata i mobsi;
- `mmo_characters`: stan postaci gracza z NPC + `npc_stats`;
- `mmo_inventory`: inventory postaci, NPC i kontenerow;
- `mmo_quest_state`, `mmo_known_dialog_state`, `mmo_script_global_state`;
- `mmo_event_ledger`: event stream z klasa eventu.
- `mmo_replay_*`: canonical current state odtworzony z baseline + eventow;
- `mmo_replay_validation`: porownanie licznikow raw snapshot vs event replay.

Przydatne widoki do kontroli po imporcie:

```sql
SELECT * FROM v_mmo_character_progress;
SELECT * FROM v_mmo_player_inventory ORDER BY item_display_name;
SELECT * FROM v_mmo_dead_npcs;
SELECT * FROM v_mmo_delta_killed_npcs;
SELECT * FROM v_mmo_replay_player_inventory ORDER BY item_display_name;
SELECT * FROM v_mmo_replay_delta;
SELECT * FROM v_mmo_replay_inventory_missing LIMIT 30;
SELECT * FROM v_mmo_replay_inventory_extra LIMIT 30;
SELECT * FROM v_mmo_event_counts ORDER BY event_count DESC;
```

To nadal nie jest finalny schemat produkcyjnego MMO. Jest to pierwszy materialized current-state model, ktory pozwala sprawdzac pytania serwerowe bez grzebania w JSONL.

## Runtime SQLite MMO Mode

Ten tryb zapisuje i odczytuje lokalna baze SQLite podczas gry. Na razie to pierwszy runtime krok, oddzielony od offline dump/import.

CLI:

```text
-mmo-sqlite <path>
-mmo-sqlite-interval-ms <ms>
-mmo-sqlite-no-restore
-mmo-sqlite-capture-baseline
```

Przyklad Windows CMD:

```cmd
build\opengothic\Debug\Gothic2Notr.exe -g "C:\Program Files (x86)\Steam\steamapps\common\Gothic II" -g2 -nomenu -mmo-sqlite runtime\g2notr.sqlite -mmo-sqlite-interval-ms 5000
```

`-mmo-sqlite-capture-baseline` nalezy podac tylko przy pierwszym uruchomieniu nowej bazy i nowej gry. Runtime odrzuca capture, jesli baza ma wiecej niz jedna sesje, aby stary save nie stal sie przypadkowym world template.

Aktualne tabele:

- `runtime_schema_meta`
- `runtime_sessions`
- `runtime_characters`
- `runtime_character_history`
- `runtime_character_inventory`
- `runtime_character_inventory_history`
- `runtime_events`
- `runtime_world_npcs`
- `runtime_world_npc_history`
- `runtime_npc_stats`
- `runtime_npc_stat_history`
- `runtime_npc_ai_state`
- `runtime_npc_ai_history`
- `runtime_quests`
- `runtime_quest_history`
- `runtime_known_dialogs`
- `runtime_known_dialog_history`
- `runtime_dialog_catalog`
- `runtime_dialog_choice_snapshots`
- `runtime_dialog_choice_rows`
- `runtime_dialog_selections`
- `runtime_world_items`
- `runtime_world_item_history`
- `runtime_world_mobsi`
- `runtime_world_mobsi_history`
- `runtime_world_mobsi_inventory`
- `runtime_script_globals`
- `runtime_script_global_history`
- `runtime_realms`
- `runtime_accounts`
- `runtime_character_bindings`

Aktualny zakres runtime:

- start sesji runtime;
- cykliczny zapis pozycji HERO, HP, many, levelu i expa;
- cykliczny zapis current inventory HERO;
- historia snapshotow inventory po tickach;
- diffowy event journal: ruch, HP/mana/level/exp, item added/removed/quantity/equip;
- current-state wszystkich NPC/mobow swiata w osobnej tabeli, bo `runtime_characters` to warstwa postaci/graczy;
- changed-history NPC/mobow: pozycja, HP/mana, level/exp, dead;
- NPC/gracz stat rows: attributes, protections, talent skills, talent values, hit chances;
- historia zmian statow w `runtime_npc_stat_history`;
- AI state/target/follow relations w `runtime_npc_ai_state` i `v_runtime_npc_follow_relations`;
- current-state itemow swiata i historia zmian spawn/remove/move/amount;
- current-state mobsi/interactives, historia state/lock/cracked;
- inventory mobsi/kontenerow;
- runtime globale Daedalusa i historia zmian, kategorie: script/dialog/quest/knowledge/reward;
- lokalny model realm/account/character binding;
- widoki server-facing: character sheet, equipment, world population, item totals, container inventory, interactives, quest/dialog/global state, event counts, persistence summary;
- runtime quest log i znane dialogi;
- quest lifecycle: `v_runtime_quest_state`, `v_runtime_quest_lifecycle`;
- dialog lifecycle: `v_runtime_dialog_state`, `v_runtime_dialog_availability`;
- klasyfikacja dialogow: `consumed_hidden`, `repeatable_known`, `repeatable_not_seen`, `one_shot_not_seen`;
- dialog timeline: pokazane opcje w `v_runtime_dialog_choice_timeline`, faktyczne klikniecia w `v_runtime_dialog_selection_timeline`;
- restore pozycji/HP/many dla HERO przy kolejnym starcie w tym samym swiecie.

Windows lokalny SQLite:

```text
thirdparty/sqlite/include/sqlite3.h
thirdparty/sqlite/lib/sqlite3.lib
thirdparty/sqlite/bin/sqlite3.dll
```

CMake najpierw probuje `find_package(SQLite3)`. Jesli go nie ma, na Windows sprawdza powyzszy lokalny katalog. Gdy znajdzie lokalny pakiet, linkuje `sqlite3.lib` i kopiuje `sqlite3.dll` obok `Gothic2Notr.exe`.

Po tescie runtime DB sprawdz:

```cmd
python tools\check_runtime_sqlite.py --db runtime\g2notr.sqlite --limit 30
```

Najwazniejsze sekcje raportu:

- `Event counts`
- `Recent events`
- `Current inventory`
- `Recent inventory history ticks`
- `World NPC summary`
- `Recent NPC history`
- `Quests`
- `Known dialogs`
- `World item summary`
- `Mobsi summary`
- `Mobsi inventory`
- `Script global categories`
- `Recent script global changes`
- `Persistence summary`
- `Character sheet view`
- `Equipment view`

Szybki audyt produkcyjnej jakosci runtime DB:

```cmd
python tools\audit_runtime_sqlite.py --db runtime\g2notr.sqlite --limit 30
```

Jesli masz z oficjalnej paczki tylko `sqlite3.dll` i `sqlite3.def`, import lib mozna wygenerowac w Developer Command Prompt for VS:

```cmd
lib /def:thirdparty\sqlite\bin\sqlite3.def /machine:x64 /out:thirdparty\sqlite\lib\sqlite3.lib
```

Interpretacja replay:

- `mmo_world_*`/`mmo_inventory` to projekcja raw snapshotu.
- `mmo_replay_*` to stan odtworzony z baseline + przefiltrowanych eventow gameplay.
- `v_mmo_replay_delta` moze pokazac `mismatch`, jesli default event export ukryl ambient/runtime noise. To jest sygnal klasyfikacyjny: trzeba zdecydowac, czy dana roznica nalezy do canonical persistence MMO, czy tylko do runtime/save noise.

Po zbudowaniu `gothic_mmo.sqlite` przydatne zapytania:

```sql
SELECT * FROM v_character_sheet;
SELECT * FROM v_character_inventory ORDER BY item_display_name;
SELECT * FROM v_item_instances WHERE owner_type = 'character';
SELECT * FROM v_character_equipment;
SELECT * FROM v_item_class_counts ORDER BY template_count DESC;
SELECT * FROM v_character_item_totals ORDER BY item_display_name;
SELECT * FROM v_character_item_stacks ORDER BY item_display_name;
SELECT * FROM v_character_stack_policy_issues;
SELECT * FROM v_world_item_stacks LIMIT 30;
SELECT * FROM v_character_inventory_anomalies;
SELECT * FROM v_world_event_counts ORDER BY event_count DESC;
SELECT * FROM v_world_replay_validation;
SELECT * FROM v_runtime_noise_inventory LIMIT 30;
```

Smoke test finalnej lokalnej bazy MMO:

```powershell
python tools/check_mmo_database.py `
  --db exports/g2notr/newworld.zen/snapshots/tick_<tick>/gothic_mmo.sqlite
```

Na etapie reverse engineering mozna dopuscic znane problemy polityki itemow:

```powershell
python tools/check_mmo_database.py `
  --db exports/g2notr/newworld.zen/snapshots/tick_<tick>/gothic_mmo.sqlite `
  --allow-known-policy-issues
```

Jawny kontrakt PostgreSQL:

```text
db/migrations/postgres/001_gothic_mmo_schema.sql
```

Inventory interpretation:

- `character_inventory` / `world_inventory` are import-preserving rows.
- `item_instances` is the server-facing durable inventory layer.
- `character_equipment` stores equipped instances by slot.
- `content_item_classification` assigns heuristic item class and stack policy.
- stack views aggregate item instances, but do not destroy raw instance rows.
- anomalies and `v_character_stack_policy_issues` are a rule-design queue, especially for equipped-vs-bag splits, stack quantities, duplicate consumables and instance items with quantity greater than instance count.

Jesli parser CLI ma inna konwencje, trzymaj sie istniejacego stylu w:

- `game/commandline.h`
- `game/commandline.cpp`
- `game/gothic.cpp`
- `game/mainwindow.cpp`

Minimalny wariant moze najpierw uzyc stalej/sciezki testowej, ale docelowo ma byc flaga.

## Export Timing

Najlepszy pierwszy punkt:

`GameSession::GameSession(std::string file)` po:

```cpp
wrld->postInit();
initScripts(true);
wrld->triggerOnStart(true);
cam->reset(wrld->player());
ticks = 1;
```

Jesli eksport rozni sie miedzy uruchomieniami, sproboj:

- wykonac jeden kontrolowany tick,
- zatrzymac AI,
- zamrozic czas,
- zapisac manifest z `ticks`, `wrldTime`, `world name`, `game target`.

## Export Format V1

Zacznij od JSONL:

```json
{"type":"world","world":"NEWWORLD.ZEN","game":"g2notr","schema":1}
{"type":"npc","world":"NEWWORLD.ZEN","slot_id":0,"symbol_index":123,"script_id":123,"name":"Xardas","pos":[0,0,0],"angle":90}
{"type":"item","world":"NEWWORLD.ZEN","slot_id":0,"symbol_index":456,"name":"ITMI_GOLD","amount":10,"pos":[1,2,3]}
```

JSONL jest latwe do diffowania i mozna je pozniej importowac do Postgres.

## Stable Identity

To bedzie trudna czesc.

Nie zakladaj, ze runtime array index zawsze jest dobrym persistent id.

Dla initial baseline zapisz kilka kandydatow:

- world name
- array slot id
- symbol index
- script instance id
- vob name, jesli istnieje
- waypoint/routine, jesli istnieje
- position rounded
- display name

Potem zbuduj `stable_key` jako hash z kilku pol. Nie usuwaj surowych pol, bo beda potrzebne do debugowania kolizji.

## What to Export First

V1:

- manifest
- NPC: id candidates, name, position, angle, guild, hp/hp_max, alive/dead, waypoint
- Items in world: id candidates, symbol, name, amount, position, transform
- Inventories: owner entity key, item symbol, amount, equipped/slot

V2:

- mobsi/interactives
- containers
- trigger state
- routines
- quest/script globals

V3:

- AI state
- combat state
- perception state
- transient state classification: persistent vs runtime-only

## Validation

Po eksporcie uruchom przynajmniej:

1. Dwa eksporty initial state z czystego startu.
2. Diff JSONL po sortowaniu po `stable_key`.
3. Liczby kontrolne: NPC count, item count, mob count, trigger count.
4. Reczny spot-check kilku znanych NPC, np. Xardas/HERO.

## Do Not

- Nie zapisuj do `logs/*.txt` z losowych miejsc w `Npc::load`.
- Nie rob eksportera, ktory odpala sie przy kazdym normalnym save.
- Nie zmieniaj semantyki save/load tylko po to, zeby latwiej eksportowac.
- Nie traktuj transient AI/combat state jako persistent DB state bez klasyfikacji.
