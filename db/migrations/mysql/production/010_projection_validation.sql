-- Gothic MMO MySQL production migration 010.
-- Basic event/projection validation scaffolding.
-- Requires 001..009 MySQL production migrations and a bootstrap import.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE IF NOT EXISTS mmo_projection_validation_runs (
  validation_run_id    BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  world_instance_id    BINARY(16) NOT NULL,
  run_key              VARCHAR(191) NOT NULL,
  run_kind             VARCHAR(32) NOT NULL DEFAULT 'basic',
  status               VARCHAR(32) NOT NULL DEFAULT 'running',
  started_at           TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  finished_at          TIMESTAMP(6) NULL,
  max_event_seq        BIGINT NOT NULL DEFAULT 0,
  error_count          INT NOT NULL DEFAULT 0,
  warning_count        INT NOT NULL DEFAULT 0,
  metadata             JSON NOT NULL DEFAULT (JSON_OBJECT()),
  UNIQUE KEY mmo_projection_validation_runs_key_uk(world_instance_id, run_key),
  KEY ix_mmo_projection_validation_runs_world_started(world_instance_id, started_at),
  CONSTRAINT mmo_projection_validation_runs_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT mmo_projection_validation_runs_kind_ck CHECK(run_kind IN ('basic','replay','parity','smoke')),
  CONSTRAINT mmo_projection_validation_runs_status_ck CHECK(status IN ('running','passed','failed')),
  CONSTRAINT mmo_projection_validation_runs_seq_ck CHECK(max_event_seq >= 0),
  CONSTRAINT mmo_projection_validation_runs_counts_ck CHECK(error_count >= 0 AND warning_count >= 0),
  CONSTRAINT mmo_projection_validation_runs_metadata_json_ck CHECK(JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_projection_validation_results (
  validation_result_id BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  validation_run_id    BINARY(16) NOT NULL,
  check_name           VARCHAR(128) NOT NULL,
  severity             VARCHAR(16) NOT NULL,
  checked_count        BIGINT NOT NULL DEFAULT 0,
  problem_count        BIGINT NOT NULL DEFAULT 0,
  details              JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at           TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY mmo_projection_validation_results_check_uk(validation_run_id, check_name),
  KEY ix_mmo_projection_validation_results_severity(severity, created_at),
  CONSTRAINT mmo_projection_validation_results_run_fk FOREIGN KEY(validation_run_id) REFERENCES mmo_projection_validation_runs(validation_run_id) ON DELETE CASCADE,
  CONSTRAINT mmo_projection_validation_results_severity_ck CHECK(severity IN ('ok','warning','error')),
  CONSTRAINT mmo_projection_validation_results_counts_ck CHECK(checked_count >= 0 AND problem_count >= 0),
  CONSTRAINT mmo_projection_validation_results_details_json_ck CHECK(JSON_VALID(details))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_validate_world_projection_basic;
DELIMITER $$
CREATE PROCEDURE mmo_validate_world_projection_basic(
  IN  p_world_instance_id  BINARY(16),
  IN  p_run_key            VARCHAR(191),
  IN  p_metadata           JSON,
  OUT p_validation_run_id  BINARY(16),
  OUT p_error_count        INT
)
BEGIN
  DECLARE v_run_id BINARY(16) DEFAULT NULL;
  DECLARE v_max_event_seq BIGINT DEFAULT 0;
  DECLARE v_character_inventory_total BIGINT DEFAULT 0;
  DECLARE v_character_inventory_errors BIGINT DEFAULT 0;
  DECLARE v_equipment_total BIGINT DEFAULT 0;
  DECLARE v_equipment_errors BIGINT DEFAULT 0;
  DECLARE v_container_inventory_total BIGINT DEFAULT 0;
  DECLARE v_container_inventory_errors BIGINT DEFAULT 0;
  DECLARE v_character_world_total BIGINT DEFAULT 0;
  DECLARE v_character_world_errors BIGINT DEFAULT 0;
  DECLARE v_live_session_total BIGINT DEFAULT 0;
  DECLARE v_live_session_errors BIGINT DEFAULT 0;
  DECLARE v_error_count INT DEFAULT 0;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET p_validation_run_id = NULL;
  SET p_error_count = NULL;

  IF p_world_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'world instance id is required';
  END IF;
  IF p_run_key IS NULL OR TRIM(p_run_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'projection validation run key is required';
  END IF;

  START TRANSACTION;

  SELECT COALESCE(MAX(event_seq), 0)
    INTO v_max_event_seq
    FROM world_event_journal
   WHERE world_instance_id = p_world_instance_id;

  INSERT INTO mmo_projection_validation_runs(
    world_instance_id, run_key, run_kind, status, max_event_seq, metadata
  ) VALUES(
    p_world_instance_id, p_run_key, 'basic', 'running', v_max_event_seq, COALESCE(p_metadata, JSON_OBJECT())
  )
  ON DUPLICATE KEY UPDATE
    status = 'running',
    started_at = CURRENT_TIMESTAMP(6),
    finished_at = NULL,
    max_event_seq = VALUES(max_event_seq),
    error_count = 0,
    warning_count = 0,
    metadata = VALUES(metadata);

  SELECT validation_run_id
    INTO v_run_id
    FROM mmo_projection_validation_runs
   WHERE world_instance_id = p_world_instance_id
     AND run_key = p_run_key
   LIMIT 1
   FOR UPDATE;

  DELETE FROM mmo_projection_validation_results WHERE validation_run_id = v_run_id;

  SELECT COUNT(*),
         SUM(CASE WHEN ii.item_instance_id IS NULL OR ii.owner_type <> 'character' OR ii.owner_id IS NULL OR ii.owner_id <> ci.character_id OR ii.lifecycle_state <> 'active' THEN 1 ELSE 0 END)
    INTO v_character_inventory_total, v_character_inventory_errors
    FROM character_inventory ci
    LEFT JOIN item_instances ii ON ii.item_instance_id = ci.item_instance_id;

  SET v_character_inventory_errors = COALESCE(v_character_inventory_errors, 0);

  INSERT INTO mmo_projection_validation_results(validation_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(
    v_run_id,
    'character_inventory_owner_consistency',
    IF(v_character_inventory_errors = 0, 'ok', 'error'),
    v_character_inventory_total,
    v_character_inventory_errors,
    JSON_OBJECT('rule', 'character_inventory rows must point to active item_instances owned by same character')
  );

  SELECT COUNT(*),
         SUM(CASE WHEN ci.item_instance_id IS NULL OR ii.item_instance_id IS NULL OR ii.owner_type <> 'character' OR ii.owner_id <> ce.character_id OR ii.lifecycle_state <> 'active' THEN 1 ELSE 0 END)
    INTO v_equipment_total, v_equipment_errors
    FROM character_equipment ce
    LEFT JOIN character_inventory ci ON ci.character_id = ce.character_id AND ci.item_instance_id = ce.item_instance_id
    LEFT JOIN item_instances ii ON ii.item_instance_id = ce.item_instance_id;

  SET v_equipment_errors = COALESCE(v_equipment_errors, 0);

  INSERT INTO mmo_projection_validation_results(validation_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(
    v_run_id,
    'character_equipment_inventory_consistency',
    IF(v_equipment_errors = 0, 'ok', 'error'),
    v_equipment_total,
    v_equipment_errors,
    JSON_OBJECT('rule', 'equipped items must still exist in character_inventory and be active character-owned item_instances')
  );

  SELECT COUNT(*),
         SUM(CASE WHEN ii.item_instance_id IS NULL OR ii.owner_type <> 'container' OR ii.lifecycle_state <> 'active' THEN 1 ELSE 0 END)
    INTO v_container_inventory_total, v_container_inventory_errors
    FROM world_inventory wi
    LEFT JOIN item_instances ii ON ii.item_instance_id = wi.item_instance_id
   WHERE wi.world_instance_id = p_world_instance_id;

  SET v_container_inventory_errors = COALESCE(v_container_inventory_errors, 0);

  INSERT INTO mmo_projection_validation_results(validation_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(
    v_run_id,
    'world_container_inventory_owner_consistency',
    IF(v_container_inventory_errors = 0, 'ok', 'error'),
    v_container_inventory_total,
    v_container_inventory_errors,
    JSON_OBJECT('rule', 'world_inventory container rows must point to active item_instances owned by container')
  );

  SELECT COUNT(*),
         SUM(CASE WHEN c.current_world_instance_id IS NULL OR cp.world_instance_id IS NULL OR c.current_world_instance_id <> cp.world_instance_id THEN 1 ELSE 0 END)
    INTO v_character_world_total, v_character_world_errors
    FROM characters c
    LEFT JOIN character_positions cp ON cp.character_id = c.character_id
   WHERE c.lifecycle_state IN ('active','dead');

  SET v_character_world_errors = COALESCE(v_character_world_errors, 0);

  INSERT INTO mmo_projection_validation_results(validation_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(
    v_run_id,
    'character_current_world_position_consistency',
    IF(v_character_world_errors = 0, 'ok', 'error'),
    v_character_world_total,
    v_character_world_errors,
    JSON_OBJECT('rule', 'characters.current_world_instance_id must match character_positions.world_instance_id')
  );

  SELECT COUNT(*),
         SUM(CASE WHEN c.character_id IS NULL OR c.lifecycle_state NOT IN ('active','dead') OR ss.world_instance_id <> c.current_world_instance_id THEN 1 ELSE 0 END)
    INTO v_live_session_total, v_live_session_errors
    FROM server_sessions ss
    LEFT JOIN characters c ON c.character_id = ss.character_id
   WHERE ss.lifecycle_state = 'active'
     AND ss.world_instance_id = p_world_instance_id;

  SET v_live_session_errors = COALESCE(v_live_session_errors, 0);

  INSERT INTO mmo_projection_validation_results(validation_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(
    v_run_id,
    'active_session_character_consistency',
    IF(v_live_session_errors = 0, 'ok', 'error'),
    v_live_session_total,
    v_live_session_errors,
    JSON_OBJECT('rule', 'active sessions must point to active/dead characters in the same current world')
  );

  SELECT COUNT(*)
    INTO v_error_count
    FROM mmo_projection_validation_results
   WHERE validation_run_id = v_run_id
     AND severity = 'error';

  UPDATE mmo_projection_validation_runs
     SET status = IF(v_error_count = 0, 'passed', 'failed'),
         finished_at = CURRENT_TIMESTAMP(6),
         error_count = v_error_count,
         warning_count = (
           SELECT COUNT(*)
             FROM mmo_projection_validation_results
            WHERE validation_run_id = v_run_id
              AND severity = 'warning'
         )
   WHERE validation_run_id = v_run_id;

  INSERT INTO world_projection_offsets(projection_name, world_instance_id, last_event_seq)
  VALUES('basic_projection_validator', p_world_instance_id, v_max_event_seq)
  ON DUPLICATE KEY UPDATE
    last_event_seq = GREATEST(last_event_seq, VALUES(last_event_seq)),
    updated_at = CURRENT_TIMESTAMP(6);

  SET p_validation_run_id = v_run_id;
  SET p_error_count = v_error_count;
  COMMIT;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_projection_validation_latest AS
SELECT
  BIN_TO_UUID(r.validation_run_id, 1) AS validation_run_id,
  BIN_TO_UUID(r.world_instance_id, 1) AS world_instance_id,
  r.run_key,
  r.run_kind,
  r.status,
  r.started_at,
  r.finished_at,
  r.max_event_seq,
  r.error_count,
  r.warning_count,
  r.metadata
FROM mmo_projection_validation_runs r
JOIN (
  SELECT world_instance_id, MAX(started_at) AS latest_started_at
    FROM mmo_projection_validation_runs
   GROUP BY world_instance_id
) latest ON latest.world_instance_id = r.world_instance_id AND latest.latest_started_at = r.started_at;

CREATE OR REPLACE VIEW v_projection_validation_results AS
SELECT
  BIN_TO_UUID(r.validation_run_id, 1) AS validation_run_id,
  BIN_TO_UUID(run.world_instance_id, 1) AS world_instance_id,
  run.run_key,
  r.check_name,
  r.severity,
  r.checked_count,
  r.problem_count,
  r.details,
  r.created_at
FROM mmo_projection_validation_results r
JOIN mmo_projection_validation_runs run ON run.validation_run_id = r.validation_run_id;

CREATE OR REPLACE VIEW v_projection_validation_errors AS
SELECT *
FROM v_projection_validation_results
WHERE severity = 'error';

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES(
  'production/mysql/010_projection_validation',
  'gothic-mmo-projection-validation-v1-mysql',
  'Adds basic projection validation runs/results and consistency validator procedure.'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);
