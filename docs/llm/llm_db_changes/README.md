# LLM DB Changes Ledger

Historical planned-only DB work from the period when migrations were paused.
Executable DB work resumes in `server/sql/step273...`; keep these entries as
non-executable notes for Step254/255 validation.

Rules:

- no executable SQL here;
- every entry stays `applied_to_db: no`;
- every entry stays `sql_created: no`;
- every entry stays `server_sql_touched: no`;
- future code must not assume these paused-note surfaces exist unless a later
  `server/sql/step*.sql` migration creates and validates them.

Validate/export:

```bash
tools/check_llm_db_changes_ledger.py --strict
tools/export_llm_db_changes_plan.py --strict \
  --output /tmp/opengothic_llm_db_changes_plan.json
```

Both tools are read-only and must not connect to MySQL.
