# 24 MMO DB Continue Startup Suppression - Step112

Observed after Step110:

- DB Continue still reused the `GameSession(std::string)` New Game constructor.
- The constructor executed New Game script startup before it knew whether this
  was a DB Continue baseline.
- Skipping `triggerOnStart(true)` removed one first-time trigger path, but it
  also skipped normal existing-world startup behavior that can wake routines.

Step112 rule:

- Server-bound DB Continue is an explicit startup mode, not a failed/missing
  native save that happens to call the New Game constructor.
- Native single-player New Game/Load remains unchanged.

Changed behavior:

- `MainWindow::loadGame` calls `GameSession(world, StartupMode::MmoDbContinue)`
  for the synthetic MMO DB Continue slot.
- DB Continue keeps script/world initialization needed to build the ZEN baseline,
  but temporarily suppresses startup `playvideo`/`playvideoex` calls.
- DB Continue restores from the server snapshot with reason
  `db_continue_baseline_loaded`.
- DB Continue runs `wrld->triggerOnStart(false)` instead of `true`, so
  existing-world startup can run without New Game first-time triggers.

Expected client evidence:

```text
MMO DB continue: native save is missing, bootstrapping baseline world ...
MMO DB continue: suppressed startup video ...
MMO server snapshot restore scheduled ... reason=db_continue_baseline_loaded ...
MMO DB continue baseline loaded: running existing-world startup trigger
```

Remaining gap:

- Full native-save parity for NPC AI/path queues still needs a DB/server routine
  domain. Step112 only fixes the client loading path so DB Continue no longer
  behaves like visible New Game startup.
