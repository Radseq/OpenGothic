# Step191 - content manifest bootstrap diagnostics

Ten krok domyka praktyczne zachowanie gate'u content packa po stronie serwera.
Jeżeli bootstrap klienta zostanie odrzucony przez walidację manifestu, serwer nie
ukrywa już tego pod ogólnym `bootstrap_failed`. Zamiast tego przepuszcza
maszynowy powód z warstwy DB do diagnostic packetu.

## Dlaczego to ma znaczenie

Content mismatch nie jest zwykłym błędem technicznym. Dla klienta MMO to osobny
stan produktu:

- klient ma złą wersję świata/moda;
- klient nie wysłał manifestu;
- serwer nie ma aktywnego manifestu;
- sesja DB nie jest aktywna;
- realm nie ma poprawnie spiętej rewizji contentu.

Każdy z tych przypadków powinien być rozpoznawalny przez UI i logi. Inaczej
gracz zobaczy tylko niejasne "bootstrap failed", a developer nie będzie wiedział,
czy problemem jest DB, manifest, wersja moda czy sama komunikacja.

## Co teraz robi serwer

Bootstrap catch klasyfikuje komunikat wyjątku z walidacji:

| DB decision | Diagnostic reason |
| --- | --- |
| `client_manifest_missing` | `client_manifest_missing` |
| `content_hash_mismatch` | `content_hash_mismatch` |
| `server_manifest_missing` | `server_manifest_missing` |
| `active_session_not_found` | `active_session_not_found` |
| `realm_not_found` | `realm_not_found` |
| inny reject contentu | `content_manifest_rejected` |
| zwykły błąd bootstrapu | `bootstrap_failed` |

Log serwera też rozdziela przypadki:

- `[bootstrap_failed]` dla zwykłych awarii;
- `[bootstrap_rejected]` dla odrzuceń, które są poprawną decyzją walidacyjną.

## Relacja z klientem

To jeszcze nie jest pełny ekran błędu po stronie klienta, ale jest przygotowany
kontrakt. Klient, który odbierze server diagnostic, może później zrobić:

| Reason | UI |
| --- | --- |
| `client_manifest_missing` | "Klient nie wysłał wersji contentu. Uruchom grę przez launcher MMO." |
| `content_hash_mismatch` | "Masz inną wersję moda niż serwer. Zaktualizuj content pack." |
| `server_manifest_missing` | "Serwer nie ma aktywnego manifestu contentu. Zgłoś administratorowi." |
| `active_session_not_found` | "Sesja wygasła. Zaloguj się ponownie." |
| `realm_not_found` | "Realm nie jest poprawnie skonfigurowany." |

## Dlaczego nie dodawać jeszcze nowego pakietu

Na tym etapie wystarczy istniejący `ServerDiagnostic`. Odrzucenie bootstrapu jest
nadal częścią handshake'u, a diagnostic już ma:

- severity;
- action name;
- reason;
- message.

Nowy typ pakietu `BootstrapRejected` warto dodać dopiero wtedy, gdy klient będzie
miał pełne UI logowania, launcher contentu i kontrolowany przepływ aktualizacji.

## Następne prace

Step192 realizuje punkt serwerowy: diagnostic może już nieść
`server_required_content_hash` i `content_revision_key` w JSON message.

Najbliższy sensowny krok po Step190, Step191 i Step192:

1. Klient odbiera diagnostic po bootstrapie.
2. Jeśli reason jest contentowy, blokuje wejście do świata MMO.
3. Jeśli message jest JSON-em, klient odczytuje wymagany hash i rewizję contentu.
4. UI pokazuje czytelny komunikat.
5. Launcher albo klient potrafi porównać lokalny manifest z wymaganym.

To dalej prowadzi do tego samego celu: serwer ma własny content pack, baza zna
aktywną rewizję contentu, a klient tylko deklaruje zgodność i odtwarza decyzje
serwera.




