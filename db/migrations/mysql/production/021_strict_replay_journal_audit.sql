-- Gothic MMO MySQL production migration 021.
-- Strict replay journal audit: stronger deterministic replay pre-flight checks.
-- Requires 001..020 MySQL production migrations.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

DROP PROCEDURE IF EXISTS mmo_audit_strict_replay_journal;
DELIMITER $$
CREATE PROCEDURE mmo_audit_strict_replay_journal(
  IN  p_world_instance_id  BINARY(16),
  IN  p_run_key            VARCHAR(191),
  IN  p_metadata           JSON,
  OUT p_validation_run_id  BINARY(16),
  OUT p_error_count        INT,
  OUT p_warning_count      INT
)
BEGIN
  DECLARE v_run_id BINARY(16) DEFAULT NULL;
  DECLARE v_max_event_seq BIGINT DEFAULT 0;
  DECLARE v_total BIGINT DEFAULT 0;
  DECLARE v_bad BIGINT DEFAULT 0;
  DECLARE v_errors INT DEFAULT 0;
  DECLARE v_warnings INT DEFAULT 0;

  IF p_world_instance_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='world_instance_id is required'; END IF;
  IF p_run_key IS NULL OR TRIM(p_run_key)='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='run_key is required'; END IF;

  SELECT COALESCE(MAX(event_seq),0) INTO v_max_event_seq FROM world_event_journal WHERE world_instance_id=p_world_instance_id;

  INSERT INTO mmo_projection_validation_runs(world_instance_id, run_key, run_kind, status, max_event_seq, metadata)
  VALUES(p_world_instance_id, p_run_key, 'replay', 'running', v_max_event_seq, JSON_MERGE_PATCH(COALESCE(p_metadata,JSON_OBJECT()), JSON_OBJECT('validator','strict-replay-journal-021')))
  ON DUPLICATE KEY UPDATE status='running', started_at=CURRENT_TIMESTAMP(6), finished_at=NULL, max_event_seq=VALUES(max_event_seq), error_count=0, warning_count=0, metadata=VALUES(metadata);

  SELECT validation_run_id INTO v_run_id FROM mmo_projection_validation_runs WHERE world_instance_id=p_world_instance_id AND run_key=p_run_key LIMIT 1;
  DELETE FROM mmo_projection_validation_results WHERE validation_run_id=v_run_id;

  SELECT COUNT(*) INTO v_total FROM world_event_journal WHERE world_instance_id=p_world_instance_id AND source IN ('server','test');

  SELECT COUNT(*) INTO v_bad
    FROM world_event_journal e
    LEFT JOIN mmo_event_projection_contracts c ON c.event_type=e.event_type
   WHERE e.world_instance_id=p_world_instance_id AND e.source IN ('server','test') AND c.event_type IS NULL;
  INSERT INTO mmo_projection_validation_results(validation_run_id,check_name,severity,checked_count,problem_count,details)
  VALUES(v_run_id,'strict_replay_all_server_events_have_contract',IF(v_bad=0,'ok','error'),v_total,v_bad,JSON_OBJECT('rule','deterministic replay needs a registered contract for every server/test event'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_bad
    FROM world_event_journal e
    JOIN mmo_event_projection_contracts c ON c.event_type=e.event_type
   WHERE e.world_instance_id=p_world_instance_id AND e.source IN ('server','test') AND e.event_class<>c.event_class;
  INSERT INTO mmo_projection_validation_results(validation_run_id,check_name,severity,checked_count,problem_count,details)
  VALUES(v_run_id,'strict_replay_event_class_matches_contract',IF(v_bad=0,'ok','error'),v_total,v_bad,JSON_OBJECT('rule','event_class must be stable because replay uses it for routing and diagnostics'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_bad
    FROM world_event_journal e
   WHERE e.world_instance_id=p_world_instance_id AND e.source IN ('server','test')
     AND (e.idempotency_key IS NULL OR TRIM(e.idempotency_key)='');
  INSERT INTO mmo_projection_validation_results(validation_run_id,check_name,severity,checked_count,problem_count,details)
  VALUES(v_run_id,'strict_replay_server_events_are_idempotent',IF(v_bad=0,'ok','warning'),v_total,v_bad,JSON_OBJECT('rule','network retry requires idempotent server events'));
  SET v_warnings = v_warnings + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_bad
    FROM world_event_journal e
   WHERE e.world_instance_id=p_world_instance_id AND e.source IN ('server','test')
     AND JSON_TYPE(e.payload) <> 'OBJECT';
  INSERT INTO mmo_projection_validation_results(validation_run_id,check_name,severity,checked_count,problem_count,details)
  VALUES(v_run_id,'strict_replay_payloads_are_json_objects',IF(v_bad=0,'ok','error'),v_total,v_bad,JSON_OBJECT('rule','event payload must be a JSON object so replay can inspect semantic fields deterministically'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_bad
    FROM world_projection_offsets po
   WHERE po.world_instance_id=p_world_instance_id AND po.last_event_seq > v_max_event_seq;
  INSERT INTO mmo_projection_validation_results(validation_run_id,check_name,severity,checked_count,problem_count,details)
  VALUES(v_run_id,'strict_replay_projection_offsets_not_ahead',IF(v_bad=0,'ok','error'),0,v_bad,JSON_OBJECT('rule','projection offset cannot be ahead of world_event_journal'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_bad
    FROM mmo_server_action_outbox o
   WHERE o.world_instance_id=p_world_instance_id
     AND o.status='applied'
     AND o.event_id IS NOT NULL
     AND NOT EXISTS (SELECT 1 FROM world_event_journal e WHERE e.event_id=o.event_id);
  INSERT INTO mmo_projection_validation_results(validation_run_id,check_name,severity,checked_count,problem_count,details)
  VALUES(v_run_id,'strict_replay_applied_actions_reference_events',IF(v_bad=0,'ok','error'),0,v_bad,JSON_OBJECT('rule','applied outbox action must point to a durable journal event'));
  SET v_errors = v_errors + IF(v_bad=0,0,1);

  SELECT COUNT(*) INTO v_bad
    FROM mmo_server_action_outbox o
   WHERE o.world_instance_id=p_world_instance_id
     AND o.status IN ('failed','dead_letter');
  INSERT INTO mmo_projection_validation_results(validation_run_id,check_name,severity,checked_count,problem_count,details)
  VALUES(v_run_id,'strict_replay_no_failed_outbox_actions',IF(v_bad=0,'ok','warning'),0,v_bad,JSON_OBJECT('rule','failed/dead-letter actions are not replayed; investigate before production readiness'));
  SET v_warnings = v_warnings + IF(v_bad=0,0,1);

  UPDATE mmo_projection_validation_runs
     SET status=IF(v_errors=0,'passed','failed'), finished_at=CURRENT_TIMESTAMP(6), error_count=v_errors, warning_count=v_warnings
   WHERE validation_run_id=v_run_id;

  SET p_validation_run_id=v_run_id;
  SET p_error_count=v_errors;
  SET p_warning_count=v_warnings;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_strict_replay_latest_results AS
SELECT BIN_TO_UUID(run.validation_run_id,1) AS validation_run_uuid,
       BIN_TO_UUID(run.world_instance_id,1) AS world_instance_uuid,
       run.run_key,
       run.status AS run_status,
       res.check_name,
       res.severity,
       res.checked_count,
       res.problem_count,
       res.details,
       res.created_at
FROM mmo_projection_validation_runs run
JOIN mmo_projection_validation_results res ON res.validation_run_id=run.validation_run_id
WHERE run.run_kind='replay'
ORDER BY run.started_at DESC, res.severity DESC, res.check_name ASC;

CREATE OR REPLACE VIEW v_strict_replay_latest_errors AS
SELECT * FROM v_strict_replay_latest_results
WHERE severity IN ('error','warning') AND problem_count > 0;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES('production/mysql/021_strict_replay_journal_audit', 'gothic-mmo-strict-replay-journal-audit-v1-mysql', 'Strict replay pre-flight audit for contract coverage, event class stability, idempotency, JSON payload shape, projection offsets and outbox linkage.')
ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes), applied_at=CURRENT_TIMESTAMP(6);
