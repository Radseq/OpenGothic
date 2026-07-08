# Step237 - AI Typed Effect Descriptor

Purpose: describe a safe, typed future effect for supported NPC perception
actions after Step236 validation.

Changed:

- `server/cpp/mmo_npc_perception_effect_descriptor.*`
- `server/cpp/mmo_npc_perception_action_dispatcher_probe.cpp`

Contract:

- Supports `npc_greet_player` and `npc_warn_player`.
- Produces a `dialog_intent` descriptor with stable action/world/session/
  character/NPC/target identity.
- Future requirements are explicit: dialog UI may be needed later, but live
  dispatch is not implemented or allowed.
- `--require-typed-effect` fails with exit code `5` for unsupported rows.

This is a descriptor only. It does not mark actions applied, send packets, open
dialog UI/audio, move NPCs or start combat.
