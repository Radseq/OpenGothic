-- Gothic MMO production bootstrap/import pipeline v1.
-- Target: PostgreSQL 15+.
-- Depends on: production/001_gothic_mmo_production_schema.sql.
-- Purpose: auditable one-way bootstrap from runtime SQLite/server-shaped staging into
-- the clean PostgreSQL production contract. This migration does not add runtime_* tables.

BEGIN;

-- Keep import metadata in production because imports are operational events, not debug rows.
-- The imported gameplay state still lands only in production-owned tables.
CREATE TABLE IF NOT EXISTS mmo_import_runs (
  import_run_id          uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  source_system          text NOT NULL,
  source_path            text NOT NULL DEFAULT '',
  source_fingerprint     text NOT NULL,
  source_schema_name     text NOT NULL DEFAULT '',
  source_schema_version  integer,
  import_mode            text NOT NULL DEFAULT 'bootstrap',
  game_code              text NOT NULL,
  content_revision_id    uuid REFERENCES content_revisions(content_revision_id) ON DELETE SET NULL,
  realm_id               uuid REFERENCES realm_realms(realm_id) ON DELETE SET NULL,
  status                 text NOT NULL DEFAULT 'started',
  counters               jsonb NOT NULL DEFAULT '{}'::jsonb,
  diagnostics            jsonb NOT NULL DEFAULT '{}'::jsonb,
  started_at             timestamptz NOT NULL DEFAULT now(),
  finished_at            timestamptz,
  CONSTRAINT mmo_import_runs_source_system_ck CHECK(source_system IN ('runtime_sqlite','world_dump','server_staging','manual','test')),
  CONSTRAINT mmo_import_runs_mode_ck CHECK(import_mode IN ('bootstrap','refresh','validation','dry_run')),
  CONSTRAINT mmo_import_runs_status_ck CHECK(status IN ('started','finished','failed','superseded')),
  CONSTRAINT mmo_import_runs_finished_ck CHECK((status IN ('finished','failed','superseded') AND finished_at IS NOT NULL) OR (status='started' AND finished_at IS NULL)),
  CONSTRAINT mmo_import_runs_game_code_ck CHECK(game_code IN ('g1','g2','g2notr'))
);

CREATE INDEX IF NOT EXISTS ix_mmo_import_runs_source
  ON mmo_import_runs(source_system, source_fingerprint, started_at DESC);

CREATE INDEX IF NOT EXISTS ix_mmo_import_runs_content
  ON mmo_import_runs(content_revision_id, status, started_at DESC)
  WHERE content_revision_id IS NOT NULL;

CREATE INDEX IF NOT EXISTS ix_mmo_import_runs_realm
  ON mmo_import_runs(realm_id, status, started_at DESC)
  WHERE realm_id IS NOT NULL;

CREATE TABLE IF NOT EXISTS mmo_import_object_map (
  import_run_id          uuid NOT NULL REFERENCES mmo_import_runs(import_run_id) ON DELETE CASCADE,
  source_table           text NOT NULL,
  source_key             text NOT NULL,
  target_table           text NOT NULL,
  target_pk              text NOT NULL DEFAULT '',
  target_key             text NOT NULL DEFAULT '',
  raw_hash               text NOT NULL DEFAULT '',
  created_at             timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY(import_run_id, source_table, source_key, target_table),
  CONSTRAINT mmo_import_object_map_source_table_ck CHECK(source_table <> ''),
  CONSTRAINT mmo_import_object_map_source_key_ck CHECK(source_key <> ''),
  CONSTRAINT mmo_import_object_map_target_table_ck CHECK(target_table <> '')
);

CREATE INDEX IF NOT EXISTS ix_mmo_import_object_map_target
  ON mmo_import_object_map(target_table, target_key);

CREATE TABLE IF NOT EXISTS mmo_import_validation_results (
  import_run_id          uuid NOT NULL REFERENCES mmo_import_runs(import_run_id) ON DELETE CASCADE,
  check_key              text NOT NULL,
  severity               text NOT NULL DEFAULT 'error',
  passed                 boolean NOT NULL,
  expected_value         text NOT NULL DEFAULT '',
  actual_value           text NOT NULL DEFAULT '',
  details                jsonb NOT NULL DEFAULT '{}'::jsonb,
  created_at             timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY(import_run_id, check_key),
  CONSTRAINT mmo_import_validation_results_severity_ck CHECK(severity IN ('info','warning','error','fatal'))
);

CREATE OR REPLACE FUNCTION mmo_mark_import_finished(
  p_import_run_id uuid,
  p_status text,
  p_counters jsonb DEFAULT '{}'::jsonb,
  p_diagnostics jsonb DEFAULT '{}'::jsonb
)
RETURNS void
LANGUAGE plpgsql
AS $$
BEGIN
  IF p_status NOT IN ('finished','failed','superseded') THEN
    RAISE EXCEPTION 'invalid import terminal status: %', p_status;
  END IF;

  UPDATE mmo_import_runs
     SET status = p_status,
         counters = COALESCE(p_counters, '{}'::jsonb),
         diagnostics = COALESCE(p_diagnostics, '{}'::jsonb),
         finished_at = now()
   WHERE import_run_id = p_import_run_id;

  IF NOT FOUND THEN
    RAISE EXCEPTION 'import run not found: %', p_import_run_id;
  END IF;
END;
$$;

CREATE OR REPLACE VIEW v_mmo_import_runs AS
SELECT
  ir.import_run_id,
  ir.source_system,
  ir.source_path,
  ir.source_fingerprint,
  ir.source_schema_name,
  ir.source_schema_version,
  ir.import_mode,
  ir.game_code,
  gt.game_code AS target_game_code,
  cr.content_revision_key,
  rr.realm_key,
  ir.status,
  ir.counters,
  ir.started_at,
  ir.finished_at,
  CASE
    WHEN ir.finished_at IS NULL THEN NULL
    ELSE EXTRACT(EPOCH FROM (ir.finished_at - ir.started_at))::bigint
  END AS duration_seconds
FROM mmo_import_runs ir
LEFT JOIN content_revisions cr ON cr.content_revision_id = ir.content_revision_id
LEFT JOIN content_game_targets gt ON gt.game_target_id = cr.game_target_id
LEFT JOIN realm_realms rr ON rr.realm_id = ir.realm_id;

CREATE OR REPLACE VIEW v_mmo_import_validation_errors AS
SELECT
  r.import_run_id,
  r.check_key,
  r.severity,
  r.expected_value,
  r.actual_value,
  r.details,
  r.created_at
FROM mmo_import_validation_results r
WHERE r.passed = false
  AND r.severity IN ('error','fatal');

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES (
  'production/002_bootstrap_import_pipeline',
  'gothic-mmo-production-bootstrap-import-v1',
  'Auditable PostgreSQL import metadata, object source maps, validation results, and import completion helper for SQLite/bootstrap migrations.'
)
ON CONFLICT(migration_key) DO UPDATE SET
  applied_at = now(),
  schema_contract = EXCLUDED.schema_contract,
  notes = EXCLUDED.notes;

COMMIT;
