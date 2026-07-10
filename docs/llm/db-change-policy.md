# DB Change Policy

Use this when a task touches MySQL schemas, procedures, projections or DB tools.

## Default Rule

Do not modify or reset the database unless the user explicitly allowed it.

When the user says not to apply DB changes yet:

- write planned SQL changes into a separate file or note;
- explain why each change is needed;
- update tools only if they do not require the DB change to be already applied;
- keep checks aware that the DB has not changed yet.

When the user allows DB/tool updates:

- apply schema/procedure changes together with matching tools/checks;
- keep changes idempotent where practical;
- run the smallest validation that proves the contract;
- update `testing.md` if validation commands changed.

## Runtime vs Content DB

- `mmo_content_build` is build-time parsed content.
- Runtime gameplay state belongs in runtime schemas/projections/procedures.
- AI/perception runtime decisions belong in `mmo_ai_runtime`.

Do not mix static content import with live gameplay mutation.

## Views/Procedures

- Avoid adding views/procedures just to hide unclear server logic.
- Use procedures when the DB must atomically validate/mutate current state.
- Use views/read-models when they simplify stable read contracts.
- If a view can break schema dumps because of invalid definers/dependencies, update snapshot tooling to skip/report it safely.

## Destructive Operations

Destructive reset commands must require explicit guard flags. Example:

```bash
python3 tools/run_mmo_step55_clean_mysql_from_pre_xardas.py \
  --sqlite runtime/g2notr_ch1_pre_xardas.sqlite \
  --mysql-url "$MYSQL_URL" \
  --i-understand-this-drops-database
```
