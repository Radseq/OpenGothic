-- Gothic MMO MySQL production migration 004.
-- Server-owned wallet/gold write path.
-- Requires 001_gothic_mmo_production_schema.sql, 002_bootstrap_import_pipeline.sql
-- and 003_server_write_path.sql.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

-- -----------------------------------------------------------------------------
-- Wallet audit. character_wallets is the current-state projection; the durable
-- source is still world_event_journal + this audit/projection pair.
-- -----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS character_wallet_audit (
  wallet_audit_id     BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  session_id          BINARY(16) NULL,
  character_id        BINARY(16) NOT NULL,
  world_instance_id   BINARY(16) NOT NULL,
  event_id            BINARY(16) NOT NULL,
  idempotency_key     VARCHAR(191) NOT NULL,
  currency_key        VARCHAR(128) NOT NULL,
  delta_amount        DECIMAL(20,0) NOT NULL,
  amount_before       DECIMAL(20,0) NOT NULL,
  amount_after        DECIMAL(20,0) NOT NULL,
  reason              VARCHAR(128) NOT NULL,
  server_tick         BIGINT NOT NULL DEFAULT 0,
  raw_delta           JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at          TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY character_wallet_idempotency_uk(world_instance_id, idempotency_key),
  KEY ix_character_wallet_audit_character_currency(character_id, currency_key, created_at),
  KEY ix_character_wallet_audit_event(event_id),
  CONSTRAINT character_wallet_audit_session_fk FOREIGN KEY(session_id) REFERENCES server_sessions(session_id) ON DELETE SET NULL,
  CONSTRAINT character_wallet_audit_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT character_wallet_audit_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE RESTRICT,
  CONSTRAINT character_wallet_audit_event_fk FOREIGN KEY(event_id) REFERENCES world_event_journal(event_id) ON DELETE RESTRICT,
  CONSTRAINT character_wallet_audit_amounts_ck CHECK(amount_before >= 0 AND amount_after >= 0),
  CONSTRAINT character_wallet_audit_tick_ck CHECK(server_tick >= 0),
  CONSTRAINT character_wallet_audit_raw_json_ck CHECK(JSON_VALID(raw_delta))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_adjust_character_wallet;
DELIMITER $$
CREATE PROCEDURE mmo_adjust_character_wallet(
  IN  p_session_id       BINARY(16),
  IN  p_currency_key     VARCHAR(128),
  IN  p_delta_amount     DECIMAL(20,0),
  IN  p_reason           VARCHAR(128),
  IN  p_server_tick      BIGINT,
  IN  p_metadata         JSON,
  IN  p_idempotency_key  VARCHAR(191),
  OUT p_event_id         BINARY(16),
  OUT p_amount_after     DECIMAL(20,0)
)
wallet_proc: BEGIN
  DECLARE v_account_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_session_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_currency_key VARCHAR(128) DEFAULT NULL;
  DECLARE v_reason VARCHAR(128) DEFAULT NULL;
  DECLARE v_existing_event_id BINARY(16) DEFAULT NULL;
  DECLARE v_existing_type VARCHAR(128) DEFAULT NULL;
  DECLARE v_existing_class VARCHAR(32) DEFAULT NULL;
  DECLARE v_amount_before DECIMAL(20,0) DEFAULT 0;
  DECLARE v_amount_after DECIMAL(20,0) DEFAULT 0;
  DECLARE v_payload JSON;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    RESIGNAL;
  END;

  SET p_event_id = NULL;
  SET p_amount_after = NULL;

  IF p_session_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'session id is required';
  END IF;

  IF p_idempotency_key IS NULL OR TRIM(p_idempotency_key) = '' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'wallet idempotency key is required';
  END IF;

  IF p_delta_amount IS NULL OR p_delta_amount = 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'wallet delta must be non-zero';
  END IF;

  SET v_currency_key = COALESCE(NULLIF(TRIM(p_currency_key), ''), 'g2notr:gold');
  SET v_reason = COALESCE(NULLIF(TRIM(p_reason), ''), 'wallet_adjust');

  START TRANSACTION;

  SELECT ss.account_id, ss.character_id, ss.realm_id, ss.world_instance_id, ss.session_key, c.character_key
    INTO v_account_id, v_character_id, v_realm_id, v_world_instance_id, v_session_key, v_character_key
    FROM server_sessions ss
    JOIN characters c ON c.character_id = ss.character_id
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1
   FOR UPDATE;

  IF v_character_id IS NULL THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'active session not found';
  END IF;

  SELECT event_id, event_type, event_class
    INTO v_existing_event_id, v_existing_type, v_existing_class
    FROM world_event_journal
   WHERE world_instance_id = v_world_instance_id
     AND idempotency_key = p_idempotency_key
   LIMIT 1
   FOR UPDATE;

  IF v_existing_event_id IS NOT NULL THEN
    IF v_existing_type <> 'character_wallet_delta' OR v_existing_class <> 'inventory' THEN
      SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT = 'wallet idempotency key reused with different event type/class';
    END IF;

    SELECT amount_after
      INTO v_amount_after
      FROM character_wallet_audit
     WHERE world_instance_id = v_world_instance_id
       AND idempotency_key = p_idempotency_key
     LIMIT 1;

    IF v_amount_after IS NULL THEN
      SELECT amount
        INTO v_amount_after
        FROM character_wallets
       WHERE character_id = v_character_id
         AND currency_key = v_currency_key
       LIMIT 1;
    END IF;

    SET p_event_id = v_existing_event_id;
    SET p_amount_after = COALESCE(v_amount_after, 0);

    UPDATE server_sessions
       SET last_seen_at = CURRENT_TIMESTAMP(6)
     WHERE session_id = p_session_id;

    COMMIT;
    LEAVE wallet_proc;
  END IF;

  INSERT INTO character_wallets(character_id, currency_key, amount)
  VALUES(v_character_id, v_currency_key, 0)
  ON DUPLICATE KEY UPDATE amount = amount;

  SELECT amount
    INTO v_amount_before
    FROM character_wallets
   WHERE character_id = v_character_id
     AND currency_key = v_currency_key
   LIMIT 1
   FOR UPDATE;

  SET v_amount_before = COALESCE(v_amount_before, 0);
  SET v_amount_after = v_amount_before + p_delta_amount;

  IF v_amount_after < 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'insufficient wallet balance';
  END IF;

  SET v_payload = JSON_OBJECT(
    'session_key', v_session_key,
    'character_key', v_character_key,
    'currency_key', v_currency_key,
    'delta_amount', CAST(p_delta_amount AS SIGNED),
    'amount_before', CAST(v_amount_before AS SIGNED),
    'amount_after', CAST(v_amount_after AS SIGNED),
    'reason', v_reason,
    'metadata', COALESCE(p_metadata, JSON_OBJECT())
  );

  CALL mmo_append_world_event(
    v_realm_id,
    v_world_instance_id,
    v_character_id,
    'character_wallet_delta',
    'inventory',
    GREATEST(0, COALESCE(p_server_tick, 0)),
    v_character_key,
    v_currency_key,
    v_payload,
    p_idempotency_key,
    'server',
    NULL,
    NULL,
    p_event_id
  );

  UPDATE character_wallets
     SET amount = v_amount_after
   WHERE character_id = v_character_id
     AND currency_key = v_currency_key;

  INSERT INTO character_wallet_audit(
    session_id, character_id, world_instance_id, event_id, idempotency_key,
    currency_key, delta_amount, amount_before, amount_after, reason,
    server_tick, raw_delta
  ) VALUES (
    p_session_id, v_character_id, v_world_instance_id, p_event_id, p_idempotency_key,
    v_currency_key, p_delta_amount, v_amount_before, v_amount_after, v_reason,
    GREATEST(0, COALESCE(p_server_tick, 0)), v_payload
  );

  UPDATE realm_world_instances
     SET current_tick = GREATEST(current_tick, GREATEST(0, COALESCE(p_server_tick, 0)))
   WHERE world_instance_id = v_world_instance_id;

  UPDATE server_sessions
     SET last_seen_at = CURRENT_TIMESTAMP(6)
   WHERE session_id = p_session_id;

  SET p_amount_after = v_amount_after;

  COMMIT;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_grant_character_gold;
DELIMITER $$
CREATE PROCEDURE mmo_grant_character_gold(
  IN  p_session_id       BINARY(16),
  IN  p_amount           DECIMAL(20,0),
  IN  p_reason           VARCHAR(128),
  IN  p_server_tick      BIGINT,
  IN  p_metadata         JSON,
  IN  p_idempotency_key  VARCHAR(191),
  OUT p_event_id         BINARY(16),
  OUT p_amount_after     DECIMAL(20,0)
)
BEGIN
  IF p_amount IS NULL OR p_amount <= 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'gold grant amount must be positive';
  END IF;

  CALL mmo_adjust_character_wallet(
    p_session_id,
    'g2notr:gold',
    p_amount,
    COALESCE(NULLIF(TRIM(p_reason), ''), 'gold_grant'),
    p_server_tick,
    p_metadata,
    p_idempotency_key,
    p_event_id,
    p_amount_after
  );
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_spend_character_gold;
DELIMITER $$
CREATE PROCEDURE mmo_spend_character_gold(
  IN  p_session_id       BINARY(16),
  IN  p_amount           DECIMAL(20,0),
  IN  p_reason           VARCHAR(128),
  IN  p_server_tick      BIGINT,
  IN  p_metadata         JSON,
  IN  p_idempotency_key  VARCHAR(191),
  OUT p_event_id         BINARY(16),
  OUT p_amount_after     DECIMAL(20,0)
)
BEGIN
  IF p_amount IS NULL OR p_amount <= 0 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'gold spend amount must be positive';
  END IF;

  CALL mmo_adjust_character_wallet(
    p_session_id,
    'g2notr:gold',
    -p_amount,
    COALESCE(NULLIF(TRIM(p_reason), ''), 'gold_spend'),
    p_server_tick,
    p_metadata,
    p_idempotency_key,
    p_event_id,
    p_amount_after
  );
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_character_wallets AS
SELECT
  c.character_key,
  c.character_name,
  r.realm_key,
  cw.currency_key,
  cw.amount,
  cw.updated_at
FROM character_wallets cw
JOIN characters c ON c.character_id = cw.character_id
JOIN realm_realms r ON r.realm_id = c.realm_id;

CREATE OR REPLACE VIEW v_character_wallet_audit AS
SELECT
  BIN_TO_UUID(cwa.wallet_audit_id, 1) AS wallet_audit_id,
  BIN_TO_UUID(cwa.event_id, 1) AS event_id,
  ss.session_key,
  c.character_key,
  c.character_name,
  r.realm_key,
  wi.world_instance_key,
  cwa.currency_key,
  cwa.delta_amount,
  cwa.amount_before,
  cwa.amount_after,
  cwa.reason,
  cwa.server_tick,
  cwa.created_at
FROM character_wallet_audit cwa
LEFT JOIN server_sessions ss ON ss.session_id = cwa.session_id
JOIN characters c ON c.character_id = cwa.character_id
JOIN realm_realms r ON r.realm_id = c.realm_id
JOIN realm_world_instances wi ON wi.world_instance_id = cwa.world_instance_id;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES(
  'production/mysql/004_wallet_write_path',
  'gothic-mmo-wallet-write-path-v1-mysql',
  'Adds server-owned wallet/gold semantic event write path, audit table, idempotent procedures and wallet views.'
)
ON DUPLICATE KEY UPDATE
  schema_contract = VALUES(schema_contract),
  notes = VALUES(notes),
  applied_at = CURRENT_TIMESTAMP(6);
