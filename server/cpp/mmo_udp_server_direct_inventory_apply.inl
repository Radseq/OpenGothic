// Internal implementation partition for mmo_udp_server.cpp.
// Handles inventory, loot, pickup, drop and equipment direct DB actions.

[[nodiscard]] DirectApplyResult directApplied() noexcept {
  return {true, true, true, "direct_applied"};
}

[[nodiscard]] Mmo::Server::GrantCharacterItemBySymbolRecord makeGrantBySymbolRecord(std::string_view sessionUuid,
                                                                                    std::int64_t symbol,
                                                                                    std::int64_t amount,
                                                                                    std::int64_t bagIndex,
                                                                                    std::uint64_t tick,
                                                                                    std::string_view dbPayload,
                                                                                    std::string_view idempotencyKey) noexcept {
  return {
    .sessionUuid = sessionUuid,
    .itemSymbol = symbol,
    .amount = amount,
    .bagIndex = bagIndex,
    .serverTick = tick,
    .dbPayload = dbPayload,
    .idempotencyKey = idempotencyKey,
  };
}

[[nodiscard]] std::int64_t tradePriceTotalFromPayload(std::string_view payload, std::int64_t amount) noexcept {
  const auto explicitTotal = optionalJsonI64(payload, "price_total", 0);
  if(explicitTotal != 0)
    return explicitTotal;

  const auto unitPrice = optionalJsonI64(payload, "unit_price", 0);
  const auto safeAmount = std::max<std::int64_t>(1, amount);
  constexpr auto MaxAbs = Mmo::Server::InventoryAuthority::MaxTradePriceAbs;
  if(unitPrice > 0 && unitPrice > MaxAbs / safeAmount)
    return MaxAbs + 1;
  if(unitPrice < 0 && unitPrice < -MaxAbs / safeAmount)
    return -MaxAbs - 1;
  return unitPrice * safeAmount;
}

[[nodiscard]] std::string tradeCurrencyKeyFromPayload(std::string_view payload) {
  auto currency = optionalJsonString(payload, "currency_key", "g2notr:gold");
  if(currency.empty())
    currency = "g2notr:gold";
  return currency;
}

[[nodiscard]] DirectApplyResult applyInventoryDirectDb(const Mmo::Server::DirectApplyRequest& request) {
  const MySqlTarget& target = request.target;
  const std::string_view sessionUuid = request.sessionUuid;
  const Mmo::Net::ClientActionPacket& packet = request.packet;
  const std::string_view dbPayload = request.dbPayload;
  const std::string_view payload = packet.payloadJson;
  const auto tick = packetServerTick(packet);

  if(packet.kind == Mmo::SemanticActionKind::SplitItemStack ||
     packet.kind == Mmo::SemanticActionKind::MergeItemStack) {
    return {true, true, false, "stack_layout_noop"};
  }

  if(packet.kind == Mmo::SemanticActionKind::TransferCharacterItem) {
    const auto targetCharacter = optionalJsonString(payload, "target_character_key");
    const auto sourceActor = optionalJsonString(payload, "source_actor_key");
    if(targetCharacter.empty() || sourceActor.empty()) {
      // Legacy Inventory::transfer packets did not carry enough owner identity to
      // apply a safe authoritative mutation. New server-bound clients emit
      // domain-specific container/loot/trade/drop hooks instead. Accept the
      // legacy packet as a no-op to avoid punishing local UI-only inventory churn.
      return {true, true, false, "transfer_character_item_legacy_noop"};
    }

    const auto amount = optionalJsonI64(payload, "amount", 1);
    const auto validation = Mmo::Server::InventoryAuthority::validateTransfer({
      .sourceCharacterKey = sourceActor,
      .targetCharacterKey = targetCharacter,
      .amount = amount,
    });
    if(!validation.accepted)
      return {true, false, false, validation.reason};

    Mmo::Server::transferCharacterItem(target, {
      .sessionUuid = sessionUuid,
      .itemUuid = resolveCharacterItemUuid(target, sessionUuid, packet),
      .targetCharacterKey = targetCharacter,
      .amount = amount,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return directApplied();
  }

  if(packet.kind == Mmo::SemanticActionKind::LootNpcInventory ||
     packet.kind == Mmo::SemanticActionKind::TakeContainerItem) {
    const auto amount = optionalJsonI64(payload, "amount", 1);
    auto bagIndex = optionalJsonI64(payload, "server_bag_index", -1);
    if(bagIndex < 0)
      bagIndex = nextBagIndex(target, sessionUuid);
    const auto ownerHint = optionalJsonString(payload, "source_npc_entity_key",
                          optionalJsonString(payload, "source_entity_key",
                          optionalJsonString(payload, "container_key", packet.targetKey)));
    const auto validation = Mmo::Server::InventoryAuthority::validateWorldInventoryTake({
      .ownerKey = ownerHint,
      .amount = amount,
      .bagIndex = bagIndex,
    });
    if(!validation.accepted)
      return {true, false, false, validation.reason};
    if(packet.kind == Mmo::SemanticActionKind::TakeContainerItem) {
      const auto actorKey = optionalJsonString(payload, "actor_key", optionalJsonString(payload, "source_actor_key", ""));
      recordPerceptionEvent({
        .perceptionId = 17,
        .sourceKey = actorKey,
        .otherKey = actorKey,
        .itemKey = ownerHint,
        .reason = "container_item_take_observed",
        .originPosition = optionalGameplayVec3(payload, "actor_position"),
        .serverTickMs = tick,
      }, &target, sessionUuid, packet.idempotencyKey);
    }

    try {
      const auto sourceEntityKey = resolveWorldInventoryOwnerEntityKey(target, sessionUuid, packet);
      const auto itemUuid = resolveNpcInventoryItemUuid(target, sessionUuid, sourceEntityKey, packet);
      Mmo::Server::lootWorldInventoryItem(target, {
        .sessionUuid = sessionUuid,
        .sourceEntityKey = sourceEntityKey,
        .itemUuid = itemUuid,
        .amount = amount,
        .bagIndex = bagIndex,
        .serverTick = tick,
        .dbPayload = dbPayload,
        .idempotencyKey = packet.idempotencyKey,
      });
      return directApplied();
    } catch(const std::exception& resolveError) {
      const bool corpseLoot = packet.kind == Mmo::SemanticActionKind::LootNpcInventory &&
        (optionalJsonBool(payload, "source_dead", false) ||
         optionalJsonBool(payload, "source_unconscious", false) ||
         optionalJsonString(payload, "reason") == "loot_dead_or_unconscious_npc");
      const auto symbol = itemSymbolFromPayload(payload);
      if(!corpseLoot || symbol < 0)
        throw;
      try {
        const auto sourceEntityKey = resolveWorldInventoryOwnerEntityKey(target, sessionUuid, packet);
        const auto itemUuid = materializeObservedNpcLootItem(target, sessionUuid, sourceEntityKey, packet, dbPayload);
        Mmo::Server::lootWorldInventoryItem(target, {
          .sessionUuid = sessionUuid,
          .sourceEntityKey = sourceEntityKey,
          .itemUuid = itemUuid,
          .amount = amount,
          .bagIndex = bagIndex,
          .serverTick = tick,
          .dbPayload = dbPayload,
          .idempotencyKey = packet.idempotencyKey,
        });
        return directApplied();
      } catch(const std::exception& materializeError) {
        std::cerr << "[npc_loot_grant_fallback] target=" << packet.targetKey
                  << " symbol=" << symbol
                  << " amount=" << amount
                  << " resolve_reason=" << resolveError.what()
                  << " materialize_reason=" << materializeError.what() << "\n";
        Mmo::Server::grantCharacterItemBySymbol(
          target,
          makeGrantBySymbolRecord(sessionUuid, symbol, amount, bagIndex, tick, dbPayload, packet.idempotencyKey));
        return directApplied();
      }
    }
  }

  if(packet.kind == Mmo::SemanticActionKind::PickupWorldItem) {
    const auto amount = optionalJsonI64(payload, "amount", 1);
    auto bagIndex = optionalJsonI64(payload, "server_bag_index", -1);
    if(bagIndex < 0)
      bagIndex = nextBagIndex(target, sessionUuid);
    const auto worldItemKey = optionalJsonString(payload, "world_item_entity_key",
                              optionalJsonString(payload, "engine_world_item_key",
                              optionalJsonString(payload, "target_key", packet.targetKey)));
    const auto symbol = itemSymbolFromPayload(payload);
    const auto pickupValidation = Mmo::Server::InventoryAuthority::validateWorldItemPickup({
      .entityKey = worldItemKey,
      .itemSymbol = symbol,
      .amount = amount,
      .bagIndex = bagIndex,
    });
    if(!pickupValidation.accepted)
      return {true, false, false, pickupValidation.reason};
    const auto actorKey = optionalJsonString(payload, "actor_key", optionalJsonString(payload, "source_actor_key", ""));
    recordPerceptionEvent({
      .perceptionId = 17,
      .sourceKey = actorKey,
      .otherKey = actorKey,
      .itemKey = worldItemKey,
      .reason = "world_item_pickup_observed",
      .originPosition = optionalGameplayVec3(payload, "actor_position"),
      .serverTickMs = tick,
    }, &target, sessionUuid, packet.idempotencyKey);

    try {
      Mmo::Server::pickupWorldItem(target, {
        .sessionUuid = sessionUuid,
        .entityKey = resolveWorldItemEntityKey(target, sessionUuid, packet),
        .amount = amount,
        .bagIndex = bagIndex,
        .serverTick = tick,
        .dbPayload = dbPayload,
        .idempotencyKey = packet.idempotencyKey,
      });
      return directApplied();
    } catch(const std::exception& resolveError) {
      if(symbol < 0)
        throw;
      try {
        Mmo::Server::pickupWorldItem(target, {
          .sessionUuid = sessionUuid,
          .entityKey = materializeObservedWorldItem(target, sessionUuid, packet, dbPayload),
          .amount = amount,
          .bagIndex = bagIndex,
          .serverTick = tick,
          .dbPayload = dbPayload,
          .idempotencyKey = packet.idempotencyKey,
        });
        return directApplied();
      } catch(const std::exception& materializeError) {
        std::cerr << "[world_item_pickup_grant_fallback] target=" << packet.targetKey
                  << " symbol=" << symbol
                  << " amount=" << amount
                  << " resolve_reason=" << resolveError.what()
                  << " materialize_reason=" << materializeError.what() << "\n";
        Mmo::Server::grantCharacterItemBySymbol(
          target,
          makeGrantBySymbolRecord(sessionUuid, symbol, amount, bagIndex, tick, dbPayload, packet.idempotencyKey));
        return directApplied();
      }
    }
  }

  if(packet.kind == Mmo::SemanticActionKind::RemoveWorldItem) {
    Mmo::Server::removeWorldItem(target, {
      .sessionUuid = sessionUuid,
      .entityKey = resolveWorldItemEntityKey(target, sessionUuid, packet),
      .reason = optionalJsonString(payload, "reason", "semantic_action"),
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return directApplied();
  }

  if(packet.kind == Mmo::SemanticActionKind::ConsumeItem) {
    const auto amount = optionalJsonI64(payload, "amount", 1);
    const auto validation = Mmo::Server::InventoryAuthority::validateConsume({.amount = amount});
    if(!validation.accepted)
      return {true, false, false, validation.reason};
    Mmo::Server::consumeCharacterItem(target, {
      .sessionUuid = sessionUuid,
      .itemUuid = resolveCharacterItemUuid(target, sessionUuid, packet),
      .amount = amount,
      .reason = optionalJsonString(payload, "reason", "item_consumed"),
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return directApplied();
  }

  if(packet.kind == Mmo::SemanticActionKind::TradeSellToNpc) {
    const auto amount = optionalJsonI64(payload, "amount", 1);
    const auto priceTotal = tradePriceTotalFromPayload(payload, amount);
    const auto currencyKey = tradeCurrencyKeyFromPayload(payload);
    const auto npcHint = optionalJsonString(payload, "npc_entity_key",
                        optionalJsonString(payload, "target_npc_entity_key",
                        optionalJsonString(payload, "npc_key", packet.targetKey)));
    const auto validation = Mmo::Server::InventoryAuthority::validateTrade({
      .npcKey = npcHint,
      .currencyKey = currencyKey,
      .amount = amount,
      .priceTotal = priceTotal,
      .bagIndex = -1,
    });
    if(!validation.accepted)
      return {true, false, false, validation.reason};

    const auto npc = resolveTradeNpcEntityKey(target, sessionUuid, packet);
    Mmo::Server::tradeSellToNpc(target, {
      .sessionUuid = sessionUuid,
      .npcKey = npc.entityKey,
      .itemUuid = resolveCharacterItemUuid(target, sessionUuid, packet),
      .priceTotal = priceTotal,
      .currencyKey = currencyKey,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return directApplied();
  }

  if(packet.kind == Mmo::SemanticActionKind::TradeBuyFromNpc) {
    const auto amount = optionalJsonI64(payload, "amount", 1);
    const auto priceTotal = tradePriceTotalFromPayload(payload, amount);
    const auto currencyKey = tradeCurrencyKeyFromPayload(payload);
    auto bagIndex = optionalJsonI64(payload, "target_bag_index",
                    optionalJsonI64(payload, "server_bag_index", -1));
    if(bagIndex < 0)
      bagIndex = nextBagIndex(target, sessionUuid);
    const auto npcHint = optionalJsonString(payload, "npc_entity_key",
                        optionalJsonString(payload, "target_npc_entity_key",
                        optionalJsonString(payload, "npc_key", packet.targetKey)));
    const auto validation = Mmo::Server::InventoryAuthority::validateTrade({
      .npcKey = npcHint,
      .currencyKey = currencyKey,
      .amount = amount,
      .priceTotal = priceTotal,
      .bagIndex = bagIndex,
    });
    if(!validation.accepted)
      return {true, false, false, validation.reason};

    const auto npc = resolveTradeNpcEntityKey(target, sessionUuid, packet);
    Mmo::Server::tradeBuyFromNpc(target, {
      .sessionUuid = sessionUuid,
      .npcKey = npc.entityKey,
      .itemUuid = resolveNpcInventoryItemUuid(target, sessionUuid, npc.entityKey, packet),
      .priceTotal = priceTotal,
      .currencyKey = currencyKey,
      .bagIndex = bagIndex,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return directApplied();
  }

  if(packet.kind == Mmo::SemanticActionKind::EquipCharacterItem) {
    const auto requestedSlot = normalizedEquipmentSlot(payload);
    const auto validation = Mmo::Server::InventoryAuthority::validateEquipment({.slot = requestedSlot});
    if(!validation.accepted)
      return {true, false, false, validation.reason};
    const auto itemUuid = resolveCharacterItemUuid(target, sessionUuid, packet);
    const auto slot = resolveEquipmentSlotForEquip(target, sessionUuid, itemUuid, requestedSlot);
    Mmo::Server::equipCharacterItem(target, {
      .sessionUuid = sessionUuid,
      .itemUuid = itemUuid,
      .equipmentSlot = slot,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return directApplied();
  }

  if(packet.kind == Mmo::SemanticActionKind::UnequipCharacterItem) {
    const auto requestedSlot = normalizedEquipmentSlot(payload);
    const auto validation = Mmo::Server::InventoryAuthority::validateEquipment({.slot = requestedSlot});
    if(!validation.accepted)
      return {true, false, false, validation.reason};
    const auto itemUuid = resolveCharacterItemUuid(target, sessionUuid, packet);
    const auto slot = resolveEquipmentSlotForUnequip(target, sessionUuid, itemUuid, requestedSlot);
    Mmo::Server::unequipCharacterItem(target, {
      .sessionUuid = sessionUuid,
      .equipmentSlot = slot,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return directApplied();
  }

  if(packet.kind == Mmo::SemanticActionKind::DropCharacterItem) {
    const auto amount = optionalJsonI64(payload, "amount", 1);
    const auto entityKey = optionalJsonString(payload, "world_item_entity_key",
                           optionalJsonString(payload, "engine_world_item_key",
                           optionalJsonString(payload, "dropped_world_item_key",
                           optionalJsonString(payload, "target_key", packet.targetKey))));
    const auto validation = Mmo::Server::InventoryAuthority::validateDrop({
      .entityKey = entityKey,
      .itemSymbol = itemSymbolFromPayload(payload),
      .amount = amount,
      .bagIndex = -1,
    });
    if(!validation.accepted)
      return {true, false, false, validation.reason};
    Mmo::Server::dropCharacterItem(target, {
      .sessionUuid = sessionUuid,
      .itemUuid = resolveCharacterItemUuid(target, sessionUuid, packet),
      .amount = amount,
      .entityKey = entityKey,
      .posX = optionalJsonPositionDouble(payload, "x", "pos_x", "world_pos_x", "actor_pos_x"),
      .posY = optionalJsonPositionDouble(payload, "y", "pos_y", "world_pos_y", "actor_pos_y"),
      .posZ = optionalJsonPositionDouble(payload, "z", "pos_z", "world_pos_z", "actor_pos_z"),
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return directApplied();
  }

  return {false, true, false, "unhandled"};
}


