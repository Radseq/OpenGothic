-- Gothic MMO MySQL production migration 005.
-- Server-owned loose world item pickup/removal write path.
-- Requires 001_gothic_mmo_production_schema.sql, 002_bootstrap_import_pipeline.sql,
-- 003_server_write_path.sql and 004_wallet_write_path.sql.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

-- -----------------------------------------------------------------------------
-- Loose world item audit.
-- world_entity_state + item_instances/character_inventory are the current-state
-- projections. world_event_journal remains the ordered durable mutation source.
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS world_item_audit (
  world_item_audit_id       BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  audit_type                VARCHAR(32) NOT NULL,
  session_id                BINARY(16) NULL,
  character_id              BINARY(16) NOT NULL,
  world_instance_id         BINARY(16) NOT NULL,
  event_id                  BINARY(16) NOT NULL,
  idempotency_key           VARCHAR(191) NOT NULL,
  world_item_entity_key     VARCHAR(191) NOT NULL,
  item_instance_id          BINARY(16) NOT NULL,
  item_instance_key         VARCHAR(191) NOT NULL,
  amount                    INT NOT NULL,
  owner_before_type         VARCHAR(32) NOT NULL,
  owner_before_id           BINARY(16) NULL,
  owner_after_type          VARCHAR(32) NOT NULL,
  owner_after_id            BINARY(16) NULL,
  lifecycle_before          VARCHAR(32) NOT NULL,
  lifecycle_after           VARCHAR(32) NOT NULL,
  world_lifecycle_before    VARCHAR(32) NOT NULL,
  world_lifecycle_after     VARCHAR(32) NOT NULL,
  server_tick               BIGINT NOT NULL DEFAULT 0,
  raw_delta                 JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at                TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY world_item_audit_idempotency_uk(world_instance_id, idempotency_key),
  KEY ix_world_item_audit_world_entity(world_instance_id, world_item_entity_key, created_at),
  KEY ix_world_item_audit_character(character_id, created_at),
  KEY ix_world_item_audit_event(event_id),
  KEY ix_world_item_audit_item(item_instance_id),
  CONSTRAINT world_item_audit_session_fk FOREIGN KEY(session_id) REFERENCES server_sessions(session_id) ON DELETE SET NULL,
  CONSTRAINT world_item_audit_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT world_item_audit_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE RESTRICT,
  CONSTRAINT world_item_audit_event_fk FOREIGN KEY(event_id) REFERENCES world_event_journal(event_id) ON DELETE RESTRICT,
  CONSTRAINT world_item_audit_item_fk FOREIGN KEY(item_instance_id) REFERENCES item_instances(item_instance_id) ON DELETE RESTRICT,
  CONSTRAINT world_item_audit_type_ck CHECK(audit_type IN ('pickup','remove')),
  CONSTRAINT world_item_audit_amount_ck CHECK(amount > 0),
  CONSTRAINT world_item_audit_tick_ck CHECK(server_tick >= 0),
  CONSTRAINT world_item_audit_raw_json_ck CHECK(JSON_VALID(raw_delta))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_pickup_world_item;
DELIMITER $$
CREATE PROCEDURE mmo_pickup_world_item(
  IN  p_session_id              BINARY(16),
  IN  p_world_item_entity_key   VARCHAR(191),
  IN  p_amount                  INT,
  IN  p_bag_index               INT,
  IN  p_server_tick             BIGINT,
  IN  p_metadata                JSON,
  IN  p_idempotency_key         VARCHAR(191),
  OUT p_event_id                BINARY(16),
  OUT p_item_instance_id        BINARY(16),
  OUT p_amount_picked           INT
)
pickup_proc: BEGIN
  DECLARE v_account_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_session_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_item_entity_state_id BINARY(16) DEFAULT NULL;
  DECLARE v_item_entity_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_world_lifecycle_before VARCHAR(32) DEFAULT NULL;
  DECLARE v_item_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_item_instance_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_owner_before_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_owner_before_id BINARY(16) DEFAULT NULL;
  DECLARE v_lifecycle_before VARCHAR(32) DEFAULT NULL;
  DECLARE v_quantity_before INT DEFAULT 0;
  DECLARE v_amount INT DEFAULT 0;
  DECLARE v_existing_event_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_type VARCHAR(128) DEFAULT NULL;
  DECLARE v_existing_class VARCHAR(32) DEFAULT NULL;
  DECLARE v_payload JSON;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET p_event_id = NULL;
  SET p_item_instance_id = NULL;
  SET p_amount_picked = NULL;

  IF p_session_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'session id is required';
  END IF;

  IF p_world_item_entity_key IS NULL OR TRIM(p_world_item_entity_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'world item entity key is required';
  END IF;

  IF p_idempotency_key IS NULL OR TRIM(p_idempotency_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'world item idempotency key is required';
  END IF;

  START TRANSACTION;

  SELECT ss.account_id, ss.character_id, ss.realm_id, ss.world_instance_id, ss.session_key, c.character_key
    INTO v_account_id, v_character_id, v_realm_id, v_world_instance_id, v_session_key, v_character_key
    FROM server_sessions ss
    JOIN characters c ON c.character_id = ss.character_id
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1
   FOR UPDATE;

  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'active session not found';
  END IF;

  SELECT event_id, event_type, event_class
    INTO v_existing_event_id, v_existing_type, v_existing_class
    FROM world_event_journal
   WHERE world_instance_id = v_world_instance_id
     AND idempotency_key = p_idempotency_key
   LIMIT 1
   FOR UPDATE;

  IF v_existing_event_id IS NOT NULL THEN
    IF v_existing_type <> 'world_item_picked_up' OR v_existing_class <> 'inventory' THEN
      SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT = 'world item idempotency key reused with different event type/class';
    END IF;

    SELECT item_instance_id, amount
      INTO p_item_instance_id, p_amount_picked
      FROM world_item_audit
     WHERE world_instance_id = v_world_instance_id
       AND idempotency_key = p_idempotency_key
     LIMIT 1;

    SET p_event_id = v_existing_event_id;

    UPDATE server_sessions
       SET last_seen_at = CURRENT_TIMESTAMP(6)
     WHERE session_id = p_session_id;

    COMMIT;
    LEAVE pickup_proc;
  END IF;

  SET v_item_entity_key = TRIM(p_world_item_entity_key);

  SELECT world_entity_state_id, lifecycle_state
    INTO v_item_entity_state_id, v_world_lifecycle_before
    FROM world_entity_state
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = v_item_entity_key
     AND entity_kind = 'item'
   LIMIT 1
   FOR UPDATE;

  IF v_item_entity_state_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'world item entity not found';
  END IF;

  IF v_world_lifecycle_before <> 'active' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'world item entity is not active';
  END IF;

  SELECT ii.item_instance_id, ii.item_instance_key, ii.owner_type, ii.owner_id, ii.lifecycle_state, ii.quantity
    INTO v_item_instance_id, v_item_instance_key, v_owner_before_type, v_owner_before_id, v_lifecycle_before, v_quantity_before
    FROM item_instances ii
   WHERE ii.realm_id = v_realm_id
     AND ii.owner_type = 'world_entity'
     AND ii.lifecycle_state = 'active'
     AND (
          ii.item_instance_key LIKE CONCAT('%:world-item:', v_item_entity_key)
          OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload, '$.item_spawn_key')) = v_item_entity_key
          OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload, '$.entity_key')) = v_item_entity_key
     )
   ORDER BY ii.updated_at DESC
   LIMIT 1
   FOR UPDATE;

  IF v_item_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'active world item instance not found';
  END IF;

  SET v_amount = COALESCE(p_amount, v_quantity_before);

  IF v_amount <= 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'pickup amount must be positive';
  END IF;

  IF v_amount <> v_quantity_before THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'partial world item pickup is not implemented in migration 005';
  END IF;

  SET v_payload = JSON_OBJECT(
    'session_key', v_session_key,
    'character_key', v_character_key,
    'world_item_entity_key', v_item_entity_key,
    'item_instance_key', v_item_instance_key,
    'amount', v_amount,
    'owner_before_type', v_owner_before_type,
    'owner_after_type', 'character',
    'world_lifecycle_before', v_world_lifecycle_before,
    'world_lifecycle_after', 'removed',
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_instance_id,
    v_character_id,
    'world_item_picked_up',
    'inventory',
    GREATEST(0, COALESCE(p_server_tick, 0)),
    v_item_entity_key,
    v_character_key,
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  UPDATE item_instances
     SET owner_type = 'character',
         owner_id = v_character_id,
         quantity = v_amount,
         lifecycle_state = 'active',
         bind_state = CASE WHEN bind_state = 'bind_on_pickup' THEN 'bound_character' ELSE bind_state END,
         raw_payload = JSON_MERGE_PATCH(
           raw_payload,
           JSON_OBJECT(
             'last_event_type', 'world_item_picked_up',
             'last_event_id', BIN_TO_UUID(p_event_id, 1),
             'picked_by_character_key', v_character_key,
             'picked_at_server_tick', GREATEST(0, COALESCE(p_server_tick, 0))
           )
         )
   WHERE item_instance_id = v_item_instance_id;

  INSERT INTO character_inventory(character_id, item_instance_id, bag_index, amount, source_amount, source_iterator_count)
  VALUES(v_character_id, v_item_instance_id, p_bag_index, v_amount, v_amount, NULL);

  UPDATE world_entity_state
     SET lifecycle_state = 'removed',
         row_version = row_version + 1,
         state_json = JSON_MERGE_PATCH(
           state_json,
           JSON_OBJECT(
             'last_event_type', 'world_item_picked_up',
             'last_event_id', BIN_TO_UUID(p_event_id, 1),
             'picked_by_character_key', v_character_key,
             'picked_item_instance_key', v_item_instance_key,
             'picked_amount', v_amount,
             'removed_at_server_tick', GREATEST(0, COALESCE(p_server_tick, 0))
           )
         )
   WHERE world_entity_state_id = v_item_entity_state_id;

  INSERT INTO world_item_audit(
    audit_type, session_id, character_id, world_instance_id, event_id, idempotency_key,
    world_item_entity_key, item_instance_id, item_instance_key, amount,
    owner_before_type, owner_before_id, owner_after_type, owner_after_id,
    lifecycle_before, lifecycle_after, world_lifecycle_before, world_lifecycle_after,
    server_tick, raw_delta
  ) VALUES (
    'pickup', p_session_id, v_character_id, v_world_instance_id, p_event_id, p_idempotency_key,
    v_item_entity_key, v_item_instance_id, v_item_instance_key, v_amount,
    v_owner_before_type, v_owner_before_id, 'character', v_character_id,
    v_lifecycle_before, 'active', v_world_lifecycle_before, 'removed',
    GREATEST(0, COALESCE(p_server_tick, 0)), v_payload
  );

  UPDATE realm_world_instances
     SET current_tick = GREATEST(current_tick, GREATEST(0, COALESCE(p_server_tick, 0)))
   WHERE world_instance_id = v_world_instance_id;

  UPDATE server_sessions
     SET last_seen_at = CURRENT_TIMESTAMP(6)
   WHERE session_id = p_session_id;

  SET p_item_instance_id = v_item_instance_id;
  SET p_amount_picked = v_amount;

  COMMIT;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_remove_world_item;
DELIMITER $$
CREATE PROCEDURE mmo_remove_world_item(
  IN  p_session_id              BINARY(16),
  IN  p_world_item_entity_key   VARCHAR(191),
  IN  p_reason                  VARCHAR(128),
  IN  p_server_tick             BIGINT,
  IN  p_metadata                JSON,
  IN  p_idempotency_key         VARCHAR(191),
  OUT p_event_id                BINARY(16),
  OUT p_item_instance_id        BINARY(16)
)
remove_proc: BEGIN
  DECLARE v_account_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_session_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_reason VARCHAR(128) DEFAULT NULL;
  DECLARE v_item_entity_state_id BINARY(16) DEFAULT NULL;
  DECLARE v_item_entity_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_world_lifecycle_before VARCHAR(32) DEFAULT NULL;
  DECLARE v_item_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_item_instance_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_owner_before_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_owner_before_id BINARY(16) DEFAULT NULL;
  DECLARE v_lifecycle_before VARCHAR(32) DEFAULT NULL;
  DECLARE v_quantity_before INT DEFAULT 0;
  DECLARE v_existing_event_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_type VARCHAR(128) DEFAULT NULL;
  DECLARE v_existing_class VARCHAR(32) DEFAULT NULL;
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

  IF p_world_item_entity_key IS NULL OR TRIM(p_world_item_entity_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'world item entity key is required';
  END IF;

  IF p_idempotency_key IS NULL OR TRIM(p_idempotency_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'world item idempotency key is required';
  END IF;

  SET v_reason = COALESCE(NULLIF(TRIM(p_reason), ''), 'remove_world_item');

  START TRANSACTION;

  SELECT ss.account_id, ss.character_id, ss.realm_id, ss.world_instance_id, ss.session_key, c.character_key
    INTO v_account_id, v_character_id, v_realm_id, v_world_instance_id, v_session_key, v_character_key
    FROM server_sessions ss
    JOIN characters c ON c.character_id = ss.character_id
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1
   FOR UPDATE;

  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'active session not found';
  END IF;

  SELECT event_id, event_type, event_class
    INTO v_existing_event_id, v_existing_type, v_existing_class
    FROM world_event_journal
   WHERE world_instance_id = v_world_instance_id
     AND idempotency_key = p_idempotency_key
   LIMIT 1
   FOR UPDATE;

  IF v_existing_event_id IS NOT NULL THEN
    IF v_existing_type <> 'world_item_removed' OR v_existing_class <> 'world_entity' THEN
      SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT = 'world item idempotency key reused with different event type/class';
    END IF;

    SELECT item_instance_id
      INTO p_item_instance_id
      FROM world_item_audit
     WHERE world_instance_id = v_world_instance_id
       AND idempotency_key = p_idempotency_key
     LIMIT 1;

    SET p_event_id = v_existing_event_id;

    UPDATE server_sessions
       SET last_seen_at = CURRENT_TIMESTAMP(6)
     WHERE session_id = p_session_id;

    COMMIT;
    LEAVE remove_proc;
  END IF;

  SET v_item_entity_key = TRIM(p_world_item_entity_key);

  SELECT world_entity_state_id, lifecycle_state
    INTO v_item_entity_state_id, v_world_lifecycle_before
    FROM world_entity_state
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = v_item_entity_key
     AND entity_kind = 'item'
   LIMIT 1
   FOR UPDATE;

  IF v_item_entity_state_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'world item entity not found';
  END IF;

  IF v_world_lifecycle_before <> 'active' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'world item entity is not active';
  END IF;

  SELECT ii.item_instance_id, ii.item_instance_key, ii.owner_type, ii.owner_id, ii.lifecycle_state, ii.quantity
    INTO v_item_instance_id, v_item_instance_key, v_owner_before_type, v_owner_before_id, v_lifecycle_before, v_quantity_before
    FROM item_instances ii
   WHERE ii.realm_id = v_realm_id
     AND ii.owner_type = 'world_entity'
     AND ii.lifecycle_state = 'active'
     AND (
          ii.item_instance_key LIKE CONCAT('%:world-item:', v_item_entity_key)
          OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload, '$.item_spawn_key')) = v_item_entity_key
          OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload, '$.entity_key')) = v_item_entity_key
     )
   ORDER BY ii.updated_at DESC
   LIMIT 1
   FOR UPDATE;

  IF v_item_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'active world item instance not found';
  END IF;

  SET v_payload = JSON_OBJECT(
    'session_key', v_session_key,
    'character_key', v_character_key,
    'world_item_entity_key', v_item_entity_key,
    'item_instance_key', v_item_instance_key,
    'amount', v_quantity_before,
    'reason', v_reason,
    'owner_before_type', v_owner_before_type,
    'owner_after_type', 'system',
    'world_lifecycle_before', v_world_lifecycle_before,
    'world_lifecycle_after', 'removed',
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_instance_id,
    v_character_id,
    'world_item_removed',
    'world_entity',
    GREATEST(0, COALESCE(p_server_tick, 0)),
    v_item_entity_key,
    v_reason,
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  UPDATE item_instances
     SET owner_type = 'system',
         owner_id = NULL,
         lifecycle_state = 'archived',
         raw_payload = JSON_MERGE_PATCH(
           raw_payload,
           JSON_OBJECT(
             'last_event_type', 'world_item_removed',
             'last_event_id', BIN_TO_UUID(p_event_id, 1),
             'removed_by_character_key', v_character_key,
             'remove_reason', v_reason,
             'removed_at_server_tick', GREATEST(0, COALESCE(p_server_tick, 0))
           )
         )
   WHERE item_instance_id = v_item_instance_id;

  UPDATE world_entity_state
     SET lifecycle_state = 'removed',
         row_version = row_version + 1,
         state_json = JSON_MERGE_PATCH(
           state_json,
           JSON_OBJECT(
             'last_event_type', 'world_item_removed',
             'last_event_id', BIN_TO_UUID(p_event_id, 1),
             'removed_by_character_key', v_character_key,
             'remove_reason', v_reason,
             'removed_item_instance_key', v_item_instance_key,
             'removed_at_server_tick', GREATEST(0, COALESCE(p_server_tick, 0))
           )
         )
   WHERE world_entity_state_id = v_item_entity_state_id;

  INSERT INTO world_item_audit(
    audit_type, session_id, character_id, world_instance_id, event_id, idempotency_key,
    world_item_entity_key, item_instance_id, item_instance_key, amount,
    owner_before_type, owner_before_id, owner_after_type, owner_after_id,
    lifecycle_before, lifecycle_after, world_lifecycle_before, world_lifecycle_after,
    server_tick, raw_delta
  ) VALUES (
    'remove', p_session_id, v_character_id, v_world_instance_id, p_event_id, p_idempotency_key,
    v_item_entity_key, v_item_instance_id, v_item_instance_key, GREATEST(1, v_quantity_before),
    v_owner_before_type, v_owner_before_id, 'system', NULL,
    v_lifecycle_before, 'archived', v_world_lifecycle_before, 'removed',
    GREATEST(0, COALESCE(p_server_tick, 0)), v_payload
  );

  UPDATE realm_world_instances
     SET current_tick = GREATEST(current_tick, GREATEST(0, COALESCE(p_server_tick, 0)))
   WHERE world_instance_id = v_world_instance_id;

  UPDATE server_sessions
     SET last_seen_at = CURRENT_TIMESTAMP(6)
   WHERE session_id = p_session_id;

  SET p_item_instance_id = v_item_instance_id;

  COMMIT;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_pickable_world_items AS
SELECT
  BIN_TO_UUID(wes.world_entity_state_id, 1) AS world_entity_state_id,
  BIN_TO_UUID(wes.world_instance_id, 1) AS world_instance_id,
  r.realm_key,
  wi.world_instance_key,
  wes.entity_key AS world_item_entity_key,
  wes.lifecycle_state AS world_lifecycle_state,
  BIN_TO_UUID(ii.item_instance_id, 1) AS item_instance_id,
  ii.item_instance_key,
  ii.quantity,
  cit.item_template_key,
  cit.display_name,
  wes.pos_x,
  wes.pos_y,
  wes.pos_z,
  wes.updated_at
FROM world_entity_state wes
JOIN realm_world_instances wi ON wi.world_instance_id = wes.world_instance_id
JOIN realm_realms r ON r.realm_id = wi.realm_id
LEFT JOIN item_instances ii
  ON ii.realm_id = r.realm_id
 AND ii.owner_type = 'world_entity'
 AND ii.lifecycle_state = 'active'
 AND (
      ii.item_instance_key LIKE CONCAT('%:world-item:', wes.entity_key)
      OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload, '$.item_spawn_key')) = wes.entity_key
      OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload, '$.entity_key')) = wes.entity_key
 )
LEFT JOIN content_item_templates cit ON cit.item_template_id = ii.item_template_id
WHERE wes.entity_kind = 'item'
  AND wes.lifecycle_state = 'active';

CREATE OR REPLACE VIEW v_world_item_audit AS
SELECT
  BIN_TO_UUID(wia.world_item_audit_id, 1) AS world_item_audit_id,
  wia.audit_type,
  BIN_TO_UUID(wia.event_id, 1) AS event_id,
  ss.session_key,
  c.character_key,
  c.character_name,
  r.realm_key,
  wi.world_instance_key,
  wia.world_item_entity_key,
  BIN_TO_UUID(wia.item_instance_id, 1) AS item_instance_id,
  wia.item_instance_key,
  wia.amount,
  wia.owner_before_type,
  IF(wia.owner_before_id IS NULL, NULL, BIN_TO_UUID(wia.owner_before_id, 1)) AS owner_before_id,
  wia.owner_after_type,
  IF(wia.owner_after_id IS NULL, NULL, BIN_TO_UUID(wia.owner_after_id, 1)) AS owner_after_id,
  wia.lifecycle_before,
  wia.lifecycle_after,
  wia.world_lifecycle_before,
  wia.world_lifecycle_after,
  wia.server_tick,
  wia.created_at
FROM world_item_audit wia
LEFT JOIN server_sessions ss ON ss.session_id = wia.session_id
JOIN characters c ON c.character_id = wia.character_id
JOIN realm_world_instances wi ON wi.world_instance_id = wia.world_instance_id
JOIN realm_realms r ON r.realm_id = wi.realm_id;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES(
  'production/mysql/005_world_item_write_path',
  'gothic-mmo-world-item-write-path-v1-mysql',
  'Adds server-owned loose world item pickup/removal semantic event write path, audit table, idempotent procedures and read views.'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);
