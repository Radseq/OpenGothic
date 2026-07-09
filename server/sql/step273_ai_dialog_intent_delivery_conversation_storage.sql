-- Step273: durable dialog-intent delivery and conversation storage.
--
-- Step256-Step272 added disabled-by-default C++ transport/proof boundaries for
-- ServerNpcDialogIntent delivery, ACK/NACK, main-thread observation receipts
-- and late-observer resume preflight. This migration creates the durable
-- runtime tables those paths need before any gameplay action may be marked
-- applied.

USE mmo_ai_runtime;

INSERT INTO ai_runtime_schema_versions (migration_key, checksum, description)
VALUES (
  'step273_ai_dialog_intent_delivery_conversation_storage',
  REPEAT('0', 64),
  'Durable gameplay delivery, receipt, conversation observer and dead-letter storage for diagnostic dialog intent transport.'
)
ON DUPLICATE KEY UPDATE
  description = VALUES(description),
  applied_at = CURRENT_TIMESTAMP(6);

CREATE TABLE IF NOT EXISTS dialog_intent_conversation_sessions (
  conversation_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  conversation_key VARCHAR(191) NOT NULL,
  world_instance_uuid CHAR(36) NOT NULL,
  world_name VARCHAR(191) NOT NULL DEFAULT '',
  content_revision_key VARCHAR(191) NOT NULL DEFAULT '',
  speaker_entity_key VARCHAR(191) NOT NULL DEFAULT '',
  speaker_npc_instance_uuid CHAR(36) NOT NULL DEFAULT '',
  line_id VARCHAR(191) NOT NULL DEFAULT '',
  audio_ref VARCHAR(255) NOT NULL DEFAULT '',
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  start_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  duration_ms INT UNSIGNED NOT NULL DEFAULT 0,
  planned_recipients INT UNSIGNED NOT NULL DEFAULT 0,
  conversation_status VARCHAR(32) NOT NULL DEFAULT 'open',
  terminal_reason VARCHAR(128) NOT NULL DEFAULT '',
  terminal_message TEXT NULL,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  opened_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  terminal_at TIMESTAMP(6) NULL DEFAULT NULL,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (conversation_id),
  UNIQUE KEY dialog_intent_conversation_key_uk (conversation_key),
  KEY ix_dialog_conversation_world_tick (world_instance_uuid, server_tick),
  KEY ix_dialog_conversation_status (conversation_status, opened_at),
  KEY ix_dialog_conversation_speaker (world_instance_uuid, speaker_entity_key, opened_at),
  CONSTRAINT dialog_intent_conversation_status_ck CHECK (
    conversation_status IN ('open', 'partially_acked', 'acked', 'nacked', 'timed_out', 'mixed_terminal')
  ),
  CONSTRAINT dialog_intent_conversation_tick_ck CHECK (server_tick >= 0 AND start_tick >= 0),
  CONSTRAINT dialog_intent_conversation_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS gameplay_outbound_deliveries (
  delivery_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  action_queue_id BINARY(16) NULL,
  decision_id BINARY(16) NULL,
  conversation_id BINARY(16) NULL,
  world_instance_uuid CHAR(36) NOT NULL,
  gameplay_kind VARCHAR(64) NOT NULL,
  delivery_kind VARCHAR(64) NOT NULL DEFAULT 'initial',
  action_id VARCHAR(191) NOT NULL,
  ack_key VARCHAR(191) NOT NULL,
  target_session_uuid CHAR(36) NOT NULL DEFAULT '',
  target_character_uuid CHAR(36) NOT NULL DEFAULT '',
  target_character_key VARCHAR(191) NOT NULL DEFAULT '',
  packet_kind VARCHAR(64) NOT NULL,
  packet_sequence BIGINT UNSIGNED NOT NULL DEFAULT 0,
  local_sequence BIGINT UNSIGNED NOT NULL DEFAULT 0,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  payload_sha256 CHAR(64) NOT NULL DEFAULT '',
  payload_bytes INT UNSIGNED NOT NULL DEFAULT 0,
  send_attempts INT UNSIGNED NOT NULL DEFAULT 0,
  delivery_status VARCHAR(32) NOT NULL DEFAULT 'pending',
  terminal_reason VARCHAR(128) NOT NULL DEFAULT '',
  terminal_message TEXT NULL,
  request_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  endpoint_audit JSON NOT NULL DEFAULT (JSON_OBJECT()),
  sent_at TIMESTAMP(6) NULL DEFAULT NULL,
  ack_deadline_at TIMESTAMP(6) NULL DEFAULT NULL,
  terminal_at TIMESTAMP(6) NULL DEFAULT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (delivery_id),
  UNIQUE KEY gameplay_delivery_ack_key_uk (ack_key),
  UNIQUE KEY gameplay_delivery_action_id_uk (action_id),
  KEY ix_gameplay_delivery_action_queue (action_queue_id),
  KEY ix_gameplay_delivery_decision (decision_id),
  KEY ix_gameplay_delivery_conversation (conversation_id),
  KEY ix_gameplay_delivery_world_status (world_instance_uuid, delivery_status, created_at),
  KEY ix_gameplay_delivery_target (target_session_uuid, target_character_key, created_at),
  KEY ix_gameplay_delivery_deadline (delivery_status, ack_deadline_at),
  CONSTRAINT gameplay_delivery_action_fk
    FOREIGN KEY (action_queue_id) REFERENCES npc_perception_action_queue(action_queue_id) ON DELETE SET NULL,
  CONSTRAINT gameplay_delivery_decision_fk
    FOREIGN KEY (decision_id) REFERENCES npc_perception_decisions(decision_id) ON DELETE SET NULL,
  CONSTRAINT gameplay_delivery_conversation_fk
    FOREIGN KEY (conversation_id) REFERENCES dialog_intent_conversation_sessions(conversation_id) ON DELETE SET NULL,
  CONSTRAINT gameplay_delivery_status_ck CHECK (
    delivery_status IN ('pending', 'acked', 'nacked', 'timed_out', 'send_failed', 'dead_letter')
  ),
  CONSTRAINT gameplay_delivery_kind_ck CHECK (
    gameplay_kind IN ('npc_dialog_intent', 'npc_dialog_resume')
  ),
  CONSTRAINT gameplay_delivery_payload_sha_ck CHECK (payload_sha256 = '' OR REGEXP_LIKE(payload_sha256, '^[0-9a-f]{64}$')),
  CONSTRAINT gameplay_delivery_payload_json_ck CHECK (JSON_VALID(request_payload)),
  CONSTRAINT gameplay_delivery_endpoint_json_ck CHECK (JSON_VALID(endpoint_audit))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS dialog_intent_conversation_observers (
  observer_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  conversation_id BINARY(16) NOT NULL,
  delivery_id BINARY(16) NULL,
  observer_session_uuid CHAR(36) NOT NULL,
  observer_character_uuid CHAR(36) NOT NULL DEFAULT '',
  observer_character_key VARCHAR(191) NOT NULL DEFAULT '',
  observer_kind VARCHAR(32) NOT NULL DEFAULT 'target_session',
  observer_status VARCHAR(32) NOT NULL DEFAULT 'pending',
  observation_status VARCHAR(32) NOT NULL DEFAULT 'none',
  action_id VARCHAR(191) NOT NULL DEFAULT '',
  ack_key VARCHAR(191) NOT NULL DEFAULT '',
  packet_sequence BIGINT UNSIGNED NOT NULL DEFAULT 0,
  local_sequence BIGINT UNSIGNED NOT NULL DEFAULT 0,
  distance_squared DOUBLE NOT NULL DEFAULT 0,
  has_position TINYINT(1) NOT NULL DEFAULT 0,
  terminal_reason VARCHAR(128) NOT NULL DEFAULT '',
  terminal_message TEXT NULL,
  observation_reason VARCHAR(128) NOT NULL DEFAULT '',
  observation_message TEXT NULL,
  observation_ui_applied TINYINT(1) NOT NULL DEFAULT 0,
  observation_audio_applied TINYINT(1) NOT NULL DEFAULT 0,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  registered_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  sent_at TIMESTAMP(6) NULL DEFAULT NULL,
  ack_deadline_at TIMESTAMP(6) NULL DEFAULT NULL,
  observed_at TIMESTAMP(6) NULL DEFAULT NULL,
  terminal_at TIMESTAMP(6) NULL DEFAULT NULL,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (observer_id),
  UNIQUE KEY dialog_observer_ack_key_uk (ack_key),
  UNIQUE KEY dialog_observer_session_uk (conversation_id, observer_session_uuid),
  KEY ix_dialog_observer_delivery (delivery_id),
  KEY ix_dialog_observer_status (observer_status, registered_at),
  KEY ix_dialog_observer_observation (observation_status, observed_at),
  CONSTRAINT dialog_observer_conversation_fk
    FOREIGN KEY (conversation_id) REFERENCES dialog_intent_conversation_sessions(conversation_id) ON DELETE CASCADE,
  CONSTRAINT dialog_observer_delivery_fk
    FOREIGN KEY (delivery_id) REFERENCES gameplay_outbound_deliveries(delivery_id) ON DELETE SET NULL,
  CONSTRAINT dialog_observer_status_ck CHECK (observer_status IN ('pending', 'acked', 'nacked', 'timed_out')),
  CONSTRAINT dialog_observer_observation_ck CHECK (observation_status IN ('none', 'observed', 'skipped')),
  CONSTRAINT dialog_observer_kind_ck CHECK (observer_kind IN ('target_session', 'aoi', 'late_observer')),
  CONSTRAINT dialog_observer_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS gameplay_delivery_receipts (
  receipt_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  delivery_id BINARY(16) NULL,
  conversation_id BINARY(16) NULL,
  receipt_kind VARCHAR(48) NOT NULL,
  action_id VARCHAR(191) NOT NULL DEFAULT '',
  ack_key VARCHAR(191) NOT NULL DEFAULT '',
  session_uuid CHAR(36) NOT NULL DEFAULT '',
  character_uuid CHAR(36) NOT NULL DEFAULT '',
  character_key VARCHAR(191) NOT NULL DEFAULT '',
  client_observation_status VARCHAR(32) NOT NULL DEFAULT '',
  reason VARCHAR(128) NOT NULL DEFAULT '',
  message_text TEXT NULL,
  payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  received_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (receipt_id),
  KEY ix_gameplay_receipt_delivery (delivery_id, received_at),
  KEY ix_gameplay_receipt_conversation (conversation_id, received_at),
  KEY ix_gameplay_receipt_ack (ack_key, received_at),
  KEY ix_gameplay_receipt_kind (receipt_kind, received_at),
  CONSTRAINT gameplay_receipt_delivery_fk
    FOREIGN KEY (delivery_id) REFERENCES gameplay_outbound_deliveries(delivery_id) ON DELETE SET NULL,
  CONSTRAINT gameplay_receipt_conversation_fk
    FOREIGN KEY (conversation_id) REFERENCES dialog_intent_conversation_sessions(conversation_id) ON DELETE SET NULL,
  CONSTRAINT gameplay_receipt_kind_ck CHECK (
    receipt_kind IN (
      'acked', 'nacked', 'observed', 'skipped', 'duplicate',
      'unknown_ack_key', 'conflicting_action_id',
      'conflicting_terminal_status', 'conflicting_observation_status',
      'invalid_receipt', 'late_after_timeout'
    )
  ),
  CONSTRAINT gameplay_receipt_observation_ck CHECK (
    client_observation_status IN ('', 'none', 'observed', 'skipped')
  ),
  CONSTRAINT gameplay_receipt_payload_json_ck CHECK (JSON_VALID(payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS gameplay_delivery_dead_letters (
  dead_letter_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  delivery_id BINARY(16) NULL,
  conversation_id BINARY(16) NULL,
  action_id VARCHAR(191) NOT NULL DEFAULT '',
  ack_key VARCHAR(191) NOT NULL DEFAULT '',
  dead_letter_reason VARCHAR(128) NOT NULL,
  retryable TINYINT(1) NOT NULL DEFAULT 0,
  send_attempts INT UNSIGNED NOT NULL DEFAULT 0,
  payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (dead_letter_id),
  KEY ix_gameplay_dead_letter_delivery (delivery_id, created_at),
  KEY ix_gameplay_dead_letter_conversation (conversation_id, created_at),
  KEY ix_gameplay_dead_letter_reason (dead_letter_reason, created_at),
  CONSTRAINT gameplay_dead_letter_delivery_fk
    FOREIGN KEY (delivery_id) REFERENCES gameplay_outbound_deliveries(delivery_id) ON DELETE SET NULL,
  CONSTRAINT gameplay_dead_letter_conversation_fk
    FOREIGN KEY (conversation_id) REFERENCES dialog_intent_conversation_sessions(conversation_id) ON DELETE SET NULL,
  CONSTRAINT gameplay_dead_letter_payload_json_ck CHECK (JSON_VALID(payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP VIEW IF EXISTS v_gameplay_outbound_deliveries;
CREATE VIEW v_gameplay_outbound_deliveries AS
SELECT
  BIN_TO_UUID(d.delivery_id, 1) AS delivery_uuid,
  BIN_TO_UUID(d.action_queue_id, 1) AS action_queue_uuid,
  BIN_TO_UUID(d.decision_id, 1) AS decision_uuid,
  BIN_TO_UUID(d.conversation_id, 1) AS conversation_uuid,
  d.world_instance_uuid,
  d.gameplay_kind,
  d.delivery_kind,
  d.action_id,
  d.ack_key,
  d.target_session_uuid,
  d.target_character_uuid,
  d.target_character_key,
  d.packet_kind,
  d.packet_sequence,
  d.local_sequence,
  d.server_tick,
  d.payload_sha256,
  d.payload_bytes,
  d.send_attempts,
  d.delivery_status,
  d.terminal_reason,
  d.terminal_message,
  d.sent_at,
  d.ack_deadline_at,
  d.terminal_at,
  d.created_at,
  d.updated_at
FROM gameplay_outbound_deliveries d;

DROP VIEW IF EXISTS v_dialog_intent_conversation_observers;
CREATE VIEW v_dialog_intent_conversation_observers AS
SELECT
  BIN_TO_UUID(c.conversation_id, 1) AS conversation_uuid,
  c.conversation_key,
  c.world_instance_uuid,
  c.world_name,
  c.speaker_entity_key,
  c.speaker_npc_instance_uuid,
  c.line_id,
  c.audio_ref,
  c.server_tick,
  c.start_tick,
  c.duration_ms,
  c.conversation_status,
  BIN_TO_UUID(o.observer_id, 1) AS observer_uuid,
  BIN_TO_UUID(o.delivery_id, 1) AS delivery_uuid,
  o.observer_session_uuid,
  o.observer_character_uuid,
  o.observer_character_key,
  o.observer_kind,
  o.observer_status,
  o.observation_status,
  o.action_id,
  o.ack_key,
  o.packet_sequence,
  o.local_sequence,
  o.distance_squared,
  o.has_position,
  o.terminal_reason,
  o.observation_reason,
  o.observation_ui_applied,
  o.observation_audio_applied,
  o.registered_at,
  o.sent_at,
  o.ack_deadline_at,
  o.observed_at,
  o.terminal_at
FROM dialog_intent_conversation_sessions c
JOIN dialog_intent_conversation_observers o ON o.conversation_id = c.conversation_id;

DROP VIEW IF EXISTS v_gameplay_delivery_health;
CREATE VIEW v_gameplay_delivery_health AS
SELECT
  'global' AS health_scope,
  (SELECT COUNT(*) FROM gameplay_outbound_deliveries WHERE delivery_status = 'pending') AS pending_count,
  (SELECT COUNT(*) FROM gameplay_outbound_deliveries WHERE delivery_status = 'acked') AS acked_count,
  (SELECT COUNT(*) FROM gameplay_outbound_deliveries WHERE delivery_status = 'nacked') AS nacked_count,
  (SELECT COUNT(*) FROM gameplay_outbound_deliveries WHERE delivery_status = 'timed_out') AS timed_out_count,
  (SELECT COUNT(*) FROM gameplay_outbound_deliveries WHERE delivery_status IN ('send_failed', 'dead_letter')) AS failed_count,
  (SELECT COUNT(*) FROM gameplay_outbound_deliveries WHERE delivery_status = 'pending' AND ack_deadline_at <= CURRENT_TIMESTAMP(6)) AS overdue_count,
  (SELECT COUNT(*) FROM dialog_intent_conversation_sessions WHERE conversation_status = 'open') AS open_conversations,
  (SELECT COUNT(*) FROM gameplay_delivery_receipts) AS receipt_count,
  (SELECT COUNT(*) FROM gameplay_delivery_dead_letters) AS dead_letter_count;

DROP PROCEDURE IF EXISTS mmo_ai_record_gameplay_delivery_sent;
DROP PROCEDURE IF EXISTS mmo_ai_record_gameplay_delivery_receipt;
DROP PROCEDURE IF EXISTS mmo_ai_mark_gameplay_delivery_timed_out;
DROP PROCEDURE IF EXISTS mmo_ai_record_gameplay_delivery_dead_letter;

DELIMITER $$
CREATE PROCEDURE mmo_ai_record_gameplay_delivery_sent(
  IN p_action_queue_id BINARY(16),
  IN p_decision_id BINARY(16),
  IN p_conversation_id BINARY(16),
  IN p_world_instance_uuid CHAR(36),
  IN p_gameplay_kind VARCHAR(64),
  IN p_delivery_kind VARCHAR(64),
  IN p_action_id VARCHAR(191),
  IN p_ack_key VARCHAR(191),
  IN p_target_session_uuid CHAR(36),
  IN p_target_character_uuid CHAR(36),
  IN p_target_character_key VARCHAR(191),
  IN p_packet_kind VARCHAR(64),
  IN p_packet_sequence BIGINT UNSIGNED,
  IN p_local_sequence BIGINT UNSIGNED,
  IN p_server_tick BIGINT UNSIGNED,
  IN p_payload_sha256 CHAR(64),
  IN p_payload_bytes INT UNSIGNED,
  IN p_ack_timeout_ms INT UNSIGNED,
  IN p_request_payload JSON,
  IN p_endpoint_audit JSON,
  OUT o_delivery_id BINARY(16),
  OUT o_delivery_status VARCHAR(32)
)
BEGIN
  SET o_delivery_id = NULL;
  SET o_delivery_status = NULL;

  INSERT INTO gameplay_outbound_deliveries (
    action_queue_id, decision_id, conversation_id, world_instance_uuid,
    gameplay_kind, delivery_kind, action_id, ack_key,
    target_session_uuid, target_character_uuid, target_character_key,
    packet_kind, packet_sequence, local_sequence, server_tick,
    payload_sha256, payload_bytes, send_attempts, delivery_status,
    request_payload, endpoint_audit, sent_at, ack_deadline_at
  )
  VALUES (
    p_action_queue_id, p_decision_id, p_conversation_id, p_world_instance_uuid,
    COALESCE(NULLIF(p_gameplay_kind, ''), 'npc_dialog_intent'),
    COALESCE(NULLIF(p_delivery_kind, ''), 'initial'),
    p_action_id, p_ack_key,
    COALESCE(p_target_session_uuid, ''), COALESCE(p_target_character_uuid, ''),
    COALESCE(p_target_character_key, ''),
    COALESCE(NULLIF(p_packet_kind, ''), 'ServerNpcDialogIntent'),
    COALESCE(p_packet_sequence, 0), COALESCE(p_local_sequence, 0), COALESCE(p_server_tick, 0),
    COALESCE(p_payload_sha256, ''), COALESCE(p_payload_bytes, 0), 1, 'pending',
    COALESCE(p_request_payload, JSON_OBJECT()), COALESCE(p_endpoint_audit, JSON_OBJECT()),
    CURRENT_TIMESTAMP(6),
    TIMESTAMPADD(MICROSECOND, COALESCE(p_ack_timeout_ms, 0) * 1000, CURRENT_TIMESTAMP(6))
  )
  ON DUPLICATE KEY UPDATE
    send_attempts = IF(delivery_status = 'pending', send_attempts + 1, send_attempts),
    sent_at = IF(delivery_status = 'pending', CURRENT_TIMESTAMP(6), sent_at),
    ack_deadline_at = IF(
      delivery_status = 'pending',
      TIMESTAMPADD(MICROSECOND, COALESCE(p_ack_timeout_ms, 0) * 1000, CURRENT_TIMESTAMP(6)),
      ack_deadline_at
    ),
    endpoint_audit = IF(delivery_status = 'pending', COALESCE(p_endpoint_audit, JSON_OBJECT()), endpoint_audit),
    updated_at = CURRENT_TIMESTAMP(6);

  SELECT delivery_id, delivery_status
    INTO o_delivery_id, o_delivery_status
    FROM gameplay_outbound_deliveries
   WHERE ack_key = p_ack_key OR action_id = p_action_id
   ORDER BY CASE WHEN ack_key = p_ack_key THEN 0 ELSE 1 END
   LIMIT 1;
END$$

CREATE PROCEDURE mmo_ai_record_gameplay_delivery_receipt(
  IN p_action_id VARCHAR(191),
  IN p_ack_key VARCHAR(191),
  IN p_receipt_kind VARCHAR(48),
  IN p_session_uuid CHAR(36),
  IN p_character_uuid CHAR(36),
  IN p_character_key VARCHAR(191),
  IN p_client_observation_status VARCHAR(32),
  IN p_reason VARCHAR(128),
  IN p_message_text TEXT,
  IN p_payload JSON,
  OUT o_delivery_id BINARY(16),
  OUT o_receipt_kind VARCHAR(48),
  OUT o_delivery_status VARCHAR(32)
)
BEGIN
  DECLARE v_not_found BOOL DEFAULT FALSE;
  DECLARE v_conversation_id BINARY(16) DEFAULT NULL;
  DECLARE v_current_status VARCHAR(32) DEFAULT NULL;
  DECLARE v_effective_kind VARCHAR(48) DEFAULT NULL;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;

  SET o_delivery_id = NULL;
  SET o_receipt_kind = 'invalid_receipt';
  SET o_delivery_status = NULL;

  SELECT delivery_id, conversation_id, delivery_status
    INTO o_delivery_id, v_conversation_id, v_current_status
    FROM gameplay_outbound_deliveries
   WHERE ack_key = COALESCE(p_ack_key, '')
      OR (COALESCE(p_ack_key, '') = '' AND action_id = COALESCE(p_action_id, ''))
   ORDER BY CASE WHEN ack_key = COALESCE(p_ack_key, '') THEN 0 ELSE 1 END
   LIMIT 1;

  IF v_not_found OR o_delivery_id IS NULL THEN
    SET v_effective_kind = 'unknown_ack_key';
    INSERT INTO gameplay_delivery_receipts (
      receipt_kind, action_id, ack_key, session_uuid, character_uuid,
      character_key, client_observation_status, reason, message_text, payload
    )
    VALUES (
      v_effective_kind, COALESCE(p_action_id, ''), COALESCE(p_ack_key, ''),
      COALESCE(p_session_uuid, ''), COALESCE(p_character_uuid, ''),
      COALESCE(p_character_key, ''), COALESCE(p_client_observation_status, ''),
      COALESCE(p_reason, ''), p_message_text, COALESCE(p_payload, JSON_OBJECT())
    );
    SET o_receipt_kind = v_effective_kind;
  ELSE
    SET v_effective_kind = CASE
      WHEN COALESCE(p_receipt_kind, '') IN (
        'acked', 'nacked', 'observed', 'skipped', 'duplicate',
        'conflicting_action_id', 'conflicting_terminal_status',
        'conflicting_observation_status', 'late_after_timeout'
      ) THEN p_receipt_kind
      ELSE 'invalid_receipt'
    END;

    IF v_current_status = 'timed_out' AND v_effective_kind IN ('acked', 'nacked') THEN
      SET v_effective_kind = 'late_after_timeout';
    ELSEIF v_current_status IN ('acked', 'nacked', 'send_failed', 'dead_letter')
       AND v_effective_kind IN ('acked', 'nacked') THEN
      SET v_effective_kind = 'duplicate';
    END IF;

    INSERT INTO gameplay_delivery_receipts (
      delivery_id, conversation_id, receipt_kind, action_id, ack_key,
      session_uuid, character_uuid, character_key, client_observation_status,
      reason, message_text, payload
    )
    VALUES (
      o_delivery_id, v_conversation_id, v_effective_kind,
      COALESCE(p_action_id, ''), COALESCE(p_ack_key, ''),
      COALESCE(p_session_uuid, ''), COALESCE(p_character_uuid, ''),
      COALESCE(p_character_key, ''), COALESCE(p_client_observation_status, ''),
      COALESCE(p_reason, ''), p_message_text, COALESCE(p_payload, JSON_OBJECT())
    );

    IF v_current_status = 'pending' AND v_effective_kind IN ('acked', 'nacked') THEN
      UPDATE gameplay_outbound_deliveries
         SET delivery_status = v_effective_kind,
             terminal_reason = COALESCE(p_reason, ''),
             terminal_message = p_message_text,
             terminal_at = CURRENT_TIMESTAMP(6),
             updated_at = CURRENT_TIMESTAMP(6)
       WHERE delivery_id = o_delivery_id;
    END IF;

    UPDATE dialog_intent_conversation_observers
       SET observation_status = CASE
             WHEN v_effective_kind = 'observed' THEN 'observed'
             WHEN v_effective_kind = 'skipped' THEN 'skipped'
             ELSE observation_status
           END,
           observer_status = CASE
             WHEN v_current_status = 'pending' AND v_effective_kind = 'acked' THEN 'acked'
             WHEN v_current_status = 'pending' AND v_effective_kind = 'nacked' THEN 'nacked'
             ELSE observer_status
           END,
           observed_at = CASE
             WHEN v_effective_kind IN ('observed', 'skipped') THEN CURRENT_TIMESTAMP(6)
             ELSE observed_at
           END,
           terminal_reason = CASE
             WHEN v_effective_kind IN ('acked', 'nacked') THEN COALESCE(p_reason, '')
             ELSE terminal_reason
           END,
           terminal_message = CASE
             WHEN v_effective_kind IN ('acked', 'nacked') THEN p_message_text
             ELSE terminal_message
           END,
           terminal_at = CASE
             WHEN v_current_status = 'pending' AND v_effective_kind IN ('acked', 'nacked') THEN CURRENT_TIMESTAMP(6)
             ELSE terminal_at
           END,
           updated_at = CURRENT_TIMESTAMP(6)
     WHERE delivery_id = o_delivery_id;

    SELECT delivery_status
      INTO o_delivery_status
      FROM gameplay_outbound_deliveries
     WHERE delivery_id = o_delivery_id
     LIMIT 1;

    SET o_receipt_kind = v_effective_kind;
  END IF;
END$$

CREATE PROCEDURE mmo_ai_mark_gameplay_delivery_timed_out(
  IN p_delivery_id BINARY(16),
  IN p_now TIMESTAMP(6),
  OUT o_timed_out_count INT
)
BEGIN
  UPDATE gameplay_outbound_deliveries
     SET delivery_status = 'timed_out',
         terminal_reason = 'ack_timeout',
         terminal_at = COALESCE(p_now, CURRENT_TIMESTAMP(6)),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE delivery_status = 'pending'
     AND (p_delivery_id IS NULL OR delivery_id = p_delivery_id)
     AND ack_deadline_at IS NOT NULL
     AND ack_deadline_at <= COALESCE(p_now, CURRENT_TIMESTAMP(6));

  SET o_timed_out_count = ROW_COUNT();

  UPDATE dialog_intent_conversation_observers o
  JOIN gameplay_outbound_deliveries d ON d.delivery_id = o.delivery_id
     SET o.observer_status = 'timed_out',
         o.terminal_reason = 'ack_timeout',
         o.terminal_at = COALESCE(p_now, CURRENT_TIMESTAMP(6)),
         o.updated_at = CURRENT_TIMESTAMP(6)
   WHERE d.delivery_status = 'timed_out'
     AND o.observer_status = 'pending';
END$$

CREATE PROCEDURE mmo_ai_record_gameplay_delivery_dead_letter(
  IN p_delivery_id BINARY(16),
  IN p_dead_letter_reason VARCHAR(128),
  IN p_retryable TINYINT(1),
  IN p_payload JSON,
  OUT o_dead_letter_id BINARY(16)
)
BEGIN
  SET o_dead_letter_id = UUID_TO_BIN(UUID(), 1);

  INSERT INTO gameplay_delivery_dead_letters (
    dead_letter_id, delivery_id, conversation_id, action_id, ack_key,
    dead_letter_reason, retryable, send_attempts, payload
  )
  SELECT
    o_dead_letter_id, delivery_id, conversation_id, action_id, ack_key,
    COALESCE(NULLIF(p_dead_letter_reason, ''), 'send_failure'),
    COALESCE(p_retryable, 0), send_attempts,
    COALESCE(p_payload, JSON_OBJECT())
    FROM gameplay_outbound_deliveries
   WHERE delivery_id = p_delivery_id;

  UPDATE gameplay_outbound_deliveries
     SET delivery_status = 'dead_letter',
         terminal_reason = COALESCE(NULLIF(p_dead_letter_reason, ''), 'send_failure'),
         terminal_at = CURRENT_TIMESTAMP(6),
         updated_at = CURRENT_TIMESTAMP(6)
   WHERE delivery_id = p_delivery_id
     AND delivery_status IN ('pending', 'send_failed', 'timed_out');
END$$
DELIMITER ;
