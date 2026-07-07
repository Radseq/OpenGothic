# Step200 - content manifest reject healthcheck

Ten krok dodaje narzędzie:

```text
tools/check_mmo_content_manifest_reject_health.py
```

Narzędzie czyta `v_mmo_content_manifest_reject_health` ze Step199 i zwraca JSON
oraz exit code.

## Przykład

```bash
tools/check_mmo_content_manifest_reject_health.py \
  --url mysql://user:pass@host:3306/db \
  --max-last-15m-rejects 50 \
  --max-last-hour-rejects 200 \
  --max-last-hour-hash-mismatches 100
```

## Co sprawdza

| Check | Domyślny próg |
| --- | --- |
| `last_15m_rejects` | 50 |
| `last_hour_rejects` | 200 |
| `last_hour_hash_mismatches` | 100 |
| `server_manifest_missing` | 0 |

Jeśli któryś check przekroczy próg, narzędzie kończy się statusem `failed` i
exit code `1`.

## Po co

To jest pierwszy prosty healthcheck content deployment:

- po deployu nowej paczki contentu można szybko wykryć falę mismatchów;
- `server_manifest_missing` może od razu oznaczać błąd konfiguracji serwera;
- `client_manifest_missing` może oznaczać klienty uruchamiane bez launchera;
- wzrost `content_hash_mismatch` może wskazywać, że launcher nie zaktualizował
  części graczy.

## Relacja do roadmapy

To nadal bridge przed pełnym dashboardem. Ale daje gotowy punkt integracji dla:

- CI/CD;
- cron healthcheck;
- panelu admina;
- alertu po wdrożeniu content packa.

## Następny krok

Następny mocny krok w tej gałęzi to launcher/update flow:

1. klient dostaje `server_required_content_hash`;
2. menu/launcher pokazuje wymaganą rewizję;
3. launcher umie pobrać właściwy content pack;
4. klient ponawia bootstrap z poprawnym hashem.
