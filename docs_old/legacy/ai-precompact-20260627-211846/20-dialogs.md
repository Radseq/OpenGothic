# Dialog Execution

Files: `game/game/gamesession.cpp`, `game/game/gamescript.cpp`.

- `GameScript::dialogChoices` filters infos by NPC, permanence, known state, importance, and Daedalus condition functions.
- `GameSession::updateDialog` calls `GameScript::updateDialog`, then journals selection and visible subchoices.
- `GameSession::dialogExec` journals the selection before `GameScript::exec` invokes the Daedalus information function.
- `GameScript::exec` marks top-level info known or removes a selected subchoice, then calls the script function.
- Runtime event journal uses `dialog_choice_executed` for `dialogExec` and `dialog_choice_updated` for `updateDialog`; both can exist for the same visible choice but they are different phases.

Do not emulate dialog visibility on a server by blindly calling condition functions: they may have side effects. Capture the client-visible result first; later add a side-effect-safe evaluator.
