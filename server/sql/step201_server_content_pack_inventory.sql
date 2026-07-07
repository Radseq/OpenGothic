-- Step201: classify server-owned content pack files into future importer roles.
-- Step188 stores the authoritative server manifest and per-file checksums.
-- This step adds a durable inventory layer that tells the future server loader
-- which files are world ZEN, compiled Daedalus DAT, OU/dialog output, archives,
-- source/reference files, or assets.

CREATE TABLE IF NOT EXISTS mmo_server_content_pack_inventory (
  content_inventory_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_file_id BINARY(16) NOT NULL,
  content_revision_id BINARY(16) NOT NULL,
  logical_path VARCHAR(512) NOT NULL,
  file_role VARCHAR(48) NOT NULL DEFAULT 'other',
  loader_stage VARCHAR(48) NOT NULL DEFAULT 'unknown',
  import_priority INT NOT NULL DEFAULT 1000,
  required_for_server_authority TINYINT(1) NOT NULL DEFAULT 0,
  import_status VARCHAR(32) NOT NULL DEFAULT 'not_started',
  import_notes JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (content_inventory_id),
  UNIQUE KEY mmo_server_content_pack_inventory_file_uk (content_file_id),
  KEY ix_mmo_content_pack_inventory_revision_role (content_revision_id, file_role),
  KEY ix_mmo_content_pack_inventory_revision_stage (content_revision_id, loader_stage),
  KEY ix_mmo_content_pack_inventory_revision_status (content_revision_id, import_status),
  KEY ix_mmo_content_pack_inventory_path (content_revision_id, logical_path),
  CONSTRAINT mmo_content_pack_inventory_file_fk
    FOREIGN KEY (content_file_id) REFERENCES mmo_server_content_pack_files(content_file_id) ON DELETE CASCADE,
  CONSTRAINT mmo_content_pack_inventory_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_content_pack_inventory_role_ck CHECK (file_role IN (
    'world_zen',
    'scripts_dat',
    'dialog_ou',
    'archive_vdf',
    'archive_mod',
    'script_source',
    'config_ini',
    'asset_texture',
    'asset_mesh',
    'asset_sound',
    'asset_video',
    'font',
    'other'
  )),
  CONSTRAINT mmo_content_pack_inventory_stage_ck CHECK (loader_stage IN (
    'archive_mount',
    'world_loader',
    'script_vm',
    'dialog_output',
    'source_reference',
    'server_config',
    'asset_lookup',
    'ignore',
    'unknown'
  )),
  CONSTRAINT mmo_content_pack_inventory_status_ck CHECK (import_status IN (
    'not_started',
    'planned',
    'ready_for_parser',
    'unsupported',
    'imported',
    'failed',
    'ignored'
  )),
  CONSTRAINT mmo_content_pack_inventory_priority_ck CHECK (import_priority >= 0),
  CONSTRAINT mmo_content_pack_inventory_notes_json_ck CHECK (JSON_VALID(import_notes))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP VIEW IF EXISTS v_mmo_server_content_pack_inventory_health;
DROP VIEW IF EXISTS v_mmo_server_content_pack_inventory;

CREATE VIEW v_mmo_server_content_pack_inventory AS
SELECT
  BIN_TO_UUID(i.content_inventory_id, 1) AS content_inventory_uuid,
  BIN_TO_UUID(i.content_file_id, 1) AS content_file_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  i.logical_path,
  f.source_kind,
  f.byte_size,
  f.sha256,
  i.file_role,
  i.loader_stage,
  i.import_priority,
  i.required_for_server_authority,
  i.import_status,
  i.import_notes,
  i.created_at,
  i.updated_at
FROM mmo_server_content_pack_inventory i
JOIN mmo_server_content_pack_files f ON f.content_file_id = i.content_file_id
JOIN content_revisions cr ON cr.content_revision_id = i.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id;

CREATE VIEW v_mmo_server_content_pack_inventory_health AS
SELECT
  cr.content_revision_key,
  cr.is_active,
  COUNT(f.content_file_id) AS manifest_file_count,
  COUNT(i.content_inventory_id) AS inventoried_file_count,
  SUM(CASE WHEN i.content_inventory_id IS NULL THEN 1 ELSE 0 END) AS missing_inventory_count,
  SUM(CASE WHEN i.file_role = 'world_zen' THEN 1 ELSE 0 END) AS world_zen_count,
  SUM(CASE WHEN i.file_role = 'scripts_dat' THEN 1 ELSE 0 END) AS scripts_dat_count,
  SUM(CASE WHEN i.file_role = 'dialog_ou' THEN 1 ELSE 0 END) AS dialog_ou_count,
  SUM(CASE WHEN i.file_role IN ('archive_vdf', 'archive_mod') THEN 1 ELSE 0 END) AS archive_count,
  SUM(CASE WHEN i.required_for_server_authority = 1 THEN 1 ELSE 0 END) AS required_for_authority_count,
  SUM(CASE WHEN i.import_status = 'ready_for_parser' THEN 1 ELSE 0 END) AS ready_for_parser_count,
  SUM(CASE WHEN i.import_status = 'unsupported' THEN 1 ELSE 0 END) AS unsupported_count,
  SUM(CASE WHEN i.import_status = 'failed' THEN 1 ELSE 0 END) AS failed_count,
  SUM(CASE WHEN i.import_status = 'imported' THEN 1 ELSE 0 END) AS imported_count
FROM content_revisions cr
JOIN mmo_server_content_pack_files f ON f.content_revision_id = cr.content_revision_id
LEFT JOIN mmo_server_content_pack_inventory i ON i.content_file_id = f.content_file_id
GROUP BY cr.content_revision_key, cr.is_active;

DELIMITER ;;

DROP PROCEDURE IF EXISTS mmo_upsert_server_content_pack_inventory;;
CREATE PROCEDURE mmo_upsert_server_content_pack_inventory(
  IN p_content_revision_key VARCHAR(191),
  IN p_logical_path VARCHAR(512),
  IN p_file_role VARCHAR(48),
  IN p_loader_stage VARCHAR(48),
  IN p_import_priority INT,
  IN p_required_for_server_authority TINYINT(1),
  IN p_import_status VARCHAR(32),
  IN p_import_notes JSON,
  OUT o_content_inventory_id BINARY(16)
)
proc: BEGIN
  DECLARE v_content_revision_id BINARY(16) DEFAULT NULL;
  DECLARE v_content_file_id BINARY(16) DEFAULT NULL;
  DECLARE v_logical_path VARCHAR(512) DEFAULT '';
  DECLARE v_file_role VARCHAR(48) DEFAULT 'other';
  DECLARE v_loader_stage VARCHAR(48) DEFAULT 'unknown';
  DECLARE v_import_status VARCHAR(32) DEFAULT 'not_started';
  DECLARE CONTINUE HANDLER FOR NOT FOUND BEGIN END;

  SET o_content_inventory_id = NULL;
  SET v_logical_path = LOWER(REPLACE(COALESCE(p_logical_path, ''), '\\', '/'));

  SELECT content_revision_id
    INTO v_content_revision_id
    FROM content_revisions
   WHERE content_revision_key = p_content_revision_key
   LIMIT 1;
  IF v_content_revision_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_pack_inventory: content revision not found';
  END IF;

  IF COALESCE(TRIM(v_logical_path), '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_pack_inventory: logical path is required';
  END IF;

  SELECT content_file_id
    INTO v_content_file_id
    FROM mmo_server_content_pack_files
   WHERE content_revision_id = v_content_revision_id
     AND logical_path = v_logical_path
   LIMIT 1;
  IF v_content_file_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_pack_inventory: content pack file not found';
  END IF;

  SET v_file_role = COALESCE(NULLIF(p_file_role, ''), 'other');
  IF v_file_role NOT IN ('world_zen','scripts_dat','dialog_ou','archive_vdf','archive_mod','script_source','config_ini','asset_texture','asset_mesh','asset_sound','asset_video','font','other') THEN
    SET v_file_role = 'other';
  END IF;

  SET v_loader_stage = COALESCE(NULLIF(p_loader_stage, ''), 'unknown');
  IF v_loader_stage NOT IN ('archive_mount','world_loader','script_vm','dialog_output','source_reference','server_config','asset_lookup','ignore','unknown') THEN
    SET v_loader_stage = 'unknown';
  END IF;

  SET v_import_status = COALESCE(NULLIF(p_import_status, ''), 'not_started');
  IF v_import_status NOT IN ('not_started','planned','ready_for_parser','unsupported','imported','failed','ignored') THEN
    SET v_import_status = 'not_started';
  END IF;

  INSERT INTO mmo_server_content_pack_inventory(
    content_file_id,
    content_revision_id,
    logical_path,
    file_role,
    loader_stage,
    import_priority,
    required_for_server_authority,
    import_status,
    import_notes
  )
  VALUES (
    v_content_file_id,
    v_content_revision_id,
    v_logical_path,
    v_file_role,
    v_loader_stage,
    COALESCE(p_import_priority, 1000),
    COALESCE(p_required_for_server_authority, 0),
    v_import_status,
    COALESCE(p_import_notes, JSON_OBJECT())
  )
  ON DUPLICATE KEY UPDATE
    content_revision_id = VALUES(content_revision_id),
    logical_path = VALUES(logical_path),
    file_role = VALUES(file_role),
    loader_stage = VALUES(loader_stage),
    import_priority = VALUES(import_priority),
    required_for_server_authority = VALUES(required_for_server_authority),
    import_status = VALUES(import_status),
    import_notes = VALUES(import_notes),
    updated_at = CURRENT_TIMESTAMP(6);

  SELECT content_inventory_id
    INTO o_content_inventory_id
    FROM mmo_server_content_pack_inventory
   WHERE content_file_id = v_content_file_id
   LIMIT 1;
END;;

DELIMITER ;

INSERT INTO mmo_schema_versions(migration_key, checksum, description)
VALUES (
  'server/sql/step201_server_content_pack_inventory.sql',
  SHA2('server/sql/step201_server_content_pack_inventory.sql', 256),
  'Step201: server content pack inventory roles for future ZEN/DAT/OU importers'
)
ON DUPLICATE KEY UPDATE
  checksum = VALUES(checksum),
  description = VALUES(description),
  applied_at = CURRENT_TIMESTAMP(6);
