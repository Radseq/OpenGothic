-- Gothic MMO MySQL production migration 012.
-- Server-owned combat/resource write path.
-- Requires 001..011 MySQL production migrations and a bootstrap import.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE IF NOT EXISTS combat_resource_audit (
  combat_audit_id       BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  audit_type            VARCHAR(32) NOT NULL,
  session_id            BINARY(16) NULL,
  actor_character_id    BINARY(16) NOT NULL,
  target_character_id   BINARY(16) NULL,
  world_instance_id     BINARY(16) NOT NULL,
  event_id              BINARY(16) NOT NULL,
  idempotency_key       VARCHAR(191) NOT NULL,
  target_entity_key     VARCHAR(191) NULL,
  item_instance_id      BINARY(16) NULL,
  amount                INT NOT NULL,
  value_before          INT NULL,
  value_after           INT NULL,
  server_tick           BIGINT NOT NULL DEFAULT 0,
  raw_delta             JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at            TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY combat_resource_audit_idempotency_uk(world_instance_id, idempotency_key),
  KEY ix_combat_resource_audit_actor(actor_character_id, created_at),
  KEY ix_combat_resource_audit_target(target_character_id, created_at),
  KEY ix_combat_resource_audit_entity(world_instance_id, target_entity_key, created_at),
  KEY ix_combat_resource_audit_event(event_id),
  CONSTRAINT combat_resource_audit_session_fk FOREIGN KEY(session_id) REFERENCES server_sessions(session_id) ON DELETE SET NULL,
  CONSTRAINT combat_resource_audit_actor_fk FOREIGN KEY(actor_character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT combat_resource_audit_target_fk FOREIGN KEY(target_character_id) REFERENCES characters(character_id) ON DELETE SET NULL,
  CONSTRAINT combat_resource_audit_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE RESTRICT,
  CONSTRAINT combat_resource_audit_event_fk FOREIGN KEY(event_id) REFERENCES world_event_journal(event_id) ON DELETE RESTRICT,
  CONSTRAINT combat_resource_audit_item_fk FOREIGN KEY(item_instance_id) REFERENCES item_instances(item_instance_id) ON DELETE SET NULL,
  CONSTRAINT combat_resource_audit_type_ck CHECK(audit_type IN ('character_damage','world_entity_damage','mana_consume','item_consume')),
  CONSTRAINT combat_resource_audit_amount_ck CHECK(amount >= 0),
  CONSTRAINT combat_resource_audit_value_ck CHECK((value_before IS NULL OR value_before >= 0) AND (value_after IS NULL OR value_after >= 0)),
  CONSTRAINT combat_resource_audit_tick_ck CHECK(server_tick >= 0),
  CONSTRAINT combat_resource_audit_raw_json_ck CHECK(JSON_VALID(raw_delta))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_apply_character_damage;
DELIMITER $$
CREATE PROCEDURE mmo_apply_character_damage(
  IN  p_session_id          BINARY(16),
  IN  p_target_character_key VARCHAR(191),
  IN  p_damage_amount       INT,
  IN  p_server_tick         BIGINT,
  IN  p_metadata            JSON,
  IN  p_idempotency_key     VARCHAR(191),
  OUT p_event_id            BINARY(16),
  OUT p_health_after        INT
)
char_damage_proc: BEGIN
  DECLARE v_actor_id BINARY(16) DEFAULT NULL;
  DECLARE v_target_id BINARY(16) DEFAULT NULL;
  DECLARE v_realm_id BINARY(16) DEFAULT NULL;
  DECLARE v_world_id BINARY(16) DEFAULT NULL;
  DECLARE v_actor_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_health_before INT DEFAULT NULL;
  DECLARE v_health_after INT DEFAULT NULL;
  DECLARE v_existing_type VARCHAR(32) DEFAULT NULL;
  DECLARE v_existing_event BINARY(16) DEFAULT NULL;
  DECLARE v_existing_after INT DEFAULT NULL;
  DECLARE v_payload JSON DEFAULT NULL;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
  SET p_event_id = NULL; SET p_health_after = NULL;
  IF p_damage_amount IS NULL OR p_damage_amount < 0 THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='damage amount must be non-negative'; END IF;
  IF p_target_character_key IS NULL OR p_target_character_key = '' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='target character key is required'; END IF;
  IF p_idempotency_key IS NULL OR p_idempotency_key = '' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='idempotency key is required'; END IF;
  START TRANSACTION;
  SELECT ss.character_id, ss.realm_id, ss.world_instance_id, c.character_key INTO v_actor_id, v_realm_id, v_world_id, v_actor_key
    FROM server_sessions ss JOIN characters c ON c.character_id=ss.character_id
   WHERE ss.session_id=p_session_id AND ss.lifecycle_state='active' LIMIT 1 FOR UPDATE;
  IF v_actor_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='active session not found'; END IF;
  SELECT audit_type, event_id, value_after INTO v_existing_type, v_existing_event, v_existing_after
    FROM combat_resource_audit WHERE world_instance_id=v_world_id AND idempotency_key=p_idempotency_key LIMIT 1 FOR UPDATE;
  IF v_existing_event IS NOT NULL THEN
    IF v_existing_type <> 'character_damage' THEN SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT='idempotency key reused with different combat type'; END IF;
    SET p_event_id = v_existing_event; SET p_health_after = v_existing_after; COMMIT; LEAVE char_damage_proc;
  END IF;
  SELECT c.character_id, cs.health_current INTO v_target_id, v_health_before
    FROM characters c JOIN character_stats cs ON cs.character_id=c.character_id
   WHERE c.character_key=p_target_character_key AND c.realm_id=v_realm_id AND c.lifecycle_state='active' LIMIT 1 FOR UPDATE;
  IF v_target_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='target character not found'; END IF;
  SET v_health_after = GREATEST(0, v_health_before - p_damage_amount);
  SET v_payload = JSON_OBJECT('actor_character_key',v_actor_key,'target_character_key',p_target_character_key,'damage_amount',p_damage_amount,'health_before',v_health_before,'health_after',v_health_after,'metadata',COALESCE(p_metadata,JSON_OBJECT()));
  CALL mmo_append_world_event(v_realm_id, v_world_id, v_actor_id, 'character_damage_applied', 'combat', COALESCE(p_server_tick,0), p_target_character_key, v_actor_key, v_payload, p_idempotency_key, 'server', NULL, NULL, p_event_id);
  UPDATE character_stats SET health_current=v_health_after, row_version=row_version+1 WHERE character_id=v_target_id;
  INSERT INTO combat_resource_audit(audit_type,session_id,actor_character_id,target_character_id,world_instance_id,event_id,idempotency_key,target_entity_key,item_instance_id,amount,value_before,value_after,server_tick,raw_delta)
  VALUES('character_damage',p_session_id,v_actor_id,v_target_id,v_world_id,p_event_id,p_idempotency_key,p_target_character_key,NULL,p_damage_amount,v_health_before,v_health_after,COALESCE(p_server_tick,0),v_payload);
  UPDATE server_sessions SET last_seen_at=CURRENT_TIMESTAMP(6) WHERE session_id=p_session_id;
  UPDATE realm_world_instances SET current_tick=GREATEST(current_tick,COALESCE(p_server_tick,0)) WHERE world_instance_id=v_world_id;
  SET p_health_after = v_health_after;
  COMMIT;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_apply_world_entity_damage;
DELIMITER $$
CREATE PROCEDURE mmo_apply_world_entity_damage(
  IN  p_session_id       BINARY(16),
  IN  p_target_entity_key VARCHAR(191),
  IN  p_damage_amount    INT,
  IN  p_mark_dead_if_zero BOOLEAN,
  IN  p_server_tick      BIGINT,
  IN  p_metadata         JSON,
  IN  p_idempotency_key  VARCHAR(191),
  OUT p_event_id         BINARY(16),
  OUT p_health_after     INT,
  OUT p_row_version_after BIGINT
)
entity_damage_proc: BEGIN
  DECLARE v_actor_id BINARY(16) DEFAULT NULL; DECLARE v_realm_id BINARY(16) DEFAULT NULL; DECLARE v_world_id BINARY(16) DEFAULT NULL; DECLARE v_actor_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_kind VARCHAR(32) DEFAULT NULL; DECLARE v_lifecycle VARCHAR(32) DEFAULT NULL; DECLARE v_health_before INT DEFAULT NULL; DECLARE v_health_after INT DEFAULT NULL; DECLARE v_row_before BIGINT DEFAULT NULL; DECLARE v_row_after BIGINT DEFAULT NULL;
  DECLARE v_existing_type VARCHAR(32) DEFAULT NULL; DECLARE v_existing_event BINARY(16) DEFAULT NULL; DECLARE v_existing_after INT DEFAULT NULL; DECLARE v_payload JSON DEFAULT NULL;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
  SET p_event_id=NULL; SET p_health_after=NULL; SET p_row_version_after=NULL;
  IF p_damage_amount IS NULL OR p_damage_amount < 0 THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='damage amount must be non-negative'; END IF;
  IF p_target_entity_key IS NULL OR p_target_entity_key='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='target entity key is required'; END IF;
  IF p_idempotency_key IS NULL OR p_idempotency_key='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='idempotency key is required'; END IF;
  START TRANSACTION;
  SELECT ss.character_id, ss.realm_id, ss.world_instance_id, c.character_key INTO v_actor_id, v_realm_id, v_world_id, v_actor_key FROM server_sessions ss JOIN characters c ON c.character_id=ss.character_id WHERE ss.session_id=p_session_id AND ss.lifecycle_state='active' LIMIT 1 FOR UPDATE;
  IF v_actor_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='active session not found'; END IF;
  SELECT audit_type, event_id, value_after INTO v_existing_type, v_existing_event, v_existing_after FROM combat_resource_audit WHERE world_instance_id=v_world_id AND idempotency_key=p_idempotency_key LIMIT 1 FOR UPDATE;
  IF v_existing_event IS NOT NULL THEN IF v_existing_type <> 'world_entity_damage' THEN SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT='idempotency key reused with different combat type'; END IF; SET p_event_id=v_existing_event; SET p_health_after=v_existing_after; COMMIT; LEAVE entity_damage_proc; END IF;
  SELECT entity_kind,lifecycle_state,health_current,row_version INTO v_kind,v_lifecycle,v_health_before,v_row_before FROM world_entity_state WHERE world_instance_id=v_world_id AND entity_key=p_target_entity_key LIMIT 1 FOR UPDATE;
  IF v_kind IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='target world entity not found'; END IF;
  IF v_kind NOT IN ('npc','creature','vob','interactive') THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='entity kind cannot receive combat damage'; END IF;
  IF v_lifecycle <> 'active' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='target entity is not active'; END IF;
  SET v_health_before = COALESCE(v_health_before,0); SET v_health_after = GREATEST(0, v_health_before - p_damage_amount); SET v_row_after=v_row_before+1;
  SET v_payload=JSON_OBJECT('actor_character_key',v_actor_key,'target_entity_key',p_target_entity_key,'damage_amount',p_damage_amount,'health_before',v_health_before,'health_after',v_health_after,'mark_dead_if_zero',COALESCE(p_mark_dead_if_zero,FALSE),'metadata',COALESCE(p_metadata,JSON_OBJECT()));
  CALL mmo_append_world_event(v_realm_id,v_world_id,v_actor_id,'world_entity_damage_applied','combat',COALESCE(p_server_tick,0),p_target_entity_key,v_actor_key,v_payload,p_idempotency_key,'server',NULL,NULL,p_event_id);
  UPDATE world_entity_state SET health_current=v_health_after, lifecycle_state=IF(v_health_after=0 AND COALESCE(p_mark_dead_if_zero,FALSE),'dead',lifecycle_state), row_version=v_row_after, updated_at=CURRENT_TIMESTAMP(6) WHERE world_instance_id=v_world_id AND entity_key=p_target_entity_key;
  INSERT INTO combat_resource_audit(audit_type,session_id,actor_character_id,target_character_id,world_instance_id,event_id,idempotency_key,target_entity_key,item_instance_id,amount,value_before,value_after,server_tick,raw_delta)
  VALUES('world_entity_damage',p_session_id,v_actor_id,NULL,v_world_id,p_event_id,p_idempotency_key,p_target_entity_key,NULL,p_damage_amount,v_health_before,v_health_after,COALESCE(p_server_tick,0),v_payload);
  UPDATE server_sessions SET last_seen_at=CURRENT_TIMESTAMP(6) WHERE session_id=p_session_id; UPDATE realm_world_instances SET current_tick=GREATEST(current_tick,COALESCE(p_server_tick,0)) WHERE world_instance_id=v_world_id;
  SET p_health_after=v_health_after; SET p_row_version_after=v_row_after; COMMIT;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_consume_character_mana;
DELIMITER $$
CREATE PROCEDURE mmo_consume_character_mana(IN p_session_id BINARY(16), IN p_mana_amount INT, IN p_server_tick BIGINT, IN p_metadata JSON, IN p_idempotency_key VARCHAR(191), OUT p_event_id BINARY(16), OUT p_mana_after INT)
mana_proc: BEGIN
  DECLARE v_character_id BINARY(16) DEFAULT NULL; DECLARE v_realm_id BINARY(16) DEFAULT NULL; DECLARE v_world_id BINARY(16) DEFAULT NULL; DECLARE v_character_key VARCHAR(191) DEFAULT NULL; DECLARE v_before INT DEFAULT NULL; DECLARE v_after INT DEFAULT NULL; DECLARE v_existing_type VARCHAR(32) DEFAULT NULL; DECLARE v_existing_event BINARY(16) DEFAULT NULL; DECLARE v_existing_after INT DEFAULT NULL; DECLARE v_payload JSON DEFAULT NULL;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
  SET p_event_id=NULL; SET p_mana_after=NULL;
  IF p_mana_amount IS NULL OR p_mana_amount < 0 THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mana amount must be non-negative'; END IF;
  IF p_idempotency_key IS NULL OR p_idempotency_key='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='idempotency key is required'; END IF;
  START TRANSACTION;
  SELECT ss.character_id,ss.realm_id,ss.world_instance_id,c.character_key INTO v_character_id,v_realm_id,v_world_id,v_character_key FROM server_sessions ss JOIN characters c ON c.character_id=ss.character_id WHERE ss.session_id=p_session_id AND ss.lifecycle_state='active' LIMIT 1 FOR UPDATE;
  IF v_character_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='active session not found'; END IF;
  SELECT audit_type,event_id,value_after INTO v_existing_type,v_existing_event,v_existing_after FROM combat_resource_audit WHERE world_instance_id=v_world_id AND idempotency_key=p_idempotency_key LIMIT 1 FOR UPDATE;
  IF v_existing_event IS NOT NULL THEN IF v_existing_type <> 'mana_consume' THEN SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT='idempotency key reused with different combat type'; END IF; SET p_event_id=v_existing_event; SET p_mana_after=v_existing_after; COMMIT; LEAVE mana_proc; END IF;
  SELECT mana_current INTO v_before FROM character_stats WHERE character_id=v_character_id LIMIT 1 FOR UPDATE;
  IF v_before < p_mana_amount THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='insufficient mana'; END IF;
  SET v_after=v_before-p_mana_amount;
  SET v_payload=JSON_OBJECT('character_key',v_character_key,'mana_amount',p_mana_amount,'mana_before',v_before,'mana_after',v_after,'metadata',COALESCE(p_metadata,JSON_OBJECT()));
  CALL mmo_append_world_event(v_realm_id,v_world_id,v_character_id,'character_mana_consumed','spell',COALESCE(p_server_tick,0),v_character_key,'mana',v_payload,p_idempotency_key,'server',NULL,NULL,p_event_id);
  UPDATE character_stats SET mana_current=v_after,row_version=row_version+1 WHERE character_id=v_character_id;
  INSERT INTO combat_resource_audit(audit_type,session_id,actor_character_id,target_character_id,world_instance_id,event_id,idempotency_key,target_entity_key,item_instance_id,amount,value_before,value_after,server_tick,raw_delta)
  VALUES('mana_consume',p_session_id,v_character_id,NULL,v_world_id,p_event_id,p_idempotency_key,v_character_key,NULL,p_mana_amount,v_before,v_after,COALESCE(p_server_tick,0),v_payload);
  UPDATE server_sessions SET last_seen_at=CURRENT_TIMESTAMP(6) WHERE session_id=p_session_id; UPDATE realm_world_instances SET current_tick=GREATEST(current_tick,COALESCE(p_server_tick,0)) WHERE world_instance_id=v_world_id;
  SET p_mana_after=v_after; COMMIT;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_consume_character_item;
DELIMITER $$
CREATE PROCEDURE mmo_consume_character_item(IN p_session_id BINARY(16), IN p_item_instance_id BINARY(16), IN p_consume_amount INT, IN p_reason VARCHAR(128), IN p_server_tick BIGINT, IN p_metadata JSON, IN p_idempotency_key VARCHAR(191), OUT p_event_id BINARY(16), OUT p_amount_after INT)
item_consume_proc: BEGIN
  DECLARE v_character_id BINARY(16) DEFAULT NULL; DECLARE v_realm_id BINARY(16) DEFAULT NULL; DECLARE v_world_id BINARY(16) DEFAULT NULL; DECLARE v_character_key VARCHAR(191) DEFAULT NULL; DECLARE v_item_key VARCHAR(191) DEFAULT NULL; DECLARE v_before INT DEFAULT NULL; DECLARE v_after INT DEFAULT NULL; DECLARE v_existing_type VARCHAR(32) DEFAULT NULL; DECLARE v_existing_event BINARY(16) DEFAULT NULL; DECLARE v_existing_after INT DEFAULT NULL; DECLARE v_payload JSON DEFAULT NULL;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
  SET p_event_id=NULL; SET p_amount_after=NULL;
  IF p_item_instance_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='item_instance_id is required'; END IF;
  IF p_consume_amount IS NULL OR p_consume_amount <= 0 THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='consume amount must be positive'; END IF;
  IF p_idempotency_key IS NULL OR p_idempotency_key='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='idempotency key is required'; END IF;
  START TRANSACTION;
  SELECT ss.character_id,ss.realm_id,ss.world_instance_id,c.character_key INTO v_character_id,v_realm_id,v_world_id,v_character_key FROM server_sessions ss JOIN characters c ON c.character_id=ss.character_id WHERE ss.session_id=p_session_id AND ss.lifecycle_state='active' LIMIT 1 FOR UPDATE;
  IF v_character_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='active session not found'; END IF;
  SELECT audit_type,event_id,value_after INTO v_existing_type,v_existing_event,v_existing_after FROM combat_resource_audit WHERE world_instance_id=v_world_id AND idempotency_key=p_idempotency_key LIMIT 1 FOR UPDATE;
  IF v_existing_event IS NOT NULL THEN IF v_existing_type <> 'item_consume' THEN SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT='idempotency key reused with different combat type'; END IF; SET p_event_id=v_existing_event; SET p_amount_after=v_existing_after; COMMIT; LEAVE item_consume_proc; END IF;
  SELECT ci.amount, ii.item_instance_key INTO v_before, v_item_key FROM character_inventory ci JOIN item_instances ii ON ii.item_instance_id=ci.item_instance_id WHERE ci.character_id=v_character_id AND ci.item_instance_id=p_item_instance_id AND ii.lifecycle_state='active' LIMIT 1 FOR UPDATE;
  IF v_before IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='character does not own active item'; END IF;
  IF v_before < p_consume_amount THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='not enough item amount to consume'; END IF;
  SET v_after=v_before-p_consume_amount;
  SET v_payload=JSON_OBJECT('character_key',v_character_key,'item_instance_key',v_item_key,'consume_amount',p_consume_amount,'amount_before',v_before,'amount_after',v_after,'reason',COALESCE(NULLIF(p_reason,''),'consume'),'metadata',COALESCE(p_metadata,JSON_OBJECT()));
  CALL mmo_append_world_event(v_realm_id,v_world_id,v_character_id,'character_item_consumed','inventory',COALESCE(p_server_tick,0),v_character_key,v_item_key,v_payload,p_idempotency_key,'server',NULL,NULL,p_event_id);
  IF v_after = 0 THEN
    DELETE FROM character_equipment WHERE character_id=v_character_id AND item_instance_id=p_item_instance_id;
    DELETE FROM character_inventory WHERE character_id=v_character_id AND item_instance_id=p_item_instance_id;
    UPDATE item_instances SET quantity=0,lifecycle_state='consumed',owner_type='system',owner_id=NULL,updated_at=CURRENT_TIMESTAMP(6) WHERE item_instance_id=p_item_instance_id;
  ELSE
    UPDATE character_inventory SET amount=v_after, source_amount=v_after, source_iterator_count=v_after WHERE character_id=v_character_id AND item_instance_id=p_item_instance_id;
    UPDATE item_instances SET quantity=v_after, updated_at=CURRENT_TIMESTAMP(6) WHERE item_instance_id=p_item_instance_id;
  END IF;
  INSERT INTO combat_resource_audit(audit_type,session_id,actor_character_id,target_character_id,world_instance_id,event_id,idempotency_key,target_entity_key,item_instance_id,amount,value_before,value_after,server_tick,raw_delta)
  VALUES('item_consume',p_session_id,v_character_id,NULL,v_world_id,p_event_id,p_idempotency_key,v_character_key,p_item_instance_id,p_consume_amount,v_before,v_after,COALESCE(p_server_tick,0),v_payload);
  UPDATE server_sessions SET last_seen_at=CURRENT_TIMESTAMP(6) WHERE session_id=p_session_id; UPDATE realm_world_instances SET current_tick=GREATEST(current_tick,COALESCE(p_server_tick,0)) WHERE world_instance_id=v_world_id;
  SET p_amount_after=v_after; COMMIT;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_combat_resource_audit AS
SELECT BIN_TO_UUID(combat_audit_id,1) AS combat_audit_uuid, audit_type, BIN_TO_UUID(event_id,1) AS event_uuid, idempotency_key, target_entity_key, BIN_TO_UUID(item_instance_id,1) AS item_instance_uuid, amount, value_before, value_after, server_tick, created_at
FROM combat_resource_audit;

CREATE OR REPLACE VIEW v_character_combat_sheet AS
SELECT c.character_key, BIN_TO_UUID(c.character_id,1) AS character_uuid, cs.health_current, cs.health_max, cs.mana_current, cs.mana_max, cs.row_version, cs.updated_at
FROM characters c JOIN character_stats cs ON cs.character_id=c.character_id;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES('production/mysql/012_combat_resource_write_path', 'gothic-mmo-combat-resource-write-path-v1-mysql', 'Character/world entity damage, mana consumption, item consumption and combat/resource audit. Recreated after status/lifecycle_state compatibility fix.')
ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes), applied_at=CURRENT_TIMESTAMP(6);
