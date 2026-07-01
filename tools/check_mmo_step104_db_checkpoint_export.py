"""Audit Step104 DB save checkpoint full script-state export coverage and live parity."""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import unquote, urlparse

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

REQUIRED_ROUTINES = (
    "mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1",
    "mmo_validate_latest_save_checkpoint_restore_v1",
    "mmo_assert_latest_save_checkpoint_restore_v1",
)

REQUIRED_VIEWS = (
    "v_mmo_latest_save_checkpoint_strict_restore",
    "v_mmo_save_checkpoint_snapshot_domain_counts",
)

MYSQL_SESSION_PREAMBLE = (
    "SET SESSION group_concat_max_len=104857600;\n"
    "SET SESSION max_execution_time=0;\n"
)

COUNT_DRIFT_EXCLUDED_DOMAINS = {"script_state"}

EXPORT_COVERAGE_DOMAINS = {
    "inventory": "inventory",
    "equipment": "equipment",
    "quests": "quests",
    "known_dialogs": "known_dialogs",
    "script_state": "script_state_full",
    "world_items_removed": "world_item_deltas",
    "interactives": "interactive_state",
    "npc_lifecycle_non_active": "npc_lifecycle_state",
    "movers": "mover_state",
}

SNAPSHOT_TABLES = (
    "mmo_save_checkpoint_character_snapshot",
    "mmo_save_checkpoint_inventory_snapshot",
    "mmo_save_checkpoint_equipment_snapshot",
    "mmo_save_checkpoint_quest_snapshot",
    "mmo_save_checkpoint_known_dialog_snapshot",
    "mmo_save_checkpoint_script_state_snapshot",
    "mmo_save_checkpoint_world_entity_snapshot",
    "mmo_save_checkpoint_world_inventory_snapshot",
    "mmo_save_checkpoint_world_clock_snapshot",
    "mmo_save_checkpoint_mover_snapshot",
)


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
        "--default-character-set=utf8mb4",
        "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci",
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
        input=MYSQL_SESSION_PREAMBLE + sql,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.stderr:
        print(proc.stderr, file=sys.stderr, end="")
    if proc.returncode != 0:
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def sql_literal(value: str) -> str:
    return "'" + value.replace("\\", "\\\\").replace("'", "''") + "'"


def split_rows(raw: str) -> list[list[str]]:
    return [line.split("\t") for line in raw.splitlines() if line.strip()]


def int_value(value: object) -> int:
    if isinstance(value, bool):
        return 1 if value else 0
    try:
        return int(str(value or "0"))
    except ValueError:
        return 0


def object_exists(target: Target, object_type: str, name: str) -> bool:
    if object_type == "routine":
        sql = f"""
        SELECT COUNT(*)
          FROM information_schema.routines
         WHERE routine_schema = DATABASE()
           AND routine_name = {sql_literal(name)};
        """
    elif object_type == "view":
        sql = f"""
        SELECT COUNT(*)
          FROM information_schema.views
         WHERE table_schema = DATABASE()
           AND table_name = {sql_literal(name)};
        """
    elif object_type == "table":
        sql = f"""
        SELECT COUNT(*)
          FROM information_schema.tables
         WHERE table_schema = DATABASE()
           AND table_name = {sql_literal(name)}
           AND table_type = 'BASE TABLE';
        """
    else:
        raise ValueError(object_type)
    rows = split_rows(run_mysql(target, sql))
    return bool(rows and rows[-1] and rows[-1][0] == "1")


def latest_session(target: Target, session_key: str, character_key: str) -> dict[str, object]:
    sql = f"""
    SELECT HEX(ss.session_id),
           BIN_TO_UUID(ss.session_id,1),
           ss.session_key,
           c.character_key,
           COALESCE(cwt.world_name, rwi.world_instance_key, ''),
           ss.lifecycle_state,
           DATE_FORMAT(ss.last_seen_at, '%Y-%m-%dT%H:%i:%s.%fZ')
      FROM server_sessions ss
      JOIN characters c ON c.character_id = ss.character_id
      JOIN realm_world_instances rwi ON rwi.world_instance_id = ss.world_instance_id
      LEFT JOIN content_world_templates cwt ON cwt.world_template_id = rwi.world_template_id
     WHERE ss.session_key = {sql_literal(session_key)}
       AND c.character_key = {sql_literal(character_key)}
     ORDER BY ss.last_seen_at DESC, ss.started_at DESC
     LIMIT 1;
    """
    rows = split_rows(run_mysql(target, sql))
    if not rows:
        return {}
    row = rows[-1]
    return {
        "session_hex": row[0],
        "session_uuid": row[1],
        "session_key": row[2],
        "character_key": row[3],
        "world_name": row[4],
        "lifecycle_state": row[5],
        "last_seen_at": row[6],
    }


def latest_manifest(target: Target, session_hex: str) -> dict[str, object]:
    sql = f"""
    SET @sid = UNHEX({sql_literal(session_hex)});
    SELECT HEX(sm.manifest_id),
           BIN_TO_UUID(sm.manifest_id,1),
           sm.manifest_key,
           COALESCE(sm.save_slot_key, ''),
           COALESCE(sm.display_name, ''),
           COALESCE(sm.client_world_name, ''),
           sm.native_save_present,
           sm.server_tick,
           sm.latest_checkpoint_tick,
           sm.recent_event_seq,
           sm.inventory_rows,
           sm.equipment_rows,
           sm.quest_rows,
           sm.known_dialog_rows,
           sm.script_state_rows,
           sm.world_item_rows,
           sm.world_inventory_rows,
           sm.interactive_rows,
           sm.npc_lifecycle_rows,
           sm.mover_rows,
           sm.row_version,
           DATE_FORMAT(sm.created_at, '%Y-%m-%dT%H:%i:%s.%fZ')
      FROM server_sessions ss
      JOIN mmo_save_checkpoint_manifests sm
        ON sm.character_id = ss.character_id
       AND sm.world_instance_id = ss.world_instance_id
     WHERE ss.session_id = @sid
     ORDER BY sm.created_at DESC, sm.row_version DESC
     LIMIT 1;
    """
    rows = split_rows(run_mysql(target, sql))
    if not rows:
        return {}
    cols = (
        "manifest_hex",
        "manifest_uuid",
        "manifest_key",
        "save_slot_key",
        "display_name",
        "client_world_name",
        "native_save_present",
        "server_tick",
        "latest_checkpoint_tick",
        "recent_event_seq",
        "inventory_rows",
        "equipment_rows",
        "quest_rows",
        "known_dialog_rows",
        "script_state_rows",
        "world_item_rows",
        "world_inventory_rows",
        "interactive_rows",
        "npc_lifecycle_rows",
        "mover_rows",
        "row_version",
        "created_at",
    )
    out = dict(zip(cols, rows[-1], strict=False))
    for key in cols[6:21]:
        out[key] = int_value(out.get(key))
    return out


def checkpoint_counts(target: Target, manifest_hex: str) -> dict[str, int]:
    sql = f"""
    SET @mid = UNHEX({sql_literal(manifest_hex)});
    SELECT
      (SELECT COUNT(*) FROM mmo_save_checkpoint_character_snapshot WHERE manifest_id=@mid),
      (SELECT COUNT(*) FROM mmo_save_checkpoint_inventory_snapshot WHERE manifest_id=@mid),
      (SELECT COUNT(*) FROM mmo_save_checkpoint_equipment_snapshot WHERE manifest_id=@mid),
      (SELECT COUNT(*) FROM mmo_save_checkpoint_quest_snapshot WHERE manifest_id=@mid),
      (SELECT COUNT(*) FROM mmo_save_checkpoint_known_dialog_snapshot WHERE manifest_id=@mid),
      (SELECT COUNT(*) FROM mmo_save_checkpoint_script_state_snapshot WHERE manifest_id=@mid),
      (SELECT COUNT(*) FROM mmo_save_checkpoint_world_entity_snapshot WHERE manifest_id=@mid),
      (SELECT COUNT(*) FROM mmo_save_checkpoint_world_entity_snapshot WHERE manifest_id=@mid AND entity_kind='item'),
      (SELECT COUNT(*) FROM mmo_save_checkpoint_world_entity_snapshot WHERE manifest_id=@mid AND entity_kind='item' AND lifecycle_state='active'),
      (SELECT COUNT(*) FROM mmo_save_checkpoint_world_entity_snapshot WHERE manifest_id=@mid AND entity_kind='item' AND lifecycle_state<>'active'),
      (SELECT COUNT(*) FROM mmo_save_checkpoint_world_entity_snapshot WHERE manifest_id=@mid AND entity_kind='interactive'),
      (SELECT COUNT(*) FROM mmo_save_checkpoint_world_entity_snapshot WHERE manifest_id=@mid AND entity_kind IN ('npc','creature')),
      (SELECT COUNT(*) FROM mmo_save_checkpoint_world_entity_snapshot WHERE manifest_id=@mid AND entity_kind IN ('npc','creature') AND lifecycle_state<>'active'),
      (SELECT COUNT(*) FROM mmo_save_checkpoint_world_inventory_snapshot WHERE manifest_id=@mid),
      (SELECT COUNT(*) FROM mmo_save_checkpoint_world_clock_snapshot WHERE manifest_id=@mid),
      (SELECT COUNT(*) FROM mmo_save_checkpoint_mover_snapshot WHERE manifest_id=@mid);
    """
    rows = split_rows(run_mysql(target, sql))
    cols = (
        "character",
        "inventory",
        "equipment",
        "quests",
        "known_dialogs",
        "script_state",
        "world_entities",
        "world_items_total",
        "world_items_active",
        "world_items_removed",
        "interactives",
        "npcs_total",
        "npc_lifecycle_non_active",
        "world_inventory",
        "world_clock",
        "movers",
    )
    return {key: int_value(value) for key, value in zip(cols, rows[-1] if rows else [], strict=False)}


def live_counts(target: Target, session_hex: str, has_mover_table: bool) -> dict[str, int]:
    mover_expr = (
        "(SELECT COUNT(*) FROM mmo_world_mover_state_current ms JOIN server_sessions ss ON ss.world_instance_id=ms.world_instance_id WHERE ss.session_id=@sid)"
        if has_mover_table
        else "0"
    )
    sql = f"""
    SET @sid = UNHEX({sql_literal(session_hex)});
    SELECT
      (SELECT COUNT(*) FROM characters c JOIN server_sessions ss ON ss.character_id=c.character_id WHERE ss.session_id=@sid),
      (SELECT COUNT(*) FROM character_inventory ci JOIN server_sessions ss ON ss.character_id=ci.character_id WHERE ss.session_id=@sid),
      (SELECT COUNT(*) FROM character_equipment ce JOIN server_sessions ss ON ss.character_id=ce.character_id WHERE ss.session_id=@sid),
      (SELECT COUNT(*) FROM character_quests q JOIN server_sessions ss ON ss.character_id=q.character_id WHERE ss.session_id=@sid),
      (SELECT COUNT(*) FROM character_known_dialogs d JOIN server_sessions ss ON ss.character_id=d.character_id WHERE ss.session_id=@sid),
      (SELECT COUNT(*) FROM character_script_state s JOIN server_sessions ss ON ss.character_id=s.character_id WHERE ss.session_id=@sid AND s.value_type IN ('int','array_int')),
      (SELECT COUNT(*) FROM world_entity_state wes JOIN server_sessions ss ON ss.world_instance_id=wes.world_instance_id WHERE ss.session_id=@sid),
      (SELECT COUNT(*) FROM world_entity_state wes JOIN server_sessions ss ON ss.world_instance_id=wes.world_instance_id WHERE ss.session_id=@sid AND wes.entity_kind='item'),
      (SELECT COUNT(*) FROM world_entity_state wes JOIN server_sessions ss ON ss.world_instance_id=wes.world_instance_id WHERE ss.session_id=@sid AND wes.entity_kind='item' AND wes.lifecycle_state='active'),
      (SELECT COUNT(*) FROM world_entity_state wes JOIN server_sessions ss ON ss.world_instance_id=wes.world_instance_id WHERE ss.session_id=@sid AND wes.entity_kind='item' AND wes.lifecycle_state<>'active'),
      (SELECT COUNT(*) FROM world_entity_state wes JOIN server_sessions ss ON ss.world_instance_id=wes.world_instance_id WHERE ss.session_id=@sid AND wes.entity_kind='interactive'),
      (SELECT COUNT(*) FROM world_entity_state wes JOIN server_sessions ss ON ss.world_instance_id=wes.world_instance_id WHERE ss.session_id=@sid AND wes.entity_kind IN ('npc','creature')),
      (SELECT COUNT(*) FROM world_entity_state wes JOIN server_sessions ss ON ss.world_instance_id=wes.world_instance_id WHERE ss.session_id=@sid AND wes.entity_kind IN ('npc','creature') AND wes.lifecycle_state<>'active'),
      (SELECT COUNT(*) FROM world_inventory wi JOIN server_sessions ss ON ss.world_instance_id=wi.world_instance_id WHERE ss.session_id=@sid),
      (SELECT COUNT(*) FROM realm_world_instances rwi JOIN server_sessions ss ON ss.world_instance_id=rwi.world_instance_id WHERE ss.session_id=@sid),
      {mover_expr};
    """
    rows = split_rows(run_mysql(target, sql))
    cols = (
        "character",
        "inventory",
        "equipment",
        "quests",
        "known_dialogs",
        "script_state",
        "world_entities",
        "world_items_total",
        "world_items_active",
        "world_items_removed",
        "interactives",
        "npcs_total",
        "npc_lifecycle_non_active",
        "world_inventory",
        "world_clock",
        "movers",
    )
    return {key: int_value(value) for key, value in zip(cols, rows[-1] if rows else [], strict=False)}


def strict_restore_row(target: Target, session_key: str, character_key: str) -> dict[str, object]:
    session = latest_session(target, session_key, character_key)
    session_hex = session.get("session_hex")
    if not session_hex:
        return {}
    return validate_restore(target, str(session_hex))


def validate_restore(target: Target, session_hex: str) -> dict[str, object]:
    sql = f"""
    SET @sid = UNHEX({sql_literal(session_hex)});
    SET @validation = NULL;
    CALL mmo_validate_latest_save_checkpoint_restore_v1(@sid, @validation);
    SELECT COALESCE(@validation, '{{}}');
    """
    rows = split_rows(run_mysql(target, sql))
    if not rows:
        return {}
    try:
        value = json.loads(rows[-1][0])
    except json.JSONDecodeError:
        return {"parse_error": "invalid_json", "raw": rows[-1][0][:4096]}
    return value if isinstance(value, dict) else {"parse_error": "json_not_object"}


def export_probe(target: Target, session_hex: str) -> dict[str, object]:
    sql = f"""
    SET @sid = UNHEX({sql_literal(session_hex)});
    SET @snapshot = NULL;
    CALL mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1(@sid, @snapshot);
    SELECT CHAR_LENGTH(@snapshot),
           JSON_UNQUOTE(JSON_EXTRACT(@snapshot, '$.snapshot_source')),
           JSON_UNQUOTE(JSON_EXTRACT(@snapshot, '$.db_save_checkpoint_manifest_uuid')),
           JSON_UNQUOTE(JSON_EXTRACT(@snapshot, '$.world_name')),
           COALESCE(JSON_LENGTH(JSON_EXTRACT(@snapshot, '$.inventory')),0),
           COALESCE(JSON_LENGTH(JSON_EXTRACT(@snapshot, '$.equipment')),0),
           COALESCE(JSON_LENGTH(JSON_EXTRACT(@snapshot, '$.quests')),0),
           COALESCE(JSON_LENGTH(JSON_EXTRACT(@snapshot, '$.known_dialogs')),0),
           COALESCE(JSON_LENGTH(JSON_EXTRACT(@snapshot, '$.script_state')),0),
           COALESCE(JSON_LENGTH(JSON_EXTRACT(@snapshot, '$.script_state_full')),0),
           COALESCE(JSON_LENGTH(JSON_EXTRACT(@snapshot, '$.active_world_items')),0),
           COALESCE(JSON_LENGTH(JSON_EXTRACT(@snapshot, '$.world_item_deltas')),0),
           COALESCE(JSON_LENGTH(JSON_EXTRACT(@snapshot, '$.interactive_state')),0),
           COALESCE(JSON_LENGTH(JSON_EXTRACT(@snapshot, '$.npc_lifecycle_state')),0),
           COALESCE(JSON_LENGTH(JSON_EXTRACT(@snapshot, '$.nearby_npcs')),0),
           COALESCE(JSON_LENGTH(JSON_EXTRACT(@snapshot, '$.mover_state')),0);
    """
    rows = split_rows(run_mysql(target, sql))
    if not rows:
        return {}
    cols = (
        "bytes",
        "snapshot_source",
        "manifest_uuid",
        "world_name",
        "inventory",
        "equipment",
        "quests",
        "known_dialogs",
        "script_state",
        "script_state_full",
        "active_world_items",
        "world_item_deltas",
        "interactive_state",
        "npc_lifecycle_state",
        "nearby_npcs",
        "mover_state",
    )
    out = dict(zip(cols, rows[-1], strict=False))
    for key in cols[:1] + cols[4:]:
        out[key] = int_value(out.get(key))
    return out


def compare_counts(checkpoint: dict[str, int], live: dict[str, int]) -> dict[str, dict[str, int | bool]]:
    domains = sorted(set(checkpoint) | set(live))
    out: dict[str, dict[str, int | bool]] = {}
    for domain in domains:
        checkpoint_value = checkpoint.get(domain, 0)
        live_value = live.get(domain, 0)
        out[domain] = {
            "checkpoint": checkpoint_value,
            "live": live_value,
            "delta_live_minus_checkpoint": live_value - checkpoint_value,
            "equal": checkpoint_value == live_value,
        }
    return out


def compare_export_coverage(
    checkpoint: dict[str, int],
    export: dict[str, object],
) -> dict[str, dict[str, int | bool | str]]:
    out: dict[str, dict[str, int | bool | str]] = {}
    for checkpoint_key, export_key in EXPORT_COVERAGE_DOMAINS.items():
        expected = checkpoint.get(checkpoint_key, 0)
        exported = int_value(export.get(export_key))
        out[checkpoint_key] = {
            "expected_from_checkpoint_table": expected,
            "exported_in_bootstrap_json": exported,
            "delta_export_minus_expected": exported - expected,
            "equal": expected == exported,
            "export_key": export_key,
        }
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="Audit DB save checkpoint snapshot export coverage and live projection parity.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--session-key", default="local-dev-PC_HERO_TEST")
    ap.add_argument("--character-key", default="PC_HERO")
    ap.add_argument("--assert-strict", action="store_true", help="Return non-zero unless strict DB checkpoint export is ready.")
    ap.add_argument("--assert-no-drift", action="store_true", help="Return non-zero unless comparable snapshot counts equal live projection counts.")
    ap.add_argument("--assert-export-coverage", action="store_true", help="Return non-zero unless exported bootstrap JSON covers checkpoint snapshot tables.")
    ap.add_argument("--output", default="")
    args = ap.parse_args()

    target = parse_mysql_url(args.url)
    result: dict[str, object] = {
        "step": "104_db_checkpoint_script_state_full_export",
        "started_at": datetime.now(timezone.utc).isoformat(),
        "database": target.database,
        "session_key": args.session_key,
        "character_key": args.character_key,
        "status": "running",
    }

    try:
        result["routines"] = {name: object_exists(target, "routine", name) for name in REQUIRED_ROUTINES}
        result["views"] = {name: object_exists(target, "view", name) for name in REQUIRED_VIEWS}
        result["snapshot_tables"] = {name: object_exists(target, "table", name) for name in SNAPSHOT_TABLES}
        has_mover_table = object_exists(target, "table", "mmo_world_mover_state_current")
        result["optional_runtime_tables"] = {"mmo_world_mover_state_current": has_mover_table}

        missing = [f"routine:{name}" for name, ok in result["routines"].items() if not ok]  # type: ignore[union-attr]
        missing.extend(f"view:{name}" for name, ok in result["views"].items() if not ok)  # type: ignore[union-attr]
        missing.extend(f"table:{name}" for name, ok in result["snapshot_tables"].items() if not ok)  # type: ignore[union-attr]
        result["missing"] = missing

        session = latest_session(target, args.session_key, args.character_key)
        result["session"] = session
        if missing:
            result["status"] = "missing_objects"
        elif not session.get("session_hex"):
            result["status"] = "missing_session"
        else:
            session_hex = str(session["session_hex"])
            manifest = latest_manifest(target, session_hex)
            result["latest_manifest"] = manifest
            if not manifest.get("manifest_hex"):
                result["status"] = "missing_manifest"
            else:
                checkpoint = checkpoint_counts(target, str(manifest["manifest_hex"]))
                live = live_counts(target, session_hex, has_mover_table)
                result["checkpoint_counts"] = checkpoint
                result["live_counts"] = live
                result["count_drift"] = compare_counts(checkpoint, live)
                result["strict_restore"] = strict_restore_row(target, args.session_key, args.character_key)
                result["validation"] = validate_restore(target, session_hex)
                result["export_probe"] = export_probe(target, session_hex)
                result["export_coverage"] = compare_export_coverage(checkpoint, result["export_probe"])  # type: ignore[arg-type]

                strict = result["strict_restore"] if isinstance(result["strict_restore"], dict) else {}
                export = result["export_probe"] if isinstance(result["export_probe"], dict) else {}
                coverage = result["export_coverage"] if isinstance(result.get("export_coverage"), dict) else {}
                export_ready = all(item.get("equal") is True for item in coverage.values() if isinstance(item, dict))
                strict_ready = (
                    int_value(strict.get("strict_restore_ok")) == 1
                    and str(strict.get("snapshot_source", "")) == "db_save_checkpoint_v1"
                    and int_value(strict.get("exported_bootstrap_bytes")) > 0
                    and str(export.get("snapshot_source", "")) == "db_save_checkpoint_v1"
                    and int_value(export.get("bytes")) > 0
                    and export_ready
                )
                drift = result["count_drift"] if isinstance(result.get("count_drift"), dict) else {}
                no_drift = all(
                    item.get("equal") is True
                    for domain, item in drift.items()
                    if domain not in COUNT_DRIFT_EXCLUDED_DOMAINS and isinstance(item, dict)
                )
                result["strict_ready"] = strict_ready
                result["export_ready"] = export_ready
                result["count_drift_excluded_domains"] = sorted(COUNT_DRIFT_EXCLUDED_DOMAINS)
                result["no_drift"] = no_drift
                result["status"] = "passed" if strict_ready and (no_drift or not args.assert_no_drift) else "not_ready"

    except Exception as exc:  # noqa: BLE001 - CLI checker should serialize failure details.
        result["status"] = "failed"
        result["error"] = str(exc)

    result["finished_at"] = datetime.now(timezone.utc).isoformat()
    text = json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True)
    if args.output:
        path = Path(args.output)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text + "\n", encoding="utf-8")
    print(text)

    if result["status"] == "failed":
        return 2
    if args.assert_strict and result.get("strict_ready") is not True:
        return 3
    if args.assert_export_coverage and result.get("export_ready") is not True:
        return 5
    if args.assert_no_drift and result.get("no_drift") is not True:
        return 4
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
