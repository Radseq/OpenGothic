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
