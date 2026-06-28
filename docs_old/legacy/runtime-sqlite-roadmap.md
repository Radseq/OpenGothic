# Runtime SQLite MMO Roadmap

Cel: uzywac SQLite jako lokalnej bazy runtime podczas gry, tak jak miniaturowy serwer MMO.

Na tym etapie nie uzywamy PostgreSQL. SQLite jest jedynym runtime DB targetem.

## Zasada

Normalny save/load Gothica zostaje jako kompatybilny backup i debug path.

Runtime SQLite ma byc osobnym autorytatywnym modelem MMO:

```text
gra dziala
  -> runtime event/state writer
  -> SQLite tables
  -> przy kolejnym starcie odczyt postaci/swiata z SQLite
```

## Phase R0: Runtime DB Bootstrap

Status: pierwszy krok zaimplementowany.

CLI:

```text
-mmo-sqlite <path>
-mmo-sqlite-interval-ms <ms>
-mmo-sqlite-no-restore
-mmo-sqlite-capture-baseline
```

Zakres:

- CMake opcjonalnie wykrywa `SQLite3`.
- Na Windows CMake umie tez wykryc lokalny pakiet w `thirdparty/sqlite`.
- Jesli SQLite3 jest dostepny, gra tworzy runtime DB.
- Jesli SQLite3 nie jest dostepny, build dziala, ale backend jest no-op i loguje brak SQLite.
- Przy starcie gry tworzona jest sesja runtime.
- Co kilka sekund zapisywany jest stan HERO:
  - world name
  - tick
  - position
  - rotation
  - hp/mana
  - level/experience
- Przy starcie mozliwy jest restore pozycji/hp/many z DB dla tego samego swiata.

Aktualne tabele runtime:

- `runtime_schema_meta`
- `runtime_sessions`
- `runtime_characters`
- `runtime_character_history`
- `runtime_character_inventory`
- `runtime_character_inventory_history`
- `runtime_events`
- `runtime_world_npcs`
- `runtime_world_npc_history`
- `runtime_quests`
- `runtime_quest_history`
- `runtime_known_dialogs`
- `runtime_known_dialog_history`
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

Widoki server-facing:

- `v_runtime_character_sheet`
- `v_runtime_character_inventory_totals`
- `v_runtime_character_equipment`
- `v_runtime_world_population`
- `v_runtime_dead_npcs`
- `v_runtime_world_item_totals`
- `v_runtime_container_inventory`
- `v_runtime_interactives`
- `v_runtime_quest_state`
- `v_runtime_dialog_state`
- `v_runtime_script_global_categories`
- `v_runtime_event_counts`
- `v_runtime_persistence_summary`

Lokalny SQLite dla Windows:

```text
thirdparty/sqlite/include/sqlite3.h
thirdparty/sqlite/lib/sqlite3.lib
thirdparty/sqlite/bin/sqlite3.dll
```

Po wykryciu tych plikow CMake linkuje `sqlite3.lib` i kopiuje `sqlite3.dll` obok `Gothic2Notr.exe`.

## Phase R1: Runtime Inventory

Status: current-state i restore inventory/equipment zaimplementowane.

Zapisuje do SQLite:

- character inventory snapshot
- equipment slots
- item class/symbol id
- amount oraz iterator_count
- equipped/equip_count/slot
- podstawowe pola klasyfikacyjne: flags, main_flag, value, spell_id

Jeszcze do zrobienia:

- runtime eventy itemow: pick/drop/use/buy/sell/equip/unequip
- rozdzielenie durable item instance vs stack policy dla przyszlego serwera sieciowego

Odczyt z SQLite przy starcie sesji odtwarza caly snapshot inventory i equipment przez API `Npc`/`Inventory`, a potem naklada canonical stat sheet, zeby bonusy przedmiotow nie dublowaly atrybutow.

Wazne: nie przepisywac jeszcze calego inventory Gothica na sile. Najpierw current-state snapshot + historia + walidacja roznic.

## Phase R2: Runtime Event Journal

Status: pierwszy event journal zaimplementowany.

Aktualnie zapisywane eventy runtime:

- `character_moved`
- `character_hp_changed`
- `character_mana_changed`
- `character_level_changed`
- `character_experience_changed`
- `item_added`
- `item_removed`
- `item_quantity_changed`
- `item_equipped`
- `item_unequipped`

Jeszcze do zrobienia: przejsc z eventow diffowych na semantyczne eventy gameplay tam, gdzie mamy hooki silnika:

- player_position_saved
- item_picked_up
- item_dropped
- item_consumed
- item_equipped
- item_unequipped
- npc_killed
- container_looted
- quest_updated
- dialog_known

Event journal powinien byc mozliwy do replayu do current state. Aktualna wersja jest pierwszym krokiem: eventy sa wyprowadzane z roznicy current-state co flush.

## Phase R3: Persistent World State

Status: pierwszy runtime world-state zaimplementowany.

SQLite runtime utrzymuje teraz:

- current-state wszystkich NPC/mobow w `runtime_world_npcs`
- changed-history NPC/mobow w `runtime_world_npc_history`
- atrybuty/protekcje/talenty, progression (XP threshold/LP), nastawienie oraz kompletne `MISSION[]`/`AIVAR[]` NPC/gracza w `runtime_npc_stats`
- historia zmian atrybutow/protekcji/talentow w `runtime_npc_stat_history`
- produkcyjny katalog statow w `mmo_stat_definitions`
- produkcyjny stat model w `v_mmo_unit_stats`, `v_mmo_unit_stat_sheet`, `v_mmo_character_stat_sheet`, `v_mmo_creature_templates`, `v_mmo_creature_spawns`
- materializowany produkcyjny current-state w `mmo_unit_stat_current`, `mmo_unit_stat_sheet_current`, `mmo_creature_templates_current`, `mmo_creature_spawns_current` od schema `14`
- od schema `15` physical canonical tables dla postaci, inventory, questow, dialogow, itemow swiata, mobsi, inventory kontenerow, globali skryptowych i nastawien gildii
- od schema `16` dokladny clock swiata oraz inventory wszystkich NPC, z osobnym snapshot markerem dla pustych inventory
- od schema `17` canonical checkpoint relacji follow/escort z targetem, `other`/`victim`, stanem Daedalusa i elapsed state time
- od schema `18` immutable `mmo_world_baseline_*`, `mmo_world_templates` i `mmo_world_instances`, z widokami delta dla NPC, itemow, interaktywnych obiektow i globali
- od schema `19` `runtime_npc_stat_capture_state`: dokladny podpis calego komponentu statow per NPC, ktory ogranicza odczyt/zapis 200k EAV rows do NPC faktycznie zmienionych od ostatniego flushu
- aktualne AI state/target/follow relations w `runtime_npc_ai_state`
- historia zmian AI state/target/follow relations w `runtime_npc_ai_history`
- widoki `v_runtime_npc_character_sheet`, `v_runtime_player_stats`, `v_runtime_npc_follow_relations`
- current-state itemow lezacych w swiecie w `runtime_world_items`
- changed-history itemow swiata w `runtime_world_item_history`
- current-state mobsi/interactives w `runtime_world_mobsi`
- changed-history mobsi/interactives w `runtime_world_mobsi_history`
- inventory kontenerow/mobsi w `runtime_world_mobsi_inventory`
- current-state globali Daedalusa w `runtime_script_globals` oraz typowane wartosci tablic w `runtime_script_global_values`
- changed-history globali Daedalusa w `runtime_script_global_history`
- macierz nastawien gildii w `runtime_guild_attitudes`
- lokalny realm/account binding w `runtime_realms`, `runtime_accounts`, `runtime_character_bindings`
- widoki do czytania bazy jak backend MMO, bez recznego skladania raw tabel
- `v_mmo_world_entity_directory` z czytelnym, stabilnym `entity_ref`, krotszym `short_id` i nazwa swiata dla narzedzi; `engine_key` pozostaje kluczem wewnetrznym restore
- kontrakt produkcyjny w widokach `v_mmo_*` od schema `12`
- quest log w `runtime_quests` i `runtime_quest_history`
- znane/wybrane dialogi w `runtime_known_dialogs`
- katalog dialogow ze skryptow w `runtime_dialog_catalog`
- quest lifecycle w `v_runtime_quest_state` i `v_runtime_quest_lifecycle`
- dialog lifecycle w `v_runtime_dialog_state` i `v_runtime_dialog_availability`
- dialog choice timeline w `runtime_dialog_choice_snapshots`, `runtime_dialog_choice_rows` i `v_runtime_dialog_choice_timeline`
- dialog selection timeline w `runtime_dialog_selections` i `v_runtime_dialog_selection_timeline`
- eventy `npc_moved`, `npc_hp_changed`, `npc_killed`, `quest_added`, `quest_status_changed`, `quest_entry_added`, `dialog_known`

Semantyka questow:

- `runtime_quests.status = 1` / `running` / `in_progress`: quest rozpoczety i nadal aktywny.
- `status = 2` / `success` / `completed_success`: quest zakonczony sukcesem.
- `status = 3` / `failed` / `completed_failed`: quest zakonczony porazka.
- `status = 4` / `obsolete`: quest wygaszony/przestarzaly.
- Quest, ktorego nie ma w `runtime_quests`, nie zostal jeszcze dodany do logu postaci.

Semantyka dialogow:

- `runtime_known_dialogs` oznacza dialog wypowiedziany/poznany przez postac.
- `runtime_dialog_catalog.permanent = 0` i `known = 1` w `v_runtime_dialog_state` oznacza `consumed_hidden`: jednorazowy dialog powinien juz zniknac.
- `permanent = 1` i `known = 1` oznacza `repeatable_known`: dialog byl juz wypowiedziany, ale moze dalej wracac.
- `known = 0` oznacza dialog jeszcze niewypowiedziany; jego pokazanie zalezy od `condition_symbol_name` i stanu skryptow.
- `runtime_dialog_choice_snapshots` zapisuje liste opcji realnie pokazanych graczowi w danym ticku.
- `runtime_dialog_choice_rows` zapisuje pojedyncze opcje z takiego snapshotu.
- `runtime_dialog_selections` zapisuje faktycznie wybrane opcje dialogowe; to jest najblizsze MMO event log dla rozmow.

Semantyka escort/follow/target:

- `runtime_npc_ai_state.target_key` zapisuje aktualny target NPC.
- `relation_kind='following_target'` oznacza state z nazwa zawierajaca `follow`.
- `relation_kind='escort_or_guide'` oznacza state z nazwa zawierajaca `escort` albo `guide`.
- State typu `ZS_GUIDE_PLAYER` sa traktowane jako relacja z graczem nawet wtedy, kiedy silnik nie trzyma jawnego `target_key`.
- `relation_kind='talking_to_target'` i `attacking_target` sa rozpoznawane po nazwie AI state.
- Dla questow eskortowych trzeba korelowac `v_runtime_npc_follow_relations` z `runtime_quests`, `runtime_quest_history` i `runtime_script_globals` kategorii `quest`.
- Schema `17` zapisuje tylko bezpieczne relacje `following_target` i `escort_or_guide` w `mmo_creature_relations_current`; przy restore odtwarza referencje i uruchamia zweryfikowany Daedalus state bez kolejki AI/pathfindingu.
- Docelowy serwer nadal potrzebuje wlasnego party/escort objective state, niezaleznego od lokalnego Daedalusa.

Semantyka kontraktu `v_mmo_*`:

- `v_mmo_character_current`, `v_mmo_character_stats`, `v_mmo_character_inventory`, `v_mmo_character_quests`, `v_mmo_character_known_dialogs` to character-scoped state.
- `v_mmo_character_stats` to nadal znormalizowane stat rows, ale z metadanymi z `mmo_stat_definitions`; do normalnego czytania postaci uzywac `v_mmo_character_stat_sheet`.
- `v_mmo_unit_stat_sheet` to szeroki profil unit/creature/character: health/mana, primary attributes, resistances, weapon skills, hit chances i kluczowe talents; normalizowane rows obejmuja tez progression (XP threshold/LP), stale/tymczasowe nastawienie oraz `MISSION[]`/`AIVAR[]` potrzebne przez logike Daedalusa.
- `v_mmo_creature_templates` oddziela content/template NPC od konkretnych spawnów w `v_mmo_creature_spawns`, podobnie do baz MMO.
- Widoki `v_mmo_*` sa publiczna projekcja i narzedzie audytu. Docelowy current-state serwera powinien czytac fizyczne tabele `mmo_*_current`, bo sa stabilne, indeksowane i nie wymagaja przeliczania JOIN/agregacji przy kazdym zapytaniu.
- Tabele `mmo_unit_stat_current` i `mmo_unit_stat_sheet_current` sa materializowane z raw `runtime_npc_stats`/`runtime_world_npcs`; `runtime_npc_stats` pozostaje techniczny import EAV z silnika, nie docelowy model gameplay.
- `v_mmo_world_entities`, `v_mmo_world_items`, `v_mmo_world_interactives`, `v_mmo_world_container_inventory`, `v_mmo_world_script_state` to world/current checkpoint lub persistent delta.
- `v_mmo_waypoint_graph` i `v_mmo_npc_routines` to content/navigation/schedule projection przydatna dla serwera.
- `v_mmo_npc_relations` i `v_mmo_runtime_npc_navigation` to runtime checkpoint/transient diagnostics; fizyczne `mmo_creature_relations_current` obejmuje wylacznie bezpieczny follow/escort restore, a zadna z tych projekcji nie jest docelowa prawda MMO tick po ticku.
- `v_mmo_event_journal` to server-facing event ledger nad `runtime_events`.
- `mmo_world_templates` i `mmo_world_baseline_*` sa niemutowalnym punktem odniesienia contentu; `mmo_world_instances` identyfikuje lokalny shard, a `v_mmo_world_*_deltas` pokazuje durable roznice wzgledem baseline.
- `v_mmo_restore_readiness` pokazuje, ktore obszary sa tylko zbierane, a ktore sa juz wstrzykiwane do silnika przy starcie.
- Restore z DB dla tego samego swiata jest zaimplementowany dla HERO (transform, progression, full stat sheet, inventory/equipment), questow, znanych dialogow, typed globali Daedalusa, nastawien gildii, dokladnego clocka swiata, NPC checkpointow wraz z inventory, relacji follow/escort, itemow swiata wraz z tombstonami, mobsi oraz inventory kontenerow.
- Nie odtwarzamy aktywnych kolejek AI, polsciezki ruchu ani animacji w trakcie ticka. Sa transient runtime state i po starcie AI/routines przejmuja sterowanie w bezpiecznym punkcie.
- Inventory zwyklych NPC jest zapisywane od schema `16`; pierwszy start po migracji tylko je zbiera, a restore zaczyna korzystac z niego przy kolejnym uruchomieniu. To chroni domyslne inventory przed zinterpretowaniem pustej tabeli migracyjnej jako prawdziwego stanu.
- Follow/escort checkpoint jest zapisywany od schema `17`; pierwszy start tylko go zbiera, a restore zaczyna z niego korzystac przy kolejnym uruchomieniu.
- Baseline schema `18` jest tworzony tylko po podaniu `-mmo-sqlite-capture-baseline` w pierwszej sesji nowej DB. Flaga nie capture'uje starego save'a, bo capture jest blokowany, gdy runtime ma juz wiecej niz jedna sesje.
- Schema `19` nie traktuje widokow jako optymalizacji. Jest to zmiana write path: runtime flush nie kasuje i nie buduje od nowa wysokokardynalnych tabel. Stale content jest zapisywany na pelnym bootstrapie, a runtime przeprowadza update/delete tylko dla zmienionych NPC, itemow, interaktywnych obiektow, inventory, globali, AI i nawigacji.
- Przy migracji z schema `14` wartosci globali i nastawienia gildii zaczynaja sie od pierwszego flush schema `15`; pozostale canonical tabele bootstrapuja sie z istniejacego `runtime_*`.

Jeszcze do zrobienia:

- trwała semantyka despawnu/spawnu NPC poza checkpointem aktualnych spawnów
- dynamiczne durable item instances oraz rozroznienie spawn/despawn NPC poza checkpointem
- bezpieczny evaluator widocznosci dialogow po condition functions bez skutkow ubocznych
- klasyfikacja, ktore globale sa server-authoritative, a ktore tylko local/runtime noise
- serwerowy ownership/locking i konfliktowanie zmian wielu graczy

Przy starcie gry trzeba wstrzyknac te zmiany do swiata po zaladowaniu baseline/save.

## Phase R4: Server-like Loop

Dodac tickowy model podobny do serwera:

- runtime session id
- dirty state batching
- periodic checkpoint
- graceful flush on exit/save/world change
- crash recovery from latest committed state

## Phase R5: Replace Save Dependency For MMO Mode

Docelowo w trybie MMO:

- Gothic save moze byc fallbackiem/backupem.
- SQLite runtime DB jest zrodlem prawdy.
- New game/baseline dostarcza template swiata.
- Runtime DB odtwarza character/world deltas.
