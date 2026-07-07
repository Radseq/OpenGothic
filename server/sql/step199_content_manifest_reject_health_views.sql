-- Step199: aggregate health views for content manifest bootstrap reject audits.

CREATE OR REPLACE VIEW v_mmo_content_manifest_reject_summary AS
SELECT
  COALESCE(reason, '<empty>') AS reason,
  COALESCE(content_revision_key, '<empty>') AS content_revision_key,
  COALESCE(server_manifest_hash, '<empty>') AS server_manifest_hash,
  COUNT(*) AS total_count,
  SUM(CASE WHEN created_at >= UTC_TIMESTAMP(6) - INTERVAL 1 HOUR THEN 1 ELSE 0 END) AS last_hour_count,
  SUM(CASE WHEN created_at >= UTC_TIMESTAMP(6) - INTERVAL 15 MINUTE THEN 1 ELSE 0 END) AS last_15m_count,
  MIN(created_at) AS first_seen_at,
  MAX(created_at) AS last_seen_at
FROM mmo_content_manifest_reject_audit
GROUP BY
  COALESCE(reason, '<empty>'),
  COALESCE(content_revision_key, '<empty>'),
  COALESCE(server_manifest_hash, '<empty>');

CREATE OR REPLACE VIEW v_mmo_content_manifest_reject_health AS
SELECT
  COUNT(*) AS total_rejects,
  SUM(CASE WHEN created_at >= UTC_TIMESTAMP(6) - INTERVAL 1 HOUR THEN 1 ELSE 0 END) AS last_hour_rejects,
  SUM(CASE WHEN created_at >= UTC_TIMESTAMP(6) - INTERVAL 15 MINUTE THEN 1 ELSE 0 END) AS last_15m_rejects,
  SUM(CASE WHEN reason = 'content_hash_mismatch' THEN 1 ELSE 0 END) AS total_hash_mismatches,
  SUM(CASE WHEN reason = 'content_hash_mismatch' AND created_at >= UTC_TIMESTAMP(6) - INTERVAL 1 HOUR THEN 1 ELSE 0 END) AS last_hour_hash_mismatches,
  SUM(CASE WHEN reason = 'client_manifest_missing' THEN 1 ELSE 0 END) AS total_missing_client_manifests,
  SUM(CASE WHEN reason = 'server_manifest_missing' THEN 1 ELSE 0 END) AS total_missing_server_manifests,
  COUNT(DISTINCT remote_endpoint) AS distinct_remote_endpoints,
  COUNT(DISTINCT client_manifest_hash) AS distinct_client_hashes,
  MAX(created_at) AS last_reject_at
FROM mmo_content_manifest_reject_audit;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES (
  'server/sql/step199_content_manifest_reject_health_views.sql',
  'server/sql',
  'Step199: aggregate health views for content manifest reject audits'
)
ON DUPLICATE KEY UPDATE
  applied_at = CURRENT_TIMESTAMP(6),
  notes = VALUES(notes);
