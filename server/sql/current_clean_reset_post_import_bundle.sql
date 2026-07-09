-- Current clean MySQL reset post-import SQL bundle.
-- Generated from the listed step SQL files to keep the DB reset tool minimal.
-- Rebuild this bundle deliberately if any source migration surface changes.


-- ============================================================================
-- BEGIN server/sql/step51_authority_gap_procedures.sql
-- ============================================================================

-- Step51 MMO authority-gap procedures/projections.
-- MySQL 8.0+. Additive migration: closes Step49 missing routine names without
-- changing the OpenGothic client hot path. These routines append journal events
-- through mmo_append_world_event(...) and maintain compact current/history read
-- projections used by the dev server worker and restart/materialization probes.

CREATE TABLE IF NOT EXISTS mmo_world_trigger_events (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) NULL,
  realm_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  actor_character_id BINARY(16) NULL,
  trigger_key VARCHAR(255) NOT NULL,
  event_type_name VARCHAR(96) NOT NULL,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  event_payload JSON NOT NULL,
  idempotency_key VARCHAR(255) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(id),
  UNIQUE KEY ux_mmo_world_trigger_events_idem(idempotency_key),
  KEY ix_mmo_world_trigger_events_world_tick(world_instance_id, server_tick),
  KEY ix_mmo_world_trigger_events_trigger(world_instance_id, trigger_key)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_world_mover_state_current (
  world_instance_id BINARY(16) NOT NULL,
  mover_key VARCHAR(255) NOT NULL,
  state_after INT NOT NULL DEFAULT 0,
  state_after_name VARCHAR(96) NULL,
  frame_index INT NULL,
  target_frame_index INT NULL,
  last_event_id BINARY(16) NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(world_instance_id, mover_key),
  KEY ix_mmo_world_mover_state_tick(world_instance_id, last_server_tick)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_world_mover_state_history (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) NULL,
  world_instance_id BINARY(16) NOT NULL,
  mover_key VARCHAR(255) NOT NULL,
  state_before INT NULL,
  state_after INT NOT NULL,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  idempotency_key VARCHAR(255) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(id),
  UNIQUE KEY ux_mmo_world_mover_state_history_idem(idempotency_key),
  KEY ix_mmo_world_mover_state_history_key(world_instance_id, mover_key, server_tick)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_weapon_state_current (
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(255) NOT NULL,
  weapon_state VARCHAR(64) NOT NULL,
  ready TINYINT(1) NOT NULL DEFAULT 0,
  last_event_id BINARY(16) NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(world_instance_id, npc_entity_key),
  KEY ix_mmo_npc_weapon_state_tick(world_instance_id, last_server_tick)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_weapon_state_history (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) NULL,
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(255) NOT NULL,
  weapon_state VARCHAR(64) NOT NULL,
  ready TINYINT(1) NOT NULL DEFAULT 0,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  idempotency_key VARCHAR(255) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(id),
  UNIQUE KEY ux_mmo_npc_weapon_state_history_idem(idempotency_key),
  KEY ix_mmo_npc_weapon_state_history_key(world_instance_id, npc_entity_key, server_tick)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_world_clock_state_current (
  world_instance_id BINARY(16) NOT NULL PRIMARY KEY,
  world_day INT NULL,
  world_time_ms BIGINT NULL,
  last_event_id BINARY(16) NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  KEY ix_mmo_world_clock_tick(last_server_tick)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_world_clock_state_history (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) NULL,
  world_instance_id BINARY(16) NOT NULL,
  world_day_before INT NULL,
  world_day_after INT NULL,
  world_time_before_ms BIGINT NULL,
  world_time_after_ms BIGINT NULL,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  idempotency_key VARCHAR(255) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(id),
  UNIQUE KEY ux_mmo_world_clock_state_history_idem(idempotency_key),
  KEY ix_mmo_world_clock_state_history_world_tick(world_instance_id, server_tick)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_character_resource_state_current (
  character_id BINARY(16) NOT NULL,
  resource_key VARCHAR(64) NOT NULL,
  value_current INT NULL,
  last_delta INT NOT NULL DEFAULT 0,
  last_event_id BINARY(16) NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(character_id, resource_key),
  KEY ix_mmo_character_resource_tick(character_id, last_server_tick)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_character_resource_state_history (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) NULL,
  character_id BINARY(16) NOT NULL,
  resource_key VARCHAR(64) NOT NULL,
  delta_amount INT NOT NULL DEFAULT 0,
  value_before INT NULL,
  value_after INT NULL,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  idempotency_key VARCHAR(255) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(id),
  UNIQUE KEY ux_mmo_character_resource_state_history_idem(idempotency_key),
  KEY ix_mmo_character_resource_state_history_key(character_id, resource_key, server_tick)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_character_training_history (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) NULL,
  character_id BINARY(16) NOT NULL,
  stat_key VARCHAR(96) NOT NULL,
  lp_cost INT NOT NULL DEFAULT 0,
  gold_cost INT NOT NULL DEFAULT 0,
  value_before INT NULL,
  value_after INT NULL,
  learning_points_before INT NULL,
  learning_points_after INT NULL,
  trainer_key VARCHAR(255) NULL,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  event_payload JSON NOT NULL,
  idempotency_key VARCHAR(255) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(id),
  UNIQUE KEY ux_mmo_character_training_history_idem(idempotency_key),
  KEY ix_mmo_character_training_history_char_tick(character_id, server_tick)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_character_training_state_current (
  character_id BINARY(16) NOT NULL,
  stat_key VARCHAR(96) NOT NULL,
  value_current INT NULL,
  last_event_id BINARY(16) NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(character_id, stat_key),
  KEY ix_mmo_character_training_state_tick(character_id, last_server_tick)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_character_teleport_history (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) NULL,
  character_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  pos_x DOUBLE NOT NULL,
  pos_y DOUBLE NOT NULL,
  pos_z DOUBLE NOT NULL,
  rotation_yaw DOUBLE NOT NULL DEFAULT 0,
  reason VARCHAR(128) NOT NULL,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  event_payload JSON NOT NULL,
  idempotency_key VARCHAR(255) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(id),
  UNIQUE KEY ux_mmo_character_teleport_history_idem(idempotency_key),
  KEY ix_mmo_character_teleport_history_char_tick(character_id, server_tick)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_world_respawn_history (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) NULL,
  world_instance_id BINARY(16) NOT NULL,
  respawn_kind VARCHAR(32) NOT NULL,
  respawn_policy_key VARCHAR(255) NOT NULL,
  owner_entity_key VARCHAR(255) NULL,
  entity_key VARCHAR(255) NULL,
  item_instance_key VARCHAR(255) NULL,
  amount INT NOT NULL DEFAULT 1,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  event_payload JSON NOT NULL,
  idempotency_key VARCHAR(255) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(id),
  UNIQUE KEY ux_mmo_world_respawn_history_idem(idempotency_key),
  KEY ix_mmo_world_respawn_history_world_tick(world_instance_id, server_tick),
  KEY ix_mmo_world_respawn_history_policy(respawn_policy_key)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_reaction_history (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) NULL,
  world_instance_id BINARY(16) NOT NULL,
  actor_npc_key VARCHAR(255) NOT NULL,
  target_key VARCHAR(255) NULL,
  reaction_kind VARCHAR(64) NOT NULL,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  event_payload JSON NOT NULL,
  idempotency_key VARCHAR(255) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(id),
  UNIQUE KEY ux_mmo_npc_reaction_history_idem(idempotency_key),
  KEY ix_mmo_npc_reaction_history_world_tick(world_instance_id, server_tick),
  KEY ix_mmo_npc_reaction_history_actor(world_instance_id, actor_npc_key, server_tick)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

DELIMITER //

DROP PROCEDURE IF EXISTS mmo_record_trigger_event//
CREATE PROCEDURE mmo_record_trigger_event(
  IN p_session_id BINARY(16),
  IN p_trigger_key VARCHAR(255),
  IN p_event_type_name VARCHAR(96),
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(255),
  OUT p_event_id BINARY(16)
)
BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_payload JSON;
  SELECT realm_id, world_instance_id, character_id INTO v_realm_id, v_world_id, v_character_id
    FROM server_sessions WHERE session_id=p_session_id LIMIT 1;
  IF v_world_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_trigger_event: invalid session'; END IF;
  SET v_payload = JSON_MERGE_PATCH(COALESCE(p_metadata, JSON_OBJECT()), JSON_OBJECT('trigger_key',p_trigger_key,'event_type_name',p_event_type_name));
  CALL mmo_append_world_event(v_realm_id, v_world_id, v_character_id, 'world_trigger_event', 'world_entity', COALESCE(p_server_tick,0), p_trigger_key, p_event_type_name, v_payload, p_idempotency_key, 'server', NULL, NULL, p_event_id);
  INSERT INTO mmo_world_trigger_events(event_id, realm_id, world_instance_id, actor_character_id, trigger_key, event_type_name, server_tick, event_payload, idempotency_key)
  VALUES(p_event_id, v_realm_id, v_world_id, v_character_id, p_trigger_key, COALESCE(p_event_type_name,''), COALESCE(p_server_tick,0), v_payload, p_idempotency_key)
  ON DUPLICATE KEY UPDATE event_id=VALUES(event_id), event_payload=VALUES(event_payload);
END//

DROP PROCEDURE IF EXISTS mmo_record_mover_state//
CREATE PROCEDURE mmo_record_mover_state(
  IN p_session_id BINARY(16),
  IN p_mover_key VARCHAR(255),
  IN p_state_before INT,
  IN p_state_after INT,
  IN p_state_after_name VARCHAR(96),
  IN p_frame_index INT,
  IN p_target_frame_index INT,
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(255),
  OUT p_event_id BINARY(16),
  OUT p_row_version_after BIGINT UNSIGNED
)
BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_payload JSON;
  SELECT realm_id, world_instance_id, character_id INTO v_realm_id, v_world_id, v_character_id
    FROM server_sessions WHERE session_id=p_session_id LIMIT 1;
  IF v_world_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_mover_state: invalid session'; END IF;
  SET v_payload = JSON_MERGE_PATCH(COALESCE(p_metadata, JSON_OBJECT()), JSON_OBJECT('mover_key',p_mover_key,'state_before',p_state_before,'state_after',p_state_after,'state_after_name',p_state_after_name,'frame_index',p_frame_index,'target_frame_index',p_target_frame_index));
  CALL mmo_append_world_event(v_realm_id, v_world_id, v_character_id, 'world_mover_state_changed', 'world_entity', COALESCE(p_server_tick,0), p_mover_key, CAST(p_state_after AS CHAR), v_payload, p_idempotency_key, 'server', NULL, NULL, p_event_id);
  INSERT INTO mmo_world_mover_state_history(event_id, world_instance_id, mover_key, state_before, state_after, server_tick, state_payload, idempotency_key)
  VALUES(p_event_id, v_world_id, p_mover_key, p_state_before, p_state_after, COALESCE(p_server_tick,0), v_payload, p_idempotency_key)
  ON DUPLICATE KEY UPDATE event_id=VALUES(event_id), state_payload=VALUES(state_payload);
  INSERT INTO mmo_world_mover_state_current(world_instance_id, mover_key, state_after, state_after_name, frame_index, target_frame_index, last_event_id, last_server_tick, state_payload, row_version)
  VALUES(v_world_id, p_mover_key, p_state_after, p_state_after_name, p_frame_index, p_target_frame_index, p_event_id, COALESCE(p_server_tick,0), v_payload, 1)
  ON DUPLICATE KEY UPDATE state_after=VALUES(state_after), state_after_name=VALUES(state_after_name), frame_index=VALUES(frame_index), target_frame_index=VALUES(target_frame_index), last_event_id=VALUES(last_event_id), last_server_tick=VALUES(last_server_tick), state_payload=VALUES(state_payload), row_version=row_version+1;
  SELECT row_version INTO p_row_version_after FROM mmo_world_mover_state_current WHERE world_instance_id=v_world_id AND mover_key=p_mover_key;
END//

DROP PROCEDURE IF EXISTS mmo_record_npc_weapon_state//
CREATE PROCEDURE mmo_record_npc_weapon_state(
  IN p_session_id BINARY(16),
  IN p_npc_entity_key VARCHAR(255),
  IN p_weapon_state VARCHAR(64),
  IN p_ready BOOLEAN,
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(255),
  OUT p_event_id BINARY(16),
  OUT p_row_version_after BIGINT UNSIGNED
)
BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_payload JSON;
  DECLARE v_event_type VARCHAR(96);
  SELECT realm_id, world_instance_id, character_id INTO v_realm_id, v_world_id, v_character_id
    FROM server_sessions WHERE session_id=p_session_id LIMIT 1;
  IF v_world_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_npc_weapon_state: invalid session'; END IF;
  SET v_event_type = IF(COALESCE(p_ready,FALSE), 'npc_weapon_readied', 'npc_weapon_holstered');
  SET v_payload = JSON_MERGE_PATCH(COALESCE(p_metadata, JSON_OBJECT()), JSON_OBJECT('npc_entity_key',p_npc_entity_key,'weapon_state',p_weapon_state,'ready',COALESCE(p_ready,FALSE)));
  CALL mmo_append_world_event(v_realm_id, v_world_id, v_character_id, v_event_type, 'combat', COALESCE(p_server_tick,0), p_npc_entity_key, p_weapon_state, v_payload, p_idempotency_key, 'server', NULL, NULL, p_event_id);
  INSERT INTO mmo_npc_weapon_state_history(event_id, world_instance_id, npc_entity_key, weapon_state, ready, server_tick, state_payload, idempotency_key)
  VALUES(p_event_id, v_world_id, p_npc_entity_key, COALESCE(p_weapon_state,''), COALESCE(p_ready,FALSE), COALESCE(p_server_tick,0), v_payload, p_idempotency_key)
  ON DUPLICATE KEY UPDATE event_id=VALUES(event_id), state_payload=VALUES(state_payload);
  INSERT INTO mmo_npc_weapon_state_current(world_instance_id, npc_entity_key, weapon_state, ready, last_event_id, last_server_tick, state_payload, row_version)
  VALUES(v_world_id, p_npc_entity_key, COALESCE(p_weapon_state,''), COALESCE(p_ready,FALSE), p_event_id, COALESCE(p_server_tick,0), v_payload, 1)
  ON DUPLICATE KEY UPDATE weapon_state=VALUES(weapon_state), ready=VALUES(ready), last_event_id=VALUES(last_event_id), last_server_tick=VALUES(last_server_tick), state_payload=VALUES(state_payload), row_version=row_version+1;
  SELECT row_version INTO p_row_version_after FROM mmo_npc_weapon_state_current WHERE world_instance_id=v_world_id AND npc_entity_key=p_npc_entity_key;
END//

DROP PROCEDURE IF EXISTS mmo_record_world_time_changed//
CREATE PROCEDURE mmo_record_world_time_changed(
  IN p_session_id BINARY(16),
  IN p_world_time_before_ms BIGINT,
  IN p_world_time_after_ms BIGINT,
  IN p_world_day_before INT,
  IN p_world_day_after INT,
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(255),
  OUT p_event_id BINARY(16),
  OUT p_row_version_after BIGINT UNSIGNED
)
BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_payload JSON;
  SELECT realm_id, world_instance_id, character_id INTO v_realm_id, v_world_id, v_character_id
    FROM server_sessions WHERE session_id=p_session_id LIMIT 1;
  IF v_world_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_world_time_changed: invalid session'; END IF;
  SET v_payload = JSON_MERGE_PATCH(COALESCE(p_metadata, JSON_OBJECT()), JSON_OBJECT('world_time_before_ms',p_world_time_before_ms,'world_time_after_ms',p_world_time_after_ms,'world_day_before',p_world_day_before,'world_day_after',p_world_day_after));
  CALL mmo_append_world_event(v_realm_id, v_world_id, v_character_id, 'world_time_changed', 'system', COALESCE(p_server_tick,0), NULL, NULL, v_payload, p_idempotency_key, 'server', NULL, NULL, p_event_id);
  INSERT INTO mmo_world_clock_state_history(event_id, world_instance_id, world_day_before, world_day_after, world_time_before_ms, world_time_after_ms, server_tick, state_payload, idempotency_key)
  VALUES(p_event_id, v_world_id, p_world_day_before, p_world_day_after, p_world_time_before_ms, p_world_time_after_ms, COALESCE(p_server_tick,0), v_payload, p_idempotency_key)
  ON DUPLICATE KEY UPDATE event_id=VALUES(event_id), state_payload=VALUES(state_payload);
  INSERT INTO mmo_world_clock_state_current(world_instance_id, world_day, world_time_ms, last_event_id, last_server_tick, state_payload, row_version)
  VALUES(v_world_id, p_world_day_after, p_world_time_after_ms, p_event_id, COALESCE(p_server_tick,0), v_payload, 1)
  ON DUPLICATE KEY UPDATE world_day=VALUES(world_day), world_time_ms=VALUES(world_time_ms), last_event_id=VALUES(last_event_id), last_server_tick=VALUES(last_server_tick), state_payload=VALUES(state_payload), row_version=row_version+1;
  SELECT row_version INTO p_row_version_after FROM mmo_world_clock_state_current WHERE world_instance_id=v_world_id;
END//

DROP PROCEDURE IF EXISTS mmo_record_character_resource_delta//
CREATE PROCEDURE mmo_record_character_resource_delta(
  IN p_session_id BINARY(16),
  IN p_character_key VARCHAR(128),
  IN p_resource_key VARCHAR(64),
  IN p_delta_amount INT,
  IN p_value_before INT,
  IN p_value_after INT,
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(255),
  OUT p_event_id BINARY(16),
  OUT p_row_version_after BIGINT UNSIGNED
)
BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_payload JSON;
  SELECT ss.realm_id, ss.world_instance_id, COALESCE(c.character_id, ss.character_id) INTO v_realm_id, v_world_id, v_character_id
    FROM server_sessions ss
    LEFT JOIN characters c ON c.realm_id=ss.realm_id AND c.character_key=COALESCE(NULLIF(p_character_key,''),'PC_HERO')
   WHERE ss.session_id=p_session_id LIMIT 1;
  IF v_character_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_character_resource_delta: invalid character/session'; END IF;
  SET v_payload = JSON_MERGE_PATCH(COALESCE(p_metadata, JSON_OBJECT()), JSON_OBJECT('character_key',p_character_key,'resource_key',p_resource_key,'delta_amount',p_delta_amount,'value_before',p_value_before,'value_after',p_value_after));
  CALL mmo_append_world_event(v_realm_id, v_world_id, v_character_id, 'character_resource_delta', 'character', COALESCE(p_server_tick,0), COALESCE(p_character_key,'PC_HERO'), p_resource_key, v_payload, p_idempotency_key, 'server', NULL, NULL, p_event_id);
  INSERT INTO mmo_character_resource_state_history(event_id, character_id, resource_key, delta_amount, value_before, value_after, server_tick, state_payload, idempotency_key)
  VALUES(p_event_id, v_character_id, COALESCE(p_resource_key,''), COALESCE(p_delta_amount,0), p_value_before, p_value_after, COALESCE(p_server_tick,0), v_payload, p_idempotency_key)
  ON DUPLICATE KEY UPDATE event_id=VALUES(event_id), state_payload=VALUES(state_payload);
  INSERT INTO mmo_character_resource_state_current(character_id, resource_key, value_current, last_delta, last_event_id, last_server_tick, state_payload, row_version)
  VALUES(v_character_id, COALESCE(p_resource_key,''), p_value_after, COALESCE(p_delta_amount,0), p_event_id, COALESCE(p_server_tick,0), v_payload, 1)
  ON DUPLICATE KEY UPDATE value_current=VALUES(value_current), last_delta=VALUES(last_delta), last_event_id=VALUES(last_event_id), last_server_tick=VALUES(last_server_tick), state_payload=VALUES(state_payload), row_version=row_version+1;
  IF LOWER(COALESCE(p_resource_key,'')) IN ('health','hp') THEN
    UPDATE character_stats SET health_current=p_value_after, row_version=row_version+1 WHERE character_id=v_character_id;
  ELSEIF LOWER(COALESCE(p_resource_key,''))='mana' THEN
    UPDATE character_stats SET mana_current=p_value_after, row_version=row_version+1 WHERE character_id=v_character_id;
  END IF;
  SELECT row_version INTO p_row_version_after FROM mmo_character_resource_state_current WHERE character_id=v_character_id AND resource_key=COALESCE(p_resource_key,'');
END//

DROP PROCEDURE IF EXISTS mmo_spend_learning_points//
CREATE PROCEDURE mmo_spend_learning_points(
  IN p_session_id BINARY(16),
  IN p_stat_key VARCHAR(96),
  IN p_lp_cost INT,
  IN p_value_before INT,
  IN p_value_after INT,
  IN p_gold_cost INT,
  IN p_trainer_key VARCHAR(255),
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(255),
  OUT p_event_id BINARY(16),
  OUT p_learning_points_after INT
)
BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_lp_before INT DEFAULT 0;
  DECLARE v_payload JSON;
  SELECT realm_id, world_instance_id, character_id INTO v_realm_id, v_world_id, v_character_id
    FROM server_sessions WHERE session_id=p_session_id LIMIT 1;
  IF v_character_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_spend_learning_points: invalid session'; END IF;
  SELECT COALESCE(learning_points,0) INTO v_lp_before FROM character_stats WHERE character_id=v_character_id FOR UPDATE;
  IF v_lp_before < COALESCE(p_lp_cost,0) THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_spend_learning_points: insufficient learning points'; END IF;
  SET p_learning_points_after = v_lp_before - COALESCE(p_lp_cost,0);
  SET v_payload = JSON_MERGE_PATCH(COALESCE(p_metadata, JSON_OBJECT()), JSON_OBJECT('stat_key',p_stat_key,'lp_cost',p_lp_cost,'gold_cost',p_gold_cost,'value_before',p_value_before,'value_after',p_value_after,'trainer_key',p_trainer_key,'learning_points_before',v_lp_before,'learning_points_after',p_learning_points_after));
  CALL mmo_append_world_event(v_realm_id, v_world_id, v_character_id, 'character_learning_points_spent', 'character', COALESCE(p_server_tick,0), p_stat_key, p_trainer_key, v_payload, p_idempotency_key, 'server', NULL, NULL, p_event_id);
  UPDATE character_stats
     SET learning_points=p_learning_points_after,
         strength=CASE WHEN LOWER(COALESCE(p_stat_key,'')) IN ('strength','atr_strength','attribute_strength') THEN COALESCE(p_value_after,strength) ELSE strength END,
         dexterity=CASE WHEN LOWER(COALESCE(p_stat_key,'')) IN ('dexterity','atr_dexterity','attribute_dexterity') THEN COALESCE(p_value_after,dexterity) ELSE dexterity END,
         row_version=row_version+1
   WHERE character_id=v_character_id;
  INSERT INTO mmo_character_training_history(event_id, character_id, stat_key, lp_cost, gold_cost, value_before, value_after, learning_points_before, learning_points_after, trainer_key, server_tick, event_payload, idempotency_key)
  VALUES(p_event_id, v_character_id, COALESCE(p_stat_key,''), COALESCE(p_lp_cost,0), COALESCE(p_gold_cost,0), p_value_before, p_value_after, v_lp_before, p_learning_points_after, p_trainer_key, COALESCE(p_server_tick,0), v_payload, p_idempotency_key)
  ON DUPLICATE KEY UPDATE event_id=VALUES(event_id), event_payload=VALUES(event_payload);
  INSERT INTO mmo_character_training_state_current(character_id, stat_key, value_current, last_event_id, last_server_tick, state_payload, row_version)
  VALUES(v_character_id, COALESCE(p_stat_key,''), p_value_after, p_event_id, COALESCE(p_server_tick,0), v_payload, 1)
  ON DUPLICATE KEY UPDATE value_current=VALUES(value_current), last_event_id=VALUES(last_event_id), last_server_tick=VALUES(last_server_tick), state_payload=VALUES(state_payload), row_version=row_version+1;
END//

DROP PROCEDURE IF EXISTS mmo_change_world_or_teleport_character//
CREATE PROCEDURE mmo_change_world_or_teleport_character(
  IN p_session_id BINARY(16),
  IN p_target_world_instance_key VARCHAR(255),
  IN p_pos_x DOUBLE,
  IN p_pos_y DOUBLE,
  IN p_pos_z DOUBLE,
  IN p_rotation_yaw DOUBLE,
  IN p_reason VARCHAR(128),
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(255),
  OUT p_event_id BINARY(16)
)
BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_id BINARY(16);
  DECLARE v_target_world_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_payload JSON;
  SELECT realm_id, world_instance_id, character_id INTO v_realm_id, v_world_id, v_character_id
    FROM server_sessions WHERE session_id=p_session_id LIMIT 1;
  IF v_character_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_change_world_or_teleport_character: invalid session'; END IF;
  SET v_target_world_id = v_world_id;
  IF p_target_world_instance_key IS NOT NULL AND p_target_world_instance_key <> '' THEN
    SELECT world_instance_id INTO v_target_world_id FROM realm_world_instances WHERE world_instance_key=p_target_world_instance_key AND realm_id=v_realm_id LIMIT 1;
  END IF;
  IF v_target_world_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_change_world_or_teleport_character: target world not found'; END IF;
  SET v_payload = JSON_MERGE_PATCH(COALESCE(p_metadata, JSON_OBJECT()), JSON_OBJECT('target_world_instance_key',p_target_world_instance_key,'pos_x',p_pos_x,'pos_y',p_pos_y,'pos_z',p_pos_z,'rotation_yaw',p_rotation_yaw,'reason',p_reason));
  CALL mmo_append_world_event(v_realm_id, v_target_world_id, v_character_id, 'character_teleported_or_world_changed', 'character', COALESCE(p_server_tick,0), NULL, p_reason, v_payload, p_idempotency_key, 'server', NULL, NULL, p_event_id);
  UPDATE characters SET current_world_instance_id=v_target_world_id, updated_at=CURRENT_TIMESTAMP(6) WHERE character_id=v_character_id;
  INSERT INTO character_positions(character_id, world_instance_id, pos_x, pos_y, pos_z, rotation_yaw, current_waypoint_key, server_tick, row_version)
  VALUES(v_character_id, v_target_world_id, p_pos_x, p_pos_y, p_pos_z, COALESCE(p_rotation_yaw,0), NULL, COALESCE(p_server_tick,0), 1)
  ON DUPLICATE KEY UPDATE world_instance_id=VALUES(world_instance_id), pos_x=VALUES(pos_x), pos_y=VALUES(pos_y), pos_z=VALUES(pos_z), rotation_yaw=VALUES(rotation_yaw), current_waypoint_key=NULL, server_tick=VALUES(server_tick), row_version=row_version+1;
  INSERT INTO mmo_character_teleport_history(event_id, character_id, world_instance_id, pos_x, pos_y, pos_z, rotation_yaw, reason, server_tick, event_payload, idempotency_key)
  VALUES(p_event_id, v_character_id, v_target_world_id, p_pos_x, p_pos_y, p_pos_z, COALESCE(p_rotation_yaw,0), COALESCE(p_reason,'teleport'), COALESCE(p_server_tick,0), v_payload, p_idempotency_key)
  ON DUPLICATE KEY UPDATE event_id=VALUES(event_id), event_payload=VALUES(event_payload);
END//

DROP PROCEDURE IF EXISTS mmo_respawn_world_item//
CREATE PROCEDURE mmo_respawn_world_item(
  IN p_session_id BINARY(16),
  IN p_respawn_policy_key VARCHAR(255),
  IN p_world_item_entity_key VARCHAR(255),
  IN p_amount INT,
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(255),
  OUT p_event_id BINARY(16)
)
BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_payload JSON;
  SELECT realm_id, world_instance_id, character_id INTO v_realm_id, v_world_id, v_character_id FROM server_sessions WHERE session_id=p_session_id LIMIT 1;
  IF v_world_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_respawn_world_item: invalid session'; END IF;
  SET v_payload = JSON_MERGE_PATCH(COALESCE(p_metadata, JSON_OBJECT()), JSON_OBJECT('respawn_policy_key',p_respawn_policy_key,'world_item_entity_key',p_world_item_entity_key,'amount',p_amount));
  CALL mmo_append_world_event(v_realm_id, v_world_id, v_character_id, 'world_item_respawned', 'world_entity', COALESCE(p_server_tick,0), p_world_item_entity_key, p_respawn_policy_key, v_payload, p_idempotency_key, 'server', NULL, NULL, p_event_id);
  UPDATE world_entity_state SET lifecycle_state='active', state_json=JSON_MERGE_PATCH(COALESCE(state_json,JSON_OBJECT()), JSON_OBJECT('respawn_policy_key',p_respawn_policy_key,'respawned_amount',p_amount)), row_version=row_version+1, updated_at=CURRENT_TIMESTAMP(6)
   WHERE world_instance_id=v_world_id AND entity_key=p_world_item_entity_key;
  INSERT INTO mmo_world_respawn_history(event_id, world_instance_id, respawn_kind, respawn_policy_key, entity_key, amount, server_tick, event_payload, idempotency_key)
  VALUES(p_event_id, v_world_id, 'world_item', COALESCE(p_respawn_policy_key,''), p_world_item_entity_key, COALESCE(p_amount,1), COALESCE(p_server_tick,0), v_payload, p_idempotency_key)
  ON DUPLICATE KEY UPDATE event_id=VALUES(event_id), event_payload=VALUES(event_payload);
END//

DROP PROCEDURE IF EXISTS mmo_respawn_container_item//
CREATE PROCEDURE mmo_respawn_container_item(
  IN p_session_id BINARY(16),
  IN p_respawn_policy_key VARCHAR(255),
  IN p_owner_entity_key VARCHAR(255),
  IN p_item_instance_key VARCHAR(255),
  IN p_amount INT,
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(255),
  OUT p_event_id BINARY(16)
)
BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_payload JSON;
  SELECT realm_id, world_instance_id, character_id INTO v_realm_id, v_world_id, v_character_id FROM server_sessions WHERE session_id=p_session_id LIMIT 1;
  IF v_world_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_respawn_container_item: invalid session'; END IF;
  SET v_payload = JSON_MERGE_PATCH(COALESCE(p_metadata, JSON_OBJECT()), JSON_OBJECT('respawn_policy_key',p_respawn_policy_key,'owner_entity_key',p_owner_entity_key,'item_instance_key',p_item_instance_key,'amount',p_amount));
  CALL mmo_append_world_event(v_realm_id, v_world_id, v_character_id, 'container_item_respawned', 'inventory', COALESCE(p_server_tick,0), p_owner_entity_key, p_item_instance_key, v_payload, p_idempotency_key, 'server', NULL, NULL, p_event_id);
  INSERT INTO mmo_world_respawn_history(event_id, world_instance_id, respawn_kind, respawn_policy_key, owner_entity_key, item_instance_key, amount, server_tick, event_payload, idempotency_key)
  VALUES(p_event_id, v_world_id, 'container_item', COALESCE(p_respawn_policy_key,''), p_owner_entity_key, p_item_instance_key, COALESCE(p_amount,1), COALESCE(p_server_tick,0), v_payload, p_idempotency_key)
  ON DUPLICATE KEY UPDATE event_id=VALUES(event_id), event_payload=VALUES(event_payload);
END//

DROP PROCEDURE IF EXISTS mmo_record_npc_reaction_started//
CREATE PROCEDURE mmo_record_npc_reaction_started(
  IN p_session_id BINARY(16),
  IN p_actor_npc_key VARCHAR(255),
  IN p_target_key VARCHAR(255),
  IN p_reaction_kind VARCHAR(64),
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(255),
  OUT p_event_id BINARY(16)
)
BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_payload JSON;
  SELECT realm_id, world_instance_id, character_id INTO v_realm_id, v_world_id, v_character_id FROM server_sessions WHERE session_id=p_session_id LIMIT 1;
  IF v_world_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_npc_reaction_started: invalid session'; END IF;
  SET v_payload = JSON_MERGE_PATCH(COALESCE(p_metadata, JSON_OBJECT()), JSON_OBJECT('actor_npc_key',p_actor_npc_key,'target_key',p_target_key,'reaction_kind',p_reaction_kind));
  CALL mmo_append_world_event(v_realm_id, v_world_id, v_character_id, 'npc_reaction_started', 'world_entity', COALESCE(p_server_tick,0), p_actor_npc_key, p_target_key, v_payload, p_idempotency_key, 'server', NULL, NULL, p_event_id);
  INSERT INTO mmo_npc_reaction_history(event_id, world_instance_id, actor_npc_key, target_key, reaction_kind, server_tick, event_payload, idempotency_key)
  VALUES(p_event_id, v_world_id, p_actor_npc_key, p_target_key, COALESCE(p_reaction_kind,''), COALESCE(p_server_tick,0), v_payload, p_idempotency_key)
  ON DUPLICATE KEY UPDATE event_id=VALUES(event_id), event_payload=VALUES(event_payload);
END//

DROP PROCEDURE IF EXISTS mmo_record_npc_dialog_initiated//
CREATE PROCEDURE mmo_record_npc_dialog_initiated(
  IN p_session_id BINARY(16),
  IN p_actor_npc_key VARCHAR(255),
  IN p_target_character_key VARCHAR(128),
  IN p_dialog_info_key VARCHAR(255),
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(255),
  OUT p_event_id BINARY(16)
)
BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_payload JSON;
  SELECT realm_id, world_instance_id, character_id INTO v_realm_id, v_world_id, v_character_id FROM server_sessions WHERE session_id=p_session_id LIMIT 1;
  IF v_world_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_npc_dialog_initiated: invalid session'; END IF;
  SET v_payload = JSON_MERGE_PATCH(COALESCE(p_metadata, JSON_OBJECT()), JSON_OBJECT('actor_npc_key',p_actor_npc_key,'target_character_key',p_target_character_key,'dialog_info_key',p_dialog_info_key));
  CALL mmo_append_world_event(v_realm_id, v_world_id, v_character_id, 'npc_dialog_initiated', 'dialog', COALESCE(p_server_tick,0), p_actor_npc_key, p_dialog_info_key, v_payload, p_idempotency_key, 'server', NULL, NULL, p_event_id);
  INSERT INTO mmo_npc_reaction_history(event_id, world_instance_id, actor_npc_key, target_key, reaction_kind, server_tick, event_payload, idempotency_key)
  VALUES(p_event_id, v_world_id, p_actor_npc_key, COALESCE(p_target_character_key,'PC_HERO'), 'dialog', COALESCE(p_server_tick,0), v_payload, p_idempotency_key)
  ON DUPLICATE KEY UPDATE event_id=VALUES(event_id), event_payload=VALUES(event_payload);
END//

DELIMITER ;

-- ============================================================================
-- END server/sql/step51_authority_gap_procedures.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step53_server_read_model_v1.sql
-- ============================================================================

-- Step53: physical typed read-model tables for MMO server materialization.
-- This patch intentionally creates no SQL views and no JSON columns.
-- It is additive: old bridge tables/procedures stay intact until the real server replaces them.

CREATE TABLE IF NOT EXISTS mmo_server_read_model_meta (
  model_key             VARCHAR(96)  NOT NULL,
  model_version         INT          NOT NULL,
  source_database       VARCHAR(128) NOT NULL,
  rebuilt_at            TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  source_event_count    BIGINT       NOT NULL DEFAULT 0,
  source_max_event_id   BIGINT       NULL,
  notes                 VARCHAR(512) NULL,
  PRIMARY KEY (model_key)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_character_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  character_key         VARCHAR(128) NOT NULL,
  account_key           VARCHAR(128) NULL,
  display_name          VARCHAR(255) NULL,
  world_name            VARCHAR(128) NOT NULL DEFAULT 'UNKNOWN',
  pos_x                 DOUBLE NULL,
  pos_y                 DOUBLE NULL,
  pos_z                 DOUBLE NULL,
  angle_y               DOUBLE NULL,
  health_current        INT NULL,
  health_max            INT NULL,
  mana_current          INT NULL,
  mana_max              INT NULL,
  level_value           INT NULL,
  experience_value      BIGINT NULL,
  experience_next       BIGINT NULL,
  learning_points       INT NULL,
  gold_amount           BIGINT NULL,
  lifecycle_state       VARCHAR(32) NOT NULL DEFAULT 'active',
  source_updated_at     TIMESTAMP(6) NULL,
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, character_key),
  KEY idx_mmo_srv_char_world (world_name),
  KEY idx_mmo_srv_char_account (account_key),
  KEY idx_mmo_srv_char_lifecycle (lifecycle_state)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_character_inventory_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  character_key         VARCHAR(128) NOT NULL,
  item_instance_key     VARCHAR(128) NOT NULL,
  item_template_key     VARCHAR(128) NULL,
  display_name          VARCHAR(255) NULL,
  amount                BIGINT NOT NULL DEFAULT 1,
  equipped              TINYINT(1) NOT NULL DEFAULT 0,
  slot_key              VARCHAR(64) NULL,
  bind_state            VARCHAR(32) NULL,
  lifecycle_state       VARCHAR(32) NOT NULL DEFAULT 'active',
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, character_key, item_instance_key),
  KEY idx_mmo_srv_char_inv_item (item_template_key),
  KEY idx_mmo_srv_char_inv_equipped (character_key, equipped, slot_key),
  KEY idx_mmo_srv_char_inv_lifecycle (lifecycle_state)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_character_quest_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  character_key         VARCHAR(128) NOT NULL,
  quest_key             VARCHAR(191) NOT NULL,
  quest_name            VARCHAR(255) NULL,
  status_key            VARCHAR(64) NOT NULL DEFAULT 'unknown',
  entry_count           INT NOT NULL DEFAULT 0,
  updated_at            TIMESTAMP(6) NULL,
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, character_key, quest_key),
  KEY idx_mmo_srv_char_quest_status (character_key, status_key),
  KEY idx_mmo_srv_char_quest_name (quest_name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_known_dialog_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  character_key         VARCHAR(128) NOT NULL,
  npc_symbol_name       VARCHAR(191) NOT NULL,
  info_symbol_name      VARCHAR(191) NOT NULL,
  availability_state    VARCHAR(64) NULL,
  first_seen_tick       BIGINT NULL,
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, character_key, npc_symbol_name, info_symbol_name),
  KEY idx_mmo_srv_known_dialog_info (info_symbol_name),
  KEY idx_mmo_srv_known_dialog_npc (npc_symbol_name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_world_entity_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  world_name            VARCHAR(128) NOT NULL DEFAULT 'UNKNOWN',
  entity_key            VARCHAR(191) NOT NULL,
  entity_kind           VARCHAR(64) NOT NULL DEFAULT 'unknown',
  template_key          VARCHAR(191) NULL,
  script_symbol_name    VARCHAR(191) NULL,
  display_name          VARCHAR(255) NULL,
  active                TINYINT(1) NOT NULL DEFAULT 1,
  dead                  TINYINT(1) NOT NULL DEFAULT 0,
  health_current        INT NULL,
  health_max            INT NULL,
  pos_x                 DOUBLE NULL,
  pos_y                 DOUBLE NULL,
  pos_z                 DOUBLE NULL,
  angle_y               DOUBLE NULL,
  current_waypoint_key  VARCHAR(191) NULL,
  current_waypoint_name VARCHAR(191) NULL,
  lifecycle_state       VARCHAR(32) NOT NULL DEFAULT 'active',
  source_updated_at     TIMESTAMP(6) NULL,
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, world_name, entity_key),
  KEY idx_mmo_srv_entity_kind (world_name, entity_kind, active, dead),
  KEY idx_mmo_srv_entity_template (template_key),
  KEY idx_mmo_srv_entity_symbol (script_symbol_name),
  KEY idx_mmo_srv_entity_waypoint (world_name, current_waypoint_key),
  KEY idx_mmo_srv_entity_lifecycle (lifecycle_state)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_world_inventory_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  world_name            VARCHAR(128) NOT NULL DEFAULT 'UNKNOWN',
  owner_key             VARCHAR(191) NOT NULL,
  owner_kind            VARCHAR(64) NOT NULL DEFAULT 'world',
  item_instance_key     VARCHAR(128) NOT NULL,
  item_template_key     VARCHAR(128) NULL,
  display_name          VARCHAR(255) NULL,
  amount                BIGINT NOT NULL DEFAULT 1,
  pos_x                 DOUBLE NULL,
  pos_y                 DOUBLE NULL,
  pos_z                 DOUBLE NULL,
  lifecycle_state       VARCHAR(32) NOT NULL DEFAULT 'active',
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, world_name, owner_key, item_instance_key),
  KEY idx_mmo_srv_world_inv_item (item_template_key),
  KEY idx_mmo_srv_world_inv_owner (world_name, owner_kind, owner_key),
  KEY idx_mmo_srv_world_inv_lifecycle (lifecycle_state)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_interactive_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  world_name            VARCHAR(128) NOT NULL DEFAULT 'UNKNOWN',
  interactive_key       VARCHAR(191) NOT NULL,
  display_name          VARCHAR(255) NULL,
  focus_name            VARCHAR(191) NULL,
  state_value           INT NULL,
  locked                TINYINT(1) NOT NULL DEFAULT 0,
  opened                TINYINT(1) NOT NULL DEFAULT 0,
  container             TINYINT(1) NOT NULL DEFAULT 0,
  door                  TINYINT(1) NOT NULL DEFAULT 0,
  active                TINYINT(1) NOT NULL DEFAULT 1,
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, world_name, interactive_key),
  KEY idx_mmo_srv_interactive_flags (world_name, container, door, locked, opened)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_script_int_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  scope_key             VARCHAR(64)  NOT NULL,
  owner_key             VARCHAR(191) NOT NULL DEFAULT '',
  symbol_name           VARCHAR(191) NOT NULL,
  int_value             BIGINT NULL,
  category_key          VARCHAR(64) NULL,
  updated_at            TIMESTAMP(6) NULL,
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, scope_key, owner_key, symbol_name),
  KEY idx_mmo_srv_script_symbol (symbol_name),
  KEY idx_mmo_srv_script_category (category_key)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_world_clock_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  world_name            VARCHAR(128) NOT NULL DEFAULT 'UNKNOWN',
  day_value             INT NULL,
  hour_value            INT NULL,
  minute_value          INT NULL,
  absolute_minute       BIGINT NULL,
  source_reason         VARCHAR(128) NULL,
  updated_at            TIMESTAMP(6) NULL,
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, world_name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_waypoint_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  world_name            VARCHAR(128) NOT NULL DEFAULT 'UNKNOWN',
  waypoint_key          VARCHAR(191) NOT NULL,
  waypoint_name         VARCHAR(191) NULL,
  kind_key              VARCHAR(64) NULL,
  pos_x                 DOUBLE NULL,
  pos_y                 DOUBLE NULL,
  pos_z                 DOUBLE NULL,
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, world_name, waypoint_key),
  KEY idx_mmo_srv_waypoint_name (world_name, waypoint_name),
  KEY idx_mmo_srv_waypoint_kind (world_name, kind_key)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS mmo_server_waypoint_edge_read_model (
  realm_key             VARCHAR(128) NOT NULL DEFAULT 'default',
  world_name            VARCHAR(128) NOT NULL DEFAULT 'UNKNOWN',
  edge_key              VARCHAR(191) NOT NULL,
  from_waypoint_key     VARCHAR(191) NOT NULL,
  to_waypoint_key       VARCHAR(191) NOT NULL,
  distance_value        DOUBLE NULL,
  materialized_at       TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (realm_key, world_name, edge_key),
  KEY idx_mmo_srv_way_edge_from (world_name, from_waypoint_key),
  KEY idx_mmo_srv_way_edge_to (world_name, to_waypoint_key)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================================
-- END server/sql/step53_server_read_model_v1.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step55_live_receiver_bridge.sql
-- ============================================================================

-- Step55e: minimal live receiver bridge for clean MySQL DBs rebuilt from the
-- pre-Xardas baseline. This is additive/idempotent and only installs the dev
-- session/outbox/worker surface required by run_mmo_action_receiver.py and
-- run_mmo_resolved_action_worker.py. It does not make SQLite a live database.

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES (
  'server/sql/step55_live_receiver_bridge',
  'gothic-mmo-step55-live-receiver-bridge-v2-mysql',
  'Minimal server_sessions + action outbox + worker routines required for Step55 client_bootstrap_request against clean MySQL imports. Step55e aligns journal event_class values with the core world_event_journal constraint.'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);

CREATE TABLE IF NOT EXISTS server_sessions (
  session_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(),1)),
  realm_id BINARY(16) NOT NULL,
  account_id BINARY(16) NOT NULL,
  character_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  session_key VARCHAR(191) NOT NULL,
  client_name VARCHAR(128) NOT NULL,
  remote_addr VARCHAR(191) NOT NULL,
  lifecycle_state VARCHAR(32) NOT NULL DEFAULT 'active',
  login_event_id BINARY(16) NULL,
  logout_event_id BINARY(16) NULL,
  client_metadata JSON NOT NULL DEFAULT (JSON_OBJECT()),
  started_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  last_seen_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  ended_at TIMESTAMP(6) NULL,
  PRIMARY KEY(session_id),
  UNIQUE KEY uq_server_sessions_session_key(session_key),
  KEY idx_server_sessions_character_state(character_id, lifecycle_state),
  KEY idx_server_sessions_world_state(world_instance_id, lifecycle_state),
  KEY idx_server_sessions_realm_state(realm_id, lifecycle_state)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS character_checkpoint_audit (
  checkpoint_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(),1)),
  session_id BINARY(16) NOT NULL,
  character_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  event_id BINARY(16) NULL,
  server_tick BIGINT NOT NULL DEFAULT 0,
  pos_x DOUBLE NOT NULL,
  pos_y DOUBLE NOT NULL,
  pos_z DOUBLE NOT NULL,
  rotation_yaw DOUBLE NOT NULL DEFAULT 0,
  current_waypoint_key VARCHAR(191) NULL,
  level_value INT NULL,
  experience_value BIGINT NULL,
  experience_next BIGINT NULL,
  learning_points INT NULL,
  health_current INT NULL,
  health_max INT NULL,
  mana_current INT NULL,
  mana_max INT NULL,
  strength_value INT NULL,
  dexterity_value INT NULL,
  guild_value INT NULL,
  true_guild_value INT NULL,
  permanent_attitude INT NULL,
  temporary_attitude INT NULL,
  metadata JSON NOT NULL DEFAULT (JSON_OBJECT()),
  idempotency_key VARCHAR(191) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(checkpoint_id),
  UNIQUE KEY uq_character_checkpoint_idempotency(idempotency_key),
  KEY idx_character_checkpoint_character_tick(character_id, server_tick),
  KEY idx_character_checkpoint_session(session_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_server_action_outbox (
  action_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(),1)),
  session_id BINARY(16) NOT NULL,
  realm_id BINARY(16) NOT NULL,
  account_id BINARY(16) NOT NULL,
  character_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  action_kind VARCHAR(128) NOT NULL,
  target_key VARCHAR(191) NULL,
  request_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  idempotency_key VARCHAR(191) NOT NULL,
  priority INT NOT NULL DEFAULT 100,
  max_attempts INT NOT NULL DEFAULT 5,
  attempt_count INT NOT NULL DEFAULT 0,
  status VARCHAR(32) NOT NULL DEFAULT 'pending',
  requested_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  locked_at TIMESTAMP(6) NULL,
  applied_at TIMESTAMP(6) NULL,
  failed_at TIMESTAMP(6) NULL,
  completed_at TIMESTAMP(6) NULL,
  next_attempt_at TIMESTAMP(6) NULL,
  event_id BINARY(16) NULL,
  last_error_code VARCHAR(64) NULL,
  last_error_message TEXT NULL,
  result_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  PRIMARY KEY(action_id),
  UNIQUE KEY uq_mmo_server_action_outbox_idem(idempotency_key),
  KEY idx_mmo_server_action_outbox_claim(status, priority, requested_at, action_id),
  KEY idx_mmo_server_action_outbox_session(session_id, status),
  KEY idx_mmo_server_action_outbox_kind(action_kind, status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_server_action_worker_runs (
  worker_run_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(),1)),
  worker_id VARCHAR(191) NOT NULL,
  run_key VARCHAR(191) NOT NULL,
  worker_mode VARCHAR(128) NOT NULL,
  status VARCHAR(32) NOT NULL DEFAULT 'running',
  metadata JSON NOT NULL DEFAULT (JSON_OBJECT()),
  started_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  finished_at TIMESTAMP(6) NULL,
  applied_count INT NOT NULL DEFAULT 0,
  failed_count INT NOT NULL DEFAULT 0,
  PRIMARY KEY(worker_run_id),
  UNIQUE KEY uq_mmo_server_action_worker_runs_run_key(run_key),
  KEY idx_mmo_server_action_worker_runs_worker(worker_id, started_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_server_action_worker_results (
  result_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(),1)),
  worker_run_id BINARY(16) NOT NULL,
  action_id BINARY(16) NOT NULL,
  action_kind VARCHAR(128) NOT NULL,
  status VARCHAR(32) NOT NULL,
  event_id BINARY(16) NULL,
  error_code VARCHAR(64) NULL,
  error_message TEXT NULL,
  details JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(result_id),
  KEY idx_mmo_server_action_worker_results_run(worker_run_id, created_at),
  KEY idx_mmo_server_action_worker_results_action(action_id, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE OR REPLACE VIEW v_active_server_sessions AS
SELECT
  BIN_TO_UUID(s.session_id,1) AS session_uuid,
  s.session_key,
  BIN_TO_UUID(s.account_id,1) AS account_uuid,
  a.account_name,
  BIN_TO_UUID(s.character_id,1) AS character_uuid,
  c.character_key,
  c.character_name,
  BIN_TO_UUID(s.realm_id,1) AS realm_uuid,
  r.realm_key,
  BIN_TO_UUID(s.world_instance_id,1) AS world_instance_uuid,
  wi.world_instance_key,
  s.client_name,
  s.remote_addr,
  s.lifecycle_state,
  s.started_at,
  s.last_seen_at
FROM server_sessions s
JOIN account_accounts a ON a.account_id=s.account_id
JOIN characters c ON c.character_id=s.character_id
JOIN realm_realms r ON r.realm_id=s.realm_id
JOIN realm_world_instances wi ON wi.world_instance_id=s.world_instance_id
WHERE s.lifecycle_state='active';

CREATE OR REPLACE VIEW v_character_latest_checkpoint AS
SELECT
  BIN_TO_UUID(a.checkpoint_id,1) AS checkpoint_uuid,
  BIN_TO_UUID(a.session_id,1) AS session_uuid,
  BIN_TO_UUID(a.character_id,1) AS character_uuid,
  c.character_key,
  BIN_TO_UUID(a.world_instance_id,1) AS world_instance_uuid,
  a.server_tick,
  a.pos_x,
  a.pos_y,
  a.pos_z,
  a.rotation_yaw,
  a.current_waypoint_key,
  a.created_at
FROM character_checkpoint_audit a
JOIN characters c ON c.character_id=a.character_id
JOIN (
  SELECT character_id, MAX(created_at) AS max_created_at
    FROM character_checkpoint_audit
   GROUP BY character_id
) latest ON latest.character_id=a.character_id AND latest.max_created_at=a.created_at;

CREATE OR REPLACE VIEW v_server_action_outbox AS
SELECT
  BIN_TO_UUID(o.action_id,1) AS action_uuid,
  BIN_TO_UUID(o.session_id,1) AS session_uuid,
  s.session_key,
  o.action_kind,
  o.target_key,
  o.idempotency_key,
  o.priority,
  o.max_attempts,
  o.attempt_count,
  o.status,
  o.requested_at,
  o.locked_at,
  o.applied_at,
  o.failed_at,
  o.completed_at,
  o.last_error_code,
  o.last_error_message
FROM mmo_server_action_outbox o
LEFT JOIN server_sessions s ON s.session_id=o.session_id;

CREATE OR REPLACE VIEW v_pending_server_actions AS
SELECT * FROM v_server_action_outbox WHERE status IN ('pending','claimed','failed','dead_letter');

DROP PROCEDURE IF EXISTS mmo_login_character;
DROP PROCEDURE IF EXISTS mmo_logout_character;
DROP PROCEDURE IF EXISTS mmo_checkpoint_character_state;
DROP PROCEDURE IF EXISTS mmo_enqueue_server_action;
DROP PROCEDURE IF EXISTS mmo_claim_next_server_action;
DROP PROCEDURE IF EXISTS mmo_mark_server_action_applied;
DROP PROCEDURE IF EXISTS mmo_mark_server_action_failed;
DROP PROCEDURE IF EXISTS mmo_start_server_action_worker_run;
DROP PROCEDURE IF EXISTS mmo_finish_server_action_worker_run;
DROP PROCEDURE IF EXISTS mmo_record_server_action_worker_result;
DROP PROCEDURE IF EXISTS mmo_validate_server_action_dispatch_contracts;

DELIMITER $$

CREATE PROCEDURE mmo_login_character(
  IN p_account_name VARCHAR(191),
  IN p_character_key VARCHAR(191),
  IN p_session_key VARCHAR(191),
  IN p_client_name VARCHAR(128),
  IN p_remote_addr VARCHAR(191),
  IN p_client_metadata JSON,
  OUT o_session_id BINARY(16)
)
BEGIN
  DECLARE v_account_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_session_id BINARY(16) DEFAULT NULL;
  DECLARE v_event_id BINARY(16) DEFAULT NULL;
  DECLARE v_not_found_dummy BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found_dummy=TRUE;

  SET o_session_id=NULL;

  SELECT account_id INTO v_account_id
    FROM account_accounts
   WHERE account_name=p_account_name
   LIMIT 1;

  IF v_account_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_login_character: account not found';
  END IF;

  SELECT c.character_id, c.realm_id, COALESCE(c.current_world_instance_id, cp.world_instance_id)
    INTO v_character_id, v_realm_id, v_world_id
    FROM characters c
    LEFT JOIN character_positions cp ON cp.character_id=c.character_id
   WHERE c.account_id=v_account_id
     AND c.character_key=p_character_key
   LIMIT 1;

  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_login_character: character not found for account';
  END IF;

  IF v_world_id IS NULL THEN
    SELECT world_instance_id INTO v_world_id
      FROM realm_world_instances
     WHERE realm_id=v_realm_id
       AND lifecycle_state='active'
     ORDER BY created_at ASC
     LIMIT 1;
  END IF;

  IF v_world_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_login_character: no active world instance';
  END IF;

  START TRANSACTION;

  SELECT session_id INTO v_existing_session_id
    FROM server_sessions
   WHERE session_key=p_session_key
   LIMIT 1
   FOR UPDATE;

  IF v_existing_session_id IS NOT NULL THEN
    UPDATE server_sessions
       SET lifecycle_state='active',
           last_seen_at=CURRENT_TIMESTAMP(6),
           remote_addr=p_remote_addr,
           client_name=p_client_name,
           client_metadata=COALESCE(p_client_metadata, JSON_OBJECT()),
           ended_at=NULL
     WHERE session_id=v_existing_session_id;
    SET o_session_id = v_existing_session_id;
  ELSE
    SET o_session_id = UUID_TO_BIN(UUID(),1);
    SET v_event_id = UUID_TO_BIN(UUID(),1);

    INSERT INTO server_sessions(
      session_id, realm_id, account_id, character_id, world_instance_id,
      session_key, client_name, remote_addr, lifecycle_state, login_event_id,
      client_metadata
    ) VALUES (
      o_session_id, v_realm_id, v_account_id, v_character_id, v_world_id,
      p_session_key, p_client_name, p_remote_addr, 'active', v_event_id,
      COALESCE(p_client_metadata, JSON_OBJECT())
    );

    INSERT INTO world_event_journal(
      event_id, realm_id, world_instance_id, actor_character_id,
      event_type, event_class, idempotency_key, entity_key, subject_key,
      server_tick, source, schema_version, payload
    ) VALUES (
      v_event_id, v_realm_id, v_world_id, v_character_id,
      'character_login', 'character', CONCAT('login:', p_session_key), p_character_key, p_session_key,
      0, 'server', 1,
      JSON_OBJECT(
        'account_name', p_account_name,
        'character_key', p_character_key,
        'session_key', p_session_key,
        'client_name', p_client_name,
        'remote_addr', p_remote_addr,
        'client_metadata', COALESCE(p_client_metadata, JSON_OBJECT())
      )
    );
  END IF;

  UPDATE characters
     SET last_login_at=CURRENT_TIMESTAMP(6),
         current_world_instance_id=v_world_id
   WHERE character_id=v_character_id;

  COMMIT;
END$$

CREATE PROCEDURE mmo_logout_character(
  IN p_session_id BINARY(16),
  IN p_reason VARCHAR(191),
  IN p_metadata JSON,
  OUT o_event_id BINARY(16)
)
BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_session_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_not_found_dummy BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found_dummy=TRUE;

  SELECT s.realm_id, s.world_instance_id, s.character_id, c.character_key, s.session_key
    INTO v_realm_id, v_world_id, v_character_id, v_character_key, v_session_key
    FROM server_sessions s
    JOIN characters c ON c.character_id=s.character_id
   WHERE s.session_id=p_session_id
   LIMIT 1;

  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_logout_character: session not found';
  END IF;

  START TRANSACTION;
  SET o_event_id = UUID_TO_BIN(UUID(),1);

  INSERT INTO world_event_journal(
    event_id, realm_id, world_instance_id, actor_character_id,
    event_type, event_class, idempotency_key, entity_key, subject_key,
    server_tick, source, schema_version, payload
  ) VALUES (
    o_event_id, v_realm_id, v_world_id, v_character_id,
    'character_logout', 'character', CONCAT('logout:', v_session_key), v_character_key, v_session_key,
    0, 'server', 1,
    JSON_OBJECT('reason', p_reason, 'metadata', COALESCE(p_metadata, JSON_OBJECT()))
  );

  UPDATE server_sessions
     SET lifecycle_state='closed', logout_event_id=o_event_id, ended_at=CURRENT_TIMESTAMP(6), last_seen_at=CURRENT_TIMESTAMP(6)
   WHERE session_id=p_session_id;

  UPDATE characters
     SET last_logout_at=CURRENT_TIMESTAMP(6)
   WHERE character_id=v_character_id;

  COMMIT;
END$$

CREATE PROCEDURE mmo_checkpoint_character_state(
  IN p_session_id BINARY(16),
  IN p_server_tick BIGINT,
  IN p_pos_x DOUBLE,
  IN p_pos_y DOUBLE,
  IN p_pos_z DOUBLE,
  IN p_rotation_yaw DOUBLE,
  IN p_current_waypoint_key VARCHAR(191),
  IN p_level_value INT,
  IN p_experience_value BIGINT,
  IN p_experience_next BIGINT,
  IN p_learning_points INT,
  IN p_health_current INT,
  IN p_health_max INT,
  IN p_mana_current INT,
  IN p_mana_max INT,
  IN p_strength_value INT,
  IN p_dexterity_value INT,
  IN p_guild_value INT,
  IN p_true_guild_value INT,
  IN p_permanent_attitude INT,
  IN p_temporary_attitude INT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(191),
  OUT o_event_id BINARY(16)
)
BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_not_found_dummy BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found_dummy=TRUE;

  SET o_event_id=NULL;

  SELECT s.realm_id, s.world_instance_id, s.character_id, c.character_key
    INTO v_realm_id, v_world_id, v_character_id, v_character_key
    FROM server_sessions s
    JOIN characters c ON c.character_id=s.character_id
   WHERE s.session_id=p_session_id
     AND s.lifecycle_state='active'
   LIMIT 1;

  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_checkpoint_character_state: active session not found';
  END IF;

  SELECT event_id INTO o_event_id
    FROM world_event_journal
   WHERE idempotency_key=p_idempotency_key
   LIMIT 1;

  IF o_event_id IS NULL THEN
    START TRANSACTION;
    SET o_event_id = UUID_TO_BIN(UUID(),1);

    INSERT INTO character_positions(
      character_id, world_instance_id, pos_x, pos_y, pos_z, rotation_yaw,
      current_waypoint_key, server_tick, row_version
    ) VALUES (
      v_character_id, v_world_id, p_pos_x, p_pos_y, p_pos_z, p_rotation_yaw,
      p_current_waypoint_key, p_server_tick, 1
    ) ON DUPLICATE KEY UPDATE
      world_instance_id=VALUES(world_instance_id),
      pos_x=VALUES(pos_x), pos_y=VALUES(pos_y), pos_z=VALUES(pos_z),
      rotation_yaw=VALUES(rotation_yaw),
      current_waypoint_key=VALUES(current_waypoint_key),
      server_tick=VALUES(server_tick),
      row_version=row_version+1,
      updated_at=CURRENT_TIMESTAMP(6);

    UPDATE characters
       SET current_world_instance_id=v_world_id,
           updated_at=CURRENT_TIMESTAMP(6)
     WHERE character_id=v_character_id;

    INSERT INTO world_event_journal(
      event_id, realm_id, world_instance_id, actor_character_id,
      event_type, event_class, idempotency_key, entity_key, subject_key,
      server_tick, source, schema_version, payload
    ) VALUES (
      o_event_id, v_realm_id, v_world_id, v_character_id,
      'character_position_checkpoint', 'character', p_idempotency_key, v_character_key, v_character_key,
      p_server_tick, 'server', 1,
      JSON_OBJECT(
        'pos_x', p_pos_x, 'pos_y', p_pos_y, 'pos_z', p_pos_z,
        'rotation_yaw', p_rotation_yaw,
        'current_waypoint_key', p_current_waypoint_key,
        'level', p_level_value,
        'experience', p_experience_value,
        'experience_next', p_experience_next,
        'learning_points', p_learning_points,
        'metadata', COALESCE(p_metadata, JSON_OBJECT())
      )
    );

    INSERT INTO character_checkpoint_audit(
      session_id, character_id, world_instance_id, event_id, server_tick,
      pos_x, pos_y, pos_z, rotation_yaw, current_waypoint_key,
      level_value, experience_value, experience_next, learning_points,
      health_current, health_max, mana_current, mana_max,
      strength_value, dexterity_value, guild_value, true_guild_value,
      permanent_attitude, temporary_attitude, metadata, idempotency_key
    ) VALUES (
      p_session_id, v_character_id, v_world_id, o_event_id, p_server_tick,
      p_pos_x, p_pos_y, p_pos_z, p_rotation_yaw, p_current_waypoint_key,
      p_level_value, p_experience_value, p_experience_next, p_learning_points,
      p_health_current, p_health_max, p_mana_current, p_mana_max,
      p_strength_value, p_dexterity_value, p_guild_value, p_true_guild_value,
      p_permanent_attitude, p_temporary_attitude, COALESCE(p_metadata, JSON_OBJECT()), p_idempotency_key
    );

    COMMIT;
  END IF;
END$$

CREATE PROCEDURE mmo_enqueue_server_action(
  IN p_session_id BINARY(16),
  IN p_action_kind VARCHAR(128),
  IN p_target_key VARCHAR(191),
  IN p_request_payload JSON,
  IN p_idempotency_key VARCHAR(191),
  IN p_priority INT,
  IN p_max_attempts INT,
  OUT o_action_id BINARY(16),
  OUT o_status VARCHAR(32)
)
BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_account_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_id BINARY(16) DEFAULT NULL;
  DECLARE v_not_found_dummy BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found_dummy=TRUE;

  SET o_action_id=NULL;
  SET o_status=NULL;

  SELECT action_id, status INTO o_action_id, o_status
    FROM mmo_server_action_outbox
   WHERE idempotency_key=p_idempotency_key
   LIMIT 1;

  IF o_action_id IS NULL THEN
    SELECT realm_id, account_id, character_id, world_instance_id
      INTO v_realm_id, v_account_id, v_character_id, v_world_id
      FROM server_sessions
     WHERE session_id=p_session_id
       AND lifecycle_state='active'
     LIMIT 1;

    IF v_character_id IS NULL THEN
      SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_enqueue_server_action: active session not found';
    END IF;

    SET o_action_id = UUID_TO_BIN(UUID(),1);
    SET o_status = 'pending';

    INSERT INTO mmo_server_action_outbox(
      action_id, session_id, realm_id, account_id, character_id, world_instance_id,
      action_kind, target_key, request_payload, idempotency_key,
      priority, max_attempts, status
    ) VALUES (
      o_action_id, p_session_id, v_realm_id, v_account_id, v_character_id, v_world_id,
      p_action_kind, p_target_key, COALESCE(p_request_payload, JSON_OBJECT()), p_idempotency_key,
      COALESCE(p_priority,100), GREATEST(COALESCE(p_max_attempts,5),1), 'pending'
    );
  END IF;
END$$

CREATE PROCEDURE mmo_claim_next_server_action(
  IN p_worker_id VARCHAR(191),
  OUT o_action_id BINARY(16),
  OUT o_action_kind VARCHAR(128),
  OUT o_session_id BINARY(16),
  OUT o_character_id BINARY(16),
  OUT o_world_instance_id BINARY(16),
  OUT o_target_key VARCHAR(191),
  OUT o_idempotency_key VARCHAR(191),
  OUT o_request_payload JSON
)
BEGIN
  DECLARE v_not_found BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found=TRUE;

  SET o_action_id=NULL;
  SET o_action_kind=NULL;
  SET o_session_id=NULL;
  SET o_character_id=NULL;
  SET o_world_instance_id=NULL;
  SET o_target_key=NULL;
  SET o_idempotency_key=NULL;
  SET o_request_payload=NULL;

  START TRANSACTION;

  SELECT action_id, action_kind, session_id, character_id, world_instance_id, target_key, idempotency_key, request_payload
    INTO o_action_id, o_action_kind, o_session_id, o_character_id, o_world_instance_id, o_target_key, o_idempotency_key, o_request_payload
    FROM mmo_server_action_outbox
   WHERE status='pending'
     AND (next_attempt_at IS NULL OR next_attempt_at <= CURRENT_TIMESTAMP(6))
   ORDER BY priority ASC, requested_at ASC, action_id ASC
   LIMIT 1
   FOR UPDATE SKIP LOCKED;

  IF NOT v_not_found AND o_action_id IS NOT NULL THEN
    UPDATE mmo_server_action_outbox
       SET status='claimed',
           attempt_count=attempt_count+1,
           locked_at=CURRENT_TIMESTAMP(6),
           result_payload=JSON_MERGE_PATCH(COALESCE(result_payload, JSON_OBJECT()), JSON_OBJECT('claimed_by', p_worker_id))
     WHERE action_id=o_action_id;
  END IF;

  COMMIT;
END$$

CREATE PROCEDURE mmo_mark_server_action_applied(
  IN p_action_id BINARY(16),
  IN p_event_id BINARY(16),
  IN p_result_payload JSON,
  OUT o_status VARCHAR(32)
)
BEGIN
  UPDATE mmo_server_action_outbox
     SET status='applied',
         event_id=p_event_id,
         result_payload=COALESCE(p_result_payload, JSON_OBJECT()),
         applied_at=CURRENT_TIMESTAMP(6),
         completed_at=CURRENT_TIMESTAMP(6),
         locked_at=NULL,
         last_error_code=NULL,
         last_error_message=NULL
   WHERE action_id=p_action_id;
  SET o_status='applied';
END$$

CREATE PROCEDURE mmo_mark_server_action_failed(
  IN p_action_id BINARY(16),
  IN p_error_code VARCHAR(64),
  IN p_error_message TEXT,
  IN p_retryable BOOL,
  OUT o_status VARCHAR(32)
)
BEGIN
  DECLARE v_attempt_count INT DEFAULT 0;
  DECLARE v_max_attempts INT DEFAULT 1;
  DECLARE v_not_found_dummy BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found_dummy=TRUE;

  SELECT attempt_count, max_attempts INTO v_attempt_count, v_max_attempts
    FROM mmo_server_action_outbox
   WHERE action_id=p_action_id
   LIMIT 1;

  IF p_retryable AND v_attempt_count < v_max_attempts THEN
    SET o_status='pending';
    UPDATE mmo_server_action_outbox
       SET status=o_status,
           locked_at=NULL,
           next_attempt_at=TIMESTAMPADD(SECOND, LEAST(60, 2 * GREATEST(v_attempt_count,1)), CURRENT_TIMESTAMP(6)),
           last_error_code=p_error_code,
           last_error_message=p_error_message,
           result_payload=JSON_MERGE_PATCH(COALESCE(result_payload, JSON_OBJECT()), JSON_OBJECT('last_error_code', p_error_code, 'last_retryable', TRUE))
     WHERE action_id=p_action_id;
  ELSE
    SET o_status='failed';
    UPDATE mmo_server_action_outbox
       SET status=o_status,
           locked_at=NULL,
           failed_at=CURRENT_TIMESTAMP(6),
           completed_at=CURRENT_TIMESTAMP(6),
           last_error_code=p_error_code,
           last_error_message=p_error_message,
           result_payload=JSON_MERGE_PATCH(COALESCE(result_payload, JSON_OBJECT()), JSON_OBJECT('last_error_code', p_error_code, 'last_retryable', FALSE))
     WHERE action_id=p_action_id;
  END IF;
END$$

CREATE PROCEDURE mmo_start_server_action_worker_run(
  IN p_worker_id VARCHAR(191),
  IN p_run_key VARCHAR(191),
  IN p_worker_mode VARCHAR(128),
  IN p_metadata JSON,
  OUT o_worker_run_id BINARY(16)
)
BEGIN
  DECLARE v_not_found_dummy BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found_dummy=TRUE;

  SET o_worker_run_id=NULL;

  SELECT worker_run_id INTO o_worker_run_id
    FROM mmo_server_action_worker_runs
   WHERE run_key=p_run_key
   LIMIT 1;

  IF o_worker_run_id IS NULL THEN
    SET o_worker_run_id=UUID_TO_BIN(UUID(),1);
    INSERT INTO mmo_server_action_worker_runs(worker_run_id, worker_id, run_key, worker_mode, metadata, status)
    VALUES(o_worker_run_id, p_worker_id, p_run_key, p_worker_mode, COALESCE(p_metadata, JSON_OBJECT()), 'running');
  ELSE
    UPDATE mmo_server_action_worker_runs
       SET status='running', started_at=CURRENT_TIMESTAMP(6), finished_at=NULL, metadata=COALESCE(p_metadata, JSON_OBJECT())
     WHERE worker_run_id=o_worker_run_id;
  END IF;
END$$

CREATE PROCEDURE mmo_finish_server_action_worker_run(
  IN p_worker_run_id BINARY(16),
  IN p_failed BOOL,
  OUT o_status VARCHAR(32),
  OUT o_applied_count INT
)
BEGIN
  SELECT COUNT(*) INTO o_applied_count
    FROM mmo_server_action_worker_results
   WHERE worker_run_id=p_worker_run_id
     AND status IN ('applied','claimed');

  SET o_status = IF(p_failed, 'failed', 'finished');

  UPDATE mmo_server_action_worker_runs
     SET status=o_status,
         applied_count=(SELECT COUNT(*) FROM mmo_server_action_worker_results WHERE worker_run_id=p_worker_run_id AND status='applied'),
         failed_count=(SELECT COUNT(*) FROM mmo_server_action_worker_results WHERE worker_run_id=p_worker_run_id AND status IN ('failed','dead_letter')),
         finished_at=CURRENT_TIMESTAMP(6)
   WHERE worker_run_id=p_worker_run_id;
END$$

CREATE PROCEDURE mmo_record_server_action_worker_result(
  IN p_worker_run_id BINARY(16),
  IN p_action_id BINARY(16),
  IN p_action_kind VARCHAR(128),
  IN p_status VARCHAR(32),
  IN p_event_id BINARY(16),
  IN p_error_code VARCHAR(64),
  IN p_error_message TEXT,
  IN p_details JSON
)
BEGIN
  INSERT INTO mmo_server_action_worker_results(
    worker_run_id, action_id, action_kind, status, event_id,
    error_code, error_message, details
  ) VALUES (
    p_worker_run_id, p_action_id, p_action_kind, p_status, p_event_id,
    p_error_code, p_error_message, COALESCE(p_details, JSON_OBJECT())
  );
END$$

CREATE PROCEDURE mmo_validate_server_action_dispatch_contracts(
  OUT o_errors INT,
  OUT o_warnings INT
)
BEGIN
  SET o_errors=0;
  SET o_warnings=0;
END$$

DELIMITER ;

-- ============================================================================
-- END server/sql/step55_live_receiver_bridge.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step56b_clean_db_progress_bridge.sql
-- ============================================================================

-- Step56b / Step57 clean-DB progress bridge.
-- Adds the minimal progress/dialog/quest procedures expected by the live
-- resolved worker when a DB was rebuilt from the Step54/55 clean MySQL flow.
-- Additive only: no table drops, no old single-player behavior changes.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;

DELIMITER $$

DROP PROCEDURE IF EXISTS mmo_set_character_script_int $$
CREATE PROCEDURE mmo_set_character_script_int(
  IN p_session_id BINARY(16),
  IN p_script_key VARCHAR(255),
  IN p_symbol_index INT,
  IN p_value_index INT,
  IN p_value_after INT,
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(255),
  OUT p_event_id BINARY(16),
  OUT p_value_after_out INT
)
BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_character_key VARCHAR(255);
  DECLARE v_payload JSON;

  SELECT s.realm_id, s.world_instance_id, s.character_id, c.character_key
    INTO v_realm_id, v_world_id, v_character_id, v_character_key
    FROM server_sessions s
    LEFT JOIN characters c ON c.character_id = s.character_id
   WHERE s.session_id = p_session_id
   LIMIT 1;

  IF v_world_id IS NULL OR v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_set_character_script_int: invalid session';
  END IF;
  IF COALESCE(p_script_key, '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_set_character_script_int: script key is required';
  END IF;

  SET v_payload = JSON_MERGE_PATCH(
    COALESCE(p_metadata, JSON_OBJECT()),
    JSON_OBJECT(
      'character_key', COALESCE(v_character_key, 'PC_HERO'),
      'script_key', p_script_key,
      'symbol_index', p_symbol_index,
      'value_index', COALESCE(p_value_index, 0),
      'value_after', p_value_after
    )
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_id,
    v_character_id,
    'character_script_int_set',
    'character',
    COALESCE(p_server_tick, 0),
    COALESCE(v_character_key, 'PC_HERO'),
    p_script_key,
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  INSERT INTO character_script_state(
    character_id,
    script_key,
    symbol_index,
    value_type,
    value_index,
    value_int,
    value_text
  )
  VALUES(
    v_character_id,
    p_script_key,
    p_symbol_index,
    'int',
    COALESCE(p_value_index, 0),
    p_value_after,
    CAST(p_value_after AS CHAR)
  )
  ON DUPLICATE KEY UPDATE
    symbol_index = VALUES(symbol_index),
    value_type = VALUES(value_type),
    value_int = VALUES(value_int),
    value_text = VALUES(value_text);

  SET p_value_after_out = p_value_after;
END $$

DROP PROCEDURE IF EXISTS mmo_update_character_quest $$
CREATE PROCEDURE mmo_update_character_quest(
  IN p_session_id BINARY(16),
  IN p_quest_key VARCHAR(255),
  IN p_quest_name VARCHAR(255),
  IN p_status VARCHAR(64),
  IN p_entry_count INT,
  IN p_entries JSON,
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(255),
  OUT p_event_id BINARY(16)
)
BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_character_key VARCHAR(255);
  DECLARE v_status VARCHAR(64);
  DECLARE v_payload JSON;

  SELECT s.realm_id, s.world_instance_id, s.character_id, c.character_key
    INTO v_realm_id, v_world_id, v_character_id, v_character_key
    FROM server_sessions s
    LEFT JOIN characters c ON c.character_id = s.character_id
   WHERE s.session_id = p_session_id
   LIMIT 1;

  IF v_world_id IS NULL OR v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_update_character_quest: invalid session';
  END IF;
  IF COALESCE(p_quest_key, '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_update_character_quest: quest key is required';
  END IF;

  SET v_status = COALESCE(NULLIF(p_status, ''), 'running');
  SET v_payload = JSON_MERGE_PATCH(
    COALESCE(p_metadata, JSON_OBJECT()),
    JSON_OBJECT(
      'character_key', COALESCE(v_character_key, 'PC_HERO'),
      'quest_key', p_quest_key,
      'quest_name', COALESCE(NULLIF(p_quest_name, ''), p_quest_key),
      'status', v_status,
      'entry_count', COALESCE(p_entry_count, 0),
      'entries', COALESCE(p_entries, JSON_ARRAY())
    )
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_id,
    v_character_id,
    'character_quest_updated',
    'character',
    COALESCE(p_server_tick, 0),
    COALESCE(v_character_key, 'PC_HERO'),
    p_quest_key,
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  INSERT INTO character_quests(
    character_id,
    quest_key,
    section,
    status,
    entry_order,
    text_entries
  )
  VALUES(
    v_character_id,
    p_quest_key,
    '',
    v_status,
    COALESCE(p_entry_count, 0),
    COALESCE(p_entries, JSON_ARRAY())
  )
  ON DUPLICATE KEY UPDATE
    status = VALUES(status),
    entry_order = VALUES(entry_order),
    text_entries = VALUES(text_entries);
END $$

DROP PROCEDURE IF EXISTS mmo_set_character_known_dialog $$
CREATE PROCEDURE mmo_set_character_known_dialog(
  IN p_session_id BINARY(16),
  IN p_npc_key VARCHAR(255),
  IN p_info_key VARCHAR(255),
  IN p_known BOOLEAN,
  IN p_permanent BOOLEAN,
  IN p_availability_state VARCHAR(64),
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(255),
  OUT p_event_id BINARY(16)
)
BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_character_key VARCHAR(255);
  DECLARE v_known BOOLEAN;
  DECLARE v_permanent BOOLEAN;
  DECLARE v_availability_state VARCHAR(64);
  DECLARE v_payload JSON;

  SELECT s.realm_id, s.world_instance_id, s.character_id, c.character_key
    INTO v_realm_id, v_world_id, v_character_id, v_character_key
    FROM server_sessions s
    LEFT JOIN characters c ON c.character_id = s.character_id
   WHERE s.session_id = p_session_id
   LIMIT 1;

  IF v_world_id IS NULL OR v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_set_character_known_dialog: invalid session';
  END IF;
  IF COALESCE(p_npc_key, '') = '' OR COALESCE(p_info_key, '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_set_character_known_dialog: npc/info key is required';
  END IF;

  SET v_known = COALESCE(p_known, TRUE);
  SET v_permanent = COALESCE(p_permanent, FALSE);
  SET v_availability_state = COALESCE(
    NULLIF(p_availability_state, ''),
    IF(v_known, IF(v_permanent, 'repeatable_known', 'consumed_hidden'), 'hidden')
  );

  SET v_payload = JSON_MERGE_PATCH(
    COALESCE(p_metadata, JSON_OBJECT()),
    JSON_OBJECT(
      'character_key', COALESCE(v_character_key, 'PC_HERO'),
      'npc_key', p_npc_key,
      'info_key', p_info_key,
      'known', v_known,
      'permanent', v_permanent,
      'availability_state', v_availability_state
    )
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_id,
    v_character_id,
    'character_dialog_known_set',
    'character',
    COALESCE(p_server_tick, 0),
    COALESCE(v_character_key, 'PC_HERO'),
    p_info_key,
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  INSERT INTO character_known_dialogs(
    character_id,
    npc_key,
    info_key,
    known,
    permanent,
    availability_state
  )
  VALUES(
    v_character_id,
    p_npc_key,
    p_info_key,
    v_known,
    v_permanent,
    v_availability_state
  )
  ON DUPLICATE KEY UPDATE
    known = VALUES(known),
    permanent = VALUES(permanent),
    availability_state = VALUES(availability_state);
END $$

DELIMITER ;

-- ============================================================================
-- END server/sql/step56b_clean_db_progress_bridge.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step59_clean_db_item_interactive_progress_bridge.sql
-- ============================================================================

-- Step59 clean-DB item/interactive/progress bridge.
--
-- Purpose:
--   Fresh Step55 clean MySQL rebuilds currently install the live receiver bridge
--   and Step56b script/dialog/quest bridge, but Step58 live testing showed the
--   resolved worker still misses item pickup, interactive state and progression
--   routines. These procedures are additive dev-authority bridge surfaces; they
--   keep the old single-player path untouched and only run when the external
--   server worker calls them.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS world_item_audit (
  audit_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) NULL,
  world_instance_id BINARY(16) NOT NULL,
  character_id BINARY(16) NULL,
  entity_key VARCHAR(512) NOT NULL,
  item_instance_id BINARY(16) NULL,
  audit_type VARCHAR(64) NOT NULL,
  amount INT NOT NULL DEFAULT 0,
  idempotency_key VARCHAR(512) NOT NULL,
  metadata JSON NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (audit_id),
  UNIQUE KEY ux_world_item_audit_idempotency (idempotency_key),
  KEY ix_world_item_audit_entity (world_instance_id, entity_key),
  KEY ix_world_item_audit_event (event_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS world_interactive_audit (
  audit_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) NULL,
  world_instance_id BINARY(16) NOT NULL,
  character_id BINARY(16) NULL,
  entity_key VARCHAR(512) NOT NULL,
  audit_type VARCHAR(64) NOT NULL,
  state_after INT NULL,
  row_version_after BIGINT NULL,
  idempotency_key VARCHAR(512) NOT NULL,
  metadata JSON NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (audit_id),
  UNIQUE KEY ux_world_interactive_audit_idempotency (idempotency_key),
  KEY ix_world_interactive_audit_entity (world_instance_id, entity_key),
  KEY ix_world_interactive_audit_event (event_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS character_progress_audit (
  audit_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) NULL,
  character_id BINARY(16) NOT NULL,
  audit_type VARCHAR(64) NOT NULL,
  experience_delta INT NOT NULL DEFAULT 0,
  learning_points_delta INT NOT NULL DEFAULT 0,
  experience_after INT NOT NULL DEFAULT 0,
  learning_points_after INT NOT NULL DEFAULT 0,
  reason VARCHAR(128) NULL,
  idempotency_key VARCHAR(512) NOT NULL,
  metadata JSON NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (audit_id),
  UNIQUE KEY ux_character_progress_audit_idempotency (idempotency_key),
  KEY ix_character_progress_audit_character (character_id),
  KEY ix_character_progress_audit_event (event_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

SET @step59_migration_key = 'dev/sql/step59_clean_db_item_interactive_progress_bridge';
SET @step59_schema_contract = 'gothic-mmo-step59-clean-db-item-interactive-progress-bridge-v2';
SET @step59_schema_notes = 'Installs item pickup/removal, interactive-state and progression bridge procedures; compatible with clean DBs with or without mmo_schema_versions.notes.';
SET @step59_has_schema_notes = (
  SELECT COUNT(*)
    FROM information_schema.COLUMNS
   WHERE TABLE_SCHEMA = DATABASE()
     AND TABLE_NAME = 'mmo_schema_versions'
     AND COLUMN_NAME = 'notes'
);
SET @step59_schema_sql = IF(
  @step59_has_schema_notes > 0,
  CONCAT(
    'INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes) VALUES (',
    QUOTE(@step59_migration_key), ', ', QUOTE(@step59_schema_contract), ', ', QUOTE(@step59_schema_notes),
    ') ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes)'
  ),
  CONCAT(
    'INSERT INTO mmo_schema_versions(migration_key, schema_contract) VALUES (',
    QUOTE(@step59_migration_key), ', ', QUOTE(@step59_schema_contract),
    ') ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract)'
  )
);
PREPARE step59_schema_stmt FROM @step59_schema_sql;
EXECUTE step59_schema_stmt;
DEALLOCATE PREPARE step59_schema_stmt;

DROP PROCEDURE IF EXISTS mmo_pickup_world_item;
DROP PROCEDURE IF EXISTS mmo_remove_world_item;
DROP PROCEDURE IF EXISTS mmo_update_interactive_state;
DROP PROCEDURE IF EXISTS mmo_adjust_character_progression;
DROP PROCEDURE IF EXISTS mmo_apply_character_experience_reward;

DELIMITER $$

CREATE PROCEDURE mmo_pickup_world_item(
  IN p_session_id BINARY(16),
  IN p_world_item_entity_key VARCHAR(512),
  IN p_amount_requested INT,
  IN p_bag_index INT,
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_item_instance_id BINARY(16),
  OUT o_amount_picked INT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_amount INT DEFAULT 1;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SELECT event_id, item_instance_id, amount
    INTO o_event_id, o_item_instance_id, o_amount_picked
    FROM world_item_audit
   WHERE idempotency_key = p_idempotency_key
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    LEAVE proc;
  END IF;

  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_pickup_world_item: active session not found';
  END IF;

  SELECT ii.item_instance_id, GREATEST(1, LEAST(COALESCE(NULLIF(p_amount_requested, 0), ii.quantity, 1), COALESCE(ii.quantity, 1)))
    INTO o_item_instance_id, v_amount
    FROM item_instances ii
   WHERE ii.realm_id = v_realm_id
     AND ii.owner_type = 'world_entity'
     AND ii.lifecycle_state = 'active'
     AND (
          JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload, '$.item_spawn_key')) = p_world_item_entity_key
          OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload, '$.entity_key')) = p_world_item_entity_key
          OR ii.item_instance_key LIKE CONCAT('%', p_world_item_entity_key, '%')
     )
   ORDER BY ii.updated_at DESC, ii.item_instance_key ASC
   LIMIT 1;
  IF o_item_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_pickup_world_item: world item instance not found';
  END IF;

  START TRANSACTION;

  UPDATE world_entity_state
     SET lifecycle_state = 'removed',
         row_version = COALESCE(row_version, 0) + 1,
         state_json = JSON_MERGE_PATCH(
           COALESCE(state_json, JSON_OBJECT()),
           JSON_OBJECT('exists_in_world', false, 'picked_by_character', BIN_TO_UUID(v_character_id, 1), 'picked_at_tick', p_server_tick)
         ),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = p_world_item_entity_key
     AND entity_kind = 'item'
     AND lifecycle_state = 'active';
  IF ROW_COUNT() <> 1 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_pickup_world_item: active world item entity not found';
  END IF;

  UPDATE item_instances
     SET owner_type = 'character',
         owner_id = v_character_id,
         quantity = v_amount,
         lifecycle_state = 'active',
         raw_payload = JSON_MERGE_PATCH(
           COALESCE(raw_payload, JSON_OBJECT()),
           JSON_OBJECT('picked_from_world_entity_key', p_world_item_entity_key, 'picked_at_tick', p_server_tick)
         ),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE item_instance_id = o_item_instance_id;

  INSERT INTO character_inventory(character_id, item_instance_id, bag_index, amount, source_amount, source_iterator_count)
  VALUES(v_character_id, o_item_instance_id, p_bag_index, v_amount, v_amount, v_amount)
  ON DUPLICATE KEY UPDATE
    bag_index = COALESCE(character_inventory.bag_index, VALUES(bag_index)),
    amount = character_inventory.amount + VALUES(amount),
    source_amount = VALUES(source_amount),
    source_iterator_count = VALUES(source_iterator_count);

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'world_item_picked_up', 'inventory', p_server_tick, p_world_item_entity_key, NULL,
    JSON_OBJECT('world_item_entity_key', p_world_item_entity_key, 'item_instance_id', BIN_TO_UUID(o_item_instance_id, 1), 'amount', v_amount, 'bag_index', p_bag_index, 'metadata', p_metadata),
    p_idempotency_key, 'server', NULL, NULL, o_event_id
  );

  INSERT INTO world_item_audit(event_id, world_instance_id, character_id, entity_key, item_instance_id, audit_type, amount, idempotency_key, metadata)
  VALUES(o_event_id, v_world_instance_id, v_character_id, p_world_item_entity_key, o_item_instance_id, 'pickup', v_amount, p_idempotency_key, p_metadata);

  COMMIT;
  SET o_amount_picked = v_amount;
END$$

CREATE PROCEDURE mmo_remove_world_item(
  IN p_session_id BINARY(16),
  IN p_world_item_entity_key VARCHAR(512),
  IN p_reason VARCHAR(128),
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_item_instance_id BINARY(16)
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  DECLARE v_character_id BINARY(16);

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SELECT event_id, item_instance_id
    INTO o_event_id, o_item_instance_id
    FROM world_item_audit
   WHERE idempotency_key = p_idempotency_key
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    LEAVE proc;
  END IF;

  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_remove_world_item: active session not found';
  END IF;

  SELECT ii.item_instance_id
    INTO o_item_instance_id
    FROM item_instances ii
   WHERE ii.realm_id = v_realm_id
     AND ii.owner_type = 'world_entity'
     AND ii.lifecycle_state = 'active'
     AND (
          JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload, '$.item_spawn_key')) = p_world_item_entity_key
          OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload, '$.entity_key')) = p_world_item_entity_key
          OR ii.item_instance_key LIKE CONCAT('%', p_world_item_entity_key, '%')
     )
   ORDER BY ii.updated_at DESC, ii.item_instance_key ASC
   LIMIT 1;
  IF o_item_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_remove_world_item: world item instance not found';
  END IF;

  START TRANSACTION;

  UPDATE world_entity_state
     SET lifecycle_state = 'removed',
         row_version = COALESCE(row_version, 0) + 1,
         state_json = JSON_MERGE_PATCH(
           COALESCE(state_json, JSON_OBJECT()),
           JSON_OBJECT('exists_in_world', false, 'remove_reason', COALESCE(p_reason, 'semantic_action'), 'removed_at_tick', p_server_tick)
         ),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = p_world_item_entity_key
     AND entity_kind = 'item'
     AND lifecycle_state = 'active';
  IF ROW_COUNT() <> 1 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_remove_world_item: active world item entity not found';
  END IF;

  UPDATE item_instances
     SET owner_type = 'system',
         owner_id = NULL,
         lifecycle_state = 'archived',
         raw_payload = JSON_MERGE_PATCH(COALESCE(raw_payload, JSON_OBJECT()), JSON_OBJECT('removed_reason', COALESCE(p_reason, 'semantic_action'), 'removed_at_tick', p_server_tick)),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE item_instance_id = o_item_instance_id;

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'world_item_removed', 'world_entity', p_server_tick, p_world_item_entity_key, NULL,
    JSON_OBJECT('world_item_entity_key', p_world_item_entity_key, 'item_instance_id', BIN_TO_UUID(o_item_instance_id, 1), 'reason', COALESCE(p_reason, 'semantic_action'), 'metadata', p_metadata),
    p_idempotency_key, 'server', NULL, NULL, o_event_id
  );

  INSERT INTO world_item_audit(event_id, world_instance_id, character_id, entity_key, item_instance_id, audit_type, amount, idempotency_key, metadata)
  VALUES(o_event_id, v_world_instance_id, v_character_id, p_world_item_entity_key, o_item_instance_id, 'remove', 1, p_idempotency_key, p_metadata);

  COMMIT;
END$$

CREATE PROCEDURE mmo_update_interactive_state(
  IN p_session_id BINARY(16),
  IN p_interactive_entity_key VARCHAR(512),
  IN p_state_after INT,
  IN p_state_count INT,
  IN p_state_mask INT,
  IN p_locked_after BOOLEAN,
  IN p_cracked_after BOOLEAN,
  IN p_lifecycle_state VARCHAR(32),
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_row_version_after BIGINT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  DECLARE v_character_id BINARY(16);

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SELECT event_id, row_version_after
    INTO o_event_id, o_row_version_after
    FROM world_interactive_audit
   WHERE idempotency_key = p_idempotency_key
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    LEAVE proc;
  END IF;

  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_update_interactive_state: active session not found';
  END IF;

  START TRANSACTION;

  UPDATE world_entity_state
     SET lifecycle_state = COALESCE(NULLIF(p_lifecycle_state, ''), lifecycle_state),
         row_version = COALESCE(row_version, 0) + 1,
         state_json = JSON_MERGE_PATCH(
           COALESCE(state_json, JSON_OBJECT()),
           JSON_OBJECT(
             'state_id', p_state_after,
             'state_count', p_state_count,
             'state_mask', p_state_mask,
             'locked', IF(p_locked_after, true, false),
             'cracked', IF(p_cracked_after, true, false),
             'updated_at_tick', p_server_tick
           )
         ),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = p_interactive_entity_key
     AND entity_kind = 'interactive';
  IF ROW_COUNT() <> 1 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_update_interactive_state: interactive entity not found';
  END IF;

  SELECT row_version
    INTO o_row_version_after
    FROM world_entity_state
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = p_interactive_entity_key
   LIMIT 1;

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'interactive_state_changed', 'world_entity', p_server_tick, p_interactive_entity_key, NULL,
    JSON_OBJECT('interactive_entity_key', p_interactive_entity_key, 'state_after', p_state_after, 'state_count', p_state_count, 'state_mask', p_state_mask, 'locked_after', p_locked_after, 'cracked_after', p_cracked_after, 'row_version_after', o_row_version_after, 'metadata', p_metadata),
    p_idempotency_key, 'server', NULL, NULL, o_event_id
  );

  INSERT INTO world_interactive_audit(event_id, world_instance_id, character_id, entity_key, audit_type, state_after, row_version_after, idempotency_key, metadata)
  VALUES(o_event_id, v_world_instance_id, v_character_id, p_interactive_entity_key, 'interactive_state', p_state_after, o_row_version_after, p_idempotency_key, p_metadata);

  COMMIT;
END$$

CREATE PROCEDURE mmo_adjust_character_progression(
  IN p_session_id BINARY(16),
  IN p_experience_delta INT,
  IN p_learning_points_delta INT,
  IN p_reason VARCHAR(128),
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_experience_after INT,
  OUT o_learning_points_after INT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_exp_delta INT DEFAULT 0;
  DECLARE v_lp_delta INT DEFAULT 0;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SELECT event_id, experience_after, learning_points_after
    INTO o_event_id, o_experience_after, o_learning_points_after
    FROM character_progress_audit
   WHERE idempotency_key = p_idempotency_key
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    LEAVE proc;
  END IF;

  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_adjust_character_progression: active session not found';
  END IF;

  SET v_exp_delta = COALESCE(p_experience_delta, 0);
  SET v_lp_delta = COALESCE(p_learning_points_delta, 0);

  START TRANSACTION;

  UPDATE character_stats
     SET experience = GREATEST(0, COALESCE(experience, 0) + v_exp_delta),
         learning_points = GREATEST(0, COALESCE(learning_points, 0) + v_lp_delta),
         row_version = COALESCE(row_version, 0) + 1,
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE character_id = v_character_id;
  IF ROW_COUNT() <> 1 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_adjust_character_progression: character stats not found';
  END IF;

  SELECT COALESCE(experience, 0), COALESCE(learning_points, 0)
    INTO o_experience_after, o_learning_points_after
    FROM character_stats
   WHERE character_id = v_character_id
   LIMIT 1;

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'character_progression_adjusted', 'character', p_server_tick, NULL, NULL,
    JSON_OBJECT('experience_delta', v_exp_delta, 'learning_points_delta', v_lp_delta, 'experience_after', o_experience_after, 'learning_points_after', o_learning_points_after, 'reason', COALESCE(p_reason, 'script_progression'), 'metadata', p_metadata),
    p_idempotency_key, 'server', NULL, NULL, o_event_id
  );

  INSERT INTO character_progress_audit(event_id, character_id, audit_type, experience_delta, learning_points_delta, experience_after, learning_points_after, reason, idempotency_key, metadata)
  VALUES(o_event_id, v_character_id, 'progression_adjust', v_exp_delta, v_lp_delta, o_experience_after, o_learning_points_after, p_reason, p_idempotency_key, p_metadata);

  COMMIT;
END$$

CREATE PROCEDURE mmo_apply_character_experience_reward(
  IN p_session_id BINARY(16),
  IN p_experience_delta INT,
  IN p_reason VARCHAR(128),
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_experience_after INT
)
BEGIN
  DECLARE v_learning_points_after INT DEFAULT 0;

  CALL mmo_adjust_character_progression(
    p_session_id,
    p_experience_delta,
    0,
    COALESCE(p_reason, 'script_experience_reward'),
    p_server_tick,
    p_metadata,
    p_idempotency_key,
    o_event_id,
    o_experience_after,
    v_learning_points_after
  );
END$$

DELIMITER ;

-- ============================================================================
-- END server/sql/step59_clean_db_item_interactive_progress_bridge.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step60_clean_db_equipment_bridge.sql
-- ============================================================================

-- Step60 clean-DB equipment bridge.
--
-- Purpose:
--   Clean Step55/59 MySQL rebuilds currently have character_equipment rows but
--   can miss the live worker routines used by equip_character_item and
--   unequip_character_item. This additive bridge keeps the old single-player
--   path untouched and only runs when the resolved server worker calls it.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS character_inventory_audit (
  audit_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) NULL,
  character_id BINARY(16) NOT NULL,
  target_character_id BINARY(16) NULL,
  item_instance_id BINARY(16) NOT NULL,
  audit_type VARCHAR(64) NOT NULL,
  amount INT NOT NULL DEFAULT 0,
  equipment_slot VARCHAR(32) NULL,
  idempotency_key VARCHAR(512) NOT NULL,
  metadata JSON NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (audit_id),
  UNIQUE KEY ux_character_inventory_audit_idem (idempotency_key),
  KEY ix_character_inventory_audit_character (character_id, created_at),
  KEY ix_character_inventory_audit_item (item_instance_id),
  KEY ix_character_inventory_audit_event (event_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

SET @step60_migration_key = 'dev/sql/step60_clean_db_equipment_bridge';
SET @step60_schema_contract = 'gothic-mmo-step60-clean-db-equipment-bridge-v1';
SET @step60_schema_notes = 'Installs minimal equip, unequip and transfer bridge procedures for clean live-worker DBs.';
SET @step60_has_schema_notes = (
  SELECT COUNT(*)
    FROM information_schema.COLUMNS
   WHERE TABLE_SCHEMA = DATABASE()
     AND TABLE_NAME = 'mmo_schema_versions'
     AND COLUMN_NAME = 'notes'
);
SET @step60_schema_sql = IF(
  @step60_has_schema_notes > 0,
  CONCAT(
    'INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes) VALUES (',
    QUOTE(@step60_migration_key), ', ', QUOTE(@step60_schema_contract), ', ', QUOTE(@step60_schema_notes),
    ') ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes)'
  ),
  CONCAT(
    'INSERT INTO mmo_schema_versions(migration_key, schema_contract) VALUES (',
    QUOTE(@step60_migration_key), ', ', QUOTE(@step60_schema_contract),
    ') ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract)'
  )
);
PREPARE step60_schema_stmt FROM @step60_schema_sql;
EXECUTE step60_schema_stmt;
DEALLOCATE PREPARE step60_schema_stmt;

DROP PROCEDURE IF EXISTS mmo_equip_character_item;
DROP PROCEDURE IF EXISTS mmo_unequip_character_item;
DROP PROCEDURE IF EXISTS mmo_transfer_character_item;

DELIMITER $$

CREATE PROCEDURE mmo_equip_character_item(
  IN p_session_id BINARY(16),
  IN p_item_instance_id BINARY(16),
  IN p_equipment_slot VARCHAR(32),
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16)
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_slot VARCHAR(32);
  DECLARE v_amount INT DEFAULT NULL;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SELECT event_id
    INTO o_event_id
    FROM character_inventory_audit
   WHERE idempotency_key = p_idempotency_key
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    LEAVE proc;
  END IF;

  SET v_slot = COALESCE(NULLIF(p_equipment_slot, ''), 'unknown');
  IF v_slot NOT IN ('weapon_melee','weapon_ranged','shield','armor','belt','amulet','ring_left','ring_right','rune','torch','unknown') THEN
    SET v_slot = 'unknown';
  END IF;

  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_equip_character_item: active session not found';
  END IF;

  SELECT ci.amount
    INTO v_amount
    FROM character_inventory ci
    JOIN item_instances ii ON ii.item_instance_id = ci.item_instance_id
   WHERE ci.character_id = v_character_id
     AND ci.item_instance_id = p_item_instance_id
     AND ii.realm_id = v_realm_id
     AND ii.owner_type = 'character'
     AND ii.owner_id = v_character_id
     AND ii.lifecycle_state = 'active'
   LIMIT 1;
  IF v_amount IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_equip_character_item: character inventory item not found';
  END IF;

  START TRANSACTION;

  DELETE FROM character_equipment
   WHERE character_id = v_character_id
     AND (equipment_slot = v_slot OR item_instance_id = p_item_instance_id);

  INSERT INTO character_equipment(character_id, equipment_slot, item_instance_id)
  VALUES(v_character_id, v_slot, p_item_instance_id);

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'character_item_equipped', 'equipment', p_server_tick,
    v_slot, BIN_TO_UUID(p_item_instance_id, 1),
    JSON_OBJECT('item_instance_id', BIN_TO_UUID(p_item_instance_id, 1), 'equipment_slot', v_slot, 'amount', v_amount, 'metadata', p_metadata),
    p_idempotency_key, 'server', NULL, NULL, o_event_id
  );

  INSERT INTO character_inventory_audit(event_id, character_id, item_instance_id, audit_type, amount, equipment_slot, idempotency_key, metadata)
  VALUES(o_event_id, v_character_id, p_item_instance_id, 'equip', v_amount, v_slot, p_idempotency_key, p_metadata);

  COMMIT;
END$$

CREATE PROCEDURE mmo_unequip_character_item(
  IN p_session_id BINARY(16),
  IN p_equipment_slot VARCHAR(32),
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_item_instance_id BINARY(16)
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_slot VARCHAR(32);
  DECLARE v_amount INT DEFAULT 1;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SELECT event_id, item_instance_id
    INTO o_event_id, o_item_instance_id
    FROM character_inventory_audit
   WHERE idempotency_key = p_idempotency_key
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    LEAVE proc;
  END IF;

  SET v_slot = COALESCE(NULLIF(p_equipment_slot, ''), 'unknown');
  IF v_slot NOT IN ('weapon_melee','weapon_ranged','shield','armor','belt','amulet','ring_left','ring_right','rune','torch','unknown') THEN
    SET v_slot = 'unknown';
  END IF;

  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_unequip_character_item: active session not found';
  END IF;

  SELECT ce.item_instance_id, COALESCE(ci.amount, 1)
    INTO o_item_instance_id, v_amount
    FROM character_equipment ce
    LEFT JOIN character_inventory ci ON ci.character_id = ce.character_id AND ci.item_instance_id = ce.item_instance_id
   WHERE ce.character_id = v_character_id
     AND ce.equipment_slot = v_slot
   LIMIT 1;
  IF o_item_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_unequip_character_item: equipment slot is empty';
  END IF;

  START TRANSACTION;

  DELETE FROM character_equipment
   WHERE character_id = v_character_id
     AND equipment_slot = v_slot;

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'character_item_unequipped', 'equipment', p_server_tick,
    v_slot, BIN_TO_UUID(o_item_instance_id, 1),
    JSON_OBJECT('item_instance_id', BIN_TO_UUID(o_item_instance_id, 1), 'equipment_slot', v_slot, 'amount', v_amount, 'metadata', p_metadata),
    p_idempotency_key, 'server', NULL, NULL, o_event_id
  );

  INSERT INTO character_inventory_audit(event_id, character_id, item_instance_id, audit_type, amount, equipment_slot, idempotency_key, metadata)
  VALUES(o_event_id, v_character_id, o_item_instance_id, 'unequip', v_amount, v_slot, p_idempotency_key, p_metadata);

  COMMIT;
END$$

CREATE PROCEDURE mmo_transfer_character_item(
  IN p_session_id BINARY(16),
  IN p_item_instance_id BINARY(16),
  IN p_target_character_key VARCHAR(191),
  IN p_amount INT,
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_target_character_id BINARY(16),
  OUT o_amount_transferred INT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  DECLARE v_source_character_id BINARY(16);
  DECLARE v_source_amount INT DEFAULT 0;
  DECLARE v_amount INT DEFAULT 1;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SELECT event_id, target_character_id, amount
    INTO o_event_id, o_target_character_id, o_amount_transferred
    FROM character_inventory_audit
   WHERE idempotency_key = p_idempotency_key
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    LEAVE proc;
  END IF;

  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_source_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
   LIMIT 1;
  IF v_source_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_transfer_character_item: active session not found';
  END IF;

  SELECT c.character_id
    INTO o_target_character_id
    FROM characters c
   WHERE c.realm_id = v_realm_id
     AND c.character_key = p_target_character_key
   LIMIT 1;
  IF o_target_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_transfer_character_item: target character not found';
  END IF;

  SELECT ci.amount
    INTO v_source_amount
    FROM character_inventory ci
    JOIN item_instances ii ON ii.item_instance_id = ci.item_instance_id
   WHERE ci.character_id = v_source_character_id
     AND ci.item_instance_id = p_item_instance_id
     AND ii.realm_id = v_realm_id
     AND ii.owner_type = 'character'
     AND ii.owner_id = v_source_character_id
     AND ii.lifecycle_state = 'active'
   LIMIT 1;
  IF v_source_amount IS NULL OR v_source_amount <= 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_transfer_character_item: source inventory item not found';
  END IF;

  SET v_amount = GREATEST(1, LEAST(COALESCE(NULLIF(p_amount, 0), v_source_amount), v_source_amount));

  START TRANSACTION;

  DELETE FROM character_equipment
   WHERE character_id = v_source_character_id
     AND item_instance_id = p_item_instance_id;

  DELETE FROM character_inventory
   WHERE character_id = v_source_character_id
     AND item_instance_id = p_item_instance_id;

  INSERT INTO character_inventory(character_id, item_instance_id, bag_index, amount, source_amount, source_iterator_count)
  VALUES(o_target_character_id, p_item_instance_id, NULL, v_amount, v_amount, v_amount)
  ON DUPLICATE KEY UPDATE
    amount = character_inventory.amount + VALUES(amount),
    source_amount = VALUES(source_amount),
    source_iterator_count = VALUES(source_iterator_count);

  UPDATE item_instances
     SET owner_type = 'character',
         owner_id = o_target_character_id,
         quantity = v_amount,
         lifecycle_state = 'active',
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE item_instance_id = p_item_instance_id;

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_source_character_id,
    'character_inventory_transferred', 'inventory', p_server_tick,
    p_target_character_key, BIN_TO_UUID(p_item_instance_id, 1),
    JSON_OBJECT('item_instance_id', BIN_TO_UUID(p_item_instance_id, 1), 'target_character_key', p_target_character_key, 'target_character_id', BIN_TO_UUID(o_target_character_id, 1), 'amount', v_amount, 'metadata', p_metadata),
    p_idempotency_key, 'server', NULL, NULL, o_event_id
  );

  INSERT INTO character_inventory_audit(event_id, character_id, target_character_id, item_instance_id, audit_type, amount, idempotency_key, metadata)
  VALUES(o_event_id, v_source_character_id, o_target_character_id, p_item_instance_id, 'transfer', v_amount, p_idempotency_key, p_metadata);

  COMMIT;
  SET o_amount_transferred = v_amount;
END$$

DELIMITER ;

-- ============================================================================
-- END server/sql/step60_clean_db_equipment_bridge.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step67_interactive_use_bridge.sql
-- ============================================================================

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET collation_connection = utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_record_interactive_use;

DELIMITER ;;
CREATE PROCEDURE mmo_record_interactive_use(
  IN p_session_id BINARY(16),
  IN p_interactive_entity_key VARCHAR(512),
  IN p_state_after INT,
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_row_version_after BIGINT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_not_found BOOL DEFAULT FALSE;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;

  SET o_event_id = NULL;
  SET o_row_version_after = NULL;

  SET v_not_found = FALSE;
  SELECT event_id, row_version_after
    INTO o_event_id, o_row_version_after
    FROM world_interactive_audit
   WHERE idempotency_key = p_idempotency_key
   LIMIT 1;
  IF NOT v_not_found AND o_event_id IS NOT NULL THEN
    LEAVE proc;
  END IF;

  SET v_not_found = FALSE;
  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
   LIMIT 1;
  IF v_not_found OR v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_record_interactive_use: active session not found';
  END IF;

  SET v_not_found = FALSE;
  SELECT row_version
    INTO o_row_version_after
    FROM world_entity_state
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = p_interactive_entity_key
     AND entity_kind = 'interactive'
   LIMIT 1;
  IF v_not_found THEN
    SET o_row_version_after = NULL;
  END IF;

  START TRANSACTION;

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_instance_id,
    v_character_id,
    'interactive_used',
    'world_entity',
    p_server_tick,
    p_interactive_entity_key,
    NULL,
    JSON_OBJECT(
      'interactive_entity_key', p_interactive_entity_key,
      'state_after', p_state_after,
      'row_version_after', o_row_version_after,
      'metadata', COALESCE(p_metadata, JSON_OBJECT())
    ),
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    o_event_id
  );

  INSERT INTO world_interactive_audit(
    event_id,
    world_instance_id,
    character_id,
    entity_key,
    audit_type,
    state_after,
    row_version_after,
    idempotency_key,
    metadata
  )
  VALUES(
    o_event_id,
    v_world_instance_id,
    v_character_id,
    p_interactive_entity_key,
    'interactive_use',
    p_state_after,
    o_row_version_after,
    p_idempotency_key,
    COALESCE(p_metadata, JSON_OBJECT())
  )
  ON DUPLICATE KEY UPDATE
    event_id = VALUES(event_id),
    row_version_after = VALUES(row_version_after),
    metadata = VALUES(metadata);

  COMMIT;
END ;;
DELIMITER ;

-- ============================================================================
-- END server/sql/step67_interactive_use_bridge.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step68_drop_loot_inventory_bridge.sql
-- ============================================================================

-- Step68: server-authoritative drop/loot inventory bridge.
-- Additive/idempotent dev MMO procedures for the live outbox worker.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_drop_character_item;
DROP PROCEDURE IF EXISTS mmo_loot_npc_inventory;

DELIMITER ;;

CREATE PROCEDURE mmo_drop_character_item(
  IN p_session_id BINARY(16),
  IN p_item_instance_id BINARY(16),
  IN p_amount_requested INT,
  IN p_world_item_entity_key VARCHAR(512),
  IN p_pos_x DOUBLE,
  IN p_pos_y DOUBLE,
  IN p_pos_z DOUBLE,
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_amount_remaining INT,
  OUT o_amount_dropped INT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_item_template_id BINARY(16);
  DECLARE v_target_item_instance_id BINARY(16);
  DECLARE v_old_item_key VARCHAR(191);
  DECLARE v_new_item_key VARCHAR(191);
  DECLARE v_world_item_entity_key VARCHAR(191);
  DECLARE v_current_amount INT DEFAULT 0;
  DECLARE v_amount INT DEFAULT 1;
  DECLARE v_payload JSON;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET o_event_id = NULL;
  SET o_amount_remaining = 0;
  SET o_amount_dropped = 0;
  SET v_world_item_entity_key = LEFT(COALESCE(NULLIF(p_world_item_entity_key, ''), CONCAT('world_item:drop:', UUID())), 191);

  SELECT event_id, amount
    INTO o_event_id, o_amount_dropped
    FROM world_item_audit
   WHERE idempotency_key = p_idempotency_key
     AND audit_type = 'drop'
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    SELECT COALESCE(ci.amount, 0)
      INTO o_amount_remaining
      FROM character_inventory ci
     WHERE ci.item_instance_id = p_item_instance_id
     LIMIT 1;
    LEAVE proc;
  END IF;

  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_drop_character_item: active session not found';
  END IF;

  START TRANSACTION;

  SELECT ci.amount, ii.item_template_id, ii.item_instance_key
    INTO v_current_amount, v_item_template_id, v_old_item_key
    FROM character_inventory ci
    JOIN item_instances ii ON ii.item_instance_id = ci.item_instance_id
   WHERE ci.character_id = v_character_id
     AND ci.item_instance_id = p_item_instance_id
     AND ii.realm_id = v_realm_id
     AND ii.owner_type = 'character'
     AND ii.owner_id = v_character_id
     AND ii.lifecycle_state = 'active'
   LIMIT 1
   FOR UPDATE;
  IF v_current_amount IS NULL OR v_current_amount <= 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_drop_character_item: character item not found';
  END IF;

  SET v_amount = GREATEST(1, LEAST(COALESCE(NULLIF(p_amount_requested, 0), v_current_amount), v_current_amount));
  SET o_amount_remaining = v_current_amount - v_amount;
  SET o_amount_dropped = v_amount;

  IF o_amount_remaining = 0 THEN
    SET v_target_item_instance_id = p_item_instance_id;

    DELETE FROM character_equipment
     WHERE character_id = v_character_id
       AND item_instance_id = p_item_instance_id;

    DELETE FROM character_inventory
     WHERE character_id = v_character_id
       AND item_instance_id = p_item_instance_id;

    UPDATE item_instances
       SET owner_type = 'world_entity',
           owner_id = NULL,
           quantity = v_amount,
           lifecycle_state = 'active',
           raw_payload = JSON_MERGE_PATCH(
             COALESCE(raw_payload, JSON_OBJECT()),
             JSON_OBJECT(
               'entity_key', v_world_item_entity_key,
               'dropped_by_character', BIN_TO_UUID(v_character_id, 1),
               'dropped_at_tick', COALESCE(p_server_tick, 0)
             )
           ),
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE item_instance_id = p_item_instance_id;
  ELSE
    SET v_target_item_instance_id = UUID_TO_BIN(UUID(), 1);
    SET v_new_item_key = CONCAT(LEFT(v_old_item_key, 140), ':drop:', REPLACE(UUID(), '-', ''));

    UPDATE character_inventory
       SET amount = o_amount_remaining,
           source_amount = o_amount_remaining,
           source_iterator_count = o_amount_remaining,
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE character_id = v_character_id
       AND item_instance_id = p_item_instance_id;

    UPDATE item_instances
       SET quantity = o_amount_remaining,
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE item_instance_id = p_item_instance_id;

    INSERT INTO item_instances(
      item_instance_id, realm_id, item_template_id, item_instance_key,
      owner_type, owner_id, quantity, bind_state, lifecycle_state, raw_payload
    )
    SELECT
      v_target_item_instance_id, realm_id, item_template_id, v_new_item_key,
      'world_entity', NULL, v_amount, bind_state, 'active',
      JSON_MERGE_PATCH(
        COALESCE(raw_payload, JSON_OBJECT()),
        JSON_OBJECT(
          'entity_key', v_world_item_entity_key,
          'split_from_item_instance_id', BIN_TO_UUID(p_item_instance_id, 1),
          'dropped_by_character', BIN_TO_UUID(v_character_id, 1),
          'dropped_at_tick', COALESCE(p_server_tick, 0)
        )
      )
      FROM item_instances
     WHERE item_instance_id = p_item_instance_id;
  END IF;

  INSERT INTO world_entity_state(
    world_instance_id, entity_key, entity_kind, lifecycle_state,
    pos_x, pos_y, pos_z, state_json, row_version
  )
  VALUES(
    v_world_instance_id, v_world_item_entity_key, 'item', 'active',
    p_pos_x, p_pos_y, p_pos_z,
    JSON_OBJECT(
      'exists_in_world', true,
      'item_instance_id', BIN_TO_UUID(v_target_item_instance_id, 1),
      'source_item_instance_id', BIN_TO_UUID(p_item_instance_id, 1),
      'dropped_by_character', BIN_TO_UUID(v_character_id, 1),
      'dropped_at_tick', COALESCE(p_server_tick, 0)
    ),
    1
  )
  ON DUPLICATE KEY UPDATE
    entity_kind = 'item',
    lifecycle_state = 'active',
    pos_x = VALUES(pos_x),
    pos_y = VALUES(pos_y),
    pos_z = VALUES(pos_z),
    state_json = JSON_MERGE_PATCH(COALESCE(world_entity_state.state_json, JSON_OBJECT()), VALUES(state_json)),
    row_version = COALESCE(world_entity_state.row_version, 0) + 1,
    updated_at = CURRENT_TIMESTAMP(6);

  INSERT INTO world_inventory(world_instance_id, owner_entity_key, item_instance_id, amount, source_amount, source_iterator_count)
  VALUES(v_world_instance_id, v_world_item_entity_key, v_target_item_instance_id, v_amount, v_amount, v_amount)
  ON DUPLICATE KEY UPDATE
    amount = VALUES(amount),
    source_amount = VALUES(source_amount),
    source_iterator_count = VALUES(source_iterator_count),
    updated_at = CURRENT_TIMESTAMP(6);

  SET v_payload = JSON_OBJECT(
    'world_item_entity_key', v_world_item_entity_key,
    'source_item_instance_id', BIN_TO_UUID(p_item_instance_id, 1),
    'item_instance_id', BIN_TO_UUID(v_target_item_instance_id, 1),
    'amount_dropped', v_amount,
    'amount_remaining', o_amount_remaining,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'character_item_dropped', 'inventory', COALESCE(p_server_tick, 0),
    v_world_item_entity_key, BIN_TO_UUID(v_target_item_instance_id, 1),
    v_payload, LEFT(p_idempotency_key, 191), 'server', NULL, NULL, o_event_id
  );

  INSERT INTO world_item_audit(event_id, world_instance_id, character_id, entity_key, item_instance_id, audit_type, amount, idempotency_key, metadata)
  VALUES(o_event_id, v_world_instance_id, v_character_id, v_world_item_entity_key, v_target_item_instance_id, 'drop', v_amount, p_idempotency_key, p_metadata);

  COMMIT;
END ;;

CREATE PROCEDURE mmo_loot_npc_inventory(
  IN p_session_id BINARY(16),
  IN p_source_entity_key VARCHAR(512),
  IN p_item_instance_id BINARY(16),
  IN p_amount_requested INT,
  IN p_bag_index INT,
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_source_amount_remaining INT,
  OUT o_amount_looted INT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_item_template_id BINARY(16);
  DECLARE v_target_item_instance_id BINARY(16);
  DECLARE v_source_entity_key VARCHAR(191);
  DECLARE v_old_item_key VARCHAR(191);
  DECLARE v_new_item_key VARCHAR(191);
  DECLARE v_current_amount INT DEFAULT 0;
  DECLARE v_amount INT DEFAULT 1;
  DECLARE v_payload JSON;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET o_event_id = NULL;
  SET o_source_amount_remaining = 0;
  SET o_amount_looted = 0;
  SET v_source_entity_key = LEFT(COALESCE(NULLIF(p_source_entity_key, ''), 'unknown:npc'), 191);

  SELECT event_id, amount
    INTO o_event_id, o_amount_looted
    FROM world_item_audit
   WHERE idempotency_key = p_idempotency_key
     AND audit_type = 'loot_npc_inventory'
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    SELECT COALESCE(wi.amount, 0)
      INTO o_source_amount_remaining
      FROM world_inventory wi
     WHERE wi.item_instance_id = p_item_instance_id
       AND wi.owner_entity_key = v_source_entity_key
     LIMIT 1;
    LEAVE proc;
  END IF;

  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_loot_npc_inventory: active session not found';
  END IF;

  START TRANSACTION;

  SELECT wi.amount, ii.item_template_id, ii.item_instance_key
    INTO v_current_amount, v_item_template_id, v_old_item_key
    FROM world_inventory wi
    JOIN item_instances ii ON ii.item_instance_id = wi.item_instance_id
   WHERE wi.world_instance_id = v_world_instance_id
     AND wi.owner_entity_key = v_source_entity_key
     AND wi.item_instance_id = p_item_instance_id
     AND ii.realm_id = v_realm_id
     AND ii.lifecycle_state = 'active'
   LIMIT 1
   FOR UPDATE;
  IF v_current_amount IS NULL OR v_current_amount <= 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_loot_npc_inventory: source inventory item not found';
  END IF;

  SET v_amount = GREATEST(1, LEAST(COALESCE(NULLIF(p_amount_requested, 0), v_current_amount), v_current_amount));
  SET o_source_amount_remaining = v_current_amount - v_amount;
  SET o_amount_looted = v_amount;

  IF o_source_amount_remaining = 0 THEN
    SET v_target_item_instance_id = p_item_instance_id;

    DELETE FROM world_inventory
     WHERE world_instance_id = v_world_instance_id
       AND owner_entity_key = v_source_entity_key
       AND item_instance_id = p_item_instance_id;

    UPDATE item_instances
       SET owner_type = 'character',
           owner_id = v_character_id,
           quantity = v_amount,
           lifecycle_state = 'active',
           raw_payload = JSON_MERGE_PATCH(
             COALESCE(raw_payload, JSON_OBJECT()),
             JSON_OBJECT(
               'looted_from_entity_key', v_source_entity_key,
               'looted_by_character', BIN_TO_UUID(v_character_id, 1),
               'looted_at_tick', COALESCE(p_server_tick, 0)
             )
           ),
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE item_instance_id = p_item_instance_id;
  ELSE
    SET v_target_item_instance_id = UUID_TO_BIN(UUID(), 1);
    SET v_new_item_key = CONCAT(LEFT(v_old_item_key, 140), ':loot:', REPLACE(UUID(), '-', ''));

    UPDATE world_inventory
       SET amount = o_source_amount_remaining,
           source_amount = o_source_amount_remaining,
           source_iterator_count = o_source_amount_remaining,
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE world_instance_id = v_world_instance_id
       AND owner_entity_key = v_source_entity_key
       AND item_instance_id = p_item_instance_id;

    UPDATE item_instances
       SET quantity = o_source_amount_remaining,
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE item_instance_id = p_item_instance_id;

    INSERT INTO item_instances(
      item_instance_id, realm_id, item_template_id, item_instance_key,
      owner_type, owner_id, quantity, bind_state, lifecycle_state, raw_payload
    )
    SELECT
      v_target_item_instance_id, realm_id, item_template_id, v_new_item_key,
      'character', v_character_id, v_amount, bind_state, 'active',
      JSON_MERGE_PATCH(
        COALESCE(raw_payload, JSON_OBJECT()),
        JSON_OBJECT(
          'split_from_item_instance_id', BIN_TO_UUID(p_item_instance_id, 1),
          'looted_from_entity_key', v_source_entity_key,
          'looted_by_character', BIN_TO_UUID(v_character_id, 1),
          'looted_at_tick', COALESCE(p_server_tick, 0)
        )
      )
      FROM item_instances
     WHERE item_instance_id = p_item_instance_id;
  END IF;

  INSERT INTO character_inventory(character_id, item_instance_id, bag_index, amount, source_amount, source_iterator_count)
  VALUES(v_character_id, v_target_item_instance_id, p_bag_index, v_amount, v_amount, v_amount);

  SET v_payload = JSON_OBJECT(
    'source_entity_key', v_source_entity_key,
    'source_item_instance_id', BIN_TO_UUID(p_item_instance_id, 1),
    'item_instance_id', BIN_TO_UUID(v_target_item_instance_id, 1),
    'amount_looted', v_amount,
    'source_amount_remaining', o_source_amount_remaining,
    'bag_index', p_bag_index,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'npc_inventory_looted', 'inventory', COALESCE(p_server_tick, 0),
    v_source_entity_key, BIN_TO_UUID(v_target_item_instance_id, 1),
    v_payload, LEFT(p_idempotency_key, 191), 'server', NULL, NULL, o_event_id
  );

  INSERT INTO world_item_audit(event_id, world_instance_id, character_id, entity_key, item_instance_id, audit_type, amount, idempotency_key, metadata)
  VALUES(o_event_id, v_world_instance_id, v_character_id, v_source_entity_key, v_target_item_instance_id, 'loot_npc_inventory', v_amount, p_idempotency_key, p_metadata);

  COMMIT;
END ;;

DELIMITER ;

-- ============================================================================
-- END server/sql/step68_drop_loot_inventory_bridge.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step83_combat_lifecycle_bridge.sql
-- ============================================================================

-- Step83 combat/lifecycle bridge for clean MySQL dev DBs.
-- Adds minimal journal-backed procedures used by the C++ ASIO server direct DB path.

DELIMITER $$

DROP PROCEDURE IF EXISTS mmo_apply_character_damage $$
CREATE PROCEDURE mmo_apply_character_damage(
  IN p_session_id BINARY(16),
  IN p_target_character_key VARCHAR(191),
  IN p_damage_amount INT,
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_health_after INT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_actor_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_target_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_health_before INT DEFAULT NULL;
  DECLARE v_health_max INT DEFAULT NULL;
  DECLARE v_damage INT DEFAULT 0;
  DECLARE v_payload JSON;
  DECLARE v_not_found BOOL DEFAULT FALSE;

  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET o_event_id = NULL;
  SET o_health_after = NULL;

  SET v_not_found = FALSE;
  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_actor_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1;
  IF v_actor_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_apply_character_damage: active session not found';
  END IF;

  SET v_not_found = FALSE;
  SELECT c.character_id, c.character_key
    INTO v_target_character_id, v_character_key
    FROM characters c
   WHERE c.character_key = COALESCE(NULLIF(p_target_character_key, ''), 'PC_HERO')
   LIMIT 1;
  IF v_target_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_apply_character_damage: target character not found';
  END IF;

  SET v_not_found = FALSE;
  SELECT wej.event_id
    INTO o_event_id
    FROM world_event_journal wej
   WHERE wej.world_instance_id = v_world_instance_id
     AND wej.idempotency_key = LEFT(p_idempotency_key, 191)
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    SELECT cs.health_current INTO o_health_after
      FROM character_stats cs
     WHERE cs.character_id = v_target_character_id
     LIMIT 1;
    LEAVE proc;
  END IF;

  START TRANSACTION;

  SET v_not_found = FALSE;
  SELECT cs.health_current, cs.health_max
    INTO v_health_before, v_health_max
    FROM character_stats cs
   WHERE cs.character_id = v_target_character_id
   LIMIT 1
   FOR UPDATE;

  IF v_health_max IS NULL THEN
    SET v_health_max = GREATEST(0, COALESCE(v_health_before, 0));
  END IF;
  IF v_health_before IS NULL THEN
    SET v_health_before = v_health_max;
  END IF;

  SET v_damage = GREATEST(0, COALESCE(p_damage_amount, 0));
  SET o_health_after = GREATEST(0, v_health_before - v_damage);

  INSERT INTO character_stats(character_id, health_current, health_max)
  VALUES(v_target_character_id, o_health_after, GREATEST(v_health_max, o_health_after))
  ON DUPLICATE KEY UPDATE
    health_current = VALUES(health_current),
    health_max = GREATEST(character_stats.health_max, VALUES(health_max)),
    row_version = row_version + 1,
    updated_at = CURRENT_TIMESTAMP(6);

  SET v_payload = JSON_OBJECT(
    'target_character_key', v_character_key,
    'damage_amount', v_damage,
    'health_before', v_health_before,
    'health_after', o_health_after,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_actor_character_id,
    'character_damage_applied', 'combat', COALESCE(p_server_tick, 0),
    v_character_key, v_character_key, v_payload, LEFT(p_idempotency_key, 191),
    'server', NULL, NULL, o_event_id
  );

  COMMIT;
END $$

DROP PROCEDURE IF EXISTS mmo_apply_world_entity_damage $$
CREATE PROCEDURE mmo_apply_world_entity_damage(
  IN p_session_id BINARY(16),
  IN p_entity_key VARCHAR(512),
  IN p_damage_amount INT,
  IN p_fatal BOOL,
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_health_after INT,
  OUT o_row_version_after BIGINT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_entity_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_health_before INT DEFAULT NULL;
  DECLARE v_health_max INT DEFAULT NULL;
  DECLARE v_damage INT DEFAULT 0;
  DECLARE v_dead BOOL DEFAULT FALSE;
  DECLARE v_payload JSON;
  DECLARE v_not_found BOOL DEFAULT FALSE;

  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET o_event_id = NULL;
  SET o_health_after = NULL;
  SET o_row_version_after = NULL;
  SET v_entity_key = LEFT(COALESCE(NULLIF(p_entity_key, ''), 'unknown:npc'), 191);

  SET v_not_found = FALSE;
  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_apply_world_entity_damage: active session not found';
  END IF;

  SET v_not_found = FALSE;
  SELECT wej.event_id
    INTO o_event_id
    FROM world_event_journal wej
   WHERE wej.world_instance_id = v_world_instance_id
     AND wej.idempotency_key = LEFT(p_idempotency_key, 191)
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    SELECT wes.health_current, wes.row_version
      INTO o_health_after, o_row_version_after
      FROM world_entity_state wes
     WHERE wes.world_instance_id = v_world_instance_id
       AND wes.entity_key = v_entity_key
     LIMIT 1;
    LEAVE proc;
  END IF;

  START TRANSACTION;

  SET v_not_found = FALSE;
  SELECT wes.health_current, wes.health_max
    INTO v_health_before, v_health_max
    FROM world_entity_state wes
   WHERE wes.world_instance_id = v_world_instance_id
     AND wes.entity_key = v_entity_key
     AND wes.entity_kind IN ('npc', 'creature')
     AND wes.lifecycle_state = 'active'
   LIMIT 1
   FOR UPDATE;
  IF v_not_found THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_apply_world_entity_damage: active world entity not found';
  END IF;

  IF v_health_max IS NULL THEN
    SET v_health_max = GREATEST(0, COALESCE(v_health_before, 1));
  END IF;
  IF v_health_before IS NULL THEN
    SET v_health_before = v_health_max;
  END IF;

  SET v_damage = GREATEST(0, COALESCE(p_damage_amount, 0));
  SET o_health_after = GREATEST(0, v_health_before - v_damage);
  IF COALESCE(p_fatal, FALSE) THEN
    SET o_health_after = 0;
  END IF;
  SET v_dead = COALESCE(p_fatal, FALSE) OR o_health_after <= 0;

  UPDATE world_entity_state
     SET health_current = o_health_after,
         health_max = GREATEST(COALESCE(health_max, 0), GREATEST(v_health_max, o_health_after)),
         lifecycle_state = IF(v_dead, 'dead', lifecycle_state),
         row_version = row_version + 1,
         state_json = JSON_MERGE_PATCH(
           COALESCE(state_json, JSON_OBJECT()),
           JSON_OBJECT(
             'last_damage_amount', v_damage,
             'last_damage_tick', COALESCE(p_server_tick, 0),
             'last_damage_fatal', v_dead
           )
         ),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = v_entity_key;

  SELECT row_version INTO o_row_version_after
    FROM world_entity_state
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = v_entity_key
   LIMIT 1;

  SET v_payload = JSON_OBJECT(
    'entity_key', v_entity_key,
    'damage_amount', v_damage,
    'fatal', v_dead,
    'health_before', v_health_before,
    'health_after', o_health_after,
    'row_version_after', o_row_version_after,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'world_entity_damage_applied', 'combat', COALESCE(p_server_tick, 0),
    v_entity_key, v_entity_key, v_payload, LEFT(p_idempotency_key, 191),
    'server', NULL, NULL, o_event_id
  );

  COMMIT;
END $$

DROP PROCEDURE IF EXISTS mmo_mark_npc_dead $$
CREATE PROCEDURE mmo_mark_npc_dead(
  IN p_session_id BINARY(16),
  IN p_entity_key VARCHAR(512),
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_row_version_after BIGINT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_entity_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_lifecycle_state VARCHAR(32) DEFAULT NULL;
  DECLARE v_payload JSON;
  DECLARE v_not_found BOOL DEFAULT FALSE;

  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET o_event_id = NULL;
  SET o_row_version_after = NULL;
  SET v_entity_key = LEFT(COALESCE(NULLIF(p_entity_key, ''), 'unknown:npc'), 191);

  SET v_not_found = FALSE;
  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_mark_npc_dead: active session not found';
  END IF;

  SET v_not_found = FALSE;
  SELECT wej.event_id
    INTO o_event_id
    FROM world_event_journal wej
   WHERE wej.world_instance_id = v_world_instance_id
     AND wej.idempotency_key = LEFT(p_idempotency_key, 191)
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    SELECT wes.row_version INTO o_row_version_after
      FROM world_entity_state wes
     WHERE wes.world_instance_id = v_world_instance_id
       AND wes.entity_key = v_entity_key
     LIMIT 1;
    LEAVE proc;
  END IF;

  START TRANSACTION;

  SET v_not_found = FALSE;
  SELECT wes.lifecycle_state, wes.row_version
    INTO v_lifecycle_state, o_row_version_after
    FROM world_entity_state wes
   WHERE wes.world_instance_id = v_world_instance_id
     AND wes.entity_key = v_entity_key
     AND wes.entity_kind IN ('npc', 'creature')
   LIMIT 1
   FOR UPDATE;
  IF v_not_found THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_mark_npc_dead: world NPC entity not found';
  END IF;

  IF v_lifecycle_state <> 'active' THEN
    COMMIT;
    LEAVE proc;
  END IF;

  UPDATE world_entity_state
     SET lifecycle_state = 'dead',
         health_current = 0,
         row_version = row_version + 1,
         state_json = JSON_MERGE_PATCH(
           COALESCE(state_json, JSON_OBJECT()),
           JSON_OBJECT('dead_at_tick', COALESCE(p_server_tick, 0))
         ),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = v_entity_key;

  SELECT row_version INTO o_row_version_after
    FROM world_entity_state
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = v_entity_key
   LIMIT 1;

  SET v_payload = JSON_OBJECT(
    'entity_key', v_entity_key,
    'row_version_after', o_row_version_after,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'npc_marked_dead', 'combat', COALESCE(p_server_tick, 0),
    v_entity_key, v_entity_key, v_payload, LEFT(p_idempotency_key, 191),
    'server', NULL, NULL, o_event_id
  );

  COMMIT;
END $$

DELIMITER ;

-- ============================================================================
-- END server/sql/step83_combat_lifecycle_bridge.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step84_world_identity_lifecycle_bridge.sql
-- ============================================================================

-- Step84 world identity/lifecycle bridge for direct C++ ASIO server.
-- Adds a safe grant fallback for locally-created/unresolved world pickups.

DELIMITER $$

DROP PROCEDURE IF EXISTS mmo_grant_character_item_by_symbol $$
CREATE PROCEDURE mmo_grant_character_item_by_symbol(
  IN p_session_id BINARY(16),
  IN p_item_symbol INT,
  IN p_amount_requested INT,
  IN p_bag_index INT,
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_item_instance_id BINARY(16),
  OUT o_amount_granted INT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_content_revision_id BINARY(16) DEFAULT NULL;
  DECLARE v_item_template_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_item_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_amount INT DEFAULT 1;
  DECLARE v_bag_index INT DEFAULT NULL;
  DECLARE v_item_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_payload JSON;
  DECLARE v_not_found BOOL DEFAULT FALSE;

  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET o_event_id = NULL;
  SET o_item_instance_id = NULL;
  SET o_amount_granted = 0;

  SELECT cia.event_id, cia.item_instance_id, cia.amount
    INTO o_event_id, o_item_instance_id, o_amount_granted
    FROM character_inventory_audit cia
   WHERE cia.idempotency_key = LEFT(p_idempotency_key, 512)
     AND cia.audit_type = 'grant_unresolved_world_pickup'
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    LEAVE proc;
  END IF;

  SET v_not_found = FALSE;
  SELECT ss.realm_id, ss.world_instance_id, ss.character_id, c.character_key, rr.active_content_revision_id
    INTO v_realm_id, v_world_instance_id, v_character_id, v_character_key, v_content_revision_id
    FROM server_sessions ss
    JOIN characters c ON c.character_id = ss.character_id
    JOIN realm_realms rr ON rr.realm_id = ss.realm_id
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_grant_character_item_by_symbol: active session not found';
  END IF;

  IF COALESCE(p_item_symbol, -1) < 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_grant_character_item_by_symbol: invalid item symbol';
  END IF;

  SET v_not_found = FALSE;
  SELECT cit.item_template_id
    INTO v_item_template_id
    FROM content_item_templates cit
   WHERE cit.content_revision_id = v_content_revision_id
     AND cit.symbol_index = p_item_symbol
   ORDER BY cit.item_template_key
   LIMIT 1;
  IF v_item_template_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_grant_character_item_by_symbol: item template not found';
  END IF;

  SET v_amount = GREATEST(1, COALESCE(NULLIF(p_amount_requested, 0), 1));

  START TRANSACTION;

  SET v_not_found = FALSE;
  SELECT ii.item_instance_id
    INTO v_existing_item_instance_id
    FROM character_inventory ci
    JOIN item_instances ii ON ii.item_instance_id = ci.item_instance_id
   WHERE ci.character_id = v_character_id
     AND ii.item_template_id = v_item_template_id
     AND ii.owner_type = 'character'
     AND ii.owner_id = v_character_id
     AND ii.lifecycle_state = 'active'
   ORDER BY ci.updated_at DESC
   LIMIT 1
   FOR UPDATE;

  IF v_existing_item_instance_id IS NOT NULL THEN
    SET o_item_instance_id = v_existing_item_instance_id;

    UPDATE character_inventory
       SET amount = amount + v_amount,
           source_amount = COALESCE(source_amount, 0) + v_amount,
           source_iterator_count = COALESCE(source_iterator_count, 0) + v_amount,
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE character_id = v_character_id
       AND item_instance_id = o_item_instance_id;

    UPDATE item_instances
       SET quantity = quantity + v_amount,
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE item_instance_id = o_item_instance_id;
  ELSE
    SELECT COALESCE(MAX(ci.bag_index), -1) + 1
      INTO v_bag_index
      FROM character_inventory ci
     WHERE ci.character_id = v_character_id;

    IF p_bag_index IS NOT NULL AND p_bag_index >= 0 THEN
      IF NOT EXISTS (
        SELECT 1 FROM character_inventory ci
         WHERE ci.character_id = v_character_id
           AND ci.bag_index = p_bag_index
         LIMIT 1
      ) THEN
        SET v_bag_index = p_bag_index;
      END IF;
    END IF;

    SET o_item_instance_id = UUID_TO_BIN(UUID(), 1);
    SET v_item_key = LEFT(CONCAT('character_item:', v_character_key, ':sym:', p_item_symbol, ':grant:', REPLACE(UUID(), '-', '')), 191);

    INSERT INTO item_instances(
      item_instance_id, realm_id, item_template_id, item_instance_key,
      owner_type, owner_id, quantity, bind_state, lifecycle_state, raw_payload
    ) VALUES(
      o_item_instance_id, v_realm_id, v_item_template_id, v_item_key,
      'character', v_character_id, v_amount, 'unbound', 'active',
      JSON_OBJECT(
        'grant_reason', 'unresolved_world_pickup',
        'item_symbol', p_item_symbol,
        'granted_at_tick', COALESCE(p_server_tick, 0),
        'metadata', COALESCE(p_metadata, JSON_OBJECT())
      )
    );

    INSERT INTO character_inventory(character_id, item_instance_id, bag_index, amount, source_amount, source_iterator_count)
    VALUES(v_character_id, o_item_instance_id, v_bag_index, v_amount, v_amount, v_amount);
  END IF;

  SET v_payload = JSON_OBJECT(
    'item_symbol', p_item_symbol,
    'item_instance_id', BIN_TO_UUID(o_item_instance_id, 1),
    'amount', v_amount,
    'bag_index', COALESCE(v_bag_index, p_bag_index),
    'reason', 'unresolved_world_pickup',
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'character_item_granted_from_unresolved_world_pickup', 'inventory', COALESCE(p_server_tick, 0),
    v_character_key, BIN_TO_UUID(o_item_instance_id, 1), v_payload,
    LEFT(p_idempotency_key, 191), 'server', NULL, NULL, o_event_id
  );

  INSERT INTO character_inventory_audit(event_id, character_id, item_instance_id, audit_type, amount, idempotency_key, metadata)
  VALUES(o_event_id, v_character_id, o_item_instance_id, 'grant_unresolved_world_pickup', v_amount, LEFT(p_idempotency_key, 512), p_metadata);

  COMMIT;
  SET o_amount_granted = v_amount;
END $$

DELIMITER ;

-- ============================================================================
-- END server/sql/step84_world_identity_lifecycle_bridge.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step93_save_checkpoint_quest_utf8_bridge.sql
-- ============================================================================

-- Step93: quest UTF-8/idempotency bridge for server-bound save/checkpoint flow.
-- Additive patch. Replaces only mmo_update_character_quest and adds an audit table.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS character_quest_audit (
  audit_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) DEFAULT NULL,
  character_id BINARY(16) NOT NULL,
  quest_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  quest_name VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL,
  status VARCHAR(32) NOT NULL,
  entry_count INT NOT NULL DEFAULT 0,
  text_entries JSON NOT NULL,
  idempotency_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  metadata JSON DEFAULT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (audit_id),
  UNIQUE KEY ux_character_quest_audit_idem (idempotency_key),
  KEY ix_character_quest_audit_character (character_id, created_at),
  KEY ix_character_quest_audit_event (event_id),
  CONSTRAINT character_quest_audit_character_fk FOREIGN KEY (character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT character_quest_audit_entries_json_ck CHECK (JSON_VALID(text_entries)),
  CONSTRAINT character_quest_audit_metadata_json_ck CHECK ((metadata IS NULL) OR JSON_VALID(metadata)),
  CONSTRAINT character_quest_audit_status_ck CHECK (status IN ('running','success','failed','obsolete'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_update_character_quest;
DELIMITER ;;
CREATE PROCEDURE mmo_update_character_quest(
  IN p_session_id BINARY(16),
  IN p_quest_key VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci,
  IN p_quest_name VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci,
  IN p_status VARCHAR(64),
  IN p_entry_count INT,
  IN p_entries JSON,
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci,
  OUT p_event_id BINARY(16)
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(255) DEFAULT NULL;
  DECLARE v_status VARCHAR(32) DEFAULT 'running';
  DECLARE v_payload JSON DEFAULT JSON_OBJECT();
  DECLARE v_existing_event_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_type VARCHAR(128) DEFAULT NULL;
  DECLARE v_existing_class VARCHAR(32) DEFAULT NULL;
  DECLARE v_not_found BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;

  SET p_event_id = NULL;
  SET v_not_found = FALSE;

  SELECT s.realm_id, s.world_instance_id, s.character_id, c.character_key
    INTO v_realm_id, v_world_id, v_character_id, v_character_key
    FROM server_sessions s
    LEFT JOIN characters c ON c.character_id = s.character_id
   WHERE s.session_id = p_session_id
   LIMIT 1;

  IF v_not_found OR v_world_id IS NULL OR v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_update_character_quest: invalid session';
  END IF;
  IF COALESCE(p_quest_key, '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_update_character_quest: quest key is required';
  END IF;

  SET v_status = CASE COALESCE(NULLIF(p_status, ''), 'running')
    WHEN '1' THEN 'running'
    WHEN '2' THEN 'success'
    WHEN '3' THEN 'failed'
    WHEN '4' THEN 'obsolete'
    WHEN 'running' THEN 'running'
    WHEN 'success' THEN 'success'
    WHEN 'failed' THEN 'failed'
    WHEN 'obsolete' THEN 'obsolete'
    ELSE 'running'
  END;

  SET v_payload = JSON_MERGE_PATCH(
    COALESCE(p_metadata, JSON_OBJECT()),
    JSON_OBJECT(
      'character_key', COALESCE(v_character_key, 'PC_HERO'),
      'quest_key', p_quest_key,
      'quest_name', COALESCE(NULLIF(p_quest_name, ''), p_quest_key),
      'status', v_status,
      'entry_count', COALESCE(p_entry_count, 0),
      'entries', COALESCE(p_entries, JSON_ARRAY())
    )
  );

  SET v_not_found = FALSE;
  IF p_idempotency_key IS NOT NULL AND p_idempotency_key <> '' THEN
    SELECT event_id, event_type, event_class
      INTO v_existing_event_id, v_existing_type, v_existing_class
      FROM world_event_journal
     WHERE world_instance_id = v_world_id
       AND idempotency_key = p_idempotency_key
     LIMIT 1;
  END IF;

  IF v_existing_event_id IS NOT NULL THEN
    IF v_existing_type <> 'character_quest_updated' OR v_existing_class NOT IN ('quest','character') THEN
      SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT='mmo_update_character_quest: idempotency key reused with incompatible event';
    END IF;
    SET p_event_id = v_existing_event_id;
  ELSE
    CALL mmo_append_world_event(
      v_realm_id,
      v_world_id,
      v_character_id,
      'character_quest_updated',
      'quest',
      COALESCE(p_server_tick, 0),
      COALESCE(v_character_key, 'PC_HERO'),
      p_quest_key,
      v_payload,
      p_idempotency_key,
      'server',
      NULL,
      NULL,
      p_event_id
    );
  END IF;

  INSERT INTO character_quests(character_id, quest_key, section, status, entry_order, text_entries)
  VALUES(v_character_id, p_quest_key, '', v_status, COALESCE(p_entry_count, 0), COALESCE(p_entries, JSON_ARRAY()))
  ON DUPLICATE KEY UPDATE
    status = VALUES(status),
    entry_order = VALUES(entry_order),
    text_entries = VALUES(text_entries);

  INSERT INTO character_quest_audit(event_id, character_id, quest_key, quest_name, status, entry_count, text_entries, idempotency_key, metadata)
  VALUES(p_event_id, v_character_id, p_quest_key, COALESCE(NULLIF(p_quest_name, ''), p_quest_key), v_status, COALESCE(p_entry_count,0), COALESCE(p_entries, JSON_ARRAY()), COALESCE(p_idempotency_key, CONCAT('quest-audit:', UUID())), v_payload)
  ON DUPLICATE KEY UPDATE
    event_id = VALUES(event_id),
    quest_name = VALUES(quest_name),
    status = VALUES(status),
    entry_count = VALUES(entry_count),
    text_entries = VALUES(text_entries),
    metadata = VALUES(metadata);
END ;;
DELIMITER ;

-- ============================================================================
-- END server/sql/step93_save_checkpoint_quest_utf8_bridge.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step94_server_save_checkpoint_manifest.sql
-- ============================================================================

-- Step94: durable server-side save/checkpoint manifest.
-- This records a compact manifest that tells the server/client which DB projections
-- were visible when the native save completed in server-bound mode.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_manifests (
  manifest_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(),1)),
  event_id BINARY(16) DEFAULT NULL,
  realm_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  character_id BINARY(16) NOT NULL,
  manifest_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  checkpoint_kind VARCHAR(64) NOT NULL DEFAULT 'native_save',
  reason VARCHAR(128) NOT NULL DEFAULT 'save_checkpoint_manifest',
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  latest_checkpoint_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  recent_event_seq BIGINT UNSIGNED NOT NULL DEFAULT 0,
  inventory_rows INT UNSIGNED NOT NULL DEFAULT 0,
  equipment_rows INT UNSIGNED NOT NULL DEFAULT 0,
  quest_rows INT UNSIGNED NOT NULL DEFAULT 0,
  known_dialog_rows INT UNSIGNED NOT NULL DEFAULT 0,
  script_state_rows INT UNSIGNED NOT NULL DEFAULT 0,
  world_item_rows INT UNSIGNED NOT NULL DEFAULT 0,
  world_inventory_rows INT UNSIGNED NOT NULL DEFAULT 0,
  interactive_rows INT UNSIGNED NOT NULL DEFAULT 0,
  npc_lifecycle_rows INT UNSIGNED NOT NULL DEFAULT 0,
  mover_rows INT UNSIGNED NOT NULL DEFAULT 0,
  metadata JSON NOT NULL,
  idempotency_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id),
  UNIQUE KEY ux_mmo_save_checkpoint_manifest_idem (world_instance_id, idempotency_key),
  KEY ix_mmo_save_checkpoint_manifest_character (character_id, created_at),
  KEY ix_mmo_save_checkpoint_manifest_world_tick (world_instance_id, server_tick),
  CONSTRAINT mmo_save_checkpoint_manifest_event_fk FOREIGN KEY (event_id) REFERENCES world_event_journal(event_id) ON DELETE SET NULL,
  CONSTRAINT mmo_save_checkpoint_manifest_realm_fk FOREIGN KEY (realm_id) REFERENCES realm_realms(realm_id) ON DELETE RESTRICT,
  CONSTRAINT mmo_save_checkpoint_manifest_world_fk FOREIGN KEY (world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT mmo_save_checkpoint_manifest_character_fk FOREIGN KEY (character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT mmo_save_checkpoint_manifest_metadata_json_ck CHECK (JSON_VALID(metadata)),
  CONSTRAINT mmo_save_checkpoint_manifest_kind_ck CHECK (checkpoint_kind IN ('native_save','server_checkpoint','debug','admin'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_create_save_checkpoint_manifest;
DELIMITER ;;
CREATE PROCEDURE mmo_create_save_checkpoint_manifest(
  IN p_session_id BINARY(16),
  IN p_manifest_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci,
  IN p_checkpoint_kind VARCHAR(64),
  IN p_reason VARCHAR(128),
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci,
  OUT p_manifest_id BINARY(16),
  OUT p_event_id BINARY(16),
  OUT p_row_version_after BIGINT UNSIGNED
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_manifest_id BINARY(16) DEFAULT NULL;
  DECLARE v_checkpoint_kind VARCHAR(64) DEFAULT 'native_save';
  DECLARE v_reason VARCHAR(128) DEFAULT 'save_checkpoint_manifest';
  DECLARE v_latest_checkpoint_tick BIGINT UNSIGNED DEFAULT 0;
  DECLARE v_recent_event_seq BIGINT UNSIGNED DEFAULT 0;
  DECLARE v_inventory_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_equipment_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_quest_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_known_dialog_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_script_state_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_world_item_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_world_inventory_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_interactive_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_npc_lifecycle_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_mover_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_payload JSON DEFAULT JSON_OBJECT();
  DECLARE v_not_found BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;

  SET p_manifest_id = NULL;
  SET p_event_id = NULL;
  SET p_row_version_after = NULL;
  SET v_not_found = FALSE;

  SELECT s.realm_id, s.world_instance_id, s.character_id, c.character_key
    INTO v_realm_id, v_world_id, v_character_id, v_character_key
    FROM server_sessions s
    LEFT JOIN characters c ON c.character_id = s.character_id
   WHERE s.session_id = p_session_id
   LIMIT 1;

  IF v_not_found OR v_world_id IS NULL OR v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_create_save_checkpoint_manifest: invalid session';
  END IF;

  SET v_checkpoint_kind = COALESCE(NULLIF(p_checkpoint_kind,''), 'native_save');
  IF v_checkpoint_kind NOT IN ('native_save','server_checkpoint','debug','admin') THEN
    SET v_checkpoint_kind = 'native_save';
  END IF;
  SET v_reason = COALESCE(NULLIF(p_reason,''), 'save_checkpoint_manifest');

  SELECT COALESCE(MAX(server_tick),0) INTO v_latest_checkpoint_tick FROM character_checkpoint_audit WHERE character_id = v_character_id;
  SELECT COALESCE(MAX(event_seq),0) INTO v_recent_event_seq FROM world_event_journal WHERE world_instance_id = v_world_id;
  SELECT COUNT(*) INTO v_inventory_rows FROM character_inventory WHERE character_id = v_character_id;
  SELECT COUNT(*) INTO v_equipment_rows FROM character_equipment WHERE character_id = v_character_id;
  SELECT COUNT(*) INTO v_quest_rows FROM character_quests WHERE character_id = v_character_id;
  SELECT COUNT(*) INTO v_known_dialog_rows FROM character_known_dialogs WHERE character_id = v_character_id;
  SELECT COUNT(*) INTO v_script_state_rows FROM character_script_state WHERE character_id = v_character_id;
  SELECT COUNT(*) INTO v_world_item_rows FROM world_entity_state WHERE world_instance_id = v_world_id AND entity_kind = 'item';
  SELECT COUNT(*) INTO v_world_inventory_rows FROM world_inventory WHERE world_instance_id = v_world_id;
  SELECT COUNT(*) INTO v_interactive_rows FROM world_entity_state WHERE world_instance_id = v_world_id AND entity_kind = 'interactive';
  SELECT COUNT(*) INTO v_npc_lifecycle_rows FROM world_entity_state WHERE world_instance_id = v_world_id AND entity_kind IN ('npc','creature') AND (lifecycle_state <> 'active' OR (health_current IS NOT NULL AND health_max IS NOT NULL AND health_current < health_max));
  SELECT COUNT(*) INTO v_mover_rows FROM mmo_world_mover_state_current WHERE world_instance_id = v_world_id;

  SET v_payload = JSON_MERGE_PATCH(
    COALESCE(p_metadata, JSON_OBJECT()),
    JSON_OBJECT(
      'character_key', COALESCE(v_character_key, 'PC_HERO'),
      'manifest_key', COALESCE(NULLIF(p_manifest_key,''), CONCAT('character:', COALESCE(v_character_key, 'PC_HERO'), ':save-checkpoint')),
      'checkpoint_kind', v_checkpoint_kind,
      'reason', v_reason,
      'latest_checkpoint_tick', v_latest_checkpoint_tick,
      'recent_event_seq', v_recent_event_seq,
      'inventory_rows', v_inventory_rows,
      'equipment_rows', v_equipment_rows,
      'quest_rows', v_quest_rows,
      'known_dialog_rows', v_known_dialog_rows,
      'script_state_rows', v_script_state_rows,
      'world_item_rows', v_world_item_rows,
      'world_inventory_rows', v_world_inventory_rows,
      'interactive_rows', v_interactive_rows,
      'npc_lifecycle_rows', v_npc_lifecycle_rows,
      'mover_rows', v_mover_rows
    )
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_id,
    v_character_id,
    'server_save_checkpoint_manifest_created',
    'system',
    COALESCE(p_server_tick,0),
    COALESCE(NULLIF(p_manifest_key,''), CONCAT('character:', COALESCE(v_character_key, 'PC_HERO'), ':save-checkpoint')),
    COALESCE(v_character_key, 'PC_HERO'),
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  SELECT manifest_id
    INTO v_manifest_id
    FROM mmo_save_checkpoint_manifests
   WHERE world_instance_id = v_world_id
     AND idempotency_key = p_idempotency_key
   LIMIT 1;

  IF v_manifest_id IS NULL THEN
    SET v_manifest_id = UUID_TO_BIN(UUID(), 1);
    INSERT INTO mmo_save_checkpoint_manifests(
      manifest_id, event_id, realm_id, world_instance_id, character_id, manifest_key,
      checkpoint_kind, reason, server_tick, latest_checkpoint_tick, recent_event_seq,
      inventory_rows, equipment_rows, quest_rows, known_dialog_rows, script_state_rows,
      world_item_rows, world_inventory_rows, interactive_rows, npc_lifecycle_rows, mover_rows,
      metadata, idempotency_key, row_version
    ) VALUES (
      v_manifest_id, p_event_id, v_realm_id, v_world_id, v_character_id,
      COALESCE(NULLIF(p_manifest_key,''), CONCAT('character:', COALESCE(v_character_key, 'PC_HERO'), ':save-checkpoint')),
      v_checkpoint_kind, v_reason, COALESCE(p_server_tick,0), v_latest_checkpoint_tick, v_recent_event_seq,
      v_inventory_rows, v_equipment_rows, v_quest_rows, v_known_dialog_rows, v_script_state_rows,
      v_world_item_rows, v_world_inventory_rows, v_interactive_rows, v_npc_lifecycle_rows, v_mover_rows,
      v_payload, COALESCE(p_idempotency_key, CONCAT('save-checkpoint:', UUID())), 1
    );
  ELSE
    UPDATE mmo_save_checkpoint_manifests
       SET event_id = p_event_id,
           latest_checkpoint_tick = v_latest_checkpoint_tick,
           recent_event_seq = v_recent_event_seq,
           inventory_rows = v_inventory_rows,
           equipment_rows = v_equipment_rows,
           quest_rows = v_quest_rows,
           known_dialog_rows = v_known_dialog_rows,
           script_state_rows = v_script_state_rows,
           world_item_rows = v_world_item_rows,
           world_inventory_rows = v_world_inventory_rows,
           interactive_rows = v_interactive_rows,
           npc_lifecycle_rows = v_npc_lifecycle_rows,
           mover_rows = v_mover_rows,
           metadata = v_payload,
           row_version = row_version + 1
     WHERE manifest_id = v_manifest_id;
  END IF;

  SELECT row_version INTO p_row_version_after FROM mmo_save_checkpoint_manifests WHERE manifest_id = v_manifest_id;
  SET p_manifest_id = v_manifest_id;
END ;;
DELIMITER ;

-- ============================================================================
-- END server/sql/step94_server_save_checkpoint_manifest.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step95_save_slot_catalog_db_continue_bridge.sql
-- ============================================================================

-- Step95: DB-backed save metadata required by DB save checkpoints.
-- This is deliberately minimal; real save content is materialized by Step96+.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_step95_add_save_manifest_columns;
DELIMITER ;;
CREATE PROCEDURE mmo_step95_add_save_manifest_columns()
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns
     WHERE table_schema = DATABASE()
       AND table_name = 'mmo_save_checkpoint_manifests'
       AND column_name = 'save_slot_key'
  ) THEN
    ALTER TABLE mmo_save_checkpoint_manifests
      ADD COLUMN save_slot_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL AFTER manifest_key;
  END IF;

  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns
     WHERE table_schema = DATABASE()
       AND table_name = 'mmo_save_checkpoint_manifests'
       AND column_name = 'native_save_path'
  ) THEN
    ALTER TABLE mmo_save_checkpoint_manifests
      ADD COLUMN native_save_path VARCHAR(512) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL AFTER save_slot_key;
  END IF;

  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns
     WHERE table_schema = DATABASE()
       AND table_name = 'mmo_save_checkpoint_manifests'
       AND column_name = 'display_name'
  ) THEN
    ALTER TABLE mmo_save_checkpoint_manifests
      ADD COLUMN display_name VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL AFTER native_save_path;
  END IF;

  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns
     WHERE table_schema = DATABASE()
       AND table_name = 'mmo_save_checkpoint_manifests'
       AND column_name = 'client_world_name'
  ) THEN
    ALTER TABLE mmo_save_checkpoint_manifests
      ADD COLUMN client_world_name VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL AFTER display_name;
  END IF;

  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns
     WHERE table_schema = DATABASE()
       AND table_name = 'mmo_save_checkpoint_manifests'
       AND column_name = 'native_save_present'
  ) THEN
    ALTER TABLE mmo_save_checkpoint_manifests
      ADD COLUMN native_save_present TINYINT(1) NOT NULL DEFAULT 0 AFTER client_world_name;
  END IF;
END ;;
DELIMITER ;
CALL mmo_step95_add_save_manifest_columns();
DROP PROCEDURE IF EXISTS mmo_step95_add_save_manifest_columns;

DROP PROCEDURE IF EXISTS mmo_create_save_checkpoint_manifest;
DELIMITER ;;
CREATE PROCEDURE mmo_create_save_checkpoint_manifest(
  IN p_session_id BINARY(16),
  IN p_manifest_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci,
  IN p_checkpoint_kind VARCHAR(64),
  IN p_reason VARCHAR(128),
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci,
  OUT p_manifest_id BINARY(16),
  OUT p_event_id BINARY(16),
  OUT p_row_version_after BIGINT UNSIGNED
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_manifest_id BINARY(16) DEFAULT NULL;
  DECLARE v_manifest_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL;
  DECLARE v_save_slot_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL;
  DECLARE v_native_save_path VARCHAR(512) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL;
  DECLARE v_display_name VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL;
  DECLARE v_client_world_name VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL;
  DECLARE v_native_save_present TINYINT(1) DEFAULT 0;
  DECLARE v_checkpoint_kind VARCHAR(64) DEFAULT 'native_save';
  DECLARE v_reason VARCHAR(128) DEFAULT 'save_checkpoint_manifest';
  DECLARE v_latest_checkpoint_tick BIGINT UNSIGNED DEFAULT 0;
  DECLARE v_recent_event_seq BIGINT UNSIGNED DEFAULT 0;
  DECLARE v_inventory_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_equipment_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_quest_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_known_dialog_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_script_state_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_world_item_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_world_inventory_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_interactive_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_npc_lifecycle_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_mover_rows INT UNSIGNED DEFAULT 0;
  DECLARE v_payload JSON DEFAULT JSON_OBJECT();
  DECLARE v_not_found BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;

  SET p_manifest_id = NULL;
  SET p_event_id = NULL;
  SET p_row_version_after = NULL;
  SET v_not_found = FALSE;

  SELECT s.realm_id, s.world_instance_id, s.character_id, c.character_key
    INTO v_realm_id, v_world_id, v_character_id, v_character_key
    FROM server_sessions s
    LEFT JOIN characters c ON c.character_id = s.character_id
   WHERE s.session_id = p_session_id
   LIMIT 1;

  IF v_not_found OR v_world_id IS NULL OR v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_create_save_checkpoint_manifest: invalid session';
  END IF;

  SET v_checkpoint_kind = COALESCE(NULLIF(p_checkpoint_kind,''), 'native_save');
  IF v_checkpoint_kind NOT IN ('native_save','server_checkpoint','debug','admin') THEN
    SET v_checkpoint_kind = 'native_save';
  END IF;
  SET v_reason = COALESCE(NULLIF(p_reason,''), 'save_checkpoint_manifest');
  SET v_manifest_key = COALESCE(NULLIF(p_manifest_key,''), CONCAT('character:', COALESCE(v_character_key, 'PC_HERO'), ':save-checkpoint'));

  SET v_save_slot_key = COALESCE(
    NULLIF(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.save_slot_key')), ''),
    NULLIF(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.slot_path')), ''),
    NULLIF(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.native_save_path')), ''),
    v_manifest_key
  );
  SET v_native_save_path = COALESCE(
    NULLIF(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.native_save_path')), ''),
    NULLIF(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.slot_path')), ''),
    v_save_slot_key
  );
  SET v_display_name = COALESCE(
    NULLIF(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.display_name')), ''),
    NULLIF(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.slot_display_name')), ''),
    NULLIF(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.slot_name')), ''),
    v_save_slot_key
  );
  SET v_client_world_name = COALESCE(
    NULLIF(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.client_world_name')), ''),
    NULLIF(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.world')), '')
  );
  SET v_native_save_present = CASE LOWER(COALESCE(JSON_UNQUOTE(JSON_EXTRACT(COALESCE(p_metadata, JSON_OBJECT()), '$.native_save_present')), 'false'))
    WHEN 'true' THEN 1
    WHEN '1' THEN 1
    ELSE 0
  END;

  SELECT COALESCE(MAX(server_tick),0) INTO v_latest_checkpoint_tick FROM character_checkpoint_audit WHERE character_id = v_character_id;
  SELECT COALESCE(MAX(event_seq),0) INTO v_recent_event_seq FROM world_event_journal WHERE world_instance_id = v_world_id;
  SELECT COUNT(*) INTO v_inventory_rows FROM character_inventory WHERE character_id = v_character_id;
  SELECT COUNT(*) INTO v_equipment_rows FROM character_equipment WHERE character_id = v_character_id;
  SELECT COUNT(*) INTO v_quest_rows FROM character_quests WHERE character_id = v_character_id;
  SELECT COUNT(*) INTO v_known_dialog_rows FROM character_known_dialogs WHERE character_id = v_character_id;
  SELECT COUNT(*) INTO v_script_state_rows FROM character_script_state WHERE character_id = v_character_id;
  SELECT COUNT(*) INTO v_world_item_rows FROM world_entity_state WHERE world_instance_id = v_world_id AND entity_kind = 'item';
  SELECT COUNT(*) INTO v_world_inventory_rows FROM world_inventory WHERE world_instance_id = v_world_id;
  SELECT COUNT(*) INTO v_interactive_rows FROM world_entity_state WHERE world_instance_id = v_world_id AND entity_kind = 'interactive';
  SELECT COUNT(*) INTO v_npc_lifecycle_rows FROM world_entity_state WHERE world_instance_id = v_world_id AND entity_kind IN ('npc','creature') AND (lifecycle_state <> 'active' OR (health_current IS NOT NULL AND health_max IS NOT NULL AND health_current < health_max));
  SELECT COUNT(*) INTO v_mover_rows FROM mmo_world_mover_state_current WHERE world_instance_id = v_world_id;

  SET v_payload = JSON_MERGE_PATCH(
    COALESCE(p_metadata, JSON_OBJECT()),
    JSON_OBJECT(
      'character_key', COALESCE(v_character_key, 'PC_HERO'),
      'manifest_key', v_manifest_key,
      'save_slot_key', v_save_slot_key,
      'native_save_path', v_native_save_path,
      'display_name', v_display_name,
      'client_world_name', v_client_world_name,
      'native_save_present', JSON_EXTRACT(IF(v_native_save_present<>0,'true','false'),'$'),
      'checkpoint_kind', v_checkpoint_kind,
      'reason', v_reason,
      'latest_checkpoint_tick', v_latest_checkpoint_tick,
      'recent_event_seq', v_recent_event_seq,
      'inventory_rows', v_inventory_rows,
      'equipment_rows', v_equipment_rows,
      'quest_rows', v_quest_rows,
      'known_dialog_rows', v_known_dialog_rows,
      'script_state_rows', v_script_state_rows,
      'world_item_rows', v_world_item_rows,
      'world_inventory_rows', v_world_inventory_rows,
      'interactive_rows', v_interactive_rows,
      'npc_lifecycle_rows', v_npc_lifecycle_rows,
      'mover_rows', v_mover_rows
    )
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_id,
    v_character_id,
    'server_save_checkpoint_manifest_created',
    'system',
    COALESCE(p_server_tick,0),
    v_manifest_key,
    COALESCE(v_character_key, 'PC_HERO'),
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  SELECT manifest_id
    INTO v_manifest_id
    FROM mmo_save_checkpoint_manifests
   WHERE world_instance_id = v_world_id
     AND idempotency_key = p_idempotency_key
   LIMIT 1;

  IF v_manifest_id IS NULL THEN
    SET v_manifest_id = UUID_TO_BIN(UUID(), 1);
    INSERT INTO mmo_save_checkpoint_manifests(
      manifest_id, event_id, realm_id, world_instance_id, character_id, manifest_key,
      save_slot_key, native_save_path, display_name, client_world_name, native_save_present,
      checkpoint_kind, reason, server_tick, latest_checkpoint_tick, recent_event_seq,
      inventory_rows, equipment_rows, quest_rows, known_dialog_rows, script_state_rows,
      world_item_rows, world_inventory_rows, interactive_rows, npc_lifecycle_rows, mover_rows,
      metadata, idempotency_key, row_version
    ) VALUES (
      v_manifest_id, p_event_id, v_realm_id, v_world_id, v_character_id, v_manifest_key,
      v_save_slot_key, v_native_save_path, v_display_name, v_client_world_name, v_native_save_present,
      v_checkpoint_kind, v_reason, COALESCE(p_server_tick,0), v_latest_checkpoint_tick, v_recent_event_seq,
      v_inventory_rows, v_equipment_rows, v_quest_rows, v_known_dialog_rows, v_script_state_rows,
      v_world_item_rows, v_world_inventory_rows, v_interactive_rows, v_npc_lifecycle_rows, v_mover_rows,
      v_payload, COALESCE(p_idempotency_key, CONCAT('save-checkpoint:', UUID())), 1
    );
  ELSE
    UPDATE mmo_save_checkpoint_manifests
       SET event_id = p_event_id,
           save_slot_key = v_save_slot_key,
           native_save_path = v_native_save_path,
           display_name = v_display_name,
           client_world_name = v_client_world_name,
           native_save_present = v_native_save_present,
           checkpoint_kind = v_checkpoint_kind,
           reason = v_reason,
           server_tick = COALESCE(p_server_tick,0),
           latest_checkpoint_tick = v_latest_checkpoint_tick,
           recent_event_seq = v_recent_event_seq,
           inventory_rows = v_inventory_rows,
           equipment_rows = v_equipment_rows,
           quest_rows = v_quest_rows,
           known_dialog_rows = v_known_dialog_rows,
           script_state_rows = v_script_state_rows,
           world_item_rows = v_world_item_rows,
           world_inventory_rows = v_world_inventory_rows,
           interactive_rows = v_interactive_rows,
           npc_lifecycle_rows = v_npc_lifecycle_rows,
           mover_rows = v_mover_rows,
           metadata = v_payload,
           row_version = row_version + 1
     WHERE manifest_id = v_manifest_id;
  END IF;

  SELECT row_version INTO p_row_version_after FROM mmo_save_checkpoint_manifests WHERE manifest_id = v_manifest_id;
  SET p_manifest_id = v_manifest_id;
END ;;
DELIMITER ;

CREATE OR REPLACE VIEW v_mmo_latest_save_checkpoint_manifests AS
SELECT *
FROM (
  SELECT
    BIN_TO_UUID(sm.manifest_id,1) AS manifest_uuid,
    BIN_TO_UUID(sm.event_id,1) AS event_uuid,
    c.character_key,
    c.character_name,
    cwt.world_name,
    rwi.world_instance_key,
    sm.manifest_key,
    sm.save_slot_key,
    sm.native_save_path,
    sm.display_name,
    sm.client_world_name,
    sm.native_save_present,
    sm.checkpoint_kind,
    sm.reason,
    sm.server_tick,
    sm.latest_checkpoint_tick,
    sm.recent_event_seq,
    sm.inventory_rows,
    sm.equipment_rows,
    sm.quest_rows,
    sm.known_dialog_rows,
    sm.script_state_rows,
    sm.world_item_rows,
    sm.world_inventory_rows,
    sm.interactive_rows,
    sm.npc_lifecycle_rows,
    sm.mover_rows,
    sm.row_version,
    sm.created_at,
    sm.updated_at,
    ROW_NUMBER() OVER (PARTITION BY sm.character_id, COALESCE(sm.save_slot_key, sm.manifest_key) ORDER BY sm.created_at DESC, sm.row_version DESC) AS save_slot_rank,
    ROW_NUMBER() OVER (PARTITION BY sm.character_id ORDER BY sm.created_at DESC, sm.row_version DESC) AS character_rank
  FROM mmo_save_checkpoint_manifests sm
  JOIN characters c ON c.character_id = sm.character_id
  JOIN realm_world_instances rwi ON rwi.world_instance_id = sm.world_instance_id
  LEFT JOIN content_world_templates cwt ON cwt.world_template_id = rwi.world_template_id
) ranked
WHERE save_slot_rank = 1;

-- ============================================================================
-- END server/sql/step95_save_slot_catalog_db_continue_bridge.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step96_db_save_checkpoint_snapshots.sql
-- ============================================================================

-- Step96: materialize DB-native save checkpoint snapshots from current projections.
-- This is the first real "save file -> DB" layer: normalized snapshot tables
-- instead of treating .sav path/catalog metadata as the save payload.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_character_snapshot (
  manifest_id BINARY(16) NOT NULL,
  character_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  pos_x DOUBLE DEFAULT NULL,
  pos_y DOUBLE DEFAULT NULL,
  pos_z DOUBLE DEFAULT NULL,
  rotation_yaw DOUBLE DEFAULT NULL,
  current_waypoint_key VARCHAR(191) DEFAULT NULL,
  position_server_tick BIGINT NOT NULL DEFAULT 0,
  position_row_version BIGINT NOT NULL DEFAULT 0,
  level_value INT NOT NULL DEFAULT 0,
  experience_value BIGINT NOT NULL DEFAULT 0,
  experience_next BIGINT DEFAULT NULL,
  learning_points INT NOT NULL DEFAULT 0,
  health_current INT NOT NULL DEFAULT 0,
  health_max INT NOT NULL DEFAULT 0,
  mana_current INT NOT NULL DEFAULT 0,
  mana_max INT NOT NULL DEFAULT 0,
  strength_value INT NOT NULL DEFAULT 0,
  dexterity_value INT NOT NULL DEFAULT 0,
  guild_value INT DEFAULT NULL,
  true_guild_value INT DEFAULT NULL,
  permanent_attitude INT DEFAULT NULL,
  temporary_attitude INT DEFAULT NULL,
  stats_row_version BIGINT NOT NULL DEFAULT 0,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id),
  KEY ix_mmo_save_character_snapshot_character (character_id),
  CONSTRAINT mmo_save_character_snapshot_manifest_fk FOREIGN KEY (manifest_id) REFERENCES mmo_save_checkpoint_manifests (manifest_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_inventory_snapshot (
  manifest_id BINARY(16) NOT NULL,
  item_instance_id BINARY(16) NOT NULL,
  item_instance_key VARCHAR(191) NOT NULL,
  item_template_key VARCHAR(191) DEFAULT NULL,
  symbol_index INT DEFAULT NULL,
  script_name VARCHAR(191) DEFAULT NULL,
  display_name VARCHAR(191) DEFAULT NULL,
  bag_index INT DEFAULT NULL,
  amount INT NOT NULL DEFAULT 1,
  instance_quantity INT NOT NULL DEFAULT 1,
  bind_state VARCHAR(32) NOT NULL DEFAULT 'unbound',
  lifecycle_state VARCHAR(32) NOT NULL DEFAULT 'active',
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id, item_instance_id),
  KEY ix_mmo_save_inventory_snapshot_template (manifest_id, item_template_key),
  CONSTRAINT mmo_save_inventory_snapshot_manifest_fk FOREIGN KEY (manifest_id) REFERENCES mmo_save_checkpoint_manifests (manifest_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_equipment_snapshot (
  manifest_id BINARY(16) NOT NULL,
  equipment_slot VARCHAR(32) NOT NULL,
  item_instance_id BINARY(16) NOT NULL,
  item_instance_key VARCHAR(191) NOT NULL,
  item_template_key VARCHAR(191) DEFAULT NULL,
  symbol_index INT DEFAULT NULL,
  script_name VARCHAR(191) DEFAULT NULL,
  display_name VARCHAR(191) DEFAULT NULL,
  amount INT NOT NULL DEFAULT 1,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id, equipment_slot),
  KEY ix_mmo_save_equipment_snapshot_item (manifest_id, item_instance_id),
  CONSTRAINT mmo_save_equipment_snapshot_manifest_fk FOREIGN KEY (manifest_id) REFERENCES mmo_save_checkpoint_manifests (manifest_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_quest_snapshot (
  manifest_id BINARY(16) NOT NULL,
  quest_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  section VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL DEFAULT '',
  status VARCHAR(32) NOT NULL,
  entry_order INT NOT NULL DEFAULT 0,
  text_entries JSON NOT NULL,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id, quest_key),
  KEY ix_mmo_save_quest_snapshot_status (manifest_id, status),
  CONSTRAINT mmo_save_quest_snapshot_manifest_fk FOREIGN KEY (manifest_id) REFERENCES mmo_save_checkpoint_manifests (manifest_id) ON DELETE CASCADE,
  CONSTRAINT mmo_save_quest_snapshot_entries_json_ck CHECK (JSON_VALID(text_entries))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_known_dialog_snapshot (
  manifest_id BINARY(16) NOT NULL,
  npc_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  info_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  known TINYINT(1) NOT NULL DEFAULT 1,
  permanent TINYINT(1) NOT NULL DEFAULT 0,
  availability_state VARCHAR(32) NOT NULL DEFAULT 'unknown',
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id, npc_key, info_key),
  CONSTRAINT mmo_save_dialog_snapshot_manifest_fk FOREIGN KEY (manifest_id) REFERENCES mmo_save_checkpoint_manifests (manifest_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_script_state_snapshot (
  manifest_id BINARY(16) NOT NULL,
  script_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  symbol_index INT DEFAULT NULL,
  value_type VARCHAR(32) NOT NULL,
  value_index INT NOT NULL DEFAULT 0,
  value_int BIGINT DEFAULT NULL,
  value_real DOUBLE DEFAULT NULL,
  value_text TEXT,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id, script_key, value_index),
  KEY ix_mmo_save_script_snapshot_symbol (manifest_id, symbol_index),
  CONSTRAINT mmo_save_script_snapshot_manifest_fk FOREIGN KEY (manifest_id) REFERENCES mmo_save_checkpoint_manifests (manifest_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_world_entity_snapshot (
  manifest_id BINARY(16) NOT NULL,
  entity_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  entity_kind VARCHAR(32) NOT NULL,
  entity_template_key VARCHAR(191) DEFAULT NULL,
  symbol_index INT DEFAULT NULL,
  script_id INT DEFAULT NULL,
  script_name VARCHAR(191) DEFAULT NULL,
  display_name VARCHAR(191) DEFAULT NULL,
  lifecycle_state VARCHAR(32) NOT NULL DEFAULT 'active',
  pos_x DOUBLE DEFAULT NULL,
  pos_y DOUBLE DEFAULT NULL,
  pos_z DOUBLE DEFAULT NULL,
  rotation_yaw DOUBLE DEFAULT NULL,
  health_current INT DEFAULT NULL,
  health_max INT DEFAULT NULL,
  state_json JSON NOT NULL,
  source_row_version BIGINT NOT NULL DEFAULT 0,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id, entity_key),
  KEY ix_mmo_save_world_entity_snapshot_kind (manifest_id, entity_kind, lifecycle_state),
  CONSTRAINT mmo_save_world_entity_snapshot_manifest_fk FOREIGN KEY (manifest_id) REFERENCES mmo_save_checkpoint_manifests (manifest_id) ON DELETE CASCADE,
  CONSTRAINT mmo_save_world_entity_snapshot_json_ck CHECK (JSON_VALID(state_json))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_world_inventory_snapshot (
  manifest_id BINARY(16) NOT NULL,
  owner_entity_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  item_instance_id BINARY(16) NOT NULL,
  item_instance_key VARCHAR(191) NOT NULL,
  item_template_key VARCHAR(191) DEFAULT NULL,
  symbol_index INT DEFAULT NULL,
  script_name VARCHAR(191) DEFAULT NULL,
  display_name VARCHAR(191) DEFAULT NULL,
  amount INT NOT NULL DEFAULT 1,
  instance_quantity INT NOT NULL DEFAULT 1,
  lifecycle_state VARCHAR(32) NOT NULL DEFAULT 'active',
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id, owner_entity_key, item_instance_id),
  KEY ix_mmo_save_world_inventory_snapshot_template (manifest_id, item_template_key),
  CONSTRAINT mmo_save_world_inventory_snapshot_manifest_fk FOREIGN KEY (manifest_id) REFERENCES mmo_save_checkpoint_manifests (manifest_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_mover_snapshot (
  manifest_id BINARY(16) NOT NULL,
  mover_key VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  state_after INT NOT NULL DEFAULT 0,
  state_after_name VARCHAR(96) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT NULL,
  frame_index INT DEFAULT NULL,
  target_frame_index INT DEFAULT NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  source_row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id, mover_key),
  CONSTRAINT mmo_save_mover_snapshot_manifest_fk FOREIGN KEY (manifest_id) REFERENCES mmo_save_checkpoint_manifests (manifest_id) ON DELETE CASCADE,
  CONSTRAINT mmo_save_mover_snapshot_json_ck CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_materialize_save_checkpoint_snapshot_v1;
DELIMITER ;;
CREATE PROCEDURE mmo_materialize_save_checkpoint_snapshot_v1(
  IN p_manifest_id BINARY(16)
)
proc: BEGIN
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;

  SELECT character_id, world_instance_id
    INTO v_character_id, v_world_instance_id
    FROM mmo_save_checkpoint_manifests
   WHERE manifest_id = p_manifest_id
   LIMIT 1;

  IF v_character_id IS NULL OR v_world_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_materialize_save_checkpoint_snapshot_v1: invalid manifest';
  END IF;

  DELETE FROM mmo_save_checkpoint_mover_snapshot WHERE manifest_id = p_manifest_id;
  DELETE FROM mmo_save_checkpoint_world_inventory_snapshot WHERE manifest_id = p_manifest_id;
  DELETE FROM mmo_save_checkpoint_world_entity_snapshot WHERE manifest_id = p_manifest_id;
  DELETE FROM mmo_save_checkpoint_script_state_snapshot WHERE manifest_id = p_manifest_id;
  DELETE FROM mmo_save_checkpoint_known_dialog_snapshot WHERE manifest_id = p_manifest_id;
  DELETE FROM mmo_save_checkpoint_quest_snapshot WHERE manifest_id = p_manifest_id;
  DELETE FROM mmo_save_checkpoint_equipment_snapshot WHERE manifest_id = p_manifest_id;
  DELETE FROM mmo_save_checkpoint_inventory_snapshot WHERE manifest_id = p_manifest_id;
  DELETE FROM mmo_save_checkpoint_character_snapshot WHERE manifest_id = p_manifest_id;

  INSERT INTO mmo_save_checkpoint_character_snapshot(
    manifest_id, character_id, world_instance_id,
    pos_x, pos_y, pos_z, rotation_yaw, current_waypoint_key,
    position_server_tick, position_row_version,
    level_value, experience_value, experience_next, learning_points,
    health_current, health_max, mana_current, mana_max, strength_value, dexterity_value,
    guild_value, true_guild_value, permanent_attitude, temporary_attitude, stats_row_version
  )
  SELECT p_manifest_id, v_character_id, v_world_instance_id,
         cp.pos_x, cp.pos_y, cp.pos_z, cp.rotation_yaw, cp.current_waypoint_key,
         COALESCE(cp.server_tick,0), COALESCE(cp.row_version,0),
         COALESCE(cs.level,0), COALESCE(cs.experience,0), cs.experience_next, COALESCE(cs.learning_points,0),
         COALESCE(cs.health_current,0), COALESCE(cs.health_max,0), COALESCE(cs.mana_current,0), COALESCE(cs.mana_max,0),
         COALESCE(cs.strength,0), COALESCE(cs.dexterity,0),
         cs.guild, cs.true_guild, cs.permanent_attitude, cs.temporary_attitude, COALESCE(cs.row_version,0)
    FROM characters c
    LEFT JOIN character_positions cp ON cp.character_id = c.character_id
    LEFT JOIN character_stats cs ON cs.character_id = c.character_id
   WHERE c.character_id = v_character_id
   LIMIT 1;

  INSERT INTO mmo_save_checkpoint_inventory_snapshot(
    manifest_id, item_instance_id, item_instance_key, item_template_key, symbol_index,
    script_name, display_name, bag_index, amount, instance_quantity, bind_state, lifecycle_state
  )
  SELECT p_manifest_id, ci.item_instance_id, ii.item_instance_key, cit.item_template_key, cit.symbol_index,
         cit.script_name, COALESCE(cit.display_name, JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.display_name'))),
         ci.bag_index, ci.amount, ii.quantity, ii.bind_state, ii.lifecycle_state
    FROM character_inventory ci
    JOIN item_instances ii ON ii.item_instance_id = ci.item_instance_id
    LEFT JOIN content_item_templates cit ON cit.item_template_id = ii.item_template_id
   WHERE ci.character_id = v_character_id;

  INSERT INTO mmo_save_checkpoint_equipment_snapshot(
    manifest_id, equipment_slot, item_instance_id, item_instance_key, item_template_key,
    symbol_index, script_name, display_name, amount
  )
  SELECT p_manifest_id, ce.equipment_slot, ce.item_instance_id, ii.item_instance_key, cit.item_template_key,
         cit.symbol_index, cit.script_name, COALESCE(cit.display_name, JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.display_name'))), ii.quantity
    FROM character_equipment ce
    JOIN item_instances ii ON ii.item_instance_id = ce.item_instance_id
    LEFT JOIN content_item_templates cit ON cit.item_template_id = ii.item_template_id
   WHERE ce.character_id = v_character_id;

  INSERT INTO mmo_save_checkpoint_quest_snapshot(
    manifest_id, quest_key, section, status, entry_order, text_entries
  )
  SELECT p_manifest_id, quest_key, section, status, entry_order, text_entries
    FROM character_quests
   WHERE character_id = v_character_id;

  INSERT INTO mmo_save_checkpoint_known_dialog_snapshot(
    manifest_id, npc_key, info_key, known, permanent, availability_state
  )
  SELECT p_manifest_id, npc_key, info_key, known, permanent, availability_state
    FROM character_known_dialogs
   WHERE character_id = v_character_id;

  INSERT INTO mmo_save_checkpoint_script_state_snapshot(
    manifest_id, script_key, symbol_index, value_type, value_index, value_int, value_real, value_text
  )
  SELECT p_manifest_id, script_key, symbol_index, value_type, value_index, value_int, value_real, value_text
    FROM character_script_state
   WHERE character_id = v_character_id;

  INSERT INTO mmo_save_checkpoint_world_entity_snapshot(
    manifest_id, entity_key, entity_kind, entity_template_key, symbol_index, script_id, script_name, display_name,
    lifecycle_state, pos_x, pos_y, pos_z, rotation_yaw, health_current, health_max, state_json, source_row_version
  )
  SELECT p_manifest_id, wes.entity_key, wes.entity_kind, cet.engine_template_key, cet.symbol_index, cet.script_id,
         cet.script_name, cet.display_name, wes.lifecycle_state,
         wes.pos_x, wes.pos_y, wes.pos_z, wes.rotation_yaw, wes.health_current, wes.health_max,
         wes.state_json, wes.row_version
    FROM world_entity_state wes
    LEFT JOIN content_entity_templates cet ON cet.entity_template_id = wes.entity_template_id
   WHERE wes.world_instance_id = v_world_instance_id;

  INSERT INTO mmo_save_checkpoint_world_inventory_snapshot(
    manifest_id, owner_entity_key, item_instance_id, item_instance_key, item_template_key,
    symbol_index, script_name, display_name, amount, instance_quantity, lifecycle_state
  )
  SELECT p_manifest_id, wi.owner_entity_key, wi.item_instance_id, ii.item_instance_key, cit.item_template_key,
         cit.symbol_index, cit.script_name, COALESCE(cit.display_name, JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.display_name'))),
         wi.amount, ii.quantity, ii.lifecycle_state
    FROM world_inventory wi
    JOIN item_instances ii ON ii.item_instance_id = wi.item_instance_id
    LEFT JOIN content_item_templates cit ON cit.item_template_id = ii.item_template_id
   WHERE wi.world_instance_id = v_world_instance_id;

  INSERT INTO mmo_save_checkpoint_mover_snapshot(
    manifest_id, mover_key, state_after, state_after_name, frame_index, target_frame_index,
    last_server_tick, state_payload, source_row_version
  )
  SELECT p_manifest_id, mover_key, state_after, state_after_name, frame_index, target_frame_index,
         last_server_tick, state_payload, row_version
    FROM mmo_world_mover_state_current
   WHERE world_instance_id = v_world_instance_id;
END ;;
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_create_db_save_checkpoint_v1;
DELIMITER ;;
CREATE PROCEDURE mmo_create_db_save_checkpoint_v1(
  IN p_session_id BINARY(16),
  IN p_manifest_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci,
  IN p_checkpoint_kind VARCHAR(64),
  IN p_reason VARCHAR(128),
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci,
  OUT p_manifest_id BINARY(16),
  OUT p_event_id BINARY(16),
  OUT p_row_version_after BIGINT UNSIGNED
)
BEGIN
  CALL mmo_create_save_checkpoint_manifest(
    p_session_id,
    p_manifest_key,
    p_checkpoint_kind,
    p_reason,
    p_server_tick,
    p_metadata,
    p_idempotency_key,
    p_manifest_id,
    p_event_id,
    p_row_version_after
  );

  CALL mmo_materialize_save_checkpoint_snapshot_v1(p_manifest_id);
END ;;
DELIMITER ;

CREATE OR REPLACE VIEW v_mmo_save_checkpoint_snapshot_domain_counts AS
SELECT
  BIN_TO_UUID(sm.manifest_id,1) AS manifest_uuid,
  c.character_key,
  COALESCE(sm.save_slot_key, sm.manifest_key) AS save_key,
  sm.display_name,
  sm.created_at,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_character_snapshot s WHERE s.manifest_id = sm.manifest_id) AS character_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_inventory_snapshot s WHERE s.manifest_id = sm.manifest_id) AS inventory_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_equipment_snapshot s WHERE s.manifest_id = sm.manifest_id) AS equipment_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_quest_snapshot s WHERE s.manifest_id = sm.manifest_id) AS quest_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_known_dialog_snapshot s WHERE s.manifest_id = sm.manifest_id) AS known_dialog_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_script_state_snapshot s WHERE s.manifest_id = sm.manifest_id) AS script_state_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_world_entity_snapshot s WHERE s.manifest_id = sm.manifest_id) AS world_entity_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_world_inventory_snapshot s WHERE s.manifest_id = sm.manifest_id) AS world_inventory_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_mover_snapshot s WHERE s.manifest_id = sm.manifest_id) AS mover_rows
FROM mmo_save_checkpoint_manifests sm
JOIN characters c ON c.character_id = sm.character_id;

-- ============================================================================
-- END server/sql/step96_db_save_checkpoint_snapshots.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step108_db_checkpoint_world_clock_foundation.sql
-- ============================================================================

-- Step108: make the procedure-only DB checkpoint export path self-contained.
-- MySQL with binary logging can reject legacy CREATE FUNCTION bridges, so clean
-- reset skips Step97/Step98 and installs the final procedure path instead. The
-- world-clock snapshot table used to live behind that legacy bridge; keep it in
-- the procedure path so Step104/Step106 can be applied from a clean database.

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_world_clock_snapshot (
  manifest_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  world_day INT DEFAULT NULL,
  world_time_ms BIGINT DEFAULT NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  source_row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id),
  KEY ix_mmo_save_world_clock_snapshot_world (world_instance_id),
  CONSTRAINT mmo_save_world_clock_snapshot_manifest_fk
    FOREIGN KEY (manifest_id)
    REFERENCES mmo_save_checkpoint_manifests(manifest_id)
    ON DELETE CASCADE,
  CONSTRAINT mmo_save_world_clock_snapshot_world_fk
    FOREIGN KEY (world_instance_id)
    REFERENCES realm_world_instances(world_instance_id)
    ON DELETE CASCADE,
  CONSTRAINT mmo_save_world_clock_snapshot_payload_json_ck
    CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_materialize_save_checkpoint_world_clock_snapshot_v1;
DELIMITER ;;
CREATE PROCEDURE mmo_materialize_save_checkpoint_world_clock_snapshot_v1(
  IN p_manifest_id BINARY(16)
)
proc: BEGIN
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;

  SELECT world_instance_id
    INTO v_world_instance_id
    FROM mmo_save_checkpoint_manifests
   WHERE manifest_id = p_manifest_id
   LIMIT 1;

  IF v_world_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000'
      SET MESSAGE_TEXT='mmo_materialize_save_checkpoint_world_clock_snapshot_v1: invalid manifest';
  END IF;

  DELETE FROM mmo_save_checkpoint_world_clock_snapshot
   WHERE manifest_id = p_manifest_id;

  INSERT INTO mmo_save_checkpoint_world_clock_snapshot(
    manifest_id, world_instance_id, world_day, world_time_ms,
    last_server_tick, state_payload, source_row_version
  )
  SELECT p_manifest_id,
         v_world_instance_id,
         world_day,
         world_time_ms,
         last_server_tick,
         state_payload,
         row_version
    FROM mmo_world_clock_state_current
   WHERE world_instance_id = v_world_instance_id
   LIMIT 1;

  IF ROW_COUNT() = 0 THEN
    INSERT INTO mmo_save_checkpoint_world_clock_snapshot(
      manifest_id, world_instance_id, world_day, world_time_ms,
      last_server_tick, state_payload, source_row_version
    )
    SELECT p_manifest_id,
           rwi.world_instance_id,
           CAST(FLOOR(COALESCE(rwi.current_world_time_ms, 0) / 86400000) AS SIGNED),
           COALESCE(rwi.current_world_time_ms, 0),
           CAST(GREATEST(COALESCE(rwi.current_tick, 0), 0) AS UNSIGNED),
           JSON_OBJECT(
             'source', 'realm_world_instances_fallback',
             'current_tick', COALESCE(rwi.current_tick, 0),
             'current_world_time_ms', COALESCE(rwi.current_world_time_ms, 0)
           ),
           1
      FROM realm_world_instances rwi
     WHERE rwi.world_instance_id = v_world_instance_id
     LIMIT 1;
  END IF;
END ;;
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_create_db_save_checkpoint_v1;
DELIMITER ;;
CREATE PROCEDURE mmo_create_db_save_checkpoint_v1(
  IN p_session_id BINARY(16),
  IN p_manifest_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci,
  IN p_checkpoint_kind VARCHAR(64),
  IN p_reason VARCHAR(128),
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci,
  OUT p_manifest_id BINARY(16),
  OUT p_event_id BINARY(16),
  OUT p_row_version_after BIGINT UNSIGNED
)
BEGIN
  CALL mmo_create_save_checkpoint_manifest(
    p_session_id,
    p_manifest_key,
    p_checkpoint_kind,
    p_reason,
    p_server_tick,
    p_metadata,
    p_idempotency_key,
    p_manifest_id,
    p_event_id,
    p_row_version_after
  );

  CALL mmo_materialize_save_checkpoint_snapshot_v1(p_manifest_id);
  CALL mmo_materialize_save_checkpoint_world_clock_snapshot_v1(p_manifest_id);
END ;;
DELIMITER ;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES(
  'step108_db_checkpoint_world_clock_foundation',
  'db_checkpoint_world_clock_snapshot_v1',
  'Creates the world-clock checkpoint snapshot table and keeps procedure-only DB save checkpoints self-contained without legacy CREATE FUNCTION bridges.'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);

-- ============================================================================
-- END server/sql/step108_db_checkpoint_world_clock_foundation.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step103_db_checkpoint_export_coverage.sql
-- ============================================================================

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;

DELIMITER //

DROP PROCEDURE IF EXISTS mmo_materialize_save_checkpoint_world_clock_snapshot_v1//
CREATE PROCEDURE mmo_materialize_save_checkpoint_world_clock_snapshot_v1(
  IN p_manifest_id BINARY(16)
)
proc: BEGIN
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;

  SELECT world_instance_id
    INTO v_world_instance_id
    FROM mmo_save_checkpoint_manifests
   WHERE manifest_id = p_manifest_id
   LIMIT 1;

  IF v_world_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000'
      SET MESSAGE_TEXT='mmo_materialize_save_checkpoint_world_clock_snapshot_v1: invalid manifest';
  END IF;

  DELETE FROM mmo_save_checkpoint_world_clock_snapshot
   WHERE manifest_id = p_manifest_id;

  INSERT INTO mmo_save_checkpoint_world_clock_snapshot(
    manifest_id, world_instance_id, world_day, world_time_ms,
    last_server_tick, state_payload, source_row_version
  )
  SELECT p_manifest_id,
         v_world_instance_id,
         world_day,
         world_time_ms,
         last_server_tick,
         state_payload,
         row_version
    FROM mmo_world_clock_state_current
   WHERE world_instance_id = v_world_instance_id
   LIMIT 1;

  IF ROW_COUNT() = 0 THEN
    INSERT INTO mmo_save_checkpoint_world_clock_snapshot(
      manifest_id, world_instance_id, world_day, world_time_ms,
      last_server_tick, state_payload, source_row_version
    )
    SELECT p_manifest_id,
           rwi.world_instance_id,
           CAST(FLOOR(COALESCE(rwi.current_world_time_ms, 0) / 86400000) AS SIGNED),
           COALESCE(rwi.current_world_time_ms, 0),
           CAST(GREATEST(COALESCE(rwi.current_tick, 0), 0) AS UNSIGNED),
           JSON_OBJECT(
             'source', 'realm_world_instances_fallback',
             'current_tick', COALESCE(rwi.current_tick, 0),
             'current_world_time_ms', COALESCE(rwi.current_world_time_ms, 0)
           ),
           1
      FROM realm_world_instances rwi
     WHERE rwi.world_instance_id = v_world_instance_id
     LIMIT 1;
  END IF;
END//

DELIMITER ;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES(
  'step103_db_checkpoint_export_coverage',
  'db_save_checkpoint_export_coverage_v1',
  'Raises session aggregation limits in C++/tools and makes world clock checkpoint snapshot fall back to realm_world_instances.'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);

-- ============================================================================
-- END server/sql/step103_db_checkpoint_export_coverage.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step104_db_checkpoint_script_state_full_export.sql
-- ============================================================================

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET SESSION group_concat_max_len=104857600;

DELIMITER //

DROP VIEW IF EXISTS v_mmo_latest_save_checkpoint_strict_restore//
DROP PROCEDURE IF EXISTS mmo_assert_latest_save_checkpoint_restore_v1//
DROP PROCEDURE IF EXISTS mmo_validate_latest_save_checkpoint_restore_v1//
DROP PROCEDURE IF EXISTS mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1//
DROP FUNCTION IF EXISTS mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1//
CREATE PROCEDURE `mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1`(
  IN p_session_id BINARY(16),
  OUT p_snapshot_json LONGTEXT
)
build_snapshot: BEGIN
  DECLARE v_manifest_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_manifest_uuid VARCHAR(36) DEFAULT NULL;
  DECLARE v_session_uuid VARCHAR(36) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT 'PC_HERO';
  DECLARE v_world_name VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT 'newworld.zen';
  DECLARE v_hx DOUBLE DEFAULT 0;
  DECLARE v_hy DOUBLE DEFAULT 0;
  DECLARE v_hz DOUBLE DEFAULT 0;
  DECLARE v_active_radius DOUBLE DEFAULT 12000.0;
  DECLARE v_nearby_npc_radius DOUBLE DEFAULT 12000.0;
  DECLARE v_nearby_waypoint_radius DOUBLE DEFAULT 12000.0;
  DECLARE v_world_entity_count BIGINT DEFAULT 0;
  DECLARE v_world_inventory_count BIGINT DEFAULT 0;
  DECLARE v_interactive_count BIGINT DEFAULT 0;
  DECLARE v_script_int_count BIGINT DEFAULT 0;
  DECLARE v_character LONGTEXT CHARACTER SET utf8mb4 DEFAULT '{}';
  DECLARE v_inventory LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_equipment LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_known_dialogs LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_quests LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_script_state LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_script_state_full LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_script_full_count BIGINT DEFAULT 0;
  DECLARE v_world_clock LONGTEXT CHARACTER SET utf8mb4 DEFAULT '{}';
  DECLARE v_active_world_items LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_world_item_deltas LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_interactive_state LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_npc_lifecycle_state LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_nearby_npcs LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_nearby_npc_known_dialogs LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_nearby_waypoints LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_recent_actions LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_mover_state LONGTEXT CHARACTER SET utf8mb4 DEFAULT '[]';
  DECLARE v_manifest LONGTEXT CHARACTER SET utf8mb4 DEFAULT '{}';
  DECLARE v_not_found BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;

  SET v_session_uuid = BIN_TO_UUID(p_session_id, 1);

  SELECT sm.manifest_id, sm.character_id, sm.world_instance_id,
         BIN_TO_UUID(sm.manifest_id,1), c.character_key,
         COALESCE(cwt.world_name, rwi.world_instance_key, sm.client_world_name, 'newworld.zen')
    INTO v_manifest_id, v_character_id, v_world_instance_id,
         v_manifest_uuid, v_character_key, v_world_name
    FROM server_sessions ss
    JOIN mmo_save_checkpoint_manifests sm
      ON sm.character_id = ss.character_id
     AND sm.world_instance_id = ss.world_instance_id
    JOIN characters c ON c.character_id = sm.character_id
    JOIN realm_world_instances rwi ON rwi.world_instance_id = sm.world_instance_id
    LEFT JOIN content_world_templates cwt ON cwt.world_template_id = rwi.world_template_id
   WHERE ss.session_id = p_session_id
   ORDER BY sm.created_at DESC, sm.row_version DESC
   LIMIT 1;

  IF v_not_found OR v_manifest_id IS NULL THEN
    SET p_snapshot_json = NULL;
    LEAVE build_snapshot;
  END IF;

  SELECT COALESCE(pos_x, 0), COALESCE(pos_y, 0), COALESCE(pos_z, 0)
    INTO v_hx, v_hy, v_hz
    FROM mmo_save_checkpoint_character_snapshot
   WHERE manifest_id = v_manifest_id
   LIMIT 1;

  SELECT COUNT(*) INTO v_world_entity_count
    FROM mmo_save_checkpoint_world_entity_snapshot
   WHERE manifest_id = v_manifest_id;

  SELECT COUNT(*) INTO v_world_inventory_count
    FROM mmo_save_checkpoint_world_inventory_snapshot
   WHERE manifest_id = v_manifest_id;

  SELECT COUNT(*) INTO v_interactive_count
    FROM mmo_save_checkpoint_world_entity_snapshot
   WHERE manifest_id = v_manifest_id
     AND entity_kind = 'interactive';

  SELECT COUNT(*) INTO v_script_int_count
    FROM mmo_save_checkpoint_script_state_snapshot
   WHERE manifest_id = v_manifest_id
     AND value_type IN ('int','array_int');

  SELECT COUNT(*) INTO v_script_full_count
    FROM mmo_save_checkpoint_script_state_snapshot
   WHERE manifest_id = v_manifest_id;

  SELECT COALESCE(JSON_OBJECT(
           'character_key', v_character_key,
           'display_name', c.character_name,
           'world_name', v_world_name,
           'position', JSON_OBJECT(
             'x', s.pos_x, 'y', s.pos_y, 'z', s.pos_z,
             'yaw', s.rotation_yaw,
             'waypoint', s.current_waypoint_key,
             'server_tick', s.position_server_tick
           ),
           'stats', JSON_OBJECT(
             'level', s.level_value,
             'experience', s.experience_value,
             'experience_next', s.experience_next,
             'learning_points', s.learning_points,
             'health_current', s.health_current,
             'health_max', s.health_max,
             'mana_current', s.mana_current,
             'mana_max', s.mana_max,
             'strength', s.strength_value,
             'dexterity', s.dexterity_value,
             'guild', s.guild_value,
             'true_guild', s.true_guild_value
           ),
           'lifecycle_state', c.lifecycle_state,
           'updated_at', DATE_FORMAT(s.captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
         ), JSON_OBJECT())
    INTO v_character
    FROM mmo_save_checkpoint_character_snapshot s
    JOIN characters c ON c.character_id = s.character_id
   WHERE s.manifest_id = v_manifest_id
   LIMIT 1;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'item_instance_uuid', BIN_TO_UUID(s.item_instance_id,1),
      'item_instance_key', s.item_instance_key,
      'item_template_key', s.item_template_key,
      'symbol_index', s.symbol_index,
      'script_name', s.script_name,
      'display_name', s.display_name,
      'classification', 'unknown',
      'stack_policy', 'unknown',
      'amount', s.amount,
      'bag_index', s.bag_index,
      'equipped_slot', es.equipment_slot,
      'lifecycle_state', s.lifecycle_state,
      'updated_at', DATE_FORMAT(s.captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json
    FROM mmo_save_checkpoint_inventory_snapshot s
    LEFT JOIN mmo_save_checkpoint_equipment_snapshot es
      ON es.manifest_id = s.manifest_id
     AND es.item_instance_id = s.item_instance_id
    WHERE s.manifest_id = v_manifest_id
    ORDER BY COALESCE(s.bag_index,999999), s.item_instance_key
    LIMIT 2048
  ) rows_json), JSON_ARRAY()) INTO v_inventory;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'slot', equipment_slot,
      'item_instance_uuid', BIN_TO_UUID(item_instance_id,1),
      'item_instance_key', item_instance_key,
      'item_template_key', item_template_key,
      'symbol_index', symbol_index,
      'display_name', display_name,
      'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json
    FROM mmo_save_checkpoint_equipment_snapshot
    WHERE manifest_id = v_manifest_id
    ORDER BY equipment_slot
    LIMIT 64
  ) rows_json), JSON_ARRAY()) INTO v_equipment;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'npc_key', npc_key,
      'info_key', info_key,
      'known', known,
      'permanent', permanent,
      'availability_state', availability_state,
      'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json
    FROM mmo_save_checkpoint_known_dialog_snapshot
    WHERE manifest_id = v_manifest_id
    ORDER BY npc_key, info_key
    LIMIT 4096
  ) rows_json), JSON_ARRAY()) INTO v_known_dialogs;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'quest_key', quest_key,
      'section', section,
      'status', status,
      'entry_order', entry_order,
      'text_entries', text_entries,
      'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json
    FROM mmo_save_checkpoint_quest_snapshot
    WHERE manifest_id = v_manifest_id
    ORDER BY quest_key
    LIMIT 1024
  ) rows_json), JSON_ARRAY()) INTO v_quests;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'script_key', script_key,
      'symbol_index', symbol_index,
      'value_type', value_type,
      'value_index', value_index,
      'value_int', value_int,
      'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json
    FROM mmo_save_checkpoint_script_state_snapshot
    WHERE manifest_id = v_manifest_id
      AND value_type IN ('int','array_int')
    ORDER BY script_key, value_index
    LIMIT 16384
  ) rows_json), JSON_ARRAY()) INTO v_script_state;

  SELECT COALESCE(CONCAT('[', GROUP_CONCAT(CAST(row_json AS CHAR CHARACTER SET utf8mb4) ORDER BY sort_script_key, sort_value_index SEPARATOR ','), ']'), '[]')
    INTO v_script_state_full
    FROM (
      SELECT JSON_OBJECT(
        'script_key', script_key,
        'symbol_index', symbol_index,
        'value_type', value_type,
        'value_index', value_index,
        'value_int', value_int,
        'value_real', value_real,
        'value_text', value_text,
        'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
      ) AS row_json,
      script_key AS sort_script_key,
      value_index AS sort_value_index
      FROM mmo_save_checkpoint_script_state_snapshot
      WHERE manifest_id = v_manifest_id
      ORDER BY script_key, value_index
      LIMIT 20000
    ) rows_json;

  SELECT COALESCE(JSON_OBJECT(
           'world_instance_uuid', BIN_TO_UUID(world_instance_id,1),
           'world_name', v_world_name,
           'world_day', world_day,
           'world_time_ms', world_time_ms,
           'last_server_tick', last_server_tick,
           'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
         ), JSON_OBJECT())
    INTO v_world_clock
    FROM mmo_save_checkpoint_world_clock_snapshot
   WHERE manifest_id = v_manifest_id
   LIMIT 1;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'owner_key', entity_key,
      'entity_key', entity_key,
      'lifecycle_state', lifecycle_state,
      'persistent_id', CAST(JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.persistent_id')) AS SIGNED),
      'symbol_index', COALESCE(symbol_index, CAST(JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.item_template_symbol')) AS SIGNED)),
      'amount', COALESCE(CAST(JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.amount')) AS UNSIGNED), 1),
      'pos_x', pos_x,
      'pos_y', pos_y,
      'pos_z', pos_z,
      'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json,
    (((pos_x-v_hx)*(pos_x-v_hx))+((pos_y-v_hy)*(pos_y-v_hy))+((pos_z-v_hz)*(pos_z-v_hz))) AS dist_sq,
    entity_key AS owner_key
    FROM mmo_save_checkpoint_world_entity_snapshot
    WHERE manifest_id = v_manifest_id
      AND entity_kind = 'item'
      AND lifecycle_state = 'active'
      AND pos_x IS NOT NULL AND pos_y IS NOT NULL AND pos_z IS NOT NULL
      AND (((pos_x-v_hx)*(pos_x-v_hx))+((pos_y-v_hy)*(pos_y-v_hy))+((pos_z-v_hz)*(pos_z-v_hz))) <= (v_active_radius*v_active_radius)
    ORDER BY dist_sq ASC, owner_key
    LIMIT 1024
  ) rows_json), JSON_ARRAY()) INTO v_active_world_items;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'entity_key', entity_key,
      'entity_kind', entity_kind,
      'lifecycle_state', lifecycle_state,
      'persistent_id', CAST(JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.persistent_id')) AS SIGNED),
      'symbol_index', COALESCE(symbol_index, CAST(JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.item_template_symbol')) AS SIGNED)),
      'exists_in_world', JSON_EXTRACT(state_json,'$.exists_in_world'),
      'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json
    FROM mmo_save_checkpoint_world_entity_snapshot
    WHERE manifest_id = v_manifest_id
      AND entity_kind = 'item'
      AND lifecycle_state <> 'active'
    ORDER BY captured_at DESC, entity_key
    LIMIT 4096
  ) rows_json), JSON_ARRAY()) INTO v_world_item_deltas;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'entity_key', entity_key,
      'lifecycle_state', lifecycle_state,
      'state_id', CAST(JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.state_id')) AS SIGNED),
      'locked', JSON_EXTRACT(state_json,'$.locked'),
      'cracked', JSON_EXTRACT(state_json,'$.cracked'),
      'state_json', state_json,
      'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json
    FROM mmo_save_checkpoint_world_entity_snapshot
    WHERE manifest_id = v_manifest_id
      AND entity_kind = 'interactive'
    ORDER BY entity_key
    LIMIT 2048
  ) rows_json), JSON_ARRAY()) INTO v_interactive_state;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'entity_key', entity_key,
      'entity_kind', entity_kind,
      'lifecycle_state', lifecycle_state,
      'persistent_id', CAST(JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.persistent_id')) AS SIGNED),
      'symbol_index', COALESCE(symbol_index, script_id, CAST(JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.symbol_index')) AS SIGNED), CAST(JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.script_id')) AS SIGNED)),
      'health_current', health_current,
      'health_max', health_max,
      'pos_x', pos_x,
      'pos_y', pos_y,
      'pos_z', pos_z,
      'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json
    FROM mmo_save_checkpoint_world_entity_snapshot
    WHERE manifest_id = v_manifest_id
      AND entity_kind IN ('npc','creature')
      AND (lifecycle_state <> 'active' OR (health_current IS NOT NULL AND health_max IS NOT NULL AND health_current < health_max))
    ORDER BY CASE WHEN lifecycle_state='dead' THEN 0 WHEN lifecycle_state<>'active' THEN 1 ELSE 2 END, captured_at DESC, entity_key
    LIMIT 2048
  ) rows_json), JSON_ARRAY()) INTO v_npc_lifecycle_state;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'entity_key', entity_key,
      'entity_kind', entity_kind,
      'lifecycle_state', lifecycle_state,
      'symbol_index', symbol_index,
      'script_id', script_id,
      'script_name', script_name,
      'display_name', display_name,
      'health_current', health_current,
      'health_max', health_max,
      'pos_x', pos_x,
      'pos_y', pos_y,
      'pos_z', pos_z,
      'current_waypoint', JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.current_waypoint')),
      'routine_waypoint', JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.routine_waypoint')),
      'ai_state_name', JSON_UNQUOTE(JSON_EXTRACT(state_json,'$.ai_state_name')),
      'distance', SQRT(((pos_x-v_hx)*(pos_x-v_hx))+((pos_y-v_hy)*(pos_y-v_hy))+((pos_z-v_hz)*(pos_z-v_hz))),
      'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json,
    (((pos_x-v_hx)*(pos_x-v_hx))+((pos_y-v_hy)*(pos_y-v_hy))+((pos_z-v_hz)*(pos_z-v_hz))) AS dist_sq,
    entity_key AS owner_key
    FROM mmo_save_checkpoint_world_entity_snapshot
    WHERE manifest_id = v_manifest_id
      AND entity_kind IN ('npc','creature')
      AND lifecycle_state IN ('active','dead','disabled')
      AND pos_x IS NOT NULL AND pos_y IS NOT NULL AND pos_z IS NOT NULL
      AND (((pos_x-v_hx)*(pos_x-v_hx))+((pos_y-v_hy)*(pos_y-v_hy))+((pos_z-v_hz)*(pos_z-v_hz))) <= (v_nearby_npc_radius*v_nearby_npc_radius)
    ORDER BY dist_sq ASC, owner_key
    LIMIT 256
  ) rows_json), JSON_ARRAY()) INTO v_nearby_npcs;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'npc_key', d.npc_key,
      'info_key', d.info_key,
      'known', d.known,
      'permanent', d.permanent,
      'availability_state', d.availability_state,
      'nearby_entity_key', npc.entity_key,
      'nearby_display_name', npc.display_name,
      'nearby_distance', SQRT(npc.dist_sq),
      'updated_at', DATE_FORMAT(d.captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json,
    npc.dist_sq,
    CONCAT(d.npc_key, ':', d.info_key) AS owner_key
    FROM (
      SELECT entity_key, script_name, display_name, symbol_index, script_id,
             (((pos_x-v_hx)*(pos_x-v_hx))+((pos_y-v_hy)*(pos_y-v_hy))+((pos_z-v_hz)*(pos_z-v_hz))) AS dist_sq
      FROM mmo_save_checkpoint_world_entity_snapshot
      WHERE manifest_id = v_manifest_id
        AND entity_kind IN ('npc','creature')
        AND lifecycle_state IN ('active','dead','disabled')
        AND pos_x IS NOT NULL AND pos_y IS NOT NULL AND pos_z IS NOT NULL
        AND (((pos_x-v_hx)*(pos_x-v_hx))+((pos_y-v_hy)*(pos_y-v_hy))+((pos_z-v_hz)*(pos_z-v_hz))) <= (v_nearby_npc_radius*v_nearby_npc_radius)
    ) npc
    JOIN mmo_save_checkpoint_known_dialog_snapshot d ON d.manifest_id = v_manifest_id
    WHERE d.npc_key IN (npc.entity_key, npc.script_name, npc.display_name, CAST(npc.symbol_index AS CHAR), CAST(npc.script_id AS CHAR))
    ORDER BY npc.dist_sq ASC, owner_key
    LIMIT 512
  ) rows_json), JSON_ARRAY()) INTO v_nearby_npc_known_dialogs;

  SET v_nearby_waypoints = JSON_ARRAY();

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'event_seq', event_seq,
      'event_type', event_type,
      'event_class', event_class,
      'entity_key', entity_key,
      'subject_key', subject_key,
      'server_tick', server_tick,
      'occurred_at', DATE_FORMAT(occurred_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json
    FROM world_event_journal
    WHERE world_instance_id = v_world_instance_id
    ORDER BY event_seq DESC
    LIMIT 64
  ) rows_json), JSON_ARRAY()) INTO v_recent_actions;

  SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (
    SELECT JSON_OBJECT(
      'mover_key', mover_key,
      'state_after', state_after,
      'state_after_name', state_after_name,
      'frame_index', frame_index,
      'target_frame_index', target_frame_index,
      'last_server_tick', last_server_tick,
      'row_version', source_row_version,
      'updated_at', DATE_FORMAT(captured_at,'%Y-%m-%dT%H:%i:%s.%fZ')
    ) AS row_json
    FROM mmo_save_checkpoint_mover_snapshot
    WHERE manifest_id = v_manifest_id
    ORDER BY last_server_tick DESC, mover_key
    LIMIT 512
  ) rows_json), JSON_ARRAY()) INTO v_mover_state;

  SELECT COALESCE(JSON_OBJECT(
           'manifest_uuid', BIN_TO_UUID(sm.manifest_id,1),
           'manifest_key', sm.manifest_key,
           'save_slot_key', sm.save_slot_key,
           'native_save_path', sm.native_save_path,
           'display_name', sm.display_name,
           'client_world_name', sm.client_world_name,
           'native_save_present', JSON_EXTRACT(IF(sm.native_save_present<>0,'true','false'),'$'),
           'checkpoint_kind', sm.checkpoint_kind,
           'reason', sm.reason,
           'server_tick', sm.server_tick,
           'latest_checkpoint_tick', sm.latest_checkpoint_tick,
           'recent_event_seq', sm.recent_event_seq,
           'inventory_rows', sm.inventory_rows,
           'equipment_rows', sm.equipment_rows,
           'quest_rows', sm.quest_rows,
           'known_dialog_rows', sm.known_dialog_rows,
           'script_state_rows', sm.script_state_rows,
           'world_item_rows', sm.world_item_rows,
           'world_inventory_rows', sm.world_inventory_rows,
           'interactive_rows', sm.interactive_rows,
           'npc_lifecycle_rows', sm.npc_lifecycle_rows,
           'mover_rows', sm.mover_rows,
           'row_version', sm.row_version,
           'created_at', DATE_FORMAT(sm.created_at,'%Y-%m-%dT%H:%i:%s.%fZ')
         ), JSON_OBJECT())
    INTO v_manifest
    FROM mmo_save_checkpoint_manifests sm
   WHERE sm.manifest_id = v_manifest_id
   LIMIT 1;

  SET p_snapshot_json = CONCAT(
    '{',
    '"schema":', JSON_QUOTE('mmo_bootstrap_snapshot_v1'),
    ',"source":', JSON_QUOTE('mmo_udp_server_cpp_db_save_checkpoint'),
    ',"snapshot_source":', JSON_QUOTE('db_save_checkpoint_v1'),
    ',"db_save_checkpoint_manifest_uuid":', JSON_QUOTE(v_manifest_uuid),
    ',"session_uuid":', JSON_QUOTE(COALESCE(v_session_uuid,'')),
    ',"character_key":', JSON_QUOTE(COALESCE(v_character_key,'PC_HERO')),
    ',"world_name":', JSON_QUOTE(COALESCE(v_world_name,'newworld.zen')),
    ',"ready":true',
    ',"world_entity_count":', COALESCE(v_world_entity_count,0),
    ',"world_inventory_count":', COALESCE(v_world_inventory_count,0),
    ',"active_world_item_radius":', v_active_radius,
    ',"nearby_npc_radius":', v_nearby_npc_radius,
    ',"nearby_waypoint_radius":', v_nearby_waypoint_radius,
    ',"interactive_count":', COALESCE(v_interactive_count,0),
    ',"script_int_count":', COALESCE(v_script_int_count,0),
    ',"script_state_full_count":', COALESCE(v_script_full_count,0),
    ',"script_state_truncated":', IF(v_script_int_count > 16384 OR v_script_full_count > 20000, 'true', 'false'),
    ',"character":', COALESCE(v_character,'{}'),
    ',"inventory":', COALESCE(v_inventory,'[]'),
    ',"equipment":', COALESCE(v_equipment,'[]'),
    ',"known_dialogs":', COALESCE(v_known_dialogs,'[]'),
    ',"quests":', COALESCE(v_quests,'[]'),
    ',"script_state":', COALESCE(v_script_state,'[]'),
    ',"script_state_full":', COALESCE(v_script_state_full,'[]'),
    ',"world_clock":', COALESCE(v_world_clock,'{}'),
    ',"active_world_items":', COALESCE(v_active_world_items,'[]'),
    ',"world_inventory_sample":', COALESCE(v_active_world_items,'[]'),
    ',"nearby_npcs":', COALESCE(v_nearby_npcs,'[]'),
    ',"nearby_npc_known_dialogs":', COALESCE(v_nearby_npc_known_dialogs,'[]'),
    ',"nearby_waypoints":', COALESCE(v_nearby_waypoints,'[]'),
    ',"interactive_state":', COALESCE(v_interactive_state,'[]'),
    ',"interactive_sample":[]',
    ',"npc_lifecycle_state":', COALESCE(v_npc_lifecycle_state,'[]'),
    ',"world_item_deltas":', COALESCE(v_world_item_deltas,'[]'),
    ',"world_entity_delta_sample":[]',
    ',"recent_actions":', COALESCE(v_recent_actions,'[]'),
    ',"recent_events_sample":', COALESCE(v_recent_actions,'[]'),
    ',"mover_state":', COALESCE(v_mover_state,'[]'),
    ',"server_checkpoint_manifest":', COALESCE(v_manifest,'{}'),
    ',"server_note":', JSON_QUOTE('server-bound client materialized from the latest DB-native save checkpoint snapshot; native .sav remains compatibility/debug cache'),
    '}'
  );
END build_snapshot//

CREATE PROCEDURE mmo_validate_latest_save_checkpoint_restore_v1(
  IN p_session_id BINARY(16),
  OUT p_validation_json LONGTEXT
)
BEGIN
  DECLARE v_session_found TINYINT(1) DEFAULT 0;
  DECLARE v_manifest_id BINARY(16) DEFAULT NULL;
  DECLARE v_manifest_uuid VARCHAR(36) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_world_instance_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_save_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_display_name VARCHAR(191) DEFAULT NULL;
  DECLARE v_client_world_name VARCHAR(191) DEFAULT NULL;
  DECLARE v_native_save_present TINYINT(1) DEFAULT 0;
  DECLARE v_created_at DATETIME(6) DEFAULT NULL;
  DECLARE v_character_rows BIGINT DEFAULT 0;
  DECLARE v_inventory_rows BIGINT DEFAULT 0;
  DECLARE v_equipment_rows BIGINT DEFAULT 0;
  DECLARE v_quest_rows BIGINT DEFAULT 0;
  DECLARE v_known_dialog_rows BIGINT DEFAULT 0;
  DECLARE v_script_state_rows BIGINT DEFAULT 0;
  DECLARE v_world_entity_rows BIGINT DEFAULT 0;
  DECLARE v_world_inventory_rows BIGINT DEFAULT 0;
  DECLARE v_world_clock_rows BIGINT DEFAULT 0;
  DECLARE v_mover_rows BIGINT DEFAULT 0;
  DECLARE v_snapshot LONGTEXT DEFAULT NULL;
  DECLARE v_exported_bootstrap_bytes BIGINT DEFAULT 0;
  DECLARE v_snapshot_source VARCHAR(96) DEFAULT NULL;
  DECLARE v_strict_ok TINYINT(1) DEFAULT 0;
  DECLARE v_reason VARCHAR(191) DEFAULT 'unknown';

  SELECT COUNT(*) > 0
    INTO v_session_found
    FROM server_sessions
   WHERE session_id = p_session_id;

  IF v_session_found THEN
    SELECT sm.manifest_id,
           BIN_TO_UUID(sm.manifest_id, 1),
           c.character_key,
           rwi.world_instance_key,
           COALESCE(sm.save_slot_key, sm.manifest_key),
           sm.display_name,
           sm.client_world_name,
           sm.native_save_present,
           sm.created_at
      INTO v_manifest_id,
           v_manifest_uuid,
           v_character_key,
           v_world_instance_key,
           v_save_key,
           v_display_name,
           v_client_world_name,
           v_native_save_present,
           v_created_at
      FROM server_sessions ss
      JOIN mmo_save_checkpoint_manifests sm
        ON sm.character_id = ss.character_id
       AND sm.world_instance_id = ss.world_instance_id
      JOIN characters c
        ON c.character_id = sm.character_id
      JOIN realm_world_instances rwi
        ON rwi.world_instance_id = sm.world_instance_id
     WHERE ss.session_id = p_session_id
     ORDER BY sm.created_at DESC, sm.row_version DESC
     LIMIT 1;
  END IF;

  IF v_manifest_id IS NOT NULL THEN
    SELECT COUNT(*) INTO v_character_rows
      FROM mmo_save_checkpoint_character_snapshot
     WHERE manifest_id = v_manifest_id;

    SELECT COUNT(*) INTO v_inventory_rows
      FROM mmo_save_checkpoint_inventory_snapshot
     WHERE manifest_id = v_manifest_id;

    SELECT COUNT(*) INTO v_equipment_rows
      FROM mmo_save_checkpoint_equipment_snapshot
     WHERE manifest_id = v_manifest_id;

    SELECT COUNT(*) INTO v_quest_rows
      FROM mmo_save_checkpoint_quest_snapshot
     WHERE manifest_id = v_manifest_id;

    SELECT COUNT(*) INTO v_known_dialog_rows
      FROM mmo_save_checkpoint_known_dialog_snapshot
     WHERE manifest_id = v_manifest_id;

    SELECT COUNT(*) INTO v_script_state_rows
      FROM mmo_save_checkpoint_script_state_snapshot
     WHERE manifest_id = v_manifest_id;

    SELECT COUNT(*) INTO v_world_entity_rows
      FROM mmo_save_checkpoint_world_entity_snapshot
     WHERE manifest_id = v_manifest_id;

    SELECT COUNT(*) INTO v_world_inventory_rows
      FROM mmo_save_checkpoint_world_inventory_snapshot
     WHERE manifest_id = v_manifest_id;

    SELECT COUNT(*) INTO v_world_clock_rows
      FROM mmo_save_checkpoint_world_clock_snapshot
     WHERE manifest_id = v_manifest_id;

    SELECT COUNT(*) INTO v_mover_rows
      FROM mmo_save_checkpoint_mover_snapshot
     WHERE manifest_id = v_manifest_id;

    CALL mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1(p_session_id, v_snapshot);
    SET v_exported_bootstrap_bytes = COALESCE(CHAR_LENGTH(v_snapshot), 0);
    SET v_snapshot_source = JSON_UNQUOTE(JSON_EXTRACT(v_snapshot, '$.snapshot_source'));
  END IF;

  IF NOT v_session_found THEN
    SET v_reason = 'session_not_found';
  ELSEIF v_manifest_id IS NULL THEN
    SET v_reason = 'missing_db_save_checkpoint_manifest';
  ELSEIF v_character_rows <> 1 THEN
    SET v_reason = 'missing_character_snapshot';
  ELSEIF v_exported_bootstrap_bytes <= 0 THEN
    SET v_reason = 'empty_bootstrap_snapshot_export';
  ELSEIF COALESCE(v_snapshot_source, '') <> 'db_save_checkpoint_v1' THEN
    SET v_reason = 'bootstrap_snapshot_is_not_db_save_checkpoint';
  ELSE
    SET v_reason = 'ok';
    SET v_strict_ok = 1;
  END IF;

  SET p_validation_json = JSON_OBJECT(
    'strict_restore_ok', IF(v_strict_ok = 1, JSON_EXTRACT('true', '$'), JSON_EXTRACT('false', '$')),
    'reason', v_reason,
    'session_found', IF(v_session_found = 1, JSON_EXTRACT('true', '$'), JSON_EXTRACT('false', '$')),
    'session_uuid', BIN_TO_UUID(p_session_id, 1),
    'manifest_uuid', v_manifest_uuid,
    'character_key', v_character_key,
    'world_instance_key', v_world_instance_key,
    'save_key', v_save_key,
    'display_name', v_display_name,
    'client_world_name', v_client_world_name,
    'native_save_present', IF(v_native_save_present = 1, JSON_EXTRACT('true', '$'), JSON_EXTRACT('false', '$')),
    'created_at', IF(v_created_at IS NULL, NULL, DATE_FORMAT(v_created_at, '%Y-%m-%dT%H:%i:%s.%fZ')),
    'snapshot_source', v_snapshot_source,
    'exported_bootstrap_bytes', v_exported_bootstrap_bytes,
    'character_rows', v_character_rows,
    'inventory_rows', v_inventory_rows,
    'equipment_rows', v_equipment_rows,
    'quest_rows', v_quest_rows,
    'known_dialog_rows', v_known_dialog_rows,
    'script_state_rows', v_script_state_rows,
    'world_entity_rows', v_world_entity_rows,
    'world_inventory_rows', v_world_inventory_rows,
    'world_clock_rows', v_world_clock_rows,
    'mover_rows', v_mover_rows
  );
END//

CREATE PROCEDURE mmo_assert_latest_save_checkpoint_restore_v1(
  IN p_session_id BINARY(16),
  OUT p_validation_json LONGTEXT
)
BEGIN
  CALL mmo_validate_latest_save_checkpoint_restore_v1(p_session_id, p_validation_json);

  IF COALESCE(JSON_UNQUOTE(JSON_EXTRACT(p_validation_json, '$.strict_restore_ok')), 'false') <> 'true' THEN
    SIGNAL SQLSTATE '45000'
      SET MESSAGE_TEXT = 'latest DB save checkpoint is not strict-restore ready';
  END IF;
END//

CREATE OR REPLACE VIEW v_mmo_latest_save_checkpoint_strict_restore AS
SELECT
  BIN_TO_UUID(ss.session_id, 1) AS session_uuid,
  ss.session_key,
  c.character_key,
  rwi.world_instance_key,
  BIN_TO_UUID(sm.manifest_id, 1) AS manifest_uuid,
  COALESCE(sm.save_slot_key, sm.manifest_key) AS save_key,
  sm.display_name,
  sm.client_world_name,
  sm.native_save_present,
  sm.created_at,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_character_snapshot s WHERE s.manifest_id = sm.manifest_id) AS character_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_inventory_snapshot s WHERE s.manifest_id = sm.manifest_id) AS inventory_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_equipment_snapshot s WHERE s.manifest_id = sm.manifest_id) AS equipment_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_quest_snapshot s WHERE s.manifest_id = sm.manifest_id) AS quest_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_known_dialog_snapshot s WHERE s.manifest_id = sm.manifest_id) AS known_dialog_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_script_state_snapshot s WHERE s.manifest_id = sm.manifest_id) AS script_state_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_world_entity_snapshot s WHERE s.manifest_id = sm.manifest_id) AS world_entity_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_world_inventory_snapshot s WHERE s.manifest_id = sm.manifest_id) AS world_inventory_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_world_clock_snapshot s WHERE s.manifest_id = sm.manifest_id) AS world_clock_rows,
  (SELECT COUNT(*) FROM mmo_save_checkpoint_mover_snapshot s WHERE s.manifest_id = sm.manifest_id) AS mover_rows,
  CAST(NULL AS UNSIGNED) AS exported_bootstrap_bytes,
  CAST(NULL AS CHAR(96)) AS snapshot_source,
  CASE
    WHEN (SELECT COUNT(*) FROM mmo_save_checkpoint_character_snapshot s WHERE s.manifest_id = sm.manifest_id) = 1 THEN 1
    ELSE 0
  END AS strict_restore_ok
FROM server_sessions ss
JOIN characters c
  ON c.character_id = ss.character_id
JOIN realm_world_instances rwi
  ON rwi.world_instance_id = ss.world_instance_id
JOIN mmo_save_checkpoint_manifests sm
  ON sm.character_id = ss.character_id
 AND sm.world_instance_id = ss.world_instance_id
WHERE sm.manifest_id = (
  SELECT sm2.manifest_id
    FROM mmo_save_checkpoint_manifests sm2
   WHERE sm2.character_id = ss.character_id
     AND sm2.world_instance_id = ss.world_instance_id
   ORDER BY sm2.created_at DESC, sm2.row_version DESC
   LIMIT 1
)//

DELIMITER ;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES(
  'step104_db_checkpoint_script_state_full_export',
  'db_save_checkpoint_script_state_full_export_proc_v2',
  'Exports safe int script_state for current client apply and full script_state_full for DB-native save checkpoint coverage. Uses PROCEDURE OUT instead of CREATE FUNCTION, so MySQL binary logging does not require SUPER/log_bin_trust_function_creators.'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);

-- ============================================================================
-- END server/sql/step104_db_checkpoint_script_state_full_export.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step108_db_checkpoint_world_clock_foundation.sql
-- ============================================================================

-- Step108: make the procedure-only DB checkpoint export path self-contained.
-- MySQL with binary logging can reject legacy CREATE FUNCTION bridges, so clean
-- reset skips Step97/Step98 and installs the final procedure path instead. The
-- world-clock snapshot table used to live behind that legacy bridge; keep it in
-- the procedure path so Step104/Step106 can be applied from a clean database.

CREATE TABLE IF NOT EXISTS mmo_save_checkpoint_world_clock_snapshot (
  manifest_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  world_day INT DEFAULT NULL,
  world_time_ms BIGINT DEFAULT NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  source_row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (manifest_id),
  KEY ix_mmo_save_world_clock_snapshot_world (world_instance_id),
  CONSTRAINT mmo_save_world_clock_snapshot_manifest_fk
    FOREIGN KEY (manifest_id)
    REFERENCES mmo_save_checkpoint_manifests(manifest_id)
    ON DELETE CASCADE,
  CONSTRAINT mmo_save_world_clock_snapshot_world_fk
    FOREIGN KEY (world_instance_id)
    REFERENCES realm_world_instances(world_instance_id)
    ON DELETE CASCADE,
  CONSTRAINT mmo_save_world_clock_snapshot_payload_json_ck
    CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_materialize_save_checkpoint_world_clock_snapshot_v1;
DELIMITER ;;
CREATE PROCEDURE mmo_materialize_save_checkpoint_world_clock_snapshot_v1(
  IN p_manifest_id BINARY(16)
)
proc: BEGIN
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;

  SELECT world_instance_id
    INTO v_world_instance_id
    FROM mmo_save_checkpoint_manifests
   WHERE manifest_id = p_manifest_id
   LIMIT 1;

  IF v_world_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000'
      SET MESSAGE_TEXT='mmo_materialize_save_checkpoint_world_clock_snapshot_v1: invalid manifest';
  END IF;

  DELETE FROM mmo_save_checkpoint_world_clock_snapshot
   WHERE manifest_id = p_manifest_id;

  INSERT INTO mmo_save_checkpoint_world_clock_snapshot(
    manifest_id, world_instance_id, world_day, world_time_ms,
    last_server_tick, state_payload, source_row_version
  )
  SELECT p_manifest_id,
         v_world_instance_id,
         world_day,
         world_time_ms,
         last_server_tick,
         state_payload,
         row_version
    FROM mmo_world_clock_state_current
   WHERE world_instance_id = v_world_instance_id
   LIMIT 1;

  IF ROW_COUNT() = 0 THEN
    INSERT INTO mmo_save_checkpoint_world_clock_snapshot(
      manifest_id, world_instance_id, world_day, world_time_ms,
      last_server_tick, state_payload, source_row_version
    )
    SELECT p_manifest_id,
           rwi.world_instance_id,
           CAST(FLOOR(COALESCE(rwi.current_world_time_ms, 0) / 86400000) AS SIGNED),
           COALESCE(rwi.current_world_time_ms, 0),
           CAST(GREATEST(COALESCE(rwi.current_tick, 0), 0) AS UNSIGNED),
           JSON_OBJECT(
             'source', 'realm_world_instances_fallback',
             'current_tick', COALESCE(rwi.current_tick, 0),
             'current_world_time_ms', COALESCE(rwi.current_world_time_ms, 0)
           ),
           1
      FROM realm_world_instances rwi
     WHERE rwi.world_instance_id = v_world_instance_id
     LIMIT 1;
  END IF;
END ;;
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_create_db_save_checkpoint_v1;
DELIMITER ;;
CREATE PROCEDURE mmo_create_db_save_checkpoint_v1(
  IN p_session_id BINARY(16),
  IN p_manifest_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci,
  IN p_checkpoint_kind VARCHAR(64),
  IN p_reason VARCHAR(128),
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci,
  OUT p_manifest_id BINARY(16),
  OUT p_event_id BINARY(16),
  OUT p_row_version_after BIGINT UNSIGNED
)
BEGIN
  CALL mmo_create_save_checkpoint_manifest(
    p_session_id,
    p_manifest_key,
    p_checkpoint_kind,
    p_reason,
    p_server_tick,
    p_metadata,
    p_idempotency_key,
    p_manifest_id,
    p_event_id,
    p_row_version_after
  );

  CALL mmo_materialize_save_checkpoint_snapshot_v1(p_manifest_id);
  CALL mmo_materialize_save_checkpoint_world_clock_snapshot_v1(p_manifest_id);
END ;;
DELIMITER ;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES(
  'step108_db_checkpoint_world_clock_foundation',
  'db_checkpoint_world_clock_snapshot_v1',
  'Creates the world-clock checkpoint snapshot table and keeps procedure-only DB save checkpoints self-contained without legacy CREATE FUNCTION bridges.'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);

-- ============================================================================
-- END server/sql/step108_db_checkpoint_world_clock_foundation.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step120_npc_authority_restore_bridge.sql
-- ============================================================================

CREATE TABLE IF NOT EXISTS mmo_npc_routine_state_current (
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(191) NOT NULL,
  routine_state VARCHAR(128) NOT NULL,
  schedule_key VARCHAR(191) NOT NULL DEFAULT '',
  current_waypoint_key VARCHAR(191) NOT NULL DEFAULT '',
  target_waypoint_key VARCHAR(191) NOT NULL DEFAULT '',
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  metadata JSON NOT NULL,
  idempotency_key VARCHAR(191) NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (world_instance_id, npc_entity_key),
  KEY ix_mmo_npc_routine_tick (world_instance_id, last_server_tick),
  CONSTRAINT mmo_npc_routine_world_fk FOREIGN KEY (world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT mmo_npc_routine_metadata_json_ck CHECK (JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_routine_state_history (
  history_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  event_id BINARY(16) DEFAULT NULL,
  session_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(191) NOT NULL,
  routine_state VARCHAR(128) NOT NULL,
  schedule_key VARCHAR(191) NOT NULL DEFAULT '',
  current_waypoint_key VARCHAR(191) NOT NULL DEFAULT '',
  target_waypoint_key VARCHAR(191) NOT NULL DEFAULT '',
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  metadata JSON NOT NULL,
  idempotency_key VARCHAR(191) NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (history_id),
  UNIQUE KEY ux_mmo_npc_routine_history_idem (world_instance_id, idempotency_key),
  KEY ix_mmo_npc_routine_history_npc (world_instance_id, npc_entity_key, captured_at),
  CONSTRAINT mmo_npc_routine_history_session_fk FOREIGN KEY (session_id) REFERENCES server_sessions(session_id) ON DELETE CASCADE,
  CONSTRAINT mmo_npc_routine_history_world_fk FOREIGN KEY (world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT mmo_npc_routine_history_metadata_json_ck CHECK (JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_ai_state_current (
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(191) NOT NULL,
  ai_state VARCHAR(191) NOT NULL,
  ai_intent VARCHAR(128) NOT NULL DEFAULT '',
  target_key VARCHAR(191) NOT NULL DEFAULT '',
  perception_state VARCHAR(512) NOT NULL DEFAULT '',
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  metadata JSON NOT NULL,
  idempotency_key VARCHAR(191) NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (world_instance_id, npc_entity_key),
  KEY ix_mmo_npc_ai_tick (world_instance_id, last_server_tick),
  CONSTRAINT mmo_npc_ai_world_fk FOREIGN KEY (world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT mmo_npc_ai_metadata_json_ck CHECK (JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_ai_state_history (
  history_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  event_id BINARY(16) DEFAULT NULL,
  session_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(191) NOT NULL,
  ai_state VARCHAR(191) NOT NULL,
  ai_intent VARCHAR(128) NOT NULL DEFAULT '',
  target_key VARCHAR(191) NOT NULL DEFAULT '',
  perception_state VARCHAR(512) NOT NULL DEFAULT '',
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  metadata JSON NOT NULL,
  idempotency_key VARCHAR(191) NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (history_id),
  UNIQUE KEY ux_mmo_npc_ai_history_idem (world_instance_id, idempotency_key),
  KEY ix_mmo_npc_ai_history_npc (world_instance_id, npc_entity_key, captured_at),
  CONSTRAINT mmo_npc_ai_history_session_fk FOREIGN KEY (session_id) REFERENCES server_sessions(session_id) ON DELETE CASCADE,
  CONSTRAINT mmo_npc_ai_history_world_fk FOREIGN KEY (world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT mmo_npc_ai_history_metadata_json_ck CHECK (JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_path_state_current (
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(191) NOT NULL,
  path_state VARCHAR(128) NOT NULL,
  route_key VARCHAR(191) NOT NULL DEFAULT '',
  current_waypoint_key VARCHAR(191) NOT NULL DEFAULT '',
  next_waypoint_key VARCHAR(191) NOT NULL DEFAULT '',
  target_waypoint_key VARCHAR(191) NOT NULL DEFAULT '',
  pos_x DOUBLE DEFAULT NULL,
  pos_y DOUBLE DEFAULT NULL,
  pos_z DOUBLE DEFAULT NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  metadata JSON NOT NULL,
  idempotency_key VARCHAR(191) NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (world_instance_id, npc_entity_key),
  KEY ix_mmo_npc_path_tick (world_instance_id, last_server_tick),
  CONSTRAINT mmo_npc_path_world_fk FOREIGN KEY (world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT mmo_npc_path_metadata_json_ck CHECK (JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_path_state_history (
  history_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  event_id BINARY(16) DEFAULT NULL,
  session_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(191) NOT NULL,
  path_state VARCHAR(128) NOT NULL,
  route_key VARCHAR(191) NOT NULL DEFAULT '',
  current_waypoint_key VARCHAR(191) NOT NULL DEFAULT '',
  next_waypoint_key VARCHAR(191) NOT NULL DEFAULT '',
  target_waypoint_key VARCHAR(191) NOT NULL DEFAULT '',
  pos_x DOUBLE DEFAULT NULL,
  pos_y DOUBLE DEFAULT NULL,
  pos_z DOUBLE DEFAULT NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  metadata JSON NOT NULL,
  idempotency_key VARCHAR(191) NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (history_id),
  UNIQUE KEY ux_mmo_npc_path_history_idem (world_instance_id, idempotency_key),
  KEY ix_mmo_npc_path_history_npc (world_instance_id, npc_entity_key, captured_at),
  CONSTRAINT mmo_npc_path_history_session_fk FOREIGN KEY (session_id) REFERENCES server_sessions(session_id) ON DELETE CASCADE,
  CONSTRAINT mmo_npc_path_history_world_fk FOREIGN KEY (world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT mmo_npc_path_history_metadata_json_ck CHECK (JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_fight_state_current (
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(191) NOT NULL,
  opponent_key VARCHAR(191) NOT NULL DEFAULT '',
  fight_state VARCHAR(128) NOT NULL,
  attack_state VARCHAR(128) NOT NULL DEFAULT '',
  combo_index INT NOT NULL DEFAULT 0,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  metadata JSON NOT NULL,
  idempotency_key VARCHAR(191) NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (world_instance_id, npc_entity_key),
  KEY ix_mmo_npc_fight_tick (world_instance_id, last_server_tick),
  CONSTRAINT mmo_npc_fight_world_fk FOREIGN KEY (world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT mmo_npc_fight_metadata_json_ck CHECK (JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_fight_state_history (
  history_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  event_id BINARY(16) DEFAULT NULL,
  session_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(191) NOT NULL,
  opponent_key VARCHAR(191) NOT NULL DEFAULT '',
  fight_state VARCHAR(128) NOT NULL,
  attack_state VARCHAR(128) NOT NULL DEFAULT '',
  combo_index INT NOT NULL DEFAULT 0,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  metadata JSON NOT NULL,
  idempotency_key VARCHAR(191) NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (history_id),
  UNIQUE KEY ux_mmo_npc_fight_history_idem (world_instance_id, idempotency_key),
  KEY ix_mmo_npc_fight_history_npc (world_instance_id, npc_entity_key, captured_at),
  CONSTRAINT mmo_npc_fight_history_session_fk FOREIGN KEY (session_id) REFERENCES server_sessions(session_id) ON DELETE CASCADE,
  CONSTRAINT mmo_npc_fight_history_world_fk FOREIGN KEY (world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT mmo_npc_fight_history_metadata_json_ck CHECK (JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DELIMITER $$

DROP PROCEDURE IF EXISTS mmo_record_npc_routine_state$$
CREATE PROCEDURE mmo_record_npc_routine_state(
  IN p_session_id BINARY(16),
  IN p_npc_entity_key VARCHAR(191),
  IN p_routine_state VARCHAR(128),
  IN p_schedule_key VARCHAR(191),
  IN p_current_waypoint_key VARCHAR(191),
  IN p_target_waypoint_key VARCHAR(191),
  IN p_last_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(191),
  OUT p_event_id BINARY(16),
  OUT p_row_version_after BIGINT UNSIGNED
)
BEGIN
  DECLARE v_world_instance_id BINARY(16);
  SET p_event_id = UUID_TO_BIN(UUID(), 1);
  SELECT world_instance_id INTO v_world_instance_id FROM server_sessions WHERE session_id = p_session_id LIMIT 1;

  INSERT INTO mmo_npc_routine_state_current(
    world_instance_id, npc_entity_key, routine_state, schedule_key, current_waypoint_key,
    target_waypoint_key, last_server_tick, metadata, idempotency_key, row_version
  )
  VALUES (
    v_world_instance_id, p_npc_entity_key, p_routine_state, COALESCE(p_schedule_key, ''), COALESCE(p_current_waypoint_key, ''),
    COALESCE(p_target_waypoint_key, ''), COALESCE(p_last_server_tick, 0), COALESCE(p_metadata, JSON_OBJECT()), p_idempotency_key, 1
  )
  ON DUPLICATE KEY UPDATE
    routine_state = VALUES(routine_state),
    schedule_key = VALUES(schedule_key),
    current_waypoint_key = VALUES(current_waypoint_key),
    target_waypoint_key = VALUES(target_waypoint_key),
    last_server_tick = VALUES(last_server_tick),
    metadata = VALUES(metadata),
    idempotency_key = VALUES(idempotency_key),
    row_version = row_version + 1;

  SELECT row_version INTO p_row_version_after
    FROM mmo_npc_routine_state_current
   WHERE world_instance_id = v_world_instance_id AND npc_entity_key = p_npc_entity_key;

  INSERT IGNORE INTO mmo_npc_routine_state_history(
    event_id, session_id, world_instance_id, npc_entity_key, routine_state, schedule_key,
    current_waypoint_key, target_waypoint_key, last_server_tick, metadata, idempotency_key, row_version
  )
  SELECT p_event_id, p_session_id, world_instance_id, npc_entity_key, routine_state, schedule_key,
         current_waypoint_key, target_waypoint_key, last_server_tick, metadata, idempotency_key, row_version
    FROM mmo_npc_routine_state_current
   WHERE world_instance_id = v_world_instance_id AND npc_entity_key = p_npc_entity_key;
END$$

DROP PROCEDURE IF EXISTS mmo_record_npc_ai_state$$
CREATE PROCEDURE mmo_record_npc_ai_state(
  IN p_session_id BINARY(16),
  IN p_npc_entity_key VARCHAR(191),
  IN p_ai_state VARCHAR(191),
  IN p_ai_intent VARCHAR(128),
  IN p_target_key VARCHAR(191),
  IN p_perception_state VARCHAR(512),
  IN p_last_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(191),
  OUT p_event_id BINARY(16),
  OUT p_row_version_after BIGINT UNSIGNED
)
BEGIN
  DECLARE v_world_instance_id BINARY(16);
  SET p_event_id = UUID_TO_BIN(UUID(), 1);
  SELECT world_instance_id INTO v_world_instance_id FROM server_sessions WHERE session_id = p_session_id LIMIT 1;

  INSERT INTO mmo_npc_ai_state_current(
    world_instance_id, npc_entity_key, ai_state, ai_intent, target_key,
    perception_state, last_server_tick, metadata, idempotency_key, row_version
  )
  VALUES (
    v_world_instance_id, p_npc_entity_key, p_ai_state, COALESCE(p_ai_intent, ''), COALESCE(p_target_key, ''),
    COALESCE(p_perception_state, ''), COALESCE(p_last_server_tick, 0), COALESCE(p_metadata, JSON_OBJECT()), p_idempotency_key, 1
  )
  ON DUPLICATE KEY UPDATE
    ai_state = VALUES(ai_state),
    ai_intent = VALUES(ai_intent),
    target_key = VALUES(target_key),
    perception_state = VALUES(perception_state),
    last_server_tick = VALUES(last_server_tick),
    metadata = VALUES(metadata),
    idempotency_key = VALUES(idempotency_key),
    row_version = row_version + 1;

  SELECT row_version INTO p_row_version_after
    FROM mmo_npc_ai_state_current
   WHERE world_instance_id = v_world_instance_id AND npc_entity_key = p_npc_entity_key;

  INSERT IGNORE INTO mmo_npc_ai_state_history(
    event_id, session_id, world_instance_id, npc_entity_key, ai_state, ai_intent,
    target_key, perception_state, last_server_tick, metadata, idempotency_key, row_version
  )
  SELECT p_event_id, p_session_id, world_instance_id, npc_entity_key, ai_state, ai_intent,
         target_key, perception_state, last_server_tick, metadata, idempotency_key, row_version
    FROM mmo_npc_ai_state_current
   WHERE world_instance_id = v_world_instance_id AND npc_entity_key = p_npc_entity_key;
END$$

DROP PROCEDURE IF EXISTS mmo_record_npc_path_state$$
CREATE PROCEDURE mmo_record_npc_path_state(
  IN p_session_id BINARY(16),
  IN p_npc_entity_key VARCHAR(191),
  IN p_path_state VARCHAR(128),
  IN p_route_key VARCHAR(191),
  IN p_current_waypoint_key VARCHAR(191),
  IN p_next_waypoint_key VARCHAR(191),
  IN p_target_waypoint_key VARCHAR(191),
  IN p_pos_x DOUBLE,
  IN p_pos_y DOUBLE,
  IN p_pos_z DOUBLE,
  IN p_last_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(191),
  OUT p_event_id BINARY(16),
  OUT p_row_version_after BIGINT UNSIGNED
)
BEGIN
  DECLARE v_world_instance_id BINARY(16);
  SET p_event_id = UUID_TO_BIN(UUID(), 1);
  SELECT world_instance_id INTO v_world_instance_id FROM server_sessions WHERE session_id = p_session_id LIMIT 1;

  INSERT INTO mmo_npc_path_state_current(
    world_instance_id, npc_entity_key, path_state, route_key, current_waypoint_key,
    next_waypoint_key, target_waypoint_key, pos_x, pos_y, pos_z,
    last_server_tick, metadata, idempotency_key, row_version
  )
  VALUES (
    v_world_instance_id, p_npc_entity_key, p_path_state, COALESCE(p_route_key, ''), COALESCE(p_current_waypoint_key, ''),
    COALESCE(p_next_waypoint_key, ''), COALESCE(p_target_waypoint_key, ''), p_pos_x, p_pos_y, p_pos_z,
    COALESCE(p_last_server_tick, 0), COALESCE(p_metadata, JSON_OBJECT()), p_idempotency_key, 1
  )
  ON DUPLICATE KEY UPDATE
    path_state = VALUES(path_state),
    route_key = VALUES(route_key),
    current_waypoint_key = VALUES(current_waypoint_key),
    next_waypoint_key = VALUES(next_waypoint_key),
    target_waypoint_key = VALUES(target_waypoint_key),
    pos_x = VALUES(pos_x),
    pos_y = VALUES(pos_y),
    pos_z = VALUES(pos_z),
    last_server_tick = VALUES(last_server_tick),
    metadata = VALUES(metadata),
    idempotency_key = VALUES(idempotency_key),
    row_version = row_version + 1;

  SELECT row_version INTO p_row_version_after
    FROM mmo_npc_path_state_current
   WHERE world_instance_id = v_world_instance_id AND npc_entity_key = p_npc_entity_key;

  INSERT IGNORE INTO mmo_npc_path_state_history(
    event_id, session_id, world_instance_id, npc_entity_key, path_state, route_key,
    current_waypoint_key, next_waypoint_key, target_waypoint_key, pos_x, pos_y, pos_z,
    last_server_tick, metadata, idempotency_key, row_version
  )
  SELECT p_event_id, p_session_id, world_instance_id, npc_entity_key, path_state, route_key,
         current_waypoint_key, next_waypoint_key, target_waypoint_key, pos_x, pos_y, pos_z,
         last_server_tick, metadata, idempotency_key, row_version
    FROM mmo_npc_path_state_current
   WHERE world_instance_id = v_world_instance_id AND npc_entity_key = p_npc_entity_key;
END$$

DROP PROCEDURE IF EXISTS mmo_record_npc_fight_state$$
CREATE PROCEDURE mmo_record_npc_fight_state(
  IN p_session_id BINARY(16),
  IN p_npc_entity_key VARCHAR(191),
  IN p_opponent_key VARCHAR(191),
  IN p_fight_state VARCHAR(128),
  IN p_attack_state VARCHAR(128),
  IN p_combo_index INT,
  IN p_last_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(191),
  OUT p_event_id BINARY(16),
  OUT p_row_version_after BIGINT UNSIGNED
)
BEGIN
  DECLARE v_world_instance_id BINARY(16);
  SET p_event_id = UUID_TO_BIN(UUID(), 1);
  SELECT world_instance_id INTO v_world_instance_id FROM server_sessions WHERE session_id = p_session_id LIMIT 1;

  INSERT INTO mmo_npc_fight_state_current(
    world_instance_id, npc_entity_key, opponent_key, fight_state, attack_state,
    combo_index, last_server_tick, metadata, idempotency_key, row_version
  )
  VALUES (
    v_world_instance_id, p_npc_entity_key, COALESCE(p_opponent_key, ''), p_fight_state, COALESCE(p_attack_state, ''),
    COALESCE(p_combo_index, 0), COALESCE(p_last_server_tick, 0), COALESCE(p_metadata, JSON_OBJECT()), p_idempotency_key, 1
  )
  ON DUPLICATE KEY UPDATE
    opponent_key = VALUES(opponent_key),
    fight_state = VALUES(fight_state),
    attack_state = VALUES(attack_state),
    combo_index = VALUES(combo_index),
    last_server_tick = VALUES(last_server_tick),
    metadata = VALUES(metadata),
    idempotency_key = VALUES(idempotency_key),
    row_version = row_version + 1;

  SELECT row_version INTO p_row_version_after
    FROM mmo_npc_fight_state_current
   WHERE world_instance_id = v_world_instance_id AND npc_entity_key = p_npc_entity_key;

  INSERT IGNORE INTO mmo_npc_fight_state_history(
    event_id, session_id, world_instance_id, npc_entity_key, opponent_key, fight_state,
    attack_state, combo_index, last_server_tick, metadata, idempotency_key, row_version
  )
  SELECT p_event_id, p_session_id, world_instance_id, npc_entity_key, opponent_key, fight_state,
         attack_state, combo_index, last_server_tick, metadata, idempotency_key, row_version
    FROM mmo_npc_fight_state_current
   WHERE world_instance_id = v_world_instance_id AND npc_entity_key = p_npc_entity_key;
END$$

DELIMITER ;

-- ============================================================================
-- END server/sql/step120_npc_authority_restore_bridge.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step121_server_parity_state_bridge.sql
-- ============================================================================

CREATE TABLE IF NOT EXISTS mmo_world_trigger_queue_current (
  world_instance_id BINARY(16) NOT NULL,
  trigger_key VARCHAR(191) NOT NULL,
  queue_state VARCHAR(64) NOT NULL,
  event_type_name VARCHAR(128) NOT NULL,
  scheduled_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  metadata JSON NOT NULL,
  idempotency_key VARCHAR(191) NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (world_instance_id, trigger_key),
  KEY ix_mmo_trigger_queue_tick (world_instance_id, scheduled_server_tick),
  CONSTRAINT mmo_trigger_queue_world_fk FOREIGN KEY (world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT mmo_trigger_queue_metadata_json_ck CHECK (JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_world_trigger_queue_history (
  history_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  event_id BINARY(16) DEFAULT NULL,
  session_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  trigger_key VARCHAR(191) NOT NULL,
  queue_state VARCHAR(64) NOT NULL,
  event_type_name VARCHAR(128) NOT NULL,
  scheduled_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  metadata JSON NOT NULL,
  idempotency_key VARCHAR(191) NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (history_id),
  KEY ix_mmo_trigger_queue_history_world (world_instance_id, captured_at),
  UNIQUE KEY ux_mmo_trigger_queue_history_idem (world_instance_id, idempotency_key),
  CONSTRAINT mmo_trigger_queue_history_session_fk FOREIGN KEY (session_id) REFERENCES server_sessions(session_id) ON DELETE CASCADE,
  CONSTRAINT mmo_trigger_queue_history_world_fk FOREIGN KEY (world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT mmo_trigger_queue_history_metadata_json_ck CHECK (JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_character_world_transition_state_current (
  character_id BINARY(16) NOT NULL,
  from_world_key VARCHAR(191) NOT NULL DEFAULT '',
  to_world_key VARCHAR(191) NOT NULL,
  transition_state VARCHAR(64) NOT NULL,
  chapter_key VARCHAR(128) NOT NULL DEFAULT '',
  visited TINYINT(1) NOT NULL DEFAULT 0,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  metadata JSON NOT NULL,
  idempotency_key VARCHAR(191) NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (character_id, to_world_key),
  KEY ix_mmo_world_transition_tick (character_id, last_server_tick),
  CONSTRAINT mmo_world_transition_character_fk FOREIGN KEY (character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT mmo_world_transition_metadata_json_ck CHECK (JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_character_world_transition_state_history (
  history_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  event_id BINARY(16) DEFAULT NULL,
  session_id BINARY(16) NOT NULL,
  character_id BINARY(16) NOT NULL,
  from_world_key VARCHAR(191) NOT NULL DEFAULT '',
  to_world_key VARCHAR(191) NOT NULL,
  transition_state VARCHAR(64) NOT NULL,
  chapter_key VARCHAR(128) NOT NULL DEFAULT '',
  visited TINYINT(1) NOT NULL DEFAULT 0,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  metadata JSON NOT NULL,
  idempotency_key VARCHAR(191) NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL,
  captured_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (history_id),
  KEY ix_mmo_world_transition_history_character (character_id, captured_at),
  UNIQUE KEY ux_mmo_world_transition_history_idem (character_id, idempotency_key),
  CONSTRAINT mmo_world_transition_history_session_fk FOREIGN KEY (session_id) REFERENCES server_sessions(session_id) ON DELETE CASCADE,
  CONSTRAINT mmo_world_transition_history_character_fk FOREIGN KEY (character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT mmo_world_transition_history_metadata_json_ck CHECK (JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_client_action_correction_current (
  correction_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  session_id BINARY(16) NOT NULL,
  character_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  action_kind VARCHAR(128) NOT NULL,
  client_local_sequence BIGINT UNSIGNED NOT NULL DEFAULT 0,
  correction_kind VARCHAR(128) NOT NULL,
  reason VARCHAR(191) NOT NULL,
  rejected_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  authoritative_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  authoritative_pos_x DOUBLE DEFAULT NULL,
  authoritative_pos_y DOUBLE DEFAULT NULL,
  authoritative_pos_z DOUBLE DEFAULT NULL,
  authoritative_yaw DOUBLE DEFAULT NULL,
  payload JSON NOT NULL,
  acknowledged TINYINT(1) NOT NULL DEFAULT 0,
  ack_server_tick BIGINT UNSIGNED DEFAULT NULL,
  ack_payload JSON DEFAULT NULL,
  idempotency_key VARCHAR(191) NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (correction_id),
  UNIQUE KEY ux_mmo_client_correction_idem (session_id, idempotency_key),
  KEY ix_mmo_client_correction_pending (session_id, acknowledged, updated_at),
  KEY ix_mmo_client_correction_action (session_id, action_kind, client_local_sequence),
  CONSTRAINT mmo_client_correction_session_fk FOREIGN KEY (session_id) REFERENCES server_sessions(session_id) ON DELETE CASCADE,
  CONSTRAINT mmo_client_correction_character_fk FOREIGN KEY (character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT mmo_client_correction_world_fk FOREIGN KEY (world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT mmo_client_correction_payload_json_ck CHECK (JSON_VALID(payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_client_action_correction_history LIKE mmo_client_action_correction_current;

DELIMITER $$

DROP PROCEDURE IF EXISTS mmo_record_trigger_queue_state$$
CREATE PROCEDURE mmo_record_trigger_queue_state(
  IN p_session_id BINARY(16),
  IN p_trigger_key VARCHAR(191),
  IN p_queue_state VARCHAR(64),
  IN p_event_type_name VARCHAR(128),
  IN p_scheduled_server_tick BIGINT UNSIGNED,
  IN p_last_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(191),
  OUT p_event_id BINARY(16),
  OUT p_row_version_after BIGINT UNSIGNED
)
BEGIN
  DECLARE v_world_instance_id BINARY(16);
  SET p_event_id = UUID_TO_BIN(UUID(), 1);
  SELECT world_instance_id INTO v_world_instance_id FROM server_sessions WHERE session_id = p_session_id LIMIT 1;

  INSERT INTO mmo_world_trigger_queue_current(
    world_instance_id, trigger_key, queue_state, event_type_name, scheduled_server_tick,
    last_server_tick, metadata, idempotency_key, row_version
  )
  VALUES (
    v_world_instance_id, p_trigger_key, p_queue_state, p_event_type_name, COALESCE(p_scheduled_server_tick, 0),
    COALESCE(p_last_server_tick, 0), COALESCE(p_metadata, JSON_OBJECT()), p_idempotency_key, 1
  )
  ON DUPLICATE KEY UPDATE
    queue_state = VALUES(queue_state),
    event_type_name = VALUES(event_type_name),
    scheduled_server_tick = VALUES(scheduled_server_tick),
    last_server_tick = VALUES(last_server_tick),
    metadata = VALUES(metadata),
    idempotency_key = VALUES(idempotency_key),
    row_version = row_version + 1;

  SELECT row_version INTO p_row_version_after
    FROM mmo_world_trigger_queue_current
   WHERE world_instance_id = v_world_instance_id AND trigger_key = p_trigger_key;

  INSERT IGNORE INTO mmo_world_trigger_queue_history(
    event_id, session_id, world_instance_id, trigger_key, queue_state, event_type_name,
    scheduled_server_tick, last_server_tick, metadata, idempotency_key, row_version
  )
  SELECT p_event_id, p_session_id, world_instance_id, trigger_key, queue_state, event_type_name,
         scheduled_server_tick, last_server_tick, metadata, idempotency_key, row_version
    FROM mmo_world_trigger_queue_current
   WHERE world_instance_id = v_world_instance_id AND trigger_key = p_trigger_key;
END$$

DROP PROCEDURE IF EXISTS mmo_record_world_transition_state$$
CREATE PROCEDURE mmo_record_world_transition_state(
  IN p_session_id BINARY(16),
  IN p_from_world_key VARCHAR(191),
  IN p_to_world_key VARCHAR(191),
  IN p_transition_state VARCHAR(64),
  IN p_chapter_key VARCHAR(128),
  IN p_visited TINYINT(1),
  IN p_last_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(191),
  OUT p_event_id BINARY(16),
  OUT p_row_version_after BIGINT UNSIGNED
)
BEGIN
  DECLARE v_character_id BINARY(16);
  SET p_event_id = UUID_TO_BIN(UUID(), 1);
  SELECT character_id INTO v_character_id FROM server_sessions WHERE session_id = p_session_id LIMIT 1;

  INSERT INTO mmo_character_world_transition_state_current(
    character_id, from_world_key, to_world_key, transition_state, chapter_key,
    visited, last_server_tick, metadata, idempotency_key, row_version
  )
  VALUES (
    v_character_id, COALESCE(p_from_world_key, ''), p_to_world_key, p_transition_state, COALESCE(p_chapter_key, ''),
    COALESCE(p_visited, 0), COALESCE(p_last_server_tick, 0), COALESCE(p_metadata, JSON_OBJECT()), p_idempotency_key, 1
  )
  ON DUPLICATE KEY UPDATE
    from_world_key = VALUES(from_world_key),
    transition_state = VALUES(transition_state),
    chapter_key = VALUES(chapter_key),
    visited = VALUES(visited),
    last_server_tick = VALUES(last_server_tick),
    metadata = VALUES(metadata),
    idempotency_key = VALUES(idempotency_key),
    row_version = row_version + 1;

  SELECT row_version INTO p_row_version_after
    FROM mmo_character_world_transition_state_current
   WHERE character_id = v_character_id AND to_world_key = p_to_world_key;

  INSERT IGNORE INTO mmo_character_world_transition_state_history(
    event_id, session_id, character_id, from_world_key, to_world_key, transition_state,
    chapter_key, visited, last_server_tick, metadata, idempotency_key, row_version
  )
  SELECT p_event_id, p_session_id, character_id, from_world_key, to_world_key, transition_state,
         chapter_key, visited, last_server_tick, metadata, idempotency_key, row_version
    FROM mmo_character_world_transition_state_current
   WHERE character_id = v_character_id AND to_world_key = p_to_world_key;
END$$

DROP PROCEDURE IF EXISTS mmo_record_client_action_correction$$
CREATE PROCEDURE mmo_record_client_action_correction(
  IN p_session_id BINARY(16),
  IN p_action_kind VARCHAR(128),
  IN p_client_local_sequence BIGINT UNSIGNED,
  IN p_correction_kind VARCHAR(128),
  IN p_reason VARCHAR(191),
  IN p_rejected_server_tick BIGINT UNSIGNED,
  IN p_payload JSON,
  IN p_idempotency_key VARCHAR(191),
  OUT p_event_id BINARY(16),
  OUT p_correction_id BINARY(16)
)
BEGIN
  DECLARE v_character_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  SET p_event_id = UUID_TO_BIN(UUID(), 1);
  SELECT character_id, world_instance_id INTO v_character_id, v_world_instance_id
    FROM server_sessions WHERE session_id = p_session_id LIMIT 1;

  INSERT INTO mmo_client_action_correction_current(
    session_id, character_id, world_instance_id, action_kind, client_local_sequence,
    correction_kind, reason, rejected_server_tick, authoritative_server_tick,
    authoritative_pos_x, authoritative_pos_y, authoritative_pos_z, authoritative_yaw,
    payload, acknowledged, idempotency_key, row_version
  )
  VALUES (
    p_session_id, v_character_id, v_world_instance_id, p_action_kind, COALESCE(p_client_local_sequence, 0),
    p_correction_kind, p_reason, COALESCE(p_rejected_server_tick, 0),
    COALESCE(JSON_UNQUOTE(JSON_EXTRACT(p_payload, '$.authoritative_server_tick')), p_rejected_server_tick, 0),
    JSON_UNQUOTE(JSON_EXTRACT(p_payload, '$.authoritative_pos_x')),
    JSON_UNQUOTE(JSON_EXTRACT(p_payload, '$.authoritative_pos_y')),
    JSON_UNQUOTE(JSON_EXTRACT(p_payload, '$.authoritative_pos_z')),
    JSON_UNQUOTE(JSON_EXTRACT(p_payload, '$.authoritative_yaw')),
    COALESCE(p_payload, JSON_OBJECT()), 0, p_idempotency_key, 1
  )
  ON DUPLICATE KEY UPDATE
    correction_kind = VALUES(correction_kind),
    reason = VALUES(reason),
    rejected_server_tick = VALUES(rejected_server_tick),
    authoritative_server_tick = VALUES(authoritative_server_tick),
    authoritative_pos_x = VALUES(authoritative_pos_x),
    authoritative_pos_y = VALUES(authoritative_pos_y),
    authoritative_pos_z = VALUES(authoritative_pos_z),
    authoritative_yaw = VALUES(authoritative_yaw),
    payload = VALUES(payload),
    acknowledged = 0,
    row_version = row_version + 1;

  SELECT correction_id INTO p_correction_id
    FROM mmo_client_action_correction_current
   WHERE session_id = p_session_id AND idempotency_key = p_idempotency_key
   LIMIT 1;

  INSERT IGNORE INTO mmo_client_action_correction_history
  SELECT * FROM mmo_client_action_correction_current WHERE correction_id = p_correction_id;
END$$

DROP PROCEDURE IF EXISTS mmo_ack_client_action_correction$$
CREATE PROCEDURE mmo_ack_client_action_correction(
  IN p_session_id BINARY(16),
  IN p_action_kind VARCHAR(128),
  IN p_client_local_sequence BIGINT UNSIGNED,
  IN p_ack_server_tick BIGINT UNSIGNED,
  IN p_ack_payload JSON,
  IN p_idempotency_key VARCHAR(191),
  OUT p_row_version_after BIGINT UNSIGNED
)
BEGIN
  UPDATE mmo_client_action_correction_current
     SET acknowledged = 1,
         ack_server_tick = COALESCE(p_ack_server_tick, ack_server_tick),
         ack_payload = COALESCE(p_ack_payload, JSON_OBJECT()),
         row_version = row_version + 1
   WHERE session_id = p_session_id
     AND action_kind = p_action_kind
     AND client_local_sequence = COALESCE(p_client_local_sequence, 0);

  SELECT COALESCE(MAX(row_version), 0) INTO p_row_version_after
    FROM mmo_client_action_correction_current
   WHERE session_id = p_session_id
     AND action_kind = p_action_kind
     AND client_local_sequence = COALESCE(p_client_local_sequence, 0);

  INSERT IGNORE INTO mmo_client_action_correction_history
  SELECT * FROM mmo_client_action_correction_current
   WHERE session_id = p_session_id
     AND action_kind = p_action_kind
     AND client_local_sequence = COALESCE(p_client_local_sequence, 0);
END$$

DELIMITER ;

-- ============================================================================
-- END server/sql/step121_server_parity_state_bridge.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step122_npc_observation_failopen_and_item_refresh_guard.sql
-- ============================================================================

-- Step122: keep NPC observation writes from poisoning live item refreshes.
--
-- The C++ server treats NPC routine/AI/path/fight samples as observations. They
-- must not reject gameplay packets or trigger corrective live snapshots that can
-- reintroduce freshly picked world items. This SQL keeps the DB side permissive
-- for the longer canonical keys emitted by live clients.

ALTER TABLE mmo_npc_ai_state_current
  MODIFY COLUMN npc_entity_key VARCHAR(512) NOT NULL,
  MODIFY COLUMN ai_state VARCHAR(191) NOT NULL,
  MODIFY COLUMN target_key VARCHAR(512) NOT NULL DEFAULT '',
  MODIFY COLUMN idempotency_key VARCHAR(512) NOT NULL;

ALTER TABLE mmo_npc_ai_state_history
  MODIFY COLUMN npc_entity_key VARCHAR(512) NOT NULL,
  MODIFY COLUMN ai_state VARCHAR(191) NOT NULL,
  MODIFY COLUMN target_key VARCHAR(512) NOT NULL DEFAULT '',
  MODIFY COLUMN idempotency_key VARCHAR(512) NOT NULL;

ALTER TABLE mmo_npc_fight_state_current
  MODIFY COLUMN npc_entity_key VARCHAR(512) NOT NULL,
  MODIFY COLUMN opponent_key VARCHAR(512) NOT NULL DEFAULT '',
  MODIFY COLUMN idempotency_key VARCHAR(512) NOT NULL;

ALTER TABLE mmo_npc_fight_state_history
  MODIFY COLUMN npc_entity_key VARCHAR(512) NOT NULL,
  MODIFY COLUMN opponent_key VARCHAR(512) NOT NULL DEFAULT '',
  MODIFY COLUMN idempotency_key VARCHAR(512) NOT NULL;

ALTER TABLE mmo_npc_path_state_current
  MODIFY COLUMN npc_entity_key VARCHAR(512) NOT NULL,
  MODIFY COLUMN route_key VARCHAR(512) NOT NULL DEFAULT '',
  MODIFY COLUMN current_waypoint_key VARCHAR(512) NOT NULL DEFAULT '',
  MODIFY COLUMN next_waypoint_key VARCHAR(512) NOT NULL DEFAULT '',
  MODIFY COLUMN target_waypoint_key VARCHAR(512) NOT NULL DEFAULT '',
  MODIFY COLUMN idempotency_key VARCHAR(512) NOT NULL;

ALTER TABLE mmo_npc_path_state_history
  MODIFY COLUMN npc_entity_key VARCHAR(512) NOT NULL,
  MODIFY COLUMN route_key VARCHAR(512) NOT NULL DEFAULT '',
  MODIFY COLUMN current_waypoint_key VARCHAR(512) NOT NULL DEFAULT '',
  MODIFY COLUMN next_waypoint_key VARCHAR(512) NOT NULL DEFAULT '',
  MODIFY COLUMN target_waypoint_key VARCHAR(512) NOT NULL DEFAULT '',
  MODIFY COLUMN idempotency_key VARCHAR(512) NOT NULL;

ALTER TABLE mmo_npc_routine_state_current
  MODIFY COLUMN npc_entity_key VARCHAR(512) NOT NULL,
  MODIFY COLUMN schedule_key VARCHAR(512) NOT NULL DEFAULT '',
  MODIFY COLUMN current_waypoint_key VARCHAR(512) NOT NULL DEFAULT '',
  MODIFY COLUMN target_waypoint_key VARCHAR(512) NOT NULL DEFAULT '',
  MODIFY COLUMN idempotency_key VARCHAR(512) NOT NULL;

ALTER TABLE mmo_npc_routine_state_history
  MODIFY COLUMN npc_entity_key VARCHAR(512) NOT NULL,
  MODIFY COLUMN schedule_key VARCHAR(512) NOT NULL DEFAULT '',
  MODIFY COLUMN current_waypoint_key VARCHAR(512) NOT NULL DEFAULT '',
  MODIFY COLUMN target_waypoint_key VARCHAR(512) NOT NULL DEFAULT '',
  MODIFY COLUMN idempotency_key VARCHAR(512) NOT NULL;

DROP PROCEDURE IF EXISTS mmo_record_npc_ai_state;
DELIMITER ;;
CREATE PROCEDURE mmo_record_npc_ai_state(
  IN p_session_id BINARY(16),
  IN p_npc_entity_key VARCHAR(512),
  IN p_ai_state VARCHAR(191),
  IN p_ai_intent VARCHAR(128),
  IN p_target_key VARCHAR(512),
  IN p_perception_state VARCHAR(512),
  IN p_last_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT p_event_id BINARY(16),
  OUT p_row_version_after BIGINT UNSIGNED
)
BEGIN
  DECLARE v_world_instance_id BINARY(16);
  SET p_event_id = UUID_TO_BIN(UUID(), 1);
  SELECT world_instance_id INTO v_world_instance_id FROM server_sessions WHERE session_id = p_session_id LIMIT 1;
  IF v_world_instance_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_npc_ai_state: invalid session'; END IF;

  INSERT INTO mmo_npc_ai_state_current(
    world_instance_id, npc_entity_key, ai_state, ai_intent, target_key,
    perception_state, last_server_tick, metadata, idempotency_key, row_version
  )
  VALUES (
    v_world_instance_id, p_npc_entity_key, p_ai_state, COALESCE(p_ai_intent, ''), COALESCE(p_target_key, ''),
    COALESCE(p_perception_state, ''), COALESCE(p_last_server_tick, 0), COALESCE(p_metadata, JSON_OBJECT()), p_idempotency_key, 1
  )
  ON DUPLICATE KEY UPDATE
    ai_state = VALUES(ai_state),
    ai_intent = VALUES(ai_intent),
    target_key = VALUES(target_key),
    perception_state = VALUES(perception_state),
    last_server_tick = VALUES(last_server_tick),
    metadata = VALUES(metadata),
    idempotency_key = VALUES(idempotency_key),
    row_version = row_version + 1;

  SELECT row_version INTO p_row_version_after
    FROM mmo_npc_ai_state_current
   WHERE world_instance_id = v_world_instance_id AND npc_entity_key = p_npc_entity_key;

  INSERT IGNORE INTO mmo_npc_ai_state_history(
    event_id, session_id, world_instance_id, npc_entity_key, ai_state, ai_intent,
    target_key, perception_state, last_server_tick, metadata, idempotency_key, row_version
  )
  SELECT p_event_id, p_session_id, world_instance_id, npc_entity_key, ai_state, ai_intent,
         target_key, perception_state, last_server_tick, metadata, idempotency_key, row_version
    FROM mmo_npc_ai_state_current
   WHERE world_instance_id = v_world_instance_id AND npc_entity_key = p_npc_entity_key;
END ;;
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_record_npc_fight_state;
DELIMITER ;;
CREATE PROCEDURE mmo_record_npc_fight_state(
  IN p_session_id BINARY(16),
  IN p_npc_entity_key VARCHAR(512),
  IN p_opponent_key VARCHAR(512),
  IN p_fight_state VARCHAR(128),
  IN p_attack_state VARCHAR(128),
  IN p_combo_index INT,
  IN p_last_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT p_event_id BINARY(16),
  OUT p_row_version_after BIGINT UNSIGNED
)
BEGIN
  DECLARE v_world_instance_id BINARY(16);
  SET p_event_id = UUID_TO_BIN(UUID(), 1);
  SELECT world_instance_id INTO v_world_instance_id FROM server_sessions WHERE session_id = p_session_id LIMIT 1;
  IF v_world_instance_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_npc_fight_state: invalid session'; END IF;

  INSERT INTO mmo_npc_fight_state_current(
    world_instance_id, npc_entity_key, opponent_key, fight_state, attack_state,
    combo_index, last_server_tick, metadata, idempotency_key, row_version
  )
  VALUES (
    v_world_instance_id, p_npc_entity_key, COALESCE(p_opponent_key, ''), p_fight_state, COALESCE(p_attack_state, ''),
    COALESCE(p_combo_index, 0), COALESCE(p_last_server_tick, 0), COALESCE(p_metadata, JSON_OBJECT()), p_idempotency_key, 1
  )
  ON DUPLICATE KEY UPDATE
    opponent_key = VALUES(opponent_key),
    fight_state = VALUES(fight_state),
    attack_state = VALUES(attack_state),
    combo_index = VALUES(combo_index),
    last_server_tick = VALUES(last_server_tick),
    metadata = VALUES(metadata),
    idempotency_key = VALUES(idempotency_key),
    row_version = row_version + 1;

  SELECT row_version INTO p_row_version_after
    FROM mmo_npc_fight_state_current
   WHERE world_instance_id = v_world_instance_id AND npc_entity_key = p_npc_entity_key;

  INSERT IGNORE INTO mmo_npc_fight_state_history(
    event_id, session_id, world_instance_id, npc_entity_key, opponent_key, fight_state,
    attack_state, combo_index, last_server_tick, metadata, idempotency_key, row_version
  )
  SELECT p_event_id, p_session_id, world_instance_id, npc_entity_key, opponent_key, fight_state,
         attack_state, combo_index, last_server_tick, metadata, idempotency_key, row_version
    FROM mmo_npc_fight_state_current
   WHERE world_instance_id = v_world_instance_id AND npc_entity_key = p_npc_entity_key;
END ;;
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_record_npc_path_state;
DELIMITER ;;
CREATE PROCEDURE mmo_record_npc_path_state(
  IN p_session_id BINARY(16),
  IN p_npc_entity_key VARCHAR(512),
  IN p_path_state VARCHAR(128),
  IN p_route_key VARCHAR(512),
  IN p_current_waypoint_key VARCHAR(512),
  IN p_next_waypoint_key VARCHAR(512),
  IN p_target_waypoint_key VARCHAR(512),
  IN p_pos_x DOUBLE,
  IN p_pos_y DOUBLE,
  IN p_pos_z DOUBLE,
  IN p_last_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT p_event_id BINARY(16),
  OUT p_row_version_after BIGINT UNSIGNED
)
BEGIN
  DECLARE v_world_instance_id BINARY(16);
  SET p_event_id = UUID_TO_BIN(UUID(), 1);
  SELECT world_instance_id INTO v_world_instance_id FROM server_sessions WHERE session_id = p_session_id LIMIT 1;
  IF v_world_instance_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_npc_path_state: invalid session'; END IF;

  INSERT INTO mmo_npc_path_state_current(
    world_instance_id, npc_entity_key, path_state, route_key, current_waypoint_key,
    next_waypoint_key, target_waypoint_key, pos_x, pos_y, pos_z,
    last_server_tick, metadata, idempotency_key, row_version
  )
  VALUES (
    v_world_instance_id, p_npc_entity_key, p_path_state, COALESCE(p_route_key, ''), COALESCE(p_current_waypoint_key, ''),
    COALESCE(p_next_waypoint_key, ''), COALESCE(p_target_waypoint_key, ''), p_pos_x, p_pos_y, p_pos_z,
    COALESCE(p_last_server_tick, 0), COALESCE(p_metadata, JSON_OBJECT()), p_idempotency_key, 1
  )
  ON DUPLICATE KEY UPDATE
    path_state = VALUES(path_state),
    route_key = VALUES(route_key),
    current_waypoint_key = VALUES(current_waypoint_key),
    next_waypoint_key = VALUES(next_waypoint_key),
    target_waypoint_key = VALUES(target_waypoint_key),
    pos_x = VALUES(pos_x),
    pos_y = VALUES(pos_y),
    pos_z = VALUES(pos_z),
    last_server_tick = VALUES(last_server_tick),
    metadata = VALUES(metadata),
    idempotency_key = VALUES(idempotency_key),
    row_version = row_version + 1;

  SELECT row_version INTO p_row_version_after
    FROM mmo_npc_path_state_current
   WHERE world_instance_id = v_world_instance_id AND npc_entity_key = p_npc_entity_key;

  INSERT IGNORE INTO mmo_npc_path_state_history(
    event_id, session_id, world_instance_id, npc_entity_key, path_state, route_key,
    current_waypoint_key, next_waypoint_key, target_waypoint_key, pos_x, pos_y, pos_z,
    last_server_tick, metadata, idempotency_key, row_version
  )
  SELECT p_event_id, p_session_id, world_instance_id, npc_entity_key, path_state, route_key,
         current_waypoint_key, next_waypoint_key, target_waypoint_key, pos_x, pos_y, pos_z,
         last_server_tick, metadata, idempotency_key, row_version
    FROM mmo_npc_path_state_current
   WHERE world_instance_id = v_world_instance_id AND npc_entity_key = p_npc_entity_key;
END ;;
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_record_npc_routine_state;
DELIMITER ;;
CREATE PROCEDURE mmo_record_npc_routine_state(
  IN p_session_id BINARY(16),
  IN p_npc_entity_key VARCHAR(512),
  IN p_routine_state VARCHAR(128),
  IN p_schedule_key VARCHAR(512),
  IN p_current_waypoint_key VARCHAR(512),
  IN p_target_waypoint_key VARCHAR(512),
  IN p_last_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT p_event_id BINARY(16),
  OUT p_row_version_after BIGINT UNSIGNED
)
BEGIN
  DECLARE v_world_instance_id BINARY(16);
  SET p_event_id = UUID_TO_BIN(UUID(), 1);
  SELECT world_instance_id INTO v_world_instance_id FROM server_sessions WHERE session_id = p_session_id LIMIT 1;
  IF v_world_instance_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_npc_routine_state: invalid session'; END IF;

  INSERT INTO mmo_npc_routine_state_current(
    world_instance_id, npc_entity_key, routine_state, schedule_key, current_waypoint_key,
    target_waypoint_key, last_server_tick, metadata, idempotency_key, row_version
  )
  VALUES (
    v_world_instance_id, p_npc_entity_key, p_routine_state, COALESCE(p_schedule_key, ''), COALESCE(p_current_waypoint_key, ''),
    COALESCE(p_target_waypoint_key, ''), COALESCE(p_last_server_tick, 0), COALESCE(p_metadata, JSON_OBJECT()), p_idempotency_key, 1
  )
  ON DUPLICATE KEY UPDATE
    routine_state = VALUES(routine_state),
    schedule_key = VALUES(schedule_key),
    current_waypoint_key = VALUES(current_waypoint_key),
    target_waypoint_key = VALUES(target_waypoint_key),
    last_server_tick = VALUES(last_server_tick),
    metadata = VALUES(metadata),
    idempotency_key = VALUES(idempotency_key),
    row_version = row_version + 1;

  SELECT row_version INTO p_row_version_after
    FROM mmo_npc_routine_state_current
   WHERE world_instance_id = v_world_instance_id AND npc_entity_key = p_npc_entity_key;

  INSERT IGNORE INTO mmo_npc_routine_state_history(
    event_id, session_id, world_instance_id, npc_entity_key, routine_state, schedule_key,
    current_waypoint_key, target_waypoint_key, last_server_tick, metadata, idempotency_key, row_version
  )
  SELECT p_event_id, p_session_id, world_instance_id, npc_entity_key, routine_state, schedule_key,
         current_waypoint_key, target_waypoint_key, last_server_tick, metadata, idempotency_key, row_version
    FROM mmo_npc_routine_state_current
   WHERE world_instance_id = v_world_instance_id AND npc_entity_key = p_npc_entity_key;
END ;;
DELIMITER ;

-- ============================================================================
-- END server/sql/step122_npc_observation_failopen_and_item_refresh_guard.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step188_server_content_pack_manifest.sql
-- ============================================================================

-- Step188: server-owned content pack manifest.
-- The server must use its own Gothic/mod content files as authority and only
-- accept clients that declare the same manifest hash.

CREATE TABLE IF NOT EXISTS mmo_server_content_pack_files (
  content_file_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_revision_id BINARY(16) NOT NULL,
  logical_path VARCHAR(512) NOT NULL,
  source_kind VARCHAR(32) NOT NULL DEFAULT 'other',
  byte_size BIGINT UNSIGNED NOT NULL DEFAULT 0,
  sha256 CHAR(64) NOT NULL,
  mtime_utc TIMESTAMP(6) NULL DEFAULT NULL,
  required TINYINT(1) NOT NULL DEFAULT 1,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (content_file_id),
  UNIQUE KEY mmo_server_content_pack_files_uk (content_revision_id, logical_path),
  KEY ix_mmo_server_content_pack_files_hash (content_revision_id, sha256),
  KEY ix_mmo_server_content_pack_files_kind (content_revision_id, source_kind),
  CONSTRAINT mmo_server_content_pack_files_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_server_content_pack_files_size_ck CHECK (byte_size >= 0),
  CONSTRAINT mmo_server_content_pack_files_kind_ck CHECK (source_kind IN (
    'zen', 'dat', 'ou', 'vdf', 'mod', 'script', 'ini', 'texture', 'mesh', 'sound', 'video', 'font', 'other'
  )),
  CONSTRAINT mmo_server_content_pack_files_sha_ck CHECK (sha256 REGEXP '^[0-9a-f]{64}$'),
  CONSTRAINT mmo_server_content_pack_files_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_server_content_pack_manifests (
  content_manifest_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_revision_id BINARY(16) NOT NULL,
  manifest_hash CHAR(64) NOT NULL,
  file_count INT NOT NULL DEFAULT 0,
  required_file_count INT NOT NULL DEFAULT 0,
  total_bytes BIGINT UNSIGNED NOT NULL DEFAULT 0,
  source_root_label VARCHAR(512) NOT NULL DEFAULT '',
  source_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (content_manifest_id),
  UNIQUE KEY mmo_server_content_pack_manifests_revision_uk (content_revision_id),
  KEY ix_mmo_server_content_pack_manifests_hash (manifest_hash),
  CONSTRAINT mmo_server_content_pack_manifests_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_server_content_pack_manifests_hash_ck CHECK (manifest_hash REGEXP '^[0-9a-f]{64}$'),
  CONSTRAINT mmo_server_content_pack_manifests_file_count_ck CHECK (file_count >= 0),
  CONSTRAINT mmo_server_content_pack_manifests_required_count_ck CHECK (required_file_count >= 0),
  CONSTRAINT mmo_server_content_pack_manifests_total_bytes_ck CHECK (total_bytes >= 0),
  CONSTRAINT mmo_server_content_pack_manifests_payload_json_ck CHECK (JSON_VALID(source_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP VIEW IF EXISTS v_mmo_server_content_pack_manifests;
CREATE VIEW v_mmo_server_content_pack_manifests AS
SELECT
  BIN_TO_UUID(m.content_manifest_id, 1) AS content_manifest_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  m.manifest_hash,
  m.file_count,
  m.required_file_count,
  m.total_bytes,
  m.source_root_label,
  m.source_payload,
  m.created_at,
  m.updated_at
FROM mmo_server_content_pack_manifests m
JOIN content_revisions cr ON cr.content_revision_id = m.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id;

DROP VIEW IF EXISTS v_mmo_server_content_pack_files;
CREATE VIEW v_mmo_server_content_pack_files AS
SELECT
  BIN_TO_UUID(f.content_file_id, 1) AS content_file_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  f.logical_path,
  f.source_kind,
  f.byte_size,
  f.sha256,
  f.mtime_utc,
  f.required,
  f.raw_payload,
  f.created_at,
  f.updated_at
FROM mmo_server_content_pack_files f
JOIN content_revisions cr ON cr.content_revision_id = f.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id;

DELIMITER ;;

DROP PROCEDURE IF EXISTS mmo_upsert_server_content_pack_file;;
CREATE PROCEDURE mmo_upsert_server_content_pack_file(
  IN p_content_revision_key VARCHAR(191),
  IN p_logical_path VARCHAR(512),
  IN p_source_kind VARCHAR(32),
  IN p_byte_size BIGINT UNSIGNED,
  IN p_sha256 CHAR(64),
  IN p_mtime_utc TIMESTAMP(6),
  IN p_required TINYINT(1),
  IN p_raw_payload JSON,
  OUT o_content_file_id BINARY(16)
)
proc: BEGIN
  DECLARE v_content_revision_id BINARY(16) DEFAULT NULL;
  DECLARE v_source_kind VARCHAR(32) DEFAULT 'other';
  DECLARE CONTINUE HANDLER FOR NOT FOUND BEGIN END;

  SET o_content_file_id = NULL;

  SELECT content_revision_id
    INTO v_content_revision_id
    FROM content_revisions
   WHERE content_revision_key = p_content_revision_key
   LIMIT 1;
  IF v_content_revision_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_pack_file: content revision not found';
  END IF;

  IF COALESCE(TRIM(p_logical_path), '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_pack_file: logical path is required';
  END IF;
  IF COALESCE(p_sha256, '') NOT REGEXP '^[0-9a-f]{64}$' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_pack_file: sha256 must be lowercase hex';
  END IF;

  SET v_source_kind = COALESCE(NULLIF(p_source_kind, ''), 'other');
  IF v_source_kind NOT IN ('zen', 'dat', 'ou', 'vdf', 'mod', 'script', 'ini', 'texture', 'mesh', 'sound', 'video', 'font', 'other') THEN
    SET v_source_kind = 'other';
  END IF;

  INSERT INTO mmo_server_content_pack_files(
    content_revision_id, logical_path, source_kind, byte_size, sha256, mtime_utc, required, raw_payload
  )
  VALUES (
    v_content_revision_id,
    LOWER(REPLACE(p_logical_path, '\\', '/')),
    v_source_kind,
    COALESCE(p_byte_size, 0),
    LOWER(p_sha256),
    p_mtime_utc,
    COALESCE(p_required, 1),
    COALESCE(p_raw_payload, JSON_OBJECT())
  )
  ON DUPLICATE KEY UPDATE
    source_kind = VALUES(source_kind),
    byte_size = VALUES(byte_size),
    sha256 = VALUES(sha256),
    mtime_utc = VALUES(mtime_utc),
    required = VALUES(required),
    raw_payload = VALUES(raw_payload),
    updated_at = CURRENT_TIMESTAMP(6);

  SELECT content_file_id
    INTO o_content_file_id
    FROM mmo_server_content_pack_files
   WHERE content_revision_id = v_content_revision_id
     AND logical_path = LOWER(REPLACE(p_logical_path, '\\', '/'))
   LIMIT 1;
END;;

DROP PROCEDURE IF EXISTS mmo_set_server_content_pack_manifest;;
CREATE PROCEDURE mmo_set_server_content_pack_manifest(
  IN p_content_revision_key VARCHAR(191),
  IN p_manifest_hash CHAR(64),
  IN p_file_count INT,
  IN p_required_file_count INT,
  IN p_total_bytes BIGINT UNSIGNED,
  IN p_source_root_label VARCHAR(512),
  IN p_source_payload JSON,
  OUT o_content_manifest_id BINARY(16)
)
proc: BEGIN
  DECLARE v_content_revision_id BINARY(16) DEFAULT NULL;
  DECLARE CONTINUE HANDLER FOR NOT FOUND BEGIN END;

  SET o_content_manifest_id = NULL;

  SELECT content_revision_id
    INTO v_content_revision_id
    FROM content_revisions
   WHERE content_revision_key = p_content_revision_key
   LIMIT 1;
  IF v_content_revision_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_set_server_content_pack_manifest: content revision not found';
  END IF;

  IF COALESCE(p_manifest_hash, '') NOT REGEXP '^[0-9a-f]{64}$' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_set_server_content_pack_manifest: manifest hash must be lowercase hex';
  END IF;

  INSERT INTO mmo_server_content_pack_manifests(
    content_revision_id, manifest_hash, file_count, required_file_count, total_bytes, source_root_label, source_payload
  )
  VALUES (
    v_content_revision_id,
    LOWER(p_manifest_hash),
    COALESCE(p_file_count, 0),
    COALESCE(p_required_file_count, 0),
    COALESCE(p_total_bytes, 0),
    COALESCE(p_source_root_label, ''),
    COALESCE(p_source_payload, JSON_OBJECT())
  )
  ON DUPLICATE KEY UPDATE
    manifest_hash = VALUES(manifest_hash),
    file_count = VALUES(file_count),
    required_file_count = VALUES(required_file_count),
    total_bytes = VALUES(total_bytes),
    source_root_label = VALUES(source_root_label),
    source_payload = VALUES(source_payload),
    updated_at = CURRENT_TIMESTAMP(6);

  SELECT content_manifest_id
    INTO o_content_manifest_id
    FROM mmo_server_content_pack_manifests
   WHERE content_revision_id = v_content_revision_id
   LIMIT 1;
END;;

DROP PROCEDURE IF EXISTS mmo_validate_client_content_pack;;
CREATE PROCEDURE mmo_validate_client_content_pack(
  IN p_realm_key VARCHAR(191),
  IN p_client_manifest_hash CHAR(64),
  OUT o_accepted TINYINT(1),
  OUT o_reason VARCHAR(191),
  OUT o_server_manifest_hash CHAR(64),
  OUT o_content_revision_key VARCHAR(191)
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_content_revision_id BINARY(16) DEFAULT NULL;
  DECLARE CONTINUE HANDLER FOR NOT FOUND BEGIN END;

  SET o_accepted = 0;
  SET o_reason = 'unknown';
  SET o_server_manifest_hash = NULL;
  SET o_content_revision_key = NULL;

  SELECT rr.realm_id, rr.active_content_revision_id, cr.content_revision_key
    INTO v_realm_id, v_content_revision_id, o_content_revision_key
    FROM realm_realms rr
    JOIN content_revisions cr ON cr.content_revision_id = rr.active_content_revision_id
   WHERE rr.realm_key = p_realm_key
   LIMIT 1;
  IF v_realm_id IS NULL THEN
    SET o_reason = 'realm_not_found';
    LEAVE proc;
  END IF;

  SELECT manifest_hash
    INTO o_server_manifest_hash
    FROM mmo_server_content_pack_manifests
   WHERE content_revision_id = v_content_revision_id
   LIMIT 1;
  IF o_server_manifest_hash IS NULL THEN
    SET o_reason = 'server_manifest_missing';
    LEAVE proc;
  END IF;

  IF COALESCE(p_client_manifest_hash, '') = '' THEN
    SET o_reason = 'client_manifest_missing';
    LEAVE proc;
  END IF;

  IF LOWER(p_client_manifest_hash) <> LOWER(o_server_manifest_hash) THEN
    SET o_reason = 'content_hash_mismatch';
    LEAVE proc;
  END IF;

  SET o_accepted = 1;
  SET o_reason = 'ok';
END;;

DELIMITER ;

-- ============================================================================
-- END server/sql/step188_server_content_pack_manifest.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step201_server_content_pack_inventory.sql
-- ============================================================================

-- Step201: classify server-owned content pack files into future importer roles.
-- Step188 stores the authoritative server manifest and per-file checksums.
-- This step adds a durable inventory layer that tells the future server loader
-- which files are world ZEN, compiled Daedalus DAT, OU/dialog output, archives,
-- source/reference files, or assets.

CREATE TABLE IF NOT EXISTS mmo_server_content_pack_inventory (
  content_inventory_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_file_id BINARY(16) NOT NULL,
  content_revision_id BINARY(16) NOT NULL,
  logical_path VARCHAR(512) NOT NULL,
  file_role VARCHAR(48) NOT NULL DEFAULT 'other',
  loader_stage VARCHAR(48) NOT NULL DEFAULT 'unknown',
  import_priority INT NOT NULL DEFAULT 1000,
  required_for_server_authority TINYINT(1) NOT NULL DEFAULT 0,
  import_status VARCHAR(32) NOT NULL DEFAULT 'not_started',
  import_notes JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (content_inventory_id),
  UNIQUE KEY mmo_server_content_pack_inventory_file_uk (content_file_id),
  KEY ix_mmo_content_pack_inventory_revision_role (content_revision_id, file_role),
  KEY ix_mmo_content_pack_inventory_revision_stage (content_revision_id, loader_stage),
  KEY ix_mmo_content_pack_inventory_revision_status (content_revision_id, import_status),
  KEY ix_mmo_content_pack_inventory_path (content_revision_id, logical_path),
  CONSTRAINT mmo_content_pack_inventory_file_fk
    FOREIGN KEY (content_file_id) REFERENCES mmo_server_content_pack_files(content_file_id) ON DELETE CASCADE,
  CONSTRAINT mmo_content_pack_inventory_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_content_pack_inventory_role_ck CHECK (file_role IN (
    'world_zen',
    'scripts_dat',
    'dialog_ou',
    'archive_vdf',
    'archive_mod',
    'script_source',
    'config_ini',
    'asset_texture',
    'asset_mesh',
    'asset_sound',
    'asset_video',
    'font',
    'other'
  )),
  CONSTRAINT mmo_content_pack_inventory_stage_ck CHECK (loader_stage IN (
    'archive_mount',
    'world_loader',
    'script_vm',
    'dialog_output',
    'source_reference',
    'server_config',
    'asset_lookup',
    'ignore',
    'unknown'
  )),
  CONSTRAINT mmo_content_pack_inventory_status_ck CHECK (import_status IN (
    'not_started',
    'planned',
    'ready_for_parser',
    'unsupported',
    'imported',
    'failed',
    'ignored'
  )),
  CONSTRAINT mmo_content_pack_inventory_priority_ck CHECK (import_priority >= 0),
  CONSTRAINT mmo_content_pack_inventory_notes_json_ck CHECK (JSON_VALID(import_notes))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP VIEW IF EXISTS v_mmo_server_content_pack_inventory_health;
DROP VIEW IF EXISTS v_mmo_server_content_pack_inventory;

CREATE VIEW v_mmo_server_content_pack_inventory AS
SELECT
  BIN_TO_UUID(i.content_inventory_id, 1) AS content_inventory_uuid,
  BIN_TO_UUID(i.content_file_id, 1) AS content_file_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  i.logical_path,
  f.source_kind,
  f.byte_size,
  f.sha256,
  i.file_role,
  i.loader_stage,
  i.import_priority,
  i.required_for_server_authority,
  i.import_status,
  i.import_notes,
  i.created_at,
  i.updated_at
FROM mmo_server_content_pack_inventory i
JOIN mmo_server_content_pack_files f ON f.content_file_id = i.content_file_id
JOIN content_revisions cr ON cr.content_revision_id = i.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id;

CREATE VIEW v_mmo_server_content_pack_inventory_health AS
SELECT
  cr.content_revision_key,
  cr.is_active,
  COUNT(f.content_file_id) AS manifest_file_count,
  COUNT(i.content_inventory_id) AS inventoried_file_count,
  SUM(CASE WHEN i.content_inventory_id IS NULL THEN 1 ELSE 0 END) AS missing_inventory_count,
  SUM(CASE WHEN i.file_role = 'world_zen' THEN 1 ELSE 0 END) AS world_zen_count,
  SUM(CASE WHEN i.file_role = 'scripts_dat' THEN 1 ELSE 0 END) AS scripts_dat_count,
  SUM(CASE WHEN i.file_role = 'dialog_ou' THEN 1 ELSE 0 END) AS dialog_ou_count,
  SUM(CASE WHEN i.file_role IN ('archive_vdf', 'archive_mod') THEN 1 ELSE 0 END) AS archive_count,
  SUM(CASE WHEN i.required_for_server_authority = 1 THEN 1 ELSE 0 END) AS required_for_authority_count,
  SUM(CASE WHEN i.import_status = 'ready_for_parser' THEN 1 ELSE 0 END) AS ready_for_parser_count,
  SUM(CASE WHEN i.import_status = 'unsupported' THEN 1 ELSE 0 END) AS unsupported_count,
  SUM(CASE WHEN i.import_status = 'failed' THEN 1 ELSE 0 END) AS failed_count,
  SUM(CASE WHEN i.import_status = 'imported' THEN 1 ELSE 0 END) AS imported_count
FROM content_revisions cr
JOIN mmo_server_content_pack_files f ON f.content_revision_id = cr.content_revision_id
LEFT JOIN mmo_server_content_pack_inventory i ON i.content_file_id = f.content_file_id
GROUP BY cr.content_revision_key, cr.is_active;

DELIMITER ;;

DROP PROCEDURE IF EXISTS mmo_upsert_server_content_pack_inventory;;
CREATE PROCEDURE mmo_upsert_server_content_pack_inventory(
  IN p_content_revision_key VARCHAR(191),
  IN p_logical_path VARCHAR(512),
  IN p_file_role VARCHAR(48),
  IN p_loader_stage VARCHAR(48),
  IN p_import_priority INT,
  IN p_required_for_server_authority TINYINT(1),
  IN p_import_status VARCHAR(32),
  IN p_import_notes JSON,
  OUT o_content_inventory_id BINARY(16)
)
proc: BEGIN
  DECLARE v_content_revision_id BINARY(16) DEFAULT NULL;
  DECLARE v_content_file_id BINARY(16) DEFAULT NULL;
  DECLARE v_logical_path VARCHAR(512) DEFAULT '';
  DECLARE v_file_role VARCHAR(48) DEFAULT 'other';
  DECLARE v_loader_stage VARCHAR(48) DEFAULT 'unknown';
  DECLARE v_import_status VARCHAR(32) DEFAULT 'not_started';
  DECLARE CONTINUE HANDLER FOR NOT FOUND BEGIN END;

  SET o_content_inventory_id = NULL;
  SET v_logical_path = LOWER(REPLACE(COALESCE(p_logical_path, ''), '\\', '/'));

  SELECT content_revision_id
    INTO v_content_revision_id
    FROM content_revisions
   WHERE content_revision_key = p_content_revision_key
   LIMIT 1;
  IF v_content_revision_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_pack_inventory: content revision not found';
  END IF;

  IF COALESCE(TRIM(v_logical_path), '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_pack_inventory: logical path is required';
  END IF;

  SELECT content_file_id
    INTO v_content_file_id
    FROM mmo_server_content_pack_files
   WHERE content_revision_id = v_content_revision_id
     AND logical_path = v_logical_path
   LIMIT 1;
  IF v_content_file_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_pack_inventory: content pack file not found';
  END IF;

  SET v_file_role = COALESCE(NULLIF(p_file_role, ''), 'other');
  IF v_file_role NOT IN ('world_zen','scripts_dat','dialog_ou','archive_vdf','archive_mod','script_source','config_ini','asset_texture','asset_mesh','asset_sound','asset_video','font','other') THEN
    SET v_file_role = 'other';
  END IF;

  SET v_loader_stage = COALESCE(NULLIF(p_loader_stage, ''), 'unknown');
  IF v_loader_stage NOT IN ('archive_mount','world_loader','script_vm','dialog_output','source_reference','server_config','asset_lookup','ignore','unknown') THEN
    SET v_loader_stage = 'unknown';
  END IF;

  SET v_import_status = COALESCE(NULLIF(p_import_status, ''), 'not_started');
  IF v_import_status NOT IN ('not_started','planned','ready_for_parser','unsupported','imported','failed','ignored') THEN
    SET v_import_status = 'not_started';
  END IF;

  INSERT INTO mmo_server_content_pack_inventory(
    content_file_id,
    content_revision_id,
    logical_path,
    file_role,
    loader_stage,
    import_priority,
    required_for_server_authority,
    import_status,
    import_notes
  )
  VALUES (
    v_content_file_id,
    v_content_revision_id,
    v_logical_path,
    v_file_role,
    v_loader_stage,
    COALESCE(p_import_priority, 1000),
    COALESCE(p_required_for_server_authority, 0),
    v_import_status,
    COALESCE(p_import_notes, JSON_OBJECT())
  )
  ON DUPLICATE KEY UPDATE
    content_revision_id = VALUES(content_revision_id),
    logical_path = VALUES(logical_path),
    file_role = VALUES(file_role),
    loader_stage = VALUES(loader_stage),
    import_priority = VALUES(import_priority),
    required_for_server_authority = VALUES(required_for_server_authority),
    import_status = VALUES(import_status),
    import_notes = VALUES(import_notes),
    updated_at = CURRENT_TIMESTAMP(6);

  SELECT content_inventory_id
    INTO o_content_inventory_id
    FROM mmo_server_content_pack_inventory
   WHERE content_file_id = v_content_file_id
   LIMIT 1;
END;;

DELIMITER ;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES (
  'server/sql/step201_server_content_pack_inventory.sql',
  'step201_server_content_pack_inventory_v1',
  'Step201: server content pack inventory roles for future ZEN/DAT/OU importers'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);

-- ============================================================================
-- END server/sql/step201_server_content_pack_inventory.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step203_server_content_archive_mounts.sql
-- ============================================================================

-- Step203: server-side archive mount/pre-extract registry.
-- Step201 identifies VDF/MOD files as archive inputs. This step records the
-- server-side mount/extract decision and, optionally, files discovered after
-- extraction. Future ZEN/DAT/OU importers should read from this registry rather
-- than guessing paths ad hoc.

CREATE TABLE IF NOT EXISTS mmo_server_content_archive_mounts (
  content_archive_mount_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_file_id BINARY(16) NOT NULL,
  content_revision_id BINARY(16) NOT NULL,
  archive_logical_path VARCHAR(512) NOT NULL,
  archive_role VARCHAR(32) NOT NULL,
  mount_strategy VARCHAR(32) NOT NULL DEFAULT 'pre_extracted',
  mount_status VARCHAR(32) NOT NULL DEFAULT 'planned',
  extracted_root_label VARCHAR(512) NOT NULL DEFAULT '',
  extracted_file_count INT NOT NULL DEFAULT 0,
  extracted_total_bytes BIGINT UNSIGNED NOT NULL DEFAULT 0,
  extracted_manifest_hash CHAR(64) NULL,
  last_verified_at TIMESTAMP(6) NULL DEFAULT NULL,
  mount_notes JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (content_archive_mount_id),
  UNIQUE KEY mmo_content_archive_mount_file_uk (content_file_id),
  KEY ix_mmo_content_archive_mount_revision (content_revision_id, archive_role, mount_status),
  KEY ix_mmo_content_archive_mount_path (content_revision_id, archive_logical_path),
  CONSTRAINT mmo_content_archive_mount_file_fk
    FOREIGN KEY (content_file_id) REFERENCES mmo_server_content_pack_files(content_file_id) ON DELETE CASCADE,
  CONSTRAINT mmo_content_archive_mount_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_content_archive_mount_role_ck CHECK (archive_role IN ('archive_vdf', 'archive_mod')),
  CONSTRAINT mmo_content_archive_mount_strategy_ck CHECK (mount_strategy IN (
    'pre_extracted', 'read_direct', 'extract_on_boot', 'external_mount', 'manual'
  )),
  CONSTRAINT mmo_content_archive_mount_status_ck CHECK (mount_status IN (
    'planned', 'mounted', 'extracted', 'verified', 'failed', 'ignored'
  )),
  CONSTRAINT mmo_content_archive_mount_file_count_ck CHECK (extracted_file_count >= 0),
  CONSTRAINT mmo_content_archive_mount_total_bytes_ck CHECK (extracted_total_bytes >= 0),
  CONSTRAINT mmo_content_archive_mount_hash_ck CHECK (extracted_manifest_hash IS NULL OR REGEXP_LIKE(extracted_manifest_hash, '^[0-9a-f]{64}$')),
  CONSTRAINT mmo_content_archive_mount_notes_json_ck CHECK (JSON_VALID(mount_notes))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_server_content_extracted_files (
  content_extracted_file_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_archive_mount_id BINARY(16) NOT NULL,
  content_revision_id BINARY(16) NOT NULL,
  archive_logical_path VARCHAR(512) NOT NULL,
  extracted_logical_path VARCHAR(512) NOT NULL,
  mapped_content_file_id BINARY(16) NULL,
  byte_size BIGINT UNSIGNED NOT NULL DEFAULT 0,
  sha256 CHAR(64) NULL,
  extracted_status VARCHAR(32) NOT NULL DEFAULT 'discovered',
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (content_extracted_file_id),
  UNIQUE KEY mmo_content_extracted_file_uk (content_archive_mount_id, extracted_logical_path),
  KEY ix_mmo_content_extracted_revision (content_revision_id, extracted_status),
  KEY ix_mmo_content_extracted_path (content_revision_id, extracted_logical_path),
  KEY ix_mmo_content_extracted_hash (content_revision_id, sha256),
  CONSTRAINT mmo_content_extracted_archive_fk
    FOREIGN KEY (content_archive_mount_id) REFERENCES mmo_server_content_archive_mounts(content_archive_mount_id) ON DELETE CASCADE,
  CONSTRAINT mmo_content_extracted_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_content_extracted_mapped_file_fk
    FOREIGN KEY (mapped_content_file_id) REFERENCES mmo_server_content_pack_files(content_file_id) ON DELETE SET NULL,
  CONSTRAINT mmo_content_extracted_size_ck CHECK (byte_size >= 0),
  CONSTRAINT mmo_content_extracted_hash_ck CHECK (sha256 IS NULL OR REGEXP_LIKE(sha256, '^[0-9a-f]{64}$')),
  CONSTRAINT mmo_content_extracted_status_ck CHECK (extracted_status IN (
    'discovered', 'hashed', 'linked', 'missing', 'hash_mismatch', 'ignored'
  )),
  CONSTRAINT mmo_content_extracted_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP VIEW IF EXISTS v_mmo_server_content_archive_health;
DROP VIEW IF EXISTS v_mmo_server_content_extracted_files;
DROP VIEW IF EXISTS v_mmo_server_content_archive_mounts;

CREATE VIEW v_mmo_server_content_archive_mounts AS
SELECT
  BIN_TO_UUID(am.content_archive_mount_id, 1) AS content_archive_mount_uuid,
  BIN_TO_UUID(am.content_file_id, 1) AS content_file_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  am.archive_logical_path,
  cpf.source_kind,
  cpf.byte_size AS archive_byte_size,
  cpf.sha256 AS archive_sha256,
  am.archive_role,
  am.mount_strategy,
  am.mount_status,
  am.extracted_root_label,
  am.extracted_file_count,
  am.extracted_total_bytes,
  am.extracted_manifest_hash,
  am.last_verified_at,
  am.mount_notes,
  am.created_at,
  am.updated_at
FROM mmo_server_content_archive_mounts am
JOIN mmo_server_content_pack_files cpf ON cpf.content_file_id = am.content_file_id
JOIN content_revisions cr ON cr.content_revision_id = am.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id;

CREATE VIEW v_mmo_server_content_extracted_files AS
SELECT
  BIN_TO_UUID(ef.content_extracted_file_id, 1) AS content_extracted_file_uuid,
  BIN_TO_UUID(ef.content_archive_mount_id, 1) AS content_archive_mount_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  ef.archive_logical_path,
  ef.extracted_logical_path,
  BIN_TO_UUID(ef.mapped_content_file_id, 1) AS mapped_content_file_uuid,
  mapped.logical_path AS mapped_manifest_logical_path,
  ef.byte_size,
  ef.sha256,
  ef.extracted_status,
  ef.raw_payload,
  ef.created_at,
  ef.updated_at
FROM mmo_server_content_extracted_files ef
JOIN content_revisions cr ON cr.content_revision_id = ef.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id
LEFT JOIN mmo_server_content_pack_files mapped ON mapped.content_file_id = ef.mapped_content_file_id;

CREATE VIEW v_mmo_server_content_archive_health AS
SELECT
  cr.content_revision_key,
  cr.is_active,
  SUM(CASE WHEN i.file_role IN ('archive_vdf', 'archive_mod') THEN 1 ELSE 0 END) AS inventory_archive_count,
  COUNT(am.content_archive_mount_id) AS archive_mount_count,
  SUM(CASE WHEN am.content_archive_mount_id IS NULL AND i.file_role IN ('archive_vdf', 'archive_mod') THEN 1 ELSE 0 END) AS missing_mount_count,
  SUM(CASE WHEN am.mount_status = 'planned' THEN 1 ELSE 0 END) AS planned_count,
  SUM(CASE WHEN am.mount_status = 'mounted' THEN 1 ELSE 0 END) AS mounted_count,
  SUM(CASE WHEN am.mount_status = 'extracted' THEN 1 ELSE 0 END) AS extracted_count,
  SUM(CASE WHEN am.mount_status = 'verified' THEN 1 ELSE 0 END) AS verified_count,
  SUM(CASE WHEN am.mount_status = 'failed' THEN 1 ELSE 0 END) AS failed_count,
  COALESCE(SUM(am.extracted_file_count), 0) AS extracted_file_count,
  COALESCE(SUM(am.extracted_total_bytes), 0) AS extracted_total_bytes
FROM content_revisions cr
JOIN mmo_server_content_pack_inventory i ON i.content_revision_id = cr.content_revision_id
LEFT JOIN mmo_server_content_archive_mounts am ON am.content_file_id = i.content_file_id
WHERE i.file_role IN ('archive_vdf', 'archive_mod')
GROUP BY cr.content_revision_key, cr.is_active;

DELIMITER ;;

DROP PROCEDURE IF EXISTS mmo_upsert_server_content_archive_mount;;
CREATE PROCEDURE mmo_upsert_server_content_archive_mount(
  IN p_content_revision_key VARCHAR(191),
  IN p_archive_logical_path VARCHAR(512),
  IN p_archive_role VARCHAR(32),
  IN p_mount_strategy VARCHAR(32),
  IN p_mount_status VARCHAR(32),
  IN p_extracted_root_label VARCHAR(512),
  IN p_extracted_file_count INT,
  IN p_extracted_total_bytes BIGINT UNSIGNED,
  IN p_extracted_manifest_hash CHAR(64),
  IN p_last_verified_at TIMESTAMP(6),
  IN p_mount_notes JSON,
  OUT o_content_archive_mount_id BINARY(16)
)
proc: BEGIN
  DECLARE v_content_revision_id BINARY(16) DEFAULT NULL;
  DECLARE v_content_file_id BINARY(16) DEFAULT NULL;
  DECLARE v_archive_logical_path VARCHAR(512) DEFAULT '';
  DECLARE v_archive_role VARCHAR(32) DEFAULT 'archive_vdf';
  DECLARE v_mount_strategy VARCHAR(32) DEFAULT 'pre_extracted';
  DECLARE v_mount_status VARCHAR(32) DEFAULT 'planned';
  DECLARE CONTINUE HANDLER FOR NOT FOUND BEGIN END;

  SET o_content_archive_mount_id = NULL;
  SET v_archive_logical_path = LOWER(REPLACE(COALESCE(p_archive_logical_path, ''), '\\', '/'));

  SELECT content_revision_id
    INTO v_content_revision_id
    FROM content_revisions
   WHERE content_revision_key = p_content_revision_key
   LIMIT 1;
  IF v_content_revision_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_archive_mount: content revision not found';
  END IF;

  SELECT content_file_id
    INTO v_content_file_id
    FROM mmo_server_content_pack_files
   WHERE content_revision_id = v_content_revision_id
     AND logical_path = v_archive_logical_path
   LIMIT 1;
  IF v_content_file_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_archive_mount: archive file not found in content manifest';
  END IF;

  SET v_archive_role = COALESCE(NULLIF(p_archive_role, ''), 'archive_vdf');
  IF v_archive_role NOT IN ('archive_vdf', 'archive_mod') THEN
    SET v_archive_role = 'archive_vdf';
  END IF;

  SET v_mount_strategy = COALESCE(NULLIF(p_mount_strategy, ''), 'pre_extracted');
  IF v_mount_strategy NOT IN ('pre_extracted', 'read_direct', 'extract_on_boot', 'external_mount', 'manual') THEN
    SET v_mount_strategy = 'pre_extracted';
  END IF;

  SET v_mount_status = COALESCE(NULLIF(p_mount_status, ''), 'planned');
  IF v_mount_status NOT IN ('planned', 'mounted', 'extracted', 'verified', 'failed', 'ignored') THEN
    SET v_mount_status = 'planned';
  END IF;

  IF p_extracted_manifest_hash IS NOT NULL AND p_extracted_manifest_hash NOT REGEXP '^[0-9a-f]{64}$' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_archive_mount: extracted manifest hash must be lowercase hex';
  END IF;

  INSERT INTO mmo_server_content_archive_mounts(
    content_file_id,
    content_revision_id,
    archive_logical_path,
    archive_role,
    mount_strategy,
    mount_status,
    extracted_root_label,
    extracted_file_count,
    extracted_total_bytes,
    extracted_manifest_hash,
    last_verified_at,
    mount_notes
  )
  VALUES (
    v_content_file_id,
    v_content_revision_id,
    v_archive_logical_path,
    v_archive_role,
    v_mount_strategy,
    v_mount_status,
    COALESCE(p_extracted_root_label, ''),
    COALESCE(p_extracted_file_count, 0),
    COALESCE(p_extracted_total_bytes, 0),
    LOWER(NULLIF(p_extracted_manifest_hash, '')),
    p_last_verified_at,
    COALESCE(p_mount_notes, JSON_OBJECT())
  )
  ON DUPLICATE KEY UPDATE
    content_revision_id = VALUES(content_revision_id),
    archive_logical_path = VALUES(archive_logical_path),
    archive_role = VALUES(archive_role),
    mount_strategy = VALUES(mount_strategy),
    mount_status = VALUES(mount_status),
    extracted_root_label = VALUES(extracted_root_label),
    extracted_file_count = VALUES(extracted_file_count),
    extracted_total_bytes = VALUES(extracted_total_bytes),
    extracted_manifest_hash = VALUES(extracted_manifest_hash),
    last_verified_at = VALUES(last_verified_at),
    mount_notes = VALUES(mount_notes),
    updated_at = CURRENT_TIMESTAMP(6);

  SELECT content_archive_mount_id
    INTO o_content_archive_mount_id
    FROM mmo_server_content_archive_mounts
   WHERE content_file_id = v_content_file_id
   LIMIT 1;
END;;

DROP PROCEDURE IF EXISTS mmo_upsert_server_content_extracted_file;;
CREATE PROCEDURE mmo_upsert_server_content_extracted_file(
  IN p_content_revision_key VARCHAR(191),
  IN p_archive_logical_path VARCHAR(512),
  IN p_extracted_logical_path VARCHAR(512),
  IN p_byte_size BIGINT UNSIGNED,
  IN p_sha256 CHAR(64),
  IN p_extracted_status VARCHAR(32),
  IN p_mapped_content_logical_path VARCHAR(512),
  IN p_raw_payload JSON,
  OUT o_content_extracted_file_id BINARY(16)
)
proc: BEGIN
  DECLARE v_content_revision_id BINARY(16) DEFAULT NULL;
  DECLARE v_content_archive_mount_id BINARY(16) DEFAULT NULL;
  DECLARE v_mapped_content_file_id BINARY(16) DEFAULT NULL;
  DECLARE v_archive_logical_path VARCHAR(512) DEFAULT '';
  DECLARE v_extracted_logical_path VARCHAR(512) DEFAULT '';
  DECLARE v_mapped_content_logical_path VARCHAR(512) DEFAULT '';
  DECLARE v_extracted_status VARCHAR(32) DEFAULT 'discovered';
  DECLARE CONTINUE HANDLER FOR NOT FOUND BEGIN END;

  SET o_content_extracted_file_id = NULL;
  SET v_archive_logical_path = LOWER(REPLACE(COALESCE(p_archive_logical_path, ''), '\\', '/'));
  SET v_extracted_logical_path = LOWER(REPLACE(COALESCE(p_extracted_logical_path, ''), '\\', '/'));
  SET v_mapped_content_logical_path = LOWER(REPLACE(COALESCE(p_mapped_content_logical_path, ''), '\\', '/'));

  SELECT content_revision_id
    INTO v_content_revision_id
    FROM content_revisions
   WHERE content_revision_key = p_content_revision_key
   LIMIT 1;
  IF v_content_revision_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_extracted_file: content revision not found';
  END IF;

  SELECT content_archive_mount_id
    INTO v_content_archive_mount_id
    FROM mmo_server_content_archive_mounts
   WHERE content_revision_id = v_content_revision_id
     AND archive_logical_path = v_archive_logical_path
   LIMIT 1;
  IF v_content_archive_mount_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_extracted_file: archive mount not found';
  END IF;

  IF COALESCE(TRIM(v_extracted_logical_path), '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_extracted_file: extracted logical path is required';
  END IF;

  IF p_sha256 IS NOT NULL AND p_sha256 NOT REGEXP '^[0-9a-f]{64}$' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_upsert_server_content_extracted_file: sha256 must be lowercase hex';
  END IF;

  IF v_mapped_content_logical_path <> '' THEN
    SELECT content_file_id
      INTO v_mapped_content_file_id
      FROM mmo_server_content_pack_files
     WHERE content_revision_id = v_content_revision_id
       AND logical_path = v_mapped_content_logical_path
     LIMIT 1;
  END IF;

  SET v_extracted_status = COALESCE(NULLIF(p_extracted_status, ''), 'discovered');
  IF v_extracted_status NOT IN ('discovered', 'hashed', 'linked', 'missing', 'hash_mismatch', 'ignored') THEN
    SET v_extracted_status = 'discovered';
  END IF;

  INSERT INTO mmo_server_content_extracted_files(
    content_archive_mount_id,
    content_revision_id,
    archive_logical_path,
    extracted_logical_path,
    mapped_content_file_id,
    byte_size,
    sha256,
    extracted_status,
    raw_payload
  )
  VALUES (
    v_content_archive_mount_id,
    v_content_revision_id,
    v_archive_logical_path,
    v_extracted_logical_path,
    v_mapped_content_file_id,
    COALESCE(p_byte_size, 0),
    LOWER(NULLIF(p_sha256, '')),
    v_extracted_status,
    COALESCE(p_raw_payload, JSON_OBJECT())
  )
  ON DUPLICATE KEY UPDATE
    mapped_content_file_id = VALUES(mapped_content_file_id),
    byte_size = VALUES(byte_size),
    sha256 = VALUES(sha256),
    extracted_status = VALUES(extracted_status),
    raw_payload = VALUES(raw_payload),
    updated_at = CURRENT_TIMESTAMP(6);

  SELECT content_extracted_file_id
    INTO o_content_extracted_file_id
    FROM mmo_server_content_extracted_files
   WHERE content_archive_mount_id = v_content_archive_mount_id
     AND extracted_logical_path = v_extracted_logical_path
   LIMIT 1;
END;;

DELIMITER ;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES (
  'server/sql/step203_server_content_archive_mounts.sql',
  'step203_server_content_archive_mounts_v1',
  'Step203: server content archive mount and pre-extract registry'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);

-- ============================================================================
-- END server/sql/step203_server_content_archive_mounts.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step206_server_content_import_jobs.sql
-- ============================================================================

-- Step206: content import job queue for future ZEN/DAT/OU importers.
-- Earlier steps identify content files and archive mount state. This step
-- creates durable import jobs so the build pipeline/server can process content
-- deterministically and expose progress through DB health views.

CREATE TABLE IF NOT EXISTS mmo_server_content_import_jobs (
  content_import_job_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_revision_id BINARY(16) NOT NULL,
  importer_key VARCHAR(96) NOT NULL,
  source_kind VARCHAR(48) NOT NULL,
  source_logical_path VARCHAR(512) NOT NULL,
  source_sha256 CHAR(64) NULL,
  job_priority INT NOT NULL DEFAULT 1000,
  job_status VARCHAR(32) NOT NULL DEFAULT 'queued',
  attempt_count INT NOT NULL DEFAULT 0,
  input_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  output_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  error_text TEXT NULL,
  lease_owner VARCHAR(191) NULL,
  lease_until TIMESTAMP(6) NULL DEFAULT NULL,
  started_at TIMESTAMP(6) NULL DEFAULT NULL,
  finished_at TIMESTAMP(6) NULL DEFAULT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (content_import_job_id),
  UNIQUE KEY mmo_content_import_job_source_uk (content_revision_id, importer_key, source_logical_path, source_sha256),
  KEY ix_mmo_content_import_job_status (content_revision_id, job_status, job_priority),
  KEY ix_mmo_content_import_job_importer (content_revision_id, importer_key),
  KEY ix_mmo_content_import_job_source (content_revision_id, source_kind, source_logical_path),
  CONSTRAINT mmo_content_import_job_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_content_import_job_priority_ck CHECK (job_priority >= 0),
  CONSTRAINT mmo_content_import_job_attempt_ck CHECK (attempt_count >= 0),
  CONSTRAINT mmo_content_import_job_sha_ck CHECK (source_sha256 IS NULL OR REGEXP_LIKE(source_sha256, '^[0-9a-f]{64}$')),
  CONSTRAINT mmo_content_import_job_status_ck CHECK (job_status IN (
    'queued', 'running', 'succeeded', 'failed', 'skipped', 'blocked'
  )),
  CONSTRAINT mmo_content_import_job_source_kind_ck CHECK (source_kind IN (
    'archive', 'world_zen', 'scripts_dat', 'dialog_ou', 'script_source', 'config_ini', 'asset', 'other'
  )),
  CONSTRAINT mmo_content_import_job_input_json_ck CHECK (JSON_VALID(input_payload)),
  CONSTRAINT mmo_content_import_job_output_json_ck CHECK (JSON_VALID(output_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP VIEW IF EXISTS v_mmo_server_content_import_job_health;
DROP VIEW IF EXISTS v_mmo_server_content_import_jobs;

CREATE VIEW v_mmo_server_content_import_jobs AS
SELECT
  BIN_TO_UUID(j.content_import_job_id, 1) AS content_import_job_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  j.importer_key,
  j.source_kind,
  j.source_logical_path,
  j.source_sha256,
  j.job_priority,
  j.job_status,
  j.attempt_count,
  j.input_payload,
  j.output_payload,
  j.error_text,
  j.lease_owner,
  j.lease_until,
  j.started_at,
  j.finished_at,
  j.created_at,
  j.updated_at
FROM mmo_server_content_import_jobs j
JOIN content_revisions cr ON cr.content_revision_id = j.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id;

CREATE VIEW v_mmo_server_content_import_job_health AS
SELECT
  cr.content_revision_key,
  cr.is_active,
  COUNT(j.content_import_job_id) AS total_jobs,
  SUM(CASE WHEN j.job_status = 'queued' THEN 1 ELSE 0 END) AS queued_count,
  SUM(CASE WHEN j.job_status = 'running' THEN 1 ELSE 0 END) AS running_count,
  SUM(CASE WHEN j.job_status = 'succeeded' THEN 1 ELSE 0 END) AS succeeded_count,
  SUM(CASE WHEN j.job_status = 'failed' THEN 1 ELSE 0 END) AS failed_count,
  SUM(CASE WHEN j.job_status = 'blocked' THEN 1 ELSE 0 END) AS blocked_count,
  SUM(CASE WHEN j.source_kind = 'archive' THEN 1 ELSE 0 END) AS archive_jobs,
  SUM(CASE WHEN j.source_kind = 'world_zen' THEN 1 ELSE 0 END) AS world_zen_jobs,
  SUM(CASE WHEN j.source_kind = 'scripts_dat' THEN 1 ELSE 0 END) AS scripts_dat_jobs,
  SUM(CASE WHEN j.source_kind = 'dialog_ou' THEN 1 ELSE 0 END) AS dialog_ou_jobs,
  MAX(j.updated_at) AS last_job_update_at
FROM content_revisions cr
LEFT JOIN mmo_server_content_import_jobs j ON j.content_revision_id = cr.content_revision_id
GROUP BY cr.content_revision_key, cr.is_active;

DELIMITER ;;

DROP PROCEDURE IF EXISTS mmo_enqueue_server_content_import_job;;
CREATE PROCEDURE mmo_enqueue_server_content_import_job(
  IN p_content_revision_key VARCHAR(191),
  IN p_importer_key VARCHAR(96),
  IN p_source_kind VARCHAR(48),
  IN p_source_logical_path VARCHAR(512),
  IN p_source_sha256 CHAR(64),
  IN p_job_priority INT,
  IN p_job_status VARCHAR(32),
  IN p_input_payload JSON,
  OUT o_content_import_job_id BINARY(16)
)
proc: BEGIN
  DECLARE v_content_revision_id BINARY(16) DEFAULT NULL;
  DECLARE v_source_kind VARCHAR(48) DEFAULT 'other';
  DECLARE v_job_status VARCHAR(32) DEFAULT 'queued';
  DECLARE v_source_logical_path VARCHAR(512) DEFAULT '';
  DECLARE CONTINUE HANDLER FOR NOT FOUND BEGIN END;

  SET o_content_import_job_id = NULL;
  SET v_source_logical_path = LOWER(REPLACE(COALESCE(p_source_logical_path, ''), '\\', '/'));

  SELECT content_revision_id
    INTO v_content_revision_id
    FROM content_revisions
   WHERE content_revision_key = p_content_revision_key
   LIMIT 1;
  IF v_content_revision_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_enqueue_server_content_import_job: content revision not found';
  END IF;

  IF COALESCE(TRIM(p_importer_key), '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_enqueue_server_content_import_job: importer key is required';
  END IF;
  IF COALESCE(TRIM(v_source_logical_path), '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_enqueue_server_content_import_job: source logical path is required';
  END IF;
  IF p_source_sha256 IS NOT NULL AND p_source_sha256 NOT REGEXP '^[0-9a-f]{64}$' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_enqueue_server_content_import_job: source sha must be lowercase hex';
  END IF;

  SET v_source_kind = COALESCE(NULLIF(p_source_kind, ''), 'other');
  IF v_source_kind NOT IN ('archive', 'world_zen', 'scripts_dat', 'dialog_ou', 'script_source', 'config_ini', 'asset', 'other') THEN
    SET v_source_kind = 'other';
  END IF;

  SET v_job_status = COALESCE(NULLIF(p_job_status, ''), 'queued');
  IF v_job_status NOT IN ('queued', 'running', 'succeeded', 'failed', 'skipped', 'blocked') THEN
    SET v_job_status = 'queued';
  END IF;

  INSERT INTO mmo_server_content_import_jobs(
    content_revision_id,
    importer_key,
    source_kind,
    source_logical_path,
    source_sha256,
    job_priority,
    job_status,
    input_payload
  )
  VALUES (
    v_content_revision_id,
    p_importer_key,
    v_source_kind,
    v_source_logical_path,
    LOWER(NULLIF(p_source_sha256, '')),
    COALESCE(p_job_priority, 1000),
    v_job_status,
    COALESCE(p_input_payload, JSON_OBJECT())
  )
  ON DUPLICATE KEY UPDATE
    source_kind = VALUES(source_kind),
    job_priority = VALUES(job_priority),
    job_status = CASE
      WHEN mmo_server_content_import_jobs.job_status IN ('succeeded', 'running')
        THEN mmo_server_content_import_jobs.job_status
      ELSE VALUES(job_status)
    END,
    input_payload = VALUES(input_payload),
    updated_at = CURRENT_TIMESTAMP(6);

  SELECT content_import_job_id
    INTO o_content_import_job_id
    FROM mmo_server_content_import_jobs
   WHERE content_revision_id = v_content_revision_id
     AND importer_key = p_importer_key
     AND source_logical_path = v_source_logical_path
     AND ((source_sha256 IS NULL AND p_source_sha256 IS NULL) OR source_sha256 = LOWER(NULLIF(p_source_sha256, '')))
   LIMIT 1;
END;;

DELIMITER ;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES (
  'server/sql/step206_server_content_import_jobs.sql',
  'step206_server_content_import_jobs_v1',
  'Step206: server content import job queue for future ZEN/DAT/OU importers'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);

-- ============================================================================
-- END server/sql/step206_server_content_import_jobs.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step208_server_content_build_indexes.sql
-- ============================================================================

-- Step208: content build result indexes for future ZEN/DAT/OU importers.
-- Step206 queues import work. This step defines the first durable output
-- contract: where the future importers write world entities, Daedalus symbols
-- and dialog outputs so the MMO server can query them without reading client
-- files.

CREATE TABLE IF NOT EXISTS mmo_server_content_build_imports (
  content_build_import_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_revision_id BINARY(16) NOT NULL,
  importer_key VARCHAR(96) NOT NULL,
  source_kind VARCHAR(48) NOT NULL,
  source_logical_path VARCHAR(512) NOT NULL,
  source_sha256 CHAR(64) NULL,
  import_status VARCHAR(32) NOT NULL DEFAULT 'imported',
  item_count INT NOT NULL DEFAULT 0,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  imported_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (content_build_import_id),
  UNIQUE KEY mmo_content_build_import_uk (content_revision_id, importer_key, source_logical_path, source_sha256),
  KEY ix_mmo_content_build_import_revision (content_revision_id, importer_key, import_status),
  CONSTRAINT mmo_content_build_import_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_content_build_import_sha_ck CHECK (source_sha256 IS NULL OR REGEXP_LIKE(source_sha256, '^[0-9a-f]{64}$')),
  CONSTRAINT mmo_content_build_import_status_ck CHECK (import_status IN ('imported', 'partial', 'failed', 'skipped')),
  CONSTRAINT mmo_content_build_import_kind_ck CHECK (source_kind IN ('world_zen', 'scripts_dat', 'dialog_ou', 'other')),
  CONSTRAINT mmo_content_build_import_count_ck CHECK (item_count >= 0),
  CONSTRAINT mmo_content_build_import_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_server_world_zen_entities (
  zen_entity_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_id BINARY(16) NOT NULL,
  world_name VARCHAR(191) NOT NULL,
  entity_kind VARCHAR(32) NOT NULL,
  entity_key VARCHAR(191) NOT NULL,
  entity_name VARCHAR(191) NOT NULL DEFAULT '',
  pos_x DOUBLE NULL,
  pos_y DOUBLE NULL,
  pos_z DOUBLE NULL,
  dir_x DOUBLE NULL,
  dir_y DOUBLE NULL,
  dir_z DOUBLE NULL,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (zen_entity_id),
  UNIQUE KEY mmo_world_zen_entity_uk (content_revision_id, world_name, entity_kind, entity_key),
  KEY ix_mmo_world_zen_entity_import (content_build_import_id),
  KEY ix_mmo_world_zen_entity_kind (content_revision_id, world_name, entity_kind),
  KEY ix_mmo_world_zen_entity_pos (content_revision_id, world_name, pos_x, pos_y, pos_z),
  CONSTRAINT mmo_world_zen_entity_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES mmo_server_content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT mmo_world_zen_entity_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_world_zen_entity_kind_ck CHECK (entity_kind IN ('world', 'waypoint', 'freepoint', 'vob', 'trigger', 'spawn', 'other')),
  CONSTRAINT mmo_world_zen_entity_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_server_daedalus_symbols (
  daedalus_symbol_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_id BINARY(16) NOT NULL,
  symbol_name VARCHAR(191) NOT NULL,
  symbol_kind VARCHAR(48) NOT NULL DEFAULT 'unknown',
  data_type VARCHAR(48) NOT NULL DEFAULT '',
  parent_symbol VARCHAR(191) NOT NULL DEFAULT '',
  ordinal INT NOT NULL DEFAULT 0,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (daedalus_symbol_id),
  UNIQUE KEY mmo_daedalus_symbol_uk (content_revision_id, symbol_name),
  KEY ix_mmo_daedalus_symbol_import (content_build_import_id),
  KEY ix_mmo_daedalus_symbol_kind (content_revision_id, symbol_kind),
  CONSTRAINT mmo_daedalus_symbol_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES mmo_server_content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT mmo_daedalus_symbol_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_daedalus_symbol_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_server_daedalus_npc_templates (
  daedalus_npc_template_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_id BINARY(16) NOT NULL,
  npc_instance VARCHAR(191) NOT NULL,
  display_name VARCHAR(191) NOT NULL DEFAULT '',
  guild VARCHAR(96) NOT NULL DEFAULT '',
  level_value INT NULL,
  routine_symbol VARCHAR(191) NOT NULL DEFAULT '',
  perception_symbol VARCHAR(191) NOT NULL DEFAULT '',
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (daedalus_npc_template_id),
  UNIQUE KEY mmo_daedalus_npc_template_uk (content_revision_id, npc_instance),
  KEY ix_mmo_daedalus_npc_template_import (content_build_import_id),
  KEY ix_mmo_daedalus_npc_template_guild (content_revision_id, guild),
  CONSTRAINT mmo_daedalus_npc_template_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES mmo_server_content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT mmo_daedalus_npc_template_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_daedalus_npc_template_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_server_dialog_outputs (
  dialog_output_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_id BINARY(16) NOT NULL,
  output_name VARCHAR(191) NOT NULL,
  text_value TEXT NULL,
  audio_ref VARCHAR(255) NOT NULL DEFAULT '',
  speaker_symbol VARCHAR(191) NOT NULL DEFAULT '',
  target_symbol VARCHAR(191) NOT NULL DEFAULT '',
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (dialog_output_id),
  UNIQUE KEY mmo_dialog_output_uk (content_revision_id, output_name),
  KEY ix_mmo_dialog_output_import (content_build_import_id),
  KEY ix_mmo_dialog_output_speaker (content_revision_id, speaker_symbol),
  CONSTRAINT mmo_dialog_output_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES mmo_server_content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT mmo_dialog_output_revision_fk
    FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE CASCADE,
  CONSTRAINT mmo_dialog_output_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP VIEW IF EXISTS v_mmo_server_content_build_health;
DROP VIEW IF EXISTS v_mmo_server_dialog_outputs;
DROP VIEW IF EXISTS v_mmo_server_daedalus_npc_templates;
DROP VIEW IF EXISTS v_mmo_server_daedalus_symbols;
DROP VIEW IF EXISTS v_mmo_server_world_zen_entities;
DROP VIEW IF EXISTS v_mmo_server_content_build_imports;

CREATE VIEW v_mmo_server_content_build_imports AS
SELECT
  BIN_TO_UUID(i.content_build_import_id, 1) AS content_build_import_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  i.importer_key,
  i.source_kind,
  i.source_logical_path,
  i.source_sha256,
  i.import_status,
  i.item_count,
  i.raw_payload,
  i.imported_at,
  i.created_at,
  i.updated_at
FROM mmo_server_content_build_imports i
JOIN content_revisions cr ON cr.content_revision_id = i.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id;

CREATE VIEW v_mmo_server_world_zen_entities AS
SELECT
  BIN_TO_UUID(e.zen_entity_id, 1) AS zen_entity_uuid,
  BIN_TO_UUID(e.content_build_import_id, 1) AS content_build_import_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  e.world_name,
  e.entity_kind,
  e.entity_key,
  e.entity_name,
  e.pos_x,
  e.pos_y,
  e.pos_z,
  e.dir_x,
  e.dir_y,
  e.dir_z,
  e.raw_payload
FROM mmo_server_world_zen_entities e
JOIN content_revisions cr ON cr.content_revision_id = e.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id;

CREATE VIEW v_mmo_server_daedalus_symbols AS
SELECT
  BIN_TO_UUID(s.daedalus_symbol_id, 1) AS daedalus_symbol_uuid,
  BIN_TO_UUID(s.content_build_import_id, 1) AS content_build_import_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  s.symbol_name,
  s.symbol_kind,
  s.data_type,
  s.parent_symbol,
  s.ordinal,
  s.raw_payload
FROM mmo_server_daedalus_symbols s
JOIN content_revisions cr ON cr.content_revision_id = s.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id;

CREATE VIEW v_mmo_server_daedalus_npc_templates AS
SELECT
  BIN_TO_UUID(n.daedalus_npc_template_id, 1) AS daedalus_npc_template_uuid,
  BIN_TO_UUID(n.content_build_import_id, 1) AS content_build_import_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  n.npc_instance,
  n.display_name,
  n.guild,
  n.level_value,
  n.routine_symbol,
  n.perception_symbol,
  n.raw_payload
FROM mmo_server_daedalus_npc_templates n
JOIN content_revisions cr ON cr.content_revision_id = n.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id;

CREATE VIEW v_mmo_server_dialog_outputs AS
SELECT
  BIN_TO_UUID(o.dialog_output_id, 1) AS dialog_output_uuid,
  BIN_TO_UUID(o.content_build_import_id, 1) AS content_build_import_uuid,
  BIN_TO_UUID(cr.content_revision_id, 1) AS content_revision_uuid,
  cgt.game_code,
  cr.content_revision_key,
  cr.is_active,
  o.output_name,
  o.text_value,
  o.audio_ref,
  o.speaker_symbol,
  o.target_symbol,
  o.raw_payload
FROM mmo_server_dialog_outputs o
JOIN content_revisions cr ON cr.content_revision_id = o.content_revision_id
JOIN content_game_targets cgt ON cgt.game_target_id = cr.game_target_id;

CREATE VIEW v_mmo_server_content_build_health AS
SELECT
  cr.content_revision_key,
  cr.is_active,
  COUNT(DISTINCT bi.content_build_import_id) AS build_import_count,
  SUM(CASE WHEN bi.import_status = 'imported' THEN 1 ELSE 0 END) AS imported_count,
  SUM(CASE WHEN bi.import_status = 'failed' THEN 1 ELSE 0 END) AS failed_count,
  (SELECT COUNT(*) FROM mmo_server_world_zen_entities z WHERE z.content_revision_id = cr.content_revision_id) AS zen_entity_count,
  (SELECT COUNT(*) FROM mmo_server_world_zen_entities z WHERE z.content_revision_id = cr.content_revision_id AND z.entity_kind = 'waypoint') AS waypoint_count,
  (SELECT COUNT(*) FROM mmo_server_world_zen_entities z WHERE z.content_revision_id = cr.content_revision_id AND z.entity_kind = 'freepoint') AS freepoint_count,
  (SELECT COUNT(*) FROM mmo_server_world_zen_entities z WHERE z.content_revision_id = cr.content_revision_id AND z.entity_kind = 'vob') AS vob_count,
  (SELECT COUNT(*) FROM mmo_server_daedalus_symbols s WHERE s.content_revision_id = cr.content_revision_id) AS daedalus_symbol_count,
  (SELECT COUNT(*) FROM mmo_server_daedalus_npc_templates n WHERE n.content_revision_id = cr.content_revision_id) AS npc_template_count,
  (SELECT COUNT(*) FROM mmo_server_dialog_outputs o WHERE o.content_revision_id = cr.content_revision_id) AS dialog_output_count,
  MAX(bi.updated_at) AS last_build_update_at
FROM content_revisions cr
LEFT JOIN mmo_server_content_build_imports bi ON bi.content_revision_id = cr.content_revision_id
GROUP BY cr.content_revision_key, cr.is_active, cr.content_revision_id;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES (
  'server/sql/step208_server_content_build_indexes.sql',
  'step208_server_content_build_indexes_v1',
  'Step208: server content build result indexes for future ZEN/DAT/OU importers'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);

-- ============================================================================
-- END server/sql/step208_server_content_build_indexes.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step189_server_content_pack_session_gate.sql
-- ============================================================================

-- Step189: session-scoped content pack validation gate.
-- Step188 validates by realm key. The UDP server usually knows the DB session
-- UUID after login/recovery, so this wrapper resolves the realm/content revision
-- through server_sessions and returns the same compact decision.

DELIMITER ;;

DROP PROCEDURE IF EXISTS mmo_validate_client_content_pack_for_session;;
CREATE PROCEDURE mmo_validate_client_content_pack_for_session(
  IN p_session_id BINARY(16),
  IN p_client_manifest_hash CHAR(64),
  OUT o_accepted TINYINT(1),
  OUT o_reason VARCHAR(191),
  OUT o_server_manifest_hash CHAR(64),
  OUT o_content_revision_key VARCHAR(191)
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_content_revision_id BINARY(16) DEFAULT NULL;
  DECLARE CONTINUE HANDLER FOR NOT FOUND BEGIN END;

  SET o_accepted = 0;
  SET o_reason = 'unknown';
  SET o_server_manifest_hash = NULL;
  SET o_content_revision_key = NULL;

  SELECT ss.realm_id, rr.active_content_revision_id, cr.content_revision_key
    INTO v_realm_id, v_content_revision_id, o_content_revision_key
    FROM server_sessions ss
    JOIN realm_realms rr ON rr.realm_id = ss.realm_id
    JOIN content_revisions cr ON cr.content_revision_id = rr.active_content_revision_id
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1;
  IF v_realm_id IS NULL THEN
    SET o_reason = 'active_session_not_found';
    LEAVE proc;
  END IF;

  SELECT manifest_hash
    INTO o_server_manifest_hash
    FROM mmo_server_content_pack_manifests
   WHERE content_revision_id = v_content_revision_id
   LIMIT 1;
  IF o_server_manifest_hash IS NULL THEN
    SET o_reason = 'server_manifest_missing';
    LEAVE proc;
  END IF;

  IF COALESCE(p_client_manifest_hash, '') = '' THEN
    SET o_reason = 'client_manifest_missing';
    LEAVE proc;
  END IF;

  IF LOWER(p_client_manifest_hash) <> LOWER(o_server_manifest_hash) THEN
    SET o_reason = 'content_hash_mismatch';
    LEAVE proc;
  END IF;

  SET o_accepted = 1;
  SET o_reason = 'ok';
END;;

DELIMITER ;

-- ============================================================================
-- END server/sql/step189_server_content_pack_session_gate.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step198_content_manifest_reject_audit.sql
-- ============================================================================

-- Step198: server-side DB audit for client content manifest bootstrap rejects.
-- Additive bridge table/procedure before the future persistence rewrite.

CREATE TABLE IF NOT EXISTS mmo_content_manifest_reject_audit (
  reject_id BINARY(16) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  session_id BINARY(16) NULL,
  realm_id BINARY(16) NULL,
  account_id BINARY(16) NULL,
  character_id BINARY(16) NULL,
  world_instance_id BINARY(16) NULL,
  content_revision_id BINARY(16) NULL,
  remote_endpoint VARCHAR(191) NOT NULL DEFAULT '',
  packet_session_key VARCHAR(191) NOT NULL DEFAULT '',
  target_key VARCHAR(191) NOT NULL DEFAULT '',
  packet_sequence BIGINT UNSIGNED NOT NULL DEFAULT 0,
  local_sequence BIGINT UNSIGNED NOT NULL DEFAULT 0,
  phase VARCHAR(64) NOT NULL DEFAULT '',
  reason VARCHAR(191) NOT NULL,
  client_manifest_hash VARCHAR(128) NULL,
  server_manifest_hash CHAR(64) NULL,
  content_revision_key VARCHAR(191) NULL,
  message VARCHAR(1024) NOT NULL DEFAULT '',
  payload_json JSON NOT NULL DEFAULT (JSON_OBJECT()),
  PRIMARY KEY (reject_id),
  KEY ix_mmo_cm_reject_created (created_at),
  KEY ix_mmo_cm_reject_reason_created (reason, created_at),
  KEY ix_mmo_cm_reject_session_created (session_id, created_at),
  KEY ix_mmo_cm_reject_realm_reason (realm_id, reason, created_at),
  KEY ix_mmo_cm_reject_revision_reason (content_revision_id, reason, created_at),
  KEY ix_mmo_cm_reject_client_hash (client_manifest_hash),
  KEY ix_mmo_cm_reject_server_hash (server_manifest_hash),
  CONSTRAINT mmo_cm_reject_session_fk FOREIGN KEY (session_id) REFERENCES server_sessions(session_id) ON DELETE SET NULL,
  CONSTRAINT mmo_cm_reject_realm_fk FOREIGN KEY (realm_id) REFERENCES realm_realms(realm_id) ON DELETE SET NULL,
  CONSTRAINT mmo_cm_reject_account_fk FOREIGN KEY (account_id) REFERENCES account_accounts(account_id) ON DELETE SET NULL,
  CONSTRAINT mmo_cm_reject_character_fk FOREIGN KEY (character_id) REFERENCES characters(character_id) ON DELETE SET NULL,
  CONSTRAINT mmo_cm_reject_world_fk FOREIGN KEY (world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE SET NULL,
  CONSTRAINT mmo_cm_reject_revision_fk FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE SET NULL,
  CONSTRAINT mmo_cm_reject_payload_json_ck CHECK (JSON_VALID(payload_json)),
  CONSTRAINT mmo_cm_reject_server_hash_ck CHECK (server_manifest_hash IS NULL OR REGEXP_LIKE(server_manifest_hash, '^[0-9a-f]{64}$'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_record_content_manifest_reject;
DELIMITER ;;
CREATE PROCEDURE mmo_record_content_manifest_reject(
  IN p_session_id BINARY(16),
  IN p_remote_endpoint VARCHAR(191),
  IN p_packet_session_key VARCHAR(191),
  IN p_target_key VARCHAR(191),
  IN p_packet_sequence BIGINT UNSIGNED,
  IN p_local_sequence BIGINT UNSIGNED,
  IN p_phase VARCHAR(64),
  IN p_reason VARCHAR(191),
  IN p_client_manifest_hash VARCHAR(128),
  IN p_server_manifest_hash CHAR(64),
  IN p_content_revision_key VARCHAR(191),
  IN p_message TEXT,
  IN p_payload_json JSON,
  OUT o_reject_id BINARY(16)
)
proc: BEGIN
  DECLARE v_reject_id BINARY(16) DEFAULT UUID_TO_BIN(UUID(), 1);
  DECLARE v_session_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_account_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_content_revision_id BINARY(16) DEFAULT NULL;
  DECLARE CONTINUE HANDLER FOR NOT FOUND BEGIN END;

  SET o_reject_id = v_reject_id;

  IF p_session_id IS NOT NULL THEN
    SELECT ss.session_id,
           ss.realm_id,
           ss.account_id,
           ss.character_id,
           ss.world_instance_id,
           rr.active_content_revision_id
      INTO v_session_id,
           v_realm_id,
           v_account_id,
           v_character_id,
           v_world_instance_id,
           v_content_revision_id
      FROM server_sessions ss
      LEFT JOIN realm_realms rr ON rr.realm_id = ss.realm_id
     WHERE ss.session_id = p_session_id
     LIMIT 1;
  END IF;

  IF v_content_revision_id IS NULL AND COALESCE(p_content_revision_key, '') <> '' THEN
    SELECT content_revision_id
      INTO v_content_revision_id
      FROM content_revisions
     WHERE content_revision_key = p_content_revision_key
     LIMIT 1;
  END IF;

  INSERT INTO mmo_content_manifest_reject_audit(
    reject_id,
    session_id,
    realm_id,
    account_id,
    character_id,
    world_instance_id,
    content_revision_id,
    remote_endpoint,
    packet_session_key,
    target_key,
    packet_sequence,
    local_sequence,
    phase,
    reason,
    client_manifest_hash,
    server_manifest_hash,
    content_revision_key,
    message,
    payload_json
  ) VALUES (
    v_reject_id,
    v_session_id,
    v_realm_id,
    v_account_id,
    v_character_id,
    v_world_instance_id,
    v_content_revision_id,
    COALESCE(p_remote_endpoint, ''),
    COALESCE(p_packet_session_key, ''),
    COALESCE(p_target_key, ''),
    COALESCE(p_packet_sequence, 0),
    COALESCE(p_local_sequence, 0),
    COALESCE(p_phase, ''),
    COALESCE(NULLIF(p_reason, ''), 'content_manifest_rejected'),
    NULLIF(LOWER(COALESCE(p_client_manifest_hash, '')), ''),
    NULLIF(LOWER(COALESCE(p_server_manifest_hash, '')), ''),
    NULLIF(p_content_revision_key, ''),
    LEFT(COALESCE(p_message, ''), 1024),
    COALESCE(p_payload_json, JSON_OBJECT())
  );
END ;;
DELIMITER ;

CREATE OR REPLACE VIEW v_mmo_content_manifest_reject_audit AS
SELECT
  BIN_TO_UUID(a.reject_id, 1) AS reject_uuid,
  a.created_at,
  BIN_TO_UUID(a.session_id, 1) AS session_uuid,
  BIN_TO_UUID(a.realm_id, 1) AS realm_uuid,
  rr.realm_key,
  BIN_TO_UUID(a.account_id, 1) AS account_uuid,
  aa.account_name,
  BIN_TO_UUID(a.character_id, 1) AS character_uuid,
  c.character_key,
  BIN_TO_UUID(a.world_instance_id, 1) AS world_instance_uuid,
  rwi.world_instance_key,
  BIN_TO_UUID(a.content_revision_id, 1) AS content_revision_uuid,
  COALESCE(cr.content_revision_key, a.content_revision_key) AS content_revision_key,
  a.remote_endpoint,
  a.packet_session_key,
  a.target_key,
  a.packet_sequence,
  a.local_sequence,
  a.phase,
  a.reason,
  a.client_manifest_hash,
  a.server_manifest_hash,
  a.message,
  a.payload_json
FROM mmo_content_manifest_reject_audit a
LEFT JOIN realm_realms rr ON rr.realm_id = a.realm_id
LEFT JOIN account_accounts aa ON aa.account_id = a.account_id
LEFT JOIN characters c ON c.character_id = a.character_id
LEFT JOIN realm_world_instances rwi ON rwi.world_instance_id = a.world_instance_id
LEFT JOIN content_revisions cr ON cr.content_revision_id = a.content_revision_id;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES (
  'server/sql/step198_content_manifest_reject_audit.sql',
  'server/sql',
  'Step198: DB audit table/procedure/view for content manifest bootstrap rejects'
)
ON DUPLICATE KEY UPDATE
  applied_at = CURRENT_TIMESTAMP(6),
  notes = VALUES(notes);

-- ============================================================================
-- END server/sql/step198_content_manifest_reject_audit.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step199_content_manifest_reject_health_views.sql
-- ============================================================================

-- Step199: aggregate health views for content manifest bootstrap reject audits.

CREATE OR REPLACE VIEW v_mmo_content_manifest_reject_summary AS
SELECT
  COALESCE(reason, '<empty>') AS reason,
  COALESCE(content_revision_key, '<empty>') AS content_revision_key,
  COALESCE(server_manifest_hash, '<empty>') AS server_manifest_hash,
  COUNT(*) AS total_count,
  SUM(CASE WHEN created_at >= UTC_TIMESTAMP(6) - INTERVAL 1 HOUR THEN 1 ELSE 0 END) AS last_hour_count,
  SUM(CASE WHEN created_at >= UTC_TIMESTAMP(6) - INTERVAL 15 MINUTE THEN 1 ELSE 0 END) AS last_15m_count,
  MIN(created_at) AS first_seen_at,
  MAX(created_at) AS last_seen_at
FROM mmo_content_manifest_reject_audit
GROUP BY
  COALESCE(reason, '<empty>'),
  COALESCE(content_revision_key, '<empty>'),
  COALESCE(server_manifest_hash, '<empty>');

CREATE OR REPLACE VIEW v_mmo_content_manifest_reject_health AS
SELECT
  COUNT(*) AS total_rejects,
  SUM(CASE WHEN created_at >= UTC_TIMESTAMP(6) - INTERVAL 1 HOUR THEN 1 ELSE 0 END) AS last_hour_rejects,
  SUM(CASE WHEN created_at >= UTC_TIMESTAMP(6) - INTERVAL 15 MINUTE THEN 1 ELSE 0 END) AS last_15m_rejects,
  SUM(CASE WHEN reason = 'content_hash_mismatch' THEN 1 ELSE 0 END) AS total_hash_mismatches,
  SUM(CASE WHEN reason = 'content_hash_mismatch' AND created_at >= UTC_TIMESTAMP(6) - INTERVAL 1 HOUR THEN 1 ELSE 0 END) AS last_hour_hash_mismatches,
  SUM(CASE WHEN reason = 'client_manifest_missing' THEN 1 ELSE 0 END) AS total_missing_client_manifests,
  SUM(CASE WHEN reason = 'server_manifest_missing' THEN 1 ELSE 0 END) AS total_missing_server_manifests,
  COUNT(DISTINCT remote_endpoint) AS distinct_remote_endpoints,
  COUNT(DISTINCT client_manifest_hash) AS distinct_client_hashes,
  MAX(created_at) AS last_reject_at
FROM mmo_content_manifest_reject_audit;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES (
  'server/sql/step199_content_manifest_reject_health_views.sql',
  'server/sql',
  'Step199: aggregate health views for content manifest reject audits'
)
ON DUPLICATE KEY UPDATE
  applied_at = CURRENT_TIMESTAMP(6),
  notes = VALUES(notes);

-- ============================================================================
-- END server/sql/step199_content_manifest_reject_health_views.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step211_content_build_database.sql
-- ============================================================================

-- Step211: separate content-build database for server-owned ZEN/DAT/OU imports.
--
-- Runtime MMO state and content-build results have different lifecycles.
-- The runtime database stores players, active worlds, events and durable MMO
-- state. This database stores parser/build output from the server's own Gothic
-- content pack copy. Future parser jobs write here first; the runtime DB should
-- only consume a validated, ready content revision.

CREATE DATABASE IF NOT EXISTS mmo_content_build
  DEFAULT CHARACTER SET utf8mb4
  DEFAULT COLLATE utf8mb4_0900_ai_ci;

USE mmo_content_build;

CREATE TABLE IF NOT EXISTS content_build_schema_versions (
  migration_key VARCHAR(255) NOT NULL,
  checksum CHAR(64) NOT NULL,
  description TEXT NOT NULL,
  applied_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (migration_key),
  CONSTRAINT content_build_schema_versions_checksum_ck CHECK (REGEXP_LIKE(checksum, '^[0-9a-f]{64}$'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS content_build_revisions (
  content_revision_key VARCHAR(191) NOT NULL,
  game_code VARCHAR(64) NOT NULL DEFAULT 'gothic2-notr',
  manifest_hash CHAR(64) NULL,
  source_root_label VARCHAR(512) NOT NULL DEFAULT '',
  build_status VARCHAR(32) NOT NULL DEFAULT 'draft',
  source_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (content_revision_key),
  KEY ix_content_build_revisions_game_status (game_code, build_status),
  KEY ix_content_build_revisions_manifest_hash (manifest_hash),
  CONSTRAINT content_build_revisions_manifest_hash_ck CHECK (manifest_hash IS NULL OR REGEXP_LIKE(manifest_hash, '^[0-9a-f]{64}$')),
  CONSTRAINT content_build_revisions_status_ck CHECK (build_status IN ('draft', 'building', 'ready', 'failed', 'retired')),
  CONSTRAINT content_build_revisions_payload_json_ck CHECK (JSON_VALID(source_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS content_build_files (
  content_revision_key VARCHAR(191) NOT NULL,
  logical_path VARCHAR(512) NOT NULL,
  source_kind VARCHAR(48) NOT NULL DEFAULT 'other',
  file_role VARCHAR(64) NOT NULL DEFAULT 'other',
  byte_size BIGINT UNSIGNED NOT NULL DEFAULT 0,
  sha256 CHAR(64) NOT NULL,
  required_for_server_authority TINYINT(1) NOT NULL DEFAULT 0,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (content_revision_key, logical_path),
  KEY ix_content_build_files_kind_role (content_revision_key, source_kind, file_role),
  KEY ix_content_build_files_sha (sha256),
  CONSTRAINT content_build_files_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT content_build_files_kind_ck CHECK (source_kind IN ('world_zen', 'scripts_dat', 'dialog_ou', 'archive_vdf', 'archive_mod', 'texture', 'mesh', 'sound', 'other')),
  CONSTRAINT content_build_files_sha_ck CHECK (REGEXP_LIKE(sha256, '^[0-9a-f]{64}$')),
  CONSTRAINT content_build_files_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS content_build_imports (
  content_build_import_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_revision_key VARCHAR(191) NOT NULL,
  importer_key VARCHAR(96) NOT NULL,
  source_kind VARCHAR(48) NOT NULL,
  source_logical_path VARCHAR(512) NOT NULL,
  source_logical_path_hash CHAR(64) GENERATED ALWAYS AS (SHA2(source_logical_path, 256)) STORED,
  source_sha256 CHAR(64) NULL,
  import_status VARCHAR(32) NOT NULL DEFAULT 'imported',
  item_count INT NOT NULL DEFAULT 0,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  imported_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (content_build_import_id),
  UNIQUE KEY content_build_import_uk (content_revision_key, importer_key, source_logical_path_hash, source_sha256),
  KEY ix_content_build_import_revision (content_revision_key, importer_key, import_status),
  KEY ix_content_build_import_source_path (content_revision_key, source_logical_path_hash),
  CONSTRAINT content_build_import_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT content_build_import_sha_ck CHECK (source_sha256 IS NULL OR REGEXP_LIKE(source_sha256, '^[0-9a-f]{64}$')),
  CONSTRAINT content_build_import_status_ck CHECK (import_status IN ('imported', 'partial', 'failed', 'skipped')),
  CONSTRAINT content_build_import_kind_ck CHECK (source_kind IN ('world_zen', 'scripts_dat', 'dialog_ou', 'archive_vdf', 'archive_mod', 'other')),
  CONSTRAINT content_build_import_count_ck CHECK (item_count >= 0),
  CONSTRAINT content_build_import_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS content_build_parser_errors (
  parser_error_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_revision_key VARCHAR(191) NOT NULL,
  content_build_import_id BINARY(16) NULL,
  severity VARCHAR(16) NOT NULL DEFAULT 'warning',
  error_scope VARCHAR(64) NOT NULL DEFAULT 'parser',
  error_code VARCHAR(96) NOT NULL DEFAULT '',
  source_logical_path VARCHAR(512) NOT NULL DEFAULT '',
  message_text TEXT NOT NULL,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (parser_error_id),
  KEY ix_content_build_parser_errors_revision (content_revision_key, severity, error_scope),
  KEY ix_content_build_parser_errors_import (content_build_import_id),
  CONSTRAINT content_build_parser_errors_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT content_build_parser_errors_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES content_build_imports(content_build_import_id) ON DELETE SET NULL,
  CONSTRAINT content_build_parser_errors_severity_ck CHECK (severity IN ('info', 'warning', 'error', 'fatal')),
  CONSTRAINT content_build_parser_errors_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS world_zen_entities (
  zen_entity_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL,
  world_name VARCHAR(191) NOT NULL,
  entity_kind VARCHAR(32) NOT NULL,
  entity_key VARCHAR(191) NOT NULL,
  entity_name VARCHAR(191) NOT NULL DEFAULT '',
  pos_x DOUBLE NULL,
  pos_y DOUBLE NULL,
  pos_z DOUBLE NULL,
  dir_x DOUBLE NULL,
  dir_y DOUBLE NULL,
  dir_z DOUBLE NULL,
  radius_value DOUBLE NULL,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (zen_entity_id),
  UNIQUE KEY world_zen_entity_uk (content_revision_key, world_name, entity_kind, entity_key),
  KEY ix_world_zen_entity_import (content_build_import_id),
  KEY ix_world_zen_entity_kind (content_revision_key, world_name, entity_kind),
  KEY ix_world_zen_entity_pos (content_revision_key, world_name, pos_x, pos_y, pos_z),
  CONSTRAINT world_zen_entity_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT world_zen_entity_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT world_zen_entity_kind_ck CHECK (entity_kind IN ('world', 'waypoint', 'freepoint', 'vob', 'trigger', 'mover', 'spawn', 'sound', 'light', 'other')),
  CONSTRAINT world_zen_entity_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS world_waypoint_edges (
  waypoint_edge_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL,
  world_name VARCHAR(191) NOT NULL,
  from_waypoint_key VARCHAR(191) NOT NULL,
  to_waypoint_key VARCHAR(191) NOT NULL,
  edge_identity_hash CHAR(64) GENERATED ALWAYS AS (SHA2(CONCAT_WS('|', world_name, from_waypoint_key, to_waypoint_key), 256)) STORED,
  travel_cost DOUBLE NOT NULL DEFAULT 1,
  edge_flags VARCHAR(191) NOT NULL DEFAULT '',
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (waypoint_edge_id),
  UNIQUE KEY world_waypoint_edge_uk (content_revision_key, edge_identity_hash),
  KEY ix_world_waypoint_edge_from (content_revision_key, world_name, from_waypoint_key),
  KEY ix_world_waypoint_edge_to (content_revision_key, world_name, to_waypoint_key),
  CONSTRAINT world_waypoint_edge_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT world_waypoint_edge_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT world_waypoint_edge_cost_ck CHECK (travel_cost >= 0),
  CONSTRAINT world_waypoint_edge_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS daedalus_symbols (
  daedalus_symbol_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL,
  symbol_name VARCHAR(191) NOT NULL,
  symbol_kind VARCHAR(48) NOT NULL DEFAULT 'unknown',
  data_type VARCHAR(48) NOT NULL DEFAULT '',
  parent_symbol VARCHAR(191) NOT NULL DEFAULT '',
  ordinal INT NOT NULL DEFAULT 0,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (daedalus_symbol_id),
  UNIQUE KEY daedalus_symbol_uk (content_revision_key, symbol_name),
  KEY ix_daedalus_symbol_import (content_build_import_id),
  KEY ix_daedalus_symbol_kind (content_revision_key, symbol_kind),
  KEY ix_daedalus_symbol_parent (content_revision_key, parent_symbol),
  CONSTRAINT daedalus_symbol_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT daedalus_symbol_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT daedalus_symbol_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS daedalus_npc_templates (
  daedalus_npc_template_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL,
  npc_instance VARCHAR(191) NOT NULL,
  display_name VARCHAR(191) NOT NULL DEFAULT '',
  guild VARCHAR(96) NOT NULL DEFAULT '',
  level_value INT NULL,
  routine_symbol VARCHAR(191) NOT NULL DEFAULT '',
  perception_symbol VARCHAR(191) NOT NULL DEFAULT '',
  fight_tactic VARCHAR(96) NOT NULL DEFAULT '',
  voice_symbol VARCHAR(191) NOT NULL DEFAULT '',
  attributes_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (daedalus_npc_template_id),
  UNIQUE KEY daedalus_npc_template_uk (content_revision_key, npc_instance),
  KEY ix_daedalus_npc_template_import (content_build_import_id),
  KEY ix_daedalus_npc_template_guild (content_revision_key, guild),
  CONSTRAINT daedalus_npc_template_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT daedalus_npc_template_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT daedalus_npc_template_attributes_json_ck CHECK (JSON_VALID(attributes_payload)),
  CONSTRAINT daedalus_npc_template_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS daedalus_item_templates (
  daedalus_item_template_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL,
  item_instance VARCHAR(191) NOT NULL,
  display_name VARCHAR(191) NOT NULL DEFAULT '',
  item_category VARCHAR(96) NOT NULL DEFAULT '',
  main_flag INT NULL,
  flags_value BIGINT NULL,
  value_amount INT NULL,
  damage_total INT NULL,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (daedalus_item_template_id),
  UNIQUE KEY daedalus_item_template_uk (content_revision_key, item_instance),
  KEY ix_daedalus_item_template_import (content_build_import_id),
  KEY ix_daedalus_item_template_category (content_revision_key, item_category),
  CONSTRAINT daedalus_item_template_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT daedalus_item_template_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT daedalus_item_template_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS daedalus_routines (
  daedalus_routine_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL,
  npc_instance VARCHAR(191) NOT NULL,
  routine_symbol VARCHAR(191) NOT NULL,
  day_minute_start SMALLINT UNSIGNED NULL,
  day_minute_end SMALLINT UNSIGNED NULL,
  target_point_key VARCHAR(191) NOT NULL DEFAULT '',
  routine_identity_hash CHAR(64) GENERATED ALWAYS AS (SHA2(CONCAT_WS('|', npc_instance, routine_symbol, COALESCE(CAST(day_minute_start AS CHAR), ''), target_point_key), 256)) STORED,
  action_symbol VARCHAR(191) NOT NULL DEFAULT '',
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (daedalus_routine_id),
  UNIQUE KEY daedalus_routine_uk (content_revision_key, routine_identity_hash),
  KEY ix_daedalus_routine_npc (content_revision_key, npc_instance),
  KEY ix_daedalus_routine_point (content_revision_key, target_point_key),
  CONSTRAINT daedalus_routine_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT daedalus_routine_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT daedalus_routine_start_ck CHECK (day_minute_start IS NULL OR day_minute_start < 1440),
  CONSTRAINT daedalus_routine_end_ck CHECK (day_minute_end IS NULL OR day_minute_end < 1440),
  CONSTRAINT daedalus_routine_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS daedalus_perception_bindings (
  daedalus_perception_binding_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL,
  owner_symbol VARCHAR(191) NOT NULL DEFAULT '',
  owner_kind VARCHAR(32) NOT NULL DEFAULT 'npc',
  perception_kind VARCHAR(64) NOT NULL,
  function_symbol VARCHAR(191) NOT NULL,
  priority_value INT NOT NULL DEFAULT 0,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (daedalus_perception_binding_id),
  UNIQUE KEY daedalus_perception_binding_uk (content_revision_key, owner_symbol, owner_kind, perception_kind, function_symbol),
  KEY ix_daedalus_perception_binding_kind (content_revision_key, perception_kind),
  KEY ix_daedalus_perception_binding_function (content_revision_key, function_symbol),
  CONSTRAINT daedalus_perception_binding_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT daedalus_perception_binding_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT daedalus_perception_binding_owner_kind_ck CHECK (owner_kind IN ('npc', 'guild', 'global', 'other')),
  CONSTRAINT daedalus_perception_binding_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS dialog_outputs (
  dialog_output_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL,
  output_name VARCHAR(191) NOT NULL,
  text_value TEXT NULL,
  audio_ref VARCHAR(255) NOT NULL DEFAULT '',
  speaker_symbol VARCHAR(191) NOT NULL DEFAULT '',
  target_symbol VARCHAR(191) NOT NULL DEFAULT '',
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (dialog_output_id),
  UNIQUE KEY dialog_output_uk (content_revision_key, output_name),
  KEY ix_dialog_output_import (content_build_import_id),
  KEY ix_dialog_output_speaker (content_revision_key, speaker_symbol),
  CONSTRAINT dialog_output_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT dialog_output_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT dialog_output_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS dialog_infos (
  dialog_info_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_build_import_id BINARY(16) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL,
  info_symbol VARCHAR(191) NOT NULL,
  npc_instance VARCHAR(191) NOT NULL DEFAULT '',
  condition_symbol VARCHAR(191) NOT NULL DEFAULT '',
  information_symbol VARCHAR(191) NOT NULL DEFAULT '',
  permanent_flag TINYINT(1) NOT NULL DEFAULT 0,
  important_flag TINYINT(1) NOT NULL DEFAULT 0,
  trade_flag TINYINT(1) NOT NULL DEFAULT 0,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (dialog_info_id),
  UNIQUE KEY dialog_info_uk (content_revision_key, info_symbol),
  KEY ix_dialog_info_npc (content_revision_key, npc_instance),
  KEY ix_dialog_info_condition (content_revision_key, condition_symbol),
  CONSTRAINT dialog_info_import_fk
    FOREIGN KEY (content_build_import_id) REFERENCES content_build_imports(content_build_import_id) ON DELETE CASCADE,
  CONSTRAINT dialog_info_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT dialog_info_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS content_build_runtime_exports (
  runtime_export_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  content_revision_key VARCHAR(191) NOT NULL,
  export_kind VARCHAR(64) NOT NULL,
  target_runtime_schema VARCHAR(191) NOT NULL DEFAULT '',
  export_status VARCHAR(32) NOT NULL DEFAULT 'planned',
  payload_sha256 CHAR(64) NULL,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (runtime_export_id),
  UNIQUE KEY content_build_runtime_export_uk (content_revision_key, export_kind, target_runtime_schema),
  KEY ix_content_build_runtime_export_status (content_revision_key, export_status),
  CONSTRAINT content_build_runtime_export_revision_fk
    FOREIGN KEY (content_revision_key) REFERENCES content_build_revisions(content_revision_key) ON DELETE CASCADE,
  CONSTRAINT content_build_runtime_export_status_ck CHECK (export_status IN ('planned', 'generated', 'validated', 'published', 'failed', 'retired')),
  CONSTRAINT content_build_runtime_export_sha_ck CHECK (payload_sha256 IS NULL OR REGEXP_LIKE(payload_sha256, '^[0-9a-f]{64}$')),
  CONSTRAINT content_build_runtime_export_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP VIEW IF EXISTS v_content_build_health;
DROP VIEW IF EXISTS v_content_build_imports;
DROP VIEW IF EXISTS v_world_zen_entities;
DROP VIEW IF EXISTS v_world_waypoint_edges;
DROP VIEW IF EXISTS v_daedalus_symbols;
DROP VIEW IF EXISTS v_daedalus_npc_templates;
DROP VIEW IF EXISTS v_daedalus_item_templates;
DROP VIEW IF EXISTS v_daedalus_routines;
DROP VIEW IF EXISTS v_daedalus_perception_bindings;
DROP VIEW IF EXISTS v_dialog_outputs;
DROP VIEW IF EXISTS v_dialog_infos;

CREATE VIEW v_content_build_imports AS
SELECT
  BIN_TO_UUID(i.content_build_import_id, 1) AS content_build_import_uuid,
  r.game_code,
  r.content_revision_key,
  r.build_status,
  i.importer_key,
  i.source_kind,
  i.source_logical_path,
  i.source_sha256,
  i.import_status,
  i.item_count,
  i.raw_payload,
  i.imported_at,
  i.created_at,
  i.updated_at
FROM content_build_imports i
JOIN content_build_revisions r ON r.content_revision_key = i.content_revision_key;

CREATE VIEW v_world_zen_entities AS
SELECT
  BIN_TO_UUID(e.zen_entity_id, 1) AS zen_entity_uuid,
  BIN_TO_UUID(e.content_build_import_id, 1) AS content_build_import_uuid,
  r.game_code,
  e.content_revision_key,
  r.build_status,
  e.world_name,
  e.entity_kind,
  e.entity_key,
  e.entity_name,
  e.pos_x,
  e.pos_y,
  e.pos_z,
  e.dir_x,
  e.dir_y,
  e.dir_z,
  e.radius_value,
  e.raw_payload
FROM world_zen_entities e
JOIN content_build_revisions r ON r.content_revision_key = e.content_revision_key;

CREATE VIEW v_world_waypoint_edges AS
SELECT
  BIN_TO_UUID(edge.waypoint_edge_id, 1) AS waypoint_edge_uuid,
  BIN_TO_UUID(edge.content_build_import_id, 1) AS content_build_import_uuid,
  r.game_code,
  edge.content_revision_key,
  r.build_status,
  edge.world_name,
  edge.from_waypoint_key,
  edge.to_waypoint_key,
  edge.travel_cost,
  edge.edge_flags,
  edge.raw_payload
FROM world_waypoint_edges edge
JOIN content_build_revisions r ON r.content_revision_key = edge.content_revision_key;

CREATE VIEW v_daedalus_symbols AS
SELECT
  BIN_TO_UUID(s.daedalus_symbol_id, 1) AS daedalus_symbol_uuid,
  BIN_TO_UUID(s.content_build_import_id, 1) AS content_build_import_uuid,
  r.game_code,
  s.content_revision_key,
  r.build_status,
  s.symbol_name,
  s.symbol_kind,
  s.data_type,
  s.parent_symbol,
  s.ordinal,
  s.raw_payload
FROM daedalus_symbols s
JOIN content_build_revisions r ON r.content_revision_key = s.content_revision_key;

CREATE VIEW v_daedalus_npc_templates AS
SELECT
  BIN_TO_UUID(n.daedalus_npc_template_id, 1) AS daedalus_npc_template_uuid,
  BIN_TO_UUID(n.content_build_import_id, 1) AS content_build_import_uuid,
  r.game_code,
  n.content_revision_key,
  r.build_status,
  n.npc_instance,
  n.display_name,
  n.guild,
  n.level_value,
  n.routine_symbol,
  n.perception_symbol,
  n.fight_tactic,
  n.voice_symbol,
  n.attributes_payload,
  n.raw_payload
FROM daedalus_npc_templates n
JOIN content_build_revisions r ON r.content_revision_key = n.content_revision_key;

CREATE VIEW v_daedalus_item_templates AS
SELECT
  BIN_TO_UUID(it.daedalus_item_template_id, 1) AS daedalus_item_template_uuid,
  BIN_TO_UUID(it.content_build_import_id, 1) AS content_build_import_uuid,
  r.game_code,
  it.content_revision_key,
  r.build_status,
  it.item_instance,
  it.display_name,
  it.item_category,
  it.main_flag,
  it.flags_value,
  it.value_amount,
  it.damage_total,
  it.raw_payload
FROM daedalus_item_templates it
JOIN content_build_revisions r ON r.content_revision_key = it.content_revision_key;

CREATE VIEW v_daedalus_routines AS
SELECT
  BIN_TO_UUID(rt.daedalus_routine_id, 1) AS daedalus_routine_uuid,
  BIN_TO_UUID(rt.content_build_import_id, 1) AS content_build_import_uuid,
  r.game_code,
  rt.content_revision_key,
  r.build_status,
  rt.npc_instance,
  rt.routine_symbol,
  rt.day_minute_start,
  rt.day_minute_end,
  rt.target_point_key,
  rt.action_symbol,
  rt.raw_payload
FROM daedalus_routines rt
JOIN content_build_revisions r ON r.content_revision_key = rt.content_revision_key;

CREATE VIEW v_daedalus_perception_bindings AS
SELECT
  BIN_TO_UUID(p.daedalus_perception_binding_id, 1) AS daedalus_perception_binding_uuid,
  BIN_TO_UUID(p.content_build_import_id, 1) AS content_build_import_uuid,
  r.game_code,
  p.content_revision_key,
  r.build_status,
  p.owner_symbol,
  p.owner_kind,
  p.perception_kind,
  p.function_symbol,
  p.priority_value,
  p.raw_payload
FROM daedalus_perception_bindings p
JOIN content_build_revisions r ON r.content_revision_key = p.content_revision_key;

CREATE VIEW v_dialog_outputs AS
SELECT
  BIN_TO_UUID(o.dialog_output_id, 1) AS dialog_output_uuid,
  BIN_TO_UUID(o.content_build_import_id, 1) AS content_build_import_uuid,
  r.game_code,
  o.content_revision_key,
  r.build_status,
  o.output_name,
  o.text_value,
  o.audio_ref,
  o.speaker_symbol,
  o.target_symbol,
  o.raw_payload
FROM dialog_outputs o
JOIN content_build_revisions r ON r.content_revision_key = o.content_revision_key;

CREATE VIEW v_dialog_infos AS
SELECT
  BIN_TO_UUID(i.dialog_info_id, 1) AS dialog_info_uuid,
  BIN_TO_UUID(i.content_build_import_id, 1) AS content_build_import_uuid,
  r.game_code,
  i.content_revision_key,
  r.build_status,
  i.info_symbol,
  i.npc_instance,
  i.condition_symbol,
  i.information_symbol,
  i.permanent_flag,
  i.important_flag,
  i.trade_flag,
  i.raw_payload
FROM dialog_infos i
JOIN content_build_revisions r ON r.content_revision_key = i.content_revision_key;

CREATE VIEW v_content_build_health AS
SELECT
  r.game_code,
  r.content_revision_key,
  r.build_status,
  r.manifest_hash,
  COUNT(DISTINCT bi.content_build_import_id) AS build_import_count,
  SUM(CASE WHEN bi.import_status = 'imported' THEN 1 ELSE 0 END) AS imported_count,
  SUM(CASE WHEN bi.import_status = 'failed' THEN 1 ELSE 0 END) AS failed_count,
  (SELECT COUNT(*) FROM content_build_files f WHERE f.content_revision_key = r.content_revision_key) AS file_count,
  (SELECT COUNT(*) FROM content_build_files f WHERE f.content_revision_key = r.content_revision_key AND f.required_for_server_authority = 1) AS required_file_count,
  (SELECT COUNT(*) FROM content_build_parser_errors pe WHERE pe.content_revision_key = r.content_revision_key AND pe.severity IN ('error', 'fatal')) AS blocking_error_count,
  (SELECT COUNT(*) FROM world_zen_entities z WHERE z.content_revision_key = r.content_revision_key) AS zen_entity_count,
  (SELECT COUNT(*) FROM world_zen_entities z WHERE z.content_revision_key = r.content_revision_key AND z.entity_kind = 'waypoint') AS waypoint_count,
  (SELECT COUNT(*) FROM world_zen_entities z WHERE z.content_revision_key = r.content_revision_key AND z.entity_kind = 'freepoint') AS freepoint_count,
  (SELECT COUNT(*) FROM world_zen_entities z WHERE z.content_revision_key = r.content_revision_key AND z.entity_kind = 'vob') AS vob_count,
  (SELECT COUNT(*) FROM world_waypoint_edges edge WHERE edge.content_revision_key = r.content_revision_key) AS waypoint_edge_count,
  (SELECT COUNT(*) FROM daedalus_symbols s WHERE s.content_revision_key = r.content_revision_key) AS daedalus_symbol_count,
  (SELECT COUNT(*) FROM daedalus_npc_templates n WHERE n.content_revision_key = r.content_revision_key) AS npc_template_count,
  (SELECT COUNT(*) FROM daedalus_item_templates it WHERE it.content_revision_key = r.content_revision_key) AS item_template_count,
  (SELECT COUNT(*) FROM daedalus_routines rt WHERE rt.content_revision_key = r.content_revision_key) AS routine_count,
  (SELECT COUNT(*) FROM daedalus_perception_bindings p WHERE p.content_revision_key = r.content_revision_key) AS perception_binding_count,
  (SELECT COUNT(*) FROM dialog_outputs o WHERE o.content_revision_key = r.content_revision_key) AS dialog_output_count,
  (SELECT COUNT(*) FROM dialog_infos i WHERE i.content_revision_key = r.content_revision_key) AS dialog_info_count,
  MAX(bi.updated_at) AS last_build_update_at
FROM content_build_revisions r
LEFT JOIN content_build_imports bi ON bi.content_revision_key = r.content_revision_key
GROUP BY r.game_code, r.content_revision_key, r.build_status, r.manifest_hash;

INSERT INTO content_build_schema_versions(migration_key, checksum, description)
VALUES (
  'server/sql/step211_content_build_database.sql',
  SHA2('server/sql/step211_content_build_database.sql', 256),
  'Step211: separate mmo_content_build database for server-owned ZEN/DAT/OU parser outputs'
)
ON DUPLICATE KEY UPDATE
  checksum = VALUES(checksum),
  description = VALUES(description),
  applied_at = CURRENT_TIMESTAMP(6);

-- ============================================================================
-- END server/sql/step211_content_build_database.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step212_ai_runtime_perception_database.sql
-- ============================================================================

-- Step212: separate AI runtime database for authoritative NPC perception.
--
-- mmo_content_build stores static parser output from ZEN/DAT/OU. The runtime
-- MMO database stores players, sessions and durable gameplay state. This schema
-- stores server-side AI decisions, perception cooldowns and queued presentation
-- actions. It intentionally uses stable natural keys/UUID text instead of
-- cross-database foreign keys so it can be rebuilt or split independently.

CREATE DATABASE IF NOT EXISTS mmo_ai_runtime
  DEFAULT CHARACTER SET utf8mb4
  DEFAULT COLLATE utf8mb4_0900_ai_ci;

USE mmo_ai_runtime;

CREATE TABLE IF NOT EXISTS ai_runtime_schema_versions (
  migration_key VARCHAR(255) NOT NULL,
  checksum CHAR(64) NOT NULL,
  description TEXT NOT NULL,
  applied_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (migration_key),
  CONSTRAINT ai_runtime_schema_versions_checksum_ck CHECK (REGEXP_LIKE(checksum, '^[0-9a-f]{64}$'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS npc_perception_rule_catalog (
  rule_key VARCHAR(191) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL DEFAULT '',
  perception_kind VARCHAR(64) NOT NULL,
  action_kind VARCHAR(64) NOT NULL,
  priority_value INT NOT NULL DEFAULT 100,
  max_distance DOUBLE NULL,
  cooldown_ticks BIGINT NOT NULL DEFAULT 0,
  requires_los TINYINT(1) NOT NULL DEFAULT 0,
  enabled TINYINT(1) NOT NULL DEFAULT 1,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (rule_key),
  KEY ix_npc_perception_rule_content (content_revision_key, perception_kind, enabled),
  KEY ix_npc_perception_rule_action (action_kind, priority_value),
  CONSTRAINT npc_perception_rule_action_ck CHECK (action_kind IN (
    'npc_assess_player', 'npc_turn_to_player', 'npc_approach_player',
    'npc_greet_player', 'npc_warn_player', 'npc_start_dialog',
    'npc_attack_player', 'npc_ignore_player', 'npc_noop'
  )),
  CONSTRAINT npc_perception_rule_cooldown_ck CHECK (cooldown_ticks >= 0),
  CONSTRAINT npc_perception_rule_distance_ck CHECK (max_distance IS NULL OR max_distance >= 0),
  CONSTRAINT npc_perception_rule_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS npc_perception_cooldowns (
  world_instance_uuid CHAR(36) NOT NULL,
  npc_entity_key VARCHAR(191) NOT NULL,
  target_key VARCHAR(191) NOT NULL,
  perception_kind VARCHAR(64) NOT NULL,
  cooldown_until_tick BIGINT NOT NULL DEFAULT 0,
  last_decision_id BINARY(16) NULL,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (world_instance_uuid, npc_entity_key, target_key, perception_kind),
  KEY ix_npc_perception_cooldowns_tick (world_instance_uuid, cooldown_until_tick),
  KEY ix_npc_perception_cooldowns_decision (last_decision_id),
  CONSTRAINT npc_perception_cooldowns_tick_ck CHECK (cooldown_until_tick >= 0),
  CONSTRAINT npc_perception_cooldowns_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS npc_perception_decisions (
  decision_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  world_instance_uuid CHAR(36) NOT NULL,
  session_uuid CHAR(36) NOT NULL DEFAULT '',
  character_uuid CHAR(36) NOT NULL DEFAULT '',
  character_key VARCHAR(191) NOT NULL DEFAULT '',
  npc_entity_key VARCHAR(191) NOT NULL,
  target_key VARCHAR(191) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL DEFAULT '',
  rule_key VARCHAR(191) NOT NULL DEFAULT '',
  perception_kind VARCHAR(64) NOT NULL,
  requested_decision_kind VARCHAR(64) NOT NULL,
  decision_kind VARCHAR(64) NOT NULL,
  decision_status VARCHAR(32) NOT NULL,
  priority_value INT NOT NULL DEFAULT 100,
  server_tick BIGINT NOT NULL DEFAULT 0,
  cooldown_until_tick BIGINT NOT NULL DEFAULT 0,
  enqueue_action TINYINT(1) NOT NULL DEFAULT 0,
  action_queue_id BINARY(16) NULL,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  idempotency_key VARCHAR(191) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (decision_id),
  UNIQUE KEY npc_perception_decision_idempotency_uk (idempotency_key),
  KEY ix_npc_perception_decisions_world_tick (world_instance_uuid, server_tick, decision_status),
  KEY ix_npc_perception_decisions_npc (world_instance_uuid, npc_entity_key, server_tick),
  KEY ix_npc_perception_decisions_target (world_instance_uuid, target_key, server_tick),
  KEY ix_npc_perception_decisions_status (decision_status, created_at),
  KEY ix_npc_perception_decisions_action_queue (action_queue_id),
  CONSTRAINT npc_perception_decisions_requested_ck CHECK (requested_decision_kind IN (
    'assess_player', 'turn_to_player', 'approach_player', 'greet_player',
    'warn_player', 'start_dialog', 'attack_player', 'ignore_player',
    'cooldown_skip', 'blocked', 'noop'
  )),
  CONSTRAINT npc_perception_decisions_kind_ck CHECK (decision_kind IN (
    'assess_player', 'turn_to_player', 'approach_player', 'greet_player',
    'warn_player', 'start_dialog', 'attack_player', 'ignore_player',
    'cooldown_skip', 'blocked', 'noop'
  )),
  CONSTRAINT npc_perception_decisions_status_ck CHECK (decision_status IN ('accepted', 'queued', 'cooldown', 'blocked', 'noop')),
  CONSTRAINT npc_perception_decisions_tick_ck CHECK (server_tick >= 0 AND cooldown_until_tick >= 0),
  CONSTRAINT npc_perception_decisions_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS npc_perception_action_queue (
  action_queue_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  decision_id BINARY(16) NOT NULL,
  world_instance_uuid CHAR(36) NOT NULL,
  session_uuid CHAR(36) NOT NULL DEFAULT '',
  character_uuid CHAR(36) NOT NULL DEFAULT '',
  action_kind VARCHAR(128) NOT NULL,
  target_key VARCHAR(191) NOT NULL DEFAULT '',
  action_status VARCHAR(32) NOT NULL DEFAULT 'pending',
  priority_value INT NOT NULL DEFAULT 100,
  request_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  idempotency_key VARCHAR(191) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (action_queue_id),
  UNIQUE KEY npc_perception_action_idempotency_uk (idempotency_key),
  KEY ix_npc_perception_action_decision (decision_id),
  KEY ix_npc_perception_action_claim (action_status, priority_value, created_at),
  KEY ix_npc_perception_action_world (world_instance_uuid, action_status, created_at),
  CONSTRAINT npc_perception_action_decision_fk
    FOREIGN KEY (decision_id) REFERENCES npc_perception_decisions(decision_id) ON DELETE CASCADE,
  CONSTRAINT npc_perception_action_status_ck CHECK (action_status IN ('pending', 'claimed', 'applied', 'failed', 'skipped')),
  CONSTRAINT npc_perception_action_payload_json_ck CHECK (JSON_VALID(request_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP VIEW IF EXISTS v_npc_perception_decisions;
CREATE VIEW v_npc_perception_decisions AS
SELECT
  BIN_TO_UUID(d.decision_id, 1) AS decision_uuid,
  d.world_instance_uuid,
  d.session_uuid,
  d.character_uuid,
  d.character_key,
  d.npc_entity_key,
  d.target_key,
  d.content_revision_key,
  d.rule_key,
  d.perception_kind,
  d.requested_decision_kind,
  d.decision_kind,
  d.decision_status,
  d.priority_value,
  d.server_tick,
  d.cooldown_until_tick,
  d.enqueue_action,
  BIN_TO_UUID(d.action_queue_id, 1) AS action_queue_uuid,
  d.idempotency_key,
  d.raw_payload,
  d.created_at
FROM npc_perception_decisions d;

DROP VIEW IF EXISTS v_npc_perception_cooldowns;
CREATE VIEW v_npc_perception_cooldowns AS
SELECT
  c.world_instance_uuid,
  c.npc_entity_key,
  c.target_key,
  c.perception_kind,
  c.cooldown_until_tick,
  BIN_TO_UUID(c.last_decision_id, 1) AS last_decision_uuid,
  c.raw_payload,
  c.created_at,
  c.updated_at
FROM npc_perception_cooldowns c;

DROP VIEW IF EXISTS v_npc_perception_action_queue;
CREATE VIEW v_npc_perception_action_queue AS
SELECT
  BIN_TO_UUID(q.action_queue_id, 1) AS action_queue_uuid,
  BIN_TO_UUID(q.decision_id, 1) AS decision_uuid,
  q.world_instance_uuid,
  q.session_uuid,
  q.character_uuid,
  q.action_kind,
  q.target_key,
  q.action_status,
  q.priority_value,
  q.idempotency_key,
  q.request_payload,
  q.created_at,
  q.updated_at
FROM npc_perception_action_queue q;

DROP VIEW IF EXISTS v_npc_perception_policy_health;
CREATE VIEW v_npc_perception_policy_health AS
SELECT
  'global' AS health_scope,
  (SELECT COUNT(*) FROM npc_perception_rule_catalog WHERE enabled = 1) AS enabled_rule_count,
  (SELECT COUNT(*) FROM npc_perception_rule_catalog WHERE enabled = 0) AS disabled_rule_count,
  (SELECT COUNT(*) FROM npc_perception_cooldowns) AS cooldown_count,
  (SELECT COUNT(*) FROM npc_perception_decisions) AS decision_count,
  (SELECT COUNT(*) FROM npc_perception_decisions WHERE decision_status = 'queued') AS queued_decision_count,
  (SELECT COUNT(*) FROM npc_perception_decisions WHERE decision_status = 'cooldown') AS cooldown_skip_count,
  (SELECT COUNT(*) FROM npc_perception_action_queue WHERE action_status = 'pending') AS pending_action_count,
  (SELECT COUNT(*) FROM npc_perception_action_queue WHERE action_status = 'failed') AS failed_action_count;

DROP PROCEDURE IF EXISTS mmo_ai_record_npc_perception_decision;

DELIMITER $$
CREATE PROCEDURE mmo_ai_record_npc_perception_decision(
  IN p_world_instance_uuid CHAR(36),
  IN p_session_uuid CHAR(36),
  IN p_character_uuid CHAR(36),
  IN p_character_key VARCHAR(191),
  IN p_npc_entity_key VARCHAR(191),
  IN p_target_key VARCHAR(191),
  IN p_content_revision_key VARCHAR(191),
  IN p_rule_key VARCHAR(191),
  IN p_perception_kind VARCHAR(64),
  IN p_decision_kind VARCHAR(64),
  IN p_priority_value INT,
  IN p_server_tick BIGINT,
  IN p_cooldown_ticks BIGINT,
  IN p_enqueue_action TINYINT(1),
  IN p_action_payload JSON,
  IN p_idempotency_key VARCHAR(191),
  OUT o_decision_id BINARY(16),
  OUT o_decision_status VARCHAR(32),
  OUT o_action_queue_id BINARY(16)
)
main: BEGIN
  DECLARE v_existing_count INT DEFAULT 0;
  DECLARE v_server_tick BIGINT DEFAULT 0;
  DECLARE v_cooldown_ticks BIGINT DEFAULT 0;
  DECLARE v_existing_cooldown_until BIGINT DEFAULT 0;
  DECLARE v_new_cooldown_until BIGINT DEFAULT 0;
  DECLARE v_requested_decision_kind VARCHAR(64) DEFAULT 'noop';
  DECLARE v_final_decision_kind VARCHAR(64) DEFAULT 'noop';
  DECLARE v_final_status VARCHAR(32) DEFAULT 'noop';
  DECLARE v_enqueue TINYINT(1) DEFAULT 0;
  DECLARE v_payload JSON;

  SET o_decision_id = NULL;
  SET o_decision_status = 'blocked';
  SET o_action_queue_id = NULL;

  SET v_server_tick = GREATEST(COALESCE(p_server_tick, 0), 0);
  SET v_cooldown_ticks = GREATEST(COALESCE(p_cooldown_ticks, 0), 0);
  SET v_requested_decision_kind = COALESCE(NULLIF(p_decision_kind, ''), 'noop');
  SET v_final_decision_kind = v_requested_decision_kind;
  SET v_enqueue = IF(COALESCE(p_enqueue_action, 0) = 1, 1, 0);
  SET v_payload = COALESCE(p_action_payload, JSON_OBJECT());

  IF COALESCE(p_idempotency_key, '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'p_idempotency_key is required';
  END IF;
  IF COALESCE(p_world_instance_uuid, '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'p_world_instance_uuid is required';
  END IF;
  IF COALESCE(p_npc_entity_key, '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'p_npc_entity_key is required';
  END IF;
  IF COALESCE(p_target_key, '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'p_target_key is required';
  END IF;
  IF COALESCE(p_perception_kind, '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'p_perception_kind is required';
  END IF;

  SELECT COUNT(*) INTO v_existing_count
  FROM npc_perception_decisions
  WHERE idempotency_key = p_idempotency_key;

  IF v_existing_count > 0 THEN
    SELECT decision_id, decision_status, action_queue_id
      INTO o_decision_id, o_decision_status, o_action_queue_id
    FROM npc_perception_decisions
    WHERE idempotency_key = p_idempotency_key
    LIMIT 1;
    LEAVE main;
  END IF;

  SELECT COALESCE(MAX(cooldown_until_tick), 0) INTO v_existing_cooldown_until
  FROM npc_perception_cooldowns
  WHERE world_instance_uuid = p_world_instance_uuid
    AND npc_entity_key = p_npc_entity_key
    AND target_key = p_target_key
    AND perception_kind = p_perception_kind;

  IF v_existing_cooldown_until > v_server_tick THEN
    SET v_final_status = 'cooldown';
    SET v_final_decision_kind = 'cooldown_skip';
    SET v_enqueue = 0;
    SET v_new_cooldown_until = v_existing_cooldown_until;
  ELSEIF v_requested_decision_kind = 'blocked' THEN
    SET v_final_status = 'blocked';
    SET v_new_cooldown_until = v_server_tick + v_cooldown_ticks;
  ELSEIF v_requested_decision_kind = 'noop' OR v_requested_decision_kind = 'ignore_player' THEN
    SET v_final_status = 'noop';
    SET v_new_cooldown_until = v_server_tick + v_cooldown_ticks;
  ELSEIF v_enqueue = 1 THEN
    SET v_final_status = 'queued';
    SET v_new_cooldown_until = v_server_tick + v_cooldown_ticks;
  ELSE
    SET v_final_status = 'accepted';
    SET v_new_cooldown_until = v_server_tick + v_cooldown_ticks;
  END IF;

  SET o_decision_id = UUID_TO_BIN(UUID(), 1);
  INSERT INTO npc_perception_decisions (
    decision_id, world_instance_uuid, session_uuid, character_uuid, character_key,
    npc_entity_key, target_key, content_revision_key, rule_key, perception_kind,
    requested_decision_kind, decision_kind, decision_status, priority_value,
    server_tick, cooldown_until_tick, enqueue_action, raw_payload, idempotency_key
  ) VALUES (
    o_decision_id, p_world_instance_uuid, COALESCE(p_session_uuid, ''), COALESCE(p_character_uuid, ''),
    COALESCE(p_character_key, ''), p_npc_entity_key, p_target_key,
    COALESCE(p_content_revision_key, ''), COALESCE(p_rule_key, ''), p_perception_kind,
    v_requested_decision_kind, v_final_decision_kind, v_final_status,
    COALESCE(p_priority_value, 100), v_server_tick, v_new_cooldown_until,
    v_enqueue, v_payload, p_idempotency_key
  );

  IF v_new_cooldown_until > 0 THEN
    INSERT INTO npc_perception_cooldowns (
      world_instance_uuid, npc_entity_key, target_key, perception_kind,
      cooldown_until_tick, last_decision_id, raw_payload
    ) VALUES (
      p_world_instance_uuid, p_npc_entity_key, p_target_key, p_perception_kind,
      v_new_cooldown_until, o_decision_id,
      JSON_OBJECT('decision_status', v_final_status, 'decision_kind', v_final_decision_kind)
    )
    ON DUPLICATE KEY UPDATE
      cooldown_until_tick = VALUES(cooldown_until_tick),
      last_decision_id = VALUES(last_decision_id),
      raw_payload = VALUES(raw_payload),
      updated_at = CURRENT_TIMESTAMP(6);
  END IF;

  IF v_enqueue = 1 AND v_final_status = 'queued' THEN
    SET o_action_queue_id = UUID_TO_BIN(UUID(), 1);
    INSERT INTO npc_perception_action_queue (
      action_queue_id, decision_id, world_instance_uuid, session_uuid, character_uuid,
      action_kind, target_key, action_status, priority_value, request_payload, idempotency_key
    ) VALUES (
      o_action_queue_id, o_decision_id, p_world_instance_uuid,
      COALESCE(p_session_uuid, ''), COALESCE(p_character_uuid, ''),
      CONCAT('npc_', v_final_decision_kind), p_target_key, 'pending',
      COALESCE(p_priority_value, 100),
      JSON_MERGE_PATCH(
        v_payload,
        JSON_OBJECT(
          'decision_uuid', BIN_TO_UUID(o_decision_id, 1),
          'npc_entity_key', p_npc_entity_key,
          'perception_kind', p_perception_kind
        )
      ),
      CONCAT(p_idempotency_key, ':action')
    );

    UPDATE npc_perception_decisions
    SET action_queue_id = o_action_queue_id
    WHERE decision_id = o_decision_id;
  END IF;

  SET o_decision_status = v_final_status;
END$$
DELIMITER ;

INSERT INTO ai_runtime_schema_versions (migration_key, checksum, description)
VALUES (
  'server/sql/step212_ai_runtime_perception_database.sql',
  SHA2('server/sql/step212_ai_runtime_perception_database.sql', 256),
  'Step212 separate mmo_ai_runtime schema for authoritative NPC perception decisions'
)
ON DUPLICATE KEY UPDATE
  checksum = VALUES(checksum),
  description = VALUES(description),
  applied_at = CURRENT_TIMESTAMP(6);

-- ============================================================================
-- END server/sql/step212_ai_runtime_perception_database.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step213_ai_runtime_action_dispatch_contracts.sql
-- ============================================================================

-- Step213: dispatch contracts for AI runtime NPC perception actions.
--
-- Step212 created the queue where authoritative NPC perception decisions can
-- enqueue effects such as turn, approach, greet, warn, dialog or attack. This
-- step makes that queue consumable by a future C++ server tick/worker without
-- routing through the generic runtime outbox.

USE mmo_ai_runtime;

DROP PROCEDURE IF EXISTS mmo_ai_add_action_queue_column_if_missing;

DELIMITER $$
CREATE PROCEDURE mmo_ai_add_action_queue_column_if_missing(
  IN p_column_name VARCHAR(64),
  IN p_column_ddl TEXT
)
BEGIN
  DECLARE v_column_count INT DEFAULT 0;

  SELECT COUNT(*) INTO v_column_count
    FROM information_schema.columns
   WHERE table_schema = DATABASE()
     AND table_name = 'npc_perception_action_queue'
     AND column_name = p_column_name;

  IF v_column_count = 0 THEN
    SET @mmo_ai_alter_sql = CONCAT('ALTER TABLE npc_perception_action_queue ADD COLUMN ', p_column_ddl);
    PREPARE mmo_ai_alter_stmt FROM @mmo_ai_alter_sql;
    EXECUTE mmo_ai_alter_stmt;
    DEALLOCATE PREPARE mmo_ai_alter_stmt;
    SET @mmo_ai_alter_sql = NULL;
  END IF;
END$$
DELIMITER ;

CALL mmo_ai_add_action_queue_column_if_missing('max_attempts', 'max_attempts INT NOT NULL DEFAULT 5 AFTER priority_value');
CALL mmo_ai_add_action_queue_column_if_missing('attempt_count', 'attempt_count INT NOT NULL DEFAULT 0 AFTER max_attempts');
CALL mmo_ai_add_action_queue_column_if_missing('worker_id', 'worker_id VARCHAR(191) NOT NULL DEFAULT '''' AFTER attempt_count');
CALL mmo_ai_add_action_queue_column_if_missing('locked_at', 'locked_at TIMESTAMP(6) NULL DEFAULT NULL AFTER updated_at');
CALL mmo_ai_add_action_queue_column_if_missing('applied_at', 'applied_at TIMESTAMP(6) NULL DEFAULT NULL AFTER locked_at');
CALL mmo_ai_add_action_queue_column_if_missing('failed_at', 'failed_at TIMESTAMP(6) NULL DEFAULT NULL AFTER applied_at');
CALL mmo_ai_add_action_queue_column_if_missing('completed_at', 'completed_at TIMESTAMP(6) NULL DEFAULT NULL AFTER failed_at');
CALL mmo_ai_add_action_queue_column_if_missing('next_attempt_at', 'next_attempt_at TIMESTAMP(6) NULL DEFAULT NULL AFTER completed_at');
CALL mmo_ai_add_action_queue_column_if_missing('last_error_code', 'last_error_code VARCHAR(64) NOT NULL DEFAULT '''' AFTER next_attempt_at');
CALL mmo_ai_add_action_queue_column_if_missing('last_error_message', 'last_error_message TEXT NULL AFTER last_error_code');
CALL mmo_ai_add_action_queue_column_if_missing('result_payload', 'result_payload JSON NOT NULL DEFAULT (JSON_OBJECT()) AFTER request_payload');

DROP PROCEDURE IF EXISTS mmo_ai_add_action_queue_column_if_missing;

CREATE TABLE IF NOT EXISTS npc_perception_action_dispatch_log (
  dispatch_log_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  action_queue_id BINARY(16) NOT NULL,
  decision_id BINARY(16) NOT NULL,
  worker_id VARCHAR(191) NOT NULL DEFAULT '',
  dispatch_event VARCHAR(32) NOT NULL,
  action_status VARCHAR(32) NOT NULL,
  attempt_count INT NOT NULL DEFAULT 0,
  error_code VARCHAR(64) NOT NULL DEFAULT '',
  message_text TEXT NULL,
  payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (dispatch_log_id),
  KEY ix_npc_perception_dispatch_action (action_queue_id, created_at),
  KEY ix_npc_perception_dispatch_decision (decision_id, created_at),
  KEY ix_npc_perception_dispatch_worker (worker_id, created_at),
  KEY ix_npc_perception_dispatch_event (dispatch_event, created_at),
  CONSTRAINT npc_perception_dispatch_action_fk
    FOREIGN KEY (action_queue_id) REFERENCES npc_perception_action_queue(action_queue_id) ON DELETE CASCADE,
  CONSTRAINT npc_perception_dispatch_decision_fk
    FOREIGN KEY (decision_id) REFERENCES npc_perception_decisions(decision_id) ON DELETE CASCADE,
  CONSTRAINT npc_perception_dispatch_event_ck CHECK (dispatch_event IN ('claimed', 'applied', 'failed', 'retry_scheduled', 'skipped')),
  CONSTRAINT npc_perception_dispatch_payload_json_ck CHECK (JSON_VALID(payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP VIEW IF EXISTS v_npc_perception_pending_actions;
CREATE VIEW v_npc_perception_pending_actions AS
SELECT
  BIN_TO_UUID(q.action_queue_id, 1) AS action_queue_uuid,
  BIN_TO_UUID(q.decision_id, 1) AS decision_uuid,
  q.world_instance_uuid,
  q.session_uuid,
  q.character_uuid,
  q.action_kind,
  q.target_key,
  q.action_status,
  q.priority_value,
  q.max_attempts,
  q.attempt_count,
  q.worker_id,
  q.idempotency_key,
  q.request_payload,
  q.result_payload,
  q.created_at,
  q.updated_at,
  q.locked_at,
  q.next_attempt_at
FROM npc_perception_action_queue q
WHERE q.action_status = 'pending'
  AND (q.next_attempt_at IS NULL OR q.next_attempt_at <= CURRENT_TIMESTAMP(6));

DROP VIEW IF EXISTS v_npc_perception_action_dispatch_log;
CREATE VIEW v_npc_perception_action_dispatch_log AS
SELECT
  BIN_TO_UUID(l.dispatch_log_id, 1) AS dispatch_log_uuid,
  BIN_TO_UUID(l.action_queue_id, 1) AS action_queue_uuid,
  BIN_TO_UUID(l.decision_id, 1) AS decision_uuid,
  l.worker_id,
  l.dispatch_event,
  l.action_status,
  l.attempt_count,
  l.error_code,
  l.message_text,
  l.payload,
  l.created_at
FROM npc_perception_action_dispatch_log l;

DROP VIEW IF EXISTS v_npc_perception_action_dispatch_health;
CREATE VIEW v_npc_perception_action_dispatch_health AS
SELECT
  'global' AS health_scope,
  (SELECT COUNT(*) FROM npc_perception_action_queue WHERE action_status = 'pending') AS pending_count,
  (SELECT COUNT(*) FROM npc_perception_action_queue WHERE action_status = 'claimed') AS claimed_count,
  (SELECT COUNT(*) FROM npc_perception_action_queue WHERE action_status = 'applied') AS applied_count,
  (SELECT COUNT(*) FROM npc_perception_action_queue WHERE action_status = 'failed') AS failed_count,
  (SELECT COUNT(*) FROM npc_perception_action_queue WHERE action_status = 'skipped') AS skipped_count,
  (SELECT COUNT(*) FROM npc_perception_action_queue WHERE action_status = 'pending' AND next_attempt_at IS NOT NULL AND next_attempt_at > CURRENT_TIMESTAMP(6)) AS delayed_retry_count,
  (SELECT COUNT(*) FROM npc_perception_action_dispatch_log) AS dispatch_log_count;

DROP PROCEDURE IF EXISTS mmo_ai_claim_next_npc_perception_action;
DROP PROCEDURE IF EXISTS mmo_ai_mark_npc_perception_action_applied;
DROP PROCEDURE IF EXISTS mmo_ai_mark_npc_perception_action_failed;
DROP PROCEDURE IF EXISTS mmo_ai_skip_npc_perception_action;

DELIMITER $$
CREATE PROCEDURE mmo_ai_claim_next_npc_perception_action(
  IN p_worker_id VARCHAR(191),
  OUT o_action_queue_id BINARY(16),
  OUT o_decision_id BINARY(16),
  OUT o_action_kind VARCHAR(128),
  OUT o_world_instance_uuid CHAR(36),
  OUT o_session_uuid CHAR(36),
  OUT o_character_uuid CHAR(36),
  OUT o_target_key VARCHAR(191),
  OUT o_idempotency_key VARCHAR(191),
  OUT o_request_payload JSON
)
BEGIN
  DECLARE v_not_found BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;

  SET o_action_queue_id = NULL;
  SET o_decision_id = NULL;
  SET o_action_kind = NULL;
  SET o_world_instance_uuid = NULL;
  SET o_session_uuid = NULL;
  SET o_character_uuid = NULL;
  SET o_target_key = NULL;
  SET o_idempotency_key = NULL;
  SET o_request_payload = NULL;

  START TRANSACTION;

  SELECT action_queue_id, decision_id, action_kind, world_instance_uuid,
         session_uuid, character_uuid, target_key, idempotency_key, request_payload
    INTO o_action_queue_id, o_decision_id, o_action_kind, o_world_instance_uuid,
         o_session_uuid, o_character_uuid, o_target_key, o_idempotency_key, o_request_payload
    FROM npc_perception_action_queue
   WHERE action_status = 'pending'
     AND attempt_count < max_attempts
     AND (next_attempt_at IS NULL OR next_attempt_at <= CURRENT_TIMESTAMP(6))
   ORDER BY priority_value ASC, created_at ASC, action_queue_id ASC
   LIMIT 1
   FOR UPDATE SKIP LOCKED;

  IF NOT v_not_found AND o_action_queue_id IS NOT NULL THEN
    UPDATE npc_perception_action_queue
       SET action_status = 'claimed',
           worker_id = COALESCE(p_worker_id, ''),
           attempt_count = attempt_count + 1,
           locked_at = CURRENT_TIMESTAMP(6),
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE action_queue_id = o_action_queue_id;

    INSERT INTO npc_perception_action_dispatch_log (
      action_queue_id, decision_id, worker_id, dispatch_event,
      action_status, attempt_count, payload
    )
    SELECT action_queue_id, decision_id, COALESCE(p_worker_id, ''), 'claimed',
           action_status, attempt_count,
           JSON_OBJECT('action_kind', action_kind, 'target_key', target_key)
      FROM npc_perception_action_queue
     WHERE action_queue_id = o_action_queue_id;
  END IF;

  COMMIT;
END$$

CREATE PROCEDURE mmo_ai_mark_npc_perception_action_applied(
  IN p_action_queue_id BINARY(16),
  IN p_worker_id VARCHAR(191),
  IN p_result_payload JSON,
  OUT o_action_status VARCHAR(32)
)
BEGIN
  DECLARE v_not_found BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;

  SET o_action_status = NULL;

  UPDATE npc_perception_action_queue
     SET action_status = 'applied',
         worker_id = COALESCE(p_worker_id, worker_id),
         applied_at = CURRENT_TIMESTAMP(6),
         completed_at = CURRENT_TIMESTAMP(6),
         updated_at = CURRENT_TIMESTAMP(6),
         result_payload = JSON_MERGE_PATCH(COALESCE(result_payload, JSON_OBJECT()), COALESCE(p_result_payload, JSON_OBJECT()))
   WHERE action_queue_id = p_action_queue_id
     AND action_status IN ('claimed', 'pending');

  SELECT action_status INTO o_action_status
    FROM npc_perception_action_queue
   WHERE action_queue_id = p_action_queue_id
   LIMIT 1;

  INSERT INTO npc_perception_action_dispatch_log (
    action_queue_id, decision_id, worker_id, dispatch_event,
    action_status, attempt_count, payload
  )
  SELECT action_queue_id, decision_id, COALESCE(p_worker_id, worker_id), 'applied',
         action_status, attempt_count, COALESCE(p_result_payload, JSON_OBJECT())
    FROM npc_perception_action_queue
   WHERE action_queue_id = p_action_queue_id;
END$$

CREATE PROCEDURE mmo_ai_mark_npc_perception_action_failed(
  IN p_action_queue_id BINARY(16),
  IN p_worker_id VARCHAR(191),
  IN p_error_code VARCHAR(64),
  IN p_error_message TEXT,
  IN p_retryable TINYINT(1),
  IN p_retry_delay_seconds INT,
  IN p_result_payload JSON,
  OUT o_action_status VARCHAR(32)
)
BEGIN
  DECLARE v_not_found BOOL DEFAULT FALSE;
  DECLARE v_attempt_count INT DEFAULT 0;
  DECLARE v_max_attempts INT DEFAULT 1;
  DECLARE v_retry TINYINT(1) DEFAULT 0;
  DECLARE v_event VARCHAR(32) DEFAULT 'failed';
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;

  SELECT attempt_count, max_attempts INTO v_attempt_count, v_max_attempts
    FROM npc_perception_action_queue
   WHERE action_queue_id = p_action_queue_id
   LIMIT 1;

  SET v_retry = IF(COALESCE(p_retryable, 0) = 1 AND v_attempt_count < v_max_attempts, 1, 0);
  SET v_event = IF(v_retry = 1, 'retry_scheduled', 'failed');

  UPDATE npc_perception_action_queue
     SET action_status = IF(v_retry = 1, 'pending', 'failed'),
         worker_id = COALESCE(p_worker_id, worker_id),
         failed_at = CURRENT_TIMESTAMP(6),
         completed_at = IF(v_retry = 1, NULL, CURRENT_TIMESTAMP(6)),
         next_attempt_at = IF(v_retry = 1, DATE_ADD(CURRENT_TIMESTAMP(6), INTERVAL GREATEST(COALESCE(p_retry_delay_seconds, 0), 0) SECOND), NULL),
         last_error_code = COALESCE(p_error_code, ''),
         last_error_message = p_error_message,
         updated_at = CURRENT_TIMESTAMP(6),
         result_payload = JSON_MERGE_PATCH(COALESCE(result_payload, JSON_OBJECT()), COALESCE(p_result_payload, JSON_OBJECT()))
   WHERE action_queue_id = p_action_queue_id
     AND action_status IN ('claimed', 'pending');

  SELECT action_status INTO o_action_status
    FROM npc_perception_action_queue
   WHERE action_queue_id = p_action_queue_id
   LIMIT 1;

  INSERT INTO npc_perception_action_dispatch_log (
    action_queue_id, decision_id, worker_id, dispatch_event,
    action_status, attempt_count, error_code, message_text, payload
  )
  SELECT action_queue_id, decision_id, COALESCE(p_worker_id, worker_id), v_event,
         action_status, attempt_count, COALESCE(p_error_code, ''), p_error_message,
         COALESCE(p_result_payload, JSON_OBJECT())
    FROM npc_perception_action_queue
   WHERE action_queue_id = p_action_queue_id;
END$$

CREATE PROCEDURE mmo_ai_skip_npc_perception_action(
  IN p_action_queue_id BINARY(16),
  IN p_worker_id VARCHAR(191),
  IN p_reason TEXT,
  OUT o_action_status VARCHAR(32)
)
BEGIN
  DECLARE v_not_found BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;

  SET o_action_status = NULL;

  UPDATE npc_perception_action_queue
     SET action_status = 'skipped',
         worker_id = COALESCE(p_worker_id, worker_id),
         completed_at = CURRENT_TIMESTAMP(6),
         updated_at = CURRENT_TIMESTAMP(6),
         result_payload = JSON_MERGE_PATCH(COALESCE(result_payload, JSON_OBJECT()), JSON_OBJECT('skip_reason', COALESCE(p_reason, '')))
   WHERE action_queue_id = p_action_queue_id
     AND action_status IN ('claimed', 'pending');

  SELECT action_status INTO o_action_status
    FROM npc_perception_action_queue
   WHERE action_queue_id = p_action_queue_id
   LIMIT 1;

  INSERT INTO npc_perception_action_dispatch_log (
    action_queue_id, decision_id, worker_id, dispatch_event,
    action_status, attempt_count, message_text, payload
  )
  SELECT action_queue_id, decision_id, COALESCE(p_worker_id, worker_id), 'skipped',
         action_status, attempt_count, p_reason,
         JSON_OBJECT('skip_reason', COALESCE(p_reason, ''))
    FROM npc_perception_action_queue
   WHERE action_queue_id = p_action_queue_id;
END$$
DELIMITER ;

INSERT INTO ai_runtime_schema_versions (migration_key, checksum, description)
VALUES (
  'server/sql/step213_ai_runtime_action_dispatch_contracts.sql',
  SHA2('server/sql/step213_ai_runtime_action_dispatch_contracts.sql', 256),
  'Step213 dispatcher claim/apply/fail/skip contracts for mmo_ai_runtime NPC perception actions'
)
ON DUPLICATE KEY UPDATE
  checksum = VALUES(checksum),
  description = VALUES(description),
  applied_at = CURRENT_TIMESTAMP(6);

-- ============================================================================
-- END server/sql/step213_ai_runtime_action_dispatch_contracts.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step273_ai_dialog_intent_delivery_conversation_storage.sql
-- ============================================================================

-- Step273: durable dialog-intent delivery and conversation storage.
--
-- Step256-Step272 added disabled-by-default C++ transport/proof boundaries for
-- ServerNpcDialogIntent delivery, ACK/NACK, main-thread observation receipts
-- and late-observer resume preflight. This migration creates the durable
-- runtime tables those paths need before any gameplay action may be marked
-- applied.

USE mmo_ai_runtime;

INSERT INTO ai_runtime_schema_versions (migration_key, checksum, description)
VALUES (
  'step273_ai_dialog_intent_delivery_conversation_storage',
  REPEAT('0', 64),
  'Durable gameplay delivery, receipt, conversation observer and dead-letter storage for diagnostic dialog intent transport.'
)
ON DUPLICATE KEY UPDATE
  description = VALUES(description),
  applied_at = CURRENT_TIMESTAMP(6);

CREATE TABLE IF NOT EXISTS dialog_intent_conversation_sessions (
  conversation_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  conversation_key VARCHAR(191) NOT NULL,
  world_instance_uuid CHAR(36) NOT NULL,
  world_name VARCHAR(191) NOT NULL DEFAULT '',
  content_revision_key VARCHAR(191) NOT NULL DEFAULT '',
  speaker_entity_key VARCHAR(191) NOT NULL DEFAULT '',
  speaker_npc_instance_uuid CHAR(36) NOT NULL DEFAULT '',
  line_id VARCHAR(191) NOT NULL DEFAULT '',
  audio_ref VARCHAR(255) NOT NULL DEFAULT '',
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  start_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  duration_ms INT UNSIGNED NOT NULL DEFAULT 0,
  planned_recipients INT UNSIGNED NOT NULL DEFAULT 0,
  conversation_status VARCHAR(32) NOT NULL DEFAULT 'open',
  terminal_reason VARCHAR(128) NOT NULL DEFAULT '',
  terminal_message TEXT NULL,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  opened_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  terminal_at TIMESTAMP(6) NULL DEFAULT NULL,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (conversation_id),
  UNIQUE KEY dialog_intent_conversation_key_uk (conversation_key),
  KEY ix_dialog_conversation_world_tick (world_instance_uuid, server_tick),
  KEY ix_dialog_conversation_status (conversation_status, opened_at),
  KEY ix_dialog_conversation_speaker (world_instance_uuid, speaker_entity_key, opened_at),
  CONSTRAINT dialog_intent_conversation_status_ck CHECK (
    conversation_status IN ('open', 'partially_acked', 'acked', 'nacked', 'timed_out', 'mixed_terminal')
  ),
  CONSTRAINT dialog_intent_conversation_tick_ck CHECK (server_tick >= 0 AND start_tick >= 0),
  CONSTRAINT dialog_intent_conversation_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS gameplay_outbound_deliveries (
  delivery_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  action_queue_id BINARY(16) NULL,
  decision_id BINARY(16) NULL,
  conversation_id BINARY(16) NULL,
  world_instance_uuid CHAR(36) NOT NULL,
  gameplay_kind VARCHAR(64) NOT NULL,
  delivery_kind VARCHAR(64) NOT NULL DEFAULT 'initial',
  action_id VARCHAR(191) NOT NULL,
  ack_key VARCHAR(191) NOT NULL,
  target_session_uuid CHAR(36) NOT NULL DEFAULT '',
  target_character_uuid CHAR(36) NOT NULL DEFAULT '',
  target_character_key VARCHAR(191) NOT NULL DEFAULT '',
  packet_kind VARCHAR(64) NOT NULL,
  packet_sequence BIGINT UNSIGNED NOT NULL DEFAULT 0,
  local_sequence BIGINT UNSIGNED NOT NULL DEFAULT 0,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  payload_sha256 CHAR(64) NOT NULL DEFAULT '',
  payload_bytes INT UNSIGNED NOT NULL DEFAULT 0,
  send_attempts INT UNSIGNED NOT NULL DEFAULT 0,
  delivery_status VARCHAR(32) NOT NULL DEFAULT 'pending',
  terminal_reason VARCHAR(128) NOT NULL DEFAULT '',
  terminal_message TEXT NULL,
  request_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  endpoint_audit JSON NOT NULL DEFAULT (JSON_OBJECT()),
  sent_at TIMESTAMP(6) NULL DEFAULT NULL,
  ack_deadline_at TIMESTAMP(6) NULL DEFAULT NULL,
  terminal_at TIMESTAMP(6) NULL DEFAULT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (delivery_id),
  UNIQUE KEY gameplay_delivery_ack_key_uk (ack_key),
  UNIQUE KEY gameplay_delivery_action_id_uk (action_id),
  KEY ix_gameplay_delivery_action_queue (action_queue_id),
  KEY ix_gameplay_delivery_decision (decision_id),
  KEY ix_gameplay_delivery_conversation (conversation_id),
  KEY ix_gameplay_delivery_world_status (world_instance_uuid, delivery_status, created_at),
  KEY ix_gameplay_delivery_target (target_session_uuid, target_character_key, created_at),
  KEY ix_gameplay_delivery_deadline (delivery_status, ack_deadline_at),
  CONSTRAINT gameplay_delivery_action_fk
    FOREIGN KEY (action_queue_id) REFERENCES npc_perception_action_queue(action_queue_id) ON DELETE SET NULL,
  CONSTRAINT gameplay_delivery_decision_fk
    FOREIGN KEY (decision_id) REFERENCES npc_perception_decisions(decision_id) ON DELETE SET NULL,
  CONSTRAINT gameplay_delivery_conversation_fk
    FOREIGN KEY (conversation_id) REFERENCES dialog_intent_conversation_sessions(conversation_id) ON DELETE SET NULL,
  CONSTRAINT gameplay_delivery_status_ck CHECK (
    delivery_status IN ('pending', 'acked', 'nacked', 'timed_out', 'send_failed', 'dead_letter')
  ),
  CONSTRAINT gameplay_delivery_kind_ck CHECK (
    gameplay_kind IN ('npc_dialog_intent', 'npc_dialog_resume')
  ),
  CONSTRAINT gameplay_delivery_payload_sha_ck CHECK (payload_sha256 = '' OR REGEXP_LIKE(payload_sha256, '^[0-9a-f]{64}$')),
  CONSTRAINT gameplay_delivery_payload_json_ck CHECK (JSON_VALID(request_payload)),
  CONSTRAINT gameplay_delivery_endpoint_json_ck CHECK (JSON_VALID(endpoint_audit))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS dialog_intent_conversation_observers (
  observer_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  conversation_id BINARY(16) NOT NULL,
  delivery_id BINARY(16) NULL,
  observer_session_uuid CHAR(36) NOT NULL,
  observer_character_uuid CHAR(36) NOT NULL DEFAULT '',
  observer_character_key VARCHAR(191) NOT NULL DEFAULT '',
  observer_kind VARCHAR(32) NOT NULL DEFAULT 'target_session',
  observer_status VARCHAR(32) NOT NULL DEFAULT 'pending',
  observation_status VARCHAR(32) NOT NULL DEFAULT 'none',
  action_id VARCHAR(191) NOT NULL DEFAULT '',
  ack_key VARCHAR(191) NOT NULL DEFAULT '',
  packet_sequence BIGINT UNSIGNED NOT NULL DEFAULT 0,
  local_sequence BIGINT UNSIGNED NOT NULL DEFAULT 0,
  distance_squared DOUBLE NOT NULL DEFAULT 0,
  has_position TINYINT(1) NOT NULL DEFAULT 0,
  terminal_reason VARCHAR(128) NOT NULL DEFAULT '',
  terminal_message TEXT NULL,
  observation_reason VARCHAR(128) NOT NULL DEFAULT '',
  observation_message TEXT NULL,
  observation_ui_applied TINYINT(1) NOT NULL DEFAULT 0,
  observation_audio_applied TINYINT(1) NOT NULL DEFAULT 0,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  registered_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  sent_at TIMESTAMP(6) NULL DEFAULT NULL,
  ack_deadline_at TIMESTAMP(6) NULL DEFAULT NULL,
  observed_at TIMESTAMP(6) NULL DEFAULT NULL,
  terminal_at TIMESTAMP(6) NULL DEFAULT NULL,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (observer_id),
  UNIQUE KEY dialog_observer_ack_key_uk (ack_key),
  UNIQUE KEY dialog_observer_session_uk (conversation_id, observer_session_uuid),
  KEY ix_dialog_observer_delivery (delivery_id),
  KEY ix_dialog_observer_status (observer_status, registered_at),
  KEY ix_dialog_observer_observation (observation_status, observed_at),
  CONSTRAINT dialog_observer_conversation_fk
    FOREIGN KEY (conversation_id) REFERENCES dialog_intent_conversation_sessions(conversation_id) ON DELETE CASCADE,
  CONSTRAINT dialog_observer_delivery_fk
    FOREIGN KEY (delivery_id) REFERENCES gameplay_outbound_deliveries(delivery_id) ON DELETE SET NULL,
  CONSTRAINT dialog_observer_status_ck CHECK (observer_status IN ('pending', 'acked', 'nacked', 'timed_out')),
  CONSTRAINT dialog_observer_observation_ck CHECK (observation_status IN ('none', 'observed', 'skipped')),
  CONSTRAINT dialog_observer_kind_ck CHECK (observer_kind IN ('target_session', 'aoi', 'late_observer')),
  CONSTRAINT dialog_observer_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS gameplay_delivery_receipts (
  receipt_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  delivery_id BINARY(16) NULL,
  conversation_id BINARY(16) NULL,
  receipt_kind VARCHAR(48) NOT NULL,
  action_id VARCHAR(191) NOT NULL DEFAULT '',
  ack_key VARCHAR(191) NOT NULL DEFAULT '',
  session_uuid CHAR(36) NOT NULL DEFAULT '',
  character_uuid CHAR(36) NOT NULL DEFAULT '',
  character_key VARCHAR(191) NOT NULL DEFAULT '',
  client_observation_status VARCHAR(32) NOT NULL DEFAULT '',
  reason VARCHAR(128) NOT NULL DEFAULT '',
  message_text TEXT NULL,
  payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  received_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (receipt_id),
  KEY ix_gameplay_receipt_delivery (delivery_id, received_at),
  KEY ix_gameplay_receipt_conversation (conversation_id, received_at),
  KEY ix_gameplay_receipt_ack (ack_key, received_at),
  KEY ix_gameplay_receipt_kind (receipt_kind, received_at),
  CONSTRAINT gameplay_receipt_delivery_fk
    FOREIGN KEY (delivery_id) REFERENCES gameplay_outbound_deliveries(delivery_id) ON DELETE SET NULL,
  CONSTRAINT gameplay_receipt_conversation_fk
    FOREIGN KEY (conversation_id) REFERENCES dialog_intent_conversation_sessions(conversation_id) ON DELETE SET NULL,
  CONSTRAINT gameplay_receipt_kind_ck CHECK (
    receipt_kind IN (
      'acked', 'nacked', 'observed', 'skipped', 'duplicate',
      'unknown_ack_key', 'conflicting_action_id',
      'conflicting_terminal_status', 'conflicting_observation_status',
      'invalid_receipt', 'late_after_timeout'
    )
  ),
  CONSTRAINT gameplay_receipt_observation_ck CHECK (
    client_observation_status IN ('', 'none', 'observed', 'skipped')
  ),
  CONSTRAINT gameplay_receipt_payload_json_ck CHECK (JSON_VALID(payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS gameplay_delivery_dead_letters (
  dead_letter_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  delivery_id BINARY(16) NULL,
  conversation_id BINARY(16) NULL,
  action_id VARCHAR(191) NOT NULL DEFAULT '',
  ack_key VARCHAR(191) NOT NULL DEFAULT '',
  dead_letter_reason VARCHAR(128) NOT NULL,
  retryable TINYINT(1) NOT NULL DEFAULT 0,
  send_attempts INT UNSIGNED NOT NULL DEFAULT 0,
  payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (dead_letter_id),
  KEY ix_gameplay_dead_letter_delivery (delivery_id, created_at),
  KEY ix_gameplay_dead_letter_conversation (conversation_id, created_at),
  KEY ix_gameplay_dead_letter_reason (dead_letter_reason, created_at),
  CONSTRAINT gameplay_dead_letter_delivery_fk
    FOREIGN KEY (delivery_id) REFERENCES gameplay_outbound_deliveries(delivery_id) ON DELETE SET NULL,
  CONSTRAINT gameplay_dead_letter_conversation_fk
    FOREIGN KEY (conversation_id) REFERENCES dialog_intent_conversation_sessions(conversation_id) ON DELETE SET NULL,
  CONSTRAINT gameplay_dead_letter_payload_json_ck CHECK (JSON_VALID(payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP VIEW IF EXISTS v_gameplay_outbound_deliveries;
CREATE VIEW v_gameplay_outbound_deliveries AS
SELECT
  BIN_TO_UUID(d.delivery_id, 1) AS delivery_uuid,
  BIN_TO_UUID(d.action_queue_id, 1) AS action_queue_uuid,
  BIN_TO_UUID(d.decision_id, 1) AS decision_uuid,
  BIN_TO_UUID(d.conversation_id, 1) AS conversation_uuid,
  d.world_instance_uuid,
  d.gameplay_kind,
  d.delivery_kind,
  d.action_id,
  d.ack_key,
  d.target_session_uuid,
  d.target_character_uuid,
  d.target_character_key,
  d.packet_kind,
  d.packet_sequence,
  d.local_sequence,
  d.server_tick,
  d.payload_sha256,
  d.payload_bytes,
  d.send_attempts,
  d.delivery_status,
  d.terminal_reason,
  d.terminal_message,
  d.sent_at,
  d.ack_deadline_at,
  d.terminal_at,
  d.created_at,
  d.updated_at
FROM gameplay_outbound_deliveries d;

DROP VIEW IF EXISTS v_dialog_intent_conversation_observers;
CREATE VIEW v_dialog_intent_conversation_observers AS
SELECT
  BIN_TO_UUID(c.conversation_id, 1) AS conversation_uuid,
  c.conversation_key,
  c.world_instance_uuid,
  c.world_name,
  c.speaker_entity_key,
  c.speaker_npc_instance_uuid,
  c.line_id,
  c.audio_ref,
  c.server_tick,
  c.start_tick,
  c.duration_ms,
  c.conversation_status,
  BIN_TO_UUID(o.observer_id, 1) AS observer_uuid,
  BIN_TO_UUID(o.delivery_id, 1) AS delivery_uuid,
  o.observer_session_uuid,
  o.observer_character_uuid,
  o.observer_character_key,
  o.observer_kind,
  o.observer_status,
  o.observation_status,
  o.action_id,
  o.ack_key,
  o.packet_sequence,
  o.local_sequence,
  o.distance_squared,
  o.has_position,
  o.terminal_reason,
  o.observation_reason,
  o.observation_ui_applied,
  o.observation_audio_applied,
  o.registered_at,
  o.sent_at,
  o.ack_deadline_at,
  o.observed_at,
  o.terminal_at
FROM dialog_intent_conversation_sessions c
JOIN dialog_intent_conversation_observers o ON o.conversation_id = c.conversation_id;

DROP VIEW IF EXISTS v_gameplay_delivery_health;
CREATE VIEW v_gameplay_delivery_health AS
SELECT
  'global' AS health_scope,
  (SELECT COUNT(*) FROM gameplay_outbound_deliveries WHERE delivery_status = 'pending') AS pending_count,
  (SELECT COUNT(*) FROM gameplay_outbound_deliveries WHERE delivery_status = 'acked') AS acked_count,
  (SELECT COUNT(*) FROM gameplay_outbound_deliveries WHERE delivery_status = 'nacked') AS nacked_count,
  (SELECT COUNT(*) FROM gameplay_outbound_deliveries WHERE delivery_status = 'timed_out') AS timed_out_count,
  (SELECT COUNT(*) FROM gameplay_outbound_deliveries WHERE delivery_status IN ('send_failed', 'dead_letter')) AS failed_count,
  (SELECT COUNT(*) FROM gameplay_outbound_deliveries WHERE delivery_status = 'pending' AND ack_deadline_at <= CURRENT_TIMESTAMP(6)) AS overdue_count,
  (SELECT COUNT(*) FROM dialog_intent_conversation_sessions WHERE conversation_status = 'open') AS open_conversations,
  (SELECT COUNT(*) FROM gameplay_delivery_receipts) AS receipt_count,
  (SELECT COUNT(*) FROM gameplay_delivery_dead_letters) AS dead_letter_count;

DROP PROCEDURE IF EXISTS mmo_ai_record_gameplay_delivery_sent;
DROP PROCEDURE IF EXISTS mmo_ai_record_gameplay_delivery_receipt;
DROP PROCEDURE IF EXISTS mmo_ai_mark_gameplay_delivery_timed_out;
DROP PROCEDURE IF EXISTS mmo_ai_record_gameplay_delivery_dead_letter;

DELIMITER $$
CREATE PROCEDURE mmo_ai_record_gameplay_delivery_sent(
  IN p_action_queue_id BINARY(16),
  IN p_decision_id BINARY(16),
  IN p_conversation_id BINARY(16),
  IN p_world_instance_uuid CHAR(36),
  IN p_gameplay_kind VARCHAR(64),
  IN p_delivery_kind VARCHAR(64),
  IN p_action_id VARCHAR(191),
  IN p_ack_key VARCHAR(191),
  IN p_target_session_uuid CHAR(36),
  IN p_target_character_uuid CHAR(36),
  IN p_target_character_key VARCHAR(191),
  IN p_packet_kind VARCHAR(64),
  IN p_packet_sequence BIGINT UNSIGNED,
  IN p_local_sequence BIGINT UNSIGNED,
  IN p_server_tick BIGINT UNSIGNED,
  IN p_payload_sha256 CHAR(64),
  IN p_payload_bytes INT UNSIGNED,
  IN p_ack_timeout_ms INT UNSIGNED,
  IN p_request_payload JSON,
  IN p_endpoint_audit JSON,
  OUT o_delivery_id BINARY(16),
  OUT o_delivery_status VARCHAR(32)
)
BEGIN
  SET o_delivery_id = NULL;
  SET o_delivery_status = NULL;

  INSERT INTO gameplay_outbound_deliveries (
    action_queue_id, decision_id, conversation_id, world_instance_uuid,
    gameplay_kind, delivery_kind, action_id, ack_key,
    target_session_uuid, target_character_uuid, target_character_key,
    packet_kind, packet_sequence, local_sequence, server_tick,
    payload_sha256, payload_bytes, send_attempts, delivery_status,
    request_payload, endpoint_audit, sent_at, ack_deadline_at
  )
  VALUES (
    p_action_queue_id, p_decision_id, p_conversation_id, p_world_instance_uuid,
    COALESCE(NULLIF(p_gameplay_kind, ''), 'npc_dialog_intent'),
    COALESCE(NULLIF(p_delivery_kind, ''), 'initial'),
    p_action_id, p_ack_key,
    COALESCE(p_target_session_uuid, ''), COALESCE(p_target_character_uuid, ''),
    COALESCE(p_target_character_key, ''),
    COALESCE(NULLIF(p_packet_kind, ''), 'ServerNpcDialogIntent'),
    COALESCE(p_packet_sequence, 0), COALESCE(p_local_sequence, 0), COALESCE(p_server_tick, 0),
    COALESCE(p_payload_sha256, ''), COALESCE(p_payload_bytes, 0), 1, 'pending',
    COALESCE(p_request_payload, JSON_OBJECT()), COALESCE(p_endpoint_audit, JSON_OBJECT()),
    CURRENT_TIMESTAMP(6),
    TIMESTAMPADD(MICROSECOND, COALESCE(p_ack_timeout_ms, 0) * 1000, CURRENT_TIMESTAMP(6))
  )
  ON DUPLICATE KEY UPDATE
    send_attempts = IF(delivery_status = 'pending', send_attempts + 1, send_attempts),
    sent_at = IF(delivery_status = 'pending', CURRENT_TIMESTAMP(6), sent_at),
    ack_deadline_at = IF(
      delivery_status = 'pending',
      TIMESTAMPADD(MICROSECOND, COALESCE(p_ack_timeout_ms, 0) * 1000, CURRENT_TIMESTAMP(6)),
      ack_deadline_at
    ),
    endpoint_audit = IF(delivery_status = 'pending', COALESCE(p_endpoint_audit, JSON_OBJECT()), endpoint_audit),
    updated_at = CURRENT_TIMESTAMP(6);

  SELECT delivery_id, delivery_status
    INTO o_delivery_id, o_delivery_status
    FROM gameplay_outbound_deliveries
   WHERE ack_key = p_ack_key OR action_id = p_action_id
   ORDER BY CASE WHEN ack_key = p_ack_key THEN 0 ELSE 1 END
   LIMIT 1;
END$$

CREATE PROCEDURE mmo_ai_record_gameplay_delivery_receipt(
  IN p_action_id VARCHAR(191),
  IN p_ack_key VARCHAR(191),
  IN p_receipt_kind VARCHAR(48),
  IN p_session_uuid CHAR(36),
  IN p_character_uuid CHAR(36),
  IN p_character_key VARCHAR(191),
  IN p_client_observation_status VARCHAR(32),
  IN p_reason VARCHAR(128),
  IN p_message_text TEXT,
  IN p_payload JSON,
  OUT o_delivery_id BINARY(16),
  OUT o_receipt_kind VARCHAR(48),
  OUT o_delivery_status VARCHAR(32)
)
BEGIN
  DECLARE v_not_found BOOL DEFAULT FALSE;
  DECLARE v_conversation_id BINARY(16) DEFAULT NULL;
  DECLARE v_current_status VARCHAR(32) DEFAULT NULL;
  DECLARE v_effective_kind VARCHAR(48) DEFAULT NULL;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;

  SET o_delivery_id = NULL;
  SET o_receipt_kind = 'invalid_receipt';
  SET o_delivery_status = NULL;

  SELECT delivery_id, conversation_id, delivery_status
    INTO o_delivery_id, v_conversation_id, v_current_status
    FROM gameplay_outbound_deliveries
   WHERE ack_key = COALESCE(p_ack_key, '')
      OR (COALESCE(p_ack_key, '') = '' AND action_id = COALESCE(p_action_id, ''))
   ORDER BY CASE WHEN ack_key = COALESCE(p_ack_key, '') THEN 0 ELSE 1 END
   LIMIT 1;

  IF v_not_found OR o_delivery_id IS NULL THEN
    SET v_effective_kind = 'unknown_ack_key';
    INSERT INTO gameplay_delivery_receipts (
      receipt_kind, action_id, ack_key, session_uuid, character_uuid,
      character_key, client_observation_status, reason, message_text, payload
    )
    VALUES (
      v_effective_kind, COALESCE(p_action_id, ''), COALESCE(p_ack_key, ''),
      COALESCE(p_session_uuid, ''), COALESCE(p_character_uuid, ''),
      COALESCE(p_character_key, ''), COALESCE(p_client_observation_status, ''),
      COALESCE(p_reason, ''), p_message_text, COALESCE(p_payload, JSON_OBJECT())
    );
    SET o_receipt_kind = v_effective_kind;
  ELSE
    SET v_effective_kind = CASE
      WHEN COALESCE(p_receipt_kind, '') IN (
        'acked', 'nacked', 'observed', 'skipped', 'duplicate',
        'conflicting_action_id', 'conflicting_terminal_status',
        'conflicting_observation_status', 'late_after_timeout'
      ) THEN p_receipt_kind
      ELSE 'invalid_receipt'
    END;

    IF v_current_status = 'timed_out' AND v_effective_kind IN ('acked', 'nacked') THEN
      SET v_effective_kind = 'late_after_timeout';
    ELSEIF v_current_status IN ('acked', 'nacked', 'send_failed', 'dead_letter')
       AND v_effective_kind IN ('acked', 'nacked') THEN
      SET v_effective_kind = 'duplicate';
    END IF;

    INSERT INTO gameplay_delivery_receipts (
      delivery_id, conversation_id, receipt_kind, action_id, ack_key,
      session_uuid, character_uuid, character_key, client_observation_status,
      reason, message_text, payload
    )
    VALUES (
      o_delivery_id, v_conversation_id, v_effective_kind,
      COALESCE(p_action_id, ''), COALESCE(p_ack_key, ''),
      COALESCE(p_session_uuid, ''), COALESCE(p_character_uuid, ''),
      COALESCE(p_character_key, ''), COALESCE(p_client_observation_status, ''),
      COALESCE(p_reason, ''), p_message_text, COALESCE(p_payload, JSON_OBJECT())
    );

    IF v_current_status = 'pending' AND v_effective_kind IN ('acked', 'nacked') THEN
      UPDATE gameplay_outbound_deliveries
         SET delivery_status = v_effective_kind,
             terminal_reason = COALESCE(p_reason, ''),
             terminal_message = p_message_text,
             terminal_at = CURRENT_TIMESTAMP(6),
             updated_at = CURRENT_TIMESTAMP(6)
       WHERE delivery_id = o_delivery_id;
    END IF;

    UPDATE dialog_intent_conversation_observers
       SET observation_status = CASE
             WHEN v_effective_kind = 'observed' THEN 'observed'
             WHEN v_effective_kind = 'skipped' THEN 'skipped'
             ELSE observation_status
           END,
           observer_status = CASE
             WHEN v_current_status = 'pending' AND v_effective_kind = 'acked' THEN 'acked'
             WHEN v_current_status = 'pending' AND v_effective_kind = 'nacked' THEN 'nacked'
             ELSE observer_status
           END,
           observed_at = CASE
             WHEN v_effective_kind IN ('observed', 'skipped') THEN CURRENT_TIMESTAMP(6)
             ELSE observed_at
           END,
           terminal_reason = CASE
             WHEN v_effective_kind IN ('acked', 'nacked') THEN COALESCE(p_reason, '')
             ELSE terminal_reason
           END,
           terminal_message = CASE
             WHEN v_effective_kind IN ('acked', 'nacked') THEN p_message_text
             ELSE terminal_message
           END,
           terminal_at = CASE
             WHEN v_current_status = 'pending' AND v_effective_kind IN ('acked', 'nacked') THEN CURRENT_TIMESTAMP(6)
             ELSE terminal_at
           END,
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE delivery_id = o_delivery_id;

    SELECT delivery_status
      INTO o_delivery_status
      FROM gameplay_outbound_deliveries
     WHERE delivery_id = o_delivery_id
     LIMIT 1;

    SET o_receipt_kind = v_effective_kind;
  END IF;
END$$

CREATE PROCEDURE mmo_ai_mark_gameplay_delivery_timed_out(
  IN p_delivery_id BINARY(16),
  IN p_now TIMESTAMP(6),
  OUT o_timed_out_count INT
)
BEGIN
  UPDATE gameplay_outbound_deliveries
     SET delivery_status = 'timed_out',
         terminal_reason = 'ack_timeout',
         terminal_at = COALESCE(p_now, CURRENT_TIMESTAMP(6)),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE delivery_status = 'pending'
     AND (p_delivery_id IS NULL OR delivery_id = p_delivery_id)
     AND ack_deadline_at IS NOT NULL
     AND ack_deadline_at <= COALESCE(p_now, CURRENT_TIMESTAMP(6));

  SET o_timed_out_count = ROW_COUNT();

  UPDATE dialog_intent_conversation_observers o
  JOIN gameplay_outbound_deliveries d ON d.delivery_id = o.delivery_id
     SET o.observer_status = 'timed_out',
         o.terminal_reason = 'ack_timeout',
         o.terminal_at = COALESCE(p_now, CURRENT_TIMESTAMP(6)),
         o.updated_at = CURRENT_TIMESTAMP(6)
   WHERE d.delivery_status = 'timed_out'
     AND o.observer_status = 'pending';
END$$

CREATE PROCEDURE mmo_ai_record_gameplay_delivery_dead_letter(
  IN p_delivery_id BINARY(16),
  IN p_dead_letter_reason VARCHAR(128),
  IN p_retryable TINYINT(1),
  IN p_payload JSON,
  OUT o_dead_letter_id BINARY(16)
)
BEGIN
  SET o_dead_letter_id = UUID_TO_BIN(UUID(), 1);

  INSERT INTO gameplay_delivery_dead_letters (
    dead_letter_id, delivery_id, conversation_id, action_id, ack_key,
    dead_letter_reason, retryable, send_attempts, payload
  )
  SELECT
    o_dead_letter_id, delivery_id, conversation_id, action_id, ack_key,
    COALESCE(NULLIF(p_dead_letter_reason, ''), 'send_failure'),
    COALESCE(p_retryable, 0), send_attempts,
    COALESCE(p_payload, JSON_OBJECT())
    FROM gameplay_outbound_deliveries
   WHERE delivery_id = p_delivery_id;

  UPDATE gameplay_outbound_deliveries
     SET delivery_status = 'dead_letter',
         terminal_reason = COALESCE(NULLIF(p_dead_letter_reason, ''), 'send_failure'),
         terminal_at = CURRENT_TIMESTAMP(6),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE delivery_id = p_delivery_id
     AND delivery_status IN ('pending', 'send_failed', 'timed_out');
END$$
DELIMITER ;

-- ============================================================================
-- END server/sql/step273_ai_dialog_intent_delivery_conversation_storage.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step281_ai_dialog_intent_delivery_runtime_activation.sql
-- ============================================================================

-- Step281: explicitly flagged C++ runtime Step273 delivery persistence activation.
-- This migration does not enable gameplay behavior by itself. It only adds a
-- schema marker and a focused health view for deliveries written by the
-- --ai-dialog-intent-step273-persistence-runtime-storage server flag.

CREATE DATABASE IF NOT EXISTS `mmo_ai_runtime`
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_0900_ai_ci;

USE `mmo_ai_runtime`;

CREATE OR REPLACE VIEW `v_step281_gameplay_delivery_runtime_activation_health` AS
SELECT
  'step281_runtime_storage' AS `health_scope`,
  (
    SELECT COUNT(*)
    FROM `gameplay_outbound_deliveries`
    WHERE JSON_UNQUOTE(JSON_EXTRACT(COALESCE(`request_payload`, JSON_OBJECT()), '$.source')) = 'step281_runtime_storage'
  ) AS `runtime_delivery_count`,
  (
    SELECT COUNT(*)
    FROM `gameplay_outbound_deliveries`
    WHERE `delivery_status` = 'pending'
      AND JSON_UNQUOTE(JSON_EXTRACT(COALESCE(`request_payload`, JSON_OBJECT()), '$.source')) = 'step281_runtime_storage'
  ) AS `runtime_pending_delivery_count`,
  (
    SELECT COUNT(*)
    FROM `gameplay_outbound_deliveries`
    WHERE `delivery_status` IN ('acked', 'nacked', 'timed_out', 'send_failed', 'dead_letter')
      AND JSON_UNQUOTE(JSON_EXTRACT(COALESCE(`request_payload`, JSON_OBJECT()), '$.source')) = 'step281_runtime_storage'
  ) AS `runtime_terminal_delivery_count`,
  (
    SELECT COUNT(*)
    FROM `gameplay_delivery_receipts`
    WHERE JSON_UNQUOTE(JSON_EXTRACT(COALESCE(`payload`, JSON_OBJECT()), '$.source')) = 'step281_runtime_storage'
  ) AS `runtime_receipt_count`,
  (
    SELECT COUNT(*)
    FROM `gameplay_delivery_receipts`
    WHERE `receipt_kind` IN ('acked', 'nacked')
      AND JSON_UNQUOTE(JSON_EXTRACT(COALESCE(`payload`, JSON_OBJECT()), '$.source')) = 'step281_runtime_storage'
  ) AS `runtime_terminal_receipt_count`,
  (
    SELECT COUNT(*)
    FROM `gameplay_delivery_receipts`
    WHERE `receipt_kind` IN ('observed', 'skipped')
      AND JSON_UNQUOTE(JSON_EXTRACT(COALESCE(`payload`, JSON_OBJECT()), '$.source')) = 'step281_runtime_storage'
  ) AS `runtime_observation_receipt_count`,
  (
    SELECT COUNT(*)
    FROM `dialog_intent_conversation_observers`
    WHERE JSON_UNQUOTE(JSON_EXTRACT(COALESCE(`raw_payload`, JSON_OBJECT()), '$.source')) = 'step281_runtime_storage'
  ) AS `runtime_observer_count`,
  (
    SELECT COUNT(*)
    FROM `gameplay_delivery_dead_letters`
    WHERE JSON_UNQUOTE(JSON_EXTRACT(COALESCE(`payload`, JSON_OBJECT()), '$.source')) = 'step281_runtime_storage'
  ) AS `runtime_dead_letter_count`;


DROP PROCEDURE IF EXISTS `mmo_ai_mark_step281_runtime_storage_timed_out`;

DELIMITER ;;
CREATE PROCEDURE `mmo_ai_mark_step281_runtime_storage_timed_out`(
  OUT `o_timed_out_count` INT
)
BEGIN
  UPDATE `gameplay_outbound_deliveries`
     SET `delivery_status` = 'timed_out',
         `terminal_reason` = 'ack_timeout',
         `terminal_message` = 'Step281 runtime storage ACK timeout',
         `terminal_at` = CURRENT_TIMESTAMP(6),
         `updated_at` = CURRENT_TIMESTAMP(6)
   WHERE `delivery_status` = 'pending'
     AND `ack_deadline_at` IS NOT NULL
     AND `ack_deadline_at` <= CURRENT_TIMESTAMP(6)
     AND JSON_UNQUOTE(JSON_EXTRACT(COALESCE(`request_payload`, JSON_OBJECT()), '$.source')) = 'step281_runtime_storage';

  SET `o_timed_out_count` = ROW_COUNT();
END;;
DELIMITER ;

INSERT INTO `ai_runtime_schema_versions`(`migration_key`, `checksum`, `description`)
VALUES(
  'step281_ai_dialog_intent_delivery_runtime_activation',
  '2810000000000000000000000000000000000000000000000000000000000000',
  'Step281 adds explicitly flagged C++ runtime storage health for Step273 dialog-intent delivery sent, receipt, observation and timeout persistence.'
)
ON DUPLICATE KEY UPDATE
  `checksum` = VALUES(`checksum`),
  `description` = VALUES(`description`),
  `applied_at` = CURRENT_TIMESTAMP(6);

-- ============================================================================
-- END server/sql/step281_ai_dialog_intent_delivery_runtime_activation.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step282_ai_dialog_intent_late_observer_replay_send.sql
-- ============================================================================

-- Step282: late-observer dialog-intent replay send runtime DB support.
-- Safe to re-run. Runtime use is guarded by
-- --ai-dialog-intent-late-observer-resume-runtime-send plus Step281 storage.

CREATE DATABASE IF NOT EXISTS `mmo_ai_runtime`
  CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
USE `mmo_ai_runtime`;

CREATE TABLE IF NOT EXISTS `ai_runtime_schema_versions` (
  `migration_key` VARCHAR(255) NOT NULL,
  `checksum` CHAR(64) NOT NULL,
  `description` TEXT NOT NULL,
  `applied_at` TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (`migration_key`),
  CONSTRAINT `ai_runtime_schema_versions_checksum_ck` CHECK (REGEXP_LIKE(`checksum`, '^[0-9a-f]{64}$'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

INSERT INTO `ai_runtime_schema_versions` (`migration_key`, `checksum`, `description`)
VALUES (
  'step282_ai_dialog_intent_late_observer_replay_send',
  '2820000000000000000000000000000000000000000000000000000000000000',
  'Late-observer replay send health and source-filtered timeout support over Step273/Step281 delivery storage.'
)
ON DUPLICATE KEY UPDATE
  `checksum` = VALUES(`checksum`),
  `applied_at` = CURRENT_TIMESTAMP(6),
  `description` = VALUES(`description`);

DROP PROCEDURE IF EXISTS `mmo_ai_mark_step282_late_observer_replay_timed_out`;
DELIMITER ;;
CREATE PROCEDURE `mmo_ai_mark_step282_late_observer_replay_timed_out`(
  OUT `o_timed_out_count` INT
)
BEGIN
  UPDATE `gameplay_outbound_deliveries`
     SET `delivery_status` = 'timed_out',
         `terminal_reason` = 'ack_timeout',
         `terminal_message` = 'Step282 late-observer replay ACK timeout',
         `terminal_at` = CURRENT_TIMESTAMP(6),
         `updated_at` = CURRENT_TIMESTAMP(6)
   WHERE `delivery_status` = 'pending'
     AND `ack_deadline_at` IS NOT NULL
     AND `ack_deadline_at` <= CURRENT_TIMESTAMP(6)
     AND `delivery_kind` = 'resume'
     AND JSON_UNQUOTE(JSON_EXTRACT(COALESCE(`request_payload`, JSON_OBJECT()), '$.source')) = 'step282_late_observer_replay_send';

  SET `o_timed_out_count` = ROW_COUNT();
END ;;
DELIMITER ;

DROP VIEW IF EXISTS `v_step282_late_observer_replay_send_health`;
CREATE VIEW `v_step282_late_observer_replay_send_health` AS
SELECT
  'step282_late_observer_replay_send' AS `health_scope`,
  (SELECT COUNT(*)
     FROM `gameplay_outbound_deliveries`
    WHERE `delivery_kind` = 'resume'
      AND JSON_UNQUOTE(JSON_EXTRACT(COALESCE(`request_payload`, JSON_OBJECT()), '$.source')) = 'step282_late_observer_replay_send') AS `replay_delivery_count`,
  (SELECT COUNT(*)
     FROM `gameplay_outbound_deliveries`
    WHERE `delivery_kind` = 'resume'
      AND `delivery_status` = 'pending'
      AND JSON_UNQUOTE(JSON_EXTRACT(COALESCE(`request_payload`, JSON_OBJECT()), '$.source')) = 'step282_late_observer_replay_send') AS `replay_pending_delivery_count`,
  (SELECT COUNT(*)
     FROM `gameplay_outbound_deliveries`
    WHERE `delivery_kind` = 'resume'
      AND `delivery_status` IN ('acked','nacked','timed_out','send_failed','dead_letter')
      AND JSON_UNQUOTE(JSON_EXTRACT(COALESCE(`request_payload`, JSON_OBJECT()), '$.source')) = 'step282_late_observer_replay_send') AS `replay_terminal_delivery_count`,
  (SELECT COUNT(*)
     FROM `gameplay_delivery_receipts`
    WHERE JSON_UNQUOTE(JSON_EXTRACT(COALESCE(`payload`, JSON_OBJECT()), '$.source')) = 'step282_late_observer_replay_send') AS `replay_receipt_count`,
  (SELECT COUNT(*)
     FROM `gameplay_delivery_receipts`
    WHERE `receipt_kind` IN ('acked','nacked')
      AND JSON_UNQUOTE(JSON_EXTRACT(COALESCE(`payload`, JSON_OBJECT()), '$.source')) = 'step282_late_observer_replay_send') AS `replay_terminal_receipt_count`,
  (SELECT COUNT(*)
     FROM `gameplay_delivery_receipts`
    WHERE `receipt_kind` IN ('observed','skipped')
      AND JSON_UNQUOTE(JSON_EXTRACT(COALESCE(`payload`, JSON_OBJECT()), '$.source')) = 'step282_late_observer_replay_send') AS `replay_observation_receipt_count`,
  (SELECT COUNT(*)
     FROM `dialog_intent_conversation_observers`
    WHERE JSON_UNQUOTE(JSON_EXTRACT(COALESCE(`raw_payload`, JSON_OBJECT()), '$.source')) = 'step282_late_observer_replay_send') AS `replay_observer_count`,
  (SELECT COUNT(*)
     FROM `gameplay_delivery_dead_letters`
    WHERE JSON_UNQUOTE(JSON_EXTRACT(COALESCE(`payload`, JSON_OBJECT()), '$.source')) = 'step282_late_observer_replay_send') AS `replay_dead_letter_count`;

-- ============================================================================
-- END server/sql/step282_ai_dialog_intent_late_observer_replay_send.sql
-- ============================================================================

-- ============================================================================
-- BEGIN server/sql/step283_ai_dialog_intent_durable_mark_applied_gate.sql
-- ============================================================================

-- Step283: durable mark_applied gate for dialog-intent deliveries.
-- This migration is intentionally source-agnostic for Step281/Step282 rows, but
-- the procedure only applies actions linked to npc_perception_action_queue.

CREATE DATABASE IF NOT EXISTS mmo_ai_runtime
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_0900_ai_ci;
USE mmo_ai_runtime;

DROP PROCEDURE IF EXISTS mmo_ai_mark_dialog_intent_action_applied_if_terminal;
DELIMITER $$
CREATE PROCEDURE mmo_ai_mark_dialog_intent_action_applied_if_terminal(
  IN p_action_id VARCHAR(191),
  IN p_ack_key VARCHAR(191),
  IN p_worker_id VARCHAR(191),
  IN p_require_observation TINYINT(1),
  IN p_result_payload JSON,
  OUT o_gate_status VARCHAR(64),
  OUT o_action_status VARCHAR(32),
  OUT o_action_queue_uuid CHAR(36)
)
proc: BEGIN
  DECLARE v_not_found BOOL DEFAULT FALSE;
  DECLARE v_delivery_id BINARY(16) DEFAULT NULL;
  DECLARE v_action_queue_id BINARY(16) DEFAULT NULL;
  DECLARE v_decision_id BINARY(16) DEFAULT NULL;
  DECLARE v_delivery_status VARCHAR(32) DEFAULT '';
  DECLARE v_action_status VARCHAR(32) DEFAULT '';
  DECLARE v_observation_count INT DEFAULT 0;
  DECLARE v_gate_payload JSON;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;

  SET o_gate_status = 'unknown';
  SET o_action_status = NULL;
  SET o_action_queue_uuid = NULL;
  SET v_gate_payload = JSON_MERGE_PATCH(
    JSON_OBJECT(
      'source', 'step283_durable_mark_applied_gate',
      'action_id', COALESCE(p_action_id, ''),
      'ack_key', COALESCE(p_ack_key, ''),
      'require_observation', COALESCE(p_require_observation, 1)
    ),
    COALESCE(p_result_payload, JSON_OBJECT())
  );

  IF COALESCE(TRIM(p_action_id), '') = '' OR COALESCE(TRIM(p_ack_key), '') = '' THEN
    SET o_gate_status = 'missing_action_or_ack_identity';
    LEAVE proc;
  END IF;

  SET v_not_found = FALSE;
  SELECT delivery_id, action_queue_id, decision_id, delivery_status
    INTO v_delivery_id, v_action_queue_id, v_decision_id, v_delivery_status
    FROM gameplay_outbound_deliveries
   WHERE action_id = p_action_id
     AND ack_key = p_ack_key
   LIMIT 1;

  IF v_not_found OR v_delivery_id IS NULL THEN
    SET o_gate_status = 'delivery_not_found';
    LEAVE proc;
  END IF;

  IF v_action_queue_id IS NULL THEN
    SET o_gate_status = 'missing_action_queue_id';
    LEAVE proc;
  END IF;

  SET o_action_queue_uuid = BIN_TO_UUID(v_action_queue_id, 1);

  IF v_delivery_status = 'pending' THEN
    SET o_gate_status = 'delivery_pending';
    LEAVE proc;
  END IF;

  IF v_delivery_status <> 'acked' THEN
    SET o_gate_status = 'delivery_terminal_not_success';
    LEAVE proc;
  END IF;

  IF COALESCE(p_require_observation, 1) <> 0 THEN
    SELECT COUNT(*)
      INTO v_observation_count
      FROM gameplay_delivery_receipts
     WHERE delivery_id = v_delivery_id
       AND receipt_kind IN ('observed', 'skipped')
       AND client_observation_status IN ('observed', 'skipped');

    IF v_observation_count = 0 THEN
      SELECT COUNT(*)
        INTO v_observation_count
        FROM dialog_intent_conversation_observers
       WHERE delivery_id = v_delivery_id
         AND observation_status IN ('observed', 'skipped');
    END IF;

    IF v_observation_count = 0 THEN
      SET o_gate_status = 'acked_without_observation';
      LEAVE proc;
    END IF;
  END IF;

  SET v_not_found = FALSE;
  SELECT action_status
    INTO v_action_status
    FROM npc_perception_action_queue
   WHERE action_queue_id = v_action_queue_id
   LIMIT 1;

  IF v_not_found THEN
    SET o_gate_status = 'action_not_found';
    LEAVE proc;
  END IF;

  SET o_action_status = v_action_status;

  IF v_action_status = 'applied' THEN
    SET o_gate_status = 'already_applied';
    LEAVE proc;
  END IF;

  IF v_action_status NOT IN ('pending', 'claimed') THEN
    SET o_gate_status = 'action_not_applyable';
    LEAVE proc;
  END IF;

  CALL mmo_ai_mark_npc_perception_action_applied(
    v_action_queue_id,
    COALESCE(NULLIF(p_worker_id, ''), 'step283_durable_mark_applied_gate'),
    v_gate_payload,
    o_action_status
  );

  IF o_action_status = 'applied' THEN
    SET o_gate_status = 'applied';
  ELSE
    SET o_gate_status = 'apply_failed';
  END IF;
END $$
DELIMITER ;

CREATE OR REPLACE VIEW v_step283_dialog_intent_mark_applied_gate_health AS
SELECT
  'step283_dialog_intent_mark_applied_gate' AS health_scope,
  COUNT(*) AS tracked_delivery_count,
  SUM(CASE WHEN d.action_queue_id IS NOT NULL THEN 1 ELSE 0 END) AS linked_action_delivery_count,
  SUM(CASE WHEN d.action_queue_id IS NULL THEN 1 ELSE 0 END) AS unlinked_diagnostic_delivery_count,
  SUM(CASE WHEN d.action_queue_id IS NOT NULL AND d.delivery_status = 'pending' THEN 1 ELSE 0 END) AS linked_pending_delivery_count,
  SUM(CASE WHEN d.action_queue_id IS NOT NULL AND d.delivery_status = 'acked' THEN 1 ELSE 0 END) AS linked_acked_delivery_count,
  SUM(CASE WHEN d.action_queue_id IS NOT NULL AND d.delivery_status = 'acked' AND q.action_status IN ('pending','claimed') THEN 1 ELSE 0 END) AS mark_applied_candidate_count,
  SUM(CASE WHEN d.action_queue_id IS NOT NULL AND d.delivery_status = 'acked' AND q.action_status = 'applied' THEN 1 ELSE 0 END) AS already_applied_count,
  SUM(CASE WHEN d.action_queue_id IS NOT NULL AND d.delivery_status IN ('nacked','timed_out','send_failed','dead_letter') THEN 1 ELSE 0 END) AS linked_terminal_not_success_count,
  SUM(CASE WHEN d.action_queue_id IS NOT NULL AND d.delivery_status = 'acked' AND EXISTS (
    SELECT 1 FROM gameplay_delivery_receipts r
     WHERE r.delivery_id = d.delivery_id
       AND r.receipt_kind IN ('observed','skipped')
       AND r.client_observation_status IN ('observed','skipped')
  ) THEN 1 ELSE 0 END) AS linked_acked_with_observation_count,
  SUM(CASE WHEN JSON_UNQUOTE(JSON_EXTRACT(d.request_payload, '$.source')) = 'step281_runtime_storage' THEN 1 ELSE 0 END) AS step281_delivery_count,
  SUM(CASE WHEN JSON_UNQUOTE(JSON_EXTRACT(d.request_payload, '$.source')) = 'step282_late_observer_replay_send' THEN 1 ELSE 0 END) AS step282_delivery_count
FROM gameplay_outbound_deliveries d
LEFT JOIN npc_perception_action_queue q ON q.action_queue_id = d.action_queue_id
WHERE d.gameplay_kind = 'npc_dialog_intent'
  AND JSON_UNQUOTE(JSON_EXTRACT(d.request_payload, '$.source')) IN ('step281_runtime_storage','step282_late_observer_replay_send','step283_smoke');

INSERT INTO ai_runtime_schema_versions(migration_key, checksum, description)
VALUES(
  'step283_ai_dialog_intent_durable_mark_applied_gate',
  '6f891a7d3d093c21c9991971e523a4fdf8fc0c1d8899645332b16c7087cc8d6d',
  'Step283 durable mark_applied gate for linked dialog-intent deliveries after ACK and observation evidence.'
)
ON DUPLICATE KEY UPDATE
  checksum = VALUES(checksum),
  description = VALUES(description),
  applied_at = CURRENT_TIMESTAMP(6);

-- ============================================================================
-- END server/sql/step283_ai_dialog_intent_durable_mark_applied_gate.sql
-- ============================================================================
