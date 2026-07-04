-- Gothic MMO MySQL bootstrap import/audit layer.
-- Apply after production/mysql/001_gothic_mmo_production_schema.sql.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';

CREATE TABLE IF NOT EXISTS mmo_import_runs (
  import_run_id          BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  source_system          VARCHAR(64) NOT NULL,
  source_path            TEXT NOT NULL,
  source_fingerprint     CHAR(64) NOT NULL,
  source_schema_name     VARCHAR(128) NOT NULL DEFAULT '',
  source_schema_version  INT NULL,
  import_mode            VARCHAR(32) NOT NULL DEFAULT 'bootstrap',
  game_code              VARCHAR(32) NOT NULL,
  content_revision_id    BINARY(16) NULL,
  status                 VARCHAR(32) NOT NULL DEFAULT 'started',
  counters               JSON NOT NULL DEFAULT (JSON_OBJECT()),
  diagnostics            JSON NOT NULL DEFAULT (JSON_OBJECT()),
  started_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  finished_at            TIMESTAMP(6) NULL,
  UNIQUE KEY ux_mmo_import_runs_fingerprint_mode(source_fingerprint, import_mode, game_code),
  KEY ix_mmo_import_runs_status(status, started_at),
  CONSTRAINT mmo_import_runs_revision_fk FOREIGN KEY(content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE SET NULL,
  CONSTRAINT mmo_import_runs_status_ck CHECK(status IN ('started','finished','failed','aborted')),
  CONSTRAINT mmo_import_runs_counters_json_ck CHECK(JSON_VALID(counters)),
  CONSTRAINT mmo_import_runs_diagnostics_json_ck CHECK(JSON_VALID(diagnostics))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_import_object_map (
  import_run_id          BINARY(16) NOT NULL,
  source_table           VARCHAR(128) NOT NULL,
  source_key             VARCHAR(191) NOT NULL,
  target_table           VARCHAR(128) NOT NULL,
  target_key             VARCHAR(191) NOT NULL,
  raw_hash               CHAR(64) NOT NULL,
  created_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(import_run_id, source_table, source_key, target_table),
  KEY ix_mmo_import_object_map_target(target_table, target_key),
  CONSTRAINT mmo_import_object_map_run_fk FOREIGN KEY(import_run_id) REFERENCES mmo_import_runs(import_run_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_import_validation_results (
  import_run_id          BINARY(16) NOT NULL,
  validation_key         VARCHAR(191) NOT NULL,
  severity               VARCHAR(16) NOT NULL,
  status                 VARCHAR(16) NOT NULL,
  expected_value         VARCHAR(191) NULL,
  actual_value           VARCHAR(191) NULL,
  details                JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(import_run_id, validation_key),
  CONSTRAINT mmo_import_validation_run_fk FOREIGN KEY(import_run_id) REFERENCES mmo_import_runs(import_run_id) ON DELETE CASCADE,
  CONSTRAINT mmo_import_validation_severity_ck CHECK(severity IN ('info','warn','error')),
  CONSTRAINT mmo_import_validation_status_ck CHECK(status IN ('ok','fail','skipped')),
  CONSTRAINT mmo_import_validation_details_json_ck CHECK(JSON_VALID(details))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_mark_import_finished;
DELIMITER $$
CREATE PROCEDURE mmo_mark_import_finished(
  IN p_import_run_id BINARY(16),
  IN p_status VARCHAR(32),
  IN p_counters JSON,
  IN p_diagnostics JSON
)
BEGIN
  UPDATE mmo_import_runs
     SET status = p_status,
         counters = COALESCE(p_counters, counters),
         diagnostics = COALESCE(p_diagnostics, diagnostics),
         finished_at = CURRENT_TIMESTAMP(6)
   WHERE import_run_id = p_import_run_id;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_mmo_import_runs AS
SELECT
  BIN_TO_UUID(ir.import_run_id, 1) AS import_run_id,
  ir.source_system,
  ir.source_path,
  ir.source_fingerprint,
  ir.source_schema_name,
  ir.source_schema_version,
  ir.import_mode,
  ir.game_code,
  cr.content_revision_key,
  ir.status,
  ir.counters,
  ir.started_at,
  ir.finished_at
FROM mmo_import_runs ir
LEFT JOIN content_revisions cr ON cr.content_revision_id = ir.content_revision_id;

CREATE OR REPLACE VIEW v_mmo_import_validation_errors AS
SELECT
  BIN_TO_UUID(import_run_id, 1) AS import_run_id,
  validation_key,
  severity,
  status,
  expected_value,
  actual_value,
  details,
  created_at
FROM mmo_import_validation_results
WHERE severity = 'error' OR status = 'fail';

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES (
  'production/mysql/002_bootstrap_import_pipeline',
  'gothic-mmo-bootstrap-import-v1-mysql',
  'MySQL bootstrap import audit metadata and validation surfaces for runtime SQLite migration.'
)
ON DUPLICATE KEY UPDATE
  applied_at = CURRENT_TIMESTAMP(6),
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes);
