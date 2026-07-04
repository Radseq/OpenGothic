-- Step94: durable server-side save/checkpoint manifest.
-- This records a compact manifest that tells the server/client which DB projections
-- were visible when the native save completed in server-bound mode.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_manifests (
  manifest_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(),1)),
  event_id BINARY(16) DEFAULT NULL,
  realm_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  character_id BINARY(16) NOT NULL,
  manifest_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  checkpoint_kind VARCHAR(64) NOT NULL DEFAULT 'native_save',
  reason VARCHAR(128) NOT NULL DEFAULT 'save_checkpoint_manifest',
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  latest_checkpoint_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  recent_event_seq BIGINT UNSIGNED NOT NULL DEFAULT 0,
  inventory_rows INT UNSIGNED NOT NULL DEFAULT 0,
  equipment_rows INT UNSIGNED NOT NULL DEFAULT 0,
  quest_rows INT UNSIGNED NOT NULL DEFAULT 0,
  known_dialog_rows INT UNSIGNED NOT NULL DEFAULT 0,
  script_state_rows INT UNSIGNED NOT NULL DEFAULT 0,
  world_item_rows INT UNSIGNED NOT NULL DEFAULT 0,
  world_inventory_rows INT UNSIGNED NOT NULL DEFAULT 0,
  interactive_rows INT UNSIGNED NOT NULL DEFAULT 0,
  npc_lifecycle_rows INT UNSIGNED NOT NULL DEFAULT 0,
  mover_rows INT UNSIGNED NOT NULL DEFAULT 0,
  metadata JSON NOT NULL,
  idempotency_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id),
  UNIQUE KEY ux_mmo_save_checkpoint_manifest_idem (world_instance_id, idempotency_key),
  KEY ix_mmo_save_checkpoint_manifest_character (character_id, created_at),
  KEY ix_mmo_save_checkpoint_manifest_world_tick (world_instance_id, server_tick),
  CONSTRAINT mmo_save_checkpoint_manifest_event_fk FOREIGN KEY (event_id) REFERENCES world_event_journal(event_id) ON DELETE SET NULL,
  CONSTRAINT mmo_save_checkpoint_manifest_realm_fk FOREIGN KEY (realm_id) REFERENCES realm_realms(realm_id) ON DELETE RESTRICT,
  CONSTRAINT mmo_save_checkpoint_manifest_world_fk FOREIGN KEY (world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT mmo_save_checkpoint_manifest_character_fk FOREIGN KEY (character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT mmo_save_checkpoint_manifest_metadata_json_ck CHECK (JSON_VALID(metadata)),
  CONSTRAINT mmo_save_checkpoint_manifest_kind_ck CHECK (checkpoint_kind IN ('native_save','server_checkpoint','debug','admin'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

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
      'manifest_key', COALESCE(NULLIF(p_manifest_key,''), CONCAT('character:', COALESCE(v_character_key, 'PC_HERO'), ':save-checkpoint')),
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
    COALESCE(NULLIF(p_manifest_key,''), CONCAT('character:', COALESCE(v_character_key, 'PC_HERO'), ':save-checkpoint')),
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
      checkpoint_kind, reason, server_tick, latest_checkpoint_tick, recent_event_seq,
      inventory_rows, equipment_rows, quest_rows, known_dialog_rows, script_state_rows,
      world_item_rows, world_inventory_rows, interactive_rows, npc_lifecycle_rows, mover_rows,
      metadata, idempotency_key, row_version
    ) VALUES (
      v_manifest_id, p_event_id, v_realm_id, v_world_id, v_character_id,
      COALESCE(NULLIF(p_manifest_key,''), CONCAT('character:', COALESCE(v_character_key, 'PC_HERO'), ':save-checkpoint')),
      v_checkpoint_kind, v_reason, COALESCE(p_server_tick,0), v_latest_checkpoint_tick, v_recent_event_seq,
      v_inventory_rows, v_equipment_rows, v_quest_rows, v_known_dialog_rows, v_script_state_rows,
      v_world_item_rows, v_world_inventory_rows, v_interactive_rows, v_npc_lifecycle_rows, v_mover_rows,
      v_payload, COALESCE(p_idempotency_key, CONCAT('save-checkpoint:', UUID())), 1
    );
  ELSE
    UPDATE mmo_save_checkpoint_manifests
       SET event_id = p_event_id,
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
