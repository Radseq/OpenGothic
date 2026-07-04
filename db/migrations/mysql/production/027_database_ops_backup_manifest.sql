-- Gothic MMO MySQL production migration 027.
-- Database backup/export/retention manifests for production operations evidence.
-- Requires 001..026 MySQL production migrations.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE IF NOT EXISTS mmo_database_backup_manifests (
  backup_manifest_id BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  world_instance_id  BINARY(16) NULL,
  manifest_key       VARCHAR(191) NOT NULL,
  backup_kind        VARCHAR(32) NOT NULL,
  storage_uri        VARCHAR(1024) NULL,
  backup_hash        CHAR(64) NULL,
  max_event_seq      BIGINT NOT NULL DEFAULT 0,
  table_count        INT NOT NULL DEFAULT 0,
  row_count          BIGINT NOT NULL DEFAULT 0,
  status             VARCHAR(32) NOT NULL DEFAULT 'recorded',
  metadata           JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at         TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY mmo_database_backup_manifests_key_uk(manifest_key),
  KEY ix_mmo_database_backup_manifests_world(world_instance_id, created_at),
  CONSTRAINT mmo_database_backup_manifests_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE SET NULL,
  CONSTRAINT mmo_database_backup_manifests_kind_ck CHECK(backup_kind IN ('logical_dump','physical_snapshot','schema_export','projection_export','event_journal_export','diagnostic')),
  CONSTRAINT mmo_database_backup_manifests_status_ck CHECK(status IN ('recorded','verified','failed','expired')),
  CONSTRAINT mmo_database_backup_manifests_hash_ck CHECK(backup_hash IS NULL OR CHAR_LENGTH(backup_hash)=64),
  CONSTRAINT mmo_database_backup_manifests_counts_ck CHECK(max_event_seq >= 0 AND table_count >= 0 AND row_count >= 0),
  CONSTRAINT mmo_database_backup_manifests_metadata_json_ck CHECK(JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_database_retention_policies (
  policy_key        VARCHAR(191) PRIMARY KEY,
  area              VARCHAR(64) NOT NULL,
  retention_days    INT NULL,
  policy_state      VARCHAR(32) NOT NULL DEFAULT 'planned',
  definition        JSON NOT NULL DEFAULT (JSON_OBJECT()),
  updated_at        TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  CONSTRAINT mmo_database_retention_policies_days_ck CHECK(retention_days IS NULL OR retention_days >= 0),
  CONSTRAINT mmo_database_retention_policies_state_ck CHECK(policy_state IN ('planned','active','disabled')),
  CONSTRAINT mmo_database_retention_policies_json_ck CHECK(JSON_VALID(definition))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

INSERT INTO mmo_database_retention_policies(policy_key, area, retention_days, policy_state, definition)
VALUES
  ('world_event_journal', 'journal', NULL, 'active', JSON_OBJECT('rule','append-only durable source of truth; do not delete without archive and replay checkpoint')),
  ('mmo_server_action_outbox_applied', 'outbox', 90, 'planned', JSON_OBJECT('rule','applied outbox rows can be archived after audit window, not deleted blindly')),
  ('mmo_server_action_worker_runs', 'ops', 180, 'planned', JSON_OBJECT('rule','worker telemetry can be compacted after observability export')),
  ('mmo_projection_validation_runs', 'validation', 180, 'planned', JSON_OBJECT('rule','keep failed validation runs longer than passed smoke runs')),
  ('mmo_restore_parity_artifacts', 'parity', NULL, 'active', JSON_OBJECT('rule','parity proof is release evidence; retain with release/build metadata')),
  ('mmo_database_backup_manifests', 'ops', NULL, 'active', JSON_OBJECT('rule','backup manifests are durable audit metadata'))
ON DUPLICATE KEY UPDATE area=VALUES(area), retention_days=VALUES(retention_days), policy_state=VALUES(policy_state), definition=VALUES(definition), updated_at=CURRENT_TIMESTAMP(6);

DROP PROCEDURE IF EXISTS mmo_record_database_backup_manifest;
DELIMITER $$
CREATE PROCEDURE mmo_record_database_backup_manifest(
  IN  p_world_instance_id BINARY(16),
  IN  p_manifest_key      VARCHAR(191),
  IN  p_backup_kind       VARCHAR(32),
  IN  p_storage_uri       VARCHAR(1024),
  IN  p_backup_hash       CHAR(64),
  IN  p_table_count       INT,
  IN  p_row_count         BIGINT,
  IN  p_status            VARCHAR(32),
  IN  p_metadata          JSON,
  OUT p_backup_manifest_id BINARY(16)
)
BEGIN
  DECLARE v_max_event_seq BIGINT DEFAULT 0;

  IF p_manifest_key IS NULL OR TRIM(p_manifest_key)='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='manifest_key is required'; END IF;
  IF p_backup_kind NOT IN ('logical_dump','physical_snapshot','schema_export','projection_export','event_journal_export','diagnostic') THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='invalid backup_kind'; END IF;
  IF p_status IS NOT NULL AND p_status NOT IN ('recorded','verified','failed','expired') THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='invalid backup status'; END IF;
  IF p_backup_hash IS NOT NULL AND p_backup_hash NOT REGEXP '^[0-9a-fA-F]{64}$' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='backup_hash must be sha256 hex or NULL'; END IF;

  IF p_world_instance_id IS NOT NULL THEN
    SELECT COALESCE(MAX(event_seq),0) INTO v_max_event_seq FROM world_event_journal WHERE world_instance_id=p_world_instance_id;
  ELSE
    SELECT COALESCE(MAX(event_seq),0) INTO v_max_event_seq FROM world_event_journal;
  END IF;

  INSERT INTO mmo_database_backup_manifests(world_instance_id, manifest_key, backup_kind, storage_uri, backup_hash, max_event_seq, table_count, row_count, status, metadata)
  VALUES(p_world_instance_id, p_manifest_key, p_backup_kind, p_storage_uri, LOWER(p_backup_hash), v_max_event_seq, COALESCE(p_table_count,0), COALESCE(p_row_count,0), COALESCE(p_status,'recorded'), COALESCE(p_metadata,JSON_OBJECT()))
  ON DUPLICATE KEY UPDATE
    world_instance_id=VALUES(world_instance_id), backup_kind=VALUES(backup_kind), storage_uri=VALUES(storage_uri), backup_hash=VALUES(backup_hash), max_event_seq=VALUES(max_event_seq),
    table_count=VALUES(table_count), row_count=VALUES(row_count), status=VALUES(status), metadata=VALUES(metadata), created_at=CURRENT_TIMESTAMP(6);

  SELECT backup_manifest_id INTO p_backup_manifest_id FROM mmo_database_backup_manifests WHERE manifest_key=p_manifest_key LIMIT 1;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_database_backup_manifests AS
SELECT BIN_TO_UUID(backup_manifest_id,1) AS backup_manifest_uuid,
       BIN_TO_UUID(world_instance_id,1) AS world_instance_uuid,
       manifest_key,
       backup_kind,
       storage_uri,
       backup_hash,
       max_event_seq,
       table_count,
       row_count,
       status,
       metadata,
       created_at
FROM mmo_database_backup_manifests;

CREATE OR REPLACE VIEW v_database_ops_dashboard AS
SELECT 'backup_manifests' AS area, COUNT(*) AS total_count, SUM(status='verified') AS green_count, SUM(status IN ('failed','expired')) AS problem_count FROM mmo_database_backup_manifests
UNION ALL SELECT 'retention_policies', COUNT(*), SUM(policy_state='active'), SUM(policy_state='disabled') FROM mmo_database_retention_policies
UNION ALL SELECT 'outbox_failed_dead_letters', COUNT(*), 0, COUNT(*) FROM mmo_server_action_outbox WHERE status IN ('failed','dead_letter');

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES('production/mysql/027_database_ops_backup_manifest', 'gothic-mmo-database-ops-backup-manifest-v1-mysql', 'Production operations metadata: backup/export manifests, retention policy registry and ops dashboard. Does not delete gameplay truth.')
ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes), applied_at=CURRENT_TIMESTAMP(6);
