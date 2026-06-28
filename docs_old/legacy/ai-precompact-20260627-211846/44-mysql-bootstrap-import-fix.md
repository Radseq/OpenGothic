# MySQL Bootstrap Import Fix

Fixes the MySQL importer failure:

```text
ERROR 3140 (22032): Invalid JSON text in character_quests.text_entries
```

Runtime SQLite stores quest log entries as plain text in `mmo_character_quests_current.entries_text`.
The OpenGothic restore path splits multiple entries with the separator `\n---\n` and treats `(no entries)` as empty.
MySQL production schema stores `character_quests.text_entries` as `JSON`, therefore the importer must convert the source text into a valid JSON array before insert.

Implemented behavior in `tools/import_runtime_sqlite_to_mysql.py`:

- `NULL`, empty string, and `(no entries)` become `[]`.
- Plain Gothic text split by `\n---\n` becomes a JSON array of strings.
- Already-valid JSON arrays are preserved.
- Already-valid JSON objects with an `entries` array are normalized to that array.
- Other valid JSON scalar/object values are wrapped in an array.

After replacing the importer, rerun the same import command. The generated SQL uses one transaction, so the failed previous import should not have committed partial DML.
