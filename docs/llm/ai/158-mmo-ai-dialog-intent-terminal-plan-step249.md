# Step249 - AI Dialog Intent Terminal Status Plan

Adds `mmo_npc_perception_dialog_intent_terminal_plan.*`.

The plan describes future terminal outcomes for ACK apply, NACK reject/retry and
timeout dead-letter handling after the receive-loop proof.

Mode must remain `plan_only_no_timer_no_db`: no timer, timeout observation,
retry/dead-letter write, socket receive, send/fanout/UI/audio, DB terminal write
or mark-applied execution.

DB status: no SQL, no schema change. Ledger:
`docs/llm/llm_db_changes/007-step249-terminal-plan-no-db.md`.
