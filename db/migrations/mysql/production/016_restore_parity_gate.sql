-- Gothic MMO MySQL production migration 016.
-- Restore parity gate against native .sav / SQLite bridge / MySQL projection.
-- Requires 001..015 MySQL production migrations and a bootstrap import.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE IF NOT EXISTS mmo_restore_parity_scenarios (
  scenario_id       BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  scenario_key      VARCHAR(191) NOT NULL,
  scenario_group    VARCHAR(64) NOT NULL,
  title             VARCHAR(255) NOT NULL,
  required          BOOLEAN NOT NULL DEFAULT TRUE,
  active            BOOLEAN NOT NULL DEFAULT TRUE,
  sort_order        INT NOT NULL DEFAULT 1000,
  definition        JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at        TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at        TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  UNIQUE KEY mmo_restore_parity_scenarios_key_uk(scenario_key),
  KEY ix_mmo_restore_parity_scenarios_active(active, required, sort_order),
  CONSTRAINT mmo_restore_parity_scenarios_definition_json_ck CHECK(JSON_VALID(definition))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_restore_parity_runs (
  parity_run_id      BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  world_instance_id  BINARY(16) NOT NULL,
  character_id       BINARY(16) NULL,
  run_key            VARCHAR(191) NOT NULL,
  status             VARCHAR(32) NOT NULL DEFAULT 'running',
  started_at         TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  finished_at        TIMESTAMP(6) NULL,
  scenario_total     INT NOT NULL DEFAULT 0,
  passed_count       INT NOT NULL DEFAULT 0,
  failed_count       INT NOT NULL DEFAULT 0,
  warning_count      INT NOT NULL DEFAULT 0,
  blocked_count      INT NOT NULL DEFAULT 0,
  metadata           JSON NOT NULL DEFAULT (JSON_OBJECT()),
  UNIQUE KEY mmo_restore_parity_runs_key_uk(world_instance_id, run_key),
  KEY ix_mmo_restore_parity_runs_world_started(world_instance_id, started_at),
  CONSTRAINT mmo_restore_parity_runs_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT mmo_restore_parity_runs_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE SET NULL,
  CONSTRAINT mmo_restore_parity_runs_status_ck CHECK(status IN ('running','passed','failed','blocked')),
  CONSTRAINT mmo_restore_parity_runs_metadata_json_ck CHECK(JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_restore_parity_results (
  parity_result_id   BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  parity_run_id      BINARY(16) NOT NULL,
  scenario_key       VARCHAR(191) NOT NULL,
  status             VARCHAR(32) NOT NULL,
  native_hash        CHAR(64) NULL,
  sqlite_hash        CHAR(64) NULL,
  mysql_hash         CHAR(64) NULL,
  details            JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at         TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY mmo_restore_parity_results_run_scenario_uk(parity_run_id, scenario_key),
  KEY ix_mmo_restore_parity_results_status(status, created_at),
  CONSTRAINT mmo_restore_parity_results_run_fk FOREIGN KEY(parity_run_id) REFERENCES mmo_restore_parity_runs(parity_run_id) ON DELETE CASCADE,
  CONSTRAINT mmo_restore_parity_results_status_ck CHECK(status IN ('passed','failed','warning','blocked','not_run')),
  CONSTRAINT mmo_restore_parity_results_details_json_ck CHECK(JSON_VALID(details))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

INSERT INTO mmo_restore_parity_scenarios(scenario_key, scenario_group, title, required, sort_order, definition)
VALUES
  ('bookstand_script_xp', 'script', 'Read bookstand/bookshelf: one-shot script flag + XP reward', TRUE, 10, JSON_OBJECT('components', JSON_ARRAY('character_script_state','character_stats','world_event_journal'))),
  ('world_item_pickup', 'inventory', 'Pickup loose world item and restore inventory/world removal', TRUE, 20, JSON_OBJECT('components', JSON_ARRAY('item_instances','character_inventory','world_entity_state'))),
  ('equip_unequip', 'inventory', 'Equip and unequip item without losing inventory ownership', TRUE, 30, JSON_OBJECT('components', JSON_ARRAY('character_inventory','character_equipment','character_stats'))),
  ('container_change', 'interactive', 'Take/put item in container and restore interactive state', TRUE, 40, JSON_OBJECT('components', JSON_ARRAY('world_inventory','item_instances','world_entity_state'))),
  ('quest_progress', 'quest', 'Quest status and ordered text entries', TRUE, 50, JSON_OBJECT('components', JSON_ARRAY('character_quests'))),
  ('dialog_consumed', 'dialog', 'Known/consumed dialog info after execution', TRUE, 60, JSON_OBJECT('components', JSON_ARRAY('character_known_dialogs'))),
  ('npc_killed', 'npc', 'NPC death/lifecycle and health projection', TRUE, 70, JSON_OBJECT('components', JSON_ARRAY('world_entity_state','world_npc_lifecycle_audit'))),
  ('chapter_change', 'script', 'KAPITEL/story chapter progression', TRUE, 80, JSON_OBJECT('components', JSON_ARRAY('character_script_state','world_event_journal'))),
  ('save_restart_load', 'system', 'Save, restart process, load and compare native .sav to MySQL projection', TRUE, 90, JSON_OBJECT('components', JSON_ARRAY('native_sav','sqlite_slot_snapshot','mysql_projection')))
ON DUPLICATE KEY UPDATE
  scenario_group=VALUES(scenario_group), title=VALUES(title), required=VALUES(required), sort_order=VALUES(sort_order), definition=VALUES(definition), active=TRUE;

DROP PROCEDURE IF EXISTS mmo_start_restore_parity_run;
DELIMITER $$
CREATE PROCEDURE mmo_start_restore_parity_run(
  IN  p_world_instance_id  BINARY(16),
  IN  p_character_key      VARCHAR(191),
  IN  p_run_key            VARCHAR(191),
  IN  p_metadata           JSON,
  OUT p_parity_run_id      BINARY(16)
)
BEGIN
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_scenario_total INT DEFAULT 0;

  SET p_parity_run_id = NULL;
  IF p_world_instance_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='world_instance_id is required'; END IF;
  IF p_run_key IS NULL OR TRIM(p_run_key)='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='run_key is required'; END IF;

  IF p_character_key IS NOT NULL AND TRIM(p_character_key) <> '' THEN
    SELECT character_id INTO v_character_id FROM characters WHERE character_key=p_character_key LIMIT 1;
  END IF;

  SELECT COUNT(*) INTO v_scenario_total FROM mmo_restore_parity_scenarios WHERE active=TRUE AND required=TRUE;

  INSERT INTO mmo_restore_parity_runs(world_instance_id, character_id, run_key, status, scenario_total, metadata)
  VALUES(p_world_instance_id, v_character_id, p_run_key, 'running', v_scenario_total, COALESCE(p_metadata,JSON_OBJECT()))
  ON DUPLICATE KEY UPDATE
    character_id=VALUES(character_id), status='running', started_at=CURRENT_TIMESTAMP(6), finished_at=NULL,
    scenario_total=VALUES(scenario_total), passed_count=0, failed_count=0, warning_count=0, blocked_count=0, metadata=VALUES(metadata);

  SELECT parity_run_id INTO p_parity_run_id FROM mmo_restore_parity_runs WHERE world_instance_id=p_world_instance_id AND run_key=p_run_key LIMIT 1;
  DELETE FROM mmo_restore_parity_results WHERE parity_run_id=p_parity_run_id;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_record_restore_parity_result;
DELIMITER $$
CREATE PROCEDURE mmo_record_restore_parity_result(
  IN p_parity_run_id BINARY(16),
  IN p_scenario_key  VARCHAR(191),
  IN p_status        VARCHAR(32),
  IN p_native_hash   CHAR(64),
  IN p_sqlite_hash   CHAR(64),
  IN p_mysql_hash    CHAR(64),
  IN p_details       JSON
)
BEGIN
  IF p_parity_run_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='parity_run_id is required'; END IF;
  IF p_scenario_key IS NULL OR TRIM(p_scenario_key)='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='scenario_key is required'; END IF;
  IF p_status NOT IN ('passed','failed','warning','blocked','not_run') THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='invalid parity result status'; END IF;

  INSERT INTO mmo_restore_parity_results(parity_run_id, scenario_key, status, native_hash, sqlite_hash, mysql_hash, details)
  VALUES(p_parity_run_id, p_scenario_key, p_status, p_native_hash, p_sqlite_hash, p_mysql_hash, COALESCE(p_details,JSON_OBJECT()))
  ON DUPLICATE KEY UPDATE status=VALUES(status), native_hash=VALUES(native_hash), sqlite_hash=VALUES(sqlite_hash), mysql_hash=VALUES(mysql_hash), details=VALUES(details), created_at=CURRENT_TIMESTAMP(6);
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_finalize_restore_parity_run;
DELIMITER $$
CREATE PROCEDURE mmo_finalize_restore_parity_run(
  IN  p_parity_run_id BINARY(16),
  OUT p_status        VARCHAR(32),
  OUT p_failed_count  INT
)
BEGIN
  DECLARE v_required_total INT DEFAULT 0;
  DECLARE v_passed INT DEFAULT 0;
  DECLARE v_failed INT DEFAULT 0;
  DECLARE v_warning INT DEFAULT 0;
  DECLARE v_blocked INT DEFAULT 0;

  IF p_parity_run_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='parity_run_id is required'; END IF;

  SELECT COUNT(*) INTO v_required_total FROM mmo_restore_parity_scenarios WHERE active=TRUE AND required=TRUE;

  SELECT SUM(CASE WHEN r.status='passed' THEN 1 ELSE 0 END),
         SUM(CASE WHEN r.status='failed' THEN 1 ELSE 0 END),
         SUM(CASE WHEN r.status='warning' THEN 1 ELSE 0 END),
         SUM(CASE WHEN r.status='blocked' THEN 1 ELSE 0 END)
    INTO v_passed, v_failed, v_warning, v_blocked
    FROM mmo_restore_parity_scenarios s
    LEFT JOIN mmo_restore_parity_results r ON r.scenario_key=s.scenario_key AND r.parity_run_id=p_parity_run_id
   WHERE s.active=TRUE AND s.required=TRUE;

  SET v_passed = COALESCE(v_passed,0);
  SET v_failed = COALESCE(v_failed,0);
  SET v_warning = COALESCE(v_warning,0);
  SET v_blocked = COALESCE(v_blocked,0) + GREATEST(v_required_total - v_passed - v_failed - v_warning - COALESCE(v_blocked,0), 0);
  SET p_failed_count = v_failed;
  SET p_status = IF(v_failed > 0, 'failed', IF(v_blocked > 0, 'blocked', 'passed'));

  UPDATE mmo_restore_parity_runs
     SET status=p_status,
         finished_at=CURRENT_TIMESTAMP(6),
         scenario_total=v_required_total,
         passed_count=v_passed,
         failed_count=v_failed,
         warning_count=v_warning,
         blocked_count=v_blocked
   WHERE parity_run_id=p_parity_run_id;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_restore_parity_latest_runs AS
SELECT BIN_TO_UUID(r.parity_run_id,1) AS parity_run_uuid,
       BIN_TO_UUID(r.world_instance_id,1) AS world_instance_uuid,
       c.character_key,
       r.run_key,
       r.status,
       r.scenario_total,
       r.passed_count,
       r.failed_count,
       r.warning_count,
       r.blocked_count,
       r.started_at,
       r.finished_at
FROM mmo_restore_parity_runs r
LEFT JOIN characters c ON c.character_id=r.character_id
ORDER BY r.started_at DESC;

CREATE OR REPLACE VIEW v_restore_parity_failures AS
SELECT BIN_TO_UUID(run.parity_run_id,1) AS parity_run_uuid,
       run.run_key,
       res.scenario_key,
       s.title,
       res.status,
       res.native_hash,
       res.sqlite_hash,
       res.mysql_hash,
       res.details,
       res.created_at
FROM mmo_restore_parity_runs run
JOIN mmo_restore_parity_results res ON res.parity_run_id=run.parity_run_id
LEFT JOIN mmo_restore_parity_scenarios s ON s.scenario_key=res.scenario_key
WHERE res.status IN ('failed','blocked','warning')
ORDER BY run.started_at DESC, s.sort_order ASC;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES('production/mysql/016_restore_parity_gate', 'gothic-mmo-restore-parity-gate-v1-mysql', 'Restore parity scenario registry and run/result gate comparing native .sav, SQLite bridge and MySQL projection.')
ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes), applied_at=CURRENT_TIMESTAMP(6);
