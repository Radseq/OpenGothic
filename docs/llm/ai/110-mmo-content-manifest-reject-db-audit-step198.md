# Step198 - DB audit for content manifest rejects

Ten krok przenosi content manifest reject audit z samego JSONL-a do bazy.
JSONL zostaje jako fallback developerski, ale serwer może teraz zapisać reject
bootstrapu przez procedurę DB.

## Nowe obiekty DB

SQL surface:

```text
server/sql/step198_content_manifest_reject_audit.sql
```

Dodaje:

| Obiekt | Rola |
| --- | --- |
| `mmo_content_manifest_reject_audit` | tabela audytu rejectów content manifest gate |
| `mmo_record_content_manifest_reject(...)` | procedura zapisu jednego rejectu |
| `v_mmo_content_manifest_reject_audit` | widok diagnostyczny z UUID i nazwami |
| `mmo_schema_versions` marker | marker nałożenia Step198 |

## Co zapisuje tabela

Tabela zachowuje:

- `session_id`, jeśli sesja istnieje;
- `realm_id`, `account_id`, `character_id`, `world_instance_id`, jeśli da się je
  wyciągnąć z sesji;
- `content_revision_id`, jeśli wynika z realmu albo `content_revision_key`;
- remote endpoint klienta;
- packet session key;
- target key;
- packet/local sequence;
- `phase`;
- `reason`;
- hash klienta;
- wymagany hash serwera;
- `content_revision_key`;
- message;
- pełny payload JSON diagnostyki.

Ważne: rekord może zostać zapisany nawet wtedy, gdy sesja jest pusta albo nie
da się jej znaleźć. To jest potrzebne dla przypadków:

- `session_uuid_missing`;
- `active_session_not_found`;
- błędny handshake przed pełnym loginem.

## C++ server bridge

Serwer nadal zapisuje:

```text
runtime/mmo_server_content_manifest_rejects.jsonl
```

Ale dodatkowo próbuje wykonać:

```cpp
Mmo::Server::recordContentManifestRejectAudit(...)
```

Jeśli procedura DB jeszcze nie istnieje albo zapis się nie uda, serwer loguje:

```text
[content_manifest_reject_db_audit_failed]
```

i kontynuuje flow. Bootstrap reject diagnostic nadal idzie do klienta.

## Narzędzia

Dodane:

```text
tools/check_mmo_step198_content_manifest_reject_audit.py
tools/validation/check_mmo_step198_content_manifest_reject_audit.py
```

Checker sprawdza:

- tabelę;
- procedurę;
- widok;
- marker w `mmo_schema_versions`;
- liczbę rekordów audytu.

Opcjonalnie:

```bash
tools/check_mmo_step198_content_manifest_reject_audit.py \
  --url mysql://user:pass@host:3306/db \
  --write-sample
```

może wstawić próbny rekord przez procedurę.

## Raport

`tools/mmo_content_manifest_reject_report.py` dostał opcjonalne `--url`.
Po jego podaniu raport łączy:

- klientowy `runtime/mmo_server_bootstrap_reject.json`;
- serwerowy `runtime/mmo_server_content_manifest_rejects.jsonl`;
- DB audit `mmo_content_manifest_reject_audit`.

## Dlaczego to ważne dla wielu graczy

Przy jednym lokalnym kliencie wystarczy plik logu. Przy wielu graczach trzeba
wiedzieć:

- ilu klientów odpada przez `content_hash_mismatch`;
- na jakiej rewizji contentu;
- z jakim wymaganym hashem;
- z jakiego realmu;
- czy problem dotyczy konkretnego deployu launchera;
- czy serwer ma błędnie skonfigurowany manifest.

DB audit jest pierwszym krokiem do dashboardu i healthchecku realm/content
deployment.

## Następny krok

Step199 realizuje pierwszy punkt: dodaje agregowane widoki per reason/revision
oraz health row dla content manifest rejectów.

Najbliższe sensowne rozwinięcia:

- dodać healthcheck, który ostrzega przy nagłym wzroście mismatchów;
- powiązać DB audit z przyszłym launcher/update flow;
- w rewrite DB przenieść ten bridge do spójnego `event_*`/`net_*` modelu.
