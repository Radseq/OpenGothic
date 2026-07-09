# Step274 - Step273 Dialog Intent Persistence Preview Bridge

Scope: C++ only, no DB/tools mutation.

Added a disabled-by-default C++ Step273 persistence bridge that builds typed
SQL/procedure previews for:

- `dialog_intent_conversation_sessions` upsert;
- `mmo_ai_record_gameplay_delivery_sent`;
- `dialog_intent_conversation_observers` upsert;
- `mmo_ai_record_gameplay_delivery_receipt`;
- `mmo_ai_record_gameplay_delivery_dead_letter`.

Server flag:

```text
--ai-dialog-intent-step273-persistence-preview
```

The flag only logs preview evidence after the late-observer resume commit
preflight. It reports statement count and bytes, but keeps `execute_mysql=off`,
`mutated_db=0`, replay send off, runtime mutation off and `mark_applied=off`.

No SQL migration, executable tool or MySQL connection is changed in this step.
