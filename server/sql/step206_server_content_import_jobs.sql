-- Step206: content import job queue for future ZEN/DAT/OU importers.
-- Earlier steps identify content files and archive mount state. This step
-- creates durable import jobs so the build pipeline/server can process content
-- deterministically and expose progress through DB health views.

CREATE TABLE IF NOT EXISTS mmo_server_content_import_jobs (
  content_import_job_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_revision_id BINARY(16) NOT NULL,
  importer_key VARCHAR(96) NOT NULL,
  source_kind VARCHAR(48) NOT NULL,
  source_logical_path VARCHAR(512) NOT NULL,
  source_sha256 CHAR(64) NULL,
  job_priority INT NOT NULL DEFAULT 1000,
  job_status VARCHAR(32) NOT NULL DEFAULT 'queued',
  attempt_count INT NOT NULL DEFAULT 0,
  input_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  output_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  error_text TEXT NULL,
  lease_owner VARCHAR(191) NULL,
  lease_until TIMESTAMP(6) NULL DEFAULT NULL,
  started_at TIMESTAMP(6) NULL DEFAULT NULL,
  finished_at TIMESTAMP(6) NULL DEFAULT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (content_import_job_id),
  UNIQUE KEY mmo_content_import_job_source_uk (content_revision_id, importer_key, source_logical_path, source_sha256),
  KEY ix_mmo_content_import_job_status (content_revision_id, job_status, job_priority),
  KEY ix_mmo_content_import_job_importer (content_revision_id, importer_key),
  KEY ix_mmo_content_import_job_source (content_revision_id, source_kind, source_logical_path),
  CONSTRAINT mmo_content_import_job_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_content_import_job_priority_ck CHECK (job_priority >= 0),
  CONSTRAINT mmo_content_import_job_attempt_ck CHECK (attempt_count >= 0),
  CONSTRAINT mmo_content_import_job_sha_ck CHECK (source_sha256 IS NULL OR REGEXP_LIKE(source_sha256, '^[0-9a-f]{64}$')),
  CONSTRAINT mmo_content_import_job_status_ck CHECK (job_status IN (
    'queued', 'running', 'succeeded', 'failed', 'skipped', 'blocked'
  )),
  CONSTRAINT mmo_content_import_job_source_kind_ck CHECK (source_kind IN (
    'archive', 'world_zen', 'scripts_dat', 'dialog_ou', 'script_source', 'config_ini', 'asset', 'other'
  )),
  CONSTRAINT mmo_content_import_job_input_json_ck CHECK (JSON_VALID(input_payload)),
  CONSTRAINT mmo_content_import_job_output_json_ck CHECK (JSON_VALID(output_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP VIEW IF EXISTS v_mmo_server_content_import_job_health;
DROP VIEW IF EXISTS v_mmo_server_content_import_jobs;

CREATE VIEW v_mmo_server_content_import_jobs AS
SELECT
  BIN_TO_UUID(j.content_import_job_id, 1) AS content_import_job_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  j.importer_key,
  j.source_kind,
  j.source_logical_path,
  j.source_sha256,
  j.job_priority,
  j.job_status,
  j.attempt_count,
  j.input_payload,
  j.output_payload,
  j.error_text,
  j.lease_owner,
  j.lease_until,
  j.started_at,
  j.finished_at,
  j.created_at,
  j.updated_at
FROM mmo_server_content_import_jobs j
JOIN content_revisions cr ON cr.content_revision_id = j.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id;

CREATE VIEW v_mmo_server_content_import_job_health AS
SELECT
  cr.content_revision_key,
  cr.is_active,
  COUNT(j.content_import_job_id) AS total_jobs,
  SUM(CASE WHEN j.job_status = 'queued' THEN 1 ELSE 0 END) AS queued_count,
  SUM(CASE WHEN j.job_status = 'running' THEN 1 ELSE 0 END) AS running_count,
  SUM(CASE WHEN j.job_status = 'succeeded' THEN 1 ELSE 0 END) AS succeeded_count,
  SUM(CASE WHEN j.job_status = 'failed' THEN 1 ELSE 0 END) AS failed_count,
  SUM(CASE WHEN j.job_status = 'blocked' THEN 1 ELSE 0 END) AS blocked_count,
  SUM(CASE WHEN j.source_kind = 'archive' THEN 1 ELSE 0 END) AS archive_jobs,
  SUM(CASE WHEN j.source_kind = 'world_zen' THEN 1 ELSE 0 END) AS world_zen_jobs,
  SUM(CASE WHEN j.source_kind = 'scripts_dat' THEN 1 ELSE 0 END) AS scripts_dat_jobs,
  SUM(CASE WHEN j.source_kind = 'dialog_ou' THEN 1 ELSE 0 END) AS dialog_ou_jobs,
  MAX(j.updated_at) AS last_job_update_at
FROM content_revisions cr
LEFT JOIN mmo_server_content_import_jobs j ON j.content_revision_id = cr.content_revision_id
GROUP BY cr.content_revision_key, cr.is_active;

DELIMITER ;;

DROP PROCEDURE IF EXISTS mmo_enqueue_server_content_import_job;;
CREATE PROCEDURE mmo_enqueue_server_content_import_job(
  IN p_content_revision_key VARCHAR(191),
  IN p_importer_key VARCHAR(96),
  IN p_source_kind VARCHAR(48),
  IN p_source_logical_path VARCHAR(512),
  IN p_source_sha256 CHAR(64),
  IN p_job_priority INT,
  IN p_job_status VARCHAR(32),
  IN p_input_payload JSON,
  OUT o_content_import_job_id BINARY(16)
)
proc: BEGIN
  DECLARE v_content_revision_id BINARY(16) DEFAULT NULL;
  DECLARE v_source_kind VARCHAR(48) DEFAULT 'other';
  DECLARE v_job_status VARCHAR(32) DEFAULT 'queued';
  DECLARE v_source_logical_path VARCHAR(512) DEFAULT '';
  DECLARE CONTINUE HANDLER FOR NOT FOUND BEGIN END;

  SET o_content_import_job_id = NULL;
  SET v_source_logical_path = LOWER(REPLACE(COALESCE(p_source_logical_path, ''), '\\', '/'));

  SELECT content_revision_id
    INTO v_content_revision_id
    FROM content_revisions
   WHERE content_revision_key = p_content_revision_key
   LIMIT 1;
  IF v_content_revision_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_enqueue_server_content_import_job: content revision not found';
  END IF;

  IF COALESCE(TRIM(p_importer_key), '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_enqueue_server_content_import_job: importer key is required';
  END IF;
  IF COALESCE(TRIM(v_source_logical_path), '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_enqueue_server_content_import_job: source logical path is required';
  END IF;
  IF p_source_sha256 IS NOT NULL AND p_source_sha256 NOT REGEXP '^[0-9a-f]{64}$' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_enqueue_server_content_import_job: source sha must be lowercase hex';
  END IF;

  SET v_source_kind = COALESCE(NULLIF(p_source_kind, ''), 'other');
  IF v_source_kind NOT IN ('archive', 'world_zen', 'scripts_dat', 'dialog_ou', 'script_source', 'config_ini', 'asset', 'other') THEN
    SET v_source_kind = 'other';
  END IF;

  SET v_job_status = COALESCE(NULLIF(p_job_status, ''), 'queued');
  IF v_job_status NOT IN ('queued', 'running', 'succeeded', 'failed', 'skipped', 'blocked') THEN
    SET v_job_status = 'queued';
  END IF;

  INSERT INTO mmo_server_content_import_jobs(
    content_revision_id,
    importer_key,
    source_kind,
    source_logical_path,
    source_sha256,
    job_priority,
    job_status,
    input_payload
  )
  VALUES (
    v_content_revision_id,
    p_importer_key,
    v_source_kind,
    v_source_logical_path,
    LOWER(NULLIF(p_source_sha256, '')),
    COALESCE(p_job_priority, 1000),
    v_job_status,
    COALESCE(p_input_payload, JSON_OBJECT())
  )
  ON DUPLICATE KEY UPDATE
    source_kind = VALUES(source_kind),
    job_priority = VALUES(job_priority),
    job_status = CASE
      WHEN mmo_server_content_import_jobs.job_status IN ('succeeded', 'running')
        THEN mmo_server_content_import_jobs.job_status
      ELSE VALUES(job_status)
    END,
    input_payload = VALUES(input_payload),
    updated_at = CURRENT_TIMESTAMP(6);

  SELECT content_import_job_id
    INTO o_content_import_job_id
    FROM mmo_server_content_import_jobs
   WHERE content_revision_id = v_content_revision_id
     AND importer_key = p_importer_key
     AND source_logical_path = v_source_logical_path
     AND ((source_sha256 IS NULL AND p_source_sha256 IS NULL) OR source_sha256 = LOWER(NULLIF(p_source_sha256, '')))
   LIMIT 1;
END;;

DELIMITER ;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES (
  'server/sql/step206_server_content_import_jobs.sql',
  'step206_server_content_import_jobs_v1',
  'Step206: server content import job queue for future ZEN/DAT/OU importers'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);
