-- Step 148: server-authoritative NPC trade inventory bridge.
-- Mirrors Gothic Npc::buyItem / Npc::sellItem semantics at the server boundary:
-- item stacks move between character_inventory and the NPC-owned world_inventory,
-- while Gothic gold is represented by the server wallet bridge currency key.

DROP PROCEDURE IF EXISTS mmo_trade_sell_to_npc;
DELIMITER ;;
CREATE PROCEDURE mmo_trade_sell_to_npc(
  IN p_session_id BINARY(16),
  IN p_npc_entity_key VARCHAR(512),
  IN p_item_instance_id BINARY(16),
  IN p_price_total BIGINT,
  IN p_currency_key VARCHAR(128),
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_wallet_after DECIMAL(20,0)
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_current_amount INT DEFAULT NULL;
  DECLARE v_amount INT DEFAULT 1;
  DECLARE v_amount_requested BIGINT DEFAULT 1;
  DECLARE v_remaining INT DEFAULT 0;
  DECLARE v_item_template_id BINARY(16);
  DECLARE v_old_item_key VARCHAR(191);
  DECLARE v_new_item_key VARCHAR(191);
  DECLARE v_world_item_instance_id BINARY(16);
  DECLARE v_wallet_before DECIMAL(20,0) DEFAULT 0;
  DECLARE v_payload JSON;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET o_event_id = NULL;
  SET o_wallet_after = NULL;

  SELECT cia.event_id, cia.character_id
    INTO o_event_id, v_character_id
    FROM character_inventory_audit cia
   WHERE cia.idempotency_key = p_idempotency_key
     AND cia.audit_type = 'trade_sell_to_npc'
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    SELECT COALESCE(cw.amount, 0)
      INTO o_wallet_after
      FROM character_wallets cw
     WHERE cw.character_id = v_character_id
       AND cw.currency_key = p_currency_key
     LIMIT 1;
    SET o_wallet_after = COALESCE(o_wallet_after, 0);
    LEAVE proc;
  END IF;

  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_trade_sell_to_npc: active session not found';
  END IF;

  SET v_amount_requested = COALESCE(
    CAST(NULLIF(JSON_UNQUOTE(JSON_EXTRACT(p_metadata, '$.amount')), '') AS SIGNED),
    CAST(NULLIF(JSON_UNQUOTE(JSON_EXTRACT(p_metadata, '$.client_payload.amount')), '') AS SIGNED),
    1
  );

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
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_trade_sell_to_npc: character item not found';
  END IF;

  SET v_amount = GREATEST(1, LEAST(COALESCE(NULLIF(v_amount_requested, 0), v_current_amount), v_current_amount));
  SET v_remaining = v_current_amount - v_amount;

  INSERT INTO character_wallets(character_id, currency_key, amount)
  VALUES(v_character_id, p_currency_key, 0)
  ON DUPLICATE KEY UPDATE amount = amount;

  SELECT cw.amount
    INTO v_wallet_before
    FROM character_wallets cw
   WHERE cw.character_id = v_character_id
     AND cw.currency_key = p_currency_key
   LIMIT 1
   FOR UPDATE;

  IF v_wallet_before + p_price_total < 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_trade_sell_to_npc: wallet would become negative';
  END IF;
  SET o_wallet_after = v_wallet_before + p_price_total;

  UPDATE character_wallets
     SET amount = o_wallet_after,
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE character_id = v_character_id
     AND currency_key = p_currency_key;

  IF v_remaining = 0 THEN
    SET v_world_item_instance_id = p_item_instance_id;

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
               'trade_sold_to_npc_entity_key', p_npc_entity_key,
               'trade_sold_by_character', BIN_TO_UUID(v_character_id, 1),
               'trade_price_total', p_price_total,
               'trade_currency_key', p_currency_key,
               'trade_tick', COALESCE(p_server_tick, 0)
             )
           ),
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE item_instance_id = p_item_instance_id;
  ELSE
    SET v_world_item_instance_id = UUID_TO_BIN(UUID(), 1);
    SET v_new_item_key = LEFT(CONCAT(v_old_item_key, ':trade-sell:', REPLACE(UUID(), '-', '')), 191);

    UPDATE character_inventory
       SET amount = v_remaining,
           source_amount = v_remaining,
           source_iterator_count = v_remaining,
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE character_id = v_character_id
       AND item_instance_id = p_item_instance_id;

    UPDATE item_instances
       SET quantity = v_remaining,
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE item_instance_id = p_item_instance_id;

    INSERT INTO item_instances(
      item_instance_id, realm_id, item_template_id, item_instance_key,
      owner_type, owner_id, quantity, bind_state, lifecycle_state, raw_payload
    )
    SELECT
      v_world_item_instance_id, realm_id, item_template_id, v_new_item_key,
      'world_entity', NULL, v_amount, bind_state, 'active',
      JSON_MERGE_PATCH(
        COALESCE(raw_payload, JSON_OBJECT()),
        JSON_OBJECT(
          'split_from_item_instance_id', BIN_TO_UUID(p_item_instance_id, 1),
          'trade_sold_to_npc_entity_key', p_npc_entity_key,
          'trade_sold_by_character', BIN_TO_UUID(v_character_id, 1),
          'trade_price_total', p_price_total,
          'trade_currency_key', p_currency_key,
          'trade_tick', COALESCE(p_server_tick, 0)
        )
      )
      FROM item_instances
     WHERE item_instance_id = p_item_instance_id;
  END IF;

  INSERT INTO world_inventory(world_instance_id, owner_entity_key, item_instance_id, amount, source_amount, source_iterator_count)
  VALUES(v_world_instance_id, p_npc_entity_key, v_world_item_instance_id, v_amount, v_amount, v_amount)
  ON DUPLICATE KEY UPDATE
    amount = world_inventory.amount + VALUES(amount),
    source_amount = COALESCE(world_inventory.source_amount, 0) + VALUES(source_amount),
    source_iterator_count = COALESCE(world_inventory.source_iterator_count, 0) + VALUES(source_iterator_count),
    updated_at = CURRENT_TIMESTAMP(6);

  SET v_payload = JSON_OBJECT(
    'npc_entity_key', p_npc_entity_key,
    'source_item_instance_id', BIN_TO_UUID(p_item_instance_id, 1),
    'world_item_instance_id', BIN_TO_UUID(v_world_item_instance_id, 1),
    'amount', v_amount,
    'price_total', p_price_total,
    'currency_key', p_currency_key,
    'wallet_before', v_wallet_before,
    'wallet_after', o_wallet_after,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'trade_sell_to_npc', 'trade', COALESCE(p_server_tick, 0),
    p_npc_entity_key, BIN_TO_UUID(v_world_item_instance_id, 1),
    v_payload, LEFT(p_idempotency_key, 191), 'server', NULL, NULL, o_event_id
  );

  INSERT INTO character_inventory_audit(
    event_id, character_id, item_instance_id, audit_type, amount, idempotency_key, metadata
  )
  VALUES(o_event_id, v_character_id, v_world_item_instance_id, 'trade_sell_to_npc', v_amount, p_idempotency_key, p_metadata);

  INSERT INTO world_item_audit(
    event_id, world_instance_id, character_id, entity_key, item_instance_id, audit_type, amount, idempotency_key, metadata
  )
  VALUES(o_event_id, v_world_instance_id, v_character_id, p_npc_entity_key, v_world_item_instance_id,
         'trade_sell_to_npc', v_amount, LEFT(CONCAT(p_idempotency_key, ':world'), 512), p_metadata);

  COMMIT;
END ;;
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_trade_buy_from_npc;
DELIMITER ;;
CREATE PROCEDURE mmo_trade_buy_from_npc(
  IN p_session_id BINARY(16),
  IN p_npc_entity_key VARCHAR(512),
  IN p_item_instance_id BINARY(16),
  IN p_price_total BIGINT,
  IN p_currency_key VARCHAR(128),
  IN p_target_bag_index INT,
  IN p_server_tick BIGINT UNSIGNED,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_wallet_after DECIMAL(20,0),
  OUT o_bag_index INT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_current_amount INT DEFAULT NULL;
  DECLARE v_amount INT DEFAULT 1;
  DECLARE v_amount_requested BIGINT DEFAULT 1;
  DECLARE v_remaining INT DEFAULT 0;
  DECLARE v_old_item_key VARCHAR(191);
  DECLARE v_new_item_key VARCHAR(191);
  DECLARE v_character_item_instance_id BINARY(16);
  DECLARE v_wallet_before DECIMAL(20,0) DEFAULT 0;
  DECLARE v_payload JSON;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET o_event_id = NULL;
  SET o_wallet_after = NULL;
  SET o_bag_index = NULL;

  SELECT cia.event_id, cia.character_id
    INTO o_event_id, v_character_id
    FROM character_inventory_audit cia
   WHERE cia.idempotency_key = p_idempotency_key
     AND cia.audit_type = 'trade_buy_from_npc'
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    SELECT COALESCE(cw.amount, 0)
      INTO o_wallet_after
      FROM character_wallets cw
     WHERE cw.character_id = v_character_id
       AND cw.currency_key = p_currency_key
     LIMIT 1;
    SELECT ci.bag_index
      INTO o_bag_index
      FROM character_inventory ci
      JOIN character_inventory_audit cia ON cia.item_instance_id = ci.item_instance_id
     WHERE cia.idempotency_key = p_idempotency_key
       AND cia.audit_type = 'trade_buy_from_npc'
     LIMIT 1;
    SET o_wallet_after = COALESCE(o_wallet_after, 0);
    LEAVE proc;
  END IF;

  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_trade_buy_from_npc: active session not found';
  END IF;

  SET v_amount_requested = COALESCE(
    CAST(NULLIF(JSON_UNQUOTE(JSON_EXTRACT(p_metadata, '$.amount')), '') AS SIGNED),
    CAST(NULLIF(JSON_UNQUOTE(JSON_EXTRACT(p_metadata, '$.client_payload.amount')), '') AS SIGNED),
    1
  );

  START TRANSACTION;

  SELECT wi.amount, ii.item_instance_key
    INTO v_current_amount, v_old_item_key
    FROM world_inventory wi
    JOIN item_instances ii ON ii.item_instance_id = wi.item_instance_id
   WHERE wi.world_instance_id = v_world_instance_id
     AND wi.owner_entity_key = p_npc_entity_key
     AND wi.item_instance_id = p_item_instance_id
     AND ii.realm_id = v_realm_id
     AND ii.lifecycle_state = 'active'
   LIMIT 1
   FOR UPDATE;
  IF v_current_amount IS NULL OR v_current_amount <= 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_trade_buy_from_npc: npc item not found';
  END IF;

  SET v_amount = GREATEST(1, LEAST(COALESCE(NULLIF(v_amount_requested, 0), v_current_amount), v_current_amount));
  SET v_remaining = v_current_amount - v_amount;
  SET o_bag_index = COALESCE(
    p_target_bag_index,
    (SELECT COALESCE(MAX(ci.bag_index), -1) + 1 FROM character_inventory ci WHERE ci.character_id = v_character_id)
  );

  INSERT INTO character_wallets(character_id, currency_key, amount)
  VALUES(v_character_id, p_currency_key, 0)
  ON DUPLICATE KEY UPDATE amount = amount;

  SELECT cw.amount
    INTO v_wallet_before
    FROM character_wallets cw
   WHERE cw.character_id = v_character_id
     AND cw.currency_key = p_currency_key
   LIMIT 1
   FOR UPDATE;

  IF v_wallet_before - p_price_total < 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_trade_buy_from_npc: insufficient wallet';
  END IF;
  SET o_wallet_after = v_wallet_before - p_price_total;

  UPDATE character_wallets
     SET amount = o_wallet_after,
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE character_id = v_character_id
     AND currency_key = p_currency_key;

  IF v_remaining = 0 THEN
    SET v_character_item_instance_id = p_item_instance_id;

    DELETE FROM world_inventory
     WHERE world_instance_id = v_world_instance_id
       AND owner_entity_key = p_npc_entity_key
       AND item_instance_id = p_item_instance_id;

    UPDATE item_instances
       SET owner_type = 'character',
           owner_id = v_character_id,
           quantity = v_amount,
           lifecycle_state = 'active',
           raw_payload = JSON_MERGE_PATCH(
             COALESCE(raw_payload, JSON_OBJECT()),
             JSON_OBJECT(
               'trade_bought_from_npc_entity_key', p_npc_entity_key,
               'trade_bought_by_character', BIN_TO_UUID(v_character_id, 1),
               'trade_price_total', p_price_total,
               'trade_currency_key', p_currency_key,
               'trade_tick', COALESCE(p_server_tick, 0)
             )
           ),
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE item_instance_id = p_item_instance_id;
  ELSE
    SET v_character_item_instance_id = UUID_TO_BIN(UUID(), 1);
    SET v_new_item_key = LEFT(CONCAT(v_old_item_key, ':trade-buy:', REPLACE(UUID(), '-', '')), 191);

    UPDATE world_inventory
       SET amount = v_remaining,
           source_amount = v_remaining,
           source_iterator_count = v_remaining,
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE world_instance_id = v_world_instance_id
       AND owner_entity_key = p_npc_entity_key
       AND item_instance_id = p_item_instance_id;

    UPDATE item_instances
       SET quantity = v_remaining,
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE item_instance_id = p_item_instance_id;

    INSERT INTO item_instances(
      item_instance_id, realm_id, item_template_id, item_instance_key,
      owner_type, owner_id, quantity, bind_state, lifecycle_state, raw_payload
    )
    SELECT
      v_character_item_instance_id, realm_id, item_template_id, v_new_item_key,
      'character', v_character_id, v_amount, bind_state, 'active',
      JSON_MERGE_PATCH(
        COALESCE(raw_payload, JSON_OBJECT()),
        JSON_OBJECT(
          'split_from_item_instance_id', BIN_TO_UUID(p_item_instance_id, 1),
          'trade_bought_from_npc_entity_key', p_npc_entity_key,
          'trade_bought_by_character', BIN_TO_UUID(v_character_id, 1),
          'trade_price_total', p_price_total,
          'trade_currency_key', p_currency_key,
          'trade_tick', COALESCE(p_server_tick, 0)
        )
      )
      FROM item_instances
     WHERE item_instance_id = p_item_instance_id;
  END IF;

  INSERT INTO character_inventory(character_id, item_instance_id, bag_index, amount, source_amount, source_iterator_count)
  VALUES(v_character_id, v_character_item_instance_id, o_bag_index, v_amount, v_amount, v_amount);

  SET v_payload = JSON_OBJECT(
    'npc_entity_key', p_npc_entity_key,
    'source_item_instance_id', BIN_TO_UUID(p_item_instance_id, 1),
    'item_instance_id', BIN_TO_UUID(v_character_item_instance_id, 1),
    'amount', v_amount,
    'price_total', p_price_total,
    'currency_key', p_currency_key,
    'wallet_before', v_wallet_before,
    'wallet_after', o_wallet_after,
    'bag_index', o_bag_index,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'trade_buy_from_npc', 'trade', COALESCE(p_server_tick, 0),
    p_npc_entity_key, BIN_TO_UUID(v_character_item_instance_id, 1),
    v_payload, LEFT(p_idempotency_key, 191), 'server', NULL, NULL, o_event_id
  );

  INSERT INTO character_inventory_audit(
    event_id, character_id, item_instance_id, audit_type, amount, idempotency_key, metadata
  )
  VALUES(o_event_id, v_character_id, v_character_item_instance_id, 'trade_buy_from_npc', v_amount, p_idempotency_key, p_metadata);

  INSERT INTO world_item_audit(
    event_id, world_instance_id, character_id, entity_key, item_instance_id, audit_type, amount, idempotency_key, metadata
  )
  VALUES(o_event_id, v_world_instance_id, v_character_id, p_npc_entity_key, p_item_instance_id,
         'trade_buy_from_npc', v_amount, LEFT(CONCAT(p_idempotency_key, ':world'), 512), p_metadata);

  COMMIT;
END ;;
DELIMITER ;
