# Step199 - content manifest reject health views

Ten krok dodaje agregowane widoki dla DB audytu ze Step198.

SQL surface:

```text
server/sql/step199_content_manifest_reject_health_views.sql
```

## Nowe widoki

| Widok | Rola |
| --- | --- |
| `v_mmo_content_manifest_reject_summary` | agregacja po `reason + content_revision_key + server_manifest_hash` |
| `v_mmo_content_manifest_reject_health` | pojedynczy health row z licznikami ostatniej godziny i 15 minut |

## Po co to jest

Sama tabela audytu mówi, że konkretna próba bootstrapu została odrzucona. Widoki
odpowiadają na pytania operacyjne:

- ile było rejectów łącznie;
- ile w ostatniej godzinie;
- ile w ostatnich 15 minutach;
- ile to `content_hash_mismatch`;
- ile to brak manifestu klienta;
- czy serwerowi brakuje manifestu;
- ile unikalnych endpointów/klientów jest dotkniętych problemem;
- jaka rewizja contentu najczęściej powoduje rejecty.

## Relacja do wielu graczy

Przy MMO ważna jest skala. Jeden reject może być lokalnym problemem gracza. Sto
rejectów po deployu oznacza problem launchera, paczki moda albo konfiguracji
realmu.

Widoki Step199 są pierwszą warstwą pod:

- healthcheck content deployment;
- alert po wzroście `content_hash_mismatch`;
- dashboard admina;
- diagnostykę launchera.

## Narzędzia

`tools/check_mmo_step198_content_manifest_reject_audit.py` sprawdza teraz także
widoki Step199 i marker:

```text
server/sql/step199_content_manifest_reject_health_views.sql
```

`tools/mmo_content_manifest_reject_report.py --url ...` próbuje użyć tych
widoków, jeśli są dostępne, i zwraca w raporcie:

- `db_audit.health`;
- `db_audit.summary_rows`.

## Następny krok

Step200 realizuje prosty healthcheck/exit-code tool:

- fail, jeśli `last_15m_rejects` przekracza próg;
- fail, jeśli `server_manifest_missing > 0`;
- warning, jeśli `content_hash_mismatch` rośnie po deployu.

To można podpiąć pod CI/CD albo panel administracyjny realmu.

Następny większy krok to launcher/update flow dla `server_required_content_hash`.
