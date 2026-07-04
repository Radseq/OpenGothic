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
