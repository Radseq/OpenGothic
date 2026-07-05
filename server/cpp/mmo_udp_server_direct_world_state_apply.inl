// Internal implementation partition for mmo_udp_server.cpp.
// Handles triggers, movers, NPC observations and world transition direct DB actions.

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
    const auto validation = Mmo::Server::Gameplay::validateNpcFightObservation({
      .npcKey = npcKey,
      .opponentKey = opponentKey,
      .fightState = fightState,
      .attackState = attackState,
      .comboIndex = comboIndex,
    });
    if(!validation.shouldPersist)
      return ignoreNpcObservation("record_npc_fight_state", packet.targetKey, validation);
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
    const auto command = Mmo::Server::NpcAction::buildActionCommand({
      .actorKey = optionalJsonString(payload, "actor_key",
                  optionalJsonString(payload, "npc_entity_key",
                  optionalJsonString(payload, "target_key", packet.targetKey))),
      .actionKey = optionalJsonString(payload, "action_key", optionalJsonString(payload, "action_name", "unknown")),
      .actionState = optionalJsonString(payload, "action_state", optionalJsonString(payload, "state", "active")),
      .targetKey = optionalJsonString(payload, "action_target_key", optionalJsonString(payload, "target_entity_key")),
      .syncGroup = optionalJsonString(payload, "sync_group", optionalJsonString(payload, "conversation_key")),
      .serverTick = tick,
    });
    if(!command.shouldPersist)
      return {true, true, true, command.reason};
    const auto activity = gNpcActivityRegistry.applyActivity({
      .actorKey = command.actorKey,
      .actionKey = command.actionKey,
      .actionState = command.actionState,
      .targetKey = command.targetKey,
      .syncGroup = command.syncGroup,
      .serverTickMs = tick,
      .expectedDurationMs = 0,
    });
    if(!activity.accepted)
      return {true, true, true, activity.reason};
    cancelInterruptedConversation(activity);
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
