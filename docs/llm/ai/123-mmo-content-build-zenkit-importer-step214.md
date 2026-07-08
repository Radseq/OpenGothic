# Step214 - C++ ZenKit content-build importer

Ten krok dodaje pierwszy realny C++ reader contentu po stronie serwera, ale
celowo nie podpina go do runtime UDP servera.

## Decyzja architektoniczna

Tak, teraz jest dobry moment, żeby zacząć czytać serwerowy content pack. Nie
powinno się jednak robić tego jako bezpośredniego zapisu do runtime DB ani przez
pliki z klienta.

Poprawny tor po Step211-213:

1. serwer albo operator wskazuje serwerową kopię ZEN/DAT/OU;
2. izolowany content-build importer czyta pliki przez ZenKit;
3. importer generuje `parser_snapshot.json`;
4. istniejące `tools/import_content_build_snapshot_database.py` zapisuje snapshot
   do `mmo_content_build`;
5. dopiero zatwierdzony build będzie później publikowany do runtime read modelu.

## Nowe pliki

| Plik | Cel |
|---|---|
| `server/cpp/mmo_content_build_loader.h` | publiczny kontrakt snapshot loadera |
| `server/cpp/mmo_content_build_loader.cpp` | loadery ZEN/DAT/OU i writer JSON |
| `server/cpp/mmo_content_build_importer.cpp` | CLI generujące parser snapshot |
| `server/cpp/CMakeLists.txt` | targety `mmo_content_build_loader` i `mmo_content_build_importer`, link do lokalnego `lib/ZenKit` |

## Zakres obecnego importera

Importer czyta:

- ZEN przez `zenkit::World`;
- waypointy/freepointy i krawędzie waynetu;
- root/child VOB-y z klasyfikacją `vob`, `trigger`, `mover`, `spawn`, `sound`,
  `light`;
- DAT przez `zenkit::DaedalusScript` jako indeks symboli;
- OU przez `zenkit::CutsceneLibrary` jako dialog outputy.

Importer nie wykonuje jeszcze Daedalusa. To jest celowe. Pełne `C_NPC`
templates, rutyny i perception bindings wymagają osobnego kroku z VM albo
kontrolowanymi no-op externalami, bo inicjalizacja instancji NPC może wykonywać
skrypt.

Uwaga po Step217: importer nadal nie wykonuje Daedalusa, ale emituje już
kandydatów read modelu z symbol table DAT dla `npc_templates`,
`item_templates`, `dialog_infos`, `routines` i `perception_bindings`.

## Przykładowy flow

```bash
cmake -S server/cpp -B build/mmo_cpp_server -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/mmo_cpp_server --target mmo_content_build_importer -j

./build/mmo_cpp_server/mmo_content_build_importer \
  --content-revision-key gothic2-notr-server-content-dev \
  --game-code gothic2-notr \
  --source-root-label local-server-content \
  --world-name newworld \
  --world-zen /srv/gothic-mmo/content/_work/Data/Worlds/NEWWORLD.ZEN \
  --world-zen-logical-path worlds/newworld/newworld.zen \
  --scripts-dat /srv/gothic-mmo/content/_work/Data/Scripts/_compiled/Gothic.dat \
  --scripts-dat-logical-path scripts/_compiled/gothic.dat \
  --dialog-ou /srv/gothic-mmo/content/_work/Data/Scripts/_compiled/OU.BIN \
  --dialog-ou-logical-path scripts/_compiled/ou.bin \
  --output runtime/content_build/parser_snapshot.json

tools/import_content_build_snapshot_database.py \
  --snapshot runtime/content_build/parser_snapshot.json \
  --output runtime/content_build/content_build_database_report.json \
  --sql-output runtime/content_build/import_content_build_database.sql
```

## Weryfikacja

Wykonane:

- `cmake -S server/cpp -B build/mmo_cpp_server -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo`
- `cmake --build build/mmo_cpp_server --target mmo_content_build_importer -j`
- minimalny run importera z pustym snapshotem;
- `python3 -m json.tool` dla wygenerowanego snapshotu;
- `tools/import_content_build_snapshot_database.py --snapshot ...` jako sprawdzenie
  kompatybilności z istniejącym importerem DB.

Uwaga historyczna: po Step214 `mmo_udp_server` nie budował się przez brakujące
wartości `RecordCombatIntent`, `RecordNpcActionState` i
`RecordNpcDialogLine` w `Mmo::SemanticActionKind`. Step215 wyrównał ten
kontrakt protokołu i przywrócił build serwera bez dodawania przedwczesnej
persystencji DB dla tych obserwacji.




