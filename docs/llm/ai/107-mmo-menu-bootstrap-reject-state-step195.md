# Step195 - menu reads bootstrap reject state

Ten krok podpina `runtime/mmo_server_bootstrap_reject.json` do menu klienta.
Po Step194 klient zapisywał ostatni content manifest reject do pliku runtime.
Teraz menu potrafi ten plik odczytać, zalogować problem i użyć go jako prostego
stanu blokującego wejście do świata MMO.

## Co robi menu

W trybie server-bound:

1. Przed nowym bootstrap requestem menu usuwa stary
   `runtime/mmo_server_bootstrap_reject.json`.
2. Podczas oczekiwania na character list snapshot sprawdza, czy sink zapisał nowy
   reject.
3. Jeśli reject istnieje, loguje go tylko raz dla danej treści pliku.
4. Sloty load mogą dostać krótki opis w `text[1]`, np.
   `MMO content rejected: content_hash_mismatch rev=gothic_ch1_clean_v1`.
5. `execLoadGame` przerywa ładowanie, jeśli aktywny jest content manifest reject.
6. DB Continue pre-world przerywa oczekiwanie na snapshot, jeśli pojawił się
   `runtime/mmo_server_bootstrap_reject.json`.

## Dlaczego czyścić stary reject

Stary plik z poprzedniego uruchomienia nie może samodzielnie blokować nowej
próby. Dlatego menu czyści plik przed wysłaniem nowego bootstrapu.

Jeśli serwer odrzuci aktualną próbę, UDP sink zapisze świeży plik. Wtedy menu
traktuje go jako aktualny stan.

## Zakres odpowiedzialności

To nadal nie jest docelowe UI. Ten krok robi tylko minimalny most:

- plik runtime -> menu;
- menu -> log;
- menu -> blokada load;
- menu -> krótki opis na slocie.

Pełne UI powinno później pokazać czytelny ekran:

- "Masz inną wersję contentu niż serwer";
- "Wymagana rewizja: ...";
- "Wymagany hash: ...";
- "Uruchom aktualizację content packa".

## Ryzyka

| Ryzyko | Mitigacja |
| --- | --- |
| Plik rejectu jest stary | Menu usuwa go przed nowym bootstrap requestem |
| Plik jest niepełny | Parser jest miękki, brakujące pola są puste |
| Brak UI | Na razie jest log i opis slotu |
| Gracz próbuje wejść mimo mismatchu | `execLoadGame` zwraca `false` przy aktywnym rejectcie |
| DB Continue czeka do timeoutu | Pre-world bootstrap kończy się od razu po wykryciu reject file |

## Następny krok

Najbliższy krok po tej stronie to przenieść tę informację z logu/slotu do
właściwego komunikatu UI w `GameMenu`, a później do launchera.




