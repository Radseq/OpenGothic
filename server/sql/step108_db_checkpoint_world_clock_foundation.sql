-- Step108: make the procedure-only DB checkpoint export path self-contained.
-- MySQL with binary logging can reject legacy CREATE FUNCTION bridges, so clean
-- reset skips Step97/Step98 and installs the final procedure path instead. The
-- world-clock snapshot table used to live behind that legacy bridge; keep it in
-- the procedure path so Step104/Step106 can be applied from a clean database.

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_world_clock_snapshot (
  manifest_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  world_day INT DEFAULT NULL,
  world_time_ms BIGINT DEFAULT NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  source_row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id),
  KEY ix_mmo_save_world_clock_snapshot_world (world_instance_id),
  CONSTRAINT mmo_save_world_clock_snapshot_manifest_fk
    FOREIGN KEY (manifest_id)
    REFERENCES mmo_save_checkpoint_manifests(manifest_id)
    ON DELETE CASCADE,
  CONSTRAINT mmo_save_world_clock_snapshot_world_fk
    FOREIGN KEY (world_instance_id)
    REFERENCES realm_world_instances(world_instance_id)
    ON DELETE CASCADE,
  CONSTRAINT mmo_save_world_clock_snapshot_payload_json_ck
    CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_materialize_save_checkpoint_world_clock_snapshot_v1;
DELIMITER ;;
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
  CALL mmo_materialize_save_checkpoint_world_clock_snapshot_v1(p_manifest_id);
END ;;
DELIMITER ;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES(
  'step108_db_checkpoint_world_clock_foundation',
  'db_checkpoint_world_clock_snapshot_v1',
  'Creates the world-clock checkpoint snapshot table and keeps procedure-only DB save checkpoints self-contained without legacy CREATE FUNCTION bridges.'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);
