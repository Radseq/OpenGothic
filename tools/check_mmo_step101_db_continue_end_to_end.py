#!/usr/bin/env python3
"""Check Step101 strict DB-native Continue readiness for a real save checkpoint."""
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
    "v_mmo_latest_save_checkpoint_manifests",
    "v_mmo_latest_save_checkpoint_restore_readiness",
    "v_mmo_latest_save_checkpoint_strict_restore",
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
        input=sql,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, file=sys.stderr, end="")
    if proc.returncode != 0:
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def sql_literal(value: str) -> str:
    return "'" + value.replace("\\", "\\\\").replace("'", "''") + "'"


def split_rows(raw: str) -> list[list[str]]:
    rows: list[list[str]] = []
    for line in raw.splitlines():
        if line.strip():
            rows.append(line.split("\t"))
    return rows


def object_exists(target: Target, kind: str, name: str) -> bool:
    if kind == "routine":
        sql = f"""
        SELECT COUNT(*)
          FROM information_schema.routines
         WHERE routine_schema = DATABASE()
           AND routine_name = {sql_literal(name)};
        """
    elif kind == "view":
        sql = f"""
        SELECT COUNT(*)
          FROM information_schema.views
         WHERE table_schema = DATABASE()
           AND table_name = {sql_literal(name)};
        """
    else:
        raise ValueError(kind)
    rows = split_rows(run_mysql(target, sql))
    return bool(rows and rows[-1] and rows[-1][0] == "1")


def parse_json_object(raw: str) -> dict[str, object]:
    if not raw:
        return {}
    try:
        value = json.loads(raw)
    except json.JSONDecodeError:
        return {"parse_error": "invalid_json", "raw": raw[:4096]}
    return value if isinstance(value, dict) else {"parse_error": "json_not_object", "raw": raw[:4096]}


def latest_session(target: Target, session_key: str, character_key: str) -> dict[str, object]:
    sql = f"""
    SELECT HEX(ss.session_id),
           BIN_TO_UUID(ss.session_id, 1),
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


def strict_restore_row(target: Target, session_key: str, character_key: str) -> dict[str, object]:
    sql = f"""
    SELECT session_uuid,
           session_key,
           character_key,
           world_instance_key,
           manifest_uuid,
           save_key,
           display_name,
           client_world_name,
           native_save_present,
           character_rows,
           inventory_rows,
           equipment_rows,
           quest_rows,
           known_dialog_rows,
           script_state_rows,
           world_entity_rows,
           world_inventory_rows,
           world_clock_rows,
           mover_rows,
           exported_bootstrap_bytes,
           snapshot_source,
           strict_restore_ok
      FROM v_mmo_latest_save_checkpoint_strict_restore
     WHERE session_key = {sql_literal(session_key)}
       AND character_key = {sql_literal(character_key)}
     ORDER BY created_at DESC
     LIMIT 1;
    """
    rows = split_rows(run_mysql(target, sql))
    if not rows:
        return {}
    cols = (
        "session_uuid",
        "session_key",
        "character_key",
        "world_instance_key",
        "manifest_uuid",
        "save_key",
        "display_name",
        "client_world_name",
        "native_save_present",
        "character_rows",
        "inventory_rows",
        "equipment_rows",
        "quest_rows",
        "known_dialog_rows",
        "script_state_rows",
        "world_entity_rows",
        "world_inventory_rows",
        "world_clock_rows",
        "mover_rows",
        "exported_bootstrap_bytes",
        "snapshot_source",
        "strict_restore_ok",
    )
    row = rows[-1]
    out = dict(zip(cols, row, strict=False))
    for key in (
        "native_save_present",
        "character_rows",
        "inventory_rows",
        "equipment_rows",
        "quest_rows",
        "known_dialog_rows",
        "script_state_rows",
        "world_entity_rows",
        "world_inventory_rows",
        "world_clock_rows",
        "mover_rows",
        "exported_bootstrap_bytes",
        "strict_restore_ok",
    ):
        out[key] = int(str(out.get(key, "0") or "0"))
    return out


def validate_session(target: Target, session_hex: str) -> dict[str, object]:
    sql = f"""
    SET @sid = UNHEX({sql_literal(session_hex)});
    SET @validation = NULL;
    CALL mmo_validate_latest_save_checkpoint_restore_v1(@sid, @validation);
    SELECT @validation;
    """
    rows = split_rows(run_mysql(target, sql))
    return parse_json_object(rows[-1][0] if rows and rows[-1] else "")


def export_snapshot_probe(target: Target, session_hex: str) -> dict[str, object]:
    sql = f"""
    SET @sid = UNHEX({sql_literal(session_hex)});
    SET @snapshot = mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1(@sid);
    SELECT CHAR_LENGTH(@snapshot),
           JSON_UNQUOTE(JSON_EXTRACT(@snapshot, '$.source')),
           JSON_UNQUOTE(JSON_EXTRACT(@snapshot, '$.snapshot_source')),
           JSON_UNQUOTE(JSON_EXTRACT(@snapshot, '$.db_save_checkpoint_manifest_uuid')),
           JSON_UNQUOTE(JSON_EXTRACT(@snapshot, '$.character.world_name')),
           JSON_LENGTH(JSON_EXTRACT(@snapshot, '$.inventory')),
           JSON_LENGTH(JSON_EXTRACT(@snapshot, '$.active_world_items')),
           JSON_LENGTH(JSON_EXTRACT(@snapshot, '$.mover_state'));
    """
    rows = split_rows(run_mysql(target, sql))
    if not rows:
        return {}
    row = rows[-1]
    return {
        "bytes": int(row[0] or "0"),
        "source": row[1],
        "snapshot_source": row[2],
        "manifest_uuid": row[3],
        "world_name": row[4],
        "inventory_rows": int(row[5] or "0"),
        "active_world_items": int(row[6] or "0"),
        "mover_state_rows": int(row[7] or "0"),
    }


def manual_commands(session_key: str, character_key: str, save_slot: str) -> dict[str, str]:
    server = f"""./build/mmo_cpp_server/mmo_udp_server \\
  --bind 127.0.0.1:29777 \\
  --mysql-url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \\
  --session-key {session_key} \\
  --character-key {character_key} \\
  --require-db-save-checkpoint-restore"""
    client = f"""./build/opengothic/Gothic2Notr \\
  -g "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \\
  -g2 \\
  -save {save_slot} \\
  -mmo-client-server 127.0.0.1:29777 \\
  -mmo-action-session-key {session_key} \\
  -mmo-db-continue-without-native-save \\
  -mmo-require-db-save-checkpoint-restore"""
    return {"server": server, "client": client}


def main() -> int:
    ap = argparse.ArgumentParser(description="Check Step101 strict DB-native Continue end-to-end readiness.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--session-key", default="local-dev-PC_HERO_TEST")
    ap.add_argument("--character-key", default="PC_HERO")
    ap.add_argument("--save-slot", default="99", help="Slot number to show in the generated strict DB Continue command.")
    ap.add_argument("--assert-ready", action="store_true", help="Return non-zero unless the latest real checkpoint is strict-restore-ready.")
    ap.add_argument("--output", default="")
    args = ap.parse_args()

    target = parse_mysql_url(args.url)
    result: dict[str, object] = {
        "step": "101_strict_db_continue_end_to_end",
        "started_at": datetime.now(timezone.utc).isoformat(),
        "database": target.database,
        "session_key": args.session_key,
        "character_key": args.character_key,
        "status": "running",
    }

    try:
        result["routines"] = {name: object_exists(target, "routine", name) for name in REQUIRED_ROUTINES}
        result["views"] = {name: object_exists(target, "view", name) for name in REQUIRED_VIEWS}
        missing = [
            f"routine:{name}"
            for name, ok in result["routines"].items()  # type: ignore[union-attr]
            if not ok
        ]
        missing.extend(
            f"view:{name}"
            for name, ok in result["views"].items()  # type: ignore[union-attr]
            if not ok
        )
        result["missing"] = missing

        session = latest_session(target, args.session_key, args.character_key)
        result["session"] = session
        result["strict_restore"] = strict_restore_row(target, args.session_key, args.character_key)

        if session.get("session_hex"):
            session_hex = str(session["session_hex"])
            result["validation"] = validate_session(target, session_hex) if not missing else {}
            result["export_probe"] = export_snapshot_probe(target, session_hex) if not missing else {}
        else:
            result["validation"] = {}
            result["export_probe"] = {}

        result["manual_commands"] = manual_commands(args.session_key, args.character_key, args.save_slot)

        validation = result.get("validation", {})
        export_probe = result.get("export_probe", {})
        strict_restore = result.get("strict_restore", {})
        ready = (
            not missing
            and bool(session)
            and isinstance(validation, dict)
            and validation.get("strict_restore_ok") is True
            and isinstance(export_probe, dict)
            and export_probe.get("snapshot_source") == "db_save_checkpoint_v1"
            and int(export_probe.get("bytes", 0) or 0) > 0
            and isinstance(strict_restore, dict)
            and int(strict_restore.get("strict_restore_ok", 0) or 0) == 1
        )
        result["ready"] = ready
        result["status"] = "ok" if ready or not args.assert_ready else "failed"
        if args.assert_ready and not ready:
            result["error"] = "latest real DB save checkpoint is not strict DB Continue ready"
    except Exception as exc:  # noqa: BLE001
        result["status"] = "failed"
        result["ready"] = False
        result["error"] = str(exc)
        print(f"ERROR: {exc}", file=sys.stderr)

    result["finished_at"] = datetime.now(timezone.utc).isoformat()
    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result.get("status") == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
