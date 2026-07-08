-- Step212: separate AI runtime database for authoritative NPC perception.
--
-- mmo_content_build stores static parser output from ZEN/DAT/OU. The runtime
-- MMO database stores players, sessions and durable gameplay state. This schema
-- stores server-side AI decisions, perception cooldowns and queued presentation
-- actions. It intentionally uses stable natural keys/UUID text instead of
-- cross-database foreign keys so it can be rebuilt or split independently.

CREATE DATABASE IF NOT EXISTS mmo_ai_runtime
  DEFAULT CHARACTER SET utf8mb4
  DEFAULT COLLATE utf8mb4_0900_ai_ci;

USE mmo_ai_runtime;

CREATE TABLE IF NOT EXISTS ai_runtime_schema_versions (
  migration_key VARCHAR(255) NOT NULL,
  checksum CHAR(64) NOT NULL,
  description TEXT NOT NULL,
  applied_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (migration_key),
  CONSTRAINT ai_runtime_schema_versions_checksum_ck CHECK (REGEXP_LIKE(checksum, '^[0-9a-f]{64}$'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS npc_perception_rule_catalog (
  rule_key VARCHAR(191) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL DEFAULT '',
  perception_kind VARCHAR(64) NOT NULL,
  action_kind VARCHAR(64) NOT NULL,
  priority_value INT NOT NULL DEFAULT 100,
  max_distance DOUBLE NULL,
  cooldown_ticks BIGINT NOT NULL DEFAULT 0,
  requires_los TINYINT(1) NOT NULL DEFAULT 0,
  enabled TINYINT(1) NOT NULL DEFAULT 1,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (rule_key),
  KEY ix_npc_perception_rule_content (content_revision_key, perception_kind, enabled),
  KEY ix_npc_perception_rule_action (action_kind, priority_value),
  CONSTRAINT npc_perception_rule_action_ck CHECK (action_kind IN (
    'npc_assess_player', 'npc_turn_to_player', 'npc_approach_player',
    'npc_greet_player', 'npc_warn_player', 'npc_start_dialog',
    'npc_attack_player', 'npc_ignore_player', 'npc_noop'
  )),
  CONSTRAINT npc_perception_rule_cooldown_ck CHECK (cooldown_ticks >= 0),
  CONSTRAINT npc_perception_rule_distance_ck CHECK (max_distance IS NULL OR max_distance >= 0),
  CONSTRAINT npc_perception_rule_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS npc_perception_cooldowns (
  world_instance_uuid CHAR(36) NOT NULL,
  npc_entity_key VARCHAR(191) NOT NULL,
  target_key VARCHAR(191) NOT NULL,
  perception_kind VARCHAR(64) NOT NULL,
  cooldown_until_tick BIGINT NOT NULL DEFAULT 0,
  last_decision_id BINARY(16) NULL,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (world_instance_uuid, npc_entity_key, target_key, perception_kind),
  KEY ix_npc_perception_cooldowns_tick (world_instance_uuid, cooldown_until_tick),
  KEY ix_npc_perception_cooldowns_decision (last_decision_id),
  CONSTRAINT npc_perception_cooldowns_tick_ck CHECK (cooldown_until_tick >= 0),
  CONSTRAINT npc_perception_cooldowns_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS npc_perception_decisions (
  decision_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  world_instance_uuid CHAR(36) NOT NULL,
  session_uuid CHAR(36) NOT NULL DEFAULT '',
  character_uuid CHAR(36) NOT NULL DEFAULT '',
  character_key VARCHAR(191) NOT NULL DEFAULT '',
  npc_entity_key VARCHAR(191) NOT NULL,
  target_key VARCHAR(191) NOT NULL,
  content_revision_key VARCHAR(191) NOT NULL DEFAULT '',
  rule_key VARCHAR(191) NOT NULL DEFAULT '',
  perception_kind VARCHAR(64) NOT NULL,
  requested_decision_kind VARCHAR(64) NOT NULL,
  decision_kind VARCHAR(64) NOT NULL,
  decision_status VARCHAR(32) NOT NULL,
  priority_value INT NOT NULL DEFAULT 100,
  server_tick BIGINT NOT NULL DEFAULT 0,
  cooldown_until_tick BIGINT NOT NULL DEFAULT 0,
  enqueue_action TINYINT(1) NOT NULL DEFAULT 0,
  action_queue_id BINARY(16) NULL,
  raw_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  idempotency_key VARCHAR(191) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (decision_id),
  UNIQUE KEY npc_perception_decision_idempotency_uk (idempotency_key),
  KEY ix_npc_perception_decisions_world_tick (world_instance_uuid, server_tick, decision_status),
  KEY ix_npc_perception_decisions_npc (world_instance_uuid, npc_entity_key, server_tick),
  KEY ix_npc_perception_decisions_target (world_instance_uuid, target_key, server_tick),
  KEY ix_npc_perception_decisions_status (decision_status, created_at),
  KEY ix_npc_perception_decisions_action_queue (action_queue_id),
  CONSTRAINT npc_perception_decisions_requested_ck CHECK (requested_decision_kind IN (
    'assess_player', 'turn_to_player', 'approach_player', 'greet_player',
    'warn_player', 'start_dialog', 'attack_player', 'ignore_player',
    'cooldown_skip', 'blocked', 'noop'
  )),
  CONSTRAINT npc_perception_decisions_kind_ck CHECK (decision_kind IN (
    'assess_player', 'turn_to_player', 'approach_player', 'greet_player',
    'warn_player', 'start_dialog', 'attack_player', 'ignore_player',
    'cooldown_skip', 'blocked', 'noop'
  )),
  CONSTRAINT npc_perception_decisions_status_ck CHECK (decision_status IN ('accepted', 'queued', 'cooldown', 'blocked', 'noop')),
  CONSTRAINT npc_perception_decisions_tick_ck CHECK (server_tick >= 0 AND cooldown_until_tick >= 0),
  CONSTRAINT npc_perception_decisions_payload_json_ck CHECK (JSON_VALID(raw_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS npc_perception_action_queue (
  action_queue_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(), 1)),
  decision_id BINARY(16) NOT NULL,
  world_instance_uuid CHAR(36) NOT NULL,
  session_uuid CHAR(36) NOT NULL DEFAULT '',
  character_uuid CHAR(36) NOT NULL DEFAULT '',
  action_kind VARCHAR(128) NOT NULL,
  target_key VARCHAR(191) NOT NULL DEFAULT '',
  action_status VARCHAR(32) NOT NULL DEFAULT 'pending',
  priority_value INT NOT NULL DEFAULT 100,
  request_payload JSON NOT NULL DEFAULT (JSON_OBJECT()),
  idempotency_key VARCHAR(191) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY (action_queue_id),
  UNIQUE KEY npc_perception_action_idempotency_uk (idempotency_key),
  KEY ix_npc_perception_action_decision (decision_id),
  KEY ix_npc_perception_action_claim (action_status, priority_value, created_at),
  KEY ix_npc_perception_action_world (world_instance_uuid, action_status, created_at),
  CONSTRAINT npc_perception_action_decision_fk
    FOREIGN KEY (decision_id) REFERENCES npc_perception_decisions(decision_id) ON DELETE CASCADE,
  CONSTRAINT npc_perception_action_status_ck CHECK (action_status IN ('pending', 'claimed', 'applied', 'failed', 'skipped')),
  CONSTRAINT npc_perception_action_payload_json_ck CHECK (JSON_VALID(request_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP VIEW IF EXISTS v_npc_perception_decisions;
CREATE VIEW v_npc_perception_decisions AS
SELECT
  BIN_TO_UUID(d.decision_id, 1) AS decision_uuid,
  d.world_instance_uuid,
  d.session_uuid,
  d.character_uuid,
  d.character_key,
  d.npc_entity_key,
  d.target_key,
  d.content_revision_key,
  d.rule_key,
  d.perception_kind,
  d.requested_decision_kind,
  d.decision_kind,
  d.decision_status,
  d.priority_value,
  d.server_tick,
  d.cooldown_until_tick,
  d.enqueue_action,
  BIN_TO_UUID(d.action_queue_id, 1) AS action_queue_uuid,
  d.idempotency_key,
  d.raw_payload,
  d.created_at
FROM npc_perception_decisions d;

DROP VIEW IF EXISTS v_npc_perception_cooldowns;
CREATE VIEW v_npc_perception_cooldowns AS
SELECT
  c.world_instance_uuid,
  c.npc_entity_key,
  c.target_key,
  c.perception_kind,
  c.cooldown_until_tick,
  BIN_TO_UUID(c.last_decision_id, 1) AS last_decision_uuid,
  c.raw_payload,
  c.created_at,
  c.updated_at
FROM npc_perception_cooldowns c;

DROP VIEW IF EXISTS v_npc_perception_action_queue;
CREATE VIEW v_npc_perception_action_queue AS
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
  q.idempotency_key,
  q.request_payload,
  q.created_at,
  q.updated_at
FROM npc_perception_action_queue q;

DROP VIEW IF EXISTS v_npc_perception_policy_health;
CREATE VIEW v_npc_perception_policy_health AS
SELECT
  'global' AS health_scope,
  (SELECT COUNT(*) FROM npc_perception_rule_catalog WHERE enabled = 1) AS enabled_rule_count,
  (SELECT COUNT(*) FROM npc_perception_rule_catalog WHERE enabled = 0) AS disabled_rule_count,
  (SELECT COUNT(*) FROM npc_perception_cooldowns) AS cooldown_count,
  (SELECT COUNT(*) FROM npc_perception_decisions) AS decision_count,
  (SELECT COUNT(*) FROM npc_perception_decisions WHERE decision_status = 'queued') AS queued_decision_count,
  (SELECT COUNT(*) FROM npc_perception_decisions WHERE decision_status = 'cooldown') AS cooldown_skip_count,
  (SELECT COUNT(*) FROM npc_perception_action_queue WHERE action_status = 'pending') AS pending_action_count,
  (SELECT COUNT(*) FROM npc_perception_action_queue WHERE action_status = 'failed') AS failed_action_count;

DROP PROCEDURE IF EXISTS mmo_ai_record_npc_perception_decision;

DELIMITER $$
CREATE PROCEDURE mmo_ai_record_npc_perception_decision(
  IN p_world_instance_uuid CHAR(36),
  IN p_session_uuid CHAR(36),
  IN p_character_uuid CHAR(36),
  IN p_character_key VARCHAR(191),
  IN p_npc_entity_key VARCHAR(191),
  IN p_target_key VARCHAR(191),
  IN p_content_revision_key VARCHAR(191),
  IN p_rule_key VARCHAR(191),
  IN p_perception_kind VARCHAR(64),
  IN p_decision_kind VARCHAR(64),
  IN p_priority_value INT,
  IN p_server_tick BIGINT,
  IN p_cooldown_ticks BIGINT,
  IN p_enqueue_action TINYINT(1),
  IN p_action_payload JSON,
  IN p_idempotency_key VARCHAR(191),
  OUT o_decision_id BINARY(16),
  OUT o_decision_status VARCHAR(32),
  OUT o_action_queue_id BINARY(16)
)
main: BEGIN
  DECLARE v_existing_count INT DEFAULT 0;
  DECLARE v_server_tick BIGINT DEFAULT 0;
  DECLARE v_cooldown_ticks BIGINT DEFAULT 0;
  DECLARE v_existing_cooldown_until BIGINT DEFAULT 0;
  DECLARE v_new_cooldown_until BIGINT DEFAULT 0;
  DECLARE v_requested_decision_kind VARCHAR(64) DEFAULT 'noop';
  DECLARE v_final_decision_kind VARCHAR(64) DEFAULT 'noop';
  DECLARE v_final_status VARCHAR(32) DEFAULT 'noop';
  DECLARE v_enqueue TINYINT(1) DEFAULT 0;
  DECLARE v_payload JSON;

  SET o_decision_id = NULL;
  SET o_decision_status = 'blocked';
  SET o_action_queue_id = NULL;

  SET v_server_tick = GREATEST(COALESCE(p_server_tick, 0), 0);
  SET v_cooldown_ticks = GREATEST(COALESCE(p_cooldown_ticks, 0), 0);
  SET v_requested_decision_kind = COALESCE(NULLIF(p_decision_kind, ''), 'noop');
  SET v_final_decision_kind = v_requested_decision_kind;
  SET v_enqueue = IF(COALESCE(p_enqueue_action, 0) = 1, 1, 0);
  SET v_payload = COALESCE(p_action_payload, JSON_OBJECT());

  IF COALESCE(p_idempotency_key, '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'p_idempotency_key is required';
  END IF;
  IF COALESCE(p_world_instance_uuid, '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'p_world_instance_uuid is required';
  END IF;
  IF COALESCE(p_npc_entity_key, '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'p_npc_entity_key is required';
  END IF;
  IF COALESCE(p_target_key, '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'p_target_key is required';
  END IF;
  IF COALESCE(p_perception_kind, '') = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'p_perception_kind is required';
  END IF;

  SELECT COUNT(*) INTO v_existing_count
  FROM npc_perception_decisions
  WHERE idempotency_key = p_idempotency_key;

  IF v_existing_count > 0 THEN
    SELECT decision_id, decision_status, action_queue_id
      INTO o_decision_id, o_decision_status, o_action_queue_id
    FROM npc_perception_decisions
    WHERE idempotency_key = p_idempotency_key
    LIMIT 1;
    LEAVE main;
  END IF;

  SELECT COALESCE(MAX(cooldown_until_tick), 0) INTO v_existing_cooldown_until
  FROM npc_perception_cooldowns
  WHERE world_instance_uuid = p_world_instance_uuid
    AND npc_entity_key = p_npc_entity_key
    AND target_key = p_target_key
    AND perception_kind = p_perception_kind;

  IF v_existing_cooldown_until > v_server_tick THEN
    SET v_final_status = 'cooldown';
    SET v_final_decision_kind = 'cooldown_skip';
    SET v_enqueue = 0;
    SET v_new_cooldown_until = v_existing_cooldown_until;
  ELSEIF v_requested_decision_kind = 'blocked' THEN
    SET v_final_status = 'blocked';
    SET v_new_cooldown_until = v_server_tick + v_cooldown_ticks;
  ELSEIF v_requested_decision_kind = 'noop' OR v_requested_decision_kind = 'ignore_player' THEN
    SET v_final_status = 'noop';
    SET v_new_cooldown_until = v_server_tick + v_cooldown_ticks;
  ELSEIF v_enqueue = 1 THEN
    SET v_final_status = 'queued';
    SET v_new_cooldown_until = v_server_tick + v_cooldown_ticks;
  ELSE
    SET v_final_status = 'accepted';
    SET v_new_cooldown_until = v_server_tick + v_cooldown_ticks;
  END IF;

  SET o_decision_id = UUID_TO_BIN(UUID(), 1);
  INSERT INTO npc_perception_decisions (
    decision_id, world_instance_uuid, session_uuid, character_uuid, character_key,
    npc_entity_key, target_key, content_revision_key, rule_key, perception_kind,
    requested_decision_kind, decision_kind, decision_status, priority_value,
    server_tick, cooldown_until_tick, enqueue_action, raw_payload, idempotency_key
  ) VALUES (
    o_decision_id, p_world_instance_uuid, COALESCE(p_session_uuid, ''), COALESCE(p_character_uuid, ''),
    COALESCE(p_character_key, ''), p_npc_entity_key, p_target_key,
    COALESCE(p_content_revision_key, ''), COALESCE(p_rule_key, ''), p_perception_kind,
    v_requested_decision_kind, v_final_decision_kind, v_final_status,
    COALESCE(p_priority_value, 100), v_server_tick, v_new_cooldown_until,
    v_enqueue, v_payload, p_idempotency_key
  );

  IF v_new_cooldown_until > 0 THEN
    INSERT INTO npc_perception_cooldowns (
      world_instance_uuid, npc_entity_key, target_key, perception_kind,
      cooldown_until_tick, last_decision_id, raw_payload
    ) VALUES (
      p_world_instance_uuid, p_npc_entity_key, p_target_key, p_perception_kind,
      v_new_cooldown_until, o_decision_id,
      JSON_OBJECT('decision_status', v_final_status, 'decision_kind', v_final_decision_kind)
    )
    ON DUPLICATE KEY UPDATE
      cooldown_until_tick = VALUES(cooldown_until_tick),
      last_decision_id = VALUES(last_decision_id),
      raw_payload = VALUES(raw_payload),
      updated_at = CURRENT_TIMESTAMP(6);
  END IF;

  IF v_enqueue = 1 AND v_final_status = 'queued' THEN
    SET o_action_queue_id = UUID_TO_BIN(UUID(), 1);
    INSERT INTO npc_perception_action_queue (
      action_queue_id, decision_id, world_instance_uuid, session_uuid, character_uuid,
      action_kind, target_key, action_status, priority_value, request_payload, idempotency_key
    ) VALUES (
      o_action_queue_id, o_decision_id, p_world_instance_uuid,
      COALESCE(p_session_uuid, ''), COALESCE(p_character_uuid, ''),
      CONCAT('npc_', v_final_decision_kind), p_target_key, 'pending',
      COALESCE(p_priority_value, 100),
      JSON_MERGE_PATCH(
        v_payload,
        JSON_OBJECT(
          'decision_uuid', BIN_TO_UUID(o_decision_id, 1),
          'npc_entity_key', p_npc_entity_key,
          'perception_kind', p_perception_kind
        )
      ),
      CONCAT(p_idempotency_key, ':action')
    );

    UPDATE npc_perception_decisions
    SET action_queue_id = o_action_queue_id
    WHERE decision_id = o_decision_id;
  END IF;

  SET o_decision_status = v_final_status;
END$$
DELIMITER ;

INSERT INTO ai_runtime_schema_versions (migration_key, checksum, description)
VALUES (
  'server/sql/step212_ai_runtime_perception_database.sql',
  SHA2('server/sql/step212_ai_runtime_perception_database.sql', 256),
  'Step212 separate mmo_ai_runtime schema for authoritative NPC perception decisions'
)
ON DUPLICATE KEY UPDATE
  checksum = VALUES(checksum),
  description = VALUES(description),
  applied_at = CURRENT_TIMESTAMP(6);
