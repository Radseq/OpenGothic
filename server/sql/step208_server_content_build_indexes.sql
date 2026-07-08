-- Step208: content build result indexes for future ZEN/DAT/OU importers.
-- Step206 queues import work. This step defines the first durable output
-- contract: where the future importers write world entities, Daedalus symbols
-- and dialog outputs so the MMO server can query them without reading client
-- files.

CREATE TABLE IF NOT EXISTS mmo_server_content_build_imports (
  content_build_import_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_revision_id BINARY(16) NOT NULL,
  importer_key VARCHAR(96) NOT NULL,
  source_kind VARCHAR(48) NOT NULL,
  source_logical_path VARCHAR(512) NOT NULL,
  source_sha256 CHAR(64) NULL,
  import_status VARCHAR(32) NOT NULL DEFAULT 'imported',
  item_count INT NOT NULL DEFAULT 0,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  imported_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (content_build_import_id),
  UNIQUE KEY mmo_content_build_import_uk (content_revision_id, importer_key, source_logical_path, source_sha256),
  KEY ix_mmo_content_build_import_revision (content_revision_id, importer_key, import_status),
  CONSTRAINT mmo_content_build_import_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_content_build_import_sha_ck CHECK (source_sha256 IS NULL OR REGEXP_LIKE(source_sha256, '^[0-9a-f]{64}$')),
  CONSTRAINT mmo_content_build_import_status_ck CHECK (import_status IN ('imported', 'partial', 'failed', 'skipped')),
  CONSTRAINT mmo_content_build_import_kind_ck CHECK (source_kind IN ('world_zen', 'scripts_dat', 'dialog_ou', 'other')),
  CONSTRAINT mmo_content_build_import_count_ck CHECK (item_count >= 0),
  CONSTRAINT mmo_content_build_import_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_server_world_zen_entities (
  zen_entity_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_id BINARY(16) NOT NULL,
  world_name VARCHAR(191) NOT NULL,
  entity_kind VARCHAR(32) NOT NULL,
  entity_key VARCHAR(191) NOT NULL,
  entity_name VARCHAR(191) NOT NULL DEFAULT '',
  pos_x DOUBLE NULL,
  pos_y DOUBLE NULL,
  pos_z DOUBLE NULL,
  dir_x DOUBLE NULL,
  dir_y DOUBLE NULL,
  dir_z DOUBLE NULL,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (zen_entity_id),
  UNIQUE KEY mmo_world_zen_entity_uk (content_revision_id, world_name, entity_kind, entity_key),
  KEY ix_mmo_world_zen_entity_import (content_build_import_id),
  KEY ix_mmo_world_zen_entity_kind (content_revision_id, world_name, entity_kind),
  KEY ix_mmo_world_zen_entity_pos (content_revision_id, world_name, pos_x, pos_y, pos_z),
  CONSTRAINT mmo_world_zen_entity_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES mmo_server_content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT mmo_world_zen_entity_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_world_zen_entity_kind_ck CHECK (entity_kind IN ('world', 'waypoint', 'freepoint', 'vob', 'trigger', 'spawn', 'other')),
  CONSTRAINT mmo_world_zen_entity_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_server_daedalus_symbols (
  daedalus_symbol_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_id BINARY(16) NOT NULL,
  symbol_name VARCHAR(191) NOT NULL,
  symbol_kind VARCHAR(48) NOT NULL DEFAULT 'unknown',
  data_type VARCHAR(48) NOT NULL DEFAULT '',
  parent_symbol VARCHAR(191) NOT NULL DEFAULT '',
  ordinal INT NOT NULL DEFAULT 0,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (daedalus_symbol_id),
  UNIQUE KEY mmo_daedalus_symbol_uk (content_revision_id, symbol_name),
  KEY ix_mmo_daedalus_symbol_import (content_build_import_id),
  KEY ix_mmo_daedalus_symbol_kind (content_revision_id, symbol_kind),
  CONSTRAINT mmo_daedalus_symbol_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES mmo_server_content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT mmo_daedalus_symbol_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_daedalus_symbol_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_server_daedalus_npc_templates (
  daedalus_npc_template_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_id BINARY(16) NOT NULL,
  npc_instance VARCHAR(191) NOT NULL,
  display_name VARCHAR(191) NOT NULL DEFAULT '',
  guild VARCHAR(96) NOT NULL DEFAULT '',
  level_value INT NULL,
  routine_symbol VARCHAR(191) NOT NULL DEFAULT '',
  perception_symbol VARCHAR(191) NOT NULL DEFAULT '',
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (daedalus_npc_template_id),
  UNIQUE KEY mmo_daedalus_npc_template_uk (content_revision_id, npc_instance),
  KEY ix_mmo_daedalus_npc_template_import (content_build_import_id),
  KEY ix_mmo_daedalus_npc_template_guild (content_revision_id, guild),
  CONSTRAINT mmo_daedalus_npc_template_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES mmo_server_content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT mmo_daedalus_npc_template_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_daedalus_npc_template_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_server_dialog_outputs (
  dialog_output_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_id BINARY(16) NOT NULL,
  output_name VARCHAR(191) NOT NULL,
  text_value TEXT NULL,
  audio_ref VARCHAR(255) NOT NULL DEFAULT '',
  speaker_symbol VARCHAR(191) NOT NULL DEFAULT '',
  target_symbol VARCHAR(191) NOT NULL DEFAULT '',
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (dialog_output_id),
  UNIQUE KEY mmo_dialog_output_uk (content_revision_id, output_name),
  KEY ix_mmo_dialog_output_import (content_build_import_id),
  KEY ix_mmo_dialog_output_speaker (content_revision_id, speaker_symbol),
  CONSTRAINT mmo_dialog_output_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES mmo_server_content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT mmo_dialog_output_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_dialog_output_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP VIEW IF EXISTS v_mmo_server_content_build_health;
DROP VIEW IF EXISTS v_mmo_server_dialog_outputs;
DROP VIEW IF EXISTS v_mmo_server_daedalus_npc_templates;
DROP VIEW IF EXISTS v_mmo_server_daedalus_symbols;
DROP VIEW IF EXISTS v_mmo_server_world_zen_entities;
DROP VIEW IF EXISTS v_mmo_server_content_build_imports;

CREATE VIEW v_mmo_server_content_build_imports AS
SELECT
  BIN_TO_UUID(i.content_build_import_id, 1) AS content_build_import_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  i.importer_key,
  i.source_kind,
  i.source_logical_path,
  i.source_sha256,
  i.import_status,
  i.item_count,
  i.raw_payload,
  i.imported_at,
  i.created_at,
  i.updated_at
FROM mmo_server_content_build_imports i
JOIN content_revisions cr ON cr.content_revision_id = i.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id;

CREATE VIEW v_mmo_server_world_zen_entities AS
SELECT
  BIN_TO_UUID(e.zen_entity_id, 1) AS zen_entity_uuid,
  BIN_TO_UUID(e.content_build_import_id, 1) AS content_build_import_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  e.world_name,
  e.entity_kind,
  e.entity_key,
  e.entity_name,
  e.pos_x,
  e.pos_y,
  e.pos_z,
  e.dir_x,
  e.dir_y,
  e.dir_z,
  e.raw_payload
FROM mmo_server_world_zen_entities e
JOIN content_revisions cr ON cr.content_revision_id = e.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id;

CREATE VIEW v_mmo_server_daedalus_symbols AS
SELECT
  BIN_TO_UUID(s.daedalus_symbol_id, 1) AS daedalus_symbol_uuid,
  BIN_TO_UUID(s.content_build_import_id, 1) AS content_build_import_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  s.symbol_name,
  s.symbol_kind,
  s.data_type,
  s.parent_symbol,
  s.ordinal,
  s.raw_payload
FROM mmo_server_daedalus_symbols s
JOIN content_revisions cr ON cr.content_revision_id = s.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id;

CREATE VIEW v_mmo_server_daedalus_npc_templates AS
SELECT
  BIN_TO_UUID(n.daedalus_npc_template_id, 1) AS daedalus_npc_template_uuid,
  BIN_TO_UUID(n.content_build_import_id, 1) AS content_build_import_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  n.npc_instance,
  n.display_name,
  n.guild,
  n.level_value,
  n.routine_symbol,
  n.perception_symbol,
  n.raw_payload
FROM mmo_server_daedalus_npc_templates n
JOIN content_revisions cr ON cr.content_revision_id = n.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id;

CREATE VIEW v_mmo_server_dialog_outputs AS
SELECT
  BIN_TO_UUID(o.dialog_output_id, 1) AS dialog_output_uuid,
  BIN_TO_UUID(o.content_build_import_id, 1) AS content_build_import_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  o.output_name,
  o.text_value,
  o.audio_ref,
  o.speaker_symbol,
  o.target_symbol,
  o.raw_payload
FROM mmo_server_dialog_outputs o
JOIN content_revisions cr ON cr.content_revision_id = o.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id;

CREATE VIEW v_mmo_server_content_build_health AS
SELECT
  cr.content_revision_key,
  cr.is_active,
  COUNT(DISTINCT bi.content_build_import_id) AS build_import_count,
  SUM(CASE WHEN bi.import_status = 'imported' THEN 1 ELSE 0 END) AS imported_count,
  SUM(CASE WHEN bi.import_status = 'failed' THEN 1 ELSE 0 END) AS failed_count,
  (SELECT COUNT(*) FROM mmo_server_world_zen_entities z WHERE z.content_revision_id = cr.content_revision_id) AS zen_entity_count,
  (SELECT COUNT(*) FROM mmo_server_world_zen_entities z WHERE z.content_revision_id = cr.content_revision_id AND z.entity_kind = 'waypoint') AS waypoint_count,
  (SELECT COUNT(*) FROM mmo_server_world_zen_entities z WHERE z.content_revision_id = cr.content_revision_id AND z.entity_kind = 'freepoint') AS freepoint_count,
  (SELECT COUNT(*) FROM mmo_server_world_zen_entities z WHERE z.content_revision_id = cr.content_revision_id AND z.entity_kind = 'vob') AS vob_count,
  (SELECT COUNT(*) FROM mmo_server_daedalus_symbols s WHERE s.content_revision_id = cr.content_revision_id) AS daedalus_symbol_count,
  (SELECT COUNT(*) FROM mmo_server_daedalus_npc_templates n WHERE n.content_revision_id = cr.content_revision_id) AS npc_template_count,
  (SELECT COUNT(*) FROM mmo_server_dialog_outputs o WHERE o.content_revision_id = cr.content_revision_id) AS dialog_output_count,
  MAX(bi.updated_at) AS last_build_update_at
FROM content_revisions cr
LEFT JOIN mmo_server_content_build_imports bi ON bi.content_revision_id = cr.content_revision_id
GROUP BY cr.content_revision_key, cr.is_active, cr.content_revision_id;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES (
  'server/sql/step208_server_content_build_indexes.sql',
  'step208_server_content_build_indexes_v1',
  'Step208: server content build result indexes for future ZEN/DAT/OU importers'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);
