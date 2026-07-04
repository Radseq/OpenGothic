-- Gothic MMO MySQL production migration 024.
-- Canonical projection hash manifest for DB restore/replay/parity evidence.
-- Requires 001..023 MySQL production migrations.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE IF NOT EXISTS mmo_projection_hash_runs (
  projection_hash_run_id BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  world_instance_id      BINARY(16) NOT NULL,
  character_id           BINARY(16) NULL,
  run_key                VARCHAR(191) NOT NULL,
  status                 VARCHAR(32) NOT NULL DEFAULT 'running',
  component_count        INT NOT NULL DEFAULT 0,
  started_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  finished_at            TIMESTAMP(6) NULL,
  metadata               JSON NOT NULL DEFAULT (JSON_OBJECT()),
  UNIQUE KEY mmo_projection_hash_runs_key_uk(world_instance_id, run_key),
  KEY ix_mmo_projection_hash_runs_world_started(world_instance_id, started_at),
  CONSTRAINT mmo_projection_hash_runs_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE CASCADE,
  CONSTRAINT mmo_projection_hash_runs_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE SET NULL,
  CONSTRAINT mmo_projection_hash_runs_status_ck CHECK(status IN ('running','materialized','failed')),
  CONSTRAINT mmo_projection_hash_runs_metadata_json_ck CHECK(JSON_VALID(metadata))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_projection_component_hashes (
  projection_hash_run_id BINARY(16) NOT NULL,
  component_key          VARCHAR(128) NOT NULL,
  scope_key              VARCHAR(191) NOT NULL DEFAULT 'world',
  row_count              BIGINT NOT NULL DEFAULT 0,
  component_hash         CHAR(64) NOT NULL,
  details                JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at             TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(projection_hash_run_id, component_key, scope_key),
  KEY ix_mmo_projection_component_hashes_component(component_key, created_at),
  CONSTRAINT mmo_projection_component_hashes_run_fk FOREIGN KEY(projection_hash_run_id) REFERENCES mmo_projection_hash_runs(projection_hash_run_id) ON DELETE CASCADE,
  CONSTRAINT mmo_projection_component_hashes_hash_ck CHECK(CHAR_LENGTH(component_hash)=64),
  CONSTRAINT mmo_projection_component_hashes_count_ck CHECK(row_count >= 0),
  CONSTRAINT mmo_projection_component_hashes_details_json_ck CHECK(JSON_VALID(details))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_materialize_projection_hash_run;
DELIMITER $$
CREATE PROCEDURE mmo_materialize_projection_hash_run(
  IN  p_world_instance_id       BINARY(16),
  IN  p_character_key           VARCHAR(191),
  IN  p_run_key                 VARCHAR(191),
  IN  p_metadata                JSON,
  OUT p_projection_hash_run_id  BINARY(16),
  OUT p_component_count         INT
)
BEGIN
  DECLARE v_run_id BINARY(16) DEFAULT NULL;
  DECLARE v_character_id BINARY(16) DEFAULT NULL;
  DECLARE v_count BIGINT DEFAULT 0;
  DECLARE v_checksum DECIMAL(65,0) DEFAULT 0;
  DECLARE v_hash CHAR(64) DEFAULT NULL;

  IF p_world_instance_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='world_instance_id is required'; END IF;
  IF p_run_key IS NULL OR TRIM(p_run_key)='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='run_key is required'; END IF;

  IF p_character_key IS NOT NULL AND TRIM(p_character_key) <> '' THEN
    SELECT character_id INTO v_character_id FROM characters WHERE character_key=p_character_key LIMIT 1;
  END IF;

  INSERT INTO mmo_projection_hash_runs(world_instance_id, character_id, run_key, status, metadata)
  VALUES(p_world_instance_id, v_character_id, p_run_key, 'running', COALESCE(p_metadata, JSON_OBJECT()))
  ON DUPLICATE KEY UPDATE character_id=VALUES(character_id), status='running', started_at=CURRENT_TIMESTAMP(6), finished_at=NULL, component_count=0, metadata=VALUES(metadata);

  SELECT projection_hash_run_id INTO v_run_id FROM mmo_projection_hash_runs WHERE world_instance_id=p_world_instance_id AND run_key=p_run_key LIMIT 1;
  DELETE FROM mmo_projection_component_hashes WHERE projection_hash_run_id=v_run_id;

  SELECT COUNT(*), COALESCE(SUM(CRC32(CONCAT_WS('|', BIN_TO_UUID(character_id,1), BIN_TO_UUID(world_instance_id,1), pos_x, pos_y, pos_z, rotation_yaw, COALESCE(current_waypoint_key,''), server_tick, row_version))),0)
    INTO v_count, v_checksum
    FROM character_positions
   WHERE (v_character_id IS NULL OR character_id=v_character_id) AND world_instance_id=p_world_instance_id;
  SET v_hash = SHA2(CONCAT('character_positions|', v_count, '|', v_checksum), 256);
  INSERT INTO mmo_projection_component_hashes VALUES(v_run_id,'character_positions',COALESCE(p_character_key,'world'),v_count,v_hash,JSON_OBJECT('algorithm','count+sum_crc32'),CURRENT_TIMESTAMP(6));

  SELECT COUNT(*), COALESCE(SUM(CRC32(CONCAT_WS('|', BIN_TO_UUID(character_id,1), level, experience, COALESCE(experience_next,''), learning_points, health_current, health_max, mana_current, mana_max, strength, dexterity, COALESCE(guild,''), COALESCE(true_guild,''), COALESCE(permanent_attitude,''), COALESCE(temporary_attitude,''), row_version))),0)
    INTO v_count, v_checksum
    FROM character_stats
   WHERE v_character_id IS NULL OR character_id=v_character_id;
  SET v_hash = SHA2(CONCAT('character_stats|', v_count, '|', v_checksum), 256);
  INSERT INTO mmo_projection_component_hashes VALUES(v_run_id,'character_stats',COALESCE(p_character_key,'character'),v_count,v_hash,JSON_OBJECT('algorithm','count+sum_crc32'),CURRENT_TIMESTAMP(6));

  SELECT COUNT(*), COALESCE(SUM(CRC32(CONCAT_WS('|', BIN_TO_UUID(character_id,1), currency_key, amount))),0)
    INTO v_count, v_checksum
    FROM character_wallets
   WHERE v_character_id IS NULL OR character_id=v_character_id;
  SET v_hash = SHA2(CONCAT('character_wallets|', v_count, '|', v_checksum), 256);
  INSERT INTO mmo_projection_component_hashes VALUES(v_run_id,'character_wallets',COALESCE(p_character_key,'character'),v_count,v_hash,JSON_OBJECT('algorithm','count+sum_crc32'),CURRENT_TIMESTAMP(6));

  SELECT COUNT(*), COALESCE(SUM(CRC32(CONCAT_WS('|', BIN_TO_UUID(ci.character_id,1), BIN_TO_UUID(ci.item_instance_id,1), COALESCE(ci.bag_index,''), ci.amount, COALESCE(ci.source_amount,''), COALESCE(ci.source_iterator_count,''), ii.owner_type, BIN_TO_UUID(ii.owner_id,1), ii.quantity, ii.lifecycle_state))),0)
    INTO v_count, v_checksum
    FROM character_inventory ci
    JOIN item_instances ii ON ii.item_instance_id=ci.item_instance_id
   WHERE v_character_id IS NULL OR ci.character_id=v_character_id;
  SET v_hash = SHA2(CONCAT('character_inventory|', v_count, '|', v_checksum), 256);
  INSERT INTO mmo_projection_component_hashes VALUES(v_run_id,'character_inventory',COALESCE(p_character_key,'character'),v_count,v_hash,JSON_OBJECT('algorithm','count+sum_crc32','joined','item_instances'),CURRENT_TIMESTAMP(6));

  SELECT COUNT(*), COALESCE(SUM(CRC32(CONCAT_WS('|', BIN_TO_UUID(character_id,1), equipment_slot, BIN_TO_UUID(item_instance_id,1)))),0)
    INTO v_count, v_checksum
    FROM character_equipment
   WHERE v_character_id IS NULL OR character_id=v_character_id;
  SET v_hash = SHA2(CONCAT('character_equipment|', v_count, '|', v_checksum), 256);
  INSERT INTO mmo_projection_component_hashes VALUES(v_run_id,'character_equipment',COALESCE(p_character_key,'character'),v_count,v_hash,JSON_OBJECT('algorithm','count+sum_crc32'),CURRENT_TIMESTAMP(6));

  SELECT COUNT(*), COALESCE(SUM(CRC32(CONCAT_WS('|', BIN_TO_UUID(character_id,1), quest_key, section, status, entry_order, JSON_EXTRACT(text_entries,'$')))),0)
    INTO v_count, v_checksum
    FROM character_quests
   WHERE v_character_id IS NULL OR character_id=v_character_id;
  SET v_hash = SHA2(CONCAT('character_quests|', v_count, '|', v_checksum), 256);
  INSERT INTO mmo_projection_component_hashes VALUES(v_run_id,'character_quests',COALESCE(p_character_key,'character'),v_count,v_hash,JSON_OBJECT('algorithm','count+sum_crc32'),CURRENT_TIMESTAMP(6));

  SELECT COUNT(*), COALESCE(SUM(CRC32(CONCAT_WS('|', BIN_TO_UUID(character_id,1), npc_key, info_key, known, permanent, availability_state))),0)
    INTO v_count, v_checksum
    FROM character_known_dialogs
   WHERE v_character_id IS NULL OR character_id=v_character_id;
  SET v_hash = SHA2(CONCAT('character_known_dialogs|', v_count, '|', v_checksum), 256);
  INSERT INTO mmo_projection_component_hashes VALUES(v_run_id,'character_known_dialogs',COALESCE(p_character_key,'character'),v_count,v_hash,JSON_OBJECT('algorithm','count+sum_crc32'),CURRENT_TIMESTAMP(6));

  SELECT COUNT(*), COALESCE(SUM(CRC32(CONCAT_WS('|', BIN_TO_UUID(character_id,1), script_key, COALESCE(symbol_index,''), value_type, value_index, COALESCE(value_int,''), COALESCE(value_real,''), COALESCE(value_text,'')))),0)
    INTO v_count, v_checksum
    FROM character_script_state
   WHERE v_character_id IS NULL OR character_id=v_character_id;
  SET v_hash = SHA2(CONCAT('character_script_state|', v_count, '|', v_checksum), 256);
  INSERT INTO mmo_projection_component_hashes VALUES(v_run_id,'character_script_state',COALESCE(p_character_key,'character'),v_count,v_hash,JSON_OBJECT('algorithm','count+sum_crc32'),CURRENT_TIMESTAMP(6));

  SELECT COUNT(*), COALESCE(SUM(CRC32(CONCAT_WS('|', entity_key, entity_kind, lifecycle_state, COALESCE(pos_x,''), COALESCE(pos_y,''), COALESCE(pos_z,''), COALESCE(rotation_yaw,''), COALESCE(health_current,''), COALESCE(health_max,''), row_version, JSON_EXTRACT(state_json,'$')))),0)
    INTO v_count, v_checksum
    FROM world_entity_state
   WHERE world_instance_id=p_world_instance_id;
  SET v_hash = SHA2(CONCAT('world_entity_state|', v_count, '|', v_checksum), 256);
  INSERT INTO mmo_projection_component_hashes VALUES(v_run_id,'world_entity_state','world',v_count,v_hash,JSON_OBJECT('algorithm','count+sum_crc32'),CURRENT_TIMESTAMP(6));

  SELECT COUNT(*), COALESCE(SUM(CRC32(CONCAT_WS('|', owner_entity_key, BIN_TO_UUID(item_instance_id,1), amount, COALESCE(source_amount,''), COALESCE(source_iterator_count,'')))),0)
    INTO v_count, v_checksum
    FROM world_inventory
   WHERE world_instance_id=p_world_instance_id;
  SET v_hash = SHA2(CONCAT('world_inventory|', v_count, '|', v_checksum), 256);
  INSERT INTO mmo_projection_component_hashes VALUES(v_run_id,'world_inventory','world',v_count,v_hash,JSON_OBJECT('algorithm','count+sum_crc32'),CURRENT_TIMESTAMP(6));

  SELECT COUNT(*), COALESCE(SUM(CRC32(CONCAT_WS('|', scope_key, script_key, COALESCE(symbol_index,''), value_type, value_index, COALESCE(value_int,''), COALESCE(value_real,''), COALESCE(value_text,'')))),0)
    INTO v_count, v_checksum
    FROM world_script_state
   WHERE world_instance_id=p_world_instance_id;
  SET v_hash = SHA2(CONCAT('world_script_state|', v_count, '|', v_checksum), 256);
  INSERT INTO mmo_projection_component_hashes VALUES(v_run_id,'world_script_state','world',v_count,v_hash,JSON_OBJECT('algorithm','count+sum_crc32'),CURRENT_TIMESTAMP(6));

  SELECT COUNT(*), COALESCE(SUM(CRC32(CONCAT_WS('|', event_seq, event_type, event_class, COALESCE(idempotency_key,''), COALESCE(entity_key,''), COALESCE(subject_key,''), server_tick, source, schema_version, JSON_EXTRACT(payload,'$')))),0)
    INTO v_count, v_checksum
    FROM world_event_journal
   WHERE world_instance_id=p_world_instance_id;
  SET v_hash = SHA2(CONCAT('world_event_journal|', v_count, '|', v_checksum), 256);
  INSERT INTO mmo_projection_component_hashes VALUES(v_run_id,'world_event_journal','world',v_count,v_hash,JSON_OBJECT('algorithm','count+sum_crc32'),CURRENT_TIMESTAMP(6));

  SELECT COUNT(*) INTO p_component_count FROM mmo_projection_component_hashes WHERE projection_hash_run_id=v_run_id;
  UPDATE mmo_projection_hash_runs SET status='materialized', component_count=p_component_count, finished_at=CURRENT_TIMESTAMP(6) WHERE projection_hash_run_id=v_run_id;
  SET p_projection_hash_run_id=v_run_id;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_projection_hash_latest AS
SELECT BIN_TO_UUID(r.projection_hash_run_id,1) AS projection_hash_run_uuid,
       BIN_TO_UUID(r.world_instance_id,1) AS world_instance_uuid,
       BIN_TO_UUID(r.character_id,1) AS character_uuid,
       r.run_key,
       r.status,
       r.component_count,
       r.started_at,
       r.finished_at
FROM mmo_projection_hash_runs r
WHERE r.started_at=(SELECT MAX(r2.started_at) FROM mmo_projection_hash_runs r2 WHERE r2.world_instance_id=r.world_instance_id);

CREATE OR REPLACE VIEW v_projection_hash_latest_components AS
SELECT BIN_TO_UUID(r.projection_hash_run_id,1) AS projection_hash_run_uuid,
       r.run_key,
       h.component_key,
       h.scope_key,
       h.row_count,
       h.component_hash,
       h.details,
       h.created_at
FROM mmo_projection_hash_runs r
JOIN mmo_projection_component_hashes h ON h.projection_hash_run_id=r.projection_hash_run_id
WHERE r.started_at=(SELECT MAX(r2.started_at) FROM mmo_projection_hash_runs r2 WHERE r2.world_instance_id=r.world_instance_id)
ORDER BY h.component_key, h.scope_key;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES('production/mysql/024_projection_hash_manifest', 'gothic-mmo-projection-hash-manifest-v1-mysql', 'Canonical projection component hash runs for DB restore, replay pre-flight and native/SQLite/MySQL parity evidence.')
ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes), applied_at=CURRENT_TIMESTAMP(6);
