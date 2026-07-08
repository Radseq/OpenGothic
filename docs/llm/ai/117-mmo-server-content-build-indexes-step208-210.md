# Step208/209/210 - server content build indexes

Ten krok dodaje pierwszy docelowy kontrakt zapisu wyników importerów ZEN/DAT/OU.

Uwaga po Step211/212/213: ten kontrakt jest już traktowany jako etap
przejściowy, bo lepszy podział architektoniczny to osobna baza
`mmo_content_build`. Runtime DB nie powinna docelowo trzymać surowego wyniku
parserów contentu; powinna konsumować zatwierdzony content revision albo
wygenerowany read model.

Do tej pory pipeline mówił:

1. serwer ma własny manifest contentu;
2. serwer klasyfikuje pliki;
3. serwer planuje mount/extract archiwów;
4. serwer tworzy import jobs.

Step208/209/210 odpowiada na pytanie: gdzie importer ma zapisać wynik?

## Nowe pliki

| Plik | Cel |
|---|---|
| `server/sql/step208_server_content_build_indexes.sql` | tabele wyników importu contentu |
| `tools/import_server_content_build_snapshot.py` | wrapper CLI |
| `tools/bootstrap/import_server_content_build_snapshot.py` | generator SQL z parser snapshot JSON |
| `tools/check_mmo_step208_server_content_build_indexes.py` | wrapper walidatora |
| `tools/validation/check_mmo_step208_server_content_build_indexes.py` | walidator DB |
| `tools/mmo_content_build_report.py` | raport lokalnego snapshotu i DB health |

## Tabele

| Tabela | Cel |
|---|---|
| `mmo_server_content_build_imports` | metadane importu konkretnego źródła |
| `mmo_server_world_zen_entities` | wynik ZEN: world/waypoint/freepoint/vob/trigger/spawn |
| `mmo_server_daedalus_symbols` | indeks symboli DAT |
| `mmo_server_daedalus_npc_templates` | pierwsza warstwa NPC template z DAT |
| `mmo_server_dialog_outputs` | outputy dialogowe OU |

## Snapshot JSON

To jest kontrakt dla przyszłych parserów. Parser ZEN/DAT/OU nie musi znać SQL.
Ma wypluć JSON w takim kształcie:

```json
{
  "content_revision_key": "gothic1-ch1-clean",
  "sources": {
    "world_zen": {
      "logical_path": "worlds/oldworld/oldworld.zen",
      "sha256": "..."
    },
    "scripts_dat": {
      "logical_path": "scripts/_compiled/gothic.dat",
      "sha256": "..."
    },
    "dialog_ou": {
      "logical_path": "scripts/_compiled/ou.bin",
      "sha256": "..."
    }
  },
  "zen_entities": [
    {
      "world_name": "oldworld",
      "entity_kind": "waypoint",
      "entity_key": "NW_CITY_GATE",
      "name": "NW_CITY_GATE",
      "pos_x": 100.0,
      "pos_y": 0.0,
      "pos_z": 200.0
    }
  ],
  "daedalus_symbols": [
    {
      "symbol_name": "PC_HERO",
      "symbol_kind": "npc_instance",
      "data_type": "C_NPC"
    }
  ],
  "npc_templates": [
    {
      "npc_instance": "VLK_100_GUARD",
      "display_name": "Guard",
      "guild": "GIL_VLK",
      "level": 10,
      "routine_symbol": "Rtn_Start_100",
      "perception_symbol": "B_AssessPlayer"
    }
  ],
  "dialog_outputs": [
    {
      "output_name": "DIA_GUARD_HELLO_00",
      "text": "Stać!",
      "audio_ref": "DIA_GUARD_HELLO_00.WAV",
      "speaker_symbol": "VLK_100_GUARD",
      "target_symbol": "PC_HERO"
    }
  ]
}
```

## Flow

1. Wygeneruj SQL z parser snapshotu:

```bash
tools/import_server_content_build_snapshot.py \
  --snapshot runtime/content_build/parser_snapshot.json \
  --output runtime/content_build/content_build_snapshot_report.json \
  --sql-output runtime/content_build/import_content_build_snapshot.sql
```

2. Opcjonalnie zapisz do DB:

```bash
tools/import_server_content_build_snapshot.py \
  --snapshot runtime/content_build/parser_snapshot.json \
  --url mysql://user:pass@127.0.0.1:3306/gothic_mmo
```

3. Sprawdź DB:

```bash
tools/check_mmo_step208_server_content_build_indexes.py \
  --url mysql://user:pass@127.0.0.1:3306/gothic_mmo \
  --expect-zen \
  --expect-daedalus \
  --expect-dialog-outputs
```

4. Raport:

```bash
tools/mmo_content_build_report.py \
  --report runtime/content_build/content_build_snapshot_report.json \
  --url mysql://user:pass@127.0.0.1:3306/gothic_mmo
```

## Dlaczego to jest duży krok

To jest pierwsza warstwa, która naprawdę zbliża serwer do bycia źródłem prawdy
dla NPC.

Po tym kroku przyszły system NPC może pytać bazę:

- gdzie są waypointy i freepointy;
- jakie VOB-y/interaktywne rzeczy istnieją w świecie;
- jakie NPC template są w DAT;
- jakie symbole percepcji/rutyn/dialogów istnieją;
- jakie outputy dialogowe może odtworzyć klient.

To jeszcze nie wykonuje Daedalusa i nie parsuje binarnego ZEN. Ale daje stabilny
kontrakt, pod który można teraz pisać prawdziwe parsery albo importery etapowe.




