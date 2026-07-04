-- Step95: DB-backed save metadata required by DB save checkpoints.
-- This is deliberately minimal; real save content is materialized by Step96+.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_step95_add_save_manifest_columns;
DELIMITER ;;
CREATE PROCEDURE mmo_step95_add_save_manifest_columns()
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns
     WHERE table_schema = DATABASE()
       AND table_name = 'mmo_save_checkpoint_manifests'
       AND column_name = 'save_slot_key'
  ) THEN
    ALTER TABLE mmo_save_checkpoint_manifests
      ADD COLUMN save_slot_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL AFTER manifest_key;
  END IF;

  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns
     WHERE table_schema = DATABASE()
       AND table_name = 'mmo_save_checkpoint_manifests'
       AND column_name = 'native_save_path'
  ) THEN
    ALTER TABLE mmo_save_checkpoint_manifests
      ADD COLUMN native_save_path VARCHAR(512) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL AFTER save_slot_key;
  END IF;

  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns
     WHERE table_schema = DATABASE()
       AND table_name = 'mmo_save_checkpoint_manifests'
       AND column_name = 'display_name'
  ) THEN
    ALTER TABLE mmo_save_checkpoint_manifests
      ADD COLUMN display_name VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL AFTER native_save_path;
  END IF;

  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns
     WHERE table_schema = DATABASE()
       AND table_name = 'mmo_save_checkpoint_manifests'
       AND column_name = 'client_world_name'
  ) THEN
    ALTER TABLE mmo_save_checkpoint_manifests
      ADD COLUMN client_world_name VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL AFTER display_name;
  END IF;

  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns
     WHERE table_schema = DATABASE()
       AND table_name = 'mmo_save_checkpoint_manifests'
       AND column_name = 'native_save_present'
  ) THEN
    ALTER TABLE mmo_save_checkpoint_manifests
      ADD COLUMN native_save_present TINYINT(1) NOT NULL DEFAULT 0 AFTER client_world_name;
  END IF;
END ;;
DELIMITER ;
CALL mmo_step95_add_save_manifest_columns();
DROP PROCEDURE IF EXISTS mmo_step95_add_save_manifest_columns;

DROP PROCEDURE IF EXISTS mmo_create_save_checkpoint_manifest;
DELIMITER ;;
CREATE PROCEDURE mmo_create_save_checkpoint_manifest(
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
proc: BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_manifest_id BINARY(16) DEFAULT NULL;
  DECLARE v_manifest_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL;
  DECLARE v_save_slot_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL;
  DECLARE v_native_save_path VARCHAR(512) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL;
  DECLARE v_display_name VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL;
  DECLARE v_client_world_name VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL;
  DECLARE v_native_save_present TINYINT(1) DEFAULT 0;
  DECLARE v_checkpoint_kind VARCHAR(64) DEFAULT 'native_save';
  DECLARE v_reason VARCHAR(128) DEFAULT 'save_checkpoint_manifest';
  DECLARE v_latest_checkpoint_tick BIGINT UNSIGNED DEFAULT 0;
  DECLARE v_recent_event_seq BIGINT UNSIGNED DEFAULT 0;
  DECLARE v_inventory_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_equipment_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_quest_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_known_dialog_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_script_state_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_world_item_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_world_inventory_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_interactive_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_npc_lifecycle_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_mover_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_payload JSON DEFAULT JSON_OBJECT();
  DECLARE v_not_found BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;

  SET p_manifest_id = NULL;
  SET p_event_id = NULL;
  SET p_row_version_after = NULL;
  SET v_not_found = FALSE;

  SELECT s.realm_id, s.world_instance_id, s.character_id, c.character_key
    INTO v_realm_id, v_world_id, v_character_id, v_character_key
    FROM server_sessions s
    LEFT JOIN characters c ON c.character_id = s.character_id
   WHERE s.session_id = p_session_id
   LIMIT 1;

  IF v_not_found OR v_world_id IS NULL OR v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_create_save_checkpoint_manifest: invalid session';
  END IF;

  SET v_checkpoint_kind = COALESCE(NULLIF(p_checkpoint_kind,''), 'native_save');
  IF v_checkpoint_kind NOT IN ('native_save','server_checkpoint','debug','admin') THEN
    SET v_checkpoint_kind = 'native_save';
  END IF;
  SET v_reason = COALESCE(NULLIF(p_reason,''), 'save_checkpoint_manifest');
  SET v_manifest_key = COALESCE(NULLIF(p_manifest_key,''), CONCAT('character:', COALESCE(v_character_key, 'PC_HERO'), ':save-checkpoint'));

  SET v_save_slot_key = COALESCE(
    NULLIF(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.save_slot_key')), ''),
    NULLIF(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.slot_path')), ''),
    NULLIF(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.native_save_path')), ''),
    v_manifest_key
  );
  SET v_native_save_path = COALESCE(
    NULLIF(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.native_save_path')), ''),
    NULLIF(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.slot_path')), ''),
    v_save_slot_key
  );
  SET v_display_name = COALESCE(
    NULLIF(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.display_name')), ''),
    NULLIF(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.slot_display_name')), ''),
    NULLIF(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.slot_name')), ''),
    v_save_slot_key
  );
  SET v_client_world_name = COALESCE(
    NULLIF(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.client_world_name')), ''),
    NULLIF(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.world')), '')
  );
  SET v_native_save_present = CASE LOWER(COALESCE(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.native_save_present')), 'false'))
    WHEN 'true' THEN 1
    WHEN '1' THEN 1
    ELSE 0
  END;

  SELECT COALESCE(MAX(server_tick),0) INTO v_latest_checkpoint_tick FROM character_checkpoint_audit WHERE character_id = v_character_id;
  SELECT COALESCE(MAX(event_seq),0) INTO v_recent_event_seq FROM world_event_journal WHERE world_instance_id = v_world_id;
  SELECT COUNT(*) INTO v_inventory_rows FROM character_inventory WHERE character_id = v_character_id;
  SELECT COUNT(*) INTO v_equipment_rows FROM character_equipment WHERE character_id = v_character_id;
  SELECT COUNT(*) INTO v_quest_rows FROM character_quests WHERE character_id = v_character_id;
  SELECT COUNT(*) INTO v_known_dialog_rows FROM character_known_dialogs WHERE character_id = v_character_id;
  SELECT COUNT(*) INTO v_script_state_rows FROM character_script_state WHERE character_id = v_character_id;
  SELECT COUNT(*) INTO v_world_item_rows FROM world_entity_state WHERE world_instance_id = v_world_id AND entity_kind = 'item';
  SELECT COUNT(*) INTO v_world_inventory_rows FROM world_inventory WHERE world_instance_id = v_world_id;
  SELECT COUNT(*) INTO v_interactive_rows FROM world_entity_state WHERE world_instance_id = v_world_id AND entity_kind = 'interactive';
  SELECT COUNT(*) INTO v_npc_lifecycle_rows FROM world_entity_state WHERE world_instance_id = v_world_id AND entity_kind IN ('npc','creature') AND (lifecycle_state <> 'active' OR (health_current IS NOT NULL AND health_max IS NOT NULL AND health_current < health_max));
  SELECT COUNT(*) INTO v_mover_rows FROM mmo_world_mover_state_current WHERE world_instance_id = v_world_id;

  SET v_payload = JSON_MERGE_PATCH(
    COALESCE(p_metadata, JSON_OBJECT()),
    JSON_OBJECT(
      'character_key', COALESCE(v_character_key, 'PC_HERO'),
      'manifest_key', v_manifest_key,
      'save_slot_key', v_save_slot_key,
      'native_save_path', v_native_save_path,
      'display_name', v_display_name,
      'client_world_name', v_client_world_name,
      'native_save_present', JSON_EXTRACT(IF(v_native_save_present<>0,'true','false'),'$'),
      'checkpoint_kind', v_checkpoint_kind,
      'reason', v_reason,
      'latest_checkpoint_tick', v_latest_checkpoint_tick,
      'recent_event_seq', v_recent_event_seq,
      'inventory_rows', v_inventory_rows,
      'equipment_rows', v_equipment_rows,
      'quest_rows', v_quest_rows,
      'known_dialog_rows', v_known_dialog_rows,
      'script_state_rows', v_script_state_rows,
      'world_item_rows', v_world_item_rows,
      'world_inventory_rows', v_world_inventory_rows,
      'interactive_rows', v_interactive_rows,
      'npc_lifecycle_rows', v_npc_lifecycle_rows,
      'mover_rows', v_mover_rows
    )
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_id,
    v_character_id,
    'server_save_checkpoint_manifest_created',
    'system',
    COALESCE(p_server_tick,0),
    v_manifest_key,
    COALESCE(v_character_key, 'PC_HERO'),
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  SELECT manifest_id
    INTO v_manifest_id
    FROM mmo_save_checkpoint_manifests
   WHERE world_instance_id = v_world_id
     AND idempotency_key = p_idempotency_key
   LIMIT 1;

  IF v_manifest_id IS NULL THEN
    SET v_manifest_id = UUID_TO_BIN(UUID(), 1);
    INSERT INTO mmo_save_checkpoint_manifests(
      manifest_id, event_id, realm_id, world_instance_id, character_id, manifest_key,
      save_slot_key, native_save_path, display_name, client_world_name, native_save_present,
      checkpoint_kind, reason, server_tick, latest_checkpoint_tick, recent_event_seq,
      inventory_rows, equipment_rows, quest_rows, known_dialog_rows, script_state_rows,
      world_item_rows, world_inventory_rows, interactive_rows, npc_lifecycle_rows, mover_rows,
      metadata, idempotency_key, row_version
    ) VALUES (
      v_manifest_id, p_event_id, v_realm_id, v_world_id, v_character_id, v_manifest_key,
      v_save_slot_key, v_native_save_path, v_display_name, v_client_world_name, v_native_save_present,
      v_checkpoint_kind, v_reason, COALESCE(p_server_tick,0), v_latest_checkpoint_tick, v_recent_event_seq,
      v_inventory_rows, v_equipment_rows, v_quest_rows, v_known_dialog_rows, v_script_state_rows,
      v_world_item_rows, v_world_inventory_rows, v_interactive_rows, v_npc_lifecycle_rows, v_mover_rows,
      v_payload, COALESCE(p_idempotency_key, CONCAT('save-checkpoint:', UUID())), 1
    );
  ELSE
    UPDATE mmo_save_checkpoint_manifests
       SET event_id = p_event_id,
           save_slot_key = v_save_slot_key,
           native_save_path = v_native_save_path,
           display_name = v_display_name,
           client_world_name = v_client_world_name,
           native_save_present = v_native_save_present,
           checkpoint_kind = v_checkpoint_kind,
           reason = v_reason,
           server_tick = COALESCE(p_server_tick,0),
           latest_checkpoint_tick = v_latest_checkpoint_tick,
           recent_event_seq = v_recent_event_seq,
           inventory_rows = v_inventory_rows,
           equipment_rows = v_equipment_rows,
           quest_rows = v_quest_rows,
           known_dialog_rows = v_known_dialog_rows,
           script_state_rows = v_script_state_rows,
           world_item_rows = v_world_item_rows,
           world_inventory_rows = v_world_inventory_rows,
           interactive_rows = v_interactive_rows,
           npc_lifecycle_rows = v_npc_lifecycle_rows,
           mover_rows = v_mover_rows,
           metadata = v_payload,
           row_version = row_version + 1
     WHERE manifest_id = v_manifest_id;
  END IF;

  SELECT row_version INTO p_row_version_after FROM mmo_save_checkpoint_manifests WHERE manifest_id = v_manifest_id;
  SET p_manifest_id = v_manifest_id;
END ;;
DELIMITER ;

CREATE OR REPLACE VIEW v_mmo_latest_save_checkpoint_manifests AS
SELECT *
FROM (
  SELECT
    BIN_TO_UUID(sm.manifest_id,1) AS manifest_uuid,
    BIN_TO_UUID(sm.event_id,1) AS event_uuid,
    c.character_key,
    c.character_name,
    cwt.world_name,
    rwi.world_instance_key,
    sm.manifest_key,
    sm.save_slot_key,
    sm.native_save_path,
    sm.display_name,
    sm.client_world_name,
    sm.native_save_present,
    sm.checkpoint_kind,
    sm.reason,
    sm.server_tick,
    sm.latest_checkpoint_tick,
    sm.recent_event_seq,
    sm.inventory_rows,
    sm.equipment_rows,
    sm.quest_rows,
    sm.known_dialog_rows,
    sm.script_state_rows,
    sm.world_item_rows,
    sm.world_inventory_rows,
    sm.interactive_rows,
    sm.npc_lifecycle_rows,
    sm.mover_rows,
    sm.row_version,
    sm.created_at,
    sm.updated_at,
    ROW_NUMBER() OVER (PARTITION BY sm.character_id, COALESCE(sm.save_slot_key, sm.manifest_key) ORDER BY sm.created_at DESC, sm.row_version DESC) AS save_slot_rank,
    ROW_NUMBER() OVER (PARTITION BY sm.character_id ORDER BY sm.created_at DESC, sm.row_version DESC) AS character_rank
  FROM mmo_save_checkpoint_manifests sm
  JOIN characters c ON c.character_id = sm.character_id
  JOIN realm_world_instances rwi ON rwi.world_instance_id = sm.world_instance_id
  LEFT JOIN content_world_templates cwt ON cwt.world_template_id = rwi.world_template_id
) ranked
WHERE save_slot_rank = 1;
