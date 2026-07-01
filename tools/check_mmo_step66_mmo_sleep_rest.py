#!/usr/bin/env python3
"""Check Step66 MMO sleep-rest guard and world-time no-op dispatch."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def require(condition: bool, failures: list[str], message: str) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".", help="project root")
    ap.add_argument("--output", default="", help="optional JSON report path")
    args = ap.parse_args()

    root = Path(args.root)
    world_cpp = root / "game/world/world.cpp"
    worker_py = root / "tools/run_mmo_resolved_action_worker.py"

    failures: list[str] = []
    world_text = world_cpp.read_text(encoding="utf-8")
    worker_text = worker_py.read_text(encoding="utf-8")

    require("applyMmoServerSleepRest" in world_text, failures, "missing C++ MMO sleep-rest helper")
    require("MmoSleepRestorePercentPerMinute = 10" in world_text, failures, "missing 10 percent per minute restore rate")
    require("mmoClientUsesServer() && applyMmoServerSleepRest" in world_text, failures, "sleep-rest is not guarded by MMO server mode")
    require("game.setTime(after);" in world_text, failures, "single-player setDayTime path is missing")
    require("Mmo::Hooks::onWorldTimeChanged" in world_text, failures, "single-player/world-time hook path is missing")

    require("world_time_changed_capture_only_v2" in worker_text, failures, "worker does not mark world_time_changed capture-only")
    require("world_time_changed_server_bound_no_mysql_mutation" in worker_text, failures, "worker no-op reason is missing")
    require("CALL mmo_record_world_time_changed" not in worker_text, failures, "worker still calls mmo_record_world_time_changed")

    result = {
        "status": "failed" if failures else "passed",
        "failures": failures,
        "checked": [
            str(world_cpp),
            str(worker_py),
        ],
    }

    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(f"status={result['status']}")
    if failures:
        for failure in failures:
            print(f"failure={failure}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
