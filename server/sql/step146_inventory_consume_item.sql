SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';

DROP PROCEDURE IF EXISTS mmo_consume_character_item;

DELIMITER ;;
CREATE PROCEDURE mmo_consume_character_item(
  IN p_session_id BINARY(16),
  IN p_item_instance_id BINARY(16),
  IN p_amount_requested INT,
  IN p_reason VARCHAR(128),
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_amount_remaining INT,
  OUT o_amount_consumed INT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_current_amount INT DEFAULT NULL;
  DECLARE v_amount INT DEFAULT 1;
  DECLARE v_reason VARCHAR(128);
  DECLARE v_payload JSON;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET o_event_id = NULL;
  SET o_amount_remaining = 0;
  SET o_amount_consumed = 0;
  SET v_reason = LEFT(COALESCE(NULLIF(p_reason, ''), 'item_consumed'), 128);

  SELECT event_id, amount
    INTO o_event_id, o_amount_consumed
    FROM character_inventory_audit
   WHERE idempotency_key = p_idempotency_key
     AND audit_type = 'consume'
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
     AND ss.lifecycle_state = 'active'
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_consume_character_item: active session not found';
  END IF;

  START TRANSACTION;

  SELECT ci.amount
    INTO v_current_amount
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
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_consume_character_item: character item not found';
  END IF;

  SET v_amount = GREATEST(1, LEAST(COALESCE(NULLIF(p_amount_requested, 0), 1), v_current_amount));
  SET o_amount_consumed = v_amount;
  SET o_amount_remaining = v_current_amount - v_amount;

  IF o_amount_remaining = 0 THEN
    DELETE FROM character_equipment
     WHERE character_id = v_character_id
       AND item_instance_id = p_item_instance_id;

    DELETE FROM character_inventory
     WHERE character_id = v_character_id
       AND item_instance_id = p_item_instance_id;

    UPDATE item_instances
       SET quantity = 0,
           lifecycle_state = 'consumed',
           raw_payload = JSON_MERGE_PATCH(
             COALESCE(raw_payload, JSON_OBJECT()),
             JSON_OBJECT(
               'consumed_by_character', BIN_TO_UUID(v_character_id, 1),
               'consumed_at_tick', COALESCE(p_server_tick, 0),
               'consume_reason', v_reason
             )
           ),
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE item_instance_id = p_item_instance_id;
  ELSE
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
  END IF;

  SET v_payload = JSON_OBJECT(
    'item_instance_id', BIN_TO_UUID(p_item_instance_id, 1),
    'amount_consumed', o_amount_consumed,
    'amount_remaining', o_amount_remaining,
    'reason', v_reason,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'character_item_consumed', 'inventory', COALESCE(p_server_tick, 0),
    BIN_TO_UUID(p_item_instance_id, 1), v_reason,
    v_payload, p_idempotency_key, 'server', NULL, NULL, o_event_id
  );

  INSERT INTO character_inventory_audit(
    event_id, character_id, item_instance_id, audit_type, amount,
    equipment_slot, idempotency_key, metadata
  )
  VALUES(
    o_event_id, v_character_id, p_item_instance_id, 'consume', o_amount_consumed,
    NULL, p_idempotency_key, v_payload
  );

  COMMIT;
END;;
DELIMITER ;
