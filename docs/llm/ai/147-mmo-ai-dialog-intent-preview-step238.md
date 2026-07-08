# Step238 - AI Dialog Intent Preview

Purpose: build a deterministic preview/log boundary for Step237 dialog intents.

Changed:

- `server/cpp/mmo_npc_perception_dialog_intent_preview.*`
- `server/cpp/mmo_npc_perception_action_dispatcher_probe.cpp`

Contract:

- Supports greeting and warning dialog-intent descriptors.
- Preview is disabled by default and requires an explicitly claimed row.
- Probe flags include `--preview-dialog-intent` and `--require-preview`.
- Required preview failure returns exit code `6`.
- Preview output carries stable identity and explicit no-side-effect booleans.

No DB apply, packet fan-out, client dialog UI/audio, movement or combat is
executed.
