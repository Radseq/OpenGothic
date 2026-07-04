-- Gothic MMO MySQL production migration 020.
-- Server action worker observability: worker runs/results for the outbox dispatcher.
-- Requires 001..019 MySQL production migrations.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE IF NOT EXISTS mmo_server_action_worker_runs (
  worker_run_id      BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  worker_id          VARCHAR(128) NOT NULL,
  run_key            VARCHAR(191) NOT NULL,
  mode               VARCHAR(32) NOT NULL DEFAULT 'dev_mysql_cli',
  status             VARCHAR(32) NOT NULL DEFAULT 'running',
  started_at         TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  finished_at        TIMESTAMP(6) NULL,
  claimed_count      INT NOT NULL DEFAULT 0,
  applied_count      INT NOT NULL DEFAULT 0,
  failed_count       INT NOT NULL DEFAULT 0,
  dead_letter_count  INT NOT NULL DEFAULT 0,
  metadata           JSON NOT NULL DEFAULT (JSON_OBJECT()),
  UNIQUE KEY mmo_server_action_worker_runs_key_uk(run_key),
  KEY ix_mmo_server_action_worker_runs_started(worker_id, started_at),
  CONSTRAINT mmo_server_action_worker_runs_status_ck CHECK(status IN ('running','finished','failed')),
  CONSTRAINT mmo_server_action_worker_runs_counts_ck CHECK(claimed_count >= 0 AND applied_count >= 0 AND failed_count >= 0 AND dead_letter_count >= 0),
  CONSTRAINT mmo_server_action_worker_runs_metadata_json_ck CHECK(JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_server_action_worker_results (
  worker_result_id   BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  worker_run_id      BINARY(16) NOT NULL,
  action_id          BINARY(16) NULL,
  action_kind        VARCHAR(128) NOT NULL,
  status             VARCHAR(32) NOT NULL,
  event_id           BINARY(16) NULL,
  error_code         VARCHAR(64) NULL,
  error_message      TEXT NULL,
  details            JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at         TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  KEY ix_mmo_server_action_worker_results_run(worker_run_id, created_at),
  KEY ix_mmo_server_action_worker_results_action(action_id),
  KEY ix_mmo_server_action_worker_results_status(status, created_at),
  CONSTRAINT mmo_server_action_worker_results_run_fk FOREIGN KEY(worker_run_id) REFERENCES mmo_server_action_worker_runs(worker_run_id) ON DELETE CASCADE,
  CONSTRAINT mmo_server_action_worker_results_action_fk FOREIGN KEY(action_id) REFERENCES mmo_server_action_outbox(action_id) ON DELETE SET NULL,
  CONSTRAINT mmo_server_action_worker_results_status_ck CHECK(status IN ('claimed','applied','failed','dead_letter','skipped')),
  CONSTRAINT mmo_server_action_worker_results_details_json_ck CHECK(JSON_VALID(details))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_start_server_action_worker_run;
DELIMITER $$
CREATE PROCEDURE mmo_start_server_action_worker_run(
  IN  p_worker_id      VARCHAR(128),
  IN  p_run_key        VARCHAR(191),
  IN  p_mode           VARCHAR(32),
  IN  p_metadata       JSON,
  OUT p_worker_run_id  BINARY(16)
)
BEGIN
  IF p_worker_id IS NULL OR TRIM(p_worker_id)='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='worker_id is required'; END IF;
  IF p_run_key IS NULL OR TRIM(p_run_key)='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='run_key is required'; END IF;

  INSERT INTO mmo_server_action_worker_runs(worker_id, run_key, mode, status, metadata)
  VALUES(p_worker_id, p_run_key, COALESCE(NULLIF(TRIM(p_mode),''),'dev_mysql_cli'), 'running', COALESCE(p_metadata,JSON_OBJECT()))
  ON DUPLICATE KEY UPDATE
    worker_id=VALUES(worker_id),
    mode=VALUES(mode),
    status='running',
    started_at=CURRENT_TIMESTAMP(6),
    finished_at=NULL,
    claimed_count=0,
    applied_count=0,
    failed_count=0,
    dead_letter_count=0,
    metadata=VALUES(metadata);

  SELECT worker_run_id INTO p_worker_run_id FROM mmo_server_action_worker_runs WHERE run_key=p_run_key LIMIT 1;
  DELETE FROM mmo_server_action_worker_results WHERE worker_run_id=p_worker_run_id;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_record_server_action_worker_result;
DELIMITER $$
CREATE PROCEDURE mmo_record_server_action_worker_result(
  IN p_worker_run_id  BINARY(16),
  IN p_action_id      BINARY(16),
  IN p_action_kind    VARCHAR(128),
  IN p_status         VARCHAR(32),
  IN p_event_id       BINARY(16),
  IN p_error_code     VARCHAR(64),
  IN p_error_message  TEXT,
  IN p_details        JSON
)
BEGIN
  IF p_worker_run_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='worker_run_id is required'; END IF;
  IF p_action_kind IS NULL OR TRIM(p_action_kind)='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='action_kind is required'; END IF;
  IF p_status NOT IN ('claimed','applied','failed','dead_letter','skipped') THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='invalid worker result status'; END IF;

  INSERT INTO mmo_server_action_worker_results(worker_run_id, action_id, action_kind, status, event_id, error_code, error_message, details)
  VALUES(p_worker_run_id, p_action_id, p_action_kind, p_status, p_event_id, p_error_code, p_error_message, COALESCE(p_details,JSON_OBJECT()));
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_finish_server_action_worker_run;
DELIMITER $$
CREATE PROCEDURE mmo_finish_server_action_worker_run(
  IN  p_worker_run_id BINARY(16),
  IN  p_failed        BOOLEAN,
  OUT p_status        VARCHAR(32),
  OUT p_applied_count INT
)
BEGIN
  DECLARE v_claimed INT DEFAULT 0;
  DECLARE v_applied INT DEFAULT 0;
  DECLARE v_failed INT DEFAULT 0;
  DECLARE v_dead INT DEFAULT 0;

  IF p_worker_run_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='worker_run_id is required'; END IF;

  SELECT SUM(CASE WHEN status='claimed' THEN 1 ELSE 0 END),
         SUM(CASE WHEN status='applied' THEN 1 ELSE 0 END),
         SUM(CASE WHEN status='failed' THEN 1 ELSE 0 END),
         SUM(CASE WHEN status='dead_letter' THEN 1 ELSE 0 END)
    INTO v_claimed, v_applied, v_failed, v_dead
    FROM mmo_server_action_worker_results
   WHERE worker_run_id=p_worker_run_id;

  SET v_claimed=COALESCE(v_claimed,0);
  SET v_applied=COALESCE(v_applied,0);
  SET v_failed=COALESCE(v_failed,0);
  SET v_dead=COALESCE(v_dead,0);
  SET p_status=IF(COALESCE(p_failed,FALSE) OR v_failed+v_dead > 0, 'failed', 'finished');
  SET p_applied_count=v_applied;

  UPDATE mmo_server_action_worker_runs
     SET status=p_status,
         finished_at=CURRENT_TIMESTAMP(6),
         claimed_count=v_claimed,
         applied_count=v_applied,
         failed_count=v_failed,
         dead_letter_count=v_dead
   WHERE worker_run_id=p_worker_run_id;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_server_action_worker_latest_runs AS
SELECT BIN_TO_UUID(worker_run_id,1) AS worker_run_uuid,
       worker_id,
       run_key,
       mode,
       status,
       claimed_count,
       applied_count,
       failed_count,
       dead_letter_count,
       started_at,
       finished_at
FROM mmo_server_action_worker_runs
ORDER BY started_at DESC;

CREATE OR REPLACE VIEW v_server_action_worker_latest_results AS
SELECT BIN_TO_UUID(r.worker_run_id,1) AS worker_run_uuid,
       w.run_key,
       w.worker_id,
       BIN_TO_UUID(r.action_id,1) AS action_uuid,
       r.action_kind,
       r.status,
       BIN_TO_UUID(r.event_id,1) AS event_uuid,
       r.error_code,
       r.error_message,
       r.details,
       r.created_at
FROM mmo_server_action_worker_results r
JOIN mmo_server_action_worker_runs w ON w.worker_run_id=r.worker_run_id
ORDER BY r.created_at DESC;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES('production/mysql/020_server_action_worker_observability', 'gothic-mmo-server-action-worker-observability-v1-mysql', 'Worker run/result telemetry for the outbox action dispatcher and thin MySQL dev adapter.')
ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes), applied_at=CURRENT_TIMESTAMP(6);
