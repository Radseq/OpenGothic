# Step194 - client writes bootstrap reject runtime file

Ten krok dodaje mały, ale praktyczny most między siecią MMO a przyszłym UI lub
launcherem. Gdy klient odbierze content manifest diagnostic, zapisuje ostatni
reject do:

```text
runtime/mmo_server_bootstrap_reject.json
```

## Format pliku

Przykład:

```json
{
  "status": "rejected",
  "reject_kind": "content_manifest",
  "action": "client_bootstrap_request",
  "reason": "content_hash_mismatch",
  "phase": "bootstrap",
  "client_content_manifest_hash": "old-client-hash",
  "server_required_content_hash": "required-server-hash",
  "content_revision_key": "gothic_ch1_clean_v1",
  "structured": true,
  "severity": 2,
  "packet_sequence": 10,
  "local_sequence": 10,
  "message": "{... original diagnostic message ...}"
}
```

## Po co ten plik

To jest tymczasowy, ale bardzo wygodny kontrakt developerski:

- `GameMenu` może później pokazać komunikat o złej wersji contentu;
- launcher może zobaczyć wymagany hash i rewizję;
- testy mogą sprawdzić, że klient rozpoznał reject;
- developer nie musi ręcznie szukać JSON-a w logu;
- pełne UI może powstać później bez zmieniania serwerowego protocol slice.

## Zasady bezpieczeństwa

Plik jest tylko lokalnym zapisem diagnostycznym. Nie jest źródłem prawdy.

Źródła prawdy nadal są:

- serwerowy content pack;
- DB z aktywną rewizją contentu;
- procedura walidacji manifestu;
- serwerowy ACK/diagnostic.

Klient nie może przez edycję tego pliku ominąć content gate. Może tylko lepiej
poinformować użytkownika, co poszło źle.

## Implementacyjnie

Zapis jest best-effort:

- katalog `runtime` jest tworzony, jeśli go nie ma;
- najpierw powstaje plik `.tmp`;
- potem następuje rename na plik docelowy;
- błędy zapisu nie zabijają wątku UDP.

## Następny krok

Step195 realizuje pierwszy następny krok: menu potrafi odczytać
`runtime/mmo_server_bootstrap_reject.json`, zalogować content reject i przerwać
ładowanie świata w trybie server-bound.

Najbliższy następny krok:

- pokazać krótki komunikat przy odrzuconym bootstrapie;
- docelowo dodać przycisk/akcję aktualizacji content packa.
