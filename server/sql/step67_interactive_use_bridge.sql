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
