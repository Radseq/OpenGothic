-- Gothic MMO production-shaped schema, v3.
-- Target: PostgreSQL 15+.
-- Source of truth for local prototype: tools/build_mmo_database.py.

BEGIN;

CREATE TABLE IF NOT EXISTS schema_meta (
  key text PRIMARY KEY,
  value text NOT NULL
);

INSERT INTO schema_meta(key, value) VALUES
  ('schema_name', 'gothic_mmo'),
  ('schema_version', '3')
ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value;

CREATE TABLE IF NOT EXISTS import_audits (
  id bigserial PRIMARY KEY,
  created_at timestamptz NOT NULL DEFAULT now(),
  source_db text NOT NULL,
  source_import_run_id bigint NOT NULL,
  source_world_instance_id bigint NOT NULL,
  source_snapshot_tick bigint,
  source_snapshot_hash text,
  notes text
);

CREATE TABLE IF NOT EXISTS account_accounts (
  id bigserial PRIMARY KEY,
  username text NOT NULL UNIQUE,
  display_name text NOT NULL,
  status text NOT NULL DEFAULT 'active',
  created_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS account_entitlements (
  id bigserial PRIMARY KEY,
  account_id bigint NOT NULL REFERENCES account_accounts(id) ON DELETE CASCADE,
  target_code text NOT NULL,
  granted_at timestamptz NOT NULL DEFAULT now(),
  UNIQUE(account_id, target_code)
);

CREATE TABLE IF NOT EXISTS realm_realms (
  id bigserial PRIMARY KEY,
  code text NOT NULL UNIQUE,
  name text NOT NULL,
  region text NOT NULL DEFAULT 'local',
  status text NOT NULL DEFAULT 'development',
  created_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS content_game_targets (
  id bigserial PRIMARY KEY,
  code text NOT NULL UNIQUE,
  game integer,
  patch integer,
  schema_version integer,
  content_hash text,
  raw_manifest_json jsonb NOT NULL
);

CREATE TABLE IF NOT EXISTS content_world_templates (
  id bigserial PRIMARY KEY,
  game_target_id bigint NOT NULL REFERENCES content_game_targets(id) ON DELETE CASCADE,
  world_name text NOT NULL,
  baseline_tick bigint,
  baseline_time_day_millis bigint,
  baseline_hash text,
  raw_manifest_json jsonb NOT NULL,
  UNIQUE(game_target_id, world_name, baseline_hash)
);

CREATE TABLE IF NOT EXISTS realm_world_instances (
  id bigserial PRIMARY KEY,
  realm_id bigint NOT NULL REFERENCES realm_realms(id) ON DELETE CASCADE,
  world_template_id bigint NOT NULL REFERENCES content_world_templates(id) ON DELETE CASCADE,
  shard_key text NOT NULL UNIQUE,
  state_source text NOT NULL,
  snapshot_tick bigint,
  snapshot_time_day_millis bigint,
  snapshot_hash text,
  created_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS content_entity_templates (
  id bigserial PRIMARY KEY,
  world_template_id bigint NOT NULL REFERENCES content_world_templates(id) ON DELETE CASCADE,
  stable_key text NOT NULL,
  entity_type text NOT NULL,
  display_name text,
  symbol_index integer,
  persistent_id bigint,
  source_file text NOT NULL,
  raw_json jsonb NOT NULL,
  UNIQUE(world_template_id, source_file, stable_key)
);

CREATE TABLE IF NOT EXISTS content_item_templates (
  id bigserial PRIMARY KEY,
  game_target_id bigint NOT NULL REFERENCES content_game_targets(id) ON DELETE CASCADE,
  symbol_index integer,
  name text,
  display_name text,
  visual text,
  value integer,
  flags bigint,
  material integer,
  sample_raw_json jsonb NOT NULL,
  UNIQUE(game_target_id, symbol_index, name, visual)
);

CREATE TABLE IF NOT EXISTS content_item_classification (
  item_template_id bigint PRIMARY KEY REFERENCES content_item_templates(id) ON DELETE CASCADE,
  item_class text NOT NULL,
  stack_policy text NOT NULL,
  max_stack integer,
  equipment_slot_group text,
  confidence text NOT NULL,
  rule_version integer NOT NULL DEFAULT 1,
  CHECK (stack_policy IN ('stack', 'instance', 'unique'))
);

CREATE TABLE IF NOT EXISTS characters (
  id bigserial PRIMARY KEY,
  account_id bigint NOT NULL REFERENCES account_accounts(id) ON DELETE CASCADE,
  realm_id bigint NOT NULL REFERENCES realm_realms(id) ON DELETE CASCADE,
  current_world_instance_id bigint REFERENCES realm_world_instances(id) ON DELETE SET NULL,
  source_stable_key text NOT NULL,
  persistent_id bigint,
  name text NOT NULL,
  character_kind text NOT NULL DEFAULT 'player',
  created_at timestamptz NOT NULL DEFAULT now(),
  updated_at timestamptz NOT NULL DEFAULT now(),
  UNIQUE(realm_id, name)
);

CREATE TABLE IF NOT EXISTS character_stats (
  character_id bigint PRIMARY KEY REFERENCES characters(id) ON DELETE CASCADE,
  hp integer,
  hp_max integer,
  mana integer,
  mana_max integer,
  level integer,
  experience integer,
  learning_points integer,
  attributes_json jsonb,
  talents_json jsonb,
  position_json jsonb,
  raw_json jsonb NOT NULL
);

CREATE TABLE IF NOT EXISTS character_inventory (
  id bigserial PRIMARY KEY,
  character_id bigint NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
  item_stable_key text NOT NULL,
  item_symbol_index integer,
  item_name text,
  item_display_name text,
  amount integer,
  iterator_count integer,
  equipped boolean NOT NULL DEFAULT false,
  slot integer,
  source_file text NOT NULL,
  raw_json jsonb NOT NULL,
  UNIQUE(character_id, item_stable_key)
);

CREATE TABLE IF NOT EXISTS item_instances (
  id bigserial PRIMARY KEY,
  item_template_id bigint REFERENCES content_item_templates(id) ON DELETE SET NULL,
  source_table text NOT NULL,
  source_row_id bigint NOT NULL,
  source_stable_key text NOT NULL,
  item_symbol_index integer,
  item_name text,
  item_display_name text,
  owner_type text NOT NULL,
  character_id bigint REFERENCES characters(id) ON DELETE CASCADE,
  world_instance_id bigint REFERENCES realm_world_instances(id) ON DELETE CASCADE,
  container_scope text,
  container_stable_key text,
  container_display_name text,
  quantity integer NOT NULL DEFAULT 1,
  iterator_count integer,
  equipped boolean NOT NULL DEFAULT false,
  equipment_slot integer,
  source_file text NOT NULL,
  raw_json jsonb NOT NULL,
  UNIQUE(source_table, source_row_id),
  CHECK (owner_type IN ('character', 'world')),
  CHECK (
    (owner_type = 'character' AND character_id IS NOT NULL)
    OR (owner_type = 'world' AND world_instance_id IS NOT NULL)
  )
);

CREATE TABLE IF NOT EXISTS character_equipment (
  id bigserial PRIMARY KEY,
  character_id bigint NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
  slot integer NOT NULL,
  item_instance_id bigint NOT NULL REFERENCES item_instances(id) ON DELETE CASCADE,
  item_template_id bigint REFERENCES content_item_templates(id) ON DELETE SET NULL,
  source_stable_key text NOT NULL,
  item_display_name text,
  raw_json jsonb NOT NULL,
  UNIQUE(character_id, slot, item_instance_id)
);

CREATE TABLE IF NOT EXISTS character_quests (
  id bigserial PRIMARY KEY,
  character_id bigint NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
  stable_key text NOT NULL,
  name text,
  section integer,
  status integer,
  entry_count integer,
  entries_json jsonb,
  raw_json jsonb NOT NULL,
  UNIQUE(character_id, stable_key)
);

CREATE TABLE IF NOT EXISTS character_known_dialogs (
  id bigserial PRIMARY KEY,
  character_id bigint NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
  stable_key text NOT NULL,
  npc_symbol_name text,
  info_symbol_name text,
  raw_json jsonb NOT NULL,
  UNIQUE(character_id, stable_key)
);

CREATE TABLE IF NOT EXISTS character_script_state (
  id bigserial PRIMARY KEY,
  character_id bigint NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
  stable_key text NOT NULL,
  symbol_index integer,
  symbol_name text,
  value_type text,
  category text,
  values_json jsonb,
  raw_json jsonb NOT NULL,
  UNIQUE(character_id, stable_key)
);

CREATE TABLE IF NOT EXISTS world_entity_state (
  id bigserial PRIMARY KEY,
  world_instance_id bigint NOT NULL REFERENCES realm_world_instances(id) ON DELETE CASCADE,
  stable_key text NOT NULL,
  entity_type text NOT NULL,
  display_name text,
  symbol_index integer,
  persistent_id bigint,
  hp integer,
  mana integer,
  dead boolean,
  mob_state integer,
  locked boolean,
  amount integer,
  position_json jsonb,
  stats_json jsonb,
  raw_json jsonb NOT NULL,
  UNIQUE(world_instance_id, entity_type, stable_key)
);

CREATE TABLE IF NOT EXISTS world_inventory (
  id bigserial PRIMARY KEY,
  world_instance_id bigint NOT NULL REFERENCES realm_world_instances(id) ON DELETE CASCADE,
  owner_scope text NOT NULL,
  owner_stable_key text,
  owner_persistent_id bigint,
  owner_display_name text,
  item_stable_key text NOT NULL,
  item_symbol_index integer,
  item_name text,
  item_display_name text,
  amount integer,
  iterator_count integer,
  equipped boolean NOT NULL DEFAULT false,
  slot integer,
  source_file text NOT NULL,
  raw_json jsonb NOT NULL,
  UNIQUE(world_instance_id, owner_scope, item_stable_key)
);

CREATE TABLE IF NOT EXISTS world_script_state (
  id bigserial PRIMARY KEY,
  world_instance_id bigint NOT NULL REFERENCES realm_world_instances(id) ON DELETE CASCADE,
  stable_key text NOT NULL,
  symbol_index integer,
  symbol_name text,
  value_type text,
  category text,
  values_json jsonb,
  raw_json jsonb NOT NULL,
  UNIQUE(world_instance_id, stable_key)
);

CREATE TABLE IF NOT EXISTS world_event_journal (
  id bigserial PRIMARY KEY,
  world_instance_id bigint NOT NULL REFERENCES realm_world_instances(id) ON DELETE CASCADE,
  source_event_index bigint NOT NULL,
  event_type text NOT NULL,
  event_class text NOT NULL,
  entity_type text,
  stable_key text,
  actor_character_id bigint REFERENCES characters(id) ON DELETE SET NULL,
  name text,
  payload_json jsonb NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  UNIQUE(world_instance_id, source_event_index)
);

CREATE TABLE IF NOT EXISTS world_replay_validation (
  world_instance_id bigint NOT NULL REFERENCES realm_world_instances(id) ON DELETE CASCADE,
  metric text NOT NULL,
  snapshot_count bigint,
  replay_count bigint,
  status text NOT NULL,
  PRIMARY KEY(world_instance_id, metric)
);

CREATE TABLE IF NOT EXISTS world_runtime_noise_candidates (
  id bigserial PRIMARY KEY,
  world_instance_id bigint NOT NULL REFERENCES realm_world_instances(id) ON DELETE CASCADE,
  reason text NOT NULL,
  owner_scope text,
  owner_display_name text,
  item_display_name text,
  item_stable_key text,
  raw_json jsonb NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_character_inventory_character
  ON character_inventory(character_id);
CREATE INDEX IF NOT EXISTS idx_item_instances_owner
  ON item_instances(owner_type, character_id, world_instance_id);
CREATE INDEX IF NOT EXISTS idx_item_instances_template
  ON item_instances(item_template_id, item_symbol_index, item_name);
CREATE INDEX IF NOT EXISTS idx_character_equipment_character
  ON character_equipment(character_id, slot);
CREATE INDEX IF NOT EXISTS idx_item_classification_class
  ON content_item_classification(item_class, stack_policy);
CREATE INDEX IF NOT EXISTS idx_world_entity_state_type
  ON world_entity_state(world_instance_id, entity_type);
CREATE INDEX IF NOT EXISTS idx_world_inventory_owner
  ON world_inventory(world_instance_id, owner_scope, owner_stable_key);
CREATE INDEX IF NOT EXISTS idx_world_event_journal_type
  ON world_event_journal(world_instance_id, event_class, event_type);

COMMIT;
