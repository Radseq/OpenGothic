-- Step116: typed DB/server authority foundation for NPC routine/AI/path/fight,
-- trigger queues, world transitions and client rollback/correction slices.
-- This is intentionally additive: old .sav and non-server-bound flows are unchanged.

CREATE TABLE IF NOT EXISTS mmo_npc_routine_state_history (
  event_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(255) NOT NULL,
  routine_state VARCHAR(64) NOT NULL,
  schedule_key VARCHAR(191) NULL,
  current_waypoint_key VARCHAR(191) NULL,
  target_waypoint_key VARCHAR(191) NULL,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  idempotency_key VARCHAR(512) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(event_id),
  UNIQUE KEY ux_mmo_npc_routine_state_history_idem(idempotency_key),
  KEY ix_mmo_npc_routine_state_history_key(world_instance_id,npc_entity_key,server_tick),
  CONSTRAINT mmo_npc_routine_state_history_payload_ck CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_routine_state_current (
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(255) NOT NULL,
  routine_state VARCHAR(64) NOT NULL,
  schedule_key VARCHAR(191) NULL,
  current_waypoint_key VARCHAR(191) NULL,
  target_waypoint_key VARCHAR(191) NULL,
  last_event_id BINARY(16) NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(world_instance_id,npc_entity_key),
  KEY ix_mmo_npc_routine_state_current_tick(world_instance_id,last_server_tick),
  CONSTRAINT mmo_npc_routine_state_current_payload_ck CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_ai_state_history (
  event_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(255) NOT NULL,
  ai_state VARCHAR(96) NOT NULL,
  ai_intent VARCHAR(96) NULL,
  target_key VARCHAR(255) NULL,
  perception_state VARCHAR(96) NULL,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  idempotency_key VARCHAR(512) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(event_id),
  UNIQUE KEY ux_mmo_npc_ai_state_history_idem(idempotency_key),
  KEY ix_mmo_npc_ai_state_history_key(world_instance_id,npc_entity_key,server_tick),
  CONSTRAINT mmo_npc_ai_state_history_payload_ck CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_ai_state_current (
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(255) NOT NULL,
  ai_state VARCHAR(96) NOT NULL,
  ai_intent VARCHAR(96) NULL,
  target_key VARCHAR(255) NULL,
  perception_state VARCHAR(96) NULL,
  last_event_id BINARY(16) NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(world_instance_id,npc_entity_key),
  KEY ix_mmo_npc_ai_state_current_tick(world_instance_id,last_server_tick),
  CONSTRAINT mmo_npc_ai_state_current_payload_ck CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_path_state_history (
  event_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(255) NOT NULL,
  path_state VARCHAR(64) NOT NULL,
  route_key VARCHAR(191) NULL,
  current_waypoint_key VARCHAR(191) NULL,
  next_waypoint_key VARCHAR(191) NULL,
  target_waypoint_key VARCHAR(191) NULL,
  pos_x DOUBLE NULL,
  pos_y DOUBLE NULL,
  pos_z DOUBLE NULL,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  idempotency_key VARCHAR(512) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(event_id),
  UNIQUE KEY ux_mmo_npc_path_state_history_idem(idempotency_key),
  KEY ix_mmo_npc_path_state_history_key(world_instance_id,npc_entity_key,server_tick),
  CONSTRAINT mmo_npc_path_state_history_payload_ck CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_path_state_current (
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(255) NOT NULL,
  path_state VARCHAR(64) NOT NULL,
  route_key VARCHAR(191) NULL,
  current_waypoint_key VARCHAR(191) NULL,
  next_waypoint_key VARCHAR(191) NULL,
  target_waypoint_key VARCHAR(191) NULL,
  pos_x DOUBLE NULL,
  pos_y DOUBLE NULL,
  pos_z DOUBLE NULL,
  last_event_id BINARY(16) NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(world_instance_id,npc_entity_key),
  KEY ix_mmo_npc_path_state_current_tick(world_instance_id,last_server_tick),
  CONSTRAINT mmo_npc_path_state_current_payload_ck CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_fight_state_history (
  event_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(255) NOT NULL,
  opponent_key VARCHAR(255) NULL,
  fight_state VARCHAR(64) NOT NULL,
  attack_state VARCHAR(96) NULL,
  combo_index INT NOT NULL DEFAULT 0,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  idempotency_key VARCHAR(512) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(event_id),
  UNIQUE KEY ux_mmo_npc_fight_state_history_idem(idempotency_key),
  KEY ix_mmo_npc_fight_state_history_key(world_instance_id,npc_entity_key,server_tick),
  CONSTRAINT mmo_npc_fight_state_history_payload_ck CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_npc_fight_state_current (
  world_instance_id BINARY(16) NOT NULL,
  npc_entity_key VARCHAR(255) NOT NULL,
  opponent_key VARCHAR(255) NULL,
  fight_state VARCHAR(64) NOT NULL,
  attack_state VARCHAR(96) NULL,
  combo_index INT NOT NULL DEFAULT 0,
  last_event_id BINARY(16) NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state_payload JSON NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(world_instance_id,npc_entity_key),
  KEY ix_mmo_npc_fight_state_current_tick(world_instance_id,last_server_tick),
  CONSTRAINT mmo_npc_fight_state_current_payload_ck CHECK (JSON_VALID(state_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_world_trigger_queue_history (
  event_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  trigger_key VARCHAR(255) NOT NULL,
  queue_state VARCHAR(64) NOT NULL,
  event_type_name VARCHAR(96) NOT NULL,
  scheduled_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  queue_payload JSON NOT NULL,
  idempotency_key VARCHAR(512) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(event_id),
  UNIQUE KEY ux_mmo_world_trigger_queue_history_idem(idempotency_key),
  KEY ix_mmo_world_trigger_queue_history_tick(world_instance_id,scheduled_server_tick,server_tick),
  CONSTRAINT mmo_world_trigger_queue_history_payload_ck CHECK (JSON_VALID(queue_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_world_trigger_queue_current (
  world_instance_id BINARY(16) NOT NULL,
  trigger_key VARCHAR(255) NOT NULL,
  queue_state VARCHAR(64) NOT NULL,
  event_type_name VARCHAR(96) NOT NULL,
  scheduled_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  last_event_id BINARY(16) NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  queue_payload JSON NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(world_instance_id,trigger_key,event_type_name),
  KEY ix_mmo_world_trigger_queue_current_tick(world_instance_id,scheduled_server_tick),
  CONSTRAINT mmo_world_trigger_queue_current_payload_ck CHECK (JSON_VALID(queue_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_character_world_transition_state_history (
  event_id BINARY(16) NOT NULL,
  character_id BINARY(16) NOT NULL,
  from_world_key VARCHAR(191) NULL,
  to_world_key VARCHAR(191) NOT NULL,
  transition_state VARCHAR(64) NOT NULL,
  chapter_key VARCHAR(96) NULL,
  visited BOOLEAN NOT NULL DEFAULT TRUE,
  server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  transition_payload JSON NOT NULL,
  idempotency_key VARCHAR(512) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(event_id),
  UNIQUE KEY ux_mmo_character_world_transition_history_idem(idempotency_key),
  KEY ix_mmo_character_world_transition_history_key(character_id,to_world_key,server_tick),
  CONSTRAINT mmo_character_world_transition_history_payload_ck CHECK (JSON_VALID(transition_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_character_world_transition_state_current (
  character_id BINARY(16) NOT NULL,
  from_world_key VARCHAR(191) NULL,
  to_world_key VARCHAR(191) NOT NULL,
  transition_state VARCHAR(64) NOT NULL,
  chapter_key VARCHAR(96) NULL,
  visited BOOLEAN NOT NULL DEFAULT TRUE,
  last_event_id BINARY(16) NULL,
  last_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  transition_payload JSON NOT NULL,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(character_id,to_world_key),
  KEY ix_mmo_character_world_transition_current_tick(character_id,last_server_tick),
  CONSTRAINT mmo_character_world_transition_current_payload_ck CHECK (JSON_VALID(transition_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_client_action_correction_history (
  correction_id BINARY(16) NOT NULL DEFAULT (UUID_TO_BIN(UUID(),1)),
  event_id BINARY(16) NOT NULL,
  session_id BINARY(16) NOT NULL,
  character_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  action_kind VARCHAR(96) NOT NULL,
  client_local_sequence BIGINT UNSIGNED NOT NULL DEFAULT 0,
  correction_kind VARCHAR(96) NOT NULL,
  reason VARCHAR(191) NOT NULL,
  rejected_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  authoritative_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  authoritative_pos_x DOUBLE NULL,
  authoritative_pos_y DOUBLE NULL,
  authoritative_pos_z DOUBLE NULL,
  authoritative_yaw DOUBLE NULL,
  correction_payload JSON NOT NULL,
  acknowledged BOOLEAN NOT NULL DEFAULT FALSE,
  idempotency_key VARCHAR(512) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(correction_id),
  UNIQUE KEY ux_mmo_client_action_correction_history_idem(idempotency_key),
  KEY ix_mmo_client_action_correction_history_session(session_id,acknowledged,created_at),
  CONSTRAINT mmo_client_action_correction_history_payload_ck CHECK (JSON_VALID(correction_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS mmo_client_action_correction_current (
  session_id BINARY(16) NOT NULL,
  character_id BINARY(16) NOT NULL,
  world_instance_id BINARY(16) NOT NULL,
  action_kind VARCHAR(96) NOT NULL,
  client_local_sequence BIGINT UNSIGNED NOT NULL DEFAULT 0,
  correction_kind VARCHAR(96) NOT NULL,
  reason VARCHAR(191) NOT NULL,
  rejected_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  authoritative_server_tick BIGINT UNSIGNED NOT NULL DEFAULT 0,
  authoritative_pos_x DOUBLE NULL,
  authoritative_pos_y DOUBLE NULL,
  authoritative_pos_z DOUBLE NULL,
  authoritative_yaw DOUBLE NULL,
  last_event_id BINARY(16) NULL,
  correction_payload JSON NOT NULL,
  acknowledged BOOLEAN NOT NULL DEFAULT FALSE,
  row_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(session_id,action_kind,client_local_sequence),
  KEY ix_mmo_client_action_correction_current_pending(session_id,acknowledged,updated_at),
  CONSTRAINT mmo_client_action_correction_current_payload_ck CHECK (JSON_VALID(correction_payload))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DELIMITER ;;

DROP PROCEDURE IF EXISTS mmo_record_npc_routine_state ;;
CREATE PROCEDURE mmo_record_npc_routine_state(
  IN p_session_id BINARY(16), IN p_npc_entity_key VARCHAR(255), IN p_routine_state VARCHAR(64),
  IN p_schedule_key VARCHAR(191), IN p_current_waypoint_key VARCHAR(191), IN p_target_waypoint_key VARCHAR(191),
  IN p_server_tick BIGINT, IN p_metadata JSON, IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16), OUT o_row_version_after BIGINT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16); DECLARE v_world_id BINARY(16); DECLARE v_character_id BINARY(16); DECLARE v_payload JSON;
  SELECT event_id INTO o_event_id FROM mmo_npc_routine_state_history WHERE idempotency_key=p_idempotency_key LIMIT 1;
  IF o_event_id IS NOT NULL THEN SELECT row_version INTO o_row_version_after FROM mmo_npc_routine_state_current WHERE world_instance_id=(SELECT world_instance_id FROM server_sessions WHERE session_id=p_session_id LIMIT 1) AND npc_entity_key=p_npc_entity_key; LEAVE proc; END IF;
  SELECT realm_id,world_instance_id,character_id INTO v_realm_id,v_world_id,v_character_id FROM server_sessions WHERE session_id=p_session_id LIMIT 1;
  IF v_world_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_npc_routine_state: invalid session'; END IF;
  SET v_payload=JSON_MERGE_PATCH(COALESCE(p_metadata,JSON_OBJECT()),JSON_OBJECT('npc_entity_key',p_npc_entity_key,'routine_state',p_routine_state,'schedule_key',p_schedule_key,'current_waypoint_key',p_current_waypoint_key,'target_waypoint_key',p_target_waypoint_key));
  CALL mmo_append_world_event(v_realm_id,v_world_id,v_character_id,'npc_routine_state_recorded','world_entity',COALESCE(p_server_tick,0),p_npc_entity_key,p_target_waypoint_key,v_payload,p_idempotency_key,'server',NULL,NULL,o_event_id);
  INSERT INTO mmo_npc_routine_state_history(event_id,world_instance_id,npc_entity_key,routine_state,schedule_key,current_waypoint_key,target_waypoint_key,server_tick,state_payload,idempotency_key)
  VALUES(o_event_id,v_world_id,p_npc_entity_key,COALESCE(p_routine_state,'unknown'),p_schedule_key,p_current_waypoint_key,p_target_waypoint_key,COALESCE(p_server_tick,0),v_payload,p_idempotency_key);
  INSERT INTO mmo_npc_routine_state_current(world_instance_id,npc_entity_key,routine_state,schedule_key,current_waypoint_key,target_waypoint_key,last_event_id,last_server_tick,state_payload,row_version)
  VALUES(v_world_id,p_npc_entity_key,COALESCE(p_routine_state,'unknown'),p_schedule_key,p_current_waypoint_key,p_target_waypoint_key,o_event_id,COALESCE(p_server_tick,0),v_payload,1)
  ON DUPLICATE KEY UPDATE routine_state=VALUES(routine_state),schedule_key=VALUES(schedule_key),current_waypoint_key=VALUES(current_waypoint_key),target_waypoint_key=VALUES(target_waypoint_key),last_event_id=VALUES(last_event_id),last_server_tick=VALUES(last_server_tick),state_payload=VALUES(state_payload),row_version=row_version+1;
  SELECT row_version INTO o_row_version_after FROM mmo_npc_routine_state_current WHERE world_instance_id=v_world_id AND npc_entity_key=p_npc_entity_key;
END ;;

DROP PROCEDURE IF EXISTS mmo_record_npc_ai_state ;;
CREATE PROCEDURE mmo_record_npc_ai_state(
  IN p_session_id BINARY(16), IN p_npc_entity_key VARCHAR(255), IN p_ai_state VARCHAR(96), IN p_ai_intent VARCHAR(96),
  IN p_target_key VARCHAR(255), IN p_perception_state VARCHAR(96), IN p_server_tick BIGINT, IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512), OUT o_event_id BINARY(16), OUT o_row_version_after BIGINT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16); DECLARE v_world_id BINARY(16); DECLARE v_character_id BINARY(16); DECLARE v_payload JSON;
  SELECT event_id INTO o_event_id FROM mmo_npc_ai_state_history WHERE idempotency_key=p_idempotency_key LIMIT 1;
  IF o_event_id IS NOT NULL THEN SELECT row_version INTO o_row_version_after FROM mmo_npc_ai_state_current WHERE world_instance_id=(SELECT world_instance_id FROM server_sessions WHERE session_id=p_session_id LIMIT 1) AND npc_entity_key=p_npc_entity_key; LEAVE proc; END IF;
  SELECT realm_id,world_instance_id,character_id INTO v_realm_id,v_world_id,v_character_id FROM server_sessions WHERE session_id=p_session_id LIMIT 1;
  IF v_world_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_npc_ai_state: invalid session'; END IF;
  SET v_payload=JSON_MERGE_PATCH(COALESCE(p_metadata,JSON_OBJECT()),JSON_OBJECT('npc_entity_key',p_npc_entity_key,'ai_state',p_ai_state,'ai_intent',p_ai_intent,'target_key',p_target_key,'perception_state',p_perception_state));
  CALL mmo_append_world_event(v_realm_id,v_world_id,v_character_id,'npc_ai_state_recorded','world_entity',COALESCE(p_server_tick,0),p_npc_entity_key,p_target_key,v_payload,p_idempotency_key,'server',NULL,NULL,o_event_id);
  INSERT INTO mmo_npc_ai_state_history(event_id,world_instance_id,npc_entity_key,ai_state,ai_intent,target_key,perception_state,server_tick,state_payload,idempotency_key)
  VALUES(o_event_id,v_world_id,p_npc_entity_key,COALESCE(p_ai_state,'unknown'),p_ai_intent,p_target_key,p_perception_state,COALESCE(p_server_tick,0),v_payload,p_idempotency_key);
  INSERT INTO mmo_npc_ai_state_current(world_instance_id,npc_entity_key,ai_state,ai_intent,target_key,perception_state,last_event_id,last_server_tick,state_payload,row_version)
  VALUES(v_world_id,p_npc_entity_key,COALESCE(p_ai_state,'unknown'),p_ai_intent,p_target_key,p_perception_state,o_event_id,COALESCE(p_server_tick,0),v_payload,1)
  ON DUPLICATE KEY UPDATE ai_state=VALUES(ai_state),ai_intent=VALUES(ai_intent),target_key=VALUES(target_key),perception_state=VALUES(perception_state),last_event_id=VALUES(last_event_id),last_server_tick=VALUES(last_server_tick),state_payload=VALUES(state_payload),row_version=row_version+1;
  SELECT row_version INTO o_row_version_after FROM mmo_npc_ai_state_current WHERE world_instance_id=v_world_id AND npc_entity_key=p_npc_entity_key;
END ;;

DROP PROCEDURE IF EXISTS mmo_record_npc_path_state ;;
CREATE PROCEDURE mmo_record_npc_path_state(
  IN p_session_id BINARY(16), IN p_npc_entity_key VARCHAR(255), IN p_path_state VARCHAR(64), IN p_route_key VARCHAR(191),
  IN p_current_waypoint_key VARCHAR(191), IN p_next_waypoint_key VARCHAR(191), IN p_target_waypoint_key VARCHAR(191),
  IN p_pos_x DOUBLE, IN p_pos_y DOUBLE, IN p_pos_z DOUBLE, IN p_server_tick BIGINT, IN p_metadata JSON,
  IN p_idempotency_key VARCHAR(512), OUT o_event_id BINARY(16), OUT o_row_version_after BIGINT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16); DECLARE v_world_id BINARY(16); DECLARE v_character_id BINARY(16); DECLARE v_payload JSON;
  SELECT event_id INTO o_event_id FROM mmo_npc_path_state_history WHERE idempotency_key=p_idempotency_key LIMIT 1;
  IF o_event_id IS NOT NULL THEN SELECT row_version INTO o_row_version_after FROM mmo_npc_path_state_current WHERE world_instance_id=(SELECT world_instance_id FROM server_sessions WHERE session_id=p_session_id LIMIT 1) AND npc_entity_key=p_npc_entity_key; LEAVE proc; END IF;
  SELECT realm_id,world_instance_id,character_id INTO v_realm_id,v_world_id,v_character_id FROM server_sessions WHERE session_id=p_session_id LIMIT 1;
  IF v_world_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_npc_path_state: invalid session'; END IF;
  SET v_payload=JSON_MERGE_PATCH(COALESCE(p_metadata,JSON_OBJECT()),JSON_OBJECT('npc_entity_key',p_npc_entity_key,'path_state',p_path_state,'route_key',p_route_key,'current_waypoint_key',p_current_waypoint_key,'next_waypoint_key',p_next_waypoint_key,'target_waypoint_key',p_target_waypoint_key,'pos_x',p_pos_x,'pos_y',p_pos_y,'pos_z',p_pos_z));
  CALL mmo_append_world_event(v_realm_id,v_world_id,v_character_id,'npc_path_state_recorded','world_entity',COALESCE(p_server_tick,0),p_npc_entity_key,p_target_waypoint_key,v_payload,p_idempotency_key,'server',NULL,NULL,o_event_id);
  INSERT INTO mmo_npc_path_state_history(event_id,world_instance_id,npc_entity_key,path_state,route_key,current_waypoint_key,next_waypoint_key,target_waypoint_key,pos_x,pos_y,pos_z,server_tick,state_payload,idempotency_key)
  VALUES(o_event_id,v_world_id,p_npc_entity_key,COALESCE(p_path_state,'unknown'),p_route_key,p_current_waypoint_key,p_next_waypoint_key,p_target_waypoint_key,p_pos_x,p_pos_y,p_pos_z,COALESCE(p_server_tick,0),v_payload,p_idempotency_key);
  INSERT INTO mmo_npc_path_state_current(world_instance_id,npc_entity_key,path_state,route_key,current_waypoint_key,next_waypoint_key,target_waypoint_key,pos_x,pos_y,pos_z,last_event_id,last_server_tick,state_payload,row_version)
  VALUES(v_world_id,p_npc_entity_key,COALESCE(p_path_state,'unknown'),p_route_key,p_current_waypoint_key,p_next_waypoint_key,p_target_waypoint_key,p_pos_x,p_pos_y,p_pos_z,o_event_id,COALESCE(p_server_tick,0),v_payload,1)
  ON DUPLICATE KEY UPDATE path_state=VALUES(path_state),route_key=VALUES(route_key),current_waypoint_key=VALUES(current_waypoint_key),next_waypoint_key=VALUES(next_waypoint_key),target_waypoint_key=VALUES(target_waypoint_key),pos_x=VALUES(pos_x),pos_y=VALUES(pos_y),pos_z=VALUES(pos_z),last_event_id=VALUES(last_event_id),last_server_tick=VALUES(last_server_tick),state_payload=VALUES(state_payload),row_version=row_version+1;
  SELECT row_version INTO o_row_version_after FROM mmo_npc_path_state_current WHERE world_instance_id=v_world_id AND npc_entity_key=p_npc_entity_key;
END ;;

DROP PROCEDURE IF EXISTS mmo_record_npc_fight_state ;;
CREATE PROCEDURE mmo_record_npc_fight_state(
  IN p_session_id BINARY(16), IN p_npc_entity_key VARCHAR(255), IN p_opponent_key VARCHAR(255), IN p_fight_state VARCHAR(64),
  IN p_attack_state VARCHAR(96), IN p_combo_index INT, IN p_server_tick BIGINT, IN p_metadata JSON, IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16), OUT o_row_version_after BIGINT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16); DECLARE v_world_id BINARY(16); DECLARE v_character_id BINARY(16); DECLARE v_payload JSON;
  SELECT event_id INTO o_event_id FROM mmo_npc_fight_state_history WHERE idempotency_key=p_idempotency_key LIMIT 1;
  IF o_event_id IS NOT NULL THEN SELECT row_version INTO o_row_version_after FROM mmo_npc_fight_state_current WHERE world_instance_id=(SELECT world_instance_id FROM server_sessions WHERE session_id=p_session_id LIMIT 1) AND npc_entity_key=p_npc_entity_key; LEAVE proc; END IF;
  SELECT realm_id,world_instance_id,character_id INTO v_realm_id,v_world_id,v_character_id FROM server_sessions WHERE session_id=p_session_id LIMIT 1;
  IF v_world_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_npc_fight_state: invalid session'; END IF;
  SET v_payload=JSON_MERGE_PATCH(COALESCE(p_metadata,JSON_OBJECT()),JSON_OBJECT('npc_entity_key',p_npc_entity_key,'opponent_key',p_opponent_key,'fight_state',p_fight_state,'attack_state',p_attack_state,'combo_index',COALESCE(p_combo_index,0)));
  CALL mmo_append_world_event(v_realm_id,v_world_id,v_character_id,'npc_fight_state_recorded','combat',COALESCE(p_server_tick,0),p_npc_entity_key,p_opponent_key,v_payload,p_idempotency_key,'server',NULL,NULL,o_event_id);
  INSERT INTO mmo_npc_fight_state_history(event_id,world_instance_id,npc_entity_key,opponent_key,fight_state,attack_state,combo_index,server_tick,state_payload,idempotency_key)
  VALUES(o_event_id,v_world_id,p_npc_entity_key,p_opponent_key,COALESCE(p_fight_state,'unknown'),p_attack_state,COALESCE(p_combo_index,0),COALESCE(p_server_tick,0),v_payload,p_idempotency_key);
  INSERT INTO mmo_npc_fight_state_current(world_instance_id,npc_entity_key,opponent_key,fight_state,attack_state,combo_index,last_event_id,last_server_tick,state_payload,row_version)
  VALUES(v_world_id,p_npc_entity_key,p_opponent_key,COALESCE(p_fight_state,'unknown'),p_attack_state,COALESCE(p_combo_index,0),o_event_id,COALESCE(p_server_tick,0),v_payload,1)
  ON DUPLICATE KEY UPDATE opponent_key=VALUES(opponent_key),fight_state=VALUES(fight_state),attack_state=VALUES(attack_state),combo_index=VALUES(combo_index),last_event_id=VALUES(last_event_id),last_server_tick=VALUES(last_server_tick),state_payload=VALUES(state_payload),row_version=row_version+1;
  SELECT row_version INTO o_row_version_after FROM mmo_npc_fight_state_current WHERE world_instance_id=v_world_id AND npc_entity_key=p_npc_entity_key;
END ;;

DROP PROCEDURE IF EXISTS mmo_record_trigger_queue_state ;;
CREATE PROCEDURE mmo_record_trigger_queue_state(
  IN p_session_id BINARY(16), IN p_trigger_key VARCHAR(255), IN p_queue_state VARCHAR(64), IN p_event_type_name VARCHAR(96),
  IN p_scheduled_server_tick BIGINT, IN p_server_tick BIGINT, IN p_metadata JSON, IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16), OUT o_row_version_after BIGINT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16); DECLARE v_world_id BINARY(16); DECLARE v_character_id BINARY(16); DECLARE v_payload JSON;
  SELECT event_id INTO o_event_id FROM mmo_world_trigger_queue_history WHERE idempotency_key=p_idempotency_key LIMIT 1;
  IF o_event_id IS NOT NULL THEN SELECT row_version INTO o_row_version_after FROM mmo_world_trigger_queue_current WHERE world_instance_id=(SELECT world_instance_id FROM server_sessions WHERE session_id=p_session_id LIMIT 1) AND trigger_key=p_trigger_key AND event_type_name=COALESCE(p_event_type_name,'trigger_event'); LEAVE proc; END IF;
  SELECT realm_id,world_instance_id,character_id INTO v_realm_id,v_world_id,v_character_id FROM server_sessions WHERE session_id=p_session_id LIMIT 1;
  IF v_world_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_trigger_queue_state: invalid session'; END IF;
  SET v_payload=JSON_MERGE_PATCH(COALESCE(p_metadata,JSON_OBJECT()),JSON_OBJECT('trigger_key',p_trigger_key,'queue_state',p_queue_state,'event_type_name',p_event_type_name,'scheduled_server_tick',COALESCE(p_scheduled_server_tick,0)));
  CALL mmo_append_world_event(v_realm_id,v_world_id,v_character_id,'trigger_queue_state_recorded','world_entity',COALESCE(p_server_tick,0),p_trigger_key,p_event_type_name,v_payload,p_idempotency_key,'server',NULL,NULL,o_event_id);
  INSERT INTO mmo_world_trigger_queue_history(event_id,world_instance_id,trigger_key,queue_state,event_type_name,scheduled_server_tick,server_tick,queue_payload,idempotency_key)
  VALUES(o_event_id,v_world_id,p_trigger_key,COALESCE(p_queue_state,'queued'),COALESCE(p_event_type_name,'trigger_event'),COALESCE(p_scheduled_server_tick,0),COALESCE(p_server_tick,0),v_payload,p_idempotency_key);
  INSERT INTO mmo_world_trigger_queue_current(world_instance_id,trigger_key,queue_state,event_type_name,scheduled_server_tick,last_event_id,last_server_tick,queue_payload,row_version)
  VALUES(v_world_id,p_trigger_key,COALESCE(p_queue_state,'queued'),COALESCE(p_event_type_name,'trigger_event'),COALESCE(p_scheduled_server_tick,0),o_event_id,COALESCE(p_server_tick,0),v_payload,1)
  ON DUPLICATE KEY UPDATE queue_state=VALUES(queue_state),scheduled_server_tick=VALUES(scheduled_server_tick),last_event_id=VALUES(last_event_id),last_server_tick=VALUES(last_server_tick),queue_payload=VALUES(queue_payload),row_version=row_version+1;
  SELECT row_version INTO o_row_version_after FROM mmo_world_trigger_queue_current WHERE world_instance_id=v_world_id AND trigger_key=p_trigger_key AND event_type_name=COALESCE(p_event_type_name,'trigger_event');
END ;;

DROP PROCEDURE IF EXISTS mmo_record_world_transition_state ;;
CREATE PROCEDURE mmo_record_world_transition_state(
  IN p_session_id BINARY(16), IN p_from_world_key VARCHAR(191), IN p_to_world_key VARCHAR(191), IN p_transition_state VARCHAR(64),
  IN p_chapter_key VARCHAR(96), IN p_visited BOOLEAN, IN p_server_tick BIGINT, IN p_metadata JSON, IN p_idempotency_key VARCHAR(512),
  OUT o_event_id BINARY(16), OUT o_row_version_after BIGINT
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16); DECLARE v_world_id BINARY(16); DECLARE v_character_id BINARY(16); DECLARE v_payload JSON; DECLARE v_to VARCHAR(191);
  SET v_to=COALESCE(NULLIF(p_to_world_key,''),'UNKNOWN');
  SELECT event_id INTO o_event_id FROM mmo_character_world_transition_state_history WHERE idempotency_key=p_idempotency_key LIMIT 1;
  IF o_event_id IS NOT NULL THEN SELECT row_version INTO o_row_version_after FROM mmo_character_world_transition_state_current WHERE character_id=(SELECT character_id FROM server_sessions WHERE session_id=p_session_id LIMIT 1) AND to_world_key=v_to; LEAVE proc; END IF;
  SELECT realm_id,world_instance_id,character_id INTO v_realm_id,v_world_id,v_character_id FROM server_sessions WHERE session_id=p_session_id LIMIT 1;
  IF v_character_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_world_transition_state: invalid session'; END IF;
  SET v_payload=JSON_MERGE_PATCH(COALESCE(p_metadata,JSON_OBJECT()),JSON_OBJECT('from_world_key',p_from_world_key,'to_world_key',v_to,'transition_state',p_transition_state,'chapter_key',p_chapter_key,'visited',COALESCE(p_visited,TRUE)));
  CALL mmo_append_world_event(v_realm_id,v_world_id,v_character_id,'world_transition_state_recorded','character',COALESCE(p_server_tick,0),v_to,p_chapter_key,v_payload,p_idempotency_key,'server',NULL,NULL,o_event_id);
  INSERT INTO mmo_character_world_transition_state_history(event_id,character_id,from_world_key,to_world_key,transition_state,chapter_key,visited,server_tick,transition_payload,idempotency_key)
  VALUES(o_event_id,v_character_id,p_from_world_key,v_to,COALESCE(p_transition_state,'visited'),p_chapter_key,COALESCE(p_visited,TRUE),COALESCE(p_server_tick,0),v_payload,p_idempotency_key);
  INSERT INTO mmo_character_world_transition_state_current(character_id,from_world_key,to_world_key,transition_state,chapter_key,visited,last_event_id,last_server_tick,transition_payload,row_version)
  VALUES(v_character_id,p_from_world_key,v_to,COALESCE(p_transition_state,'visited'),p_chapter_key,COALESCE(p_visited,TRUE),o_event_id,COALESCE(p_server_tick,0),v_payload,1)
  ON DUPLICATE KEY UPDATE from_world_key=VALUES(from_world_key),transition_state=VALUES(transition_state),chapter_key=VALUES(chapter_key),visited=VALUES(visited),last_event_id=VALUES(last_event_id),last_server_tick=VALUES(last_server_tick),transition_payload=VALUES(transition_payload),row_version=row_version+1;
  SELECT row_version INTO o_row_version_after FROM mmo_character_world_transition_state_current WHERE character_id=v_character_id AND to_world_key=v_to;
END ;;

DROP PROCEDURE IF EXISTS mmo_record_client_action_correction ;;
CREATE PROCEDURE mmo_record_client_action_correction(
  IN p_session_id BINARY(16), IN p_action_kind VARCHAR(96), IN p_client_local_sequence BIGINT,
  IN p_correction_kind VARCHAR(96), IN p_reason VARCHAR(191), IN p_rejected_server_tick BIGINT,
  IN p_metadata JSON, IN p_idempotency_key VARCHAR(512), OUT o_event_id BINARY(16), OUT o_correction_id BINARY(16)
)
proc: BEGIN
  DECLARE v_realm_id BINARY(16); DECLARE v_world_id BINARY(16); DECLARE v_character_id BINARY(16);
  DECLARE v_pos_x DOUBLE; DECLARE v_pos_y DOUBLE; DECLARE v_pos_z DOUBLE; DECLARE v_yaw DOUBLE; DECLARE v_auth_tick BIGINT UNSIGNED DEFAULT 0; DECLARE v_payload JSON;
  SELECT correction_id,event_id INTO o_correction_id,o_event_id FROM mmo_client_action_correction_history WHERE idempotency_key=p_idempotency_key LIMIT 1;
  IF o_correction_id IS NOT NULL THEN LEAVE proc; END IF;
  SELECT ss.realm_id,ss.world_instance_id,ss.character_id,cp.pos_x,cp.pos_y,cp.pos_z,cp.rotation_yaw,cp.server_tick
    INTO v_realm_id,v_world_id,v_character_id,v_pos_x,v_pos_y,v_pos_z,v_yaw,v_auth_tick
    FROM server_sessions ss LEFT JOIN character_positions cp ON cp.character_id=ss.character_id
   WHERE ss.session_id=p_session_id LIMIT 1;
  IF v_character_id IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='mmo_record_client_action_correction: invalid session'; END IF;
  SET v_payload=JSON_MERGE_PATCH(COALESCE(p_metadata,JSON_OBJECT()),JSON_OBJECT('action_kind',p_action_kind,'client_local_sequence',COALESCE(p_client_local_sequence,0),'correction_kind',p_correction_kind,'reason',p_reason,'authoritative_pos_x',v_pos_x,'authoritative_pos_y',v_pos_y,'authoritative_pos_z',v_pos_z,'authoritative_yaw',v_yaw,'authoritative_server_tick',v_auth_tick));
  CALL mmo_append_world_event(v_realm_id,v_world_id,v_character_id,'client_action_correction_recorded','diagnostic',COALESCE(p_rejected_server_tick,0),p_action_kind,CAST(COALESCE(p_client_local_sequence,0) AS CHAR),v_payload,p_idempotency_key,'server',NULL,NULL,o_event_id);
  INSERT INTO mmo_client_action_correction_history(event_id,session_id,character_id,world_instance_id,action_kind,client_local_sequence,correction_kind,reason,rejected_server_tick,authoritative_server_tick,authoritative_pos_x,authoritative_pos_y,authoritative_pos_z,authoritative_yaw,correction_payload,idempotency_key)
  VALUES(o_event_id,p_session_id,v_character_id,v_world_id,COALESCE(p_action_kind,'unknown'),COALESCE(p_client_local_sequence,0),COALESCE(p_correction_kind,'rollback_to_authoritative_position'),COALESCE(p_reason,'rejected'),COALESCE(p_rejected_server_tick,0),COALESCE(v_auth_tick,0),v_pos_x,v_pos_y,v_pos_z,v_yaw,v_payload,p_idempotency_key);
  SET o_correction_id=UUID_TO_BIN(UUID(),1);
  SELECT correction_id INTO o_correction_id FROM mmo_client_action_correction_history WHERE event_id=o_event_id LIMIT 1;
  INSERT INTO mmo_client_action_correction_current(session_id,character_id,world_instance_id,action_kind,client_local_sequence,correction_kind,reason,rejected_server_tick,authoritative_server_tick,authoritative_pos_x,authoritative_pos_y,authoritative_pos_z,authoritative_yaw,last_event_id,correction_payload,acknowledged,row_version)
  VALUES(p_session_id,v_character_id,v_world_id,COALESCE(p_action_kind,'unknown'),COALESCE(p_client_local_sequence,0),COALESCE(p_correction_kind,'rollback_to_authoritative_position'),COALESCE(p_reason,'rejected'),COALESCE(p_rejected_server_tick,0),COALESCE(v_auth_tick,0),v_pos_x,v_pos_y,v_pos_z,v_yaw,o_event_id,v_payload,FALSE,1)
  ON DUPLICATE KEY UPDATE correction_kind=VALUES(correction_kind),reason=VALUES(reason),rejected_server_tick=VALUES(rejected_server_tick),authoritative_server_tick=VALUES(authoritative_server_tick),authoritative_pos_x=VALUES(authoritative_pos_x),authoritative_pos_y=VALUES(authoritative_pos_y),authoritative_pos_z=VALUES(authoritative_pos_z),authoritative_yaw=VALUES(authoritative_yaw),last_event_id=VALUES(last_event_id),correction_payload=VALUES(correction_payload),acknowledged=FALSE,row_version=row_version+1;
END ;;

DROP PROCEDURE IF EXISTS mmo_ack_client_action_correction ;;
CREATE PROCEDURE mmo_ack_client_action_correction(
  IN p_session_id BINARY(16), IN p_action_kind VARCHAR(96), IN p_client_local_sequence BIGINT,
  IN p_server_tick BIGINT, IN p_metadata JSON, IN p_idempotency_key VARCHAR(512), OUT o_row_version_after BIGINT
)
BEGIN
  UPDATE mmo_client_action_correction_current
     SET acknowledged=TRUE,
         correction_payload=JSON_MERGE_PATCH(COALESCE(correction_payload,JSON_OBJECT()),JSON_OBJECT('ack_server_tick',COALESCE(p_server_tick,0),'ack_metadata',COALESCE(p_metadata,JSON_OBJECT()),'ack_idempotency_key',p_idempotency_key)),
         row_version=row_version+1
   WHERE session_id=p_session_id
     AND action_kind=COALESCE(p_action_kind,action_kind)
     AND client_local_sequence=COALESCE(p_client_local_sequence,client_local_sequence);
  SELECT COALESCE(MAX(row_version),0) INTO o_row_version_after
    FROM mmo_client_action_correction_current
   WHERE session_id=p_session_id
     AND action_kind=COALESCE(p_action_kind,action_kind)
     AND client_local_sequence=COALESCE(p_client_local_sequence,client_local_sequence);
END ;;

DELIMITER ;
