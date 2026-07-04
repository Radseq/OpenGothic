-- Step68: server-authoritative drop/loot inventory bridge.
-- Additive/idempotent dev MMO procedures for the live outbox worker.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_drop_character_item;
DROP PROCEDURE IF EXISTS mmo_loot_npc_inventory;

DELIMITER ;;

CREATE PROCEDURE mmo_drop_character_item(
  IN p_session_id BINARY(16),
  IN p_item_instance_id BINARY(16),
  IN p_amount_requested INT,
  IN p_world_item_entity_key VARCHAR(512),
  IN p_pos_x DOUBLE,
  IN p_pos_y DOUBLE,
  IN p_pos_z DOUBLE,
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_amount_remaining INT,
  OUT o_amount_dropped INT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_item_template_id BINARY(16);
  DECLARE v_target_item_instance_id BINARY(16);
  DECLARE v_old_item_key VARCHAR(191);
  DECLARE v_new_item_key VARCHAR(191);
  DECLARE v_world_item_entity_key VARCHAR(191);
  DECLARE v_current_amount INT DEFAULT 0;
  DECLARE v_amount INT DEFAULT 1;
  DECLARE v_payload JSON;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET o_event_id = NULL;
  SET o_amount_remaining = 0;
  SET o_amount_dropped = 0;
  SET v_world_item_entity_key = LEFT(COALESCE(NULLIF(p_world_item_entity_key, ''), CONCAT('world_item:drop:', UUID())), 191);

  SELECT event_id, amount
    INTO o_event_id, o_amount_dropped
    FROM world_item_audit
   WHERE idempotency_key = p_idempotency_key
     AND audit_type = 'drop'
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
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_drop_character_item: active session not found';
  END IF;

  START TRANSACTION;

  SELECT ci.amount, ii.item_template_id, ii.item_instance_key
    INTO v_current_amount, v_item_template_id, v_old_item_key
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
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_drop_character_item: character item not found';
  END IF;

  SET v_amount = GREATEST(1, LEAST(COALESCE(NULLIF(p_amount_requested, 0), v_current_amount), v_current_amount));
  SET o_amount_remaining = v_current_amount - v_amount;
  SET o_amount_dropped = v_amount;

  IF o_amount_remaining = 0 THEN
    SET v_target_item_instance_id = p_item_instance_id;

    DELETE FROM character_equipment
     WHERE character_id = v_character_id
       AND item_instance_id = p_item_instance_id;

    DELETE FROM character_inventory
     WHERE character_id = v_character_id
       AND item_instance_id = p_item_instance_id;

    UPDATE item_instances
       SET owner_type = 'world_entity',
           owner_id = NULL,
           quantity = v_amount,
           lifecycle_state = 'active',
           raw_payload = JSON_MERGE_PATCH(
             COALESCE(raw_payload, JSON_OBJECT()),
             JSON_OBJECT(
               'entity_key', v_world_item_entity_key,
               'dropped_by_character', BIN_TO_UUID(v_character_id, 1),
               'dropped_at_tick', COALESCE(p_server_tick, 0)
             )
           ),
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE item_instance_id = p_item_instance_id;
  ELSE
    SET v_target_item_instance_id = UUID_TO_BIN(UUID(), 1);
    SET v_new_item_key = CONCAT(LEFT(v_old_item_key, 140), ':drop:', REPLACE(UUID(), '-', ''));

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

    INSERT INTO item_instances(
      item_instance_id, realm_id, item_template_id, item_instance_key,
      owner_type, owner_id, quantity, bind_state, lifecycle_state, raw_payload
    )
    SELECT
      v_target_item_instance_id, realm_id, item_template_id, v_new_item_key,
      'world_entity', NULL, v_amount, bind_state, 'active',
      JSON_MERGE_PATCH(
        COALESCE(raw_payload, JSON_OBJECT()),
        JSON_OBJECT(
          'entity_key', v_world_item_entity_key,
          'split_from_item_instance_id', BIN_TO_UUID(p_item_instance_id, 1),
          'dropped_by_character', BIN_TO_UUID(v_character_id, 1),
          'dropped_at_tick', COALESCE(p_server_tick, 0)
        )
      )
      FROM item_instances
     WHERE item_instance_id = p_item_instance_id;
  END IF;

  INSERT INTO world_entity_state(
    world_instance_id, entity_key, entity_kind, lifecycle_state,
    pos_x, pos_y, pos_z, state_json, row_version
  )
  VALUES(
    v_world_instance_id, v_world_item_entity_key, 'item', 'active',
    p_pos_x, p_pos_y, p_pos_z,
    JSON_OBJECT(
      'exists_in_world', true,
      'item_instance_id', BIN_TO_UUID(v_target_item_instance_id, 1),
      'source_item_instance_id', BIN_TO_UUID(p_item_instance_id, 1),
      'dropped_by_character', BIN_TO_UUID(v_character_id, 1),
      'dropped_at_tick', COALESCE(p_server_tick, 0)
    ),
    1
  )
  ON DUPLICATE KEY UPDATE
    entity_kind = 'item',
    lifecycle_state = 'active',
    pos_x = VALUES(pos_x),
    pos_y = VALUES(pos_y),
    pos_z = VALUES(pos_z),
    state_json = JSON_MERGE_PATCH(COALESCE(world_entity_state.state_json, JSON_OBJECT()), VALUES(state_json)),
    row_version = COALESCE(world_entity_state.row_version, 0) + 1,
    updated_at = CURRENT_TIMESTAMP(6);

  INSERT INTO world_inventory(world_instance_id, owner_entity_key, item_instance_id, amount, source_amount, source_iterator_count)
  VALUES(v_world_instance_id, v_world_item_entity_key, v_target_item_instance_id, v_amount, v_amount, v_amount)
  ON DUPLICATE KEY UPDATE
    amount = VALUES(amount),
    source_amount = VALUES(source_amount),
    source_iterator_count = VALUES(source_iterator_count),
    updated_at = CURRENT_TIMESTAMP(6);

  SET v_payload = JSON_OBJECT(
    'world_item_entity_key', v_world_item_entity_key,
    'source_item_instance_id', BIN_TO_UUID(p_item_instance_id, 1),
    'item_instance_id', BIN_TO_UUID(v_target_item_instance_id, 1),
    'amount_dropped', v_amount,
    'amount_remaining', o_amount_remaining,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'character_item_dropped', 'inventory', COALESCE(p_server_tick, 0),
    v_world_item_entity_key, BIN_TO_UUID(v_target_item_instance_id, 1),
    v_payload, LEFT(p_idempotency_key, 191), 'server', NULL, NULL, o_event_id
  );

  INSERT INTO world_item_audit(event_id, world_instance_id, character_id, entity_key, item_instance_id, audit_type, amount, idempotency_key, metadata)
  VALUES(o_event_id, v_world_instance_id, v_character_id, v_world_item_entity_key, v_target_item_instance_id, 'drop', v_amount, p_idempotency_key, p_metadata);

  COMMIT;
END ;;

CREATE PROCEDURE mmo_loot_npc_inventory(
  IN p_session_id BINARY(16),
  IN p_source_entity_key VARCHAR(512),
  IN p_item_instance_id BINARY(16),
  IN p_amount_requested INT,
  IN p_bag_index INT,
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_source_amount_remaining INT,
  OUT o_amount_looted INT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_item_template_id BINARY(16);
  DECLARE v_target_item_instance_id BINARY(16);
  DECLARE v_source_entity_key VARCHAR(191);
  DECLARE v_old_item_key VARCHAR(191);
  DECLARE v_new_item_key VARCHAR(191);
  DECLARE v_current_amount INT DEFAULT 0;
  DECLARE v_amount INT DEFAULT 1;
  DECLARE v_payload JSON;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET o_event_id = NULL;
  SET o_source_amount_remaining = 0;
  SET o_amount_looted = 0;
  SET v_source_entity_key = LEFT(COALESCE(NULLIF(p_source_entity_key, ''), 'unknown:npc'), 191);

  SELECT event_id, amount
    INTO o_event_id, o_amount_looted
    FROM world_item_audit
   WHERE idempotency_key = p_idempotency_key
     AND audit_type = 'loot_npc_inventory'
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    SELECT COALESCE(wi.amount, 0)
      INTO o_source_amount_remaining
      FROM world_inventory wi
     WHERE wi.item_instance_id = p_item_instance_id
       AND wi.owner_entity_key = v_source_entity_key
     LIMIT 1;
    LEAVE proc;
  END IF;

  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_loot_npc_inventory: active session not found';
  END IF;

  START TRANSACTION;

  SELECT wi.amount, ii.item_template_id, ii.item_instance_key
    INTO v_current_amount, v_item_template_id, v_old_item_key
    FROM world_inventory wi
    JOIN item_instances ii ON ii.item_instance_id = wi.item_instance_id
   WHERE wi.world_instance_id = v_world_instance_id
     AND wi.owner_entity_key = v_source_entity_key
     AND wi.item_instance_id = p_item_instance_id
     AND ii.realm_id = v_realm_id
     AND ii.lifecycle_state = 'active'
   LIMIT 1
   FOR UPDATE;
  IF v_current_amount IS NULL OR v_current_amount <= 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_loot_npc_inventory: source inventory item not found';
  END IF;

  SET v_amount = GREATEST(1, LEAST(COALESCE(NULLIF(p_amount_requested, 0), v_current_amount), v_current_amount));
  SET o_source_amount_remaining = v_current_amount - v_amount;
  SET o_amount_looted = v_amount;

  IF o_source_amount_remaining = 0 THEN
    SET v_target_item_instance_id = p_item_instance_id;

    DELETE FROM world_inventory
     WHERE world_instance_id = v_world_instance_id
       AND owner_entity_key = v_source_entity_key
       AND item_instance_id = p_item_instance_id;

    UPDATE item_instances
       SET owner_type = 'character',
           owner_id = v_character_id,
           quantity = v_amount,
           lifecycle_state = 'active',
           raw_payload = JSON_MERGE_PATCH(
             COALESCE(raw_payload, JSON_OBJECT()),
             JSON_OBJECT(
               'looted_from_entity_key', v_source_entity_key,
               'looted_by_character', BIN_TO_UUID(v_character_id, 1),
               'looted_at_tick', COALESCE(p_server_tick, 0)
             )
           ),
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE item_instance_id = p_item_instance_id;
  ELSE
    SET v_target_item_instance_id = UUID_TO_BIN(UUID(), 1);
    SET v_new_item_key = CONCAT(LEFT(v_old_item_key, 140), ':loot:', REPLACE(UUID(), '-', ''));

    UPDATE world_inventory
       SET amount = o_source_amount_remaining,
           source_amount = o_source_amount_remaining,
           source_iterator_count = o_source_amount_remaining,
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE world_instance_id = v_world_instance_id
       AND owner_entity_key = v_source_entity_key
       AND item_instance_id = p_item_instance_id;

    UPDATE item_instances
       SET quantity = o_source_amount_remaining,
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE item_instance_id = p_item_instance_id;

    INSERT INTO item_instances(
      item_instance_id, realm_id, item_template_id, item_instance_key,
      owner_type, owner_id, quantity, bind_state, lifecycle_state, raw_payload
    )
    SELECT
      v_target_item_instance_id, realm_id, item_template_id, v_new_item_key,
      'character', v_character_id, v_amount, bind_state, 'active',
      JSON_MERGE_PATCH(
        COALESCE(raw_payload, JSON_OBJECT()),
        JSON_OBJECT(
          'split_from_item_instance_id', BIN_TO_UUID(p_item_instance_id, 1),
          'looted_from_entity_key', v_source_entity_key,
          'looted_by_character', BIN_TO_UUID(v_character_id, 1),
          'looted_at_tick', COALESCE(p_server_tick, 0)
        )
      )
      FROM item_instances
     WHERE item_instance_id = p_item_instance_id;
  END IF;

  INSERT INTO character_inventory(character_id, item_instance_id, bag_index, amount, source_amount, source_iterator_count)
  VALUES(v_character_id, v_target_item_instance_id, p_bag_index, v_amount, v_amount, v_amount);

  SET v_payload = JSON_OBJECT(
    'source_entity_key', v_source_entity_key,
    'source_item_instance_id', BIN_TO_UUID(p_item_instance_id, 1),
    'item_instance_id', BIN_TO_UUID(v_target_item_instance_id, 1),
    'amount_looted', v_amount,
    'source_amount_remaining', o_source_amount_remaining,
    'bag_index', p_bag_index,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'npc_inventory_looted', 'inventory', COALESCE(p_server_tick, 0),
    v_source_entity_key, BIN_TO_UUID(v_target_item_instance_id, 1),
    v_payload, LEFT(p_idempotency_key, 191), 'server', NULL, NULL, o_event_id
  );

  INSERT INTO world_item_audit(event_id, world_instance_id, character_id, entity_key, item_instance_id, audit_type, amount, idempotency_key, metadata)
  VALUES(o_event_id, v_world_instance_id, v_character_id, v_source_entity_key, v_target_item_instance_id, 'loot_npc_inventory', v_amount, p_idempotency_key, p_metadata);

  COMMIT;
END ;;

DELIMITER ;
