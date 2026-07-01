#!/usr/bin/env python3
"""Run Step37 bookstand/script-XP JSONL through the dev server boundary into MySQL.

Pipeline:
  client JSONL from real OpenGothic C++ hooks
    -> temporary rewritten Step37-only JSONL
    -> run_mmo_action_receiver.py UDP receiver with --enqueue-outbox
    -> run_mmo_resolved_action_worker.py
    -> check_mmo_step37_bookstand_script_xp.py

This is a dev evidence runner. It is intentionally outside the game process and
keeps the final production rule intact: OpenGothic does not call MySQL directly.
"""
from __future__ import annotations

import argparse
import json
import os
import signal
import socket
import subprocess
import sys
import time
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

STEP37_KINDS = {
    "set_script_int",
    "adjust_progression",
    "apply_experience_reward",
    "update_quest",
    "set_known_dialog",
}
XP_KINDS = {"adjust_progression", "apply_experience_reward"}


@dataclass
class CommandResult:
    name: str
    argv: list[str]
    returncode: int
    stdout_tail: str = ""
    stderr_tail: str = ""


def parse_endpoint(value: str) -> tuple[str, int]:
    if ":" not in value:
        raise argparse.ArgumentTypeError("expected host:port")
    host, port_text = value.rsplit(":", 1)
    if not host:
        raise argparse.ArgumentTypeError("missing host")
    try:
        port = int(port_text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("invalid port") from exc
    if port <= 0 or port > 65535:
        raise argparse.ArgumentTypeError("port out of range")
    return host, port


def rewrite_session(obj: dict[str, Any], session_key: str) -> dict[str, Any]:
    out = json.loads(json.dumps(obj, ensure_ascii=False))
    idem = str(out.get("idempotency_key") or "")
    if idem:
        suffix = idem.split(":", 1)[1] if ":" in idem else idem
        out["idempotency_key"] = f"{session_key}:{suffix}"
    else:
        out["idempotency_key"] = f"{session_key}:missing:{out.get('action_kind','unknown')}:{out.get('local_sequence',0)}"
    return out


def load_step37_rows(path: Path, session_key: str, rewrite: bool) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line_no, line in enumerate(f, 1):
            text = line.strip()
            if not text:
                continue
            try:
                obj = json.loads(text)
            except json.JSONDecodeError as exc:
                raise SystemExit(f"{path}:{line_no}: invalid JSON: {exc}") from exc
            if not isinstance(obj, dict):
                raise SystemExit(f"{path}:{line_no}: expected JSON object")
            if str(obj.get("action_kind") or "") not in STEP37_KINDS:
                continue
            rows.append(rewrite_session(obj, session_key) if rewrite else obj)
    return rows


def validate_step37_rows(rows: list[dict[str, Any]], session_key: str) -> None:
    if not rows:
        raise SystemExit("no Step37 actions found in client JSONL")
    counts = Counter(str(r.get("action_kind") or "") for r in rows)
    if counts["set_script_int"] <= 0:
        raise SystemExit("client JSONL has no set_script_int rows")
    if sum(counts[k] for k in XP_KINDS) <= 0:
        raise SystemExit("client JSONL has no adjust_progression/apply_experience_reward rows")
    idem_counts = Counter(str(r.get("idempotency_key") or "") for r in rows)
    missing_idem = [r for r in rows if not r.get("idempotency_key")]
    dupes = {k: v for k, v in idem_counts.items() if k and v > 1}
    wrong_prefix = [k for k in idem_counts if k and not k.startswith(session_key + ":")]
    if missing_idem:
        raise SystemExit(f"client JSONL has rows without idempotency_key: {len(missing_idem)}")
    if dupes:
        raise SystemExit("client JSONL idempotency duplicates after rewrite: " + json.dumps(dupes, sort_keys=True))
    if wrong_prefix:
        raise SystemExit(f"client JSONL contains idempotency keys outside requested session prefix: {wrong_prefix[:3]}")


def write_jsonl(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row, ensure_ascii=False, separators=(",", ":")) + "\n")


def tail(text: str, limit: int = 6000) -> str:
    if len(text) <= limit:
        return text
    return text[-limit:]


def run_command(name: str, argv: list[str], *, timeout: int | None = None) -> CommandResult:
    proc = subprocess.run(argv, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    return CommandResult(name=name, argv=argv, returncode=proc.returncode, stdout_tail=tail(proc.stdout), stderr_tail=tail(proc.stderr))


def send_udp(rows: list[dict[str, Any]], endpoint: tuple[str, int], sleep_ms: int, duplicate_replay: bool) -> int:
    host, port = endpoint
    repeats = 2 if duplicate_replay else 1
    sent = 0
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        for _ in range(repeats):
            for row in rows:
                payload = json.dumps(row, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
                sock.sendto(payload, (host, port))
                sent += 1
                if sleep_ms > 0:
                    time.sleep(sleep_ms / 1000.0)
    finally:
        sock.close()
    return sent


def stop_process(proc: subprocess.Popen[str]) -> None:
    if proc.poll() is not None:
        return
    try:
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=5)
    except Exception:
        try:
            proc.kill()
        except Exception:
            pass


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Run Step37 bookstand/script-XP JSONL through receiver/outbox/worker/MySQL checker")
    ap.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    ap.add_argument("--client-jsonl", required=True, type=Path, help="real OpenGothic Step37 client JSONL")
    ap.add_argument("--session-key", required=True, help="session/idempotency prefix for this e2e DB run")
    ap.add_argument("--db-session-key", default="", help="server_sessions.session_key; defaults to --session-key")
    ap.add_argument("--account-name", default="local-import")
    ap.add_argument("--character-key", default="PC_HERO")
    ap.add_argument("--receiver-bind", type=parse_endpoint, default=parse_endpoint("127.0.0.1:29777"))
    ap.add_argument("--server-jsonl", type=Path, default=Path("runtime/mmo_server_actions_step37_bookstand_e2e.jsonl"))
    ap.add_argument("--reject-jsonl", type=Path, default=Path("runtime/mmo_server_rejects_step37_bookstand_e2e.jsonl"))
    ap.add_argument("--rewritten-client-jsonl", type=Path, default=Path("runtime/mmo_client_actions_step37_bookstand_e2e.jsonl"))
    ap.add_argument("--sqlite", type=Path, default=Path("runtime/g2notr.sqlite"))
    ap.add_argument("--output", type=Path, default=Path("runtime/mmo_step37_bookstand_mysql_e2e.json"))
    ap.add_argument("--checker-output", type=Path, default=Path("runtime/mmo_step37_bookstand_script_xp_e2e.json"))
    ap.add_argument("--tools-dir", type=Path, default=Path("tools"))
    ap.add_argument("--receiver-startup-delay-ms", type=int, default=750)
    ap.add_argument("--send-sleep-ms", type=int, default=5)
    ap.add_argument("--worker-id", default="dev-step37-bookstand-e2e-worker")
    ap.add_argument("--worker-max-actions", type=int, default=0, help="defaults to number of unique Step37 rows")
    ap.add_argument("--reset-matching-failed", action="store_true", help="reset failed/dead_letter/claimed rows for this session before worker dispatch; use after fixing worker/procedure mapping and rerunning the same session-key")
    ap.add_argument("--no-rewrite-session-key", action="store_true", help="use idempotency keys exactly as present in client JSONL")
    ap.add_argument("--duplicate-replay", action="store_true", help="send the same UDP packets twice; receiver should de-dupe within this run")
    ap.add_argument("--no-jsonl-correlation", action="store_true", help="do not require client/server JSONL fingerprint correlation in final checker")
    ap.add_argument("--dry-run", action="store_true", help="write rewritten JSONL and print plan, do not start receiver/worker/checker")
    args = ap.parse_args(argv)

    rows = load_step37_rows(args.client_jsonl, args.session_key, rewrite=not args.no_rewrite_session_key)
    validate_step37_rows(rows, args.session_key)
    write_jsonl(args.rewritten_client_jsonl, rows)

    counts = Counter(str(r.get("action_kind") or "") for r in rows)
    result: dict[str, Any] = {
        "tool": "run_mmo_step37_bookstand_mysql_e2e.py",
        "status": "planned" if args.dry_run else "running",
        "session_key": args.session_key,
        "client_jsonl": str(args.client_jsonl),
        "rewritten_client_jsonl": str(args.rewritten_client_jsonl),
        "server_jsonl": str(args.server_jsonl),
        "reject_jsonl": str(args.reject_jsonl),
        "checker_output": str(args.checker_output),
        "rows": len(rows),
        "kind_counts": dict(sorted(counts.items())),
        "commands": [],
    }

    if args.dry_run:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
        print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
        print(f"artifact={args.output}")
        return 0

    receiver_packets = len(rows) * (2 if args.duplicate_replay else 1)
    receiver_cmd = [
        sys.executable,
        str(args.tools_dir / "run_mmo_action_receiver.py"),
        "--bind", f"{args.receiver_bind[0]}:{args.receiver_bind[1]}",
        "--jsonl", str(args.server_jsonl),
        "--reject-jsonl", str(args.reject_jsonl),
        "--require-session", args.session_key,
        "--mysql-url", args.url,
        "--account-name", args.account_name,
        "--character-key", args.character_key,
        "--db-session-key", args.db_session_key or args.session_key,
        "--enqueue-outbox",
        "--strict-dispatch-payload",
        "--truncate",
        "--max-packets", str(receiver_packets),
        "--print-every", "1",
    ]

    receiver_stdout = ""
    receiver_stderr = ""
    receiver_rc = 999
    proc: subprocess.Popen[str] | None = None
    try:
        proc = subprocess.Popen(receiver_cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        time.sleep(max(args.receiver_startup_delay_ms, 0) / 1000.0)
        if proc.poll() is not None:
            out, err = proc.communicate(timeout=2)
            receiver_stdout, receiver_stderr, receiver_rc = out, err, proc.returncode
            raise RuntimeError(f"receiver exited before replay: rc={receiver_rc}")
        sent = send_udp(rows, args.receiver_bind, args.send_sleep_ms, args.duplicate_replay)
        try:
            receiver_stdout, receiver_stderr = proc.communicate(timeout=30)
        except subprocess.TimeoutExpired:
            stop_process(proc)
            receiver_stdout, receiver_stderr = proc.communicate(timeout=5)
        receiver_rc = int(proc.returncode or 0)
        result["udp_sent"] = sent
    except Exception as exc:
        if proc is not None:
            stop_process(proc)
        result["status"] = "failed"
        result["receiver_error"] = str(exc)
    result["commands"].append(asdict(CommandResult("receiver", receiver_cmd, receiver_rc, tail(receiver_stdout), tail(receiver_stderr))))

    if result.get("status") == "failed" or receiver_rc != 0:
        result["status"] = "failed"
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
        print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
        print(f"artifact={args.output}")
        return 2

    worker_cmd = [
        sys.executable,
        str(args.tools_dir / "run_mmo_resolved_action_worker.py"),
        "--url", args.url,
        "--worker-id", args.worker_id,
        "--session-key", args.session_key,
        "--max-actions", str(args.worker_max_actions or len(rows)),
    ]
    if args.reset_matching_failed:
        worker_cmd.append("--reset-matching-failed")
    worker = run_command("worker", worker_cmd)
    result["commands"].append(asdict(worker))
    if worker.returncode != 0:
        result["status"] = "failed"
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
        print(worker.stdout_tail, end="")
        print(worker.stderr_tail, file=sys.stderr, end="")
        print(f"artifact={args.output}")
        return worker.returncode or 2

    checker_cmd = [
        sys.executable,
        str(args.tools_dir / "check_mmo_step37_bookstand_script_xp.py"),
        "--url", args.url,
        "--session-key", args.session_key,
        "--client-jsonl", str(args.rewritten_client_jsonl),
        "--server-jsonl", str(args.server_jsonl),
        "--output", str(args.checker_output),
    ]
    if args.sqlite and args.sqlite.exists():
        checker_cmd += ["--sqlite", str(args.sqlite)]
    if not args.no_jsonl_correlation:
        checker_cmd.append("--require-jsonl-correlation")
    checker = run_command("checker", checker_cmd)
    result["commands"].append(asdict(checker))
    result["status"] = "passed" if checker.returncode == 0 else "failed"

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")

    # Print the important child outputs in order, so terminal logs remain useful.
    if receiver_stdout:
        print(receiver_stdout, end="")
    if receiver_stderr:
        print(receiver_stderr, file=sys.stderr, end="")
    if worker.stdout_tail:
        print(worker.stdout_tail, end="")
    if worker.stderr_tail:
        print(worker.stderr_tail, file=sys.stderr, end="")
    if checker.stdout_tail:
        print(checker.stdout_tail, end="")
    if checker.stderr_tail:
        print(checker.stderr_tail, file=sys.stderr, end="")
    print(f"status={result['status']}")
    print(f"artifact={args.output}")
    print(f"checker_artifact={args.checker_output}")
    return checker.returncode


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
