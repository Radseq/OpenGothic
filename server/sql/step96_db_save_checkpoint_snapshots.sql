-- Step96: materialize DB-native save checkpoint snapshots from current projections.
-- This is the first real "save file -> DB" layer: normalized snapshot tables
-- instead of treating .sav path/catalog metadata as the save payload.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_character_snapshot (
  manifest_id BINARY(16) NOT NULL,
  character_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  pos_x DOUBLE DEFAULT NULL,
  pos_y DOUBLE DEFAULT NULL,
  pos_z DOUBLE DEFAULT NULL,
  rotation_yaw DOUBLE DEFAULT NULL,
  current_waypoint_key VARCHAR(191) DEFAULT NULL,
  position_server_tick BIGINT NOT NULL DEFAULT 0,
  position_row_version BIGINT NOT NULL DEFAULT 0,
  level_value INT NOT NULL DEFAULT 0,
  experience_value BIGINT NOT NULL DEFAULT 0,
  experience_next BIGINT DEFAULT NULL,
  learning_points INT NOT NULL DEFAULT 0,
  health_current INT NOT NULL DEFAULT 0,
  health_max INT NOT NULL DEFAULT 0,
  mana_current INT NOT NULL DEFAULT 0,
  mana_max INT NOT NULL DEFAULT 0,
  strength_value INT NOT NULL DEFAULT 0,
  dexterity_value INT NOT NULL DEFAULT 0,
  guild_value INT DEFAULT NULL,
  true_guild_value INT DEFAULT NULL,
  permanent_attitude INT DEFAULT NULL,
  temporary_attitude INT DEFAULT NULL,
  stats_row_version BIGINT NOT NULL DEFAULT 0,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id),
  KEY ix_mmo_save_character_snapshot_character (character_id),
  CONSTRAINT mmo_save_character_snapshot_manifest_fk FOREIGN KEY (manifest_id) REFERENCES mmo_save_checkpoint_manifests (manifest_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_inventory_snapshot (
  manifest_id BINARY(16) NOT NULL,
  item_instance_id BINARY(16) NOT NULL,
  item_instance_key VARCHAR(191) NOT NULL,
  item_template_key VARCHAR(191) DEFAULT NULL,
  symbol_index INT DEFAULT NULL,
  script_name VARCHAR(191) DEFAULT NULL,
  display_name VARCHAR(191) DEFAULT NULL,
  bag_index INT DEFAULT NULL,
  amount INT NOT NULL DEFAULT 1,
  instance_quantity INT NOT NULL DEFAULT 1,
  bind_state VARCHAR(32) NOT NULL DEFAULT 'unbound',
  lifecycle_state VARCHAR(32) NOT NULL DEFAULT 'active',
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id, item_instance_id),
  KEY ix_mmo_save_inventory_snapshot_template (manifest_id, item_template_key),
  CONSTRAINT mmo_save_inventory_snapshot_manifest_fk FOREIGN KEY (manifest_id) REFERENCES mmo_save_checkpoint_manifests (manifest_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_equipment_snapshot (
  manifest_id BINARY(16) NOT NULL,
  equipment_slot VARCHAR(32) NOT NULL,
  item_instance_id BINARY(16) NOT NULL,
  item_instance_key VARCHAR(191) NOT NULL,
  item_template_key VARCHAR(191) DEFAULT NULL,
  symbol_index INT DEFAULT NULL,
  script_name VARCHAR(191) DEFAULT NULL,
  display_name VARCHAR(191) DEFAULT NULL,
  amount INT NOT NULL DEFAULT 1,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id, equipment_slot),
  KEY ix_mmo_save_equipment_snapshot_item (manifest_id, item_instance_id),
  CONSTRAINT mmo_save_equipment_snapshot_manifest_fk FOREIGN KEY (manifest_id) REFERENCES mmo_save_checkpoint_manifests (manifest_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_quest_snapshot (
  manifest_id BINARY(16) NOT NULL,
  quest_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  section VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL DEFAULT '',
  status VARCHAR(32) NOT NULL,
  entry_order INT NOT NULL DEFAULT 0,
  text_entries JSON NOT NULL,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id, quest_key),
  KEY ix_mmo_save_quest_snapshot_status (manifest_id, status),
  CONSTRAINT mmo_save_quest_snapshot_manifest_fk FOREIGN KEY (manifest_id) REFERENCES mmo_save_checkpoint_manifests (manifest_id) ON DELETE CASCADE,
  CONSTRAINT mmo_save_quest_snapshot_entries_json_ck CHECK (JSON_VALID(text_entries))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_known_dialog_snapshot (
  manifest_id BINARY(16) NOT NULL,
  npc_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  info_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  known TINYINT(1) NOT NULL DEFAULT 1,
  permanent TINYINT(1) NOT NULL DEFAULT 0,
  availability_state VARCHAR(32) NOT NULL DEFAULT 'unknown',
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id, npc_key, info_key),
  CONSTRAINT mmo_save_dialog_snapshot_manifest_fk FOREIGN KEY (manifest_id) REFERENCES mmo_save_checkpoint_manifests (manifest_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_script_state_snapshot (
  manifest_id BINARY(16) NOT NULL,
  script_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  symbol_index INT DEFAULT NULL,
  value_type VARCHAR(32) NOT NULL,
  value_index INT NOT NULL DEFAULT 0,
  value_int BIGINT DEFAULT NULL,
  value_real DOUBLE DEFAULT NULL,
  value_text TEXT,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id, script_key, value_index),
  KEY ix_mmo_save_script_snapshot_symbol (manifest_id, symbol_index),
  CONSTRAINT mmo_save_script_snapshot_manifest_fk FOREIGN KEY (manifest_id) REFERENCES mmo_save_checkpoint_manifests (manifest_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_world_entity_snapshot (
  manifest_id BINARY(16) NOT NULL,
  entity_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  entity_kind VARCHAR(32) NOT NULL,
  entity_template_key VARCHAR(191) DEFAULT NULL,
  symbol_index INT DEFAULT NULL,
  script_id INT DEFAULT NULL,
  script_name VARCHAR(191) DEFAULT NULL,
  display_name VARCHAR(191) DEFAULT NULL,
  lifecycle_state VARCHAR(32) NOT NULL DEFAULT 'active',
  pos_x DOUBLE DEFAULT NULL,
  pos_y DOUBLE DEFAULT NULL,
  pos_z DOUBLE DEFAULT NULL,
  rotation_yaw DOUBLE DEFAULT NULL,
  health_current INT DEFAULT NULL,
  health_max INT DEFAULT NULL,
  state_json JSON NOT NULL,
  source_row_version BIGINT NOT NULL DEFAULT 0,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id, entity_key),
  KEY ix_mmo_save_world_entity_snapshot_kind (manifest_id, entity_kind, lifecycle_state),
  CONSTRAINT mmo_save_world_entity_snapshot_manifest_fk FOREIGN KEY (manifest_id) REFERENCES mmo_save_checkpoint_manifests (manifest_id) ON DELETE CASCADE,
  CONSTRAINT mmo_save_world_entity_snapshot_json_ck CHECK (JSON_VALID(state_json))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_world_inventory_snapshot (
  manifest_id BINARY(16) NOT NULL,
  owner_entity_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  item_instance_id BINARY(16) NOT NULL,
  item_instance_key VARCHAR(191) NOT NULL,
  item_template_key VARCHAR(191) DEFAULT NULL,
  symbol_index INT DEFAULT NULL,
  script_name VARCHAR(191) DEFAULT NULL,
  display_name VARCHAR(191) DEFAULT NULL,
  amount INT NOT NULL DEFAULT 1,
  instance_quantity INT NOT NULL DEFAULT 1,
  lifecycle_state VARCHAR(32) NOT NULL DEFAULT 'active',
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id, owner_entity_key, item_instance_id),
  KEY ix_mmo_save_world_inventory_snapshot_template (manifest_id, item_template_key),
  CONSTRAINT mmo_save_world_inventory_snapshot_manifest_fk FOREIGN KEY (manifest_id) REFERENCES mmo_save_checkpoint_manifests (manifest_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_mover_snapshot (
  manifest_id BINARY(16) NOT NULL,
  mover_key VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  state_after INT NOT NULL DEFAULT 0,
  state_after_name VARCHAR(96) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL,
  frame_index INT DEFAULT NULL,
  target_frame_index INT DEFAULT NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  source_row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id, mover_key),
  CONSTRAINT mmo_save_mover_snapshot_manifest_fk FOREIGN KEY (manifest_id) REFERENCES mmo_save_checkpoint_manifests (manifest_id) ON DELETE CASCADE,
  CONSTRAINT mmo_save_mover_snapshot_json_ck CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_materialize_save_checkpoint_snapshot_v1;
DELIMITER ;;
CREATE PROCEDURE mmo_materialize_save_checkpoint_snapshot_v1(
  IN p_manifest_id BINARY(16)
)
proc: BEGIN
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;

  SELECT character_id, world_instance_id
    INTO v_character_id, v_world_instance_id
    FROM mmo_save_checkpoint_manifests
   WHERE manifest_id = p_manifest_id
   LIMIT 1;

  IF v_character_id IS NULL OR v_world_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_materialize_save_checkpoint_snapshot_v1: invalid manifest';
  END IF;

  DELETE FROM mmo_save_checkpoint_mover_snapshot WHERE manifest_id = p_manifest_id;
  DELETE FROM mmo_save_checkpoint_world_inventory_snapshot WHERE manifest_id = p_manifest_id;
  DELETE FROM mmo_save_checkpoint_world_entity_snapshot WHERE manifest_id = p_manifest_id;
  DELETE FROM mmo_save_checkpoint_script_state_snapshot WHERE manifest_id = p_manifest_id;
  DELETE FROM mmo_save_checkpoint_known_dialog_snapshot WHERE manifest_id = p_manifest_id;
  DELETE FROM mmo_save_checkpoint_quest_snapshot WHERE manifest_id = p_manifest_id;
  DELETE FROM mmo_save_checkpoint_equipment_snapshot WHERE manifest_id = p_manifest_id;
  DELETE FROM mmo_save_checkpoint_inventory_snapshot WHERE manifest_id = p_manifest_id;
  DELETE FROM mmo_save_checkpoint_character_snapshot WHERE manifest_id = p_manifest_id;

  INSERT INTO mmo_save_checkpoint_character_snapshot(
    manifest_id, character_id, world_instance_id,
    pos_x, pos_y, pos_z, rotation_yaw, current_waypoint_key,
    position_server_tick, position_row_version,
    level_value, experience_value, experience_next, learning_points,
    health_current, health_max, mana_current, mana_max, strength_value, dexterity_value,
    guild_value, true_guild_value, permanent_attitude, temporary_attitude, stats_row_version
  )
  SELECT p_manifest_id, v_character_id, v_world_instance_id,
         cp.pos_x, cp.pos_y, cp.pos_z, cp.rotation_yaw, cp.current_waypoint_key,
         COALESCE(cp.server_tick,0), COALESCE(cp.row_version,0),
         COALESCE(cs.level,0), COALESCE(cs.experience,0), cs.experience_next, COALESCE(cs.learning_points,0),
         COALESCE(cs.health_current,0), COALESCE(cs.health_max,0), COALESCE(cs.mana_current,0), COALESCE(cs.mana_max,0),
         COALESCE(cs.strength,0), COALESCE(cs.dexterity,0),
         cs.guild, cs.true_guild, cs.permanent_attitude, cs.temporary_attitude, COALESCE(cs.row_version,0)
    FROM characters c
    LEFT JOIN character_positions cp ON cp.character_id = c.character_id
    LEFT JOIN character_stats cs ON cs.character_id = c.character_id
   WHERE c.character_id = v_character_id
   LIMIT 1;

  INSERT INTO mmo_save_checkpoint_inventory_snapshot(
    manifest_id, item_instance_id, item_instance_key, item_template_key, symbol_index,
    script_name, display_name, bag_index, amount, instance_quantity, bind_state, lifecycle_state
  )
  SELECT p_manifest_id, ci.item_instance_id, ii.item_instance_key, cit.item_template_key, cit.symbol_index,
         cit.script_name, COALESCE(cit.display_name, JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.display_name'))),
         ci.bag_index, ci.amount, ii.quantity, ii.bind_state, ii.lifecycle_state
    FROM character_inventory ci
    JOIN item_instances ii ON ii.item_instance_id = ci.item_instance_id
    LEFT JOIN content_item_templates cit ON cit.item_template_id = ii.item_template_id
   WHERE ci.character_id = v_character_id;

  INSERT INTO mmo_save_checkpoint_equipment_snapshot(
    manifest_id, equipment_slot, item_instance_id, item_instance_key, item_template_key,
    symbol_index, script_name, display_name, amount
  )
  SELECT p_manifest_id, ce.equipment_slot, ce.item_instance_id, ii.item_instance_key, cit.item_template_key,
         cit.symbol_index, cit.script_name, COALESCE(cit.display_name, JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.display_name'))), ii.quantity
    FROM character_equipment ce
    JOIN item_instances ii ON ii.item_instance_id = ce.item_instance_id
    LEFT JOIN content_item_templates cit ON cit.item_template_id = ii.item_template_id
   WHERE ce.character_id = v_character_id;

  INSERT INTO mmo_save_checkpoint_quest_snapshot(
    manifest_id, quest_key, section, status, entry_order, text_entries
  )
  SELECT p_manifest_id, quest_key, section, status, entry_order, text_entries
    FROM character_quests
   WHERE character_id = v_character_id;

  INSERT INTO mmo_save_checkpoint_known_dialog_snapshot(
    manifest_id, npc_key, info_key, known, permanent, availability_state
  )
  SELECT p_manifest_id, npc_key, info_key, known, permanent, availability_state
    FROM character_known_dialogs
   WHERE character_id = v_character_id;

  INSERT INTO mmo_save_checkpoint_script_state_snapshot(
    manifest_id, script_key, symbol_index, value_type, value_index, value_int, value_real, value_text
  )
  SELECT p_manifest_id, script_key, symbol_index, value_type, value_index, value_int, value_real, value_text
    FROM character_script_state
   WHERE character_id = v_character_id;

  INSERT INTO mmo_save_checkpoint_world_entity_snapshot(
    manifest_id, entity_key, entity_kind, entity_template_key, symbol_index, script_id, script_name, display_name,
    lifecycle_state, pos_x, pos_y, pos_z, rotation_yaw, health_current, health_max, state_json, source_row_version
  )
  SELECT p_manifest_id, wes.entity_key, wes.entity_kind, cet.engine_template_key, cet.symbol_index, cet.script_id,
         cet.script_name, cet.display_name, wes.lifecycle_state,
         wes.pos_x, wes.pos_y, wes.pos_z, wes.rotation_yaw, wes.health_current, wes.health_max,
         wes.state_json, wes.row_version
    FROM world_entity_state wes
    LEFT JOIN content_entity_templates cet ON cet.entity_template_id = wes.entity_template_id
   WHERE wes.world_instance_id = v_world_instance_id;

  INSERT INTO mmo_save_checkpoint_world_inventory_snapshot(
    manifest_id, owner_entity_key, item_instance_id, item_instance_key, item_template_key,
    symbol_index, script_name, display_name, amount, instance_quantity, lifecycle_state
  )
  SELECT p_manifest_id, wi.owner_entity_key, wi.item_instance_id, ii.item_instance_key, cit.item_template_key,
         cit.symbol_index, cit.script_name, COALESCE(cit.display_name, JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.display_name'))),
         wi.amount, ii.quantity, ii.lifecycle_state
    FROM world_inventory wi
    JOIN item_instances ii ON ii.item_instance_id = wi.item_instance_id
    LEFT JOIN content_item_templates cit ON cit.item_template_id = ii.item_template_id
   WHERE wi.world_instance_id = v_world_instance_id;

  INSERT INTO mmo_save_checkpoint_mover_snapshot(
    manifest_id, mover_key, state_after, state_after_name, frame_index, target_frame_index,
    last_server_tick, state_payload, source_row_version
  )
  SELECT p_manifest_id, mover_key, state_after, state_after_name, frame_index, target_frame_index,
         last_server_tick, state_payload, row_version
    FROM mmo_world_mover_state_current
   WHERE world_instance_id = v_world_instance_id;
END ;;
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_create_db_save_checkpoint_v1;
DELIMITER ;;
CREATE PROCEDURE mmo_create_db_save_checkpoint_v1(
  IN p_session_id BINARY(16),
  IN p_manifest_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci,
  IN p_checkpoint_kind VARCHAR(64),
  IN p_reason VARCHAR(128),
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci,
  OUT p_manifest_id BINARY(16),
  OUT p_event_id BINARY(16),
  OUT p_row_version_after BIGINT UNSIGNED
)
BEGIN
  CALL mmo_create_save_checkpoint_manifest(
    p_session_id,
    p_manifest_key,
    p_checkpoint_kind,
    p_reason,
    p_server_tick,
    p_metadata,
    p_idempotency_key,
    p_manifest_id,
    p_event_id,
    p_row_version_after
  );

  CALL mmo_materialize_save_checkpoint_snapshot_v1(p_manifest_id);
END ;;
DELIMITER ;

CREATE OR REPLACE VIEW v_mmo_save_checkpoint_snapshot_domain_counts AS
SELECT
  BIN_TO_UUID(sm.manifest_id,1) AS manifest_uuid,
  c.character_key,
  COALESCE(sm.save_slot_key, sm.manifest_key) AS save_key,
  sm.display_name,
  sm.created_at,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_character_snapshot s WHERE s.manifest_id = sm.manifest_id) AS character_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_inventory_snapshot s WHERE s.manifest_id = sm.manifest_id) AS inventory_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_equipment_snapshot s WHERE s.manifest_id = sm.manifest_id) AS equipment_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_quest_snapshot s WHERE s.manifest_id = sm.manifest_id) AS quest_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_known_dialog_snapshot s WHERE s.manifest_id = sm.manifest_id) AS known_dialog_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_script_state_snapshot s WHERE s.manifest_id = sm.manifest_id) AS script_state_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_world_entity_snapshot s WHERE s.manifest_id = sm.manifest_id) AS world_entity_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_world_inventory_snapshot s WHERE s.manifest_id = sm.manifest_id) AS world_inventory_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_mover_snapshot s WHERE s.manifest_id = sm.manifest_id) AS mover_rows
FROM mmo_save_checkpoint_manifests sm
JOIN characters c ON c.character_id = sm.character_id;
