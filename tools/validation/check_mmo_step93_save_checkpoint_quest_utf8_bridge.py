#!/usr/bin/env python3
"""Check Step93 quest UTF-8/idempotency bridge."""
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

REQUIRED_TABLES = ("character_quest_audit",)
REQUIRED_ROUTINES = ("mmo_update_character_quest",)


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
    cmd = [exe, "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci", "--batch", "--raw", "--skip-column-names", "-h", target.host, "-P", str(target.port), "-u", target.user]
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
    if kind == "table":
        sql = f"SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_type='BASE TABLE' AND table_name={sql_literal(name)};"
    elif kind == "routine":
        sql = f"SELECT COUNT(*) FROM information_schema.routines WHERE routine_schema=DATABASE() AND routine_name={sql_literal(name)};"
    else:
        raise ValueError(kind)
    return int((run_mysql(target, sql) or "0").splitlines()[-1])


def inspect(target: Target) -> dict[str, object]:
    return {
        "tables": {name: count_object(target, "table", name) == 1 for name in REQUIRED_TABLES},
        "routines": {name: count_object(target, "routine", name) == 1 for name in REQUIRED_ROUTINES},
    }


def smoke(target: Target) -> dict[str, object]:
    idem = "step93-smoke-quest-utf8"
    text = "Zażółć gęślą jaźń"
    sql = f"""
    SET @sid=(SELECT session_id FROM server_sessions ORDER BY started_at DESC LIMIT 1);
    SET @event_id=NULL;
    CALL mmo_update_character_quest(@sid,'step93.smoke.quest',{sql_literal(text)},'running',1,JSON_ARRAY({sql_literal(text)}),123,JSON_OBJECT('smoke_text',{sql_literal(text)}),{sql_literal(idem)},@event_id);
    CALL mmo_update_character_quest(@sid,'step93.smoke.quest',{sql_literal(text)},'running',1,JSON_ARRAY({sql_literal(text)}),123,JSON_OBJECT('smoke_text',{sql_literal(text)}),{sql_literal(idem)},@event_id);
    SELECT CONCAT(BIN_TO_UUID(@event_id,1),'\t',q.quest_key,'\t',JSON_UNQUOTE(JSON_EXTRACT(q.text_entries,'$[0]')),'\t',wej.event_class)
      FROM characters c
      JOIN character_quests q ON q.character_id=c.character_id
      JOIN world_event_journal wej ON wej.event_id=@event_id
     WHERE q.quest_key='step93.smoke.quest'
     LIMIT 1;
    """
    out = run_mysql(target, sql)
    last = out.splitlines()[-1] if out.splitlines() else ""
    parts = last.split("\t")
    return {"ok": len(parts) == 4 and parts[2] == text and parts[3] in {"quest", "character"}, "row": last}


def main() -> int:
    ap = argparse.ArgumentParser(description="Check Step93 quest UTF-8/idempotency bridge.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--smoke", action="store_true")
    ap.add_argument("--output", default="")
    args = ap.parse_args()

    target = parse_mysql_url(args.url)
    result: dict[str, object] = {"step": "93_save_checkpoint_quest_utf8_bridge", "started_at": datetime.now(timezone.utc).isoformat(), "database": target.database, "status": "running"}
    try:
        result.update(inspect(target))
        missing = [name for name, ok in result["tables"].items() if not ok] + [name for name, ok in result["routines"].items() if not ok]  # type: ignore[index]
        if missing:
            result["status"] = "failed"
            result["missing"] = missing
        elif args.smoke:
            result["smoke"] = smoke(target)
            result["status"] = "ok" if result["smoke"]["ok"] else "failed"  # type: ignore[index]
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
