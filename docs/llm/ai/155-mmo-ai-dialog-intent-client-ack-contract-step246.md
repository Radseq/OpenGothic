# Step246 - AI Dialog Intent Client ACK/NACK Contract Preview

Adds `mmo_npc_perception_dialog_intent_client_ack_contract.*`.

The preview derives the ACK correlation key, expected ACK/NACK packet kinds and
timeout contract from the no-send chain. It proves that `mark_applied` must only
happen after a future validated client ACK.

Mode must remain `preview_only_no_client_ack`: no endpoint lookup, send, socket
receive, timer, DB terminal write, UI/audio or mark-applied execution.

DB status: no SQL, no schema change. Ledger:
`docs/llm/llm_db_changes/004-step246-client-ack-contract-no-db.md`.
