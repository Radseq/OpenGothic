# Step253 - AI Dialog Intent Proof Chain Package

Adds `mmo_npc_perception_dialog_intent_chain_package.*`.

The package consumes the Step252 gate and produces a read-only handoff shape:
gate verdict, safety flags, blocked transitions, future storage surfaces and a
pointer to `docs/llm/llm_db_changes/`.

Mode must remain `read_only_package_no_dispatch_no_db`: no file write, SQL
generation, DB write, socket receive, send/fanout/UI/audio or mark-applied path.

DB status: no SQL, no schema change. Ledger:
`docs/llm/llm_db_changes/011-step253-chain-package-no-db.md`.
