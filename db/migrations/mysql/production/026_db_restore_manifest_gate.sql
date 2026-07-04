-- Gothic MMO MySQL production migration 026.
-- DB-only restore manifest and strict restore gate metadata.
-- Requires 001..025 MySQL production migrations.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE IF NOT EXISTS mmo_db_restore_manifests (
  restore_manifest_id     BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  world_instance_id       BINARY(16) NOT NULL,
  character_id            BINARY(16) NULL,
  manifest_key            VARCHAR(191) NOT NULL,
  manifest_status         VARCHAR(32) NOT NULL DEFAULT 'created',
  projection_hash_run_id  BINARY(16) NULL,
  validation_run_id       BINARY(16) NULL,
  max_event_seq           BIGINT NOT NULL DEFAULT 0,
  component_count         INT NOT NULL DEFAULT 0,
  db_error_count          INT NOT NULL DEFAULT 0,
  db_warning_count        INT NOT NULL DEFAULT 0,
  external_blocker_count  INT NOT NULL DEFAULT 0,
  manifest_payload        JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at              TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at              TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  UNIQUE KEY mmo_db_restore_manifests_key_uk(world_instance_id, manifest_key),
  KEY ix_mmo_db_restore_manifests_world_status(world_instance_id, manifest_status, created_at),
  CONSTRAINT mmo_db_restore_manifests_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT mmo_db_restore_manifests_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE SET NULL,
  CONSTRAINT mmo_db_restore_manifests_hash_run_fk FOREIGN KEY(projection_hash_run_id) REFERENCES mmo_projection_hash_runs(projection_hash_run_id) ON DELETE SET NULL,
  CONSTRAINT mmo_db_restore_manifests_validation_fk FOREIGN KEY(validation_run_id) REFERENCES mmo_projection_validation_runs(validation_run_id) ON DELETE SET NULL,
  CONSTRAINT mmo_db_restore_manifests_status_ck CHECK(manifest_status IN ('created','db_ready','blocked_external','failed')),
  CONSTRAINT mmo_db_restore_manifests_seq_ck CHECK(max_event_seq >= 0),
  CONSTRAINT mmo_db_restore_manifests_counts_ck CHECK(component_count >= 0 AND db_error_count >= 0 AND db_warning_count >= 0 AND external_blocker_count >= 0),
  CONSTRAINT mmo_db_restore_manifests_payload_json_ck CHECK(JSON_VALID(manifest_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_create_db_restore_manifest;
DELIMITER $$
CREATE PROCEDURE mmo_create_db_restore_manifest(
  IN  p_world_instance_id      BINARY(16),
  IN  p_character_key          VARCHAR(191),
  IN  p_manifest_key           VARCHAR(191),
  IN  p_metadata               JSON,
  OUT p_restore_manifest_id    BINARY(16),
  OUT p_manifest_status        VARCHAR(32),
  OUT p_db_error_count         INT,
  OUT p_external_blocker_count INT
)
BEGIN
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_hash_run_id BINARY(16) DEFAULT NULL;
  DECLARE v_validation_run_id BINARY(16) DEFAULT NULL;
  DECLARE v_component_count INT DEFAULT 0;
  DECLARE v_errors INT DEFAULT 0;
  DECLARE v_warnings INT DEFAULT 0;
  DECLARE v_external_blockers INT DEFAULT 0;
  DECLARE v_max_event_seq BIGINT DEFAULT 0;
  DECLARE v_manifest_id BINARY(16) DEFAULT NULL;

  IF p_world_instance_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='world_instance_id is required'; END IF;
  IF p_manifest_key IS NULL OR TRIM(p_manifest_key)='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='manifest_key is required'; END IF;

  IF p_character_key IS NOT NULL AND TRIM(p_character_key) <> '' THEN
    SELECT character_id INTO v_character_id FROM characters WHERE character_key=p_character_key LIMIT 1;
  END IF;

  CALL mmo_materialize_projection_hash_run(p_world_instance_id, p_character_key, CONCAT('restore-manifest-hash:', p_manifest_key), JSON_OBJECT('source','mmo_create_db_restore_manifest','metadata',COALESCE(p_metadata,JSON_OBJECT())), v_hash_run_id, v_component_count);
  CALL mmo_run_final_database_integrity_audit(p_world_instance_id, CONCAT('final-db:restore-manifest:', p_manifest_key), JSON_OBJECT('source','mmo_create_db_restore_manifest','metadata',COALESCE(p_metadata,JSON_OBJECT())), v_validation_run_id, v_errors, v_warnings);

  SELECT COALESCE(MAX(event_seq),0) INTO v_max_event_seq FROM world_event_journal WHERE world_instance_id=p_world_instance_id;

  SELECT COUNT(*) INTO v_external_blockers
    FROM mmo_restore_parity_scenarios s
   WHERE s.active=TRUE AND s.required=TRUE
     AND NOT EXISTS (
       SELECT 1 FROM mmo_restore_parity_results r
       JOIN mmo_restore_parity_runs pr ON pr.parity_run_id=r.parity_run_id
       WHERE r.scenario_key=s.scenario_key AND r.status='passed' AND pr.status='passed'
     );

  SET p_manifest_status = IF(v_errors > 0, 'failed', IF(v_external_blockers > 0, 'blocked_external', 'db_ready'));

  INSERT INTO mmo_db_restore_manifests(world_instance_id, character_id, manifest_key, manifest_status, projection_hash_run_id, validation_run_id, max_event_seq, component_count, db_error_count, db_warning_count, external_blocker_count, manifest_payload)
  VALUES(p_world_instance_id, v_character_id, p_manifest_key, p_manifest_status, v_hash_run_id, v_validation_run_id, v_max_event_seq, v_component_count, v_errors, v_warnings, v_external_blockers,
         JSON_OBJECT('metadata',COALESCE(p_metadata,JSON_OBJECT()),'component_count',v_component_count,'max_event_seq',v_max_event_seq,'rule','DB restore manifest is DB-ready only when DB errors are zero; external parity may still block MMO readiness.'))
  ON DUPLICATE KEY UPDATE
    character_id=VALUES(character_id), manifest_status=VALUES(manifest_status), projection_hash_run_id=VALUES(projection_hash_run_id), validation_run_id=VALUES(validation_run_id),
    max_event_seq=VALUES(max_event_seq), component_count=VALUES(component_count), db_error_count=VALUES(db_error_count), db_warning_count=VALUES(db_warning_count), external_blocker_count=VALUES(external_blocker_count), manifest_payload=VALUES(manifest_payload), updated_at=CURRENT_TIMESTAMP(6);

  SELECT restore_manifest_id INTO v_manifest_id FROM mmo_db_restore_manifests WHERE world_instance_id=p_world_instance_id AND manifest_key=p_manifest_key LIMIT 1;

  SET p_restore_manifest_id=v_manifest_id;
  SET p_db_error_count=v_errors;
  SET p_external_blocker_count=v_external_blockers;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_db_restore_manifests AS
SELECT BIN_TO_UUID(m.restore_manifest_id,1) AS restore_manifest_uuid,
       BIN_TO_UUID(m.world_instance_id,1) AS world_instance_uuid,
       BIN_TO_UUID(m.character_id,1) AS character_uuid,
       m.manifest_key,
       m.manifest_status,
       BIN_TO_UUID(m.projection_hash_run_id,1) AS projection_hash_run_uuid,
       BIN_TO_UUID(m.validation_run_id,1) AS validation_run_uuid,
       m.max_event_seq,
       m.component_count,
       m.db_error_count,
       m.db_warning_count,
       m.external_blocker_count,
       m.manifest_payload,
       m.created_at,
       m.updated_at
FROM mmo_db_restore_manifests m;

CREATE OR REPLACE VIEW v_db_restore_manifest_latest AS
SELECT * FROM v_db_restore_manifests m
WHERE m.created_at=(SELECT MAX(m2.created_at) FROM mmo_db_restore_manifests m2);

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES('production/mysql/026_db_restore_manifest_gate', 'gothic-mmo-db-restore-manifest-gate-v1-mysql', 'DB-only restore manifest gate that materializes projection hashes, runs final DB integrity audit and separates DB readiness from external parity blockers.')
ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes), applied_at=CURRENT_TIMESTAMP(6);
