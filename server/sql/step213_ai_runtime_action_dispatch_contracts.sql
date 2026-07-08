-- Step213: dispatch contracts for AI runtime NPC perception actions.
--
-- Step212 created the queue where authoritative NPC perception decisions can
-- enqueue effects such as turn, approach, greet, warn, dialog or attack. This
-- step makes that queue consumable by a future C++ server tick/worker without
-- routing through the generic runtime outbox.

USE mmo_ai_runtime;

DROP PROCEDURE IF EXISTS mmo_ai_add_action_queue_column_if_missing;

DELIMITER $$
CREATE PROCEDURE mmo_ai_add_action_queue_column_if_missing(
  IN p_column_name VARCHAR(64),
  IN p_column_ddl TEXT
)
BEGIN
  DECLARE v_column_count INT DEFAULT 0;

  SELECT COUNT(*) INTO v_column_count
    FROM information_schema.columns
   WHERE table_schema = DATABASE()
     AND table_name = 'npc_perception_action_queue'
     AND column_name = p_column_name;

  IF v_column_count = 0 THEN
    SET @mmo_ai_alter_sql = CONCAT('ALTER TABLE npc_perception_action_queue ADD COLUMN ', p_column_ddl);
    PREPARE mmo_ai_alter_stmt FROM @mmo_ai_alter_sql;
    EXECUTE mmo_ai_alter_stmt;
    DEALLOCATE PREPARE mmo_ai_alter_stmt;
    SET @mmo_ai_alter_sql = NULL;
  END IF;
END$$
DELIMITER ;

CALL mmo_ai_add_action_queue_column_if_missing('max_attempts', 'max_attempts INT NOT NULL DEFAULT 5 AFTER priority_value');
CALL mmo_ai_add_action_queue_column_if_missing('attempt_count', 'attempt_count INT NOT NULL DEFAULT 0 AFTER max_attempts');
CALL mmo_ai_add_action_queue_column_if_missing('worker_id', 'worker_id VARCHAR(191) NOT NULL DEFAULT '''' AFTER attempt_count');
CALL mmo_ai_add_action_queue_column_if_missing('locked_at', 'locked_at TIMESTAMP(6) NULL DEFAULT NULL AFTER updated_at');
CALL mmo_ai_add_action_queue_column_if_missing('applied_at', 'applied_at TIMESTAMP(6) NULL DEFAULT NULL AFTER locked_at');
CALL mmo_ai_add_action_queue_column_if_missing('failed_at', 'failed_at TIMESTAMP(6) NULL DEFAULT NULL AFTER applied_at');
CALL mmo_ai_add_action_queue_column_if_missing('completed_at', 'completed_at TIMESTAMP(6) NULL DEFAULT NULL AFTER failed_at');
CALL mmo_ai_add_action_queue_column_if_missing('next_attempt_at', 'next_attempt_at TIMESTAMP(6) NULL DEFAULT NULL AFTER completed_at');
CALL mmo_ai_add_action_queue_column_if_missing('last_error_code', 'last_error_code VARCHAR(64) NOT NULL DEFAULT '''' AFTER next_attempt_at');
CALL mmo_ai_add_action_queue_column_if_missing('last_error_message', 'last_error_message TEXT NULL AFTER last_error_code');
CALL mmo_ai_add_action_queue_column_if_missing('result_payload', 'result_payload JSON NOT NULL DEFAULT (JSON_OBJECT()) AFTER request_payload');

DROP PROCEDURE IF EXISTS mmo_ai_add_action_queue_column_if_missing;

CREATE TABLE IF NOT EXISTS npc_perception_action_dispatch_log (
  dispatch_log_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  action_queue_id BINARY(16) NOT NULL,
  decision_id BINARY(16) NOT NULL,
  worker_id VARCHAR(191) NOT NULL DEFAULT '',
  dispatch_event VARCHAR(32) NOT NULL,
  action_status VARCHAR(32) NOT NULL,
  attempt_count INT NOT NULL DEFAULT 0,
  error_code VARCHAR(64) NOT NULL DEFAULT '',
  message_text TEXT NULL,
  payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (dispatch_log_id),
  KEY ix_npc_perception_dispatch_action (action_queue_id, created_at),
  KEY ix_npc_perception_dispatch_decision (decision_id, created_at),
  KEY ix_npc_perception_dispatch_worker (worker_id, created_at),
  KEY ix_npc_perception_dispatch_event (dispatch_event, created_at),
  CONSTRAINT npc_perception_dispatch_action_fk
    FOREIGN KEY (action_queue_id) REFERENCES npc_perception_action_queue(action_queue_id) ON DELETE CASCADE,
  CONSTRAINT npc_perception_dispatch_decision_fk
    FOREIGN KEY (decision_id) REFERENCES npc_perception_decisions(decision_id) ON DELETE CASCADE,
  CONSTRAINT npc_perception_dispatch_event_ck CHECK (dispatch_event IN ('claimed', 'applied', 'failed', 'retry_scheduled', 'skipped')),
  CONSTRAINT npc_perception_dispatch_payload_json_ck CHECK (JSON_VALID(payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP VIEW IF EXISTS v_npc_perception_pending_actions;
CREATE VIEW v_npc_perception_pending_actions AS
SELECT
  BIN_TO_UUID(q.action_queue_id, 1) AS action_queue_uuid,
  BIN_TO_UUID(q.decision_id, 1) AS decision_uuid,
  q.world_instance_uuid,
  q.session_uuid,
  q.character_uuid,
  q.action_kind,
  q.target_key,
  q.action_status,
  q.priority_value,
  q.max_attempts,
  q.attempt_count,
  q.worker_id,
  q.idempotency_key,
  q.request_payload,
  q.result_payload,
  q.created_at,
  q.updated_at,
  q.locked_at,
  q.next_attempt_at
FROM npc_perception_action_queue q
WHERE q.action_status = 'pending'
  AND (q.next_attempt_at IS NULL OR q.next_attempt_at <= CURRENT_TIMESTAMP(6));

DROP VIEW IF EXISTS v_npc_perception_action_dispatch_log;
CREATE VIEW v_npc_perception_action_dispatch_log AS
SELECT
  BIN_TO_UUID(l.dispatch_log_id, 1) AS dispatch_log_uuid,
  BIN_TO_UUID(l.action_queue_id, 1) AS action_queue_uuid,
  BIN_TO_UUID(l.decision_id, 1) AS decision_uuid,
  l.worker_id,
  l.dispatch_event,
  l.action_status,
  l.attempt_count,
  l.error_code,
  l.message_text,
  l.payload,
  l.created_at
FROM npc_perception_action_dispatch_log l;

DROP VIEW IF EXISTS v_npc_perception_action_dispatch_health;
CREATE VIEW v_npc_perception_action_dispatch_health AS
SELECT
  'global' AS health_scope,
  (SELECT COUNT(*) FROM npc_perception_action_queue WHERE action_status = 'pending') AS pending_count,
  (SELECT COUNT(*) FROM npc_perception_action_queue WHERE action_status = 'claimed') AS claimed_count,
  (SELECT COUNT(*) FROM npc_perception_action_queue WHERE action_status = 'applied') AS applied_count,
  (SELECT COUNT(*) FROM npc_perception_action_queue WHERE action_status = 'failed') AS failed_count,
  (SELECT COUNT(*) FROM npc_perception_action_queue WHERE action_status = 'skipped') AS skipped_count,
  (SELECT COUNT(*) FROM npc_perception_action_queue WHERE action_status = 'pending' AND next_attempt_at IS NOT NULL AND next_attempt_at > CURRENT_TIMESTAMP(6)) AS delayed_retry_count,
  (SELECT COUNT(*) FROM npc_perception_action_dispatch_log) AS dispatch_log_count;

DROP PROCEDURE IF EXISTS mmo_ai_claim_next_npc_perception_action;
DROP PROCEDURE IF EXISTS mmo_ai_mark_npc_perception_action_applied;
DROP PROCEDURE IF EXISTS mmo_ai_mark_npc_perception_action_failed;
DROP PROCEDURE IF EXISTS mmo_ai_skip_npc_perception_action;

DELIMITER $$
CREATE PROCEDURE mmo_ai_claim_next_npc_perception_action(
  IN p_worker_id VARCHAR(191),
  OUT o_action_queue_id BINARY(16),
  OUT o_decision_id BINARY(16),
  OUT o_action_kind VARCHAR(128),
  OUT o_world_instance_uuid CHAR(36),
  OUT o_session_uuid CHAR(36),
  OUT o_character_uuid CHAR(36),
  OUT o_target_key VARCHAR(191),
  OUT o_idempotency_key VARCHAR(191),
  OUT o_request_payload JSON
)
BEGIN
  DECLARE v_not_found BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;

  SET o_action_queue_id = NULL;
  SET o_decision_id = NULL;
  SET o_action_kind = NULL;
  SET o_world_instance_uuid = NULL;
  SET o_session_uuid = NULL;
  SET o_character_uuid = NULL;
  SET o_target_key = NULL;
  SET o_idempotency_key = NULL;
  SET o_request_payload = NULL;

  START TRANSACTION;

  SELECT action_queue_id, decision_id, action_kind, world_instance_uuid,
         session_uuid, character_uuid, target_key, idempotency_key, request_payload
    INTO o_action_queue_id, o_decision_id, o_action_kind, o_world_instance_uuid,
         o_session_uuid, o_character_uuid, o_target_key, o_idempotency_key, o_request_payload
    FROM npc_perception_action_queue
   WHERE action_status = 'pending'
     AND attempt_count < max_attempts
     AND (next_attempt_at IS NULL OR next_attempt_at <= CURRENT_TIMESTAMP(6))
   ORDER BY priority_value ASC, created_at ASC, action_queue_id ASC
   LIMIT 1
   FOR UPDATE SKIP LOCKED;

  IF NOT v_not_found AND o_action_queue_id IS NOT NULL THEN
    UPDATE npc_perception_action_queue
       SET action_status = 'claimed',
           worker_id = COALESCE(p_worker_id, ''),
           attempt_count = attempt_count + 1,
           locked_at = CURRENT_TIMESTAMP(6),
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE action_queue_id = o_action_queue_id;

    INSERT INTO npc_perception_action_dispatch_log (
      action_queue_id, decision_id, worker_id, dispatch_event,
      action_status, attempt_count, payload
    )
    SELECT action_queue_id, decision_id, COALESCE(p_worker_id, ''), 'claimed',
           action_status, attempt_count,
           JSON_OBJECT('action_kind', action_kind, 'target_key', target_key)
      FROM npc_perception_action_queue
     WHERE action_queue_id = o_action_queue_id;
  END IF;

  COMMIT;
END$$

CREATE PROCEDURE mmo_ai_mark_npc_perception_action_applied(
  IN p_action_queue_id BINARY(16),
  IN p_worker_id VARCHAR(191),
  IN p_result_payload JSON,
  OUT o_action_status VARCHAR(32)
)
BEGIN
  DECLARE v_not_found BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;

  SET o_action_status = NULL;

  UPDATE npc_perception_action_queue
     SET action_status = 'applied',
         worker_id = COALESCE(p_worker_id, worker_id),
         applied_at = CURRENT_TIMESTAMP(6),
         completed_at = CURRENT_TIMESTAMP(6),
         updated_at = CURRENT_TIMESTAMP(6),
         result_payload = JSON_MERGE_PATCH(COALESCE(result_payload, JSON_OBJECT()), COALESCE(p_result_payload, JSON_OBJECT()))
   WHERE action_queue_id = p_action_queue_id
     AND action_status IN ('claimed', 'pending');

  SELECT action_status INTO o_action_status
    FROM npc_perception_action_queue
   WHERE action_queue_id = p_action_queue_id
   LIMIT 1;

  INSERT INTO npc_perception_action_dispatch_log (
    action_queue_id, decision_id, worker_id, dispatch_event,
    action_status, attempt_count, payload
  )
  SELECT action_queue_id, decision_id, COALESCE(p_worker_id, worker_id), 'applied',
         action_status, attempt_count, COALESCE(p_result_payload, JSON_OBJECT())
    FROM npc_perception_action_queue
   WHERE action_queue_id = p_action_queue_id;
END$$

CREATE PROCEDURE mmo_ai_mark_npc_perception_action_failed(
  IN p_action_queue_id BINARY(16),
  IN p_worker_id VARCHAR(191),
  IN p_error_code VARCHAR(64),
  IN p_error_message TEXT,
  IN p_retryable TINYINT(1),
  IN p_retry_delay_seconds INT,
  IN p_result_payload JSON,
  OUT o_action_status VARCHAR(32)
)
BEGIN
  DECLARE v_not_found BOOL DEFAULT FALSE;
  DECLARE v_attempt_count INT DEFAULT 0;
  DECLARE v_max_attempts INT DEFAULT 1;
  DECLARE v_retry TINYINT(1) DEFAULT 0;
  DECLARE v_event VARCHAR(32) DEFAULT 'failed';
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;

  SELECT attempt_count, max_attempts INTO v_attempt_count, v_max_attempts
    FROM npc_perception_action_queue
   WHERE action_queue_id = p_action_queue_id
   LIMIT 1;

  SET v_retry = IF(COALESCE(p_retryable, 0) = 1 AND v_attempt_count < v_max_attempts, 1, 0);
  SET v_event = IF(v_retry = 1, 'retry_scheduled', 'failed');

  UPDATE npc_perception_action_queue
     SET action_status = IF(v_retry = 1, 'pending', 'failed'),
         worker_id = COALESCE(p_worker_id, worker_id),
         failed_at = CURRENT_TIMESTAMP(6),
         completed_at = IF(v_retry = 1, NULL, CURRENT_TIMESTAMP(6)),
         next_attempt_at = IF(v_retry = 1, DATE_ADD(CURRENT_TIMESTAMP(6), INTERVAL GREATEST(COALESCE(p_retry_delay_seconds, 0), 0) SECOND), NULL),
         last_error_code = COALESCE(p_error_code, ''),
         last_error_message = p_error_message,
         updated_at = CURRENT_TIMESTAMP(6),
         result_payload = JSON_MERGE_PATCH(COALESCE(result_payload, JSON_OBJECT()), COALESCE(p_result_payload, JSON_OBJECT()))
   WHERE action_queue_id = p_action_queue_id
     AND action_status IN ('claimed', 'pending');

  SELECT action_status INTO o_action_status
    FROM npc_perception_action_queue
   WHERE action_queue_id = p_action_queue_id
   LIMIT 1;

  INSERT INTO npc_perception_action_dispatch_log (
    action_queue_id, decision_id, worker_id, dispatch_event,
    action_status, attempt_count, error_code, message_text, payload
  )
  SELECT action_queue_id, decision_id, COALESCE(p_worker_id, worker_id), v_event,
         action_status, attempt_count, COALESCE(p_error_code, ''), p_error_message,
         COALESCE(p_result_payload, JSON_OBJECT())
    FROM npc_perception_action_queue
   WHERE action_queue_id = p_action_queue_id;
END$$

CREATE PROCEDURE mmo_ai_skip_npc_perception_action(
  IN p_action_queue_id BINARY(16),
  IN p_worker_id VARCHAR(191),
  IN p_reason TEXT,
  OUT o_action_status VARCHAR(32)
)
BEGIN
  DECLARE v_not_found BOOL DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;

  SET o_action_status = NULL;

  UPDATE npc_perception_action_queue
     SET action_status = 'skipped',
         worker_id = COALESCE(p_worker_id, worker_id),
         completed_at = CURRENT_TIMESTAMP(6),
         updated_at = CURRENT_TIMESTAMP(6),
         result_payload = JSON_MERGE_PATCH(COALESCE(result_payload, JSON_OBJECT()), JSON_OBJECT('skip_reason', COALESCE(p_reason, '')))
   WHERE action_queue_id = p_action_queue_id
     AND action_status IN ('claimed', 'pending');

  SELECT action_status INTO o_action_status
    FROM npc_perception_action_queue
   WHERE action_queue_id = p_action_queue_id
   LIMIT 1;

  INSERT INTO npc_perception_action_dispatch_log (
    action_queue_id, decision_id, worker_id, dispatch_event,
    action_status, attempt_count, message_text, payload
  )
  SELECT action_queue_id, decision_id, COALESCE(p_worker_id, worker_id), 'skipped',
         action_status, attempt_count, p_reason,
         JSON_OBJECT('skip_reason', COALESCE(p_reason, ''))
    FROM npc_perception_action_queue
   WHERE action_queue_id = p_action_queue_id;
END$$
DELIMITER ;

INSERT INTO ai_runtime_schema_versions (migration_key, checksum, description)
VALUES (
  'server/sql/step213_ai_runtime_action_dispatch_contracts.sql',
  SHA2('server/sql/step213_ai_runtime_action_dispatch_contracts.sql', 256),
  'Step213 dispatcher claim/apply/fail/skip contracts for mmo_ai_runtime NPC perception actions'
)
ON DUPLICATE KEY UPDATE
  checksum = VALUES(checksum),
  description = VALUES(description),
  applied_at = CURRENT_TIMESTAMP(6);
