-- Step59 clean-DB item/interactive/progress bridge.
--
-- Purpose:
--   Fresh Step55 clean MySQL rebuilds currently install the live receiver bridge
--   and Step56b script/dialog/quest bridge, but Step58 live testing showed the
--   resolved worker still misses item pickup, interactive state and progression
--   routines. These procedures are additive dev-authority bridge surfaces; they
--   keep the old single-player path untouched and only run when the external
--   server worker calls them.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS world_item_audit (
  audit_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) NULL,
  world_instance_id BINARY(16) NOT NULL,
  character_id BINARY(16) NULL,
  entity_key VARCHAR(512) NOT NULL,
  item_instance_id BINARY(16) NULL,
  audit_type VARCHAR(64) NOT NULL,
  amount INT NOT NULL DEFAULT 0,
  idempotency_key VARCHAR(512) NOT NULL,
  metadata JSON NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (audit_id),
  UNIQUE KEY ux_world_item_audit_idempotency (idempotency_key),
  KEY ix_world_item_audit_entity (world_instance_id, entity_key),
  KEY ix_world_item_audit_event (event_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS world_interactive_audit (
  audit_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) NULL,
  world_instance_id BINARY(16) NOT NULL,
  character_id BINARY(16) NULL,
  entity_key VARCHAR(512) NOT NULL,
  audit_type VARCHAR(64) NOT NULL,
  state_after INT NULL,
  row_version_after BIGINT NULL,
  idempotency_key VARCHAR(512) NOT NULL,
  metadata JSON NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (audit_id),
  UNIQUE KEY ux_world_interactive_audit_idempotency (idempotency_key),
  KEY ix_world_interactive_audit_entity (world_instance_id, entity_key),
  KEY ix_world_interactive_audit_event (event_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS character_progress_audit (
  audit_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  event_id BINARY(16) NULL,
  character_id BINARY(16) NOT NULL,
  audit_type VARCHAR(64) NOT NULL,
  experience_delta INT NOT NULL DEFAULT 0,
  learning_points_delta INT NOT NULL DEFAULT 0,
  experience_after INT NOT NULL DEFAULT 0,
  learning_points_after INT NOT NULL DEFAULT 0,
  reason VARCHAR(128) NULL,
  idempotency_key VARCHAR(512) NOT NULL,
  metadata JSON NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (audit_id),
  UNIQUE KEY ux_character_progress_audit_idempotency (idempotency_key),
  KEY ix_character_progress_audit_character (character_id),
  KEY ix_character_progress_audit_event (event_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

SET @step59_migration_key = 'dev/sql/step59_clean_db_item_interactive_progress_bridge';
SET @step59_schema_contract = 'gothic-mmo-step59-clean-db-item-interactive-progress-bridge-v2';
SET @step59_schema_notes = 'Installs item pickup/removal, interactive-state and progression bridge procedures; compatible with clean DBs with or without mmo_schema_versions.notes.';
SET @step59_has_schema_notes = (
  SELECT COUNT(*)
    FROM information_schema.COLUMNS
   WHERE TABLE_SCHEMA = DATABASE()
     AND TABLE_NAME = 'mmo_schema_versions'
     AND COLUMN_NAME = 'notes'
);
SET @step59_schema_sql = IF(
  @step59_has_schema_notes > 0,
  CONCAT(
    'INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes) VALUES (',
    QUOTE(@step59_migration_key), ', ', QUOTE(@step59_schema_contract), ', ', QUOTE(@step59_schema_notes),
    ') ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes)'
  ),
  CONCAT(
    'INSERT INTO mmo_schema_versions(migration_key, schema_contract) VALUES (',
    QUOTE(@step59_migration_key), ', ', QUOTE(@step59_schema_contract),
    ') ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract)'
  )
);
PREPARE step59_schema_stmt FROM @step59_schema_sql;
EXECUTE step59_schema_stmt;
DEALLOCATE PREPARE step59_schema_stmt;

DROP PROCEDURE IF EXISTS mmo_pickup_world_item;
DROP PROCEDURE IF EXISTS mmo_remove_world_item;
DROP PROCEDURE IF EXISTS mmo_update_interactive_state;
DROP PROCEDURE IF EXISTS mmo_adjust_character_progression;
DROP PROCEDURE IF EXISTS mmo_apply_character_experience_reward;

DELIMITER $$

CREATE PROCEDURE mmo_pickup_world_item(
  IN p_session_id BINARY(16),
  IN p_world_item_entity_key VARCHAR(512),
  IN p_amount_requested INT,
  IN p_bag_index INT,
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_item_instance_id BINARY(16),
  OUT o_amount_picked INT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_amount INT DEFAULT 1;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SELECT event_id, item_instance_id, amount
    INTO o_event_id, o_item_instance_id, o_amount_picked
    FROM world_item_audit
   WHERE idempotency_key = p_idempotency_key
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    LEAVE proc;
  END IF;

  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_pickup_world_item: active session not found';
  END IF;

  SELECT ii.item_instance_id, GREATEST(1, LEAST(COALESCE(NULLIF(p_amount_requested, 0), ii.quantity, 1), COALESCE(ii.quantity, 1)))
    INTO o_item_instance_id, v_amount
    FROM item_instances ii
   WHERE ii.realm_id = v_realm_id
     AND ii.owner_type = 'world_entity'
     AND ii.lifecycle_state = 'active'
     AND (
          JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload, '$.item_spawn_key')) = p_world_item_entity_key
          OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload, '$.entity_key')) = p_world_item_entity_key
          OR ii.item_instance_key LIKE CONCAT('%', p_world_item_entity_key, '%')
     )
   ORDER BY ii.updated_at DESC, ii.item_instance_key ASC
   LIMIT 1;
  IF o_item_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_pickup_world_item: world item instance not found';
  END IF;

  START TRANSACTION;

  UPDATE world_entity_state
     SET lifecycle_state = 'removed',
         row_version = COALESCE(row_version, 0) + 1,
         state_json = JSON_MERGE_PATCH(
           COALESCE(state_json, JSON_OBJECT()),
           JSON_OBJECT('exists_in_world', false, 'picked_by_character', BIN_TO_UUID(v_character_id, 1), 'picked_at_tick', p_server_tick)
         ),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = p_world_item_entity_key
     AND entity_kind = 'item'
     AND lifecycle_state = 'active';
  IF ROW_COUNT() <> 1 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_pickup_world_item: active world item entity not found';
  END IF;

  UPDATE item_instances
     SET owner_type = 'character',
         owner_id = v_character_id,
         quantity = v_amount,
         lifecycle_state = 'active',
         raw_payload = JSON_MERGE_PATCH(
           COALESCE(raw_payload, JSON_OBJECT()),
           JSON_OBJECT('picked_from_world_entity_key', p_world_item_entity_key, 'picked_at_tick', p_server_tick)
         ),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE item_instance_id = o_item_instance_id;

  INSERT INTO character_inventory(character_id, item_instance_id, bag_index, amount, source_amount, source_iterator_count)
  VALUES(v_character_id, o_item_instance_id, p_bag_index, v_amount, v_amount, v_amount)
  ON DUPLICATE KEY UPDATE
    bag_index = COALESCE(character_inventory.bag_index, VALUES(bag_index)),
    amount = character_inventory.amount + VALUES(amount),
    source_amount = VALUES(source_amount),
    source_iterator_count = VALUES(source_iterator_count);

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'world_item_picked_up', 'inventory', p_server_tick, p_world_item_entity_key, NULL,
    JSON_OBJECT('world_item_entity_key', p_world_item_entity_key, 'item_instance_id', BIN_TO_UUID(o_item_instance_id, 1), 'amount', v_amount, 'bag_index', p_bag_index, 'metadata', p_metadata),
    p_idempotency_key, 'server', NULL, NULL, o_event_id
  );

  INSERT INTO world_item_audit(event_id, world_instance_id, character_id, entity_key, item_instance_id, audit_type, amount, idempotency_key, metadata)
  VALUES(o_event_id, v_world_instance_id, v_character_id, p_world_item_entity_key, o_item_instance_id, 'pickup', v_amount, p_idempotency_key, p_metadata);

  COMMIT;
  SET o_amount_picked = v_amount;
END$$

CREATE PROCEDURE mmo_remove_world_item(
  IN p_session_id BINARY(16),
  IN p_world_item_entity_key VARCHAR(512),
  IN p_reason VARCHAR(128),
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

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SELECT event_id, item_instance_id
    INTO o_event_id, o_item_instance_id
    FROM world_item_audit
   WHERE idempotency_key = p_idempotency_key
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    LEAVE proc;
  END IF;

  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_remove_world_item: active session not found';
  END IF;

  SELECT ii.item_instance_id
    INTO o_item_instance_id
    FROM item_instances ii
   WHERE ii.realm_id = v_realm_id
     AND ii.owner_type = 'world_entity'
     AND ii.lifecycle_state = 'active'
     AND (
          JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload, '$.item_spawn_key')) = p_world_item_entity_key
          OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload, '$.entity_key')) = p_world_item_entity_key
          OR ii.item_instance_key LIKE CONCAT('%', p_world_item_entity_key, '%')
     )
   ORDER BY ii.updated_at DESC, ii.item_instance_key ASC
   LIMIT 1;
  IF o_item_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_remove_world_item: world item instance not found';
  END IF;

  START TRANSACTION;

  UPDATE world_entity_state
     SET lifecycle_state = 'removed',
         row_version = COALESCE(row_version, 0) + 1,
         state_json = JSON_MERGE_PATCH(
           COALESCE(state_json, JSON_OBJECT()),
           JSON_OBJECT('exists_in_world', false, 'remove_reason', COALESCE(p_reason, 'semantic_action'), 'removed_at_tick', p_server_tick)
         ),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = p_world_item_entity_key
     AND entity_kind = 'item'
     AND lifecycle_state = 'active';
  IF ROW_COUNT() <> 1 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_remove_world_item: active world item entity not found';
  END IF;

  UPDATE item_instances
     SET owner_type = 'system',
         owner_id = NULL,
         lifecycle_state = 'archived',
         raw_payload = JSON_MERGE_PATCH(COALESCE(raw_payload, JSON_OBJECT()), JSON_OBJECT('removed_reason', COALESCE(p_reason, 'semantic_action'), 'removed_at_tick', p_server_tick)),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE item_instance_id = o_item_instance_id;

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'world_item_removed', 'world_entity', p_server_tick, p_world_item_entity_key, NULL,
    JSON_OBJECT('world_item_entity_key', p_world_item_entity_key, 'item_instance_id', BIN_TO_UUID(o_item_instance_id, 1), 'reason', COALESCE(p_reason, 'semantic_action'), 'metadata', p_metadata),
    p_idempotency_key, 'server', NULL, NULL, o_event_id
  );

  INSERT INTO world_item_audit(event_id, world_instance_id, character_id, entity_key, item_instance_id, audit_type, amount, idempotency_key, metadata)
  VALUES(o_event_id, v_world_instance_id, v_character_id, p_world_item_entity_key, o_item_instance_id, 'remove', 1, p_idempotency_key, p_metadata);

  COMMIT;
END$$

CREATE PROCEDURE mmo_update_interactive_state(
  IN p_session_id BINARY(16),
  IN p_interactive_entity_key VARCHAR(512),
  IN p_state_after INT,
  IN p_state_count INT,
  IN p_state_mask INT,
  IN p_locked_after BOOLEAN,
  IN p_cracked_after BOOLEAN,
  IN p_lifecycle_state VARCHAR(32),
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

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SELECT event_id, row_version_after
    INTO o_event_id, o_row_version_after
    FROM world_interactive_audit
   WHERE idempotency_key = p_idempotency_key
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    LEAVE proc;
  END IF;

  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_update_interactive_state: active session not found';
  END IF;

  START TRANSACTION;

  UPDATE world_entity_state
     SET lifecycle_state = COALESCE(NULLIF(p_lifecycle_state, ''), lifecycle_state),
         row_version = COALESCE(row_version, 0) + 1,
         state_json = JSON_MERGE_PATCH(
           COALESCE(state_json, JSON_OBJECT()),
           JSON_OBJECT(
             'state_id', p_state_after,
             'state_count', p_state_count,
             'state_mask', p_state_mask,
             'locked', IF(p_locked_after, true, false),
             'cracked', IF(p_cracked_after, true, false),
             'updated_at_tick', p_server_tick
           )
         ),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = p_interactive_entity_key
     AND entity_kind = 'interactive';
  IF ROW_COUNT() <> 1 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_update_interactive_state: interactive entity not found';
  END IF;

  SELECT row_version
    INTO o_row_version_after
    FROM world_entity_state
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = p_interactive_entity_key
   LIMIT 1;

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'interactive_state_changed', 'world_entity', p_server_tick, p_interactive_entity_key, NULL,
    JSON_OBJECT('interactive_entity_key', p_interactive_entity_key, 'state_after', p_state_after, 'state_count', p_state_count, 'state_mask', p_state_mask, 'locked_after', p_locked_after, 'cracked_after', p_cracked_after, 'row_version_after', o_row_version_after, 'metadata', p_metadata),
    p_idempotency_key, 'server', NULL, NULL, o_event_id
  );

  INSERT INTO world_interactive_audit(event_id, world_instance_id, character_id, entity_key, audit_type, state_after, row_version_after, idempotency_key, metadata)
  VALUES(o_event_id, v_world_instance_id, v_character_id, p_interactive_entity_key, 'interactive_state', p_state_after, o_row_version_after, p_idempotency_key, p_metadata);

  COMMIT;
END$$

CREATE PROCEDURE mmo_adjust_character_progression(
  IN p_session_id BINARY(16),
  IN p_experience_delta INT,
  IN p_learning_points_delta INT,
  IN p_reason VARCHAR(128),
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_experience_after INT,
  OUT o_learning_points_after INT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16);
  DECLARE v_world_instance_id BINARY(16);
  DECLARE v_character_id BINARY(16);
  DECLARE v_exp_delta INT DEFAULT 0;
  DECLARE v_lp_delta INT DEFAULT 0;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SELECT event_id, experience_after, learning_points_after
    INTO o_event_id, o_experience_after, o_learning_points_after
    FROM character_progress_audit
   WHERE idempotency_key = p_idempotency_key
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    LEAVE proc;
  END IF;

  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_adjust_character_progression: active session not found';
  END IF;

  SET v_exp_delta = COALESCE(p_experience_delta, 0);
  SET v_lp_delta = COALESCE(p_learning_points_delta, 0);

  START TRANSACTION;

  UPDATE character_stats
     SET experience = GREATEST(0, COALESCE(experience, 0) + v_exp_delta),
         learning_points = GREATEST(0, COALESCE(learning_points, 0) + v_lp_delta),
         row_version = COALESCE(row_version, 0) + 1,
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE character_id = v_character_id;
  IF ROW_COUNT() <> 1 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_adjust_character_progression: character stats not found';
  END IF;

  SELECT COALESCE(experience, 0), COALESCE(learning_points, 0)
    INTO o_experience_after, o_learning_points_after
    FROM character_stats
   WHERE character_id = v_character_id
   LIMIT 1;

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'character_progression_adjusted', 'character', p_server_tick, NULL, NULL,
    JSON_OBJECT('experience_delta', v_exp_delta, 'learning_points_delta', v_lp_delta, 'experience_after', o_experience_after, 'learning_points_after', o_learning_points_after, 'reason', COALESCE(p_reason, 'script_progression'), 'metadata', p_metadata),
    p_idempotency_key, 'server', NULL, NULL, o_event_id
  );

  INSERT INTO character_progress_audit(event_id, character_id, audit_type, experience_delta, learning_points_delta, experience_after, learning_points_after, reason, idempotency_key, metadata)
  VALUES(o_event_id, v_character_id, 'progression_adjust', v_exp_delta, v_lp_delta, o_experience_after, o_learning_points_after, p_reason, p_idempotency_key, p_metadata);

  COMMIT;
END$$

CREATE PROCEDURE mmo_apply_character_experience_reward(
  IN p_session_id BINARY(16),
  IN p_experience_delta INT,
  IN p_reason VARCHAR(128),
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_experience_after INT
)
BEGIN
  DECLARE v_learning_points_after INT DEFAULT 0;

  CALL mmo_adjust_character_progression(
    p_session_id,
    p_experience_delta,
    0,
    COALESCE(p_reason, 'script_experience_reward'),
    p_server_tick,
    p_metadata,
    p_idempotency_key,
    o_event_id,
    o_experience_after,
    v_learning_points_after
  );
END$$

DELIMITER ;
