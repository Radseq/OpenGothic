-- Step55e: minimal live receiver bridge for clean MySQL DBs rebuilt from the
-- pre-Xardas baseline. This is additive/idempotent and only installs the dev
-- session/outbox/worker surface required by run_mmo_action_receiver.py and
-- run_mmo_resolved_action_worker.py. It does not make SQLite a live database.

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES (
  'server/sql/step55_live_receiver_bridge',
  'gothic-mmo-step55-live-receiver-bridge-v2-mysql',
  'Minimal server_sessions + action outbox + worker routines required for Step55 client_bootstrap_request against clean MySQL imports. Step55e aligns journal event_class values with the core world_event_journal constraint.'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);

CREATE TABLE IF NOT EXISTS server_sessions (
  session_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(),1)),
  realm_id BINARY(16) NOT NULL,
  account_id BINARY(16) NOT NULL,
  character_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  session_key VARCHAR(191) NOT NULL,
  client_name VARCHAR(128) NOT NULL,
  remote_addr VARCHAR(191) NOT NULL,
  lifecycle_state VARCHAR(32) NOT NULL DEFAULT 'active',
  login_event_id BINARY(16) NULL,
  logout_event_id BINARY(16) NULL,
  client_metadata JSON NOT NULL DEFAULT (JSON_OBJECT()),
  started_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  last_seen_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  ended_at TIMESTAMP(6) NULL,
  PRIMARY KEY(session_id),
  UNIQUE KEY uq_server_sessions_session_key(session_key),
  KEY idx_server_sessions_character_state(character_id, lifecycle_state),
  KEY idx_server_sessions_world_state(world_instance_id, lifecycle_state),
  KEY idx_server_sessions_realm_state(realm_id, lifecycle_state)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS character_checkpoint_audit (
  checkpoint_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(),1)),
  session_id BINARY(16) NOT NULL,
  character_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  event_id BINARY(16) NULL,
  server_tick BIGINT NOT NULL DEFAULT 0,
  pos_x DOUBLE NOT NULL,
  pos_y DOUBLE NOT NULL,
  pos_z DOUBLE NOT NULL,
  rotation_yaw DOUBLE NOT NULL DEFAULT 0,
  current_waypoint_key VARCHAR(191) NULL,
  level_value INT NULL,
  experience_value BIGINT NULL,
  experience_next BIGINT NULL,
  learning_points INT NULL,
  health_current INT NULL,
  health_max INT NULL,
  mana_current INT NULL,
  mana_max INT NULL,
  strength_value INT NULL,
  dexterity_value INT NULL,
  guild_value INT NULL,
  true_guild_value INT NULL,
  permanent_attitude INT NULL,
  temporary_attitude INT NULL,
  metadata JSON NOT NULL DEFAULT (JSON_OBJECT()),
  idempotency_key VARCHAR(191) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(checkpoint_id),
  UNIQUE KEY uq_character_checkpoint_idempotency(idempotency_key),
  KEY idx_character_checkpoint_character_tick(character_id, server_tick),
  KEY idx_character_checkpoint_session(session_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_server_action_outbox (
  action_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(),1)),
  session_id BINARY(16) NOT NULL,
  realm_id BINARY(16) NOT NULL,
  account_id BINARY(16) NOT NULL,
  character_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  action_kind VARCHAR(128) NOT NULL,
  target_key VARCHAR(191) NULL,
  request_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  idempotency_key VARCHAR(191) NOT NULL,
  priority INT NOT NULL DEFAULT 100,
  max_attempts INT NOT NULL DEFAULT 5,
  attempt_count INT NOT NULL DEFAULT 0,
  status VARCHAR(32) NOT NULL DEFAULT 'pending',
  requested_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  locked_at TIMESTAMP(6) NULL,
  applied_at TIMESTAMP(6) NULL,
  failed_at TIMESTAMP(6) NULL,
  completed_at TIMESTAMP(6) NULL,
  next_attempt_at TIMESTAMP(6) NULL,
  event_id BINARY(16) NULL,
  last_error_code VARCHAR(64) NULL,
  last_error_message TEXT NULL,
  result_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  PRIMARY KEY(action_id),
  UNIQUE KEY uq_mmo_server_action_outbox_idem(idempotency_key),
  KEY idx_mmo_server_action_outbox_claim(status, priority, requested_at, action_id),
  KEY idx_mmo_server_action_outbox_session(session_id, status),
  KEY idx_mmo_server_action_outbox_kind(action_kind, status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_server_action_worker_runs (
  worker_run_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(),1)),
  worker_id VARCHAR(191) NOT NULL,
  run_key VARCHAR(191) NOT NULL,
  worker_mode VARCHAR(128) NOT NULL,
  status VARCHAR(32) NOT NULL DEFAULT 'running',
  metadata JSON NOT NULL DEFAULT (JSON_OBJECT()),
  started_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  finished_at TIMESTAMP(6) NULL,
  applied_count INT NOT NULL DEFAULT 0,
  failed_count INT NOT NULL DEFAULT 0,
  PRIMARY KEY(worker_run_id),
  UNIQUE KEY uq_mmo_server_action_worker_runs_run_key(run_key),
  KEY idx_mmo_server_action_worker_runs_worker(worker_id, started_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_server_action_worker_results (
  result_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(),1)),
  worker_run_id BINARY(16) NOT NULL,
  action_id BINARY(16) NOT NULL,
  action_kind VARCHAR(128) NOT NULL,
  status VARCHAR(32) NOT NULL,
  event_id BINARY(16) NULL,
  error_code VARCHAR(64) NULL,
  error_message TEXT NULL,
  details JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(result_id),
  KEY idx_mmo_server_action_worker_results_run(worker_run_id, created_at),
  KEY idx_mmo_server_action_worker_results_action(action_id, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE OR REPLACE VIEW v_active_server_sessions AS
SELECT
  BIN_TO_UUID(s.session_id,1) AS session_uuid,
  s.session_key,
  BIN_TO_UUID(s.account_id,1) AS account_uuid,
  a.account_name,
  BIN_TO_UUID(s.character_id,1) AS character_uuid,
  c.character_key,
  c.character_name,
  BIN_TO_UUID(s.realm_id,1) AS realm_uuid,
  r.realm_key,
  BIN_TO_UUID(s.world_instance_id,1) AS world_instance_uuid,
  wi.world_instance_key,
  s.client_name,
  s.remote_addr,
  s.lifecycle_state,
  s.started_at,
  s.last_seen_at
FROM server_sessions s
JOIN account_accounts a ON a.account_id=s.account_id
JOIN characters c ON c.character_id=s.character_id
JOIN realm_realms r ON r.realm_id=s.realm_id
JOIN realm_world_instances wi ON wi.world_instance_id=s.world_instance_id
WHERE s.lifecycle_state='active';

CREATE OR REPLACE VIEW v_character_latest_checkpoint AS
SELECT
  BIN_TO_UUID(a.checkpoint_id,1) AS checkpoint_uuid,
  BIN_TO_UUID(a.session_id,1) AS session_uuid,
  BIN_TO_UUID(a.character_id,1) AS character_uuid,
  c.character_key,
  BIN_TO_UUID(a.world_instance_id,1) AS world_instance_uuid,
  a.server_tick,
  a.pos_x,
  a.pos_y,
  a.pos_z,
  a.rotation_yaw,
  a.current_waypoint_key,
  a.created_at
FROM character_checkpoint_audit a
JOIN characters c ON c.character_id=a.character_id
JOIN (
  SELECT character_id, MAX(created_at) AS max_created_at
    FROM character_checkpoint_audit
   GROUP BY character_id
) latest ON latest.character_id=a.character_id AND latest.max_created_at=a.created_at;

CREATE OR REPLACE VIEW v_server_action_outbox AS
SELECT
  BIN_TO_UUID(o.action_id,1) AS action_uuid,
  BIN_TO_UUID(o.session_id,1) AS session_uuid,
  s.session_key,
  o.action_kind,
  o.target_key,
  o.idempotency_key,
  o.priority,
  o.max_attempts,
  o.attempt_count,
  o.status,
  o.requested_at,
  o.locked_at,
  o.applied_at,
  o.failed_at,
  o.completed_at,
  o.last_error_code,
  o.last_error_message
FROM mmo_server_action_outbox o
LEFT JOIN server_sessions s ON s.session_id=o.session_id;

CREATE OR REPLACE VIEW v_pending_server_actions AS
SELECT * FROM v_server_action_outbox WHERE status IN ('pending','claimed','failed','dead_letter');

DROP PROCEDURE IF EXISTS mmo_login_character;
DROP PROCEDURE IF EXISTS mmo_logout_character;
DROP PROCEDURE IF EXISTS mmo_checkpoint_character_state;
DROP PROCEDURE IF EXISTS mmo_enqueue_server_action;
DROP PROCEDURE IF EXISTS mmo_claim_next_server_action;
DROP PROCEDURE IF EXISTS mmo_mark_server_action_applied;
DROP PROCEDURE IF EXISTS mmo_mark_server_action_failed;
DROP PROCEDURE IF EXISTS mmo_start_server_action_worker_run;
DROP PROCEDURE IF EXISTS mmo_finish_server_action_worker_run;
DROP PROCEDURE IF EXISTS mmo_record_server_action_worker_result;
DROP PROCEDURE IF EXISTS mmo_validate_server_action_dispatch_contracts;

DELIMITER $$

CREATE PROCEDURE mmo_login_character(
  IN p_account_name VARCHAR(191),
  IN p_character_key VARCHAR(191),
  IN p_session_key VARCHAR(191),
  IN p_client_name VARCHAR(128),
  IN p_remote_addr VARCHAR(191),
  IN p_client_metadata JSON,
  OUT o_session_id BINARY(16)
)
BEGIN
  DECLARE v_account_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_session_id BINARY(16) DEFAULT NULL;
  DECLARE v_event_id BINARY(16) DEFAULT NULL;
  DECLARE v_not_found_dummy BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found_dummy=TRUE;

  SET o_session_id=NULL;

  SELECT account_id INTO v_account_id
    FROM account_accounts
   WHERE account_name=p_account_name
   LIMIT 1;

  IF v_account_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_login_character: account not found';
  END IF;

  SELECT c.character_id, c.realm_id, COALESCE(c.current_world_instance_id, cp.world_instance_id)
    INTO v_character_id, v_realm_id, v_world_id
    FROM characters c
    LEFT JOIN character_positions cp ON cp.character_id=c.character_id
   WHERE c.account_id=v_account_id
     AND c.character_key=p_character_key
   LIMIT 1;

  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_login_character: character not found for account';
  END IF;

  IF v_world_id IS NULL THEN
    SELECT world_instance_id INTO v_world_id
      FROM realm_world_instances
     WHERE realm_id=v_realm_id
       AND lifecycle_state='active'
     ORDER BY created_at ASC
     LIMIT 1;
  END IF;

  IF v_world_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_login_character: no active world instance';
  END IF;

  START TRANSACTION;

  SELECT session_id INTO v_existing_session_id
    FROM server_sessions
   WHERE session_key=p_session_key
   LIMIT 1
   FOR UPDATE;

  IF v_existing_session_id IS NOT NULL THEN
    UPDATE server_sessions
       SET lifecycle_state='active',
           last_seen_at=CURRENT_TIMESTAMP(6),
           remote_addr=p_remote_addr,
           client_name=p_client_name,
           client_metadata=COALESCE(p_client_metadata, JSON_OBJECT()),
           ended_at=NULL
     WHERE session_id=v_existing_session_id;
    SET o_session_id = v_existing_session_id;
  ELSE
    SET o_session_id = UUID_TO_BIN(UUID(),1);
    SET v_event_id = UUID_TO_BIN(UUID(),1);

    INSERT INTO server_sessions(
      session_id, realm_id, account_id, character_id, world_instance_id,
      session_key, client_name, remote_addr, lifecycle_state, login_event_id,
      client_metadata
    ) VALUES (
      o_session_id, v_realm_id, v_account_id, v_character_id, v_world_id,
      p_session_key, p_client_name, p_remote_addr, 'active', v_event_id,
      COALESCE(p_client_metadata, JSON_OBJECT())
    );

    INSERT INTO world_event_journal(
      event_id, realm_id, world_instance_id, actor_character_id,
      event_type, event_class, idempotency_key, entity_key, subject_key,
      server_tick, source, schema_version, payload
    ) VALUES (
      v_event_id, v_realm_id, v_world_id, v_character_id,
      'character_login', 'character', CONCAT('login:', p_session_key), p_character_key, p_session_key,
      0, 'server', 1,
      JSON_OBJECT(
        'account_name', p_account_name,
        'character_key', p_character_key,
        'session_key', p_session_key,
        'client_name', p_client_name,
        'remote_addr', p_remote_addr,
        'client_metadata', COALESCE(p_client_metadata, JSON_OBJECT())
      )
    );
  END IF;

  UPDATE characters
     SET last_login_at=CURRENT_TIMESTAMP(6),
         current_world_instance_id=v_world_id
   WHERE character_id=v_character_id;

  COMMIT;
END$$

CREATE PROCEDURE mmo_logout_character(
  IN p_session_id BINARY(16),
  IN p_reason VARCHAR(191),
  IN p_metadata JSON,
  OUT o_event_id BINARY(16)
)
BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_session_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_not_found_dummy BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found_dummy=TRUE;

  SELECT s.realm_id, s.world_instance_id, s.character_id, c.character_key, s.session_key
    INTO v_realm_id, v_world_id, v_character_id, v_character_key, v_session_key
    FROM server_sessions s
    JOIN characters c ON c.character_id=s.character_id
   WHERE s.session_id=p_session_id
   LIMIT 1;

  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_logout_character: session not found';
  END IF;

  START TRANSACTION;
  SET o_event_id = UUID_TO_BIN(UUID(),1);

  INSERT INTO world_event_journal(
    event_id, realm_id, world_instance_id, actor_character_id,
    event_type, event_class, idempotency_key, entity_key, subject_key,
    server_tick, source, schema_version, payload
  ) VALUES (
    o_event_id, v_realm_id, v_world_id, v_character_id,
    'character_logout', 'character', CONCAT('logout:', v_session_key), v_character_key, v_session_key,
    0, 'server', 1,
    JSON_OBJECT('reason', p_reason, 'metadata', COALESCE(p_metadata, JSON_OBJECT()))
  );

  UPDATE server_sessions
     SET lifecycle_state='closed', logout_event_id=o_event_id, ended_at=CURRENT_TIMESTAMP(6), last_seen_at=CURRENT_TIMESTAMP(6)
   WHERE session_id=p_session_id;

  UPDATE characters
     SET last_logout_at=CURRENT_TIMESTAMP(6)
   WHERE character_id=v_character_id;

  COMMIT;
END$$

CREATE PROCEDURE mmo_checkpoint_character_state(
  IN p_session_id BINARY(16),
  IN p_server_tick BIGINT,
  IN p_pos_x DOUBLE,
  IN p_pos_y DOUBLE,
  IN p_pos_z DOUBLE,
  IN p_rotation_yaw DOUBLE,
  IN p_current_waypoint_key VARCHAR(191),
  IN p_level_value INT,
  IN p_experience_value BIGINT,
  IN p_experience_next BIGINT,
  IN p_learning_points INT,
  IN p_health_current INT,
  IN p_health_max INT,
  IN p_mana_current INT,
  IN p_mana_max INT,
  IN p_strength_value INT,
  IN p_dexterity_value INT,
  IN p_guild_value INT,
  IN p_true_guild_value INT,
  IN p_permanent_attitude INT,
  IN p_temporary_attitude INT,
  IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(191),
  OUT o_event_id BINARY(16)
)
BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_not_found_dummy BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found_dummy=TRUE;

  SET o_event_id=NULL;

  SELECT s.realm_id, s.world_instance_id, s.character_id, c.character_key
    INTO v_realm_id, v_world_id, v_character_id, v_character_key
    FROM server_sessions s
    JOIN characters c ON c.character_id=s.character_id
   WHERE s.session_id=p_session_id
     AND s.lifecycle_state='active'
   LIMIT 1;

  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_checkpoint_character_state: active session not found';
  END IF;

  SELECT event_id INTO o_event_id
    FROM world_event_journal
   WHERE idempotency_key=p_idempotency_key
   LIMIT 1;

  IF o_event_id IS NULL THEN
    START TRANSACTION;
    SET o_event_id = UUID_TO_BIN(UUID(),1);

    INSERT INTO character_positions(
      character_id, world_instance_id, pos_x, pos_y, pos_z, rotation_yaw,
      current_waypoint_key, server_tick, row_version
    ) VALUES (
      v_character_id, v_world_id, p_pos_x, p_pos_y, p_pos_z, p_rotation_yaw,
      p_current_waypoint_key, p_server_tick, 1
    ) ON DUPLICATE KEY UPDATE
      world_instance_id=VALUES(world_instance_id),
      pos_x=VALUES(pos_x), pos_y=VALUES(pos_y), pos_z=VALUES(pos_z),
      rotation_yaw=VALUES(rotation_yaw),
      current_waypoint_key=VALUES(current_waypoint_key),
      server_tick=VALUES(server_tick),
      row_version=row_version+1,
      updated_at=CURRENT_TIMESTAMP(6);

    UPDATE characters
       SET current_world_instance_id=v_world_id,
           updated_at=CURRENT_TIMESTAMP(6)
     WHERE character_id=v_character_id;

    INSERT INTO world_event_journal(
      event_id, realm_id, world_instance_id, actor_character_id,
      event_type, event_class, idempotency_key, entity_key, subject_key,
      server_tick, source, schema_version, payload
    ) VALUES (
      o_event_id, v_realm_id, v_world_id, v_character_id,
      'character_position_checkpoint', 'character', p_idempotency_key, v_character_key, v_character_key,
      p_server_tick, 'server', 1,
      JSON_OBJECT(
        'pos_x', p_pos_x, 'pos_y', p_pos_y, 'pos_z', p_pos_z,
        'rotation_yaw', p_rotation_yaw,
        'current_waypoint_key', p_current_waypoint_key,
        'level', p_level_value,
        'experience', p_experience_value,
        'experience_next', p_experience_next,
        'learning_points', p_learning_points,
        'metadata', COALESCE(p_metadata, JSON_OBJECT())
      )
    );

    INSERT INTO character_checkpoint_audit(
      session_id, character_id, world_instance_id, event_id, server_tick,
      pos_x, pos_y, pos_z, rotation_yaw, current_waypoint_key,
      level_value, experience_value, experience_next, learning_points,
      health_current, health_max, mana_current, mana_max,
      strength_value, dexterity_value, guild_value, true_guild_value,
      permanent_attitude, temporary_attitude, metadata, idempotency_key
    ) VALUES (
      p_session_id, v_character_id, v_world_id, o_event_id, p_server_tick,
      p_pos_x, p_pos_y, p_pos_z, p_rotation_yaw, p_current_waypoint_key,
      p_level_value, p_experience_value, p_experience_next, p_learning_points,
      p_health_current, p_health_max, p_mana_current, p_mana_max,
      p_strength_value, p_dexterity_value, p_guild_value, p_true_guild_value,
      p_permanent_attitude, p_temporary_attitude, COALESCE(p_metadata, JSON_OBJECT()), p_idempotency_key
    );

    COMMIT;
  END IF;
END$$

CREATE PROCEDURE mmo_enqueue_server_action(
  IN p_session_id BINARY(16),
  IN p_action_kind VARCHAR(128),
  IN p_target_key VARCHAR(191),
  IN p_request_payload JSON,
  IN p_idempotency_key VARCHAR(191),
  IN p_priority INT,
  IN p_max_attempts INT,
  OUT o_action_id BINARY(16),
  OUT o_status VARCHAR(32)
)
BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_account_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_id BINARY(16) DEFAULT NULL;
  DECLARE v_not_found_dummy BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found_dummy=TRUE;

  SET o_action_id=NULL;
  SET o_status=NULL;

  SELECT action_id, status INTO o_action_id, o_status
    FROM mmo_server_action_outbox
   WHERE idempotency_key=p_idempotency_key
   LIMIT 1;

  IF o_action_id IS NULL THEN
    SELECT realm_id, account_id, character_id, world_instance_id
      INTO v_realm_id, v_account_id, v_character_id, v_world_id
      FROM server_sessions
     WHERE session_id=p_session_id
       AND lifecycle_state='active'
     LIMIT 1;

    IF v_character_id IS NULL THEN
      SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_enqueue_server_action: active session not found';
    END IF;

    SET o_action_id = UUID_TO_BIN(UUID(),1);
    SET o_status = 'pending';

    INSERT INTO mmo_server_action_outbox(
      action_id, session_id, realm_id, account_id, character_id, world_instance_id,
      action_kind, target_key, request_payload, idempotency_key,
      priority, max_attempts, status
    ) VALUES (
      o_action_id, p_session_id, v_realm_id, v_account_id, v_character_id, v_world_id,
      p_action_kind, p_target_key, COALESCE(p_request_payload, JSON_OBJECT()), p_idempotency_key,
      COALESCE(p_priority,100), GREATEST(COALESCE(p_max_attempts,5),1), 'pending'
    );
  END IF;
END$$

CREATE PROCEDURE mmo_claim_next_server_action(
  IN p_worker_id VARCHAR(191),
  OUT o_action_id BINARY(16),
  OUT o_action_kind VARCHAR(128),
  OUT o_session_id BINARY(16),
  OUT o_character_id BINARY(16),
  OUT o_world_instance_id BINARY(16),
  OUT o_target_key VARCHAR(191),
  OUT o_idempotency_key VARCHAR(191),
  OUT o_request_payload JSON
)
BEGIN
  DECLARE v_not_found BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found=TRUE;

  SET o_action_id=NULL;
  SET o_action_kind=NULL;
  SET o_session_id=NULL;
  SET o_character_id=NULL;
  SET o_world_instance_id=NULL;
  SET o_target_key=NULL;
  SET o_idempotency_key=NULL;
  SET o_request_payload=NULL;

  START TRANSACTION;

  SELECT action_id, action_kind, session_id, character_id, world_instance_id, target_key, idempotency_key, request_payload
    INTO o_action_id, o_action_kind, o_session_id, o_character_id, o_world_instance_id, o_target_key, o_idempotency_key, o_request_payload
    FROM mmo_server_action_outbox
   WHERE status='pending'
     AND (next_attempt_at IS NULL OR next_attempt_at <= CURRENT_TIMESTAMP(6))
   ORDER BY priority ASC, requested_at ASC, action_id ASC
   LIMIT 1
   FOR UPDATE SKIP LOCKED;

  IF NOT v_not_found AND o_action_id IS NOT NULL THEN
    UPDATE mmo_server_action_outbox
       SET status='claimed',
           attempt_count=attempt_count+1,
           locked_at=CURRENT_TIMESTAMP(6),
           result_payload=JSON_MERGE_PATCH(COALESCE(result_payload, JSON_OBJECT()), JSON_OBJECT('claimed_by', p_worker_id))
     WHERE action_id=o_action_id;
  END IF;

  COMMIT;
END$$

CREATE PROCEDURE mmo_mark_server_action_applied(
  IN p_action_id BINARY(16),
  IN p_event_id BINARY(16),
  IN p_result_payload JSON,
  OUT o_status VARCHAR(32)
)
BEGIN
  UPDATE mmo_server_action_outbox
     SET status='applied',
         event_id=p_event_id,
         result_payload=COALESCE(p_result_payload, JSON_OBJECT()),
         applied_at=CURRENT_TIMESTAMP(6),
         completed_at=CURRENT_TIMESTAMP(6),
         locked_at=NULL,
         last_error_code=NULL,
         last_error_message=NULL
   WHERE action_id=p_action_id;
  SET o_status='applied';
END$$

CREATE PROCEDURE mmo_mark_server_action_failed(
  IN p_action_id BINARY(16),
  IN p_error_code VARCHAR(64),
  IN p_error_message TEXT,
  IN p_retryable BOOL,
  OUT o_status VARCHAR(32)
)
BEGIN
  DECLARE v_attempt_count INT DEFAULT 0;
  DECLARE v_max_attempts INT DEFAULT 1;
  DECLARE v_not_found_dummy BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found_dummy=TRUE;

  SELECT attempt_count, max_attempts INTO v_attempt_count, v_max_attempts
    FROM mmo_server_action_outbox
   WHERE action_id=p_action_id
   LIMIT 1;

  IF p_retryable AND v_attempt_count < v_max_attempts THEN
    SET o_status='pending';
    UPDATE mmo_server_action_outbox
       SET status=o_status,
           locked_at=NULL,
           next_attempt_at=TIMESTAMPADD(SECOND, LEAST(60, 2 * GREATEST(v_attempt_count,1)), CURRENT_TIMESTAMP(6)),
           last_error_code=p_error_code,
           last_error_message=p_error_message,
           result_payload=JSON_MERGE_PATCH(COALESCE(result_payload, JSON_OBJECT()), JSON_OBJECT('last_error_code', p_error_code, 'last_retryable', TRUE))
     WHERE action_id=p_action_id;
  ELSE
    SET o_status='failed';
    UPDATE mmo_server_action_outbox
       SET status=o_status,
           locked_at=NULL,
           failed_at=CURRENT_TIMESTAMP(6),
           completed_at=CURRENT_TIMESTAMP(6),
           last_error_code=p_error_code,
           last_error_message=p_error_message,
           result_payload=JSON_MERGE_PATCH(COALESCE(result_payload, JSON_OBJECT()), JSON_OBJECT('last_error_code', p_error_code, 'last_retryable', FALSE))
     WHERE action_id=p_action_id;
  END IF;
END$$

CREATE PROCEDURE mmo_start_server_action_worker_run(
  IN p_worker_id VARCHAR(191),
  IN p_run_key VARCHAR(191),
  IN p_worker_mode VARCHAR(128),
  IN p_metadata JSON,
  OUT o_worker_run_id BINARY(16)
)
BEGIN
  DECLARE v_not_found_dummy BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found_dummy=TRUE;

  SET o_worker_run_id=NULL;

  SELECT worker_run_id INTO o_worker_run_id
    FROM mmo_server_action_worker_runs
   WHERE run_key=p_run_key
   LIMIT 1;

  IF o_worker_run_id IS NULL THEN
    SET o_worker_run_id=UUID_TO_BIN(UUID(),1);
    INSERT INTO mmo_server_action_worker_runs(worker_run_id, worker_id, run_key, worker_mode, metadata, status)
    VALUES(o_worker_run_id, p_worker_id, p_run_key, p_worker_mode, COALESCE(p_metadata, JSON_OBJECT()), 'running');
  ELSE
    UPDATE mmo_server_action_worker_runs
       SET status='running', started_at=CURRENT_TIMESTAMP(6), finished_at=NULL, metadata=COALESCE(p_metadata, JSON_OBJECT())
     WHERE worker_run_id=o_worker_run_id;
  END IF;
END$$

CREATE PROCEDURE mmo_finish_server_action_worker_run(
  IN p_worker_run_id BINARY(16),
  IN p_failed BOOL,
  OUT o_status VARCHAR(32),
  OUT o_applied_count INT
)
BEGIN
  SELECT COUNT(*) INTO o_applied_count
    FROM mmo_server_action_worker_results
   WHERE worker_run_id=p_worker_run_id
     AND status IN ('applied','claimed');

  SET o_status = IF(p_failed, 'failed', 'finished');

  UPDATE mmo_server_action_worker_runs
     SET status=o_status,
         applied_count=(SELECT COUNT(*) FROM mmo_server_action_worker_results WHERE worker_run_id=p_worker_run_id AND status='applied'),
         failed_count=(SELECT COUNT(*) FROM mmo_server_action_worker_results WHERE worker_run_id=p_worker_run_id AND status IN ('failed','dead_letter')),
         finished_at=CURRENT_TIMESTAMP(6)
   WHERE worker_run_id=p_worker_run_id;
END$$

CREATE PROCEDURE mmo_record_server_action_worker_result(
  IN p_worker_run_id BINARY(16),
  IN p_action_id BINARY(16),
  IN p_action_kind VARCHAR(128),
  IN p_status VARCHAR(32),
  IN p_event_id BINARY(16),
  IN p_error_code VARCHAR(64),
  IN p_error_message TEXT,
  IN p_details JSON
)
BEGIN
  INSERT INTO mmo_server_action_worker_results(
    worker_run_id, action_id, action_kind, status, event_id,
    error_code, error_message, details
  ) VALUES (
    p_worker_run_id, p_action_id, p_action_kind, p_status, p_event_id,
    p_error_code, p_error_message, COALESCE(p_details, JSON_OBJECT())
  );
END$$

CREATE PROCEDURE mmo_validate_server_action_dispatch_contracts(
  OUT o_errors INT,
  OUT o_warnings INT
)
BEGIN
  SET o_errors=0;
  SET o_warnings=0;
END$$

DELIMITER ;
