# MMO server-authoritative character events - step 182

Ten krok koryguje kierunek ze step 181.

## Problem

`ClientCharacterStatePacket` sugerowal, ze klient moze wyslac prawdziwy stan postaci:

- `experience_after`,
- `mana_after`,
- `level_after`,
- `health_after`,
- `learning_points_after`.

To jest zle dla MMO. Klient nie moze byc zrodlem prawdy o statystykach postaci.

## Poprawka

`ClientCharacterStatePacket` zostal zastapiony przez:

- `PacketKind::ClientCharacterEvent`
- `ClientCharacterEventPacket`
- `encodeClientCharacterEventPacket`
- `decodeClientCharacterEventPacket`

Pakiet klient -> serwer przenosi tylko wejscie do walidacji:

- `requested_delta`,
- `requested_amount`,
- `mana_amount`,
- `experience_reward`,
- `learning_points_reward`,
- `resource_key`,
- `progression_key`,
- `reward_key`,
- `reason`,
- `source_actor_key`,
- `source_entity_key`.

Nie przenosi finalnego stanu postaci.

## Zasada

Klient moze powiedziec:

- chce zuzyc 15 many,
- skrypt/zdarzenie zglosilo nagrode exp,
- zasob postaci zmienil sie lokalnie w Gothic hooku i wymaga walidacji.

Serwer musi:

- odczytac aktualny stan z DB/runtime,
- zwalidowac zdarzenie,
- policzyc finalny stan,
- zapisac wynik,
- odeslac autorytatywne `ServerLiveDeltaKind::CharacterStats`.

## Serwer

`mmo_udp_server_direct_combat_apply.inl` nie ufa juz `value_before/value_after` dla resource delta.
Dla `ApplyCharacterResourceDelta` i `ConsumeMana` czyta aktualna wartosc zasobu po stronie serwera, liczy delte i dopiero wtedy zapisuje wynik.

## JSON

UDP pozostaje binarne.

JSON wystepuje tylko jako lokalny compatibility bridge po stronie serwera i ma teraz pola `requested_*` oraz `server_authoritative=true`.
Nie udaje finalnego stanu postaci.

## Weryfikacja

Sprawdzone:

- round-trip `ClientCharacterEventPacket` dla `ApplyExperienceReward`,
- round-trip `ClientCharacterEventPacket` dla `ConsumeMana`,
- brak pozostalosci `ClientCharacterState` w zmienionych plikach,
- `mmonetprotocol.h` przez `g++ -std=c++20 -fsyntax-only`.

