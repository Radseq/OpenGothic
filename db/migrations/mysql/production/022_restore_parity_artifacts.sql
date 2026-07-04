-- Gothic MMO MySQL production migration 022.
-- Restore parity artifact hashes: native .sav / SQLite save-slot / MySQL projection evidence.
-- Requires 001..021 MySQL production migrations.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE IF NOT EXISTS mmo_restore_parity_artifacts (
  parity_artifact_id BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  parity_run_id      BINARY(16) NOT NULL,
  scenario_key       VARCHAR(191) NOT NULL,
  artifact_kind      VARCHAR(32) NOT NULL,
  artifact_key       VARCHAR(191) NOT NULL,
  artifact_hash      CHAR(64) NOT NULL,
  row_count          BIGINT NULL,
  payload_summary    JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at         TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY mmo_restore_parity_artifacts_uk(parity_run_id, scenario_key, artifact_kind, artifact_key),
  KEY ix_mmo_restore_parity_artifacts_run_scenario(parity_run_id, scenario_key),
  CONSTRAINT mmo_restore_parity_artifacts_run_fk FOREIGN KEY(parity_run_id) REFERENCES mmo_restore_parity_runs(parity_run_id) ON DELETE CASCADE,
  CONSTRAINT mmo_restore_parity_artifacts_kind_ck CHECK(artifact_kind IN ('native_sav','sqlite_snapshot','mysql_projection','runtime_sqlite','diagnostic')),
  CONSTRAINT mmo_restore_parity_artifacts_hash_len_ck CHECK(CHAR_LENGTH(artifact_hash)=64),
  CONSTRAINT mmo_restore_parity_artifacts_summary_json_ck CHECK(JSON_VALID(payload_summary))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_record_restore_parity_artifact;
DELIMITER $$
CREATE PROCEDURE mmo_record_restore_parity_artifact(
  IN p_parity_run_id    BINARY(16),
  IN p_scenario_key     VARCHAR(191),
  IN p_artifact_kind    VARCHAR(32),
  IN p_artifact_key     VARCHAR(191),
  IN p_artifact_hash    CHAR(64),
  IN p_row_count        BIGINT,
  IN p_payload_summary  JSON
)
BEGIN
  IF p_parity_run_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='parity_run_id is required'; END IF;
  IF p_scenario_key IS NULL OR TRIM(p_scenario_key)='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='scenario_key is required'; END IF;
  IF p_artifact_kind NOT IN ('native_sav','sqlite_snapshot','mysql_projection','runtime_sqlite','diagnostic') THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='invalid artifact_kind'; END IF;
  IF p_artifact_key IS NULL OR TRIM(p_artifact_key)='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='artifact_key is required'; END IF;
  IF p_artifact_hash IS NULL OR p_artifact_hash NOT REGEXP '^[0-9a-fA-F]{64}$' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='artifact_hash must be a sha256 hex string'; END IF;

  INSERT INTO mmo_restore_parity_artifacts(parity_run_id, scenario_key, artifact_kind, artifact_key, artifact_hash, row_count, payload_summary)
  VALUES(p_parity_run_id, p_scenario_key, p_artifact_kind, p_artifact_key, LOWER(p_artifact_hash), p_row_count, COALESCE(p_payload_summary,JSON_OBJECT()))
  ON DUPLICATE KEY UPDATE
    artifact_hash=VALUES(artifact_hash),
    row_count=VALUES(row_count),
    payload_summary=VALUES(payload_summary),
    created_at=CURRENT_TIMESTAMP(6);
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_restore_parity_artifact_comparison AS
SELECT BIN_TO_UUID(a.parity_run_id,1) AS parity_run_uuid,
       a.parity_run_id,
       a.scenario_key,
       MAX(CASE WHEN a.artifact_kind='native_sav' THEN a.artifact_hash END) AS native_hash,
       MAX(CASE WHEN a.artifact_kind IN ('sqlite_snapshot','runtime_sqlite') THEN a.artifact_hash END) AS sqlite_hash,
       MAX(CASE WHEN a.artifact_kind='mysql_projection' THEN a.artifact_hash END) AS mysql_hash,
       COUNT(*) AS artifact_count,
       CASE
         WHEN MAX(CASE WHEN a.artifact_kind='native_sav' THEN a.artifact_hash END) IS NULL
           OR MAX(CASE WHEN a.artifact_kind IN ('sqlite_snapshot','runtime_sqlite') THEN a.artifact_hash END) IS NULL
           OR MAX(CASE WHEN a.artifact_kind='mysql_projection' THEN a.artifact_hash END) IS NULL THEN 'blocked'
         WHEN MAX(CASE WHEN a.artifact_kind='native_sav' THEN a.artifact_hash END) = MAX(CASE WHEN a.artifact_kind IN ('sqlite_snapshot','runtime_sqlite') THEN a.artifact_hash END)
          AND MAX(CASE WHEN a.artifact_kind='native_sav' THEN a.artifact_hash END) = MAX(CASE WHEN a.artifact_kind='mysql_projection' THEN a.artifact_hash END) THEN 'passed'
         ELSE 'failed'
       END AS comparison_status
FROM mmo_restore_parity_artifacts a
GROUP BY a.parity_run_id, BIN_TO_UUID(a.parity_run_id,1), a.scenario_key;

DROP PROCEDURE IF EXISTS mmo_materialize_restore_parity_artifact_results;
DELIMITER $$
CREATE PROCEDURE mmo_materialize_restore_parity_artifact_results(
  IN  p_parity_run_id      BINARY(16),
  OUT p_materialized_count INT,
  OUT p_failed_count       INT
)
BEGIN
  IF p_parity_run_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='parity_run_id is required'; END IF;

  INSERT INTO mmo_restore_parity_results(parity_run_id, scenario_key, status, native_hash, sqlite_hash, mysql_hash, details)
  SELECT c.parity_run_id,
         c.scenario_key,
         c.comparison_status,
         c.native_hash,
         c.sqlite_hash,
         c.mysql_hash,
         JSON_OBJECT('artifact_count', c.artifact_count, 'source', 'mmo_restore_parity_artifacts')
    FROM v_restore_parity_artifact_comparison c
   WHERE c.parity_run_id=p_parity_run_id
  ON DUPLICATE KEY UPDATE
    status=VALUES(status),
    native_hash=VALUES(native_hash),
    sqlite_hash=VALUES(sqlite_hash),
    mysql_hash=VALUES(mysql_hash),
    details=VALUES(details),
    created_at=CURRENT_TIMESTAMP(6);

  SET p_materialized_count = ROW_COUNT();
  SELECT COUNT(*) INTO p_failed_count FROM mmo_restore_parity_results WHERE parity_run_id=p_parity_run_id AND status='failed';
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_restore_parity_artifact_failures AS
SELECT c.parity_run_uuid,
       c.scenario_key,
       c.native_hash,
       c.sqlite_hash,
       c.mysql_hash,
       c.artifact_count,
       c.comparison_status
FROM v_restore_parity_artifact_comparison c
WHERE c.comparison_status IN ('failed','blocked')
ORDER BY c.scenario_key;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES('production/mysql/022_restore_parity_artifacts', 'gothic-mmo-restore-parity-artifacts-v1-mysql', 'Restore parity artifact hash storage and comparison views for native .sav, SQLite save-slot/runtime and MySQL projection evidence.')
ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes), applied_at=CURRENT_TIMESTAMP(6);
