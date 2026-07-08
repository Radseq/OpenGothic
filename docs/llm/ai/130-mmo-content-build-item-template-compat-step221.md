# Step221 - content-build item template compatibility

Ten krok naprawia błąd:

```text
ERROR 1146 (42S02): Table 'mmo_content_build.daedalus_item_templates' doesn't exist
```

## Przyczyna

Importer DAT/OU emituje `item_templates`, a
`tools/import_content_build_snapshot_database.py` zapisuje je do
`mmo_content_build.daedalus_item_templates`.

Część lokalnych baz `mmo_content_build` była utworzona starszą wersją Step211,
która miała tabele dla NPC/routines/perception/dialogów, ale nie miała jeszcze
`daedalus_item_templates`.

## Nowe pliki

| Plik | Rola |
|---|---|
| `server/sql/step221_content_build_item_templates_compat.sql` | dodaje brakującą tabelę/widok i health count |
| `tools/apply_content_build_item_templates_compat.py` | wrapper CLI |
| `tools/bootstrap/apply_content_build_item_templates_compat.py` | aplikator SQL |

## Co uruchomić

```bash
tools/apply_content_build_item_templates_compat.py \
  --url "$MYSQL_URL" \
  --output runtime/step221_content_build_item_templates_compat/apply.json

tools/check_mmo_step211_content_build_database.py \
  --url "$MYSQL_URL" \
  --output runtime/step211_content_build_database/check.json
```

Potem nie trzeba ponownie generować snapshotu. Można użyć już istniejącego SQL:

```bash
tools/import_content_build_snapshot_database.py \
  --snapshot runtime/content_build/parser_snapshot.json \
  --url "$MYSQL_URL" \
  --output runtime/content_build/content_build_database_report.json \
  --sql-output runtime/content_build/import_content_build_database.sql
```

Jeżeli chcesz regenerować `runtime/content_build/run_content_build_importer.sh`,
najpierw uruchom ponownie `tools/discover_gothic_content_sources.py`. Samo
podmienienie narzędzi nie nadpisuje starego wygenerowanego skryptu.

