-- Gothic MMO MySQL production migration 023.
-- Final database-completion registry and run/result tables.
-- Requires 001..022 MySQL production migrations.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE IF NOT EXISTS mmo_database_completion_requirements (
  requirement_key   VARCHAR(191) PRIMARY KEY,
  area              VARCHAR(64) NOT NULL,
  requirement_kind  VARCHAR(32) NOT NULL,
  title             VARCHAR(255) NOT NULL,
  required_for_db   BOOLEAN NOT NULL DEFAULT TRUE,
  required_for_mmo  BOOLEAN NOT NULL DEFAULT TRUE,
  sort_order        INT NOT NULL DEFAULT 1000,
  definition        JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at        TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at        TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  KEY ix_mmo_db_completion_req_area(area, sort_order),
  KEY ix_mmo_db_completion_req_required(required_for_db, required_for_mmo, sort_order),
  CONSTRAINT mmo_db_completion_req_kind_ck CHECK(requirement_kind IN ('schema','write_path','replay','parity','ops','cxx','server','network','external')),
  CONSTRAINT mmo_db_completion_req_json_ck CHECK(JSON_VALID(definition))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_database_completion_runs (
  completion_run_id BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  world_instance_id BINARY(16) NOT NULL,
  character_id      BINARY(16) NULL,
  run_key           VARCHAR(191) NOT NULL,
  database_status   VARCHAR(32) NOT NULL DEFAULT 'running',
  mmo_status        VARCHAR(32) NOT NULL DEFAULT 'running',
  db_blocker_count  INT NOT NULL DEFAULT 0,
  external_blocker_count INT NOT NULL DEFAULT 0,
  warning_count     INT NOT NULL DEFAULT 0,
  started_at        TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  finished_at       TIMESTAMP(6) NULL,
  metadata          JSON NOT NULL DEFAULT (JSON_OBJECT()),
  UNIQUE KEY mmo_database_completion_runs_key_uk(world_instance_id, run_key),
  KEY ix_mmo_database_completion_runs_world_started(world_instance_id, started_at),
  CONSTRAINT mmo_database_completion_runs_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT mmo_database_completion_runs_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE SET NULL,
  CONSTRAINT mmo_database_completion_runs_status_ck CHECK(database_status IN ('running','complete','blocked','failed') AND mmo_status IN ('running','green','yellow','red','blocked')),
  CONSTRAINT mmo_database_completion_runs_metadata_json_ck CHECK(JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_database_completion_results (
  completion_result_id BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  completion_run_id    BINARY(16) NOT NULL,
  requirement_key      VARCHAR(191) NOT NULL,
  status               VARCHAR(32) NOT NULL,
  severity             VARCHAR(16) NOT NULL,
  checked_count        BIGINT NOT NULL DEFAULT 0,
  problem_count        BIGINT NOT NULL DEFAULT 0,
  details              JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at           TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY mmo_database_completion_results_uk(completion_run_id, requirement_key),
  KEY ix_mmo_database_completion_results_status(status, severity, created_at),
  CONSTRAINT mmo_database_completion_results_run_fk FOREIGN KEY(completion_run_id) REFERENCES mmo_database_completion_runs(completion_run_id) ON DELETE CASCADE,
  CONSTRAINT mmo_database_completion_results_req_fk FOREIGN KEY(requirement_key) REFERENCES mmo_database_completion_requirements(requirement_key) ON DELETE RESTRICT,
  CONSTRAINT mmo_database_completion_results_status_ck CHECK(status IN ('passed','failed','warning','blocked','not_run')),
  CONSTRAINT mmo_database_completion_results_severity_ck CHECK(severity IN ('ok','warning','db_blocker','external_blocker')),
  CONSTRAINT mmo_database_completion_results_counts_ck CHECK(checked_count >= 0 AND problem_count >= 0),
  CONSTRAINT mmo_database_completion_results_json_ck CHECK(JSON_VALID(details))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

INSERT INTO mmo_database_completion_requirements(requirement_key, area, requirement_kind, title, required_for_db, required_for_mmo, sort_order, definition)
VALUES
  ('mysql_migrations_001_030_present', 'schema', 'schema', 'All MySQL production migrations 001..030 are applied', TRUE, TRUE, 10, JSON_OBJECT('migration_range','001..030')),
  ('bootstrap_import_present', 'schema', 'schema', 'Bootstrap import produced realm/world/account/character state', TRUE, TRUE, 20, JSON_OBJECT('tables',JSON_ARRAY('realm_world_instances','account_accounts','characters'))),
  ('write_paths_registered', 'write_path', 'write_path', 'All current semantic write paths are registered in dispatch contracts', TRUE, TRUE, 30, JSON_OBJECT('table','mmo_server_action_dispatch_contracts')),
  ('event_replay_contracts_complete', 'replay', 'replay', 'Every server/test event type has a replay contract', TRUE, TRUE, 40, JSON_OBJECT('view','v_event_replay_contract_gaps')),
  ('strict_replay_prefight_clean', 'replay', 'replay', 'Strict replay pre-flight has zero errors for current journal', TRUE, TRUE, 50, JSON_OBJECT('procedure','mmo_audit_strict_replay_journal')),
  ('projection_integrity_clean', 'replay', 'replay', 'Extended projection/invariant checks have zero DB errors', TRUE, TRUE, 60, JSON_OBJECT('procedure','mmo_run_final_database_integrity_audit')),
  ('projection_hash_manifest_present', 'replay', 'replay', 'Canonical projection component hashes can be materialized', TRUE, TRUE, 70, JSON_OBJECT('procedure','mmo_materialize_projection_hash_run')),
  ('outbox_dispatch_clean', 'write_path', 'write_path', 'Action outbox has no failed/dead-letter rows', TRUE, TRUE, 80, JSON_OBJECT('table','mmo_server_action_outbox')),
  ('worker_observability_present', 'ops', 'ops', 'Worker telemetry exists for dispatched DB actions', TRUE, TRUE, 90, JSON_OBJECT('tables',JSON_ARRAY('mmo_server_action_worker_runs','mmo_server_action_worker_results'))),
  ('restore_manifest_available', 'parity', 'parity', 'DB restore manifest can be created from current projections', TRUE, TRUE, 100, JSON_OBJECT('procedure','mmo_create_db_restore_manifest')),
  ('backup_manifest_available', 'ops', 'ops', 'Database backup/export manifest can be recorded', TRUE, TRUE, 110, JSON_OBJECT('procedure','mmo_record_database_backup_manifest')),
  ('native_sqlite_mysql_parity_passed', 'parity', 'parity', 'Native .sav, SQLite save-slot and MySQL projection parity passed for required scenarios', FALSE, TRUE, 120, JSON_OBJECT('gate','requires real game scenario runs')),
  ('real_cpp_hooks_inserted', 'cxx', 'cxx', 'Real C++ semantic hooks are inserted at mutation boundaries', FALSE, TRUE, 130, JSON_OBJECT('gate','requires source integration and gameplay compile')),
  ('production_rpc_worker_ready', 'server', 'server', 'Dev mysql-cli worker replaced by production RPC/server worker', FALSE, TRUE, 140, JSON_OBJECT('gate','requires service implementation')),
  ('server_authority_network_ready', 'network', 'network', 'Server authority, replication, reconnect and shard orchestration are implemented', FALSE, TRUE, 150, JSON_OBJECT('gate','requires MMO server layer'))
ON DUPLICATE KEY UPDATE
  area=VALUES(area), requirement_kind=VALUES(requirement_kind), title=VALUES(title), required_for_db=VALUES(required_for_db), required_for_mmo=VALUES(required_for_mmo), sort_order=VALUES(sort_order), definition=VALUES(definition), updated_at=CURRENT_TIMESTAMP(6);

CREATE OR REPLACE VIEW v_mmo_database_completion_requirements AS
SELECT requirement_key, area, requirement_kind, title, required_for_db, required_for_mmo, sort_order, definition, updated_at
FROM mmo_database_completion_requirements
ORDER BY sort_order, requirement_key;

CREATE OR REPLACE VIEW v_mmo_database_completion_latest AS
SELECT BIN_TO_UUID(r.completion_run_id,1) AS completion_run_uuid,
       BIN_TO_UUID(r.world_instance_id,1) AS world_instance_uuid,
       BIN_TO_UUID(r.character_id,1) AS character_uuid,
       r.run_key,
       r.database_status,
       r.mmo_status,
       r.db_blocker_count,
       r.external_blocker_count,
       r.warning_count,
       r.started_at,
       r.finished_at
FROM mmo_database_completion_runs r
WHERE r.started_at = (SELECT MAX(r2.started_at) FROM mmo_database_completion_runs r2 WHERE r2.world_instance_id=r.world_instance_id);

CREATE OR REPLACE VIEW v_mmo_database_completion_blockers AS
SELECT BIN_TO_UUID(run.completion_run_id,1) AS completion_run_uuid,
       run.run_key,
       run.database_status,
       run.mmo_status,
       res.requirement_key,
       req.area,
       req.title,
       res.status,
       res.severity,
       res.checked_count,
       res.problem_count,
       res.details,
       res.created_at
FROM mmo_database_completion_runs run
JOIN mmo_database_completion_results res ON res.completion_run_id=run.completion_run_id
JOIN mmo_database_completion_requirements req ON req.requirement_key=res.requirement_key
WHERE res.severity IN ('db_blocker','external_blocker','warning')
  AND res.problem_count > 0
ORDER BY run.started_at DESC, FIELD(res.severity,'db_blocker','external_blocker','warning'), req.sort_order;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES('production/mysql/023_database_completion_registry', 'gothic-mmo-database-completion-registry-v1-mysql', 'Final database-completion requirement registry plus completion run/result tables. Separates DB-layer completeness from external MMO integration gates.')
ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes), applied_at=CURRENT_TIMESTAMP(6);
