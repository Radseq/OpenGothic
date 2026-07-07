// Internal implementation partition for mmo_udp_server.cpp.
// Handles combat, damage and character resource direct DB actions.

[[nodiscard]] std::int64_t readCurrentCharacterResourceValue(const MySqlTarget& target,
                                                             std::string_view sessionUuid,
                                                             std::string_view characterKey,
                                                             std::string_view resourceKey) {
  std::string column;
  if(resourceKey == "mana")
    column = "cs.mana_current";
  else if(resourceKey == "health" || resourceKey == "hp")
    column = "cs.health_current";
  else
    throw std::runtime_error("resource_key_not_server_resolved");

  std::string query;
  query += "SELECT COALESCE(" + column + ",0) ";
  query += "FROM server_sessions ss ";
  query += "JOIN characters c ON c.realm_id=ss.realm_id AND c.character_key=" + sqlLiteral(characterKey) + " ";
  query += "JOIN character_stats cs ON cs.character_id=c.character_id ";
  query += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) LIMIT 1;";

  const auto value = parseI64(mysqlSingleField(target, query));
  if(!value)
    throw std::runtime_error("character_resource_current_value_missing");
  return *value;
}

[[nodiscard]] std::string combatPayloadKey(std::string_view prefix, std::string_view field) {
  std::string out(prefix);
  out += "_combat_";
  out += field;
  return out;
}

[[nodiscard]] std::string indexedCombatPayloadKey(std::string_view prefix,
                                                  std::string_view field,
                                                  std::size_t index) {
  auto out = combatPayloadKey(prefix, field);
  out.push_back('_');
  out += std::to_string(index);
  return out;
}

[[nodiscard]] Mmo::Server::DamageCalculator::DamageKind damageKindFromPayload(std::string_view payload) {
  const auto kind = optionalJsonString(payload, "damage_kind", "unknown");
  if(kind == "melee")
    return Mmo::Server::DamageCalculator::DamageKind::Melee;
  if(kind == "ranged")
    return Mmo::Server::DamageCalculator::DamageKind::Ranged;
  if(kind == "magic" || kind == "spell")
    return Mmo::Server::DamageCalculator::DamageKind::Magic;
  if(kind == "fall")
    return Mmo::Server::DamageCalculator::DamageKind::Fall;
  return Mmo::Server::DamageCalculator::DamageKind::Unknown;
}

[[nodiscard]] Mmo::Server::Combat::DamageModifier damageModifierFromPayload(std::string_view payload) {
  const auto modifier = optionalJsonString(payload, "damage_modifier", "normal");
  if(modifier == "double")
    return Mmo::Server::Combat::DamageModifier::Double;
  if(modifier == "half")
    return Mmo::Server::Combat::DamageModifier::Half;
  if(modifier == "blocked")
    return Mmo::Server::Combat::DamageModifier::Blocked;
  return Mmo::Server::Combat::DamageModifier::Normal;
}

[[nodiscard]] bool hasCombatProfile(std::string_view payload, std::string_view prefix) {
  return jsonNumberTextField(payload, combatPayloadKey(prefix, "damage_type_mask")).has_value();
}

[[nodiscard]] Mmo::Server::Combat::DamageVector combatVectorFromPayload(std::string_view payload,
                                                                        std::string_view prefix,
                                                                        std::string_view field) {
  Mmo::Server::Combat::DamageVector out;
  for(std::size_t i = 0; i < Mmo::Server::Combat::DamageTypeCount; ++i)
    out.values[i] = optionalJsonI64(payload, indexedCombatPayloadKey(prefix, field, i), 0);
  return out;
}

[[nodiscard]] Mmo::Server::Combat::DamageActorProfile combatProfileFromPayload(std::string_view payload,
                                                                               std::string_view prefix) {
  return {
    .strength = optionalJsonI64(payload, combatPayloadKey(prefix, "strength"), 0),
    .dexterity = optionalJsonI64(payload, combatPayloadKey(prefix, "dexterity"), 0),
    .damageTypeMask = optionalJsonI64(payload, combatPayloadKey(prefix, "damage_type_mask"), 0),
    .damage = combatVectorFromPayload(payload, prefix, "damage"),
    .protection = combatVectorFromPayload(payload, prefix, "protection"),
  };
}

[[nodiscard]] bool hasExplicitDamageVector(std::string_view payload) {
  for(std::size_t i = 0; i < Mmo::Server::Combat::DamageTypeCount; ++i) {
    const std::string key = "damage_vector_" + std::to_string(i);
    if(jsonNumberTextField(payload, key).has_value())
      return true;
  }
  return false;
}

[[nodiscard]] Mmo::Server::Combat::DamageVector explicitDamageVectorFromPayload(std::string_view payload) {
  Mmo::Server::Combat::DamageVector out;
  for(std::size_t i = 0; i < Mmo::Server::Combat::DamageTypeCount; ++i) {
    const std::string key = "damage_vector_" + std::to_string(i);
    out.values[i] = optionalJsonI64(payload, key, 0);
  }
  return out;
}

[[nodiscard]] Mmo::Server::DamageCalculator::ObservedDamageInput observedDamageInputFromPayload(
    std::string_view payload,
    std::int64_t proposedDamage) {
  const bool hasSource = hasCombatProfile(payload, "source_actor");
  const bool hasTarget = hasCombatProfile(payload, "target");
  const bool sourceMonster = optionalJsonBool(payload, "source_actor_combat_monster", false);
  const bool sourceHasWeapon = optionalJsonBool(payload, "source_actor_combat_has_active_weapon", false);
  const bool hasExplicitVector = hasExplicitDamageVector(payload);
  const bool hasMeleeRoll = jsonNumberTextField(payload, "melee_random_roll").has_value() &&
                             (jsonNumberTextField(payload, "melee_talent_chance").has_value() ||
                              jsonNumberTextField(payload, "source_actor_combat_melee_talent_chance").has_value());
  const bool hasRangedRoll = jsonNumberTextField(payload, "projectile_distance").has_value() &&
                              jsonNumberTextField(payload, "projectile_random_hit_roll").has_value();

  return {
    .kind = damageKindFromPayload(payload),
    .proposedDamage = proposedDamage,
    .attacker = hasSource ? combatProfileFromPayload(payload, "source_actor") : Mmo::Server::Combat::DamageActorProfile{},
    .victim = hasTarget ? combatProfileFromPayload(payload, "target") : Mmo::Server::Combat::DamageActorProfile{},
    .explicitDamage = hasExplicitVector ? explicitDamageVectorFromPayload(payload) : Mmo::Server::Combat::DamageVector{},
    .hasAttackerProfile = hasSource,
    .hasVictimProfile = hasTarget,
    .hasExplicitDamage = hasExplicitVector,
    .gothic2 = optionalJsonI64(payload, "gothic_game", 2) == 2,
    .criticalHit = optionalJsonBool(payload, "critical_hit", false),
    .monsterWithoutWeapon = sourceMonster && !sourceHasWeapon,
    .criticalMultiplier = optionalJsonI64(payload, "critical_damage_multiplier", Mmo::Server::Combat::DefaultCriticalDamageMultiplier),
    .hasMeleeRoll = hasMeleeRoll,
    .meleeTalentChance = optionalJsonI64(payload, "melee_talent_chance",
                         optionalJsonI64(payload, "source_actor_combat_melee_talent_chance", 0)),
    .meleeRandomRoll = optionalJsonI64(payload, "melee_random_roll", 0),
    .modifier = damageModifierFromPayload(payload),
    .hasRangedRoll = hasRangedRoll,
    .projectileSpell = optionalJsonBool(payload, "projectile_spell", false),
    .projectileDistance = optionalJsonDouble(payload, "projectile_distance", 0.0),
    .projectileWeaponChance = optionalJsonDouble(payload, "projectile_weapon_chance", 0.0),
    .projectileRandomHitRoll = optionalJsonDouble(payload, "projectile_random_hit_roll", 0.0),
    .projectileCriticalHit = optionalJsonBool(payload, "projectile_critical_hit", false),
    .fall = {
      .speed = optionalJsonDouble(payload, "fall_speed", 0.0),
      .gravity = optionalJsonDouble(payload, "fall_gravity", 0.000981),
      .fallHeightThreshold = optionalJsonI64(payload, "fall_height_threshold", 0),
      .damagePerMeter = optionalJsonI64(payload, "fall_damage_per_meter", 0),
      .fallProtection = optionalJsonI64(payload, "target_combat_protection_7", 0),
    },
    .hasFallInput = jsonNumberTextField(payload, "fall_speed").has_value(),
    .tolerance = optionalJsonI64(payload, "damage_tolerance", Mmo::Server::DamageCalculator::DefaultObservedDamageTolerance),
  };
}

void auditObservedDamage(std::string_view payload,
                         std::int64_t damage,
                         std::string_view attackerKey,
                         std::string_view targetKey) {
  const auto evaluation = Mmo::Server::DamageCalculator::evaluateObservedDamage(
    observedDamageInputFromPayload(payload, damage));
  if(!evaluation.comparable || evaluation.accepted)
    return;
  std::cerr << "[combat_damage_calculator_mismatch]"
            << " attacker=" << attackerKey
            << " target=" << targetKey
            << " proposed=" << damage
            << " min=" << evaluation.minimum.value
            << " max=" << evaluation.maximum.value
            << " reason=" << evaluation.reason
            << "\n";
}

[[nodiscard]] DirectApplyResult applyCombatDirectDb(const Mmo::Server::DirectApplyRequest& request) {
  const MySqlTarget& target = request.target;
  const std::string_view sessionUuid = request.sessionUuid;
  const Mmo::Net::ClientActionPacket& packet = request.packet;
  const std::string_view dbPayload = request.dbPayload;
  const std::string_view payload = packet.payloadJson;
  const auto tick = packetServerTick(packet);

  if(packet.kind == Mmo::SemanticActionKind::RecordCombatIntent) {
    for(const auto& expired : gFightIntentRegistry.expire(tick)) {
      std::cerr << "[combat_intent_proposal_expired]"
                << " actor=" << expired.actorKey
                << " target=" << expired.targetKey
                << " action=" << expired.actionName
                << "\n";
    }
    const auto actorKey = optionalJsonString(payload, "actor_key",
                         optionalJsonString(payload, "actor_npc_entity_key", packet.targetKey));
    const auto targetKey = optionalJsonString(payload, "target_key",
                          optionalJsonString(payload, "target_npc_entity_key", ""));
    const auto actionName = optionalJsonString(payload, "combat_action", "");
    const auto intentState = optionalJsonString(payload, "intent_state", "observed");
    const auto state = Mmo::Server::FightIntent::intentStateFromName(intentState);
    const auto action = Mmo::Server::FightMove::actionFromName(actionName);
    if(intentState == "proposed" || intentState == "observed") {
      std::uint8_t perceptionId = 0;
      const char* perceptionReason = "combat_intent_observed";
      if(actionName == "cast_spell") {
        perceptionId = 27;
        perceptionReason = "magic_cast_intent_observed";
      } else if(actionName == "attack" || actionName == "attack_left" ||
                actionName == "attack_right" || actionName == "shoot_ranged") {
        perceptionId = 10;
        perceptionReason = "threat_intent_observed";
      }
      if(perceptionId != 0) {
        recordPerceptionEvent({
          .perceptionId = perceptionId,
          .sourceKey = actorKey,
          .otherKey = actorKey,
          .victimKey = targetKey,
          .reason = perceptionReason,
          .originPosition = optionalGameplayVec3(payload, "actor_position"),
          .serverTickMs = tick,
        }, &target, sessionUuid, packet.idempotencyKey);
      }
    }
    const auto correlation = gFightIntentRegistry.apply({
      .actorKey = actorKey,
      .targetKey = targetKey,
      .actionName = actionName,
      .state = state,
      .action = action,
      .serverTickMs = tick,
    });
    if(!correlation.accepted) {
      std::cerr << "[combat_intent_correlation_mismatch]"
                << " actor=" << actorKey
                << " target=" << targetKey
                << " action=" << actionName
                << " state=" << intentState
                << " reason=" << correlation.reason
                << "\n";
    }
    const auto intent = Mmo::Server::FightIntent::evaluate({
      .actorKey = actorKey,
      .targetKey = targetKey,
      .action = action,
      .serverTickMs = tick,
    }, gCombatTimelineRegistry);
    if(intent.hasTimeline && !intent.accepted) {
      std::cerr << "[combat_explicit_intent_soft_reject]"
                << " actor=" << actorKey
                << " target=" << targetKey
                << " action=" << actionName
                << " state=" << intentState
                << " reason=" << intent.reason
                << "\n";
    }
    return {true, true, true, !correlation.accepted ? correlation.reason : intent.reason};
  }

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

    const auto sourceActorKey = optionalJsonString(payload, "source_actor_entity_key",
                              optionalJsonString(payload, "source_actor_key", ""));
    recordPerceptionEvent({
      .perceptionId = 8,
      .sourceKey = sourceActorKey,
      .otherKey = sourceActorKey,
      .victimKey = characterKey,
      .reason = "character_damage_observed",
      .originPosition = optionalGameplayVec3(payload, "target_position"),
      .serverTickMs = tick,
    }, &target, sessionUuid, packet.idempotencyKey);
    auditObservedDamage(payload, damage, sourceActorKey, characterKey);

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

    const auto sourceActorKey = optionalJsonString(payload, "source_actor_entity_key",
                              optionalJsonString(payload, "source_actor_key", ""));
    recordPerceptionEvent({
      .perceptionId = fatal ? static_cast<std::uint8_t>(6) : static_cast<std::uint8_t>(8),
      .sourceKey = sourceActorKey,
      .otherKey = sourceActorKey,
      .victimKey = npc.entityKey,
      .reason = fatal ? "world_entity_murder_observed" : "world_entity_damage_observed",
      .originPosition = optionalGameplayVec3(payload, "target_position"),
      .serverTickMs = tick,
    }, &target, sessionUuid, packet.idempotencyKey);
    if(!fatal) {
      recordPerceptionEvent({
        .perceptionId = 9,
        .sourceKey = sourceActorKey,
        .otherKey = sourceActorKey,
        .victimKey = npc.entityKey,
        .reason = "others_damage_observed",
        .originPosition = optionalGameplayVec3(payload, "target_position"),
        .serverTickMs = tick,
      }, &target, sessionUuid, packet.idempotencyKey);
    }
    const auto intent = Mmo::Server::FightIntent::evaluate({
      .actorKey = sourceActorKey,
      .targetKey = npc.entityKey,
      .action = Mmo::Server::FightMove::Action::Attack,
      .serverTickMs = tick,
    }, gCombatTimelineRegistry);
    if(intent.hasTimeline && !intent.accepted) {
      std::cerr << "[combat_intent_soft_reject]"
                << " attacker=" << sourceActorKey
                << " target=" << npc.entityKey
                << " action=attack"
                << " reason=" << intent.reason
                << "\n";
    }
    const auto evidence = gCombatTimelineRegistry.evaluateDamageEvidence({
      .attackerKey = sourceActorKey,
      .targetKey = npc.entityKey,
      .serverTickMs = tick,
      .projectileOrSpell = false,
    });
    if(evidence.hasAttackerTimeline && !evidence.plausibleMeleeHit) {
      std::cerr << "[combat_timeline_damage_mismatch]"
                << " attacker=" << sourceActorKey
                << " target=" << npc.entityKey
                << " reason=" << evidence.reason
                << "\n";
    }
    auditObservedDamage(payload, damage, sourceActorKey, npc.entityKey);

    Mmo::Server::applyWorldEntityDamage(target, {
      .sessionUuid = sessionUuid,
      .entityKey = npc.entityKey,
      .damage = damage,
      .fatal = fatal,
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
    const auto resourceKey = packet.kind == Mmo::SemanticActionKind::ConsumeMana ?
      std::string("mana") :
      optionalJsonString(payload, "resource_key", "unknown");
    auto valueBefore = readCurrentCharacterResourceValue(target, sessionUuid, characterKey, resourceKey);
    auto delta = optionalJsonI64(payload,
                                "requested_delta",
                                optionalJsonI64(payload,
                                                "delta_amount",
                                                optionalJsonI64(payload, "delta", 0)));
    auto valueAfter = valueBefore + delta;

    if(packet.kind == Mmo::SemanticActionKind::ConsumeMana) {
      const auto amount = optionalJsonI64(payload, "mana_amount",
                         optionalJsonI64(payload, "amount", optionalJsonI64(payload, "requested_amount", 0)));
      const auto spend = Mmo::Server::Combat::resolveResourceSpend({
        .characterKey = characterKey,
        .resourceKey = resourceKey,
        .currentValue = valueBefore,
        .amount = amount,
      });
      if(!spend.validation.accepted)
        return {true, false, false, spend.validation.reason};
      delta = spend.delta;
      valueBefore = spend.valueBefore;
      valueAfter = spend.valueAfter;
    }

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





