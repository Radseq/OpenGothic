# Step212 - separate AI runtime perception database

Ten krok dodaje osobną bazę `mmo_ai_runtime`. To jest odpowiedź na problem:
`mmo_content_build` nie powinno trzymać decyzji NPC, a runtime MMO DB nie
powinna puchnąć od szczegółów ticka AI.

## Podział odpowiedzialności

| Baza/obszar | Co trzyma |
|---|---|
| `mmo_content_build` | statyczny wynik parsowania ZEN/DAT/OU |
| runtime MMO DB | sesje, postacie, world instances, trwały stan gry |
| `mmo_ai_runtime` | decyzje percepcji NPC, cooldowny, kolejkę akcji AI |
| cache/read model | szybkie struktury w pamięci dla ticka serwera |

Serwerowy tick NPC będzie czytał zatwierdzony content revision z
`mmo_content_build`, aktualny stan graczy/NPC z runtime DB, a decyzje zapisze w
`mmo_ai_runtime`.

## Nowe pliki

| Plik | Cel |
|---|---|
| `server/sql/step212_ai_runtime_perception_database.sql` | tworzy bazę `mmo_ai_runtime` |
| `tools/apply_ai_runtime_database.py` | wrapper aplikatora SQL |
| `tools/bootstrap/apply_ai_runtime_database.py` | właściwy aplikator bazy AI |
| `tools/check_mmo_step212_ai_runtime_perception_database.py` | wrapper walidatora |
| `tools/validation/check_mmo_step212_ai_runtime_perception_database.py` | walidator DB |
| `tools/mmo_ai_runtime_report.py` | raport health/recent decisions/pending actions |

## Tabele

| Tabela | Co przechowuje |
|---|---|
| `ai_runtime_schema_versions` | marker migracji bazy AI |
| `npc_perception_rule_catalog` | przyszłe reguły: percepcja, akcja, dystans, cooldown, LOS |
| `npc_perception_cooldowns` | cooldown per world instance, NPC, target, perception |
| `npc_perception_decisions` | dziennik decyzji NPC dla wielu graczy |
| `npc_perception_action_queue` | akcje do wykonania/broadcastu: obrót, podejście, dialog, atak |

Nie ma cross-database foreign key do runtime DB ani `mmo_content_build`. Ta baza
ma naturalne klucze i UUID-y tekstowe, żeby późniejszy rewrite storage był
łatwiejszy.

## Procedura

`mmo_ai_record_npc_perception_decision(...)` robi pierwszy kontrakt dla
autorytatywnego serwera:

- wymaga `world_instance_uuid`, `npc_entity_key`, `target_key`, `perception_kind`
  i `idempotency_key`;
- sprawdza cooldown NPC/target/perception;
- zapisuje decyzję jako `accepted`, `queued`, `cooldown`, `blocked` albo `noop`;
- opcjonalnie tworzy wpis w `npc_perception_action_queue`;
- zapisuje/odświeża cooldown.

To jeszcze nie jest pełny tick AI. To jest miejsce, w które przyszły tick będzie
zapisywał wynik decyzji, zanim klient dostanie prezentacyjny delta event.

## Minimalny flow

0. Jeśli użytkownik MySQL nie ma prawa tworzenia baz, przygotuj bazę jako admin:

```sql
CREATE DATABASE IF NOT EXISTS mmo_ai_runtime
  DEFAULT CHARACTER SET utf8mb4
  DEFAULT COLLATE utf8mb4_0900_ai_ci;

GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, ALTER, INDEX, REFERENCES,
      EXECUTE, SHOW VIEW, CREATE VIEW, DROP
ON mmo_ai_runtime.* TO 'gothic'@'localhost';

GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, ALTER, INDEX, REFERENCES,
      EXECUTE, SHOW VIEW, CREATE VIEW, DROP
ON mmo_ai_runtime.* TO 'gothic'@'127.0.0.1';

GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, ALTER, INDEX, REFERENCES,
      EXECUTE, SHOW VIEW, CREATE VIEW, DROP
ON mmo_ai_runtime.* TO 'gothic'@'192.168.195.%';

FLUSH PRIVILEGES;
```

Jeśli aplikator ma sam wykonać `CREATE DATABASE`, użytkownik z `MYSQL_URL`
musi mieć też odpowiednie globalne prawo `CREATE`. Bezpieczniejszy wariant na
lokalnym devie to stworzyć bazę i granty powyższym blokiem, a aplikatorowi
zostawić tworzenie tabel, widoków i procedury.

1. Utwórz/zaaktualizuj obiekty w bazie:

```bash
tools/apply_ai_runtime_database.py \
  --url "$MYSQL_URL" \
  --output runtime/step212_ai_runtime_perception_database/apply.json
```

2. Sprawdź obiekty:

```bash
tools/check_mmo_step212_ai_runtime_perception_database.py \
  --url "$MYSQL_URL" \
  --output runtime/step212_ai_runtime_perception_database/check.json
```

3. Zrób raport:

```bash
tools/mmo_ai_runtime_report.py \
  --url "$MYSQL_URL" \
  --output runtime/step212_ai_runtime_perception_database/report.json
```

## Jak to łączy się z NPC zaczepiającym gracza

Przyszły tick serwera będzie działał tak:

1. Z runtime DB bierze aktywne NPC i graczy w world instance.
2. Z cache/read modelu robi szybki spatial query.
3. Z `mmo_content_build` zna template NPC, rutyny, perception bindings i dialogi.
4. Wylicza decyzję: `greet_player`, `warn_player`, `start_dialog`, `attack_player`,
   `ignore_player` itd.
5. Zapisuje decyzję/cooldown do `mmo_ai_runtime`.
6. Kolejka akcji trafia do warstwy broadcastu, która wyśle klientom tylko wynik:
   obrót NPC, podejście, UI dialogu, audio/napisy albo walkę.

Najważniejszy efekt architektoniczny: klient dalej nie jest źródłem prawdy.
Klient odtwarza skutki decyzji, a decyzja powstaje i zostaje zapisana po stronie
serwera.




