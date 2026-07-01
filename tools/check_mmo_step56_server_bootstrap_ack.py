#!/usr/bin/env python3
"""Check Step56 server bootstrap ACK/read-model manifest evidence.

Step56 is intentionally small: the client still only sends the Step55
`client_bootstrap_request`, while the server-side worker now checks the typed
Step53 MySQL read-model and emits a visible `bootstrap_ack` response artifact.
No client gameplay state is replaced by this checker.
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
    ("tools/run_mmo_resolved_action_worker.py", "worker emits bootstrap_ack", '"response_kind": "bootstrap_ack"'),
    ("tools/run_mmo_resolved_action_worker.py", "worker checks typed read model", "inspect_bootstrap_read_model"),
    ("tools/run_mmo_resolved_action_worker.py", "worker can write ack JSONL", "--bootstrap-ack-jsonl"),
    ("tools/run_mmo_resolved_action_worker.py", "worker stores server bootstrap manifest", "server_bootstrap_manifest_v1"),
    ("tools/check_mmo_step56_server_bootstrap_ack.py", "Step56 checker", "bootstrap_ack"),
    ("docs/llm/ai/15-step56-server-bootstrap-ack.md", "Step56 documentation", "bootstrap_ack"),
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
    return Target(parsed.hostname or "127.0.0.1", parsed.port or 3306, unquote(parsed.username or ""), unquote(parsed.password or ""), database)


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


def file_contains(root: Path, rel: str, needle: str) -> bool:
    path = root / rel
    return path.exists() and needle in path.read_text(encoding="utf-8", errors="replace")


def parse_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as inp:
        for line_no, line in enumerate(inp, 1):
            text = line.strip()
            if not text:
                continue
            try:
                obj = json.loads(text)
            except json.JSONDecodeError as exc:
                rows.append({"_invalid_json_line": line_no, "error": str(exc)})
                continue
            rows.append(obj if isinstance(obj, dict) else {"_invalid_json_line": line_no, "value": obj})
    return rows


def check_ack_rows(rows: list[dict[str, Any]], require_ready: bool) -> dict[str, Any]:
    acks = [row for row in rows if row.get("response_kind") == "bootstrap_ack"]
    invalid_json = [row for row in rows if "_invalid_json_line" in row]
    ready_acks = [row for row in acks if row.get("accepted") is True and row.get("read_model", {}).get("verdict", {}).get("ready_for_bootstrap_ack") is True]
    status = "passed" if acks and not invalid_json and (ready_acks or not require_ready) else "failed"
    return {
        "status": status,
        "rows": len(rows),
        "bootstrap_ack_count": len(acks),
        "ready_bootstrap_ack_count": len(ready_acks),
        "invalid_json_count": len(invalid_json),
        "last_ack": acks[-1] if acks else None,
    }


def mysql_ack_evidence(target: Target, session_key: str, require_ready: bool) -> dict[str, Any]:
    where = "action_kind='client_bootstrap_request'"
    if session_key:
        where += f" AND idempotency_key LIKE {sql_literal(session_key + ':%')}"
    out = run_mysql(
        target,
        f"""
        SELECT action_kind,
               status,
               idempotency_key,
               COALESCE(JSON_UNQUOTE(JSON_EXTRACT(result_payload,'$.response_kind')),''),
               COALESCE(JSON_UNQUOTE(JSON_EXTRACT(result_payload,'$.bootstrap_status')),''),
               COALESCE(JSON_UNQUOTE(JSON_EXTRACT(result_payload,'$.accepted')),''),
               COALESCE(JSON_UNQUOTE(JSON_EXTRACT(result_payload,'$.read_model.verdict.ready_for_bootstrap_ack')),'')
          FROM mmo_server_action_outbox
         WHERE {where}
         ORDER BY requested_at DESC, action_id DESC
         LIMIT 20;
        """,
    )
    rows: list[dict[str, Any]] = []
    for line in out.splitlines():
        parts = line.split("\t")
        if len(parts) >= 7:
            rows.append(
                {
                    "action_kind": parts[0],
                    "status": parts[1],
                    "idempotency_key": parts[2],
                    "response_kind": parts[3],
                    "bootstrap_status": parts[4],
                    "accepted": parts[5],
                    "ready_for_bootstrap_ack": parts[6],
                }
            )
    ack_rows = [row for row in rows if row["response_kind"] == "bootstrap_ack"]
    ready_rows = [row for row in ack_rows if row["accepted"] == "true" and row["ready_for_bootstrap_ack"] == "true"]
    status = "passed" if ack_rows and (ready_rows or not require_ready) else "failed"
    return {"status": status, "rows": rows, "bootstrap_ack_count": len(ack_rows), "ready_bootstrap_ack_count": len(ready_rows)}


def main() -> int:
    ap = argparse.ArgumentParser(description="Check Step56 server bootstrap ACK/read-model manifest evidence.")
    ap.add_argument("--root", default=str(ROOT))
    ap.add_argument("--bootstrap-ack-jsonl", help="optional worker-produced bootstrap_ack JSONL artifact")
    ap.add_argument("--url", help="optional mysql:// URL to inspect outbox result_payload")
    ap.add_argument("--session-key", default="", help="optional idempotency key prefix for MySQL evidence")
    ap.add_argument("--allow-not-ready", action="store_true", help="accept a bootstrap_ack NACK when read model is not ready")
    ap.add_argument("--output", help="optional JSON report path")
    args = ap.parse_args()

    root = Path(args.root)
    static = {label: file_contains(root, rel, needle) for rel, label, needle in STATIC_CHECKS}
    report: dict[str, Any] = {
        "step": 56,
        "status": "passed",
        "static_checks": static,
        "important": {
            "client_still_only_sends_client_bootstrap_request": True,
            "server_ack_is_a_visible_artifact_not_full_replication": True,
            "no_world_gameplay_state_is_replaced_by_this_step": True,
        },
    }

    if not all(static.values()):
        report["status"] = "failed"
        report["missing_static_checks"] = [label for label, ok in static.items() if not ok]

    require_ready = not args.allow_not_ready
    if args.bootstrap_ack_jsonl:
        path = Path(args.bootstrap_ack_jsonl)
        if not path.exists():
            report["status"] = "failed"
            report["bootstrap_ack_jsonl"] = {"status": "failed", "error": f"missing file: {path}"}
        else:
            evidence = check_ack_rows(parse_jsonl(path), require_ready)
            report["bootstrap_ack_jsonl"] = evidence
            if evidence["status"] != "passed":
                report["status"] = "failed"

    if args.url:
        evidence = mysql_ack_evidence(parse_mysql_url(args.url), args.session_key, require_ready)
        report["mysql_outbox"] = evidence
        if evidence["status"] != "passed":
            report["status"] = "failed"

    print("Step56 server bootstrap ACK check")
    print("static:")
    for label, ok in static.items():
        print(f"  {label}: {'ok' if ok else 'missing'}")
    if "bootstrap_ack_jsonl" in report:
        e = report["bootstrap_ack_jsonl"]
        print(f"bootstrap_ack_jsonl={e.get('status')} acks={e.get('bootstrap_ack_count', 0)} ready={e.get('ready_bootstrap_ack_count', 0)}")
    if "mysql_outbox" in report:
        e = report["mysql_outbox"]
        print(f"mysql_outbox={e.get('status')} acks={e.get('bootstrap_ack_count', 0)} ready={e.get('ready_bootstrap_ack_count', 0)}")
    print("status=" + report["status"])

    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"artifact={out}")

    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
