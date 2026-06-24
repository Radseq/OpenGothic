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

Status: pierwszy runtime snapshot inventory zaimplementowany.

Zapisuje do SQLite:

- character inventory snapshot
- equipment slots
- item class/symbol id
- amount oraz iterator_count
- equipped/equip_count/slot
- podstawowe pola klasyfikacyjne: flags, main_flag, value, spell_id

Jeszcze do zrobienia:

- runtime eventy itemow: pick/drop/use/buy/sell/equip/unequip
- restore inventory przy starcie sesji
- rozdzielenie durable item instance vs stack policy

Odczytywac z SQLite:

- inventory postaci przy starcie sesji
- equipped items

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
- atrybuty/protekcje/talenty NPC i gracza w `runtime_npc_stats`
- historia zmian atrybutow/protekcji/talentow w `runtime_npc_stat_history`
- aktualne AI state/target/follow relations w `runtime_npc_ai_state`
- historia zmian AI state/target/follow relations w `runtime_npc_ai_history`
- widoki `v_runtime_npc_character_sheet`, `v_runtime_player_stats`, `v_runtime_npc_follow_relations`
- current-state itemow lezacych w swiecie w `runtime_world_items`
- changed-history itemow swiata w `runtime_world_item_history`
- current-state mobsi/interactives w `runtime_world_mobsi`
- changed-history mobsi/interactives w `runtime_world_mobsi_history`
- inventory kontenerow/mobsi w `runtime_world_mobsi_inventory`
- current-state globali Daedalusa w `runtime_script_globals`
- changed-history globali Daedalusa w `runtime_script_global_history`
- lokalny realm/account binding w `runtime_realms`, `runtime_accounts`, `runtime_character_bindings`
- widoki do czytania bazy jak backend MMO, bez recznego skladania raw tabel
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
- `relation_kind='talking_to_target'` i `attacking_target` sa rozpoznawane po nazwie AI state.
- Dla questow eskortowych trzeba korelowac `v_runtime_npc_follow_relations` z `runtime_quests`, `runtime_quest_history` i `runtime_script_globals` kategorii `quest`.
- Jesli HERO ma target NPC, albo NPC ma target HERO, to baza ma juz material do odtworzenia relacji, ale docelowo trzeba dodac dedykowane server-side party/escort objective state.

Jeszcze do zrobienia:

- killed unique NPCs
- restore removed/spawned world items
- restore container inventory
- restore mobsi/interactable state
- bezpieczny evaluator widocznosci dialogow po condition functions bez skutkow ubocznych
- klasyfikacja, ktore globale sa server-authoritative, a ktore tylko local/runtime noise
- world script state subset

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
