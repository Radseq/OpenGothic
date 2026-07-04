-- Gothic MMO MySQL production migration 003.
-- Minimal server write path: login, character position/stat checkpoint, logout.
-- Requires 001_gothic_mmo_production_schema.sql and 002_bootstrap_import_pipeline.sql.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

-- -----------------------------------------------------------------------------
-- Server session ownership. This is the first server-owned runtime table.
-- It is not copied from SQLite and is not a savegame bridge.
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS server_sessions (
  session_id             BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  session_key            VARCHAR(191) NOT NULL,
  account_id             BINARY(16) NOT NULL,
  character_id           BINARY(16) NOT NULL,
  realm_id               BINARY(16) NOT NULL,
  world_instance_id      BINARY(16) NOT NULL,
  lifecycle_state        VARCHAR(32) NOT NULL DEFAULT 'active',
  client_build           VARCHAR(128) NULL,
  remote_addr            VARCHAR(128) NULL,
  login_event_id         BINARY(16) NULL,
  logout_event_id        BINARY(16) NULL,
  login_at               TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  last_seen_at           TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  logout_at              TIMESTAMP(6) NULL,
  metadata               JSON NOT NULL DEFAULT (JSON_OBJECT()),
  UNIQUE KEY server_sessions_key_uk(session_key),
  KEY ix_server_sessions_character_state(character_id, lifecycle_state, last_seen_at),
  KEY ix_server_sessions_world_state(world_instance_id, lifecycle_state, last_seen_at),
  KEY ix_server_sessions_account_state(account_id, lifecycle_state, last_seen_at),
  CONSTRAINT server_sessions_account_fk FOREIGN KEY(account_id) REFERENCES account_accounts(account_id) ON DELETE RESTRICT,
  CONSTRAINT server_sessions_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE RESTRICT,
  CONSTRAINT server_sessions_realm_fk FOREIGN KEY(realm_id) REFERENCES realm_realms(realm_id) ON DELETE RESTRICT,
  CONSTRAINT server_sessions_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE RESTRICT,
  CONSTRAINT server_sessions_login_event_fk FOREIGN KEY(login_event_id) REFERENCES world_event_journal(event_id) ON DELETE SET NULL,
  CONSTRAINT server_sessions_logout_event_fk FOREIGN KEY(logout_event_id) REFERENCES world_event_journal(event_id) ON DELETE SET NULL,
  CONSTRAINT server_sessions_lifecycle_ck CHECK(lifecycle_state IN ('active','logged_out','expired','crashed')),
  CONSTRAINT server_sessions_metadata_json_ck CHECK(JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS character_checkpoint_audit (
  checkpoint_id              BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  session_id                 BINARY(16) NULL,
  character_id               BINARY(16) NOT NULL,
  world_instance_id          BINARY(16) NOT NULL,
  event_id                   BINARY(16) NOT NULL,
  idempotency_key            VARCHAR(191) NOT NULL,
  server_tick                BIGINT NOT NULL,
  pos_x                      DOUBLE NOT NULL,
  pos_y                      DOUBLE NOT NULL,
  pos_z                      DOUBLE NOT NULL,
  rotation_yaw               DOUBLE NOT NULL,
  position_row_version_after BIGINT NOT NULL,
  stats_row_version_after    BIGINT NOT NULL,
  raw_checkpoint             JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at                 TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY character_checkpoint_idempotency_uk(world_instance_id, idempotency_key),
  KEY ix_character_checkpoint_character_tick(character_id, server_tick),
  KEY ix_character_checkpoint_event(event_id),
  CONSTRAINT character_checkpoint_session_fk FOREIGN KEY(session_id) REFERENCES server_sessions(session_id) ON DELETE SET NULL,
  CONSTRAINT character_checkpoint_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT character_checkpoint_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE RESTRICT,
  CONSTRAINT character_checkpoint_event_fk FOREIGN KEY(event_id) REFERENCES world_event_journal(event_id) ON DELETE RESTRICT,
  CONSTRAINT character_checkpoint_tick_ck CHECK(server_tick >= 0),
  CONSTRAINT character_checkpoint_versions_ck CHECK(position_row_version_after >= 0 AND stats_row_version_after >= 0),
  CONSTRAINT character_checkpoint_raw_json_ck CHECK(JSON_VALID(raw_checkpoint))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_login_character;
DELIMITER $$
CREATE PROCEDURE mmo_login_character(
  IN  p_account_name   VARCHAR(191),
  IN  p_character_key  VARCHAR(191),
  IN  p_session_key    VARCHAR(191),
  IN  p_client_build   VARCHAR(128),
  IN  p_remote_addr    VARCHAR(128),
  IN  p_metadata       JSON,
  OUT p_session_id     BINARY(16)
)
login_proc: BEGIN
  DECLARE v_account_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_session_id BINARY(16) DEFAULT NULL;
  DECLARE v_event_id BINARY(16) DEFAULT NULL;
  DECLARE v_payload JSON;

  IF p_session_key IS NULL OR TRIM(p_session_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'session key is required';
  END IF;

  SET v_existing_session_id = (
    SELECT session_id
      FROM server_sessions
     WHERE session_key = p_session_key
     LIMIT 1
  );

  IF v_existing_session_id IS NOT NULL THEN
    UPDATE server_sessions
       SET last_seen_at = CURRENT_TIMESTAMP(6)
     WHERE session_id = v_existing_session_id;
    SET p_session_id = v_existing_session_id;
    LEAVE login_proc;
  END IF;

  SET v_account_id = (
    SELECT account_id
      FROM account_accounts
     WHERE account_name = p_account_name
       AND status = 'active'
     LIMIT 1
  );

  IF v_account_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'active account not found';
  END IF;

  SET v_character_id = (
    SELECT character_id
      FROM characters
     WHERE account_id = v_account_id
       AND character_key = p_character_key
       AND lifecycle_state = 'active'
     LIMIT 1
  );

  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'active character not found for account';
  END IF;

  SELECT c.realm_id,
         COALESCE(c.current_world_instance_id, cp.world_instance_id)
    INTO v_realm_id, v_world_instance_id
    FROM characters c
    LEFT JOIN character_positions cp ON cp.character_id = c.character_id
   WHERE c.character_id = v_character_id
   LIMIT 1;

  IF v_world_instance_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'character has no current world instance';
  END IF;

  SET p_session_id = UUID_TO_BIN(UUID(), 1);
  SET v_payload = JSON_OBJECT(
    'session_key', p_session_key,
    'client_build', p_client_build,
    'remote_addr', p_remote_addr,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  INSERT INTO server_sessions(
    session_id, session_key, account_id, character_id, realm_id, world_instance_id,
    lifecycle_state, client_build, remote_addr, metadata
  ) VALUES (
    p_session_id, p_session_key, v_account_id, v_character_id, v_realm_id, v_world_instance_id,
    'active', p_client_build, p_remote_addr, COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_instance_id,
    v_character_id,
    'character_login',
    'character',
    0,
    p_character_key,
    p_session_key,
    v_payload,
    CONCAT('login:', p_session_key),
    'server',
    NULL,
    NULL,
    v_event_id
  );

  UPDATE server_sessions
     SET login_event_id = v_event_id,
         last_seen_at = CURRENT_TIMESTAMP(6)
   WHERE session_id = p_session_id;

  UPDATE characters
     SET current_world_instance_id = v_world_instance_id,
         last_login_at = CURRENT_TIMESTAMP(6)
   WHERE character_id = v_character_id;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_checkpoint_character_state;
DELIMITER $$
CREATE PROCEDURE mmo_checkpoint_character_state(
  IN  p_session_id           BINARY(16),
  IN  p_server_tick          BIGINT,
  IN  p_pos_x                DOUBLE,
  IN  p_pos_y                DOUBLE,
  IN  p_pos_z                DOUBLE,
  IN  p_rotation_yaw         DOUBLE,
  IN  p_current_waypoint_key VARCHAR(191),
  IN  p_level                INT,
  IN  p_experience           BIGINT,
  IN  p_experience_next      BIGINT,
  IN  p_learning_points      INT,
  IN  p_health_current       INT,
  IN  p_health_max           INT,
  IN  p_mana_current         INT,
  IN  p_mana_max             INT,
  IN  p_strength             INT,
  IN  p_dexterity            INT,
  IN  p_guild                INT,
  IN  p_true_guild           INT,
  IN  p_permanent_attitude   INT,
  IN  p_temporary_attitude   INT,
  IN  p_raw_stats            JSON,
  IN  p_idempotency_key      VARCHAR(191),
  OUT p_event_id             BINARY(16)
)
checkpoint_proc: BEGIN
  DECLARE v_account_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_session_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_existing_event_id BINARY(16) DEFAULT NULL;
  DECLARE v_payload JSON;
  DECLARE v_level INT DEFAULT 0;
  DECLARE v_experience BIGINT DEFAULT 0;
  DECLARE v_learning_points INT DEFAULT 0;
  DECLARE v_health_current INT DEFAULT 0;
  DECLARE v_health_max INT DEFAULT 0;
  DECLARE v_mana_current INT DEFAULT 0;
  DECLARE v_mana_max INT DEFAULT 0;
  DECLARE v_strength INT DEFAULT 0;
  DECLARE v_dexterity INT DEFAULT 0;
  DECLARE v_position_version BIGINT DEFAULT 0;
  DECLARE v_stats_version BIGINT DEFAULT 0;

  IF p_idempotency_key IS NULL OR TRIM(p_idempotency_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'checkpoint idempotency key is required';
  END IF;

  SELECT ss.account_id, ss.character_id, ss.realm_id, ss.world_instance_id, ss.session_key, c.character_key
    INTO v_account_id, v_character_id, v_realm_id, v_world_instance_id, v_session_key, v_character_key
    FROM server_sessions ss
    JOIN characters c ON c.character_id = ss.character_id
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1;

  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'active session not found';
  END IF;

  SET v_existing_event_id = (
    SELECT event_id
      FROM world_event_journal
     WHERE world_instance_id = v_world_instance_id
       AND idempotency_key = p_idempotency_key
     LIMIT 1
  );

  IF v_existing_event_id IS NOT NULL THEN
    SET p_event_id = v_existing_event_id;
    UPDATE server_sessions
       SET last_seen_at = CURRENT_TIMESTAMP(6)
     WHERE session_id = p_session_id;
    LEAVE checkpoint_proc;
  END IF;

  SET v_level = GREATEST(0, COALESCE(p_level, 0));
  SET v_experience = GREATEST(0, COALESCE(p_experience, 0));
  SET v_learning_points = GREATEST(0, COALESCE(p_learning_points, 0));
  SET v_health_current = GREATEST(0, COALESCE(p_health_current, 0));
  SET v_health_max = GREATEST(v_health_current, GREATEST(0, COALESCE(p_health_max, v_health_current)));
  SET v_mana_current = GREATEST(0, COALESCE(p_mana_current, 0));
  SET v_mana_max = GREATEST(v_mana_current, GREATEST(0, COALESCE(p_mana_max, v_mana_current)));
  SET v_strength = GREATEST(0, COALESCE(p_strength, 0));
  SET v_dexterity = GREATEST(0, COALESCE(p_dexterity, 0));

  SET v_payload = JSON_OBJECT(
    'session_key', v_session_key,
    'character_key', v_character_key,
    'position', JSON_OBJECT(
      'pos_x', COALESCE(p_pos_x, 0),
      'pos_y', COALESCE(p_pos_y, 0),
      'pos_z', COALESCE(p_pos_z, 0),
      'rotation_yaw', COALESCE(p_rotation_yaw, 0),
      'current_waypoint_key', p_current_waypoint_key
    ),
    'stats', JSON_OBJECT(
      'level', v_level,
      'experience', v_experience,
      'experience_next', p_experience_next,
      'learning_points', v_learning_points,
      'health_current', v_health_current,
      'health_max', v_health_max,
      'mana_current', v_mana_current,
      'mana_max', v_mana_max,
      'strength', v_strength,
      'dexterity', v_dexterity,
      'guild', p_guild,
      'true_guild', p_true_guild,
      'permanent_attitude', p_permanent_attitude,
      'temporary_attitude', p_temporary_attitude
    ),
    'raw_stats', COALESCE(p_raw_stats, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_instance_id,
    v_character_id,
    'character_position_checkpoint',
    'character',
    GREATEST(0, COALESCE(p_server_tick, 0)),
    v_character_key,
    v_session_key,
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  INSERT INTO character_positions(
    character_id, world_instance_id, pos_x, pos_y, pos_z, rotation_yaw,
    current_waypoint_key, server_tick, row_version
  ) VALUES (
    v_character_id, v_world_instance_id, COALESCE(p_pos_x, 0), COALESCE(p_pos_y, 0), COALESCE(p_pos_z, 0), COALESCE(p_rotation_yaw, 0),
    p_current_waypoint_key, GREATEST(0, COALESCE(p_server_tick, 0)), 1
  )
  ON DUPLICATE KEY UPDATE
    world_instance_id = VALUES(world_instance_id),
    pos_x = VALUES(pos_x),
    pos_y = VALUES(pos_y),
    pos_z = VALUES(pos_z),
    rotation_yaw = VALUES(rotation_yaw),
    current_waypoint_key = VALUES(current_waypoint_key),
    server_tick = VALUES(server_tick),
    row_version = row_version + 1;

  INSERT INTO character_stats(
    character_id, level, experience, experience_next, learning_points,
    health_current, health_max, mana_current, mana_max, strength, dexterity,
    guild, true_guild, permanent_attitude, temporary_attitude, raw_stats, row_version
  ) VALUES (
    v_character_id, v_level, v_experience, p_experience_next, v_learning_points,
    v_health_current, v_health_max, v_mana_current, v_mana_max, v_strength, v_dexterity,
    p_guild, p_true_guild, p_permanent_attitude, p_temporary_attitude, COALESCE(p_raw_stats, JSON_OBJECT()), 1
  )
  ON DUPLICATE KEY UPDATE
    level = VALUES(level),
    experience = VALUES(experience),
    experience_next = VALUES(experience_next),
    learning_points = VALUES(learning_points),
    health_current = VALUES(health_current),
    health_max = VALUES(health_max),
    mana_current = VALUES(mana_current),
    mana_max = VALUES(mana_max),
    strength = VALUES(strength),
    dexterity = VALUES(dexterity),
    guild = VALUES(guild),
    true_guild = VALUES(true_guild),
    permanent_attitude = VALUES(permanent_attitude),
    temporary_attitude = VALUES(temporary_attitude),
    raw_stats = VALUES(raw_stats),
    row_version = row_version + 1;

  SET v_position_version = (
    SELECT row_version FROM character_positions WHERE character_id = v_character_id LIMIT 1
  );
  SET v_stats_version = (
    SELECT row_version FROM character_stats WHERE character_id = v_character_id LIMIT 1
  );

  INSERT INTO character_checkpoint_audit(
    session_id, character_id, world_instance_id, event_id, idempotency_key,
    server_tick, pos_x, pos_y, pos_z, rotation_yaw,
    position_row_version_after, stats_row_version_after, raw_checkpoint
  ) VALUES (
    p_session_id, v_character_id, v_world_instance_id, p_event_id, p_idempotency_key,
    GREATEST(0, COALESCE(p_server_tick, 0)), COALESCE(p_pos_x, 0), COALESCE(p_pos_y, 0), COALESCE(p_pos_z, 0), COALESCE(p_rotation_yaw, 0),
    v_position_version, v_stats_version, v_payload
  );

  UPDATE realm_world_instances
     SET current_tick = GREATEST(current_tick, GREATEST(0, COALESCE(p_server_tick, 0)))
   WHERE world_instance_id = v_world_instance_id;

  UPDATE server_sessions
     SET last_seen_at = CURRENT_TIMESTAMP(6)
   WHERE session_id = p_session_id;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_logout_character;
DELIMITER $$
CREATE PROCEDURE mmo_logout_character(
  IN  p_session_id BINARY(16),
  IN  p_reason     VARCHAR(128),
  IN  p_metadata   JSON,
  OUT p_event_id   BINARY(16)
)
logout_proc: BEGIN
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_session_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_lifecycle_state VARCHAR(32) DEFAULT NULL;
  DECLARE v_payload JSON;

  SELECT ss.character_id, ss.realm_id, ss.world_instance_id, c.character_key, ss.session_key, ss.lifecycle_state, ss.logout_event_id
    INTO v_character_id, v_realm_id, v_world_instance_id, v_character_key, v_session_key, v_lifecycle_state, p_event_id
    FROM server_sessions ss
    JOIN characters c ON c.character_id = ss.character_id
   WHERE ss.session_id = p_session_id
   LIMIT 1;

  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'session not found';
  END IF;

  IF v_lifecycle_state <> 'active' THEN
    LEAVE logout_proc;
  END IF;

  SET v_payload = JSON_OBJECT(
    'session_key', v_session_key,
    'reason', COALESCE(p_reason, 'logout'),
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_instance_id,
    v_character_id,
    'character_logout',
    'character',
    0,
    v_character_key,
    v_session_key,
    v_payload,
    CONCAT('logout:', v_session_key),
    'server',
    NULL,
    NULL,
    p_event_id
  );

  UPDATE server_sessions
     SET lifecycle_state = 'logged_out',
         logout_event_id = p_event_id,
         logout_at = CURRENT_TIMESTAMP(6),
         last_seen_at = CURRENT_TIMESTAMP(6)
   WHERE session_id = p_session_id;

  UPDATE characters
     SET last_logout_at = CURRENT_TIMESTAMP(6)
   WHERE character_id = v_character_id;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_active_server_sessions AS
SELECT
  BIN_TO_UUID(ss.session_id, 1) AS session_id,
  ss.session_key,
  a.account_name,
  c.character_key,
  c.character_name,
  r.realm_key,
  wi.world_instance_key,
  ss.client_build,
  ss.remote_addr,
  ss.login_at,
  ss.last_seen_at,
  TIMESTAMPDIFF(SECOND, ss.last_seen_at, CURRENT_TIMESTAMP(6)) AS idle_seconds
FROM server_sessions ss
JOIN account_accounts a ON a.account_id = ss.account_id
JOIN characters c ON c.character_id = ss.character_id
JOIN realm_realms r ON r.realm_id = ss.realm_id
JOIN realm_world_instances wi ON wi.world_instance_id = ss.world_instance_id
WHERE ss.lifecycle_state = 'active';

CREATE OR REPLACE VIEW v_character_latest_checkpoint AS
SELECT
  BIN_TO_UUID(cca.checkpoint_id, 1) AS checkpoint_id,
  BIN_TO_UUID(cca.event_id, 1) AS event_id,
  c.character_key,
  c.character_name,
  wi.world_instance_key,
  cca.idempotency_key,
  cca.server_tick,
  cca.pos_x,
  cca.pos_y,
  cca.pos_z,
  cca.rotation_yaw,
  cca.position_row_version_after,
  cca.stats_row_version_after,
  cca.created_at
FROM character_checkpoint_audit cca
JOIN characters c ON c.character_id = cca.character_id
JOIN realm_world_instances wi ON wi.world_instance_id = cca.world_instance_id
WHERE cca.created_at = (
  SELECT MAX(cca2.created_at)
    FROM character_checkpoint_audit cca2
   WHERE cca2.character_id = cca.character_id
);

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES (
  'production/mysql/003_server_write_path',
  'gothic-mmo-server-write-path-v1-mysql',
  'Adds server_sessions, character checkpoint audit, login/checkpoint/logout procedures, and smoke-test read views.'
)
ON DUPLICATE KEY UPDATE
  applied_at = CURRENT_TIMESTAMP(6),
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes);
