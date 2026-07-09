# Step255 - AI Dialog Intent DB Ledger Plan Export

Adds read-only planned-DB manifest export:

- `tools/export_llm_db_changes_plan.py`
- `tools/validation/export_llm_db_changes_plan.py`

The exporter converts `docs/llm/llm_db_changes/` into JSON with
`planned_only=true` while keeping `applied_to_db=false`, `db_mutated=false`,
`sql_generated=false`, `server_sql_touched=false` and
`mysql_connection_used=false`.

DB status: no SQL, no migration file, no schema change, no MySQL connection.
