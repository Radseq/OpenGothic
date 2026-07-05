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
