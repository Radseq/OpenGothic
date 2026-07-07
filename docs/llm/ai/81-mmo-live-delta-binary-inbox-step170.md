# MMO live delta binary inbox - step 170

This step removes the JSON latest-cache live-delta path introduced by the previous live-delta experiments.

## What changed

- `ServerLiveDeltaPacket` now carries typed UDP fields for:
  - authoritative position/yaw,
  - complete player stats block,
  - server tick,
  - domain/kind flags,
  - `requires_snapshot_refresh` hints for domains not yet encoded as typed binary lists.
- The client UDP sink now enqueues decoded `ServerLiveDeltaPacket` values in memory.
- `GameSession` drains that inbox on the main thread and applies only typed position/stat data.
- `runtime/mmo_server_live_deltas.jsonl` remains a debug/audit log generated locally from typed packets.

## What was removed

- `runtime/mmo_server_live_delta_latest*.json` is no longer written or read.
- `runtime/mmo_server_live_delta_manifest.json` is no longer used.
- The server no longer embeds authoritative JSON slices in live-delta UDP packets.
- `GameSession` no longer parses live-delta JSON files as an apply channel.

## Current boundary

Inventory, equipment, story, world-item and interactive updates are no longer tunneled through JSON live-delta. They currently set a typed `requires_snapshot_refresh` hint. The next modular step should add dedicated binary list/change packets for those domains instead of reusing JSON snapshots as live traffic.
