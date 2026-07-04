-- Gothic MMO production database contract v1 for MySQL 8.0+.
-- This is the clean server-authoritative schema, not the runtime SQLite bridge.
-- MySQL target uses InnoDB, utf8mb4, BINARY(16) UUIDs and JSON columns.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE IF NOT EXISTS mmo_schema_versions (
  migration_key      VARCHAR(191) PRIMARY KEY,
  applied_at         TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  schema_contract    VARCHAR(191) NOT NULL,
  notes              TEXT NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

-- -----------------------------------------------------------------------------
-- Account / entitlement ownership
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS account_accounts (
  account_id         BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  account_name       VARCHAR(191) COLLATE utf8mb4_0900_ai_ci NOT NULL,
  email              VARCHAR(320) COLLATE utf8mb4_0900_ai_ci NULL,
  auth_provider      VARCHAR(64) NOT NULL DEFAULT 'local',
  external_subject   VARCHAR(191) NULL,
  password_hash      TEXT NULL,
  status             VARCHAR(32) NOT NULL DEFAULT 'active',
  flags              JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at         TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at         TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  UNIQUE KEY account_accounts_account_name_uk(account_name),
  UNIQUE KEY account_accounts_email_uk(email),
  UNIQUE KEY account_accounts_provider_subject_uk(auth_provider, external_subject),
  CONSTRAINT account_accounts_status_ck CHECK(status IN ('active','locked','banned','deleted')),
  CONSTRAINT account_accounts_external_subject_ck CHECK((auth_provider='local' AND external_subject IS NULL) OR (auth_provider<>'local' AND external_subject IS NOT NULL)),
  CONSTRAINT account_accounts_flags_json_ck CHECK(JSON_VALID(flags))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS account_entitlements (
  entitlement_id     BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  account_id         BINARY(16) NOT NULL,
  game_code          VARCHAR(32) NOT NULL,
  entitlement_key    VARCHAR(128) NOT NULL,
  source             VARCHAR(64) NOT NULL DEFAULT 'manual',
  status             VARCHAR(32) NOT NULL DEFAULT 'active',
  granted_at         TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  expires_at         TIMESTAMP(6) NULL,
  metadata           JSON NOT NULL DEFAULT (JSON_OBJECT()),
  UNIQUE KEY account_entitlements_unique_uk(account_id, game_code, entitlement_key),
  KEY ix_account_entitlements_account_status(account_id, status),
  CONSTRAINT account_entitlements_account_fk FOREIGN KEY(account_id) REFERENCES account_accounts(account_id) ON DELETE CASCADE,
  CONSTRAINT account_entitlements_status_ck CHECK(status IN ('active','revoked','expired')),
  CONSTRAINT account_entitlements_expiry_ck CHECK(expires_at IS NULL OR expires_at > granted_at),
  CONSTRAINT account_entitlements_metadata_json_ck CHECK(JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

-- -----------------------------------------------------------------------------
-- Content revisions / immutable templates
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS content_game_targets (
  game_target_id        BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  game_code             VARCHAR(32) NOT NULL,
  display_name          VARCHAR(191) NOT NULL,
  engine                VARCHAR(64) NOT NULL DEFAULT 'opengothic',
  save_format_version   INT NULL,
  created_at            TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY content_game_targets_code_uk(game_code),
  CONSTRAINT content_game_targets_code_ck CHECK(game_code IN ('g1','g2','g2notr'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS content_revisions (
  content_revision_id   BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  game_target_id        BINARY(16) NOT NULL,
  content_revision_key  VARCHAR(191) NOT NULL,
  script_symbols_hash   CHAR(64) NOT NULL,
  worlds_hash           CHAR(64) NOT NULL,
  items_hash            CHAR(64) NOT NULL,
  npcs_hash             CHAR(64) NOT NULL,
  migration_hash        CHAR(64) NOT NULL DEFAULT '',
  source_description    TEXT NOT NULL,
  is_active             BOOLEAN NOT NULL DEFAULT FALSE,
  active_target_key     BINARY(16) GENERATED ALWAYS AS (CASE WHEN is_active THEN game_target_id ELSE NULL END) STORED,
  created_at            TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY content_revisions_key_uk(content_revision_key),
  UNIQUE KEY content_revisions_game_key_uk(game_target_id, content_revision_key),
  UNIQUE KEY ux_content_revisions_one_active_per_target(active_target_key),
  KEY ix_content_revisions_target(game_target_id),
  CONSTRAINT content_revisions_game_target_fk FOREIGN KEY(game_target_id) REFERENCES content_game_targets(game_target_id) ON DELETE RESTRICT
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS content_world_templates (
  world_template_id         BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_revision_id       BINARY(16) NOT NULL,
  world_key                 VARCHAR(191) NOT NULL,
  world_name                VARCHAR(191) NOT NULL,
  zen_path                  VARCHAR(512) NOT NULL,
  baseline_hash             CHAR(64) NOT NULL,
  baseline_tick             BIGINT NOT NULL DEFAULT 0,
  baseline_world_time_ms    BIGINT NOT NULL DEFAULT 0,
  baseline_payload          JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at                TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY content_world_templates_uk(content_revision_id, world_key),
  CONSTRAINT content_world_templates_revision_fk FOREIGN KEY(content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT content_world_templates_tick_ck CHECK(baseline_tick >= 0),
  CONSTRAINT content_world_templates_time_ck CHECK(baseline_world_time_ms >= 0),
  CONSTRAINT content_world_templates_payload_json_ck CHECK(JSON_VALID(baseline_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS content_entity_templates (
  entity_template_id     BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_revision_id    BINARY(16) NOT NULL,
  entity_kind            VARCHAR(32) NOT NULL,
  engine_template_key    VARCHAR(191) NOT NULL,
  symbol_index           INT NULL,
  script_id              INT NULL,
  script_name            VARCHAR(191) NULL,
  display_name           VARCHAR(191) NULL,
  visual_key             VARCHAR(191) NULL,
  raw_payload            JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY content_entity_templates_uk(content_revision_id, entity_kind, engine_template_key),
  KEY ix_content_entity_templates_symbol(content_revision_id, entity_kind, symbol_index),
  CONSTRAINT content_entity_templates_revision_fk FOREIGN KEY(content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT content_entity_templates_kind_ck CHECK(entity_kind IN ('npc','creature','item','interactive','trigger','vob','waypoint')),
  CONSTRAINT content_entity_templates_payload_json_ck CHECK(JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS content_item_templates (
  item_template_id       BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_revision_id    BINARY(16) NOT NULL,
  item_template_key      VARCHAR(191) NOT NULL,
  symbol_index           INT NULL,
  script_name            VARCHAR(191) NULL,
  display_name           VARCHAR(191) NULL,
  classification         VARCHAR(32) NOT NULL DEFAULT 'unknown',
  stack_policy           VARCHAR(32) NOT NULL DEFAULT 'unknown',
  max_stack              INT NULL,
  value                  INT NULL,
  flags                  JSON NOT NULL DEFAULT (JSON_OBJECT()),
  raw_payload            JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY content_item_templates_uk(content_revision_id, item_template_key),
  KEY ix_content_item_templates_symbol(content_revision_id, symbol_index),
  CONSTRAINT content_item_templates_revision_fk FOREIGN KEY(content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT content_item_templates_classification_ck CHECK(classification IN ('currency','consumable','weapon','armor','jewelry','rune','scroll','ammo','quest','misc','unknown')),
  CONSTRAINT content_item_templates_stack_policy_ck CHECK(stack_policy IN ('currency','stackable','durable_instance','unique','quest_rule','unknown')),
  CONSTRAINT content_item_templates_max_stack_ck CHECK(max_stack IS NULL OR max_stack > 0),
  CONSTRAINT content_item_templates_flags_json_ck CHECK(JSON_VALID(flags)),
  CONSTRAINT content_item_templates_payload_json_ck CHECK(JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

-- -----------------------------------------------------------------------------
-- Realm / shard / world instances
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS realm_realms (
  realm_id                    BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  game_target_id              BINARY(16) NOT NULL,
  active_content_revision_id  BINARY(16) NOT NULL,
  realm_key                   VARCHAR(191) NOT NULL,
  display_name                VARCHAR(191) NOT NULL,
  status                      VARCHAR(32) NOT NULL DEFAULT 'offline',
  max_players                 INT NOT NULL DEFAULT 1000,
  created_at                  TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at                  TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  UNIQUE KEY realm_realms_key_uk(realm_key),
  KEY ix_realm_realms_target(game_target_id),
  KEY ix_realm_realms_revision(active_content_revision_id),
  CONSTRAINT realm_realms_target_fk FOREIGN KEY(game_target_id) REFERENCES content_game_targets(game_target_id) ON DELETE RESTRICT,
  CONSTRAINT realm_realms_revision_fk FOREIGN KEY(active_content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE RESTRICT,
  CONSTRAINT realm_realms_status_ck CHECK(status IN ('offline','maintenance','online','locked','retired')),
  CONSTRAINT realm_realms_max_players_ck CHECK(max_players > 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS realm_world_instances (
  world_instance_id           BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  realm_id                    BINARY(16) NOT NULL,
  world_template_id           BINARY(16) NOT NULL,
  world_instance_key          VARCHAR(191) NOT NULL,
  lifecycle_state             VARCHAR(32) NOT NULL DEFAULT 'active',
  generation                  INT NOT NULL DEFAULT 1,
  current_tick                BIGINT NOT NULL DEFAULT 0,
  current_world_time_ms       BIGINT NOT NULL DEFAULT 0,
  created_at                  TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at                  TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  UNIQUE KEY realm_world_instances_key_uk(world_instance_key),
  UNIQUE KEY realm_world_instances_realm_template_generation_uk(realm_id, world_template_id, generation),
  KEY ix_realm_world_instances_realm_state(realm_id, lifecycle_state),
  CONSTRAINT realm_world_instances_realm_fk FOREIGN KEY(realm_id) REFERENCES realm_realms(realm_id) ON DELETE CASCADE,
  CONSTRAINT realm_world_instances_template_fk FOREIGN KEY(world_template_id) REFERENCES content_world_templates(world_template_id) ON DELETE RESTRICT,
  CONSTRAINT realm_world_instances_lifecycle_ck CHECK(lifecycle_state IN ('creating','active','paused','archived','deleted')),
  CONSTRAINT realm_world_instances_generation_ck CHECK(generation > 0),
  CONSTRAINT realm_world_instances_tick_ck CHECK(current_tick >= 0),
  CONSTRAINT realm_world_instances_time_ck CHECK(current_world_time_ms >= 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

-- -----------------------------------------------------------------------------
-- Character state
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS characters (
  character_id             BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  account_id               BINARY(16) NOT NULL,
  realm_id                 BINARY(16) NOT NULL,
  current_world_instance_id BINARY(16) NULL,
  character_key            VARCHAR(191) NOT NULL,
  character_name           VARCHAR(191) COLLATE utf8mb4_0900_ai_ci NOT NULL,
  lifecycle_state          VARCHAR(32) NOT NULL DEFAULT 'active',
  created_at               TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at               TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  last_login_at            TIMESTAMP(6) NULL,
  last_logout_at           TIMESTAMP(6) NULL,
  metadata                 JSON NOT NULL DEFAULT (JSON_OBJECT()),
  UNIQUE KEY characters_key_uk(character_key),
  UNIQUE KEY characters_name_per_realm_uk(realm_id, character_name),
  KEY ix_characters_account_realm(account_id, realm_id, lifecycle_state),
  KEY ix_characters_current_world(current_world_instance_id),
  CONSTRAINT characters_account_fk FOREIGN KEY(account_id) REFERENCES account_accounts(account_id) ON DELETE RESTRICT,
  CONSTRAINT characters_realm_fk FOREIGN KEY(realm_id) REFERENCES realm_realms(realm_id) ON DELETE RESTRICT,
  CONSTRAINT characters_world_fk FOREIGN KEY(current_world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE RESTRICT,
  CONSTRAINT characters_lifecycle_ck CHECK(lifecycle_state IN ('creating','active','dead','deleted','migrated')),
  CONSTRAINT characters_metadata_json_ck CHECK(JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS character_positions (
  character_id           BINARY(16) PRIMARY KEY,
  world_instance_id      BINARY(16) NOT NULL,
  pos_x                  DOUBLE NOT NULL,
  pos_y                  DOUBLE NOT NULL,
  pos_z                  DOUBLE NOT NULL,
  rotation_yaw           DOUBLE NOT NULL DEFAULT 0,
  current_waypoint_key   VARCHAR(191) NULL,
  server_tick            BIGINT NOT NULL DEFAULT 0,
  row_version            BIGINT NOT NULL DEFAULT 0,
  updated_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  KEY ix_character_positions_world(world_instance_id, server_tick),
  CONSTRAINT character_positions_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT character_positions_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE RESTRICT,
  CONSTRAINT character_positions_tick_ck CHECK(server_tick >= 0),
  CONSTRAINT character_positions_version_ck CHECK(row_version >= 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS character_stats (
  character_id           BINARY(16) PRIMARY KEY,
  level                  INT NOT NULL DEFAULT 0,
  experience             BIGINT NOT NULL DEFAULT 0,
  experience_next        BIGINT NULL,
  learning_points        INT NOT NULL DEFAULT 0,
  health_current         INT NOT NULL DEFAULT 0,
  health_max             INT NOT NULL DEFAULT 0,
  mana_current           INT NOT NULL DEFAULT 0,
  mana_max               INT NOT NULL DEFAULT 0,
  strength               INT NOT NULL DEFAULT 0,
  dexterity              INT NOT NULL DEFAULT 0,
  guild                  INT NULL,
  true_guild             INT NULL,
  permanent_attitude     INT NULL,
  temporary_attitude     INT NULL,
  raw_stats              JSON NOT NULL DEFAULT (JSON_OBJECT()),
  row_version            BIGINT NOT NULL DEFAULT 0,
  updated_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  CONSTRAINT character_stats_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT character_stats_level_ck CHECK(level >= 0),
  CONSTRAINT character_stats_exp_ck CHECK(experience >= 0 AND (experience_next IS NULL OR experience_next >= 0)),
  CONSTRAINT character_stats_lp_ck CHECK(learning_points >= 0),
  CONSTRAINT character_stats_hp_ck CHECK(health_current >= 0 AND health_max >= 0 AND health_current <= health_max),
  CONSTRAINT character_stats_mana_ck CHECK(mana_current >= 0 AND mana_max >= 0 AND mana_current <= mana_max),
  CONSTRAINT character_stats_attrs_ck CHECK(strength >= 0 AND dexterity >= 0),
  CONSTRAINT character_stats_version_ck CHECK(row_version >= 0),
  CONSTRAINT character_stats_raw_json_ck CHECK(JSON_VALID(raw_stats))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS character_wallets (
  character_id           BINARY(16) NOT NULL,
  currency_key           VARCHAR(128) NOT NULL,
  amount                 DECIMAL(20,0) NOT NULL DEFAULT 0,
  updated_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(character_id, currency_key),
  CONSTRAINT character_wallets_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT character_wallets_amount_ck CHECK(amount >= 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

-- -----------------------------------------------------------------------------
-- Items, inventory and equipment
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS item_instances (
  item_instance_id       BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  realm_id               BINARY(16) NOT NULL,
  item_template_id       BINARY(16) NOT NULL,
  item_instance_key      VARCHAR(191) NOT NULL,
  owner_type             VARCHAR(32) NOT NULL DEFAULT 'none',
  owner_id               BINARY(16) NULL,
  quantity               INT NOT NULL DEFAULT 1,
  bind_state             VARCHAR(32) NOT NULL DEFAULT 'unbound',
  lifecycle_state        VARCHAR(32) NOT NULL DEFAULT 'active',
  raw_payload            JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  UNIQUE KEY item_instances_key_uk(item_instance_key),
  KEY ix_item_instances_realm_owner(realm_id, owner_type, owner_id),
  KEY ix_item_instances_template(item_template_id, lifecycle_state),
  CONSTRAINT item_instances_realm_fk FOREIGN KEY(realm_id) REFERENCES realm_realms(realm_id) ON DELETE CASCADE,
  CONSTRAINT item_instances_template_fk FOREIGN KEY(item_template_id) REFERENCES content_item_templates(item_template_id) ON DELETE RESTRICT,
  CONSTRAINT item_instances_owner_type_ck CHECK(owner_type IN ('none','character','world_entity','container','system')),
  CONSTRAINT item_instances_quantity_ck CHECK(quantity >= 0),
  CONSTRAINT item_instances_bind_state_ck CHECK(bind_state IN ('unbound','bind_on_pickup','bound_character','bound_account','quest_locked')),
  CONSTRAINT item_instances_lifecycle_ck CHECK(lifecycle_state IN ('active','consumed','destroyed','archived')),
  CONSTRAINT item_instances_payload_json_ck CHECK(JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS character_inventory (
  character_id           BINARY(16) NOT NULL,
  item_instance_id       BINARY(16) NOT NULL,
  bag_index              INT NULL,
  amount                 INT NOT NULL DEFAULT 1,
  source_amount          INT NULL,
  source_iterator_count  INT NULL,
  updated_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(character_id, item_instance_id),
  UNIQUE KEY character_inventory_bag_uk(character_id, bag_index),
  KEY ix_character_inventory_character(character_id),
  CONSTRAINT character_inventory_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT character_inventory_item_fk FOREIGN KEY(item_instance_id) REFERENCES item_instances(item_instance_id) ON DELETE RESTRICT,
  CONSTRAINT character_inventory_amount_ck CHECK(amount > 0),
  CONSTRAINT character_inventory_source_ck CHECK((source_amount IS NULL OR source_amount >= 0) AND (source_iterator_count IS NULL OR source_iterator_count >= 0))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS character_equipment (
  character_id           BINARY(16) NOT NULL,
  equipment_slot         VARCHAR(32) NOT NULL,
  item_instance_id       BINARY(16) NOT NULL,
  equipped_at            TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(character_id, equipment_slot),
  UNIQUE KEY character_equipment_item_uk(item_instance_id),
  CONSTRAINT character_equipment_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT character_equipment_item_fk FOREIGN KEY(item_instance_id) REFERENCES item_instances(item_instance_id) ON DELETE RESTRICT,
  CONSTRAINT character_equipment_slot_ck CHECK(equipment_slot IN ('weapon_melee','weapon_ranged','shield','armor','belt','amulet','ring_left','ring_right','rune','torch','unknown'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

-- -----------------------------------------------------------------------------
-- Character progress
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS character_quests (
  character_id           BINARY(16) NOT NULL,
  quest_key              VARCHAR(191) NOT NULL,
  section                VARCHAR(191) NOT NULL DEFAULT '',
  status                 VARCHAR(32) NOT NULL,
  entry_order            INT NOT NULL DEFAULT 0,
  text_entries           JSON NOT NULL DEFAULT (JSON_ARRAY()),
  updated_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(character_id, quest_key),
  KEY ix_character_quests_character_status(character_id, status),
  CONSTRAINT character_quests_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT character_quests_status_ck CHECK(status IN ('running','success','failed','obsolete')),
  CONSTRAINT character_quests_entry_order_ck CHECK(entry_order >= 0),
  CONSTRAINT character_quests_entries_json_ck CHECK(JSON_VALID(text_entries))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS character_known_dialogs (
  character_id           BINARY(16) NOT NULL,
  npc_key                VARCHAR(191) NOT NULL,
  info_key               VARCHAR(191) NOT NULL,
  known                  BOOLEAN NOT NULL DEFAULT TRUE,
  permanent              BOOLEAN NOT NULL DEFAULT FALSE,
  availability_state     VARCHAR(32) NOT NULL DEFAULT 'unknown',
  updated_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(character_id, npc_key, info_key),
  CONSTRAINT character_known_dialogs_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT character_known_dialogs_availability_ck CHECK(availability_state IN ('unknown','visible','hidden','consumed_hidden','repeatable_known'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS character_script_state (
  character_id           BINARY(16) NOT NULL,
  script_key             VARCHAR(191) NOT NULL,
  symbol_index           INT NULL,
  value_type             VARCHAR(32) NOT NULL,
  value_index            INT NOT NULL DEFAULT 0,
  value_int              BIGINT NULL,
  value_real             DOUBLE NULL,
  value_text             TEXT NULL,
  updated_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(character_id, script_key, value_index),
  CONSTRAINT character_script_state_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT character_script_state_value_type_ck CHECK(value_type IN ('int','float','string','array_int','array_float','array_string','unknown')),
  CONSTRAINT character_script_state_value_index_ck CHECK(value_index >= 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

-- -----------------------------------------------------------------------------
-- Persistent world state
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS world_entity_state (
  world_entity_state_id  BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  world_instance_id      BINARY(16) NOT NULL,
  entity_key             VARCHAR(191) NOT NULL,
  entity_kind            VARCHAR(32) NOT NULL,
  entity_template_id     BINARY(16) NULL,
  lifecycle_state        VARCHAR(32) NOT NULL DEFAULT 'active',
  pos_x                  DOUBLE NULL,
  pos_y                  DOUBLE NULL,
  pos_z                  DOUBLE NULL,
  rotation_yaw           DOUBLE NULL,
  health_current         INT NULL,
  health_max             INT NULL,
  state_json             JSON NOT NULL DEFAULT (JSON_OBJECT()),
  row_version            BIGINT NOT NULL DEFAULT 0,
  updated_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  UNIQUE KEY world_entity_state_uk(world_instance_id, entity_key),
  KEY ix_world_entity_state_world_kind_state(world_instance_id, entity_kind, lifecycle_state),
  CONSTRAINT world_entity_state_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT world_entity_state_template_fk FOREIGN KEY(entity_template_id) REFERENCES content_entity_templates(entity_template_id) ON DELETE RESTRICT,
  CONSTRAINT world_entity_state_kind_ck CHECK(entity_kind IN ('npc','creature','item','interactive','trigger','vob','waypoint')),
  CONSTRAINT world_entity_state_lifecycle_ck CHECK(lifecycle_state IN ('active','dead','removed','disabled','consumed','archived')),
  CONSTRAINT world_entity_state_health_ck CHECK((health_current IS NULL OR health_current >= 0) AND (health_max IS NULL OR health_max >= 0)),
  CONSTRAINT world_entity_state_version_ck CHECK(row_version >= 0),
  CONSTRAINT world_entity_state_json_ck CHECK(JSON_VALID(state_json))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS world_inventory (
  world_instance_id      BINARY(16) NOT NULL,
  owner_entity_key       VARCHAR(191) NOT NULL,
  item_instance_id       BINARY(16) NOT NULL,
  amount                 INT NOT NULL DEFAULT 1,
  source_amount          INT NULL,
  source_iterator_count  INT NULL,
  updated_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(world_instance_id, owner_entity_key, item_instance_id),
  CONSTRAINT world_inventory_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT world_inventory_item_fk FOREIGN KEY(item_instance_id) REFERENCES item_instances(item_instance_id) ON DELETE RESTRICT,
  CONSTRAINT world_inventory_amount_ck CHECK(amount > 0),
  CONSTRAINT world_inventory_source_ck CHECK((source_amount IS NULL OR source_amount >= 0) AND (source_iterator_count IS NULL OR source_iterator_count >= 0))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS world_script_state (
  world_instance_id      BINARY(16) NOT NULL,
  script_key             VARCHAR(191) NOT NULL,
  scope_key              VARCHAR(191) NOT NULL DEFAULT 'world',
  symbol_index           INT NULL,
  value_type             VARCHAR(32) NOT NULL,
  value_index            INT NOT NULL DEFAULT 0,
  value_int              BIGINT NULL,
  value_real             DOUBLE NULL,
  value_text             TEXT NULL,
  updated_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(world_instance_id, scope_key, script_key, value_index),
  CONSTRAINT world_script_state_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT world_script_state_value_type_ck CHECK(value_type IN ('int','float','string','array_int','array_float','array_string','unknown')),
  CONSTRAINT world_script_state_value_index_ck CHECK(value_index >= 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

-- -----------------------------------------------------------------------------
-- Append-only event journal and projection bookkeeping
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS world_event_journal (
  event_id               BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  event_seq              BIGINT NOT NULL AUTO_INCREMENT,
  realm_id               BINARY(16) NOT NULL,
  world_instance_id      BINARY(16) NOT NULL,
  actor_character_id     BINARY(16) NULL,
  event_type             VARCHAR(128) NOT NULL,
  event_class            VARCHAR(32) NOT NULL,
  idempotency_key        VARCHAR(191) NULL,
  causation_event_id     BINARY(16) NULL,
  correlation_id         BINARY(16) NULL,
  entity_key             VARCHAR(191) NULL,
  subject_key            VARCHAR(191) NULL,
  server_tick            BIGINT NOT NULL DEFAULT 0,
  occurred_at            TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  source                 VARCHAR(32) NOT NULL DEFAULT 'server',
  schema_version         INT NOT NULL DEFAULT 1,
  payload                JSON NOT NULL DEFAULT (JSON_OBJECT()),
  UNIQUE KEY world_event_journal_seq_uk(event_seq),
  UNIQUE KEY ux_world_event_journal_idempotency(world_instance_id, idempotency_key),
  UNIQUE KEY world_event_journal_world_seq_uk(world_instance_id, event_seq),
  KEY ix_world_event_journal_world_seq(world_instance_id, event_seq),
  KEY ix_world_event_journal_actor_seq(actor_character_id, event_seq),
  KEY ix_world_event_journal_type_seq(event_type, event_seq),
  KEY ix_world_event_journal_realm_seq(realm_id, event_seq),
  CONSTRAINT world_event_journal_realm_fk FOREIGN KEY(realm_id) REFERENCES realm_realms(realm_id) ON DELETE RESTRICT,
  CONSTRAINT world_event_journal_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE RESTRICT,
  CONSTRAINT world_event_journal_actor_fk FOREIGN KEY(actor_character_id) REFERENCES characters(character_id) ON DELETE SET NULL,
  CONSTRAINT world_event_journal_causation_fk FOREIGN KEY(causation_event_id) REFERENCES world_event_journal(event_id) ON DELETE SET NULL,
  CONSTRAINT world_event_journal_event_class_ck CHECK(event_class IN ('character','inventory','equipment','world_entity','quest','dialog','script','combat','trade','spell','system','diagnostic')),
  CONSTRAINT world_event_journal_source_ck CHECK(source IN ('server','import','runtime_sqlite','admin','test')),
  CONSTRAINT world_event_journal_tick_ck CHECK(server_tick >= 0),
  CONSTRAINT world_event_journal_schema_version_ck CHECK(schema_version > 0),
  CONSTRAINT world_event_journal_payload_json_ck CHECK(JSON_VALID(payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS world_projection_offsets (
  projection_name        VARCHAR(128) NOT NULL,
  world_instance_id      BINARY(16) NOT NULL,
  last_event_seq         BIGINT NOT NULL DEFAULT 0,
  updated_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(projection_name, world_instance_id),
  CONSTRAINT world_projection_offsets_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT world_projection_offsets_event_seq_ck CHECK(last_event_seq >= 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS world_state_snapshots (
  snapshot_id            BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  world_instance_id      BINARY(16) NOT NULL,
  snapshot_kind          VARCHAR(32) NOT NULL DEFAULT 'periodic',
  max_event_seq          BIGINT NOT NULL,
  server_tick            BIGINT NOT NULL,
  payload_uri            TEXT NULL,
  payload_hash           CHAR(64) NOT NULL,
  created_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  KEY ix_world_state_snapshots_world_seq(world_instance_id, max_event_seq),
  CONSTRAINT world_state_snapshots_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT world_state_snapshots_kind_ck CHECK(snapshot_kind IN ('import','periodic','shutdown','admin','test')),
  CONSTRAINT world_state_snapshots_seq_ck CHECK(max_event_seq >= 0),
  CONSTRAINT world_state_snapshots_tick_ck CHECK(server_tick >= 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

-- -----------------------------------------------------------------------------
-- Idempotent event append procedure.
-- -----------------------------------------------------------------------------

DROP PROCEDURE IF EXISTS mmo_append_world_event;
DELIMITER $$
CREATE PROCEDURE mmo_append_world_event(
  IN  p_realm_id            BINARY(16),
  IN  p_world_instance_id   BINARY(16),
  IN  p_actor_character_id  BINARY(16),
  IN  p_event_type          VARCHAR(128),
  IN  p_event_class         VARCHAR(32),
  IN  p_server_tick         BIGINT,
  IN  p_entity_key          VARCHAR(191),
  IN  p_subject_key         VARCHAR(191),
  IN  p_payload             JSON,
  IN  p_idempotency_key     VARCHAR(191),
  IN  p_source              VARCHAR(32),
  IN  p_causation_event_id  BINARY(16),
  IN  p_correlation_id      BINARY(16),
  OUT p_event_id            BINARY(16)
)
BEGIN
  DECLARE v_existing_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_type VARCHAR(128) DEFAULT NULL;
  DECLARE v_existing_class VARCHAR(32) DEFAULT NULL;
  DECLARE v_new_event_id BINARY(16);
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_existing_id = NULL;

  SET v_new_event_id = UUID_TO_BIN(UUID(), 1);

  IF p_idempotency_key IS NOT NULL THEN
    SELECT event_id, event_type, event_class
      INTO v_existing_id, v_existing_type, v_existing_class
      FROM world_event_journal
     WHERE world_instance_id = p_world_instance_id
       AND idempotency_key = p_idempotency_key
     LIMIT 1;

    IF v_existing_id IS NOT NULL THEN
      IF v_existing_type <> p_event_type OR v_existing_class <> p_event_class THEN
        SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT = 'idempotency key reused with different event type/class';
      END IF;
      SET p_event_id = v_existing_id;
    ELSE
      INSERT INTO world_event_journal(
        event_id, realm_id, world_instance_id, actor_character_id, event_type,
        event_class, idempotency_key, causation_event_id, correlation_id,
        entity_key, subject_key, server_tick, source, payload
      ) VALUES (
        v_new_event_id, p_realm_id, p_world_instance_id, p_actor_character_id, p_event_type,
        p_event_class, p_idempotency_key, p_causation_event_id, p_correlation_id,
        p_entity_key, p_subject_key, COALESCE(p_server_tick, 0), COALESCE(p_source, 'server'), COALESCE(p_payload, JSON_OBJECT())
      );
      SET p_event_id = v_new_event_id;
    END IF;
  ELSE
    INSERT INTO world_event_journal(
      event_id, realm_id, world_instance_id, actor_character_id, event_type,
      event_class, causation_event_id, correlation_id, entity_key, subject_key,
      server_tick, source, payload
    ) VALUES (
      v_new_event_id, p_realm_id, p_world_instance_id, p_actor_character_id, p_event_type,
      p_event_class, p_causation_event_id, p_correlation_id, p_entity_key, p_subject_key,
      COALESCE(p_server_tick, 0), COALESCE(p_source, 'server'), COALESCE(p_payload, JSON_OBJECT())
    );
    SET p_event_id = v_new_event_id;
  END IF;
END$$
DELIMITER ;

-- -----------------------------------------------------------------------------
-- Read models / admin views. Views are not write targets.
-- -----------------------------------------------------------------------------

CREATE OR REPLACE VIEW v_character_sheet AS
SELECT
  BIN_TO_UUID(c.character_id, 1) AS character_id,
  c.character_key,
  c.character_name,
  c.lifecycle_state,
  r.realm_key,
  w.world_instance_key,
  p.pos_x,
  p.pos_y,
  p.pos_z,
  p.rotation_yaw,
  s.level,
  s.experience,
  s.experience_next,
  s.learning_points,
  s.health_current,
  s.health_max,
  s.mana_current,
  s.mana_max,
  s.strength,
  s.dexterity,
  c.last_login_at,
  c.last_logout_at,
  c.updated_at
FROM characters c
JOIN realm_realms r ON r.realm_id = c.realm_id
LEFT JOIN realm_world_instances w ON w.world_instance_id = c.current_world_instance_id
LEFT JOIN character_positions p ON p.character_id = c.character_id
LEFT JOIN character_stats s ON s.character_id = c.character_id;

CREATE OR REPLACE VIEW v_character_inventory AS
SELECT
  BIN_TO_UUID(c.character_id, 1) AS character_id,
  c.character_name,
  BIN_TO_UUID(ii.item_instance_id, 1) AS item_instance_id,
  ii.item_instance_key,
  it.item_template_key,
  it.display_name AS item_display_name,
  it.classification,
  it.stack_policy,
  ci.bag_index,
  ci.amount,
  ii.quantity,
  ii.lifecycle_state,
  ci.updated_at
FROM character_inventory ci
JOIN characters c ON c.character_id = ci.character_id
JOIN item_instances ii ON ii.item_instance_id = ci.item_instance_id
JOIN content_item_templates it ON it.item_template_id = ii.item_template_id;

CREATE OR REPLACE VIEW v_world_event_counts AS
SELECT
  BIN_TO_UUID(world_instance_id, 1) AS world_instance_id,
  event_class,
  event_type,
  COUNT(*) AS event_count,
  MAX(event_seq) AS max_event_seq,
  MAX(occurred_at) AS last_event_at
FROM world_event_journal
GROUP BY world_instance_id, event_class, event_type;

CREATE OR REPLACE VIEW v_world_dead_entities AS
SELECT
  BIN_TO_UUID(wes.world_instance_id, 1) AS world_instance_id,
  wes.entity_key,
  wes.entity_kind,
  wes.health_current,
  wes.health_max,
  wes.updated_at
FROM world_entity_state wes
WHERE wes.lifecycle_state = 'dead';

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES (
  'production/mysql/001_gothic_mmo_production_schema',
  'gothic-mmo-production-db-v1-mysql',
  'Clean MySQL 8.0 contract for account, realm, content, character, inventory, persistent world, append-only event journal, snapshots and read models.'
)
ON DUPLICATE KEY UPDATE
  applied_at = CURRENT_TIMESTAMP(6),
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes);
