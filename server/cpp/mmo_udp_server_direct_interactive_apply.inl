// Internal implementation partition for mmo_udp_server.cpp.
// Handles interactives and weapon state direct DB actions.

[[nodiscard]] DirectApplyResult applyInteractiveDirectDb(const Mmo::Server::DirectApplyRequest& request) {
  const MySqlTarget& target = request.target;
  const std::string_view sessionUuid = request.sessionUuid;
  const Mmo::Net::ClientActionPacket& packet = request.packet;
  const std::string_view dbPayload = request.dbPayload;
  const std::string_view payload = packet.payloadJson;
  const auto tick = packetServerTick(packet);

  if(packet.kind == Mmo::SemanticActionKind::UseInteractive) {
    const auto key = optionalJsonString(payload, "interactive_key", optionalJsonString(payload, "target_key", packet.targetKey));
    const auto state = optionalJsonI64(payload, "state_after", optionalJsonI64(payload, "state", 0));
    const auto validation = Mmo::Server::Gameplay::validateInteractiveUse({
      .key = key,
      .state = state,
    });
    if(!validation.accepted)
      return {true, false, false, validation.reason};
    Mmo::Server::recordInteractiveUse(target, {
      .sessionUuid = sessionUuid,
      .interactiveKey = key,
      .stateAfter = state,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  if(packet.kind == Mmo::SemanticActionKind::UpdateInteractiveState) {
    const auto key = optionalJsonString(payload, "interactive_key", optionalJsonString(payload, "target_key", packet.targetKey));
    const auto state = optionalJsonI64(payload, "state_after", optionalJsonI64(payload, "state", 0));
    const auto count = optionalJsonI64(payload, "state_count", 0);
    const auto mask = optionalJsonI64(payload, "state_mask", 0);
    const bool locked = optionalJsonBool(payload, "locked_after", optionalJsonBool(payload, "locked", false));
    const bool cracked = optionalJsonBool(payload, "cracked_after", optionalJsonBool(payload, "cracked", false));
    const auto lifecycle = optionalJsonString(payload, "lifecycle_state", "active");
    const auto validation = Mmo::Server::Gameplay::validateInteractiveState({
      .key = key,
      .state = state,
      .stateCount = count,
      .stateMask = mask,
      .lifecycleState = lifecycle,
    });
    if(!validation.accepted)
      return {true, false, false, validation.reason};
    Mmo::Server::updateInteractiveState(target, {
      .sessionUuid = sessionUuid,
      .interactiveKey = key,
      .stateAfter = state,
      .stateCount = count,
      .stateMask = mask,
      .locked = locked,
      .cracked = cracked,
      .lifecycle = lifecycle,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  if(packet.kind == Mmo::SemanticActionKind::ReadyWeapon || packet.kind == Mmo::SemanticActionKind::HolsterWeapon) {
    const auto actorKey = optionalJsonString(payload, "actor_key", optionalJsonString(payload, "actor_entity_key", packet.targetKey));
    const auto state = optionalJsonString(payload, "new_weapon_state",
                       optionalJsonString(payload, "weapon_state",
                       packet.kind == Mmo::SemanticActionKind::HolsterWeapon ? "no_weapon" : "ready_weapon"));
    const bool ready = optionalJsonBool(payload, "ready", packet.kind == Mmo::SemanticActionKind::ReadyWeapon);
    recordPerceptionEvent({
      .perceptionId = static_cast<std::uint8_t>(ready ? 24 : 11),
      .sourceKey = actorKey,
      .otherKey = actorKey,
      .reason = ready ? "weapon_draw_observed" : "weapon_remove_observed",
      .originPosition = optionalGameplayVec3(payload, "actor_position"),
      .serverTickMs = tick,
    }, &target, sessionUuid, packet.idempotencyKey);
    if(ready) {
      const auto targetKey = optionalJsonString(payload, "target_entity_key",
                           optionalJsonString(payload, "threat_key", ""));
      const auto activity = gNpcActivityRegistry.applyActivity({
        .actorKey = actorKey,
        .actionKey = "alert",
        .actionState = "weapon_ready",
        .targetKey = targetKey,
        .syncGroup = targetKey.empty() ? actorKey : targetKey,
        .serverTickMs = tick,
        .expectedDurationMs = 0,
      });
      if(!activity.accepted)
        return {true, true, true, activity.reason};
      cancelInterruptedConversation(activity);
    } else {
      (void)gNpcActivityRegistry.clearActorIfKind(actorKey, Mmo::Server::NpcActivity::Kind::Alert);
    }
    Mmo::Server::recordNpcWeaponState(target, {
      .sessionUuid = sessionUuid,
      .actorKey = actorKey,
      .weaponState = state,
      .ready = ready,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  return {false, true, false, "unhandled"};
}


