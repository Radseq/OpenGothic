#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import socket
import subprocess
import select
import sys
import time
from pathlib import Path
from typing import Any

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def free_udp_port() -> int:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])
    finally:
        sock.close()


def envelope(session_key: str, seq: int, kind: str, payload: dict[str, Any], target_key: str = "character:PC_HERO:movement") -> dict[str, Any]:
    event_map = {
        "movement_proposal": ("movement_proposal_submitted", "movement", "server_validate_movement_proposal"),
        "character_checkpoint": ("character_position_checkpoint", "character", "mmo_checkpoint_character_state"),
    }
    event_type, event_class, procedure = event_map[kind]
    tick = int(payload.get("to_tick") or payload.get("client_tick") or payload.get("server_tick") or seq)
    return {
        "version": 1,
        "action_kind": kind,
        "event_type": event_type,
        "event_class": event_class,
        "procedure": procedure,
        "local_sequence": seq,
        "client_tick": tick,
        "target_key": target_key,
        "idempotency_key": f"{session_key}:{kind}:PC_HERO:{seq}",
        "payload": payload,
    }


def send_packet(port: int, obj: dict[str, Any]) -> None:
    data = json.dumps(obj, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.sendto(data, ("127.0.0.1", port))
    finally:
        sock.close()


def main() -> int:
    ap = argparse.ArgumentParser(description="Run Step43 MMO server smoke test without MySQL")
    ap.add_argument("--output-dir", type=Path, default=Path("runtime/step43_server_smoke"))
    ap.add_argument("--session-key", default="local-dev-PC_HERO_STEP43_SERVER_SMOKE")
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--server-script", type=Path, default=Path("tools/run_mmo_server.py"))
    ap.add_argument("--check-script", type=Path, default=Path("tools/check_mmo_step43_server_live.py"))
    ap.add_argument("--python", default=sys.executable)
    args = ap.parse_args()

    out = args.output_dir
    out.mkdir(parents=True, exist_ok=True)
    port = args.port or free_udp_port()
    accepted = out / "mmo_server_actions_step43_smoke.jsonl"
    rejected = out / "mmo_server_rejects_step43_smoke.jsonl"
    checkpoints = out / "mmo_server_checkpoints_step43_smoke.jsonl"
    summary = out / "mmo_server_step43_smoke_summary.json"
    check_report = out / "mmo_step43_server_smoke_check.json"
    combined_report = out / "mmo_step43_server_smoke_manifest.json"

    server_cmd = [
        args.python,
        str(args.server_script),
        "--bind", f"127.0.0.1:{port}",
        "--accepted-jsonl", str(accepted),
        "--rejected-jsonl", str(rejected),
        "--checkpoint-jsonl", str(checkpoints),
        "--summary-json", str(summary),
        "--require-session", args.session_key,
        "--max-packets", "4",
        "--truncate",
        "--print-every", "1",
        "--require-motion-state-for-large-fall",
        "--max-step-distance", "2500",
        "--max-horizontal-speed", "2500",
        "--max-vertical-speed", "2500",
        "--max-vertical-delta", "1600",
        "--max-fall-speed", "9000",
        "--max-fall-delta", "12000",
    ]
    proc = subprocess.Popen(server_cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    server_stdout_parts: list[str] = []
    deadline = time.time() + 10.0
    while True:
        if proc.poll() is not None:
            stdout_now, stderr_now = proc.communicate(timeout=1)
            raise SystemExit(f"ERROR: Step43 server exited before listening\n{''.join(server_stdout_parts)}{stdout_now}{stderr_now}")
        assert proc.stdout is not None
        ready, _, _ = select.select([proc.stdout], [], [], 0.1)
        if ready:
            line = proc.stdout.readline()
            server_stdout_parts.append(line)
            if "listening udp://" in line:
                break
        if time.time() > deadline:
            proc.kill()
            stdout_now, stderr_now = proc.communicate(timeout=3)
            raise SystemExit(f"ERROR: Step43 server did not start listening\n{''.join(server_stdout_parts)}{stdout_now}{stderr_now}")

    common = {
        "actor_key": "character:PC_HERO",
        "character_key": "PC_HERO",
        "world": "NEWWORLD/NEWWORLD.ZEN",
        "to_rotation_yaw": 0.0,
        "health_current": 100,
        "health_max": 100,
        "mana_current": 10,
        "mana_max": 10,
        "level": 1,
        "experience": 0,
        "experience_next": 500,
        "learning_points": 0,
        "strength": 10,
        "dexterity": 10,
    }
    valid_walk = envelope(args.session_key, 1, "movement_proposal", {
        **common,
        "reason": "smoke_walk",
        "from_tick": 100,
        "to_tick": 110,
        "delta_ms": 100,
        "from_pos_x": 0.0,
        "from_pos_y": 0.0,
        "from_pos_z": 0.0,
        "to_pos_x": 100.0,
        "to_pos_y": 0.0,
        "to_pos_z": 0.0,
        "from_health_current": 100,
        "vertical_axis": "y",
    })
    valid_fall = envelope(args.session_key, 2, "movement_proposal", {
        **common,
        "reason": "smoke_fall",
        "from_tick": 110,
        "to_tick": 120,
        "delta_ms": 200,
        "from_pos_x": 100.0,
        "from_pos_y": 0.0,
        "from_pos_z": 0.0,
        "to_pos_x": 120.0,
        "to_pos_y": -1000.0,
        "to_pos_z": 0.0,
        "from_is_in_air": True,
        "to_is_falling": True,
        "from_health_current": 100,
        "health_current": 90,
        "vertical_axis": "y",
    })
    invalid_teleport = envelope(args.session_key, 3, "movement_proposal", {
        **common,
        "reason": "smoke_teleport",
        "from_tick": 120,
        "to_tick": 130,
        "delta_ms": 100,
        "from_pos_x": 120.0,
        "from_pos_y": -1000.0,
        "from_pos_z": 0.0,
        "to_pos_x": 50000.0,
        "to_pos_y": -1000.0,
        "to_pos_z": 0.0,
        "from_is_in_air": False,
        "to_is_in_air": False,
        "from_health_current": 90,
        "health_current": 90,
        "vertical_axis": "y",
    })
    direct_checkpoint = envelope(args.session_key, 4, "character_checkpoint", {
        "actor_key": "character:PC_HERO",
        "character_key": "PC_HERO",
        "target_key": "character:PC_HERO:checkpoint",
        "pos_x": 120.0,
        "pos_y": -1000.0,
        "pos_z": 0.0,
        "rotation_yaw": 0.0,
        "server_tick": 140,
        "client_tick": 140,
        "health_current": 90,
        "health_max": 100,
        "mana_current": 10,
        "mana_max": 10,
        "reason": "smoke_direct_checkpoint_passthrough",
        "world": "NEWWORLD/NEWWORLD.ZEN",
    }, target_key="character:PC_HERO:checkpoint")

    for obj in (valid_walk, valid_fall, invalid_teleport, direct_checkpoint):
        send_packet(port, obj)
        time.sleep(0.05)

    try:
        stdout, stderr = proc.communicate(timeout=10)
        stdout = "".join(server_stdout_parts) + stdout
    except subprocess.TimeoutExpired:
        proc.kill()
        stdout, stderr = proc.communicate(timeout=3)
        stdout = "".join(server_stdout_parts) + stdout
        raise SystemExit("ERROR: Step43 server smoke timed out\n" + stdout + stderr)

    check_cmd = [
        args.python,
        str(args.check_script),
        "--summary", str(summary),
        "--accepted-jsonl", str(accepted),
        "--rejected-jsonl", str(rejected),
        "--checkpoint-jsonl", str(checkpoints),
        "--output", str(check_report),
        "--session-key", args.session_key,
        "--min-accepted", "3",
        "--min-accepted-movement-proposals", "2",
        "--min-checkpoints", "2",
        "--min-rejected", "1",
        "--require-reject-reason", "horizontal_speed_too_large",
        "--require-fall-segment",
    ]
    check_proc = subprocess.run(check_cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    manifest = {
        "tool": "run_mmo_step43_server_smoke.py",
        "status": "passed" if proc.returncode == 0 and check_proc.returncode == 0 else "failed",
        "server_returncode": proc.returncode,
        "check_returncode": check_proc.returncode,
        "port": port,
        "session_key": args.session_key,
        "artifacts": {
            "accepted_jsonl": str(accepted),
            "rejected_jsonl": str(rejected),
            "checkpoint_jsonl": str(checkpoints),
            "summary_json": str(summary),
            "check_report": str(check_report),
        },
        "server_stdout_tail": stdout[-4000:],
        "server_stderr_tail": stderr[-4000:],
        "check_stdout": check_proc.stdout,
        "check_stderr": check_proc.stderr,
    }
    combined_report.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    if proc.returncode != 0:
        print(stdout, end="")
        print(stderr, end="", file=sys.stderr)
        print(f"ERROR: server returned {proc.returncode}")
    print(check_proc.stdout, end="")
    if check_proc.stderr:
        print(check_proc.stderr, end="", file=sys.stderr)
    print(f"artifact={combined_report}")
    print(f"status={manifest['status']}")
    return 0 if manifest["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())


