#!/usr/bin/env python3
"""Capture a repeatable Gothic II NotR Chapter 1 clean-start SQLite baseline.

The intended capture point is: New Game -> runtime DB initialized -> before the
first Xardas dialog is started/advanced. This tool does not invent game state; it
copies the current runtime SQLite file into a named baseline artifact and writes a
manifest beside it.

SQLite is deliberately used here as a local compatibility oracle / zero-point
file. Production server authority still goes through the server/MySQL path.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sqlite3
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = ROOT / "runtime" / "g2notr.sqlite"
DEFAULT_OUTPUT = ROOT / "runtime" / "baselines" / "g2notr_chapter1_before_xardas.sqlite"
DEFAULT_MANIFEST = ROOT / "runtime" / "baselines" / "g2notr_chapter1_before_xardas.manifest.json"
BASELINE_KEY = "g2notr_chapter1_before_xardas"

CORE_TABLES = (
    "runtime_schema_meta",
    "runtime_sessions",
    "runtime_characters",
    "runtime_world_npcs",
    "runtime_events",
    "mmo_characters_current",
    "mmo_world_items_current",
    "mmo_world_interactives_current",
    "mmo_script_globals_current",
    "mmo_world_baseline_creatures",
    "mmo_world_baseline_items",
)
COUNT_TABLES = (
    "runtime_sessions",
    "runtime_events",
    "runtime_characters",
    "runtime_character_inventory",
    "runtime_world_npcs",
    "runtime_world_items",
    "runtime_world_mobsi",
    "runtime_script_globals",
    "runtime_story_progress_current",
    "runtime_dialog_selections",
    "mmo_characters_current",
    "mmo_character_inventory_current",
    "mmo_character_quests_current",
    "mmo_character_known_dialogs_current",
    "mmo_character_story_progress_current",
    "mmo_world_clock_current",
    "mmo_creature_spawns_current",
    "mmo_world_items_current",
    "mmo_world_interactives_current",
    "mmo_world_container_inventory_current",
    "mmo_script_globals_current",
    "mmo_world_baseline_creatures",
    "mmo_world_baseline_items",
    "mmo_world_baseline_interactives",
)


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return str(path)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def quote_ident(name: str) -> str:
    return '"' + name.replace('"', '""') + '"'


def open_db(path: Path) -> sqlite3.Connection:
    con = sqlite3.connect(str(path))
    con.row_factory = sqlite3.Row
    return con


def tables(con: sqlite3.Connection) -> set[str]:
    return {str(r[0]) for r in con.execute("SELECT name FROM sqlite_master WHERE type='table'")}


def columns(con: sqlite3.Connection, table: str) -> set[str]:
    if table not in tables(con):
        return set()
    return {str(r[1]) for r in con.execute(f"PRAGMA table_info({quote_ident(table)})")}


def scalar(con: sqlite3.Connection, sql: str, params: tuple[Any, ...] = (), default: Any = 0) -> Any:
    row = con.execute(sql, params).fetchone()
    return default if row is None else row[0]


def table_count(con: sqlite3.Connection, table: str) -> int | None:
    if table not in tables(con):
        return None
    return int(scalar(con, f"SELECT COUNT(*) FROM {quote_ident(table)}", default=0))


def schema_version(con: sqlite3.Connection) -> int | None:
    if "runtime_schema_meta" not in tables(con):
        return None
    try:
        value = scalar(con, "SELECT value FROM runtime_schema_meta WHERE key='schema_version'", default=None)
        return None if value is None else int(value)
    except (TypeError, ValueError, sqlite3.Error):
        return None


def story_rows(con: sqlite3.Connection) -> list[dict[str, Any]]:
    for table in ("mmo_character_story_progress_current", "runtime_story_progress_current"):
        if table in tables(con):
            return [dict(r) | {"_source_table": table} for r in con.execute(f"SELECT * FROM {quote_ident(table)}").fetchall()]
    return []


def find_chapter_number(rows: list[dict[str, Any]]) -> int | None:
    for row in rows:
        for key in ("chapter_number", "chapter", "chapter_after"):
            if key in row and row[key] is not None:
                try:
                    return int(row[key])
                except (TypeError, ValueError):
                    pass
        chapter_key = str(row.get("chapter_key") or "").lower()
        if "chapter_1" in chapter_key or chapter_key.endswith("1"):
            return 1
    return None


def xardas_dialog_count(con: sqlite3.Connection) -> int:
    if "runtime_dialog_selections" not in tables(con):
        return 0
    cols = columns(con, "runtime_dialog_selections")
    predicates = []
    for col in ("npc_key", "npc_display_name", "info_symbol_name", "script_function_name", "title"):
        if col in cols:
            predicates.append(f"LOWER(COALESCE({quote_ident(col)}, '')) LIKE '%xardas%'")
    if not predicates:
        return 0
    return int(scalar(con, f"SELECT COUNT(*) FROM runtime_dialog_selections WHERE {' OR '.join(predicates)}", default=0))


def any_dialog_selection_count(con: sqlite3.Connection) -> int:
    return table_count(con, "runtime_dialog_selections") or 0


def pc_hero_present(con: sqlite3.Connection, character_key: str) -> bool:
    for table in ("mmo_characters_current", "runtime_characters"):
        if table not in tables(con):
            continue
        cols = columns(con, table)
        if "character_key" in cols:
            if int(scalar(con, f"SELECT COUNT(*) FROM {quote_ident(table)} WHERE character_key=?", (character_key,), 0)) > 0:
                return True
    return False


def validate(con: sqlite3.Connection, *, character_key: str, strict: bool, allow_later_chapter: bool) -> dict[str, Any]:
    existing_tables = tables(con)
    missing_core = [name for name in CORE_TABLES if name not in existing_tables]
    counts = {name: table_count(con, name) for name in COUNT_TABLES}
    sv = schema_version(con)
    stories = story_rows(con)
    chapter = find_chapter_number(stories)
    xardas_rows = xardas_dialog_count(con)
    dialog_rows = any_dialog_selection_count(con)
    hero = pc_hero_present(con, character_key)

    errors: list[str] = []
    warnings: list[str] = []

    if sv is None:
        errors.append("runtime_schema_meta.schema_version is missing")
    elif sv < 25:
        warnings.append(f"schema_version={sv}; expected recent runtime schema around 25+")

    if not hero:
        errors.append(f"character_key {character_key!r} not found in mmo_characters_current/runtime_characters")

    if chapter is not None and chapter != 1 and not allow_later_chapter:
        errors.append(f"story progress chapter_number={chapter}; expected Chapter 1")
    if chapter is None:
        warnings.append("story chapter could not be proven from current story tables; keeping baseline but mark needs manual verification")

    if xardas_rows > 0:
        errors.append(f"Xardas dialog selections already exist: {xardas_rows}; capture earlier, before starting/advancing Xardas dialog")
    if strict and dialog_rows > 0:
        errors.append(f"dialog selections already exist: {dialog_rows}; strict clean-start capture requires zero dialog selections")

    if missing_core:
        warnings.append("missing non-fatal core tables: " + ", ".join(missing_core))

    return {
        "schema_version": sv,
        "chapter_number_detected": chapter,
        "story_progress_rows": stories[:5],
        "pc_hero_present": hero,
        "xardas_dialog_selection_rows": xardas_rows,
        "dialog_selection_rows": dialog_rows,
        "counts": counts,
        "missing_core_tables": missing_core,
        "warnings": warnings,
        "errors": errors,
        "valid": not errors,
    }


def sqlite_backup(source: Path, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        output.unlink()
    src = sqlite3.connect(str(source))
    try:
        dst = sqlite3.connect(str(output))
        try:
            src.backup(dst)
        finally:
            dst.close()
    finally:
        src.close()


def stamp_baseline(db_path: Path, manifest: dict[str, Any]) -> None:
    con = sqlite3.connect(str(db_path))
    try:
        con.execute("CREATE TABLE IF NOT EXISTS mmo_dev_baseline_meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)")
        rows = {
            "baseline_key": BASELINE_KEY,
            "captured_at": manifest["captured_at"],
            "source_path": manifest["source"]["path"],
            "source_sha256": manifest["source"]["sha256"],
            "manifest_path": manifest["manifest_path"],
            "purpose": "Chapter 1 clean start before Xardas dialog; local dev/server bootstrap baseline",
        }
        con.executemany(
            "INSERT OR REPLACE INTO mmo_dev_baseline_meta(key, value) VALUES(?, ?)",
            sorted(rows.items()),
        )
        con.commit()
    finally:
        con.close()


def main() -> int:
    ap = argparse.ArgumentParser(description="Capture Chapter 1 before-Xardas runtime SQLite baseline.")
    ap.add_argument("--source", type=Path, default=DEFAULT_SOURCE, help="Source runtime SQLite DB. Default: runtime/g2notr.sqlite")
    ap.add_argument("--output", type=Path, default=DEFAULT_OUTPUT, help="Baseline SQLite output path.")
    ap.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST, help="Sidecar manifest JSON path.")
    ap.add_argument("--character-key", default="PC_HERO")
    ap.add_argument("--strict", action="store_true", help="Require zero dialog selections, not only zero Xardas selections.")
    ap.add_argument("--allow-later-chapter", action="store_true", help="Do not fail when story progress is not Chapter 1.")
    ap.add_argument("--overwrite", action="store_true", help="Overwrite existing output baseline and manifest.")
    ap.add_argument("--no-stamp", action="store_true", help="Do not add mmo_dev_baseline_meta table to the copied SQLite baseline.")
    args = ap.parse_args()

    source = args.source if args.source.is_absolute() else ROOT / args.source
    output = args.output if args.output.is_absolute() else ROOT / args.output
    manifest_path = args.manifest if args.manifest.is_absolute() else ROOT / args.manifest

    if not source.exists():
        print(f"ERROR: source SQLite does not exist: {source}", file=sys.stderr)
        return 1
    if (output.exists() or manifest_path.exists()) and not args.overwrite:
        print("ERROR: baseline output/manifest already exists; pass --overwrite to replace", file=sys.stderr)
        print(f"  output={output}", file=sys.stderr)
        print(f"  manifest={manifest_path}", file=sys.stderr)
        return 1

    con = open_db(source)
    try:
        validation = validate(con, character_key=args.character_key, strict=args.strict, allow_later_chapter=args.allow_later_chapter)
    finally:
        con.close()

    if not validation["valid"]:
        print("ERROR: source DB is not an acceptable Chapter 1 before-Xardas baseline", file=sys.stderr)
        for err in validation["errors"]:
            print(f"  - {err}", file=sys.stderr)
        return 1

    src_sha = sha256(source)
    captured_at = datetime.now(timezone.utc).isoformat()
    manifest = {
        "step": 54,
        "baseline_key": BASELINE_KEY,
        "game_code": "g2notr",
        "display_name": "Gothic II NotR - Chapter 1 clean start before Xardas dialog",
        "captured_at": captured_at,
        "source": {"path": rel(source), "sha256": src_sha, "size_bytes": source.stat().st_size},
        "output": {"path": rel(output)},
        "manifest_path": rel(manifest_path),
        "validation": validation,
        "intended_use": [
            "restore runtime/g2notr.sqlite to a known clean point",
            "import this zero-point into a freshly created MySQL dev DB",
            "start client-server feature migration from a deterministic Chapter 1 baseline",
        ],
    }

    sqlite_backup(source, output)
    if not args.no_stamp:
        stamp_baseline(output, manifest)
    out_sha = sha256(output)
    manifest["output"].update({"sha256": out_sha, "size_bytes": output.stat().st_size, "stamped": not args.no_stamp})
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print("Step54 Chapter 1 clean-start baseline captured")
    print(f"baseline={rel(output)}")
    print(f"manifest={rel(manifest_path)}")
    print(f"schema_version={validation['schema_version']}")
    print(f"chapter_number_detected={validation['chapter_number_detected']}")
    print(f"xardas_dialog_selection_rows={validation['xardas_dialog_selection_rows']}")
    print(f"sha256={out_sha}")
    if validation["warnings"]:
        print("warnings:")
        for warning in validation["warnings"]:
            print(f"  - {warning}")
    print("status=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
