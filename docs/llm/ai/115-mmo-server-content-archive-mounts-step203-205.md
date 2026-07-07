# Step203/204/205 - server content archive mounts

Ten krok realizuje następną część roadmapy po `content_pack_inventory`.

Step201/202 mówiły: serwer widzi, które pliki w content packu są archiwami
`VDF/MOD`, ZEN-ami, DAT-em i OU. Step203/204/205 dodaje warstwę operacyjną:
co serwer ma zrobić z archiwami, zanim przyszłe parsery zaczną czytać ZEN/DAT/OU.

## Po co to jest

Serwer nie powinien zgadywać ścieżek typu:

- `Data/Worlds.vdf`;
- `Data/Scripts.vdf`;
- `Data/modname.mod`;
- ręcznie rozpakowany folder admina.

Zamiast tego baza ma jawnie mówić:

- które archiwum należy do aktywnej rewizji contentu;
- czy archiwum jest tylko zaplanowane;
- czy jest zamontowane;
- czy jest rozpakowane;
- jaki katalog reprezentuje wynik rozpakowania;
- ile plików znaleziono po extract;
- jaki jest hash manifestu rozpakowanych plików.

## Nowe pliki

| Plik | Cel |
|---|---|
| `server/sql/step203_server_content_archive_mounts.sql` | tabela mountów archiwów, extracted files, widoki i procedury |
| `tools/plan_server_content_archive_mounts.py` | wrapper CLI planera |
| `tools/bootstrap/plan_server_content_archive_mounts.py` | generator planu JSON/SQL |
| `tools/check_mmo_step203_server_content_archive_mounts.py` | wrapper walidatora |
| `tools/validation/check_mmo_step203_server_content_archive_mounts.py` | walidator DB |
| `tools/mmo_content_archive_mount_report.py` | raport lokalnego planu i DB health |

## Nowe tabele

### `mmo_server_content_archive_mounts`

Jedna pozycja na archiwum VDF/MOD.

| Kolumna | Znaczenie |
|---|---|
| `content_file_id` | plik archiwum z manifestu Step188 |
| `content_revision_id` | rewizja contentu |
| `archive_logical_path` | np. `data/worlds.vdf` |
| `archive_role` | `archive_vdf` albo `archive_mod` |
| `mount_strategy` | `pre_extracted`, `read_direct`, `extract_on_boot`, `external_mount`, `manual` |
| `mount_status` | `planned`, `mounted`, `extracted`, `verified`, `failed`, `ignored` |
| `extracted_root_label` | stabilna etykieta katalogu po extract |
| `extracted_file_count` | liczba plików po extract |
| `extracted_manifest_hash` | hash deterministycznej listy extracted files |

### `mmo_server_content_extracted_files`

Opcjonalna lista plików znalezionych po rozpakowaniu archiwum.

Ta tabela jest ważna później, bo importer ZEN/DAT/OU może dostać stabilny katalog
wejściowy i hash każdego pliku bez ufania klientowi.

## Procedury

| Procedura | Cel |
|---|---|
| `mmo_upsert_server_content_archive_mount(...)` | zapisuje/aktualizuje plan mount/extract archiwum |
| `mmo_upsert_server_content_extracted_file(...)` | zapisuje plik znaleziony po extract |

## Widoki

| Widok | Cel |
|---|---|
| `v_mmo_server_content_archive_mounts` | czytelny widok mountów archiwów |
| `v_mmo_server_content_extracted_files` | czytelny widok extracted files |
| `v_mmo_server_content_archive_health` | health aktywnych/nieaktywnych rewizji |

## Przykładowy flow

1. Wygeneruj inventory z manifestu Step188:

```bash
tools/analyze_server_content_pack_inventory.py \
  --manifest runtime/content_manifest/manifest.json \
  --output runtime/content_inventory/inventory.json
```

2. Zaplanuj archiwa bez extract:

```bash
tools/plan_server_content_archive_mounts.py \
  --inventory runtime/content_inventory/inventory.json \
  --output runtime/archive_mounts/archive_mount_plan.json \
  --sql-output runtime/archive_mounts/register_archive_mounts.sql
```

3. Zaplanuj archiwa z pre-extracted folderem:

```bash
tools/plan_server_content_archive_mounts.py \
  --inventory runtime/content_inventory/inventory.json \
  --extracted-root /srv/gothic-mmo/content/ch1-extracted \
  --extracted-archive-logical-path data/worlds.vdf \
  --extracted-root-label ch1-extracted-2026-07-06 \
  --output runtime/archive_mounts/archive_mount_plan.json \
  --sql-output runtime/archive_mounts/register_archive_mounts.sql
```

4. Opcjonalnie zapisz od razu do DB:

```bash
tools/plan_server_content_archive_mounts.py \
  --inventory runtime/content_inventory/inventory.json \
  --url mysql://user:pass@127.0.0.1:3306/gothic_mmo
```

5. Sprawdź DB:

```bash
tools/check_mmo_step203_server_content_archive_mounts.py \
  --url mysql://user:pass@127.0.0.1:3306/gothic_mmo \
  --fail-on-missing-mount
```

6. Wygeneruj raport:

```bash
tools/mmo_content_archive_mount_report.py \
  --plan runtime/archive_mounts/archive_mount_plan.json \
  --url mysql://user:pass@127.0.0.1:3306/gothic_mmo
```

## Co dalej

Po tym kroku parsery nie muszą już zastanawiać się, skąd wziąć pliki.

Następne logiczne kroki:

1. `server_world_zen_imports` - importer ZEN czyta `world_zen` z inventory
   albo pliki znalezione w pre-extracted root.
2. `server_daedalus_symbol_index` - importer DAT czyta `scripts_dat`.
3. `server_dialog_output_index` - importer OU czyta `dialog_ou`.
4. `server_content_import_jobs` - kolejka importów content build DB.

Ten etap nadal nie implementuje parsera VDF/MOD. Celowo. Najpierw zapisujemy
kontrakt operacyjny, potem można podmienić `manual/pre_extracted` na realny
reader archiwów bez zmiany dalszych warstw.
