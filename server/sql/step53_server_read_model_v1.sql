-- Step53: physical typed read-model tables for MMO server materialization.
-- This patch intentionally creates no SQL views and no JSON columns.
-- It is additive: old bridge tables/procedures stay intact until the real server replaces them.

CREATE TABLE IF NOT EXISTS mmo_server_read_model_meta (
  model_key             VARCHAR(96)  NOT NULL,
  model_version         INT          NOT NULL,
  source_database       VARCHAR(128) NOT NULL,
  rebuilt_at            TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  source_event_count    BIGINT       NOT NULL DEFAULT 0,
  source_max_event_id   BIGINT       NULL,
  notes                 VARCHAR(512) NULL,
  PRIMARY KEY (model_key)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_character_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  character_key         VARCHAR(128) NOT NULL,
  account_key           VARCHAR(128) NULL,
  display_name          VARCHAR(255) NULL,
  world_name            VARCHAR(128) NOT NULL DEFAULT 'UNKNOWN',
  pos_x                 DOUBLE NULL,
  pos_y                 DOUBLE NULL,
  pos_z                 DOUBLE NULL,
  angle_y               DOUBLE NULL,
  health_current        INT NULL,
  health_max            INT NULL,
  mana_current          INT NULL,
  mana_max              INT NULL,
  level_value           INT NULL,
  experience_value      BIGINT NULL,
  experience_next       BIGINT NULL,
  learning_points       INT NULL,
  gold_amount           BIGINT NULL,
  lifecycle_state       VARCHAR(32) NOT NULL DEFAULT 'active',
  source_updated_at     TIMESTAMP(6) NULL,
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, character_key),
  KEY idx_mmo_srv_char_world (world_name),
  KEY idx_mmo_srv_char_account (account_key),
  KEY idx_mmo_srv_char_lifecycle (lifecycle_state)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_character_inventory_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  character_key         VARCHAR(128) NOT NULL,
  item_instance_key     VARCHAR(128) NOT NULL,
  item_template_key     VARCHAR(128) NULL,
  display_name          VARCHAR(255) NULL,
  amount                BIGINT NOT NULL DEFAULT 1,
  equipped              TINYINT(1) NOT NULL DEFAULT 0,
  slot_key              VARCHAR(64) NULL,
  bind_state            VARCHAR(32) NULL,
  lifecycle_state       VARCHAR(32) NOT NULL DEFAULT 'active',
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, character_key, item_instance_key),
  KEY idx_mmo_srv_char_inv_item (item_template_key),
  KEY idx_mmo_srv_char_inv_equipped (character_key, equipped, slot_key),
  KEY idx_mmo_srv_char_inv_lifecycle (lifecycle_state)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_character_quest_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  character_key         VARCHAR(128) NOT NULL,
  quest_key             VARCHAR(191) NOT NULL,
  quest_name            VARCHAR(255) NULL,
  status_key            VARCHAR(64) NOT NULL DEFAULT 'unknown',
  entry_count           INT NOT NULL DEFAULT 0,
  updated_at            TIMESTAMP(6) NULL,
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, character_key, quest_key),
  KEY idx_mmo_srv_char_quest_status (character_key, status_key),
  KEY idx_mmo_srv_char_quest_name (quest_name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_known_dialog_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  character_key         VARCHAR(128) NOT NULL,
  npc_symbol_name       VARCHAR(191) NOT NULL,
  info_symbol_name      VARCHAR(191) NOT NULL,
  availability_state    VARCHAR(64) NULL,
  first_seen_tick       BIGINT NULL,
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, character_key, npc_symbol_name, info_symbol_name),
  KEY idx_mmo_srv_known_dialog_info (info_symbol_name),
  KEY idx_mmo_srv_known_dialog_npc (npc_symbol_name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_world_entity_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  world_name            VARCHAR(128) NOT NULL DEFAULT 'UNKNOWN',
  entity_key            VARCHAR(191) NOT NULL,
  entity_kind           VARCHAR(64) NOT NULL DEFAULT 'unknown',
  template_key          VARCHAR(191) NULL,
  script_symbol_name    VARCHAR(191) NULL,
  display_name          VARCHAR(255) NULL,
  active                TINYINT(1) NOT NULL DEFAULT 1,
  dead                  TINYINT(1) NOT NULL DEFAULT 0,
  health_current        INT NULL,
  health_max            INT NULL,
  pos_x                 DOUBLE NULL,
  pos_y                 DOUBLE NULL,
  pos_z                 DOUBLE NULL,
  angle_y               DOUBLE NULL,
  current_waypoint_key  VARCHAR(191) NULL,
  current_waypoint_name VARCHAR(191) NULL,
  lifecycle_state       VARCHAR(32) NOT NULL DEFAULT 'active',
  source_updated_at     TIMESTAMP(6) NULL,
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, world_name, entity_key),
  KEY idx_mmo_srv_entity_kind (world_name, entity_kind, active, dead),
  KEY idx_mmo_srv_entity_template (template_key),
  KEY idx_mmo_srv_entity_symbol (script_symbol_name),
  KEY idx_mmo_srv_entity_waypoint (world_name, current_waypoint_key),
  KEY idx_mmo_srv_entity_lifecycle (lifecycle_state)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_world_inventory_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  world_name            VARCHAR(128) NOT NULL DEFAULT 'UNKNOWN',
  owner_key             VARCHAR(191) NOT NULL,
  owner_kind            VARCHAR(64) NOT NULL DEFAULT 'world',
  item_instance_key     VARCHAR(128) NOT NULL,
  item_template_key     VARCHAR(128) NULL,
  display_name          VARCHAR(255) NULL,
  amount                BIGINT NOT NULL DEFAULT 1,
  pos_x                 DOUBLE NULL,
  pos_y                 DOUBLE NULL,
  pos_z                 DOUBLE NULL,
  lifecycle_state       VARCHAR(32) NOT NULL DEFAULT 'active',
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, world_name, owner_key, item_instance_key),
  KEY idx_mmo_srv_world_inv_item (item_template_key),
  KEY idx_mmo_srv_world_inv_owner (world_name, owner_kind, owner_key),
  KEY idx_mmo_srv_world_inv_lifecycle (lifecycle_state)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_interactive_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  world_name            VARCHAR(128) NOT NULL DEFAULT 'UNKNOWN',
  interactive_key       VARCHAR(191) NOT NULL,
  display_name          VARCHAR(255) NULL,
  focus_name            VARCHAR(191) NULL,
  state_value           INT NULL,
  locked                TINYINT(1) NOT NULL DEFAULT 0,
  opened                TINYINT(1) NOT NULL DEFAULT 0,
  container             TINYINT(1) NOT NULL DEFAULT 0,
  door                  TINYINT(1) NOT NULL DEFAULT 0,
  active                TINYINT(1) NOT NULL DEFAULT 1,
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, world_name, interactive_key),
  KEY idx_mmo_srv_interactive_flags (world_name, container, door, locked, opened)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_script_int_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  scope_key             VARCHAR(64)  NOT NULL,
  owner_key             VARCHAR(191) NOT NULL DEFAULT '',
  symbol_name           VARCHAR(191) NOT NULL,
  int_value             BIGINT NULL,
  category_key          VARCHAR(64) NULL,
  updated_at            TIMESTAMP(6) NULL,
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, scope_key, owner_key, symbol_name),
  KEY idx_mmo_srv_script_symbol (symbol_name),
  KEY idx_mmo_srv_script_category (category_key)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_world_clock_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  world_name            VARCHAR(128) NOT NULL DEFAULT 'UNKNOWN',
  day_value             INT NULL,
  hour_value            INT NULL,
  minute_value          INT NULL,
  absolute_minute       BIGINT NULL,
  source_reason         VARCHAR(128) NULL,
  updated_at            TIMESTAMP(6) NULL,
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, world_name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_waypoint_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  world_name            VARCHAR(128) NOT NULL DEFAULT 'UNKNOWN',
  waypoint_key          VARCHAR(191) NOT NULL,
  waypoint_name         VARCHAR(191) NULL,
  kind_key              VARCHAR(64) NULL,
  pos_x                 DOUBLE NULL,
  pos_y                 DOUBLE NULL,
  pos_z                 DOUBLE NULL,
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, world_name, waypoint_key),
  KEY idx_mmo_srv_waypoint_name (world_name, waypoint_name),
  KEY idx_mmo_srv_waypoint_kind (world_name, kind_key)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_waypoint_edge_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  world_name            VARCHAR(128) NOT NULL DEFAULT 'UNKNOWN',
  edge_key              VARCHAR(191) NOT NULL,
  from_waypoint_key     VARCHAR(191) NOT NULL,
  to_waypoint_key       VARCHAR(191) NOT NULL,
  distance_value        DOUBLE NULL,
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, world_name, edge_key),
  KEY idx_mmo_srv_way_edge_from (world_name, from_waypoint_key),
  KEY idx_mmo_srv_way_edge_to (world_name, to_waypoint_key)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
