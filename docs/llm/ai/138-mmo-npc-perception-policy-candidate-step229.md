# Step229 - C++ NPC perception policy candidate pass

## Powod

Step228 laduje `WorldInstanceContentCache` podczas startu serwera. Nastepny
krok przed mutacja `mmo_ai_runtime` to deterministyczna C++ warstwa policy,
ktora potrafi z aktywnych NPC/graczy i content cache wybrac kandydacka decyzje
percepcji.

To nadal nie jest Daedalus VM, live NPC movement, dialog broadcast ani zapis do
DB. Ten krok przygotowuje dane pod procedure
`mmo_ai_record_npc_perception_decision(...)`.

## Zrobione

Dodano:

- `server/cpp/mmo_npc_perception_policy.h`
- `server/cpp/mmo_npc_perception_policy.cpp`
- `server/cpp/mmo_npc_perception_policy_probe.cpp`
- target CMake `mmo_npc_perception_policy`
- target CMake `mmo_npc_perception_policy_probe`

Policy:

- przyjmuje listy aktywnych NPC i graczy;
- filtruje pary po aktywnosci i dystansie;
- uzywa `WorldInstanceContentCache::perceptionBindingsByKind`;
- mapuje kandydacki `perception_kind` na bezpieczny `decision_kind`;
- buduje stabilny `idempotency_key`;
- buduje payload JSON pasujacy do przyszlego zapisu decyzji;
- nie mutuje DB i nie wysyla zadnych pakietow.

Domyslne mapowanie:

| Perception kind | Decision kind |
|---|---|
| `PERC_ASSESSPLAYER` | `greet_player` |
| `PERC_ASSESSWARN` | `warn_player` |
| `PERC_ASSESSENEMY` / `PERC_ASSESSFIGHTER` | `attack_player` |
| `PERC_ASSESSBODY` / `PERC_ASSESSMAGIC` / `PERC_CANDIDATE` | `assess_player` |
| inne | `noop` |

## Zweryfikowane komendy

Build:

```bash
cmake --build build/mmo_cpp_server --target mmo_npc_perception_policy_probe -j
```

Pozytywny probe:

```bash
build/mmo_cpp_server/mmo_npc_perception_policy_probe \
  /tmp/opengothic_step227_runtime_read_model.json \
  gothic2-notr-steam-local \
  newworld \
  newworld
```

Wynik:

```text
status=ready
perception_kind=PERC_ASSESSPLAYER
evaluated_pairs=1
decisions=1
rule_key=B_ASSESSPLAYER
decision_kind=greet_player
enqueue_action=true
```

Negatywne probe:

```bash
build/mmo_cpp_server/mmo_npc_perception_policy_probe \
  /tmp/opengothic_step227_runtime_read_model.json \
  gothic2-notr-steam-local \
  newworld \
  newworld \
  --target-distance 5000 \
  --max-distance 100
```

Zwraca `status=no_decision` i `distance_pairs_skipped=1`.

```bash
build/mmo_cpp_server/mmo_npc_perception_policy_probe \
  /tmp/opengothic_step227_runtime_read_model.json \
  gothic2-notr-steam-local \
  newworld \
  newworld \
  --perception-kind PERC_DOES_NOT_EXIST
```

Zwraca `status=no_decision` i `missing_perception_binding_pairs=1`.

## Nastepny krok

Step230 powinien podlaczyc kandydacka decyzje do
`mmo_ai_record_npc_perception_decision(...)`:

- pobrac/otrzymac realne aktywne NPC i graczy z runtime DB/projekcji;
- uzyc Step229 policy do decyzji;
- zapisac decyzje do `mmo_ai_runtime`;
- nadal nie broadcastowac dialogu/ruchu, dopoki dispatch/fan-out nie jest
  domkniety.
