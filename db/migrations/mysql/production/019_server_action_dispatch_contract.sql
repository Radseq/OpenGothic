-- Gothic MMO MySQL production migration 019.
-- Server action dispatch contract + claim/requeue procedures.
-- Requires 001..018 MySQL production migrations.

SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;
SET time_zone = '+00:00';
SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE IF NOT EXISTS mmo_server_action_dispatch_contracts (
  action_kind        VARCHAR(128) PRIMARY KEY,
  procedure_name     VARCHAR(191) NOT NULL,
  event_type         VARCHAR(128) NULL,
  event_class        VARCHAR(32) NULL,
  projection_name    VARCHAR(128) NULL,
  enabled            BOOLEAN NOT NULL DEFAULT TRUE,
  request_schema     JSON NOT NULL DEFAULT (JSON_OBJECT()),
  notes              TEXT NULL,
  created_at         TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  updated_at         TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  KEY ix_mmo_server_action_dispatch_enabled(enabled, action_kind),
  CONSTRAINT mmo_server_action_dispatch_event_class_ck CHECK(event_class IS NULL OR event_class IN ('character','inventory','equipment','world_entity','quest','dialog','script','combat','trade','spell','system','diagnostic')),
  CONSTRAINT mmo_server_action_dispatch_schema_json_ck CHECK(JSON_VALID(request_schema))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

INSERT INTO mmo_server_action_dispatch_contracts(action_kind, procedure_name, event_type, event_class, projection_name, enabled, request_schema, notes)
VALUES
  ('character_checkpoint',        'mmo_checkpoint_character_state',        'character_position_checkpoint',   'character',    'character_positions',      TRUE, JSON_OBJECT('required',JSON_ARRAY('position','stats','server_tick')), 'Periodic low-frequency server checkpoint; not per-frame movement replication.'),
  ('wallet_delta',                'mmo_adjust_character_wallet',           'character_wallet_delta',          'inventory',    'character_wallets',        TRUE, JSON_OBJECT('required',JSON_ARRAY('currency_key','delta_amount','reason_key','server_tick')), 'Generic wallet delta. Wallet emits inventory class in migration 004.'),
  ('grant_gold',                  'mmo_grant_character_gold',              'character_wallet_delta',          'inventory',    'character_wallets',        TRUE, JSON_OBJECT('required',JSON_ARRAY('amount','reason_key','server_tick')), 'Convenience gold grant action.'),
  ('spend_gold',                  'mmo_spend_character_gold',              'character_wallet_delta',          'inventory',    'character_wallets',        TRUE, JSON_OBJECT('required',JSON_ARRAY('amount','reason_key','server_tick')), 'Convenience gold spend action.'),
  ('pickup_world_item',           'mmo_pickup_world_item',                 'world_item_picked_up',            'inventory',    'item_instances',           TRUE, JSON_OBJECT('required',JSON_ARRAY('world_item_entity_key','item_instance_id','bag_index','server_tick')), 'Loose world-item pickup.'),
  ('remove_world_item',           'mmo_remove_world_item',                 'world_item_removed',              'world_entity', 'world_entity_state',       TRUE, JSON_OBJECT('required',JSON_ARRAY('world_item_entity_key','item_instance_id','server_tick')), 'Loose world-item removal/despawn.'),
  ('transfer_character_item',     'mmo_transfer_character_item',           'character_inventory_transferred', 'inventory',    'character_inventory',      TRUE, JSON_OBJECT('required',JSON_ARRAY('target_character_key','item_instance_id','target_bag_index','server_tick')), 'Character-to-character item transfer.'),
  ('equip_character_item',        'mmo_equip_character_item',              'character_item_equipped',         'equipment',    'character_equipment',      TRUE, JSON_OBJECT('required',JSON_ARRAY('item_instance_id','equipment_slot','server_tick')), 'Equip item after inventory validation.'),
  ('unequip_character_item',      'mmo_unequip_character_item',            'character_item_unequipped',       'equipment',    'character_equipment',      TRUE, JSON_OBJECT('required',JSON_ARRAY('equipment_slot','target_bag_index','server_tick')), 'Unequip item back to inventory.'),
  ('take_container_item',         'mmo_take_container_item',               'container_item_taken',            'inventory',    'world_inventory',          TRUE, JSON_OBJECT('required',JSON_ARRAY('owner_entity_key','item_instance_id','target_bag_index','server_tick')), 'Take item from container/interactive inventory.'),
  ('put_container_item',          'mmo_put_container_item',                'container_item_put',              'inventory',    'world_inventory',          TRUE, JSON_OBJECT('required',JSON_ARRAY('owner_entity_key','item_instance_id','server_tick')), 'Put item into container/interactive inventory.'),
  ('update_interactive_state',    'mmo_update_interactive_state',          'interactive_state_changed',       'world_entity', 'world_entity_state',       TRUE, JSON_OBJECT('required',JSON_ARRAY('entity_key','state_id','state_count','state_mask','server_tick')), 'Door/container/lock committed interactive state.'),
  ('set_script_int',              'mmo_set_character_script_int',          'character_script_int_set',        'script',       'character_script_state',   TRUE, JSON_OBJECT('required',JSON_ARRAY('script_key','symbol_index','value_index','value_int','server_tick')), 'Character-scoped Daedalus INT write.'),
  ('update_quest',                'mmo_update_character_quest',            'character_quest_updated',         'quest',        'character_quests',         TRUE, JSON_OBJECT('required',JSON_ARRAY('quest_key','section','status','text_entries','server_tick')), 'Quest status/text mutation.'),
  ('set_known_dialog',            'mmo_set_character_known_dialog',        'character_dialog_known_set',      'dialog',       'character_known_dialogs',  TRUE, JSON_OBJECT('required',JSON_ARRAY('npc_key','info_key','known','permanent','availability_state','server_tick')), 'Known/consumed dialog state.'),
  ('adjust_progression',          'mmo_adjust_character_progression',      'character_progression_adjusted',  'character',    'character_stats',          TRUE, JSON_OBJECT('required',JSON_ARRAY('experience_delta','learning_points_delta','reason_key','server_tick')), 'Experience/learning-points mutation.'),
  ('apply_experience_reward',     'mmo_apply_character_experience_reward', 'character_progression_adjusted',  'character',    'character_stats',          TRUE, JSON_OBJECT('required',JSON_ARRAY('experience_reward','reason_key','server_tick')), 'Positive XP reward helper.'),
  ('mark_npc_dead',               'mmo_mark_npc_dead',                     'npc_marked_dead',                 'combat',       'world_entity_state',       TRUE, JSON_OBJECT('required',JSON_ARRAY('npc_entity_key','server_tick')), 'NPC death; migration 009 emits combat class.'),
  ('respawn_npc',                 'mmo_respawn_npc',                       'npc_respawned',                   'combat',       'world_entity_state',       TRUE, JSON_OBJECT('required',JSON_ARRAY('npc_entity_key','position','health','server_tick')), 'NPC respawn; migration 009 emits combat class.'),
  ('trade_buy_from_npc',          'mmo_trade_buy_from_npc',                'trade_buy_from_npc',              'trade',        'character_inventory',      TRUE, JSON_OBJECT('required',JSON_ARRAY('npc_entity_key','stock_item_instance_id','price_gold','target_bag_index','server_tick')), 'Buy item from NPC stock.'),
  ('trade_sell_to_npc',           'mmo_trade_sell_to_npc',                 'trade_sell_to_npc',               'trade',        'npc_trade_inventory',      TRUE, JSON_OBJECT('required',JSON_ARRAY('npc_entity_key','item_instance_id','price_gold','server_tick')), 'Sell item to NPC stock.'),
  ('apply_character_damage',      'mmo_apply_character_damage',            'character_damage_applied',        'combat',       'character_stats',          TRUE, JSON_OBJECT('required',JSON_ARRAY('target_character_key','damage_amount','server_tick')), 'Character HP damage projection.'),
  ('apply_world_entity_damage',   'mmo_apply_world_entity_damage',         'world_entity_damage_applied',     'combat',       'world_entity_state',       TRUE, JSON_OBJECT('required',JSON_ARRAY('target_entity_key','damage_amount','mark_dead_if_zero','server_tick')), 'World entity/NPC damage projection.'),
  ('consume_mana',                'mmo_consume_character_mana',            'character_mana_consumed',         'spell',        'character_stats',          TRUE, JSON_OBJECT('required',JSON_ARRAY('mana_amount','server_tick')), 'Mana resource consumption for spells.'),
  ('consume_item',                'mmo_consume_character_item',            'character_item_consumed',         'inventory',    'character_inventory',      TRUE, JSON_OBJECT('required',JSON_ARRAY('item_instance_id','consume_amount','reason_key','server_tick')), 'Consumable/ammunition decrement.'),
  ('split_item_stack',            'mmo_split_character_item_stack',        'item_stack_split',                'inventory',    'character_inventory',      TRUE, JSON_OBJECT('required',JSON_ARRAY('source_item_instance_id','split_amount','target_bag_index','new_item_instance_key','server_tick')), 'Partial stack split.'),
  ('merge_item_stack',            'mmo_merge_character_item_stack',        'item_stack_merged',               'inventory',    'character_inventory',      TRUE, JSON_OBJECT('required',JSON_ARRAY('source_item_instance_id','target_item_instance_id','server_tick')), 'Stack merge.')
ON DUPLICATE KEY UPDATE
  procedure_name=VALUES(procedure_name),
  event_type=VALUES(event_type),
  event_class=VALUES(event_class),
  projection_name=VALUES(projection_name),
  enabled=VALUES(enabled),
  request_schema=VALUES(request_schema),
  notes=VALUES(notes),
  updated_at=CURRENT_TIMESTAMP(6);

CREATE OR REPLACE VIEW v_server_action_dispatch_contracts AS
SELECT action_kind,
       procedure_name,
       event_type,
       event_class,
       projection_name,
       enabled,
       request_schema,
       notes,
       updated_at
FROM mmo_server_action_dispatch_contracts
ORDER BY action_kind;

CREATE OR REPLACE VIEW v_server_action_dispatch_gaps AS
SELECT BIN_TO_UUID(o.action_id,1) AS action_uuid,
       o.action_kind,
       o.status,
       o.idempotency_key,
       BIN_TO_UUID(o.world_instance_id,1) AS world_instance_uuid,
       o.requested_at,
       c.procedure_name,
       c.enabled AS contract_enabled,
       CASE
         WHEN c.action_kind IS NULL THEN 'missing_contract'
         WHEN c.enabled = FALSE THEN 'disabled_contract'
         ELSE 'ok'
       END AS gap_status
FROM mmo_server_action_outbox o
LEFT JOIN mmo_server_action_dispatch_contracts c ON c.action_kind=o.action_kind
WHERE o.status IN ('pending','claimed')
  AND (c.action_kind IS NULL OR c.enabled=FALSE);

CREATE OR REPLACE VIEW v_claimable_server_actions AS
SELECT BIN_TO_UUID(o.action_id,1) AS action_uuid,
       o.action_kind,
       c.procedure_name,
       o.target_key,
       o.status,
       o.priority,
       o.attempt_count,
       o.max_attempts,
       o.idempotency_key,
       o.request_payload,
       o.requested_at
FROM mmo_server_action_outbox o
JOIN mmo_server_action_dispatch_contracts c ON c.action_kind=o.action_kind AND c.enabled=TRUE
WHERE o.status='pending'
ORDER BY o.priority ASC, o.requested_at ASC;

DROP PROCEDURE IF EXISTS mmo_validate_server_action_dispatch_contracts;
DELIMITER $$
CREATE PROCEDURE mmo_validate_server_action_dispatch_contracts(
  OUT p_error_count   INT,
  OUT p_warning_count INT
)
BEGIN
  DECLARE v_missing BIGINT DEFAULT 0;
  DECLARE v_disabled BIGINT DEFAULT 0;
  DECLARE v_dead BIGINT DEFAULT 0;

  SELECT COUNT(*) INTO v_missing
    FROM mmo_server_action_outbox o
    LEFT JOIN mmo_server_action_dispatch_contracts c ON c.action_kind=o.action_kind
   WHERE o.status IN ('pending','claimed') AND c.action_kind IS NULL;

  SELECT COUNT(*) INTO v_disabled
    FROM mmo_server_action_outbox o
    JOIN mmo_server_action_dispatch_contracts c ON c.action_kind=o.action_kind
   WHERE o.status IN ('pending','claimed') AND c.enabled=FALSE;

  SELECT COUNT(*) INTO v_dead
    FROM mmo_server_action_outbox
   WHERE status='dead_letter';

  SET p_error_count = IF(v_missing + v_disabled > 0, 1, 0);
  SET p_warning_count = IF(v_dead > 0, 1, 0);
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_claim_next_server_action;
DELIMITER $$
CREATE PROCEDURE mmo_claim_next_server_action(
  IN  p_worker_id          VARCHAR(128),
  OUT p_action_id          BINARY(16),
  OUT p_action_kind        VARCHAR(128),
  OUT p_session_id         BINARY(16),
  OUT p_character_id       BINARY(16),
  OUT p_world_instance_id  BINARY(16),
  OUT p_target_key         VARCHAR(191),
  OUT p_idempotency_key    VARCHAR(191),
  OUT p_request_payload    JSON
)
claim_proc: BEGIN
  DECLARE v_action_id BINARY(16) DEFAULT NULL;
  DECLARE v_not_found BOOLEAN DEFAULT FALSE;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;

  SET p_action_id=NULL;
  SET p_action_kind=NULL;
  SET p_session_id=NULL;
  SET p_character_id=NULL;
  SET p_world_instance_id=NULL;
  SET p_target_key=NULL;
  SET p_idempotency_key=NULL;
  SET p_request_payload=NULL;

  IF p_worker_id IS NULL OR TRIM(p_worker_id)='' THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='worker_id is required';
  END IF;

  START TRANSACTION;

  SELECT o.action_id
    INTO v_action_id
    FROM mmo_server_action_outbox o
    JOIN mmo_server_action_dispatch_contracts c ON c.action_kind=o.action_kind AND c.enabled=TRUE
   WHERE o.status='pending'
     AND o.attempt_count < o.max_attempts
   ORDER BY o.priority ASC, o.requested_at ASC
   LIMIT 1
   FOR UPDATE SKIP LOCKED;

  IF v_not_found OR v_action_id IS NULL THEN
    COMMIT;
    LEAVE claim_proc;
  END IF;

  UPDATE mmo_server_action_outbox
     SET status='claimed',
         locked_at=CURRENT_TIMESTAMP(6),
         result_payload=JSON_MERGE_PATCH(COALESCE(result_payload,JSON_OBJECT()), JSON_OBJECT('claimed_by',p_worker_id))
   WHERE action_id=v_action_id;

  SELECT action_id, action_kind, session_id, character_id, world_instance_id, target_key, idempotency_key, request_payload
    INTO p_action_id, p_action_kind, p_session_id, p_character_id, p_world_instance_id, p_target_key, p_idempotency_key, p_request_payload
    FROM mmo_server_action_outbox
   WHERE action_id=v_action_id
   LIMIT 1;

  COMMIT;
END$$
DELIMITER ;

DROP PROCEDURE IF EXISTS mmo_requeue_stale_claimed_actions;
DELIMITER $$
CREATE PROCEDURE mmo_requeue_stale_claimed_actions(
  IN  p_older_than_seconds INT,
  OUT p_requeued_count     INT
)
BEGIN
  DECLARE v_cutoff TIMESTAMP(6) DEFAULT NULL;
  SET p_requeued_count = 0;
  SET v_cutoff = TIMESTAMPADD(SECOND, -GREATEST(COALESCE(p_older_than_seconds,300),1), CURRENT_TIMESTAMP(6));

  UPDATE mmo_server_action_outbox
     SET status='pending',
         locked_at=NULL,
         last_error_code='stale_claim_requeued',
         last_error_message='Claimed action was requeued by mmo_requeue_stale_claimed_actions.'
   WHERE status='claimed'
     AND locked_at IS NOT NULL
     AND locked_at < v_cutoff
     AND attempt_count < max_attempts;

  SET p_requeued_count = ROW_COUNT();
END$$
DELIMITER ;

INSERT INTO mmo_schema_versions(migration_key, schema_contract, notes)
VALUES('production/mysql/019_server_action_dispatch_contract', 'gothic-mmo-server-action-dispatch-contract-v1-mysql', 'Server action dispatch registry, claim/requeue procedures and dispatch-gap views for outbox driven C++/RPC integration.')
ON DUPLICATE KEY UPDATE schema_contract=VALUES(schema_contract), notes=VALUES(notes), applied_at=CURRENT_TIMESTAMP(6);
