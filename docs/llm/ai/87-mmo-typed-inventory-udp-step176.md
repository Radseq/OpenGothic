# Step 176 - typed inventory/equipment/trade UDP

## Cel

Kontynuacja przenoszenia komunikacji klient-serwer z JSON-over-UDP na binarny protokol UDP.
JSON zostaje tylko jako lokalny format debug/zgodnosci po stronie klienta i serwera.

## Zakres pakietu

Dodano `PacketKind::ClientInventory` oraz `ClientInventoryPacket` dla akcji:

- `PickupWorldItem`
- `RemoveWorldItem`
- `TransferCharacterItem`
- `EquipCharacterItem`
- `UnequipCharacterItem`
- `DropCharacterItem`
- `LootNpcInventory`
- `TakeContainerItem`
- `PutContainerItem`
- `TradeBuyFromNpc`
- `TradeSellToNpc`
- `ConsumeItem`
- `SplitItemStack`
- `MergeItemStack`

Pakiet niesie pola wspolne dla inventory/equipment/container/trade:

- actor/source/target keys,
- item template, item symbol i persistent ids,
- world item, vendor item i seller item ids,
- slot/bag/target bag,
- container/NPC/source entity keys,
- ceny, currency i wallet before/after,
- actor/item/source positions,
- flagi: whole-instance move, source dead/unconscious, container, position presence, equipment slot, trade price, wallet.

## Klient

`mmosemanticactionsink.cpp` buduje `ClientInventoryPacket` z dotychczasowego payloadu debugowego.
W trybie server-bound UDP kolejnosc pakowania jest teraz:

1. typed combat damage,
2. typed movement/checkpoint,
3. typed inventory/equipment/container/trade,
4. legacy `ClientAction` tylko dla akcji jeszcze nieprzeniesionych.

To usuwa JSON z kabla dla szerokiego zestawu akcji inventory bez przebudowy callerow gameplay.

## Serwer

`mmo_udp_server_main_loop.inl` dekoduje `ClientInventoryPacket` i tworzy lokalny payload zgodnosci dla istniejacych handlerow direct DB/outbox.
Most odtwarza aliasy uzywane przez aktualne moduly, m.in.:

- `world_item_entity_key` / `engine_world_item_key`,
- `source_npc_key` / `source_npc_entity_key`,
- `npc_key` / `npc_entity_key` / `target_npc_entity_key`,
- `equipment_slot`,
- `source_container_key` / `container_key`,
- `actor_position`, `item_position`, `source_npc_position`,
- `unit_price`, `price_total`, `wallet_before`, `wallet_after`.

## Uwagi

- To jest modul komunikacyjny, nie nowa procedura DB ani widok SQL.
- Serwer nadal moze logowac payload JSON i podawac go do istniejacych lokalnych handlerow, ale UDP wire-format dla tych akcji jest binarny.
- Legacy `ClientAction` zostaje jako fallback dla akcji, ktore nie maja jeszcze typed packetu.

## Weryfikacja

- `ClientInventoryPacket` encode/decode round-trip dla `LootNpcInventory`.
- `mmonetprotocol.h` syntax-only C++20.
