# Step211/212/213 - separate content-build database

Ten krok robi właściwy split bazy pod przyszłe autorytatywne czytanie plików
Gothica przez serwer.

Do tej pory Step208 dawał indeksy content build w runtime DB. To było przydatne
jako przejściowy kontrakt, ale mieszało dwa różne cykle życia:

- runtime DB: gracze, sesje, aktywne instancje świata, eventy, durable gameplay;
- content-build DB: wynik parsowania ZEN/DAT/OU z serwerowej kopii contentu.

Od tego kroku parsery powinny pisać do osobnej bazy `mmo_content_build`.

## Nowe pliki

| Plik | Cel |
|---|---|
| `server/sql/step211_content_build_database.sql` | tworzy schemat `mmo_content_build` |
| `tools/apply_content_build_database.py` | wrapper aplikatora SQL |
| `tools/bootstrap/apply_content_build_database.py` | właściwy aplikator bazy buildowej |
| `tools/import_content_build_snapshot_database.py` | wrapper importera snapshotu |
| `tools/bootstrap/import_content_build_snapshot_database.py` | generator/aplikator SQL z parser snapshot JSON |
| `tools/check_mmo_step211_content_build_database.py` | wrapper walidatora |
| `tools/validation/check_mmo_step211_content_build_database.py` | walidator DB |
| `tools/mmo_content_build_database_report.py` | raport lokalnego importu i stanu DB |

## Dlaczego osobna baza

Serwer powinien mieć własną kopię content packa, ale content pack nie jest
stanem aktywnej rozgrywki. ZEN/DAT/OU są wejściem do builda. Wynik builda może
być cache'owany, walidowany, odrzucany, porównywany i publikowany bez dotykania
aktywnych graczy.

To daje czystszy model:

| Obszar | Odpowiedzialność |
|---|---|
| `mmo_content_build` | parsery, indeksy contentu, health builda, eksporty |
| runtime MMO DB | sesje, postacie, active NPC, world instances, event journal |
| cache/read model | szybkie struktury do ticka NPC/perception/pathing |

## Tabele Step211

| Tabela | Co przechowuje |
|---|---|
| `content_build_schema_versions` | marker migracji bazy buildowej |
| `content_build_revisions` | rewizje contentu, status builda, manifest hash |
| `content_build_files` | pliki wejściowe: ZEN/DAT/OU/VDF/MOD i role |
| `content_build_imports` | pojedyncze importy parserów |
| `content_build_parser_errors` | ostrzeżenia/błędy parserów |
| `world_zen_entities` | world/waypoint/freepoint/vob/trigger/mover/spawn |
| `world_waypoint_edges` | graf waypointów pod przyszłe pathing/rutyny |
| `daedalus_symbols` | indeks symboli DAT |
| `daedalus_npc_templates` | NPC templates z DAT |
| `daedalus_routines` | rutyny NPC z Daedalusa |
| `daedalus_perception_bindings` | przypięcia percepcji do funkcji |
| `dialog_outputs` | OU outputy: tekst/audio/speaker/target |
| `dialog_infos` | informacje dialogowe i symbole condition/information |
| `content_build_runtime_exports` | przyszłe publikacje builda do runtime DB/read modelu |

## Minimalny flow

1. Utwórz/zaaktualizuj bazę buildową:

```bash
tools/apply_content_build_database.py \
  --url mysql://user:pass@127.0.0.1:3306/gothic_mmo
```

Ten URL wskazuje istniejącą bazę do połączenia z MySQL. SQL sam tworzy
`mmo_content_build`.

2. Wygeneruj SQL z parser snapshotu:

```bash
tools/import_content_build_snapshot_database.py \
  --snapshot runtime/content_build/parser_snapshot.json \
  --output runtime/content_build/content_build_database_report.json \
  --sql-output runtime/content_build/import_content_build_database.sql
```

3. Albo od razu zapisz snapshot do bazy:

```bash
tools/import_content_build_snapshot_database.py \
  --snapshot runtime/content_build/parser_snapshot.json \
  --url mysql://user:pass@127.0.0.1:3306/gothic_mmo
```

4. Sprawdź bazę:

```bash
tools/check_mmo_step211_content_build_database.py \
  --url mysql://user:pass@127.0.0.1:3306/gothic_mmo \
  --expect-zen \
  --expect-daedalus \
  --expect-dialog-outputs
```

5. Zrób raport:

```bash
tools/mmo_content_build_database_report.py \
  --report runtime/content_build/content_build_database_report.json \
  --url mysql://user:pass@127.0.0.1:3306/gothic_mmo
```

## Kontrakt snapshotu parsera

Obecny importer rozumie bazowy format Step208 oraz rozszerzenia potrzebne do
NPC AI:

- `sources.world_zen`, `sources.scripts_dat`, `sources.dialog_ou`;
- `zen_entities`;
- `waypoint_edges`;
- `daedalus_symbols`;
- `npc_templates`;
- `routines`;
- `perception_bindings`;
- `dialog_infos`;
- `dialog_outputs`.

To nadal nie jest pełny parser ZEN/DAT/OU. To jest stabilne miejsce zapisu
wyniku parserów. Następny techniczny krok to realny importer, który z serwerowej
kopii contentu zacznie produkować taki snapshot.

## Relacja do klienta

Klient nie wysyła plików i nie jest źródłem prawdy. Klient może wysłać hash
manifestu. Serwer porównuje go z aktywną rewizją contentu, którą sam zbudował
albo zatwierdził w `mmo_content_build`.

W przyszłości flow powinien wyglądać tak:

1. serwer ma content pack;
2. parsery czytają ZEN/DAT/OU z serwerowej kopii;
3. parsery zapisują wynik do `mmo_content_build`;
4. build przechodzi health/check;
5. gotowy revision jest publikowany do runtime DB/read modelu;
6. NPC perception/routine/dialog tick używa tego modelu dla wielu graczy;
7. klient dostaje tylko wynik decyzji: obrót NPC, podejście, dialog, audio,
   napisy, walka albo ignore.

## Status procentowy LLM roadmapy

Ten krok domyka fundament bazy buildowej. Sama roadmapa content-authority jest
teraz mniej więcej w 35-40% ogarnięta na poziomie infrastruktury:

- handshake hash/content identity: zrobione;
- manifest/inventory/archive planning: zrobione;
- import job queue: zrobione;
- osobna baza buildowa: zrobione;
- kontrakt snapshotu parserów: zrobione;
- realny parser ZEN/DAT/OU: jeszcze nie;
- publikacja builda do runtime read modelu: jeszcze nie;
- serwerowy NPC perception/routine tick dla wielu graczy: jeszcze nie;
- pełne zastąpienie obecnej bazy docelową architekturą: jeszcze nie.

Docelowa baza runtime nadal powinna zostać kiedyś przepisana/utwardzona. Ten
krok robi do tego przygotowanie: oddziela content build od stanu MMO, dzięki
czemu późniejszy rewrite runtime DB będzie mniej ryzykowny.




