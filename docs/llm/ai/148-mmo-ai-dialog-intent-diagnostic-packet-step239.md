# Step239 - AI Dialog Intent Diagnostic Packet

Purpose: turn a Step238 preview into a client-safe diagnostic packet contract
without sending it.

Changed:

- `server/cpp/mmo_npc_perception_dialog_intent_diagnostic_packet.*`
- `server/cpp/mmo_npc_perception_action_dispatcher_probe.cpp`

Contract:

- `--emit-preview-diagnostic-packet` requires `--preview-dialog-intent`.
- `--require-preview-diagnostic-packet` fails with exit code `7` if the packet
  contract cannot be built.
- Packet data includes contract version, packet kind, severity, action/reason,
  preview/effect/intent kinds and stable action/world/session/character/NPC
  identity.
- All live-dispatch, fan-out, UI, audio and mark-applied booleans remain false.

This is a typed packet contract only, not client fan-out.
