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
