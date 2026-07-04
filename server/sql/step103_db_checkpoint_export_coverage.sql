SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;

DELIMITER //

DROP PROCEDURE IF EXISTS mmo_materialize_save_checkpoint_world_clock_snapshot_v1//
CREATE PROCEDURE mmo_materialize_save_checkpoint_world_clock_snapshot_v1(
  IN p_manifest_id BINARY(16)
)
proc: BEGIN
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;

  SELECT world_instance_id
    INTO v_world_instance_id
    FROM mmo_save_checkpoint_manifests
   WHERE manifest_id = p_manifest_id
   LIMIT 1;

  IF v_world_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000'
      SET MESSAGE_TEXT='mmo_materialize_save_checkpoint_world_clock_snapshot_v1: invalid manifest';
  END IF;

  DELETE FROM mmo_save_checkpoint_world_clock_snapshot
   WHERE manifest_id = p_manifest_id;

  INSERT INTO mmo_save_checkpoint_world_clock_snapshot(
    manifest_id, world_instance_id, world_day, world_time_ms,
    last_server_tick, state_payload, source_row_version
  )
  SELECT p_manifest_id,
         v_world_instance_id,
         world_day,
         world_time_ms,
         last_server_tick,
         state_payload,
         row_version
    FROM mmo_world_clock_state_current
   WHERE world_instance_id = v_world_instance_id
   LIMIT 1;

  IF ROW_COUNT() = 0 THEN
    INSERT INTO mmo_save_checkpoint_world_clock_snapshot(
      manifest_id, world_instance_id, world_day, world_time_ms,
      last_server_tick, state_payload, source_row_version
    )
    SELECT p_manifest_id,
           rwi.world_instance_id,
           CAST(FLOOR(COALESCE(rwi.current_world_time_ms, 0) / 86400000) AS SIGNED),
           COALESCE(rwi.current_world_time_ms, 0),
           CAST(GREATEST(COALESCE(rwi.current_tick, 0), 0) AS UNSIGNED),
           JSON_OBJECT(
             'source', 'realm_world_instances_fallback',
             'current_tick', COALESCE(rwi.current_tick, 0),
             'current_world_time_ms', COALESCE(rwi.current_world_time_ms, 0)
           ),
           1
      FROM realm_world_instances rwi
     WHERE rwi.world_instance_id = v_world_instance_id
     LIMIT 1;
  END IF;
END//

DELIMITER ;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES(
  'step103_db_checkpoint_export_coverage',
  'db_save_checkpoint_export_coverage_v1',
  'Raises session aggregation limits in C++/tools and makes world clock checkpoint snapshot fall back to realm_world_instances.'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);

