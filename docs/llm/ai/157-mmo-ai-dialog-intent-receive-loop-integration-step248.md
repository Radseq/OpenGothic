# Step248 - AI Dialog Intent Receive-Loop Integration Proof

Adds `mmo_npc_perception_dialog_intent_receive_loop_integration.*`.

The proof consumes the Step247 receipt preview and shapes the future receive
route key from target session plus ACK correlation. It proves routing input only;
it does not enter the live UDP receive loop.

Mode must remain `proof_only_no_socket_receive`: no socket read, live decode,
client ACK/NACK observation, send/fanout/UI/audio/DB/apply side effect.

DB status: no SQL, no schema change. Ledger:
`docs/llm/llm_db_changes/006-step248-receive-loop-integration-no-db.md`.
