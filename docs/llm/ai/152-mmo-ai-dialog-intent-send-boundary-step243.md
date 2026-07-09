# Step243 - AI Dialog Intent Send Boundary

Adds `mmo_npc_perception_dialog_intent_send_boundary.*`.

The dispatcher probe can build/require a send-boundary descriptor after the
Step242 fanout plan. It validates target session/character identity, encoded
diagnostic bytes and single-datagram readiness.

Mode must remain `prepare_only_no_send`: no UDP send, packet fanout, dialog
UI/audio, DB terminal write or mark-applied path is enabled.

DB status: no SQL, no schema change. Ledger:
`docs/llm/llm_db_changes/001-step243-send-boundary-no-db.md`.
