-- Step203: server-side archive mount/pre-extract registry.
-- Step201 identifies VDF/MOD files as archive inputs. This step records the
-- server-side mount/extract decision and, optionally, files discovered after
-- extraction. Future ZEN/DAT/OU importers should read from this registry rather
-- than guessing paths ad hoc.

CREATE TABLE IF NOT EXISTS mmo_server_content_archive_mounts (
  content_archive_mount_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_file_id BINARY(16) NOT NULL,
  content_revision_id BINARY(16) NOT NULL,
  archive_logical_path VARCHAR(512) NOT NULL,
  archive_role VARCHAR(32) NOT NULL,
  mount_strategy VARCHAR(32) NOT NULL DEFAULT 'pre_extracted',
  mount_status VARCHAR(32) NOT NULL DEFAULT 'planned',
  extracted_root_label VARCHAR(512) NOT NULL DEFAULT '',
  extracted_file_count INT NOT NULL DEFAULT 0,
  extracted_total_bytes BIGINT UNSIGNED NOT NULL DEFAULT 0,
  extracted_manifest_hash CHAR(64) NULL,
  last_verified_at TIMESTAMP(6) NULL DEFAULT NULL,
  mount_notes JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (content_archive_mount_id),
  UNIQUE KEY mmo_content_archive_mount_file_uk (content_file_id),
  KEY ix_mmo_content_archive_mount_revision (content_revision_id, archive_role, mount_status),
  KEY ix_mmo_content_archive_mount_path (content_revision_id, archive_logical_path),
  CONSTRAINT mmo_content_archive_mount_file_fk
    FOREIGN KEY (content_file_id) REFERENCES mmo_server_content_pack_files(content_file_id) ON DELETE CASCADE,
  CONSTRAINT mmo_content_archive_mount_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_content_archive_mount_role_ck CHECK (archive_role IN ('archive_vdf', 'archive_mod')),
  CONSTRAINT mmo_content_archive_mount_strategy_ck CHECK (mount_strategy IN (
    'pre_extracted', 'read_direct', 'extract_on_boot', 'external_mount', 'manual'
  )),
  CONSTRAINT mmo_content_archive_mount_status_ck CHECK (mount_status IN (
    'planned', 'mounted', 'extracted', 'verified', 'failed', 'ignored'
  )),
  CONSTRAINT mmo_content_archive_mount_file_count_ck CHECK (extracted_file_count >= 0),
  CONSTRAINT mmo_content_archive_mount_total_bytes_ck CHECK (extracted_total_bytes >= 0),
  CONSTRAINT mmo_content_archive_mount_hash_ck CHECK (extracted_manifest_hash IS NULL OR REGEXP_LIKE(extracted_manifest_hash, '^[0-9a-f]{64}$')),
  CONSTRAINT mmo_content_archive_mount_notes_json_ck CHECK (JSON_VALID(mount_notes))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_server_content_extracted_files (
  content_extracted_file_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_archive_mount_id BINARY(16) NOT NULL,
  content_revision_id BINARY(16) NOT NULL,
  archive_logical_path VARCHAR(512) NOT NULL,
  extracted_logical_path VARCHAR(512) NOT NULL,
  mapped_content_file_id BINARY(16) NULL,
  byte_size BIGINT UNSIGNED NOT NULL DEFAULT 0,
  sha256 CHAR(64) NULL,
  extracted_status VARCHAR(32) NOT NULL DEFAULT 'discovered',
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (content_extracted_file_id),
  UNIQUE KEY mmo_content_extracted_file_uk (content_archive_mount_id, extracted_logical_path),
  KEY ix_mmo_content_extracted_revision (content_revision_id, extracted_status),
  KEY ix_mmo_content_extracted_path (content_revision_id, extracted_logical_path),
  KEY ix_mmo_content_extracted_hash (content_revision_id, sha256),
  CONSTRAINT mmo_content_extracted_archive_fk
    FOREIGN KEY (content_archive_mount_id) REFERENCES mmo_server_content_archive_mounts(content_archive_mount_id) ON DELETE CASCADE,
  CONSTRAINT mmo_content_extracted_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_content_extracted_mapped_file_fk
    FOREIGN KEY (mapped_content_file_id) REFERENCES mmo_server_content_pack_files(content_file_id) ON DELETE SET NULL,
  CONSTRAINT mmo_content_extracted_size_ck CHECK (byte_size >= 0),
  CONSTRAINT mmo_content_extracted_hash_ck CHECK (sha256 IS NULL OR REGEXP_LIKE(sha256, '^[0-9a-f]{64}$')),
  CONSTRAINT mmo_content_extracted_status_ck CHECK (extracted_status IN (
    'discovered', 'hashed', 'linked', 'missing', 'hash_mismatch', 'ignored'
  )),
  CONSTRAINT mmo_content_extracted_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP VIEW IF EXISTS v_mmo_server_content_archive_health;
DROP VIEW IF EXISTS v_mmo_server_content_extracted_files;
DROP VIEW IF EXISTS v_mmo_server_content_archive_mounts;

CREATE VIEW v_mmo_server_content_archive_mounts AS
SELECT
  BIN_TO_UUID(am.content_archive_mount_id, 1) AS content_archive_mount_uuid,
  BIN_TO_UUID(am.content_file_id, 1) AS content_file_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  am.archive_logical_path,
  cpf.source_kind,
  cpf.byte_size AS archive_byte_size,
  cpf.sha256 AS archive_sha256,
  am.archive_role,
  am.mount_strategy,
  am.mount_status,
  am.extracted_root_label,
  am.extracted_file_count,
  am.extracted_total_bytes,
  am.extracted_manifest_hash,
  am.last_verified_at,
  am.mount_notes,
  am.created_at,
  am.updated_at
FROM mmo_server_content_archive_mounts am
JOIN mmo_server_content_pack_files cpf ON cpf.content_file_id = am.content_file_id
JOIN content_revisions cr ON cr.content_revision_id = am.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id;

CREATE VIEW v_mmo_server_content_extracted_files AS
SELECT
  BIN_TO_UUID(ef.content_extracted_file_id, 1) AS content_extracted_file_uuid,
  BIN_TO_UUID(ef.content_archive_mount_id, 1) AS content_archive_mount_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  ef.archive_logical_path,
  ef.extracted_logical_path,
  BIN_TO_UUID(ef.mapped_content_file_id, 1) AS mapped_content_file_uuid,
  mapped.logical_path AS mapped_manifest_logical_path,
  ef.byte_size,
  ef.sha256,
  ef.extracted_status,
  ef.raw_payload,
  ef.created_at,
  ef.updated_at
FROM mmo_server_content_extracted_files ef
JOIN content_revisions cr ON cr.content_revision_id = ef.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id
LEFT JOIN mmo_server_content_pack_files mapped ON mapped.content_file_id = ef.mapped_content_file_id;

CREATE VIEW v_mmo_server_content_archive_health AS
SELECT
  cr.content_revision_key,
  cr.is_active,
  SUM(CASE WHEN i.file_role IN ('archive_vdf', 'archive_mod') THEN 1 ELSE 0 END) AS inventory_archive_count,
  COUNT(am.content_archive_mount_id) AS archive_mount_count,
  SUM(CASE WHEN am.content_archive_mount_id IS NULL AND i.file_role IN ('archive_vdf', 'archive_mod') THEN 1 ELSE 0 END) AS missing_mount_count,
  SUM(CASE WHEN am.mount_status = 'planned' THEN 1 ELSE 0 END) AS planned_count,
  SUM(CASE WHEN am.mount_status = 'mounted' THEN 1 ELSE 0 END) AS mounted_count,
  SUM(CASE WHEN am.mount_status = 'extracted' THEN 1 ELSE 0 END) AS extracted_count,
  SUM(CASE WHEN am.mount_status = 'verified' THEN 1 ELSE 0 END) AS verified_count,
  SUM(CASE WHEN am.mount_status = 'failed' THEN 1 ELSE 0 END) AS failed_count,
  COALESCE(SUM(am.extracted_file_count), 0) AS extracted_file_count,
  COALESCE(SUM(am.extracted_total_bytes), 0) AS extracted_total_bytes
FROM content_revisions cr
JOIN mmo_server_content_pack_inventory i ON i.content_revision_id = cr.content_revision_id
LEFT JOIN mmo_server_content_archive_mounts am ON am.content_file_id = i.content_file_id
WHERE i.file_role IN ('archive_vdf', 'archive_mod')
GROUP BY cr.content_revision_key, cr.is_active;

DELIMITER ;;

DROP PROCEDURE IF EXISTS mmo_upsert_server_content_archive_mount;;
CREATE PROCEDURE mmo_upsert_server_content_archive_mount(
  IN p_content_revision_key VARCHAR(191),
  IN p_archive_logical_path VARCHAR(512),
  IN p_archive_role VARCHAR(32),
  IN p_mount_strategy VARCHAR(32),
  IN p_mount_status VARCHAR(32),
  IN p_extracted_root_label VARCHAR(512),
  IN p_extracted_file_count INT,
  IN p_extracted_total_bytes BIGINT UNSIGNED,
  IN p_extracted_manifest_hash CHAR(64),
  IN p_last_verified_at TIMESTAMP(6),
  IN p_mount_notes JSON,
  OUT o_content_archive_mount_id BINARY(16)
)
proc: BEGIN
  DECLARE v_content_revision_id BINARY(16) DEFAULT NULL;
  DECLARE v_content_file_id BINARY(16) DEFAULT NULL;
  DECLARE v_archive_logical_path VARCHAR(512) DEFAULT '';
  DECLARE v_archive_role VARCHAR(32) DEFAULT 'archive_vdf';
  DECLARE v_mount_strategy VARCHAR(32) DEFAULT 'pre_extracted';
  DECLARE v_mount_status VARCHAR(32) DEFAULT 'planned';
  DECLARE CONTINUE HANDLER FOR NOT FOUND BEGIN END;

  SET o_content_archive_mount_id = NULL;
  SET v_archive_logical_path = LOWER(REPLACE(COALESCE(p_archive_logical_path, ''), '\\', '/'));

  SELECT content_revision_id
    INTO v_content_revision_id
    FROM content_revisions
   WHERE content_revision_key = p_content_revision_key
   LIMIT 1;
  IF v_content_revision_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_archive_mount: content revision not found';
  END IF;

  SELECT content_file_id
    INTO v_content_file_id
    FROM mmo_server_content_pack_files
   WHERE content_revision_id = v_content_revision_id
     AND logical_path = v_archive_logical_path
   LIMIT 1;
  IF v_content_file_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_archive_mount: archive file not found in content manifest';
  END IF;

  SET v_archive_role = COALESCE(NULLIF(p_archive_role, ''), 'archive_vdf');
  IF v_archive_role NOT IN ('archive_vdf', 'archive_mod') THEN
    SET v_archive_role = 'archive_vdf';
  END IF;

  SET v_mount_strategy = COALESCE(NULLIF(p_mount_strategy, ''), 'pre_extracted');
  IF v_mount_strategy NOT IN ('pre_extracted', 'read_direct', 'extract_on_boot', 'external_mount', 'manual') THEN
    SET v_mount_strategy = 'pre_extracted';
  END IF;

  SET v_mount_status = COALESCE(NULLIF(p_mount_status, ''), 'planned');
  IF v_mount_status NOT IN ('planned', 'mounted', 'extracted', 'verified', 'failed', 'ignored') THEN
    SET v_mount_status = 'planned';
  END IF;

  IF p_extracted_manifest_hash IS NOT NULL AND p_extracted_manifest_hash NOT REGEXP '^[0-9a-f]{64}$' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_archive_mount: extracted manifest hash must be lowercase hex';
  END IF;

  INSERT INTO mmo_server_content_archive_mounts(
    content_file_id,
    content_revision_id,
    archive_logical_path,
    archive_role,
    mount_strategy,
    mount_status,
    extracted_root_label,
    extracted_file_count,
    extracted_total_bytes,
    extracted_manifest_hash,
    last_verified_at,
    mount_notes
  )
  VALUES (
    v_content_file_id,
    v_content_revision_id,
    v_archive_logical_path,
    v_archive_role,
    v_mount_strategy,
    v_mount_status,
    COALESCE(p_extracted_root_label, ''),
    COALESCE(p_extracted_file_count, 0),
    COALESCE(p_extracted_total_bytes, 0),
    LOWER(NULLIF(p_extracted_manifest_hash, '')),
    p_last_verified_at,
    COALESCE(p_mount_notes, JSON_OBJECT())
  )
  ON DUPLICATE KEY UPDATE
    content_revision_id = VALUES(content_revision_id),
    archive_logical_path = VALUES(archive_logical_path),
    archive_role = VALUES(archive_role),
    mount_strategy = VALUES(mount_strategy),
    mount_status = VALUES(mount_status),
    extracted_root_label = VALUES(extracted_root_label),
    extracted_file_count = VALUES(extracted_file_count),
    extracted_total_bytes = VALUES(extracted_total_bytes),
    extracted_manifest_hash = VALUES(extracted_manifest_hash),
    last_verified_at = VALUES(last_verified_at),
    mount_notes = VALUES(mount_notes),
    updated_at = CURRENT_TIMESTAMP(6);

  SELECT content_archive_mount_id
    INTO o_content_archive_mount_id
    FROM mmo_server_content_archive_mounts
   WHERE content_file_id = v_content_file_id
   LIMIT 1;
END;;

DROP PROCEDURE IF EXISTS mmo_upsert_server_content_extracted_file;;
CREATE PROCEDURE mmo_upsert_server_content_extracted_file(
  IN p_content_revision_key VARCHAR(191),
  IN p_archive_logical_path VARCHAR(512),
  IN p_extracted_logical_path VARCHAR(512),
  IN p_byte_size BIGINT UNSIGNED,
  IN p_sha256 CHAR(64),
  IN p_extracted_status VARCHAR(32),
  IN p_mapped_content_logical_path VARCHAR(512),
  IN p_raw_payload JSON,
  OUT o_content_extracted_file_id BINARY(16)
)
proc: BEGIN
  DECLARE v_content_revision_id BINARY(16) DEFAULT NULL;
  DECLARE v_content_archive_mount_id BINARY(16) DEFAULT NULL;
  DECLARE v_mapped_content_file_id BINARY(16) DEFAULT NULL;
  DECLARE v_archive_logical_path VARCHAR(512) DEFAULT '';
  DECLARE v_extracted_logical_path VARCHAR(512) DEFAULT '';
  DECLARE v_mapped_content_logical_path VARCHAR(512) DEFAULT '';
  DECLARE v_extracted_status VARCHAR(32) DEFAULT 'discovered';
  DECLARE CONTINUE HANDLER FOR NOT FOUND BEGIN END;

  SET o_content_extracted_file_id = NULL;
  SET v_archive_logical_path = LOWER(REPLACE(COALESCE(p_archive_logical_path, ''), '\\', '/'));
  SET v_extracted_logical_path = LOWER(REPLACE(COALESCE(p_extracted_logical_path, ''), '\\', '/'));
  SET v_mapped_content_logical_path = LOWER(REPLACE(COALESCE(p_mapped_content_logical_path, ''), '\\', '/'));

  SELECT content_revision_id
    INTO v_content_revision_id
    FROM content_revisions
   WHERE content_revision_key = p_content_revision_key
   LIMIT 1;
  IF v_content_revision_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_extracted_file: content revision not found';
  END IF;

  SELECT content_archive_mount_id
    INTO v_content_archive_mount_id
    FROM mmo_server_content_archive_mounts
   WHERE content_revision_id = v_content_revision_id
     AND archive_logical_path = v_archive_logical_path
   LIMIT 1;
  IF v_content_archive_mount_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_extracted_file: archive mount not found';
  END IF;

  IF COALESCE(TRIM(v_extracted_logical_path), '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_extracted_file: extracted logical path is required';
  END IF;

  IF p_sha256 IS NOT NULL AND p_sha256 NOT REGEXP '^[0-9a-f]{64}$' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_extracted_file: sha256 must be lowercase hex';
  END IF;

  IF v_mapped_content_logical_path <> '' THEN
    SELECT content_file_id
      INTO v_mapped_content_file_id
      FROM mmo_server_content_pack_files
     WHERE content_revision_id = v_content_revision_id
       AND logical_path = v_mapped_content_logical_path
     LIMIT 1;
  END IF;

  SET v_extracted_status = COALESCE(NULLIF(p_extracted_status, ''), 'discovered');
  IF v_extracted_status NOT IN ('discovered', 'hashed', 'linked', 'missing', 'hash_mismatch', 'ignored') THEN
    SET v_extracted_status = 'discovered';
  END IF;

  INSERT INTO mmo_server_content_extracted_files(
    content_archive_mount_id,
    content_revision_id,
    archive_logical_path,
    extracted_logical_path,
    mapped_content_file_id,
    byte_size,
    sha256,
    extracted_status,
    raw_payload
  )
  VALUES (
    v_content_archive_mount_id,
    v_content_revision_id,
    v_archive_logical_path,
    v_extracted_logical_path,
    v_mapped_content_file_id,
    COALESCE(p_byte_size, 0),
    LOWER(NULLIF(p_sha256, '')),
    v_extracted_status,
    COALESCE(p_raw_payload, JSON_OBJECT())
  )
  ON DUPLICATE KEY UPDATE
    mapped_content_file_id = VALUES(mapped_content_file_id),
    byte_size = VALUES(byte_size),
    sha256 = VALUES(sha256),
    extracted_status = VALUES(extracted_status),
    raw_payload = VALUES(raw_payload),
    updated_at = CURRENT_TIMESTAMP(6);

  SELECT content_extracted_file_id
    INTO o_content_extracted_file_id
    FROM mmo_server_content_extracted_files
   WHERE content_archive_mount_id = v_content_archive_mount_id
     AND extracted_logical_path = v_extracted_logical_path
   LIMIT 1;
END;;

DELIMITER ;

INSERT INTO mmo_schema_versions(migration_key, checksum, description)
VALUES (
  'server/sql/step203_server_content_archive_mounts.sql',
  SHA2('server/sql/step203_server_content_archive_mounts.sql', 256),
  'Step203: server content archive mount and pre-extract registry'
)
ON DUPLICATE KEY UPDATE
  checksum = VALUES(checksum),
  description = VALUES(description),
  applied_at = CURRENT_TIMESTAMP(6);
