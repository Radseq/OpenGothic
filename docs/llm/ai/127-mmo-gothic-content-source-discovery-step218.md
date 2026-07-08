# Step218 - Gothic content source discovery for ZenKit importer

Ten krok domyka praktyczny brak między katalogiem instalacji Gothic II a
`mmo_content_build_importer`.

## Decyzja

ZEN/DAT/OU nie są git submodule. To są pliki contentu gry/moda. Submodulem jest
czytnik, czyli `lib/ZenKit`, używany już przez klienta i teraz także przez
serwerowy importer.

Serwer nie ma czytać plików klienta gracza. Operator serwera wskazuje własną
kopię contentu, np. lokalną instalację albo pre-extracted content root. Wynik
trafia najpierw do `parser_snapshot.json`, potem do osobnej bazy
`mmo_content_build`.

## Nowe pliki

| Plik | Rola |
|---|---|
| `tools/discover_gothic_content_sources.py` | wrapper CLI |
| `tools/bootstrap/discover_gothic_content_sources.py` | scanner katalogu Gothic II/pre-extracted root |
| `tools/mmo_gothic_content_discovery_report.py` | krótki raport operatorski z wyniku discovery |
| `server/cpp/CMakeLists.txt` | samodzielny target CMake dla `mmo_content_build_importer` |

## Co robi scanner

Scanner:

- znajduje luźne pliki `.zen`, `.dat`, `OU.BIN`/`OU.CSL`;
- traktuje jako world ZEN tylko właściwe pliki świata z `Data/Worlds` /
  `_work/Data/Worlds`, a nie presety typu `Lensflare.zen`;
- znajduje archiwa `.vdf`/`.mod`;
- liczy SHA-256 wybranych plików;
- wybiera domyślne źródła dla importera;
- zapisuje raport JSON;
- generuje `runtime/content_build/run_content_build_importer.sh`.

Jeżeli instalacja ma tylko `.vdf/.mod`, raport dostaje status
`needs_extract_or_vfs_mount`. To jest poprawny wynik dla wielu instalacji Steam:
obecny C++ importer czyta luźne ścieżki ZEN/DAT/OU, a nie mountuje jeszcze VFS
z archiwów.

## Kubuntu flow dla lokalnej ścieżki

```bash
tools/discover_gothic_content_sources.py \
  --gothic-root "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  --content-revision-key gothic2-notr-steam-local \
  --output runtime/content_build/gothic_content_sources.json \
  --run-script runtime/content_build/run_content_build_importer.sh
```

Jeżeli raport ma `status=ready_for_importer`, budujesz importer i uruchamiasz
wygenerowany skrypt:

```bash
cmake -S server/cpp -B build/mmo_cpp_server -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/mmo_cpp_server --target mmo_content_build_importer -j

runtime/content_build/run_content_build_importer.sh
```

Krótki raport:

```bash
tools/mmo_gothic_content_discovery_report.py \
  --discovery runtime/content_build/gothic_content_sources.json
```

Jeżeli raport ma `status=needs_extract_or_vfs_mount`, następny krok to
wypakowanie/mount VDF/MOD albo rozszerzenie C++ importera o ZenKit VFS mount.
Dopiero po tym importer może dostać realne `--world-zen`, `--scripts-dat` i
`--dialog-ou`.

## Bazy

Ten krok nadal nie zapisuje nic do runtime DB.

Przepływ danych:

1. katalog Gothic II/pre-extracted root;
2. discovery report;
3. `mmo_content_build_importer`;
4. `parser_snapshot.json`;
5. `tools/import_content_build_snapshot_database.py`;
6. `mmo_content_build`.

`mmo_ai_runtime` pozostaje osobną bazą dla decyzji NPC, cooldownów i kolejki
akcji. Runtime MMO DB dostanie później tylko zatwierdzony read model/export z
`mmo_content_build`.

