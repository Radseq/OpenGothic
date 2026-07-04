SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET SESSION group_concat_max_len=104857600;

DELIMITER //

DROP VIEW IF EXISTS v_mmo_latest_save_checkpoint_strict_restore//
DROP PROCEDURE IF EXISTS mmo_assert_latest_save_checkpoint_restore_v1//
DROP PROCEDURE IF EXISTS mmo_validate_latest_save_checkpoint_restore_v1//
DROP PROCEDURE IF EXISTS mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1//
DROP FUNCTION IF EXISTS mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1//
CREATE PROCEDURE `mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1`(
  IN p_session_id BINARY(16),
  OUT p_snapshot_json LONGTEXT
)
build_snapshot: BEGIN
  DECLARE v_manifest_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_manifest_uuid VARCHAR(36) DEFAULT NULL;
  DECLARE v_session_uuid VARCHAR(36) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT 'PC_HERO';
  DECLARE v_world_name VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT 'newworld.zen';
  DECLARE v_hx DOUBLE DEFAULT 0;
  DECLARE v_hy DOUBLE DEFAULT 0;
  DECLARE v_hz DOUBLE DEFAULT 0;
  DECLARE v_active_radius DOUBLE DEFAULT 12000.0;
  DECLARE v_nearby_npc_radius DOUBLE DEFAULT 12000.0;
  DECLARE v_nearby_waypoint_radius DOUBLE DEFAULT 12000.0;
  DECLARE v_world_entity_count BIGINT DEFAULT 0;
  DECLARE v_world_inventory_count BIGINT DEFAULT 0;
  DECLARE v_interactive_count BIGINT DEFAULT 0;
  DECLARE v_script_int_count BIGINT DEFAULT 0;
  DECLARE v_character LONGTEXT CHARACTER SET utf8mb4 DEFAULT '{}';
  DECLARE v_inventory LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_equipment LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_known_dialogs LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_quests LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_script_state LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_script_state_full LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_script_full_count BIGINT DEFAULT 0;
  DECLARE v_world_clock LONGTEXT CHARACTER SET utf8mb4 DEFAULT '{}';
  DECLARE v_active_world_items LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_world_item_deltas LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_interactive_state LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_npc_lifecycle_state LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_nearby_npcs LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_nearby_npc_known_dialogs LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_nearby_waypoints LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_recent_actions LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_mover_state LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_manifest LONGTEXT CHARACTER SET utf8mb4 DEFAULT '{}';
  DECLARE v_not_found BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;

  SET v_session_uuid = BIN_TO_UUID(p_session_id, 1);

  SELECT sm.manifest_id, sm.character_id, sm.world_instance_id,
         BIN_TO_UUID(sm.manifest_id,1), c.character_key,
         COALESCE(cwt.world_name, rwi.world_instance_key, sm.client_world_name, 'newworld.zen')
    INTO v_manifest_id, v_character_id, v_world_instance_id,
         v_manifest_uuid, v_character_key, v_world_name
    FROM server_sessions ss
    JOIN mmo_save_checkpoint_manifests sm
      ON sm.character_id = ss.character_id
     AND sm.world_instance_id = ss.world_instance_id
    JOIN characters c ON c.character_id = sm.character_id
    JOIN realm_world_instances rwi ON rwi.world_instance_id = sm.world_instance_id
    LEFT JOIN content_world_templates cwt ON cwt.world_template_id = rwi.world_template_id
   WHERE ss.session_id = p_session_id
   ORDER BY sm.created_at DESC, sm.row_version DESC
   LIMIT 1;

  IF v_not_found OR v_manifest_id IS NULL THEN
    SET p_snapshot_json = NULL;
    LEAVE build_snapshot;
  END IF;

  SELECT COALESCE(pos_x, 0), COALESCE(pos_y, 0), COALESCE(pos_z, 0)
    INTO v_hx, v_hy, v_hz
    FROM mmo_save_checkpoint_character_snapshot
   WHERE manifest_id = v_manifest_id
   LIMIT 1;

  SELECT COUNT(*) INTO v_world_entity_count
    FROM mmo_save_checkpoint_world_entity_snapshot
   WHERE manifest_id = v_manifest_id;

  SELECT COUNT(*) INTO v_world_inventory_count
    FROM mmo_save_checkpoint_world_inventory_snapshot
   WHERE manifest_id = v_manifest_id;

  SELECT COUNT(*) INTO v_interactive_count
    FROM mmo_save_checkpoint_world_entity_snapshot
   WHERE manifest_id = v_manifest_id
     AND entity_kind = 'interactive';

  SELECT COUNT(*) INTO v_script_int_count
    FROM mmo_save_checkpoint_script_state_snapshot
   WHERE manifest_id = v_manifest_id
     AND value_type IN ('int','array_int');

  SELECT COUNT(*) INTO v_script_full_count
    FROM mmo_save_checkpoint_script_state_snapshot
   WHERE manifest_id = v_manifest_id;

  SELECT COALESCE(JSON_OBJECT(
           'character_key', v_character_key,
           'display_name', c.character_name,
           'world_name', v_world_name,
           'position', JSON_OBJECT(
             'x', s.pos_x, 'y', s.pos_y, 'z', s.pos_z,
             'yaw', s.rotation_yaw,
             'waypoint', s.current_waypoint_key,
             'server_tick', s.position_server_tick
           ),
           'stats', JSON_OBJECT(
             'level', s.level_value,
             'experience', s.experience_value,
             'experience_next', s.experience_next,
             'learning_points', s.learning_points,
             'health_current', s.health_current,
             'health_max', s.health_max,
             'mana_current', s.mana_current,
             'mana_max', s.mana_max,
             'strength', s.strength_value,
             'dexterity', s.dexterity_value,
             'guild', s.guild_value,
             'true_guild', s.true_guild_value
           ),
           'lifecycle_state', c.lifecycle_state,
           'updated_at', DATE_FORMAT(s.captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
         ), JSON_OBJECT())
    INTO v_character
    FROM mmo_save_checkpoint_character_snapshot s
    JOIN characters c ON c.character_id = s.character_id
   WHERE s.manifest_id = v_manifest_id
   LIMIT 1;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'item_instance_uuid', BIN_TO_UUID(s.item_instance_id,1),
      'item_instance_key', s.item_instance_key,
      'item_template_key', s.item_template_key,
      'symbol_index', s.symbol_index,
      'script_name', s.script_name,
      'display_name', s.display_name,
      'classification', 'unknown',
      'stack_policy', 'unknown',
      'amount', s.amount,
      'bag_index', s.bag_index,
      'equipped_slot', es.equipment_slot,
      'lifecycle_state', s.lifecycle_state,
      'updated_at', DATE_FORMAT(s.captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json
    FROM mmo_save_checkpoint_inventory_snapshot s
    LEFT JOIN mmo_save_checkpoint_equipment_snapshot es
      ON es.manifest_id = s.manifest_id
     AND es.item_instance_id = s.item_instance_id
    WHERE s.manifest_id = v_manifest_id
    ORDER BY COALESCE(s.bag_index,999999), s.item_instance_key
    LIMIT 2048
  ) rows_json), JSON_ARRAY()) INTO v_inventory;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'slot', equipment_slot,
      'item_instance_uuid', BIN_TO_UUID(item_instance_id,1),
      'item_instance_key', item_instance_key,
      'item_template_key', item_template_key,
      'symbol_index', symbol_index,
      'display_name', display_name,
      'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json
    FROM mmo_save_checkpoint_equipment_snapshot
    WHERE manifest_id = v_manifest_id
    ORDER BY equipment_slot
    LIMIT 64
  ) rows_json), JSON_ARRAY()) INTO v_equipment;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'npc_key', npc_key,
      'info_key', info_key,
      'known', known,
      'permanent', permanent,
      'availability_state', availability_state,
      'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json
    FROM mmo_save_checkpoint_known_dialog_snapshot
    WHERE manifest_id = v_manifest_id
    ORDER BY npc_key, info_key
    LIMIT 4096
  ) rows_json), JSON_ARRAY()) INTO v_known_dialogs;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'quest_key', quest_key,
      'section', section,
      'status', status,
      'entry_order', entry_order,
      'text_entries', text_entries,
      'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json
    FROM mmo_save_checkpoint_quest_snapshot
    WHERE manifest_id = v_manifest_id
    ORDER BY quest_key
    LIMIT 1024
  ) rows_json), JSON_ARRAY()) INTO v_quests;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'script_key', script_key,
      'symbol_index', symbol_index,
      'value_type', value_type,
      'value_index', value_index,
      'value_int', value_int,
      'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json
    FROM mmo_save_checkpoint_script_state_snapshot
    WHERE manifest_id = v_manifest_id
      AND value_type IN ('int','array_int')
    ORDER BY script_key, value_index
    LIMIT 16384
  ) rows_json), JSON_ARRAY()) INTO v_script_state;

  SELECT COALESCE(CONCAT('[', GROUP_CONCAT(CAST(row_json AS CHAR CHARACTER SET utf8mb4) ORDER BY sort_script_key, sort_value_index SEPARATOR ','), ']'), '[]')
    INTO v_script_state_full
    FROM (
      SELECT JSON_OBJECT(
        'script_key', script_key,
        'symbol_index', symbol_index,
        'value_type', value_type,
        'value_index', value_index,
        'value_int', value_int,
        'value_real', value_real,
        'value_text', value_text,
        'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
      ) AS row_json,
      script_key AS sort_script_key,
      value_index AS sort_value_index
      FROM mmo_save_checkpoint_script_state_snapshot
      WHERE manifest_id = v_manifest_id
      ORDER BY script_key, value_index
      LIMIT 20000
    ) rows_json;

  SELECT COALESCE(JSON_OBJECT(
           'world_instance_uuid', BIN_TO_UUID(world_instance_id,1),
           'world_name', v_world_name,
           'world_day', world_day,
           'world_time_ms', world_time_ms,
           'last_server_tick', last_server_tick,
           'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
         ), JSON_OBJECT())
    INTO v_world_clock
    FROM mmo_save_checkpoint_world_clock_snapshot
   WHERE manifest_id = v_manifest_id
   LIMIT 1;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'owner_key', entity_key,
      'entity_key', entity_key,
      'lifecycle_state', lifecycle_state,
      'persistent_id', CAST(JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.persistent_id')) AS SIGNED),
      'symbol_index', COALESCE(symbol_index, CAST(JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.item_template_symbol')) AS SIGNED)),
      'amount', COALESCE(CAST(JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.amount')) AS UNSIGNED), 1),
      'pos_x', pos_x,
      'pos_y', pos_y,
      'pos_z', pos_z,
      'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json,
    (((pos_x-v_hx)*(pos_x-v_hx))+((pos_y-v_hy)*(pos_y-v_hy))+((pos_z-v_hz)*(pos_z-v_hz))) AS dist_sq,
    entity_key AS owner_key
    FROM mmo_save_checkpoint_world_entity_snapshot
    WHERE manifest_id = v_manifest_id
      AND entity_kind = 'item'
      AND lifecycle_state = 'active'
      AND pos_x IS NOT NULL AND pos_y IS NOT NULL AND pos_z IS NOT NULL
      AND (((pos_x-v_hx)*(pos_x-v_hx))+((pos_y-v_hy)*(pos_y-v_hy))+((pos_z-v_hz)*(pos_z-v_hz))) <= (v_active_radius*v_active_radius)
    ORDER BY dist_sq ASC, owner_key
    LIMIT 1024
  ) rows_json), JSON_ARRAY()) INTO v_active_world_items;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'entity_key', entity_key,
      'entity_kind', entity_kind,
      'lifecycle_state', lifecycle_state,
      'persistent_id', CAST(JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.persistent_id')) AS SIGNED),
      'symbol_index', COALESCE(symbol_index, CAST(JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.item_template_symbol')) AS SIGNED)),
      'exists_in_world', JSON_EXTRACT(state_json,'$.exists_in_world'),
      'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json
    FROM mmo_save_checkpoint_world_entity_snapshot
    WHERE manifest_id = v_manifest_id
      AND entity_kind = 'item'
      AND lifecycle_state <> 'active'
    ORDER BY captured_at DESC, entity_key
    LIMIT 4096
  ) rows_json), JSON_ARRAY()) INTO v_world_item_deltas;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'entity_key', entity_key,
      'lifecycle_state', lifecycle_state,
      'state_id', CAST(JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.state_id')) AS SIGNED),
      'locked', JSON_EXTRACT(state_json,'$.locked'),
      'cracked', JSON_EXTRACT(state_json,'$.cracked'),
      'state_json', state_json,
      'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json
    FROM mmo_save_checkpoint_world_entity_snapshot
    WHERE manifest_id = v_manifest_id
      AND entity_kind = 'interactive'
    ORDER BY entity_key
    LIMIT 2048
  ) rows_json), JSON_ARRAY()) INTO v_interactive_state;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'entity_key', entity_key,
      'entity_kind', entity_kind,
      'lifecycle_state', lifecycle_state,
      'persistent_id', CAST(JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.persistent_id')) AS SIGNED),
      'symbol_index', COALESCE(symbol_index, script_id, CAST(JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.symbol_index')) AS SIGNED), CAST(JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.script_id')) AS SIGNED)),
      'health_current', health_current,
      'health_max', health_max,
      'pos_x', pos_x,
      'pos_y', pos_y,
      'pos_z', pos_z,
      'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json
    FROM mmo_save_checkpoint_world_entity_snapshot
    WHERE manifest_id = v_manifest_id
      AND entity_kind IN ('npc','creature')
      AND (lifecycle_state <> 'active' OR (health_current IS NOT NULL AND health_max IS NOT NULL AND health_current < health_max))
    ORDER BY CASE WHEN lifecycle_state='dead' THEN 0 WHEN lifecycle_state<>'active' THEN 1 ELSE 2 END, captured_at DESC, entity_key
    LIMIT 2048
  ) rows_json), JSON_ARRAY()) INTO v_npc_lifecycle_state;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'entity_key', entity_key,
      'entity_kind', entity_kind,
      'lifecycle_state', lifecycle_state,
      'symbol_index', symbol_index,
      'script_id', script_id,
      'script_name', script_name,
      'display_name', display_name,
      'health_current', health_current,
      'health_max', health_max,
      'pos_x', pos_x,
      'pos_y', pos_y,
      'pos_z', pos_z,
      'current_waypoint', JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.current_waypoint')),
      'routine_waypoint', JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.routine_waypoint')),
      'ai_state_name', JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.ai_state_name')),
      'distance', SQRT(((pos_x-v_hx)*(pos_x-v_hx))+((pos_y-v_hy)*(pos_y-v_hy))+((pos_z-v_hz)*(pos_z-v_hz))),
      'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json,
    (((pos_x-v_hx)*(pos_x-v_hx))+((pos_y-v_hy)*(pos_y-v_hy))+((pos_z-v_hz)*(pos_z-v_hz))) AS dist_sq,
    entity_key AS owner_key
    FROM mmo_save_checkpoint_world_entity_snapshot
    WHERE manifest_id = v_manifest_id
      AND entity_kind IN ('npc','creature')
      AND lifecycle_state IN ('active','dead','disabled')
      AND pos_x IS NOT NULL AND pos_y IS NOT NULL AND pos_z IS NOT NULL
      AND (((pos_x-v_hx)*(pos_x-v_hx))+((pos_y-v_hy)*(pos_y-v_hy))+((pos_z-v_hz)*(pos_z-v_hz))) <= (v_nearby_npc_radius*v_nearby_npc_radius)
    ORDER BY dist_sq ASC, owner_key
    LIMIT 256
  ) rows_json), JSON_ARRAY()) INTO v_nearby_npcs;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'npc_key', d.npc_key,
      'info_key', d.info_key,
      'known', d.known,
      'permanent', d.permanent,
      'availability_state', d.availability_state,
      'nearby_entity_key', npc.entity_key,
      'nearby_display_name', npc.display_name,
      'nearby_distance', SQRT(npc.dist_sq),
      'updated_at', DATE_FORMAT(d.captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json,
    npc.dist_sq,
    CONCAT(d.npc_key, ':', d.info_key) AS owner_key
    FROM (
      SELECT entity_key, script_name, display_name, symbol_index, script_id,
             (((pos_x-v_hx)*(pos_x-v_hx))+((pos_y-v_hy)*(pos_y-v_hy))+((pos_z-v_hz)*(pos_z-v_hz))) AS dist_sq
      FROM mmo_save_checkpoint_world_entity_snapshot
      WHERE manifest_id = v_manifest_id
        AND entity_kind IN ('npc','creature')
        AND lifecycle_state IN ('active','dead','disabled')
        AND pos_x IS NOT NULL AND pos_y IS NOT NULL AND pos_z IS NOT NULL
        AND (((pos_x-v_hx)*(pos_x-v_hx))+((pos_y-v_hy)*(pos_y-v_hy))+((pos_z-v_hz)*(pos_z-v_hz))) <= (v_nearby_npc_radius*v_nearby_npc_radius)
    ) npc
    JOIN mmo_save_checkpoint_known_dialog_snapshot d ON d.manifest_id = v_manifest_id
    WHERE d.npc_key IN (npc.entity_key, npc.script_name, npc.display_name, CAST(npc.symbol_index AS CHAR), CAST(npc.script_id AS CHAR))
    ORDER BY npc.dist_sq ASC, owner_key
    LIMIT 512
  ) rows_json), JSON_ARRAY()) INTO v_nearby_npc_known_dialogs;

  SET v_nearby_waypoints = JSON_ARRAY();

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'event_seq', event_seq,
      'event_type', event_type,
      'event_class', event_class,
      'entity_key', entity_key,
      'subject_key', subject_key,
      'server_tick', server_tick,
      'occurred_at', DATE_FORMAT(occurred_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json
    FROM world_event_journal
    WHERE world_instance_id = v_world_instance_id
    ORDER BY event_seq DESC
    LIMIT 64
  ) rows_json), JSON_ARRAY()) INTO v_recent_actions;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'mover_key', mover_key,
      'state_after', state_after,
      'state_after_name', state_after_name,
      'frame_index', frame_index,
      'target_frame_index', target_frame_index,
      'last_server_tick', last_server_tick,
      'row_version', source_row_version,
      'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json
    FROM mmo_save_checkpoint_mover_snapshot
    WHERE manifest_id = v_manifest_id
    ORDER BY last_server_tick DESC, mover_key
    LIMIT 512
  ) rows_json), JSON_ARRAY()) INTO v_mover_state;

  SELECT COALESCE(JSON_OBJECT(
           'manifest_uuid', BIN_TO_UUID(sm.manifest_id,1),
           'manifest_key', sm.manifest_key,
           'save_slot_key', sm.save_slot_key,
           'native_save_path', sm.native_save_path,
           'display_name', sm.display_name,
           'client_world_name', sm.client_world_name,
           'native_save_present', JSON_EXTRACT(IF(sm.native_save_present<>0,'true','false'),'$'),
           'checkpoint_kind', sm.checkpoint_kind,
           'reason', sm.reason,
           'server_tick', sm.server_tick,
           'latest_checkpoint_tick', sm.latest_checkpoint_tick,
           'recent_event_seq', sm.recent_event_seq,
           'inventory_rows', sm.inventory_rows,
           'equipment_rows', sm.equipment_rows,
           'quest_rows', sm.quest_rows,
           'known_dialog_rows', sm.known_dialog_rows,
           'script_state_rows', sm.script_state_rows,
           'world_item_rows', sm.world_item_rows,
           'world_inventory_rows', sm.world_inventory_rows,
           'interactive_rows', sm.interactive_rows,
           'npc_lifecycle_rows', sm.npc_lifecycle_rows,
           'mover_rows', sm.mover_rows,
           'row_version', sm.row_version,
           'created_at', DATE_FORMAT(sm.created_at,'%Y-%m-%dT%H:%i:%s.%fZ')
         ), JSON_OBJECT())
    INTO v_manifest
    FROM mmo_save_checkpoint_manifests sm
   WHERE sm.manifest_id = v_manifest_id
   LIMIT 1;

  SET p_snapshot_json = CONCAT(
    '{',
    '"schema":', JSON_QUOTE('mmo_bootstrap_snapshot_v1'),
    ',"source":', JSON_QUOTE('mmo_udp_server_cpp_db_save_checkpoint'),
    ',"snapshot_source":', JSON_QUOTE('db_save_checkpoint_v1'),
    ',"db_save_checkpoint_manifest_uuid":', JSON_QUOTE(v_manifest_uuid),
    ',"session_uuid":', JSON_QUOTE(COALESCE(v_session_uuid,'')),
    ',"character_key":', JSON_QUOTE(COALESCE(v_character_key,'PC_HERO')),
    ',"world_name":', JSON_QUOTE(COALESCE(v_world_name,'newworld.zen')),
    ',"ready":true',
    ',"world_entity_count":', COALESCE(v_world_entity_count,0),
    ',"world_inventory_count":', COALESCE(v_world_inventory_count,0),
    ',"active_world_item_radius":', v_active_radius,
    ',"nearby_npc_radius":', v_nearby_npc_radius,
    ',"nearby_waypoint_radius":', v_nearby_waypoint_radius,
    ',"interactive_count":', COALESCE(v_interactive_count,0),
    ',"script_int_count":', COALESCE(v_script_int_count,0),
    ',"script_state_full_count":', COALESCE(v_script_full_count,0),
    ',"script_state_truncated":', IF(v_script_int_count > 16384 OR v_script_full_count > 20000, 'true', 'false'),
    ',"character":', COALESCE(v_character,'{}'),
    ',"inventory":', COALESCE(v_inventory,'[]'),
    ',"equipment":', COALESCE(v_equipment,'[]'),
    ',"known_dialogs":', COALESCE(v_known_dialogs,'[]'),
    ',"quests":', COALESCE(v_quests,'[]'),
    ',"script_state":', COALESCE(v_script_state,'[]'),
    ',"script_state_full":', COALESCE(v_script_state_full,'[]'),
    ',"world_clock":', COALESCE(v_world_clock,'{}'),
    ',"active_world_items":', COALESCE(v_active_world_items,'[]'),
    ',"world_inventory_sample":', COALESCE(v_active_world_items,'[]'),
    ',"nearby_npcs":', COALESCE(v_nearby_npcs,'[]'),
    ',"nearby_npc_known_dialogs":', COALESCE(v_nearby_npc_known_dialogs,'[]'),
    ',"nearby_waypoints":', COALESCE(v_nearby_waypoints,'[]'),
    ',"interactive_state":', COALESCE(v_interactive_state,'[]'),
    ',"interactive_sample":[]',
    ',"npc_lifecycle_state":', COALESCE(v_npc_lifecycle_state,'[]'),
    ',"world_item_deltas":', COALESCE(v_world_item_deltas,'[]'),
    ',"world_entity_delta_sample":[]',
    ',"recent_actions":', COALESCE(v_recent_actions,'[]'),
    ',"recent_events_sample":', COALESCE(v_recent_actions,'[]'),
    ',"mover_state":', COALESCE(v_mover_state,'[]'),
    ',"server_checkpoint_manifest":', COALESCE(v_manifest,'{}'),
    ',"server_note":', JSON_QUOTE('server-bound client materialized from the latest DB-native save checkpoint snapshot; native .sav remains compatibility/debug cache'),
    '}'
  );
END build_snapshot//

CREATE PROCEDURE mmo_validate_latest_save_checkpoint_restore_v1(
  IN p_session_id BINARY(16),
  OUT p_validation_json LONGTEXT
)
BEGIN
  DECLARE v_session_found TINYINT(1) DEFAULT 0;
  DECLARE v_manifest_id BINARY(16) DEFAULT NULL;
  DECLARE v_manifest_uuid VARCHAR(36) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_world_instance_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_save_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_display_name VARCHAR(191) DEFAULT NULL;
  DECLARE v_client_world_name VARCHAR(191) DEFAULT NULL;
  DECLARE v_native_save_present TINYINT(1) DEFAULT 0;
  DECLARE v_created_at DATETIME(6) DEFAULT NULL;
  DECLARE v_character_rows BIGINT DEFAULT 0;
  DECLARE v_inventory_rows BIGINT DEFAULT 0;
  DECLARE v_equipment_rows BIGINT DEFAULT 0;
  DECLARE v_quest_rows BIGINT DEFAULT 0;
  DECLARE v_known_dialog_rows BIGINT DEFAULT 0;
  DECLARE v_script_state_rows BIGINT DEFAULT 0;
  DECLARE v_world_entity_rows BIGINT DEFAULT 0;
  DECLARE v_world_inventory_rows BIGINT DEFAULT 0;
  DECLARE v_world_clock_rows BIGINT DEFAULT 0;
  DECLARE v_mover_rows BIGINT DEFAULT 0;
  DECLARE v_snapshot LONGTEXT DEFAULT NULL;
  DECLARE v_exported_bootstrap_bytes BIGINT DEFAULT 0;
  DECLARE v_snapshot_source VARCHAR(96) DEFAULT NULL;
  DECLARE v_strict_ok TINYINT(1) DEFAULT 0;
  DECLARE v_reason VARCHAR(191) DEFAULT 'unknown';

  SELECT COUNT(*) > 0
    INTO v_session_found
    FROM server_sessions
   WHERE session_id = p_session_id;

  IF v_session_found THEN
    SELECT sm.manifest_id,
           BIN_TO_UUID(sm.manifest_id, 1),
           c.character_key,
           rwi.world_instance_key,
           COALESCE(sm.save_slot_key, sm.manifest_key),
           sm.display_name,
           sm.client_world_name,
           sm.native_save_present,
           sm.created_at
      INTO v_manifest_id,
           v_manifest_uuid,
           v_character_key,
           v_world_instance_key,
           v_save_key,
           v_display_name,
           v_client_world_name,
           v_native_save_present,
           v_created_at
      FROM server_sessions ss
      JOIN mmo_save_checkpoint_manifests sm
        ON sm.character_id = ss.character_id
       AND sm.world_instance_id = ss.world_instance_id
      JOIN characters c
        ON c.character_id = sm.character_id
      JOIN realm_world_instances rwi
        ON rwi.world_instance_id = sm.world_instance_id
     WHERE ss.session_id = p_session_id
     ORDER BY sm.created_at DESC, sm.row_version DESC
     LIMIT 1;
  END IF;

  IF v_manifest_id IS NOT NULL THEN
    SELECT COUNT(*) INTO v_character_rows
      FROM mmo_save_checkpoint_character_snapshot
     WHERE manifest_id = v_manifest_id;

    SELECT COUNT(*) INTO v_inventory_rows
      FROM mmo_save_checkpoint_inventory_snapshot
     WHERE manifest_id = v_manifest_id;

    SELECT COUNT(*) INTO v_equipment_rows
      FROM mmo_save_checkpoint_equipment_snapshot
     WHERE manifest_id = v_manifest_id;

    SELECT COUNT(*) INTO v_quest_rows
      FROM mmo_save_checkpoint_quest_snapshot
     WHERE manifest_id = v_manifest_id;

    SELECT COUNT(*) INTO v_known_dialog_rows
      FROM mmo_save_checkpoint_known_dialog_snapshot
     WHERE manifest_id = v_manifest_id;

    SELECT COUNT(*) INTO v_script_state_rows
      FROM mmo_save_checkpoint_script_state_snapshot
     WHERE manifest_id = v_manifest_id;

    SELECT COUNT(*) INTO v_world_entity_rows
      FROM mmo_save_checkpoint_world_entity_snapshot
     WHERE manifest_id = v_manifest_id;

    SELECT COUNT(*) INTO v_world_inventory_rows
      FROM mmo_save_checkpoint_world_inventory_snapshot
     WHERE manifest_id = v_manifest_id;

    SELECT COUNT(*) INTO v_world_clock_rows
      FROM mmo_save_checkpoint_world_clock_snapshot
     WHERE manifest_id = v_manifest_id;

    SELECT COUNT(*) INTO v_mover_rows
      FROM mmo_save_checkpoint_mover_snapshot
     WHERE manifest_id = v_manifest_id;

    CALL mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1(p_session_id, v_snapshot);
    SET v_exported_bootstrap_bytes = COALESCE(CHAR_LENGTH(v_snapshot), 0);
    SET v_snapshot_source = JSON_UNQUOTE(JSON_EXTRACT(v_snapshot, '$.snapshot_source'));
  END IF;

  IF NOT v_session_found THEN
    SET v_reason = 'session_not_found';
  ELSEIF v_manifest_id IS NULL THEN
    SET v_reason = 'missing_db_save_checkpoint_manifest';
  ELSEIF v_character_rows <> 1 THEN
    SET v_reason = 'missing_character_snapshot';
  ELSEIF v_exported_bootstrap_bytes <= 0 THEN
    SET v_reason = 'empty_bootstrap_snapshot_export';
  ELSEIF COALESCE(v_snapshot_source, '') <> 'db_save_checkpoint_v1' THEN
    SET v_reason = 'bootstrap_snapshot_is_not_db_save_checkpoint';
  ELSE
    SET v_reason = 'ok';
    SET v_strict_ok = 1;
  END IF;

  SET p_validation_json = JSON_OBJECT(
    'strict_restore_ok', IF(v_strict_ok = 1, JSON_EXTRACT('true', '$'), JSON_EXTRACT('false', '$')),
    'reason', v_reason,
    'session_found', IF(v_session_found = 1, JSON_EXTRACT('true', '$'), JSON_EXTRACT('false', '$')),
    'session_uuid', BIN_TO_UUID(p_session_id, 1),
    'manifest_uuid', v_manifest_uuid,
    'character_key', v_character_key,
    'world_instance_key', v_world_instance_key,
    'save_key', v_save_key,
    'display_name', v_display_name,
    'client_world_name', v_client_world_name,
    'native_save_present', IF(v_native_save_present = 1, JSON_EXTRACT('true', '$'), JSON_EXTRACT('false', '$')),
    'created_at', IF(v_created_at IS NULL, NULL, DATE_FORMAT(v_created_at, '%Y-%m-%dT%H:%i:%s.%fZ')),
    'snapshot_source', v_snapshot_source,
    'exported_bootstrap_bytes', v_exported_bootstrap_bytes,
    'character_rows', v_character_rows,
    'inventory_rows', v_inventory_rows,
    'equipment_rows', v_equipment_rows,
    'quest_rows', v_quest_rows,
    'known_dialog_rows', v_known_dialog_rows,
    'script_state_rows', v_script_state_rows,
    'world_entity_rows', v_world_entity_rows,
    'world_inventory_rows', v_world_inventory_rows,
    'world_clock_rows', v_world_clock_rows,
    'mover_rows', v_mover_rows
  );
END//

CREATE PROCEDURE mmo_assert_latest_save_checkpoint_restore_v1(
  IN p_session_id BINARY(16),
  OUT p_validation_json LONGTEXT
)
BEGIN
  CALL mmo_validate_latest_save_checkpoint_restore_v1(p_session_id, p_validation_json);

  IF COALESCE(JSON_UNQUOTE(JSON_EXTRACT(p_validation_json, '$.strict_restore_ok')), 'false') <> 'true' THEN
    SIGNAL SQLSTATE '45000'
      SET MESSAGE_TEXT = 'latest DB save checkpoint is not strict-restore ready';
  END IF;
END//

CREATE OR REPLACE VIEW v_mmo_latest_save_checkpoint_strict_restore AS
SELECT
  BIN_TO_UUID(ss.session_id, 1) AS session_uuid,
  ss.session_key,
  c.character_key,
  rwi.world_instance_key,
  BIN_TO_UUID(sm.manifest_id, 1) AS manifest_uuid,
  COALESCE(sm.save_slot_key, sm.manifest_key) AS save_key,
  sm.display_name,
  sm.client_world_name,
  sm.native_save_present,
  sm.created_at,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_character_snapshot s WHERE s.manifest_id = sm.manifest_id) AS character_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_inventory_snapshot s WHERE s.manifest_id = sm.manifest_id) AS inventory_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_equipment_snapshot s WHERE s.manifest_id = sm.manifest_id) AS equipment_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_quest_snapshot s WHERE s.manifest_id = sm.manifest_id) AS quest_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_known_dialog_snapshot s WHERE s.manifest_id = sm.manifest_id) AS known_dialog_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_script_state_snapshot s WHERE s.manifest_id = sm.manifest_id) AS script_state_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_world_entity_snapshot s WHERE s.manifest_id = sm.manifest_id) AS world_entity_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_world_inventory_snapshot s WHERE s.manifest_id = sm.manifest_id) AS world_inventory_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_world_clock_snapshot s WHERE s.manifest_id = sm.manifest_id) AS world_clock_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_mover_snapshot s WHERE s.manifest_id = sm.manifest_id) AS mover_rows,
  CAST(NULL AS UNSIGNED) AS exported_bootstrap_bytes,
  CAST(NULL AS CHAR(96)) AS snapshot_source,
  CASE
    WHEN (SELECT COUNT(*) FROM mmo_save_checkpoint_character_snapshot s WHERE s.manifest_id = sm.manifest_id) = 1 THEN 1
    ELSE 0
  END AS strict_restore_ok
FROM server_sessions ss
JOIN characters c
  ON c.character_id = ss.character_id
JOIN realm_world_instances rwi
  ON rwi.world_instance_id = ss.world_instance_id
JOIN mmo_save_checkpoint_manifests sm
  ON sm.character_id = ss.character_id
 AND sm.world_instance_id = ss.world_instance_id
WHERE sm.manifest_id = (
  SELECT sm2.manifest_id
    FROM mmo_save_checkpoint_manifests sm2
   WHERE sm2.character_id = ss.character_id
     AND sm2.world_instance_id = ss.world_instance_id
   ORDER BY sm2.created_at DESC, sm2.row_version DESC
   LIMIT 1
)//

DELIMITER ;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES(
  'step104_db_checkpoint_script_state_full_export',
  'db_save_checkpoint_script_state_full_export_proc_v2',
  'Exports safe int script_state for current client apply and full script_state_full for DB-native save checkpoint coverage. Uses PROCEDURE OUT instead of CREATE FUNCTION, so MySQL binary logging does not require SUPER/log_bin_trust_function_creators.'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);
