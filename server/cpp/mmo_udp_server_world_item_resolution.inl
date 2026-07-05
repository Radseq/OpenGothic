// Internal implementation partition for mmo_udp_server.cpp.
// Owns world-item identity resolution and observed item materialization.

[[nodiscard]] std::string resolveWorldItemEntityKey(const MySqlTarget& target,
                                                    std::string_view sessionUuid,
                                                    const Mmo::Net::ClientActionPacket& packet) {
  const std::string_view payload = packet.payloadJson;
  auto identity = parseWorldItemIdentity(optionalJsonString(payload, "world_item_entity_key",
                                      optionalJsonString(payload, "engine_world_item_key",
                                      optionalJsonString(payload, "target_key", packet.targetKey))));
  if(identity.persistentId < 0)
    identity.persistentId = optionalJsonI64(payload, "source_world_item_persistent_id",
                            optionalJsonI64(payload, "item_persistent_id", -1));
  if(identity.symbol < 0)
    identity.symbol = optionalJsonI64(payload, "item_symbol",
                      optionalJsonI64(payload, "inventory_item_symbol",
                      optionalJsonI64(payload, "item_template_symbol", -1)));
  if(identity.world.empty())
    identity.world = optionalJsonString(payload, "world");

  const bool hasStableIdentity = !identity.world.empty() && identity.persistentId >= 0 && identity.symbol >= 0;
  const bool hasPidSym = identity.persistentId >= 0 && identity.symbol >= 0;
  const std::string dbLike = hasStableIdentity ?
    Mmo::Server::Identity::canonicalWorldItemDbLike(identity.world, identity.persistentId, identity.symbol) : std::string();
  const std::string hookKey = hasStableIdentity ?
    Mmo::Server::Identity::canonicalWorldItemHookKey(identity.world, identity.persistentId, identity.symbol) : std::string();
  const std::string anyHookLike = hasPidSym ?
    Mmo::Server::Identity::anyWorldItemHookLike(identity.persistentId, identity.symbol) : std::string();

  std::string where = "wes.entity_key=" + sqlLiteral(identity.exact);
  if(!dbLike.empty()) {
    where += " OR wes.entity_key LIKE ";
    where += sqlLiteral(dbLike);
  }
  if(!hookKey.empty())
    where += " OR wes.entity_key=" + sqlLiteral(hookKey);
  if(!anyHookLike.empty())
    where += " OR wes.entity_key LIKE " + sqlLiteral(anyHookLike);
  if(identity.persistentId >= 0) {
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED)=";
    where += std::to_string(identity.persistentId);
    where += " OR JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_spawn_key')) LIKE ";
    where += sqlLiteral("%:" + std::to_string(identity.persistentId) + ":%");
    where += " OR wes.entity_key LIKE ";
    where += sqlLiteral("%:pid:" + std::to_string(identity.persistentId) + ":%");
  }
  if(!identity.exact.empty()) {
    where += " OR JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_spawn_key'))=";
    where += sqlLiteral(identity.exact);
    where += " OR JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.entity_key'))=";
    where += sqlLiteral(identity.exact);
  }

  std::string query;
  query += "SELECT wes.entity_key FROM world_entity_state wes ";
  query += "JOIN server_sessions ss ON ss.world_instance_id=wes.world_instance_id ";
  query += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) ";
  query += "AND wes.entity_kind='item' AND wes.lifecycle_state='active' AND (";
  query += where;
  query += ")";
  if(identity.symbol >= 0) {
    query += " AND (CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_template_symbol')) AS SIGNED)=";
    query += std::to_string(identity.symbol);
    query += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')) AS SIGNED)=";
    query += std::to_string(identity.symbol);
    query += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_symbol')) AS SIGNED)=";
    query += std::to_string(identity.symbol);
    query += " OR wes.entity_key LIKE ";
    query += sqlLiteral("%:sym:" + std::to_string(identity.symbol) + "%");
    query += " OR wes.entity_key LIKE ";
    query += sqlLiteral("%:" + std::to_string(identity.symbol) + ":%");
    query += ")";
  }
  query += " ORDER BY CASE WHEN wes.entity_key=" + sqlLiteral(identity.exact) + " THEN 0 ";
  if(!hookKey.empty())
    query += "WHEN wes.entity_key=" + sqlLiteral(hookKey) + " THEN 1 ";
  if(!dbLike.empty())
    query += "WHEN wes.entity_key LIKE " + sqlLiteral(dbLike) + " THEN 2 ";
  if(!anyHookLike.empty())
    query += "WHEN wes.entity_key LIKE " + sqlLiteral(anyHookLike) + " THEN 3 ";
  query += "ELSE 4 END, wes.updated_at DESC LIMIT 1;";
  auto out = mysqlSingleField(target, query);
  if(out.empty())
    throw std::runtime_error("world item could not be resolved: key=" + identity.exact +
                             " world=" + identity.world +
                             " pid=" + std::to_string(identity.persistentId) +
                             " sym=" + std::to_string(identity.symbol));
  return out;
}

[[nodiscard]] std::optional<JsonVec3> worldItemPositionFromPayload(std::string_view payload) {
  auto pos = optionalJsonVec3(payload, "item_position");
  if(!pos)
    pos = optionalJsonVec3(payload, "world_item_position");
  if(!pos)
    pos = optionalJsonVec3(payload, "target_position");
  if(!pos)
    pos = optionalJsonVec3(payload, "actor_position");
  return pos;
}

[[nodiscard]] std::string materializeObservedWorldItem(const MySqlTarget& target,
                                                      std::string_view sessionUuid,
                                                      const Mmo::Net::ClientActionPacket& packet,
                                                      std::string_view dbPayload) {
  const std::string_view payload = packet.payloadJson;
  auto identity = parseWorldItemIdentity(optionalJsonString(payload, "world_item_entity_key",
                                      optionalJsonString(payload, "engine_world_item_key",
                                      optionalJsonString(payload, "target_key", packet.targetKey))));
  if(identity.persistentId < 0)
    identity.persistentId = optionalJsonI64(payload, "source_world_item_persistent_id",
                            optionalJsonI64(payload, "world_item_persistent_id",
                            optionalJsonI64(payload, "item_persistent_id", -1)));
  if(identity.symbol < 0)
    identity.symbol = optionalJsonI64(payload, "item_symbol",
                      optionalJsonI64(payload, "inventory_item_symbol",
                      optionalJsonI64(payload, "item_template_symbol", -1)));
  if(identity.symbol < 0) {
    const auto key = optionalJsonString(payload, "item_template_key");
    constexpr std::string_view Prefix = "item-template:";
    if(startsWith(key, Prefix))
      identity.symbol = parseI64(std::string_view(key).substr(Prefix.size())).value_or(-1);
  }
  if(identity.world.empty())
    identity.world = optionalJsonString(payload, "world");

  const auto entityKey = (!identity.world.empty() && identity.persistentId >= 0 && identity.symbol >= 0) ?
    Mmo::Server::Identity::canonicalWorldItemHookKey(identity.world, identity.persistentId, identity.symbol) :
    std::string(identity.exact);
  if(entityKey.empty() || identity.symbol < 0)
    throw std::runtime_error("observed world item cannot be materialized without stable key/symbol");

  const auto amount = std::max<std::int64_t>(1, optionalJsonI64(payload, "amount", 1));
  const auto tick = packetServerTick(packet);
  const auto pos = worldItemPositionFromPayload(payload);
  const std::string entitySql = sqlLiteral(entityKey);
  const std::string idem = packet.idempotencyKey + ":observed-world-item";

  std::string sql;
  sql += "SET @realm_id=NULL; SET @world_id=NULL; SET @character_id=NULL; SET @content_revision_id=NULL;";
  sql += "SET @template_id=NULL; SET @item_id=NULL; SET @item_key=NULL; SET @event_id=NULL;";
  sql += "SELECT ss.realm_id,ss.world_instance_id,ss.character_id,rr.active_content_revision_id ";
  sql += "INTO @realm_id,@world_id,@character_id,@content_revision_id ";
  sql += "FROM server_sessions ss JOIN realm_realms rr ON rr.realm_id=ss.realm_id ";
  sql += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) AND ss.lifecycle_state='active' LIMIT 1;";
  sql += "SELECT cit.item_template_id INTO @template_id FROM content_item_templates cit ";
  sql += "WHERE cit.content_revision_id=@content_revision_id AND cit.symbol_index=" + std::to_string(identity.symbol) + " ";
  sql += "ORDER BY cit.item_template_key LIMIT 1;";
  sql += "SET @item_key=LEFT(CONCAT('observed-world-item:',SHA2(" + sqlLiteral(idem) + ",256)),191);";
  sql += "SELECT ii.item_instance_id INTO @item_id FROM item_instances ii ";
  sql += "WHERE ii.realm_id=@realm_id AND ii.item_instance_key=@item_key LIMIT 1;";
  sql += "INSERT INTO item_instances(";
  sql += "item_instance_id,realm_id,item_template_id,item_instance_key,owner_type,owner_id,quantity,bind_state,lifecycle_state,raw_payload";
  sql += ") SELECT UUID_TO_BIN(UUID(),1),@realm_id,@template_id,@item_key,'world_entity',NULL,";
  sql += std::to_string(amount) + ",'unbound','active',JSON_OBJECT(";
  sql += "'observed_world_item',true,";
  sql += "'observed_from','mmo_udp_server_cpp',";
  sql += "'entity_key'," + entitySql + ",";
  sql += "'item_spawn_key'," + entitySql + ",";
  sql += "'persistent_id'," + std::to_string(identity.persistentId) + ",";
  sql += "'source_world_item_persistent_id'," + std::to_string(identity.persistentId) + ",";
  sql += "'item_symbol'," + std::to_string(identity.symbol) + ",";
  sql += "'symbol_index'," + std::to_string(identity.symbol) + ",";
  sql += "'item_template_symbol'," + std::to_string(identity.symbol) + ",";
  sql += "'amount'," + std::to_string(amount) + ",";
  sql += "'observed_at_tick'," + std::to_string(tick) + ",";
  sql += "'metadata'," + sqlJson(dbPayload);
  sql += ") WHERE @realm_id IS NOT NULL AND @template_id IS NOT NULL AND @item_id IS NULL;";
  sql += "SELECT ii.item_instance_id INTO @item_id FROM item_instances ii ";
  sql += "WHERE ii.realm_id=@realm_id AND ii.item_instance_key=@item_key LIMIT 1;";
  sql += "INSERT INTO world_entity_state(";
  sql += "world_instance_id,entity_key,entity_kind,lifecycle_state,pos_x,pos_y,pos_z,state_json,row_version";
  sql += ") SELECT @world_id," + entitySql + ",'item','active',";
  sql += (pos ? std::to_string(pos->x) : "NULL");
  sql += ",";
  sql += (pos ? std::to_string(pos->y) : "NULL");
  sql += ",";
  sql += (pos ? std::to_string(pos->z) : "NULL");
  sql += ",JSON_OBJECT(";
  sql += "'exists_in_world',true,";
  sql += "'observed_world_item',true,";
  sql += "'item_instance_id',BIN_TO_UUID(@item_id,1),";
  sql += "'entity_key'," + entitySql + ",";
  sql += "'item_spawn_key'," + entitySql + ",";
  sql += "'persistent_id'," + std::to_string(identity.persistentId) + ",";
  sql += "'item_symbol'," + std::to_string(identity.symbol) + ",";
  sql += "'symbol_index'," + std::to_string(identity.symbol) + ",";
  sql += "'amount'," + std::to_string(amount) + ",";
  sql += "'observed_at_tick'," + std::to_string(tick);
  sql += "),1 WHERE @world_id IS NOT NULL AND @item_id IS NOT NULL ";
  sql += "ON DUPLICATE KEY UPDATE ";
  sql += "entity_kind='item',lifecycle_state='active',";
  sql += "pos_x=COALESCE(VALUES(pos_x),pos_x),pos_y=COALESCE(VALUES(pos_y),pos_y),pos_z=COALESCE(VALUES(pos_z),pos_z),";
  sql += "state_json=JSON_MERGE_PATCH(COALESCE(state_json,JSON_OBJECT()),VALUES(state_json)),";
  sql += "row_version=COALESCE(row_version,0)+1,updated_at=CURRENT_TIMESTAMP(6);";
  sql += "INSERT INTO world_inventory(world_instance_id,owner_entity_key,item_instance_id,amount,source_amount,source_iterator_count) ";
  sql += "SELECT @world_id," + entitySql + ",@item_id," + std::to_string(amount) + ",";
  sql += std::to_string(amount) + "," + std::to_string(amount) + " ";
  sql += "WHERE @world_id IS NOT NULL AND @item_id IS NOT NULL ";
  sql += "ON DUPLICATE KEY UPDATE ";
  sql += "amount=GREATEST(world_inventory.amount,VALUES(amount)),";
  sql += "source_amount=GREATEST(COALESCE(world_inventory.source_amount,0),VALUES(source_amount)),";
  sql += "source_iterator_count=GREATEST(COALESCE(world_inventory.source_iterator_count,0),VALUES(source_iterator_count)),";
  sql += "updated_at=CURRENT_TIMESTAMP(6);";
  sql += "CALL mmo_append_world_event(@realm_id,@world_id,@character_id,";
  sql += "'world_item_observed','world_entity'," + std::to_string(tick) + ",";
  sql += entitySql + ",@item_key,";
  sql += "JSON_OBJECT('world_item_entity_key'," + entitySql;
  sql += ",'item_instance_id',BIN_TO_UUID(@item_id,1)";
  sql += ",'item_symbol'," + std::to_string(identity.symbol);
  sql += ",'persistent_id'," + std::to_string(identity.persistentId);
  sql += ",'amount'," + std::to_string(amount);
  sql += ",'metadata'," + sqlJson(dbPayload) + "),";
  sql += sqlLiteral(idem) + ",'server',NULL,NULL,@event_id);";
  sql += "SELECT " + entitySql + ";";

  auto out = mysqlSingleField(target, sql);
  if(out.empty())
    throw std::runtime_error("observed world item materialization failed: key=" + entityKey +
                             " symbol=" + std::to_string(identity.symbol));

  std::cerr << "[observed_world_item_materialized] entity=" << out
            << " symbol=" << identity.symbol
            << " amount=" << amount
            << "\n";
  return out;
}
