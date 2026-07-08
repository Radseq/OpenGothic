# Step196 - server JSONL audit for content manifest rejects

Ten krok dodaje lekki audyt serwerowy dla content manifest rejectów. Gdy bootstrap
zostanie odrzucony przez `ContentManifestValidationError`, serwer zapisuje rekord
JSONL do:

```text
runtime/mmo_server_content_manifest_rejects.jsonl
```

## Przykładowy rekord

```json
{
  "event": "content_manifest_bootstrap_rejected",
  "remote": "127.0.0.1:53210",
  "session_uuid": "...",
  "packet_session_key": "local-dev-PC_HERO_TEST",
  "target_key": "character:PC_HERO:character-list",
  "packet_sequence": 10,
  "local_sequence": 10,
  "reason": "content_hash_mismatch",
  "phase": "bootstrap",
  "client_content_manifest_hash": "old-client-hash",
  "server_required_content_hash": "required-server-hash",
  "content_revision_key": "gothic_ch1_clean_v1",
  "message": "client content manifest rejected: content_hash_mismatch"
}
```

## Dlaczego JSONL

To jest etap przejściowy przed docelowym audytem w DB:

- łatwo dopisywać rekordy append-only;
- można szybko grepować i tailować;
- nie wymaga nowej migracji schematu;
- nie blokuje ścieżki bootstrapu, jeśli zapis się nie uda;
- daje materiał testowy dla późniejszej tabeli audytowej.

## Co powinno trafić do DB później

Docelowy rewrite bazy powinien mieć tabelę w stylu:

| Kolumna | Sens |
| --- | --- |
| `event_id` | identyfikator eventu audytu |
| `created_at` | czas serwera |
| `realm_id` | realm |
| `session_id` | sesja, jeśli istnieje |
| `account_id` | konto, jeśli znane |
| `character_id` | postać, jeśli znana |
| `remote_endpoint` | adres klienta |
| `client_manifest_hash` | hash zadeklarowany przez klienta |
| `server_manifest_hash` | hash wymagany przez serwer |
| `content_revision_key` | aktywna rewizja contentu |
| `reason` | `client_manifest_missing`, `content_hash_mismatch`, itd. |
| `payload_json` | pełne dane diagnostyczne |

## Relacja do wielu graczy

W single-player błędna wersja contentu jest lokalnym problemem. W MMO to problem
operacyjny:

- jeden gracz może mieć stary mod;
- wielu graczy może wejść po aktualizacji serwera z nieaktualnym klientem;
- launcher może mieć błąd dystrybucji;
- realm może wskazywać złą rewizję contentu.

JSONL pozwala szybko zobaczyć skalę problemu zanim powstanie pełna tabela DB i
dashboard.

Step197 dodaje małe narzędzie raportujące dla tego pliku:
`tools/mmo_content_manifest_reject_report.py`.

Step198 dodaje DB audit jako docelowszy bridge. JSONL zostaje fallbackiem i
lokalnym śladem developerskim.

## Następny krok

Najbliższy krok serwerowy:

- dodać agregację per `reason` i `content_revision_key`;
- użyć tych danych w healthchecku realm/content deployment.




