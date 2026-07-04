-- Step98b: strict DB-native Continue/restore validation without CREATE FUNCTION.
-- MySQL instances with binary logging can reject stored functions for normal
-- users. This bridge is procedure-only, so it does not require SUPER or
-- log_bin_trust_function_creators.

DROP VIEW IF EXISTS v_mmo_latest_save_checkpoint_strict_restore;
DROP PROCEDURE IF EXISTS mmo_assert_latest_save_checkpoint_restore_v1;
DROP PROCEDURE IF EXISTS mmo_validate_latest_save_checkpoint_restore_v1;
DROP FUNCTION IF EXISTS mmo_validate_latest_save_checkpoint_restore_v1;

DELIMITER ;;

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

    SET v_snapshot = mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1(p_session_id);
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
END ;;

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
END ;;

DELIMITER ;

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
  CHAR_LENGTH(mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1(ss.session_id)) AS exported_bootstrap_bytes,
  JSON_UNQUOTE(JSON_EXTRACT(mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1(ss.session_id), '$.snapshot_source')) AS snapshot_source,
  CASE
    WHEN (SELECT COUNT(*) FROM mmo_save_checkpoint_character_snapshot s WHERE s.manifest_id = sm.manifest_id) <> 1 THEN 0
    WHEN CHAR_LENGTH(mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1(ss.session_id)) <= 0 THEN 0
    WHEN JSON_UNQUOTE(JSON_EXTRACT(mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1(ss.session_id), '$.snapshot_source')) <> 'db_save_checkpoint_v1' THEN 0
    ELSE 1
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
);

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES (
  'step98_strict_db_continue_restore',
  'strict-db-continue-restore-v1',
  'Strict DB-native Continue/restore validation bridge; procedure-only to work without SUPER when MySQL binary logging is enabled.'
)
ON DUPLICATE KEY UPDATE
  applied_at = CURRENT_TIMESTAMP(6),
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes);
