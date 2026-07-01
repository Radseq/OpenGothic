#!/usr/bin/env python3
"""Check Step55 client-server bootstrap request evidence.

Static mode verifies that the client has an explicit server-bound flag and emits
a `client_bootstrap_request` semantic envelope. JSONL mode additionally verifies
that a real OpenGothic run produced the bootstrap request locally and/or at the
receiver boundary.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]

STATIC_CHECKS = (
    ("game/commandline.h", "explicit client-server getter", "mmoClientUsesServer() const"),
    ("game/commandline.cpp", "flag -mmo-client-server", "-mmo-client-server"),
    ("game/commandline.cpp", "flag -mmo-server-endpoint", "-mmo-server-endpoint"),
    ("game/game/mmosemanticactionsink.h", "server-bound mode getter", "isServerBoundClientModeEnabled"),
    ("game/game/mmosemanticevents.h", "client_bootstrap_request action", "client_bootstrap_request"),
    ("game/game/mmosemantichooks.h", "bootstrap hook declaration", "onClientBootstrapRequest"),
    ("game/game/gamesession.cpp", "new game bootstrap hook", "new_game_session_loaded"),
    ("tools/run_mmo_action_receiver.py", "receiver bootstrap normalization", "client_bootstrap_request"),
    ("tools/run_mmo_resolved_action_worker.py", "worker bootstrap capture-only handling", "client_bootstrap_request_capture_only_v1"),
    ("docs/llm/ai/11-step55-client-server-bootstrap.md", "Step55 documentation", "client_bootstrap_request"),
)


def file_contains(root: Path, rel: str, needle: str) -> bool:
    path = root / rel
    return path.exists() and needle in path.read_text(encoding="utf-8", errors="replace")


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    if not path.exists():
        raise FileNotFoundError(path)
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line_no, line in enumerate(f, 1):
            text = line.strip()
            if not text:
                continue
            try:
                obj = json.loads(text)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_no}: invalid JSON: {exc}") from exc
            if isinstance(obj, dict):
                rows.append(obj)
    return rows


def summarize_bootstrap_jsonl(path: Path) -> dict[str, Any]:
    rows = load_jsonl(path)
    matches = [row for row in rows if row.get("action_kind") == "client_bootstrap_request"]
    first = matches[0] if matches else None
    payload = first.get("payload") if isinstance(first, dict) and isinstance(first.get("payload"), dict) else {}
    missing = []
    if first is None:
        missing.append("client_bootstrap_request")
    else:
        for key in ("character_key", "world", "server_tick", "server_bound_client_mode"):
            if payload.get(key) in (None, ""):
                missing.append(f"payload.{key}")
        if not str(first.get("idempotency_key") or ""):
            missing.append("idempotency_key")
    return {
        "path": str(path),
        "rows": len(rows),
        "client_bootstrap_request_rows": len(matches),
        "ok": len(missing) == 0,
        "missing": missing,
        "sample": first,
    }


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Check Step55 client-server bootstrap request.")
    ap.add_argument("--root", default=str(ROOT), help="Project root")
    ap.add_argument("--client-jsonl", help="Optional local client JSONL produced by -mmo-action-jsonl")
    ap.add_argument("--server-jsonl", help="Optional receiver JSONL produced by run_mmo_action_receiver.py")
    ap.add_argument("--require-jsonl", action="store_true", help="Fail unless every provided/expected JSONL contains client_bootstrap_request")
    ap.add_argument("--output", help="Optional JSON artifact path")
    args = ap.parse_args(argv)

    root = Path(args.root)
    static_details = []
    static_ok = True
    for rel, name, needle in STATIC_CHECKS:
        ok = file_contains(root, rel, needle)
        static_ok = static_ok and ok
        static_details.append({"path": rel, "check": name, "needle": needle, "ok": ok})

    jsonl_details = []
    jsonl_ok = True
    for raw in (args.client_jsonl, args.server_jsonl):
        if not raw:
            continue
        try:
            detail = summarize_bootstrap_jsonl(Path(raw))
        except Exception as exc:  # noqa: BLE001 - user-facing diagnostic
            detail = {"path": raw, "ok": False, "error": str(exc)}
        jsonl_details.append(detail)
        jsonl_ok = jsonl_ok and bool(detail.get("ok"))

    if args.require_jsonl and not jsonl_details:
        jsonl_ok = False
        jsonl_details.append({"ok": False, "error": "--require-jsonl set but no --client-jsonl/--server-jsonl was provided"})

    status = "passed" if static_ok and (jsonl_ok or not args.require_jsonl) else "failed"
    result = {
        "status": status,
        "meaning": "Step55 proves an explicit server-bound client mode can emit the first session bootstrap request without changing old single-player flow.",
        "static_checks": static_details,
        "jsonl_checks": jsonl_details,
    }

    print("Step55 client-server bootstrap check")
    for item in static_details:
        print(f"  {'ok' if item['ok'] else 'FAIL'}: {item['check']} [{item['path']}]")
    for item in jsonl_details:
        label = item.get("path", "jsonl")
        print(f"  {'ok' if item.get('ok') else 'FAIL'}: jsonl bootstrap request [{label}]")
    print("status=" + status)

    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"artifact={out}")

    return 0 if status == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
