// Internal implementation partition for mmo_udp_server.cpp.
// Handles combat, damage and character resource direct DB actions.

[[nodiscard]] DirectApplyResult applyCombatDirectDb(const Mmo::Server::DirectApplyRequest& request) {
  const MySqlTarget& target = request.target;
  const std::string_view sessionUuid = request.sessionUuid;
  const Mmo::Net::ClientActionPacket& packet = request.packet;
  const std::string_view dbPayload = request.dbPayload;
  const std::string_view payload = packet.payloadJson;
  const auto tick = packetServerTick(packet);

  if(packet.kind == Mmo::SemanticActionKind::ApplyCharacterDamage) {
    const auto characterKey = optionalJsonString(payload, "target_character_key",
                            optionalJsonString(payload, "character_key", "PC_HERO"));
    const auto damage = damageAmountFromPayload(payload);
    const auto validation = Mmo::Server::Combat::validateDamage({
      .targetKey = characterKey,
      .amount = damage,
      .fatal = false,
    });
    if(!validation.accepted)
      return {true, false, false, validation.reason};

    Mmo::Server::applyCharacterDamage(target, {
      .sessionUuid = sessionUuid,
      .characterKey = characterKey,
      .damage = damage,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  if(packet.kind == Mmo::SemanticActionKind::ApplyWorldEntityDamage) {
    ResolvedWorldNpcEntity npc;
    try {
      npc = resolveTargetWorldNpcEntityKey(target, sessionUuid, packet);
    } catch(const std::exception& resolveError) {
      std::cerr << "[observed_world_npc_resolve_fallback] action=apply_world_entity_damage"
                << " target=" << packet.targetKey
                << " reason=" << resolveError.what() << "\n";
      npc = materializeObservedWorldNpcEntity(target, sessionUuid, packet, dbPayload);
    }
    if(npc.lifecycleState != "active")
      return {true, true, false, "world_entity_damage_noop_inactive"};

    const auto damage = damageAmountFromPayload(payload);
    const bool fatal = optionalJsonBool(payload, "fatal", optionalJsonBool(payload, "dead", false));
    const auto validation = Mmo::Server::Combat::validateDamage({
      .targetKey = npc.entityKey,
      .amount = damage,
      .fatal = fatal,
    });
    if(!validation.accepted)
      return {true, false, false, validation.reason};
    const auto attackerKey = optionalJsonString(payload, "attacker_key",
                            optionalJsonString(payload, "source_actor_key",
                            optionalJsonString(payload, "source_entity_key", "")));
    const auto damageEvidence = gCombatTimelineRegistry.evaluateDamageEvidence({
      .attackerKey = attackerKey,
      .targetKey = npc.entityKey,
      .serverTickMs = tick,
      .projectileOrSpell = optionalJsonBool(payload, "projectile", optionalJsonBool(payload, "spell", false)),
    });
    if(damageEvidence.hasAttackerTimeline && !damageEvidence.plausibleMeleeHit) {
      std::cerr << "[combat_timeline_damage_mismatch]"
                << " attacker=" << attackerKey
                << " target=" << npc.entityKey
                << " reason=" << damageEvidence.reason
                << "\n";
    }
    const auto outcome = Mmo::Server::CombatOutcome::decide({
      .fatalDamage = fatal,
      .observedDead = optionalJsonBool(payload, "dead", false),
      .observedUnconscious = optionalJsonBool(payload, "unconscious", optionalJsonBool(payload, "down", false)),
      .targetIsPlayer = optionalJsonBool(payload, "target_is_player", false),
      .lethalRequested = optionalJsonBool(payload, "lethal", optionalJsonBool(payload, "kill_intent", false)),
      .nonLethalRequested = optionalJsonBool(payload, "non_lethal", optionalJsonBool(payload, "knockout_intent", false)),
      .killAllowed = optionalJsonBool(payload, "allow_kill", optionalJsonBool(payload, "source_can_kill", false)),
    });
    const auto activity = gNpcActivityRegistry.applyActivity({
      .actorKey = npc.entityKey,
      .actionKey = Mmo::Server::CombatOutcome::activityKey(outcome),
      .actionState = Mmo::Server::CombatOutcome::reason(outcome),
      .targetKey = attackerKey,
      .syncGroup = attackerKey.empty() ? npc.entityKey : attackerKey,
      .serverTickMs = tick,
      .expectedDurationMs = 0,
    });
    if(!activity.accepted)
      return {true, true, true, activity.reason};
    cancelInterruptedConversation(activity);

    Mmo::Server::applyWorldEntityDamage(target, {
      .sessionUuid = sessionUuid,
      .entityKey = npc.entityKey,
      .damage = damage,
      .fatal = outcome == Mmo::Server::CombatOutcome::Result::Dead,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  if(packet.kind == Mmo::SemanticActionKind::MarkNpcDead) {
    ResolvedWorldNpcEntity npc;
    try {
      npc = resolveTargetWorldNpcEntityKey(target, sessionUuid, packet);
    } catch(const std::exception& resolveError) {
      std::cerr << "[observed_world_npc_resolve_fallback] action=mark_npc_dead"
                << " target=" << packet.targetKey
                << " reason=" << resolveError.what() << "\n";
      npc = materializeObservedWorldNpcEntity(target, sessionUuid, packet, dbPayload);
    }
    if(npc.lifecycleState != "active")
      return {true, true, false, "mark_npc_dead_noop_inactive"};
    const bool observedDead = optionalJsonBool(payload, "dead", true);
    const bool observedUnconscious = optionalJsonBool(payload, "unconscious", optionalJsonBool(payload, "down", false));
    const auto outcome = Mmo::Server::CombatOutcome::decide({
      .fatalDamage = observedDead || observedUnconscious,
      .observedDead = observedDead,
      .observedUnconscious = observedUnconscious,
      .targetIsPlayer = false,
      .lethalRequested = observedDead,
      .nonLethalRequested = observedUnconscious && !observedDead,
      .killAllowed = observedDead,
    });
    const auto activity = gNpcActivityRegistry.applyActivity({
      .actorKey = npc.entityKey,
      .actionKey = Mmo::Server::CombatOutcome::activityKey(outcome),
      .actionState = Mmo::Server::CombatOutcome::reason(outcome),
      .targetKey = "",
      .syncGroup = npc.entityKey,
      .serverTickMs = tick,
      .expectedDurationMs = 0,
    });
    if(!activity.accepted)
      return {true, true, true, activity.reason};
    cancelInterruptedConversation(activity);
    if(outcome != Mmo::Server::CombatOutcome::Result::Dead)
      return {true, true, true, "npc_lifecycle_down_runtime_applied"};

    Mmo::Server::markNpcDead(target, {
      .sessionUuid = sessionUuid,
      .entityKey = npc.entityKey,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  if(packet.kind == Mmo::SemanticActionKind::ApplyCharacterResourceDelta ||
     packet.kind == Mmo::SemanticActionKind::ConsumeMana) {
    const auto characterKey = optionalJsonString(payload, "character_key",
                            optionalJsonString(payload, "target_character_key", "PC_HERO"));
    const auto resourceKey = optionalJsonString(payload, "resource_key",
                           packet.kind == Mmo::SemanticActionKind::ConsumeMana ? "mana" : "unknown");
    const auto valueBefore = optionalJsonI64(payload, "value_before", 0);
    const auto valueAfter = optionalJsonI64(payload, "value_after", valueBefore);
    const auto delta = packet.kind == Mmo::SemanticActionKind::ConsumeMana ?
      -optionalJsonI64(payload, "mana_amount", optionalJsonI64(payload, "amount", valueBefore - valueAfter)) :
      optionalJsonI64(payload, "delta_amount", valueAfter - valueBefore);
    const auto validation = Mmo::Server::Combat::validateResourceDelta({
      .characterKey = characterKey,
      .resourceKey = resourceKey,
      .delta = delta,
      .valueBefore = valueBefore,
      .valueAfter = valueAfter,
    });
    if(!validation.accepted)
      return {true, false, false, validation.reason};

    Mmo::Server::recordCharacterResourceDelta(target, {
      .sessionUuid = sessionUuid,
      .characterKey = characterKey,
      .resourceKey = resourceKey,
      .delta = delta,
      .valueBefore = valueBefore,
      .valueAfter = valueAfter,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  }

  return {false, true, false, "unhandled"};
}
