# Step244 - AI Dialog Intent Sender Adapter Proof

Adds `mmo_npc_perception_dialog_intent_sender_adapter.*`.

The proof consumes the Step243 send boundary and validates the transport-facing
input shape: target identity, diagnostic bytes, sequence consistency and
single-datagram readiness.

Mode must remain `proof_only_no_send`: route lookup and endpoint resolution are
not executed, and no send/fanout/UI/audio/DB/apply side effect is enabled.

DB status: no SQL, no schema change. Ledger:
`docs/llm/llm_db_changes/002-step244-sender-adapter-no-db.md`.
