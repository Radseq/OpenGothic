-- Gothic MMO MySQL production migration 008.
-- Server-owned quest/dialog/script/progression write path.
-- Requires 001..007 MySQL production migrations and a bootstrap import.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

-- -----------------------------------------------------------------------------
-- Character progress audit.
-- character_quests, character_known_dialogs, character_script_state and
-- character_stats are current-state projections. world_event_journal remains the
-- ordered durable mutation source.
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS character_progress_audit (
  progress_audit_id       BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  audit_type              VARCHAR(32) NOT NULL,
  session_id              BINARY(16) NULL,
  character_id            BINARY(16) NOT NULL,
  world_instance_id       BINARY(16) NOT NULL,
  event_id                BINARY(16) NOT NULL,
  idempotency_key         VARCHAR(191) NOT NULL,
  progress_key            VARCHAR(191) NOT NULL,
  secondary_key           VARCHAR(191) NULL,
  value_index             INT NULL,
  value_before_int        BIGINT NULL,
  value_after_int         BIGINT NULL,
  value_before_text       TEXT NULL,
  value_after_text        TEXT NULL,
  state_before            JSON NULL,
  state_after             JSON NULL,
  server_tick             BIGINT NOT NULL DEFAULT 0,
  raw_delta               JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at              TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY character_progress_audit_idempotency_uk(world_instance_id, idempotency_key),
  KEY ix_character_progress_audit_character(character_id, created_at),
  KEY ix_character_progress_audit_key(character_id, audit_type, progress_key, created_at),
  KEY ix_character_progress_audit_event(event_id),
  CONSTRAINT character_progress_audit_session_fk FOREIGN KEY(session_id) REFERENCES server_sessions(session_id) ON DELETE SET NULL,
  CONSTRAINT character_progress_audit_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT character_progress_audit_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE RESTRICT,
  CONSTRAINT character_progress_audit_event_fk FOREIGN KEY(event_id) REFERENCES world_event_journal(event_id) ON DELETE RESTRICT,
  CONSTRAINT character_progress_audit_type_ck CHECK(audit_type IN ('script_int','quest','dialog','progression')),
  CONSTRAINT character_progress_audit_index_ck CHECK(value_index IS NULL OR value_index >= 0),
  CONSTRAINT character_progress_audit_tick_ck CHECK(server_tick >= 0),
  CONSTRAINT character_progress_audit_state_before_json_ck CHECK(state_before IS NULL OR JSON_VALID(state_before)),
  CONSTRAINT character_progress_audit_state_after_json_ck CHECK(state_after IS NULL OR JSON_VALID(state_after)),
  CONSTRAINT character_progress_audit_raw_json_ck CHECK(JSON_VALID(raw_delta))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_set_character_script_int;
DELIMITER $$
CREATE PROCEDURE mmo_set_character_script_int(
  IN  p_session_id       BINARY(16),
  IN  p_script_key       VARCHAR(191),
  IN  p_symbol_index     INT,
  IN  p_value_index      INT,
  IN  p_value_int        BIGINT,
  IN  p_server_tick      BIGINT,
  IN  p_metadata         JSON,
  IN  p_idempotency_key  VARCHAR(191),
  OUT p_event_id         BINARY(16),
  OUT p_value_after      BIGINT
)
script_proc: BEGIN
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_old_value BIGINT DEFAULT NULL;
  DECLARE v_existing_audit_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_existing_event_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_after BIGINT DEFAULT NULL;
  DECLARE v_payload JSON;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET p_event_id = NULL;
  SET p_value_after = NULL;

  IF p_session_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'session id is required';
  END IF;
  IF p_script_key IS NULL OR TRIM(p_script_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'script key is required';
  END IF;
  IF p_value_index IS NULL OR p_value_index < 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'script value index must be non-negative';
  END IF;
  IF p_value_int IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'script int value is required';
  END IF;
  IF p_idempotency_key IS NULL OR TRIM(p_idempotency_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'script idempotency key is required';
  END IF;

  START TRANSACTION;

  SELECT ss.character_id, ss.realm_id, ss.world_instance_id, c.character_key
    INTO v_character_id, v_realm_id, v_world_instance_id, v_character_key
    FROM server_sessions ss
    JOIN characters c ON c.character_id = ss.character_id
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1
   FOR UPDATE;

  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'active session not found';
  END IF;

  SELECT audit_type, event_id, value_after_int
    INTO v_existing_audit_type, v_existing_event_id, v_existing_after
    FROM character_progress_audit
   WHERE world_instance_id = v_world_instance_id
     AND idempotency_key = p_idempotency_key
   LIMIT 1
   FOR UPDATE;

  IF v_existing_event_id IS NOT NULL THEN
    IF v_existing_audit_type <> 'script_int' THEN
      SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT = 'idempotency key reused with different progress audit type';
    END IF;
    SET p_event_id = v_existing_event_id;
    SET p_value_after = v_existing_after;
    COMMIT;
    LEAVE script_proc;
  END IF;

  SELECT value_int
    INTO v_old_value
    FROM character_script_state
   WHERE character_id = v_character_id
     AND script_key = p_script_key
     AND value_index = p_value_index
   LIMIT 1
   FOR UPDATE;

  SET v_payload = JSON_OBJECT(
    'character_key', v_character_key,
    'script_key', p_script_key,
    'symbol_index', p_symbol_index,
    'value_index', p_value_index,
    'old_value', v_old_value,
    'new_value', p_value_int,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_instance_id,
    v_character_id,
    'character_script_int_set',
    'script',
    COALESCE(p_server_tick, 0),
    NULL,
    p_script_key,
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  INSERT INTO character_script_state(
    character_id, script_key, symbol_index, value_type, value_index,
    value_int, value_real, value_text
  ) VALUES(
    v_character_id, p_script_key, p_symbol_index, 'int', p_value_index,
    p_value_int, NULL, NULL
  )
  ON DUPLICATE KEY UPDATE
    symbol_index = VALUES(symbol_index),
    value_type = 'int',
    value_int = VALUES(value_int),
    value_real = NULL,
    value_text = NULL,
    updated_at = CURRENT_TIMESTAMP(6);

  INSERT INTO character_progress_audit(
    audit_type, session_id, character_id, world_instance_id, event_id,
    idempotency_key, progress_key, value_index, value_before_int,
    value_after_int, server_tick, raw_delta
  ) VALUES(
    'script_int', p_session_id, v_character_id, v_world_instance_id, p_event_id,
    p_idempotency_key, p_script_key, p_value_index, v_old_value,
    p_value_int, COALESCE(p_server_tick, 0), v_payload
  );

  UPDATE realm_world_instances
     SET current_tick = GREATEST(current_tick, COALESCE(p_server_tick, 0)),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id;

  UPDATE server_sessions
     SET last_seen_at = CURRENT_TIMESTAMP(6),
         metadata = JSON_SET(COALESCE(metadata, JSON_OBJECT()), '$.last_progress_event', BIN_TO_UUID(p_event_id, 1))
   WHERE session_id = p_session_id;

  SET p_value_after = p_value_int;
  COMMIT;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_update_character_quest;
DELIMITER $$
CREATE PROCEDURE mmo_update_character_quest(
  IN  p_session_id       BINARY(16),
  IN  p_quest_key        VARCHAR(191),
  IN  p_section          VARCHAR(191),
  IN  p_status           VARCHAR(32),
  IN  p_entry_order      INT,
  IN  p_text_entries     JSON,
  IN  p_server_tick      BIGINT,
  IN  p_metadata         JSON,
  IN  p_idempotency_key  VARCHAR(191),
  OUT p_event_id         BINARY(16)
)
quest_proc: BEGIN
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_old_state JSON DEFAULT NULL;
  DECLARE v_new_state JSON DEFAULT NULL;
  DECLARE v_existing_audit_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_existing_event_id BINARY(16) DEFAULT NULL;
  DECLARE v_payload JSON;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET p_event_id = NULL;

  IF p_session_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'session id is required';
  END IF;
  IF p_quest_key IS NULL OR TRIM(p_quest_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'quest key is required';
  END IF;
  IF p_status NOT IN ('running','success','failed','obsolete') THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'invalid quest status';
  END IF;
  IF p_entry_order IS NULL OR p_entry_order < 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'quest entry order must be non-negative';
  END IF;
  IF p_idempotency_key IS NULL OR TRIM(p_idempotency_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'quest idempotency key is required';
  END IF;

  START TRANSACTION;

  SELECT ss.character_id, ss.realm_id, ss.world_instance_id, c.character_key
    INTO v_character_id, v_realm_id, v_world_instance_id, v_character_key
    FROM server_sessions ss
    JOIN characters c ON c.character_id = ss.character_id
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1
   FOR UPDATE;

  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'active session not found';
  END IF;

  SELECT audit_type, event_id
    INTO v_existing_audit_type, v_existing_event_id
    FROM character_progress_audit
   WHERE world_instance_id = v_world_instance_id
     AND idempotency_key = p_idempotency_key
   LIMIT 1
   FOR UPDATE;

  IF v_existing_event_id IS NOT NULL THEN
    IF v_existing_audit_type <> 'quest' THEN
      SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT = 'idempotency key reused with different progress audit type';
    END IF;
    SET p_event_id = v_existing_event_id;
    COMMIT;
    LEAVE quest_proc;
  END IF;

  SELECT JSON_OBJECT('section', section, 'status', status, 'entry_order', entry_order, 'text_entries', text_entries)
    INTO v_old_state
    FROM character_quests
   WHERE character_id = v_character_id
     AND quest_key = p_quest_key
   LIMIT 1
   FOR UPDATE;

  SET v_new_state = JSON_OBJECT(
    'section', COALESCE(p_section, ''),
    'status', p_status,
    'entry_order', p_entry_order,
    'text_entries', COALESCE(p_text_entries, JSON_ARRAY())
  );

  SET v_payload = JSON_OBJECT(
    'character_key', v_character_key,
    'quest_key', p_quest_key,
    'old_state', v_old_state,
    'new_state', v_new_state,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_instance_id,
    v_character_id,
    'character_quest_updated',
    'quest',
    COALESCE(p_server_tick, 0),
    NULL,
    p_quest_key,
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  INSERT INTO character_quests(character_id, quest_key, section, status, entry_order, text_entries)
  VALUES(v_character_id, p_quest_key, COALESCE(p_section, ''), p_status, p_entry_order, COALESCE(p_text_entries, JSON_ARRAY()))
  ON DUPLICATE KEY UPDATE
    section = VALUES(section),
    status = VALUES(status),
    entry_order = VALUES(entry_order),
    text_entries = VALUES(text_entries),
    updated_at = CURRENT_TIMESTAMP(6);

  INSERT INTO character_progress_audit(
    audit_type, session_id, character_id, world_instance_id, event_id,
    idempotency_key, progress_key, state_before, state_after,
    server_tick, raw_delta
  ) VALUES(
    'quest', p_session_id, v_character_id, v_world_instance_id, p_event_id,
    p_idempotency_key, p_quest_key, v_old_state, v_new_state,
    COALESCE(p_server_tick, 0), v_payload
  );

  UPDATE realm_world_instances
     SET current_tick = GREATEST(current_tick, COALESCE(p_server_tick, 0)),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id;

  UPDATE server_sessions
     SET last_seen_at = CURRENT_TIMESTAMP(6),
         metadata = JSON_SET(COALESCE(metadata, JSON_OBJECT()), '$.last_progress_event', BIN_TO_UUID(p_event_id, 1))
   WHERE session_id = p_session_id;

  COMMIT;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_set_character_known_dialog;
DELIMITER $$
CREATE PROCEDURE mmo_set_character_known_dialog(
  IN  p_session_id          BINARY(16),
  IN  p_npc_key             VARCHAR(191),
  IN  p_info_key            VARCHAR(191),
  IN  p_known               BOOLEAN,
  IN  p_permanent           BOOLEAN,
  IN  p_availability_state  VARCHAR(32),
  IN  p_server_tick         BIGINT,
  IN  p_metadata            JSON,
  IN  p_idempotency_key     VARCHAR(191),
  OUT p_event_id            BINARY(16)
)
dialog_proc: BEGIN
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_old_state JSON DEFAULT NULL;
  DECLARE v_new_state JSON DEFAULT NULL;
  DECLARE v_existing_audit_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_existing_event_id BINARY(16) DEFAULT NULL;
  DECLARE v_payload JSON;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET p_event_id = NULL;

  IF p_session_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'session id is required';
  END IF;
  IF p_npc_key IS NULL OR TRIM(p_npc_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'npc dialog key is required';
  END IF;
  IF p_info_key IS NULL OR TRIM(p_info_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'info dialog key is required';
  END IF;
  IF p_availability_state NOT IN ('unknown','visible','hidden','consumed_hidden','repeatable_known') THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'invalid dialog availability state';
  END IF;
  IF p_idempotency_key IS NULL OR TRIM(p_idempotency_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'dialog idempotency key is required';
  END IF;

  START TRANSACTION;

  SELECT ss.character_id, ss.realm_id, ss.world_instance_id, c.character_key
    INTO v_character_id, v_realm_id, v_world_instance_id, v_character_key
    FROM server_sessions ss
    JOIN characters c ON c.character_id = ss.character_id
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1
   FOR UPDATE;

  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'active session not found';
  END IF;

  SELECT audit_type, event_id
    INTO v_existing_audit_type, v_existing_event_id
    FROM character_progress_audit
   WHERE world_instance_id = v_world_instance_id
     AND idempotency_key = p_idempotency_key
   LIMIT 1
   FOR UPDATE;

  IF v_existing_event_id IS NOT NULL THEN
    IF v_existing_audit_type <> 'dialog' THEN
      SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT = 'idempotency key reused with different progress audit type';
    END IF;
    SET p_event_id = v_existing_event_id;
    COMMIT;
    LEAVE dialog_proc;
  END IF;

  SELECT JSON_OBJECT('known', known, 'permanent', permanent, 'availability_state', availability_state)
    INTO v_old_state
    FROM character_known_dialogs
   WHERE character_id = v_character_id
     AND npc_key = p_npc_key
     AND info_key = p_info_key
   LIMIT 1
   FOR UPDATE;

  SET v_new_state = JSON_OBJECT(
    'known', COALESCE(p_known, TRUE),
    'permanent', COALESCE(p_permanent, FALSE),
    'availability_state', p_availability_state
  );

  SET v_payload = JSON_OBJECT(
    'character_key', v_character_key,
    'npc_key', p_npc_key,
    'info_key', p_info_key,
    'old_state', v_old_state,
    'new_state', v_new_state,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_instance_id,
    v_character_id,
    'character_dialog_known_set',
    'dialog',
    COALESCE(p_server_tick, 0),
    p_npc_key,
    p_info_key,
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  INSERT INTO character_known_dialogs(character_id, npc_key, info_key, known, permanent, availability_state)
  VALUES(v_character_id, p_npc_key, p_info_key, COALESCE(p_known, TRUE), COALESCE(p_permanent, FALSE), p_availability_state)
  ON DUPLICATE KEY UPDATE
    known = VALUES(known),
    permanent = VALUES(permanent),
    availability_state = VALUES(availability_state),
    updated_at = CURRENT_TIMESTAMP(6);

  INSERT INTO character_progress_audit(
    audit_type, session_id, character_id, world_instance_id, event_id,
    idempotency_key, progress_key, secondary_key, state_before, state_after,
    server_tick, raw_delta
  ) VALUES(
    'dialog', p_session_id, v_character_id, v_world_instance_id, p_event_id,
    p_idempotency_key, p_npc_key, p_info_key, v_old_state, v_new_state,
    COALESCE(p_server_tick, 0), v_payload
  );

  UPDATE realm_world_instances
     SET current_tick = GREATEST(current_tick, COALESCE(p_server_tick, 0)),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id;

  UPDATE server_sessions
     SET last_seen_at = CURRENT_TIMESTAMP(6),
         metadata = JSON_SET(COALESCE(metadata, JSON_OBJECT()), '$.last_progress_event', BIN_TO_UUID(p_event_id, 1))
   WHERE session_id = p_session_id;

  COMMIT;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_adjust_character_progression;
DELIMITER $$
CREATE PROCEDURE mmo_adjust_character_progression(
  IN  p_session_id              BINARY(16),
  IN  p_experience_delta        BIGINT,
  IN  p_learning_points_delta   INT,
  IN  p_reason_key              VARCHAR(191),
  IN  p_server_tick             BIGINT,
  IN  p_metadata                JSON,
  IN  p_idempotency_key         VARCHAR(191),
  OUT p_event_id                BINARY(16),
  OUT p_experience_after        BIGINT,
  OUT p_learning_points_after   INT
)
progression_proc: BEGIN
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_exp_before BIGINT DEFAULT NULL;
  DECLARE v_lp_before INT DEFAULT NULL;
  DECLARE v_exp_after BIGINT DEFAULT NULL;
  DECLARE v_lp_after INT DEFAULT NULL;
  DECLARE v_existing_audit_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_existing_event_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_exp_after BIGINT DEFAULT NULL;
  DECLARE v_existing_lp_after BIGINT DEFAULT NULL;
  DECLARE v_payload JSON;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET p_event_id = NULL;
  SET p_experience_after = NULL;
  SET p_learning_points_after = NULL;

  IF p_session_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'session id is required';
  END IF;
  IF p_experience_delta IS NULL THEN
    SET p_experience_delta = 0;
  END IF;
  IF p_learning_points_delta IS NULL THEN
    SET p_learning_points_delta = 0;
  END IF;
  IF p_experience_delta = 0 AND p_learning_points_delta = 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'progression delta must not be zero';
  END IF;
  IF p_reason_key IS NULL OR TRIM(p_reason_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'progression reason key is required';
  END IF;
  IF p_idempotency_key IS NULL OR TRIM(p_idempotency_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'progression idempotency key is required';
  END IF;

  START TRANSACTION;

  SELECT ss.character_id, ss.realm_id, ss.world_instance_id, c.character_key
    INTO v_character_id, v_realm_id, v_world_instance_id, v_character_key
    FROM server_sessions ss
    JOIN characters c ON c.character_id = ss.character_id
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1
   FOR UPDATE;

  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'active session not found';
  END IF;

  SELECT audit_type, event_id, value_after_int, CAST(JSON_UNQUOTE(JSON_EXTRACT(state_after, '$.learning_points_after')) AS SIGNED)
    INTO v_existing_audit_type, v_existing_event_id, v_existing_exp_after, v_existing_lp_after
    FROM character_progress_audit
   WHERE world_instance_id = v_world_instance_id
     AND idempotency_key = p_idempotency_key
   LIMIT 1
   FOR UPDATE;

  IF v_existing_event_id IS NOT NULL THEN
    IF v_existing_audit_type <> 'progression' THEN
      SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT = 'idempotency key reused with different progress audit type';
    END IF;
    SET p_event_id = v_existing_event_id;
    SET p_experience_after = v_existing_exp_after;
    SET p_learning_points_after = CAST(v_existing_lp_after AS SIGNED);
    COMMIT;
    LEAVE progression_proc;
  END IF;

  SELECT experience, learning_points
    INTO v_exp_before, v_lp_before
    FROM character_stats
   WHERE character_id = v_character_id
   LIMIT 1
   FOR UPDATE;

  IF v_exp_before IS NULL OR v_lp_before IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'character stats not found';
  END IF;

  SET v_exp_after = v_exp_before + p_experience_delta;
  SET v_lp_after = v_lp_before + p_learning_points_delta;

  IF v_exp_after < 0 OR v_lp_after < 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'progression adjustment would make values negative';
  END IF;

  SET v_payload = JSON_OBJECT(
    'character_key', v_character_key,
    'reason_key', p_reason_key,
    'experience_before', v_exp_before,
    'experience_delta', p_experience_delta,
    'experience_after', v_exp_after,
    'learning_points_before', v_lp_before,
    'learning_points_delta', p_learning_points_delta,
    'learning_points_after', v_lp_after,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_instance_id,
    v_character_id,
    'character_progression_adjusted',
    'character',
    COALESCE(p_server_tick, 0),
    NULL,
    p_reason_key,
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  UPDATE character_stats
     SET experience = v_exp_after,
         learning_points = v_lp_after,
         row_version = row_version + 1,
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE character_id = v_character_id;

  INSERT INTO character_progress_audit(
    audit_type, session_id, character_id, world_instance_id, event_id,
    idempotency_key, progress_key, value_before_int, value_after_int,
    state_before, state_after, server_tick, raw_delta
  ) VALUES(
    'progression', p_session_id, v_character_id, v_world_instance_id, p_event_id,
    p_idempotency_key, p_reason_key, v_exp_before, v_exp_after,
    JSON_OBJECT('experience_before', v_exp_before, 'learning_points_before', v_lp_before),
    JSON_OBJECT('experience_after', v_exp_after, 'learning_points_after', v_lp_after),
    COALESCE(p_server_tick, 0), v_payload
  );

  UPDATE realm_world_instances
     SET current_tick = GREATEST(current_tick, COALESCE(p_server_tick, 0)),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id;

  UPDATE server_sessions
     SET last_seen_at = CURRENT_TIMESTAMP(6),
         metadata = JSON_SET(COALESCE(metadata, JSON_OBJECT()), '$.last_progress_event', BIN_TO_UUID(p_event_id, 1))
   WHERE session_id = p_session_id;

  SET p_experience_after = v_exp_after;
  SET p_learning_points_after = v_lp_after;
  COMMIT;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_apply_character_experience_reward;
DELIMITER $$
CREATE PROCEDURE mmo_apply_character_experience_reward(
  IN  p_session_id          BINARY(16),
  IN  p_experience_reward   BIGINT,
  IN  p_reason_key          VARCHAR(191),
  IN  p_server_tick         BIGINT,
  IN  p_metadata            JSON,
  IN  p_idempotency_key     VARCHAR(191),
  OUT p_event_id            BINARY(16),
  OUT p_experience_after    BIGINT
)
BEGIN
  DECLARE v_lp_after INT DEFAULT NULL;
  IF p_experience_reward IS NULL OR p_experience_reward <= 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'experience reward must be positive';
  END IF;
  CALL mmo_adjust_character_progression(
    p_session_id,
    p_experience_reward,
    0,
    p_reason_key,
    p_server_tick,
    p_metadata,
    p_idempotency_key,
    p_event_id,
    p_experience_after,
    v_lp_after
  );
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_character_script_progress AS
SELECT
  BIN_TO_UUID(c.character_id, 1) AS character_id,
  c.character_key,
  css.script_key,
  css.symbol_index,
  css.value_type,
  css.value_index,
  css.value_int,
  css.value_real,
  css.value_text,
  css.updated_at
FROM character_script_state css
JOIN characters c ON c.character_id = css.character_id;

CREATE OR REPLACE VIEW v_character_quest_progress AS
SELECT
  BIN_TO_UUID(c.character_id, 1) AS character_id,
  c.character_key,
  cq.quest_key,
  cq.section,
  cq.status,
  cq.entry_order,
  cq.text_entries,
  cq.updated_at
FROM character_quests cq
JOIN characters c ON c.character_id = cq.character_id;

CREATE OR REPLACE VIEW v_character_dialog_progress AS
SELECT
  BIN_TO_UUID(c.character_id, 1) AS character_id,
  c.character_key,
  ckd.npc_key,
  ckd.info_key,
  ckd.known,
  ckd.permanent,
  ckd.availability_state,
  ckd.updated_at
FROM character_known_dialogs ckd
JOIN characters c ON c.character_id = ckd.character_id;

CREATE OR REPLACE VIEW v_character_progress_audit AS
SELECT
  BIN_TO_UUID(a.progress_audit_id, 1) AS progress_audit_id,
  a.audit_type,
  BIN_TO_UUID(a.session_id, 1) AS session_id,
  BIN_TO_UUID(a.character_id, 1) AS character_id,
  c.character_key,
  BIN_TO_UUID(a.world_instance_id, 1) AS world_instance_id,
  BIN_TO_UUID(a.event_id, 1) AS event_id,
  a.idempotency_key,
  a.progress_key,
  a.secondary_key,
  a.value_index,
  a.value_before_int,
  a.value_after_int,
  a.server_tick,
  a.created_at
FROM character_progress_audit a
JOIN characters c ON c.character_id = a.character_id;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES(
  'production/mysql/008_character_progress_write_path',
  'gothic-mmo-character-progress-write-path-v1-mysql',
  'Adds server-owned quest/dialog/script/progression mutation procedures with idempotent audit.'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);
