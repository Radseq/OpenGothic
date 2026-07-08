# Step206/207 - server content import jobs

Ten krok dokłada kolejkę importów contentu. Po Step188-205 serwer wie:

- jaka jest aktywna rewizja contentu;
- jakie pliki składają się na manifest;
- które pliki są ZEN/DAT/OU/VDF/MOD;
- które archiwa mają być zamontowane albo rozpakowane.

Step206/207 odpowiada na pytanie: co konkretnie ma teraz przetworzyć importer?

## Nowe pliki

| Plik | Cel |
|---|---|
| `server/sql/step206_server_content_import_jobs.sql` | tabela jobów, procedura enqueue i health views |
| `tools/enqueue_server_content_import_jobs.py` | wrapper CLI generatora jobów |
| `tools/bootstrap/enqueue_server_content_import_jobs.py` | generator jobów JSON/SQL |
| `tools/check_mmo_step206_server_content_import_jobs.py` | wrapper walidatora |
| `tools/validation/check_mmo_step206_server_content_import_jobs.py` | walidator DB |
| `tools/mmo_content_import_job_report.py` | raport lokalnego JSON i DB health |

## Tabela

`mmo_server_content_import_jobs` przechowuje durable queue dla content build DB.

Najważniejsze kolumny:

| Kolumna | Znaczenie |
|---|---|
| `importer_key` | np. `zen_world_import`, `daedalus_dat_index`, `dialog_ou_index` |
| `source_kind` | `archive`, `world_zen`, `scripts_dat`, `dialog_ou` |
| `source_logical_path` | plik wejściowy |
| `source_sha256` | hash wejścia |
| `job_priority` | kolejność importu |
| `job_status` | `queued`, `running`, `succeeded`, `failed`, `skipped`, `blocked` |
| `input_payload` | JSON z parametrami importera |
| `output_payload` | JSON na wynik importera |
| `lease_owner`, `lease_until` | miejsce na przyszłego workera |

## Generowane joby

| Źródło | Importer | Priorytet |
|---|---|---:|
| `archive_vdf/archive_mod` z archive plan | `archive_mount_verify` | 5 |
| `world_zen` | `zen_world_import` | 10 |
| `scripts_dat` | `daedalus_dat_index` | 20 |
| `dialog_ou` | `dialog_ou_index` | 30 |

Jeżeli archiwum jest tylko `planned`, job `archive_mount_verify` dostaje status
`blocked`. Jeżeli jest `mounted`, `extracted` albo `verified`, job może być
`queued`.

## Flow

1. Wygeneruj inventory:

```bash
tools/analyze_server_content_pack_inventory.py \
  --manifest runtime/content_manifest/manifest.json \
  --output runtime/content_inventory/inventory.json
```

2. Wygeneruj archive mount plan:

```bash
tools/plan_server_content_archive_mounts.py \
  --inventory runtime/content_inventory/inventory.json \
  --output runtime/archive_mounts/archive_mount_plan.json
```

3. Wygeneruj import jobs:

```bash
tools/enqueue_server_content_import_jobs.py \
  --inventory runtime/content_inventory/inventory.json \
  --archive-plan runtime/archive_mounts/archive_mount_plan.json \
  --output runtime/content_import_jobs/import_jobs.json \
  --sql-output runtime/content_import_jobs/enqueue_import_jobs.sql
```

4. Opcjonalnie zapisz do DB:

```bash
tools/enqueue_server_content_import_jobs.py \
  --inventory runtime/content_inventory/inventory.json \
  --archive-plan runtime/archive_mounts/archive_mount_plan.json \
  --url mysql://user:pass@127.0.0.1:3306/gothic_mmo
```

5. Sprawdź DB:

```bash
tools/check_mmo_step206_server_content_import_jobs.py \
  --url mysql://user:pass@127.0.0.1:3306/gothic_mmo \
  --expect-jobs \
  --fail-on-failed-jobs
```

6. Raport:

```bash
tools/mmo_content_import_job_report.py \
  --jobs runtime/content_import_jobs/import_jobs.json \
  --url mysql://user:pass@127.0.0.1:3306/gothic_mmo
```

## Dlaczego to ważne

To jest pierwszy moment, gdzie roadmapa zmienia się z “mamy dane o plikach” na
“mamy kolejkę pracy dla importerów”.

Dzięki temu następne kroki mogą być niezależnymi workerami:

- worker ZEN bierze job `zen_world_import`;
- worker DAT bierze job `daedalus_dat_index`;
- worker OU bierze job `dialog_ou_index`;
- worker archiwów bierze job `archive_mount_verify`.

Worker po sukcesie zmieni `job_status` na `succeeded` i zapisze wynik w
docelowych tabelach content build DB.




