# Step211 fix - MySQL key length in content-build database

Problem:

```text
ERROR 1071 (42000): Specified key was too long; max key length is 3072 bytes
```

Przyczyna:

`mmo_content_build` używa `utf8mb4`, więc indeksowane `VARCHAR` liczą się jako
do 4 bajtów na znak. Unikalny indeks:

```sql
UNIQUE KEY content_build_import_uk
  (content_revision_key, importer_key, source_logical_path, source_sha256)
```

zawierał `source_logical_path VARCHAR(512)` i przekraczał limit klucza.

Poprawka:

Pełne pola tekstowe zostają w tabeli, ale unikalność dla długich identyfikatorów
opiera się o generowane hashe:

- `source_logical_path_hash` dla `content_build_imports`;
- `edge_identity_hash` dla `world_waypoint_edges`;
- `routine_identity_hash` dla `daedalus_routines`.

Dzięki temu DB nadal przechowuje pełne ścieżki/symbole, ale indeksy są krótkie i
stabilne na MySQL 8 z `utf8mb4`.

Zakres:

- zmieniony tylko `server/sql/step211_content_build_database.sql`;
- importer snapshotu nie wymaga zmian, bo kolumny hash są generowane przez DB;
- można ponowić `tools/apply_content_build_database.py` po częściowo nieudanej
  próbie Step211.




