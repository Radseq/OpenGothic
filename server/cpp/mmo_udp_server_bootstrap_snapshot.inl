// Internal implementation partition for mmo_udp_server.cpp.
// Kept include-based during the monolith split to preserve behavior.

[[nodiscard]] std::string buildBootstrapSnapshotJson(const MySqlTarget& target,
                                                     std::string_view sessionUuid,
                                                     std::string_view characterKey,
                                                     std::string_view worldName,
                                                     const BootstrapReadiness& readiness,
                                                     bool preferSaveCheckpointRestore,
                                                     bool requireSaveCheckpointRestore) {
  if(preferSaveCheckpointRestore) {
    if(auto checkpointSnapshot = buildSaveCheckpointBootstrapSnapshotJson(target, sessionUuid); !checkpointSnapshot.empty()) {
      std::cout << "[bootstrap_db_save_checkpoint_restore] bytes=" << checkpointSnapshot.size()
                << " session=" << sessionUuid << "\n";
      return checkpointSnapshot;
    }

    if(requireSaveCheckpointRestore)
      throw std::runtime_error("strict DB-save-checkpoint restore requested but no checkpoint bootstrap snapshot is available");

    std::cout << "[bootstrap_live_projection_fallback] reason=no_db_save_checkpoint session="
              << sessionUuid << "\n";
  }

  const std::string sessionSql = sqlLiteral(sessionUuid);
  const std::string worldSql = sqlLiteral(worldName);
  const auto characterSlices = readCharacterBootstrapSnapshotSlices(target, sessionUuid, characterKey, worldName);
  const auto& characterList = characterSlices.characterListJson;
  const auto& character = characterSlices.characterJson;
  const auto& inventory = characterSlices.inventoryJson;
  const auto& equipment = characterSlices.equipmentJson;
  const auto& dialogs = characterSlices.knownDialogsJson;
  const auto& quests = characterSlices.questsJson;
  const auto& scriptState = characterSlices.scriptStateJson;

  const auto worldSlices = readWorldBootstrapSnapshotSlices(target, sessionUuid, worldName);
  const auto& worldDeltas = worldSlices.worldDeltasJson;
  const auto& worldClock = worldSlices.worldClockJson;

  const std::string activeItemRadiusSql = std::to_string(Mmo::Server::BootstrapActiveWorldItemRadius);

  const std::string activeHeroSubquery =
      "(SELECT ss.character_id,ss.realm_id,ss.world_instance_id,COALESCE(cp.pos_x,cca.pos_x,0) AS hx,"
      "COALESCE(cp.pos_y,cca.pos_y,0) AS hy,COALESCE(cp.pos_z,cca.pos_z,0) AS hz "
      "FROM server_sessions ss "
      "LEFT JOIN character_positions cp ON cp.character_id=ss.character_id "
      "LEFT JOIN character_checkpoint_audit cca ON cca.checkpoint_id=(SELECT ca.checkpoint_id FROM character_checkpoint_audit ca WHERE ca.session_id=ss.session_id ORDER BY ca.created_at DESC LIMIT 1) "
      "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) LIMIT 1) h";

  std::string activeWorldInventorySource;
  activeWorldInventorySource += "SELECT JSON_OBJECT('owner_key',wi.owner_entity_key,'source','world_inventory','item_instance_uuid',BIN_TO_UUID(ii.item_instance_id,1),";
  activeWorldInventorySource += "'item_instance_key',ii.item_instance_key,'item_template_key',cit.item_template_key,'symbol_index',cit.symbol_index,";
  activeWorldInventorySource += "'display_name',cit.display_name,'amount',wi.amount,'lifecycle_state',wes.lifecycle_state,";
  activeWorldInventorySource += "'persistent_id',CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED),";
  activeWorldInventorySource += "'pos_x',wes.pos_x,'pos_y',wes.pos_y,'pos_z',wes.pos_z,";
  activeWorldInventorySource += "'distance',SQRT(((wes.pos_x-h.hx)*(wes.pos_x-h.hx))+((wes.pos_y-h.hy)*(wes.pos_y-h.hy))+((wes.pos_z-h.hz)*(wes.pos_z-h.hz))),";
  activeWorldInventorySource += "'updated_at',DATE_FORMAT(GREATEST(wi.updated_at,wes.updated_at),'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json,";
  activeWorldInventorySource += "((wes.pos_x-h.hx)*(wes.pos_x-h.hx))+((wes.pos_y-h.hy)*(wes.pos_y-h.hy))+((wes.pos_z-h.hz)*(wes.pos_z-h.hz)) AS dist_sq,wi.owner_entity_key AS owner_key ";
  activeWorldInventorySource += "FROM " + activeHeroSubquery + " JOIN world_inventory wi ON wi.world_instance_id=h.world_instance_id ";
  activeWorldInventorySource += "JOIN item_instances ii ON ii.item_instance_id=wi.item_instance_id AND ii.lifecycle_state='active' ";
  activeWorldInventorySource += "JOIN content_item_templates cit ON cit.item_template_id=ii.item_template_id ";
  activeWorldInventorySource += "JOIN world_entity_state wes ON wes.world_instance_id=wi.world_instance_id AND wes.entity_key=wi.owner_entity_key ";
  activeWorldInventorySource += "WHERE wes.entity_kind='item' AND wes.lifecycle_state='active' AND wi.amount>0 AND wes.pos_x IS NOT NULL AND wes.pos_y IS NOT NULL AND wes.pos_z IS NOT NULL ";
  activeWorldInventorySource += "AND (((wes.pos_x-h.hx)*(wes.pos_x-h.hx))+((wes.pos_y-h.hy)*(wes.pos_y-h.hy))+((wes.pos_z-h.hz)*(wes.pos_z-h.hz))) <= (" + activeItemRadiusSql + "*" + activeItemRadiusSql + ")";

  std::string activeWorldEntitySource;
  activeWorldEntitySource += "SELECT JSON_OBJECT('owner_key',src.entity_key,'source','world_entity_state','item_instance_uuid',CAST(NULL AS CHAR),";
  activeWorldEntitySource += "'item_instance_key',src.entity_key,'item_template_key',cit.item_template_key,'symbol_index',src.symbol_index,";
  activeWorldEntitySource += "'display_name',COALESCE(cit.display_name,src.display_name),'amount',src.amount,'lifecycle_state',src.lifecycle_state,";
  activeWorldEntitySource += "'persistent_id',src.persistent_id,'pos_x',src.pos_x,'pos_y',src.pos_y,'pos_z',src.pos_z,";
  activeWorldEntitySource += "'distance',SQRT(((src.pos_x-h.hx)*(src.pos_x-h.hx))+((src.pos_y-h.hy)*(src.pos_y-h.hy))+((src.pos_z-h.hz)*(src.pos_z-h.hz))),";
  activeWorldEntitySource += "'updated_at',DATE_FORMAT(src.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json,";
  activeWorldEntitySource += "((src.pos_x-h.hx)*(src.pos_x-h.hx))+((src.pos_y-h.hy)*(src.pos_y-h.hy))+((src.pos_z-h.hz)*(src.pos_z-h.hz)) AS dist_sq,src.entity_key AS owner_key ";
  activeWorldEntitySource += "FROM " + activeHeroSubquery + " JOIN realm_realms rr ON rr.realm_id=h.realm_id JOIN (";
  activeWorldEntitySource += "SELECT wes.world_instance_id,wes.entity_key,wes.lifecycle_state,wes.pos_x,wes.pos_y,wes.pos_z,wes.updated_at,";
  activeWorldEntitySource += "COALESCE(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.display_name')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.name'))) AS display_name,";
  activeWorldEntitySource += "CAST(COALESCE(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_template_symbol')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_id')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_symbol'))) AS SIGNED) AS symbol_index,";
  activeWorldEntitySource += "CAST(COALESCE(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.pid'))) AS SIGNED) AS persistent_id,";
  activeWorldEntitySource += "GREATEST(1,COALESCE(CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.amount')) AS SIGNED),CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.quantity')) AS SIGNED),1)) AS amount ";
  activeWorldEntitySource += "FROM world_entity_state wes WHERE wes.entity_kind='item' AND wes.lifecycle_state='active' AND wes.pos_x IS NOT NULL AND wes.pos_y IS NOT NULL AND wes.pos_z IS NOT NULL";
  activeWorldEntitySource += ") src ON src.world_instance_id=h.world_instance_id ";
  activeWorldEntitySource += "LEFT JOIN content_item_templates cit ON cit.content_revision_id=rr.active_content_revision_id AND cit.symbol_index=src.symbol_index ";
  activeWorldEntitySource += "WHERE src.symbol_index IS NOT NULL AND src.symbol_index>=0 ";
  activeWorldEntitySource += "AND NOT EXISTS (SELECT 1 FROM world_inventory wi WHERE wi.world_instance_id=src.world_instance_id AND wi.owner_entity_key=src.entity_key) ";
  activeWorldEntitySource += "AND (((src.pos_x-h.hx)*(src.pos_x-h.hx))+((src.pos_y-h.hy)*(src.pos_y-h.hy))+((src.pos_z-h.hz)*(src.pos_z-h.hz))) <= (" + activeItemRadiusSql + "*" + activeItemRadiusSql + ")";

  std::string activeReadModelSource;
  activeReadModelSource += "SELECT JSON_OBJECT('owner_key',rm.owner_key,'source','read_model','item_instance_uuid',CAST(NULL AS CHAR),";
  activeReadModelSource += "'item_instance_key',rm.item_instance_key,'item_template_key',rm.item_template_key,'symbol_index',cit.symbol_index,";
  activeReadModelSource += "'display_name',COALESCE(rm.display_name,cit.display_name),'amount',GREATEST(1,CAST(rm.amount AS SIGNED)),'lifecycle_state',rm.lifecycle_state,";
  activeReadModelSource += "'persistent_id',CAST(NULL AS SIGNED),'pos_x',rm.pos_x,'pos_y',rm.pos_y,'pos_z',rm.pos_z,";
  activeReadModelSource += "'distance',SQRT(((rm.pos_x-h.hx)*(rm.pos_x-h.hx))+((rm.pos_y-h.hy)*(rm.pos_y-h.hy))+((rm.pos_z-h.hz)*(rm.pos_z-h.hz))),";
  activeReadModelSource += "'updated_at',DATE_FORMAT(rm.materialized_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json,";
  activeReadModelSource += "((rm.pos_x-h.hx)*(rm.pos_x-h.hx))+((rm.pos_y-h.hy)*(rm.pos_y-h.hy))+((rm.pos_z-h.hz)*(rm.pos_z-h.hz)) AS dist_sq,rm.owner_key AS owner_key ";
  activeReadModelSource += "FROM " + activeHeroSubquery + " JOIN realm_world_instances rwi ON rwi.world_instance_id=h.world_instance_id ";
  activeReadModelSource += "LEFT JOIN content_world_templates cwt ON cwt.world_template_id=rwi.world_template_id ";
  activeReadModelSource += "JOIN realm_realms rr ON rr.realm_id=h.realm_id ";
  activeReadModelSource += "JOIN mmo_server_world_inventory_read_model rm ON rm.world_name=COALESCE(cwt.world_name,rwi.world_instance_key," + worldSql + ") ";
  activeReadModelSource += "LEFT JOIN content_item_templates cit ON cit.content_revision_id=rr.active_content_revision_id AND (cit.item_template_key=rm.item_template_key OR (rm.item_template_key REGEXP '^-?[0-9]+$' AND cit.symbol_index=CAST(rm.item_template_key AS SIGNED))) ";
  activeReadModelSource += "WHERE rm.lifecycle_state='active' AND rm.amount>0 AND rm.pos_x IS NOT NULL AND rm.pos_y IS NOT NULL AND rm.pos_z IS NOT NULL ";
  activeReadModelSource += "AND rm.owner_kind IN ('world','world_item','item') AND cit.symbol_index IS NOT NULL ";
  activeReadModelSource += "AND (((rm.pos_x-h.hx)*(rm.pos_x-h.hx))+((rm.pos_y-h.hy)*(rm.pos_y-h.hy))+((rm.pos_z-h.hz)*(rm.pos_z-h.hz))) <= (" + activeItemRadiusSql + "*" + activeItemRadiusSql + ")";

  const auto sourceRowsToJsonArray = [](const std::string& sourceSql) {
    std::string query;
    query += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (SELECT row_json FROM (";
    query += sourceSql;
    query += ") source_rows ORDER BY dist_sq ASC,owner_key LIMIT " + std::to_string(Mmo::Server::MaxBootstrapActiveWorldItemRows);
    query += ") ordered_rows), JSON_ARRAY());";
    return query;
  };

  const auto activeWorldInventoryItems = mysqlJsonOrWithDiagnostic(
      target,
      sourceRowsToJsonArray(activeWorldInventorySource),
      "[]",
      "bootstrap_active_world_inventory_items");
  const auto activeWorldEntityItems = mysqlJsonOrWithDiagnostic(
      target,
      sourceRowsToJsonArray(activeWorldEntitySource),
      "[]",
      "bootstrap_active_world_entity_items");
  const auto activeReadModelItems = mysqlJsonOrWithDiagnostic(
      target,
      sourceRowsToJsonArray(activeReadModelSource),
      "[]",
      "bootstrap_active_world_read_model_items");
  const auto activeWorldItems = concatenateJsonArrays({activeWorldInventoryItems, activeWorldEntityItems, activeReadModelItems});

  std::string activeWorldItemDebugQuery;
  activeWorldItemDebugQuery += "SELECT CONCAT('center=',ROUND(h.hx,2),',',ROUND(h.hy,2),',',ROUND(h.hz,2),' radius='," + activeItemRadiusSql + ",";
  activeWorldItemDebugQuery += "' wes_item_total=',(SELECT COUNT(*) FROM world_entity_state wes WHERE wes.world_instance_id=h.world_instance_id AND wes.entity_kind='item' AND wes.lifecycle_state='active'),";
  activeWorldItemDebugQuery += "' wes_item_near=',(SELECT COUNT(*) FROM world_entity_state wes WHERE wes.world_instance_id=h.world_instance_id AND wes.entity_kind='item' AND wes.lifecycle_state='active' AND wes.pos_x IS NOT NULL AND wes.pos_y IS NOT NULL AND wes.pos_z IS NOT NULL AND (((wes.pos_x-h.hx)*(wes.pos_x-h.hx))+((wes.pos_y-h.hy)*(wes.pos_y-h.hy))+((wes.pos_z-h.hz)*(wes.pos_z-h.hz))) <= (" + activeItemRadiusSql + "*" + activeItemRadiusSql + ")),";
  activeWorldItemDebugQuery += "' world_inventory_total=',(SELECT COUNT(*) FROM world_inventory wi WHERE wi.world_instance_id=h.world_instance_id),";
  activeWorldItemDebugQuery += "' world_inventory_item_near=',(SELECT COUNT(*) FROM world_inventory wi JOIN world_entity_state wes ON wes.world_instance_id=wi.world_instance_id AND wes.entity_key=wi.owner_entity_key WHERE wi.world_instance_id=h.world_instance_id AND wes.entity_kind='item' AND wes.lifecycle_state='active' AND wi.amount>0 AND wes.pos_x IS NOT NULL AND wes.pos_y IS NOT NULL AND wes.pos_z IS NOT NULL AND (((wes.pos_x-h.hx)*(wes.pos_x-h.hx))+((wes.pos_y-h.hy)*(wes.pos_y-h.hy))+((wes.pos_z-h.hz)*(wes.pos_z-h.hz))) <= (" + activeItemRadiusSql + "*" + activeItemRadiusSql + ")),";
  activeWorldItemDebugQuery += "' read_model_near=',(SELECT COUNT(*) FROM mmo_server_world_inventory_read_model rm WHERE rm.lifecycle_state='active' AND rm.amount>0 AND rm.pos_x IS NOT NULL AND rm.pos_y IS NOT NULL AND rm.pos_z IS NOT NULL AND rm.world_name=COALESCE(cwt.world_name,rwi.world_instance_key," + worldSql + ") AND (((rm.pos_x-h.hx)*(rm.pos_x-h.hx))+((rm.pos_y-h.hy)*(rm.pos_y-h.hy))+((rm.pos_z-h.hz)*(rm.pos_z-h.hz))) <= (" + activeItemRadiusSql + "*" + activeItemRadiusSql + "))) ";
  activeWorldItemDebugQuery += "FROM " + activeHeroSubquery + " JOIN realm_world_instances rwi ON rwi.world_instance_id=h.world_instance_id ";
  activeWorldItemDebugQuery += "LEFT JOIN content_world_templates cwt ON cwt.world_template_id=rwi.world_template_id;";
  const auto activeWorldItemDebug = mysqlSingleFieldWithDiagnostic(target, activeWorldItemDebugQuery, "bootstrap_active_world_items_debug");
  std::cout << "[bootstrap_active_world_items] bytes=" << activeWorldItems.size()
            << " world_inventory_bytes=" << activeWorldInventoryItems.size()
            << " world_entity_bytes=" << activeWorldEntityItems.size()
            << " read_model_bytes=" << activeReadModelItems.size();
  if(!activeWorldItemDebug.empty())
    std::cout << ' ' << activeWorldItemDebug;
  std::cout << "\n";

  const std::string nearbyNpcRadiusSql = std::to_string(Mmo::Server::BootstrapNearbyNpcRadius);

  std::string nearbyNpcIdentitySource;
  nearbyNpcIdentitySource += "SELECT h.character_id,wes.world_instance_id,wes.entity_key,wes.entity_kind,wes.lifecycle_state,wes.pos_x,wes.pos_y,wes.pos_z,wes.rotation_yaw,wes.health_current,wes.health_max,wes.updated_at,";
  nearbyNpcIdentitySource += "cet.engine_template_key AS entity_template_key,";
  nearbyNpcIdentitySource += "COALESCE(cet.symbol_index,CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')) AS SIGNED),CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_id')) AS SIGNED),CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_symbol')) AS SIGNED)) AS symbol_index,";
  nearbyNpcIdentitySource += "COALESCE(cet.script_id,CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_id')) AS SIGNED),CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_symbol')) AS SIGNED),CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')) AS SIGNED)) AS script_id,";
  nearbyNpcIdentitySource += "COALESCE(cet.script_name,JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_name')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_name')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_name'))) AS script_name,";
  nearbyNpcIdentitySource += "COALESCE(cet.display_name,JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.display_name')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.name')),wes.entity_key) AS display_name,";
  nearbyNpcIdentitySource += "COALESCE(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.current_waypoint_name')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.current_waypoint_key')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.current_waypoint')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.waypoint'))) AS current_waypoint,";
  nearbyNpcIdentitySource += "COALESCE(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.routine_waypoint_name')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.routine_waypoint')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.path_next_waypoint_name')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.move_target_waypoint_name'))) AS routine_waypoint,";
  nearbyNpcIdentitySource += "COALESCE(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.ai_state_name')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.state_name')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.move_hint'))) AS ai_state_name,";
  nearbyNpcIdentitySource += "((wes.pos_x-h.hx)*(wes.pos_x-h.hx))+((wes.pos_y-h.hy)*(wes.pos_y-h.hy))+((wes.pos_z-h.hz)*(wes.pos_z-h.hz)) AS dist_sq ";
  nearbyNpcIdentitySource += "FROM " + activeHeroSubquery + " JOIN world_entity_state wes ON wes.world_instance_id=h.world_instance_id ";
  nearbyNpcIdentitySource += "LEFT JOIN content_entity_templates cet ON cet.entity_template_id=wes.entity_template_id ";
  nearbyNpcIdentitySource += "WHERE wes.entity_kind IN ('npc','creature') AND wes.lifecycle_state IN ('active','dead','disabled') ";
  nearbyNpcIdentitySource += "AND wes.pos_x IS NOT NULL AND wes.pos_y IS NOT NULL AND wes.pos_z IS NOT NULL ";
  nearbyNpcIdentitySource += "AND (((wes.pos_x-h.hx)*(wes.pos_x-h.hx))+((wes.pos_y-h.hy)*(wes.pos_y-h.hy))+((wes.pos_z-h.hz)*(wes.pos_z-h.hz))) <= (" + nearbyNpcRadiusSql + "*" + nearbyNpcRadiusSql + ")";

  std::string nearbyNpcSource;
  nearbyNpcSource += "SELECT JSON_OBJECT('entity_key',src.entity_key,'entity_kind',src.entity_kind,'lifecycle_state',src.lifecycle_state,";
  nearbyNpcSource += "'entity_template_key',src.entity_template_key,'symbol_index',src.symbol_index,'script_id',src.script_id,'script_name',src.script_name,'display_name',src.display_name,";
  nearbyNpcSource += "'health_current',src.health_current,'health_max',src.health_max,'pos_x',src.pos_x,'pos_y',src.pos_y,'pos_z',src.pos_z,'rotation_yaw',src.rotation_yaw,";
  nearbyNpcSource += "'current_waypoint',src.current_waypoint,'routine_waypoint',src.routine_waypoint,'ai_state_name',src.ai_state_name,";
  nearbyNpcSource += "'distance',SQRT(src.dist_sq),'updated_at',DATE_FORMAT(src.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json,src.dist_sq,src.entity_key AS owner_key ";
  nearbyNpcSource += "FROM (" + nearbyNpcIdentitySource + ") src";

  std::string nearbyNpcDialogSource;
  nearbyNpcDialogSource += "SELECT JSON_OBJECT('npc_key',d.npc_key,'info_key',d.info_key,'known',d.known,'permanent',d.permanent,'availability_state',d.availability_state,";
  nearbyNpcDialogSource += "'nearby_entity_key',npc.entity_key,'nearby_display_name',npc.display_name,'nearby_distance',SQRT(npc.dist_sq),'updated_at',DATE_FORMAT(d.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json,npc.dist_sq,CONCAT(d.npc_key,':',d.info_key) AS owner_key ";
  nearbyNpcDialogSource += "FROM (" + nearbyNpcIdentitySource + ") npc JOIN character_known_dialogs d ON d.character_id=npc.character_id ";
  nearbyNpcDialogSource += "WHERE d.npc_key IN (npc.entity_key,npc.script_name,npc.display_name,CAST(npc.symbol_index AS CHAR),CAST(npc.script_id AS CHAR))";

  const auto sourceRowsToJsonArrayWithLimit = [](const std::string& sourceSql, std::size_t limit) {
    std::string query;
    query += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (SELECT row_json FROM (";
    query += sourceSql;
    query += ") source_rows ORDER BY dist_sq ASC,owner_key LIMIT " + std::to_string(limit);
    query += ") ordered_rows), JSON_ARRAY());";
    return query;
  };

  const auto nearbyNpcs = mysqlJsonOrWithDiagnostic(
      target,
      sourceRowsToJsonArrayWithLimit(nearbyNpcSource, Mmo::Server::MaxBootstrapNearbyNpcRows),
      "[]",
      "bootstrap_nearby_npcs");
  const auto nearbyNpcKnownDialogs = mysqlJsonOrWithDiagnostic(
      target,
      sourceRowsToJsonArrayWithLimit(nearbyNpcDialogSource, Mmo::Server::MaxBootstrapNearbyNpcKnownDialogRows),
      "[]",
      "bootstrap_nearby_npc_known_dialogs");
  const auto nearbyWaypointBootstrap = Mmo::Server::Waypoint::readNearbyWaypointBootstrap(target, sessionUuid, worldName);
  const auto& nearbyWaypoints = nearbyWaypointBootstrap.json;

  std::string nearbyNpcDebugQuery;
  nearbyNpcDebugQuery += "SELECT CONCAT('center=',ROUND(h.hx,2),',',ROUND(h.hy,2),',',ROUND(h.hz,2),' radius='," + nearbyNpcRadiusSql + ",";
  nearbyNpcDebugQuery += "' npc_total=',(SELECT COUNT(*) FROM world_entity_state wes WHERE wes.world_instance_id=h.world_instance_id AND wes.entity_kind IN ('npc','creature')),";
  nearbyNpcDebugQuery += "' npc_near=',(SELECT COUNT(*) FROM world_entity_state wes WHERE wes.world_instance_id=h.world_instance_id AND wes.entity_kind IN ('npc','creature') AND wes.lifecycle_state IN ('active','dead','disabled') AND wes.pos_x IS NOT NULL AND wes.pos_y IS NOT NULL AND wes.pos_z IS NOT NULL AND (((wes.pos_x-h.hx)*(wes.pos_x-h.hx))+((wes.pos_y-h.hy)*(wes.pos_y-h.hy))+((wes.pos_z-h.hz)*(wes.pos_z-h.hz))) <= (" + nearbyNpcRadiusSql + "*" + nearbyNpcRadiusSql + "))) ";
  nearbyNpcDebugQuery += "FROM " + activeHeroSubquery + " JOIN realm_world_instances rwi ON rwi.world_instance_id=h.world_instance_id ";
  nearbyNpcDebugQuery += "LEFT JOIN content_world_templates cwt ON cwt.world_template_id=rwi.world_template_id;";
  const auto nearbyNpcDebug = mysqlSingleFieldWithDiagnostic(target, nearbyNpcDebugQuery, "bootstrap_nearby_npcs_debug");
  std::cout << "[bootstrap_nearby_npcs] bytes=" << nearbyNpcs.size()
            << " known_dialog_bytes=" << nearbyNpcKnownDialogs.size()
            << " waypoint_bytes=" << nearbyWaypoints.size();
  if(!nearbyNpcDebug.empty())
    std::cout << ' ' << nearbyNpcDebug;
  if(!nearbyWaypointBootstrap.diagnostic.empty())
    std::cout << ' ' << nearbyWaypointBootstrap.diagnostic;
  std::cout << "\n";

  const auto& interactivesSample = worldSlices.interactiveStateJson;
  const auto& npcLifecycle = worldSlices.npcLifecycleJson;
  const auto& recentEvents = worldSlices.recentEventsJson;
  const auto& moverState = worldSlices.moverStateJson;

  const auto npcAuthority = readNpcAuthoritySnapshotSlices(target, sessionUuid, "bootstrap");
  const auto& npcRoutineState = npcAuthority.routineStateJson;
  const auto& npcAiState = npcAuthority.aiStateJson;
  const auto& npcPathState = npcAuthority.pathStateJson;
  const auto& npcFightState = npcAuthority.fightStateJson;

  const auto& triggerQueue = worldSlices.triggerQueueJson;
  const auto& worldTransitionState = worldSlices.worldTransitionStateJson;
  const auto& clientCorrections = worldSlices.clientCorrectionsJson;
  const auto& checkpointManifest = worldSlices.checkpointManifestJson;

  std::string out;
  out.reserve(character.size() + inventory.size() + equipment.size() + dialogs.size() + quests.size() +
              scriptState.size() + worldDeltas.size() + worldClock.size() + activeWorldItems.size() +
              nearbyNpcs.size() + nearbyNpcKnownDialogs.size() + nearbyWaypoints.size() +
              interactivesSample.size() + npcLifecycle.size() + recentEvents.size() +
              moverState.size() + npcRoutineState.size() + npcAiState.size() + npcPathState.size() +
              npcFightState.size() + triggerQueue.size() + worldTransitionState.size() +
              clientCorrections.size() + checkpointManifest.size() + characterList.size() + 2048);
  out.push_back('{');
  out += "\"schema\":";
  out += jsonEscape(Mmo::Server::BootstrapSnapshotSchema);
  appendJsonField(out, "source", "mmo_udp_server_cpp_live_mysql");
  appendJsonField(out, "snapshot_source", "current_projections_v1");
  appendJsonField(out, "session_uuid", sessionUuid);
  appendJsonField(out, "character_key", characterKey);
  appendJsonField(out, "world_name", worldName);
  appendJsonRawField(out, "ready", readiness.ready ? "true" : "false");
  appendJsonNumberField(out, "world_entity_count", readiness.worldEntityRows);
  appendJsonNumberField(out, "world_inventory_count", readiness.worldInventoryRows);
  appendJsonRawField(out, "active_world_item_radius", std::to_string(Mmo::Server::BootstrapActiveWorldItemRadius));
  appendJsonRawField(out, "nearby_npc_radius", std::to_string(Mmo::Server::BootstrapNearbyNpcRadius));
  appendJsonRawField(out, "nearby_waypoint_radius", std::to_string(Mmo::Server::BootstrapNearbyWaypointRadius));
  appendJsonNumberField(out, "interactive_count", readiness.interactiveRows);
  appendJsonNumberField(out, "script_int_count", readiness.scriptIntRows);
  appendJsonRawField(out, "script_state_truncated", readiness.scriptIntRows > Mmo::Server::MaxBootstrapScriptStateRows ? "true" : "false");
  appendJsonRawField(out, "character_list", characterList);
  appendJsonRawField(out, "character", character);
  appendJsonRawField(out, "inventory", inventory);
  appendJsonRawField(out, "equipment", equipment);
  appendJsonRawField(out, "known_dialogs", dialogs);
  appendJsonRawField(out, "quests", quests);
  appendJsonRawField(out, "script_state", scriptState);
  appendJsonRawField(out, "world_clock", worldClock);
  appendJsonRawField(out, Mmo::Server::BootstrapActiveWorldItemsSection, activeWorldItems);
  appendJsonRawField(out, "world_inventory_sample", activeWorldItems);
  appendJsonRawField(out, Mmo::Server::BootstrapNearbyNpcsSection, nearbyNpcs);
  appendJsonRawField(out, Mmo::Server::BootstrapNearbyNpcKnownDialogsSection, nearbyNpcKnownDialogs);
  appendJsonRawField(out, Mmo::Server::BootstrapNearbyWaypointsSection, nearbyWaypoints);
  appendJsonRawField(out, Mmo::Server::BootstrapInteractiveStateSection, interactivesSample);
  appendJsonRawField(out, "interactive_sample", "[]");
  appendJsonRawField(out, Mmo::Server::BootstrapNpcLifecycleStateSection, npcLifecycle);
  appendJsonRawField(out, Mmo::Server::BootstrapWorldItemDeltasSection, worldDeltas);
  appendJsonRawField(out, "world_entity_delta_sample", "[]");
  appendJsonRawField(out, Mmo::Server::BootstrapRecentActionsSection, recentEvents);
  appendJsonRawField(out, "recent_events_sample", recentEvents);
  appendJsonRawField(out, Mmo::Server::BootstrapMoverStateSection, moverState);
  appendJsonRawField(out, Mmo::Server::BootstrapNpcRoutineStateSection, npcRoutineState);
  appendJsonRawField(out, Mmo::Server::BootstrapNpcAiStateSection, npcAiState);
  appendJsonRawField(out, Mmo::Server::BootstrapNpcPathStateSection, npcPathState);
  appendJsonRawField(out, Mmo::Server::BootstrapNpcFightStateSection, npcFightState);
  appendJsonRawField(out, Mmo::Server::BootstrapTriggerQueueSection, triggerQueue);
  appendJsonRawField(out, Mmo::Server::BootstrapWorldTransitionStateSection, worldTransitionState);
  appendJsonRawField(out, Mmo::Server::BootstrapClientCorrectionsSection, clientCorrections);
  appendJsonRawField(out, Mmo::Server::BootstrapServerCheckpointManifestSection, checkpointManifest);
  out += ",\"server_note\":\"server-bound client applies HERO stats, inventory, equipment, position, story, script ints, world item tombstones, active world items, nearby NPC/dialog/waypoint/action windows, interactive state, mover state, correction slices, server checkpoint manifest and NPC lifecycle/authority slices when safe\"}";
  return out;
}
