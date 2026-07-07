-- Step189: session-scoped content pack validation gate.
-- Step188 validates by realm key. The UDP server usually knows the DB session
-- UUID after login/recovery, so this wrapper resolves the realm/content revision
-- through server_sessions and returns the same compact decision.

DELIMITER ;;

DROP PROCEDURE IF EXISTS mmo_validate_client_content_pack_for_session;;
CREATE PROCEDURE mmo_validate_client_content_pack_for_session(
  IN p_session_id BINARY(16),
  IN p_client_manifest_hash CHAR(64),
  OUT o_accepted TINYINT(1),
  OUT o_reason VARCHAR(191),
  OUT o_server_manifest_hash CHAR(64),
  OUT o_content_revision_key VARCHAR(191)
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_content_revision_id BINARY(16) DEFAULT NULL;
  DECLARE CONTINUE HANDLER FOR NOT FOUND BEGIN END;

  SET o_accepted = 0;
  SET o_reason = 'unknown';
  SET o_server_manifest_hash = NULL;
  SET o_content_revision_key = NULL;

  SELECT ss.realm_id, rr.active_content_revision_id, cr.content_revision_key
    INTO v_realm_id, v_content_revision_id, o_content_revision_key
    FROM server_sessions ss
    JOIN realm_realms rr ON rr.realm_id = ss.realm_id
    JOIN content_revisions cr ON cr.content_revision_id = rr.active_content_revision_id
   WHERE ss.session_id = p_session_id
     AND ss.lifecycle_state = 'active'
   LIMIT 1;
  IF v_realm_id IS NULL THEN
    SET o_reason = 'active_session_not_found';
    LEAVE proc;
  END IF;

  SELECT manifest_hash
    INTO o_server_manifest_hash
    FROM mmo_server_content_pack_manifests
   WHERE content_revision_id = v_content_revision_id
   LIMIT 1;
  IF o_server_manifest_hash IS NULL THEN
    SET o_reason = 'server_manifest_missing';
    LEAVE proc;
  END IF;

  IF COALESCE(p_client_manifest_hash, '') = '' THEN
    SET o_reason = 'client_manifest_missing';
    LEAVE proc;
  END IF;

  IF LOWER(p_client_manifest_hash) <> LOWER(o_server_manifest_hash) THEN
    SET o_reason = 'content_hash_mismatch';
    LEAVE proc;
  END IF;

  SET o_accepted = 1;
  SET o_reason = 'ok';
END;;

DELIMITER ;
