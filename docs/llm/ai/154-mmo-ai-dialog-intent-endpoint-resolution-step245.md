# Step245 - AI Dialog Intent Endpoint Resolution Proof

Adds `mmo_npc_perception_dialog_intent_endpoint_resolution.*`.

The proof validates that a future sender can use the target `session_uuid` as
the active-session UDP endpoint lookup key. It still does not inspect socket
state, resolve endpoints or send packets.

Mode must remain `proof_only_no_endpoint_lookup`. All route lookup, endpoint,
send/fanout/UI/audio/DB/apply side effects remain disabled.

DB status: no SQL, no schema change. Ledger:
`docs/llm/llm_db_changes/003-step245-endpoint-resolution-no-db.md`.
