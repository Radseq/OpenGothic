-- Gothic MMO MySQL production migration 007.
-- Server-owned container inventory and interactive state write path.
-- Requires 001..006 MySQL production migrations and a bootstrap import.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

-- -----------------------------------------------------------------------------
-- Container / interactive audit.
-- world_inventory, character_inventory, item_instances and world_entity_state are
-- current-state projections. world_event_journal remains the ordered durable
-- mutation source.
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS world_interactive_audit (
  interactive_audit_id     BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  audit_type               VARCHAR(32) NOT NULL,
  session_id               BINARY(16) NULL,
  character_id             BINARY(16) NOT NULL,
  world_instance_id        BINARY(16) NOT NULL,
  event_id                 BINARY(16) NOT NULL,
  idempotency_key          VARCHAR(191) NOT NULL,
  entity_key               VARCHAR(191) NOT NULL,
  item_instance_id         BINARY(16) NULL,
  item_instance_key        VARCHAR(191) NULL,
  amount                   INT NULL,
  owner_before_type        VARCHAR(32) NULL,
  owner_after_type         VARCHAR(32) NULL,
  row_version_before       BIGINT NULL,
  row_version_after        BIGINT NULL,
  server_tick              BIGINT NOT NULL DEFAULT 0,
  state_before             JSON NULL,
  state_after              JSON NULL,
  raw_delta                JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at               TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY world_interactive_audit_idempotency_uk(world_instance_id, idempotency_key),
  KEY ix_world_interactive_audit_character(character_id, created_at),
  KEY ix_world_interactive_audit_entity(world_instance_id, entity_key, created_at),
  KEY ix_world_interactive_audit_item(item_instance_id),
  KEY ix_world_interactive_audit_event(event_id),
  CONSTRAINT world_interactive_audit_session_fk FOREIGN KEY(session_id) REFERENCES server_sessions(session_id) ON DELETE SET NULL,
  CONSTRAINT world_interactive_audit_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT world_interactive_audit_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE RESTRICT,
  CONSTRAINT world_interactive_audit_event_fk FOREIGN KEY(event_id) REFERENCES world_event_journal(event_id) ON DELETE RESTRICT,
  CONSTRAINT world_interactive_audit_item_fk FOREIGN KEY(item_instance_id) REFERENCES item_instances(item_instance_id) ON DELETE RESTRICT,
  CONSTRAINT world_interactive_audit_type_ck CHECK(audit_type IN ('container_take','container_put','interactive_state')),
  CONSTRAINT world_interactive_audit_amount_ck CHECK(amount IS NULL OR amount > 0),
  CONSTRAINT world_interactive_audit_version_ck CHECK((row_version_before IS NULL OR row_version_before >= 0) AND (row_version_after IS NULL OR row_version_after >= 0)),
  CONSTRAINT world_interactive_audit_tick_ck CHECK(server_tick >= 0),
  CONSTRAINT world_interactive_audit_state_before_json_ck CHECK(state_before IS NULL OR JSON_VALID(state_before)),
  CONSTRAINT world_interactive_audit_state_after_json_ck CHECK(state_after IS NULL OR JSON_VALID(state_after)),
  CONSTRAINT world_interactive_audit_raw_json_ck CHECK(JSON_VALID(raw_delta))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_take_container_item;
DELIMITER $$
CREATE PROCEDURE mmo_take_container_item(
  IN  p_session_id       BINARY(16),
  IN  p_owner_entity_key VARCHAR(191),
  IN  p_item_instance_id BINARY(16),
  IN  p_target_bag_index INT,
  IN  p_server_tick      BIGINT,
  IN  p_metadata         JSON,
  IN  p_idempotency_key  VARCHAR(191),
  OUT p_event_id         BINARY(16),
  OUT p_amount_taken     INT
)
take_proc: BEGIN
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_entity_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_item_instance_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_owner_before_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_lifecycle_state VARCHAR(32) DEFAULT NULL;
  DECLARE v_inventory_amount INT DEFAULT 0;
  DECLARE v_existing_audit_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_existing_event_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_amount INT DEFAULT NULL;
  DECLARE v_payload JSON;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET p_event_id = NULL;
  SET p_amount_taken = NULL;

  IF p_session_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'session id is required';
  END IF;
  IF p_owner_entity_key IS NULL OR TRIM(p_owner_entity_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'container owner entity key is required';
  END IF;
  IF p_item_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'item instance id is required';
  END IF;
  IF p_target_bag_index IS NOT NULL AND p_target_bag_index < 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'target bag index must be null or non-negative';
  END IF;
  IF p_idempotency_key IS NULL OR TRIM(p_idempotency_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'container idempotency key is required';
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

  SELECT audit_type, event_id, amount
    INTO v_existing_audit_type, v_existing_event_id, v_existing_amount
    FROM world_interactive_audit
   WHERE world_instance_id = v_world_instance_id
     AND idempotency_key = p_idempotency_key
   LIMIT 1
   FOR UPDATE;

  IF v_existing_event_id IS NOT NULL THEN
    IF v_existing_audit_type <> 'container_take' THEN
      SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT = 'idempotency key reused with different interactive audit type';
    END IF;
    SET p_event_id = v_existing_event_id;
    SET p_amount_taken = v_existing_amount;
    COMMIT;
    LEAVE take_proc;
  END IF;

  SELECT entity_key
    INTO v_entity_key
    FROM world_entity_state
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = p_owner_entity_key
     AND entity_kind = 'interactive'
     AND lifecycle_state = 'active'
   LIMIT 1
   FOR UPDATE;

  IF v_entity_key IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'active interactive/container entity not found in session world';
  END IF;

  SELECT ii.item_instance_key, ii.owner_type, ii.lifecycle_state, wi.amount
    INTO v_item_instance_key, v_owner_before_type, v_lifecycle_state, v_inventory_amount
    FROM world_inventory wi
    JOIN item_instances ii ON ii.item_instance_id = wi.item_instance_id
   WHERE wi.world_instance_id = v_world_instance_id
     AND wi.owner_entity_key = p_owner_entity_key
     AND wi.item_instance_id = p_item_instance_id
   LIMIT 1
   FOR UPDATE;

  IF v_item_instance_key IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'container does not own this item';
  END IF;
  IF v_owner_before_type <> 'container' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'item instance owner is not container';
  END IF;
  IF v_lifecycle_state <> 'active' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'container item is not active';
  END IF;
  IF v_inventory_amount <= 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'container amount must be positive';
  END IF;

  SET v_payload = JSON_OBJECT(
    'character_key', v_character_key,
    'owner_entity_key', p_owner_entity_key,
    'item_instance_key', v_item_instance_key,
    'amount', v_inventory_amount,
    'target_bag_index', p_target_bag_index,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_instance_id,
    v_character_id,
    'container_item_taken',
    'inventory',
    COALESCE(p_server_tick, 0),
    p_owner_entity_key,
    v_item_instance_key,
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  DELETE FROM world_inventory
   WHERE world_instance_id = v_world_instance_id
     AND owner_entity_key = p_owner_entity_key
     AND item_instance_id = p_item_instance_id;

  UPDATE item_instances
     SET owner_type = 'character',
         owner_id = v_character_id,
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE item_instance_id = p_item_instance_id;

  INSERT INTO character_inventory(character_id, item_instance_id, bag_index, amount, source_amount, source_iterator_count)
  VALUES(v_character_id, p_item_instance_id, p_target_bag_index, v_inventory_amount, NULL, NULL);

  INSERT INTO world_interactive_audit(
    audit_type, session_id, character_id, world_instance_id, event_id, idempotency_key,
    entity_key, item_instance_id, item_instance_key, amount, owner_before_type, owner_after_type,
    server_tick, raw_delta
  ) VALUES(
    'container_take', p_session_id, v_character_id, v_world_instance_id, p_event_id, p_idempotency_key,
    p_owner_entity_key, p_item_instance_id, v_item_instance_key, v_inventory_amount, 'container', 'character',
    COALESCE(p_server_tick, 0), v_payload
  );

  UPDATE realm_world_instances
     SET current_tick = GREATEST(current_tick, COALESCE(p_server_tick, 0)),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id;

  UPDATE server_sessions
     SET last_seen_at = CURRENT_TIMESTAMP(6),
         metadata = JSON_SET(COALESCE(metadata, JSON_OBJECT()), '$.last_container_event', BIN_TO_UUID(p_event_id, 1))
   WHERE session_id = p_session_id;

  SET p_amount_taken = v_inventory_amount;
  COMMIT;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_put_container_item;
DELIMITER $$
CREATE PROCEDURE mmo_put_container_item(
  IN  p_session_id       BINARY(16),
  IN  p_owner_entity_key VARCHAR(191),
  IN  p_item_instance_id BINARY(16),
  IN  p_server_tick      BIGINT,
  IN  p_metadata         JSON,
  IN  p_idempotency_key  VARCHAR(191),
  OUT p_event_id         BINARY(16),
  OUT p_amount_put       INT
)
put_proc: BEGIN
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_entity_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_item_instance_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_owner_before_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_owner_before_id BINARY(16) DEFAULT NULL;
  DECLARE v_lifecycle_state VARCHAR(32) DEFAULT NULL;
  DECLARE v_inventory_amount INT DEFAULT 0;
  DECLARE v_existing_audit_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_existing_event_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_amount INT DEFAULT NULL;
  DECLARE v_payload JSON;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET p_event_id = NULL;
  SET p_amount_put = NULL;

  IF p_session_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'session id is required';
  END IF;
  IF p_owner_entity_key IS NULL OR TRIM(p_owner_entity_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'container owner entity key is required';
  END IF;
  IF p_item_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'item instance id is required';
  END IF;
  IF p_idempotency_key IS NULL OR TRIM(p_idempotency_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'container idempotency key is required';
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

  SELECT audit_type, event_id, amount
    INTO v_existing_audit_type, v_existing_event_id, v_existing_amount
    FROM world_interactive_audit
   WHERE world_instance_id = v_world_instance_id
     AND idempotency_key = p_idempotency_key
   LIMIT 1
   FOR UPDATE;

  IF v_existing_event_id IS NOT NULL THEN
    IF v_existing_audit_type <> 'container_put' THEN
      SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT = 'idempotency key reused with different interactive audit type';
    END IF;
    SET p_event_id = v_existing_event_id;
    SET p_amount_put = v_existing_amount;
    COMMIT;
    LEAVE put_proc;
  END IF;

  SELECT entity_key
    INTO v_entity_key
    FROM world_entity_state
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = p_owner_entity_key
     AND entity_kind = 'interactive'
     AND lifecycle_state = 'active'
   LIMIT 1
   FOR UPDATE;

  IF v_entity_key IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'active interactive/container entity not found in session world';
  END IF;

  SELECT ii.item_instance_key, ii.owner_type, ii.owner_id, ii.lifecycle_state, ci.amount
    INTO v_item_instance_key, v_owner_before_type, v_owner_before_id, v_lifecycle_state, v_inventory_amount
    FROM character_inventory ci
    JOIN item_instances ii ON ii.item_instance_id = ci.item_instance_id
   WHERE ci.character_id = v_character_id
     AND ci.item_instance_id = p_item_instance_id
   LIMIT 1
   FOR UPDATE;

  IF v_item_instance_key IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'character does not own this inventory item';
  END IF;
  IF v_owner_before_type <> 'character' OR v_owner_before_id <> v_character_id THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'item instance owner does not match character';
  END IF;
  IF v_lifecycle_state <> 'active' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'character item is not active';
  END IF;
  IF v_inventory_amount <= 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'inventory amount must be positive';
  END IF;

  SET v_payload = JSON_OBJECT(
    'character_key', v_character_key,
    'owner_entity_key', p_owner_entity_key,
    'item_instance_key', v_item_instance_key,
    'amount', v_inventory_amount,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_instance_id,
    v_character_id,
    'container_item_put',
    'inventory',
    COALESCE(p_server_tick, 0),
    p_owner_entity_key,
    v_item_instance_key,
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  DELETE FROM character_equipment
   WHERE character_id = v_character_id
     AND item_instance_id = p_item_instance_id;

  DELETE FROM character_inventory
   WHERE character_id = v_character_id
     AND item_instance_id = p_item_instance_id;

  UPDATE item_instances
     SET owner_type = 'container',
         owner_id = NULL,
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE item_instance_id = p_item_instance_id;

  INSERT INTO world_inventory(world_instance_id, owner_entity_key, item_instance_id, amount, source_amount, source_iterator_count)
  VALUES(v_world_instance_id, p_owner_entity_key, p_item_instance_id, v_inventory_amount, NULL, NULL);

  INSERT INTO world_interactive_audit(
    audit_type, session_id, character_id, world_instance_id, event_id, idempotency_key,
    entity_key, item_instance_id, item_instance_key, amount, owner_before_type, owner_after_type,
    server_tick, raw_delta
  ) VALUES(
    'container_put', p_session_id, v_character_id, v_world_instance_id, p_event_id, p_idempotency_key,
    p_owner_entity_key, p_item_instance_id, v_item_instance_key, v_inventory_amount, 'character', 'container',
    COALESCE(p_server_tick, 0), v_payload
  );

  UPDATE realm_world_instances
     SET current_tick = GREATEST(current_tick, COALESCE(p_server_tick, 0)),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id;

  UPDATE server_sessions
     SET last_seen_at = CURRENT_TIMESTAMP(6),
         metadata = JSON_SET(COALESCE(metadata, JSON_OBJECT()), '$.last_container_event', BIN_TO_UUID(p_event_id, 1))
   WHERE session_id = p_session_id;

  SET p_amount_put = v_inventory_amount;
  COMMIT;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_update_interactive_state;
DELIMITER $$
CREATE PROCEDURE mmo_update_interactive_state(
  IN  p_session_id       BINARY(16),
  IN  p_entity_key       VARCHAR(191),
  IN  p_state_id         INT,
  IN  p_state_count      INT,
  IN  p_state_mask       BIGINT,
  IN  p_locked           BOOLEAN,
  IN  p_cracked          BOOLEAN,
  IN  p_lifecycle_state  VARCHAR(32),
  IN  p_server_tick      BIGINT,
  IN  p_metadata         JSON,
  IN  p_idempotency_key  VARCHAR(191),
  OUT p_event_id         BINARY(16),
  OUT p_row_version_after BIGINT
)
interactive_proc: BEGIN
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_existing_audit_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_existing_event_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_row_version_after BIGINT DEFAULT NULL;
  DECLARE v_state_before JSON DEFAULT NULL;
  DECLARE v_state_after JSON DEFAULT NULL;
  DECLARE v_lifecycle_before VARCHAR(32) DEFAULT NULL;
  DECLARE v_lifecycle_after VARCHAR(32) DEFAULT NULL;
  DECLARE v_row_version_before BIGINT DEFAULT NULL;
  DECLARE v_payload JSON;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET p_event_id = NULL;
  SET p_row_version_after = NULL;

  IF p_session_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'session id is required';
  END IF;
  IF p_entity_key IS NULL OR TRIM(p_entity_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'interactive entity key is required';
  END IF;
  IF p_idempotency_key IS NULL OR TRIM(p_idempotency_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'interactive idempotency key is required';
  END IF;
  IF p_lifecycle_state IS NOT NULL AND p_lifecycle_state NOT IN ('active','dead','removed','disabled','consumed','archived') THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'invalid interactive lifecycle state';
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

  SELECT audit_type, event_id, row_version_after
    INTO v_existing_audit_type, v_existing_event_id, v_existing_row_version_after
    FROM world_interactive_audit
   WHERE world_instance_id = v_world_instance_id
     AND idempotency_key = p_idempotency_key
   LIMIT 1
   FOR UPDATE;

  IF v_existing_event_id IS NOT NULL THEN
    IF v_existing_audit_type <> 'interactive_state' THEN
      SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT = 'idempotency key reused with different interactive audit type';
    END IF;
    SET p_event_id = v_existing_event_id;
    SET p_row_version_after = v_existing_row_version_after;
    COMMIT;
    LEAVE interactive_proc;
  END IF;

  SELECT lifecycle_state, state_json, row_version
    INTO v_lifecycle_before, v_state_before, v_row_version_before
    FROM world_entity_state
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = p_entity_key
     AND entity_kind = 'interactive'
   LIMIT 1
   FOR UPDATE;

  IF v_lifecycle_before IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'interactive entity not found in session world';
  END IF;

  SET v_lifecycle_after = COALESCE(p_lifecycle_state, v_lifecycle_before);
  SET v_state_after = JSON_SET(
    COALESCE(v_state_before, JSON_OBJECT()),
    '$.state_id', p_state_id,
    '$.state_count', p_state_count,
    '$.state_mask', p_state_mask,
    '$.locked', IF(p_locked IS NULL, NULL, IF(p_locked, 1, 0)),
    '$.cracked', IF(p_cracked IS NULL, NULL, IF(p_cracked, 1, 0)),
    '$.metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  SET v_payload = JSON_OBJECT(
    'character_key', v_character_key,
    'entity_key', p_entity_key,
    'lifecycle_before', v_lifecycle_before,
    'lifecycle_after', v_lifecycle_after,
    'state_before', COALESCE(v_state_before, JSON_OBJECT()),
    'state_after', v_state_after,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_instance_id,
    v_character_id,
    'interactive_state_changed',
    'world_entity',
    COALESCE(p_server_tick, 0),
    p_entity_key,
    NULL,
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  UPDATE world_entity_state
     SET lifecycle_state = v_lifecycle_after,
         state_json = v_state_after,
         row_version = row_version + 1,
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = p_entity_key;

  SET p_row_version_after = v_row_version_before + 1;

  INSERT INTO world_interactive_audit(
    audit_type, session_id, character_id, world_instance_id, event_id, idempotency_key,
    entity_key, item_instance_id, item_instance_key, amount, owner_before_type, owner_after_type,
    row_version_before, row_version_after, server_tick, state_before, state_after, raw_delta
  ) VALUES(
    'interactive_state', p_session_id, v_character_id, v_world_instance_id, p_event_id, p_idempotency_key,
    p_entity_key, NULL, NULL, NULL, NULL, NULL,
    v_row_version_before, p_row_version_after, COALESCE(p_server_tick, 0), COALESCE(v_state_before, JSON_OBJECT()), v_state_after, v_payload
  );

  UPDATE realm_world_instances
     SET current_tick = GREATEST(current_tick, COALESCE(p_server_tick, 0)),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id;

  UPDATE server_sessions
     SET last_seen_at = CURRENT_TIMESTAMP(6),
         metadata = JSON_SET(COALESCE(metadata, JSON_OBJECT()), '$.last_interactive_event', BIN_TO_UUID(p_event_id, 1))
   WHERE session_id = p_session_id;

  COMMIT;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_world_container_inventory_state AS
SELECT
  r.realm_key,
  wi.world_instance_id,
  w.world_instance_key,
  wi.owner_entity_key,
  wes.lifecycle_state AS owner_lifecycle_state,
  ii.item_instance_id,
  ii.item_instance_key,
  ii.owner_type,
  ii.lifecycle_state AS item_lifecycle_state,
  it.item_template_key,
  it.display_name AS item_display_name,
  wi.amount,
  wi.source_amount,
  wi.source_iterator_count,
  wi.updated_at
FROM world_inventory wi
JOIN realm_world_instances w ON w.world_instance_id = wi.world_instance_id
JOIN realm_realms r ON r.realm_id = w.realm_id
LEFT JOIN world_entity_state wes ON wes.world_instance_id = wi.world_instance_id AND wes.entity_key = wi.owner_entity_key
JOIN item_instances ii ON ii.item_instance_id = wi.item_instance_id
JOIN content_item_templates it ON it.item_template_id = ii.item_template_id;

CREATE OR REPLACE VIEW v_world_interactive_state AS
SELECT
  r.realm_key,
  wes.world_instance_id,
  w.world_instance_key,
  wes.entity_key,
  wes.lifecycle_state,
  JSON_UNQUOTE(JSON_EXTRACT(wes.state_json, '$.state_id')) AS state_id,
  JSON_UNQUOTE(JSON_EXTRACT(wes.state_json, '$.state_count')) AS state_count,
  JSON_UNQUOTE(JSON_EXTRACT(wes.state_json, '$.state_mask')) AS state_mask,
  JSON_UNQUOTE(JSON_EXTRACT(wes.state_json, '$.locked')) AS locked,
  JSON_UNQUOTE(JSON_EXTRACT(wes.state_json, '$.cracked')) AS cracked,
  wes.row_version,
  wes.updated_at
FROM world_entity_state wes
JOIN realm_world_instances w ON w.world_instance_id = wes.world_instance_id
JOIN realm_realms r ON r.realm_id = w.realm_id
WHERE wes.entity_kind = 'interactive';

CREATE OR REPLACE VIEW v_world_interactive_audit AS
SELECT
  a.audit_type,
  BIN_TO_UUID(a.event_id, 1) AS event_id,
  a.idempotency_key,
  c.character_key,
  a.entity_key,
  BIN_TO_UUID(a.item_instance_id, 1) AS item_instance_id,
  a.item_instance_key,
  a.amount,
  a.owner_before_type,
  a.owner_after_type,
  a.row_version_before,
  a.row_version_after,
  a.server_tick,
  a.created_at
FROM world_interactive_audit a
JOIN characters c ON c.character_id = a.character_id;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES(
  'production/mysql/007_container_interactive_write_path',
  'gothic-mmo-container-interactive-write-path-v1-mysql',
  'Adds server-owned container take/put and interactive state procedures with idempotent audit.'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);
