# MMO tools layout

Root `tools/*.py` files are compatibility launchers only. Put new implementation
code in one of the subdirectories below.

## Active pipeline

- `bootstrap/` - local SQLite capture/baseline tools and destructive MySQL clean
  rebuild tools. This is where the current New Game -> SQLite oracle -> clean
  MySQL bootstrap path lives.
- `production/` - runnable server/worker entrypoints that are still useful for
  live-loop and fallback/debug work. Current gameplay authority should keep
  moving toward the C++ UDP server and typed DB procedures.
- `migration/` - read-model materialization and schema/server-readiness probes.
- `validation/` - checks for the current DB Continue, checkpoint export/parity,
  clean rebuild and live-loop readiness path.
- `_archive/` - old step archaeology. Do not add new dependencies on these
  scripts unless an archived bridge is deliberately being revived.

## Current clean MMO rebuild

The supported destructive local rebuild remains:

```bash
python3 tools/run_mmo_step55_clean_mysql_from_pre_xardas.py \
  --sqlite runtime/g2notr_ch1_pre_xardas.sqlite \
  --mysql-url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --i-understand-this-drops-database
```

`runtime/g2notr_ch1_pre_xardas.sqlite` is a captured New Game zero-point/oracle,
not the MMO production database. To recreate that SQLite file, run the game once
with the runtime SQLite capture flags, for example:

```bash
OpenGothic -mmo-sqlite runtime/g2notr_ch1_pre_xardas.sqlite \
  -mmo-sqlite-capture-pre-start-exit
```

Then validate/copy it into the canonical local baseline with:

```bash
python3 tools/capture_mmo_chapter1_start_sqlite_baseline.py \
  --source runtime/g2notr_ch1_pre_xardas.sqlite \
  --strict \
  --overwrite
```

The project direction is `.sav`-free server-bound MMO play: MySQL/current DB
truth replaces native save state in `-mmo-client-server` flows, while old
single-player behavior stays unchanged without the MMO flag.
