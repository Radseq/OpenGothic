#!/usr/bin/env python3
"""Inspect Step53 physical typed MMO server read-model tables.

This checker is stricter than the older maturity probe for the new read-model
prefix only: no JSON columns, no SQL views, required indexes/tables present and
core tables populated enough for server bootstrap experiments.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlparse

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

READ_MODEL_TABLES = [
    "mmo_server_read_model_meta",
    "mmo_server_character_read_model",
    "mmo_server_character_inventory_read_model",
    "mmo_server_character_quest_read_model",
    "mmo_server_known_dialog_read_model",
    "mmo_server_world_entity_read_model",
    "mmo_server_world_inventory_read_model",
    "mmo_server_interactive_read_model",
    "mmo_server_script_int_read_model",
    "mmo_server_world_clock_read_model",
    "mmo_server_waypoint_read_model",
    "mmo_server_waypoint_edge_read_model",
]
CORE_TABLES = [
    "mmo_server_character_read_model",
    "mmo_server_world_entity_read_model",
    "mmo_server_script_int_read_model",
]


@dataclass(frozen=True)
class Target:
    host: str
    port: int
    user: str
    password: str
    database: str


def parse_mysql_url(url: str) -> Target:
    parsed = urlparse(url)
    if parsed.scheme not in {"mysql", "mysql+pymysql"}:
        raise ValueError("expected mysql:// URL")
    database = (parsed.path or "/").lstrip("/")
    if not database:
        raise ValueError("database is missing in mysql URL")
    return Target(parsed.hostname or "127.0.0.1", parsed.port or 3306, unquote(parsed.username or ""), unquote(parsed.password or ""), database)


def mysql_cmd(target: Target) -> list[str]:
    exe = shutil.which("mysql")
    if exe is None:
        raise RuntimeError("mysql executable was not found in PATH")
    cmd = [exe, "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci", "--batch", "--raw", "--skip-column-names", "-h", target.host, "-P", str(target.port), "-u", target.user]
    if target.password:
        cmd.append(f"-p{target.password}")
    cmd.append(target.database)
    return cmd


def run_mysql(target: Target, sql: str) -> str:
    proc = subprocess.run(mysql_cmd(target), input=sql, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout


def sql_literal(value: Any) -> str:
    if value is None:
        return "NULL"
    return "'" + str(value).replace("\\", "\\\\").replace("'", "''") + "'"


def qident(name: str) -> str:
    return "`" + name.replace("`", "``") + "`"


def scalar_int(target: Target, sql: str) -> int:
    out = run_mysql(target, sql).strip()
    if not out:
        return 0
    return int(out.splitlines()[-1].strip() or "0")


def tsv_rows(target: Target, sql: str) -> list[list[str]]:
    out = run_mysql(target, sql)
    rows: list[list[str]] = []
    for line in out.splitlines():
        if line.strip():
            rows.append(line.split("\t"))
    return rows


def table_exists(target: Target, table: str) -> bool:
    return scalar_int(
        target,
        f"""
        SELECT COUNT(*)
          FROM information_schema.tables
         WHERE table_schema=DATABASE() AND table_name={sql_literal(table)} AND table_type='BASE TABLE';
        """,
    ) == 1


def count_rows(target: Target, table: str) -> int:
    if not table_exists(target, table):
        return -1
    return scalar_int(target, f"SELECT COUNT(*) FROM {qident(table)};")


def indexes_for(target: Target, table: str) -> list[str]:
    if not table_exists(target, table):
        return []
    return [row[0] for row in tsv_rows(target, f"SHOW INDEX FROM {qident(table)};") if row]


def inspect(target: Target, limit: int) -> dict[str, Any]:
    missing_tables = [table for table in READ_MODEL_TABLES if not table_exists(target, table)]
    counts = {table: count_rows(target, table) for table in READ_MODEL_TABLES}
    json_columns = [
        {"table": row[0], "column": row[1], "data_type": row[2], "column_type": row[3]}
        for row in tsv_rows(
            target,
            """
            SELECT table_name, column_name, data_type, column_type
              FROM information_schema.columns
             WHERE table_schema=DATABASE()
               AND table_name LIKE 'mmo_server\\_%\\_read_model'
               AND data_type='json'
             ORDER BY table_name, ordinal_position;
            """,
        )
    ]
    views = [
        row[0]
        for row in tsv_rows(
            target,
            """
            SELECT table_name
              FROM information_schema.tables
             WHERE table_schema=DATABASE()
               AND table_type='VIEW'
               AND table_name LIKE 'mmo_server\\_%'
             ORDER BY table_name;
            """,
        )
    ]
    suspect_payload_columns = [
        {"table": row[0], "column": row[1], "data_type": row[2], "column_type": row[3]}
        for row in tsv_rows(
            target,
            f"""
            SELECT table_name, column_name, data_type, column_type
              FROM information_schema.columns
             WHERE table_schema=DATABASE()
               AND table_name LIKE 'mmo_server\\_%\\_read_model'
               AND (
                    LOWER(column_name) LIKE '%json%'
                 OR LOWER(column_name) LIKE '%payload%'
                 OR LOWER(column_name) LIKE '%metadata%'
                 OR LOWER(column_name) LIKE '%raw%'
               )
             ORDER BY table_name, ordinal_position
             LIMIT {int(limit)};
            """,
        )
    ]
    index_counts = {table: len(set(indexes_for(target, table))) for table in READ_MODEL_TABLES if table_exists(target, table)}

    source_counts: dict[str, int] = {}
    for table in [
        "characters",
        "character_inventory",
        "character_quests",
        "character_known_dialogs",
        "world_entity_state",
        "world_inventory",
        "character_script_state",
        "world_script_state",
        "world_event_journal",
    ]:
        try:
            source_counts[table] = count_rows(target, table)
        except Exception:
            source_counts[table] = -1

    populated_core = {table: counts.get(table, -1) > 0 for table in CORE_TABLES}
    no_json = not json_columns and not suspect_payload_columns
    no_views = not views
    all_tables = not missing_tables
    core_ready = all(populated_core.values())
    ready = bool(all_tables and no_json and no_views and core_ready)

    reasons: list[str] = []
    if missing_tables:
        reasons.append("read-model tables missing: " + ", ".join(missing_tables))
    if json_columns or suspect_payload_columns:
        reasons.append("read-model contains JSON/raw/payload-shaped columns; this prefix must stay typed")
    if views:
        reasons.append("mmo_server_* views exist; server read model must be physical tables only")
    if not core_ready:
        empty = [k for k, ok in populated_core.items() if not ok]
        reasons.append("core server materialization tables are empty: " + ", ".join(empty))

    return {
        "step": 53,
        "database": target.database,
        "status": "passed" if ready else "failed",
        "counts": counts,
        "source_counts": source_counts,
        "index_counts": index_counts,
        "missing_tables": missing_tables,
        "json_columns": json_columns[:limit],
        "suspect_payload_columns": suspect_payload_columns[:limit],
        "views": views[:limit],
        "verdict": {
            "server_read_model_v1_exists": all_tables,
            "server_read_model_v1_has_no_json": no_json,
            "server_read_model_v1_uses_no_views": no_views,
            "server_read_model_v1_core_populated": core_ready,
            "server_read_model_v1_ready_for_bootstrap_experiments": ready,
            "still_final_production_db": False,
            "reason_if_not_ready": reasons,
        },
    }


def print_report(result: dict[str, Any]) -> None:
    print("Step53 server read-model v1 inspection")
    print(f"database={result['database']}")
    print("counts:")
    for table, count in result["counts"].items():
        print(f"  {table}: {count}")
    print("verdict:")
    for key, value in result["verdict"].items():
        if isinstance(value, list):
            print(f"  {key}:")
            for item in value:
                print(f"    - {item}")
        else:
            print(f"  {key}: {value}")
    print("status=" + result["status"])


def main() -> int:
    parser = argparse.ArgumentParser(description="Inspect Step53 physical typed MMO server read-model tables.")
    parser.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    parser.add_argument("--output", help="Optional JSON report path")
    parser.add_argument("--limit", type=int, default=50)
    parser.add_argument("--allow-empty-core", action="store_true", help="Do not fail when core tables are empty; useful right after CREATE only")
    args = parser.parse_args()

    result = inspect(parse_mysql_url(args.url), max(1, args.limit))
    if args.allow_empty_core and result["verdict"]["server_read_model_v1_exists"] and result["verdict"]["server_read_model_v1_has_no_json"] and result["verdict"]["server_read_model_v1_uses_no_views"]:
        result["status"] = "passed"
    print_report(result)
    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"artifact={out}")
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
