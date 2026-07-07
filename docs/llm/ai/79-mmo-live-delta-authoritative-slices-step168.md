# MMO live delta authoritative slices - step 168

## Cel

Krok 167 dodal typowany kanal `ServerLiveDelta`. Krok 168 wzmacnia go tak, zeby nie byl tylko potwierdzeniem akcji ani echem payloadu klienta. Serwer dolacza male, domenowe wycinki autorytatywnego stanu z istniejacych read-modeli DB, a klient cache'uje ostatnia delte per domena.

Nie dodano zadnych nowych widokow SQL ani procedur. Uzyte sa istniejace funkcje:

- `readCharacterBootstrapSnapshotSlices`
- `readWorldBootstrapSnapshotSlices`
- `readPositionedBootstrapSnapshotSlices`

## Serwer

`ServerLiveDelta` nadal idzie po zaakceptowanej akcji `direct_db`, ale payload `mmo.server_live_delta.v1` moze teraz zawierac pola `authoritative_*`.

Mapowanie domen:

- `MovementCorrection`, `CharacterStats` -> `authoritative_character`
- `Inventory`, `Equipment` -> `authoritative_character`, `authoritative_inventory`, `authoritative_equipment`
- `Story` -> `authoritative_known_dialogs`, `authoritative_quests`, `authoritative_script_state`
- `WorldItem` -> `authoritative_world_item_deltas`, `authoritative_active_world_items`
- `InteractiveState` -> `authoritative_interactive_state`, `authoritative_mover_state`, `authoritative_trigger_queue`, `authoritative_world_transition_state`, `authoritative_world_clock`
- `Combat` -> `authoritative_character`, `authoritative_npc_lifecycle_state`, `authoritative_recent_actions`

Payload ma budzet slice'ow, zeby nie przekraczac bezpiecznego rozmiaru UDP. Za duze slice'y sa pomijane i oznaczane polami:

- `<slice>_omitted`
- `<slice>_bytes`
- `authoritative_slices_included`
- `authoritative_slices_omitted`
- `authoritative_slice_budget_remaining`

## Klient

Klient nadal zapisuje pelny audit:

```text
runtime/mmo_server_live_deltas.jsonl
```

Dodatkowo atomowo nadpisuje:

```text
runtime/mmo_server_live_delta_latest.json
runtime/mmo_server_live_delta_latest_<domain>.json
runtime/mmo_server_live_delta_manifest.json
```

Przyklady domen:

- `movement`
- `character`
- `inventory`
- `equipment`
- `world_item`
- `interactive`
- `combat`
- `story`
- `generic`

To jeszcze nie aplikuje delty do runtime stanu gry z watku UDP. To celowa granica bezpieczenstwa. Kolejny krok moze podpiac `GameSession` pod pliki latest i aplikowac wybrane domeny na glownym watku gry, korzystajac z juz istniejacych restore/applierow.

## Zmienione pliki

- `server/cpp/mmo_udp_server_transport_diagnostics.inl`
- `server/cpp/mmo_udp_server_main_loop.inl`
- `game/game/mmosemanticactionsink.cpp`
