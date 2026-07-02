#!/usr/bin/env python3
"""Inspect MMO MySQL schema maturity and surface prototype DB debt.

This is intentionally not a gameplay checker. It answers a different question:
"is the current MySQL schema already a final production MMO database?" The
expected answer today is no. The current DB is a dev/prototype authority bridge:
it proves semantics and replay/projection rules before the real server runtime
owns transactions with a stricter, typed hot schema.
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
    return Target(
        host=parsed.hostname or "127.0.0.1",
        port=parsed.port or 3306,
        user=unquote(parsed.username or ""),
        password=unquote(parsed.password or ""),
        database=database,
    )


def mysql_cmd(target: Target) -> list[str]:
    exe = shutil.which("mysql")
    if exe is None:
        raise RuntimeError("mysql executable was not found in PATH")
    cmd = [
        exe,
        "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci",
        "--batch",
        "--raw",
        "--skip-column-names",
        "-h",
        target.host,
        "-P",
        str(target.port),
        "-u",
        target.user,
    ]
    if target.password:
        cmd.append(f"-p{target.password}")
    cmd.append(target.database)
    return cmd


def run_mysql(target: Target, sql: str) -> str:
    proc = subprocess.run(
        mysql_cmd(target),
        input=sql,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout


def scalar_int(target: Target, sql: str) -> int:
    out = run_mysql(target, sql).strip()
    if not out:
        return 0
    return int(out.splitlines()[-1].strip() or "0")


def tsv_rows(target: Target, sql: str) -> list[list[str]]:
    out = run_mysql(target, sql)
    rows: list[list[str]] = []
    for line in out.splitlines():
        if not line.strip():
            continue
        rows.append(line.split("\t"))
    return rows


def is_evidence_table(table: str) -> bool:
    t = table.lower()
    tokens = (
        "journal",
        "outbox",
        "inbox",
        "audit",
        "log",
        "debug",
        "raw",
        "payload",
        "dispatch",
        "worker_run",
        "server_action",
    )
    return any(token in t for token in tokens)


def classify_json_column(table: str, column: str) -> str:
    t = table.lower()
    c = column.lower()
    if is_evidence_table(t):
        return "evidence_or_ingress_allowed_temporarily"
    if t.endswith("_current") or "projection" in t or "state" in t or c in {"state_json", "raw_payload"}:
        return "hot_path_json_debt"
    return "schema_json_debt"


def rowdict(keys: list[str], row: list[str]) -> dict[str, str]:
    return {key: (row[i] if i < len(row) else "") for i, key in enumerate(keys)}


def inspect(target: Target, limit: int) -> dict[str, Any]:
    base_tables = scalar_int(
        target,
        """
        SELECT COUNT(*)
          FROM information_schema.tables
         WHERE table_schema=DATABASE() AND table_type='BASE TABLE';
        """,
    )
    views = scalar_int(
        target,
        """
        SELECT COUNT(*)
          FROM information_schema.tables
         WHERE table_schema=DATABASE() AND table_type='VIEW';
        """,
    )
    routines = scalar_int(
        target,
        """
        SELECT COUNT(*)
          FROM information_schema.routines
         WHERE routine_schema=DATABASE();
        """,
    )
    mmo_routines = scalar_int(
        target,
        """
        SELECT COUNT(*)
          FROM information_schema.routines
         WHERE routine_schema=DATABASE() AND routine_name LIKE 'mmo_%';
        """,
    )
    triggers = scalar_int(
        target,
        """
        SELECT COUNT(*)
          FROM information_schema.triggers
         WHERE trigger_schema=DATABASE();
        """,
    )
    server_read_model_tables = scalar_int(
        target,
        """
        SELECT COUNT(*)
          FROM information_schema.tables
         WHERE table_schema=DATABASE()
           AND table_type='BASE TABLE'
           AND table_name LIKE 'mmo_server\\_%\\_read_model';
        """,
    )
    server_read_model_json_columns = scalar_int(
        target,
        """
        SELECT COUNT(*)
          FROM information_schema.columns
         WHERE table_schema=DATABASE()
           AND table_name LIKE 'mmo_server\\_%\\_read_model'
           AND data_type='json';
        """,
    )
    server_read_model_views = scalar_int(
        target,
        """
        SELECT COUNT(*)
          FROM information_schema.tables
         WHERE table_schema=DATABASE()
           AND table_type='VIEW'
           AND table_name LIKE 'mmo_server\\_%';
        """,
    )

    json_keys = ["table", "column", "data_type", "column_type", "nullable", "key"]
    json_columns = [
        rowdict(json_keys, row)
        for row in tsv_rows(
            target,
            """
            SELECT table_name, column_name, data_type, column_type, is_nullable, column_key
              FROM information_schema.columns
             WHERE table_schema=DATABASE() AND data_type='json'
             ORDER BY table_name, ordinal_position;
            """,
        )
    ]
    for item in json_columns:
        item["classification"] = classify_json_column(item["table"], item["column"])

    suspect_keys = ["table", "column", "data_type", "column_type", "nullable", "key"]
    suspect_json_text_columns = [
        rowdict(suspect_keys, row)
        for row in tsv_rows(
            target,
            """
            SELECT table_name, column_name, data_type, column_type, is_nullable, column_key
              FROM information_schema.columns
             WHERE table_schema=DATABASE()
               AND data_type IN ('text','mediumtext','longtext','varchar')
               AND (
                    LOWER(column_name) LIKE '%json%'
                 OR LOWER(column_name) LIKE '%payload%'
                 OR LOWER(column_name) LIKE '%metadata%'
                 OR LOWER(column_name) LIKE '%details%'
                 OR LOWER(column_name) LIKE '%state%'
               )
             ORDER BY table_name, ordinal_position
             LIMIT 500;
            """,
        )
    ]
    for item in suspect_json_text_columns:
        item["classification"] = classify_json_column(item["table"], item["column"])

    view_rows = [
        rowdict(["view"], row)
        for row in tsv_rows(
            target,
            f"""
            SELECT table_name
              FROM information_schema.tables
             WHERE table_schema=DATABASE() AND table_type='VIEW'
             ORDER BY table_name
             LIMIT {int(limit)};
            """,
        )
    ]
    routine_rows = [
        rowdict(["routine", "type"], row)
        for row in tsv_rows(
            target,
            f"""
            SELECT routine_name, routine_type
              FROM information_schema.routines
             WHERE routine_schema=DATABASE()
             ORDER BY routine_name
             LIMIT {int(limit)};
            """,
        )
    ]
    widest_tables = [
        rowdict(["table", "columns"], row)
        for row in tsv_rows(
            target,
            f"""
            SELECT table_name, COUNT(*) AS column_count
              FROM information_schema.columns
             WHERE table_schema=DATABASE()
             GROUP BY table_name
             ORDER BY column_count DESC, table_name
             LIMIT {int(limit)};
            """,
        )
    ]

    hot_json = [item for item in json_columns + suspect_json_text_columns if item.get("classification") == "hot_path_json_debt"]
    schema_json = [item for item in json_columns + suspect_json_text_columns if item.get("classification") == "schema_json_debt"]
    evidence_json = [item for item in json_columns + suspect_json_text_columns if item.get("classification") == "evidence_or_ingress_allowed_temporarily"]

    procedure_debt = routines > 12 or mmo_routines > 12
    view_debt = views > 0
    json_debt = bool(hot_json or schema_json)

    hard_reasons: list[str] = []
    if json_debt:
        hard_reasons.append("JSON/text payload columns exist outside strict evidence/ingress surfaces")
    if view_debt:
        hard_reasons.append("SQL views exist; runtime server must not depend on views")
    if procedure_debt:
        hard_reasons.append("stored procedure surface is large; most procedures are temporary authority-bridge scaffolding")
    hard_reasons.append("server runtime materialization/replication/AI ownership is not implemented yet")

    return {
        "step": 52,
        "database": target.database,
        "status": "schema_debt_documented",
        "counts": {
            "base_tables": base_tables,
            "views": views,
            "routines": routines,
            "mmo_routines": mmo_routines,
            "triggers": triggers,
            "server_read_model_tables": server_read_model_tables,
            "server_read_model_json_columns": server_read_model_json_columns,
            "server_read_model_views": server_read_model_views,
            "json_columns": len(json_columns),
            "suspect_json_text_columns": len(suspect_json_text_columns),
            "hot_path_json_debt_columns": len(hot_json),
            "schema_json_debt_columns": len(schema_json),
            "evidence_or_ingress_json_columns": len(evidence_json),
        },
        "samples": {
            "json_columns": json_columns[:limit],
            "suspect_json_text_columns": suspect_json_text_columns[:limit],
            "views": view_rows,
            "routines": routine_rows,
            "widest_tables": widest_tables,
        },
        "verdict": {
            "current_db_is_final_production_mmo_schema": False,
            "current_db_is_dev_authority_bridge": True,
            "json_debt": json_debt,
            "view_debt": view_debt,
            "procedure_debt": procedure_debt,
            "production_server_db_rule": "hot gameplay projections must be typed/indexed columns; JSON is only for raw ingress/debug/audit and must not be used for authoritative resolver joins",
            "procedure_rule": "stored procedures are acceptable as transitional DB-side contracts; the future C++/server runtime should delete or collapse most of them once deterministic server transactions exist",
            "view_rule": "runtime path must not depend on SQL views; use physical read models/materialized tables or server-owned memory snapshots",
            "step53_read_model_progress": {
                "physical_server_read_model_tables": server_read_model_tables,
                "server_read_model_json_columns": server_read_model_json_columns,
                "server_read_model_views": server_read_model_views,
                "meaning": "positive mitigation only; the old bridge debt still exists until hot server code uses typed read models and legacy views/JSON/procedure sprawl are removed",
            },
            "why_not_production": hard_reasons,
        },
        "future_target": {
            "hot_path_json_columns": 0,
            "runtime_views": 0,
            "procedure_surface": "small, audited, or replaced by server-owned transaction code",
            "db_role": "durable state/event storage + indexed projections, not gameplay brain",
            "server_role": "authoritative validation, AI, movement, replication, transactions",
        },
    }


def print_report(result: dict[str, Any]) -> None:
    counts = result["counts"]
    verdict = result["verdict"]
    print("Step52 MySQL schema maturity")
    print(f"database={result['database']}")
    print("counts:")
    for key, value in counts.items():
        print(f"  {key}: {value}")
    print("verdict:")
    print(f"  current_db_is_final_production_mmo_schema: {verdict['current_db_is_final_production_mmo_schema']}")
    print(f"  current_db_is_dev_authority_bridge: {verdict['current_db_is_dev_authority_bridge']}")
    print(f"  json_debt: {verdict['json_debt']}")
    print(f"  view_debt: {verdict['view_debt']}")
    print(f"  procedure_debt: {verdict['procedure_debt']}")
    progress = verdict.get('step53_read_model_progress', {})
    if progress:
        print("  step53_read_model_progress:")
        print(f"    physical_server_read_model_tables: {progress.get('physical_server_read_model_tables')}")
        print(f"    server_read_model_json_columns: {progress.get('server_read_model_json_columns')}")
        print(f"    server_read_model_views: {progress.get('server_read_model_views')}")
    print("  why_not_production:")
    for reason in verdict["why_not_production"]:
        print(f"    - {reason}")
    samples = result["samples"]
    if samples["json_columns"]:
        print("json_columns_sample:")
        for item in samples["json_columns"][:10]:
            print(f"  {item['table']}.{item['column']} {item['data_type']} [{item['classification']}]")
    if samples["suspect_json_text_columns"]:
        print("suspect_text_payload_columns_sample:")
        for item in samples["suspect_json_text_columns"][:10]:
            print(f"  {item['table']}.{item['column']} {item['data_type']} [{item['classification']}]")


def main() -> int:
    parser = argparse.ArgumentParser(description="Inspect whether the current MySQL schema is production-MMO mature.")
    parser.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    parser.add_argument("--output", help="Optional JSON report path")
    parser.add_argument("--limit", type=int, default=50, help="Rows to include per sample list")
    parser.add_argument("--strict", action="store_true", help="Return non-zero when production schema debt is detected")
    args = parser.parse_args()

    target = parse_mysql_url(args.url)
    result = inspect(target, max(1, args.limit))
    print_report(result)

    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"artifact={out}")
    print("status=" + str(result["status"]))
    if args.strict and not result["verdict"]["current_db_is_final_production_mmo_schema"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


