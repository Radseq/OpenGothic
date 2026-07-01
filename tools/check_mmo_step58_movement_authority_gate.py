#!/usr/bin/env python3
"""Check Step58 movement proposal authority-gate evidence.

Step58 keeps old single-player and the Step56b no-op behavior unchanged by
requiring --enable-movement-authority-gate. When enabled, movement_proposal rows
are validated as server intents, accepted proposals are persisted as bounded
character checkpoints, and rejected proposals produce ACK/NACK evidence without
mutating gameplay state.
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

STATIC_CHECKS = (
    ("tools/run_mmo_resolved_action_worker.py", "Step58 authority flag", "--enable-movement-authority-gate"),
    ("tools/run_mmo_resolved_action_worker.py", "movement authority ACK", "movement_authority_ack"),
    ("tools/run_mmo_resolved_action_worker.py", "movement validator", "movement_proposal_authority_gate_v1"),
    ("tools/run_mmo_resolved_action_worker.py", "accepted proposal persists checkpoint", "accepted_checkpoint_persisted"),
    ("tools/run_mmo_resolved_action_worker.py", "rejected proposal no mutation", "rejected_no_mutation"),
    ("tools/run_mmo_resolved_action_worker.py", "movement authority JSONL output", "--movement-authority-jsonl"),
    ("tools/check_mmo_step58_movement_authority_gate.py", "Step58 checker", "movement_authority_ack"),
    ("docs/llm/ai/18-step58-movement-authority-gate.md", "Step58 documentation", "movement_proposal -> server authority gate"),
)


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
    result: dict[str, bool] = {}
    for rel, label, needle in STATIC_CHECKS:
        path = root / rel
        result[label] = path.exists() and needle in path.read_text(encoding="utf-8", errors="replace")
    return result


def load_authority_acks(path: Path) -> tuple[list[dict[str, Any]], list[str]]:
    rows: list[dict[str, Any]] = []
    errors: list[str] = []
    if not path.exists():
        return rows, [f"JSONL does not exist: {path}"]
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line_no, raw in enumerate(f, 1):
            text = raw.strip()
            if not text:
                continue
            try:
                obj = json.loads(text)
            except json.JSONDecodeError as exc:
                errors.append(f"line {line_no}: bad JSON: {exc}")
                continue
            if not isinstance(obj, dict):
                errors.append(f"line {line_no}: row is not an object")
                continue
            if obj.get("response_kind") != "movement_authority_ack":
                continue
            rows.append(obj)
    return rows, errors


def validate_authority_jsonl(path: Path, min_rows: int, min_accepted: int, min_rejected: int) -> dict[str, Any]:
    rows, errors = load_authority_acks(path)
    accepted = [row for row in rows if row.get("accepted") is True]
    rejected = [row for row in rows if row.get("accepted") is False]
    accepted_missing_event = [row for row in accepted if not row.get("event_uuid")]
    rejected_with_event = [row for row in rejected if row.get("event_uuid")]
    rejected_mutated = [row for row in rejected if row.get("authority", {}).get("rejected_proposal_mutated_no_gameplay_state") is not True]

    if len(rows) < min_rows:
        errors.append(f"movement_authority_ack rows {len(rows)} < {min_rows}")
    if len(accepted) < min_accepted:
        errors.append(f"accepted movement_authority_ack rows {len(accepted)} < {min_accepted}")
    if len(rejected) < min_rejected:
        errors.append(f"rejected movement_authority_ack rows {len(rejected)} < {min_rejected}")
    if accepted_missing_event:
        errors.append(f"accepted rows without event_uuid={len(accepted_missing_event)}")
    if rejected_with_event:
        errors.append(f"rejected rows with event_uuid={len(rejected_with_event)}")
    if rejected_mutated:
        errors.append(f"rejected rows missing no-mutation authority marker={len(rejected_mutated)}")

    return {
        "status": "failed" if errors else "passed",
        "path": str(path),
        "rows": len(rows),
        "accepted": len(accepted),
        "rejected": len(rejected),
        "errors": errors,
        "samples": rows[:4],
    }


def mysql_checks(target: Target, session_key: str) -> dict[str, Any]:
    routine_rows = run_mysql(
        target,
        """
        SELECT routine_name
          FROM information_schema.ROUTINES
         WHERE routine_schema=DATABASE()
           AND routine_name='mmo_checkpoint_character_state';
        """,
    )
    has_checkpoint_proc = "mmo_checkpoint_character_state" in {line.strip() for line in routine_rows.splitlines() if line.strip()}

    counts: dict[str, int] = {}
    if session_key:
        raw = run_mysql(
            target,
            f"""
            SELECT
              SUM(action_kind='movement_proposal' AND status='applied') AS movement_applied,
              SUM(action_kind='movement_proposal' AND status='failed') AS movement_failed,
              SUM(action_kind='movement_proposal' AND status='applied' AND JSON_UNQUOTE(JSON_EXTRACT(result_payload,'$.response_kind'))='movement_authority_ack') AS authority_acks,
              SUM(action_kind='movement_proposal' AND status='applied' AND JSON_UNQUOTE(JSON_EXTRACT(result_payload,'$.accepted'))='true') AS accepted_acks,
              SUM(action_kind='movement_proposal' AND status='applied' AND JSON_UNQUOTE(JSON_EXTRACT(result_payload,'$.accepted'))='false') AS rejected_acks
              FROM mmo_server_action_outbox
             WHERE idempotency_key LIKE {sql_literal(session_key + ':%')};
            """,
        )
        fields = (raw.splitlines()[-1].split("\t") if raw else ["0", "0", "0", "0", "0"])
        names = ("movement_applied", "movement_failed", "authority_acks", "accepted_acks", "rejected_acks")
        for name, value in zip(names, fields):
            counts[name] = int(value or 0)

    errors: list[str] = []
    if not has_checkpoint_proc:
        errors.append("missing mmo_checkpoint_character_state")
    if session_key and counts.get("movement_failed", 0) > 0:
        errors.append(f"failed movement_proposal rows={counts['movement_failed']}")
    return {
        "status": "failed" if errors else "passed",
        "database": target.database,
        "session_key": session_key,
        "has_mmo_checkpoint_character_state": has_checkpoint_proc,
        "counts": counts,
        "errors": errors,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Check Step58 movement authority gate.")
    ap.add_argument("--root", default=str(ROOT))
    ap.add_argument("--movement-authority-jsonl", help="Worker-produced Step58 movement_authority_ack JSONL")
    ap.add_argument("--min-rows", type=int, default=1)
    ap.add_argument("--min-accepted", type=int, default=0)
    ap.add_argument("--min-rejected", type=int, default=0)
    ap.add_argument("--url", help="Optional mysql:// URL for procedure/outbox checks")
    ap.add_argument("--session-key", default="", help="Optional session prefix for MySQL outbox checks")
    ap.add_argument("--output")
    args = ap.parse_args()

    root = Path(args.root).resolve()
    checks = static_checks(root)
    errors = [label for label, ok in checks.items() if not ok]
    result: dict[str, Any] = {"status": "passed", "static": checks, "errors": errors}

    if args.movement_authority_jsonl:
        authority = validate_authority_jsonl(Path(args.movement_authority_jsonl), args.min_rows, args.min_accepted, args.min_rejected)
        result["movement_authority_jsonl"] = authority
        if authority["status"] != "passed":
            errors.append("movement_authority_jsonl")

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

    print("Step58 movement authority gate check")
    for label, ok in checks.items():
        print(f"  {label}: {'ok' if ok else 'missing'}")
    print("status=" + result["status"])
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
