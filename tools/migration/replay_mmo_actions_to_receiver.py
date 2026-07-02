#!/usr/bin/env python3
"""Replay semantic action JSONL to the dev UDP receiver.

Useful for receiver/outbox tests without relaunching OpenGothic. Can optionally
rewrite the session-key prefix inside idempotency keys so old outbox rows do not
mask a new receiver/worker test.
"""
from __future__ import annotations

import argparse
import json
import socket
import sys
import time
from pathlib import Path
from typing import Any


def parse_endpoint(value: str) -> tuple[str, int]:
    if ":" not in value:
        raise argparse.ArgumentTypeError("expected host:port")
    host, port_text = value.rsplit(":", 1)
    try:
        port = int(port_text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("invalid port") from exc
    if port <= 0 or port > 65535:
        raise argparse.ArgumentTypeError("port out of range")
    return host, port


def load_actions(path: Path, require_kinds: set[str]) -> list[dict[str, Any]]:
    actions: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as f:
        for line_no, line in enumerate(f, 1):
            stripped = line.strip()
            if not stripped:
                continue
            obj = json.loads(stripped)
            if require_kinds and str(obj.get("action_kind")) not in require_kinds:
                continue
            actions.append(obj)
    return actions


def rewrite_session(obj: dict[str, Any], new_session: str) -> dict[str, Any]:
    if not new_session:
        return obj
    out = json.loads(json.dumps(obj, ensure_ascii=False))
    idem = str(out.get("idempotency_key") or "")
    parts = idem.split(":", 1)
    if len(parts) == 2:
        out["idempotency_key"] = new_session + ":" + parts[1]
    else:
        out["idempotency_key"] = new_session + ":" + idem
    return out


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Replay MMO semantic action JSONL to a UDP receiver")
    ap.add_argument("jsonl", type=Path)
    ap.add_argument("--to", type=parse_endpoint, default=parse_endpoint("127.0.0.1:29777"))
    ap.add_argument("--repeat", type=int, default=1)
    ap.add_argument("--sleep-ms", type=int, default=0)
    ap.add_argument("--require-kind", action="append", default=[])
    ap.add_argument("--rewrite-session-key", default="", help="replace the idempotency-key prefix before ':' with this value")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args(argv)

    actions = [rewrite_session(a, args.rewrite_session_key) for a in load_actions(args.jsonl, set(args.require_kind))]
    if not actions:
        print("no actions to replay")
        return 2

    host, port = args.to
    sent = 0
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        for i in range(max(args.repeat, 1)):
            for obj in actions:
                line = json.dumps(obj, ensure_ascii=False, separators=(",", ":"))
                if not args.dry_run:
                    sock.sendto(line.encode("utf-8"), (host, port))
                sent += 1
                print(f"sent={sent} repeat={i + 1} kind={obj.get('action_kind')} idem={obj.get('idempotency_key')}")
                if args.sleep_ms > 0:
                    time.sleep(args.sleep_ms / 1000.0)
    finally:
        sock.close()

    print(f"actions={len(actions)} repeat={max(args.repeat, 1)} sent={sent} target={host}:{port}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
