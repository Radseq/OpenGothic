# Step220 - content-build run script guard

Ten krok poprawia zachowanie po wykryciu `needs_extract_or_vfs_mount`.

## Problem

Discovery poprawnie wykryło brak prawdziwego world ZEN, ale wygenerowany
`runtime/content_build/run_content_build_importer.sh` nadal pozwalał uruchomić
częściowy DAT/OU import bez jawnej decyzji operatora. Potem skrypt od razu
zaczął aplikować duży SQL do MySQL, co wyglądało jak zawieszenie.

## Zmiana

Wygenerowany run script:

- przerywa przy `needs_extract_or_vfs_mount`;
- pozwala na częściowy DAT/OU snapshot tylko po ustawieniu
  `MMO_ALLOW_PARTIAL_CONTENT_BUILD=1`;
- domyślnie generuje `parser_snapshot.json`, raport i SQL, ale nie aplikuje SQL
  do MySQL;
- aplikuje SQL dopiero przy `MMO_CONTENT_BUILD_DB_MODE=apply`.

## Nowy flow

Dla normalnego pełnego importu:

```bash
runtime/content_build/run_content_build_importer.sh
```

Dla świadomego częściowego snapshotu DAT/OU bez world ZEN:

```bash
MMO_ALLOW_PARTIAL_CONTENT_BUILD=1 \
runtime/content_build/run_content_build_importer.sh
```

Dla aplikacji wygenerowanego SQL do MySQL:

```bash
MMO_CONTENT_BUILD_DB_MODE=apply \
runtime/content_build/run_content_build_importer.sh
```

Dla częściowego DAT/OU snapshotu plus aplikacja DB:

```bash
MMO_ALLOW_PARTIAL_CONTENT_BUILD=1 \
MMO_CONTENT_BUILD_DB_MODE=apply \
runtime/content_build/run_content_build_importer.sh
```

Przy dużym DAT/OU imporcie lepiej najpierw użyć domyślnego trybu `generate`,
sprawdzić rozmiar `runtime/content_build/import_content_build_database.sql`, a
dopiero potem jawnie odpalić `apply`.

