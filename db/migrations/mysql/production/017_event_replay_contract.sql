-- Gothic MMO MySQL production migration 017.
-- Event replay contract coverage checks from world_event_journal to projection/audit tables.
-- Requires 001..016 MySQL production migrations and a bootstrap import.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE IF NOT EXISTS mmo_event_projection_contracts (
  event_type        VARCHAR(128) PRIMARY KEY,
  event_class       VARCHAR(32) NOT NULL,
  projection_name   VARCHAR(128) NOT NULL,
  audit_table       VARCHAR(128) NULL,
  procedure_name    VARCHAR(128) NULL,
  replay_priority   INT NOT NULL DEFAULT 1000,
  strict_replay     BOOLEAN NOT NULL DEFAULT TRUE,
  notes             TEXT NULL,
  updated_at        TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  CONSTRAINT mmo_event_projection_contracts_class_ck CHECK(event_class IN ('character','inventory','equipment','world_entity','quest','dialog','script','combat','trade','spell','system','diagnostic')),
  CONSTRAINT mmo_event_projection_contracts_priority_ck CHECK(replay_priority >= 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

INSERT INTO mmo_event_projection_contracts(event_type,event_class,projection_name,audit_table,procedure_name,replay_priority,strict_replay,notes)
VALUES
  ('bootstrap_import_completed','system','bootstrap_import','mmo_import_runs','tools/import_runtime_sqlite_to_mysql.py',10,TRUE,'Bootstrap import terminal event.'),
  ('character_login','character','server_sessions','server_sessions','mmo_login_character',20,TRUE,'Session opened.'),
  ('character_logout','character','server_sessions','server_sessions','mmo_logout_character',30,TRUE,'Session closed.'),
  ('character_position_checkpoint','character','character_positions','character_checkpoint_audit','mmo_checkpoint_character_state',40,TRUE,'Position/stat checkpoint.'),
  ('character_wallet_delta','inventory','character_wallets','character_wallet_audit','mmo_adjust_character_wallet',50,TRUE,'Generic wallet delta. Event class follows migration 004 emitted inventory-class journal rows.'),
  ('world_item_picked_up','inventory','item_instances+character_inventory+world_entity_state','world_item_audit','mmo_pickup_world_item',60,TRUE,'Loose world item picked up.'),
  ('world_item_removed','world_entity','item_instances+world_entity_state','world_item_audit','mmo_remove_world_item',70,TRUE,'Loose world item removed/archived.'),
  ('character_inventory_transferred','inventory','item_instances+character_inventory','character_inventory_audit','mmo_transfer_character_item',80,TRUE,'Character-to-character item instance transfer.'),
  ('character_item_equipped','equipment','character_equipment','character_inventory_audit','mmo_equip_character_item',90,TRUE,'Equipment projection over inventory.'),
  ('character_item_unequipped','equipment','character_equipment','character_inventory_audit','mmo_unequip_character_item',100,TRUE,'Equipment row removed; inventory stays.'),
  ('container_item_taken','inventory','world_inventory+character_inventory+item_instances','world_interactive_audit','mmo_take_container_item',110,TRUE,'Container to character.'),
  ('container_item_put','inventory','world_inventory+character_inventory+item_instances','world_interactive_audit','mmo_put_container_item',120,TRUE,'Character to container.'),
  ('interactive_state_changed','world_entity','world_entity_state','world_interactive_audit','mmo_update_interactive_state',130,TRUE,'Interactive state projection.'),
  ('character_script_int_set','script','character_script_state','character_progress_audit','mmo_set_character_script_int',140,TRUE,'Daedalus/global int projection.'),
  ('character_quest_updated','quest','character_quests','character_progress_audit','mmo_update_character_quest',150,TRUE,'Quest log projection.'),
  ('character_dialog_known_set','dialog','character_known_dialogs','character_progress_audit','mmo_set_character_known_dialog',160,TRUE,'Known/consumed dialog projection.'),
  ('character_progression_adjusted','character','character_stats','character_progress_audit','mmo_adjust_character_progression',170,TRUE,'XP/LP/stat progression delta.'),
  ('npc_marked_dead','combat','world_entity_state','world_npc_lifecycle_audit','mmo_mark_npc_dead',180,TRUE,'NPC death/lifecycle projection. Event class follows the emitted combat-class journal rows from migration 009.'),
  ('npc_respawned','combat','world_entity_state','world_npc_lifecycle_audit','mmo_respawn_npc',190,TRUE,'NPC respawn projection. Event class follows the emitted combat-class journal rows from migration 009.'),
  ('trade_buy_from_npc','trade','npc_trade_inventory+wallet+character_inventory','trade_economy_audit','mmo_trade_buy_from_npc',200,TRUE,'NPC stock to character with wallet debit.'),
  ('trade_sell_to_npc','trade','npc_trade_inventory+wallet+character_inventory','trade_economy_audit','mmo_trade_sell_to_npc',210,TRUE,'Character item to NPC stock with wallet credit.'),
  ('character_damage_applied','combat','character_stats','combat_resource_audit','mmo_apply_character_damage',220,TRUE,'Damage to character sheet.'),
  ('world_entity_damage_applied','combat','world_entity_state','combat_resource_audit','mmo_apply_world_entity_damage',230,TRUE,'Damage to NPC/world entity.'),
  ('character_mana_consumed','spell','character_stats','combat_resource_audit','mmo_consume_character_mana',240,TRUE,'Mana/resource spend.'),
  ('character_item_consumed','inventory','item_instances+character_inventory','combat_resource_audit','mmo_consume_character_item',250,TRUE,'Ammo/consumable quantity spend.'),
  ('item_stack_split','inventory','item_instances+character_inventory','item_stack_audit','mmo_split_character_item_stack',260,TRUE,'Explicit stack split.'),
  ('item_stack_merged','inventory','item_instances+character_inventory','item_stack_audit','mmo_merge_character_item_stack',270,TRUE,'Explicit stack merge.')
ON DUPLICATE KEY UPDATE
  event_class=VALUES(event_class), projection_name=VALUES(projection_name), audit_table=VALUES(audit_table), procedure_name=VALUES(procedure_name), replay_priority=VALUES(replay_priority), strict_replay=VALUES(strict_replay), notes=VALUES(notes);

DROP PROCEDURE IF EXISTS mmo_validate_event_replay_contract;
DELIMITER $$
CREATE PROCEDURE mmo_validate_event_replay_contract(
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
  VALUES(p_world_instance_id, p_run_key, 'replay', 'running', v_max_event_seq, JSON_MERGE_PATCH(COALESCE(p_metadata,JSON_OBJECT()), JSON_OBJECT('validator','event-replay-contract-017')))
  ON DUPLICATE KEY UPDATE status='running', started_at=CURRENT_TIMESTAMP(6), finished_at=NULL, max_event_seq=VALUES(max_event_seq), error_count=0, warning_count=0, metadata=VALUES(metadata);

  SELECT validation_run_id INTO v_run_id FROM mmo_projection_validation_runs WHERE world_instance_id=p_world_instance_id AND run_key=p_run_key LIMIT 1;
  DELETE FROM mmo_projection_validation_results WHERE validation_run_id=v_run_id;

  SELECT COUNT(*) INTO v_total FROM world_event_journal WHERE world_instance_id=p_world_instance_id AND source IN ('server','test');
  SELECT COUNT(*) INTO v_bad
    FROM world_event_journal e
    LEFT JOIN mmo_event_projection_contracts c ON c.event_type=e.event_type
   WHERE e.world_instance_id=p_world_instance_id AND e.source IN ('server','test') AND c.event_type IS NULL;
  INSERT INTO mmo_projection_validation_results(validation_run_id,check_name,severity,checked_count,problem_count,details)
  VALUES(v_run_id,'server_events_have_projection_contract',IF(v_bad=0,'ok','error'),v_total,v_bad,JSON_OBJECT('rule','every server/test event_type must be registered in mmo_event_projection_contracts'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_bad
    FROM world_event_journal e
    JOIN mmo_event_projection_contracts c ON c.event_type=e.event_type
   WHERE e.world_instance_id=p_world_instance_id AND e.source IN ('server','test') AND e.event_class<>c.event_class;
  INSERT INTO mmo_projection_validation_results(validation_run_id,check_name,severity,checked_count,problem_count,details)
  VALUES(v_run_id,'event_class_matches_contract',IF(v_bad=0,'ok','error'),v_total,v_bad,JSON_OBJECT('rule','world_event_journal.event_class must match contract event_class'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_bad
    FROM world_event_journal e
   WHERE e.world_instance_id=p_world_instance_id AND e.source IN ('server','test')
     AND (e.idempotency_key IS NULL OR TRIM(e.idempotency_key)='');
  INSERT INTO mmo_projection_validation_results(validation_run_id,check_name,severity,checked_count,problem_count,details)
  VALUES(v_run_id,'server_events_are_idempotent',IF(v_bad=0,'ok','warning'),v_total,v_bad,JSON_OBJECT('rule','server/test gameplay events should carry an idempotency_key'));
  SET v_warnings = v_warnings + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_bad
    FROM world_projection_offsets po
   WHERE po.world_instance_id=p_world_instance_id AND po.last_event_seq > v_max_event_seq;
  INSERT INTO mmo_projection_validation_results(validation_run_id,check_name,severity,checked_count,problem_count,details)
  VALUES(v_run_id,'projection_offsets_not_ahead_of_journal',IF(v_bad=0,'ok','error'),0,v_bad,JSON_OBJECT('rule','projection offset cannot be ahead of max world_event_journal.event_seq'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_total FROM (
    SELECT event_id FROM character_checkpoint_audit WHERE world_instance_id=p_world_instance_id
    UNION ALL SELECT event_id FROM character_wallet_audit WHERE world_instance_id=p_world_instance_id
    UNION ALL SELECT event_id FROM world_item_audit WHERE world_instance_id=p_world_instance_id
    UNION ALL SELECT event_id FROM character_inventory_audit WHERE world_instance_id=p_world_instance_id
    UNION ALL SELECT event_id FROM world_interactive_audit WHERE world_instance_id=p_world_instance_id
    UNION ALL SELECT event_id FROM character_progress_audit WHERE world_instance_id=p_world_instance_id
    UNION ALL SELECT event_id FROM world_npc_lifecycle_audit WHERE world_instance_id=p_world_instance_id
    UNION ALL SELECT event_id FROM trade_economy_audit WHERE world_instance_id=p_world_instance_id
    UNION ALL SELECT event_id FROM combat_resource_audit WHERE world_instance_id=p_world_instance_id
    UNION ALL SELECT event_id FROM item_stack_audit WHERE world_instance_id=p_world_instance_id
  ) x;

  SELECT COUNT(*) INTO v_bad FROM (
    SELECT event_id FROM character_checkpoint_audit WHERE world_instance_id=p_world_instance_id
    UNION ALL SELECT event_id FROM character_wallet_audit WHERE world_instance_id=p_world_instance_id
    UNION ALL SELECT event_id FROM world_item_audit WHERE world_instance_id=p_world_instance_id
    UNION ALL SELECT event_id FROM character_inventory_audit WHERE world_instance_id=p_world_instance_id
    UNION ALL SELECT event_id FROM world_interactive_audit WHERE world_instance_id=p_world_instance_id
    UNION ALL SELECT event_id FROM character_progress_audit WHERE world_instance_id=p_world_instance_id
    UNION ALL SELECT event_id FROM world_npc_lifecycle_audit WHERE world_instance_id=p_world_instance_id
    UNION ALL SELECT event_id FROM trade_economy_audit WHERE world_instance_id=p_world_instance_id
    UNION ALL SELECT event_id FROM combat_resource_audit WHERE world_instance_id=p_world_instance_id
    UNION ALL SELECT event_id FROM item_stack_audit WHERE world_instance_id=p_world_instance_id
  ) x LEFT JOIN world_event_journal e ON e.event_id=x.event_id
  WHERE e.event_id IS NULL;
  INSERT INTO mmo_projection_validation_results(validation_run_id,check_name,severity,checked_count,problem_count,details)
  VALUES(v_run_id,'audit_rows_reference_existing_events',IF(v_bad=0,'ok','error'),v_total,v_bad,JSON_OBJECT('rule','all write-path audit rows must reference an existing world_event_journal row'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  UPDATE mmo_projection_validation_runs
     SET status=IF(v_errors=0,'passed','failed'), finished_at=CURRENT_TIMESTAMP(6), error_count=v_errors, warning_count=v_warnings
   WHERE validation_run_id=v_run_id;

  SET p_validation_run_id=v_run_id;
  SET p_error_count=v_errors;
  SET p_warning_count=v_warnings;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_event_replay_contract_coverage AS
SELECT e.event_type,
       e.event_class AS observed_event_class,
       c.event_class AS contract_event_class,
       c.projection_name,
       c.audit_table,
       c.procedure_name,
       COUNT(*) AS event_count,
       SUM(CASE WHEN c.event_type IS NULL THEN 1 ELSE 0 END) AS missing_contract_count,
       MIN(e.event_seq) AS first_event_seq,
       MAX(e.event_seq) AS last_event_seq
FROM world_event_journal e
LEFT JOIN mmo_event_projection_contracts c ON c.event_type=e.event_type
GROUP BY e.event_type, e.event_class, c.event_class, c.projection_name, c.audit_table, c.procedure_name;

CREATE OR REPLACE VIEW v_event_replay_contract_gaps AS
SELECT * FROM v_event_replay_contract_coverage WHERE missing_contract_count > 0 OR observed_event_class <> contract_event_class;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES('production/mysql/017_event_replay_contract', 'gothic-mmo-event-replay-contract-v1-mysql', 'Event replay contract registry and validator for world_event_journal event_type coverage, class matching, idempotency and audit references. Fix: NPC lifecycle contract classes match migration 009 emitted combat event_class rows; wallet delta contract class matches migration 004 emitted inventory event_class rows.')
ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes), applied_at=CURRENT_TIMESTAMP(6);
