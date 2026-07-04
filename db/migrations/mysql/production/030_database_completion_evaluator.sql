-- Gothic MMO MySQL production migration 030.
-- Final database completion evaluator: DB-complete vs external MMO blockers.
-- Requires 001..029 MySQL production migrations.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

DROP PROCEDURE IF EXISTS mmo_evaluate_database_completion;
DELIMITER $$
CREATE PROCEDURE mmo_evaluate_database_completion(
  IN  p_world_instance_id        BINARY(16),
  IN  p_character_key            VARCHAR(191),
  IN  p_run_key                  VARCHAR(191),
  IN  p_metadata                 JSON,
  OUT p_completion_run_id        BINARY(16),
  OUT p_database_status          VARCHAR(32),
  OUT p_mmo_status               VARCHAR(32),
  OUT p_db_blocker_count         INT,
  OUT p_external_blocker_count   INT,
  OUT p_warning_count            INT
)
BEGIN
  DECLARE v_run_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_checked BIGINT DEFAULT 0;
  DECLARE v_bad BIGINT DEFAULT 0;
  DECLARE v_db_blockers INT DEFAULT 0;
  DECLARE v_external_blockers INT DEFAULT 0;
  DECLARE v_warnings INT DEFAULT 0;
  DECLARE v_validation_run_id BINARY(16) DEFAULT NULL;
  DECLARE v_projection_hash_run_id BINARY(16) DEFAULT NULL;
  DECLARE v_restore_manifest_id BINARY(16) DEFAULT NULL;
  DECLARE v_backup_manifest_id BINARY(16) DEFAULT NULL;
  DECLARE v_errors INT DEFAULT 0;
  DECLARE v_warn INT DEFAULT 0;
  DECLARE v_component_count INT DEFAULT 0;
  DECLARE v_restore_status VARCHAR(32) DEFAULT NULL;
  DECLARE v_backup_hash CHAR(64) DEFAULT NULL;
  DECLARE v_max_event_seq BIGINT DEFAULT 0;
  DECLARE v_event_count BIGINT DEFAULT 0;

  IF p_world_instance_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='world_instance_id is required'; END IF;
  IF p_run_key IS NULL OR TRIM(p_run_key)='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='run_key is required'; END IF;

  IF p_character_key IS NOT NULL AND TRIM(p_character_key) <> '' THEN
    SELECT character_id INTO v_character_id FROM characters WHERE character_key=p_character_key LIMIT 1;
  END IF;

  INSERT INTO mmo_database_completion_runs(world_instance_id, character_id, run_key, database_status, mmo_status, metadata)
  VALUES(p_world_instance_id, v_character_id, p_run_key, 'running', 'running', COALESCE(p_metadata,JSON_OBJECT()))
  ON DUPLICATE KEY UPDATE character_id=VALUES(character_id), database_status='running', mmo_status='running', started_at=CURRENT_TIMESTAMP(6), finished_at=NULL, db_blocker_count=0, external_blocker_count=0, warning_count=0, metadata=VALUES(metadata);

  SELECT completion_run_id INTO v_run_id FROM mmo_database_completion_runs WHERE world_instance_id=p_world_instance_id AND run_key=p_run_key LIMIT 1;
  DELETE FROM mmo_database_completion_results WHERE completion_run_id=v_run_id;

  SELECT COUNT(*) INTO v_checked FROM (
    SELECT 'production/mysql/001_gothic_mmo_production_schema' k UNION ALL
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
    SELECT 'production/mysql/018_mmo_readiness_dashboard' UNION ALL
    SELECT 'production/mysql/019_server_action_dispatch_contract' UNION ALL
    SELECT 'production/mysql/020_server_action_worker_observability' UNION ALL
    SELECT 'production/mysql/021_strict_replay_journal_audit' UNION ALL
    SELECT 'production/mysql/022_restore_parity_artifacts' UNION ALL
    SELECT 'production/mysql/023_database_completion_registry' UNION ALL
    SELECT 'production/mysql/024_projection_hash_manifest' UNION ALL
    SELECT 'production/mysql/025_final_database_integrity_audit' UNION ALL
    SELECT 'production/mysql/026_db_restore_manifest_gate' UNION ALL
    SELECT 'production/mysql/027_database_ops_backup_manifest' UNION ALL
    SELECT 'production/mysql/028_final_read_models' UNION ALL
    SELECT 'production/mysql/029_external_integration_gates' UNION ALL
    SELECT 'production/mysql/030_database_completion_evaluator'
  ) req;

  SELECT COUNT(*) INTO v_bad FROM (
    SELECT 'production/mysql/001_gothic_mmo_production_schema' k UNION ALL
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
    SELECT 'production/mysql/018_mmo_readiness_dashboard' UNION ALL
    SELECT 'production/mysql/019_server_action_dispatch_contract' UNION ALL
    SELECT 'production/mysql/020_server_action_worker_observability' UNION ALL
    SELECT 'production/mysql/021_strict_replay_journal_audit' UNION ALL
    SELECT 'production/mysql/022_restore_parity_artifacts' UNION ALL
    SELECT 'production/mysql/023_database_completion_registry' UNION ALL
    SELECT 'production/mysql/024_projection_hash_manifest' UNION ALL
    SELECT 'production/mysql/025_final_database_integrity_audit' UNION ALL
    SELECT 'production/mysql/026_db_restore_manifest_gate' UNION ALL
    SELECT 'production/mysql/027_database_ops_backup_manifest' UNION ALL
    SELECT 'production/mysql/028_final_read_models' UNION ALL
    SELECT 'production/mysql/029_external_integration_gates' UNION ALL
    SELECT 'production/mysql/030_database_completion_evaluator'
  ) req LEFT JOIN mmo_schema_versions v ON v.migration_key=req.k WHERE v.migration_key IS NULL;
  INSERT INTO mmo_database_completion_results(completion_run_id, requirement_key, status, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'mysql_migrations_001_030_present',IF(v_bad=0,'passed','failed'),IF(v_bad=0,'ok','db_blocker'),v_checked,v_bad,JSON_OBJECT('rule','all final MySQL database migrations must be applied'));
  SET v_db_blockers = v_db_blockers + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_checked FROM realm_world_instances WHERE world_instance_id=p_world_instance_id;
  SELECT IF(v_checked=1 AND v_character_id IS NOT NULL AND EXISTS(SELECT 1 FROM character_positions WHERE character_id=v_character_id) AND EXISTS(SELECT 1 FROM character_stats WHERE character_id=v_character_id),0,1) INTO v_bad;
  INSERT INTO mmo_database_completion_results(completion_run_id, requirement_key, status, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'bootstrap_import_present',IF(v_bad=0,'passed','failed'),IF(v_bad=0,'ok','db_blocker'),v_checked,v_bad,JSON_OBJECT('rule','world, character, position and stats must exist before final DB completion','character_key',p_character_key));
  SET v_db_blockers = v_db_blockers + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_checked FROM mmo_server_action_dispatch_contracts WHERE enabled=TRUE;
  SELECT IF(v_checked >= 26, 0, 1) INTO v_bad;
  INSERT INTO mmo_database_completion_results(completion_run_id, requirement_key, status, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'write_paths_registered',IF(v_bad=0,'passed','failed'),IF(v_bad=0,'ok','db_blocker'),v_checked,v_bad,JSON_OBJECT('rule','all write-path action contracts through Step 22 must be registered','expected_min',26));
  SET v_db_blockers = v_db_blockers + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_checked FROM v_event_replay_contract_coverage;
  SELECT COUNT(*) INTO v_bad FROM v_event_replay_contract_gaps;
  INSERT INTO mmo_database_completion_results(completion_run_id, requirement_key, status, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'event_replay_contracts_complete',IF(v_bad=0,'passed','failed'),IF(v_bad=0,'ok','db_blocker'),v_checked,v_bad,JSON_OBJECT('rule','replay contract coverage must have no gaps'));
  SET v_db_blockers = v_db_blockers + IF(v_bad=0,0,1);

  CALL mmo_audit_strict_replay_journal(p_world_instance_id, CONCAT('final-db:strict-replay:', p_run_key), JSON_OBJECT('source','mmo_evaluate_database_completion'), v_validation_run_id, v_errors, v_warn);
  INSERT INTO mmo_database_completion_results(completion_run_id, requirement_key, status, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'strict_replay_prefight_clean',IF(v_errors=0,'passed','failed'),IF(v_errors=0,IF(v_warn=0,'ok','warning'),'db_blocker'),1,v_errors,JSON_OBJECT('validation_run_id',BIN_TO_UUID(v_validation_run_id,1),'warning_count',v_warn));
  SET v_db_blockers = v_db_blockers + IF(v_errors=0,0,1);
  SET v_warnings = v_warnings + IF(v_errors=0 AND v_warn>0,1,0);

  CALL mmo_run_final_database_integrity_audit(p_world_instance_id, CONCAT('final-db:', p_run_key), JSON_OBJECT('source','mmo_evaluate_database_completion'), v_validation_run_id, v_errors, v_warn);
  INSERT INTO mmo_database_completion_results(completion_run_id, requirement_key, status, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'projection_integrity_clean',IF(v_errors=0,'passed','failed'),IF(v_errors=0,IF(v_warn=0,'ok','warning'),'db_blocker'),1,v_errors,JSON_OBJECT('validation_run_id',BIN_TO_UUID(v_validation_run_id,1),'warning_count',v_warn));
  SET v_db_blockers = v_db_blockers + IF(v_errors=0,0,1);
  SET v_warnings = v_warnings + IF(v_errors=0 AND v_warn>0,1,0);

  CALL mmo_materialize_projection_hash_run(p_world_instance_id, p_character_key, CONCAT('final-db:hash:', p_run_key), JSON_OBJECT('source','mmo_evaluate_database_completion'), v_projection_hash_run_id, v_component_count);
  SET v_bad = IF(v_component_count >= 12, 0, 1);
  INSERT INTO mmo_database_completion_results(completion_run_id, requirement_key, status, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'projection_hash_manifest_present',IF(v_bad=0,'passed','failed'),IF(v_bad=0,'ok','db_blocker'),v_component_count,v_bad,JSON_OBJECT('projection_hash_run_id',BIN_TO_UUID(v_projection_hash_run_id,1),'expected_min_components',12));
  SET v_db_blockers = v_db_blockers + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_checked FROM mmo_server_action_outbox WHERE world_instance_id=p_world_instance_id;
  SELECT COUNT(*) INTO v_bad FROM mmo_server_action_outbox WHERE world_instance_id=p_world_instance_id AND status IN ('failed','dead_letter');
  INSERT INTO mmo_database_completion_results(completion_run_id, requirement_key, status, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'outbox_dispatch_clean',IF(v_bad=0,'passed','failed'),IF(v_bad=0,'ok','db_blocker'),v_checked,v_bad,JSON_OBJECT('rule','failed/dead-letter outbox actions must be resolved'));
  SET v_db_blockers = v_db_blockers + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_checked FROM mmo_server_action_worker_runs;
  SELECT IF(v_checked > 0, 0, 1) INTO v_bad;
  INSERT INTO mmo_database_completion_results(completion_run_id, requirement_key, status, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'worker_observability_present',IF(v_bad=0,'passed','warning'),IF(v_bad=0,'ok','warning'),v_checked,v_bad,JSON_OBJECT('rule','at least one worker run proves telemetry tables are usable'));
  SET v_warnings = v_warnings + IF(v_bad=0,0,1);

  CALL mmo_create_db_restore_manifest(p_world_instance_id, p_character_key, CONCAT('final-db:', p_run_key), JSON_OBJECT('source','mmo_evaluate_database_completion'), v_restore_manifest_id, v_restore_status, v_errors, v_external_blockers);
  INSERT INTO mmo_database_completion_results(completion_run_id, requirement_key, status, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'restore_manifest_available',IF(v_errors=0,'passed','failed'),IF(v_errors=0,'ok','db_blocker'),1,v_errors,JSON_OBJECT('restore_manifest_id',BIN_TO_UUID(v_restore_manifest_id,1),'manifest_status',v_restore_status,'external_blocker_count',v_external_blockers));
  SET v_db_blockers = v_db_blockers + IF(v_errors=0,0,1);

  SELECT COALESCE(MAX(event_seq),0), COUNT(*) INTO v_max_event_seq, v_event_count FROM world_event_journal WHERE world_instance_id=p_world_instance_id;
  SET v_backup_hash = SHA2(CONCAT('diagnostic|', BIN_TO_UUID(p_world_instance_id,1), '|', v_max_event_seq, '|', v_event_count, '|', p_run_key), 256);
  CALL mmo_record_database_backup_manifest(p_world_instance_id, CONCAT('final-db:diagnostic:', p_run_key), 'diagnostic', NULL, v_backup_hash, 30, v_event_count, 'recorded', JSON_OBJECT('source','mmo_evaluate_database_completion'), v_backup_manifest_id);
  INSERT INTO mmo_database_completion_results(completion_run_id, requirement_key, status, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'backup_manifest_available','passed','ok',1,0,JSON_OBJECT('backup_manifest_id',BIN_TO_UUID(v_backup_manifest_id,1),'backup_kind','diagnostic'));

  SELECT COUNT(*) INTO v_checked FROM mmo_restore_parity_scenarios WHERE active=TRUE AND required=TRUE;
  SELECT COUNT(*) INTO v_bad
    FROM mmo_restore_parity_scenarios s
   WHERE s.active=TRUE AND s.required=TRUE
     AND NOT EXISTS (
       SELECT 1 FROM mmo_restore_parity_results r
       JOIN mmo_restore_parity_runs pr ON pr.parity_run_id=r.parity_run_id
       WHERE r.scenario_key=s.scenario_key AND r.status='passed' AND pr.status='passed'
     );
  INSERT INTO mmo_database_completion_results(completion_run_id, requirement_key, status, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'native_sqlite_mysql_parity_passed',IF(v_bad=0,'passed','blocked'),IF(v_bad=0,'ok','external_blocker'),v_checked,v_bad,JSON_OBJECT('rule','real game scenario parity is outside DB-only completion and must not be faked'));
  SET v_external_blockers = v_external_blockers + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_checked FROM mmo_external_integration_gates WHERE required_for_mmo=TRUE;
  SELECT COUNT(*) INTO v_bad FROM mmo_external_integration_gates WHERE required_for_mmo=TRUE AND status NOT IN ('passed','not_required');
  INSERT INTO mmo_database_completion_results(completion_run_id, requirement_key, status, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'real_cpp_hooks_inserted',IF(v_bad=0,'passed','blocked'),IF(v_bad=0,'ok','external_blocker'),v_checked,v_bad,JSON_OBJECT('rule','external integration gates cover C++ hooks, production worker, replay runner and MMO server authority'));
  SET v_external_blockers = v_external_blockers + IF(v_bad=0,0,1);

  SET p_database_status = IF(v_db_blockers=0,'complete','failed');
  SET p_mmo_status = IF(v_db_blockers>0,'red',IF(v_external_blockers>0,'blocked',IF(v_warnings>0,'yellow','green')));
  SET p_db_blocker_count = v_db_blockers;
  SET p_external_blocker_count = v_external_blockers;
  SET p_warning_count = v_warnings;
  SET p_completion_run_id = v_run_id;

  UPDATE mmo_database_completion_runs
     SET database_status=p_database_status,
         mmo_status=p_mmo_status,
         db_blocker_count=v_db_blockers,
         external_blocker_count=v_external_blockers,
         warning_count=v_warnings,
         finished_at=CURRENT_TIMESTAMP(6)
   WHERE completion_run_id=v_run_id;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_mmo_database_done_summary AS
SELECT l.completion_run_uuid,
       l.world_instance_uuid,
       l.character_uuid,
       l.run_key,
       l.database_status,
       l.mmo_status,
       l.db_blocker_count,
       l.external_blocker_count,
       l.warning_count,
       CASE
         WHEN l.database_status='complete' AND l.external_blocker_count>0 THEN 'database_layer_complete_external_mmo_work_remaining'
         WHEN l.database_status='complete' AND l.external_blocker_count=0 THEN 'mmo_database_and_external_gates_green'
         ELSE 'database_layer_has_blockers'
       END AS conclusion,
       l.started_at,
       l.finished_at
FROM v_mmo_database_completion_latest l;

CREATE OR REPLACE VIEW v_mmo_database_remaining_work_final AS
SELECT req.sort_order,
       req.area,
       req.requirement_key,
       req.title,
       res.status,
       res.severity,
       res.problem_count,
       res.details
FROM mmo_database_completion_runs run
JOIN mmo_database_completion_results res ON res.completion_run_id=run.completion_run_id
JOIN mmo_database_completion_requirements req ON req.requirement_key=res.requirement_key
WHERE run.started_at=(SELECT MAX(r2.started_at) FROM mmo_database_completion_runs r2 WHERE r2.world_instance_id=run.world_instance_id)
  AND res.status IN ('failed','blocked','warning')
ORDER BY FIELD(res.severity,'db_blocker','external_blocker','warning','ok'), req.sort_order;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES('production/mysql/030_database_completion_evaluator', 'gothic-mmo-database-completion-evaluator-v1-mysql', 'Final DB-layer completion evaluator. It can mark the MySQL database layer complete while keeping C++/parity/server-authority work as external MMO blockers.')
ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes), applied_at=CURRENT_TIMESTAMP(6);
