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
