# Step252 - AI Dialog Intent Proof Chain Gate

Adds `mmo_npc_perception_dialog_intent_chain_gate.*`.

The gate consumes the Step251 report and emits a CI/human verdict proving that
the proof chain is complete while DB work and live dispatch remain paused.

Mode must remain `ci_readiness_gate_no_dispatch_no_db`: no SQL generation,
`server/sql/*` edit, DB write, socket receive, send/fanout/UI/audio or
mark-applied execution.

DB status: no SQL, no schema change. Ledger:
`docs/llm/llm_db_changes/010-step252-chain-gate-no-db.md`.
