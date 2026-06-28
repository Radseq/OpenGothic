#!/usr/bin/env python3
"""Replay real Step38 OpenGothic JSONL through receiver -> outbox -> worker -> MySQL checker."""
from __future__ import annotations

import argparse
import json
import signal
import socket
import subprocess
import sys
import time
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

STEP38_KINDS = {
    "trade_buy_from_npc",
    "trade_sell_to_npc",
    "consume_mana",
    "consume_item",
    "apply_character_damage",
    "apply_world_entity_damage",
    "mark_npc_dead",
}

@dataclass
class CommandResult:
    name: str
    argv: list[str]
    returncode: int
    stdout_tail: str = ""
    stderr_tail: str = ""


def parse_endpoint(value: str) -> tuple[str, int]:
    host, port = value.rsplit(":", 1)
    return host, int(port)


def rewrite_session(obj: dict[str, Any], session_key: str) -> dict[str, Any]:
    out = json.loads(json.dumps(obj, ensure_ascii=False))
    idem = str(out.get("idempotency_key") or "")
    suffix = idem.split(":", 1)[1] if ":" in idem else f"{out.get('action_kind','unknown')}:{out.get('local_sequence',0)}"
    out["idempotency_key"] = f"{session_key}:{suffix}"
    return out


def load_rows(path: Path, session_key: str, rewrite: bool, require_kind: set[str]) -> list[dict[str, Any]]:
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
                continue
            kind = str(obj.get("action_kind") or "")
            if kind not in STEP38_KINDS:
                continue
            if require_kind and kind not in require_kind:
                continue
            rows.append(rewrite_session(obj, session_key) if rewrite else obj)
    if not rows:
        raise SystemExit("no Step38 actions found in client JSONL")
    return rows


def validate_rows(rows: list[dict[str, Any]], session_key: str) -> None:
    idem = Counter(str(r.get("idempotency_key") or "") for r in rows)
    dupes = {k: v for k, v in idem.items() if k and v > 1}
    wrong = [k for k in idem if k and not k.startswith(session_key + ":")]
    if dupes:
        raise SystemExit("duplicate idempotency after rewrite: " + json.dumps(dupes, sort_keys=True))
    if wrong:
        raise SystemExit("wrong idempotency prefix: " + json.dumps(wrong[:5]))


def write_jsonl(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row, ensure_ascii=False, separators=(",", ":")) + "\n")


def tail(text: str, limit: int = 6000) -> str:
    return text if len(text) <= limit else text[-limit:]


def run_command(name: str, argv: list[str], timeout: int | None = None) -> CommandResult:
    proc = subprocess.run(argv, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    return CommandResult(name, argv, proc.returncode, tail(proc.stdout), tail(proc.stderr))


def send_udp(rows: list[dict[str, Any]], endpoint: tuple[str, int], sleep_ms: int) -> int:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sent = 0
    try:
        for row in rows:
            sock.sendto(json.dumps(row, ensure_ascii=False, separators=(",", ":")).encode("utf-8"), endpoint)
            sent += 1
            if sleep_ms > 0:
                time.sleep(sleep_ms / 1000.0)
    finally:
        sock.close()
    return sent


def stop(proc: subprocess.Popen[str]) -> None:
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
    ap = argparse.ArgumentParser(description="Run Step38 JSONL through receiver/outbox/worker/MySQL checker")
    ap.add_argument("--url", required=True)
    ap.add_argument("--client-jsonl", required=True, type=Path)
    ap.add_argument("--session-key", required=True)
    ap.add_argument("--db-session-key", default="")
    ap.add_argument("--account-name", default="local-import")
    ap.add_argument("--character-key", default="PC_HERO")
    ap.add_argument("--receiver-bind", type=parse_endpoint, default=parse_endpoint("127.0.0.1:29777"))
    ap.add_argument("--server-jsonl", type=Path, default=Path("runtime/mmo_server_actions_step38_trade_combat_e2e.jsonl"))
    ap.add_argument("--reject-jsonl", type=Path, default=Path("runtime/mmo_server_rejects_step38_trade_combat_e2e.jsonl"))
    ap.add_argument("--rewritten-client-jsonl", type=Path, default=Path("runtime/mmo_client_actions_step38_trade_combat_e2e.jsonl"))
    ap.add_argument("--output", type=Path, default=Path("runtime/mmo_step38_trade_combat_e2e.json"))
    ap.add_argument("--checker-output", type=Path, default=Path("runtime/mmo_step38_trade_combat_mysql_e2e.json"))
    ap.add_argument("--tools-dir", type=Path, default=Path("tools"))
    ap.add_argument("--worker-id", default="dev-step38-trade-combat-e2e-worker")
    ap.add_argument("--worker-max-actions", type=int, default=0)
    ap.add_argument("--require-kind", action="append", default=[])
    ap.add_argument("--reset-matching-failed", action="store_true")
    ap.add_argument("--no-rewrite-session-key", action="store_true")
    ap.add_argument("--send-sleep-ms", type=int, default=5)
    ap.add_argument("--receiver-startup-delay-ms", type=int, default=750)
    ap.add_argument("--continue-on-error", action="store_true")
    ap.add_argument("--prepare-dev-fixture", action="store_true", help="after receiver enqueue, align missing Step38 NPC/ammo rows in MySQL for local dev E2E only")
    ap.add_argument("--fixture-output", type=Path, default=Path("runtime/mmo_step38_dev_fixture_e2e.json"))
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args(argv)

    require = set(args.require_kind)
    rows = load_rows(args.client_jsonl, args.session_key, rewrite=not args.no_rewrite_session_key, require_kind=require)
    validate_rows(rows, args.session_key)
    write_jsonl(args.rewritten_client_jsonl, rows)
    counts = Counter(str(r.get("action_kind") or "") for r in rows)

    result: dict[str, Any] = {
        "tool": "run_mmo_step38_trade_combat_e2e.py",
        "status": "planned" if args.dry_run else "running",
        "session_key": args.session_key,
        "rows": len(rows),
        "kind_counts": dict(sorted(counts.items())),
        "client_jsonl": str(args.client_jsonl),
        "rewritten_client_jsonl": str(args.rewritten_client_jsonl),
        "server_jsonl": str(args.server_jsonl),
        "reject_jsonl": str(args.reject_jsonl),
        "checker_output": str(args.checker_output),
        "commands": [],
    }
    if args.dry_run:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
        print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
        print(f"artifact={args.output}")
        return 0

    receiver_cmd = [
        sys.executable, str(args.tools_dir / "run_mmo_action_receiver.py"),
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
        "--max-packets", str(len(rows)),
        "--print-every", "1",
    ]
    proc: subprocess.Popen[str] | None = None
    try:
        proc = subprocess.Popen(receiver_cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        time.sleep(max(args.receiver_startup_delay_ms, 0) / 1000.0)
        if proc.poll() is not None:
            out, err = proc.communicate(timeout=2)
            result["commands"].append(asdict(CommandResult("receiver", receiver_cmd, proc.returncode or 1, tail(out), tail(err))))
            raise RuntimeError("receiver exited before replay")
        result["udp_sent"] = send_udp(rows, args.receiver_bind, args.send_sleep_ms)
        try:
            out, err = proc.communicate(timeout=30)
        except subprocess.TimeoutExpired:
            stop(proc)
            out, err = proc.communicate(timeout=5)
        result["commands"].append(asdict(CommandResult("receiver", receiver_cmd, proc.returncode or 0, tail(out), tail(err))))
        if proc.returncode != 0:
            raise RuntimeError("receiver failed")
    except Exception as exc:
        if proc is not None:
            stop(proc)
        result["status"] = "failed"
        result["error"] = str(exc)

    if args.prepare_dev_fixture and result.get("status") != "failed":
        fixture_cmd = [
            sys.executable, str(args.tools_dir / "prepare_mmo_step38_dev_fixture.py"),
            "--url", args.url,
            "--session-key", args.session_key,
            "--client-jsonl", str(args.rewritten_client_jsonl),
            "--output", str(args.fixture_output),
            "--apply",
        ]
        fr = run_command("prepare-dev-fixture", fixture_cmd, timeout=90)
        result["commands"].append(asdict(fr))
        result["fixture_output"] = str(args.fixture_output)
        if fr.returncode != 0:
            result["status"] = "failed"

    worker_cmd = [
        sys.executable, str(args.tools_dir / "run_mmo_resolved_action_worker.py"),
        "--url", args.url,
        "--worker-id", args.worker_id,
        "--session-key", args.session_key,
        "--max-actions", str(args.worker_max_actions or len(rows)),
    ]
    if args.reset_matching_failed:
        worker_cmd.append("--reset-matching-failed")
    if args.continue_on_error:
        worker_cmd.append("--continue-on-error")
    if result.get("status") != "failed":
        wr = run_command("worker", worker_cmd, timeout=120)
        result["commands"].append(asdict(wr))
        if wr.returncode != 0:
            result["status"] = "failed"

    checker_cmd = [
        sys.executable, str(args.tools_dir / "check_mmo_step38_trade_combat_mysql.py"),
        "--url", args.url,
        "--session-key", args.session_key,
        "--require-no-failed",
        "--output", str(args.checker_output),
    ]
    for kind in args.require_kind:
        checker_cmd.extend(["--require-kind", kind])
    if result.get("status") != "failed":
        cr = run_command("checker", checker_cmd, timeout=60)
        result["commands"].append(asdict(cr))
        result["status"] = "passed" if cr.returncode == 0 else "failed"

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
    for cmd in result["commands"]:
        if cmd.get("stdout_tail"):
            print(cmd["stdout_tail"], end="" if cmd["stdout_tail"].endswith("\n") else "\n")
        if cmd.get("stderr_tail"):
            print(cmd["stderr_tail"], file=sys.stderr, end="" if cmd["stderr_tail"].endswith("\n") else "\n")
    print(f"status={result['status']}")
    print(f"artifact={args.output}")
    print(f"checker_artifact={args.checker_output}")
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))


