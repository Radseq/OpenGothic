# 23 DB Continue Without New Game Trigger - Step110

Observed after Step109:

- The server-bound menu correctly avoided local save-slot selection.
- The DB Continue path still constructed the baseline through the New Game
  constructor and then executed `wrld->triggerOnStart(true)`.
- That made DB Continue behave like New Game in visible ways: startup videos,
  start-position camera behavior and first-time world-start side effects.

Step110 rule:

- DB Continue may use the ZEN baseline as a physical world container, but it must
  not execute first-time New Game world-start triggers.
- Native single-player New Game/Load remains unchanged without
  `-mmo-client-server`.

Changed behavior:

- When a pre-world DB checkpoint snapshot is reused, `GameSession(std::string)`
  now schedules restore with reason `db_continue_baseline_loaded`.
- It skips `wrld->triggerOnStart(true)` for that DB Continue baseline.
- It still initializes scripts/routines from content and resets the camera after
  the DB snapshot is applied.

Remaining gap:

- Full native-save parity for NPC AI/routines still requires a DB/server domain
  for NPC routine/AI/path queues. Step110 only removes the accidental New Game
  trigger from DB Continue.
