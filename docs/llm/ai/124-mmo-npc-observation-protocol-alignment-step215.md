# Step215 - NPC observation protocol alignment

Ten krok usuwa blokadę po Step214: klient miał już hooki dla kilku obserwacji
NPC, ale wspólny `SemanticActionKind` nie zawierał wszystkich akcji używanych
przez `mmonetprotocol.h` i `mmosemantichooks.cpp`.

## Decyzja

Nie dodajemy teraz pełnej persystencji DB dla:

- `record_combat_intent`;
- `record_npc_action_state`;
- `record_npc_dialog_line`.

To są obserwacje pomocnicze do przyszłego server-side NPC ticka. Bez gotowego
read modelu z `mmo_content_build` serwer nie ma jeszcze pełnego kontekstu
template NPC, rutyn, perception bindings i dialog outputs, więc zapis tych
akcji jako twardej prawdy w runtime DB byłby przedwczesny.

Poprawny kierunek pozostaje taki:

1. serwer czyta własny content pack do `mmo_content_build`;
2. zatwierdzony build publikuje runtime read model;
3. server NPC perception/routine tick korzysta z read modelu;
4. dopiero decyzje ticka trafiają do `mmo_ai_runtime` i dispatch queue.

## Zmiany

| Plik | Zmiana |
|---|---|
| `game/game/mmosemanticevents.h` | dodano trzy brakujące `SemanticActionKind` na końcu enumu, bez przesuwania istniejących ID |
| `server/cpp/mmo_udp_server.cpp` | dodano fail-open/no-op dla nowych obserwacji w direct DB path i przed starym outbox fallback |
| `server/cpp/mmo_udp_server_payload_mapper.inl` | zaktualizowano listę obserwacji fail-open dla przyszłego splitu |
| `server/cpp/mmo_udp_server_main_loop.inl` | zabezpieczono unhandled branch dla obserwacji fail-open |

## Weryfikacja

Wykonane:

```bash
cmake --build build/mmo_cpp_server --target mmo_udp_server -j
```

Wynik:

- `mmo_udp_server` buduje się poprawnie;
- obserwacyjne pakiety NPC nie powinny już kończyć się `direct_db_unhandled`
  tylko dlatego, że docelowy kontrakt DB nie istnieje jeszcze w tej warstwie;
- przy `--enqueue-outbox` nie powinny wpadać do starego
  `mmo_server_action_outbox`, bo NPC perception ma docelowo osobny runtime
  dispatch w `mmo_ai_runtime`.




