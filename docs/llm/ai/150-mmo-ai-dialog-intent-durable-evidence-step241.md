# Step241 - AI Dialog Intent Durable Evidence

Purpose: write an explicit JSONL evidence record for a claimed safe dialog
intent that passed Step236-Step240.

Changed:

- `server/cpp/mmo_npc_perception_dialog_intent_durable_evidence.*`
- `server/cpp/mmo_npc_perception_action_dispatcher_probe.cpp`

Contract:

- Evidence write is disabled by default.
- `--write-preview-evidence-jsonl PATH` requires a claimed row, Step240
  encoding and `--i-understand-this-writes-evidence`.
- `--require-preview-evidence` fails with exit code `9` when evidence cannot be
  built or written.
- JSONL records stable identity, Step236-Step240 statuses and explicit
  no-side-effect booleans.

Evidence writing does not mutate DB and does not send packets, open UI/audio or
mark the action applied. Optional claim/skip cleanup remains explicit DB
mutation.
