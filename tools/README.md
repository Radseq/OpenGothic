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

## Server content-build discovery

Before running the ZenKit C++ importer, discover whether the server content root
contains loose ZEN/DAT/OU files or only VDF/MOD archives:

```bash
python3 tools/discover_gothic_content_sources.py \
  --gothic-root "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  --content-revision-key gothic2-notr-steam-local
```

If the generated report is `ready_for_importer`, build
`mmo_content_build_importer` and run
`runtime/content_build/run_content_build_importer.sh`. If it is
`needs_extract_or_vfs_mount`, extract/mount the archives first or extend the
importer with ZenKit VFS support.

For a Steam Gothic II install where world ZEN files are only inside VDF/MOD
archives, build and run the isolated VFS probe/extractor:

```bash
cmake --build build/mmo_cpp_server --target mmo_vdf_world_zen_probe -j

tools/probe_gothic_world_zen_archives.py \
  --gothic-root "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  --world-name newworld.zen \
  --extract
```

Then rerun discovery with the extracted loose ZEN as an additional root:

```bash
tools/discover_gothic_content_sources.py \
  --gothic-root "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  --extra-root runtime/content_build/vfs_extracted \
  --content-revision-key gothic2-notr-steam-local
```

Discovery deliberately ignores non-world `.zen` files such as
`Presets/Lensflare.zen`. A valid world source should normally come from
`Data/Worlds` or `_work/Data/Worlds`.

If DAT/OU import fails with missing `mmo_content_build.daedalus_item_templates`,
apply the compatibility migration:

```bash
python3 tools/apply_content_build_item_templates_compat.py \
  --url "$MYSQL_URL"
```

For a short operator summary:

```bash
python3 tools/mmo_gothic_content_discovery_report.py \
  --discovery runtime/content_build/gothic_content_sources.json
```

## Current clean MMO rebuild

The supported destructive local rebuild remains:

```bash
python3 tools/run_mmo_step55_clean_mysql_from_pre_xardas.py \
  --sqlite runtime/g2notr_ch1_pre_xardas.sqlite \
  --mysql-url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo_ch1_clean" \
  --i-understand-this-drops-database
```

The clean rebuild applies the active bridge SQL surfaces by default, including
`server/sql/step120_npc_authority_restore_bridge.sql` and
`server/sql/step121_server_parity_state_bridge.sql`.

For an already-created DB, use the current-state entrypoint instead of applying
individual StepXX SQL files by hand:

```bash
python3 tools/apply_current_mmo_db_state.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo_ch1_clean" \
  --output runtime/current_mmo_db_state/apply.json
```

The old StepXX SQL files are migration history and debugging anchors. New local
work should target the current-state tool unless a regression needs a specific
old step. Step53 read-model materialization remains a separate Python job and is
still run by the clean rebuild path.

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
