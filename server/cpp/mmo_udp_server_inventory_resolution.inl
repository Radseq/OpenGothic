// Internal implementation partition for mmo_udp_server.cpp.
// Kept include-based during the monolith split to preserve behavior.

[[nodiscard]] int nextBagIndex(const MySqlTarget& target, std::string_view sessionUuid) {
  std::string sql;
  sql += "SELECT COALESCE(MAX(ci.bag_index), -1) + 1 ";
  sql += "FROM character_inventory ci ";
  sql += "JOIN server_sessions ss ON ss.character_id=ci.character_id ";
  sql += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1);";
  auto value = parseInt(mysqlSingleField(target, sql));
  return value.value_or(0);
}

constexpr std::int64_t InvalidGothicPersistentId = 4294967295LL;

[[nodiscard]] bool isUsablePersistentId(std::int64_t value) noexcept {
  return value >= 0 && value != InvalidGothicPersistentId;
}

[[nodiscard]] std::int64_t itemSymbolFromPayload(std::string_view payload) noexcept {
  const auto explicitSymbol = optionalJsonI64(payload, "item_symbol",
                              optionalJsonI64(payload, "inventory_item_symbol",
                              optionalJsonI64(payload, "item_template_symbol", -1)));
  if(explicitSymbol >= 0)
    return explicitSymbol;

  const auto key = optionalJsonString(payload, "item_template_key");
  constexpr std::string_view Prefix = "item-template:";
  if(!startsWith(key, Prefix))
    return -1;
  return parseI64(std::string_view(key).substr(Prefix.size())).value_or(-1);
}

[[nodiscard]] std::int64_t itemPersistentIdFromPayload(std::string_view payload) noexcept {
  const auto value = optionalJsonI64(payload, "item_instance_persistent_id",
                     optionalJsonI64(payload, "source_item_persistent_id",
                     optionalJsonI64(payload, "source_world_item_persistent_id",
                     optionalJsonI64(payload, "item_persistent_id", -1))));
  return isUsablePersistentId(value) ? value : -1;
}

[[nodiscard]] std::uint32_t u32FromMysqlField(const std::string& value) noexcept {
  const auto parsed = parseI64(value);
  if(!parsed || *parsed < 0 || *parsed > std::numeric_limits<std::uint32_t>::max())
    return 0;
  return static_cast<std::uint32_t>(*parsed);
}

[[nodiscard]] std::int64_t i64FromMysqlField(const std::string& value) noexcept {
  return parseI64(value).value_or(0);
}

[[nodiscard]] std::string jsonI64Expr(std::string_view object,
                                      std::string_view path,
                                      std::string_view fallback = "0") {
  std::string out;
  out += "COALESCE(NULLIF(JSON_UNQUOTE(JSON_EXTRACT(";
  out += object;
  out += ",";
  out += sqlLiteral(path);
  out += ")),''),";
  out += sqlLiteral(fallback);
  out += ")";
  return out;
}

[[nodiscard]] std::string jsonI64AnyExpr(std::string_view object,
                                         std::initializer_list<std::string_view> paths,
                                         std::string_view fallback = "0") {
  std::string out = "COALESCE(";
  bool first = true;
  for(const auto path : paths) {
    if(!first)
      out += ",";
    first = false;
    out += "NULLIF(JSON_UNQUOTE(JSON_EXTRACT(";
    out += object;
    out += ",";
    out += sqlLiteral(path);
    out += ")), '')";
  }
  if(!first)
    out += ",";
  out += sqlLiteral(fallback);
  out += ")";
  return out;
}

[[nodiscard]] Mmo::Server::InventoryAuthority::CharacterUseStats readCharacterUseStats(
    const MySqlTarget& target,
    std::string_view sessionUuid) {
  std::string query;
  query += "SELECT cs.health_current,cs.health_max,cs.mana_current,cs.mana_max,cs.strength,cs.dexterity,";
  query += jsonI64AnyExpr("cs.raw_stats", {"$.attributes[6]", "$.attribute[6]", "$.regenerate_hp", "$.regenerateHp"});
  query += ",";
  query += jsonI64AnyExpr("cs.raw_stats", {"$.attributes[7]", "$.attribute[7]", "$.regenerate_mana", "$.regenerateMana"});
  query += ",";
  query += jsonI64AnyExpr("cs.raw_stats", {"$.talent_skill[7]", "$.talentSkills[7]", "$.talents.mage.skill", "$.mage_skill", "$.mageCircle"});
  query += " FROM character_stats cs ";
  query += "JOIN server_sessions ss ON ss.character_id=cs.character_id ";
  query += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) LIMIT 1;";

  const auto parts = splitMysqlLastRow(runMysql(target, query));
  if(parts.size() < 9)
    throw std::runtime_error("character use stats could not be read");

  return {
    .healthCurrent = i64FromMysqlField(parts[0]),
    .healthMax = i64FromMysqlField(parts[1]),
    .manaCurrent = i64FromMysqlField(parts[2]),
    .manaMax = i64FromMysqlField(parts[3]),
    .strength = i64FromMysqlField(parts[4]),
    .dexterity = i64FromMysqlField(parts[5]),
    .regenerateHp = i64FromMysqlField(parts[6]),
    .regenerateMana = i64FromMysqlField(parts[7]),
    .mageCircle = i64FromMysqlField(parts[8]),
  };
}

[[nodiscard]] Mmo::Server::InventoryAuthority::ItemUseRequirements readCharacterItemUseRequirements(
    const MySqlTarget& target,
    std::string_view sessionUuid,
    std::string_view itemUuid) {
  std::string query;
  query += "SELECT ";
  for(std::size_t i = 0; i < Mmo::Server::InventoryAuthority::MaxItemUseConditions; ++i) {
    if(i != 0)
      query += ",";
    query += jsonI64AnyExpr("cit.raw_payload", {
      "$.cond_atr[" + std::to_string(i) + "]",
      "$.condition_attributes[" + std::to_string(i) + "]",
      "$.conditions[" + std::to_string(i) + "].attribute"
    });
    query += ",";
    query += jsonI64AnyExpr("cit.raw_payload", {
      "$.cond_value[" + std::to_string(i) + "]",
      "$.condition_values[" + std::to_string(i) + "]",
      "$.conditions[" + std::to_string(i) + "].value"
    });
  }
  query += ",";
  query += jsonI64AnyExpr("cit.raw_payload", {"$.mag_circle", "$.magic_circle", "$.magCircle"});
  query += " FROM item_instances ii ";
  query += "JOIN content_item_templates cit ON cit.item_template_id=ii.item_template_id ";
  query += "JOIN character_inventory ci ON ci.item_instance_id=ii.item_instance_id ";
  query += "JOIN server_sessions ss ON ss.character_id=ci.character_id ";
  query += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) ";
  query += "AND ii.item_instance_id=UUID_TO_BIN(" + sqlLiteral(itemUuid) + ",1) ";
  query += "AND ii.owner_type='character' AND ii.owner_id=ss.character_id ";
  query += "AND ii.lifecycle_state='active' LIMIT 1;";

  const auto parts = splitMysqlLastRow(runMysql(target, query));
  if(parts.size() < Mmo::Server::InventoryAuthority::MaxItemUseConditions * 2 + 1)
    throw std::runtime_error("character item use requirements could not be read");

  Mmo::Server::InventoryAuthority::ItemUseRequirements out;
  for(std::size_t i = 0; i < Mmo::Server::InventoryAuthority::MaxItemUseConditions; ++i) {
    out.conditionAttributes[i] = i64FromMysqlField(parts[i * 2]);
    out.conditionValues[i] = i64FromMysqlField(parts[i * 2 + 1]);
  }
  out.magicCircle = i64FromMysqlField(parts[Mmo::Server::InventoryAuthority::MaxItemUseConditions * 2]);
  return out;
}

void validateCharacterItemUseRequirements(const MySqlTarget& target,
                                          std::string_view sessionUuid,
                                          std::string_view itemUuid) {
  const auto stats = readCharacterUseStats(target, sessionUuid);
  const auto requirements = readCharacterItemUseRequirements(target, sessionUuid, itemUuid);
  const auto validation = Mmo::Server::InventoryAuthority::validateItemUseRequirements(stats, requirements);
  if(!validation.accepted) {
    std::string reason = validation.reason;
    reason += ": required_attribute=" + std::to_string(validation.requiredAttribute);
    reason += " current=" + std::to_string(validation.currentValue);
    reason += " required=" + std::to_string(validation.requiredValue);
    throw std::runtime_error(reason);
  }
}

[[nodiscard]] Mmo::Server::InventoryAuthority::ItemEquipInput readCharacterItemEquipInput(
    const MySqlTarget& target,
    std::string_view sessionUuid,
    std::string_view itemUuid,
    std::string_view slot) {
  std::string query;
  query += "SELECT COALESCE(cit.classification,''),";
  query += "COALESCE(JSON_UNQUOTE(JSON_EXTRACT(cit.raw_payload,'$.main_flag')),JSON_UNQUOTE(JSON_EXTRACT(cit.flags,'$.main_flag')),''),";
  query += "COALESCE(JSON_UNQUOTE(JSON_EXTRACT(cit.raw_payload,'$.flags')),JSON_UNQUOTE(JSON_EXTRACT(cit.raw_payload,'$.item_flags')),JSON_UNQUOTE(JSON_EXTRACT(cit.flags,'$.flags')),JSON_UNQUOTE(JSON_EXTRACT(cit.flags,'$.item_flags')),'') ";
  query += "FROM item_instances ii ";
  query += "JOIN content_item_templates cit ON cit.item_template_id=ii.item_template_id ";
  query += "JOIN character_inventory ci ON ci.item_instance_id=ii.item_instance_id ";
  query += "JOIN server_sessions ss ON ss.character_id=ci.character_id ";
  query += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) ";
  query += "AND ii.item_instance_id=UUID_TO_BIN(" + sqlLiteral(itemUuid) + ",1) ";
  query += "AND ii.owner_type='character' AND ii.owner_id=ss.character_id ";
  query += "AND ii.lifecycle_state='active' LIMIT 1;";

  auto parts = splitMysqlLastRow(runMysql(target, query));
  if(parts.size() < 3)
    throw std::runtime_error("character item template semantics could not be read");

  const auto mainFlag = u32FromMysqlField(parts[1]);
  const auto itemFlags = u32FromMysqlField(parts[2]);
  return {
    .slot = slot,
    .classification = parts[0],
    .mainFlag = mainFlag,
    .itemFlags = itemFlags,
    .hasGothicFlags = !parts[1].empty() || !parts[2].empty(),
  };
}

[[nodiscard]] std::string chooseRingSlotForEquip(const MySqlTarget& target,
                                                 std::string_view sessionUuid,
                                                 std::string_view itemUuid) {
  std::string query;
  query += "SELECT ";
  query += "COALESCE(MAX(CASE WHEN ce.equipment_slot='ring_left' THEN BIN_TO_UUID(ce.item_instance_id,1) ELSE '' END),''),";
  query += "COALESCE(MAX(CASE WHEN ce.equipment_slot='ring_right' THEN BIN_TO_UUID(ce.item_instance_id,1) ELSE '' END),'') ";
  query += "FROM character_equipment ce ";
  query += "JOIN server_sessions ss ON ss.character_id=ce.character_id ";
  query += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) ";
  query += "AND ce.equipment_slot IN ('ring_left','ring_right');";

  const auto parts = splitMysqlLastRow(runMysql(target, query));
  const std::string left = parts.size() > 0 ? parts[0] : "";
  const std::string right = parts.size() > 1 ? parts[1] : "";
  if(left.empty() || left == itemUuid)
    return std::string(Mmo::Server::InventoryAuthority::SlotRingLeft);
  if(right.empty() || right == itemUuid)
    return std::string(Mmo::Server::InventoryAuthority::SlotRingRight);
  throw std::runtime_error("equipment_ring_slots_full");
}

[[nodiscard]] std::string inferEquipmentSlotFromItem(const MySqlTarget& target,
                                                     std::string_view sessionUuid,
                                                     std::string_view itemUuid,
                                                     const Mmo::Server::InventoryAuthority::ItemEquipInput& item) {
  namespace Inv = Mmo::Server::InventoryAuthority;
  if(item.hasGothicFlags) {
    if(Inv::hasFlag(item.itemFlags, Inv::ItmShield))
      return std::string(Inv::SlotShield);
    if(Inv::hasFlag(item.mainFlag, Inv::ItmCatMeleeWeapon))
      return std::string(Inv::SlotWeaponMelee);
    if(Inv::hasFlag(item.mainFlag, Inv::ItmCatRangedWeapon))
      return std::string(Inv::SlotWeaponRanged);
    if(Inv::hasFlag(item.mainFlag, Inv::ItmCatRune))
      return std::string(Inv::SlotRune);
    if(Inv::hasFlag(item.mainFlag, Inv::ItmCatArmor))
      return std::string(Inv::SlotArmor);
    if(Inv::hasFlag(item.itemFlags, Inv::ItmBelt))
      return std::string(Inv::SlotBelt);
    if(Inv::hasFlag(item.itemFlags, Inv::ItmAmulet))
      return std::string(Inv::SlotAmulet);
    if(Inv::hasFlag(item.itemFlags, Inv::ItmRing))
      return chooseRingSlotForEquip(target, sessionUuid, itemUuid);
    if(Inv::hasFlag(item.itemFlags, Inv::ItmTorch))
      return std::string(Inv::SlotTorch);
  }

  if(item.classification == "armor")
    return std::string(Inv::SlotArmor);
  if(item.classification == "rune" || item.classification == "scroll")
    return std::string(Inv::SlotRune);
  throw std::runtime_error("equipment_slot_could_not_be_inferred");
}

[[nodiscard]] std::string resolveEquipmentSlotForEquip(const MySqlTarget& target,
                                                       std::string_view sessionUuid,
                                                       std::string_view itemUuid,
                                                       std::string_view requestedSlot) {
  namespace Inv = Mmo::Server::InventoryAuthority;
  if(requestedSlot != Inv::SlotUnknown) {
    const auto validation = Inv::validateEquipItem(readCharacterItemEquipInput(target, sessionUuid, itemUuid, requestedSlot));
    if(!validation.accepted)
      throw std::runtime_error(validation.reason);
    validateCharacterItemUseRequirements(target, sessionUuid, itemUuid);
    return std::string(requestedSlot);
  }

  auto item = readCharacterItemEquipInput(target, sessionUuid, itemUuid, requestedSlot);
  const auto inferredSlot = inferEquipmentSlotFromItem(target, sessionUuid, itemUuid, item);
  item.slot = inferredSlot;
  const auto validation = Inv::validateEquipItem(item);
  if(!validation.accepted)
    throw std::runtime_error(validation.reason);
  validateCharacterItemUseRequirements(target, sessionUuid, itemUuid);
  return inferredSlot;
}

[[nodiscard]] std::string resolveEquipmentSlotForUnequip(const MySqlTarget& target,
                                                         std::string_view sessionUuid,
                                                         std::string_view itemUuid,
                                                         std::string_view requestedSlot) {
  namespace Inv = Mmo::Server::InventoryAuthority;
  if(requestedSlot != Inv::SlotUnknown)
    return std::string(requestedSlot);

  std::string query;
  query += "SELECT ce.equipment_slot ";
  query += "FROM character_equipment ce ";
  query += "JOIN server_sessions ss ON ss.character_id=ce.character_id ";
  query += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) ";
  query += "AND ce.item_instance_id=UUID_TO_BIN(" + sqlLiteral(itemUuid) + ",1) LIMIT 1;";
  auto slot = mysqlSingleField(target, query);
  if(slot.empty())
    throw std::runtime_error("equipment_slot_could_not_be_resolved_for_unequip");
  return slot;
}

[[nodiscard]] std::string resolveNpcInventoryItemUuid(const MySqlTarget& target,
                                                     std::string_view sessionUuid,
                                                     std::string_view sourceNpcKey,
                                                     const Mmo::Net::ClientActionPacket& packet) {
  const std::string_view payload = packet.payloadJson;
  if(auto id = jsonStringField(payload, "item_instance_id"); id && !id->empty())
    return *id;
  if(auto id = jsonStringField(payload, "item_instance_uuid"); id && !id->empty())
    return *id;

  const auto symbol = itemSymbolFromPayload(payload);
  const auto pid = itemPersistentIdFromPayload(payload);
  if(sourceNpcKey.empty())
    throw std::runtime_error("source_entity_key is required to resolve world inventory item");
  if(symbol < 0)
    throw std::runtime_error("item_symbol is required to resolve world inventory item");

  std::string query;
  query += "SELECT BIN_TO_UUID(ii.item_instance_id,1) ";
  query += "FROM world_inventory wi ";
  query += "JOIN item_instances ii ON ii.item_instance_id=wi.item_instance_id ";
  query += "JOIN content_item_templates it ON it.item_template_id=ii.item_template_id ";
  query += "JOIN server_sessions ss ON ss.world_instance_id=wi.world_instance_id ";
  query += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) ";
  query += "AND wi.owner_entity_key=" + sqlLiteral(sourceNpcKey) + " ";
  query += "AND ii.lifecycle_state='active' AND it.symbol_index=" + std::to_string(symbol) + " ";
  if(isUsablePersistentId(pid)) {
    query += "AND (JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.persistent_id'))=" + sqlLiteral(std::to_string(pid));
    query += " OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.source_item_persistent_id'))=" + sqlLiteral(std::to_string(pid));
    query += " OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.item_persistent_id'))=" + sqlLiteral(std::to_string(pid));
    query += " OR ii.item_instance_key LIKE " + sqlLiteral("%:" + std::to_string(symbol) + ":" + std::to_string(pid) + "%");
    query += " OR ii.item_instance_key LIKE " + sqlLiteral("%:" + std::to_string(pid) + ":" + std::to_string(symbol) + "%") + ") ";
  }
  query += "ORDER BY wi.amount DESC, ii.item_instance_key ASC LIMIT 1;";

  auto out = mysqlSingleField(target, query);
  if(out.empty())
    throw std::runtime_error("world inventory item could not be resolved: owner=" + std::string(sourceNpcKey) +
                             " pid=" + std::to_string(pid) + " sym=" + std::to_string(symbol));
  return out;
}

[[nodiscard]] std::string materializeObservedNpcLootItem(const MySqlTarget& target,
                                                        std::string_view sessionUuid,
                                                        std::string_view sourceNpcKey,
                                                        const Mmo::Net::ClientActionPacket& packet,
                                                        std::string_view dbPayload) {
  const std::string_view payload = packet.payloadJson;
  const auto symbol = itemSymbolFromPayload(payload);
  if(sourceNpcKey.empty())
    throw std::runtime_error("observed NPC loot cannot be materialized without source entity key");
  if(symbol < 0)
    throw std::runtime_error("observed NPC loot cannot be materialized without item symbol");

  const auto amount = std::max<std::int64_t>(1, optionalJsonI64(payload, "amount", 1));
  const auto pid = itemPersistentIdFromPayload(payload);
  const auto tick = packetServerTick(packet);
  const std::string sourceNpcSql = sqlLiteral(sourceNpcKey);
  const std::string idem = packet.idempotencyKey + ":observed-npc-loot";

  std::string sql;
  sql += "SET @realm_id=NULL; SET @world_id=NULL; SET @character_id=NULL; SET @content_revision_id=NULL;";
  sql += "SET @template_id=NULL; SET @item_id=NULL; SET @item_key=NULL; SET @event_id=NULL;";
  sql += "SELECT ss.realm_id,ss.world_instance_id,ss.character_id,rr.active_content_revision_id ";
  sql += "INTO @realm_id,@world_id,@character_id,@content_revision_id ";
  sql += "FROM server_sessions ss JOIN realm_realms rr ON rr.realm_id=ss.realm_id ";
  sql += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) AND ss.lifecycle_state='active' LIMIT 1;";
  sql += "SELECT cit.item_template_id INTO @template_id FROM content_item_templates cit ";
  sql += "WHERE cit.content_revision_id=@content_revision_id AND cit.symbol_index=" + std::to_string(symbol) + " ";
  sql += "ORDER BY cit.item_template_key LIMIT 1;";
  sql += "SET @item_key=LEFT(CONCAT('observed-corpse-loot:',SHA2(" + sqlLiteral(idem) + ",256)),191);";
  sql += "SELECT ii.item_instance_id INTO @item_id FROM item_instances ii ";
  sql += "WHERE ii.realm_id=@realm_id AND ii.item_instance_key=@item_key LIMIT 1;";
  sql += "INSERT INTO item_instances(";
  sql += "item_instance_id,realm_id,item_template_id,item_instance_key,owner_type,owner_id,quantity,bind_state,lifecycle_state,raw_payload";
  sql += ") SELECT UUID_TO_BIN(UUID(),1),@realm_id,@template_id,@item_key,'world_entity',NULL,";
  sql += std::to_string(amount) + ",'unbound','active',JSON_OBJECT(";
  sql += "'observed_corpse_loot',true,";
  sql += "'observed_from','mmo_udp_server_cpp',";
  sql += "'source_entity_key'," + sourceNpcSql + ",";
  sql += "'item_symbol'," + std::to_string(symbol) + ",";
  sql += "'persistent_id'," + std::to_string(pid) + ",";
  sql += "'source_item_persistent_id'," + std::to_string(pid) + ",";
  sql += "'amount'," + std::to_string(amount) + ",";
  sql += "'observed_at_tick'," + std::to_string(tick) + ",";
  sql += "'metadata'," + sqlJson(dbPayload);
  sql += ") WHERE @realm_id IS NOT NULL AND @template_id IS NOT NULL AND @item_id IS NULL;";
  sql += "SELECT ii.item_instance_id INTO @item_id FROM item_instances ii ";
  sql += "WHERE ii.realm_id=@realm_id AND ii.item_instance_key=@item_key LIMIT 1;";
  sql += "INSERT INTO world_inventory(world_instance_id,owner_entity_key,item_instance_id,amount,source_amount,source_iterator_count) ";
  sql += "SELECT @world_id," + sourceNpcSql + ",@item_id," + std::to_string(amount) + ",";
  sql += std::to_string(amount) + "," + std::to_string(amount) + " ";
  sql += "WHERE @world_id IS NOT NULL AND @item_id IS NOT NULL ";
  sql += "ON DUPLICATE KEY UPDATE ";
  sql += "amount=GREATEST(world_inventory.amount,VALUES(amount)),";
  sql += "source_amount=GREATEST(COALESCE(world_inventory.source_amount,0),VALUES(source_amount)),";
  sql += "source_iterator_count=GREATEST(COALESCE(world_inventory.source_iterator_count,0),VALUES(source_iterator_count)),";
  sql += "updated_at=CURRENT_TIMESTAMP(6);";
  sql += "CALL mmo_append_world_event(@realm_id,@world_id,@character_id,";
  sql += "'world_npc_loot_observed','inventory'," + std::to_string(tick) + ",";
  sql += sourceNpcSql + ",@item_key,";
  sql += "JSON_OBJECT('source_entity_key'," + sourceNpcSql;
  sql += ",'item_instance_id',BIN_TO_UUID(@item_id,1)";
  sql += ",'item_symbol'," + std::to_string(symbol);
  sql += ",'persistent_id'," + std::to_string(pid);
  sql += ",'amount'," + std::to_string(amount);
  sql += ",'metadata'," + sqlJson(dbPayload) + "),";
  sql += sqlLiteral(idem) + ",'server',NULL,NULL,@event_id);";
  sql += "SELECT BIN_TO_UUID(@item_id,1);";

  auto out = mysqlSingleField(target, sql);
  if(out.empty())
    throw std::runtime_error("observed NPC loot materialization failed: owner=" + std::string(sourceNpcKey) +
                             " symbol=" + std::to_string(symbol));

  std::cerr << "[observed_npc_loot_materialized] owner=" << sourceNpcKey
            << " symbol=" << symbol
            << " amount=" << amount
            << " item=" << out
            << "\n";
  return out;
}

[[nodiscard]] std::string resolveCharacterItemUuid(const MySqlTarget& target,
                                                   std::string_view sessionUuid,
                                                   const Mmo::Net::ClientActionPacket& packet) {
  const std::string_view payload = packet.payloadJson;
  if(auto id = jsonStringField(payload, "item_instance_id"); id && !id->empty())
    return *id;
  if(auto id = jsonStringField(payload, "item_instance_uuid"); id && !id->empty())
    return *id;

  const auto symbol = itemSymbolFromPayload(payload);
  const auto pid = itemPersistentIdFromPayload(payload);
  const auto slot = normalizedEquipmentSlot(payload);
  if(symbol < 0)
    throw std::runtime_error("item_symbol or item_template_key is required to resolve character item");

  std::string query;
  query += "SELECT BIN_TO_UUID(ii.item_instance_id,1) ";
  query += "FROM item_instances ii ";
  query += "JOIN character_inventory ci ON ci.item_instance_id=ii.item_instance_id ";
  query += "JOIN content_item_templates it ON it.item_template_id=ii.item_template_id ";
  query += "JOIN server_sessions ss ON ss.character_id=ci.character_id ";
  query += "LEFT JOIN character_equipment ce ON ce.character_id=ci.character_id AND ce.item_instance_id=ii.item_instance_id ";
  query += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) ";
  query += "AND ii.owner_type='character' AND ii.lifecycle_state='active' ";
  query += "AND it.symbol_index=" + std::to_string(symbol) + " ";
  if(isUsablePersistentId(pid)) {
    query += "AND (JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.persistent_id'))=" + sqlLiteral(std::to_string(pid));
    query += " OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.source_item_persistent_id'))=" + sqlLiteral(std::to_string(pid));
    query += " OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.source_world_item_persistent_id'))=" + sqlLiteral(std::to_string(pid));
    query += " OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.item_persistent_id'))=" + sqlLiteral(std::to_string(pid));
    query += " OR ii.item_instance_key LIKE " + sqlLiteral("%:" + std::to_string(symbol) + ":" + std::to_string(pid) + ":%");
    query += " OR ii.item_instance_key LIKE " + sqlLiteral("%:" + std::to_string(pid) + ":" + std::to_string(symbol) + ":%") + ") ";
  }
  query += "ORDER BY CASE ";
  query += "WHEN ce.equipment_slot=" + sqlLiteral(slot) + " THEN 0 ";
  query += "WHEN ce.equipment_slot IS NOT NULL THEN 1 ";
  query += "ELSE 2 END, COALESCE(ci.bag_index,999999), ci.amount DESC, ii.item_instance_key ASC LIMIT 1;";

  auto out = mysqlSingleField(target, query);
  if(out.empty())
    throw std::runtime_error("character item could not be resolved: symbol=" + std::to_string(symbol) +
                             " pid=" + std::to_string(pid) + " slot=" + slot);
  return out;
}
