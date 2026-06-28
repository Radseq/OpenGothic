# 19 Step37 C++ Script Hooks

Purpose: make Step37 evidence real at the OpenGothic gameplay boundary. The previous Step37 tools could validate `set_script_int` and XP/progression actions, but the game still needed a producer for those actions.

Changed C++ surfaces:
- `game/game/gamescript.cpp`
- `game/game/mmosemantichooks.h`
- `game/game/mmosemantichooks.cpp`

Captured script boundaries:
- `GameScript::exec` for selected dialog execution.
- `GameScript::invokeItem` for usable item scripts.
- `GameScript::useInteractive` for mobsi/interactive scripts, including the expected bookstand/bookshelf/regal path.

Capture model:
1. Before script execution, if semantic capture is enabled and the actor is the player in a live world tick, snapshot:
   - mutable global Daedalus INT symbols;
   - player level, experience, experience_next and learning_points;
   - known dialog pairs;
   - quest status and entry count.
2. Execute the script normally.
3. After successful execution, diff the snapshot and submit semantic envelopes:
   - `set_script_int` for changed global INTs;
   - `adjust_progression` for XP/LP/level deltas;
   - `set_known_dialog` for newly known info pairs;
   - `update_quest` for quest status/entry count changes.

Safety/performance notes:
- Disabled mode is guarded before snapshot allocation.
- Non-player script activity and bootstrap tick 0 are ignored.
- Snapshot/diff exceptions are swallowed so evidence capture cannot break gameplay.
- This is intentionally not a hot path; it runs only around selected script execution surfaces when semantic capture is enabled.

Validation:
- Use `tools/check_mmo_step37_script_jsonl.py` for local C++ JSONL evidence.
- Then route through `run_mmo_action_receiver.py` and `run_mmo_resolved_action_worker.py`.
- Finally run `tools/check_mmo_step37_bookstand_script_xp.py` against MySQL.

This still does not mean restore parity is complete. It proves the first real producer for the bookstand/script-XP one-shot contract.
