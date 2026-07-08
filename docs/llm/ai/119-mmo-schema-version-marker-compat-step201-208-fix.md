# Step201/203/206/208 - mmo_schema_versions compatibility fix

Problem:

```text
SQL Error [1054] [42S22]: Unknown column 'checksum' in 'field list'
```

Dotyczyło SQL-i:

- `step201_server_content_pack_inventory.sql`;
- `step203_server_content_archive_mounts.sql`;
- `step206_server_content_import_jobs.sql`;
- `step208_server_content_build_indexes.sql`.

Przyczyna:

Te kroki wpisywały marker migracji jako:

```sql
INSERT INTO mmo_schema_versions(migration_key, checksum, description)
```

Aktualna runtime DB ma jednak tabelę:

```sql
mmo_schema_versions(
  migration_key,
  applied_at,
  schema_contract,
  notes
)
```

Step211 działał poprawnie, bo używa osobnej tabeli
`mmo_content_build.content_build_schema_versions`, gdzie `checksum` i
`description` rzeczywiście istnieją.

Poprawka:

Runtime kroki 201/203/206/208 używają teraz:

```sql
INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
```

oraz:

```sql
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);
```

Zakres:

- bez zmiany tabel contentu;
- bez zmiany procedur;
- bez zmiany Step211;
- tylko kompatybilność markerów migracji z realnym schematem runtime DB.




