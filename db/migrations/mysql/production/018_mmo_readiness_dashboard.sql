-- Gothic MMO MySQL production migration 018.
-- Production readiness dashboard across schema, projection, replay, parity and action outbox.
-- Requires 001..017 MySQL production migrations and a bootstrap import.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE IF NOT EXISTS mmo_readiness_runs (
  readiness_run_id  BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  world_instance_id BINARY(16) NOT NULL,
  run_key           VARCHAR(191) NOT NULL,
  status            VARCHAR(32) NOT NULL DEFAULT 'running',
  started_at        TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  finished_at       TIMESTAMP(6) NULL,
  blocker_count     INT NOT NULL DEFAULT 0,
  warning_count     INT NOT NULL DEFAULT 0,
  metadata          JSON NOT NULL DEFAULT (JSON_OBJECT()),
  UNIQUE KEY mmo_readiness_runs_key_uk(world_instance_id, run_key),
  KEY ix_mmo_readiness_runs_world_started(world_instance_id, started_at),
  CONSTRAINT mmo_readiness_runs_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT mmo_readiness_runs_status_ck CHECK(status IN ('running','green','yellow','red')),
  CONSTRAINT mmo_readiness_runs_metadata_json_ck CHECK(JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_readiness_results (
  readiness_result_id BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  readiness_run_id    BINARY(16) NOT NULL,
  check_name          VARCHAR(128) NOT NULL,
  severity            VARCHAR(16) NOT NULL,
  checked_count       BIGINT NOT NULL DEFAULT 0,
  problem_count       BIGINT NOT NULL DEFAULT 0,
  details             JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at          TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY mmo_readiness_results_check_uk(readiness_run_id, check_name),
  KEY ix_mmo_readiness_results_severity(severity, created_at),
  CONSTRAINT mmo_readiness_results_run_fk FOREIGN KEY(readiness_run_id) REFERENCES mmo_readiness_runs(readiness_run_id) ON DELETE CASCADE,
  CONSTRAINT mmo_readiness_results_severity_ck CHECK(severity IN ('ok','warning','blocker')),
  CONSTRAINT mmo_readiness_results_counts_ck CHECK(checked_count >= 0 AND problem_count >= 0),
  CONSTRAINT mmo_readiness_results_details_json_ck CHECK(JSON_VALID(details))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_evaluate_mmo_readiness;
DELIMITER $$
CREATE PROCEDURE mmo_evaluate_mmo_readiness(
  IN  p_world_instance_id  BINARY(16),
  IN  p_run_key            VARCHAR(191),
  IN  p_metadata           JSON,
  OUT p_readiness_run_id   BINARY(16),
  OUT p_status             VARCHAR(32),
  OUT p_blocker_count      INT,
  OUT p_warning_count      INT
)
BEGIN
  DECLARE v_run_id BINARY(16) DEFAULT NULL;
  DECLARE v_total BIGINT DEFAULT 0;
  DECLARE v_bad BIGINT DEFAULT 0;
  DECLARE v_blockers INT DEFAULT 0;
  DECLARE v_warnings INT DEFAULT 0;

  IF p_world_instance_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='world_instance_id is required'; END IF;
  IF p_run_key IS NULL OR TRIM(p_run_key)='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='run_key is required'; END IF;

  INSERT INTO mmo_readiness_runs(world_instance_id, run_key, status, metadata)
  VALUES(p_world_instance_id, p_run_key, 'running', COALESCE(p_metadata, JSON_OBJECT()))
  ON DUPLICATE KEY UPDATE status='running', started_at=CURRENT_TIMESTAMP(6), finished_at=NULL, blocker_count=0, warning_count=0, metadata=VALUES(metadata);

  SELECT readiness_run_id INTO v_run_id FROM mmo_readiness_runs WHERE world_instance_id=p_world_instance_id AND run_key=p_run_key LIMIT 1;
  DELETE FROM mmo_readiness_results WHERE readiness_run_id=v_run_id;

  SELECT COUNT(*) INTO v_total FROM mmo_schema_versions WHERE migration_key LIKE 'production/mysql/%';
  SELECT COUNT(*) INTO v_bad FROM (
    SELECT 'production/mysql/001_gothic_mmo_production_schema' AS k UNION ALL
    SELECT 'production/mysql/002_bootstrap_import_pipeline' UNION ALL
    SELECT 'production/mysql/003_server_write_path' UNION ALL
    SELECT 'production/mysql/004_wallet_write_path' UNION ALL
    SELECT 'production/mysql/005_world_item_write_path' UNION ALL
    SELECT 'production/mysql/006_character_inventory_equipment_write_path' UNION ALL
    SELECT 'production/mysql/007_container_interactive_write_path' UNION ALL
    SELECT 'production/mysql/008_character_progress_write_path' UNION ALL
    SELECT 'production/mysql/009_npc_lifecycle_write_path' UNION ALL
    SELECT 'production/mysql/010_projection_validation' UNION ALL
    SELECT 'production/mysql/011_trade_economy_write_path' UNION ALL
    SELECT 'production/mysql/012_combat_resource_write_path' UNION ALL
    SELECT 'production/mysql/013_item_stack_write_path' UNION ALL
    SELECT 'production/mysql/014_projection_diagnostics' UNION ALL
    SELECT 'production/mysql/015_server_action_outbox' UNION ALL
    SELECT 'production/mysql/016_restore_parity_gate' UNION ALL
    SELECT 'production/mysql/017_event_replay_contract' UNION ALL
    SELECT 'production/mysql/018_mmo_readiness_dashboard'
  ) req LEFT JOIN mmo_schema_versions v ON v.migration_key=req.k WHERE v.migration_key IS NULL;
  INSERT INTO mmo_readiness_results(readiness_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(v_run_id, 'mysql_migrations_001_018_present', IF(v_bad=0,'ok','blocker'), 18, v_bad, JSON_OBJECT('rule','all MySQL production migrations 001..018 must be applied'));
  SET v_blockers = v_blockers + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_total FROM server_sessions WHERE world_instance_id=p_world_instance_id;
  SELECT COUNT(*) INTO v_bad FROM server_sessions WHERE world_instance_id=p_world_instance_id AND lifecycle_state='active';
  INSERT INTO mmo_readiness_results(readiness_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(v_run_id, 'no_orphan_active_sessions_for_gate', IF(v_bad=0,'ok','warning'), v_total, v_bad, JSON_OBJECT('rule','readiness gate should run without leftover active smoke/game sessions'));
  SET v_warnings = v_warnings + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_total FROM mmo_server_action_outbox WHERE world_instance_id=p_world_instance_id;
  SELECT COUNT(*) INTO v_bad FROM mmo_server_action_outbox WHERE world_instance_id=p_world_instance_id AND status IN ('failed','dead_letter');
  INSERT INTO mmo_readiness_results(readiness_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(v_run_id, 'server_action_outbox_no_dead_letters', IF(v_bad=0,'ok','blocker'), v_total, v_bad, JSON_OBJECT('rule','server action outbox cannot contain failed/dead_letter actions for production gate'));
  SET v_blockers = v_blockers + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_total FROM v_event_replay_contract_coverage;
  SELECT COUNT(*) INTO v_bad FROM v_event_replay_contract_gaps;
  INSERT INTO mmo_readiness_results(readiness_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(v_run_id, 'event_replay_contract_has_no_gaps', IF(v_bad=0,'ok','blocker'), v_total, v_bad, JSON_OBJECT('rule','server/test event types must be covered by replay contracts'));
  SET v_blockers = v_blockers + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_total FROM mmo_restore_parity_scenarios WHERE active=TRUE AND required=TRUE;
  SELECT COUNT(*) INTO v_bad FROM mmo_restore_parity_scenarios s
   WHERE s.active=TRUE AND s.required=TRUE
     AND NOT EXISTS (
       SELECT 1 FROM mmo_restore_parity_results r
       JOIN mmo_restore_parity_runs run ON run.parity_run_id=r.parity_run_id
       WHERE r.scenario_key=s.scenario_key AND r.status='passed' AND run.status='passed'
     );
  INSERT INTO mmo_readiness_results(readiness_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(v_run_id, 'restore_parity_required_scenarios_green', IF(v_bad=0,'ok','blocker'), v_total, v_bad, JSON_OBJECT('rule','each required restore parity scenario must have at least one passed parity run'));
  SET v_blockers = v_blockers + IF(v_bad=0,0,1);

  SET p_status = IF(v_blockers > 0, 'red', IF(v_warnings > 0, 'yellow', 'green'));
  SET p_blocker_count = v_blockers;
  SET p_warning_count = v_warnings;
  SET p_readiness_run_id = v_run_id;

  UPDATE mmo_readiness_runs
     SET status=p_status, finished_at=CURRENT_TIMESTAMP(6), blocker_count=v_blockers, warning_count=v_warnings
   WHERE readiness_run_id=v_run_id;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_mmo_readiness_latest AS
SELECT BIN_TO_UUID(r.readiness_run_id,1) AS readiness_run_uuid,
       BIN_TO_UUID(r.world_instance_id,1) AS world_instance_uuid,
       r.run_key,
       r.status,
       r.blocker_count,
       r.warning_count,
       r.started_at,
       r.finished_at
FROM mmo_readiness_runs r
WHERE r.started_at = (SELECT MAX(r2.started_at) FROM mmo_readiness_runs r2 WHERE r2.world_instance_id=r.world_instance_id);

CREATE OR REPLACE VIEW v_mmo_readiness_blockers AS
SELECT BIN_TO_UUID(run.readiness_run_id,1) AS readiness_run_uuid,
       run.run_key,
       run.status AS run_status,
       res.check_name,
       res.severity,
       res.checked_count,
       res.problem_count,
       res.details,
       res.created_at
FROM mmo_readiness_runs run
JOIN mmo_readiness_results res ON res.readiness_run_id=run.readiness_run_id
WHERE res.severity IN ('blocker','warning')
ORDER BY run.started_at DESC, res.severity ASC, res.problem_count DESC;

CREATE OR REPLACE VIEW v_mmo_remaining_work AS
SELECT 1 AS sort_order, 'C++ semantic hooks' AS area, 'partial' AS status, 'Header-only contract/scaffold exists; real calls still need insertion at World/Inventory/Npc/Interactive/GameScript mutation boundaries.' AS detail
UNION ALL SELECT 2, 'Server RPC/MySQL adapter', 'partial', 'DB action outbox exists; production transport/worker process still needed.'
UNION ALL SELECT 3, 'Strict replay', 'partial', 'Event contract validator exists; deterministic full rebuild from content baseline is still future work.'
UNION ALL SELECT 4, 'Restore parity', 'partial', 'Scenario registry/gate exists; actual native .sav vs MySQL comparison must be run for all scenarios.'
UNION ALL SELECT 5, 'Server authority/network', 'not_started', 'Movement/combat validation, interest management, protocol, anti-cheat and shard orchestration still needed.';

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES('production/mysql/018_mmo_readiness_dashboard', 'gothic-mmo-readiness-dashboard-v1-mysql', 'Production readiness dashboard aggregating migrations, action outbox, replay contracts and restore parity gate blockers.')
ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes), applied_at=CURRENT_TIMESTAMP(6);
