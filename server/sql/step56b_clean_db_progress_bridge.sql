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
