# Step222 - VDF world ZEN probe/extract

Ten krok odblokowuje brakujący etap po imporcie DAT/OU: świat ZEN jest często
w archiwach `Data/*.vdf` albo `Data/*.mod`, a obecny
`mmo_content_build_importer` czyta jeszcze tylko luźne pliki.

## Co dodaje

| Plik | Rola |
|---|---|
| `server/cpp/mmo_vdf_world_zen_probe.cpp` | izolowany ZenKit VFS probe/extractor |
| `server/cpp/CMakeLists.txt` | target `mmo_vdf_world_zen_probe` |
| `tools/probe_gothic_world_zen_archives.py` | wrapper CLI |
| `tools/bootstrap/probe_gothic_world_zen_archives.py` | uruchamia binarkę i streszcza raport |
| `tools/bootstrap/discover_gothic_content_sources.py` | nowe `--extra-root` |

## Dlaczego tak

Pełne czytanie ZEN przez VFS w głównym importerze jest następnym krokiem, ale
najpierw trzeba potwierdzić realne nazwy ścieżek w archiwach Steam. Probe jest
izolowany i nie zapisuje do DB. Może tylko:

- zamontować archiwa VDF/MOD przez ZenKit;
- wypisać kandydatów `.ZEN`;
- opcjonalnie wyciągnąć wybrany świat jako luźny plik.

## Komendy

```bash
cmake -S server/cpp -B build/mmo_cpp_server -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/mmo_cpp_server --target mmo_vdf_world_zen_probe -j

tools/probe_gothic_world_zen_archives.py \
  --gothic-root "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  --world-name newworld.zen \
  --extract
```

Po ekstrakcji:

```bash
tools/discover_gothic_content_sources.py \
  --gothic-root "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  --extra-root runtime/content_build/vfs_extracted \
  --content-revision-key gothic2-notr-steam-local

tools/mmo_gothic_content_discovery_report.py \
  --discovery runtime/content_build/gothic_content_sources.json
```

Jeżeli discovery pokaże `ready_for_importer`, uruchom:

```bash
runtime/content_build/run_content_build_importer.sh
```

Domyślnie skrypt wygeneruje snapshot i SQL/report. Żeby zastosować import do
MySQL:

```bash
MMO_CONTENT_BUILD_DB_MODE=apply runtime/content_build/run_content_build_importer.sh
```

Potem checker powinien przejść także z `--expect-zen`:

```bash
tools/check_mmo_step211_content_build_database.py \
  --url "$MYSQL_URL" \
  --expect-zen \
  --expect-daedalus \
  --expect-dialog-outputs \
  --output runtime/step211_content_build_database/check_after_zen_import.json
```
