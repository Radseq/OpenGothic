#!/usr/bin/env python3
"""Check Step98 strict DB-native Continue/restore validation bridge."""
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
    "mmo_validate_latest_save_checkpoint_restore_v1",
    "mmo_assert_latest_save_checkpoint_restore_v1",
)
REQUIRED_VIEWS = ("v_mmo_latest_save_checkpoint_strict_restore",)


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
    proc = subprocess.run(mysql_cmd(target), input=sql, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, file=sys.stderr, end="")
    if proc.returncode != 0:
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def sql_literal(value: str) -> str:
    return "'" + value.replace("\\", "\\\\").replace("'", "''") + "'"


def count_object(target: Target, kind: str, name: str) -> int:
    if kind == "routine":
        sql = f"""SELECT COUNT(*) FROM information_schema.routines
                  WHERE routine_schema=DATABASE() AND routine_name={sql_literal(name)};"""
    elif kind == "view":
        sql = f"""SELECT COUNT(*) FROM information_schema.views
                  WHERE table_schema=DATABASE() AND table_name={sql_literal(name)};"""
    else:
        raise ValueError(kind)
    out = run_mysql(target, sql)
    return int((out or "0").splitlines()[-1])


def inspect(target: Target) -> dict[str, object]:
    return {
        "routines": {name: count_object(target, "routine", name) == 1 for name in REQUIRED_ROUTINES},
        "views": {name: count_object(target, "view", name) == 1 for name in REQUIRED_VIEWS},
    }


def parse_validation(text: str) -> dict[str, object]:
    if not text:
        return {"strict_restore_ok": False, "reason": "empty_validation"}
    try:
        data = json.loads(text)
        if isinstance(data, dict):
            return data
    except json.JSONDecodeError:
        pass
    return {"strict_restore_ok": False, "reason": "invalid_validation_json", "raw": text}


def latest_session_hex_ids(target: Target) -> list[str]:
    out = run_mysql(
        target,
        """
        SELECT HEX(session_id)
          FROM server_sessions
         ORDER BY last_seen_at DESC, started_at DESC
         LIMIT 5;
        """,
    )
    return [line.strip() for line in out.splitlines() if line.strip()]


def validate_session(target: Target, session_hex: str) -> dict[str, object]:
    out = run_mysql(
        target,
        f"""
        SET @sid = UNHEX({sql_literal(session_hex)});
        SET @validation = NULL;
        CALL mmo_validate_latest_save_checkpoint_restore_v1(@sid, @validation);
        SELECT @validation;
        """,
    )
    return parse_validation(out.splitlines()[-1] if out.splitlines() else "")


def latest_validation(target: Target) -> list[dict[str, object]]:
    return [validate_session(target, session_hex) for session_hex in latest_session_hex_ids(target)]


def smoke(target: Target) -> dict[str, object]:
    idem = "step98-smoke-strict-db-continue-restore"
    sql = f"""
    SET @sid=(SELECT session_id FROM server_sessions ORDER BY last_seen_at DESC, started_at DESC LIMIT 1);
    SET @manifest_id=NULL; SET @event_id=NULL; SET @row_version_after=NULL;
    CALL mmo_create_db_save_checkpoint_v1(
      @sid,
      'character:PC_HERO:strict-db-continue-restore',
      'native_save',
      'step98_smoke',
      980,
      JSON_OBJECT(
        'smoke', true,
        'save_slot_key', 'step98-smoke-slot',
        'native_save_path', 'runtime/step98-smoke.sav',
        'display_name', 'Step98 Strict DB Continue Smoke',
        'client_world_name', 'NEWWORLD.ZEN',
        'native_save_present', true
      ),
      {sql_literal(idem)},
      @manifest_id,
      @event_id,
      @row_version_after
    );
    SET @validation=NULL;
    CALL mmo_assert_latest_save_checkpoint_restore_v1(@sid, @validation);
    SELECT @validation;
    """
    out = run_mysql(target, sql)
    validation = parse_validation(out.splitlines()[-1] if out.splitlines() else "")
    return {
        "ok": validation.get("strict_restore_ok") is True,
        "validation": validation,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Check Step98 strict DB-native Continue/restore validation bridge.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--smoke", action="store_true", help="Create/validate a DB-native save checkpoint smoke row.")
    ap.add_argument("--require-existing", action="store_true", help="Fail if the latest existing session has no strict-restore-ready DB checkpoint.")
    ap.add_argument("--output", default="")
    args = ap.parse_args()

    target = parse_mysql_url(args.url)
    result: dict[str, object] = {
        "step": "98_strict_db_continue_restore",
        "started_at": datetime.now(timezone.utc).isoformat(),
        "database": target.database,
        "status": "running",
    }

    try:
        result.update(inspect(target))
        missing = []
        for section in ("routines", "views"):
            missing.extend(f"{section}:{name}" for name, ok in result[section].items() if not ok)  # type: ignore[index]
        if missing:
            result["status"] = "failed"
            result["missing"] = missing
        elif args.smoke:
            result["smoke"] = smoke(target)
            result["latest"] = latest_validation(target)
            result["status"] = "ok" if result["smoke"]["ok"] else "failed"  # type: ignore[index]
        else:
            result["latest"] = latest_validation(target)
            if args.require_existing:
                latest = result["latest"]  # type: ignore[assignment]
                ok = bool(latest) and isinstance(latest, list) and latest[0].get("strict_restore_ok") is True
                result["status"] = "ok" if ok else "failed"
                if not ok:
                    result["error"] = "latest existing DB save checkpoint is not strict-restore ready"
            else:
                result["status"] = "ok"
    except Exception as exc:  # noqa: BLE001
        result["status"] = "failed"
        result["error"] = str(exc)
        print(f"ERROR: {exc}", file=sys.stderr)

    result["finished_at"] = datetime.now(timezone.utc).isoformat()
    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"artifact={out}")
    print("status=" + str(result["status"]))
    return 0 if result["status"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
