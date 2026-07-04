-- Step60 clean-DB equipment bridge.
--
-- Purpose:
--   Clean Step55/59 MySQL rebuilds currently have character_equipment rows but
--   can miss the live worker routines used by equip_character_item and
--   unequip_character_item. This additive bridge keeps the old single-player
--   path untouched and only runs when the resolved server worker calls it.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS character_inventory_audit (
  audit_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) NULL,
  character_id BINARY(16) NOT NULL,
  target_character_id BINARY(16) NULL,
  item_instance_id BINARY(16) NOT NULL,
  audit_type VARCHAR(64) NOT NULL,
  amount INT NOT NULL DEFAULT 0,
  equipment_slot VARCHAR(32) NULL,
  idempotency_key VARCHAR(512) NOT NULL,
  metadata JSON NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (audit_id),
  UNIQUE KEY ux_character_inventory_audit_idem (idempotency_key),
  KEY ix_character_inventory_audit_character (character_id, created_at),
  KEY ix_character_inventory_audit_item (item_instance_id),
  KEY ix_character_inventory_audit_event (event_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

SET @step60_migration_key = 'dev/sql/step60_clean_db_equipment_bridge';
SET @step60_schema_contract = 'gothic-mmo-step60-clean-db-equipment-bridge-v1';
SET @step60_schema_notes = 'Installs minimal equip, unequip and transfer bridge procedures for clean live-worker DBs.';
SET @step60_has_schema_notes = (
  SELECT COUNT(*)
    FROM information_schema.COLUMNS
   WHERE TABLE_SCHEMA = DATABASE()
     AND TABLE_NAME = 'mmo_schema_versions'
     AND COLUMN_NAME = 'notes'
);
SET @step60_schema_sql = IF(
  @step60_has_schema_notes > 0,
  CONCAT(
    'INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes) VALUES (',
    QUOTE(@step60_migration_key), ', ', QUOTE(@step60_schema_contract), ', ', QUOTE(@step60_schema_notes),
    ') ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes)'
  ),
  CONCAT(
    'INSERT INTO mmo_schema_versions(migration_key, schema_contract) VALUES (',
    QUOTE(@step60_migration_key), ', ', QUOTE(@step60_schema_contract),
    ') ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract)'
  )
);
PREPARE step60_schema_stmt FROM @step60_schema_sql;
EXECUTE step60_schema_stmt;
DEALLOCATE PREPARE step60_schema_stmt;

DROP PROCEDURE IF EXISTS mmo_equip_character_item;
DROP PROCEDURE IF EXISTS mmo_unequip_character_item;
DROP PROCEDURE IF EXISTS mmo_transfer_character_item;

DELIMITER $$

CREATE PROCEDURE mmo_equip_character_item(
  IN p_session_id BINARY(16),
  IN p_item_instance_id BINARY(16),
  IN p_equipment_slot VARCHAR(32),
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16)
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_slot VARCHAR(32);
  DECLARE v_amount INT DEFAULT NULL;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SELECT event_id
    INTO o_event_id
    FROM character_inventory_audit
   WHERE idempotency_key = p_idempotency_key
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    LEAVE proc;
  END IF;

  SET v_slot = COALESCE(NULLIF(p_equipment_slot, ''), 'unknown');
  IF v_slot NOT IN ('weapon_melee','weapon_ranged','shield','armor','belt','amulet','ring_left','ring_right','rune','torch','unknown') THEN
    SET v_slot = 'unknown';
  END IF;

  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_equip_character_item: active session not found';
  END IF;

  SELECT ci.amount
    INTO v_amount
    FROM character_inventory ci
    JOIN item_instances ii ON ii.item_instance_id = ci.item_instance_id
   WHERE ci.character_id = v_character_id
     AND ci.item_instance_id = p_item_instance_id
     AND ii.realm_id = v_realm_id
     AND ii.owner_type = 'character'
     AND ii.owner_id = v_character_id
     AND ii.lifecycle_state = 'active'
   LIMIT 1;
  IF v_amount IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_equip_character_item: character inventory item not found';
  END IF;

  START TRANSACTION;

  DELETE FROM character_equipment
   WHERE character_id = v_character_id
     AND (equipment_slot = v_slot OR item_instance_id = p_item_instance_id);

  INSERT INTO character_equipment(character_id, equipment_slot, item_instance_id)
  VALUES(v_character_id, v_slot, p_item_instance_id);

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'character_item_equipped', 'equipment', p_server_tick,
    v_slot, BIN_TO_UUID(p_item_instance_id, 1),
    JSON_OBJECT('item_instance_id', BIN_TO_UUID(p_item_instance_id, 1), 'equipment_slot', v_slot, 'amount', v_amount, 'metadata', p_metadata),
    p_idempotency_key, 'server', NULL, NULL, o_event_id
  );

  INSERT INTO character_inventory_audit(event_id, character_id, item_instance_id, audit_type, amount, equipment_slot, idempotency_key, metadata)
  VALUES(o_event_id, v_character_id, p_item_instance_id, 'equip', v_amount, v_slot, p_idempotency_key, p_metadata);

  COMMIT;
END$$

CREATE PROCEDURE mmo_unequip_character_item(
  IN p_session_id BINARY(16),
  IN p_equipment_slot VARCHAR(32),
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_item_instance_id BINARY(16)
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_slot VARCHAR(32);
  DECLARE v_amount INT DEFAULT 1;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SELECT event_id, item_instance_id
    INTO o_event_id, o_item_instance_id
    FROM character_inventory_audit
   WHERE idempotency_key = p_idempotency_key
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    LEAVE proc;
  END IF;

  SET v_slot = COALESCE(NULLIF(p_equipment_slot, ''), 'unknown');
  IF v_slot NOT IN ('weapon_melee','weapon_ranged','shield','armor','belt','amulet','ring_left','ring_right','rune','torch','unknown') THEN
    SET v_slot = 'unknown';
  END IF;

  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_unequip_character_item: active session not found';
  END IF;

  SELECT ce.item_instance_id, COALESCE(ci.amount, 1)
    INTO o_item_instance_id, v_amount
    FROM character_equipment ce
    LEFT JOIN character_inventory ci ON ci.character_id = ce.character_id AND ci.item_instance_id = ce.item_instance_id
   WHERE ce.character_id = v_character_id
     AND ce.equipment_slot = v_slot
   LIMIT 1;
  IF o_item_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_unequip_character_item: equipment slot is empty';
  END IF;

  START TRANSACTION;

  DELETE FROM character_equipment
   WHERE character_id = v_character_id
     AND equipment_slot = v_slot;

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'character_item_unequipped', 'equipment', p_server_tick,
    v_slot, BIN_TO_UUID(o_item_instance_id, 1),
    JSON_OBJECT('item_instance_id', BIN_TO_UUID(o_item_instance_id, 1), 'equipment_slot', v_slot, 'amount', v_amount, 'metadata', p_metadata),
    p_idempotency_key, 'server', NULL, NULL, o_event_id
  );

  INSERT INTO character_inventory_audit(event_id, character_id, item_instance_id, audit_type, amount, equipment_slot, idempotency_key, metadata)
  VALUES(o_event_id, v_character_id, o_item_instance_id, 'unequip', v_amount, v_slot, p_idempotency_key, p_metadata);

  COMMIT;
END$$

CREATE PROCEDURE mmo_transfer_character_item(
  IN p_session_id BINARY(16),
  IN p_item_instance_id BINARY(16),
  IN p_target_character_key VARCHAR(191),
  IN p_amount INT,
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_target_character_id BINARY(16),
  OUT o_amount_transferred INT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  DECLARE v_source_character_id BINARY(16);
  DECLARE v_source_amount INT DEFAULT 0;
  DECLARE v_amount INT DEFAULT 1;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SELECT event_id, target_character_id, amount
    INTO o_event_id, o_target_character_id, o_amount_transferred
    FROM character_inventory_audit
   WHERE idempotency_key = p_idempotency_key
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    LEAVE proc;
  END IF;

  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_source_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
   LIMIT 1;
  IF v_source_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_transfer_character_item: active session not found';
  END IF;

  SELECT c.character_id
    INTO o_target_character_id
    FROM characters c
   WHERE c.realm_id = v_realm_id
     AND c.character_key = p_target_character_key
   LIMIT 1;
  IF o_target_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_transfer_character_item: target character not found';
  END IF;

  SELECT ci.amount
    INTO v_source_amount
    FROM character_inventory ci
    JOIN item_instances ii ON ii.item_instance_id = ci.item_instance_id
   WHERE ci.character_id = v_source_character_id
     AND ci.item_instance_id = p_item_instance_id
     AND ii.realm_id = v_realm_id
     AND ii.owner_type = 'character'
     AND ii.owner_id = v_source_character_id
     AND ii.lifecycle_state = 'active'
   LIMIT 1;
  IF v_source_amount IS NULL OR v_source_amount <= 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_transfer_character_item: source inventory item not found';
  END IF;

  SET v_amount = GREATEST(1, LEAST(COALESCE(NULLIF(p_amount, 0), v_source_amount), v_source_amount));

  START TRANSACTION;

  DELETE FROM character_equipment
   WHERE character_id = v_source_character_id
     AND item_instance_id = p_item_instance_id;

  DELETE FROM character_inventory
   WHERE character_id = v_source_character_id
     AND item_instance_id = p_item_instance_id;

  INSERT INTO character_inventory(character_id, item_instance_id, bag_index, amount, source_amount, source_iterator_count)
  VALUES(o_target_character_id, p_item_instance_id, NULL, v_amount, v_amount, v_amount)
  ON DUPLICATE KEY UPDATE
    amount = character_inventory.amount + VALUES(amount),
    source_amount = VALUES(source_amount),
    source_iterator_count = VALUES(source_iterator_count);

  UPDATE item_instances
     SET owner_type = 'character',
         owner_id = o_target_character_id,
         quantity = v_amount,
         lifecycle_state = 'active',
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE item_instance_id = p_item_instance_id;

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_source_character_id,
    'character_inventory_transferred', 'inventory', p_server_tick,
    p_target_character_key, BIN_TO_UUID(p_item_instance_id, 1),
    JSON_OBJECT('item_instance_id', BIN_TO_UUID(p_item_instance_id, 1), 'target_character_key', p_target_character_key, 'target_character_id', BIN_TO_UUID(o_target_character_id, 1), 'amount', v_amount, 'metadata', p_metadata),
    p_idempotency_key, 'server', NULL, NULL, o_event_id
  );

  INSERT INTO character_inventory_audit(event_id, character_id, target_character_id, item_instance_id, audit_type, amount, idempotency_key, metadata)
  VALUES(o_event_id, v_source_character_id, o_target_character_id, p_item_instance_id, 'transfer', v_amount, p_idempotency_key, p_metadata);

  COMMIT;
  SET o_amount_transferred = v_amount;
END$$

DELIMITER ;
