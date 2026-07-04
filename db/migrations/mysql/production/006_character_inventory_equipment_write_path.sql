-- Gothic MMO MySQL production migration 006.
-- Server-owned character inventory/equipment write path.
-- Requires 001..005 MySQL production migrations and a bootstrap import.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

-- -----------------------------------------------------------------------------
-- Character inventory/equipment audit.
-- character_inventory, character_equipment and item_instances are projections.
-- world_event_journal remains the ordered durable mutation source.
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS character_inventory_audit (
  inventory_audit_id       BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  audit_type               VARCHAR(32) NOT NULL,
  session_id               BINARY(16) NULL,
  source_character_id      BINARY(16) NOT NULL,
  target_character_id      BINARY(16) NULL,
  world_instance_id        BINARY(16) NOT NULL,
  event_id                 BINARY(16) NOT NULL,
  idempotency_key          VARCHAR(191) NOT NULL,
  item_instance_id         BINARY(16) NOT NULL,
  item_instance_key        VARCHAR(191) NOT NULL,
  equipment_slot           VARCHAR(32) NULL,
  source_bag_before        INT NULL,
  target_bag_after         INT NULL,
  amount                   INT NOT NULL,
  owner_before_type        VARCHAR(32) NOT NULL,
  owner_before_id          BINARY(16) NULL,
  owner_after_type         VARCHAR(32) NOT NULL,
  owner_after_id           BINARY(16) NULL,
  server_tick              BIGINT NOT NULL DEFAULT 0,
  raw_delta                JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at               TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY character_inventory_audit_idempotency_uk(world_instance_id, idempotency_key),
  KEY ix_character_inventory_audit_source(source_character_id, created_at),
  KEY ix_character_inventory_audit_target(target_character_id, created_at),
  KEY ix_character_inventory_audit_event(event_id),
  KEY ix_character_inventory_audit_item(item_instance_id),
  CONSTRAINT character_inventory_audit_session_fk FOREIGN KEY(session_id) REFERENCES server_sessions(session_id) ON DELETE SET NULL,
  CONSTRAINT character_inventory_audit_source_fk FOREIGN KEY(source_character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT character_inventory_audit_target_fk FOREIGN KEY(target_character_id) REFERENCES characters(character_id) ON DELETE SET NULL,
  CONSTRAINT character_inventory_audit_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE RESTRICT,
  CONSTRAINT character_inventory_audit_event_fk FOREIGN KEY(event_id) REFERENCES world_event_journal(event_id) ON DELETE RESTRICT,
  CONSTRAINT character_inventory_audit_item_fk FOREIGN KEY(item_instance_id) REFERENCES item_instances(item_instance_id) ON DELETE RESTRICT,
  CONSTRAINT character_inventory_audit_type_ck CHECK(audit_type IN ('transfer','equip','unequip')),
  CONSTRAINT character_inventory_audit_slot_ck CHECK(equipment_slot IS NULL OR equipment_slot IN ('weapon_melee','weapon_ranged','shield','armor','belt','amulet','ring_left','ring_right','rune','torch','unknown')),
  CONSTRAINT character_inventory_audit_amount_ck CHECK(amount > 0),
  CONSTRAINT character_inventory_audit_tick_ck CHECK(server_tick >= 0),
  CONSTRAINT character_inventory_audit_raw_json_ck CHECK(JSON_VALID(raw_delta))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_transfer_character_item;
DELIMITER $$
CREATE PROCEDURE mmo_transfer_character_item(
  IN  p_session_id           BINARY(16),
  IN  p_item_instance_id     BINARY(16),
  IN  p_target_character_key VARCHAR(191),
  IN  p_target_bag_index     INT,
  IN  p_server_tick          BIGINT,
  IN  p_metadata             JSON,
  IN  p_idempotency_key      VARCHAR(191),
  OUT p_event_id             BINARY(16),
  OUT p_target_character_id  BINARY(16),
  OUT p_amount_transferred   INT
)
transfer_proc: BEGIN
  DECLARE v_source_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_target_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_source_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_target_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_item_instance_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_owner_before_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_owner_before_id BINARY(16) DEFAULT NULL;
  DECLARE v_lifecycle_state VARCHAR(32) DEFAULT NULL;
  DECLARE v_inventory_amount INT DEFAULT 0;
  DECLARE v_source_bag_before INT DEFAULT NULL;
  DECLARE v_existing_audit_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_existing_event_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_target_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_amount INT DEFAULT NULL;
  DECLARE v_payload JSON;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET p_event_id = NULL;
  SET p_target_character_id = NULL;
  SET p_amount_transferred = NULL;

  IF p_session_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'session id is required';
  END IF;
  IF p_item_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'item instance id is required';
  END IF;
  IF p_target_character_key IS NULL OR TRIM(p_target_character_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'target character key is required';
  END IF;
  IF p_idempotency_key IS NULL OR TRIM(p_idempotency_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'inventory idempotency key is required';
  END IF;
  IF p_target_bag_index IS NOT NULL AND p_target_bag_index < 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'target bag index must be null or non-negative';
  END IF;

  START TRANSACTION;

  SELECT ss.character_id, ss.realm_id, ss.world_instance_id, c.character_key
    INTO v_source_character_id, v_realm_id, v_world_instance_id, v_source_character_key
    FROM server_sessions ss
    JOIN characters c ON c.character_id = ss.character_id
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1
   FOR UPDATE;

  IF v_source_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'active session not found';
  END IF;

  SELECT audit_type, event_id, target_character_id, amount
    INTO v_existing_audit_type, v_existing_event_id, v_existing_target_id, v_existing_amount
    FROM character_inventory_audit
   WHERE world_instance_id = v_world_instance_id
     AND idempotency_key = p_idempotency_key
   LIMIT 1
   FOR UPDATE;

  IF v_existing_event_id IS NOT NULL THEN
    IF v_existing_audit_type <> 'transfer' THEN
      SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT = 'idempotency key reused with different inventory audit type';
    END IF;
    SET p_event_id = v_existing_event_id;
    SET p_target_character_id = v_existing_target_id;
    SET p_amount_transferred = v_existing_amount;
    COMMIT;
    LEAVE transfer_proc;
  END IF;

  SELECT c.character_id, c.character_key
    INTO v_target_character_id, v_target_character_key
    FROM characters c
   WHERE c.character_key = p_target_character_key
     AND c.realm_id = v_realm_id
     AND c.lifecycle_state = 'active'
   LIMIT 1
   FOR UPDATE;

  IF v_target_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'active target character not found in same realm';
  END IF;

  SELECT ii.item_instance_key, ii.owner_type, ii.owner_id, ii.lifecycle_state, ci.amount, ci.bag_index
    INTO v_item_instance_key, v_owner_before_type, v_owner_before_id, v_lifecycle_state, v_inventory_amount, v_source_bag_before
    FROM item_instances ii
    JOIN character_inventory ci ON ci.item_instance_id = ii.item_instance_id
   WHERE ii.item_instance_id = p_item_instance_id
     AND ci.character_id = v_source_character_id
   LIMIT 1
   FOR UPDATE;

  IF v_item_instance_key IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'source character does not own this inventory item';
  END IF;
  IF v_owner_before_type <> 'character' OR v_owner_before_id <> v_source_character_id THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'item instance owner does not match source character';
  END IF;
  IF v_lifecycle_state <> 'active' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'item instance is not active';
  END IF;
  IF v_inventory_amount <= 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'inventory amount must be positive';
  END IF;

  SET v_payload = JSON_OBJECT(
    'source_character_key', v_source_character_key,
    'target_character_key', v_target_character_key,
    'item_instance_key', v_item_instance_key,
    'amount', v_inventory_amount,
    'source_bag_before', v_source_bag_before,
    'target_bag_after', p_target_bag_index,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_instance_id,
    v_source_character_id,
    'character_inventory_transferred',
    'inventory',
    COALESCE(p_server_tick, 0),
    v_item_instance_key,
    v_target_character_key,
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  DELETE FROM character_equipment
   WHERE character_id = v_source_character_id
     AND item_instance_id = p_item_instance_id;

  DELETE FROM character_inventory
   WHERE character_id = v_source_character_id
     AND item_instance_id = p_item_instance_id;

  UPDATE item_instances
     SET owner_type = 'character',
         owner_id = v_target_character_id,
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE item_instance_id = p_item_instance_id;

  INSERT INTO character_inventory(character_id, item_instance_id, bag_index, amount, source_amount, source_iterator_count)
  VALUES(v_target_character_id, p_item_instance_id, p_target_bag_index, v_inventory_amount, NULL, NULL);

  INSERT INTO character_inventory_audit(
    audit_type, session_id, source_character_id, target_character_id, world_instance_id, event_id,
    idempotency_key, item_instance_id, item_instance_key, equipment_slot, source_bag_before,
    target_bag_after, amount, owner_before_type, owner_before_id, owner_after_type, owner_after_id,
    server_tick, raw_delta
  ) VALUES(
    'transfer', p_session_id, v_source_character_id, v_target_character_id, v_world_instance_id, p_event_id,
    p_idempotency_key, p_item_instance_id, v_item_instance_key, NULL, v_source_bag_before,
    p_target_bag_index, v_inventory_amount, v_owner_before_type, v_owner_before_id, 'character', v_target_character_id,
    COALESCE(p_server_tick, 0), v_payload
  );

  UPDATE realm_world_instances
     SET current_tick = GREATEST(current_tick, COALESCE(p_server_tick, 0)),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id;

  UPDATE server_sessions
     SET last_seen_at = CURRENT_TIMESTAMP(6),
         metadata = JSON_SET(COALESCE(metadata, JSON_OBJECT()), '$.last_inventory_event', BIN_TO_UUID(p_event_id, 1))
   WHERE session_id = p_session_id;

  SET p_target_character_id = v_target_character_id;
  SET p_amount_transferred = v_inventory_amount;
  COMMIT;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_equip_character_item;
DELIMITER $$
CREATE PROCEDURE mmo_equip_character_item(
  IN  p_session_id       BINARY(16),
  IN  p_item_instance_id BINARY(16),
  IN  p_equipment_slot   VARCHAR(32),
  IN  p_server_tick      BIGINT,
  IN  p_metadata         JSON,
  IN  p_idempotency_key  VARCHAR(191),
  OUT p_event_id         BINARY(16)
)
equip_proc: BEGIN
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_item_instance_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_owner_before_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_owner_before_id BINARY(16) DEFAULT NULL;
  DECLARE v_lifecycle_state VARCHAR(32) DEFAULT NULL;
  DECLARE v_inventory_amount INT DEFAULT 0;
  DECLARE v_source_bag_before INT DEFAULT NULL;
  DECLARE v_slot_occupied BINARY(16) DEFAULT NULL;
  DECLARE v_item_equipped_slot VARCHAR(32) DEFAULT NULL;
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
  IF p_item_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'item instance id is required';
  END IF;
  IF p_equipment_slot IS NULL OR p_equipment_slot NOT IN ('weapon_melee','weapon_ranged','shield','armor','belt','amulet','ring_left','ring_right','rune','torch','unknown') THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'valid equipment slot is required';
  END IF;
  IF p_idempotency_key IS NULL OR TRIM(p_idempotency_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'equipment idempotency key is required';
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
    FROM character_inventory_audit
   WHERE world_instance_id = v_world_instance_id
     AND idempotency_key = p_idempotency_key
   LIMIT 1
   FOR UPDATE;

  IF v_existing_event_id IS NOT NULL THEN
    IF v_existing_audit_type <> 'equip' THEN
      SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT = 'idempotency key reused with different inventory audit type';
    END IF;
    SET p_event_id = v_existing_event_id;
    COMMIT;
    LEAVE equip_proc;
  END IF;

  SELECT ii.item_instance_key, ii.owner_type, ii.owner_id, ii.lifecycle_state, ci.amount, ci.bag_index
    INTO v_item_instance_key, v_owner_before_type, v_owner_before_id, v_lifecycle_state, v_inventory_amount, v_source_bag_before
    FROM item_instances ii
    JOIN character_inventory ci ON ci.item_instance_id = ii.item_instance_id
   WHERE ii.item_instance_id = p_item_instance_id
     AND ci.character_id = v_character_id
   LIMIT 1
   FOR UPDATE;

  IF v_item_instance_key IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'character does not own this inventory item';
  END IF;
  IF v_owner_before_type <> 'character' OR v_owner_before_id <> v_character_id THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'item instance owner does not match character';
  END IF;
  IF v_lifecycle_state <> 'active' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'item instance is not active';
  END IF;

  SELECT item_instance_id
    INTO v_slot_occupied
    FROM character_equipment
   WHERE character_id = v_character_id
     AND equipment_slot = p_equipment_slot
   LIMIT 1
   FOR UPDATE;

  IF v_slot_occupied IS NOT NULL AND v_slot_occupied <> p_item_instance_id THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'equipment slot is already occupied; unequip first';
  END IF;

  SELECT equipment_slot
    INTO v_item_equipped_slot
    FROM character_equipment
   WHERE item_instance_id = p_item_instance_id
   LIMIT 1
   FOR UPDATE;

  IF v_item_equipped_slot IS NOT NULL AND v_item_equipped_slot <> p_equipment_slot THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'item is already equipped in another slot';
  END IF;

  SET v_payload = JSON_OBJECT(
    'character_key', v_character_key,
    'item_instance_key', v_item_instance_key,
    'equipment_slot', p_equipment_slot,
    'amount', v_inventory_amount,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_instance_id,
    v_character_id,
    'character_item_equipped',
    'equipment',
    COALESCE(p_server_tick, 0),
    v_item_instance_key,
    p_equipment_slot,
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  INSERT INTO character_equipment(character_id, equipment_slot, item_instance_id)
  VALUES(v_character_id, p_equipment_slot, p_item_instance_id)
  ON DUPLICATE KEY UPDATE
    item_instance_id = VALUES(item_instance_id),
    updated_at = CURRENT_TIMESTAMP(6);

  INSERT INTO character_inventory_audit(
    audit_type, session_id, source_character_id, target_character_id, world_instance_id, event_id,
    idempotency_key, item_instance_id, item_instance_key, equipment_slot, source_bag_before,
    target_bag_after, amount, owner_before_type, owner_before_id, owner_after_type, owner_after_id,
    server_tick, raw_delta
  ) VALUES(
    'equip', p_session_id, v_character_id, v_character_id, v_world_instance_id, p_event_id,
    p_idempotency_key, p_item_instance_id, v_item_instance_key, p_equipment_slot, v_source_bag_before,
    v_source_bag_before, v_inventory_amount, v_owner_before_type, v_owner_before_id, 'character', v_character_id,
    COALESCE(p_server_tick, 0), v_payload
  );

  UPDATE realm_world_instances
     SET current_tick = GREATEST(current_tick, COALESCE(p_server_tick, 0)),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id;

  UPDATE server_sessions
     SET last_seen_at = CURRENT_TIMESTAMP(6),
         metadata = JSON_SET(COALESCE(metadata, JSON_OBJECT()), '$.last_equipment_event', BIN_TO_UUID(p_event_id, 1))
   WHERE session_id = p_session_id;

  COMMIT;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_unequip_character_item;
DELIMITER $$
CREATE PROCEDURE mmo_unequip_character_item(
  IN  p_session_id       BINARY(16),
  IN  p_equipment_slot   VARCHAR(32),
  IN  p_server_tick      BIGINT,
  IN  p_metadata         JSON,
  IN  p_idempotency_key  VARCHAR(191),
  OUT p_event_id         BINARY(16),
  OUT p_item_instance_id BINARY(16)
)
unequip_proc: BEGIN
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_item_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_item_instance_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_owner_before_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_owner_before_id BINARY(16) DEFAULT NULL;
  DECLARE v_inventory_amount INT DEFAULT 0;
  DECLARE v_source_bag_before INT DEFAULT NULL;
  DECLARE v_existing_audit_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_existing_event_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_item_id BINARY(16) DEFAULT NULL;
  DECLARE v_payload JSON;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET p_event_id = NULL;
  SET p_item_instance_id = NULL;

  IF p_session_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'session id is required';
  END IF;
  IF p_equipment_slot IS NULL OR p_equipment_slot NOT IN ('weapon_melee','weapon_ranged','shield','armor','belt','amulet','ring_left','ring_right','rune','torch','unknown') THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'valid equipment slot is required';
  END IF;
  IF p_idempotency_key IS NULL OR TRIM(p_idempotency_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'equipment idempotency key is required';
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

  SELECT audit_type, event_id, item_instance_id
    INTO v_existing_audit_type, v_existing_event_id, v_existing_item_id
    FROM character_inventory_audit
   WHERE world_instance_id = v_world_instance_id
     AND idempotency_key = p_idempotency_key
   LIMIT 1
   FOR UPDATE;

  IF v_existing_event_id IS NOT NULL THEN
    IF v_existing_audit_type <> 'unequip' THEN
      SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT = 'idempotency key reused with different inventory audit type';
    END IF;
    SET p_event_id = v_existing_event_id;
    SET p_item_instance_id = v_existing_item_id;
    COMMIT;
    LEAVE unequip_proc;
  END IF;

  SELECT ce.item_instance_id, ii.item_instance_key, ii.owner_type, ii.owner_id, ci.amount, ci.bag_index
    INTO v_item_instance_id, v_item_instance_key, v_owner_before_type, v_owner_before_id, v_inventory_amount, v_source_bag_before
    FROM character_equipment ce
    JOIN item_instances ii ON ii.item_instance_id = ce.item_instance_id
    JOIN character_inventory ci ON ci.character_id = ce.character_id AND ci.item_instance_id = ce.item_instance_id
   WHERE ce.character_id = v_character_id
     AND ce.equipment_slot = p_equipment_slot
   LIMIT 1
   FOR UPDATE;

  IF v_item_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'equipment slot is empty';
  END IF;

  SET v_payload = JSON_OBJECT(
    'character_key', v_character_key,
    'item_instance_key', v_item_instance_key,
    'equipment_slot', p_equipment_slot,
    'amount', v_inventory_amount,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_instance_id,
    v_character_id,
    'character_item_unequipped',
    'equipment',
    COALESCE(p_server_tick, 0),
    v_item_instance_key,
    p_equipment_slot,
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  DELETE FROM character_equipment
   WHERE character_id = v_character_id
     AND equipment_slot = p_equipment_slot;

  INSERT INTO character_inventory_audit(
    audit_type, session_id, source_character_id, target_character_id, world_instance_id, event_id,
    idempotency_key, item_instance_id, item_instance_key, equipment_slot, source_bag_before,
    target_bag_after, amount, owner_before_type, owner_before_id, owner_after_type, owner_after_id,
    server_tick, raw_delta
  ) VALUES(
    'unequip', p_session_id, v_character_id, v_character_id, v_world_instance_id, p_event_id,
    p_idempotency_key, v_item_instance_id, v_item_instance_key, p_equipment_slot, v_source_bag_before,
    v_source_bag_before, v_inventory_amount, v_owner_before_type, v_owner_before_id, 'character', v_character_id,
    COALESCE(p_server_tick, 0), v_payload
  );

  UPDATE realm_world_instances
     SET current_tick = GREATEST(current_tick, COALESCE(p_server_tick, 0)),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id;

  UPDATE server_sessions
     SET last_seen_at = CURRENT_TIMESTAMP(6),
         metadata = JSON_SET(COALESCE(metadata, JSON_OBJECT()), '$.last_equipment_event', BIN_TO_UUID(p_event_id, 1))
   WHERE session_id = p_session_id;

  SET p_item_instance_id = v_item_instance_id;
  COMMIT;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_character_equipment_state AS
SELECT
  BIN_TO_UUID(c.character_id, 1) AS character_id,
  c.character_key,
  c.character_name,
  ce.equipment_slot,
  BIN_TO_UUID(ii.item_instance_id, 1) AS item_instance_id,
  ii.item_instance_key,
  it.item_template_key,
  it.display_name AS item_display_name,
  it.classification,
  it.stack_policy,
  ii.quantity,
  ii.lifecycle_state,
  ce.equipped_at,
  ce.updated_at
FROM character_equipment ce
JOIN characters c ON c.character_id = ce.character_id
JOIN item_instances ii ON ii.item_instance_id = ce.item_instance_id
JOIN content_item_templates it ON it.item_template_id = ii.item_template_id;

CREATE OR REPLACE VIEW v_character_inventory_audit AS
SELECT
  BIN_TO_UUID(a.inventory_audit_id, 1) AS inventory_audit_id,
  a.audit_type,
  BIN_TO_UUID(a.event_id, 1) AS event_id,
  a.idempotency_key,
  src.character_key AS source_character_key,
  dst.character_key AS target_character_key,
  BIN_TO_UUID(a.item_instance_id, 1) AS item_instance_id,
  a.item_instance_key,
  a.equipment_slot,
  a.source_bag_before,
  a.target_bag_after,
  a.amount,
  a.server_tick,
  a.created_at
FROM character_inventory_audit a
JOIN characters src ON src.character_id = a.source_character_id
LEFT JOIN characters dst ON dst.character_id = a.target_character_id;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES(
  'production/mysql/006_character_inventory_equipment_write_path',
  'gothic-mmo-character-inventory-equipment-write-path-v1-mysql',
  'Adds server-owned character inventory transfer and equipment equip/unequip procedures with idempotent audit.'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);
