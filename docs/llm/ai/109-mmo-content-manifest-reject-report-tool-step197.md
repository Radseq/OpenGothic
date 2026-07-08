# Step197 - content manifest reject report tool

Ten krok dodaje narzędzie developerskie:

```text
tools/mmo_content_manifest_reject_report.py
```

Skrypt czyta dwa pliki runtime:

| Plik | Źródło | Znaczenie |
| --- | --- | --- |
| `runtime/mmo_server_bootstrap_reject.json` | klient | ostatni bootstrap reject odebrany przez klienta |
| `runtime/mmo_server_content_manifest_rejects.jsonl` | serwer | append-only audyt content manifest rejectów |

Po Step198 skrypt może też opcjonalnie czytać DB audit, jeśli dostanie `--url`.

## Przykład użycia

```bash
tools/mmo_content_manifest_reject_report.py --runtime-dir runtime --limit 20
```

Z DB audytem:

```bash
tools/mmo_content_manifest_reject_report.py \
  --runtime-dir runtime \
  --url mysql://user:pass@host:3306/db \
  --limit 20
```

Wynik jest JSON-em:

- `client_reject_present`;
- pełny `client_reject`;
- liczba rekordów serwerowych;
- agregacja po `reason`;
- agregacja po `content_revision_key`;
- agregacja po `server_required_content_hash`;
- ostatnie rekordy audytu serwerowego.
- opcjonalny `db_audit`, jeśli podano `--url`.

## Dlaczego to jest przydatne

To narzędzie jest małe, ale praktyczne przy pracy nad content gate:

- pozwala szybko sprawdzić, czy klient dostał reject;
- pozwala sprawdzić, jaki hash wymaga serwer;
- pokazuje, czy mismatch dotyczy jednego klienta czy wielu;
- ułatwia testy bez pełnego UI;
- może być później użyte przez CI albo launcher diagnostics.

## Relacja do docelowej architektury

To nadal narzędzie developerskie, nie docelowy system telemetryczny. Docelowo
audyt content rejectów powinien trafić do DB i dashboardu serwera.

Ale ten skrypt daje szybki most między:

- runtime klienta;
- runtime serwera;
- testami lokalnymi;
- przyszłym launcherem.

## Weryfikacja

Skrypt powinien działać także wtedy, gdy plików jeszcze nie ma. W takim przypadku
zwraca pusty raport zamiast błędu.




