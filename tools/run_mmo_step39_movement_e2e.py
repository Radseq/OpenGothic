#!/usr/bin/env python3
"""Replay Step39 character_checkpoint JSONL through receiver -> outbox -> worker -> MySQL checker."""
from __future__ import annotations

import argparse
import json
import math
import signal
import socket
import subprocess
import sys
import time
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

STEP39_KINDS = {"character_checkpoint"}

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


def load_rows(path: Path, session_key: str, rewrite: bool, max_rows: int) -> list[dict[str, Any]]:
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
            if str(obj.get("action_kind") or "") not in STEP39_KINDS:
                continue
            rows.append(rewrite_session(obj, session_key) if rewrite else obj)
            if max_rows > 0 and len(rows) >= max_rows:
                break
    if not rows:
        raise SystemExit("no Step39 character_checkpoint actions found in client JSONL")
    return rows


def validate_rows(rows: list[dict[str, Any]], session_key: str) -> None:
    idem = Counter(str(r.get("idempotency_key") or "") for r in rows)
    dupes = {k: v for k, v in idem.items() if k and v > 1}
    wrong = [k for k in idem if k and not k.startswith(session_key + ":")]
    if dupes:
        raise SystemExit("duplicate idempotency after rewrite: " + json.dumps(dupes, sort_keys=True))
    if wrong:
        raise SystemExit("wrong idempotency prefix: " + json.dumps(wrong[:5]))



def payload_position(row: dict[str, Any]) -> tuple[float, float, float] | None:
    payload = row.get("payload") if isinstance(row.get("payload"), dict) else {}
    try:
        return (float(payload.get("pos_x")), float(payload.get("pos_y")), float(payload.get("pos_z")))
    except (TypeError, ValueError):
        return None


def payload_tick(row: dict[str, Any]) -> int:
    payload = row.get("payload") if isinstance(row.get("payload"), dict) else {}
    try:
        return int(row.get("client_tick") or payload.get("client_tick") or 0)
    except (TypeError, ValueError):
        return 0


def dist(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
    return math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2)


def coalesce_rows(rows: list[dict[str, Any]], min_distance: float, force_tick_delta: int) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    if not rows or min_distance <= 0 and force_tick_delta <= 0:
        return rows, {"enabled": False, "input_rows": len(rows), "output_rows": len(rows), "dropped_rows": 0}
    out: list[dict[str, Any]] = []
    last_pos: tuple[float, float, float] | None = None
    last_tick = 0
    dropped = 0
    forced = 0
    distance_kept = 0
    invalid_kept = 0
    for row in rows:
        pos = payload_position(row)
        tick = payload_tick(row)
        keep = False
        if not out or pos is None or last_pos is None:
            keep = True
            if pos is None:
                invalid_kept += 1
        elif force_tick_delta > 0 and tick >= last_tick + force_tick_delta:
            keep = True
            forced += 1
        elif min_distance > 0 and dist(pos, last_pos) >= min_distance:
            keep = True
            distance_kept += 1
        if keep:
            out.append(row)
            if pos is not None:
                last_pos = pos
                last_tick = tick
        else:
            dropped += 1
    return out, {
        "enabled": True,
        "input_rows": len(rows),
        "output_rows": len(out),
        "dropped_rows": dropped,
        "min_distance": min_distance,
        "force_tick_delta": force_tick_delta,
        "forced_kept": forced,
        "distance_kept": distance_kept,
        "invalid_kept": invalid_kept,
    }

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
    ap = argparse.ArgumentParser(description="Run Step39 movement JSONL through receiver/outbox/worker/MySQL checker")
    ap.add_argument("--url", required=True)
    ap.add_argument("--client-jsonl", required=True, type=Path)
    ap.add_argument("--session-key", required=True)
    ap.add_argument("--db-session-key", default="")
    ap.add_argument("--account-name", default="local-import")
    ap.add_argument("--character-key", default="PC_HERO")
    ap.add_argument("--receiver-bind", type=parse_endpoint, default=parse_endpoint("127.0.0.1:29777"))
    ap.add_argument("--server-jsonl", type=Path, default=Path("runtime/mmo_server_actions_step39_movement_e2e.jsonl"))
    ap.add_argument("--reject-jsonl", type=Path, default=Path("runtime/mmo_server_rejects_step39_movement_e2e.jsonl"))
    ap.add_argument("--rewritten-client-jsonl", type=Path, default=Path("runtime/mmo_client_actions_step39_movement_e2e.jsonl"))
    ap.add_argument("--output", type=Path, default=Path("runtime/mmo_step39_movement_e2e.json"))
    ap.add_argument("--checker-output", type=Path, default=Path("runtime/mmo_step39_movement_mysql_e2e.json"))
    ap.add_argument("--tools-dir", type=Path, default=Path("tools"))
    ap.add_argument("--worker-id", default="dev-step39-movement-e2e-worker")
    ap.add_argument("--max-rows", type=int, default=20)
    ap.add_argument("--coalesce-min-distance", type=float, default=0.0, help="Replay-side safety valve: keep only checkpoints at least this far apart in world units.")
    ap.add_argument("--coalesce-force-tick-delta", type=int, default=0, help="Keep a checkpoint at least this many client ticks after the previous kept row even if stationary.")
    ap.add_argument("--require-position-change", action="store_true", help="Run the JSONL checker before replay and require movement evidence.")
    ap.add_argument("--reset-matching-failed", action="store_true")
    ap.add_argument("--no-rewrite-session-key", action="store_true")
    ap.add_argument("--send-sleep-ms", type=int, default=5)
    ap.add_argument("--receiver-startup-delay-ms", type=int, default=750)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args(argv)

    loaded_rows = load_rows(args.client_jsonl, args.session_key, rewrite=not args.no_rewrite_session_key, max_rows=args.max_rows)
    rows, coalesce_report = coalesce_rows(loaded_rows, args.coalesce_min_distance, args.coalesce_force_tick_delta)
    validate_rows(rows, args.session_key)
    write_jsonl(args.rewritten_client_jsonl, rows)
    counts = Counter(str(r.get("action_kind") or "") for r in rows)

    result: dict[str, Any] = {
        "tool": "run_mmo_step39_movement_e2e.py",
        "status": "planned" if args.dry_run else "running",
        "session_key": args.session_key,
        "rows": len(rows),
        "input_rows": len(loaded_rows),
        "coalesce": coalesce_report,
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

    jsonl_checker_cmd = [
        sys.executable, str(args.tools_dir / "check_mmo_step39_movement_jsonl.py"),
        "--jsonl", str(args.rewritten_client_jsonl),
        "--session-key", args.session_key,
        "--min-rows", "1",
        "--output", str(args.output.with_name(args.output.stem + "_jsonl_check.json")),
    ]
    if args.require_position_change:
        jsonl_checker_cmd.append("--require-position-change")
    jr = run_command("jsonl_checker", jsonl_checker_cmd, timeout=30)
    result["commands"].append(asdict(jr))
    if jr.returncode != 0:
        result["status"] = "failed"

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
    if result.get("status") == "failed":
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
        return 1
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

    worker_cmd = [
        sys.executable, str(args.tools_dir / "run_mmo_resolved_action_worker.py"),
        "--url", args.url,
        "--worker-id", args.worker_id,
        "--session-key", args.session_key,
        "--max-actions", str(len(rows)),
    ]
    if args.reset_matching_failed:
        worker_cmd.append("--reset-matching-failed")
    if result.get("status") != "failed":
        wr = run_command("worker", worker_cmd, timeout=120)
        result["commands"].append(asdict(wr))
        if wr.returncode != 0:
            result["status"] = "failed"

    checker_cmd = [
        sys.executable, str(args.tools_dir / "check_mmo_step39_movement_mysql.py"),
        "--url", args.url,
        "--session-key", args.session_key,
        "--require-no-failed",
        "--min-applied", str(len(rows)),
        "--output", str(args.checker_output),
    ]
    if args.require_position_change:
        checker_cmd.append("--require-position-change")
        checker_cmd.extend(["--min-distinct-positions", "2"])
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
