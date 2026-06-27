-- Gothic MMO production database contract v1.
-- Target: PostgreSQL 15+.
-- This is the clean server-authoritative schema, not the runtime SQLite bridge.
-- Run on a fresh production database for the MMO server. Keep legacy SQLite/.sav as import, bootstrap and parity-validation paths.

BEGIN;

CREATE EXTENSION IF NOT EXISTS pgcrypto;
CREATE EXTENSION IF NOT EXISTS citext;

CREATE TABLE IF NOT EXISTS mmo_schema_versions (
  migration_key      text PRIMARY KEY,
  applied_at         timestamptz NOT NULL DEFAULT now(),
  schema_contract    text NOT NULL,
  notes              text NOT NULL DEFAULT ''
);

CREATE OR REPLACE FUNCTION mmo_touch_updated_at()
RETURNS trigger
LANGUAGE plpgsql
AS $$
BEGIN
  NEW.updated_at = now();
  RETURN NEW;
END;
$$;

-- -----------------------------------------------------------------------------
-- Account / entitlement ownership
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS account_accounts (
  account_id         uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  account_name       citext NOT NULL,
  email              citext,
  auth_provider      text NOT NULL DEFAULT 'local',
  external_subject   text,
  password_hash      text,
  status             text NOT NULL DEFAULT 'active',
  flags              jsonb NOT NULL DEFAULT '{}'::jsonb,
  created_at         timestamptz NOT NULL DEFAULT now(),
  updated_at         timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT account_accounts_account_name_uk UNIQUE(account_name),
  CONSTRAINT account_accounts_email_uk UNIQUE(email),
  CONSTRAINT account_accounts_provider_subject_uk UNIQUE(auth_provider, external_subject),
  CONSTRAINT account_accounts_status_ck CHECK(status IN ('active','locked','banned','deleted')),
  CONSTRAINT account_accounts_external_subject_ck CHECK((auth_provider='local' AND external_subject IS NULL) OR (auth_provider<>'local' AND external_subject IS NOT NULL))
);

DROP TRIGGER IF EXISTS trg_account_accounts_touch ON account_accounts;
CREATE TRIGGER trg_account_accounts_touch
BEFORE UPDATE ON account_accounts
FOR EACH ROW EXECUTE FUNCTION mmo_touch_updated_at();

CREATE TABLE IF NOT EXISTS account_entitlements (
  entitlement_id     uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  account_id         uuid NOT NULL REFERENCES account_accounts(account_id) ON DELETE CASCADE,
  game_code          text NOT NULL,
  entitlement_key    text NOT NULL,
  source             text NOT NULL DEFAULT 'manual',
  status             text NOT NULL DEFAULT 'active',
  granted_at         timestamptz NOT NULL DEFAULT now(),
  expires_at         timestamptz,
  metadata           jsonb NOT NULL DEFAULT '{}'::jsonb,
  CONSTRAINT account_entitlements_unique_uk UNIQUE(account_id, game_code, entitlement_key),
  CONSTRAINT account_entitlements_status_ck CHECK(status IN ('active','revoked','expired')),
  CONSTRAINT account_entitlements_expiry_ck CHECK(expires_at IS NULL OR expires_at > granted_at)
);

CREATE INDEX IF NOT EXISTS ix_account_entitlements_account_status
  ON account_entitlements(account_id, status);

-- -----------------------------------------------------------------------------
-- Content revisions / immutable templates
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS content_game_targets (
  game_target_id        uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  game_code             text NOT NULL,
  display_name          text NOT NULL,
  engine                text NOT NULL DEFAULT 'opengothic',
  save_format_version   integer,
  created_at            timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT content_game_targets_code_uk UNIQUE(game_code),
  CONSTRAINT content_game_targets_code_ck CHECK(game_code IN ('g1','g2','g2notr'))
);

CREATE TABLE IF NOT EXISTS content_revisions (
  content_revision_id   uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  game_target_id        uuid NOT NULL REFERENCES content_game_targets(game_target_id) ON DELETE RESTRICT,
  content_revision_key  text NOT NULL,
  script_symbols_hash   text NOT NULL,
  worlds_hash           text NOT NULL,
  items_hash            text NOT NULL,
  npcs_hash             text NOT NULL,
  migration_hash        text NOT NULL DEFAULT '',
  source_description    text NOT NULL DEFAULT '',
  is_active             boolean NOT NULL DEFAULT false,
  created_at            timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT content_revisions_key_uk UNIQUE(content_revision_key),
  CONSTRAINT content_revisions_game_key_uk UNIQUE(game_target_id, content_revision_key)
);

CREATE UNIQUE INDEX IF NOT EXISTS ux_content_revisions_one_active_per_target
  ON content_revisions(game_target_id)
  WHERE is_active;

CREATE TABLE IF NOT EXISTS content_world_templates (
  world_template_id         uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  content_revision_id       uuid NOT NULL REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  world_key                 text NOT NULL,
  world_name                text NOT NULL,
  zen_path                  text NOT NULL,
  baseline_hash             text NOT NULL,
  baseline_tick             bigint NOT NULL DEFAULT 0,
  baseline_world_time_ms    bigint NOT NULL DEFAULT 0,
  baseline_payload          jsonb NOT NULL DEFAULT '{}'::jsonb,
  created_at                timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT content_world_templates_uk UNIQUE(content_revision_id, world_key),
  CONSTRAINT content_world_templates_tick_ck CHECK(baseline_tick >= 0),
  CONSTRAINT content_world_templates_time_ck CHECK(baseline_world_time_ms >= 0)
);

CREATE TABLE IF NOT EXISTS content_entity_templates (
  entity_template_id     uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  content_revision_id    uuid NOT NULL REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  entity_kind            text NOT NULL,
  engine_template_key    text NOT NULL,
  symbol_index           integer,
  script_id              integer,
  script_name            text,
  display_name           text,
  visual_key             text,
  raw_payload            jsonb NOT NULL DEFAULT '{}'::jsonb,
  created_at             timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT content_entity_templates_kind_ck CHECK(entity_kind IN ('npc','creature','item','interactive','trigger','vob','waypoint')),
  CONSTRAINT content_entity_templates_uk UNIQUE(content_revision_id, entity_kind, engine_template_key)
);

CREATE INDEX IF NOT EXISTS ix_content_entity_templates_symbol
  ON content_entity_templates(content_revision_id, entity_kind, symbol_index);

CREATE TABLE IF NOT EXISTS content_item_templates (
  item_template_id       uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  content_revision_id    uuid NOT NULL REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  item_template_key      text NOT NULL,
  symbol_index           integer,
  script_name            text,
  display_name           text,
  classification         text NOT NULL DEFAULT 'unknown',
  stack_policy           text NOT NULL DEFAULT 'unknown',
  max_stack              integer,
  value                  integer,
  flags                  jsonb NOT NULL DEFAULT '{}'::jsonb,
  raw_payload            jsonb NOT NULL DEFAULT '{}'::jsonb,
  created_at             timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT content_item_templates_uk UNIQUE(content_revision_id, item_template_key),
  CONSTRAINT content_item_templates_classification_ck CHECK(classification IN ('currency','consumable','weapon','armor','jewelry','rune','scroll','ammo','quest','misc','unknown')),
  CONSTRAINT content_item_templates_stack_policy_ck CHECK(stack_policy IN ('currency','stackable','durable_instance','unique','quest_rule','unknown')),
  CONSTRAINT content_item_templates_max_stack_ck CHECK(max_stack IS NULL OR max_stack > 0)
);

CREATE INDEX IF NOT EXISTS ix_content_item_templates_symbol
  ON content_item_templates(content_revision_id, symbol_index);

-- -----------------------------------------------------------------------------
-- Realm / shard / world instances
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS realm_realms (
  realm_id                    uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  game_target_id              uuid NOT NULL REFERENCES content_game_targets(game_target_id) ON DELETE RESTRICT,
  active_content_revision_id  uuid NOT NULL REFERENCES content_revisions(content_revision_id) ON DELETE RESTRICT,
  realm_key                   text NOT NULL,
  display_name                text NOT NULL,
  status                      text NOT NULL DEFAULT 'offline',
  max_players                 integer NOT NULL DEFAULT 1000,
  created_at                  timestamptz NOT NULL DEFAULT now(),
  updated_at                  timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT realm_realms_key_uk UNIQUE(realm_key),
  CONSTRAINT realm_realms_status_ck CHECK(status IN ('offline','maintenance','online','locked','retired')),
  CONSTRAINT realm_realms_max_players_ck CHECK(max_players > 0)
);

DROP TRIGGER IF EXISTS trg_realm_realms_touch ON realm_realms;
CREATE TRIGGER trg_realm_realms_touch
BEFORE UPDATE ON realm_realms
FOR EACH ROW EXECUTE FUNCTION mmo_touch_updated_at();

CREATE TABLE IF NOT EXISTS realm_world_instances (
  world_instance_id           uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  realm_id                    uuid NOT NULL REFERENCES realm_realms(realm_id) ON DELETE CASCADE,
  world_template_id           uuid NOT NULL REFERENCES content_world_templates(world_template_id) ON DELETE RESTRICT,
  world_instance_key          text NOT NULL,
  lifecycle_state             text NOT NULL DEFAULT 'active',
  generation                  integer NOT NULL DEFAULT 1,
  current_tick                bigint NOT NULL DEFAULT 0,
  current_world_time_ms       bigint NOT NULL DEFAULT 0,
  created_at                  timestamptz NOT NULL DEFAULT now(),
  updated_at                  timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT realm_world_instances_key_uk UNIQUE(world_instance_key),
  CONSTRAINT realm_world_instances_realm_template_generation_uk UNIQUE(realm_id, world_template_id, generation),
  CONSTRAINT realm_world_instances_lifecycle_ck CHECK(lifecycle_state IN ('creating','active','paused','archived','deleted')),
  CONSTRAINT realm_world_instances_generation_ck CHECK(generation > 0),
  CONSTRAINT realm_world_instances_tick_ck CHECK(current_tick >= 0),
  CONSTRAINT realm_world_instances_time_ck CHECK(current_world_time_ms >= 0)
);

CREATE INDEX IF NOT EXISTS ix_realm_world_instances_realm_state
  ON realm_world_instances(realm_id, lifecycle_state);

DROP TRIGGER IF EXISTS trg_realm_world_instances_touch ON realm_world_instances;
CREATE TRIGGER trg_realm_world_instances_touch
BEFORE UPDATE ON realm_world_instances
FOR EACH ROW EXECUTE FUNCTION mmo_touch_updated_at();

-- -----------------------------------------------------------------------------
-- Character state
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS characters (
  character_id             uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  account_id               uuid NOT NULL REFERENCES account_accounts(account_id) ON DELETE RESTRICT,
  realm_id                 uuid NOT NULL REFERENCES realm_realms(realm_id) ON DELETE RESTRICT,
  current_world_instance_id uuid REFERENCES realm_world_instances(world_instance_id) ON DELETE RESTRICT,
  character_key            text NOT NULL,
  character_name           citext NOT NULL,
  lifecycle_state          text NOT NULL DEFAULT 'active',
  created_at               timestamptz NOT NULL DEFAULT now(),
  updated_at               timestamptz NOT NULL DEFAULT now(),
  last_login_at            timestamptz,
  last_logout_at           timestamptz,
  metadata                 jsonb NOT NULL DEFAULT '{}'::jsonb,
  CONSTRAINT characters_key_uk UNIQUE(character_key),
  CONSTRAINT characters_name_per_realm_uk UNIQUE(realm_id, character_name),
  CONSTRAINT characters_lifecycle_ck CHECK(lifecycle_state IN ('creating','active','dead','deleted','migrated'))
);

CREATE INDEX IF NOT EXISTS ix_characters_account_realm
  ON characters(account_id, realm_id, lifecycle_state);

DROP TRIGGER IF EXISTS trg_characters_touch ON characters;
CREATE TRIGGER trg_characters_touch
BEFORE UPDATE ON characters
FOR EACH ROW EXECUTE FUNCTION mmo_touch_updated_at();

CREATE TABLE IF NOT EXISTS character_positions (
  character_id           uuid PRIMARY KEY REFERENCES characters(character_id) ON DELETE CASCADE,
  world_instance_id      uuid NOT NULL REFERENCES realm_world_instances(world_instance_id) ON DELETE RESTRICT,
  pos_x                  double precision NOT NULL,
  pos_y                  double precision NOT NULL,
  pos_z                  double precision NOT NULL,
  rotation_yaw           double precision NOT NULL DEFAULT 0,
  current_waypoint_key   text,
  server_tick            bigint NOT NULL DEFAULT 0,
  row_version            bigint NOT NULL DEFAULT 0,
  updated_at             timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT character_positions_tick_ck CHECK(server_tick >= 0),
  CONSTRAINT character_positions_version_ck CHECK(row_version >= 0)
);

CREATE INDEX IF NOT EXISTS ix_character_positions_world
  ON character_positions(world_instance_id, server_tick DESC);

DROP TRIGGER IF EXISTS trg_character_positions_touch ON character_positions;
CREATE TRIGGER trg_character_positions_touch
BEFORE UPDATE ON character_positions
FOR EACH ROW EXECUTE FUNCTION mmo_touch_updated_at();

CREATE TABLE IF NOT EXISTS character_stats (
  character_id           uuid PRIMARY KEY REFERENCES characters(character_id) ON DELETE CASCADE,
  level                  integer NOT NULL DEFAULT 0,
  experience             bigint NOT NULL DEFAULT 0,
  experience_next        bigint,
  learning_points        integer NOT NULL DEFAULT 0,
  health_current         integer NOT NULL DEFAULT 0,
  health_max             integer NOT NULL DEFAULT 0,
  mana_current           integer NOT NULL DEFAULT 0,
  mana_max               integer NOT NULL DEFAULT 0,
  strength               integer NOT NULL DEFAULT 0,
  dexterity              integer NOT NULL DEFAULT 0,
  guild                  integer,
  true_guild             integer,
  permanent_attitude     integer,
  temporary_attitude     integer,
  raw_stats              jsonb NOT NULL DEFAULT '{}'::jsonb,
  row_version            bigint NOT NULL DEFAULT 0,
  updated_at             timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT character_stats_level_ck CHECK(level >= 0),
  CONSTRAINT character_stats_exp_ck CHECK(experience >= 0 AND (experience_next IS NULL OR experience_next >= 0)),
  CONSTRAINT character_stats_lp_ck CHECK(learning_points >= 0),
  CONSTRAINT character_stats_hp_ck CHECK(health_current >= 0 AND health_max >= 0 AND health_current <= health_max),
  CONSTRAINT character_stats_mana_ck CHECK(mana_current >= 0 AND mana_max >= 0 AND mana_current <= mana_max),
  CONSTRAINT character_stats_attrs_ck CHECK(strength >= 0 AND dexterity >= 0),
  CONSTRAINT character_stats_version_ck CHECK(row_version >= 0)
);

DROP TRIGGER IF EXISTS trg_character_stats_touch ON character_stats;
CREATE TRIGGER trg_character_stats_touch
BEFORE UPDATE ON character_stats
FOR EACH ROW EXECUTE FUNCTION mmo_touch_updated_at();

CREATE TABLE IF NOT EXISTS character_wallets (
  character_id           uuid NOT NULL REFERENCES characters(character_id) ON DELETE CASCADE,
  currency_key           text NOT NULL,
  amount                 numeric(20,0) NOT NULL DEFAULT 0,
  updated_at             timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY(character_id, currency_key),
  CONSTRAINT character_wallets_amount_ck CHECK(amount >= 0)
);

DROP TRIGGER IF EXISTS trg_character_wallets_touch ON character_wallets;
CREATE TRIGGER trg_character_wallets_touch
BEFORE UPDATE ON character_wallets
FOR EACH ROW EXECUTE FUNCTION mmo_touch_updated_at();

-- -----------------------------------------------------------------------------
-- Items, inventory and equipment
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS item_instances (
  item_instance_id       uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  realm_id               uuid NOT NULL REFERENCES realm_realms(realm_id) ON DELETE CASCADE,
  item_template_id       uuid NOT NULL REFERENCES content_item_templates(item_template_id) ON DELETE RESTRICT,
  item_instance_key      text NOT NULL,
  owner_type             text NOT NULL DEFAULT 'none',
  owner_id               uuid,
  quantity               integer NOT NULL DEFAULT 1,
  bind_state             text NOT NULL DEFAULT 'unbound',
  lifecycle_state        text NOT NULL DEFAULT 'active',
  raw_payload            jsonb NOT NULL DEFAULT '{}'::jsonb,
  created_at             timestamptz NOT NULL DEFAULT now(),
  updated_at             timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT item_instances_key_uk UNIQUE(item_instance_key),
  CONSTRAINT item_instances_owner_type_ck CHECK(owner_type IN ('none','character','world_entity','container','system')),
  CONSTRAINT item_instances_quantity_ck CHECK(quantity >= 0),
  CONSTRAINT item_instances_bind_state_ck CHECK(bind_state IN ('unbound','bind_on_pickup','bound_character','bound_account','quest_locked')),
  CONSTRAINT item_instances_lifecycle_ck CHECK(lifecycle_state IN ('active','consumed','destroyed','archived'))
);

CREATE INDEX IF NOT EXISTS ix_item_instances_realm_owner
  ON item_instances(realm_id, owner_type, owner_id);

CREATE INDEX IF NOT EXISTS ix_item_instances_template
  ON item_instances(item_template_id, lifecycle_state);

DROP TRIGGER IF EXISTS trg_item_instances_touch ON item_instances;
CREATE TRIGGER trg_item_instances_touch
BEFORE UPDATE ON item_instances
FOR EACH ROW EXECUTE FUNCTION mmo_touch_updated_at();

CREATE TABLE IF NOT EXISTS character_inventory (
  character_id           uuid NOT NULL REFERENCES characters(character_id) ON DELETE CASCADE,
  item_instance_id       uuid NOT NULL REFERENCES item_instances(item_instance_id) ON DELETE RESTRICT,
  bag_index              integer,
  amount                 integer NOT NULL DEFAULT 1,
  source_amount          integer,
  source_iterator_count  integer,
  updated_at             timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY(character_id, item_instance_id),
  CONSTRAINT character_inventory_bag_uk UNIQUE(character_id, bag_index),
  CONSTRAINT character_inventory_amount_ck CHECK(amount > 0),
  CONSTRAINT character_inventory_source_ck CHECK((source_amount IS NULL OR source_amount >= 0) AND (source_iterator_count IS NULL OR source_iterator_count >= 0))
);

CREATE INDEX IF NOT EXISTS ix_character_inventory_character
  ON character_inventory(character_id);

DROP TRIGGER IF EXISTS trg_character_inventory_touch ON character_inventory;
CREATE TRIGGER trg_character_inventory_touch
BEFORE UPDATE ON character_inventory
FOR EACH ROW EXECUTE FUNCTION mmo_touch_updated_at();

CREATE TABLE IF NOT EXISTS character_equipment (
  character_id           uuid NOT NULL REFERENCES characters(character_id) ON DELETE CASCADE,
  equipment_slot         text NOT NULL,
  item_instance_id       uuid NOT NULL REFERENCES item_instances(item_instance_id) ON DELETE RESTRICT,
  equipped_at            timestamptz NOT NULL DEFAULT now(),
  updated_at             timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY(character_id, equipment_slot),
  CONSTRAINT character_equipment_item_uk UNIQUE(item_instance_id),
  CONSTRAINT character_equipment_slot_ck CHECK(equipment_slot IN ('weapon_melee','weapon_ranged','shield','armor','belt','amulet','ring_left','ring_right','rune','torch','unknown'))
);

DROP TRIGGER IF EXISTS trg_character_equipment_touch ON character_equipment;
CREATE TRIGGER trg_character_equipment_touch
BEFORE UPDATE ON character_equipment
FOR EACH ROW EXECUTE FUNCTION mmo_touch_updated_at();

-- -----------------------------------------------------------------------------
-- Character progress: quests, dialogs, script state
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS character_quests (
  character_id           uuid NOT NULL REFERENCES characters(character_id) ON DELETE CASCADE,
  quest_key              text NOT NULL,
  section                text NOT NULL DEFAULT '',
  status                 text NOT NULL,
  entry_order            integer NOT NULL DEFAULT 0,
  text_entries           jsonb NOT NULL DEFAULT '[]'::jsonb,
  updated_at             timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY(character_id, quest_key),
  CONSTRAINT character_quests_status_ck CHECK(status IN ('running','success','failed','obsolete')),
  CONSTRAINT character_quests_entry_order_ck CHECK(entry_order >= 0)
);

CREATE INDEX IF NOT EXISTS ix_character_quests_character_status
  ON character_quests(character_id, status);

DROP TRIGGER IF EXISTS trg_character_quests_touch ON character_quests;
CREATE TRIGGER trg_character_quests_touch
BEFORE UPDATE ON character_quests
FOR EACH ROW EXECUTE FUNCTION mmo_touch_updated_at();

CREATE TABLE IF NOT EXISTS character_known_dialogs (
  character_id           uuid NOT NULL REFERENCES characters(character_id) ON DELETE CASCADE,
  npc_key                text NOT NULL,
  info_key               text NOT NULL,
  known                  boolean NOT NULL DEFAULT true,
  permanent              boolean NOT NULL DEFAULT false,
  availability_state     text NOT NULL DEFAULT 'unknown',
  updated_at             timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY(character_id, npc_key, info_key),
  CONSTRAINT character_known_dialogs_availability_ck CHECK(availability_state IN ('unknown','visible','hidden','consumed_hidden','repeatable_known'))
);

DROP TRIGGER IF EXISTS trg_character_known_dialogs_touch ON character_known_dialogs;
CREATE TRIGGER trg_character_known_dialogs_touch
BEFORE UPDATE ON character_known_dialogs
FOR EACH ROW EXECUTE FUNCTION mmo_touch_updated_at();

CREATE TABLE IF NOT EXISTS character_script_state (
  character_id           uuid NOT NULL REFERENCES characters(character_id) ON DELETE CASCADE,
  script_key             text NOT NULL,
  symbol_index           integer,
  value_type             text NOT NULL,
  value_index            integer NOT NULL DEFAULT 0,
  value_int              bigint,
  value_real             double precision,
  value_text             text,
  updated_at             timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY(character_id, script_key, value_index),
  CONSTRAINT character_script_state_value_type_ck CHECK(value_type IN ('int','float','string','array_int','array_float','array_string','unknown')),
  CONSTRAINT character_script_state_value_index_ck CHECK(value_index >= 0)
);

DROP TRIGGER IF EXISTS trg_character_script_state_touch ON character_script_state;
CREATE TRIGGER trg_character_script_state_touch
BEFORE UPDATE ON character_script_state
FOR EACH ROW EXECUTE FUNCTION mmo_touch_updated_at();

-- -----------------------------------------------------------------------------
-- Persistent world state
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS world_entity_state (
  world_entity_state_id  uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  world_instance_id      uuid NOT NULL REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  entity_key             text NOT NULL,
  entity_kind            text NOT NULL,
  entity_template_id     uuid REFERENCES content_entity_templates(entity_template_id) ON DELETE RESTRICT,
  lifecycle_state        text NOT NULL DEFAULT 'active',
  pos_x                  double precision,
  pos_y                  double precision,
  pos_z                  double precision,
  rotation_yaw           double precision,
  health_current         integer,
  health_max             integer,
  state_json             jsonb NOT NULL DEFAULT '{}'::jsonb,
  row_version            bigint NOT NULL DEFAULT 0,
  updated_at             timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT world_entity_state_uk UNIQUE(world_instance_id, entity_key),
  CONSTRAINT world_entity_state_kind_ck CHECK(entity_kind IN ('npc','creature','item','interactive','trigger','vob','waypoint')),
  CONSTRAINT world_entity_state_lifecycle_ck CHECK(lifecycle_state IN ('active','dead','removed','disabled','consumed','archived')),
  CONSTRAINT world_entity_state_health_ck CHECK((health_current IS NULL OR health_current >= 0) AND (health_max IS NULL OR health_max >= 0)),
  CONSTRAINT world_entity_state_version_ck CHECK(row_version >= 0)
);

CREATE INDEX IF NOT EXISTS ix_world_entity_state_world_kind_state
  ON world_entity_state(world_instance_id, entity_kind, lifecycle_state);

DROP TRIGGER IF EXISTS trg_world_entity_state_touch ON world_entity_state;
CREATE TRIGGER trg_world_entity_state_touch
BEFORE UPDATE ON world_entity_state
FOR EACH ROW EXECUTE FUNCTION mmo_touch_updated_at();

CREATE TABLE IF NOT EXISTS world_inventory (
  world_instance_id      uuid NOT NULL REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  owner_entity_key       text NOT NULL,
  item_instance_id       uuid NOT NULL REFERENCES item_instances(item_instance_id) ON DELETE RESTRICT,
  amount                 integer NOT NULL DEFAULT 1,
  source_amount          integer,
  source_iterator_count  integer,
  updated_at             timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY(world_instance_id, owner_entity_key, item_instance_id),
  CONSTRAINT world_inventory_amount_ck CHECK(amount > 0),
  CONSTRAINT world_inventory_source_ck CHECK((source_amount IS NULL OR source_amount >= 0) AND (source_iterator_count IS NULL OR source_iterator_count >= 0))
);

DROP TRIGGER IF EXISTS trg_world_inventory_touch ON world_inventory;
CREATE TRIGGER trg_world_inventory_touch
BEFORE UPDATE ON world_inventory
FOR EACH ROW EXECUTE FUNCTION mmo_touch_updated_at();

CREATE TABLE IF NOT EXISTS world_script_state (
  world_instance_id      uuid NOT NULL REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  script_key             text NOT NULL,
  scope_key              text NOT NULL DEFAULT 'world',
  symbol_index           integer,
  value_type             text NOT NULL,
  value_index            integer NOT NULL DEFAULT 0,
  value_int              bigint,
  value_real             double precision,
  value_text             text,
  updated_at             timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY(world_instance_id, scope_key, script_key, value_index),
  CONSTRAINT world_script_state_value_type_ck CHECK(value_type IN ('int','float','string','array_int','array_float','array_string','unknown')),
  CONSTRAINT world_script_state_value_index_ck CHECK(value_index >= 0)
);

DROP TRIGGER IF EXISTS trg_world_script_state_touch ON world_script_state;
CREATE TRIGGER trg_world_script_state_touch
BEFORE UPDATE ON world_script_state
FOR EACH ROW EXECUTE FUNCTION mmo_touch_updated_at();

-- -----------------------------------------------------------------------------
-- Append-only event journal and projection bookkeeping
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS world_event_journal (
  event_id               uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  event_seq              bigint GENERATED ALWAYS AS IDENTITY,
  realm_id               uuid NOT NULL REFERENCES realm_realms(realm_id) ON DELETE RESTRICT,
  world_instance_id      uuid NOT NULL REFERENCES realm_world_instances(world_instance_id) ON DELETE RESTRICT,
  actor_character_id     uuid REFERENCES characters(character_id) ON DELETE SET NULL,
  event_type             text NOT NULL,
  event_class            text NOT NULL,
  idempotency_key        text,
  causation_event_id     uuid REFERENCES world_event_journal(event_id) ON DELETE SET NULL,
  correlation_id         uuid,
  entity_key             text,
  subject_key            text,
  server_tick            bigint NOT NULL DEFAULT 0,
  occurred_at            timestamptz NOT NULL DEFAULT now(),
  source                 text NOT NULL DEFAULT 'server',
  schema_version         integer NOT NULL DEFAULT 1,
  payload                jsonb NOT NULL DEFAULT '{}'::jsonb,
  CONSTRAINT world_event_journal_seq_uk UNIQUE(event_seq),
  CONSTRAINT world_event_journal_world_seq_uk UNIQUE(world_instance_id, event_seq),
  CONSTRAINT world_event_journal_event_class_ck CHECK(event_class IN ('character','inventory','equipment','world_entity','quest','dialog','script','combat','trade','spell','system','diagnostic')),
  CONSTRAINT world_event_journal_source_ck CHECK(source IN ('server','import','runtime_sqlite','admin','test')),
  CONSTRAINT world_event_journal_tick_ck CHECK(server_tick >= 0),
  CONSTRAINT world_event_journal_schema_version_ck CHECK(schema_version > 0)
);

CREATE UNIQUE INDEX IF NOT EXISTS ux_world_event_journal_idempotency
  ON world_event_journal(world_instance_id, idempotency_key)
  WHERE idempotency_key IS NOT NULL;

CREATE INDEX IF NOT EXISTS ix_world_event_journal_world_seq
  ON world_event_journal(world_instance_id, event_seq DESC);

CREATE INDEX IF NOT EXISTS ix_world_event_journal_actor_seq
  ON world_event_journal(actor_character_id, event_seq DESC)
  WHERE actor_character_id IS NOT NULL;

CREATE INDEX IF NOT EXISTS ix_world_event_journal_type_seq
  ON world_event_journal(event_type, event_seq DESC);

CREATE INDEX IF NOT EXISTS ix_world_event_journal_payload_gin
  ON world_event_journal USING gin(payload);

CREATE OR REPLACE FUNCTION mmo_append_world_event(
  p_realm_id            uuid,
  p_world_instance_id   uuid,
  p_actor_character_id  uuid,
  p_event_type          text,
  p_event_class         text,
  p_server_tick         bigint,
  p_entity_key          text DEFAULT NULL,
  p_subject_key         text DEFAULT NULL,
  p_payload             jsonb DEFAULT '{}'::jsonb,
  p_idempotency_key     text DEFAULT NULL,
  p_source              text DEFAULT 'server',
  p_causation_event_id  uuid DEFAULT NULL,
  p_correlation_id      uuid DEFAULT NULL
)
RETURNS uuid
LANGUAGE plpgsql
AS $$
DECLARE
  v_event_id uuid;
  v_existing world_event_journal%ROWTYPE;
BEGIN
  INSERT INTO world_event_journal(
    realm_id,
    world_instance_id,
    actor_character_id,
    event_type,
    event_class,
    idempotency_key,
    causation_event_id,
    correlation_id,
    entity_key,
    subject_key,
    server_tick,
    source,
    payload
  ) VALUES (
    p_realm_id,
    p_world_instance_id,
    p_actor_character_id,
    p_event_type,
    p_event_class,
    p_idempotency_key,
    p_causation_event_id,
    p_correlation_id,
    p_entity_key,
    p_subject_key,
    p_server_tick,
    p_source,
    COALESCE(p_payload, '{}'::jsonb)
  )
  RETURNING event_id INTO v_event_id;

  RETURN v_event_id;
EXCEPTION WHEN unique_violation THEN
  IF p_idempotency_key IS NULL THEN
    RAISE;
  END IF;

  SELECT * INTO v_existing
    FROM world_event_journal
   WHERE world_instance_id = p_world_instance_id
     AND idempotency_key = p_idempotency_key;

  IF NOT FOUND THEN
    RAISE;
  END IF;

  IF v_existing.event_type <> p_event_type
     OR v_existing.event_class <> p_event_class
     OR v_existing.payload <> COALESCE(p_payload, '{}'::jsonb) THEN
    RAISE EXCEPTION 'idempotency key % reused with different event payload/type', p_idempotency_key
      USING ERRCODE = '23505';
  END IF;

  RETURN v_existing.event_id;
END;
$$;

CREATE TABLE IF NOT EXISTS world_projection_offsets (
  projection_name        text NOT NULL,
  world_instance_id      uuid NOT NULL REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  last_event_seq         bigint NOT NULL DEFAULT 0,
  updated_at             timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY(projection_name, world_instance_id),
  CONSTRAINT world_projection_offsets_event_seq_ck CHECK(last_event_seq >= 0)
);

DROP TRIGGER IF EXISTS trg_world_projection_offsets_touch ON world_projection_offsets;
CREATE TRIGGER trg_world_projection_offsets_touch
BEFORE UPDATE ON world_projection_offsets
FOR EACH ROW EXECUTE FUNCTION mmo_touch_updated_at();

CREATE TABLE IF NOT EXISTS world_state_snapshots (
  snapshot_id            uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  world_instance_id      uuid NOT NULL REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  snapshot_kind          text NOT NULL DEFAULT 'periodic',
  max_event_seq          bigint NOT NULL,
  server_tick            bigint NOT NULL,
  payload_uri            text,
  payload_hash           text NOT NULL,
  created_at             timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT world_state_snapshots_kind_ck CHECK(snapshot_kind IN ('import','periodic','shutdown','admin','test')),
  CONSTRAINT world_state_snapshots_seq_ck CHECK(max_event_seq >= 0),
  CONSTRAINT world_state_snapshots_tick_ck CHECK(server_tick >= 0)
);

CREATE INDEX IF NOT EXISTS ix_world_state_snapshots_world_seq
  ON world_state_snapshots(world_instance_id, max_event_seq DESC);

-- -----------------------------------------------------------------------------
-- Read models / admin views. Views are not write targets.
-- -----------------------------------------------------------------------------

CREATE OR REPLACE VIEW v_character_sheet AS
SELECT
  c.character_id,
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
  c.character_id,
  c.character_name,
  ii.item_instance_id,
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
  world_instance_id,
  event_class,
  event_type,
  count(*) AS event_count,
  max(event_seq) AS max_event_seq,
  max(occurred_at) AS last_event_at
FROM world_event_journal
GROUP BY world_instance_id, event_class, event_type;

CREATE OR REPLACE VIEW v_world_dead_entities AS
SELECT
  wes.world_instance_id,
  wes.entity_key,
  wes.entity_kind,
  wes.health_current,
  wes.health_max,
  wes.updated_at
FROM world_entity_state wes
WHERE wes.lifecycle_state = 'dead';

-- -----------------------------------------------------------------------------
-- Contract marker
-- -----------------------------------------------------------------------------

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES (
  'production/001_gothic_mmo_production_schema',
  'gothic-mmo-production-db-v1',
  'Clean PostgreSQL contract for account, realm, content, character, inventory, persistent world, append-only event journal, snapshots and read models.'
)
ON CONFLICT(migration_key) DO UPDATE SET
  applied_at = now(),
  schema_contract = EXCLUDED.schema_contract,
  notes = EXCLUDED.notes;

COMMIT;
