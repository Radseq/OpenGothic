#!/usr/bin/env python3
"""Check Step57 clean-reset hardening and movement checkpoint ACK surface.

This checker is intentionally light: it verifies that the clean MySQL reset path
installs the Step56b progress bridge and runs collation normalization, and it can
validate worker-produced movement_checkpoint_ack JSONL rows.
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

ROOT = Path(__file__).resolve().parents[1]
REQUIRED_ROUTINES = [
    "mmo_set_character_script_int",
    "mmo_update_character_quest",
    "mmo_set_character_known_dialog",
    "mmo_checkpoint_character_state",
]
TARGET_COLLATION = "utf8mb4_0900_ai_ci"


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


def static_checks(root: Path) -> dict[str, bool]:
    reset = (root / "tools" / "reset_mmo_mysql_from_chapter1_start.py").read_text(encoding="utf-8", errors="replace")
    wrapper = (root / "tools" / "run_mmo_step55_clean_mysql_from_pre_xardas.py").read_text(encoding="utf-8", errors="replace")
    worker = (root / "tools" / "run_mmo_resolved_action_worker.py").read_text(encoding="utf-8", errors="replace")
    return {
        "reset_applies_step56b_progress_bridge": "STEP56B_PROGRESS_BRIDGE_SQL" in reset and "with-step56b-progress-bridge" in reset,
        "reset_runs_collation_normalizer": "normalize_mmo_mysql_collation.py" in reset and "--normalize-collation" in reset,
        "step55_wrapper_passes_step56b_flag": "skip-step56b-progress-bridge" in wrapper and "--no-with-step56b-progress-bridge" in wrapper,
        "step55_wrapper_passes_collation_flag": "skip-collation-normalize" in wrapper and "--no-normalize-collation" in wrapper,
        "worker_emits_checkpoint_ack": "movement_checkpoint_ack" in worker and "--checkpoint-ack-jsonl" in worker,
        "normalizer_exists": (root / "tools" / "normalize_mmo_mysql_collation.py").exists(),
        "step56b_sql_exists": (root / "server" / "sql" / "step56b_clean_db_progress_bridge.sql").exists(),
    }


def validate_ack_jsonl(path: Path, *, min_rows: int, allow_not_accepted: bool) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    bad_json = 0
    if not path.exists():
        return {"status": "failed", "path": str(path), "error": "jsonl does not exist"}
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line_no, raw in enumerate(f, 1):
            text = raw.strip()
            if not text:
                continue
            try:
                obj = json.loads(text)
            except json.JSONDecodeError:
                bad_json += 1
                continue
            if isinstance(obj, dict) and obj.get("response_kind") == "movement_checkpoint_ack":
                rows.append(obj)
    errors: list[str] = []
    if bad_json:
        errors.append(f"bad JSON rows={bad_json}")
    if len(rows) < min_rows:
        errors.append(f"movement_checkpoint_ack rows {len(rows)} < {min_rows}")
    if not allow_not_accepted:
        not_accepted = [r for r in rows if r.get("accepted") is not True or not r.get("event_uuid")]
        if not_accepted:
            errors.append(f"not accepted or missing event_uuid rows={len(not_accepted)}")
    return {"status": "failed" if errors else "passed", "path": str(path), "rows": len(rows), "errors": errors, "samples": rows[:3]}


def mysql_checks(target: Target, session_key: str) -> dict[str, Any]:
    routine_rows = run_mysql(
        target,
        """
        SELECT routine_name
          FROM information_schema.ROUTINES
         WHERE routine_schema=DATABASE()
           AND routine_name IN ('mmo_set_character_script_int','mmo_update_character_quest','mmo_set_character_known_dialog','mmo_checkpoint_character_state')
         ORDER BY routine_name;
        """,
    )
    routines = sorted(line.strip() for line in routine_rows.splitlines() if line.strip())
    collation_rows = run_mysql(
        target,
        f"""
        SELECT COALESCE(COLLATION_NAME,''), COUNT(*)
          FROM information_schema.COLUMNS
         WHERE TABLE_SCHEMA=DATABASE()
           AND COLLATION_NAME IS NOT NULL
           AND COLLATION_NAME <> {sql_literal(TARGET_COLLATION)}
         GROUP BY COLLATION_NAME
         ORDER BY COLLATION_NAME;
        """,
    )
    non_target_collations = {parts[0]: int(parts[1]) for parts in (line.split("\t") for line in collation_rows.splitlines() if line.strip()) if len(parts) >= 2}
    checkpoint_count = None
    movement_noop_count = None
    if session_key:
        checkpoint_raw = run_mysql(
            target,
            f"""
            SELECT COUNT(*)
              FROM mmo_server_action_outbox
             WHERE idempotency_key LIKE {sql_literal(session_key + ':%')}
               AND action_kind='character_checkpoint'
               AND status='applied';
            """,
        )
        movement_raw = run_mysql(
            target,
            f"""
            SELECT COUNT(*)
              FROM mmo_server_action_outbox
             WHERE idempotency_key LIKE {sql_literal(session_key + ':%')}
               AND action_kind='movement_proposal'
               AND status='applied'
               AND JSON_EXTRACT(result_payload,'$.applied_noop') = true;
            """,
        )
        checkpoint_count = int((checkpoint_raw or "0").splitlines()[-1])
        movement_noop_count = int((movement_raw or "0").splitlines()[-1])
    missing = [name for name in REQUIRED_ROUTINES if name not in routines]
    return {
        "status": "passed" if not missing and not non_target_collations else "failed",
        "database": target.database,
        "routines": routines,
        "missing_routines": missing,
        "non_target_collations": non_target_collations,
        "session_key": session_key,
        "applied_character_checkpoints": checkpoint_count,
        "applied_noop_movement_proposals": movement_noop_count,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Check Step57 clean reset hardening and movement checkpoint ACK.")
    ap.add_argument("--root", default=str(ROOT))
    ap.add_argument("--url", help="Optional mysql:// URL for DB object/collation checks")
    ap.add_argument("--session-key", default="", help="Optional session prefix for MySQL checkpoint/noop counts")
    ap.add_argument("--checkpoint-ack-jsonl", help="Optional worker-produced movement checkpoint ACK JSONL")
    ap.add_argument("--min-acks", type=int, default=1)
    ap.add_argument("--allow-not-accepted", action="store_true")
    ap.add_argument("--output")
    args = ap.parse_args()

    root = Path(args.root).resolve()
    checks = static_checks(root)
    errors = [name for name, ok in checks.items() if not ok]
    result: dict[str, Any] = {"status": "passed", "static": checks, "errors": errors}

    if args.checkpoint_ack_jsonl:
        ack = validate_ack_jsonl(Path(args.checkpoint_ack_jsonl), min_rows=args.min_acks, allow_not_accepted=args.allow_not_accepted)
        result["checkpoint_ack_jsonl"] = ack
        if ack["status"] != "passed":
            errors.append("checkpoint_ack_jsonl")

    if args.url:
        db = mysql_checks(parse_mysql_url(args.url), args.session_key)
        result["mysql"] = db
        if db["status"] != "passed":
            errors.append("mysql")

    result["status"] = "failed" if errors else "passed"
    result["errors"] = errors

    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"artifact={out}")

    print("Step57 clean reset/checkpoint ACK check")
    for name, ok in checks.items():
        print(f"  {name}: {'ok' if ok else 'missing'}")
    print("status=" + result["status"])
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
