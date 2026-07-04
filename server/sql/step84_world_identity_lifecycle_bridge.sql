-- Step84 world identity/lifecycle bridge for direct C++ ASIO server.
-- Adds a safe grant fallback for locally-created/unresolved world pickups.

DELIMITER $$

DROP PROCEDURE IF EXISTS mmo_grant_character_item_by_symbol $$
CREATE PROCEDURE mmo_grant_character_item_by_symbol(
  IN p_session_id BINARY(16),
  IN p_item_symbol INT,
  IN p_amount_requested INT,
  IN p_bag_index INT,
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_item_instance_id BINARY(16),
  OUT o_amount_granted INT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_content_revision_id BINARY(16) DEFAULT NULL;
  DECLARE v_item_template_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_item_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_amount INT DEFAULT 1;
  DECLARE v_bag_index INT DEFAULT NULL;
  DECLARE v_item_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_payload JSON;
  DECLARE v_not_found BOOL DEFAULT FALSE;

  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET o_event_id = NULL;
  SET o_item_instance_id = NULL;
  SET o_amount_granted = 0;

  SELECT cia.event_id, cia.item_instance_id, cia.amount
    INTO o_event_id, o_item_instance_id, o_amount_granted
    FROM character_inventory_audit cia
   WHERE cia.idempotency_key = LEFT(p_idempotency_key, 512)
     AND cia.audit_type = 'grant_unresolved_world_pickup'
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    LEAVE proc;
  END IF;

  SET v_not_found = FALSE;
  SELECT ss.realm_id, ss.world_instance_id, ss.character_id, c.character_key, rr.active_content_revision_id
    INTO v_realm_id, v_world_instance_id, v_character_id, v_character_key, v_content_revision_id
    FROM server_sessions ss
    JOIN characters c ON c.character_id = ss.character_id
    JOIN realm_realms rr ON rr.realm_id = ss.realm_id
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_grant_character_item_by_symbol: active session not found';
  END IF;

  IF COALESCE(p_item_symbol, -1) < 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_grant_character_item_by_symbol: invalid item symbol';
  END IF;

  SET v_not_found = FALSE;
  SELECT cit.item_template_id
    INTO v_item_template_id
    FROM content_item_templates cit
   WHERE cit.content_revision_id = v_content_revision_id
     AND cit.symbol_index = p_item_symbol
   ORDER BY cit.item_template_key
   LIMIT 1;
  IF v_item_template_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_grant_character_item_by_symbol: item template not found';
  END IF;

  SET v_amount = GREATEST(1, COALESCE(NULLIF(p_amount_requested, 0), 1));

  START TRANSACTION;

  SET v_not_found = FALSE;
  SELECT ii.item_instance_id
    INTO v_existing_item_instance_id
    FROM character_inventory ci
    JOIN item_instances ii ON ii.item_instance_id = ci.item_instance_id
   WHERE ci.character_id = v_character_id
     AND ii.item_template_id = v_item_template_id
     AND ii.owner_type = 'character'
     AND ii.owner_id = v_character_id
     AND ii.lifecycle_state = 'active'
   ORDER BY ci.updated_at DESC
   LIMIT 1
   FOR UPDATE;

  IF v_existing_item_instance_id IS NOT NULL THEN
    SET o_item_instance_id = v_existing_item_instance_id;

    UPDATE character_inventory
       SET amount = amount + v_amount,
           source_amount = COALESCE(source_amount, 0) + v_amount,
           source_iterator_count = COALESCE(source_iterator_count, 0) + v_amount,
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE character_id = v_character_id
       AND item_instance_id = o_item_instance_id;

    UPDATE item_instances
       SET quantity = quantity + v_amount,
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE item_instance_id = o_item_instance_id;
  ELSE
    SELECT COALESCE(MAX(ci.bag_index), -1) + 1
      INTO v_bag_index
      FROM character_inventory ci
     WHERE ci.character_id = v_character_id;

    IF p_bag_index IS NOT NULL AND p_bag_index >= 0 THEN
      IF NOT EXISTS (
        SELECT 1 FROM character_inventory ci
         WHERE ci.character_id = v_character_id
           AND ci.bag_index = p_bag_index
         LIMIT 1
      ) THEN
        SET v_bag_index = p_bag_index;
      END IF;
    END IF;

    SET o_item_instance_id = UUID_TO_BIN(UUID(), 1);
    SET v_item_key = LEFT(CONCAT('character_item:', v_character_key, ':sym:', p_item_symbol, ':grant:', REPLACE(UUID(), '-', '')), 191);

    INSERT INTO item_instances(
      item_instance_id, realm_id, item_template_id, item_instance_key,
      owner_type, owner_id, quantity, bind_state, lifecycle_state, raw_payload
    ) VALUES(
      o_item_instance_id, v_realm_id, v_item_template_id, v_item_key,
      'character', v_character_id, v_amount, 'unbound', 'active',
      JSON_OBJECT(
        'grant_reason', 'unresolved_world_pickup',
        'item_symbol', p_item_symbol,
        'granted_at_tick', COALESCE(p_server_tick, 0),
        'metadata', COALESCE(p_metadata, JSON_OBJECT())
      )
    );

    INSERT INTO character_inventory(character_id, item_instance_id, bag_index, amount, source_amount, source_iterator_count)
    VALUES(v_character_id, o_item_instance_id, v_bag_index, v_amount, v_amount, v_amount);
  END IF;

  SET v_payload = JSON_OBJECT(
    'item_symbol', p_item_symbol,
    'item_instance_id', BIN_TO_UUID(o_item_instance_id, 1),
    'amount', v_amount,
    'bag_index', COALESCE(v_bag_index, p_bag_index),
    'reason', 'unresolved_world_pickup',
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'character_item_granted_from_unresolved_world_pickup', 'inventory', COALESCE(p_server_tick, 0),
    v_character_key, BIN_TO_UUID(o_item_instance_id, 1), v_payload,
    LEFT(p_idempotency_key, 191), 'server', NULL, NULL, o_event_id
  );

  INSERT INTO character_inventory_audit(event_id, character_id, item_instance_id, audit_type, amount, idempotency_key, metadata)
  VALUES(o_event_id, v_character_id, o_item_instance_id, 'grant_unresolved_world_pickup', v_amount, LEFT(p_idempotency_key, 512), p_metadata);

  COMMIT;
  SET o_amount_granted = v_amount;
END $$

DELIMITER ;
