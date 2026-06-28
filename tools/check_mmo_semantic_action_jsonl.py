#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

REQUIRED_TOP = {
    "version",
    "action_kind",
    "event_type",
    "event_class",
    "procedure",
    "local_sequence",
    "client_tick",
    "target_key",
    "idempotency_key",
    "payload",
}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Validate OpenGothic MMO semantic action JSONL emitted by C++ hooks.")
    ap.add_argument("jsonl", type=Path)
    ap.add_argument("--require-kind", action="append", default=[])
    ap.add_argument("--max-errors", type=int, default=20)
    args = ap.parse_args(argv)

    errors: list[str] = []
    kinds: Counter[str] = Counter()
    ticks: Counter[str] = Counter()
    actors: Counter[str] = Counter()
    sources: Counter[str] = Counter()
    seen_idempotency: set[str] = set()
    duplicates: list[str] = []
    rows = 0

    with args.jsonl.open("r", encoding="utf-8") as f:
        for line_no, raw in enumerate(f, 1):
            raw = raw.strip()
            if not raw:
                continue
            rows += 1
            try:
                obj = json.loads(raw)
            except json.JSONDecodeError as exc:
                errors.append(f"line {line_no}: invalid json: {exc}")
                if len(errors) >= args.max_errors:
                    break
                continue
            missing = sorted(REQUIRED_TOP - set(obj))
            if missing:
                errors.append(f"line {line_no}: missing {missing}")
            payload = obj.get("payload")
            if not isinstance(payload, dict):
                errors.append(f"line {line_no}: payload is not object")
            idem = obj.get("idempotency_key")
            if not isinstance(idem, str) or not idem:
                errors.append(f"line {line_no}: empty idempotency_key")
            elif idem in seen_idempotency:
                duplicates.append(idem)
            else:
                seen_idempotency.add(idem)
            kind = obj.get("action_kind")
            if isinstance(kind, str):
                kinds[kind] += 1
            else:
                errors.append(f"line {line_no}: action_kind is not string")
            tick = obj.get("client_tick")
            if isinstance(tick, int):
                ticks["tick0" if tick == 0 else "tick_gt0"] += 1
            if isinstance(payload, dict):
                actor = payload.get("actor_key") or payload.get("source_actor_key")
                if isinstance(actor, str):
                    if actor.startswith("character:"):
                        actors["character"] += 1
                    elif actor.startswith("npc:"):
                        actors["npc"] += 1
                    else:
                        actors["other"] += 1
                else:
                    actors["none"] += 1
                source = payload.get("source")
                if isinstance(source, str):
                    sources[source] += 1
            if len(errors) >= args.max_errors:
                break

    print(f"rows={rows}")
    for kind, count in sorted(kinds.items()):
        print(f"kind.{kind}={count}")
    for bucket, count in sorted(ticks.items()):
        print(f"{bucket}={count}")
    for bucket, count in sorted(actors.items()):
        print(f"actor.{bucket}={count}")
    for source, count in sources.most_common(10):
        print(f"source.{source}={count}")
    if duplicates:
        print(f"duplicate_idempotency={len(duplicates)}")
        for idem in duplicates[: args.max_errors]:
            print(f"duplicate: {idem}")

    for required in args.require_kind:
        if kinds[required] <= 0:
            errors.append(f"required action kind not found: {required}")

    if errors:
        print("[FAIL]")
        for err in errors[: args.max_errors]:
            print(err)
        return 1
    print("[OK]")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
