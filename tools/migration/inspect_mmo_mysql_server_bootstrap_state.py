#!/usr/bin/env python3
"""Read-only MySQL server bootstrap/world-state probe.

The goal is to answer the MMO restart question: what can the server materialize
from current projections after restart, and which domains still need canonical
procedures before becoming authoritative.
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
    p = urlparse(url)
    if p.scheme not in {"mysql", "mysql+pymysql"}:
        raise ValueError("expected mysql:// URL")
    db = (p.path or "/").lstrip("/")
    if not db:
        raise ValueError("database is missing in mysql URL")
    return Target(
        host=p.hostname or "127.0.0.1",
        port=p.port or 3306,
        user=unquote(p.username or ""),
        password=unquote(p.password or ""),
        database=db,
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
        "-h", target.host,
        "-P", str(target.port),
        "-u", target.user,
    ]
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
    return proc.stdout.strip()


def sql_literal(value: Any) -> str:
    if value is None:
        return "NULL"
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return repr(value)
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''") + "'"


def table_exists(target: Target, table: str) -> bool:
    out = run_mysql(
        target,
        f"""
        SELECT COUNT(*)
          FROM information_schema.tables
         WHERE table_schema=DATABASE()
           AND table_name={sql_literal(table)};
        """,
    )
    return out.splitlines()[-1].strip() == "1" if out else False


def columns(target: Target, table: str) -> set[str]:
    if not table_exists(target, table):
        return set()
    out = run_mysql(
        target,
        f"""
        SELECT column_name
          FROM information_schema.columns
         WHERE table_schema=DATABASE()
           AND table_name={sql_literal(table)}
         ORDER BY ordinal_position;
        """,
    )
    return {line.strip() for line in out.splitlines() if line.strip()}


def scalar(target: Target, sql: str, default: Any = 0) -> Any:
    try:
        out = run_mysql(target, sql)
    except RuntimeError:
        return default
    if not out:
        return default
    row = out.splitlines()[-1].split("\t")
    return row[0] if row else default


def query_rows(target: Target, sql: str, limit: int = 20) -> list[dict[str, Any]]:
    # The query must return a JSON object per row as its first selected column.
    try:
        out = run_mysql(target, sql)
    except RuntimeError as exc:
        return [{"error": str(exc), "sql": " ".join(sql.split())[:240]}]
    rows: list[dict[str, Any]] = []
    for line in out.splitlines()[:limit]:
        if not line.strip():
            continue
        try:
            obj = json.loads(line)
            if isinstance(obj, dict):
                rows.append(obj)
            else:
                rows.append({"value": obj})
        except json.JSONDecodeError:
            rows.append({"raw": line})
    return rows


def count_table(target: Target, table: str) -> int | None:
    if not table_exists(target, table):
        return None
    return int(scalar(target, f"SELECT COUNT(*) FROM `{table}`;", default=0))


def json_object_sql(pairs: list[tuple[str, str]]) -> str:
    args = []
    for key, expr in pairs:
        args.append(sql_literal(key))
        args.append(expr)
    return "JSON_OBJECT(" + ",".join(args) + ")"


def group_counts(target: Target, table: str, group_cols: list[str], limit: int) -> list[dict[str, Any]]:
    cols = columns(target, table)
    existing = [c for c in group_cols if c in cols]
    if not existing:
        return []
    exprs = [(c, f"COALESCE(CAST(`{c}` AS CHAR),'')") for c in existing]
    exprs.append(("count", "COUNT(*)"))
    group = ", ".join(f"`{c}`" for c in existing)
    order = "COUNT(*) DESC"
    return query_rows(
        target,
        f"""
        SELECT {json_object_sql(exprs)}
          FROM `{table}`
         GROUP BY {group}
         ORDER BY {order}
         LIMIT {int(limit)};
        """,
        limit=limit,
    )


def sample_table(target: Target, table: str, wanted_cols: list[str], limit: int, where: str = "1") -> list[dict[str, Any]]:
    cols = columns(target, table)
    existing = [c for c in wanted_cols if c in cols]
    if not existing:
        return []
    pairs = [(c, f"COALESCE(CAST(`{c}` AS CHAR),'')") for c in existing]
    return query_rows(
        target,
        f"""
        SELECT {json_object_sql(pairs)}
          FROM `{table}`
         WHERE {where}
         LIMIT {int(limit)};
        """,
        limit=limit,
    )


def routine_exists(target: Target, routine: str) -> bool:
    return str(scalar(
        target,
        f"""
        SELECT COUNT(*)
          FROM information_schema.routines
         WHERE routine_schema=DATABASE()
           AND routine_name={sql_literal(routine)};
        """,
        default=0,
    )) == "1"


def inspect(target: Target, character_key: str, limit: int) -> dict[str, Any]:
    report: dict[str, Any] = {
        "database": target.database,
        "character_key": character_key,
        "tables": {},
        "world_bootstrap": {},
        "character_bootstrap": {},
        "procedure_gaps": {},
        "verdict": {},
    }

    table_names = [
        "characters",
        "character_positions",
        "character_stats",
        "character_wallets",
        "character_inventory",
        "character_equipment",
        "character_quests",
        "character_known_dialogs",
        "character_script_state",
        "character_story_progress",
        "world_entity_state",
        "world_inventory",
        "world_script_state",
        "item_instances",
        "world_event_journal",
        "mmo_server_action_outbox",
        "server_sessions",
        "mmo_world_trigger_events",
        "mmo_world_mover_state_current",
        "mmo_npc_weapon_state_current",
        "mmo_world_clock_state_current",
        "mmo_character_resource_state_current",
        "mmo_character_training_state_current",
        "mmo_character_teleport_history",
        "mmo_world_respawn_history",
        "mmo_npc_reaction_history",
    ]
    for table in table_names:
        report["tables"][table] = count_table(target, table)

    if table_exists(target, "world_entity_state"):
        wes_cols = columns(target, "world_entity_state")
        kind_col = "entity_kind" if "entity_kind" in wes_cols else "entity_type" if "entity_type" in wes_cols else None
        group_cols = [c for c in (kind_col, "lifecycle_state", "dead") if c]
        report["world_bootstrap"]["world_entity_state_by_kind"] = group_counts(target, "world_entity_state", group_cols, limit)
        inactive_where_parts = []
        if "lifecycle_state" in wes_cols:
            inactive_where_parts.append("COALESCE(lifecycle_state,'active')<>'active'")
        if "dead" in wes_cols:
            inactive_where_parts.append("COALESCE(dead,0)<>0")
        inactive_where = " OR ".join(inactive_where_parts) if inactive_where_parts else "0"
        report["world_bootstrap"]["inactive_entity_count"] = int(scalar(
            target,
            f"SELECT COUNT(*) FROM world_entity_state WHERE {inactive_where};",
            default=0,
        ))
        report["world_bootstrap"]["entity_sample"] = sample_table(
            target,
            "world_entity_state",
            ["entity_key", "stable_key", "entity_kind", "entity_type", "lifecycle_state", "display_name", "persistent_id", "symbol_index", "health_current", "health_max", "dead", "row_version"],
            limit,
        )

    if table_exists(target, "item_instances"):
        item_cols = columns(target, "item_instances")
        report["world_bootstrap"]["item_instances_by_owner"] = group_counts(
            target,
            "item_instances",
            ["owner_type", "lifecycle_state", "container_scope"],
            limit,
        )
        gone_where_parts = []
        if "lifecycle_state" in item_cols:
            gone_where_parts.append("COALESCE(lifecycle_state,'active')<>'active'")
        if "owner_type" in item_cols:
            gone_where_parts.append("COALESCE(owner_type,'') IN ('character','container','world')")
        report["world_bootstrap"]["item_sample"] = sample_table(
            target,
            "item_instances",
            ["item_instance_key", "source_stable_key", "source_table", "item_display_name", "owner_type", "lifecycle_state", "container_scope", "container_stable_key", "quantity", "iterator_count"],
            limit,
        )

    if table_exists(target, "world_inventory"):
        report["world_bootstrap"]["world_inventory_by_owner"] = group_counts(
            target,
            "world_inventory",
            ["owner_type", "owner_scope", "lifecycle_state"],
            limit,
        )
        report["world_bootstrap"]["world_inventory_sample"] = sample_table(
            target,
            "world_inventory",
            ["owner_key", "owner_stable_key", "owner_display_name", "item_instance_key", "item_display_name", "amount", "iterator_count", "lifecycle_state"],
            limit,
        )

    for table in ("character_stats", "character_positions", "character_inventory", "character_equipment", "character_quests", "character_known_dialogs", "character_script_state", "character_story_progress"):
        if not table_exists(target, table):
            continue
        report["character_bootstrap"][table] = sample_table(
            target,
            table,
            ["character_key", "level", "experience", "experience_next", "learning_points", "hp", "hp_max", "mana", "mana_max", "quest_key", "stable_key", "status", "entry_count", "script_key", "symbol_index", "symbol_name", "chapter_number", "chapter_key", "pos_x", "pos_y", "pos_z"],
            limit,
        )

    required_routines = [
        "mmo_checkpoint_character_state",
        "mmo_pickup_world_item",
        "mmo_take_container_item",
        "mmo_put_container_item",
        "mmo_update_interactive_state",
        "mmo_set_character_script_int",
        "mmo_update_character_quest",
        "mmo_set_character_known_dialog",
        "mmo_adjust_character_progression",
        "mmo_apply_character_experience_reward",
        "mmo_mark_npc_dead",
        "mmo_respawn_npc",
        "mmo_record_trigger_event",
        "mmo_record_mover_state",
        "mmo_record_npc_weapon_state",
        "mmo_record_world_time_changed",
        "mmo_record_character_resource_delta",
        "mmo_spend_learning_points",
        "mmo_change_world_or_teleport_character",
        "mmo_respawn_world_item",
        "mmo_respawn_container_item",
        "mmo_record_npc_reaction_started",
        "mmo_record_npc_dialog_initiated",
    ]
    report["procedure_gaps"] = {name: routine_exists(target, name) for name in required_routines}

    current_tables_ready = bool(report["tables"].get("world_entity_state") and report["tables"].get("item_instances"))
    gap_false = [name for name, exists in report["procedure_gaps"].items() if not exists]
    report["verdict"] = {
        "server_can_materialize_static_world_from_current_projection": current_tables_ready,
        "server_can_be_fully_authoritative_after_restart_today": False,
        "why_not_full_authority_yet": "server runtime materialization + canonical procedures for capture-only domains are still incomplete",
        "important_missing_or_unconfirmed_routines": gap_false,
        "restart_rule": "server should load current projections; clients must not respawn items already moved to character/container/inactive state",
        "respawn_rule": "future respawn should be explicit scheduled events, never a reset to baseline on login",
    }
    return report


def print_summary(report: dict[str, Any]) -> None:
    print(f"database={report['database']} character={report['character_key']}")
    print("tables:")
    for name, count in report["tables"].items():
        print(f"  {name}: {'missing' if count is None else count}")
    print("procedure_gaps:")
    for name, exists in report["procedure_gaps"].items():
        print(f"  {name}: {'ok' if exists else 'missing'}")
    print("verdict:")
    for key, value in report["verdict"].items():
        print(f"  {key}: {value}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Inspect MySQL projection readiness for server restart/bootstrap.")
    parser.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    parser.add_argument("--character-key", default="PC_HERO")
    parser.add_argument("--output", help="Optional JSON report path")
    parser.add_argument("--limit", type=int, default=20)
    args = parser.parse_args()

    target = parse_mysql_url(args.url)
    report = inspect(target, args.character_key, args.limit)
    print_summary(report)
    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"artifact={out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


