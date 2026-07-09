# Step250 - AI Dialog Intent Proof Chain Guard

Adds `mmo_npc_perception_dialog_intent_chain_guard.*`.

The guard verifies that the Step236-Step249 proof chain is complete: typed
effect, preview, diagnostic packet, encoder, evidence, fanout plan, send
boundary, sender adapter, endpoint proof, ACK/NACK contract, receipt preview,
receive-loop proof and terminal plan.

Mode must remain `guard_only_no_dispatch_no_db`. It also records that future DB
storage prerequisites are still missing.

DB status: no SQL, no schema change. Ledger:
`docs/llm/llm_db_changes/008-step250-chain-guard-no-db.md`.
