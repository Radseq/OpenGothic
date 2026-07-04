-- Step83 combat/lifecycle bridge for clean MySQL dev DBs.
-- Adds minimal journal-backed procedures used by the C++ ASIO server direct DB path.

DELIMITER $$

DROP PROCEDURE IF EXISTS mmo_apply_character_damage $$
CREATE PROCEDURE mmo_apply_character_damage(
  IN p_session_id BINARY(16),
  IN p_target_character_key VARCHAR(191),
  IN p_damage_amount INT,
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_health_after INT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_actor_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_target_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_health_before INT DEFAULT NULL;
  DECLARE v_health_max INT DEFAULT NULL;
  DECLARE v_damage INT DEFAULT 0;
  DECLARE v_payload JSON;
  DECLARE v_not_found BOOL DEFAULT FALSE;

  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET o_event_id = NULL;
  SET o_health_after = NULL;

  SET v_not_found = FALSE;
  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_actor_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1;
  IF v_actor_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_apply_character_damage: active session not found';
  END IF;

  SET v_not_found = FALSE;
  SELECT c.character_id, c.character_key
    INTO v_target_character_id, v_character_key
    FROM characters c
   WHERE c.character_key = COALESCE(NULLIF(p_target_character_key, ''), 'PC_HERO')
   LIMIT 1;
  IF v_target_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_apply_character_damage: target character not found';
  END IF;

  SET v_not_found = FALSE;
  SELECT wej.event_id
    INTO o_event_id
    FROM world_event_journal wej
   WHERE wej.world_instance_id = v_world_instance_id
     AND wej.idempotency_key = LEFT(p_idempotency_key, 191)
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    SELECT cs.health_current INTO o_health_after
      FROM character_stats cs
     WHERE cs.character_id = v_target_character_id
     LIMIT 1;
    LEAVE proc;
  END IF;

  START TRANSACTION;

  SET v_not_found = FALSE;
  SELECT cs.health_current, cs.health_max
    INTO v_health_before, v_health_max
    FROM character_stats cs
   WHERE cs.character_id = v_target_character_id
   LIMIT 1
   FOR UPDATE;

  IF v_health_max IS NULL THEN
    SET v_health_max = GREATEST(0, COALESCE(v_health_before, 0));
  END IF;
  IF v_health_before IS NULL THEN
    SET v_health_before = v_health_max;
  END IF;

  SET v_damage = GREATEST(0, COALESCE(p_damage_amount, 0));
  SET o_health_after = GREATEST(0, v_health_before - v_damage);

  INSERT INTO character_stats(character_id, health_current, health_max)
  VALUES(v_target_character_id, o_health_after, GREATEST(v_health_max, o_health_after))
  ON DUPLICATE KEY UPDATE
    health_current = VALUES(health_current),
    health_max = GREATEST(character_stats.health_max, VALUES(health_max)),
    row_version = row_version + 1,
    updated_at = CURRENT_TIMESTAMP(6);

  SET v_payload = JSON_OBJECT(
    'target_character_key', v_character_key,
    'damage_amount', v_damage,
    'health_before', v_health_before,
    'health_after', o_health_after,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_actor_character_id,
    'character_damage_applied', 'combat', COALESCE(p_server_tick, 0),
    v_character_key, v_character_key, v_payload, LEFT(p_idempotency_key, 191),
    'server', NULL, NULL, o_event_id
  );

  COMMIT;
END $$

DROP PROCEDURE IF EXISTS mmo_apply_world_entity_damage $$
CREATE PROCEDURE mmo_apply_world_entity_damage(
  IN p_session_id BINARY(16),
  IN p_entity_key VARCHAR(512),
  IN p_damage_amount INT,
  IN p_fatal BOOL,
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_health_after INT,
  OUT o_row_version_after BIGINT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_entity_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_health_before INT DEFAULT NULL;
  DECLARE v_health_max INT DEFAULT NULL;
  DECLARE v_damage INT DEFAULT 0;
  DECLARE v_dead BOOL DEFAULT FALSE;
  DECLARE v_payload JSON;
  DECLARE v_not_found BOOL DEFAULT FALSE;

  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET o_event_id = NULL;
  SET o_health_after = NULL;
  SET o_row_version_after = NULL;
  SET v_entity_key = LEFT(COALESCE(NULLIF(p_entity_key, ''), 'unknown:npc'), 191);

  SET v_not_found = FALSE;
  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_apply_world_entity_damage: active session not found';
  END IF;

  SET v_not_found = FALSE;
  SELECT wej.event_id
    INTO o_event_id
    FROM world_event_journal wej
   WHERE wej.world_instance_id = v_world_instance_id
     AND wej.idempotency_key = LEFT(p_idempotency_key, 191)
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    SELECT wes.health_current, wes.row_version
      INTO o_health_after, o_row_version_after
      FROM world_entity_state wes
     WHERE wes.world_instance_id = v_world_instance_id
       AND wes.entity_key = v_entity_key
     LIMIT 1;
    LEAVE proc;
  END IF;

  START TRANSACTION;

  SET v_not_found = FALSE;
  SELECT wes.health_current, wes.health_max
    INTO v_health_before, v_health_max
    FROM world_entity_state wes
   WHERE wes.world_instance_id = v_world_instance_id
     AND wes.entity_key = v_entity_key
     AND wes.entity_kind IN ('npc', 'creature')
     AND wes.lifecycle_state = 'active'
   LIMIT 1
   FOR UPDATE;
  IF v_not_found THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_apply_world_entity_damage: active world entity not found';
  END IF;

  IF v_health_max IS NULL THEN
    SET v_health_max = GREATEST(0, COALESCE(v_health_before, 1));
  END IF;
  IF v_health_before IS NULL THEN
    SET v_health_before = v_health_max;
  END IF;

  SET v_damage = GREATEST(0, COALESCE(p_damage_amount, 0));
  SET o_health_after = GREATEST(0, v_health_before - v_damage);
  IF COALESCE(p_fatal, FALSE) THEN
    SET o_health_after = 0;
  END IF;
  SET v_dead = COALESCE(p_fatal, FALSE) OR o_health_after <= 0;

  UPDATE world_entity_state
     SET health_current = o_health_after,
         health_max = GREATEST(COALESCE(health_max, 0), GREATEST(v_health_max, o_health_after)),
         lifecycle_state = IF(v_dead, 'dead', lifecycle_state),
         row_version = row_version + 1,
         state_json = JSON_MERGE_PATCH(
           COALESCE(state_json, JSON_OBJECT()),
           JSON_OBJECT(
             'last_damage_amount', v_damage,
             'last_damage_tick', COALESCE(p_server_tick, 0),
             'last_damage_fatal', v_dead
           )
         ),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = v_entity_key;

  SELECT row_version INTO o_row_version_after
    FROM world_entity_state
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = v_entity_key
   LIMIT 1;

  SET v_payload = JSON_OBJECT(
    'entity_key', v_entity_key,
    'damage_amount', v_damage,
    'fatal', v_dead,
    'health_before', v_health_before,
    'health_after', o_health_after,
    'row_version_after', o_row_version_after,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'world_entity_damage_applied', 'combat', COALESCE(p_server_tick, 0),
    v_entity_key, v_entity_key, v_payload, LEFT(p_idempotency_key, 191),
    'server', NULL, NULL, o_event_id
  );

  COMMIT;
END $$

DROP PROCEDURE IF EXISTS mmo_mark_npc_dead $$
CREATE PROCEDURE mmo_mark_npc_dead(
  IN p_session_id BINARY(16),
  IN p_entity_key VARCHAR(512),
  IN p_server_tick BIGINT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16),
  OUT o_row_version_after BIGINT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_entity_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_lifecycle_state VARCHAR(32) DEFAULT NULL;
  DECLARE v_payload JSON;
  DECLARE v_not_found BOOL DEFAULT FALSE;

  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET o_event_id = NULL;
  SET o_row_version_after = NULL;
  SET v_entity_key = LEFT(COALESCE(NULLIF(p_entity_key, ''), 'unknown:npc'), 191);

  SET v_not_found = FALSE;
  SELECT ss.realm_id, ss.world_instance_id, ss.character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions ss
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1;
  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_mark_npc_dead: active session not found';
  END IF;

  SET v_not_found = FALSE;
  SELECT wej.event_id
    INTO o_event_id
    FROM world_event_journal wej
   WHERE wej.world_instance_id = v_world_instance_id
     AND wej.idempotency_key = LEFT(p_idempotency_key, 191)
   LIMIT 1;
  IF o_event_id IS NOT NULL THEN
    SELECT wes.row_version INTO o_row_version_after
      FROM world_entity_state wes
     WHERE wes.world_instance_id = v_world_instance_id
       AND wes.entity_key = v_entity_key
     LIMIT 1;
    LEAVE proc;
  END IF;

  START TRANSACTION;

  SET v_not_found = FALSE;
  SELECT wes.lifecycle_state, wes.row_version
    INTO v_lifecycle_state, o_row_version_after
    FROM world_entity_state wes
   WHERE wes.world_instance_id = v_world_instance_id
     AND wes.entity_key = v_entity_key
     AND wes.entity_kind IN ('npc', 'creature')
   LIMIT 1
   FOR UPDATE;
  IF v_not_found THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mmo_mark_npc_dead: world NPC entity not found';
  END IF;

  IF v_lifecycle_state <> 'active' THEN
    COMMIT;
    LEAVE proc;
  END IF;

  UPDATE world_entity_state
     SET lifecycle_state = 'dead',
         health_current = 0,
         row_version = row_version + 1,
         state_json = JSON_MERGE_PATCH(
           COALESCE(state_json, JSON_OBJECT()),
           JSON_OBJECT('dead_at_tick', COALESCE(p_server_tick, 0))
         ),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = v_entity_key;

  SELECT row_version INTO o_row_version_after
    FROM world_entity_state
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = v_entity_key
   LIMIT 1;

  SET v_payload = JSON_OBJECT(
    'entity_key', v_entity_key,
    'row_version_after', o_row_version_after,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id, v_world_instance_id, v_character_id,
    'npc_marked_dead', 'combat', COALESCE(p_server_tick, 0),
    v_entity_key, v_entity_key, v_payload, LEFT(p_idempotency_key, 191),
    'server', NULL, NULL, o_event_id
  );

  COMMIT;
END $$

DELIMITER ;
