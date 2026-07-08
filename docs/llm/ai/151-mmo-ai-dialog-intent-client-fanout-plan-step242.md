# Step242 - AI Dialog Intent Client Fan-Out Plan

Purpose: build a deterministic plan for future client fan-out after Step241
evidence, still without sending UDP or applying the AI action.

Changed:

- `server/cpp/mmo_npc_perception_dialog_intent_fanout_plan.*`
- `server/cpp/mmo_npc_perception_action_dispatcher_probe.cpp`

Contract:

- `--plan-preview-client-fanout` requires Step241 evidence.
- `--require-preview-client-fanout-plan` fails with exit code `10` when the plan
  cannot be built.
- Plan accepts only safe greeting/warning diagnostic encodings with written
  evidence and stable world/session/character/NPC/target identity.
- Synthetic rows without target session/character are rejected by design.
- Plan records recipient kind, packet kind, encoded size and datagram count.

No UDP send, packet fan-out, dialog UI/audio, DB apply or
`mmo_ai_mark_npc_perception_action_applied` is executed.
