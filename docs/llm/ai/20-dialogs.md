# Dialog Execution

Files: `game/game/gamesession.cpp`, `game/game/gamescript.cpp`.

- `GameScript::dialogChoices` filters infos by NPC, permanence, known state, importance, and Daedalus condition functions.
- `GameSession::updateDialog` calls `GameScript::updateDialog`, then journals selection and visible subchoices.
- `GameSession::dialogExec` journals the selection before `GameScript::exec` invokes the Daedalus information function.
- `GameScript::exec` marks top-level info known or removes a selected subchoice, then calls the script function.

Do not emulate dialog visibility on a server by blindly calling condition functions: they may have side effects. Capture the client-visible result first; later add a side-effect-safe evaluator.

