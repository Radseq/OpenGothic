// Internal implementation partition for mmo_udp_server.cpp.
// Owns NPC/creature identity resolution and observed NPC materialization.

[[nodiscard]] std::string worldNpcFallbackRawKey(const Mmo::Net::ClientActionPacket& packet) {
  const std::string_view payload = packet.payloadJson;
  return optionalJsonString(payload, "target_world_entity_key",
         optionalJsonString(payload, "target_npc_entity_key",
         optionalJsonString(payload, "npc_entity_key",
         optionalJsonString(payload, "target_key", packet.targetKey))));
}

[[nodiscard]] ResolvedWorldNpcEntity resolveWorldNpcEntityKey(const MySqlTarget& target,
                                                             std::string_view sessionUuid,
                                                             const Mmo::Net::ClientActionPacket& packet,
                                                             std::string rawKey) {
  const std::string_view payload = packet.payloadJson;
  if(rawKey.empty())
    rawKey = worldNpcFallbackRawKey(packet);

  auto identity = parseWorldNpcIdentity(std::move(rawKey));
  fillWorldNpcIdentityFromPayload(identity, payload);

  const std::string exactSql = sqlLiteral(identity.exact);
  const bool hasStableIdentity = !identity.world.empty() && identity.persistentId >= 0 && identity.symbol >= 0;
  const bool hasPidSym = identity.persistentId >= 0 && identity.symbol >= 0;
  const std::string canonicalHookKey = hasStableIdentity ?
    Mmo::Server::Identity::canonicalNpcHookKey(identity.world, identity.persistentId, identity.symbol) : std::string();
  const std::string canonicalCreatureKey = hasStableIdentity ?
    Mmo::Server::Identity::canonicalCreatureHookKey(identity.world, identity.persistentId, identity.symbol) : std::string();
  const std::string worldPidSymLike = hasStableIdentity ?
    Mmo::Server::Identity::canonicalNpcLegacyLike(identity.world, identity.persistentId, identity.symbol) : std::string();
  const std::string creaturePidSymLike = hasStableIdentity ?
    Mmo::Server::Identity::canonicalCreatureLegacyLike(identity.world, identity.persistentId, identity.symbol) : std::string();
  const std::string anyWorldHookLike = hasPidSym ?
    Mmo::Server::Identity::anyWorldNpcHookLike(identity.persistentId, identity.symbol) : std::string();
  const std::string anyWorldCreatureLike = hasPidSym ?
    Mmo::Server::Identity::anyWorldCreatureHookLike(identity.persistentId, identity.symbol) : std::string();
  const std::string anyWorldPidSymLike = hasPidSym ?
    Mmo::Server::Identity::anyWorldNpcLegacyLike(identity.persistentId, identity.symbol) : std::string();
  const std::string anyWorldCreaturePidSymLike = hasPidSym ?
    Mmo::Server::Identity::anyWorldCreatureLegacyLike(identity.persistentId, identity.symbol) : std::string();

  std::string where = "wes.entity_key=" + exactSql;
  if(!canonicalHookKey.empty())
    where += " OR wes.entity_key=" + sqlLiteral(canonicalHookKey);
  if(!canonicalCreatureKey.empty())
    where += " OR wes.entity_key=" + sqlLiteral(canonicalCreatureKey);
  if(!worldPidSymLike.empty())
    where += " OR wes.entity_key LIKE " + sqlLiteral(worldPidSymLike);
  if(!creaturePidSymLike.empty())
    where += " OR wes.entity_key LIKE " + sqlLiteral(creaturePidSymLike);
  if(!anyWorldHookLike.empty())
    where += " OR wes.entity_key LIKE " + sqlLiteral(anyWorldHookLike);
  if(!anyWorldCreatureLike.empty())
    where += " OR wes.entity_key LIKE " + sqlLiteral(anyWorldCreatureLike);
  if(!anyWorldPidSymLike.empty())
    where += " OR wes.entity_key LIKE " + sqlLiteral(anyWorldPidSymLike);
  if(!anyWorldCreaturePidSymLike.empty())
    where += " OR wes.entity_key LIKE " + sqlLiteral(anyWorldCreaturePidSymLike);
  if(!identity.exact.empty()) {
    where += " OR JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.creature_spawn_key'))=" + exactSql;
    where += " OR JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.entity_key'))=" + exactSql;
    where += " OR JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.npc_entity_key'))=" + exactSql;
    where += " OR JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.source_entity_key'))=" + exactSql;
  }
  if(identity.persistentId >= 0 && identity.symbol >= 0) {
    where += " OR (CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED)=";
    where += std::to_string(identity.persistentId);
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.source_persistent_id')) AS SIGNED)=";
    where += std::to_string(identity.persistentId);
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.target_npc_persistent_id')) AS SIGNED)=";
    where += std::to_string(identity.persistentId);
    where += ") AND (";
    where += "CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_id')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_symbol')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.npc_symbol')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += " OR cet.symbol_index=";
    where += std::to_string(identity.symbol);
    where += " OR cet.script_id=";
    where += std::to_string(identity.symbol);
    where += ")";
    where += " OR (CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED)=";
    where += std::to_string(identity.persistentId);
    where += " AND (CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_id')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_symbol')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += "))";
  } else if(identity.persistentId >= 0) {
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED)=";
    where += std::to_string(identity.persistentId);
  } else if(identity.symbol >= 0) {
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_id')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_symbol')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.npc_symbol')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += " OR cet.symbol_index=";
    where += std::to_string(identity.symbol);
    where += " OR cet.script_id=";
    where += std::to_string(identity.symbol);
  }

  std::string query;
  query += "SELECT wes.entity_key, wes.lifecycle_state, COALESCE(wes.row_version,0) ";
  query += "FROM world_entity_state wes ";
  query += "LEFT JOIN content_entity_templates cet ON cet.entity_template_id=wes.entity_template_id ";
  query += "JOIN server_sessions ss ON ss.world_instance_id=wes.world_instance_id ";
  query += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) ";
  query += "AND wes.entity_kind IN ('npc','creature') AND (" + where + ") ";
  query += "ORDER BY CASE WHEN wes.entity_key=" + exactSql + " THEN 0 ";
  if(!canonicalHookKey.empty())
    query += "WHEN wes.entity_key=" + sqlLiteral(canonicalHookKey) + " THEN 1 ";
  if(!canonicalCreatureKey.empty())
    query += "WHEN wes.entity_key=" + sqlLiteral(canonicalCreatureKey) + " THEN 2 ";
  if(!worldPidSymLike.empty())
    query += "WHEN wes.entity_key LIKE " + sqlLiteral(worldPidSymLike) + " THEN 3 ";
  if(!creaturePidSymLike.empty())
    query += "WHEN wes.entity_key LIKE " + sqlLiteral(creaturePidSymLike) + " THEN 4 ";
  if(!anyWorldHookLike.empty())
    query += "WHEN wes.entity_key LIKE " + sqlLiteral(anyWorldHookLike) + " THEN 5 ";
  if(!anyWorldCreatureLike.empty())
    query += "WHEN wes.entity_key LIKE " + sqlLiteral(anyWorldCreatureLike) + " THEN 6 ";
  if(!anyWorldPidSymLike.empty())
    query += "WHEN wes.entity_key LIKE " + sqlLiteral(anyWorldPidSymLike) + " THEN 7 ";
  if(!anyWorldCreaturePidSymLike.empty())
    query += "WHEN wes.entity_key LIKE " + sqlLiteral(anyWorldCreaturePidSymLike) + " THEN 8 ";
  query += "ELSE 9 END, CASE WHEN wes.lifecycle_state='active' THEN 0 ELSE 1 END, wes.updated_at DESC LIMIT 1;";

  auto parts = splitMysqlLastRow(runMysql(target, query));

  if((parts.empty() || parts.front().empty()) && identity.symbol >= 0) {
    auto pos = optionalJsonVec3(payload, "target_position");
    if(!pos)
      pos = optionalJsonVec3(payload, "source_npc_position");
    if(!pos)
      pos = optionalJsonVec3(payload, "npc_position");
    if(!pos)
      pos = optionalJsonVec3(payload, "source_position");
    if(pos) {
      std::string fuzzy;
      fuzzy += "SELECT wes.entity_key, wes.lifecycle_state, COALESCE(wes.row_version,0) ";
      fuzzy += "FROM world_entity_state wes ";
      fuzzy += "LEFT JOIN content_entity_templates cet ON cet.entity_template_id=wes.entity_template_id ";
      fuzzy += "JOIN server_sessions ss ON ss.world_instance_id=wes.world_instance_id ";
      fuzzy += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) ";
      fuzzy += "AND wes.entity_kind IN ('npc','creature') AND wes.pos_x IS NOT NULL AND wes.pos_y IS NOT NULL AND wes.pos_z IS NOT NULL ";
      fuzzy += "AND (CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')) AS SIGNED)=" + std::to_string(identity.symbol);
      fuzzy += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_id')) AS SIGNED)=" + std::to_string(identity.symbol);
      fuzzy += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_symbol')) AS SIGNED)=" + std::to_string(identity.symbol);
      fuzzy += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.npc_symbol')) AS SIGNED)=" + std::to_string(identity.symbol);
      fuzzy += " OR cet.symbol_index=" + std::to_string(identity.symbol);
      fuzzy += " OR cet.script_id=" + std::to_string(identity.symbol);
      fuzzy += " OR wes.entity_key LIKE " + sqlLiteral("%:sym:" + std::to_string(identity.symbol) + "%");
      fuzzy += " OR wes.entity_key LIKE " + sqlLiteral("%:" + std::to_string(identity.symbol) + ":%");
      fuzzy += ") ";
      fuzzy += "AND ((wes.pos_x-(" + std::to_string(pos->x) + "))*(wes.pos_x-(" + std::to_string(pos->x) + ")) + ";
      fuzzy += "(wes.pos_y-(" + std::to_string(pos->y) + "))*(wes.pos_y-(" + std::to_string(pos->y) + ")) + ";
      fuzzy += "(wes.pos_z-(" + std::to_string(pos->z) + "))*(wes.pos_z-(" + std::to_string(pos->z) + "))) <= 100000000.0 ";
      fuzzy += "ORDER BY ((wes.pos_x-(" + std::to_string(pos->x) + "))*(wes.pos_x-(" + std::to_string(pos->x) + ")) + ";
      fuzzy += "(wes.pos_y-(" + std::to_string(pos->y) + "))*(wes.pos_y-(" + std::to_string(pos->y) + ")) + ";
      fuzzy += "(wes.pos_z-(" + std::to_string(pos->z) + "))*(wes.pos_z-(" + std::to_string(pos->z) + "))) ASC, ";
      fuzzy += "CASE WHEN wes.lifecycle_state='active' THEN 0 ELSE 1 END, wes.updated_at DESC LIMIT 1;";
      parts = splitMysqlLastRow(runMysql(target, fuzzy));
    }
  }

  if(parts.empty() || parts.front().empty()) {
    throw std::runtime_error("world NPC entity could not be resolved: key=" + identity.exact +
                             " world=" + identity.world +
                             " pid=" + std::to_string(identity.persistentId) +
                             " sym=" + std::to_string(identity.symbol));
  }

  ResolvedWorldNpcEntity out;
  out.entityKey = parts[0];
  if(parts.size() > 1)
    out.lifecycleState = parts[1];
  if(parts.size() > 2)
    out.rowVersion = parseI64(parts[2]).value_or(0);
  return out;
}

[[nodiscard]] std::optional<JsonVec3> worldNpcPositionFromPayload(std::string_view payload) {
  auto pos = optionalJsonVec3(payload, "target_position");
  if(!pos)
    pos = optionalJsonVec3(payload, "source_npc_position");
  if(!pos)
    pos = optionalJsonVec3(payload, "npc_position");
  if(!pos)
    pos = optionalJsonVec3(payload, "source_position");
  return pos;
}

[[nodiscard]] std::string stableObservedWorldNpcKey(const WorldNpcIdentity& identity) {
  if(Mmo::Server::Identity::startsWith(identity.exact, Mmo::Server::Identity::NpcHookPrefix) ||
     Mmo::Server::Identity::startsWith(identity.exact, Mmo::Server::Identity::CreatureHookPrefix))
    return identity.exact;
  if(identity.world.empty() || identity.persistentId < 0 || identity.symbol < 0)
    return {};
  return Mmo::Server::Identity::canonicalNpcHookKey(identity.world, identity.persistentId, identity.symbol);
}

[[nodiscard]] ResolvedWorldNpcEntity materializeObservedWorldNpcEntity(const MySqlTarget& target,
                                                                      std::string_view sessionUuid,
                                                                      const Mmo::Net::ClientActionPacket& packet,
                                                                      std::string_view dbPayload) {
  const std::string_view payload = packet.payloadJson;
  auto identity = parseWorldNpcIdentity(worldNpcFallbackRawKey(packet));
  fillWorldNpcIdentityFromPayload(identity, payload);
  const auto entityKey = stableObservedWorldNpcKey(identity);
  if(entityKey.empty() || identity.persistentId < 0 || identity.symbol < 0)
    throw std::runtime_error("observed world NPC cannot be materialized without stable pid/symbol identity");

  const auto pos = worldNpcPositionFromPayload(payload);
  const std::int64_t valueBefore = optionalJsonI64(payload, "value_before", -1);
  const std::int64_t valueAfter = optionalJsonI64(payload, "value_after", -1);
  const std::int64_t rawDamage = optionalJsonI64(payload, "damage_amount",
                                optionalJsonI64(payload, "amount",
                                optionalJsonI64(payload, "delta", 0)));
  const std::int64_t damage = rawDamage < 0 ? -rawDamage : rawDamage;
  std::int64_t healthMax = optionalJsonI64(payload, "health_max",
                           optionalJsonI64(payload, "target_npc_health_max",
                           optionalJsonI64(payload, "max_hitpoints", -1)));
  if(healthMax < 0)
    healthMax = std::max<std::int64_t>(1, std::max(valueBefore, std::max(valueAfter, damage)));
  std::int64_t healthCurrent = valueBefore >= 0 ? valueBefore : healthMax;
  healthCurrent = std::max<std::int64_t>(0, std::min(healthCurrent, healthMax));

  const auto displayName = optionalJsonString(payload, "target_npc_display_name",
                           optionalJsonString(payload, "source_npc_display_name",
                           optionalJsonString(payload, "npc_display_name")));
  const auto tick = packetServerTick(packet);

  std::string sql;
  sql += "SET @realm_id=NULL; SET @world_id=NULL; SET @character_id=NULL;";
  sql += "SET @content_revision_id=NULL; SET @template_id=NULL; SET @entity_kind=NULL; SET @event_id=NULL;";
  sql += "SELECT ss.realm_id,ss.world_instance_id,ss.character_id,rr.active_content_revision_id ";
  sql += "INTO @realm_id,@world_id,@character_id,@content_revision_id ";
  sql += "FROM server_sessions ss JOIN realm_realms rr ON rr.realm_id=ss.realm_id ";
  sql += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) AND ss.lifecycle_state='active' LIMIT 1;";
  sql += "SELECT cet.entity_template_id,cet.entity_kind INTO @template_id,@entity_kind ";
  sql += "FROM content_entity_templates cet WHERE cet.content_revision_id=@content_revision_id ";
  sql += "AND cet.entity_kind IN ('creature','npc') AND (cet.symbol_index=" + std::to_string(identity.symbol);
  sql += " OR cet.script_id=" + std::to_string(identity.symbol);
  sql += " OR cet.engine_template_key=" + sqlLiteral("creature-symbol:" + std::to_string(identity.symbol));
  sql += " OR cet.engine_template_key=" + sqlLiteral("npc-symbol:" + std::to_string(identity.symbol));
  sql += " OR cet.engine_template_key LIKE " + sqlLiteral("%:" + std::to_string(identity.symbol)) + ") ";
  sql += "ORDER BY CASE WHEN cet.entity_kind='creature' THEN 0 ELSE 1 END,cet.engine_template_key LIMIT 1;";
  sql += "INSERT INTO world_entity_state(";
  sql += "world_instance_id,entity_key,entity_kind,entity_template_id,lifecycle_state,pos_x,pos_y,pos_z,rotation_yaw,health_current,health_max,state_json,row_version";
  sql += ") VALUES(@world_id,";
  sql += sqlLiteral(entityKey) + ",COALESCE(@entity_kind,'creature'),@template_id,'active',";
  sql += (pos ? std::to_string(pos->x) : "NULL");
  sql += ",";
  sql += (pos ? std::to_string(pos->y) : "NULL");
  sql += ",";
  sql += (pos ? std::to_string(pos->z) : "NULL");
  sql += ",NULL,";
  sql += std::to_string(healthCurrent) + "," + std::to_string(healthMax) + ",";
  sql += "JSON_OBJECT(";
  sql += "'observed_runtime_entity',true,";
  sql += "'observed_from','mmo_udp_server_cpp',";
  sql += "'observed_at_tick'," + std::to_string(tick) + ",";
  sql += "'world'," + sqlLiteral(identity.world) + ",";
  sql += "'persistent_id'," + std::to_string(identity.persistentId) + ",";
  sql += "'symbol_index'," + std::to_string(identity.symbol) + ",";
  sql += "'entity_key'," + sqlLiteral(entityKey) + ",";
  sql += "'display_name'," + sqlLiteral(displayName) + ",";
  sql += "'last_payload'," + sqlJson(dbPayload);
  sql += "),1) ";
  sql += "ON DUPLICATE KEY UPDATE ";
  sql += "entity_template_id=COALESCE(entity_template_id,VALUES(entity_template_id)),";
  sql += "pos_x=COALESCE(VALUES(pos_x),pos_x),pos_y=COALESCE(VALUES(pos_y),pos_y),pos_z=COALESCE(VALUES(pos_z),pos_z),";
  sql += "health_max=GREATEST(COALESCE(health_max,0),VALUES(health_max)),";
  sql += "health_current=COALESCE(health_current,VALUES(health_current)),";
  sql += "state_json=JSON_MERGE_PATCH(COALESCE(state_json,JSON_OBJECT()),JSON_OBJECT(";
  sql += "'observed_runtime_entity',true,'last_observed_tick'," + std::to_string(tick) + ",";
  sql += "'persistent_id'," + std::to_string(identity.persistentId) + ",";
  sql += "'symbol_index'," + std::to_string(identity.symbol) + ",";
  sql += "'display_name'," + sqlLiteral(displayName);
  sql += ")),row_version=row_version+1,updated_at=CURRENT_TIMESTAMP(6);";
  sql += "CALL mmo_append_world_event(@realm_id,@world_id,@character_id,";
  sql += "'world_npc_observed','world_entity'," + std::to_string(tick) + ",";
  sql += sqlLiteral(entityKey) + "," + sqlLiteral(entityKey) + ",";
  sql += "JSON_OBJECT('entity_key'," + sqlLiteral(entityKey);
  sql += ",'persistent_id'," + std::to_string(identity.persistentId);
  sql += ",'symbol_index'," + std::to_string(identity.symbol);
  sql += ",'display_name'," + sqlLiteral(displayName);
  sql += ",'metadata'," + sqlJson(dbPayload) + "),";
  sql += sqlLiteral(packet.idempotencyKey + ":observed-npc") + ",'server',NULL,NULL,@event_id);";
  (void)runMysql(target, sql);
  std::cerr << "[observed_world_npc_materialized] entity=" << entityKey
            << " pid=" << identity.persistentId
            << " sym=" << identity.symbol
            << " display=" << displayName
            << "\n";
  return resolveWorldNpcEntityKey(target, sessionUuid, packet, entityKey);
}

[[nodiscard]] ResolvedWorldNpcEntity resolveTargetWorldNpcEntityKey(const MySqlTarget& target,
                                                                   std::string_view sessionUuid,
                                                                   const Mmo::Net::ClientActionPacket& packet) {
  return resolveWorldNpcEntityKey(target, sessionUuid, packet, worldNpcFallbackRawKey(packet));
}

[[nodiscard]] ResolvedWorldNpcEntity resolveTradeNpcEntityKey(const MySqlTarget& target,
                                                             std::string_view sessionUuid,
                                                             const Mmo::Net::ClientActionPacket& packet) {
  const std::string_view payload = packet.payloadJson;
  const auto raw = optionalJsonString(payload, "npc_entity_key",
                   optionalJsonString(payload, "target_npc_entity_key",
                   optionalJsonString(payload, "npc_key",
                   optionalJsonString(payload, "target_key", packet.targetKey))));
  return resolveWorldNpcEntityKey(target, sessionUuid, packet, raw);
}

[[nodiscard]] std::string resolveWorldInventoryOwnerEntityKey(const MySqlTarget& target,
                                                              std::string_view sessionUuid,
                                                              const Mmo::Net::ClientActionPacket& packet) {
  const std::string_view payload = packet.payloadJson;
  const auto raw = optionalJsonString(payload, "source_npc_entity_key",
                   optionalJsonString(payload, "source_entity_key",
                   optionalJsonString(payload, "source_container_key",
                   optionalJsonString(payload, "container_key",
                   optionalJsonString(payload, "owner_entity_key",
                   optionalJsonString(payload, "source_npc_key",
                   optionalJsonString(payload, "source_actor_key", packet.targetKey)))))));
  if(Mmo::Server::Identity::looksLikeNpcKey(raw))
    return resolveWorldNpcEntityKey(target, sessionUuid, packet, raw).entityKey;
  return raw;
}

[[nodiscard]] std::int64_t damageAmountFromPayload(std::string_view payload) noexcept {
  const auto amount = optionalJsonI64(payload, "damage_amount",
                      optionalJsonI64(payload, "amount",
                      optionalJsonI64(payload, "delta", 0)));
  if(amount == std::numeric_limits<std::int64_t>::min())
    return std::numeric_limits<std::int64_t>::max();
  return amount < 0 ? -amount : amount;
}

