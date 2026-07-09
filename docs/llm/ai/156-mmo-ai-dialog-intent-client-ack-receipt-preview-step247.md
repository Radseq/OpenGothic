# Step247 - AI Dialog Intent Client ACK/NACK Receipt Preview

Adds `mmo_npc_perception_dialog_intent_client_ack_receipt_preview.*`.

The preview builds synthetic ACK, NACK and malformed receipt examples from the
Step246 contract. ACK/NACK examples must validate; malformed input must reject.

Mode must remain `preview_only_no_socket_receive`: no live socket receive, live
packet decode, client observation, send/fanout/UI/audio/DB/apply path is enabled.

DB status: no SQL, no schema change. Ledger:
`docs/llm/llm_db_changes/005-step247-client-ack-receipt-preview-no-db.md`.
