-- Gothic MMO MySQL production migration 015.
-- Server action outbox / RPC boundary for C++ semantic hook integration.
-- Requires 001..014 MySQL production migrations and a bootstrap import.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE IF NOT EXISTS mmo_server_action_outbox (
  action_id             BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  realm_id              BINARY(16) NOT NULL,
  world_instance_id     BINARY(16) NOT NULL,
  session_id            BINARY(16) NULL,
  character_id          BINARY(16) NULL,
  action_kind           VARCHAR(128) NOT NULL,
  target_key            VARCHAR(191) NULL,
  idempotency_key       VARCHAR(191) NOT NULL,
  status                VARCHAR(32) NOT NULL DEFAULT 'pending',
  priority              INT NOT NULL DEFAULT 100,
  attempt_count         INT NOT NULL DEFAULT 0,
  max_attempts          INT NOT NULL DEFAULT 5,
  requested_at          TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  locked_at             TIMESTAMP(6) NULL,
  applied_at            TIMESTAMP(6) NULL,
  failed_at             TIMESTAMP(6) NULL,
  event_id              BINARY(16) NULL,
  request_payload       JSON NOT NULL DEFAULT (JSON_OBJECT()),
  result_payload        JSON NOT NULL DEFAULT (JSON_OBJECT()),
  last_error_code       VARCHAR(64) NULL,
  last_error_message    TEXT NULL,
  updated_at            TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  UNIQUE KEY mmo_server_action_outbox_idem_uk(world_instance_id, idempotency_key),
  KEY ix_mmo_server_action_outbox_pending(status, priority, requested_at),
  KEY ix_mmo_server_action_outbox_world_status(world_instance_id, status, requested_at),
  KEY ix_mmo_server_action_outbox_character_status(character_id, status, requested_at),
  KEY ix_mmo_server_action_outbox_kind_status(action_kind, status, requested_at),
  KEY ix_mmo_server_action_outbox_event(event_id),
  CONSTRAINT mmo_server_action_outbox_realm_fk FOREIGN KEY(realm_id) REFERENCES realm_realms(realm_id) ON DELETE RESTRICT,
  CONSTRAINT mmo_server_action_outbox_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE RESTRICT,
  CONSTRAINT mmo_server_action_outbox_session_fk FOREIGN KEY(session_id) REFERENCES server_sessions(session_id) ON DELETE SET NULL,
  CONSTRAINT mmo_server_action_outbox_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE SET NULL,
  CONSTRAINT mmo_server_action_outbox_event_fk FOREIGN KEY(event_id) REFERENCES world_event_journal(event_id) ON DELETE SET NULL,
  CONSTRAINT mmo_server_action_outbox_status_ck CHECK(status IN ('pending','claimed','applied','failed','dead_letter','cancelled')),
  CONSTRAINT mmo_server_action_outbox_attempts_ck CHECK(attempt_count >= 0 AND max_attempts > 0),
  CONSTRAINT mmo_server_action_outbox_priority_ck CHECK(priority >= 0),
  CONSTRAINT mmo_server_action_outbox_request_json_ck CHECK(JSON_VALID(request_payload)),
  CONSTRAINT mmo_server_action_outbox_result_json_ck CHECK(JSON_VALID(result_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_enqueue_server_action;
DELIMITER $$
CREATE PROCEDURE mmo_enqueue_server_action(
  IN  p_session_id       BINARY(16),
  IN  p_action_kind      VARCHAR(128),
  IN  p_target_key       VARCHAR(191),
  IN  p_request_payload  JSON,
  IN  p_idempotency_key  VARCHAR(191),
  IN  p_priority         INT,
  IN  p_max_attempts     INT,
  OUT p_action_id        BINARY(16),
  OUT p_status           VARCHAR(32)
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_action_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_status VARCHAR(32) DEFAULT NULL;

  SET p_action_id = NULL;
  SET p_status = NULL;

  IF p_session_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='session_id is required'; END IF;
  IF p_action_kind IS NULL OR TRIM(p_action_kind)='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='action_kind is required'; END IF;
  IF p_idempotency_key IS NULL OR TRIM(p_idempotency_key)='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='idempotency_key is required'; END IF;

  SELECT realm_id, world_instance_id, character_id
    INTO v_realm_id, v_world_instance_id, v_character_id
    FROM server_sessions
   WHERE session_id=p_session_id AND lifecycle_state='active'
   LIMIT 1;

  IF v_world_instance_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='active session not found'; END IF;

  SELECT action_id, status
    INTO v_existing_action_id, v_existing_status
    FROM mmo_server_action_outbox
   WHERE world_instance_id=v_world_instance_id AND idempotency_key=p_idempotency_key
   LIMIT 1;

  IF v_existing_action_id IS NOT NULL THEN
    SET p_action_id = v_existing_action_id;
    SET p_status = v_existing_status;
    LEAVE proc;
  END IF;

  SET p_action_id = UUID_TO_BIN(UUID(), 1);
  SET p_status = 'pending';

  INSERT INTO mmo_server_action_outbox(
    action_id, realm_id, world_instance_id, session_id, character_id,
    action_kind, target_key, idempotency_key, status, priority, max_attempts, request_payload
  ) VALUES (
    p_action_id, v_realm_id, v_world_instance_id, p_session_id, v_character_id,
    p_action_kind, p_target_key, p_idempotency_key, 'pending', COALESCE(p_priority,100), COALESCE(p_max_attempts,5), COALESCE(p_request_payload,JSON_OBJECT())
  );
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_mark_server_action_applied;
DELIMITER $$
CREATE PROCEDURE mmo_mark_server_action_applied(
  IN  p_action_id       BINARY(16),
  IN  p_event_id        BINARY(16),
  IN  p_result_payload  JSON,
  OUT p_status          VARCHAR(32)
)
BEGIN
  DECLARE v_status VARCHAR(32) DEFAULT NULL;

  SET p_status = NULL;
  IF p_action_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='action_id is required'; END IF;

  SELECT status INTO v_status
    FROM mmo_server_action_outbox
   WHERE action_id=p_action_id
   LIMIT 1 FOR UPDATE;

  IF v_status IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='server action not found'; END IF;

  UPDATE mmo_server_action_outbox
     SET status='applied',
         event_id=COALESCE(p_event_id, event_id),
         result_payload=COALESCE(p_result_payload, JSON_OBJECT()),
         applied_at=COALESCE(applied_at, CURRENT_TIMESTAMP(6)),
         last_error_code=NULL,
         last_error_message=NULL
   WHERE action_id=p_action_id;

  SET p_status = 'applied';
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_mark_server_action_failed;
DELIMITER $$
CREATE PROCEDURE mmo_mark_server_action_failed(
  IN  p_action_id       BINARY(16),
  IN  p_error_code      VARCHAR(64),
  IN  p_error_message   TEXT,
  IN  p_retryable       BOOLEAN,
  OUT p_status          VARCHAR(32)
)
BEGIN
  DECLARE v_attempt_count INT DEFAULT 0;
  DECLARE v_max_attempts INT DEFAULT 0;

  SET p_status = NULL;
  IF p_action_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='action_id is required'; END IF;

  SELECT attempt_count, max_attempts
    INTO v_attempt_count, v_max_attempts
    FROM mmo_server_action_outbox
   WHERE action_id=p_action_id
   LIMIT 1 FOR UPDATE;

  UPDATE mmo_server_action_outbox
     SET attempt_count=attempt_count+1,
         status=IF(COALESCE(p_retryable, FALSE) AND attempt_count+1 < max_attempts, 'pending', IF(COALESCE(p_retryable, FALSE), 'dead_letter', 'failed')),
         failed_at=CURRENT_TIMESTAMP(6),
         last_error_code=p_error_code,
         last_error_message=p_error_message,
         result_payload=JSON_OBJECT('retryable', COALESCE(p_retryable, FALSE), 'attempt_after', attempt_count+1)
   WHERE action_id=p_action_id;

  SELECT status INTO p_status FROM mmo_server_action_outbox WHERE action_id=p_action_id LIMIT 1;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_pending_server_actions AS
SELECT BIN_TO_UUID(action_id,1) AS action_uuid,
       action_kind,
       target_key,
       status,
       priority,
       attempt_count,
       max_attempts,
       BIN_TO_UUID(session_id,1) AS session_uuid,
       BIN_TO_UUID(character_id,1) AS character_uuid,
       BIN_TO_UUID(world_instance_id,1) AS world_instance_uuid,
       idempotency_key,
       request_payload,
       requested_at,
       updated_at
FROM mmo_server_action_outbox
WHERE status IN ('pending','claimed')
ORDER BY priority ASC, requested_at ASC;

CREATE OR REPLACE VIEW v_server_action_outbox AS
SELECT BIN_TO_UUID(action_id,1) AS action_uuid,
       action_kind,
       target_key,
       status,
       priority,
       attempt_count,
       max_attempts,
       BIN_TO_UUID(event_id,1) AS event_uuid,
       BIN_TO_UUID(session_id,1) AS session_uuid,
       BIN_TO_UUID(character_id,1) AS character_uuid,
       BIN_TO_UUID(world_instance_id,1) AS world_instance_uuid,
       idempotency_key,
       last_error_code,
       last_error_message,
       requested_at,
       applied_at,
       failed_at,
       updated_at
FROM mmo_server_action_outbox;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES('production/mysql/015_server_action_outbox', 'gothic-mmo-server-action-outbox-v1-mysql', 'Server action outbox/RPC boundary for C++ semantic hook integration with idempotent action enqueue and apply/fail state.')
ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes), applied_at=CURRENT_TIMESTAMP(6);
