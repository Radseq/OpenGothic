-- Gothic MMO MySQL production migration 028.
-- Final database read models for admin/server loading and completion diagnostics.
-- Requires 001..027 MySQL production migrations.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

CREATE OR REPLACE VIEW v_mmo_character_load_sheet_final AS
SELECT BIN_TO_UUID(c.character_id,1) AS character_uuid,
       c.character_id,
       c.character_key,
       c.character_name,
       c.lifecycle_state,
       aa.account_name,
       rr.realm_key,
       BIN_TO_UUID(c.current_world_instance_id,1) AS current_world_instance_uuid,
       rwi.world_instance_key,
       cp.pos_x,
       cp.pos_y,
       cp.pos_z,
       cp.rotation_yaw,
       cp.current_waypoint_key,
       cp.server_tick AS position_server_tick,
       cs.level,
       cs.experience,
       cs.experience_next,
       cs.learning_points,
       cs.health_current,
       cs.health_max,
       cs.mana_current,
       cs.mana_max,
       cs.strength,
       cs.dexterity,
       cs.guild,
       cs.true_guild,
       cs.permanent_attitude,
       cs.temporary_attitude,
       COALESCE(inv.inventory_count,0) AS inventory_count,
       COALESCE(eq.equipment_count,0) AS equipment_count,
       COALESCE(wallet.wallet_count,0) AS wallet_count,
       c.last_login_at,
       c.last_logout_at,
       c.updated_at
FROM characters c
JOIN account_accounts aa ON aa.account_id=c.account_id
JOIN realm_realms rr ON rr.realm_id=c.realm_id
LEFT JOIN realm_world_instances rwi ON rwi.world_instance_id=c.current_world_instance_id
LEFT JOIN character_positions cp ON cp.character_id=c.character_id
LEFT JOIN character_stats cs ON cs.character_id=c.character_id
LEFT JOIN (SELECT character_id, COUNT(*) AS inventory_count FROM character_inventory GROUP BY character_id) inv ON inv.character_id=c.character_id
LEFT JOIN (SELECT character_id, COUNT(*) AS equipment_count FROM character_equipment GROUP BY character_id) eq ON eq.character_id=c.character_id
LEFT JOIN (SELECT character_id, COUNT(*) AS wallet_count FROM character_wallets GROUP BY character_id) wallet ON wallet.character_id=c.character_id;

CREATE OR REPLACE VIEW v_mmo_world_state_summary_final AS
SELECT BIN_TO_UUID(w.world_instance_id,1) AS world_instance_uuid,
       w.world_instance_id,
       rr.realm_key,
       w.world_instance_key,
       w.lifecycle_state,
       w.generation,
       w.current_tick,
       w.current_world_time_ms,
       COALESCE(entity.entity_count,0) AS entity_count,
       COALESCE(entity.active_entity_count,0) AS active_entity_count,
       COALESCE(entity.dead_entity_count,0) AS dead_entity_count,
       COALESCE(entity.removed_entity_count,0) AS removed_entity_count,
       COALESCE(world_inv.world_inventory_count,0) AS world_inventory_count,
       COALESCE(events.event_count,0) AS event_count,
       COALESCE(events.max_event_seq,0) AS max_event_seq,
       COALESCE(actions.pending_action_count,0) AS pending_action_count,
       COALESCE(actions.failed_action_count,0) AS failed_action_count,
       COALESCE(sessions.active_session_count,0) AS active_session_count,
       w.updated_at
FROM realm_world_instances w
JOIN realm_realms rr ON rr.realm_id=w.realm_id
LEFT JOIN (
  SELECT world_instance_id,
         COUNT(*) AS entity_count,
         SUM(lifecycle_state='active') AS active_entity_count,
         SUM(lifecycle_state='dead') AS dead_entity_count,
         SUM(lifecycle_state='removed') AS removed_entity_count
    FROM world_entity_state GROUP BY world_instance_id
) entity ON entity.world_instance_id=w.world_instance_id
LEFT JOIN (
  SELECT world_instance_id, COUNT(*) AS world_inventory_count FROM world_inventory GROUP BY world_instance_id
) world_inv ON world_inv.world_instance_id=w.world_instance_id
LEFT JOIN (
  SELECT world_instance_id, COUNT(*) AS event_count, MAX(event_seq) AS max_event_seq FROM world_event_journal GROUP BY world_instance_id
) events ON events.world_instance_id=w.world_instance_id
LEFT JOIN (
  SELECT world_instance_id,
         SUM(status IN ('pending','claimed')) AS pending_action_count,
         SUM(status IN ('failed','dead_letter')) AS failed_action_count
    FROM mmo_server_action_outbox GROUP BY world_instance_id
) actions ON actions.world_instance_id=w.world_instance_id
LEFT JOIN (
  SELECT world_instance_id, COUNT(*) AS active_session_count FROM server_sessions WHERE lifecycle_state='active' GROUP BY world_instance_id
) sessions ON sessions.world_instance_id=w.world_instance_id;

CREATE OR REPLACE VIEW v_mmo_database_final_dashboard AS
SELECT 'schema_migrations' AS area,
       COUNT(*) AS total_count,
       SUM(migration_key LIKE 'production/mysql/%') AS ok_count,
       0 AS problem_count,
       JSON_OBJECT('latest_applied_at', MAX(applied_at)) AS details
  FROM mmo_schema_versions
UNION ALL
SELECT 'dispatch_contracts', COUNT(*), SUM(enabled=TRUE), SUM(enabled=FALSE), JSON_OBJECT('source','mmo_server_action_dispatch_contracts') FROM mmo_server_action_dispatch_contracts
UNION ALL
SELECT 'event_replay_gaps', COUNT(*), 0, COUNT(*), JSON_OBJECT('source','v_event_replay_contract_gaps') FROM v_event_replay_contract_gaps
UNION ALL
SELECT 'outbox_failed_dead_letters', COUNT(*), 0, COUNT(*), JSON_OBJECT('source','mmo_server_action_outbox') FROM mmo_server_action_outbox WHERE status IN ('failed','dead_letter')
UNION ALL
SELECT 'restore_parity_required_scenarios', COUNT(*), SUM(active=TRUE AND required=TRUE), 0, JSON_OBJECT('source','mmo_restore_parity_scenarios') FROM mmo_restore_parity_scenarios
UNION ALL
SELECT 'db_restore_manifests', COUNT(*), SUM(manifest_status IN ('db_ready','blocked_external')), SUM(manifest_status='failed'), JSON_OBJECT('source','mmo_db_restore_manifests') FROM mmo_db_restore_manifests
UNION ALL
SELECT 'backup_manifests', COUNT(*), SUM(status IN ('recorded','verified')), SUM(status IN ('failed','expired')), JSON_OBJECT('source','mmo_database_backup_manifests') FROM mmo_database_backup_manifests;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES('production/mysql/028_final_read_models', 'gothic-mmo-final-read-models-v1-mysql', 'Final DB read models for character load sheet, world state summary and database completion dashboard.')
ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes), applied_at=CURRENT_TIMESTAMP(6);
