-- Gothic MMO MySQL production migration 011.
-- Server-owned trade/economy write path.
-- Requires 001..010 MySQL production migrations and a bootstrap import.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE IF NOT EXISTS npc_trade_inventory (
  world_instance_id      BINARY(16) NOT NULL,
  npc_entity_key         VARCHAR(191) NOT NULL,
  item_instance_id       BINARY(16) NOT NULL,
  amount                 INT NOT NULL DEFAULT 1,
  unit_price             DECIMAL(20,0) NOT NULL DEFAULT 0,
  currency_key           VARCHAR(128) NOT NULL DEFAULT 'g2notr:gold',
  stock_state            VARCHAR(32) NOT NULL DEFAULT 'available',
  raw_payload            JSON NOT NULL DEFAULT (JSON_OBJECT()),
  updated_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(world_instance_id, npc_entity_key, item_instance_id),
  KEY ix_npc_trade_inventory_npc(world_instance_id, npc_entity_key, stock_state),
  KEY ix_npc_trade_inventory_item(item_instance_id),
  CONSTRAINT npc_trade_inventory_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT npc_trade_inventory_item_fk FOREIGN KEY(item_instance_id) REFERENCES item_instances(item_instance_id) ON DELETE RESTRICT,
  CONSTRAINT npc_trade_inventory_amount_ck CHECK(amount > 0),
  CONSTRAINT npc_trade_inventory_price_ck CHECK(unit_price >= 0),
  CONSTRAINT npc_trade_inventory_state_ck CHECK(stock_state IN ('available','reserved','sold','disabled')),
  CONSTRAINT npc_trade_inventory_raw_json_ck CHECK(JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS trade_economy_audit (
  trade_audit_id         BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  audit_type             VARCHAR(32) NOT NULL,
  session_id             BINARY(16) NULL,
  character_id           BINARY(16) NOT NULL,
  world_instance_id      BINARY(16) NOT NULL,
  event_id               BINARY(16) NOT NULL,
  idempotency_key        VARCHAR(191) NOT NULL,
  npc_entity_key         VARCHAR(191) NOT NULL,
  item_instance_id       BINARY(16) NOT NULL,
  item_instance_key      VARCHAR(191) NOT NULL,
  amount                 INT NOT NULL,
  price_amount           DECIMAL(20,0) NOT NULL,
  currency_key           VARCHAR(128) NOT NULL,
  wallet_before          DECIMAL(20,0) NOT NULL,
  wallet_after           DECIMAL(20,0) NOT NULL,
  server_tick            BIGINT NOT NULL DEFAULT 0,
  raw_delta              JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY trade_economy_audit_idempotency_uk(world_instance_id, idempotency_key),
  KEY ix_trade_economy_audit_character(character_id, created_at),
  KEY ix_trade_economy_audit_npc(world_instance_id, npc_entity_key, created_at),
  KEY ix_trade_economy_audit_event(event_id),
  CONSTRAINT trade_economy_audit_session_fk FOREIGN KEY(session_id) REFERENCES server_sessions(session_id) ON DELETE SET NULL,
  CONSTRAINT trade_economy_audit_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT trade_economy_audit_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE RESTRICT,
  CONSTRAINT trade_economy_audit_event_fk FOREIGN KEY(event_id) REFERENCES world_event_journal(event_id) ON DELETE RESTRICT,
  CONSTRAINT trade_economy_audit_item_fk FOREIGN KEY(item_instance_id) REFERENCES item_instances(item_instance_id) ON DELETE RESTRICT,
  CONSTRAINT trade_economy_audit_type_ck CHECK(audit_type IN ('buy','sell')),
  CONSTRAINT trade_economy_audit_amount_ck CHECK(amount > 0),
  CONSTRAINT trade_economy_audit_price_ck CHECK(price_amount >= 0),
  CONSTRAINT trade_economy_audit_wallet_ck CHECK(wallet_before >= 0 AND wallet_after >= 0),
  CONSTRAINT trade_economy_audit_tick_ck CHECK(server_tick >= 0),
  CONSTRAINT trade_economy_audit_raw_json_ck CHECK(JSON_VALID(raw_delta))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_trade_buy_from_npc;
DELIMITER $$
CREATE PROCEDURE mmo_trade_buy_from_npc(
  IN  p_session_id       BINARY(16),
  IN  p_npc_entity_key   VARCHAR(191),
  IN  p_item_instance_id BINARY(16),
  IN  p_price_amount     DECIMAL(20,0),
  IN  p_currency_key     VARCHAR(128),
  IN  p_target_bag_index INT,
  IN  p_server_tick      BIGINT,
  IN  p_metadata         JSON,
  IN  p_idempotency_key  VARCHAR(191),
  OUT p_event_id         BINARY(16),
  OUT p_wallet_after     DECIMAL(20,0),
  OUT p_bag_index        INT
)
buy_proc: BEGIN
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_entity_kind VARCHAR(32) DEFAULT NULL;
  DECLARE v_npc_lifecycle VARCHAR(32) DEFAULT NULL;
  DECLARE v_item_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_owner_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_owner_id BINARY(16) DEFAULT NULL;
  DECLARE v_item_state VARCHAR(32) DEFAULT NULL;
  DECLARE v_item_amount INT DEFAULT NULL;
  DECLARE v_stock_amount INT DEFAULT NULL;
  DECLARE v_stock_state VARCHAR(32) DEFAULT NULL;
  DECLARE v_price DECIMAL(20,0) DEFAULT 0;
  DECLARE v_currency VARCHAR(128) DEFAULT 'g2notr:gold';
  DECLARE v_wallet_before DECIMAL(20,0) DEFAULT 0;
  DECLARE v_existing_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_existing_event BINARY(16) DEFAULT NULL;
  DECLARE v_existing_wallet DECIMAL(20,0) DEFAULT NULL;
  DECLARE v_existing_bag INT DEFAULT NULL;
  DECLARE v_payload JSON DEFAULT NULL;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET p_event_id = NULL;
  SET p_wallet_after = NULL;
  SET p_bag_index = p_target_bag_index;
  SET v_currency = COALESCE(NULLIF(p_currency_key, ''), 'g2notr:gold');
  SET v_price = COALESCE(p_price_amount, 0);

  IF p_session_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='session_id is required'; END IF;
  IF p_npc_entity_key IS NULL OR p_npc_entity_key = '' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='npc_entity_key is required'; END IF;
  IF p_item_instance_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='item_instance_id is required'; END IF;
  IF v_price < 0 THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='price must be non-negative'; END IF;
  IF p_idempotency_key IS NULL OR p_idempotency_key = '' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='idempotency key is required'; END IF;

  START TRANSACTION;

  SELECT ss.character_id, ss.realm_id, ss.world_instance_id, c.character_key
    INTO v_character_id, v_realm_id, v_world_instance_id, v_character_key
    FROM server_sessions ss JOIN characters c ON c.character_id = ss.character_id
   WHERE ss.session_id = p_session_id AND ss.lifecycle_state = 'active'
   LIMIT 1 FOR UPDATE;
  IF v_character_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='active session not found'; END IF;

  SELECT audit_type,
         event_id,
         wallet_after,
         CAST(JSON_UNQUOTE(JSON_EXTRACT(raw_delta, '$.bag_index')) AS SIGNED)
    INTO v_existing_type, v_existing_event, v_existing_wallet, v_existing_bag
    FROM trade_economy_audit
   WHERE world_instance_id = v_world_instance_id AND idempotency_key = p_idempotency_key
   LIMIT 1 FOR UPDATE;
  IF v_existing_event IS NOT NULL THEN
    IF v_existing_type <> 'buy' THEN SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT='idempotency key reused with different trade type'; END IF;
    SET p_event_id = v_existing_event;
    SET p_wallet_after = v_existing_wallet;
    SET p_bag_index = v_existing_bag;
    COMMIT;
    LEAVE buy_proc;
  END IF;

  SELECT entity_kind, lifecycle_state INTO v_entity_kind, v_npc_lifecycle
    FROM world_entity_state
   WHERE world_instance_id = v_world_instance_id AND entity_key = p_npc_entity_key
   LIMIT 1 FOR UPDATE;
  IF v_entity_kind IS NULL OR v_entity_kind NOT IN ('npc','creature') THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='trade target is not an npc'; END IF;
  IF v_npc_lifecycle <> 'active' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='npc is not active'; END IF;

  SELECT amount, stock_state INTO v_stock_amount, v_stock_state
    FROM npc_trade_inventory
   WHERE world_instance_id = v_world_instance_id AND npc_entity_key = p_npc_entity_key AND item_instance_id = p_item_instance_id
   LIMIT 1 FOR UPDATE;
  IF v_stock_amount IS NULL OR v_stock_state <> 'available' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='item is not available in npc trade inventory'; END IF;

  SELECT item_instance_key, owner_type, owner_id, lifecycle_state, quantity
    INTO v_item_key, v_owner_type, v_owner_id, v_item_state, v_item_amount
    FROM item_instances
   WHERE item_instance_id = p_item_instance_id
   LIMIT 1 FOR UPDATE;
  IF v_item_key IS NULL OR v_item_state <> 'active' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='item instance is not active'; END IF;
  IF v_item_amount <> v_stock_amount THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='npc stock amount and item quantity mismatch'; END IF;

  INSERT INTO character_wallets(character_id, currency_key, amount)
  VALUES(v_character_id, v_currency, 0)
  ON DUPLICATE KEY UPDATE amount = amount;
  SELECT amount INTO v_wallet_before
    FROM character_wallets
   WHERE character_id = v_character_id AND currency_key = v_currency
   LIMIT 1 FOR UPDATE;
  IF v_wallet_before < v_price THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='insufficient wallet balance for npc buy'; END IF;

  SET p_wallet_after = v_wallet_before - v_price;
  SET p_bag_index = p_target_bag_index;
  IF p_bag_index IS NULL THEN
    SELECT COALESCE(MAX(bag_index) + 1, 0) INTO p_bag_index FROM character_inventory WHERE character_id = v_character_id;
  END IF;

  SET v_payload = JSON_OBJECT(
    'character_key', v_character_key,
    'npc_entity_key', p_npc_entity_key,
    'item_instance_key', v_item_key,
    'amount', v_item_amount,
    'price_amount', v_price,
    'currency_key', v_currency,
    'wallet_before', v_wallet_before,
    'wallet_after', p_wallet_after,
    'bag_index', p_bag_index,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(v_realm_id, v_world_instance_id, v_character_id, 'trade_buy_from_npc', 'trade', COALESCE(p_server_tick,0), p_npc_entity_key, v_item_key, v_payload, p_idempotency_key, 'server', NULL, NULL, p_event_id);

  UPDATE character_wallets SET amount = p_wallet_after WHERE character_id = v_character_id AND currency_key = v_currency;
  DELETE FROM npc_trade_inventory WHERE world_instance_id = v_world_instance_id AND npc_entity_key = p_npc_entity_key AND item_instance_id = p_item_instance_id;
  UPDATE item_instances SET owner_type='character', owner_id=v_character_id, updated_at=CURRENT_TIMESTAMP(6) WHERE item_instance_id = p_item_instance_id;
  INSERT INTO character_inventory(character_id, item_instance_id, bag_index, amount, source_amount, source_iterator_count)
  VALUES(v_character_id, p_item_instance_id, p_bag_index, v_item_amount, v_item_amount, v_item_amount);

  INSERT INTO trade_economy_audit(audit_type, session_id, character_id, world_instance_id, event_id, idempotency_key, npc_entity_key, item_instance_id, item_instance_key, amount, price_amount, currency_key, wallet_before, wallet_after, server_tick, raw_delta)
  VALUES('buy', p_session_id, v_character_id, v_world_instance_id, p_event_id, p_idempotency_key, p_npc_entity_key, p_item_instance_id, v_item_key, v_item_amount, v_price, v_currency, v_wallet_before, p_wallet_after, COALESCE(p_server_tick,0), v_payload);

  UPDATE server_sessions SET last_seen_at=CURRENT_TIMESTAMP(6) WHERE session_id = p_session_id;
  UPDATE realm_world_instances SET current_tick = GREATEST(current_tick, COALESCE(p_server_tick,0)) WHERE world_instance_id = v_world_instance_id;
  COMMIT;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_trade_sell_to_npc;
DELIMITER $$
CREATE PROCEDURE mmo_trade_sell_to_npc(
  IN  p_session_id       BINARY(16),
  IN  p_npc_entity_key   VARCHAR(191),
  IN  p_item_instance_id BINARY(16),
  IN  p_price_amount     DECIMAL(20,0),
  IN  p_currency_key     VARCHAR(128),
  IN  p_server_tick      BIGINT,
  IN  p_metadata         JSON,
  IN  p_idempotency_key  VARCHAR(191),
  OUT p_event_id         BINARY(16),
  OUT p_wallet_after     DECIMAL(20,0)
)
sell_proc: BEGIN
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_entity_kind VARCHAR(32) DEFAULT NULL;
  DECLARE v_npc_lifecycle VARCHAR(32) DEFAULT NULL;
  DECLARE v_item_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_item_state VARCHAR(32) DEFAULT NULL;
  DECLARE v_item_amount INT DEFAULT NULL;
  DECLARE v_inventory_amount INT DEFAULT NULL;
  DECLARE v_price DECIMAL(20,0) DEFAULT 0;
  DECLARE v_currency VARCHAR(128) DEFAULT 'g2notr:gold';
  DECLARE v_wallet_before DECIMAL(20,0) DEFAULT 0;
  DECLARE v_existing_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_existing_event BINARY(16) DEFAULT NULL;
  DECLARE v_existing_wallet DECIMAL(20,0) DEFAULT NULL;
  DECLARE v_payload JSON DEFAULT NULL;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
  SET p_event_id = NULL;
  SET p_wallet_after = NULL;
  SET v_currency = COALESCE(NULLIF(p_currency_key, ''), 'g2notr:gold');
  SET v_price = COALESCE(p_price_amount, 0);
  IF p_session_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='session_id is required'; END IF;
  IF p_npc_entity_key IS NULL OR p_npc_entity_key = '' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='npc_entity_key is required'; END IF;
  IF p_item_instance_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='item_instance_id is required'; END IF;
  IF v_price < 0 THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='price must be non-negative'; END IF;
  IF p_idempotency_key IS NULL OR p_idempotency_key = '' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='idempotency key is required'; END IF;

  START TRANSACTION;
  SELECT ss.character_id, ss.realm_id, ss.world_instance_id, c.character_key
    INTO v_character_id, v_realm_id, v_world_instance_id, v_character_key
    FROM server_sessions ss JOIN characters c ON c.character_id = ss.character_id
   WHERE ss.session_id = p_session_id AND ss.lifecycle_state = 'active'
   LIMIT 1 FOR UPDATE;
  IF v_character_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='active session not found'; END IF;

  SELECT audit_type, event_id, wallet_after INTO v_existing_type, v_existing_event, v_existing_wallet
    FROM trade_economy_audit WHERE world_instance_id = v_world_instance_id AND idempotency_key = p_idempotency_key LIMIT 1 FOR UPDATE;
  IF v_existing_event IS NOT NULL THEN
    IF v_existing_type <> 'sell' THEN SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT='idempotency key reused with different trade type'; END IF;
    SET p_event_id = v_existing_event;
    SET p_wallet_after = v_existing_wallet;
    COMMIT;
    LEAVE sell_proc;
  END IF;

  SELECT entity_kind, lifecycle_state INTO v_entity_kind, v_npc_lifecycle
    FROM world_entity_state WHERE world_instance_id = v_world_instance_id AND entity_key = p_npc_entity_key LIMIT 1 FOR UPDATE;
  IF v_entity_kind IS NULL OR v_entity_kind NOT IN ('npc','creature') THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='trade target is not an npc'; END IF;
  IF v_npc_lifecycle <> 'active' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='npc is not active'; END IF;

  SELECT ci.amount, ii.item_instance_key, ii.lifecycle_state, ii.quantity
    INTO v_inventory_amount, v_item_key, v_item_state, v_item_amount
    FROM character_inventory ci JOIN item_instances ii ON ii.item_instance_id = ci.item_instance_id
   WHERE ci.character_id = v_character_id AND ci.item_instance_id = p_item_instance_id
   LIMIT 1 FOR UPDATE;
  IF v_inventory_amount IS NULL OR v_item_state <> 'active' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='character does not own active item'; END IF;
  IF v_inventory_amount <> v_item_amount THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='inventory amount and item quantity mismatch'; END IF;

  INSERT INTO character_wallets(character_id, currency_key, amount)
  VALUES(v_character_id, v_currency, 0)
  ON DUPLICATE KEY UPDATE amount = amount;
  SELECT amount INTO v_wallet_before FROM character_wallets WHERE character_id = v_character_id AND currency_key = v_currency LIMIT 1 FOR UPDATE;
  SET p_wallet_after = v_wallet_before + v_price;

  SET v_payload = JSON_OBJECT('character_key', v_character_key, 'npc_entity_key', p_npc_entity_key, 'item_instance_key', v_item_key, 'amount', v_item_amount, 'price_amount', v_price, 'currency_key', v_currency, 'wallet_before', v_wallet_before, 'wallet_after', p_wallet_after, 'metadata', COALESCE(p_metadata, JSON_OBJECT()));
  CALL mmo_append_world_event(v_realm_id, v_world_instance_id, v_character_id, 'trade_sell_to_npc', 'trade', COALESCE(p_server_tick,0), p_npc_entity_key, v_item_key, v_payload, p_idempotency_key, 'server', NULL, NULL, p_event_id);

  UPDATE character_wallets SET amount = p_wallet_after WHERE character_id = v_character_id AND currency_key = v_currency;
  DELETE FROM character_equipment WHERE character_id = v_character_id AND item_instance_id = p_item_instance_id;
  DELETE FROM character_inventory WHERE character_id = v_character_id AND item_instance_id = p_item_instance_id;
  UPDATE item_instances SET owner_type='system', owner_id=NULL, updated_at=CURRENT_TIMESTAMP(6) WHERE item_instance_id = p_item_instance_id;
  INSERT INTO npc_trade_inventory(world_instance_id, npc_entity_key, item_instance_id, amount, unit_price, currency_key, stock_state, raw_payload)
  VALUES(v_world_instance_id, p_npc_entity_key, p_item_instance_id, v_item_amount, v_price, v_currency, 'available', JSON_OBJECT('source','character_sell','seller_character_key',v_character_key))
  ON DUPLICATE KEY UPDATE amount=VALUES(amount), unit_price=VALUES(unit_price), currency_key=VALUES(currency_key), stock_state='available', raw_payload=VALUES(raw_payload);

  INSERT INTO trade_economy_audit(audit_type, session_id, character_id, world_instance_id, event_id, idempotency_key, npc_entity_key, item_instance_id, item_instance_key, amount, price_amount, currency_key, wallet_before, wallet_after, server_tick, raw_delta)
  VALUES('sell', p_session_id, v_character_id, v_world_instance_id, p_event_id, p_idempotency_key, p_npc_entity_key, p_item_instance_id, v_item_key, v_item_amount, v_price, v_currency, v_wallet_before, p_wallet_after, COALESCE(p_server_tick,0), v_payload);

  UPDATE server_sessions SET last_seen_at=CURRENT_TIMESTAMP(6) WHERE session_id = p_session_id;
  UPDATE realm_world_instances SET current_tick = GREATEST(current_tick, COALESCE(p_server_tick,0)) WHERE world_instance_id = v_world_instance_id;
  COMMIT;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_npc_trade_inventory AS
SELECT BIN_TO_UUID(nti.world_instance_id,1) AS world_instance_uuid, nti.npc_entity_key, BIN_TO_UUID(nti.item_instance_id,1) AS item_instance_uuid, ii.item_instance_key, nti.amount, nti.unit_price, nti.currency_key, nti.stock_state, nti.updated_at
FROM npc_trade_inventory nti JOIN item_instances ii ON ii.item_instance_id = nti.item_instance_id;

CREATE OR REPLACE VIEW v_trade_economy_audit AS
SELECT BIN_TO_UUID(tea.trade_audit_id,1) AS trade_audit_uuid, tea.audit_type, BIN_TO_UUID(tea.event_id,1) AS event_uuid, tea.idempotency_key, tea.npc_entity_key, ii.item_instance_key, tea.amount, tea.price_amount, tea.currency_key, tea.wallet_before, tea.wallet_after, tea.server_tick, tea.created_at
FROM trade_economy_audit tea JOIN item_instances ii ON ii.item_instance_id = tea.item_instance_id;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES('production/mysql/011_trade_economy_write_path', 'gothic-mmo-trade-economy-write-path-v1-mysql', 'NPC trade inventory, buy/sell procedures, wallet projection updates and trade audit.')
ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes), applied_at=CURRENT_TIMESTAMP(6);
