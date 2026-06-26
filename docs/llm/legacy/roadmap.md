# Roadmap

## Phase 0: Deterministic Initial State

Cel: umiec zlapac stan gry po `New Game` w powtarzalnym punkcie.

Kroki:

1. Dodac tryb uruchomieniowy, np. `--dump-initial-world` albo dev command.
2. Uruchomic normalny flow new game dla Gothic 2 NK.
3. Po `initScripts(true)` i `triggerOnStart(true)` zatrzymac gre w kontrolowanym miejscu.
4. Opcjonalnie wykonac jeden kontrolowany tick, jesli czesc stanu stabilizuje sie dopiero po ticku.
5. Wywolac exporter.
6. Zakonczyc proces albo wrocic do menu.

Output:

- `exports/g2notr/initial/<world>.jsonl`
- `exports/g2notr/initial/manifest.json`

## Phase 1: Structured Exporter

Cel: zastapic debugowe `logs/*.txt` deterministycznym formatem.

Zakres:

- world manifest
- NPC export
- item export
- mob/interactable export
- trigger/routine export
- script globals export w osobnym pliku

Preferowany format na start:

- JSONL dla latwego diffowania,
- albo SQLite staging, jesli od razu potrzebne sa relacje.

Wersja pierwsza moze byc read-only i nie musi jeszcze odtwarzac swiata z DB.

## Phase 2: Baseline vs Delta

Cel: rozdzielic stan poczatkowy od zmian persistent.

Baseline:

- wynik initial export,
- wersjonowany po game target, mod pack, skrypty, save format.

Delta:

- zmiany po starcie serwera,
- rzeczy istotne persistent: smierc NPC, zabrany item, otwarta skrzynia, zmieniony mob state, quest state gracza.

Nie zapisywac w DB wszystkiego co tick. Zapisywac eventy i snapshoty okresowe.

## Phase 3: Import to Database

Cel: zaladowac baseline do bazy.

Startowo:

- PostgreSQL dla danych trwalych,
- opcjonalnie SQLite dla lokalnych testow,
- Redis/in-memory dopiero dla runtime cache.

Wynik:

- migracje SQL,
- importer JSONL -> DB,
- walidator porownujacy liczby NPC/itemow/mobsi z eksportem.

Aktualny stan lokalny:

- `world_staging.sqlite`: staging/replay/debug DB z dumpow.
- `gothic_mmo.sqlite`: pierwsza baza server-shaped z tabelami account/realm/content/characters/world/event journal.
- `db/migrations/postgres/001_gothic_mmo_schema.sql`: pierwszy jawny kontrakt PostgreSQL dla server-shaped DB.
- `tools/check_mmo_database.py`: lokalny smoke test/invariant checker dla `gothic_mmo.sqlite`.
- Nastepny krok Phase 3: importer piszacy bezposrednio do PostgreSQL albo generator seed/import SQL z `gothic_mmo.sqlite`.

## Phase 4: Runtime Server Prototype

Cel: pierwszy autorytatywny serwer.

Minimalny scope:

- jedna mapa,
- logowanie postaci,
- spawn gracza,
- synchronizacja pozycji i animacji graczy,
- zapis pozycji gracza do DB,
- bez pelnego AI NPC.

To ma udowodnic architekture, nie jeszcze pelny Gothic MMO.

## Phase 5: Persistent World Mechanics

Dodawac po kolei:

1. Inventory gracza.
2. Loot ze swiata.
3. Container/mobsi state.
4. NPC alive/dead and respawn rules.
5. Combat.
6. Quest state per player.
7. World events.
8. Admin tools.

## Phase 6: Gothic 2 NK Production Hardening

Wymagane przed produkcja:

- migracje DB z wersjonowaniem,
- test odtworzenia swiata po restartach,
- narzedzia naprawiania stanu,
- snapshoty i backupy,
- telemetry,
- anty-cheat na poziomie autorytatywnego serwera,
- testy load/save/import na wielu save version.

## Phase 7: Gothic 1 and Gothic 2 Vanilla

Dopiero gdy G2 NK pipeline dziala:

- dodac `game_target`,
- dodac mapowanie symboli dla G1/G2,
- rozdzielic baseline per target,
- zrobic kompatybilne importery.

Nie robic abstrakcji zbyt wczesnie. G2 NK najpierw.
