# Step193 - client receives content manifest diagnostics

Ten krok dodaje pierwszą klientową reakcję na content manifest reject. Klient nie
ma jeszcze pełnego UI/launchera, ale przestaje traktować contentowy diagnostic
jak zwykły tekstowy błąd serwera.

## Co robi klient

Po odebraniu `ServerDiagnosticPacket` klient:

1. Sprawdza `reason`.
2. Jeżeli reason wygląda na content manifest problem, zwiększa licznik
   `contentManifestDiagnostics`.
3. Jeżeli `message` jest JSON-em z `error = client_content_manifest_rejected`,
   wyciąga:
   - `reason`;
   - `phase`;
   - `client_content_manifest_hash`;
   - `server_required_content_hash`;
   - `content_revision_key`.
4. Loguje specjalną linię `MMO content manifest diagnostic ...`.

To daje developerowi i późniejszemu UI konkret:

```text
reason=content_hash_mismatch
client_hash=...
server_required_hash=...
content_revision=...
```

## Dlaczego to jest ważne

Bez tego klient widziałby tylko:

```text
MMO server diagnostic reason=content_hash_mismatch message={...}
```

To jest wystarczające dla surowego loga, ale za słabe jako fundament pod
launcher i ekran błędu. Po tym kroku klient ma już jedno miejsce, w którym można
później podpiąć:

- blokadę wejścia do świata MMO;
- komunikat w menu;
- link/akcję aktualizacji content packa;
- zapis ostatniego bootstrap rejectu do pliku runtime;
- automatyczne porównanie lokalnego manifestu z wymaganym hashem serwera.

## Ochrona parsera

Parser diagnostic message jest celowo miękki:

- jeżeli message nie jest JSON-em, klient nadal loguje reason;
- jeżeli JSON jest niepełny, brakujące pola trafiają jako `<empty>`;
- wyjątek przy parsowaniu nie zabija wątku UDP.

To ważne, bo `ServerDiagnostic` jest warstwą diagnostyczną, a nie krytyczną
ścieżką symulacji świata.

## Relacja do autorytatywnego serwera

Ten krok nie daje klientowi prawa do decydowania o content packu. Klient tylko
umie lepiej zrozumieć decyzję serwera:

- serwer: porównuje manifest klienta z własnym contentem;
- DB: zwraca decyzję i aktywną rewizję;
- serwer: wysyła diagnostic;
- klient: rozpoznaje, loguje i docelowo zablokuje świat albo uruchomi update.

## Następny krok

Step194 realizuje pierwszy punkt: ostatni content manifest reject jest zapisywany
do `runtime/mmo_server_bootstrap_reject.json`.

Najbardziej logiczne następne prace:

- dodać prosty stan klienta: `lastBootstrapRejectedReason`;
- powstrzymać apply snapshotu/DB continue, jeśli bootstrap ACK jest rejected i
  reason jest contentowy;
- później pokazać to w `GameMenu`.




