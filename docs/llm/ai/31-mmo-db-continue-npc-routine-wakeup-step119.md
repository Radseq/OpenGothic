# MMO DB Continue NPC routine wakeup - Step119

Problem observed after Step118:

- Fresh server-bound `New Game` produced a correct local baseline.
- Xardas walked and used bookstands normally.
- After save, exit and DB-backed load/continue, Xardas could stand idle instead
  of resuming his TA routine.

Why this differs from native `.sav`:

- Native save stores `Npc::saveAiState`, `AiQueue`, `AiQueueOverlay`, the
  routine list, current waypoint/freepoint and `WayPath`.
- DB checkpoint restore intentionally avoids raw transient queues and rebuilds a
  ZEN/script baseline, then materializes typed server slices.
- `WorldObjects::resetPositionToTA()` places NPCs at their TA points but also
  clears local AI queues. If nothing resumes the current routine after the final
  DB startup reset, a routine NPC can remain standing.

Changes:

- `World::resumeNpcRoutinesAfterServerRestore()` resumes active routine NPCs
  through the existing `Npc::resumeAiRoutine()` path.
- DB snapshot restore runs a TA reset plus routine wakeup after DB world/lifecycle
  state is applied.
- DB Continue runs a second wakeup after `triggerOnStart(false)`, because that
  startup path performs another `resetPositionToTA()`.
- DB checkpoint bootstrap JSON is augmented with current NPC routine/AI/path/fight
  arrays from the same tables used by live bootstrap.
- `server/sql/step119_npc_authority_restore_bridge.sql` creates the current and
  history tables plus `mmo_record_npc_*` procedures used by the Step117 client
  observer.

Expected log after DB Continue:

```text
MMO DB continue startup NPC routines resumed: count=N
```

For Xardas, `N` should be greater than zero in a normal NewWorld load with
routine NPCs present.




