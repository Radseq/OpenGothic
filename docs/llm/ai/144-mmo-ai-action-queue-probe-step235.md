# Step235 - AI Action Queue Probe

Purpose: make `mmo_ai_runtime.npc_perception_action_queue` inspectable and
manually claimable before any dispatcher exists.

Changed:

- `server/cpp/mmo_ai_runtime_persistence.*`
- `server/cpp/mmo_npc_perception_action_queue_probe.cpp`

Contract:

- Default probe mode is read-only and reports queue counts plus bounded pending
  action samples.
- `--claim-one` requires `--i-understand-this-mutates-db`.
- `--skip-after-claim` may skip only the row claimed by this probe.
- Claim/skip uses the existing `mmo_ai_runtime` procedures.

The probe never applies gameplay, never sends packets and never dispatches
dialog, movement or combat. It is queue evidence only.
