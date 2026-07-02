#!/usr/bin/env python3
"""Check Step56b clean-DB progress/dialog/quest bridge.

The checker verifies that the minimal procedures used by the resolved worker are
installed. With --smoke it logs in a synthetic session and calls the procedures
with harmless Step56b smoke keys.
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

REQUIRED_ROUTINES = [
    "mmo_set_character_script_int",
    "mmo_update_character_quest",
    "mmo_set_character_known_dialog",
]
SUPPORT_ROUTINES = [
    "mmo_login_character",
    "mmo_logout_character",
    "mmo_append_world_event",
]
REQUIRED_TABLES = [
    "server_sessions",
    "characters",
    "character_script_state",
    "character_quests",
    "character_known_dialogs",
    "world_event_journal",
]


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
    return Target(p.hostname or "127.0.0.1", p.port or 3306, unquote(p.username or ""), unquote(p.password or ""), db)


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
    proc = subprocess.run(mysql_cmd(target), input=sql, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def sql_literal(value: Any) -> str:
    if value is None:
        return "NULL"
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''") + "'"


def count_object(target: Target, kind: str, name: str) -> int:
    if kind == "table":
        sql = f"""
        SELECT COUNT(*)
          FROM information_schema.tables
         WHERE table_schema=DATABASE()
           AND table_name={sql_literal(name)}
           AND table_type='BASE TABLE';
        """
    elif kind == "routine":
        sql = f"""
        SELECT COUNT(*)
          FROM information_schema.routines
         WHERE routine_schema=DATABASE()
           AND routine_name={sql_literal(name)};
        """
    else:
        raise ValueError(kind)
    out = run_mysql(target, sql)
    return int((out or "0").splitlines()[-1])


def uuid_bin(uuid_text: str) -> str:
    return f"UUID_TO_BIN({sql_literal(uuid_text)},1)"


def smoke(target: Target, account_name: str, character_key: str, session_key: str) -> dict[str, Any]:
    out = run_mysql(
        target,
        f"""
        SET @session_id = NULL;
        CALL mmo_login_character(
          {sql_literal(account_name)},
          {sql_literal(character_key)},
          {sql_literal(session_key)},
          'step56b-progress-bridge-smoke',
          'local',
          JSON_OBJECT('tool','check_mmo_step56b_clean_db_progress_bridge'),
          @session_id
        );
        SELECT BIN_TO_UUID(@session_id,1);
        """,
    )
    session_uuid = out.splitlines()[-1].strip()
    prefix = session_key + ":"
    result: dict[str, Any] = {"session_uuid": session_uuid}

    out = run_mysql(
        target,
        f"""
        SET @event_id=NULL; SET @value_after=NULL;
        CALL mmo_set_character_script_int(
          {uuid_bin(session_uuid)},
          'step56b_smoke_script_int',
          NULL,
          0,
          56057,
          56057,
          JSON_OBJECT('smoke', TRUE),
          {sql_literal(prefix + 'set_script_int')},
          @event_id,
          @value_after
        );
        SELECT BIN_TO_UUID(@event_id,1), @value_after;
        """,
    )
    parts = out.splitlines()[-1].split("\t")
    result["script_int"] = {"event_uuid": parts[0], "value_after": int(parts[1])}

    out = run_mysql(
        target,
        f"""
        SET @event_id=NULL;
        CALL mmo_update_character_quest(
          {uuid_bin(session_uuid)},
          'step56b_smoke_quest',
          'Step56b Smoke Quest',
          'running',
          1,
          JSON_ARRAY('step56b smoke entry'),
          56058,
          JSON_OBJECT('smoke', TRUE),
          {sql_literal(prefix + 'update_quest')},
          @event_id
        );
        SELECT BIN_TO_UUID(@event_id,1);
        """,
    )
    result["quest"] = {"event_uuid": out.splitlines()[-1].strip()}

    out = run_mysql(
        target,
        f"""
        SET @event_id=NULL;
        CALL mmo_set_character_known_dialog(
          {uuid_bin(session_uuid)},
          'step56b_smoke_npc',
          'step56b_smoke_info',
          TRUE,
          FALSE,
          'consumed_hidden',
          56059,
          JSON_OBJECT('smoke', TRUE),
          {sql_literal(prefix + 'set_known_dialog')},
          @event_id
        );
        SELECT BIN_TO_UUID(@event_id,1);
        """,
    )
    result["known_dialog"] = {"event_uuid": out.splitlines()[-1].strip()}

    try:
        run_mysql(
            target,
            f"""
            SET @event_id=NULL;
            CALL mmo_logout_character(
              {uuid_bin(session_uuid)},
              'step56b_progress_bridge_smoke_done',
              JSON_OBJECT('tool','check_mmo_step56b_clean_db_progress_bridge'),
              @event_id
            );
            """,
        )
    except Exception as exc:  # logout should not hide successful bridge checks
        result["logout_warning"] = str(exc)

    return result


def main() -> int:
    ap = argparse.ArgumentParser(description="Check Step56b clean-DB progress/dialog/quest bridge.")
    ap.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    ap.add_argument("--smoke", action="store_true")
    ap.add_argument("--account-name", default="local-import")
    ap.add_argument("--character-key", default="PC_HERO")
    ap.add_argument("--session-key", default="step56b-progress-bridge-smoke")
    ap.add_argument("--output")
    args = ap.parse_args()

    target = parse_mysql_url(args.url)
    result: dict[str, Any] = {
        "status": "passed",
        "database": target.database,
        "tables": {name: count_object(target, "table", name) == 1 for name in REQUIRED_TABLES},
        "routines": {name: count_object(target, "routine", name) == 1 for name in REQUIRED_ROUTINES},
        "support_routines": {name: count_object(target, "routine", name) == 1 for name in SUPPORT_ROUTINES},
    }
    missing_tables = [k for k, ok in result["tables"].items() if not ok]
    missing_routines = [k for k, ok in result["routines"].items() if not ok]
    missing_support = [k for k, ok in result["support_routines"].items() if not ok]
    if missing_tables or missing_routines or missing_support:
        result["status"] = "failed"
        result["missing_tables"] = missing_tables
        result["missing_routines"] = missing_routines
        result["missing_support_routines"] = missing_support

    if result["status"] == "passed" and args.smoke:
        result["smoke"] = smoke(target, args.account_name, args.character_key, args.session_key)

    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"artifact={out}")

    print("Step56b clean DB progress bridge check")
    print("status=" + result["status"])
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
