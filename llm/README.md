# LLM Context: Gothic MMO Save/Load Database

Ten katalog jest dla przyszlego LLM/Codexa pracujacego nad zadaniem:

> Zrobic z mechanizmu load/save OpenGothic pipeline do odczytu poczatkowego stanu gry i zbudowania na tej podstawie bazy danych dla produkcyjnego MMO, najpierw Gothic 2 NK, potem Gothic 1 i Gothic 2.

Najpierw czytaj te pliki w kolejnosci:

1. `task-brief.md` - cel, zalozenia i definicja sukcesu.
2. `code-map.md` - gdzie w kodzie jest new game, save/load, world snapshot, NPC, itemy.
3. `roadmap.md` - kolejne etapy od initial save do produkcyjnego MMO.
4. `implementation-playbook.md` - konkretne kroki implementacyjne dla eksportera.
5. `data-model.md` - szkic tabel/encji dla baseline, delta i runtime.
6. `production-db-target.md` - docelowy podzial bazy w stylu MMO: account/realm/content/characters/world/event journal.
7. `runtime-sqlite-roadmap.md` - aktualny plan runtime SQLite uzywanego podczas gry.

Najwazniejsza zasada: save game jest swietnym bootstrapem i narzedziem reverse-engineeringu, ale produkcyjne MMO powinno miec wlasny model danych. Docelowy flow:

```text
ZEN + scripts + New Game initial save
  -> deterministic exporter
  -> canonical DB baseline
  -> authoritative server runtime
  -> persistent DB delta
```

Nie zaczynaj od pelnego MMO. Najpierw zrob deterministyczny eksport stanu po `New Game` dla Gothic 2 NK.

Aktualny lokalny DB flow:

```text
world_events.jsonl
  -> world_staging.sqlite
  -> gothic_mmo.sqlite
  -> db/migrations/postgres/001_gothic_mmo_schema.sql
```

Do sanity check uzyj:

```text
tools/check_mmo_database.py
```
