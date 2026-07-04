-- Gothic MMO MySQL production migration 014.
-- Extended projection diagnostics for dev/prod validation.
-- Requires 001..013 MySQL production migrations and a bootstrap import.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

DROP PROCEDURE IF EXISTS mmo_validate_world_projection_extended;
DELIMITER $$
CREATE PROCEDURE mmo_validate_world_projection_extended(
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
  DECLARE v_cnt BIGINT DEFAULT 0;
  DECLARE v_bad BIGINT DEFAULT 0;
  DECLARE v_errors INT DEFAULT 0;
  DECLARE v_warnings INT DEFAULT 0;

  IF p_world_instance_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='world_instance_id is required'; END IF;
  IF p_run_key IS NULL OR p_run_key='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='run key is required'; END IF;

  SELECT COALESCE(MAX(event_seq),0) INTO v_max_event_seq FROM world_event_journal WHERE world_instance_id=p_world_instance_id;

  INSERT INTO mmo_projection_validation_runs(world_instance_id, run_key, run_kind, status, max_event_seq, metadata)
  VALUES(p_world_instance_id, p_run_key, 'basic', 'running', v_max_event_seq, JSON_MERGE_PATCH(COALESCE(p_metadata, JSON_OBJECT()), JSON_OBJECT('validator','extended-014')))
  ON DUPLICATE KEY UPDATE status='running', started_at=CURRENT_TIMESTAMP(6), finished_at=NULL, max_event_seq=VALUES(max_event_seq), error_count=0, warning_count=0, metadata=VALUES(metadata);

  SELECT validation_run_id INTO v_run_id FROM mmo_projection_validation_runs WHERE world_instance_id=p_world_instance_id AND run_key=p_run_key LIMIT 1;
  DELETE FROM mmo_projection_validation_results WHERE validation_run_id=v_run_id;

  SELECT COUNT(*) INTO v_cnt FROM character_inventory;
  SELECT COUNT(*) INTO v_bad
    FROM character_inventory ci JOIN item_instances ii ON ii.item_instance_id=ci.item_instance_id
   WHERE ii.lifecycle_state='active' AND ii.owner_type='character' AND (ci.amount <> ii.quantity OR ii.owner_id <> ci.character_id);
  INSERT INTO mmo_projection_validation_results(validation_run_id,check_name,severity,checked_count,problem_count,details)
  VALUES(v_run_id,'character_inventory_amount_owner_match',IF(v_bad=0,'ok','error'),v_cnt,v_bad,JSON_OBJECT('rule','character_inventory.amount must equal item_instances.quantity and owner_id must equal character_id'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_cnt FROM item_instances WHERE owner_type='character' AND lifecycle_state='active';
  SELECT COUNT(*) INTO v_bad
    FROM item_instances ii
   WHERE ii.owner_type='character' AND ii.lifecycle_state='active'
     AND NOT EXISTS (SELECT 1 FROM character_inventory ci WHERE ci.character_id=ii.owner_id AND ci.item_instance_id=ii.item_instance_id);
  INSERT INTO mmo_projection_validation_results(validation_run_id,check_name,severity,checked_count,problem_count,details)
  VALUES(v_run_id,'active_character_item_has_inventory_row',IF(v_bad=0,'ok','error'),v_cnt,v_bad,JSON_OBJECT('rule','every active character-owned item must be present in character_inventory'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_cnt FROM character_equipment;
  SELECT COUNT(*) INTO v_bad
    FROM character_equipment ce
   WHERE NOT EXISTS (SELECT 1 FROM character_inventory ci WHERE ci.character_id=ce.character_id AND ci.item_instance_id=ce.item_instance_id);
  INSERT INTO mmo_projection_validation_results(validation_run_id,check_name,severity,checked_count,problem_count,details)
  VALUES(v_run_id,'equipment_item_is_in_inventory',IF(v_bad=0,'ok','error'),v_cnt,v_bad,JSON_OBJECT('rule','equipment is a view over inventory, not separate ownership'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_cnt FROM world_inventory WHERE world_instance_id=p_world_instance_id;
  SELECT COUNT(*) INTO v_bad
    FROM world_inventory wi JOIN item_instances ii ON ii.item_instance_id=wi.item_instance_id
   WHERE wi.world_instance_id=p_world_instance_id AND (ii.lifecycle_state <> 'active' OR ii.quantity <> wi.amount OR ii.owner_type NOT IN ('container','world_entity'));
  INSERT INTO mmo_projection_validation_results(validation_run_id,check_name,severity,checked_count,problem_count,details)
  VALUES(v_run_id,'world_inventory_item_owner_amount_match',IF(v_bad=0,'ok','error'),v_cnt,v_bad,JSON_OBJECT('rule','world/container inventory rows must point to active item instances with matching quantity'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_cnt FROM npc_trade_inventory WHERE world_instance_id=p_world_instance_id;
  SELECT COUNT(*) INTO v_bad
    FROM npc_trade_inventory nti JOIN item_instances ii ON ii.item_instance_id=nti.item_instance_id
   WHERE nti.world_instance_id=p_world_instance_id AND (nti.amount <> ii.quantity OR ii.lifecycle_state <> 'active' OR nti.stock_state='available' AND ii.owner_type <> 'system');
  INSERT INTO mmo_projection_validation_results(validation_run_id,check_name,severity,checked_count,problem_count,details)
  VALUES(v_run_id,'npc_trade_inventory_consistency',IF(v_bad=0,'ok','error'),v_cnt,v_bad,JSON_OBJECT('rule','available NPC stock is held in npc_trade_inventory and item_instances.owner_type=system'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_cnt FROM world_entity_state WHERE world_instance_id=p_world_instance_id;
  SELECT COUNT(*) INTO v_bad
    FROM world_entity_state
   WHERE world_instance_id=p_world_instance_id AND health_current IS NOT NULL AND health_max IS NOT NULL AND health_current > health_max;
  INSERT INTO mmo_projection_validation_results(validation_run_id,check_name,severity,checked_count,problem_count,details)
  VALUES(v_run_id,'world_entity_health_bounds',IF(v_bad=0,'ok','error'),v_cnt,v_bad,JSON_OBJECT('rule','world_entity_state.health_current cannot exceed health_max'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_bad
    FROM world_entity_state
   WHERE world_instance_id=p_world_instance_id AND lifecycle_state='dead' AND COALESCE(health_current,0) > 0;
  INSERT INTO mmo_projection_validation_results(validation_run_id,check_name,severity,checked_count,problem_count,details)
  VALUES(v_run_id,'dead_world_entity_has_zero_health',IF(v_bad=0,'ok','warning'),v_cnt,v_bad,JSON_OBJECT('rule','dead entities should have zero health unless classified as scripted exception'));
  SET v_warnings = v_warnings + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_bad
    FROM item_instances ii
   WHERE ii.lifecycle_state IN ('consumed','destroyed','archived')
     AND (EXISTS (SELECT 1 FROM character_inventory ci WHERE ci.item_instance_id=ii.item_instance_id)
       OR EXISTS (SELECT 1 FROM world_inventory wi WHERE wi.item_instance_id=ii.item_instance_id)
       OR EXISTS (SELECT 1 FROM npc_trade_inventory nti WHERE nti.item_instance_id=ii.item_instance_id));
  INSERT INTO mmo_projection_validation_results(validation_run_id,check_name,severity,checked_count,problem_count,details)
  VALUES(v_run_id,'inactive_item_not_in_inventory',IF(v_bad=0,'ok','error'),0,v_bad,JSON_OBJECT('rule','consumed/destroyed/archived item instances cannot still be in inventory projections'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  UPDATE mmo_projection_validation_runs
     SET status=IF(v_errors=0,'passed','failed'), finished_at=CURRENT_TIMESTAMP(6), error_count=v_errors, warning_count=v_warnings
   WHERE validation_run_id=v_run_id;

  SET p_validation_run_id=v_run_id;
  SET p_error_count=v_errors;
  SET p_warning_count=v_warnings;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_projection_validation_latest_errors AS
SELECT BIN_TO_UUID(r.validation_run_id,1) AS validation_run_uuid,
       r.run_key,
       r.status,
       res.check_name,
       res.severity,
       res.checked_count,
       res.problem_count,
       res.details,
       res.created_at
FROM mmo_projection_validation_runs r
JOIN mmo_projection_validation_results res ON res.validation_run_id=r.validation_run_id
WHERE r.started_at = (SELECT MAX(r2.started_at) FROM mmo_projection_validation_runs r2 WHERE r2.world_instance_id=r.world_instance_id)
  AND res.severity IN ('error','warning')
ORDER BY res.severity DESC, res.problem_count DESC, res.check_name;

CREATE OR REPLACE VIEW v_item_projection_diagnostics AS
SELECT 'character_item_without_inventory' AS diagnostic,
       ii.item_instance_key,
       BIN_TO_UUID(ii.item_instance_id,1) AS item_instance_uuid,
       ii.owner_type,
       BIN_TO_UUID(ii.owner_id,1) AS owner_uuid,
       ii.quantity,
       ii.lifecycle_state
FROM item_instances ii
WHERE ii.owner_type='character' AND ii.lifecycle_state='active'
  AND NOT EXISTS (SELECT 1 FROM character_inventory ci WHERE ci.character_id=ii.owner_id AND ci.item_instance_id=ii.item_instance_id)
UNION ALL
SELECT 'inventory_amount_mismatch' AS diagnostic,
       ii.item_instance_key,
       BIN_TO_UUID(ii.item_instance_id,1) AS item_instance_uuid,
       ii.owner_type,
       BIN_TO_UUID(ii.owner_id,1) AS owner_uuid,
       ii.quantity,
       ii.lifecycle_state
FROM character_inventory ci JOIN item_instances ii ON ii.item_instance_id=ci.item_instance_id
WHERE ii.lifecycle_state='active' AND (ci.amount <> ii.quantity OR ii.owner_id <> ci.character_id)
UNION ALL
SELECT 'inactive_item_still_projected' AS diagnostic,
       ii.item_instance_key,
       BIN_TO_UUID(ii.item_instance_id,1) AS item_instance_uuid,
       ii.owner_type,
       BIN_TO_UUID(ii.owner_id,1) AS owner_uuid,
       ii.quantity,
       ii.lifecycle_state
FROM item_instances ii
WHERE ii.lifecycle_state IN ('consumed','destroyed','archived')
  AND (EXISTS (SELECT 1 FROM character_inventory ci WHERE ci.item_instance_id=ii.item_instance_id)
    OR EXISTS (SELECT 1 FROM world_inventory wi WHERE wi.item_instance_id=ii.item_instance_id)
    OR EXISTS (SELECT 1 FROM npc_trade_inventory nti WHERE nti.item_instance_id=ii.item_instance_id));

CREATE OR REPLACE VIEW v_world_entity_projection_diagnostics AS
SELECT BIN_TO_UUID(world_instance_id,1) AS world_instance_uuid,
       entity_key,
       entity_kind,
       lifecycle_state,
       health_current,
       health_max,
       row_version,
       CASE
         WHEN health_current IS NOT NULL AND health_max IS NOT NULL AND health_current > health_max THEN 'health_current_gt_health_max'
         WHEN lifecycle_state='dead' AND COALESCE(health_current,0) > 0 THEN 'dead_nonzero_health'
         ELSE 'ok'
       END AS diagnostic
FROM world_entity_state
WHERE (health_current IS NOT NULL AND health_max IS NOT NULL AND health_current > health_max)
   OR (lifecycle_state='dead' AND COALESCE(health_current,0) > 0);

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES('production/mysql/014_projection_diagnostics', 'gothic-mmo-projection-diagnostics-v1-mysql', 'Extended projection diagnostics and human-readable error views for item ownership, inventory and world entity invariants.')
ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes), applied_at=CURRENT_TIMESTAMP(6);
