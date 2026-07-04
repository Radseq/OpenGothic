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
