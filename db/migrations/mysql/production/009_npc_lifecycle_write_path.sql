-- Gothic MMO MySQL production migration 009.
-- Server-owned NPC lifecycle write path.
-- Requires 001..008 MySQL production migrations and a bootstrap import.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE IF NOT EXISTS world_npc_lifecycle_audit (
  npc_lifecycle_audit_id  BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  audit_type              VARCHAR(32) NOT NULL,
  session_id              BINARY(16) NULL,
  actor_character_id      BINARY(16) NOT NULL,
  world_instance_id       BINARY(16) NOT NULL,
  event_id                BINARY(16) NOT NULL,
  idempotency_key         VARCHAR(191) NOT NULL,
  npc_entity_key          VARCHAR(191) NOT NULL,
  lifecycle_before        VARCHAR(32) NULL,
  lifecycle_after         VARCHAR(32) NOT NULL,
  health_before           INT NULL,
  health_after            INT NULL,
  row_version_before      BIGINT NULL,
  row_version_after       BIGINT NOT NULL,
  server_tick             BIGINT NOT NULL DEFAULT 0,
  state_before            JSON NULL,
  state_after             JSON NULL,
  raw_delta               JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at              TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY world_npc_lifecycle_audit_idempotency_uk(world_instance_id, idempotency_key),
  KEY ix_world_npc_lifecycle_audit_npc(world_instance_id, npc_entity_key, created_at),
  KEY ix_world_npc_lifecycle_audit_actor(actor_character_id, created_at),
  KEY ix_world_npc_lifecycle_audit_event(event_id),
  CONSTRAINT world_npc_lifecycle_audit_session_fk FOREIGN KEY(session_id) REFERENCES server_sessions(session_id) ON DELETE SET NULL,
  CONSTRAINT world_npc_lifecycle_audit_actor_fk FOREIGN KEY(actor_character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT world_npc_lifecycle_audit_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE RESTRICT,
  CONSTRAINT world_npc_lifecycle_audit_event_fk FOREIGN KEY(event_id) REFERENCES world_event_journal(event_id) ON DELETE RESTRICT,
  CONSTRAINT world_npc_lifecycle_audit_type_ck CHECK(audit_type IN ('npc_dead','npc_respawn')),
  CONSTRAINT world_npc_lifecycle_audit_lifecycle_after_ck CHECK(lifecycle_after IN ('active','dead','removed','disabled','consumed','archived')),
  CONSTRAINT world_npc_lifecycle_audit_health_ck CHECK((health_before IS NULL OR health_before >= 0) AND (health_after IS NULL OR health_after >= 0)),
  CONSTRAINT world_npc_lifecycle_audit_version_ck CHECK((row_version_before IS NULL OR row_version_before >= 0) AND row_version_after >= 0),
  CONSTRAINT world_npc_lifecycle_audit_tick_ck CHECK(server_tick >= 0),
  CONSTRAINT world_npc_lifecycle_audit_state_before_json_ck CHECK(state_before IS NULL OR JSON_VALID(state_before)),
  CONSTRAINT world_npc_lifecycle_audit_state_after_json_ck CHECK(state_after IS NULL OR JSON_VALID(state_after)),
  CONSTRAINT world_npc_lifecycle_audit_raw_json_ck CHECK(JSON_VALID(raw_delta))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_mark_npc_dead;
DELIMITER $$
CREATE PROCEDURE mmo_mark_npc_dead(
  IN  p_session_id       BINARY(16),
  IN  p_npc_entity_key   VARCHAR(191),
  IN  p_server_tick      BIGINT,
  IN  p_metadata         JSON,
  IN  p_idempotency_key  VARCHAR(191),
  OUT p_event_id         BINARY(16),
  OUT p_row_version_after BIGINT
)
dead_proc: BEGIN
  DECLARE v_actor_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_actor_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_entity_kind VARCHAR(32) DEFAULT NULL;
  DECLARE v_lifecycle_before VARCHAR(32) DEFAULT NULL;
  DECLARE v_health_before INT DEFAULT NULL;
  DECLARE v_row_version_before BIGINT DEFAULT NULL;
  DECLARE v_row_version_after BIGINT DEFAULT NULL;
  DECLARE v_state_before JSON DEFAULT NULL;
  DECLARE v_state_after JSON DEFAULT NULL;
  DECLARE v_existing_audit_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_existing_event_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_row_after BIGINT DEFAULT NULL;
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
  IF p_npc_entity_key IS NULL OR TRIM(p_npc_entity_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'npc entity key is required';
  END IF;
  IF p_idempotency_key IS NULL OR TRIM(p_idempotency_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'npc lifecycle idempotency key is required';
  END IF;

  START TRANSACTION;

  SELECT ss.character_id, ss.realm_id, ss.world_instance_id, c.character_key
    INTO v_actor_character_id, v_realm_id, v_world_instance_id, v_actor_character_key
    FROM server_sessions ss
    JOIN characters c ON c.character_id = ss.character_id
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1
   FOR UPDATE;

  IF v_actor_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'active session not found';
  END IF;

  SELECT audit_type, event_id, row_version_after
    INTO v_existing_audit_type, v_existing_event_id, v_existing_row_after
    FROM world_npc_lifecycle_audit
   WHERE world_instance_id = v_world_instance_id
     AND idempotency_key = p_idempotency_key
   LIMIT 1
   FOR UPDATE;

  IF v_existing_event_id IS NOT NULL THEN
    IF v_existing_audit_type <> 'npc_dead' THEN
      SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT = 'idempotency key reused with different npc lifecycle audit type';
    END IF;
    SET p_event_id = v_existing_event_id;
    SET p_row_version_after = v_existing_row_after;
    COMMIT;
    LEAVE dead_proc;
  END IF;

  SELECT entity_kind, lifecycle_state, health_current, row_version, state_json
    INTO v_entity_kind, v_lifecycle_before, v_health_before, v_row_version_before, v_state_before
    FROM world_entity_state
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = p_npc_entity_key
   LIMIT 1
   FOR UPDATE;

  IF v_entity_kind IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'npc entity not found in session world';
  END IF;
  IF v_entity_kind NOT IN ('npc','creature') THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'entity is not an npc or creature';
  END IF;
  IF v_lifecycle_before <> 'active' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'npc must be active before death';
  END IF;

  SET v_row_version_after = v_row_version_before + 1;
  SET v_state_after = JSON_MERGE_PATCH(
    COALESCE(v_state_before, JSON_OBJECT()),
    JSON_OBJECT(
      'dead', TRUE,
      'last_death_tick', COALESCE(p_server_tick, 0),
      'killed_by_character_key', v_actor_character_key,
      'metadata', COALESCE(p_metadata, JSON_OBJECT())
    )
  );

  SET v_payload = JSON_OBJECT(
    'actor_character_key', v_actor_character_key,
    'npc_entity_key', p_npc_entity_key,
    'lifecycle_before', v_lifecycle_before,
    'lifecycle_after', 'dead',
    'health_before', v_health_before,
    'health_after', 0,
    'row_version_before', v_row_version_before,
    'row_version_after', v_row_version_after,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_instance_id,
    v_actor_character_id,
    'npc_marked_dead',
    'combat',
    COALESCE(p_server_tick, 0),
    p_npc_entity_key,
    v_actor_character_key,
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  UPDATE world_entity_state
     SET lifecycle_state = 'dead',
         health_current = 0,
         state_json = v_state_after,
         row_version = v_row_version_after,
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = p_npc_entity_key;

  INSERT INTO world_npc_lifecycle_audit(
    audit_type, session_id, actor_character_id, world_instance_id, event_id,
    idempotency_key, npc_entity_key, lifecycle_before, lifecycle_after,
    health_before, health_after, row_version_before, row_version_after,
    server_tick, state_before, state_after, raw_delta
  ) VALUES(
    'npc_dead', p_session_id, v_actor_character_id, v_world_instance_id, p_event_id,
    p_idempotency_key, p_npc_entity_key, v_lifecycle_before, 'dead',
    v_health_before, 0, v_row_version_before, v_row_version_after,
    COALESCE(p_server_tick, 0), v_state_before, v_state_after, v_payload
  );

  UPDATE realm_world_instances
     SET current_tick = GREATEST(current_tick, COALESCE(p_server_tick, 0)),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id;

  UPDATE server_sessions
     SET last_seen_at = CURRENT_TIMESTAMP(6),
         metadata = JSON_SET(COALESCE(metadata, JSON_OBJECT()), '$.last_npc_lifecycle_event', BIN_TO_UUID(p_event_id, 1))
   WHERE session_id = p_session_id;

  SET p_row_version_after = v_row_version_after;
  COMMIT;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_respawn_npc;
DELIMITER $$
CREATE PROCEDURE mmo_respawn_npc(
  IN  p_session_id        BINARY(16),
  IN  p_npc_entity_key    VARCHAR(191),
  IN  p_pos_x             DOUBLE,
  IN  p_pos_y             DOUBLE,
  IN  p_pos_z             DOUBLE,
  IN  p_rotation_yaw      DOUBLE,
  IN  p_health_current    INT,
  IN  p_health_max        INT,
  IN  p_server_tick       BIGINT,
  IN  p_metadata          JSON,
  IN  p_idempotency_key   VARCHAR(191),
  OUT p_event_id          BINARY(16),
  OUT p_row_version_after BIGINT
)
respawn_proc: BEGIN
  DECLARE v_actor_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_actor_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_entity_kind VARCHAR(32) DEFAULT NULL;
  DECLARE v_lifecycle_before VARCHAR(32) DEFAULT NULL;
  DECLARE v_health_before INT DEFAULT NULL;
  DECLARE v_existing_health_max INT DEFAULT NULL;
  DECLARE v_row_version_before BIGINT DEFAULT NULL;
  DECLARE v_row_version_after BIGINT DEFAULT NULL;
  DECLARE v_state_before JSON DEFAULT NULL;
  DECLARE v_state_after JSON DEFAULT NULL;
  DECLARE v_existing_audit_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_existing_event_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_row_after BIGINT DEFAULT NULL;
  DECLARE v_new_health_current INT DEFAULT NULL;
  DECLARE v_new_health_max INT DEFAULT NULL;
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
  IF p_npc_entity_key IS NULL OR TRIM(p_npc_entity_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'npc entity key is required';
  END IF;
  IF p_health_current IS NOT NULL AND p_health_current < 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'respawn health_current must be non-negative';
  END IF;
  IF p_health_max IS NOT NULL AND p_health_max < 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'respawn health_max must be non-negative';
  END IF;
  IF p_idempotency_key IS NULL OR TRIM(p_idempotency_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'npc lifecycle idempotency key is required';
  END IF;

  START TRANSACTION;

  SELECT ss.character_id, ss.realm_id, ss.world_instance_id, c.character_key
    INTO v_actor_character_id, v_realm_id, v_world_instance_id, v_actor_character_key
    FROM server_sessions ss
    JOIN characters c ON c.character_id = ss.character_id
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1
   FOR UPDATE;

  IF v_actor_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'active session not found';
  END IF;

  SELECT audit_type, event_id, row_version_after
    INTO v_existing_audit_type, v_existing_event_id, v_existing_row_after
    FROM world_npc_lifecycle_audit
   WHERE world_instance_id = v_world_instance_id
     AND idempotency_key = p_idempotency_key
   LIMIT 1
   FOR UPDATE;

  IF v_existing_event_id IS NOT NULL THEN
    IF v_existing_audit_type <> 'npc_respawn' THEN
      SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT = 'idempotency key reused with different npc lifecycle audit type';
    END IF;
    SET p_event_id = v_existing_event_id;
    SET p_row_version_after = v_existing_row_after;
    COMMIT;
    LEAVE respawn_proc;
  END IF;

  SELECT entity_kind, lifecycle_state, health_current, health_max, row_version, state_json
    INTO v_entity_kind, v_lifecycle_before, v_health_before, v_existing_health_max, v_row_version_before, v_state_before
    FROM world_entity_state
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = p_npc_entity_key
   LIMIT 1
   FOR UPDATE;

  IF v_entity_kind IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'npc entity not found in session world';
  END IF;
  IF v_entity_kind NOT IN ('npc','creature') THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'entity is not an npc or creature';
  END IF;
  IF v_lifecycle_before = 'active' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'npc is already active';
  END IF;

  SET v_new_health_max = COALESCE(p_health_max, v_existing_health_max, p_health_current, 1);
  SET v_new_health_current = COALESCE(p_health_current, v_new_health_max);
  IF v_new_health_current > v_new_health_max THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'respawn health_current cannot exceed health_max';
  END IF;

  SET v_row_version_after = v_row_version_before + 1;
  SET v_state_after = JSON_MERGE_PATCH(
    COALESCE(v_state_before, JSON_OBJECT()),
    JSON_OBJECT(
      'dead', FALSE,
      'last_respawn_tick', COALESCE(p_server_tick, 0),
      'respawned_by_character_key', v_actor_character_key,
      'metadata', COALESCE(p_metadata, JSON_OBJECT())
    )
  );

  SET v_payload = JSON_OBJECT(
    'actor_character_key', v_actor_character_key,
    'npc_entity_key', p_npc_entity_key,
    'lifecycle_before', v_lifecycle_before,
    'lifecycle_after', 'active',
    'health_before', v_health_before,
    'health_after', v_new_health_current,
    'row_version_before', v_row_version_before,
    'row_version_after', v_row_version_after,
    'pos_x', p_pos_x,
    'pos_y', p_pos_y,
    'pos_z', p_pos_z,
    'rotation_yaw', COALESCE(p_rotation_yaw, 0),
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_instance_id,
    v_actor_character_id,
    'npc_respawned',
    'combat',
    COALESCE(p_server_tick, 0),
    p_npc_entity_key,
    v_actor_character_key,
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  UPDATE world_entity_state
     SET lifecycle_state = 'active',
         pos_x = COALESCE(p_pos_x, pos_x),
         pos_y = COALESCE(p_pos_y, pos_y),
         pos_z = COALESCE(p_pos_z, pos_z),
         rotation_yaw = COALESCE(p_rotation_yaw, rotation_yaw, 0),
         health_current = v_new_health_current,
         health_max = v_new_health_max,
         state_json = v_state_after,
         row_version = v_row_version_after,
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id
     AND entity_key = p_npc_entity_key;

  INSERT INTO world_npc_lifecycle_audit(
    audit_type, session_id, actor_character_id, world_instance_id, event_id,
    idempotency_key, npc_entity_key, lifecycle_before, lifecycle_after,
    health_before, health_after, row_version_before, row_version_after,
    server_tick, state_before, state_after, raw_delta
  ) VALUES(
    'npc_respawn', p_session_id, v_actor_character_id, v_world_instance_id, p_event_id,
    p_idempotency_key, p_npc_entity_key, v_lifecycle_before, 'active',
    v_health_before, v_new_health_current, v_row_version_before, v_row_version_after,
    COALESCE(p_server_tick, 0), v_state_before, v_state_after, v_payload
  );

  UPDATE realm_world_instances
     SET current_tick = GREATEST(current_tick, COALESCE(p_server_tick, 0)),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE world_instance_id = v_world_instance_id;

  UPDATE server_sessions
     SET last_seen_at = CURRENT_TIMESTAMP(6),
         metadata = JSON_SET(COALESCE(metadata, JSON_OBJECT()), '$.last_npc_lifecycle_event', BIN_TO_UUID(p_event_id, 1))
   WHERE session_id = p_session_id;

  SET p_row_version_after = v_row_version_after;
  COMMIT;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_world_npc_lifecycle_state AS
SELECT
  BIN_TO_UUID(wes.world_instance_id, 1) AS world_instance_id,
  wes.entity_key,
  wes.entity_kind,
  wes.lifecycle_state,
  wes.health_current,
  wes.health_max,
  wes.pos_x,
  wes.pos_y,
  wes.pos_z,
  wes.rotation_yaw,
  wes.row_version,
  wes.state_json,
  wes.updated_at
FROM world_entity_state wes
WHERE wes.entity_kind IN ('npc','creature');

CREATE OR REPLACE VIEW v_world_npc_lifecycle_audit AS
SELECT
  BIN_TO_UUID(a.npc_lifecycle_audit_id, 1) AS npc_lifecycle_audit_id,
  a.audit_type,
  BIN_TO_UUID(a.session_id, 1) AS session_id,
  BIN_TO_UUID(a.actor_character_id, 1) AS actor_character_id,
  c.character_key AS actor_character_key,
  BIN_TO_UUID(a.world_instance_id, 1) AS world_instance_id,
  BIN_TO_UUID(a.event_id, 1) AS event_id,
  a.idempotency_key,
  a.npc_entity_key,
  a.lifecycle_before,
  a.lifecycle_after,
  a.health_before,
  a.health_after,
  a.row_version_before,
  a.row_version_after,
  a.server_tick,
  a.created_at
FROM world_npc_lifecycle_audit a
JOIN characters c ON c.character_id = a.actor_character_id;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES(
  'production/mysql/009_npc_lifecycle_write_path',
  'gothic-mmo-npc-lifecycle-write-path-v1-mysql',
  'Adds server-owned NPC death/respawn lifecycle mutation procedures with idempotent audit.'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);
