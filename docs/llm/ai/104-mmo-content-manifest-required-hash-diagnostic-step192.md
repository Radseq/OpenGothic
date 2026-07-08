# Step192 - required content hash in bootstrap diagnostic

Ten krok rozwija Step191. Serwer nie tylko mówi już, że bootstrap został
odrzucony przez content manifest gate, ale potrafi przekazać klientowi szczegóły
potrzebne do naprawy problemu.

## Co zostało dodane

Warstwa persistence dostała osobny typ błędu:

```cpp
ContentManifestValidationError
```

Ten błąd niesie:

| Pole | Znaczenie |
| --- | --- |
| `decision` | kod decyzji DB, np. `content_hash_mismatch` |
| `clientHash` | hash zadeklarowany przez klienta |
| `serverHash` | hash wymagany przez serwer |
| `revisionKey` | aktywna rewizja contentu realm/server |
| `phase` | faza walidacji, np. `startup` albo `bootstrap` |

Bootstrap handler łapie ten typ przed zwykłym `std::exception` i buduje
strukturalny JSON w `ServerDiagnostic.message`.

## Format diagnostic message

Przykładowy payload w `ServerDiagnostic.message`:

```json
{
  "error": "client_content_manifest_rejected",
  "reason": "content_hash_mismatch",
  "phase": "bootstrap",
  "client_content_manifest_hash": "old-client-hash",
  "server_required_content_hash": "required-server-hash",
  "content_revision_key": "gothic_ch1_clean_v1",
  "message": "client content manifest rejected: content_hash_mismatch"
}
```

`ServerDiagnostic.reason` nadal dostaje krótki kod maszynowy, np.
`content_hash_mismatch`. `message` jest miejscem na bogatsze dane.

## Dlaczego message JSON zamiast nowego pakietu

Na tym etapie istniejący `ServerDiagnosticPacket` wystarcza:

- ma `reason` dla prostego routingu;
- ma `message` jako dłuższy string;
- nie trzeba zmieniać wersji protokołu;
- stare klienty nadal tylko zalogują tekst;
- nowe klienty mogą rozpoznać, że `message` zaczyna się od JSON object i
  sparsować szczegóły.

Nowy typ pakietu `BootstrapRejected` warto dodać dopiero wtedy, gdy logowanie MMO,
launcher i UI aktualizacji contentu będą miały stabilny przepływ.

## Co klient może z tym zrobić

Minimalne zachowanie klienta:

1. Odbierz `ServerDiagnostic`.
2. Jeśli `reason` jest jednym z contentowych kodów, nie próbuj dalej wchodzić do
   świata.
3. Jeśli `message` jest JSON-em, odczytaj `server_required_content_hash` i
   `content_revision_key`.
4. Pokaż użytkownikowi czytelny komunikat.
5. W przyszłości przekaż te dane launcherowi albo updaterowi moda.

Przykładowe mapowanie:

| Reason | UI |
| --- | --- |
| `client_manifest_missing` | Klient nie wysłał wersji contentu. Uruchom przez launcher MMO. |
| `content_hash_mismatch` | Masz inną wersję contentu niż serwer. Wymagana rewizja: `content_revision_key`. |
| `server_manifest_missing` | Serwer nie ma manifestu contentu. To błąd konfiguracji serwera. |
| `realm_not_found` | Realm nie jest poprawnie skonfigurowany. |
| `active_session_not_found` | Sesja wygasła lub nie została poprawnie utworzona. |
| `session_uuid_missing` | Serwer wymaga content gate, ale nie ma sesji DB do walidacji. |

## Relacja do autorytatywnego serwera

To nadal nie importuje jeszcze ZEN/DAT/OU na serwer, ale usuwa ważną dziurę w
handshake'u:

- serwer zna własny wymagany content hash;
- klient deklaruje swój hash;
- DB podejmuje decyzję;
- serwer odsyła kod i szczegóły wymaganej wersji;
- klient może zablokować świat zanim zacznie renderować niezgodny content.

To jest podstawa pod późniejsze NPC/percepcję/dialogi. Jeżeli klient ma inny
content, serwerowy event typu "NPC zaczepia gracza" mógłby wskazywać dialog,
NPC albo output, którego klient nie posiada albo ma w innej wersji.

## Następny krok

Step193 realizuje pierwszy klientowy etap: klient rozpoznaje contentowe
`ServerDiagnostic.reason` i próbuje sparsować `ServerDiagnostic.message` jako
JSON.

Najbliższy sensowny następny krok po stronie klienta:

- zapisać szczegóły ostatniego bootstrap rejectu;
- zatrzymać wejście do świata MMO w trybie server-bound;
- przygotować małą warstwę UI/logging dla komunikatu "zła wersja contentu".

Najbliższy sensowny następny krok po stronie serwera:

- zapisać content reject bootstrapu do DB jako audyt sesji;
- dodać timestamp i remote endpoint;
- policzyć metryki, ile klientów odpada przez mismatch;
- później rozbudować to o launcher/updater.




