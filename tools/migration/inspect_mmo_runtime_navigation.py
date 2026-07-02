#!/usr/bin/env python3
"""Read-only runtime SQLite navigation/routine/AI relation probe for MMO server planning.

This tool does not make SQLite authoritative. It answers whether the current
OpenGothic runtime capture contains enough waypoint/routine/NPC->NPC context to
bootstrap a future server-side NPC simulation model.
"""
from __future__ import annotations

import argparse
import json
import sqlite3
import sys
from pathlib import Path
from typing import Any

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def quote_identifier(name: str) -> str:
    return '"' + name.replace('"', '""') + '"'


def table_exists(conn: sqlite3.Connection, name: str) -> bool:
    return conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type IN ('table','view') AND name=?",
        (name,),
    ).fetchone() is not None


def columns(conn: sqlite3.Connection, name: str) -> set[str]:
    if not table_exists(conn, name):
        return set()
    return {str(row["name"]) for row in conn.execute(f"PRAGMA table_info({quote_identifier(name)})")}


def scalar(conn: sqlite3.Connection, sql: str, args: tuple[Any, ...] = (), default: Any = 0) -> Any:
    try:
        row = conn.execute(sql, args).fetchone()
    except sqlite3.Error:
        return default
    return default if row is None else row[0]


def rows(conn: sqlite3.Connection, sql: str, args: tuple[Any, ...] = (), limit: int = 20) -> list[dict[str, Any]]:
    try:
        cur = conn.execute(sql, args)
        out = []
        for row in cur.fetchmany(limit):
            out.append({k: row[k] for k in row.keys()})
        return out
    except sqlite3.Error as exc:
        return [{"error": str(exc), "sql": " ".join(sql.split())[:240]}]


def count_table(conn: sqlite3.Connection, name: str) -> int | None:
    if not table_exists(conn, name):
        return None
    return int(scalar(conn, f"SELECT COUNT(*) FROM {quote_identifier(name)}", default=0))


def inspect(conn: sqlite3.Connection, limit: int) -> dict[str, Any]:
    summary: dict[str, Any] = {
        "schema_version": scalar(conn, "SELECT value FROM runtime_schema_meta WHERE key='schema_version'", default="?"),
        "tables": {},
        "waypoints": {},
        "npc_navigation": {},
        "npc_routines": {},
        "npc_relations": {},
        "verdict": {},
    }

    table_names = [
        "runtime_waypoints",
        "runtime_waypoint_edges",
        "runtime_npc_routines",
        "runtime_npc_navigation_state",
        "runtime_npc_navigation_history",
        "runtime_npc_ai_state",
        "runtime_npc_ai_history",
        "runtime_npc_relation_checkpoints",
    ]
    for name in table_names:
        summary["tables"][name] = count_table(conn, name)

    if table_exists(conn, "runtime_waypoints"):
        summary["waypoints"]["by_kind"] = rows(
            conn,
            """
            SELECT world_name, kind, COUNT(*) AS count
              FROM runtime_waypoints
             GROUP BY world_name, kind
             ORDER BY world_name, kind
            """,
            limit=limit,
        )
        summary["waypoints"]["sample"] = rows(
            conn,
            """
            SELECT waypoint_key, world_name, kind, name, pos_x, pos_y, pos_z, connected, use_count
              FROM runtime_waypoints
             ORDER BY world_name, kind, name
             LIMIT ?
            """,
            (limit,),
            limit=limit,
        )

    if table_exists(conn, "runtime_waypoint_edges"):
        summary["waypoints"]["edge_sample"] = rows(
            conn,
            """
            SELECT world_name, from_name, to_name, distance, ladder
              FROM runtime_waypoint_edges
             ORDER BY world_name, from_name, to_name
             LIMIT ?
            """,
            (limit,),
            limit=limit,
        )

    if table_exists(conn, "runtime_npc_navigation_state"):
        nav_cols = columns(conn, "runtime_npc_navigation_state")
        select_cols = [
            "display_name",
            "entity_key",
            "world_name",
            "current_waypoint_name",
            "routine_waypoint_name",
            "move_hint",
            "move_target_waypoint_name",
            "path_next_waypoint_name",
            "tick_count",
        ]
        select_cols = [c for c in select_cols if c in nav_cols]
        summary["npc_navigation"]["non_empty_current_waypoint"] = int(scalar(
            conn,
            "SELECT COUNT(*) FROM runtime_npc_navigation_state WHERE COALESCE(current_waypoint_name,'')<>''",
            default=0,
        )) if "current_waypoint_name" in nav_cols else None
        summary["npc_navigation"]["non_empty_routine_waypoint"] = int(scalar(
            conn,
            "SELECT COUNT(*) FROM runtime_npc_navigation_state WHERE COALESCE(routine_waypoint_name,'')<>''",
            default=0,
        )) if "routine_waypoint_name" in nav_cols else None
        summary["npc_navigation"]["moving_or_targeting"] = int(scalar(
            conn,
            """
            SELECT COUNT(*) FROM runtime_npc_navigation_state
             WHERE COALESCE(move_hint,'')<>''
                OR COALESCE(move_target_waypoint_name,'')<>''
                OR COALESCE(path_next_waypoint_name,'')<>''
            """,
            default=0,
        )) if {"move_hint", "move_target_waypoint_name", "path_next_waypoint_name"} <= nav_cols else None
        if select_cols:
            summary["npc_navigation"]["sample"] = rows(
                conn,
                f"""
                SELECT {', '.join(quote_identifier(c) for c in select_cols)}
                  FROM runtime_npc_navigation_state
                 ORDER BY updated_at DESC, display_name
                 LIMIT ?
                """,
                (limit,),
                limit=limit,
            )

    if table_exists(conn, "runtime_npc_navigation_history"):
        hist_cols = columns(conn, "runtime_npc_navigation_history")
        summary["npc_navigation"]["history_changes"] = int(count_table(conn, "runtime_npc_navigation_history") or 0)
        if "changed_fields" in hist_cols:
            summary["npc_navigation"]["history_sample"] = rows(
                conn,
                """
                SELECT tick_count, display_name, current_waypoint_name, routine_waypoint_name,
                       move_hint, move_target_waypoint_name, path_next_waypoint_name, changed_fields
                  FROM runtime_npc_navigation_history
                 ORDER BY id DESC
                 LIMIT ?
                """,
                (limit,),
                limit=limit,
            )

    if table_exists(conn, "runtime_npc_routines"):
        routine_cols = columns(conn, "runtime_npc_routines")
        summary["npc_routines"]["active_count"] = int(scalar(
            conn,
            "SELECT COUNT(*) FROM runtime_npc_routines WHERE active<>0",
            default=0,
        )) if "active" in routine_cols else None
        summary["npc_routines"]["sample_active"] = rows(
            conn,
            """
            SELECT display_name, entity_key, world_name, routine_index, start_minute, end_minute,
                   callback_symbol_name, waypoint_name, active
              FROM runtime_npc_routines
             WHERE active<>0
             ORDER BY world_name, display_name, start_minute
             LIMIT ?
            """,
            (limit,),
            limit=limit,
        ) if "active" in routine_cols else []
        summary["npc_routines"]["sample_by_waypoint"] = rows(
            conn,
            """
            SELECT waypoint_name, COUNT(*) AS npc_routine_count
              FROM runtime_npc_routines
             WHERE COALESCE(waypoint_name,'')<>''
             GROUP BY waypoint_name
             ORDER BY npc_routine_count DESC, waypoint_name
             LIMIT ?
            """,
            (limit,),
            limit=limit,
        )

    for table in ("runtime_npc_ai_state", "runtime_npc_relation_checkpoints"):
        if not table_exists(conn, table):
            continue
        ai_cols = columns(conn, table)
        relation_exprs = []
        for col in ("target_key", "state_other_key", "state_victim_key", "other_key", "victim_key"):
            if col in ai_cols:
                relation_exprs.append(f"COALESCE({quote_identifier(col)},'')<>");
        non_empty_sql = " OR ".join(expr + "''" for expr in relation_exprs) if relation_exprs else "0"
        summary["npc_relations"][table] = {
            "rows": count_table(conn, table),
            "rows_with_target_or_context": int(scalar(conn, f"SELECT COUNT(*) FROM {quote_identifier(table)} WHERE {non_empty_sql}", default=0)),
            "sample": rows(
                conn,
                f"""
                SELECT {', '.join(quote_identifier(c) for c in [
                    c for c in ('display_name','entity_key','world_name','ai_state_name','relation_kind','target_display_name','target_key','state_other_key','state_victim_key','other_key','victim_key','tick_count')
                    if c in ai_cols
                ])}
                  FROM {quote_identifier(table)}
                 WHERE {non_empty_sql}
                 ORDER BY updated_at DESC
                 LIMIT ?
                """,
                (limit,),
                limit=limit,
            ),
        }

    waypoint_rows = summary["tables"].get("runtime_waypoints") or 0
    nav_rows = summary["tables"].get("runtime_npc_navigation_state") or 0
    routine_rows = summary["tables"].get("runtime_npc_routines") or 0
    relation_rows = 0
    for item in summary["npc_relations"].values():
        if isinstance(item, dict):
            relation_rows += int(item.get("rows_with_target_or_context") or 0)
    summary["verdict"] = {
        "waynet_available_for_server_bootstrap": waypoint_rows > 0,
        "npc_waypoint_context_available": nav_rows > 0 or routine_rows > 0,
        "npc_to_npc_context_available": relation_rows > 0,
        "authoritative_ai_ready": False,
        "note": "read-only evidence only; server still needs deterministic AI/movement ownership before NPCs can be authoritative",
    }
    return summary


def print_summary(report: dict[str, Any]) -> None:
    print(f"schema_version={report['schema_version']}")
    print("tables:")
    for name, count in report["tables"].items():
        print(f"  {name}: {'missing' if count is None else count}")
    print("verdict:")
    for key, value in report["verdict"].items():
        print(f"  {key}: {value}")

    nav = report.get("npc_navigation", {})
    if nav:
        print("npc_navigation:")
        for key in ("non_empty_current_waypoint", "non_empty_routine_waypoint", "moving_or_targeting", "history_changes"):
            if key in nav:
                print(f"  {key}: {nav[key]}")

    routines = report.get("npc_routines", {})
    if routines:
        print("npc_routines:")
        print(f"  active_count: {routines.get('active_count')}")

    relations = report.get("npc_relations", {})
    if relations:
        print("npc_to_npc_context:")
        for table, item in relations.items():
            print(f"  {table}: rows={item.get('rows')} target/context={item.get('rows_with_target_or_context')}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Inspect runtime SQLite waypoint/routine/NPC relation evidence.")
    parser.add_argument("--db", required=True, help="Path to runtime SQLite database, e.g. runtime/g2notr.sqlite")
    parser.add_argument("--output", help="Optional JSON report path")
    parser.add_argument("--limit", type=int, default=20)
    args = parser.parse_args()

    db = Path(args.db)
    if not db.exists():
        print(f"ERROR: DB does not exist: {db}", file=sys.stderr)
        return 2
    conn = sqlite3.connect(db)
    conn.row_factory = sqlite3.Row
    report = inspect(conn, args.limit)
    print_summary(report)
    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"artifact={out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
