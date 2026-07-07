# Step201/202 - server content pack inventory

Ten krok rozszerza poprzedni content manifest gate. Step188 mówił:

- serwer ma własny content pack;
- serwer liczy hash manifestu;
- klient musi zadeklarować ten sam hash;
- serwer odrzuca klienta z inną wersją.

Step201/202 dodaje kolejną warstwę: serwer zaczyna rozumieć, do czego służą
pliki w jego własnym content packu. To jeszcze nie jest pełny parser ZEN/DAT/OU,
ale jest to praktyczny most do parserów.

## Nowe pliki

| Plik | Cel |
|---|---|
| `server/sql/step201_server_content_pack_inventory.sql` | tabela, procedura i widoki inventory |
| `tools/analyze_server_content_pack_inventory.py` | wrapper CLI |
| `tools/bootstrap/analyze_server_content_pack_inventory.py` | generator inventory JSON/SQL |
| `tools/mmo_content_pack_inventory_report.py` | raport lokalnego inventory i opcjonalnego DB health |
| `tools/check_mmo_step201_server_content_pack_inventory.py` | wrapper walidatora |
| `tools/validation/check_mmo_step201_server_content_pack_inventory.py` | walidator DB |

## Nowa tabela

`mmo_server_content_pack_inventory` mapuje każdy plik z
`mmo_server_content_pack_files` na rolę importera.

Najważniejsze kolumny:

| Kolumna | Znaczenie |
|---|---|
| `content_file_id` | plik z manifestu Step188 |
| `content_revision_id` | wersja contentu/moda |
| `logical_path` | znormalizowana ścieżka pliku |
| `file_role` | rola pliku, np. `world_zen`, `scripts_dat`, `dialog_ou` |
| `loader_stage` | etap przyszłego loadera, np. `world_loader`, `script_vm` |
| `import_priority` | kolejność importu |
| `required_for_server_authority` | czy plik jest istotny dla autorytatywnej logiki serwera |
| `import_status` | status przyszłego importera |
| `import_notes` | JSON z powodem klasyfikacji i hashem |

## Role plików

| Rola | Loader | Priorytet | Znaczenie |
|---|---:|---:|---|
| `archive_vdf` | `archive_mount` | 5 | VDF do mount/pre-extract |
| `archive_mod` | `archive_mount` | 5 | MOD do mount/pre-extract |
| `world_zen` | `world_loader` | 10 | świat, VOB-y, waypointy, spawn |
| `scripts_dat` | `script_vm` | 20 | skompilowany Daedalus, NPC, guildy, rutyny, dialogi |
| `dialog_ou` | `dialog_output` | 30 | output units, napisy/audio/dialog output IDs |
| `config_ini` | `server_config` | 60 | konfiguracja content packa |
| `script_source` | `source_reference` | 80 | źródła `.d/.src`, pomocne dla narzędzi |
| `asset_*` | `asset_lookup` | 200+ | assety głównie do walidacji wersji |
| `other` | `unknown` | 1000 | plik znany z hasha, ale jeszcze bez importera |

## Przepływ użycia

1. Wygeneruj Step188 manifest:

```bash
tools/register_server_content_pack_manifest.py \
  --content-root /srv/gothic-mmo/content/ch1 \
  --content-revision-key gothic1-ch1-clean \
  --output runtime/content_manifest/manifest.json \
  --sql-output runtime/content_manifest/register_manifest.sql
```

2. Zastosuj SQL Step201:

```bash
tools/apply_current_mmo_db_state.py \
  --url mysql://user:pass@127.0.0.1:3306/gothic_mmo \
  --skip authority_gap \
  --skip live_receiver
```

W normalnym środowisku docelowo nie trzeba będzie używać `--skip`; tu jest tylko
przykład dla niepełnych paczek roboczych.

3. Wygeneruj inventory z manifestu:

```bash
tools/analyze_server_content_pack_inventory.py \
  --manifest runtime/content_manifest/manifest.json \
  --output runtime/content_inventory/inventory.json \
  --sql-output runtime/content_inventory/register_inventory.sql
```

4. Opcjonalnie od razu zapisz inventory do DB:

```bash
tools/analyze_server_content_pack_inventory.py \
  --manifest runtime/content_manifest/manifest.json \
  --url mysql://user:pass@127.0.0.1:3306/gothic_mmo
```

5. Sprawdź DB:

```bash
tools/check_mmo_step201_server_content_pack_inventory.py \
  --url mysql://user:pass@127.0.0.1:3306/gothic_mmo \
  --expect-world-zen \
  --expect-scripts-dat \
  --expect-dialog-ou
```

6. Wygeneruj raport lokalny/DB:

```bash
tools/mmo_content_pack_inventory_report.py \
  --inventory runtime/content_inventory/inventory.json \
  --url mysql://user:pass@127.0.0.1:3306/gothic_mmo
```

## Dlaczego to jest ważne

Docelowy autorytatywny serwer nie może tylko znać jednego hasha contentu.
Musi wiedzieć:

- które pliki ma mountować jako archiwa;
- które ZEN-y reprezentują światy;
- który DAT zawiera skompilowany Daedalus;
- które OU/bin/csl zawierają dialog output;
- które pliki są tylko assetami klienta;
- które pliki muszą być zaimportowane, zanim NPC/percepcja/dialogi będą działać.

Ten krok daje właśnie tę mapę.

## Co dalej

Następne kroki roadmapy:

1. `server_content_archive_mounts` - zrobione w Step203/204/205: tabela,
   narzędzie i raport opisujące, czy VDF/MOD został rozpakowany lub zamontowany
   po stronie serwera.
2. `server_world_zen_imports` - importer ZEN do DB: światy, waypointy,
   freepointy, startowe VOB-y i podstawowe bounding boxy.
3. `server_daedalus_symbol_index` - indeks symboli DAT: NPC, guildy, C_NPC,
   rutyny, funkcje percepcji, warunki dialogów.
4. `server_dialog_output_index` - OU/output registry: stabilne output IDs,
   napisy, audio refs.
5. `server_npc_perception_policy` - pierwsza autorytatywna warstwa percepcji:
   zasięg, LOS, cooldown, priorytety reakcji.
6. `server_npc_interaction_events` - eventy typu `greet_player`,
   `warn_player`, `start_dialog`, `attack`, wysyłane do wielu klientów.

## Ważna decyzja o bazie

Obecna baza jest etapem przejściowym. Roadmapa zakłada, że baza zostanie później
przepisana/utwardzona: schematy zostaną rozdzielone na content build DB,
runtime MMO DB i event/audit DB. Dzisiejsze tabele są po to, żeby obecny serwer
mógł iść do przodu bez czekania na finalną architekturę storage.
