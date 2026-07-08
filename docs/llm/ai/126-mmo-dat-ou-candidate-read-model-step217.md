# Step217 - DAT/OU candidate read model

Ten krok kontynuuje Step216 praktycznie: serwerowy content build zaczyna
produkować bogatszy read model z DAT/OU, ale nadal nie wykonuje Daedalusa jako
runtime logiki świata.

## Decyzja

Nie odpalamy jeszcze pełnej VM Daedalusa dla NPC/script ticka. Step217 robi
bezpieczny etap pośredni:

- czyta symbol table DAT przez ZenKit;
- klasyfikuje `C_NPC`, `C_ITEM` i `C_INFO` jako kandydatów read modelu;
- wykrywa kandydatów rutyn po nazwach funkcji typu `RTN_*`;
- wykrywa kandydatów percepcji po nazwach typu `ASSESSPLAYER`;
- zostawia runtime execution na późniejszy server tick.

## Nowy snapshot

C++ `mmo_content_build_importer` emituje teraz sekcje:

- `npc_templates`;
- `item_templates`;
- `routines`;
- `perception_bindings`;
- `dialog_infos`;
- `dialog_outputs`.

Sekcje `npc_templates`, `item_templates`, `routines`,
`perception_bindings` i `dialog_infos` są na razie kandydatami z symbol table.
Nie zawierają jeszcze pełnych wartości pól z wykonanych inicjalizatorów
instancji.

## DB

`mmo_content_build` miało już tabele dla NPC/routines/perception/dialog infos.
Step217 dodaje brakującą tabelę i widok:

- `daedalus_item_templates`;
- `v_daedalus_item_templates`;
- `item_template_count` w `v_content_build_health`.

`tools/import_content_build_snapshot_database.py` zapisuje teraz
`item_templates` do `daedalus_item_templates`, a raport/check Step211 pokazuje
licznik item templates.

## Znaczenie dla MMO

To nadal nie jest etap, w którym serwer wykonuje skrypty dla wszystkich graczy.
To jest etap, który daje przyszłemu server tickowi listę rzeczy do
zweryfikowania i załadowania:

- jakie NPC template istnieją;
- jakie item template istnieją;
- jakie dialog info symbol są w DAT;
- jakie funkcje wyglądają jak rutyny;
- jakie funkcje wyglądają jak percepcja/assess.

## Weryfikacja

Wykonane:

```bash
cmake --build build/mmo_cpp_server --target mmo_content_build_importer -j
./build/mmo_cpp_server/mmo_content_build_importer --content-revision-key step217-empty --output /tmp/opengothic_step217_empty_snapshot.json
python3 -m py_compile tools/bootstrap/import_content_build_snapshot_database.py tools/mmo_content_build_database_report.py tools/validation/check_mmo_step211_content_build_database.py
tools/import_content_build_snapshot_database.py --snapshot /tmp/opengothic_step217_empty_snapshot.json --output /tmp/opengothic_step217_content_build_report.json --sql-output /tmp/opengothic_step217_import_content_build.sql
python3 -m json.tool /tmp/opengothic_step217_empty_snapshot.json
```

Wynik:

- importer C++ buduje się;
- pusty snapshot zawiera nowe sekcje;
- importer DB generuje raport i SQL;
- syntetyczny test funkcji SQL potwierdza obsługę `item_templates`.




