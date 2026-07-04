-- Gothic MMO MySQL production migration 025.
-- Final database integrity audit over projections, outbox, replay contracts and parity evidence.
-- Requires 001..024 MySQL production migrations.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

DROP PROCEDURE IF EXISTS mmo_run_final_database_integrity_audit;
DELIMITER $$
CREATE PROCEDURE mmo_run_final_database_integrity_audit(
  IN  p_world_instance_id  BINARY(16),
  IN  p_run_key            VARCHAR(191),
  IN  p_metadata           JSON,
  OUT p_validation_run_id  BINARY(16),
  OUT p_error_count        INT,
  OUT p_warning_count      INT
)
BEGIN
  DECLARE v_run_id BINARY(16) DEFAULT NULL;
  DECLARE v_max_event_seq BIGINT DEFAULT 0;
  DECLARE v_total BIGINT DEFAULT 0;
  DECLARE v_bad BIGINT DEFAULT 0;
  DECLARE v_errors INT DEFAULT 0;
  DECLARE v_warnings INT DEFAULT 0;

  IF p_world_instance_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='world_instance_id is required'; END IF;
  IF p_run_key IS NULL OR TRIM(p_run_key)='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='run_key is required'; END IF;

  SELECT COALESCE(MAX(event_seq),0) INTO v_max_event_seq FROM world_event_journal WHERE world_instance_id=p_world_instance_id;

  INSERT INTO mmo_projection_validation_runs(world_instance_id, run_key, run_kind, status, max_event_seq, metadata)
  VALUES(p_world_instance_id, p_run_key, 'smoke', 'running', v_max_event_seq, COALESCE(p_metadata,JSON_OBJECT()))
  ON DUPLICATE KEY UPDATE run_kind='smoke', status='running', started_at=CURRENT_TIMESTAMP(6), finished_at=NULL, max_event_seq=VALUES(max_event_seq), error_count=0, warning_count=0, metadata=VALUES(metadata);

  SELECT validation_run_id INTO v_run_id FROM mmo_projection_validation_runs WHERE world_instance_id=p_world_instance_id AND run_key=p_run_key LIMIT 1;
  DELETE FROM mmo_projection_validation_results WHERE validation_run_id=v_run_id;

  SELECT COUNT(*) INTO v_total FROM character_wallets;
  SELECT COUNT(*) INTO v_bad FROM character_wallets WHERE amount < 0;
  INSERT INTO mmo_projection_validation_results(validation_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'final_wallets_non_negative',IF(v_bad=0,'ok','error'),v_total,v_bad,JSON_OBJECT('rule','wallet balances cannot be negative'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_total FROM character_inventory ci JOIN item_instances ii ON ii.item_instance_id=ci.item_instance_id;
  SELECT COUNT(*) INTO v_bad
    FROM character_inventory ci
    JOIN item_instances ii ON ii.item_instance_id=ci.item_instance_id
   WHERE ii.owner_type <> 'character'
      OR ii.owner_id <> ci.character_id
      OR ii.lifecycle_state <> 'active'
      OR ii.quantity <> ci.amount;
  INSERT INTO mmo_projection_validation_results(validation_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'final_character_inventory_owner_quantity_match',IF(v_bad=0,'ok','error'),v_total,v_bad,JSON_OBJECT('rule','character inventory rows must match active item owner and quantity'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_total FROM character_equipment ce;
  SELECT COUNT(*) INTO v_bad
    FROM character_equipment ce
   WHERE NOT EXISTS (SELECT 1 FROM character_inventory ci WHERE ci.character_id=ce.character_id AND ci.item_instance_id=ce.item_instance_id);
  INSERT INTO mmo_projection_validation_results(validation_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'final_equipment_items_are_inventory_items',IF(v_bad=0,'ok','error'),v_total,v_bad,JSON_OBJECT('rule','equipped item must still be in character inventory'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_total FROM world_inventory wi JOIN item_instances ii ON ii.item_instance_id=wi.item_instance_id WHERE wi.world_instance_id=p_world_instance_id;
  SELECT COUNT(*) INTO v_bad
    FROM world_inventory wi
    JOIN item_instances ii ON ii.item_instance_id=wi.item_instance_id
   WHERE wi.world_instance_id=p_world_instance_id
     AND (ii.owner_type <> 'container' OR ii.lifecycle_state <> 'active' OR ii.quantity <> wi.amount);
  INSERT INTO mmo_projection_validation_results(validation_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'final_world_inventory_owner_quantity_match',IF(v_bad=0,'ok','error'),v_total,v_bad,JSON_OBJECT('rule','world/container inventory rows must match active container item quantity'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_total FROM world_entity_state WHERE world_instance_id=p_world_instance_id AND health_current IS NOT NULL;
  SELECT COUNT(*) INTO v_bad FROM world_entity_state WHERE world_instance_id=p_world_instance_id AND health_current IS NOT NULL AND health_max IS NOT NULL AND health_current > health_max;
  INSERT INTO mmo_projection_validation_results(validation_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'final_world_entity_health_bounds',IF(v_bad=0,'ok','error'),v_total,v_bad,JSON_OBJECT('rule','world entity health_current cannot exceed health_max'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_total FROM world_event_journal WHERE world_instance_id=p_world_instance_id AND source IN ('server','test');
  SELECT COUNT(*) INTO v_bad
    FROM world_event_journal e
   WHERE e.world_instance_id=p_world_instance_id
     AND e.source IN ('server','test')
     AND (e.idempotency_key IS NULL OR TRIM(e.idempotency_key)='');
  INSERT INTO mmo_projection_validation_results(validation_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'final_server_events_are_idempotent',IF(v_bad=0,'ok','error'),v_total,v_bad,JSON_OBJECT('rule','server/test semantic events require idempotency keys'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_total FROM v_event_replay_contract_coverage;
  SELECT COUNT(*) INTO v_bad FROM v_event_replay_contract_gaps;
  INSERT INTO mmo_projection_validation_results(validation_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'final_replay_contract_gap_count',IF(v_bad=0,'ok','error'),v_total,v_bad,JSON_OBJECT('rule','event replay contract registry must cover all server/test event types'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_total FROM mmo_server_action_dispatch_contracts WHERE enabled=TRUE;
  SELECT COUNT(*) INTO v_bad
    FROM mmo_server_action_dispatch_contracts d
   WHERE d.enabled=TRUE
     AND d.event_type IS NOT NULL
     AND NOT EXISTS (SELECT 1 FROM mmo_event_projection_contracts c WHERE c.event_type=d.event_type AND c.event_class=d.event_class);
  INSERT INTO mmo_projection_validation_results(validation_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'final_dispatch_contracts_have_replay_contracts',IF(v_bad=0,'ok','error'),v_total,v_bad,JSON_OBJECT('rule','enabled dispatch contracts with events need matching replay contracts by event_type and event_class'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_total FROM mmo_server_action_outbox WHERE world_instance_id=p_world_instance_id;
  SELECT COUNT(*) INTO v_bad FROM mmo_server_action_outbox WHERE world_instance_id=p_world_instance_id AND status IN ('failed','dead_letter');
  INSERT INTO mmo_projection_validation_results(validation_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'final_outbox_has_no_failed_dead_letters',IF(v_bad=0,'ok','error'),v_total,v_bad,JSON_OBJECT('rule','failed/dead-letter actions must be resolved before DB completion'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_total FROM mmo_projection_component_hashes h JOIN mmo_projection_hash_runs r ON r.projection_hash_run_id=h.projection_hash_run_id WHERE r.world_instance_id=p_world_instance_id;
  SELECT IF(v_total >= 12, 0, 1) INTO v_bad;
  INSERT INTO mmo_projection_validation_results(validation_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'final_projection_hash_manifest_has_components',IF(v_bad=0,'ok','warning'),v_total,v_bad,JSON_OBJECT('rule','materialize projection hash manifest before final DB signoff','expected_min_components',12));
  SET v_warnings = v_warnings + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_total FROM mmo_restore_parity_scenarios WHERE active=TRUE AND required=TRUE;
  SELECT COUNT(*) INTO v_bad
    FROM mmo_restore_parity_scenarios s
   WHERE s.active=TRUE AND s.required=TRUE
     AND NOT EXISTS (
       SELECT 1 FROM mmo_restore_parity_results r
       JOIN mmo_restore_parity_runs pr ON pr.parity_run_id=r.parity_run_id
       WHERE r.scenario_key=s.scenario_key AND r.status='passed' AND pr.status='passed'
     );
  INSERT INTO mmo_projection_validation_results(validation_run_id, check_name, severity, checked_count, problem_count, details)
  VALUES(v_run_id,'final_restore_parity_scenarios_passed',IF(v_bad=0,'ok','warning'),v_total,v_bad,JSON_OBJECT('rule','external game parity scenarios must pass before MMO readiness, but DB schema can be complete without faking them'));
  SET v_warnings = v_warnings + IF(v_bad=0,0,1);

  UPDATE mmo_projection_validation_runs
     SET status=IF(v_errors=0,'passed','failed'), finished_at=CURRENT_TIMESTAMP(6), error_count=v_errors, warning_count=v_warnings
   WHERE validation_run_id=v_run_id;

  SET p_validation_run_id=v_run_id;
  SET p_error_count=v_errors;
  SET p_warning_count=v_warnings;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_final_database_integrity_latest AS
SELECT BIN_TO_UUID(run.validation_run_id,1) AS validation_run_uuid,
       BIN_TO_UUID(run.world_instance_id,1) AS world_instance_uuid,
       run.run_key,
       run.status AS run_status,
       run.error_count,
       run.warning_count,
       res.check_name,
       res.severity,
       res.checked_count,
       res.problem_count,
       res.details,
       res.created_at
FROM mmo_projection_validation_runs run
JOIN mmo_projection_validation_results res ON res.validation_run_id=run.validation_run_id
WHERE run.run_kind='smoke'
  AND run.run_key LIKE 'final-db:%'
ORDER BY run.started_at DESC, FIELD(res.severity,'error','warning','ok'), res.check_name;

CREATE OR REPLACE VIEW v_final_database_integrity_latest_errors AS
SELECT * FROM v_final_database_integrity_latest
WHERE severity IN ('error','warning') AND problem_count > 0;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES('production/mysql/025_final_database_integrity_audit', 'gothic-mmo-final-database-integrity-audit-v1-mysql', 'Final DB-layer integrity audit for inventory ownership, equipment, world inventory, health bounds, idempotency, replay contracts, outbox and projection hash evidence. Fix: mmo_event_projection_contracts has no enabled column; dispatch-to-replay matching now uses event_type/event_class only.')
ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes), applied_at=CURRENT_TIMESTAMP(6);
