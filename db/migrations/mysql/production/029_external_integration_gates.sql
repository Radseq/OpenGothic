-- Gothic MMO MySQL production migration 029.
-- External integration gates: C++ hooks, production worker, parity runner and server-authority layer.
-- Requires 001..028 MySQL production migrations.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE IF NOT EXISTS mmo_external_integration_gates (
  gate_key          VARCHAR(191) PRIMARY KEY,
  gate_group        VARCHAR(64) NOT NULL,
  title             VARCHAR(255) NOT NULL,
  required_for_mmo  BOOLEAN NOT NULL DEFAULT TRUE,
  status            VARCHAR(32) NOT NULL DEFAULT 'blocked',
  sort_order        INT NOT NULL DEFAULT 1000,
  evidence          JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at        TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at        TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  KEY ix_mmo_external_integration_gates_group_status(gate_group, status, sort_order),
  CONSTRAINT mmo_external_integration_gates_status_ck CHECK(status IN ('passed','warning','blocked','not_started','not_required')),
  CONSTRAINT mmo_external_integration_gates_evidence_json_ck CHECK(JSON_VALID(evidence))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

INSERT INTO mmo_external_integration_gates(gate_key, gate_group, title, required_for_mmo, status, sort_order, evidence)
VALUES
  ('cpp_world_item_hooks', 'cxx_hooks', 'C++ hooks inserted for World::takeItem/removeItem/addItem/removeNpc', TRUE, 'not_started', 10, JSON_OBJECT('expected_files',JSON_ARRAY('game/world/world.cpp','game/world/worldobjects.cpp'))),
  ('cpp_inventory_equipment_hooks', 'cxx_hooks', 'C++ hooks inserted for Inventory::transfer/equip/unequip/use', TRUE, 'not_started', 20, JSON_OBJECT('expected_files',JSON_ARRAY('game/game/inventory.cpp'))),
  ('cpp_trade_hooks', 'cxx_hooks', 'C++ hooks inserted for Npc::buyItem/sellItem trade boundaries', TRUE, 'not_started', 30, JSON_OBJECT('expected_files',JSON_ARRAY('game/world/objects/npc.cpp'))),
  ('cpp_combat_spell_hooks', 'cxx_hooks', 'C++ hooks inserted for damage, death, mana, ammunition and spell commit', TRUE, 'not_started', 40, JSON_OBJECT('expected_files',JSON_ARRAY('game/world/objects/npc.cpp','game/world/bullet.cpp'))),
  ('cpp_interactive_hooks', 'cxx_hooks', 'C++ hooks inserted for Interactive committed door/container/lock state transitions', TRUE, 'not_started', 50, JSON_OBJECT('expected_files',JSON_ARRAY('game/world/objects/interactive.cpp'))),
  ('cpp_quest_dialog_script_hooks', 'cxx_hooks', 'C++ hooks inserted for GameScript/GameSession quest/dialog/script progress', TRUE, 'not_started', 60, JSON_OBJECT('expected_files',JSON_ARRAY('game/game/gamescript.cpp','game/game/gamesession.cpp'))),
  ('production_rpc_worker', 'server_worker', 'Production RPC/server worker replaces dev mysql-cli worker', TRUE, 'not_started', 70, JSON_OBJECT('current','tools/run_mysql_mmo_action_worker.py is development-only')),
  ('deterministic_replay_executor', 'replay', 'Deterministic replay executor rebuilds projections from content baseline + world_event_journal', TRUE, 'not_started', 80, JSON_OBJECT('current','strict pre-flight audit exists; clean rebuild executor still required')),
  ('restore_parity_runner', 'parity', 'Automated native .sav + SQLite save-slot + MySQL projection parity runner covers all required scenarios', TRUE, 'not_started', 90, JSON_OBJECT('current','artifact tables exist; real scenario automation still required')),
  ('server_authority_network_layer', 'network', 'Movement/combat authority, replication, interest management, reconnect and shard orchestration', TRUE, 'not_started', 100, JSON_OBJECT('current','outside database scope'))
ON DUPLICATE KEY UPDATE
  gate_group=VALUES(gate_group), title=VALUES(title), required_for_mmo=VALUES(required_for_mmo), sort_order=VALUES(sort_order), evidence=VALUES(evidence),
  status=IF(status='passed', status, VALUES(status)), updated_at=CURRENT_TIMESTAMP(6);

DROP PROCEDURE IF EXISTS mmo_set_external_integration_gate_status;
DELIMITER $$
CREATE PROCEDURE mmo_set_external_integration_gate_status(
  IN p_gate_key VARCHAR(191),
  IN p_status   VARCHAR(32),
  IN p_evidence JSON
)
BEGIN
  IF p_gate_key IS NULL OR TRIM(p_gate_key)='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='gate_key is required'; END IF;
  IF p_status NOT IN ('passed','warning','blocked','not_started','not_required') THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='invalid gate status'; END IF;

  UPDATE mmo_external_integration_gates
     SET status=p_status,
         evidence=JSON_MERGE_PATCH(evidence, COALESCE(p_evidence,JSON_OBJECT())),
         updated_at=CURRENT_TIMESTAMP(6)
   WHERE gate_key=p_gate_key;

  IF ROW_COUNT() = 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='unknown external integration gate';
  END IF;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_external_integration_gates AS
SELECT gate_key, gate_group, title, required_for_mmo, status, sort_order, evidence, updated_at
FROM mmo_external_integration_gates
ORDER BY sort_order, gate_key;

CREATE OR REPLACE VIEW v_external_integration_blockers AS
SELECT * FROM v_external_integration_gates
WHERE required_for_mmo=TRUE AND status NOT IN ('passed','not_required')
ORDER BY sort_order, gate_key;

CREATE OR REPLACE VIEW v_external_integration_summary AS
SELECT gate_group,
       COUNT(*) AS gate_count,
       SUM(status IN ('passed','not_required')) AS passed_count,
       SUM(required_for_mmo=TRUE AND status NOT IN ('passed','not_required')) AS blocker_count
FROM mmo_external_integration_gates
GROUP BY gate_group;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES('production/mysql/029_external_integration_gates', 'gothic-mmo-external-integration-gates-v1-mysql', 'External gate registry separating completed DB layer from required C++ hook, production worker, replay executor, parity runner and network/server-authority work.')
ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes), applied_at=CURRENT_TIMESTAMP(6);
