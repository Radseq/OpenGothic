-- Step211: separate content-build database for server-owned ZEN/DAT/OU imports.
--
-- Runtime MMO state and content-build results have different lifecycles.
-- The runtime database stores players, active worlds, events and durable MMO
-- state. This database stores parser/build output from the server's own Gothic
-- content pack copy. Future parser jobs write here first; the runtime DB should
-- only consume a validated, ready content revision.

CREATE DATABASE IF NOT EXISTS mmo_content_build
  DEFAULT CHARACTER SET utf8mb4
  DEFAULT COLLATE utf8mb4_0900_ai_ci;

USE mmo_content_build;

CREATE TABLE IF NOT EXISTS content_build_schema_versions (
  migration_key VARCHAR(255) NOT NULL,
  checksum CHAR(64) NOT NULL,
  description TEXT NOT NULL,
  applied_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (migration_key),
  CONSTRAINT content_build_schema_versions_checksum_ck CHECK (REGEXP_LIKE(checksum, '^[0-9a-f]{64}$'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS content_build_revisions (
  content_revision_key VARCHAR(191) NOT NULL,
  game_code VARCHAR(64) NOT NULL DEFAULT 'gothic2-notr',
  manifest_hash CHAR(64) NULL,
  source_root_label VARCHAR(512) NOT NULL DEFAULT '',
  build_status VARCHAR(32) NOT NULL DEFAULT 'draft',
  source_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (content_revision_key),
  KEY ix_content_build_revisions_game_status (game_code, build_status),
  KEY ix_content_build_revisions_manifest_hash (manifest_hash),
  CONSTRAINT content_build_revisions_manifest_hash_ck CHECK (manifest_hash IS NULL OR REGEXP_LIKE(manifest_hash, '^[0-9a-f]{64}$')),
  CONSTRAINT content_build_revisions_status_ck CHECK (build_status IN ('draft', 'building', 'ready', 'failed', 'retired')),
  CONSTRAINT content_build_revisions_payload_json_ck CHECK (JSON_VALID(source_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS content_build_files (
  content_revision_key VARCHAR(191) NOT NULL,
  logical_path VARCHAR(512) NOT NULL,
  source_kind VARCHAR(48) NOT NULL DEFAULT 'other',
  file_role VARCHAR(64) NOT NULL DEFAULT 'other',
  byte_size BIGINT UNSIGNED NOT NULL DEFAULT 0,
  sha256 CHAR(64) NOT NULL,
  required_for_server_authority TINYINT(1) NOT NULL DEFAULT 0,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (content_revision_key, logical_path),
  KEY ix_content_build_files_kind_role (content_revision_key, source_kind, file_role),
  KEY ix_content_build_files_sha (sha256),
  CONSTRAINT content_build_files_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT content_build_files_kind_ck CHECK (source_kind IN ('world_zen', 'scripts_dat', 'dialog_ou', 'archive_vdf', 'archive_mod', 'texture', 'mesh', 'sound', 'other')),
  CONSTRAINT content_build_files_sha_ck CHECK (REGEXP_LIKE(sha256, '^[0-9a-f]{64}$')),
  CONSTRAINT content_build_files_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS content_build_imports (
  content_build_import_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_revision_key VARCHAR(191) NOT NULL,
  importer_key VARCHAR(96) NOT NULL,
  source_kind VARCHAR(48) NOT NULL,
  source_logical_path VARCHAR(512) NOT NULL,
  source_logical_path_hash CHAR(64) GENERATED ALWAYS AS (SHA2(source_logical_path, 256)) STORED,
  source_sha256 CHAR(64) NULL,
  import_status VARCHAR(32) NOT NULL DEFAULT 'imported',
  item_count INT NOT NULL DEFAULT 0,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  imported_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (content_build_import_id),
  UNIQUE KEY content_build_import_uk (content_revision_key, importer_key, source_logical_path_hash, source_sha256),
  KEY ix_content_build_import_revision (content_revision_key, importer_key, import_status),
  KEY ix_content_build_import_source_path (content_revision_key, source_logical_path_hash),
  CONSTRAINT content_build_import_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT content_build_import_sha_ck CHECK (source_sha256 IS NULL OR REGEXP_LIKE(source_sha256, '^[0-9a-f]{64}$')),
  CONSTRAINT content_build_import_status_ck CHECK (import_status IN ('imported', 'partial', 'failed', 'skipped')),
  CONSTRAINT content_build_import_kind_ck CHECK (source_kind IN ('world_zen', 'scripts_dat', 'dialog_ou', 'archive_vdf', 'archive_mod', 'other')),
  CONSTRAINT content_build_import_count_ck CHECK (item_count >= 0),
  CONSTRAINT content_build_import_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS content_build_parser_errors (
  parser_error_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_revision_key VARCHAR(191) NOT NULL,
  content_build_import_id BINARY(16) NULL,
  severity VARCHAR(16) NOT NULL DEFAULT 'warning',
  error_scope VARCHAR(64) NOT NULL DEFAULT 'parser',
  error_code VARCHAR(96) NOT NULL DEFAULT '',
  source_logical_path VARCHAR(512) NOT NULL DEFAULT '',
  message_text TEXT NOT NULL,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (parser_error_id),
  KEY ix_content_build_parser_errors_revision (content_revision_key, severity, error_scope),
  KEY ix_content_build_parser_errors_import (content_build_import_id),
  CONSTRAINT content_build_parser_errors_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT content_build_parser_errors_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES content_build_imports(content_build_import_id) ON DELETE SET NULL,
  CONSTRAINT content_build_parser_errors_severity_ck CHECK (severity IN ('info', 'warning', 'error', 'fatal')),
  CONSTRAINT content_build_parser_errors_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS world_zen_entities (
  zen_entity_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL,
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
  radius_value DOUBLE NULL,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (zen_entity_id),
  UNIQUE KEY world_zen_entity_uk (content_revision_key, world_name, entity_kind, entity_key),
  KEY ix_world_zen_entity_import (content_build_import_id),
  KEY ix_world_zen_entity_kind (content_revision_key, world_name, entity_kind),
  KEY ix_world_zen_entity_pos (content_revision_key, world_name, pos_x, pos_y, pos_z),
  CONSTRAINT world_zen_entity_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT world_zen_entity_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT world_zen_entity_kind_ck CHECK (entity_kind IN ('world', 'waypoint', 'freepoint', 'vob', 'trigger', 'mover', 'spawn', 'sound', 'light', 'other')),
  CONSTRAINT world_zen_entity_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS world_waypoint_edges (
  waypoint_edge_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL,
  world_name VARCHAR(191) NOT NULL,
  from_waypoint_key VARCHAR(191) NOT NULL,
  to_waypoint_key VARCHAR(191) NOT NULL,
  edge_identity_hash CHAR(64) GENERATED ALWAYS AS (SHA2(CONCAT_WS('|', world_name, from_waypoint_key, to_waypoint_key), 256)) STORED,
  travel_cost DOUBLE NOT NULL DEFAULT 1,
  edge_flags VARCHAR(191) NOT NULL DEFAULT '',
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (waypoint_edge_id),
  UNIQUE KEY world_waypoint_edge_uk (content_revision_key, edge_identity_hash),
  KEY ix_world_waypoint_edge_from (content_revision_key, world_name, from_waypoint_key),
  KEY ix_world_waypoint_edge_to (content_revision_key, world_name, to_waypoint_key),
  CONSTRAINT world_waypoint_edge_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT world_waypoint_edge_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT world_waypoint_edge_cost_ck CHECK (travel_cost >= 0),
  CONSTRAINT world_waypoint_edge_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS daedalus_symbols (
  daedalus_symbol_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL,
  symbol_name VARCHAR(191) NOT NULL,
  symbol_kind VARCHAR(48) NOT NULL DEFAULT 'unknown',
  data_type VARCHAR(48) NOT NULL DEFAULT '',
  parent_symbol VARCHAR(191) NOT NULL DEFAULT '',
  ordinal INT NOT NULL DEFAULT 0,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (daedalus_symbol_id),
  UNIQUE KEY daedalus_symbol_uk (content_revision_key, symbol_name),
  KEY ix_daedalus_symbol_import (content_build_import_id),
  KEY ix_daedalus_symbol_kind (content_revision_key, symbol_kind),
  KEY ix_daedalus_symbol_parent (content_revision_key, parent_symbol),
  CONSTRAINT daedalus_symbol_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT daedalus_symbol_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT daedalus_symbol_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS daedalus_npc_templates (
  daedalus_npc_template_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL,
  npc_instance VARCHAR(191) NOT NULL,
  display_name VARCHAR(191) NOT NULL DEFAULT '',
  guild VARCHAR(96) NOT NULL DEFAULT '',
  level_value INT NULL,
  routine_symbol VARCHAR(191) NOT NULL DEFAULT '',
  perception_symbol VARCHAR(191) NOT NULL DEFAULT '',
  fight_tactic VARCHAR(96) NOT NULL DEFAULT '',
  voice_symbol VARCHAR(191) NOT NULL DEFAULT '',
  attributes_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (daedalus_npc_template_id),
  UNIQUE KEY daedalus_npc_template_uk (content_revision_key, npc_instance),
  KEY ix_daedalus_npc_template_import (content_build_import_id),
  KEY ix_daedalus_npc_template_guild (content_revision_key, guild),
  CONSTRAINT daedalus_npc_template_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT daedalus_npc_template_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT daedalus_npc_template_attributes_json_ck CHECK (JSON_VALID(attributes_payload)),
  CONSTRAINT daedalus_npc_template_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS daedalus_item_templates (
  daedalus_item_template_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL,
  item_instance VARCHAR(191) NOT NULL,
  display_name VARCHAR(191) NOT NULL DEFAULT '',
  item_category VARCHAR(96) NOT NULL DEFAULT '',
  main_flag INT NULL,
  flags_value BIGINT NULL,
  value_amount INT NULL,
  damage_total INT NULL,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (daedalus_item_template_id),
  UNIQUE KEY daedalus_item_template_uk (content_revision_key, item_instance),
  KEY ix_daedalus_item_template_import (content_build_import_id),
  KEY ix_daedalus_item_template_category (content_revision_key, item_category),
  CONSTRAINT daedalus_item_template_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT daedalus_item_template_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT daedalus_item_template_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS daedalus_routines (
  daedalus_routine_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL,
  npc_instance VARCHAR(191) NOT NULL,
  routine_symbol VARCHAR(191) NOT NULL,
  day_minute_start SMALLINT UNSIGNED NULL,
  day_minute_end SMALLINT UNSIGNED NULL,
  target_point_key VARCHAR(191) NOT NULL DEFAULT '',
  routine_identity_hash CHAR(64) GENERATED ALWAYS AS (SHA2(CONCAT_WS('|', npc_instance, routine_symbol, COALESCE(CAST(day_minute_start AS CHAR), ''), target_point_key), 256)) STORED,
  action_symbol VARCHAR(191) NOT NULL DEFAULT '',
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (daedalus_routine_id),
  UNIQUE KEY daedalus_routine_uk (content_revision_key, routine_identity_hash),
  KEY ix_daedalus_routine_npc (content_revision_key, npc_instance),
  KEY ix_daedalus_routine_point (content_revision_key, target_point_key),
  CONSTRAINT daedalus_routine_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT daedalus_routine_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT daedalus_routine_start_ck CHECK (day_minute_start IS NULL OR day_minute_start < 1440),
  CONSTRAINT daedalus_routine_end_ck CHECK (day_minute_end IS NULL OR day_minute_end < 1440),
  CONSTRAINT daedalus_routine_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS daedalus_perception_bindings (
  daedalus_perception_binding_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL,
  owner_symbol VARCHAR(191) NOT NULL DEFAULT '',
  owner_kind VARCHAR(32) NOT NULL DEFAULT 'npc',
  perception_kind VARCHAR(64) NOT NULL,
  function_symbol VARCHAR(191) NOT NULL,
  priority_value INT NOT NULL DEFAULT 0,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (daedalus_perception_binding_id),
  UNIQUE KEY daedalus_perception_binding_uk (content_revision_key, owner_symbol, owner_kind, perception_kind, function_symbol),
  KEY ix_daedalus_perception_binding_kind (content_revision_key, perception_kind),
  KEY ix_daedalus_perception_binding_function (content_revision_key, function_symbol),
  CONSTRAINT daedalus_perception_binding_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT daedalus_perception_binding_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT daedalus_perception_binding_owner_kind_ck CHECK (owner_kind IN ('npc', 'guild', 'global', 'other')),
  CONSTRAINT daedalus_perception_binding_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS dialog_outputs (
  dialog_output_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL,
  output_name VARCHAR(191) NOT NULL,
  text_value TEXT NULL,
  audio_ref VARCHAR(255) NOT NULL DEFAULT '',
  speaker_symbol VARCHAR(191) NOT NULL DEFAULT '',
  target_symbol VARCHAR(191) NOT NULL DEFAULT '',
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (dialog_output_id),
  UNIQUE KEY dialog_output_uk (content_revision_key, output_name),
  KEY ix_dialog_output_import (content_build_import_id),
  KEY ix_dialog_output_speaker (content_revision_key, speaker_symbol),
  CONSTRAINT dialog_output_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT dialog_output_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT dialog_output_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS dialog_infos (
  dialog_info_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL,
  info_symbol VARCHAR(191) NOT NULL,
  npc_instance VARCHAR(191) NOT NULL DEFAULT '',
  condition_symbol VARCHAR(191) NOT NULL DEFAULT '',
  information_symbol VARCHAR(191) NOT NULL DEFAULT '',
  permanent_flag TINYINT(1) NOT NULL DEFAULT 0,
  important_flag TINYINT(1) NOT NULL DEFAULT 0,
  trade_flag TINYINT(1) NOT NULL DEFAULT 0,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (dialog_info_id),
  UNIQUE KEY dialog_info_uk (content_revision_key, info_symbol),
  KEY ix_dialog_info_npc (content_revision_key, npc_instance),
  KEY ix_dialog_info_condition (content_revision_key, condition_symbol),
  CONSTRAINT dialog_info_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT dialog_info_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT dialog_info_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS content_build_runtime_exports (
  runtime_export_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_revision_key VARCHAR(191) NOT NULL,
  export_kind VARCHAR(64) NOT NULL,
  target_runtime_schema VARCHAR(191) NOT NULL DEFAULT '',
  export_status VARCHAR(32) NOT NULL DEFAULT 'planned',
  payload_sha256 CHAR(64) NULL,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (runtime_export_id),
  UNIQUE KEY content_build_runtime_export_uk (content_revision_key, export_kind, target_runtime_schema),
  KEY ix_content_build_runtime_export_status (content_revision_key, export_status),
  CONSTRAINT content_build_runtime_export_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT content_build_runtime_export_status_ck CHECK (export_status IN ('planned', 'generated', 'validated', 'published', 'failed', 'retired')),
  CONSTRAINT content_build_runtime_export_sha_ck CHECK (payload_sha256 IS NULL OR REGEXP_LIKE(payload_sha256, '^[0-9a-f]{64}$')),
  CONSTRAINT content_build_runtime_export_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP VIEW IF EXISTS v_content_build_health;
DROP VIEW IF EXISTS v_content_build_imports;
DROP VIEW IF EXISTS v_world_zen_entities;
DROP VIEW IF EXISTS v_world_waypoint_edges;
DROP VIEW IF EXISTS v_daedalus_symbols;
DROP VIEW IF EXISTS v_daedalus_npc_templates;
DROP VIEW IF EXISTS v_daedalus_item_templates;
DROP VIEW IF EXISTS v_daedalus_routines;
DROP VIEW IF EXISTS v_daedalus_perception_bindings;
DROP VIEW IF EXISTS v_dialog_outputs;
DROP VIEW IF EXISTS v_dialog_infos;

CREATE VIEW v_content_build_imports AS
SELECT
  BIN_TO_UUID(i.content_build_import_id, 1) AS content_build_import_uuid,
  r.game_code,
  r.content_revision_key,
  r.build_status,
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
FROM content_build_imports i
JOIN content_build_revisions r ON r.content_revision_key = i.content_revision_key;

CREATE VIEW v_world_zen_entities AS
SELECT
  BIN_TO_UUID(e.zen_entity_id, 1) AS zen_entity_uuid,
  BIN_TO_UUID(e.content_build_import_id, 1) AS content_build_import_uuid,
  r.game_code,
  e.content_revision_key,
  r.build_status,
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
  e.radius_value,
  e.raw_payload
FROM world_zen_entities e
JOIN content_build_revisions r ON r.content_revision_key = e.content_revision_key;

CREATE VIEW v_world_waypoint_edges AS
SELECT
  BIN_TO_UUID(edge.waypoint_edge_id, 1) AS waypoint_edge_uuid,
  BIN_TO_UUID(edge.content_build_import_id, 1) AS content_build_import_uuid,
  r.game_code,
  edge.content_revision_key,
  r.build_status,
  edge.world_name,
  edge.from_waypoint_key,
  edge.to_waypoint_key,
  edge.travel_cost,
  edge.edge_flags,
  edge.raw_payload
FROM world_waypoint_edges edge
JOIN content_build_revisions r ON r.content_revision_key = edge.content_revision_key;

CREATE VIEW v_daedalus_symbols AS
SELECT
  BIN_TO_UUID(s.daedalus_symbol_id, 1) AS daedalus_symbol_uuid,
  BIN_TO_UUID(s.content_build_import_id, 1) AS content_build_import_uuid,
  r.game_code,
  s.content_revision_key,
  r.build_status,
  s.symbol_name,
  s.symbol_kind,
  s.data_type,
  s.parent_symbol,
  s.ordinal,
  s.raw_payload
FROM daedalus_symbols s
JOIN content_build_revisions r ON r.content_revision_key = s.content_revision_key;

CREATE VIEW v_daedalus_npc_templates AS
SELECT
  BIN_TO_UUID(n.daedalus_npc_template_id, 1) AS daedalus_npc_template_uuid,
  BIN_TO_UUID(n.content_build_import_id, 1) AS content_build_import_uuid,
  r.game_code,
  n.content_revision_key,
  r.build_status,
  n.npc_instance,
  n.display_name,
  n.guild,
  n.level_value,
  n.routine_symbol,
  n.perception_symbol,
  n.fight_tactic,
  n.voice_symbol,
  n.attributes_payload,
  n.raw_payload
FROM daedalus_npc_templates n
JOIN content_build_revisions r ON r.content_revision_key = n.content_revision_key;

CREATE VIEW v_daedalus_item_templates AS
SELECT
  BIN_TO_UUID(it.daedalus_item_template_id, 1) AS daedalus_item_template_uuid,
  BIN_TO_UUID(it.content_build_import_id, 1) AS content_build_import_uuid,
  r.game_code,
  it.content_revision_key,
  r.build_status,
  it.item_instance,
  it.display_name,
  it.item_category,
  it.main_flag,
  it.flags_value,
  it.value_amount,
  it.damage_total,
  it.raw_payload
FROM daedalus_item_templates it
JOIN content_build_revisions r ON r.content_revision_key = it.content_revision_key;

CREATE VIEW v_daedalus_routines AS
SELECT
  BIN_TO_UUID(rt.daedalus_routine_id, 1) AS daedalus_routine_uuid,
  BIN_TO_UUID(rt.content_build_import_id, 1) AS content_build_import_uuid,
  r.game_code,
  rt.content_revision_key,
  r.build_status,
  rt.npc_instance,
  rt.routine_symbol,
  rt.day_minute_start,
  rt.day_minute_end,
  rt.target_point_key,
  rt.action_symbol,
  rt.raw_payload
FROM daedalus_routines rt
JOIN content_build_revisions r ON r.content_revision_key = rt.content_revision_key;

CREATE VIEW v_daedalus_perception_bindings AS
SELECT
  BIN_TO_UUID(p.daedalus_perception_binding_id, 1) AS daedalus_perception_binding_uuid,
  BIN_TO_UUID(p.content_build_import_id, 1) AS content_build_import_uuid,
  r.game_code,
  p.content_revision_key,
  r.build_status,
  p.owner_symbol,
  p.owner_kind,
  p.perception_kind,
  p.function_symbol,
  p.priority_value,
  p.raw_payload
FROM daedalus_perception_bindings p
JOIN content_build_revisions r ON r.content_revision_key = p.content_revision_key;

CREATE VIEW v_dialog_outputs AS
SELECT
  BIN_TO_UUID(o.dialog_output_id, 1) AS dialog_output_uuid,
  BIN_TO_UUID(o.content_build_import_id, 1) AS content_build_import_uuid,
  r.game_code,
  o.content_revision_key,
  r.build_status,
  o.output_name,
  o.text_value,
  o.audio_ref,
  o.speaker_symbol,
  o.target_symbol,
  o.raw_payload
FROM dialog_outputs o
JOIN content_build_revisions r ON r.content_revision_key = o.content_revision_key;

CREATE VIEW v_dialog_infos AS
SELECT
  BIN_TO_UUID(i.dialog_info_id, 1) AS dialog_info_uuid,
  BIN_TO_UUID(i.content_build_import_id, 1) AS content_build_import_uuid,
  r.game_code,
  i.content_revision_key,
  r.build_status,
  i.info_symbol,
  i.npc_instance,
  i.condition_symbol,
  i.information_symbol,
  i.permanent_flag,
  i.important_flag,
  i.trade_flag,
  i.raw_payload
FROM dialog_infos i
JOIN content_build_revisions r ON r.content_revision_key = i.content_revision_key;

CREATE VIEW v_content_build_health AS
SELECT
  r.game_code,
  r.content_revision_key,
  r.build_status,
  r.manifest_hash,
  COUNT(DISTINCT bi.content_build_import_id) AS build_import_count,
  SUM(CASE WHEN bi.import_status = 'imported' THEN 1 ELSE 0 END) AS imported_count,
  SUM(CASE WHEN bi.import_status = 'failed' THEN 1 ELSE 0 END) AS failed_count,
  (SELECT COUNT(*) FROM content_build_files f WHERE f.content_revision_key = r.content_revision_key) AS file_count,
  (SELECT COUNT(*) FROM content_build_files f WHERE f.content_revision_key = r.content_revision_key AND f.required_for_server_authority = 1) AS required_file_count,
  (SELECT COUNT(*) FROM content_build_parser_errors pe WHERE pe.content_revision_key = r.content_revision_key AND pe.severity IN ('error', 'fatal')) AS blocking_error_count,
  (SELECT COUNT(*) FROM world_zen_entities z WHERE z.content_revision_key = r.content_revision_key) AS zen_entity_count,
  (SELECT COUNT(*) FROM world_zen_entities z WHERE z.content_revision_key = r.content_revision_key AND z.entity_kind = 'waypoint') AS waypoint_count,
  (SELECT COUNT(*) FROM world_zen_entities z WHERE z.content_revision_key = r.content_revision_key AND z.entity_kind = 'freepoint') AS freepoint_count,
  (SELECT COUNT(*) FROM world_zen_entities z WHERE z.content_revision_key = r.content_revision_key AND z.entity_kind = 'vob') AS vob_count,
  (SELECT COUNT(*) FROM world_waypoint_edges edge WHERE edge.content_revision_key = r.content_revision_key) AS waypoint_edge_count,
  (SELECT COUNT(*) FROM daedalus_symbols s WHERE s.content_revision_key = r.content_revision_key) AS daedalus_symbol_count,
  (SELECT COUNT(*) FROM daedalus_npc_templates n WHERE n.content_revision_key = r.content_revision_key) AS npc_template_count,
  (SELECT COUNT(*) FROM daedalus_item_templates it WHERE it.content_revision_key = r.content_revision_key) AS item_template_count,
  (SELECT COUNT(*) FROM daedalus_routines rt WHERE rt.content_revision_key = r.content_revision_key) AS routine_count,
  (SELECT COUNT(*) FROM daedalus_perception_bindings p WHERE p.content_revision_key = r.content_revision_key) AS perception_binding_count,
  (SELECT COUNT(*) FROM dialog_outputs o WHERE o.content_revision_key = r.content_revision_key) AS dialog_output_count,
  (SELECT COUNT(*) FROM dialog_infos i WHERE i.content_revision_key = r.content_revision_key) AS dialog_info_count,
  MAX(bi.updated_at) AS last_build_update_at
FROM content_build_revisions r
LEFT JOIN content_build_imports bi ON bi.content_revision_key = r.content_revision_key
GROUP BY r.game_code, r.content_revision_key, r.build_status, r.manifest_hash;

INSERT INTO content_build_schema_versions(migration_key, checksum, description)
VALUES (
  'server/sql/step211_content_build_database.sql',
  SHA2('server/sql/step211_content_build_database.sql', 256),
  'Step211: separate mmo_content_build database for server-owned ZEN/DAT/OU parser outputs'
)
ON DUPLICATE KEY UPDATE
  checksum = VALUES(checksum),
  description = VALUES(description),
  applied_at = CURRENT_TIMESTAMP(6);
