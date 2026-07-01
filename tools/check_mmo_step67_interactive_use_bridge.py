#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import unquote, urlparse


REQUIRED_ROUTINES = ("mmo_record_interactive_use",)
REQUIRED_TABLES = ("world_interactive_audit", "world_event_journal", "server_sessions")


@dataclass(frozen=True)
class Target:
    host: str
    port: int
    user: str
    password: str
    database: str


class MysqlError(RuntimeError):
    def __init__(self, returncode: int, stdout: str, stderr: str, sql: str) -> None:
        super().__init__(f"mysql exited with status {returncode}")
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr
        self.sql = sql


def parse_mysql_url(url: str) -> Target:
    p = urlparse(url)
    if p.scheme not in {"mysql", "mysql+pymysql"}:
        raise ValueError("expected mysql:// URL")
    return Target(
        host=p.hostname or "127.0.0.1",
        port=p.port or 3306,
        user=unquote(p.username or ""),
        password=unquote(p.password or ""),
        database=(p.path or "/").lstrip("/"),
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
    proc = subprocess.run(mysql_cmd(target) + ["--execute", sql], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        raise MysqlError(proc.returncode, proc.stdout, proc.stderr, sql)
    return proc.stdout.strip()


def sql_literal(value: object) -> str:
    if value is None:
        return "NULL"
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, (int, float)):
        return str(value)
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''") + "'"


def line_set(raw: str) -> set[str]:
    return {line.strip() for line in raw.splitlines() if line.strip()}


def last_row(raw: str) -> list[str]:
    return (raw.splitlines()[-1] if raw else "").split("\t")


def check_objects(target: Target) -> dict[str, object]:
    db = target.database.replace("'", "''")
    tables = line_set(run_mysql(target, f"SELECT table_name FROM information_schema.tables WHERE table_schema='{db}' AND table_type='BASE TABLE';"))
    routines = line_set(run_mysql(target, f"SELECT routine_name FROM information_schema.routines WHERE routine_schema='{db}';"))
    return {
        "missing_tables": sorted(set(REQUIRED_TABLES) - tables),
        "missing_routines": sorted(set(REQUIRED_ROUTINES) - routines),
    }


def smoke_phase(result: dict[str, object], name: str, target: Target, sql: str) -> str | None:
    phases = result.setdefault("phases", [])
    assert isinstance(phases, list)
    phase: dict[str, object] = {"name": name}
    try:
        raw = run_mysql(target, sql)
    except MysqlError as exc:
        phase.update(
            {
                "ok": False,
                "error": str(exc),
                "stderr_tail": exc.stderr[-4000:],
                "stdout_tail": exc.stdout[-4000:],
                "sql_head": "\n".join(line.rstrip() for line in exc.sql.strip().splitlines()[:24]),
            }
        )
        phases.append(phase)
        result["ok"] = False
        return None
    phase.update({"ok": True, "stdout_tail": raw[-4000:]})
    phases.append(phase)
    return raw


def run_smoke(target: Target, account_name: str, character_key: str) -> dict[str, object]:
    suffix = uuid.uuid4().hex
    session_key = f"step67-smoke:{suffix}"
    interactive_key = f"smoke:step67:interactive:{suffix}"
    idem = f"step67:interactive-use:{suffix}"
    result: dict[str, object] = {"session_key": session_key, "interactive_key": interactive_key, "ok": False}

    login_raw = smoke_phase(
        result,
        "login",
        target,
        f"""
    SET @session_id=NULL;
    CALL mmo_login_character({sql_literal(account_name)}, {sql_literal(character_key)}, {sql_literal(session_key)}, 'step67-smoke', 'local', JSON_OBJECT('tool','check_mmo_step67'), @session_id);
    SELECT BIN_TO_UUID(@session_id,1);
    """,
    )
    if login_raw is None:
        return result
    session_uuid = last_row(login_raw)[0]
    result["session_uuid"] = session_uuid

    if not smoke_phase(
        result,
        "fixture",
        target,
        f"""
    SET @realm_id=(SELECT realm_id FROM characters WHERE character_key={sql_literal(character_key)} LIMIT 1);
    SET @world_id=(SELECT current_world_instance_id FROM characters WHERE character_key={sql_literal(character_key)} LIMIT 1);
    SET @content_revision_id=(SELECT active_content_revision_id FROM realm_realms WHERE realm_id=@realm_id LIMIT 1);
    SET @entity_template_id=(SELECT entity_template_id FROM content_entity_templates WHERE content_revision_id=@content_revision_id AND entity_kind='interactive' ORDER BY created_at DESC, engine_template_key DESC LIMIT 1);
    INSERT INTO world_entity_state(world_instance_id, entity_key, entity_kind, entity_template_id, lifecycle_state, pos_x, pos_y, pos_z, state_json, row_version)
    VALUES(@world_id, {sql_literal(interactive_key)}, 'interactive', @entity_template_id, 'active', 0, 0, 0, JSON_OBJECT('state_id',1,'tool','check_mmo_step67'), 1)
    ON DUPLICATE KEY UPDATE entity_template_id=VALUES(entity_template_id), lifecycle_state=VALUES(lifecycle_state), state_json=VALUES(state_json), row_version=VALUES(row_version);
    SELECT BIN_TO_UUID(@world_id,1), BIN_TO_UUID(@entity_template_id,1);
    """,
    ):
        return result

    use_raw = smoke_phase(
        result,
        "record_interactive_use",
        target,
        f"""
    SET @event_id=NULL; SET @row_after=NULL;
    CALL mmo_record_interactive_use(UUID_TO_BIN({sql_literal(session_uuid)},1), {sql_literal(interactive_key)}, 1, 6701, JSON_OBJECT('smoke',true), {sql_literal(idem)}, @event_id, @row_after);
    SELECT BIN_TO_UUID(@event_id,1), @row_after,
           (SELECT COUNT(*) FROM world_event_journal WHERE idempotency_key={sql_literal(idem)} AND event_type='interactive_used' AND event_class='world_entity'),
           (SELECT COUNT(*) FROM world_interactive_audit WHERE idempotency_key={sql_literal(idem)} AND audit_type='interactive_use');
    """,
    )
    if use_raw is None:
        return result
    event_uuid, row_after, event_count, audit_count = last_row(use_raw)
    result.update(
        {
            "event_uuid": event_uuid,
            "row_version_after": row_after,
            "event_count": int(event_count or 0),
            "audit_count": int(audit_count or 0),
        }
    )

    result["ok"] = bool(event_uuid and result["event_count"] == 1 and result["audit_count"] == 1)
    return result


def main() -> int:
    ap = argparse.ArgumentParser(description="Check Step67 interactive-use bridge.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--account-name", default="local-import")
    ap.add_argument("--character-key", default="PC_HERO")
    ap.add_argument("--smoke", action="store_true")
    ap.add_argument("--output", default="")
    args = ap.parse_args()

    target = parse_mysql_url(args.url)
    result: dict[str, object] = {
        "step": "67_interactive_use_bridge",
        "checked_at": datetime.now(timezone.utc).isoformat(),
        "database": target.database,
    }
    result.update(check_objects(target))
    if args.smoke and not result["missing_tables"] and not result["missing_routines"]:
        result["smoke"] = run_smoke(target, args.account_name, args.character_key)

    errors = []
    if result["missing_tables"]:
        errors.append("missing_tables")
    if result["missing_routines"]:
        errors.append("missing_routines")
    if args.smoke and not result.get("smoke", {}).get("ok"):
        errors.append("smoke")
    result["status"] = "failed" if errors else "passed"
    result["errors"] = errors

    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("status=" + result["status"])
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
