-- Step188: server-owned content pack manifest.
-- The server must use its own Gothic/mod content files as authority and only
-- accept clients that declare the same manifest hash.

CREATE TABLE IF NOT EXISTS mmo_server_content_pack_files (
  content_file_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_revision_id BINARY(16) NOT NULL,
  logical_path VARCHAR(512) NOT NULL,
  source_kind VARCHAR(32) NOT NULL DEFAULT 'other',
  byte_size BIGINT UNSIGNED NOT NULL DEFAULT 0,
  sha256 CHAR(64) NOT NULL,
  mtime_utc TIMESTAMP(6) NULL DEFAULT NULL,
  required TINYINT(1) NOT NULL DEFAULT 1,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (content_file_id),
  UNIQUE KEY mmo_server_content_pack_files_uk (content_revision_id, logical_path),
  KEY ix_mmo_server_content_pack_files_hash (content_revision_id, sha256),
  KEY ix_mmo_server_content_pack_files_kind (content_revision_id, source_kind),
  CONSTRAINT mmo_server_content_pack_files_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_server_content_pack_files_size_ck CHECK (byte_size >= 0),
  CONSTRAINT mmo_server_content_pack_files_kind_ck CHECK (source_kind IN (
    'zen', 'dat', 'ou', 'vdf', 'mod', 'script', 'ini', 'texture', 'mesh', 'sound', 'video', 'font', 'other'
  )),
  CONSTRAINT mmo_server_content_pack_files_sha_ck CHECK (sha256 REGEXP '^[0-9a-f]{64}$'),
  CONSTRAINT mmo_server_content_pack_files_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_server_content_pack_manifests (
  content_manifest_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_revision_id BINARY(16) NOT NULL,
  manifest_hash CHAR(64) NOT NULL,
  file_count INT NOT NULL DEFAULT 0,
  required_file_count INT NOT NULL DEFAULT 0,
  total_bytes BIGINT UNSIGNED NOT NULL DEFAULT 0,
  source_root_label VARCHAR(512) NOT NULL DEFAULT '',
  source_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (content_manifest_id),
  UNIQUE KEY mmo_server_content_pack_manifests_revision_uk (content_revision_id),
  KEY ix_mmo_server_content_pack_manifests_hash (manifest_hash),
  CONSTRAINT mmo_server_content_pack_manifests_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_server_content_pack_manifests_hash_ck CHECK (manifest_hash REGEXP '^[0-9a-f]{64}$'),
  CONSTRAINT mmo_server_content_pack_manifests_file_count_ck CHECK (file_count >= 0),
  CONSTRAINT mmo_server_content_pack_manifests_required_count_ck CHECK (required_file_count >= 0),
  CONSTRAINT mmo_server_content_pack_manifests_total_bytes_ck CHECK (total_bytes >= 0),
  CONSTRAINT mmo_server_content_pack_manifests_payload_json_ck CHECK (JSON_VALID(source_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP VIEW IF EXISTS v_mmo_server_content_pack_manifests;
CREATE VIEW v_mmo_server_content_pack_manifests AS
SELECT
  BIN_TO_UUID(m.content_manifest_id, 1) AS content_manifest_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  m.manifest_hash,
  m.file_count,
  m.required_file_count,
  m.total_bytes,
  m.source_root_label,
  m.source_payload,
  m.created_at,
  m.updated_at
FROM mmo_server_content_pack_manifests m
JOIN content_revisions cr ON cr.content_revision_id = m.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id;

DROP VIEW IF EXISTS v_mmo_server_content_pack_files;
CREATE VIEW v_mmo_server_content_pack_files AS
SELECT
  BIN_TO_UUID(f.content_file_id, 1) AS content_file_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  f.logical_path,
  f.source_kind,
  f.byte_size,
  f.sha256,
  f.mtime_utc,
  f.required,
  f.raw_payload,
  f.created_at,
  f.updated_at
FROM mmo_server_content_pack_files f
JOIN content_revisions cr ON cr.content_revision_id = f.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id;

DELIMITER ;;

DROP PROCEDURE IF EXISTS mmo_upsert_server_content_pack_file;;
CREATE PROCEDURE mmo_upsert_server_content_pack_file(
  IN p_content_revision_key VARCHAR(191),
  IN p_logical_path VARCHAR(512),
  IN p_source_kind VARCHAR(32),
  IN p_byte_size BIGINT UNSIGNED,
  IN p_sha256 CHAR(64),
  IN p_mtime_utc TIMESTAMP(6),
  IN p_required TINYINT(1),
  IN p_raw_payload JSON,
  OUT o_content_file_id BINARY(16)
)
proc: BEGIN
  DECLARE v_content_revision_id BINARY(16) DEFAULT NULL;
  DECLARE v_source_kind VARCHAR(32) DEFAULT 'other';
  DECLARE CONTINUE HANDLER FOR NOT FOUND BEGIN END;

  SET o_content_file_id = NULL;

  SELECT content_revision_id
    INTO v_content_revision_id
    FROM content_revisions
   WHERE content_revision_key = p_content_revision_key
   LIMIT 1;
  IF v_content_revision_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_pack_file: content revision not found';
  END IF;

  IF COALESCE(TRIM(p_logical_path), '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_pack_file: logical path is required';
  END IF;
  IF COALESCE(p_sha256, '') NOT REGEXP '^[0-9a-f]{64}$' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_pack_file: sha256 must be lowercase hex';
  END IF;

  SET v_source_kind = COALESCE(NULLIF(p_source_kind, ''), 'other');
  IF v_source_kind NOT IN ('zen', 'dat', 'ou', 'vdf', 'mod', 'script', 'ini', 'texture', 'mesh', 'sound', 'video', 'font', 'other') THEN
    SET v_source_kind = 'other';
  END IF;

  INSERT INTO mmo_server_content_pack_files(
    content_revision_id, logical_path, source_kind, byte_size, sha256, mtime_utc, required, raw_payload
  )
  VALUES (
    v_content_revision_id,
    LOWER(REPLACE(p_logical_path, '\\', '/')),
    v_source_kind,
    COALESCE(p_byte_size, 0),
    LOWER(p_sha256),
    p_mtime_utc,
    COALESCE(p_required, 1),
    COALESCE(p_raw_payload, JSON_OBJECT())
  )
  ON DUPLICATE KEY UPDATE
    source_kind = VALUES(source_kind),
    byte_size = VALUES(byte_size),
    sha256 = VALUES(sha256),
    mtime_utc = VALUES(mtime_utc),
    required = VALUES(required),
    raw_payload = VALUES(raw_payload),
    updated_at = CURRENT_TIMESTAMP(6);

  SELECT content_file_id
    INTO o_content_file_id
    FROM mmo_server_content_pack_files
   WHERE content_revision_id = v_content_revision_id
     AND logical_path = LOWER(REPLACE(p_logical_path, '\\', '/'))
   LIMIT 1;
END;;

DROP PROCEDURE IF EXISTS mmo_set_server_content_pack_manifest;;
CREATE PROCEDURE mmo_set_server_content_pack_manifest(
  IN p_content_revision_key VARCHAR(191),
  IN p_manifest_hash CHAR(64),
  IN p_file_count INT,
  IN p_required_file_count INT,
  IN p_total_bytes BIGINT UNSIGNED,
  IN p_source_root_label VARCHAR(512),
  IN p_source_payload JSON,
  OUT o_content_manifest_id BINARY(16)
)
proc: BEGIN
  DECLARE v_content_revision_id BINARY(16) DEFAULT NULL;
  DECLARE CONTINUE HANDLER FOR NOT FOUND BEGIN END;

  SET o_content_manifest_id = NULL;

  SELECT content_revision_id
    INTO v_content_revision_id
    FROM content_revisions
   WHERE content_revision_key = p_content_revision_key
   LIMIT 1;
  IF v_content_revision_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_set_server_content_pack_manifest: content revision not found';
  END IF;

  IF COALESCE(p_manifest_hash, '') NOT REGEXP '^[0-9a-f]{64}$' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_set_server_content_pack_manifest: manifest hash must be lowercase hex';
  END IF;

  INSERT INTO mmo_server_content_pack_manifests(
    content_revision_id, manifest_hash, file_count, required_file_count, total_bytes, source_root_label, source_payload
  )
  VALUES (
    v_content_revision_id,
    LOWER(p_manifest_hash),
    COALESCE(p_file_count, 0),
    COALESCE(p_required_file_count, 0),
    COALESCE(p_total_bytes, 0),
    COALESCE(p_source_root_label, ''),
    COALESCE(p_source_payload, JSON_OBJECT())
  )
  ON DUPLICATE KEY UPDATE
    manifest_hash = VALUES(manifest_hash),
    file_count = VALUES(file_count),
    required_file_count = VALUES(required_file_count),
    total_bytes = VALUES(total_bytes),
    source_root_label = VALUES(source_root_label),
    source_payload = VALUES(source_payload),
    updated_at = CURRENT_TIMESTAMP(6);

  SELECT content_manifest_id
    INTO o_content_manifest_id
    FROM mmo_server_content_pack_manifests
   WHERE content_revision_id = v_content_revision_id
   LIMIT 1;
END;;

DROP PROCEDURE IF EXISTS mmo_validate_client_content_pack;;
CREATE PROCEDURE mmo_validate_client_content_pack(
  IN p_realm_key VARCHAR(191),
  IN p_client_manifest_hash CHAR(64),
  OUT o_accepted TINYINT(1),
  OUT o_reason VARCHAR(191),
  OUT o_server_manifest_hash CHAR(64),
  OUT o_content_revision_key VARCHAR(191)
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_content_revision_id BINARY(16) DEFAULT NULL;
  DECLARE CONTINUE HANDLER FOR NOT FOUND BEGIN END;

  SET o_accepted = 0;
  SET o_reason = 'unknown';
  SET o_server_manifest_hash = NULL;
  SET o_content_revision_key = NULL;

  SELECT rr.realm_id, rr.active_content_revision_id, cr.content_revision_key
    INTO v_realm_id, v_content_revision_id, o_content_revision_key
    FROM realm_realms rr
    JOIN content_revisions cr ON cr.content_revision_id = rr.active_content_revision_id
   WHERE rr.realm_key = p_realm_key
   LIMIT 1;
  IF v_realm_id IS NULL THEN
    SET o_reason = 'realm_not_found';
    LEAVE proc;
  END IF;

  SELECT manifest_hash
    INTO o_server_manifest_hash
    FROM mmo_server_content_pack_manifests
   WHERE content_revision_id = v_content_revision_id
   LIMIT 1;
  IF o_server_manifest_hash IS NULL THEN
    SET o_reason = 'server_manifest_missing';
    LEAVE proc;
  END IF;

  IF COALESCE(p_client_manifest_hash, '') = '' THEN
    SET o_reason = 'client_manifest_missing';
    LEAVE proc;
  END IF;

  IF LOWER(p_client_manifest_hash) <> LOWER(o_server_manifest_hash) THEN
    SET o_reason = 'content_hash_mismatch';
    LEAVE proc;
  END IF;

  SET o_accepted = 1;
  SET o_reason = 'ok';
END;;

DELIMITER ;
