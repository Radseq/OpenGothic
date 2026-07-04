CREATE TABLE IF NOT EXISTS mmo_npc_routine_state_current (
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(255) NOT NULL,
  routine_state VARCHAR(96) NOT NULL,
  schedule_key VARCHAR(255) DEFAULT NULL,
  current_waypoint_key VARCHAR(255) DEFAULT NULL,
  target_waypoint_key VARCHAR(255) DEFAULT NULL,
  last_event_id BINARY(16) DEFAULT NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(world_instance_id, npc_entity_key),
  KEY ix_mmo_npc_routine_tick(world_instance_id, last_server_tick),
  CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_routine_state_history (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) DEFAULT NULL,
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(255) NOT NULL,
  routine_state VARCHAR(96) NOT NULL,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  idempotency_key VARCHAR(255) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(id),
  UNIQUE KEY ux_mmo_npc_routine_history_idem(idempotency_key),
  KEY ix_mmo_npc_routine_history_key(world_instance_id, npc_entity_key, server_tick),
  CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_ai_state_current (
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(255) NOT NULL,
  ai_state VARCHAR(128) NOT NULL,
  ai_intent VARCHAR(128) DEFAULT NULL,
  target_key VARCHAR(255) DEFAULT NULL,
  perception_state VARCHAR(255) DEFAULT NULL,
  last_event_id BINARY(16) DEFAULT NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(world_instance_id, npc_entity_key),
  KEY ix_mmo_npc_ai_tick(world_instance_id, last_server_tick),
  KEY ix_mmo_npc_ai_target(world_instance_id, target_key),
  CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_ai_state_history (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) DEFAULT NULL,
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(255) NOT NULL,
  ai_state VARCHAR(128) NOT NULL,
  ai_intent VARCHAR(128) DEFAULT NULL,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  idempotency_key VARCHAR(255) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(id),
  UNIQUE KEY ux_mmo_npc_ai_history_idem(idempotency_key),
  KEY ix_mmo_npc_ai_history_key(world_instance_id, npc_entity_key, server_tick),
  CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_path_state_current (
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(255) NOT NULL,
  path_state VARCHAR(96) NOT NULL,
  route_key VARCHAR(255) DEFAULT NULL,
  current_waypoint_key VARCHAR(255) DEFAULT NULL,
  next_waypoint_key VARCHAR(255) DEFAULT NULL,
  target_waypoint_key VARCHAR(255) DEFAULT NULL,
  pos_x DOUBLE DEFAULT NULL,
  pos_y DOUBLE DEFAULT NULL,
  pos_z DOUBLE DEFAULT NULL,
  last_event_id BINARY(16) DEFAULT NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(world_instance_id, npc_entity_key),
  KEY ix_mmo_npc_path_tick(world_instance_id, last_server_tick),
  KEY ix_mmo_npc_path_target(world_instance_id, target_waypoint_key),
  CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_path_state_history (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) DEFAULT NULL,
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(255) NOT NULL,
  path_state VARCHAR(96) NOT NULL,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  idempotency_key VARCHAR(255) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(id),
  UNIQUE KEY ux_mmo_npc_path_history_idem(idempotency_key),
  KEY ix_mmo_npc_path_history_key(world_instance_id, npc_entity_key, server_tick),
  CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_fight_state_current (
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(255) NOT NULL,
  opponent_key VARCHAR(255) DEFAULT NULL,
  fight_state VARCHAR(96) NOT NULL,
  attack_state VARCHAR(96) DEFAULT NULL,
  combo_index INT NOT NULL DEFAULT 0,
  last_event_id BINARY(16) DEFAULT NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(world_instance_id, npc_entity_key),
  KEY ix_mmo_npc_fight_tick(world_instance_id, last_server_tick),
  KEY ix_mmo_npc_fight_opponent(world_instance_id, opponent_key),
  CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_fight_state_history (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) DEFAULT NULL,
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(255) NOT NULL,
  opponent_key VARCHAR(255) DEFAULT NULL,
  fight_state VARCHAR(96) NOT NULL,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  idempotency_key VARCHAR(255) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(id),
  UNIQUE KEY ux_mmo_npc_fight_history_idem(idempotency_key),
  KEY ix_mmo_npc_fight_history_key(world_instance_id, npc_entity_key, server_tick),
  CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DELIMITER ;;

DROP PROCEDURE IF EXISTS mmo_record_npc_routine_state;;
CREATE PROCEDURE mmo_record_npc_routine_state(
  IN p_session_id BINARY(16),
  IN p_npc_entity_key VARCHAR(255),
  IN p_routine_state VARCHAR(96),
  IN p_schedule_key VARCHAR(255),
  IN p_current_waypoint_key VARCHAR(255),
  IN p_target_waypoint_key VARCHAR(255),
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
  IF v_world_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_npc_routine_state: invalid session'; END IF;
  SET v_payload = JSON_MERGE_PATCH(COALESCE(p_metadata, JSON_OBJECT()), JSON_OBJECT('npc_entity_key',p_npc_entity_key,'routine_state',p_routine_state,'schedule_key',p_schedule_key,'current_waypoint_key',p_current_waypoint_key,'target_waypoint_key',p_target_waypoint_key));
  CALL mmo_append_world_event(v_realm_id, v_world_id, v_character_id, 'npc_routine_state_recorded', 'world_entity', COALESCE(p_server_tick,0), p_npc_entity_key, p_routine_state, v_payload, p_idempotency_key, 'server', NULL, NULL, p_event_id);
  INSERT INTO mmo_npc_routine_state_history(event_id, world_instance_id, npc_entity_key, routine_state, server_tick, state_payload, idempotency_key)
  VALUES(p_event_id, v_world_id, p_npc_entity_key, COALESCE(p_routine_state,'unknown'), COALESCE(p_server_tick,0), v_payload, p_idempotency_key)
  ON DUPLICATE KEY UPDATE event_id=VALUES(event_id), state_payload=VALUES(state_payload);
  INSERT INTO mmo_npc_routine_state_current(world_instance_id, npc_entity_key, routine_state, schedule_key, current_waypoint_key, target_waypoint_key, last_event_id, last_server_tick, state_payload, row_version)
  VALUES(v_world_id, p_npc_entity_key, COALESCE(p_routine_state,'unknown'), p_schedule_key, p_current_waypoint_key, p_target_waypoint_key, p_event_id, COALESCE(p_server_tick,0), v_payload, 1)
  ON DUPLICATE KEY UPDATE routine_state=VALUES(routine_state), schedule_key=VALUES(schedule_key), current_waypoint_key=VALUES(current_waypoint_key), target_waypoint_key=VALUES(target_waypoint_key), last_event_id=VALUES(last_event_id), last_server_tick=VALUES(last_server_tick), state_payload=VALUES(state_payload), row_version=row_version+1;
  SELECT row_version INTO p_row_version_after FROM mmo_npc_routine_state_current WHERE world_instance_id=v_world_id AND npc_entity_key=p_npc_entity_key;
END;;

DROP PROCEDURE IF EXISTS mmo_record_npc_ai_state;;
CREATE PROCEDURE mmo_record_npc_ai_state(
  IN p_session_id BINARY(16),
  IN p_npc_entity_key VARCHAR(255),
  IN p_ai_state VARCHAR(128),
  IN p_ai_intent VARCHAR(128),
  IN p_target_key VARCHAR(255),
  IN p_perception_state VARCHAR(255),
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
  IF v_world_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_npc_ai_state: invalid session'; END IF;
  SET v_payload = JSON_MERGE_PATCH(COALESCE(p_metadata, JSON_OBJECT()), JSON_OBJECT('npc_entity_key',p_npc_entity_key,'ai_state',p_ai_state,'ai_intent',p_ai_intent,'target_key',p_target_key,'perception_state',p_perception_state));
  CALL mmo_append_world_event(v_realm_id, v_world_id, v_character_id, 'npc_ai_state_recorded', 'world_entity', COALESCE(p_server_tick,0), p_npc_entity_key, p_ai_state, v_payload, p_idempotency_key, 'server', NULL, NULL, p_event_id);
  INSERT INTO mmo_npc_ai_state_history(event_id, world_instance_id, npc_entity_key, ai_state, ai_intent, server_tick, state_payload, idempotency_key)
  VALUES(p_event_id, v_world_id, p_npc_entity_key, COALESCE(p_ai_state,'unknown'), p_ai_intent, COALESCE(p_server_tick,0), v_payload, p_idempotency_key)
  ON DUPLICATE KEY UPDATE event_id=VALUES(event_id), state_payload=VALUES(state_payload);
  INSERT INTO mmo_npc_ai_state_current(world_instance_id, npc_entity_key, ai_state, ai_intent, target_key, perception_state, last_event_id, last_server_tick, state_payload, row_version)
  VALUES(v_world_id, p_npc_entity_key, COALESCE(p_ai_state,'unknown'), p_ai_intent, p_target_key, p_perception_state, p_event_id, COALESCE(p_server_tick,0), v_payload, 1)
  ON DUPLICATE KEY UPDATE ai_state=VALUES(ai_state), ai_intent=VALUES(ai_intent), target_key=VALUES(target_key), perception_state=VALUES(perception_state), last_event_id=VALUES(last_event_id), last_server_tick=VALUES(last_server_tick), state_payload=VALUES(state_payload), row_version=row_version+1;
  SELECT row_version INTO p_row_version_after FROM mmo_npc_ai_state_current WHERE world_instance_id=v_world_id AND npc_entity_key=p_npc_entity_key;
END;;

DROP PROCEDURE IF EXISTS mmo_record_npc_path_state;;
CREATE PROCEDURE mmo_record_npc_path_state(
  IN p_session_id BINARY(16),
  IN p_npc_entity_key VARCHAR(255),
  IN p_path_state VARCHAR(96),
  IN p_route_key VARCHAR(255),
  IN p_current_waypoint_key VARCHAR(255),
  IN p_next_waypoint_key VARCHAR(255),
  IN p_target_waypoint_key VARCHAR(255),
  IN p_pos_x DOUBLE,
  IN p_pos_y DOUBLE,
  IN p_pos_z DOUBLE,
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
  IF v_world_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_npc_path_state: invalid session'; END IF;
  SET v_payload = JSON_MERGE_PATCH(COALESCE(p_metadata, JSON_OBJECT()), JSON_OBJECT('npc_entity_key',p_npc_entity_key,'path_state',p_path_state,'route_key',p_route_key,'current_waypoint_key',p_current_waypoint_key,'next_waypoint_key',p_next_waypoint_key,'target_waypoint_key',p_target_waypoint_key,'pos_x',p_pos_x,'pos_y',p_pos_y,'pos_z',p_pos_z));
  CALL mmo_append_world_event(v_realm_id, v_world_id, v_character_id, 'npc_path_state_recorded', 'world_entity', COALESCE(p_server_tick,0), p_npc_entity_key, p_path_state, v_payload, p_idempotency_key, 'server', NULL, NULL, p_event_id);
  INSERT INTO mmo_npc_path_state_history(event_id, world_instance_id, npc_entity_key, path_state, server_tick, state_payload, idempotency_key)
  VALUES(p_event_id, v_world_id, p_npc_entity_key, COALESCE(p_path_state,'unknown'), COALESCE(p_server_tick,0), v_payload, p_idempotency_key)
  ON DUPLICATE KEY UPDATE event_id=VALUES(event_id), state_payload=VALUES(state_payload);
  INSERT INTO mmo_npc_path_state_current(world_instance_id, npc_entity_key, path_state, route_key, current_waypoint_key, next_waypoint_key, target_waypoint_key, pos_x, pos_y, pos_z, last_event_id, last_server_tick, state_payload, row_version)
  VALUES(v_world_id, p_npc_entity_key, COALESCE(p_path_state,'unknown'), p_route_key, p_current_waypoint_key, p_next_waypoint_key, p_target_waypoint_key, p_pos_x, p_pos_y, p_pos_z, p_event_id, COALESCE(p_server_tick,0), v_payload, 1)
  ON DUPLICATE KEY UPDATE path_state=VALUES(path_state), route_key=VALUES(route_key), current_waypoint_key=VALUES(current_waypoint_key), next_waypoint_key=VALUES(next_waypoint_key), target_waypoint_key=VALUES(target_waypoint_key), pos_x=VALUES(pos_x), pos_y=VALUES(pos_y), pos_z=VALUES(pos_z), last_event_id=VALUES(last_event_id), last_server_tick=VALUES(last_server_tick), state_payload=VALUES(state_payload), row_version=row_version+1;
  SELECT row_version INTO p_row_version_after FROM mmo_npc_path_state_current WHERE world_instance_id=v_world_id AND npc_entity_key=p_npc_entity_key;
END;;

DROP PROCEDURE IF EXISTS mmo_record_npc_fight_state;;
CREATE PROCEDURE mmo_record_npc_fight_state(
  IN p_session_id BINARY(16),
  IN p_npc_entity_key VARCHAR(255),
  IN p_opponent_key VARCHAR(255),
  IN p_fight_state VARCHAR(96),
  IN p_attack_state VARCHAR(96),
  IN p_combo_index INT,
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
  IF v_world_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_npc_fight_state: invalid session'; END IF;
  SET v_payload = JSON_MERGE_PATCH(COALESCE(p_metadata, JSON_OBJECT()), JSON_OBJECT('npc_entity_key',p_npc_entity_key,'opponent_key',p_opponent_key,'fight_state',p_fight_state,'attack_state',p_attack_state,'combo_index',COALESCE(p_combo_index,0)));
  CALL mmo_append_world_event(v_realm_id, v_world_id, v_character_id, 'npc_fight_state_recorded', 'combat', COALESCE(p_server_tick,0), p_npc_entity_key, p_opponent_key, v_payload, p_idempotency_key, 'server', NULL, NULL, p_event_id);
  INSERT INTO mmo_npc_fight_state_history(event_id, world_instance_id, npc_entity_key, opponent_key, fight_state, server_tick, state_payload, idempotency_key)
  VALUES(p_event_id, v_world_id, p_npc_entity_key, p_opponent_key, COALESCE(p_fight_state,'unknown'), COALESCE(p_server_tick,0), v_payload, p_idempotency_key)
  ON DUPLICATE KEY UPDATE event_id=VALUES(event_id), state_payload=VALUES(state_payload);
  INSERT INTO mmo_npc_fight_state_current(world_instance_id, npc_entity_key, opponent_key, fight_state, attack_state, combo_index, last_event_id, last_server_tick, state_payload, row_version)
  VALUES(v_world_id, p_npc_entity_key, p_opponent_key, COALESCE(p_fight_state,'unknown'), p_attack_state, COALESCE(p_combo_index,0), p_event_id, COALESCE(p_server_tick,0), v_payload, 1)
  ON DUPLICATE KEY UPDATE opponent_key=VALUES(opponent_key), fight_state=VALUES(fight_state), attack_state=VALUES(attack_state), combo_index=VALUES(combo_index), last_event_id=VALUES(last_event_id), last_server_tick=VALUES(last_server_tick), state_payload=VALUES(state_payload), row_version=row_version+1;
  SELECT row_version INTO p_row_version_after FROM mmo_npc_fight_state_current WHERE world_instance_id=v_world_id AND npc_entity_key=p_npc_entity_key;
END;;

DELIMITER ;
