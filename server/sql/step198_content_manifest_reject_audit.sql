-- Step198: server-side DB audit for client content manifest bootstrap rejects.
-- Additive bridge table/procedure before the future persistence rewrite.

CREATE TABLE IF NOT EXISTS mmo_content_manifest_reject_audit (
  reject_id BINARY(16) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  session_id BINARY(16) NULL,
  realm_id BINARY(16) NULL,
  account_id BINARY(16) NULL,
  character_id BINARY(16) NULL,
  world_instance_id BINARY(16) NULL,
  content_revision_id BINARY(16) NULL,
  remote_endpoint VARCHAR(191) NOT NULL DEFAULT '',
  packet_session_key VARCHAR(191) NOT NULL DEFAULT '',
  target_key VARCHAR(191) NOT NULL DEFAULT '',
  packet_sequence BIGINT UNSIGNED NOT NULL DEFAULT 0,
  local_sequence BIGINT UNSIGNED NOT NULL DEFAULT 0,
  phase VARCHAR(64) NOT NULL DEFAULT '',
  reason VARCHAR(191) NOT NULL,
  client_manifest_hash VARCHAR(128) NULL,
  server_manifest_hash CHAR(64) NULL,
  content_revision_key VARCHAR(191) NULL,
  message VARCHAR(1024) NOT NULL DEFAULT '',
  payload_json JSON NOT NULL DEFAULT (JSON_OBJECT()),
  PRIMARY KEY (reject_id),
  KEY ix_mmo_cm_reject_created (created_at),
  KEY ix_mmo_cm_reject_reason_created (reason, created_at),
  KEY ix_mmo_cm_reject_session_created (session_id, created_at),
  KEY ix_mmo_cm_reject_realm_reason (realm_id, reason, created_at),
  KEY ix_mmo_cm_reject_revision_reason (content_revision_id, reason, created_at),
  KEY ix_mmo_cm_reject_client_hash (client_manifest_hash),
  KEY ix_mmo_cm_reject_server_hash (server_manifest_hash),
  CONSTRAINT mmo_cm_reject_session_fk FOREIGN KEY (session_id) REFERENCES server_sessions(session_id) ON DELETE SET NULL,
  CONSTRAINT mmo_cm_reject_realm_fk FOREIGN KEY (realm_id) REFERENCES realm_realms(realm_id) ON DELETE SET NULL,
  CONSTRAINT mmo_cm_reject_account_fk FOREIGN KEY (account_id) REFERENCES account_accounts(account_id) ON DELETE SET NULL,
  CONSTRAINT mmo_cm_reject_character_fk FOREIGN KEY (character_id) REFERENCES characters(character_id) ON DELETE SET NULL,
  CONSTRAINT mmo_cm_reject_world_fk FOREIGN KEY (world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE SET NULL,
  CONSTRAINT mmo_cm_reject_revision_fk FOREIGN KEY (content_revision_id) REFERENCES content_revisions(content_revision_id) ON DELETE SET NULL,
  CONSTRAINT mmo_cm_reject_payload_json_ck CHECK (JSON_VALID(payload_json)),
  CONSTRAINT mmo_cm_reject_server_hash_ck CHECK (server_manifest_hash IS NULL OR REGEXP_LIKE(server_manifest_hash, '^[0-9a-f]{64}$'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_record_content_manifest_reject;
DELIMITER ;;
CREATE PROCEDURE mmo_record_content_manifest_reject(
  IN p_session_id BINARY(16),
  IN p_remote_endpoint VARCHAR(191),
  IN p_packet_session_key VARCHAR(191),
  IN p_target_key VARCHAR(191),
  IN p_packet_sequence BIGINT UNSIGNED,
  IN p_local_sequence BIGINT UNSIGNED,
  IN p_phase VARCHAR(64),
  IN p_reason VARCHAR(191),
  IN p_client_manifest_hash VARCHAR(128),
  IN p_server_manifest_hash CHAR(64),
  IN p_content_revision_key VARCHAR(191),
  IN p_message TEXT,
  IN p_payload_json JSON,
  OUT o_reject_id BINARY(16)
)
proc: BEGIN
  DECLARE v_reject_id BINARY(16) DEFAULT UUID_TO_BIN(UUID(), 1);
  DECLARE v_session_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_account_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_instance_id BINARY(16) DEFAULT NULL;
  DECLARE v_content_revision_id BINARY(16) DEFAULT NULL;
  DECLARE CONTINUE HANDLER FOR NOT FOUND BEGIN END;

  SET o_reject_id = v_reject_id;

  IF p_session_id IS NOT NULL THEN
    SELECT ss.session_id,
           ss.realm_id,
           ss.account_id,
           ss.character_id,
           ss.world_instance_id,
           rr.active_content_revision_id
      INTO v_session_id,
           v_realm_id,
           v_account_id,
           v_character_id,
           v_world_instance_id,
           v_content_revision_id
      FROM server_sessions ss
      LEFT JOIN realm_realms rr ON rr.realm_id = ss.realm_id
     WHERE ss.session_id = p_session_id
     LIMIT 1;
  END IF;

  IF v_content_revision_id IS NULL AND COALESCE(p_content_revision_key, '') <> '' THEN
    SELECT content_revision_id
      INTO v_content_revision_id
      FROM content_revisions
     WHERE content_revision_key = p_content_revision_key
     LIMIT 1;
  END IF;

  INSERT INTO mmo_content_manifest_reject_audit(
    reject_id,
    session_id,
    realm_id,
    account_id,
    character_id,
    world_instance_id,
    content_revision_id,
    remote_endpoint,
    packet_session_key,
    target_key,
    packet_sequence,
    local_sequence,
    phase,
    reason,
    client_manifest_hash,
    server_manifest_hash,
    content_revision_key,
    message,
    payload_json
  ) VALUES (
    v_reject_id,
    v_session_id,
    v_realm_id,
    v_account_id,
    v_character_id,
    v_world_instance_id,
    v_content_revision_id,
    COALESCE(p_remote_endpoint, ''),
    COALESCE(p_packet_session_key, ''),
    COALESCE(p_target_key, ''),
    COALESCE(p_packet_sequence, 0),
    COALESCE(p_local_sequence, 0),
    COALESCE(p_phase, ''),
    COALESCE(NULLIF(p_reason, ''), 'content_manifest_rejected'),
    NULLIF(LOWER(COALESCE(p_client_manifest_hash, '')), ''),
    NULLIF(LOWER(COALESCE(p_server_manifest_hash, '')), ''),
    NULLIF(p_content_revision_key, ''),
    LEFT(COALESCE(p_message, ''), 1024),
    COALESCE(p_payload_json, JSON_OBJECT())
  );
END ;;
DELIMITER ;

CREATE OR REPLACE VIEW v_mmo_content_manifest_reject_audit AS
SELECT
  BIN_TO_UUID(a.reject_id, 1) AS reject_uuid,
  a.created_at,
  BIN_TO_UUID(a.session_id, 1) AS session_uuid,
  BIN_TO_UUID(a.realm_id, 1) AS realm_uuid,
  rr.realm_key,
  BIN_TO_UUID(a.account_id, 1) AS account_uuid,
  aa.account_name,
  BIN_TO_UUID(a.character_id, 1) AS character_uuid,
  c.character_key,
  BIN_TO_UUID(a.world_instance_id, 1) AS world_instance_uuid,
  rwi.world_instance_key,
  BIN_TO_UUID(a.content_revision_id, 1) AS content_revision_uuid,
  COALESCE(cr.content_revision_key, a.content_revision_key) AS content_revision_key,
  a.remote_endpoint,
  a.packet_session_key,
  a.target_key,
  a.packet_sequence,
  a.local_sequence,
  a.phase,
  a.reason,
  a.client_manifest_hash,
  a.server_manifest_hash,
  a.message,
  a.payload_json
FROM mmo_content_manifest_reject_audit a
LEFT JOIN realm_realms rr ON rr.realm_id = a.realm_id
LEFT JOIN account_accounts aa ON aa.account_id = a.account_id
LEFT JOIN characters c ON c.character_id = a.character_id
LEFT JOIN realm_world_instances rwi ON rwi.world_instance_id = a.world_instance_id
LEFT JOIN content_revisions cr ON cr.content_revision_id = a.content_revision_id;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES (
  'server/sql/step198_content_manifest_reject_audit.sql',
  'server/sql',
  'Step198: DB audit table/procedure/view for content manifest bootstrap rejects'
)
ON DUPLICATE KEY UPDATE
  applied_at = CURRENT_TIMESTAMP(6),
  notes = VALUES(notes);
