// Internal implementation partition for mmo_udp_server.cpp.
// Handles triggers, movers, NPC observations and world transition direct DB actions.

[[nodiscard]] Mmo::Server::NpcAction::ActionCommand buildNpcActionCommandFromPayload(
    std::string_view payload,
    std::string_view fallbackTargetKey,
    std::uint64_t tick) {
  return Mmo::Server::NpcAction::buildActionCommand({
    .actorKey = optionalJsonString(payload, "actor_key",
                optionalJsonString(payload, "npc_entity_key",
                optionalJsonString(payload, "target_key", std::string(fallbackTargetKey)))),
    .actionKey = optionalJsonString(payload, "action_key", optionalJsonString(payload, "action_name", "unknown")),
    .actionState = optionalJsonString(payload, "action_state", optionalJsonString(payload, "state", "active")),
    .targetKey = optionalJsonString(payload, "action_target_key", optionalJsonString(payload, "target_entity_key")),
    .syncGroup = optionalJsonString(payload, "sync_group", optionalJsonString(payload, "conversation_key")),
    .serverTick = tick,
  });
}

struct ClaimedNpcActionRequest final {
  std::string actionUuid;
  std::string targetKey;
  std::string idempotencyKey;
  std::string payloadJson;
};

struct NpcActionWorkerResult final {
  bool claimed = false;
  bool applied = false;
  bool failed = false;
  bool retryable = false;
  const char* status = "idle";
  std::string actionUuid;
  std::string actorKey;
  std::string actionKey;
  std::string targetKey;
  std::string reason;
};

[[nodiscard]] std::string buildNpcActionWorkerResultJson(std::string_view status,
                                                         std::string_view actionUuid,
                                                         std::string_view actorKey,
                                                         std::string_view actionKey,
                                                         std::string_view targetKey,
                                                         std::string_view reason) {
  std::string out;
  out.reserve(256 + actionUuid.size() + actorKey.size() + actionKey.size() + targetKey.size() + reason.size());
  out += "{\"schema\":\"mmo.npc_action_worker_result.v1\"";
  out += ",\"status\":";
  appendPerceptionJsonString(out, status);
  out += ",\"action_uuid\":";
  appendPerceptionJsonString(out, actionUuid);
  out += ",\"actor_key\":";
  appendPerceptionJsonString(out, actorKey);
  out += ",\"action_key\":";
  appendPerceptionJsonString(out, actionKey);
  out += ",\"target_key\":";
  appendPerceptionJsonString(out, targetKey);
  out += ",\"reason\":";
  appendPerceptionJsonString(out, reason);
  out += "}";
  return out;
}

[[nodiscard]] std::optional<ClaimedNpcActionRequest> claimNextNpcActionRequest(const MySqlTarget& target) {
  std::string sql;
  sql += "SET @mmo_npc_action_id=NULL;";
  sql += "SET @mmo_npc_action_target=NULL;";
  sql += "SET @mmo_npc_action_idem=NULL;";
  sql += "SET @mmo_npc_action_payload=NULL;";
  sql += "START TRANSACTION;";
  sql += "SELECT action_id,target_key,idempotency_key,CAST(request_payload AS CHAR) ";
  sql += "INTO @mmo_npc_action_id,@mmo_npc_action_target,@mmo_npc_action_idem,@mmo_npc_action_payload ";
  sql += "FROM mmo_server_action_outbox ";
  sql += "WHERE status='pending' AND action_kind='npc_action_request' ";
  sql += "AND (next_attempt_at IS NULL OR next_attempt_at <= CURRENT_TIMESTAMP(6)) ";
  sql += "ORDER BY priority ASC, requested_at ASC, action_id ASC ";
  sql += "LIMIT 1 FOR UPDATE SKIP LOCKED;";
  sql += "UPDATE mmo_server_action_outbox ";
  sql += "SET status='claimed',attempt_count=attempt_count+1,locked_at=CURRENT_TIMESTAMP(6),";
  sql += "result_payload=JSON_MERGE_PATCH(COALESCE(result_payload, JSON_OBJECT()), ";
  sql += "JSON_OBJECT('claimed_by','mmo_udp_server:npc_action_request')) ";
  sql += "WHERE action_id=@mmo_npc_action_id;";
  sql += "COMMIT;";
  sql += "SELECT CONCAT(COALESCE(BIN_TO_UUID(@mmo_npc_action_id,1),''),'\\t',";
  sql += "COALESCE(@mmo_npc_action_target,''),'\\t',";
  sql += "COALESCE(@mmo_npc_action_idem,''),'\\t',";
  sql += "COALESCE(@mmo_npc_action_payload,''));";

  const auto parts = splitMysqlLastRow(runMysql(target, sql));
  if(parts.empty() || parts.front().empty())
    return std::nullopt;

  ClaimedNpcActionRequest out;
  out.actionUuid = parts.size() > 0 ? parts[0] : std::string();
  out.targetKey = parts.size() > 1 ? parts[1] : std::string();
  out.idempotencyKey = parts.size() > 2 ? parts[2] : std::string();
  out.payloadJson = parts.size() > 3 ? parts[3] : std::string();
  if(out.actionUuid.empty())
    return std::nullopt;
  return out;
}

[[nodiscard]] std::string ensureNpcActionWorkerRun(const MySqlTarget& target,
                                                   std::string_view sessionUuid) {
  std::string metadata;
  metadata.reserve(96 + sessionUuid.size());
  metadata += "{\"schema\":\"mmo.npc_action_worker_run.v1\",\"session_uuid\":";
  appendPerceptionJsonString(metadata, sessionUuid);
  metadata += "}";

  std::string sql;
  sql += "SET @mmo_npc_worker_run_id=NULL;";
  sql += "CALL mmo_start_server_action_worker_run(";
  sql += sqlLiteral("mmo_udp_server");
  sql += ",";
  sql += sqlLiteral("mmo_udp_server:npc_action_request");
  sql += ",";
  sql += sqlLiteral("inline_npc_action_worker");
  sql += ",";
  sql += sqlJson(metadata);
  sql += ",@mmo_npc_worker_run_id);";
  sql += "SELECT COALESCE(BIN_TO_UUID(@mmo_npc_worker_run_id,1),'');";
  return mysqlSingleField(target, sql);
}

void recordNpcActionWorkerResult(const MySqlTarget& target,
                                 std::string_view workerRunUuid,
                                 const ClaimedNpcActionRequest& request,
                                 std::string_view status,
                                 std::string_view errorCode,
                                 std::string_view errorMessage,
                                 std::string_view detailsJson) {
  std::string sql;
  sql += "CALL mmo_record_server_action_worker_result(";
  sql += "UUID_TO_BIN(" + sqlLiteral(workerRunUuid) + ",1),";
  sql += "UUID_TO_BIN(" + sqlLiteral(request.actionUuid) + ",1),";
  sql += sqlLiteral("npc_action_request");
  sql += ",";
  sql += sqlLiteral(status);
  sql += ",NULL,";
  sql += errorCode.empty() ? std::string("NULL") : sqlLiteral(errorCode);
  sql += ",";
  sql += errorMessage.empty() ? std::string("NULL") : sqlLiteral(errorMessage);
  sql += ",";
  sql += sqlJson(detailsJson);
  sql += ");";
  (void)runMysql(target, sql);
}

void markNpcActionWorkerApplied(const MySqlTarget& target,
                                const ClaimedNpcActionRequest& request,
                                std::string_view resultJson) {
  std::string sql;
  sql += "SET @mmo_npc_action_status=NULL;";
  sql += "CALL mmo_mark_server_action_applied(UUID_TO_BIN(";
  sql += sqlLiteral(request.actionUuid);
  sql += ",1),NULL,";
  sql += sqlJson(resultJson);
  sql += ",@mmo_npc_action_status);";
  (void)runMysql(target, sql);
}

void markNpcActionWorkerFailed(const MySqlTarget& target,
                               const ClaimedNpcActionRequest& request,
                               std::string_view errorCode,
                               std::string_view errorMessage,
                               bool retryable) {
  std::string sql;
  sql += "SET @mmo_npc_action_status=NULL;";
  sql += "CALL mmo_mark_server_action_failed(UUID_TO_BIN(";
  sql += sqlLiteral(request.actionUuid);
  sql += ",1),";
  sql += sqlLiteral(errorCode);
  sql += ",";
  sql += sqlLiteral(errorMessage);
  sql += ",";
  sql += sqlBool(retryable);
  sql += ",@mmo_npc_action_status);";
  (void)runMysql(target, sql);
}

[[nodiscard]] NpcActionWorkerResult runNpcActionRequestWorkerOnce(const MySqlTarget& target,
                                                                  std::string_view sessionUuid) {
  const auto request = claimNextNpcActionRequest(target);
  if(!request)
    return {};

  NpcActionWorkerResult result;
  result.claimed = true;
  result.actionUuid = request->actionUuid;
  result.targetKey = request->targetKey;

  try {
    const auto workerRunUuid = ensureNpcActionWorkerRun(target, sessionUuid);
    if(workerRunUuid.empty())
      throw std::runtime_error("npc_action_worker_run_missing");
    const auto tick = static_cast<std::uint64_t>(std::max<std::int64_t>(
      0, optionalJsonI64(request->payloadJson, "server_tick", 0)));
    const auto command = buildNpcActionCommandFromPayload(request->payloadJson, request->targetKey, tick);
    result.actorKey = command.actorKey;
    result.actionKey = command.actionKey;
    result.targetKey = command.targetKey;
    if(!command.shouldPersist) {
      result.failed = true;
      result.status = "failed";
      result.reason = command.reason;
      const auto details = buildNpcActionWorkerResultJson("failed", request->actionUuid, command.actorKey,
                                                         command.actionKey, command.targetKey, command.reason);
      recordNpcActionWorkerResult(target, workerRunUuid, *request, "failed",
                                  command.reason, command.reason, details);
      markNpcActionWorkerFailed(target, *request, command.reason, command.reason, false);
      return result;
    }

    const auto activity = applyNpcActivityCommand(command.actorKey,
                                                 command.actionKey,
                                                 command.actionState,
                                                 command.targetKey,
                                                 command.syncGroup,
                                                 tick,
                                                 0,
                                                 "npc_action_worker_activity_rejected");
    if(activity.accepted) {
      result.applied = true;
      result.status = "applied";
      result.reason = activity.reason;
      const auto details = buildNpcActionWorkerResultJson("applied", request->actionUuid, command.actorKey,
                                                         command.actionKey, command.targetKey, activity.reason);
      recordNpcActionWorkerResult(target, workerRunUuid, *request, "applied", {}, {}, details);
      markNpcActionWorkerApplied(target, *request, details);
    } else {
      result.failed = true;
      result.retryable = true;
      result.status = "failed";
      result.reason = activity.reason;
      const auto details = buildNpcActionWorkerResultJson("failed", request->actionUuid, command.actorKey,
                                                         command.actionKey, command.targetKey, activity.reason);
      recordNpcActionWorkerResult(target, workerRunUuid, *request, "failed",
                                  activity.reason, activity.reason, details);
      markNpcActionWorkerFailed(target, *request, activity.reason, activity.reason, true);
    }
    return result;
  } catch(const std::exception& error) {
    result.failed = true;
    result.retryable = true;
    result.status = "failed";
    result.reason = error.what();
    try {
      markNpcActionWorkerFailed(target, *request, "npc_action_worker_exception", error.what(), true);
    } catch(const std::exception& markError) {
      std::cerr << "[npc_action_worker_mark_failed_failed]"
                << " action_uuid=" << request->actionUuid
                << " error=" << markError.what()
                << "\n";
    }
    return result;
  }
}

[[nodiscard]] DirectApplyResult applyWorldStateDirectDb(const Mmo::Server::DirectApplyRequest& request) {
  const MySqlTarget& target = request.target;
  const std::string_view sessionUuid = request.sessionUuid;
  const Mmo::Net::ClientActionPacket& packet = request.packet;
  const std::string_view dbPayload = request.dbPayload;
  const std::string_view payload = packet.payloadJson;
  const auto tick = packetServerTick(packet);

  if(packet.kind == Mmo::SemanticActionKind::TriggerEvent) {
    const auto triggerKey = optionalJsonString(payload, "trigger_key", optionalJsonString(payload, "target_key", packet.targetKey));
    const auto eventTypeName = optionalJsonString(payload, "event_type_name", optionalJsonString(payload, "reason", "trigger_event"));
    const auto validation = Mmo::Server::Gameplay::validateTriggerEvent({
      .key = triggerKey,
      .eventTypeName = eventTypeName,
    });
    if(!validation.accepted)
      return {true, false, false, validation.reason};
    Mmo::Server::recordTriggerEvent(target, {
      .sessionUuid = sessionUuid,
      .triggerKey = triggerKey,
      .eventTypeName = eventTypeName,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  if(packet.kind == Mmo::SemanticActionKind::MoverStateChanged) {
    const auto moverKey = optionalJsonString(payload, "mover_key", optionalJsonString(payload, "target_key", packet.targetKey));
    const auto stateBefore = optionalJsonI64(payload, "state_before", 0);
    const auto stateAfter = optionalJsonI64(payload, "state_after", stateBefore);
    const auto stateAfterName = optionalJsonString(payload, "state_after_name", "");
    const auto frame = optionalJsonI64(payload, "frame", optionalJsonI64(payload, "frame_index", 0));
    const auto targetFrame = optionalJsonI64(payload, "target_frame", optionalJsonI64(payload, "target_frame_index", frame));
    const auto validation = Mmo::Server::Gameplay::validateMoverState({
      .key = moverKey,
      .stateBefore = stateBefore,
      .stateAfter = stateAfter,
      .stateAfterName = stateAfterName,
      .frameIndex = frame,
      .targetFrameIndex = targetFrame,
    });
    if(!validation.accepted)
      return {true, false, false, validation.reason};
    Mmo::Server::recordMoverState(target, {
      .sessionUuid = sessionUuid,
      .moverKey = moverKey,
      .stateBefore = stateBefore,
      .stateAfter = stateAfter,
      .stateAfterName = stateAfterName,
      .frame = frame,
      .targetFrame = targetFrame,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  if(packet.kind == Mmo::SemanticActionKind::RecordNpcRoutineState) {
    const auto npcKey = optionalJsonString(payload, "npc_entity_key",
                        optionalJsonString(payload, "actor_npc_key",
                        optionalJsonString(payload, "target_key", packet.targetKey)));
    const auto command = Mmo::Server::Waypoint::buildNpcRoutineCommand({
      .npcKey = npcKey,
      .routineState = optionalJsonString(payload, "routine_state", "unknown"),
      .scheduleKey = optionalJsonString(payload, "schedule_key", optionalJsonString(payload, "routine_key")),
      .currentWaypoint = {
        .key = optionalJsonString(payload, "current_waypoint_key"),
        .name = optionalJsonString(payload, "current_waypoint_name"),
        .legacy = optionalJsonString(payload, "current_waypoint"),
      },
      .targetWaypoint = {
        .key = optionalJsonString(payload, "target_waypoint_key"),
        .name = optionalJsonString(payload, "target_waypoint_name"),
        .legacy = optionalJsonString(payload, "target_waypoint"),
      },
    });
    if(!command.shouldPersist)
      return ignoreNpcObservation("record_npc_routine_state", packet.targetKey, {
        .accepted = command.accepted,
        .shouldPersist = command.shouldPersist,
        .reason = command.reason,
      });
    Mmo::Server::recordNpcRoutineState(target, {
      .sessionUuid = sessionUuid,
      .npcKey = command.npcKey,
      .routineState = command.routineState,
      .scheduleKey = command.scheduleKey,
      .currentWaypoint = command.currentWaypoint,
      .targetWaypoint = command.targetWaypoint,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  if(packet.kind == Mmo::SemanticActionKind::RecordNpcAiState) {
    const auto npcKey = optionalJsonString(payload, "npc_entity_key",
                        optionalJsonString(payload, "actor_npc_key",
                        optionalJsonString(payload, "target_key", packet.targetKey)));
    const auto aiState = optionalJsonString(payload, "ai_state", optionalJsonString(payload, "ai_state_name", "unknown"));
    const auto aiIntent = optionalJsonString(payload, "ai_intent", optionalJsonString(payload, "intent", ""));
    const auto targetEntity = optionalJsonString(payload, "ai_target_key",
                            optionalJsonString(payload, "target_entity_key",
                            optionalJsonString(payload, "target_key", "")));
    const auto perceptionState = optionalJsonString(payload, "perception_state", "");
    const auto validation = Mmo::Server::Gameplay::validateNpcAiObservation({
      .npcKey = npcKey,
      .aiState = aiState,
      .aiIntent = aiIntent,
      .targetKey = targetEntity,
      .perceptionState = perceptionState,
    });
    if(!validation.shouldPersist)
      return ignoreNpcObservation("record_npc_ai_state", packet.targetKey, validation);
    Mmo::Server::recordNpcAiState(target, {
      .sessionUuid = sessionUuid,
      .npcKey = npcKey,
      .aiState = aiState,
      .aiIntent = aiIntent,
      .targetEntity = targetEntity,
      .perceptionState = perceptionState,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  if(packet.kind == Mmo::SemanticActionKind::RecordNpcPathState) {
    const auto npcKey = optionalJsonString(payload, "npc_entity_key",
                        optionalJsonString(payload, "actor_npc_key",
                        optionalJsonString(payload, "target_key", packet.targetKey)));
    const auto position = optionalFlatGameplayVec3(payload, "pos_x", "pos_y", "pos_z");
    const auto command = Mmo::Server::Waypoint::buildNpcPathCommand({
      .npcKey = npcKey,
      .pathState = optionalJsonString(payload, "path_state", "unknown"),
      .routeKey = optionalJsonString(payload, "route_key", ""),
      .currentWaypoint = {
        .key = optionalJsonString(payload, "current_waypoint_key"),
        .name = optionalJsonString(payload, "current_waypoint_name"),
        .legacy = optionalJsonString(payload, "current_waypoint"),
      },
      .nextWaypoint = {
        .key = optionalJsonString(payload, "next_waypoint_key"),
        .name = optionalJsonString(payload, "next_waypoint_name"),
        .legacy = optionalJsonString(payload, "next_waypoint"),
      },
      .targetWaypoint = {
        .key = optionalJsonString(payload, "target_waypoint_key"),
        .name = optionalJsonString(payload, "target_waypoint_name"),
        .legacy = optionalJsonString(payload, "target_waypoint"),
      },
      .position = position,
    });
    if(!command.shouldPersist)
      return ignoreNpcObservation("record_npc_path_state", packet.targetKey, {
        .accepted = command.accepted,
        .shouldPersist = command.shouldPersist,
        .reason = command.reason,
      });
    if(command.position) {
      std::optional<double> yawRad;
      if(jsonNumberTextField(payload, "rotation_yaw").has_value())
        yawRad = optionalJsonDouble(payload, "rotation_yaw", 0.0);
      else if(jsonNumberTextField(payload, "yaw").has_value())
        yawRad = optionalJsonDouble(payload, "yaw", 0.0);
      (void)gPerceptionWitnessRegistry.observe({
        .npcKey = command.npcKey,
        .position = *command.position,
        .yawRad = yawRad,
        .serverTickMs = tick,
      });
    }
    Mmo::Server::recordNpcPathState(target, {
      .sessionUuid = sessionUuid,
      .npcKey = command.npcKey,
      .pathState = command.pathState,
      .routeKey = command.routeKey,
      .currentWaypoint = command.currentWaypoint,
      .nextWaypoint = command.nextWaypoint,
      .targetWaypoint = command.targetWaypoint,
      .posX = command.position ? std::optional<double>(command.position->x) : std::nullopt,
      .posY = command.position ? std::optional<double>(command.position->y) : std::nullopt,
      .posZ = command.position ? std::optional<double>(command.position->z) : std::nullopt,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  if(packet.kind == Mmo::SemanticActionKind::RecordNpcFightState) {
    gCombatTimelineRegistry.expire(tick);
    const auto npcKey = optionalJsonString(payload, "npc_entity_key",
                        optionalJsonString(payload, "actor_npc_key",
                        optionalJsonString(payload, "target_key", packet.targetKey)));
    const auto opponentKey = optionalJsonString(payload, "opponent_key", optionalJsonString(payload, "target_entity_key"));
    const auto fightState = optionalJsonString(payload, "fight_state", "unknown");
    const auto attackState = optionalJsonString(payload, "attack_state", "");
    const auto comboIndex = optionalJsonI64(payload, "combo_index", 0);
    const auto bodyState = optionalJsonI64(payload, "body_state", 0);
    const auto weaponStateId = optionalJsonI64(payload, "weapon_state_id", 0);
    const auto weaponState = optionalJsonString(payload, "weapon_state", "");
    const auto attackerCenter = optionalJsonVec3(payload, "attacker_center");
    const auto opponentCenter = optionalJsonVec3(payload, "opponent_center");
    const auto toGameplayVec3 = [](const JsonVec3& value) noexcept {
      return Mmo::Server::Gameplay::Vec3{value.x, value.y, value.z};
    };
    const auto validation = Mmo::Server::Gameplay::validateNpcFightObservation({
      .npcKey = npcKey,
      .opponentKey = opponentKey,
      .fightState = fightState,
      .attackState = attackState,
      .comboIndex = comboIndex,
    });
    if(!validation.shouldPersist)
      return ignoreNpcObservation("record_npc_fight_state", packet.targetKey, validation);
    if(attackerCenter) {
      (void)gPerceptionWitnessRegistry.observe({
        .npcKey = npcKey,
        .position = toGameplayVec3(*attackerCenter),
        .yawRad = optionalJsonDouble(payload, "attacker_yaw_rad", 0.0),
        .down = optionalJsonBool(payload, "down", false) ||
                optionalJsonBool(payload, "unconscious", false),
        .dead = optionalJsonBool(payload, "dead", false),
        .serverTickMs = tick,
      });
    }
    const auto timeline = gCombatTimelineRegistry.applyObservedFight({
      .actorKey = npcKey,
      .opponentKey = opponentKey,
      .fightState = fightState,
      .attackState = attackState,
      .weaponState = weaponState,
      .animationName = optionalJsonString(payload, "animation_name", ""),
      .attackAnimationName = optionalJsonString(payload, "attack_animation_name", ""),
      .serverTickMs = tick,
      .animationElapsedMs = static_cast<std::uint64_t>(std::max<std::int64_t>(0, optionalJsonI64(payload, "animation_elapsed_ms", 0))),
      .attackAnimationElapsedMs = static_cast<std::uint64_t>(std::max<std::int64_t>(0, optionalJsonI64(payload, "attack_animation_elapsed_ms", 0))),
      .animationTotalMs = static_cast<std::uint64_t>(std::max<std::int64_t>(0, optionalJsonI64(payload, "animation_total_ms", 0))),
      .attackTotalMs = static_cast<std::uint64_t>(std::max<std::int64_t>(0, optionalJsonI64(payload, "attack_total_ms", 0))),
      .attackOptimalMs = static_cast<std::uint64_t>(std::max<std::int64_t>(0, optionalJsonI64(payload, "attack_optimal_ms", 0))),
      .attackHitEndMs = static_cast<std::uint64_t>(std::max<std::int64_t>(0, optionalJsonI64(payload, "attack_hit_end_ms", 0))),
      .parryWindowStartMs = static_cast<std::uint64_t>(std::max<std::int64_t>(0, optionalJsonI64(payload, "parry_window_start_ms", 0))),
      .parryWindowEndMs = static_cast<std::uint64_t>(std::max<std::int64_t>(0, optionalJsonI64(payload, "parry_window_end_ms", 0))),
      .comboWindowStartMs = static_cast<std::uint64_t>(std::max<std::int64_t>(0, optionalJsonI64(payload, "combo_window_start_ms", 0))),
      .comboWindowEndMs = static_cast<std::uint64_t>(std::max<std::int64_t>(0, optionalJsonI64(payload, "combo_window_end_ms", 0))),
      .attackerCenter = attackerCenter ? toGameplayVec3(*attackerCenter) : Mmo::Server::Gameplay::Vec3{},
      .opponentCenter = opponentCenter ? toGameplayVec3(*opponentCenter) : Mmo::Server::Gameplay::Vec3{},
      .attackerYawRad = optionalJsonDouble(payload, "attacker_yaw_rad", 0.0),
      .opponentYawRad = optionalJsonDouble(payload, "opponent_yaw_rad", 0.0),
      .attackRange = optionalJsonDouble(payload, "attack_range", 0.0),
      .opponentAttackRange = optionalJsonDouble(payload, "opponent_attack_range", 0.0),
      .attackerBaseRange = optionalJsonDouble(payload, "attacker_fight_range_base", 0.0),
      .opponentBaseRange = optionalJsonDouble(payload, "opponent_fight_range_base", 0.0),
      .weaponRange = optionalJsonDouble(payload, "weapon_range", 0.0),
      .hasAttackerCenter = attackerCenter.has_value(),
      .hasOpponentCenter = opponentCenter.has_value(),
      .hasAttackerYaw = jsonNumberTextField(payload, "attacker_yaw_rad").has_value(),
      .hasOpponentYaw = jsonNumberTextField(payload, "opponent_yaw_rad").has_value(),
      .hasAttackRange = jsonNumberTextField(payload, "attack_range").has_value(),
      .hasOpponentAttackRange = jsonNumberTextField(payload, "opponent_attack_range").has_value(),
      .hasAttackerBaseRange = jsonNumberTextField(payload, "attacker_fight_range_base").has_value(),
      .hasOpponentBaseRange = jsonNumberTextField(payload, "opponent_fight_range_base").has_value(),
      .hasWeaponRange = jsonNumberTextField(payload, "weapon_range").has_value(),
      .actorRunning = optionalJsonBool(payload, "actor_running", false),
      .opponentRunning = optionalJsonBool(payload, "opponent_running", false),
      .opponentPrehit = optionalJsonBool(payload, "opponent_prehit", false),
      .comboIndex = comboIndex,
      .bodyState = bodyState,
      .weaponStateId = weaponStateId,
      .attackAnim = optionalJsonBool(payload, "attack_anim", false),
      .prehit = optionalJsonBool(payload, "prehit", false),
      .down = optionalJsonBool(payload, "down", false),
      .dead = optionalJsonBool(payload, "dead", false),
      .unconscious = optionalJsonBool(payload, "unconscious", false),
    });
    if(!timeline.accepted)
      return {true, true, true, timeline.reason};
    const auto observedFightAction = Mmo::Server::FightIntent::observedAction(
      attackState,
      bodyState,
      optionalJsonBool(payload, "attack_anim", false),
      optionalJsonBool(payload, "prehit", false));
    if(observedFightAction != Mmo::Server::FightMove::Action::None) {
      const auto intent = Mmo::Server::FightIntent::evaluate({
        .actorKey = npcKey,
        .targetKey = opponentKey,
        .action = observedFightAction,
        .serverTickMs = tick,
      }, gCombatTimelineRegistry);
      if(intent.hasTimeline && !intent.accepted) {
        std::cerr << "[combat_observed_intent_soft_reject]"
                  << " actor=" << npcKey
                  << " target=" << opponentKey
                  << " action=" << Mmo::Server::FightMove::actionName(observedFightAction)
                  << " reason=" << intent.reason
                  << "\n";
      }
    }
    const auto activity = gNpcActivityRegistry.applyActivity({
      .actorKey = npcKey,
      .actionKey = "combat",
      .actionState = fightState,
      .targetKey = opponentKey,
      .syncGroup = opponentKey.empty() ? npcKey : opponentKey,
      .serverTickMs = tick,
      .expectedDurationMs = 0,
    });
    if(!activity.accepted)
      return {true, true, true, activity.reason};
    cancelInterruptedConversation(activity);
    Mmo::Server::recordNpcFightState(target, {
      .sessionUuid = sessionUuid,
      .npcKey = npcKey,
      .opponentKey = opponentKey,
      .fightState = fightState,
      .attackState = attackState,
      .comboIndex = comboIndex,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  if(packet.kind == Mmo::SemanticActionKind::RecordNpcActionState) {
    const auto command = buildNpcActionCommandFromPayload(payload, packet.targetKey, tick);
    if(!command.shouldPersist)
      return {true, true, true, command.reason};
    const auto activity = applyNpcActivityCommand(command.actorKey,
                                                 command.actionKey,
                                                 command.actionState,
                                                 command.targetKey,
                                                 command.syncGroup,
                                                 tick,
                                                 0,
                                                 {});
    if(!activity.accepted)
      return {true, true, true, activity.reason};
    return {false, true, false, "npc_action_outbox"};
  }

  if(packet.kind == Mmo::SemanticActionKind::RecordNpcDialogLine) {
    gNpcActivityRegistry.expire(tick);
    gConversationRegistry.expire(tick);
    const auto duration = optionalJsonI64(payload, "line_duration_ms",
                          optionalJsonI64(payload, "duration_ms",
                          optionalJsonI64(payload, "message_time_ms", 0)));
    const auto command = Mmo::Server::NpcAction::buildDialogLineCommand({
      .conversationKey = optionalJsonString(payload, "conversation_key", optionalJsonString(payload, "sync_group")),
      .speakerKey = optionalJsonString(payload, "speaker_key", optionalJsonString(payload, "actor_key")),
      .listenerKey = optionalJsonString(payload, "listener_key", optionalJsonString(payload, "target_key", packet.targetKey)),
      .infoKey = optionalJsonString(payload, "info_key"),
      .outputName = optionalJsonString(payload, "output_name", optionalJsonString(payload, "message_name")),
      .subtitleText = optionalJsonString(payload, "subtitle_text", optionalJsonString(payload, "text")),
      .lineDurationMs = duration > 0 ? static_cast<std::uint32_t>(std::min<std::int64_t>(duration, Mmo::Server::NpcAction::MaxNpcDialogLineMs + 1)) : 0u,
      .serverTick = tick,
    });
    if(!command.shouldPersist)
      return {true, true, true, command.reason};
    const auto activity = gNpcActivityRegistry.applyDialogLock({
      .conversationKey = command.conversationKey,
      .speakerKey = command.speakerKey,
      .listenerKey = command.listenerKey,
      .serverTickMs = tick,
      .lineDurationMs = command.lineDurationMs,
    });
    if(!activity.accepted)
      return {true, true, true, activity.reason};
    const auto conversation = gConversationRegistry.applyLine({
      .conversationKey = command.conversationKey,
      .speakerKey = command.speakerKey,
      .listenerKey = command.listenerKey,
      .outputName = command.outputName,
      .subtitleText = command.subtitleText,
      .startServerTickMs = tick,
      .durationMs = command.lineDurationMs,
    });
    if(!conversation.accepted)
      return {true, true, true, conversation.reason};
    return {false, true, false, "npc_dialog_line_outbox"};
  }

  if(packet.kind == Mmo::SemanticActionKind::RecordTriggerQueueState) {
    const auto triggerKey = optionalJsonString(payload, "trigger_key", optionalJsonString(payload, "target_key", packet.targetKey));
    const auto queueState = optionalJsonString(payload, "queue_state", "queued");
    const auto eventTypeName = optionalJsonString(payload, "event_type_name", optionalJsonString(payload, "reason", "trigger_event"));
    const auto scheduledTick = optionalJsonI64(payload, "scheduled_server_tick", optionalJsonI64(payload, "execute_at_tick", tick));
    Mmo::Server::recordTriggerQueueState(target, {
      .sessionUuid = sessionUuid,
      .triggerKey = triggerKey,
      .queueState = queueState,
      .eventTypeName = eventTypeName,
      .scheduledServerTick = scheduledTick,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  if(packet.kind == Mmo::SemanticActionKind::RecordWorldTransitionState) {
    const auto fromWorld = optionalJsonString(payload, "from_world_key", optionalJsonString(payload, "from_world", ""));
    const auto toWorld = optionalJsonString(payload, "to_world_key", optionalJsonString(payload, "to_world", optionalJsonString(payload, "world", "")));
    const auto transitionState = optionalJsonString(payload, "transition_state", "visited");
    const auto chapterKey = optionalJsonString(payload, "chapter_key", optionalJsonString(payload, "chapter", ""));
    const bool visited = optionalJsonBool(payload, "visited", true);
    Mmo::Server::recordWorldTransitionState(target, {
      .sessionUuid = sessionUuid,
      .fromWorld = fromWorld,
      .toWorld = toWorld,
      .transitionState = transitionState,
      .chapterKey = chapterKey,
      .visited = visited,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  if(packet.kind == Mmo::SemanticActionKind::ClientCorrectionAck) {
    const auto actionKind = optionalJsonString(payload, "action_kind", "");
    const auto localSequence = optionalJsonI64(payload, "client_local_sequence", 0);
    Mmo::Server::ackClientActionCorrection(target, {
      .sessionUuid = sessionUuid,
      .actionKind = actionKind,
      .localSequence = localSequence,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  return {false, true, false, "unhandled"};
}




