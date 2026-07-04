-- Gothic MMO MySQL production migration 013.
-- Explicit split/merge contract for partial stack mutations.
-- Requires 001..012 MySQL production migrations and a bootstrap import.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE IF NOT EXISTS item_stack_audit (
  stack_audit_id        BINARY(16) PRIMARY KEY DEFAULT (UUID_TO_BIN(UUID(), 1)),
  audit_type            VARCHAR(32) NOT NULL,
  session_id            BINARY(16) NULL,
  character_id          BINARY(16) NOT NULL,
  world_instance_id     BINARY(16) NOT NULL,
  event_id              BINARY(16) NOT NULL,
  idempotency_key       VARCHAR(191) NOT NULL,
  source_item_instance_id BINARY(16) NOT NULL,
  target_item_instance_id BINARY(16) NULL,
  source_item_key       VARCHAR(191) NOT NULL,
  target_item_key       VARCHAR(191) NULL,
  source_before         INT NOT NULL,
  source_after          INT NOT NULL,
  target_before         INT NULL,
  target_after          INT NULL,
  moved_amount          INT NOT NULL,
  server_tick           BIGINT NOT NULL DEFAULT 0,
  raw_delta             JSON NOT NULL DEFAULT (JSON_OBJECT()),
  created_at            TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY item_stack_audit_idempotency_uk(world_instance_id, idempotency_key),
  KEY ix_item_stack_audit_character(character_id, created_at),
  KEY ix_item_stack_audit_source(source_item_instance_id),
  KEY ix_item_stack_audit_target(target_item_instance_id),
  CONSTRAINT item_stack_audit_session_fk FOREIGN KEY(session_id) REFERENCES server_sessions(session_id) ON DELETE SET NULL,
  CONSTRAINT item_stack_audit_character_fk FOREIGN KEY(character_id) REFERENCES characters(character_id) ON DELETE CASCADE,
  CONSTRAINT item_stack_audit_world_fk FOREIGN KEY(world_instance_id) REFERENCES realm_world_instances(world_instance_id) ON DELETE RESTRICT,
  CONSTRAINT item_stack_audit_event_fk FOREIGN KEY(event_id) REFERENCES world_event_journal(event_id) ON DELETE RESTRICT,
  CONSTRAINT item_stack_audit_source_fk FOREIGN KEY(source_item_instance_id) REFERENCES item_instances(item_instance_id) ON DELETE RESTRICT,
  CONSTRAINT item_stack_audit_target_fk FOREIGN KEY(target_item_instance_id) REFERENCES item_instances(item_instance_id) ON DELETE SET NULL,
  CONSTRAINT item_stack_audit_type_ck CHECK(audit_type IN ('split','merge')),
  CONSTRAINT item_stack_audit_amount_ck CHECK(source_before > 0 AND source_after >= 0 AND moved_amount > 0 AND (target_before IS NULL OR target_before >= 0) AND (target_after IS NULL OR target_after > 0)),
  CONSTRAINT item_stack_audit_tick_ck CHECK(server_tick >= 0),
  CONSTRAINT item_stack_audit_raw_json_ck CHECK(JSON_VALID(raw_delta))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DROP PROCEDURE IF EXISTS mmo_split_character_item_stack;
DELIMITER $$
CREATE PROCEDURE mmo_split_character_item_stack(
  IN  p_session_id              BINARY(16),
  IN  p_source_item_instance_id BINARY(16),
  IN  p_split_amount            INT,
  IN  p_new_item_instance_key   VARCHAR(191),
  IN  p_target_bag_index        INT,
  IN  p_server_tick             BIGINT,
  IN  p_metadata                JSON,
  IN  p_idempotency_key         VARCHAR(191),
  OUT p_event_id                BINARY(16),
  OUT p_new_item_instance_id    BINARY(16),
  OUT p_source_amount_after     INT
)
split_proc: BEGIN
  DECLARE v_character_id BINARY(16) DEFAULT NULL; DECLARE v_realm_id BINARY(16) DEFAULT NULL; DECLARE v_world_id BINARY(16) DEFAULT NULL; DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_template_id BINARY(16) DEFAULT NULL; DECLARE v_source_key VARCHAR(191) DEFAULT NULL; DECLARE v_source_amount INT DEFAULT NULL; DECLARE v_source_after INT DEFAULT NULL; DECLARE v_raw JSON DEFAULT NULL;
  DECLARE v_existing_type VARCHAR(32) DEFAULT NULL; DECLARE v_existing_event BINARY(16) DEFAULT NULL; DECLARE v_existing_target BINARY(16) DEFAULT NULL; DECLARE v_existing_after INT DEFAULT NULL; DECLARE v_bag INT DEFAULT NULL; DECLARE v_payload JSON DEFAULT NULL;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
  SET p_event_id=NULL; SET p_new_item_instance_id=NULL; SET p_source_amount_after=NULL;
  IF p_source_item_instance_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='source item is required'; END IF;
  IF p_split_amount IS NULL OR p_split_amount <= 0 THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='split amount must be positive'; END IF;
  IF p_new_item_instance_key IS NULL OR p_new_item_instance_key='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='new item instance key is required'; END IF;
  IF p_idempotency_key IS NULL OR p_idempotency_key='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='idempotency key is required'; END IF;
  START TRANSACTION;
  SELECT ss.character_id,ss.realm_id,ss.world_instance_id,c.character_key INTO v_character_id,v_realm_id,v_world_id,v_character_key FROM server_sessions ss JOIN characters c ON c.character_id=ss.character_id WHERE ss.session_id=p_session_id AND ss.lifecycle_state='active' LIMIT 1 FOR UPDATE;
  IF v_character_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='active session not found'; END IF;
  SELECT audit_type,event_id,target_item_instance_id,source_after INTO v_existing_type,v_existing_event,v_existing_target,v_existing_after FROM item_stack_audit WHERE world_instance_id=v_world_id AND idempotency_key=p_idempotency_key LIMIT 1 FOR UPDATE;
  IF v_existing_event IS NOT NULL THEN IF v_existing_type <> 'split' THEN SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT='idempotency key reused with different stack type'; END IF; SET p_event_id=v_existing_event; SET p_new_item_instance_id=v_existing_target; SET p_source_amount_after=v_existing_after; COMMIT; LEAVE split_proc; END IF;
  SELECT ii.item_template_id, ii.item_instance_key, ci.amount, ii.raw_payload INTO v_template_id,v_source_key,v_source_amount,v_raw FROM character_inventory ci JOIN item_instances ii ON ii.item_instance_id=ci.item_instance_id WHERE ci.character_id=v_character_id AND ci.item_instance_id=p_source_item_instance_id AND ii.owner_type='character' AND ii.lifecycle_state='active' LIMIT 1 FOR UPDATE;
  IF v_source_amount IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='source stack not found in character inventory'; END IF;
  IF v_source_amount <= p_split_amount THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='split amount must be lower than source amount'; END IF;
  SET v_source_after=v_source_amount-p_split_amount;
  SET v_bag=p_target_bag_index;
  IF v_bag IS NULL THEN SELECT COALESCE(MAX(bag_index)+1,0) INTO v_bag FROM character_inventory WHERE character_id=v_character_id; END IF;
  INSERT INTO item_instances(realm_id,item_template_id,item_instance_key,owner_type,owner_id,quantity,bind_state,lifecycle_state,raw_payload)
  VALUES(v_realm_id,v_template_id,p_new_item_instance_key,'character',v_character_id,p_split_amount,'unbound','active',JSON_MERGE_PATCH(COALESCE(v_raw,JSON_OBJECT()), JSON_OBJECT('split_from',v_source_key,'split_amount',p_split_amount))) ;
  SET p_new_item_instance_id = UUID_TO_BIN(UUID(),1);
  SELECT item_instance_id INTO p_new_item_instance_id FROM item_instances WHERE item_instance_key=p_new_item_instance_key LIMIT 1 FOR UPDATE;
  SET v_payload=JSON_OBJECT('character_key',v_character_key,'source_item_instance_key',v_source_key,'target_item_instance_key',p_new_item_instance_key,'source_before',v_source_amount,'source_after',v_source_after,'split_amount',p_split_amount,'target_bag_index',v_bag,'metadata',COALESCE(p_metadata,JSON_OBJECT()));
  CALL mmo_append_world_event(v_realm_id,v_world_id,v_character_id,'item_stack_split','inventory',COALESCE(p_server_tick,0),v_character_key,p_new_item_instance_key,v_payload,p_idempotency_key,'server',NULL,NULL,p_event_id);
  UPDATE character_inventory SET amount=v_source_after,source_amount=v_source_after,source_iterator_count=v_source_after WHERE character_id=v_character_id AND item_instance_id=p_source_item_instance_id;
  UPDATE item_instances SET quantity=v_source_after,updated_at=CURRENT_TIMESTAMP(6) WHERE item_instance_id=p_source_item_instance_id;
  INSERT INTO character_inventory(character_id,item_instance_id,bag_index,amount,source_amount,source_iterator_count) VALUES(v_character_id,p_new_item_instance_id,v_bag,p_split_amount,p_split_amount,p_split_amount);
  INSERT INTO item_stack_audit(audit_type,session_id,character_id,world_instance_id,event_id,idempotency_key,source_item_instance_id,target_item_instance_id,source_item_key,target_item_key,source_before,source_after,target_before,target_after,moved_amount,server_tick,raw_delta)
  VALUES('split',p_session_id,v_character_id,v_world_id,p_event_id,p_idempotency_key,p_source_item_instance_id,p_new_item_instance_id,v_source_key,p_new_item_instance_key,v_source_amount,v_source_after,NULL,p_split_amount,p_split_amount,COALESCE(p_server_tick,0),v_payload);
  UPDATE server_sessions SET last_seen_at=CURRENT_TIMESTAMP(6) WHERE session_id=p_session_id; UPDATE realm_world_instances SET current_tick=GREATEST(current_tick,COALESCE(p_server_tick,0)) WHERE world_instance_id=v_world_id;
  SET p_source_amount_after=v_source_after; COMMIT;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_merge_character_item_stack;
DELIMITER $$
CREATE PROCEDURE mmo_merge_character_item_stack(
  IN  p_session_id              BINARY(16),
  IN  p_source_item_instance_id BINARY(16),
  IN  p_target_item_instance_id BINARY(16),
  IN  p_server_tick             BIGINT,
  IN  p_metadata                JSON,
  IN  p_idempotency_key         VARCHAR(191),
  OUT p_event_id                BINARY(16),
  OUT p_target_amount_after     INT
)
merge_proc: BEGIN
  DECLARE v_character_id BINARY(16) DEFAULT NULL; DECLARE v_realm_id BINARY(16) DEFAULT NULL; DECLARE v_world_id BINARY(16) DEFAULT NULL; DECLARE v_character_key VARCHAR(191) DEFAULT NULL;
  DECLARE v_source_template BINARY(16) DEFAULT NULL; DECLARE v_target_template BINARY(16) DEFAULT NULL; DECLARE v_source_key VARCHAR(191) DEFAULT NULL; DECLARE v_target_key VARCHAR(191) DEFAULT NULL; DECLARE v_source_amount INT DEFAULT NULL; DECLARE v_target_before INT DEFAULT NULL; DECLARE v_target_after INT DEFAULT NULL;
  DECLARE v_existing_type VARCHAR(32) DEFAULT NULL; DECLARE v_existing_event BINARY(16) DEFAULT NULL; DECLARE v_existing_after INT DEFAULT NULL; DECLARE v_payload JSON DEFAULT NULL;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
  SET p_event_id=NULL; SET p_target_amount_after=NULL;
  IF p_source_item_instance_id IS NULL OR p_target_item_instance_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='source and target items are required'; END IF;
  IF p_source_item_instance_id = p_target_item_instance_id THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='cannot merge item stack into itself'; END IF;
  IF p_idempotency_key IS NULL OR p_idempotency_key='' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='idempotency key is required'; END IF;
  START TRANSACTION;
  SELECT ss.character_id,ss.realm_id,ss.world_instance_id,c.character_key INTO v_character_id,v_realm_id,v_world_id,v_character_key FROM server_sessions ss JOIN characters c ON c.character_id=ss.character_id WHERE ss.session_id=p_session_id AND ss.lifecycle_state='active' LIMIT 1 FOR UPDATE;
  IF v_character_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='active session not found'; END IF;
  SELECT audit_type,event_id,target_after INTO v_existing_type,v_existing_event,v_existing_after FROM item_stack_audit WHERE world_instance_id=v_world_id AND idempotency_key=p_idempotency_key LIMIT 1 FOR UPDATE;
  IF v_existing_event IS NOT NULL THEN IF v_existing_type <> 'merge' THEN SIGNAL SQLSTATE '23000' SET MESSAGE_TEXT='idempotency key reused with different stack type'; END IF; SET p_event_id=v_existing_event; SET p_target_amount_after=v_existing_after; COMMIT; LEAVE merge_proc; END IF;
  SELECT ii.item_template_id,ii.item_instance_key,ci.amount INTO v_source_template,v_source_key,v_source_amount FROM character_inventory ci JOIN item_instances ii ON ii.item_instance_id=ci.item_instance_id WHERE ci.character_id=v_character_id AND ci.item_instance_id=p_source_item_instance_id AND ii.owner_type='character' AND ii.lifecycle_state='active' LIMIT 1 FOR UPDATE;
  SELECT ii.item_template_id,ii.item_instance_key,ci.amount INTO v_target_template,v_target_key,v_target_before FROM character_inventory ci JOIN item_instances ii ON ii.item_instance_id=ci.item_instance_id WHERE ci.character_id=v_character_id AND ci.item_instance_id=p_target_item_instance_id AND ii.owner_type='character' AND ii.lifecycle_state='active' LIMIT 1 FOR UPDATE;
  IF v_source_amount IS NULL OR v_target_before IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='source or target stack not found'; END IF;
  IF v_source_template <> v_target_template THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='cannot merge different item templates'; END IF;
  SET v_target_after=v_target_before+v_source_amount;
  SET v_payload=JSON_OBJECT('character_key',v_character_key,'source_item_instance_key',v_source_key,'target_item_instance_key',v_target_key,'source_before',v_source_amount,'source_after',0,'target_before',v_target_before,'target_after',v_target_after,'metadata',COALESCE(p_metadata,JSON_OBJECT()));
  CALL mmo_append_world_event(v_realm_id,v_world_id,v_character_id,'item_stack_merged','inventory',COALESCE(p_server_tick,0),v_character_key,v_target_key,v_payload,p_idempotency_key,'server',NULL,NULL,p_event_id);
  DELETE FROM character_equipment WHERE character_id=v_character_id AND item_instance_id=p_source_item_instance_id;
  DELETE FROM character_inventory WHERE character_id=v_character_id AND item_instance_id=p_source_item_instance_id;
  UPDATE item_instances SET quantity=0,lifecycle_state='consumed',owner_type='system',owner_id=NULL,updated_at=CURRENT_TIMESTAMP(6) WHERE item_instance_id=p_source_item_instance_id;
  UPDATE character_inventory SET amount=v_target_after,source_amount=v_target_after,source_iterator_count=v_target_after WHERE character_id=v_character_id AND item_instance_id=p_target_item_instance_id;
  UPDATE item_instances SET quantity=v_target_after,updated_at=CURRENT_TIMESTAMP(6) WHERE item_instance_id=p_target_item_instance_id;
  INSERT INTO item_stack_audit(audit_type,session_id,character_id,world_instance_id,event_id,idempotency_key,source_item_instance_id,target_item_instance_id,source_item_key,target_item_key,source_before,source_after,target_before,target_after,moved_amount,server_tick,raw_delta)
  VALUES('merge',p_session_id,v_character_id,v_world_id,p_event_id,p_idempotency_key,p_source_item_instance_id,p_target_item_instance_id,v_source_key,v_target_key,v_source_amount,0,v_target_before,v_target_after,v_source_amount,COALESCE(p_server_tick,0),v_payload);
  UPDATE server_sessions SET last_seen_at=CURRENT_TIMESTAMP(6) WHERE session_id=p_session_id; UPDATE realm_world_instances SET current_tick=GREATEST(current_tick,COALESCE(p_server_tick,0)) WHERE world_instance_id=v_world_id;
  SET p_target_amount_after=v_target_after; COMMIT;
END$$
DELIMITER ;

CREATE OR REPLACE VIEW v_item_stack_audit AS
SELECT BIN_TO_UUID(stack_audit_id,1) AS stack_audit_uuid, audit_type, BIN_TO_UUID(event_id,1) AS event_uuid, idempotency_key, source_item_key, target_item_key, source_before, source_after, target_before, target_after, moved_amount, server_tick, created_at
FROM item_stack_audit;

CREATE OR REPLACE VIEW v_character_stack_items AS
SELECT c.character_key, BIN_TO_UUID(ii.item_instance_id,1) AS item_instance_uuid, ii.item_instance_key, ii.item_template_id, ci.amount, ii.quantity, ii.lifecycle_state, ci.bag_index
FROM character_inventory ci JOIN characters c ON c.character_id=ci.character_id JOIN item_instances ii ON ii.item_instance_id=ci.item_instance_id
WHERE ci.amount > 1 AND ii.lifecycle_state='active';

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES('production/mysql/013_item_stack_write_path', 'gothic-mmo-item-stack-write-path-v1-mysql', 'Explicit split/merge item stack procedures with deterministic item instance ownership projection.')
ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes), applied_at=CURRENT_TIMESTAMP(6);
