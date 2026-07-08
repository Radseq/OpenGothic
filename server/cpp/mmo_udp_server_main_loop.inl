// Internal implementation partition for mmo_udp_server.cpp.
// Owns process setup and the UDP receive/apply/ACK loop.

[[nodiscard]] std::pair<std::string, std::string> parseBind(std::string_view value) {
  const auto colon = value.rfind(':');
  if(colon == std::string_view::npos || colon == 0 || colon + 1 >= value.size())
    throw std::runtime_error("expected --bind host:port");
  return {std::string(value.substr(0, colon)), std::string(value.substr(colon + 1))};
}

[[nodiscard]] std::string contentManifestDiagnosticReason(std::string_view decision) {
  if(decision == "client_manifest_missing" ||
     decision == "content_hash_mismatch" ||
     decision == "server_manifest_missing" ||
     decision == "active_session_not_found" ||
     decision == "session_uuid_missing" ||
     decision == "realm_not_found") {
    return std::string(decision);
  }
  return "content_manifest_rejected";
}

[[nodiscard]] std::string bootstrapFailureDiagnosticReason(std::string_view message) {
  constexpr std::string_view prefix = "client content manifest rejected: ";
  const auto at = message.find(prefix);
  if(at == std::string_view::npos)
    return "bootstrap_failed";

  return contentManifestDiagnosticReason(message.substr(at + prefix.size()));
}

[[nodiscard]] std::string contentManifestDiagnosticMessage(const Mmo::Server::ContentManifestValidationError& error) {
  std::string out = "{\"error\":\"client_content_manifest_rejected\"";
  appendJsonField(out, "reason", error.decision);
  appendJsonField(out, "phase", error.phase);
  appendJsonField(out, "client_content_manifest_hash", error.clientHash);
  appendJsonField(out, "server_required_content_hash", error.serverHash);
  appendJsonField(out, "content_revision_key", error.revisionKey);
  appendJsonField(out, "message", error.what());
  out.push_back('}');
  return out;
}

void appendContentManifestRejectAudit(std::string_view remote,
                                      const Mmo::Net::ClientActionPacket& packet,
                                      std::string_view sessionUuid,
                                      const Mmo::Server::ContentManifestValidationError& error) noexcept {
  try {
    std::string out = "{\"event\":\"content_manifest_bootstrap_rejected\"";
    appendJsonField(out, "remote", remote);
    appendJsonField(out, "session_uuid", sessionUuid);
    appendJsonField(out, "packet_session_key", packet.sessionKey);
    appendJsonField(out, "target_key", packet.targetKey);
    appendJsonRawField(out, "packet_sequence", std::to_string(packet.packetSequence));
    appendJsonRawField(out, "local_sequence", std::to_string(packet.localSequence));
    appendJsonField(out, "reason", error.decision);
    appendJsonField(out, "phase", error.phase);
    appendJsonField(out, "client_content_manifest_hash", error.clientHash);
    appendJsonField(out, "server_required_content_hash", error.serverHash);
    appendJsonField(out, "content_revision_key", error.revisionKey);
    appendJsonField(out, "message", error.what());
    out.append("}\n");

    if(auto* file = std::fopen("runtime/mmo_server_content_manifest_rejects.jsonl", "ab")) {
      (void)std::fwrite(out.data(), 1, out.size(), file);
      (void)std::fclose(file);
    }
  } catch(...) {
  }
}

Options parseArgs(int argc, char** argv) {
  Options opt;
  auto need = [&](int& i, std::string_view flag) -> std::string {
    if(i + 1 >= argc)
      throw std::runtime_error(std::string(flag) + " requires value");
    return argv[++i];
  };

  for(int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if(arg == "--bind") opt.bind = need(i, arg);
    else if(arg == "--mysql-url" || arg == "--url") opt.mysqlUrl = need(i, arg);
    else if(arg == "--account-name") opt.accountName = need(i, arg);
    else if(arg == "--character-key") opt.characterKey = need(i, arg);
    else if(arg == "--character-name" || arg == "--character-display-name") opt.characterDisplayName = need(i, arg);
    else if(arg == "--session-key") opt.sessionKey = need(i, arg);
    else if(arg == "--db-session-uuid") opt.dbSessionUuid = need(i, arg);
    else if(arg == "--client-content-manifest-hash") opt.clientContentManifestHash = need(i, arg);
    else if(arg == "--outbox-priority") opt.outboxPriority = parseInt(need(i, arg)).value_or(opt.outboxPriority);
    else if(arg == "--outbox-max-attempts") opt.outboxMaxAttempts = parseInt(need(i, arg)).value_or(opt.outboxMaxAttempts);
    else if(arg == "--npc-action-worker-max-per-packet") opt.npcActionWorkerMaxPerPacket = parseInt(need(i, arg)).value_or(opt.npcActionWorkerMaxPerPacket);
    else if(arg == "--max-packets") opt.maxPackets = parseInt(need(i, arg)).value_or(0);
    else if(arg == "--direct-db") opt.directDb = true;
    else if(arg == "--no-direct-db") opt.directDb = false;
    else if(arg == "--enqueue-outbox") opt.enqueueOutbox = true;
    else if(arg == "--no-enqueue-outbox") opt.enqueueOutbox = false;
    else if(arg == "--npc-action-worker") opt.npcActionWorker = true;
    else if(arg == "--no-npc-action-worker") opt.npcActionWorker = false;
    else if(arg == "--forward-bootstrap-outbox") opt.forwardBootstrapOutbox = true;
    else if(arg == "--require-client-content-manifest") opt.requireClientContentManifest = true;
    else if(arg == "--no-require-client-content-manifest") opt.requireClientContentManifest = false;
    else if(arg == "--require-db-save-checkpoint-restore") opt.requireDbSaveCheckpointRestore = true;
    else if(arg == "--no-require-db-save-checkpoint-restore") opt.requireDbSaveCheckpointRestore = false;
    else if(arg == "--help" || arg == "-h") {
      std::cout << "Usage: mmo_udp_server --bind 127.0.0.1:29777 --mysql-url mysql://user:pass@host:3306/db [--session-key local-dev-PC_HERO_TEST] [--enqueue-outbox] [--no-direct-db] [--npc-action-worker-max-per-packet 4] [--client-content-manifest-hash HASH] [--require-client-content-manifest] [--require-db-save-checkpoint-restore]\n";
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + std::string(arg));
    }
  }
  return opt;
}

[[nodiscard]] bool hasCombatFlag(const Mmo::Net::ClientCombatDamagePacket& packet, std::uint32_t flag) noexcept {
  return (packet.flags & flag) != 0;
}

[[nodiscard]] const char* typedCombatDamageKindName(Mmo::Net::ClientCombatDamageKind kind) noexcept {
  switch(kind) {
    case Mmo::Net::ClientCombatDamageKind::Melee:   return "melee";
    case Mmo::Net::ClientCombatDamageKind::Ranged:  return "ranged";
    case Mmo::Net::ClientCombatDamageKind::Magic:   return "magic";
    case Mmo::Net::ClientCombatDamageKind::Fall:    return "fall";
    case Mmo::Net::ClientCombatDamageKind::Unknown: break;
  }
  return "unknown";
}

[[nodiscard]] const char* typedCombatDamageModifierName(Mmo::Net::ClientCombatDamageModifier modifier) noexcept {
  switch(modifier) {
    case Mmo::Net::ClientCombatDamageModifier::Double:  return "double";
    case Mmo::Net::ClientCombatDamageModifier::Half:    return "half";
    case Mmo::Net::ClientCombatDamageModifier::Blocked: return "blocked";
    case Mmo::Net::ClientCombatDamageModifier::Normal:  break;
  }
  return "normal";
}

void appendJsonSignedField(std::string& out, std::string_view key, std::int64_t value) {
  appendJsonRawField(out, key, std::to_string(value));
}

void appendJsonDoubleField(std::string& out, std::string_view key, double value) {
  appendJsonRawField(out, key, std::to_string(value));
}

void appendTypedCombatProfilePayload(std::string& out,
                                     std::string_view prefix,
                                     const Mmo::Net::ClientCombatProfile& profile,
                                     bool monster,
                                     bool hasActiveWeapon) {
  appendJsonSignedField(out, combatPayloadKey(prefix, "strength"), profile.strength);
  appendJsonSignedField(out, combatPayloadKey(prefix, "dexterity"), profile.dexterity);
  appendJsonSignedField(out, combatPayloadKey(prefix, "damage_type_mask"), profile.damageTypeMask);
  appendJsonRawField(out, combatPayloadKey(prefix, "monster"), monster ? "true" : "false");
  appendJsonRawField(out, combatPayloadKey(prefix, "has_active_weapon"), hasActiveWeapon ? "true" : "false");
  appendJsonSignedField(out, combatPayloadKey(prefix, "melee_talent_chance"), profile.meleeTalentChance);
  for(std::size_t i = 0; i < Mmo::Net::CombatDamageTypeCount; ++i) {
    appendJsonSignedField(out, indexedCombatPayloadKey(prefix, "damage", i), profile.damage[i]);
    appendJsonSignedField(out, indexedCombatPayloadKey(prefix, "protection", i), profile.protection[i]);
  }
}

[[nodiscard]] std::string typedCombatDamagePayloadJson(const Mmo::Net::ClientCombatDamagePacket& packet) {
  std::string out;
  out.reserve(1792);
  out += "{\"source\":\"typed_udp_client_combat_damage\"";

  const std::string targetEntity = packet.targetEntityKey.empty() ? packet.targetKey : packet.targetEntityKey;
  appendJsonField(out, "target_key", targetEntity);
  if(!packet.world.empty())
    appendJsonField(out, "world", packet.world);
  if(!packet.reason.empty())
    appendJsonField(out, "reason", packet.reason);
  if(!packet.sourceActorKey.empty())
    appendJsonField(out, "source_actor_key", packet.sourceActorKey);
  if(!packet.sourceActorEntityKey.empty())
    appendJsonField(out, "source_actor_entity_key", packet.sourceActorEntityKey);

  if(packet.kind == Mmo::SemanticActionKind::ApplyCharacterDamage) {
    if(!packet.targetCharacterKey.empty()) {
      appendJsonField(out, "target_character_key", packet.targetCharacterKey);
      appendJsonField(out, "character_key", packet.targetCharacterKey);
    }
  } else {
    appendJsonField(out, "target_world_entity_key", targetEntity);
    appendJsonField(out, "target_npc_entity_key", targetEntity);
  }

  appendJsonField(out, "damage_kind", typedCombatDamageKindName(packet.damageKind));
  appendJsonField(out, "damage_modifier", typedCombatDamageModifierName(packet.modifier));
  appendJsonSignedField(out, "gothic_game", packet.gothicGame);
  appendJsonSignedField(out, "damage_amount", packet.damageAmount);
  appendJsonSignedField(out, "value_before", packet.valueBefore);
  appendJsonSignedField(out, "value_after", packet.valueAfter);
  appendJsonSignedField(out, "requested_delta", packet.requestedDelta);
  appendJsonSignedField(out, "critical_damage_multiplier", packet.criticalDamageMultiplier);
  appendJsonRawField(out, "fatal", hasCombatFlag(packet, Mmo::Net::ClientCombatDamageFatal) ? "true" : "false");
  appendJsonRawField(out, "critical_hit", hasCombatFlag(packet, Mmo::Net::ClientCombatDamageCriticalHit) ? "true" : "false");

  if(hasCombatFlag(packet, Mmo::Net::ClientCombatDamageHasSourceProfile)) {
    appendTypedCombatProfilePayload(out,
                                    "source_actor",
                                    packet.source,
                                    hasCombatFlag(packet, Mmo::Net::ClientCombatDamageSourceMonster),
                                    hasCombatFlag(packet, Mmo::Net::ClientCombatDamageSourceHasActiveWeapon));
  }
  if(hasCombatFlag(packet, Mmo::Net::ClientCombatDamageHasTargetProfile)) {
    appendTypedCombatProfilePayload(out,
                                    "target",
                                    packet.target,
                                    hasCombatFlag(packet, Mmo::Net::ClientCombatDamageTargetMonster),
                                    hasCombatFlag(packet, Mmo::Net::ClientCombatDamageTargetHasActiveWeapon));
  }
  if(hasCombatFlag(packet, Mmo::Net::ClientCombatDamageHasExplicitDamage)) {
    for(std::size_t i = 0; i < Mmo::Net::CombatDamageTypeCount; ++i) {
      std::string key = "damage_vector_";
      key += std::to_string(i);
      appendJsonSignedField(out, key, packet.explicitDamage[i]);
    }
  }
  if(hasCombatFlag(packet, Mmo::Net::ClientCombatDamageHasMeleeRoll)) {
    appendJsonSignedField(out, "melee_random_roll", packet.meleeRandomRoll);
    appendJsonSignedField(out, "melee_talent_chance", packet.meleeTalentChance);
  }
  if(hasCombatFlag(packet, Mmo::Net::ClientCombatDamageHasRangedRoll)) {
    appendJsonDoubleField(out, "projectile_distance", packet.projectileDistance);
    appendJsonDoubleField(out, "projectile_weapon_chance", packet.projectileWeaponChance);
    appendJsonDoubleField(out, "projectile_random_hit_roll", packet.projectileRandomHitRoll);
    appendJsonRawField(out, "projectile_spell", hasCombatFlag(packet, Mmo::Net::ClientCombatDamageProjectileSpell) ? "true" : "false");
    appendJsonRawField(out, "projectile_critical_hit", hasCombatFlag(packet, Mmo::Net::ClientCombatDamageProjectileCriticalHit) ? "true" : "false");
  }
  if(hasCombatFlag(packet, Mmo::Net::ClientCombatDamageHasFallInput)) {
    appendJsonDoubleField(out, "fall_speed", packet.fallSpeed);
    appendJsonDoubleField(out, "fall_gravity", packet.fallGravity);
    appendJsonSignedField(out, "fall_height_threshold", packet.fallHeightThreshold);
    appendJsonSignedField(out, "fall_damage_per_meter", packet.fallDamagePerMeter);
  }
  if(hasCombatFlag(packet, Mmo::Net::ClientCombatDamageHasTargetPosition)) {
    appendJsonRawField(out,
                       "target_position",
                       std::string("{\"x\":") + std::to_string(packet.targetPosX) +
                         ",\"y\":" + std::to_string(packet.targetPosY) +
                         ",\"z\":" + std::to_string(packet.targetPosZ) + "}");
  }
  out.push_back('}');
  return out;
}

[[nodiscard]] Mmo::Net::ClientActionPacket typedCombatDamageAsClientAction(
    const Mmo::Net::ClientCombatDamagePacket& packet) {
  Mmo::Net::ClientActionPacket out;
  out.kind = packet.kind;
  out.packetSequence = packet.packetSequence;
  out.clientTick = packet.clientTick;
  out.localSequence = packet.localSequence;
  out.sessionKey = packet.sessionKey;
  out.targetKey = packet.targetKey;
  out.idempotencyKey = packet.idempotencyKey;
  out.payloadJson = typedCombatDamagePayloadJson(packet);
  return out;
}

[[nodiscard]] bool hasMovementFlag(const Mmo::Net::ClientMovementPacket& packet, std::uint32_t flag) noexcept {
  return (packet.flags & flag) != 0;
}

void appendJsonBoolField(std::string& out, std::string_view key, bool value) {
  appendJsonRawField(out, key, value ? "true" : "false");
}

void appendTypedMovementStatsPayload(std::string& out, const Mmo::Net::ClientMovementStats& stats) {
  appendJsonSignedField(out, "level", stats.level);
  appendJsonSignedField(out, "experience", stats.experience);
  appendJsonSignedField(out, "experience_next", stats.experienceNext);
  appendJsonSignedField(out, "learning_points", stats.learningPoints);
  appendJsonSignedField(out, "health_current", stats.healthCurrent);
  appendJsonSignedField(out, "health_max", stats.healthMax);
  appendJsonSignedField(out, "mana_current", stats.manaCurrent);
  appendJsonSignedField(out, "mana_max", stats.manaMax);
  appendJsonSignedField(out, "strength", stats.strength);
  appendJsonSignedField(out, "dexterity", stats.dexterity);
  appendJsonSignedField(out, "guild", stats.guild);
  appendJsonSignedField(out, "true_guild", stats.trueGuild);
  appendJsonSignedField(out, "permanent_attitude", stats.permanentAttitude);
  appendJsonSignedField(out, "temporary_attitude", stats.temporaryAttitude);
}

void appendTypedMovementStatePayload(std::string& out, const Mmo::Net::ClientMovementPacket& packet) {
  appendJsonBoolField(out, "from_is_in_air", hasMovementFlag(packet, Mmo::Net::ClientMovementFromInAir));
  appendJsonBoolField(out, "from_is_falling", hasMovementFlag(packet, Mmo::Net::ClientMovementFromFalling));
  appendJsonBoolField(out, "from_is_falling_deep", hasMovementFlag(packet, Mmo::Net::ClientMovementFromFallingDeep));
  appendJsonBoolField(out, "from_is_slide", hasMovementFlag(packet, Mmo::Net::ClientMovementFromSlide));
  appendJsonBoolField(out, "from_is_jump", hasMovementFlag(packet, Mmo::Net::ClientMovementFromJump));
  appendJsonBoolField(out, "from_is_jump_up", hasMovementFlag(packet, Mmo::Net::ClientMovementFromJumpUp));
  appendJsonBoolField(out, "from_is_swim", hasMovementFlag(packet, Mmo::Net::ClientMovementFromSwim));
  appendJsonBoolField(out, "from_is_dive", hasMovementFlag(packet, Mmo::Net::ClientMovementFromDive));
  appendJsonBoolField(out, "from_is_in_water", hasMovementFlag(packet, Mmo::Net::ClientMovementFromInWater));
  appendJsonBoolField(out, "to_is_in_air", hasMovementFlag(packet, Mmo::Net::ClientMovementToInAir));
  appendJsonBoolField(out, "to_is_falling", hasMovementFlag(packet, Mmo::Net::ClientMovementToFalling));
  appendJsonBoolField(out, "to_is_falling_deep", hasMovementFlag(packet, Mmo::Net::ClientMovementToFallingDeep));
  appendJsonBoolField(out, "to_is_slide", hasMovementFlag(packet, Mmo::Net::ClientMovementToSlide));
  appendJsonBoolField(out, "to_is_jump", hasMovementFlag(packet, Mmo::Net::ClientMovementToJump));
  appendJsonBoolField(out, "to_is_jump_up", hasMovementFlag(packet, Mmo::Net::ClientMovementToJumpUp));
  appendJsonBoolField(out, "to_is_swim", hasMovementFlag(packet, Mmo::Net::ClientMovementToSwim));
  appendJsonBoolField(out, "to_is_dive", hasMovementFlag(packet, Mmo::Net::ClientMovementToDive));
  appendJsonBoolField(out, "to_is_in_water", hasMovementFlag(packet, Mmo::Net::ClientMovementToInWater));
}

[[nodiscard]] std::string typedMovementPayloadJson(const Mmo::Net::ClientMovementPacket& packet) {
  std::string out;
  out.reserve(1600);
  out += "{\"source\":\"typed_udp_client_movement\"";
  if(!packet.source.empty())
    appendJsonField(out, "client_source", packet.source);
  if(!packet.actorKey.empty())
    appendJsonField(out, "actor_key", packet.actorKey);
  if(!packet.characterKey.empty())
    appendJsonField(out, "character_key", packet.characterKey);
  appendJsonField(out, "target_key", packet.targetKey);
  if(!packet.world.empty())
    appendJsonField(out, "world", packet.world);
  appendJsonField(out, "current_waypoint_key", packet.waypointKey);
  if(!packet.reason.empty())
    appendJsonField(out, "reason", packet.reason);
  appendJsonField(out, "vertical_axis", "y");
  appendTypedMovementStatsPayload(out, packet.stats);

  if(packet.kind == Mmo::SemanticActionKind::MovementProposal) {
    appendJsonSignedField(out, "proposal_version", 1);
    appendJsonField(out, "input_model", "checkpoint_delta_v1");
    appendJsonField(out, "movement_intent", "delta_transform");
    appendJsonNumberField(out, "from_tick", packet.fromTick);
    appendJsonNumberField(out, "to_tick", packet.toTick);
    appendJsonNumberField(out, "delta_ms", packet.deltaMs);
    appendJsonDoubleField(out, "from_pos_x", packet.fromX);
    appendJsonDoubleField(out, "from_pos_y", packet.fromY);
    appendJsonDoubleField(out, "from_pos_z", packet.fromZ);
    appendJsonDoubleField(out, "to_pos_x", packet.toX);
    appendJsonDoubleField(out, "to_pos_y", packet.toY);
    appendJsonDoubleField(out, "to_pos_z", packet.toZ);
    appendJsonDoubleField(out, "from_rotation_yaw", packet.fromYaw);
    appendJsonDoubleField(out, "to_rotation_yaw", packet.toYaw);
    appendTypedMovementStatePayload(out, packet);
    appendJsonNumberField(out, "proposal_interval_ms", packet.cadenceIntervalMs);
    appendJsonDoubleField(out, "proposal_min_distance", packet.cadenceMinDistance);
    appendJsonDoubleField(out, "proposal_min_yaw_deg", packet.cadenceMinYawDeg);
  } else {
    appendJsonDoubleField(out, "pos_x", packet.toX);
    appendJsonDoubleField(out, "pos_y", packet.toY);
    appendJsonDoubleField(out, "pos_z", packet.toZ);
    appendJsonDoubleField(out, "rotation_yaw", packet.toYaw);
    appendJsonDoubleField(out, "to_pos_x", packet.toX);
    appendJsonDoubleField(out, "to_pos_y", packet.toY);
    appendJsonDoubleField(out, "to_pos_z", packet.toZ);
    appendJsonDoubleField(out, "to_rotation_yaw", packet.toYaw);
    appendJsonNumberField(out, "checkpoint_interval_ms", packet.cadenceIntervalMs);
    appendJsonDoubleField(out, "checkpoint_min_distance", packet.cadenceMinDistance);
    appendJsonDoubleField(out, "checkpoint_min_yaw_deg", packet.cadenceMinYawDeg);
    appendJsonNumberField(out, "checkpoint_force_interval_ms", packet.checkpointForceIntervalMs);
  }

  appendJsonRawField(out,
                     "actor_position",
                     std::string("{\"x\":") + std::to_string(packet.toX) +
                       ",\"y\":" + std::to_string(packet.toY) +
                       ",\"z\":" + std::to_string(packet.toZ) + "}");
  out.push_back('}');
  return out;
}

[[nodiscard]] Mmo::Net::ClientActionPacket typedMovementAsClientAction(
    const Mmo::Net::ClientMovementPacket& packet) {
  Mmo::Net::ClientActionPacket out;
  out.kind = packet.kind;
  out.packetSequence = packet.packetSequence;
  out.clientTick = packet.clientTick;
  out.localSequence = packet.localSequence;
  out.sessionKey = packet.sessionKey;
  out.targetKey = packet.targetKey;
  out.idempotencyKey = packet.idempotencyKey;
  out.payloadJson = typedMovementPayloadJson(packet);
  return out;
}

[[nodiscard]] bool hasInventoryFlag(const Mmo::Net::ClientInventoryPacket& packet, std::uint32_t flag) noexcept {
  return (packet.flags & flag) != 0;
}

void appendJsonOptionalStringField(std::string& out, std::string_view key, const std::string& value) {
  if(!value.empty())
    appendJsonField(out, key, value);
}

void appendJsonOptionalSignedField(std::string& out, std::string_view key, std::int64_t value) {
  if(value >= 0)
    appendJsonSignedField(out, key, value);
}

void appendInventoryPositionObject(std::string& out,
                                   std::string_view key,
                                   double x,
                                   double y,
                                   double z) {
  appendJsonRawField(out,
                     key,
                     std::string("{\"x\":") + std::to_string(x) +
                       ",\"y\":" + std::to_string(y) +
                       ",\"z\":" + std::to_string(z) + "}");
}

[[nodiscard]] std::string inventoryItemTemplateKey(const Mmo::Net::ClientInventoryPacket& packet) {
  if(!packet.itemTemplateKey.empty())
    return packet.itemTemplateKey;
  if(packet.itemSymbol >= 0)
    return "item-template:" + std::to_string(packet.itemSymbol);
  return {};
}

[[nodiscard]] std::string inventoryNpcEntityKey(const Mmo::Net::ClientInventoryPacket& packet) {
  if(!packet.npcKey.empty())
    return packet.npcKey;
  if(!packet.targetNpcEntityKey.empty())
    return packet.targetNpcEntityKey;
  if(!packet.sourceNpcKey.empty())
    return packet.sourceNpcKey;
  return {};
}

[[nodiscard]] std::string typedInventoryPayloadJson(const Mmo::Net::ClientInventoryPacket& packet) {
  std::string out;
  out.reserve(2200);
  out += "{\"source\":\"typed_udp_client_inventory\"";

  const std::string itemTemplate = inventoryItemTemplateKey(packet);
  const std::string npcEntity = inventoryNpcEntityKey(packet);
  appendJsonOptionalStringField(out, "client_source", packet.source);
  appendJsonField(out, "target_key", packet.targetKey);
  appendJsonOptionalStringField(out, "actor_key", packet.actorKey);
  appendJsonOptionalStringField(out, "source_actor_key", packet.sourceActorKey);
  appendJsonOptionalStringField(out, "target_character_key", packet.targetCharacterKey);
  if(!packet.targetCharacterKey.empty())
    appendJsonField(out, "character_key", packet.targetCharacterKey);
  appendJsonOptionalStringField(out, "item_template_key", itemTemplate);
  appendJsonOptionalSignedField(out, "item_symbol", packet.itemSymbol);
  appendJsonOptionalSignedField(out, "item_template_symbol", packet.itemSymbol);
  appendJsonOptionalSignedField(out, "inventory_item_symbol", packet.inventoryItemSymbol);
  appendJsonOptionalStringField(out, "item_instance_id", packet.itemInstanceId);
  appendJsonOptionalStringField(out, "item_instance_uuid", packet.itemInstanceUuid);
  appendJsonOptionalSignedField(out, "item_persistent_id", packet.itemPersistentId);
  appendJsonOptionalSignedField(out, "item_instance_persistent_id", packet.itemPersistentId);
  appendJsonOptionalSignedField(out, "source_item_persistent_id", packet.sourceItemPersistentId);
  appendJsonOptionalSignedField(out, "source_world_item_persistent_id", packet.sourceWorldItemPersistentId);
  appendJsonOptionalSignedField(out, "world_item_persistent_id", packet.worldItemPersistentId);
  appendJsonOptionalSignedField(out, "vendor_item_persistent_id", packet.vendorItemPersistentId);
  appendJsonOptionalSignedField(out, "seller_item_persistent_id", packet.sellerItemPersistentId);
  appendJsonSignedField(out, "amount", packet.amount);
  appendJsonSignedField(out, "slot", packet.slot);
  appendJsonOptionalSignedField(out, "server_bag_index", packet.bagIndex);
  appendJsonOptionalSignedField(out, "bag_index", packet.bagIndex);
  appendJsonOptionalSignedField(out, "target_bag_index", packet.targetBagIndex);
  appendJsonOptionalStringField(out, "equipment_slot", packet.equipmentSlot);
  appendJsonOptionalStringField(out, "source_entity_key", packet.sourceEntityKey);
  appendJsonOptionalStringField(out, "source_container_key", packet.sourceContainerKey);
  appendJsonOptionalStringField(out, "container_key", packet.containerKey);
  appendJsonOptionalStringField(out, "source_npc_key", packet.sourceNpcKey);
  appendJsonOptionalStringField(out, "source_npc_entity_key", packet.sourceNpcKey);
  appendJsonOptionalStringField(out, "target_npc_entity_key", packet.targetNpcEntityKey);
  appendJsonOptionalStringField(out, "npc_key", packet.npcKey);
  appendJsonOptionalStringField(out, "npc_entity_key", npcEntity);
  appendJsonOptionalStringField(out, "world", packet.world);
  appendJsonOptionalStringField(out, "tag", packet.tag);
  appendJsonOptionalStringField(out, "focus_name", packet.focusName);
  appendJsonOptionalStringField(out, "display_name", packet.displayName);
  appendJsonOptionalStringField(out, "scheme", packet.scheme);
  appendJsonOptionalStringField(out, "reason", packet.reason);
  appendJsonOptionalStringField(out, "currency_key", packet.currencyKey);
  appendJsonNumberField(out, "slot_id", packet.slotId);
  appendJsonNumberField(out, "vob_id", packet.vobId);
  appendJsonSignedField(out, "unit_price", packet.unitPrice);
  appendJsonSignedField(out, "price_total", packet.priceTotal);
  appendJsonSignedField(out, "wallet_before", packet.walletBefore);
  appendJsonSignedField(out, "wallet_after", packet.walletAfter);
  appendJsonBoolField(out, "moved_whole_instance", hasInventoryFlag(packet, Mmo::Net::ClientInventoryMovedWholeInstance));
  appendJsonBoolField(out, "source_dead", hasInventoryFlag(packet, Mmo::Net::ClientInventorySourceDead));
  appendJsonBoolField(out, "source_unconscious", hasInventoryFlag(packet, Mmo::Net::ClientInventorySourceUnconscious));
  appendJsonBoolField(out, "container", hasInventoryFlag(packet, Mmo::Net::ClientInventoryContainer));

  if(hasInventoryFlag(packet, Mmo::Net::ClientInventoryHasActorPosition)) {
    appendInventoryPositionObject(out, "actor_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ);
    appendInventoryPositionObject(out, "looter_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ);
    appendInventoryPositionObject(out, "buyer_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ);
    appendInventoryPositionObject(out, "seller_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ);
  }
  if(hasInventoryFlag(packet, Mmo::Net::ClientInventoryHasItemPosition))
    appendInventoryPositionObject(out, "item_position", packet.itemPosX, packet.itemPosY, packet.itemPosZ);
  if(hasInventoryFlag(packet, Mmo::Net::ClientInventoryHasSourcePosition))
    appendInventoryPositionObject(out, "source_npc_position", packet.sourcePosX, packet.sourcePosY, packet.sourcePosZ);

  if(packet.kind == Mmo::SemanticActionKind::PickupWorldItem ||
     packet.kind == Mmo::SemanticActionKind::RemoveWorldItem ||
     packet.kind == Mmo::SemanticActionKind::DropCharacterItem) {
    appendJsonField(out, "world_item_entity_key", packet.targetKey);
    appendJsonField(out, "engine_world_item_key", packet.targetKey);
  }
  if(packet.kind == Mmo::SemanticActionKind::DropCharacterItem)
    appendJsonField(out, "dropped_world_item_key", packet.targetKey);
  out.push_back('}');
  return out;
}

[[nodiscard]] Mmo::Net::ClientActionPacket typedInventoryAsClientAction(
    const Mmo::Net::ClientInventoryPacket& packet) {
  Mmo::Net::ClientActionPacket out;
  out.kind = packet.kind;
  out.packetSequence = packet.packetSequence;
  out.clientTick = packet.clientTick;
  out.localSequence = packet.localSequence;
  out.sessionKey = packet.sessionKey;
  out.targetKey = packet.targetKey;
  out.idempotencyKey = packet.idempotencyKey;
  out.payloadJson = typedInventoryPayloadJson(packet);
  return out;
}

[[nodiscard]] bool hasWorldStateFlag(const Mmo::Net::ClientWorldStatePacket& packet, std::uint32_t flag) noexcept {
  return (packet.flags & flag) != 0;
}

void appendWorldStatePositionObject(std::string& out,
                                    std::string_view key,
                                    double x,
                                    double y,
                                    double z) {
  appendJsonRawField(out,
                     key,
                     std::string("{\"x\":") + std::to_string(x) +
                       ",\"y\":" + std::to_string(y) +
                       ",\"z\":" + std::to_string(z) + "}");
}

[[nodiscard]] std::string typedWorldStatePayloadJson(const Mmo::Net::ClientWorldStatePacket& packet) {
  std::string out;
  out.reserve(packet.subtitleText.size() + 2600);
  out += "{\"source\":\"typed_udp_client_world_state\"";
  appendJsonOptionalStringField(out, "client_source", packet.source);
  appendJsonField(out, "target_key", packet.targetKey);
  appendJsonOptionalStringField(out, "actor_key", packet.actorKey);
  appendJsonOptionalStringField(out, "character_key", packet.characterKey);
  appendJsonOptionalStringField(out, "world", packet.world);
  appendJsonOptionalStringField(out, "reason", packet.reason);
  appendJsonOptionalStringField(out, "resource_key", packet.resourceKey);
  appendJsonSignedField(out, "value_before", packet.valueBefore);
  appendJsonSignedField(out, "value_after", packet.valueAfter);
  appendJsonSignedField(out, "delta_amount", packet.valueDelta);
  appendJsonSignedField(out, "requested_delta", packet.valueDelta);

  appendJsonSignedField(out, "level_before", packet.valueBefore);
  appendJsonSignedField(out, "level_after", packet.valueAfter);
  appendJsonSignedField(out, "level_delta", packet.valueDelta);
  appendJsonSignedField(out, "experience_before", packet.secondaryBefore);
  appendJsonSignedField(out, "experience_after", packet.secondaryAfter);
  appendJsonSignedField(out, "experience_delta", packet.secondaryDelta);
  appendJsonSignedField(out, "learning_points_before", packet.tertiaryBefore);
  appendJsonSignedField(out, "learning_points_after", packet.tertiaryAfter);
  appendJsonSignedField(out, "learning_points_delta", packet.tertiaryDelta);

  appendJsonOptionalSignedField(out, "symbol_index", packet.symbolIndex);
  appendJsonOptionalSignedField(out, "value_index", packet.valueIndex);
  appendJsonOptionalSignedField(out, "script_function_symbol", packet.scriptFunctionSymbol);
  appendJsonOptionalStringField(out, "script_function_name", packet.scriptFunctionName);
  appendJsonOptionalStringField(out, "script_key", packet.scriptKey);
  appendJsonOptionalStringField(out, "global_key", packet.globalKey);
  appendJsonOptionalStringField(out, "symbol_name", packet.symbolName);
  appendJsonOptionalStringField(out, "quest_key", packet.questKey);
  appendJsonOptionalStringField(out, "quest_name", packet.questName);
  appendJsonOptionalStringField(out, "status", packet.status);
  appendJsonSignedField(out, "entry_count", packet.entryCount);
  appendJsonRawField(out, "entries", "[]");
  appendJsonOptionalStringField(out, "npc_key", packet.npcKey);
  appendJsonOptionalSignedField(out, "npc_symbol", packet.npcSymbol);
  appendJsonOptionalStringField(out, "npc_symbol_name", packet.npcSymbolName);
  appendJsonOptionalStringField(out, "info_key", packet.infoKey);
  appendJsonOptionalSignedField(out, "info_symbol", packet.infoSymbol);
  appendJsonOptionalStringField(out, "info_symbol_name", packet.infoSymbolName);
  appendJsonBoolField(out, "known", hasWorldStateFlag(packet, Mmo::Net::ClientWorldStateKnown));
  appendJsonBoolField(out, "removed", hasWorldStateFlag(packet, Mmo::Net::ClientWorldStateRemoved));

  appendJsonOptionalStringField(out, "interactive_key", packet.interactiveKey.empty() ? packet.entityKey : packet.interactiveKey);
  appendJsonOptionalStringField(out, "interactive_entity_key", packet.interactiveKey.empty() ? packet.entityKey : packet.interactiveKey);
  appendJsonOptionalStringField(out, "entity_key", packet.entityKey);
  appendJsonOptionalStringField(out, "tag", packet.tag);
  appendJsonOptionalStringField(out, "focus_name", packet.focusName);
  appendJsonOptionalStringField(out, "display_name", packet.displayName);
  appendJsonOptionalStringField(out, "scheme", packet.scheme);
  appendJsonSignedField(out, "slot_id", packet.slotId);
  appendJsonSignedField(out, "vob_id", packet.vobId);
  appendJsonSignedField(out, "state_before", packet.stateBefore);
  appendJsonSignedField(out, "state_after", packet.stateAfter);
  appendJsonSignedField(out, "state", packet.stateAfter);
  appendJsonSignedField(out, "state_count", packet.stateCount);
  appendJsonSignedField(out, "state_mask", packet.stateMask);
  appendJsonBoolField(out, "locked_before", hasWorldStateFlag(packet, Mmo::Net::ClientWorldStateLockedBefore));
  appendJsonBoolField(out, "locked_after", hasWorldStateFlag(packet, Mmo::Net::ClientWorldStateLockedAfter));
  appendJsonBoolField(out, "cracked_before", hasWorldStateFlag(packet, Mmo::Net::ClientWorldStateCrackedBefore));
  appendJsonBoolField(out, "cracked_after", hasWorldStateFlag(packet, Mmo::Net::ClientWorldStateCrackedAfter));
  appendJsonBoolField(out, "container", hasWorldStateFlag(packet, Mmo::Net::ClientWorldStateContainer));
  appendJsonBoolField(out, "door", hasWorldStateFlag(packet, Mmo::Net::ClientWorldStateDoor));
  appendJsonBoolField(out, "ladder", hasWorldStateFlag(packet, Mmo::Net::ClientWorldStateLadder));

  appendJsonOptionalStringField(out, "trigger_key", packet.entityKey.empty() ? packet.targetKey : packet.entityKey);
  appendJsonOptionalStringField(out, "mover_key", packet.entityKey.empty() ? packet.targetKey : packet.entityKey);
  appendJsonOptionalStringField(out, "trigger_name", packet.triggerName);
  appendJsonOptionalStringField(out, "target_name", packet.triggerTargetName);
  appendJsonOptionalStringField(out, "event_target", packet.eventTarget);
  appendJsonOptionalStringField(out, "event_emitter", packet.eventEmitter);
  appendJsonSignedField(out, "event_type", packet.eventType);
  appendJsonOptionalStringField(out, "event_type_name", packet.eventTypeName);
  appendJsonSignedField(out, "frame", packet.frame);
  appendJsonSignedField(out, "frame_index", packet.frame);
  appendJsonSignedField(out, "target_frame", packet.targetFrame);
  appendJsonSignedField(out, "target_frame_index", packet.targetFrame);
  appendJsonOptionalStringField(out, "state_before_name", packet.stateBeforeName);
  appendJsonOptionalStringField(out, "state_after_name", packet.stateAfterName);
  appendJsonOptionalStringField(out, "new_weapon_state", packet.stateAfterName);
  appendJsonOptionalStringField(out, "previous_weapon_state", packet.stateBeforeName);
  appendJsonBoolField(out, "ready", hasWorldStateFlag(packet, Mmo::Net::ClientWorldStateReady));

  appendJsonSignedField(out, "world_time_before_ms", packet.worldTimeBeforeMs);
  appendJsonSignedField(out, "world_time_after_ms", packet.worldTimeAfterMs);
  appendJsonSignedField(out, "time_delta_ms", packet.worldTimeAfterMs - packet.worldTimeBeforeMs);
  appendJsonSignedField(out, "world_day_before", packet.worldDayBefore);
  appendJsonSignedField(out, "world_day_after", packet.worldDayAfter);
  appendJsonSignedField(out, "world_hour_before", packet.worldHourBefore);
  appendJsonSignedField(out, "world_hour_after", packet.worldHourAfter);
  appendJsonSignedField(out, "world_minute_before", packet.worldMinuteBefore);
  appendJsonSignedField(out, "world_minute_after", packet.worldMinuteAfter);

  appendJsonOptionalStringField(out, "conversation_key", packet.conversationKey);
  appendJsonOptionalStringField(out, "sync_group", packet.syncGroup);
  appendJsonOptionalStringField(out, "speaker_key", packet.speakerKey);
  appendJsonOptionalStringField(out, "listener_key", packet.listenerKey);
  appendJsonOptionalStringField(out, "output_name", packet.outputName);
  appendJsonOptionalStringField(out, "message_name", packet.messageName);
  appendJsonOptionalStringField(out, "subtitle_text", packet.subtitleText);
  appendJsonSignedField(out, "line_duration_ms", packet.durationMs);

  if(hasWorldStateFlag(packet, Mmo::Net::ClientWorldStateHasActorPosition)) {
    appendWorldStatePositionObject(out, "actor_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ);
    appendWorldStatePositionObject(out, "speaker_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ);
  }
  if(hasWorldStateFlag(packet, Mmo::Net::ClientWorldStateHasTargetPosition))
    appendWorldStatePositionObject(out, "target_position", packet.targetPosX, packet.targetPosY, packet.targetPosZ);
  if(hasWorldStateFlag(packet, Mmo::Net::ClientWorldStateHasSourcePosition)) {
    appendWorldStatePositionObject(out, "source_position", packet.sourcePosX, packet.sourcePosY, packet.sourcePosZ);
    appendWorldStatePositionObject(out, "listener_position", packet.sourcePosX, packet.sourcePosY, packet.sourcePosZ);
  }

  out.push_back('}');
  return out;
}

[[nodiscard]] Mmo::Net::ClientActionPacket typedWorldStateAsClientAction(
    const Mmo::Net::ClientWorldStatePacket& packet) {
  Mmo::Net::ClientActionPacket out;
  out.kind = packet.kind;
  out.packetSequence = packet.packetSequence;
  out.clientTick = packet.clientTick;
  out.localSequence = packet.localSequence;
  out.sessionKey = packet.sessionKey;
  out.targetKey = packet.targetKey;
  out.idempotencyKey = packet.idempotencyKey;
  out.payloadJson = typedWorldStatePayloadJson(packet);
  return out;
}

[[nodiscard]] bool hasNpcStateFlag(const Mmo::Net::ClientNpcStatePacket& packet, std::uint32_t flag) noexcept {
  return (packet.flags & flag) != 0;
}

void appendNpcStatePositionObject(std::string& out,
                                  std::string_view key,
                                  double x,
                                  double y,
                                  double z) {
  appendJsonRawField(out,
                     key,
                     std::string("{\"x\":") + std::to_string(x) +
                       ",\"y\":" + std::to_string(y) +
                       ",\"z\":" + std::to_string(z) + "}");
}

[[nodiscard]] const std::string& firstNonEmpty(const std::string& a,
                                               const std::string& b,
                                               const std::string& fallback) noexcept {
  if(!a.empty())
    return a;
  if(!b.empty())
    return b;
  return fallback;
}

[[nodiscard]] std::string typedNpcStatePayloadJson(const Mmo::Net::ClientNpcStatePacket& packet) {
  std::string out;
  out.reserve(3200);
  out += "{\"source\":\"typed_udp_client_npc_state\"";

  const std::string npcEntity = firstNonEmpty(packet.npcEntityKey, packet.targetNpcEntityKey, packet.targetKey);
  const std::string targetNpcEntity = firstNonEmpty(packet.targetNpcEntityKey, packet.npcEntityKey, packet.targetKey);
  appendJsonOptionalStringField(out, "client_source", packet.source);
  appendJsonField(out, "target_key", packet.targetKey);
  appendJsonOptionalStringField(out, "reason", packet.reason);
  appendJsonOptionalStringField(out, "world", packet.world);
  appendJsonOptionalStringField(out, "actor_key", packet.actorKey.empty() ? npcEntity : packet.actorKey);
  appendJsonOptionalStringField(out, "npc_entity_key", npcEntity);
  appendJsonOptionalStringField(out, "actor_npc_entity_key", npcEntity);
  appendJsonOptionalStringField(out, "actor_npc_key", packet.npcKey);
  appendJsonOptionalStringField(out, "npc_key", packet.npcKey);
  appendJsonOptionalStringField(out, "target_npc_entity_key", targetNpcEntity);
  appendJsonOptionalStringField(out, "target_world_entity_key", targetNpcEntity);
  appendJsonOptionalStringField(out, "target_npc_key", packet.targetNpcKey);
  appendJsonOptionalStringField(out, "source_npc_entity_key", packet.sourceNpcEntityKey);
  appendJsonOptionalStringField(out, "source_actor_entity_key", packet.sourceActorKey.empty() ? packet.sourceNpcEntityKey : packet.sourceActorKey);
  appendJsonOptionalStringField(out, "source_actor_key", packet.sourceActorKey);
  appendJsonOptionalStringField(out, "display_name", packet.displayName);
  appendJsonOptionalStringField(out, "actor_npc_display_name", packet.displayName);
  appendJsonOptionalStringField(out, "target_npc_display_name", packet.targetDisplayName.empty() ? packet.displayName : packet.targetDisplayName);
  appendJsonOptionalSignedField(out, "npc_persistent_id", packet.npcPersistentId);
  appendJsonOptionalSignedField(out, "actor_npc_persistent_id", packet.npcPersistentId);
  appendJsonOptionalSignedField(out, "target_npc_persistent_id", packet.targetNpcPersistentId >= 0 ? packet.targetNpcPersistentId : packet.npcPersistentId);
  appendJsonOptionalSignedField(out, "source_npc_persistent_id", packet.sourceNpcPersistentId);
  appendJsonOptionalSignedField(out, "npc_symbol", packet.npcSymbol);
  appendJsonOptionalSignedField(out, "actor_npc_symbol", packet.npcSymbol);
  appendJsonOptionalSignedField(out, "target_npc_symbol", packet.targetNpcSymbol >= 0 ? packet.targetNpcSymbol : packet.npcSymbol);
  appendJsonOptionalSignedField(out, "source_npc_symbol", packet.sourceNpcSymbol);
  appendJsonSignedField(out, "health_current", packet.healthCurrent);
  appendJsonSignedField(out, "health_max", packet.healthMax);
  appendJsonBoolField(out, "dead", hasNpcStateFlag(packet, Mmo::Net::ClientNpcStateDead));
  appendJsonBoolField(out, "unconscious", hasNpcStateFlag(packet, Mmo::Net::ClientNpcStateUnconscious));
  appendJsonBoolField(out, "down", hasNpcStateFlag(packet, Mmo::Net::ClientNpcStateDown));

  appendJsonOptionalStringField(out, "routine_state", packet.routineState);
  appendJsonOptionalStringField(out, "schedule_key", packet.scheduleKey);
  appendJsonOptionalStringField(out, "current_waypoint_key", packet.currentWaypointKey);
  appendJsonOptionalStringField(out, "current_waypoint_name", packet.currentWaypointName);
  appendJsonOptionalStringField(out, "current_waypoint", packet.currentWaypointLegacy);
  appendJsonOptionalStringField(out, "target_waypoint_key", packet.targetWaypointKey);
  appendJsonOptionalStringField(out, "target_waypoint_name", packet.targetWaypointName);
  appendJsonOptionalStringField(out, "target_waypoint", packet.targetWaypointLegacy);
  appendJsonOptionalStringField(out, "next_waypoint_key", packet.nextWaypointKey);
  appendJsonOptionalStringField(out, "next_waypoint_name", packet.nextWaypointName);
  appendJsonOptionalStringField(out, "next_waypoint", packet.nextWaypointLegacy);

  appendJsonOptionalStringField(out, "ai_state", packet.aiState);
  appendJsonSignedField(out, "ai_state_function", packet.aiStateFunction);
  appendJsonOptionalStringField(out, "ai_intent", packet.aiIntent);
  appendJsonOptionalStringField(out, "ai_target_key", packet.aiTargetKey);
  appendJsonOptionalStringField(out, "perception_state", packet.perceptionState);

  appendJsonOptionalStringField(out, "action_key", packet.actionKey);
  appendJsonOptionalStringField(out, "action_name", packet.actionName);
  appendJsonOptionalStringField(out, "action_state", packet.actionState);
  appendJsonOptionalStringField(out, "action_target_key", packet.actionTargetKey);
  appendJsonOptionalStringField(out, "sync_group", packet.syncGroup);
  appendJsonOptionalStringField(out, "path_state", packet.pathState);
  appendJsonOptionalStringField(out, "route_key", packet.routeKey);
  appendJsonSignedField(out, "remaining_path_points", packet.remainingPathPoints);
  appendJsonOptionalStringField(out, "move_hint", packet.moveHint);

  appendJsonOptionalStringField(out, "opponent_key", packet.opponentKey);
  appendJsonOptionalStringField(out, "target_entity_key", packet.opponentKey);
  appendJsonOptionalStringField(out, "fight_state", packet.fightState);
  appendJsonOptionalStringField(out, "attack_state", packet.attackState);
  appendJsonSignedField(out, "combo_index", packet.comboIndex);
  appendJsonOptionalStringField(out, "animation_name", packet.animationName);
  appendJsonOptionalStringField(out, "attack_animation_name", packet.attackAnimationName);
  appendJsonSignedField(out, "animation_elapsed_ms", packet.animationElapsedMs);
  appendJsonSignedField(out, "attack_animation_elapsed_ms", packet.attackAnimationElapsedMs);
  appendJsonSignedField(out, "animation_total_ms", packet.animationTotalMs);
  appendJsonSignedField(out, "attack_total_ms", packet.attackTotalMs);
  appendJsonSignedField(out, "attack_optimal_ms", packet.attackOptimalMs);
  appendJsonSignedField(out, "attack_hit_end_ms", packet.attackHitEndMs);
  appendJsonSignedField(out, "parry_window_start_ms", packet.parryWindowStartMs);
  appendJsonSignedField(out, "parry_window_end_ms", packet.parryWindowEndMs);
  appendJsonSignedField(out, "combo_window_start_ms", packet.comboWindowStartMs);
  appendJsonSignedField(out, "combo_window_end_ms", packet.comboWindowEndMs);
  appendJsonSignedField(out, "body_state", packet.bodyState);
  appendJsonOptionalStringField(out, "weapon_state", packet.weaponState);
  appendJsonSignedField(out, "weapon_state_id", packet.weaponStateId);
  appendJsonBoolField(out, "attack_anim", hasNpcStateFlag(packet, Mmo::Net::ClientNpcStateAttackAnim));
  appendJsonBoolField(out, "prehit", hasNpcStateFlag(packet, Mmo::Net::ClientNpcStatePrehit));
  appendJsonBoolField(out, "actor_running", hasNpcStateFlag(packet, Mmo::Net::ClientNpcStateActorRunning));
  appendJsonBoolField(out, "opponent_running", hasNpcStateFlag(packet, Mmo::Net::ClientNpcStateOpponentRunning));
  appendJsonBoolField(out, "opponent_prehit", hasNpcStateFlag(packet, Mmo::Net::ClientNpcStateOpponentPrehit));
  appendJsonDoubleField(out, "attacker_yaw_rad", packet.attackerYawRad);
  appendJsonDoubleField(out, "opponent_yaw_rad", packet.opponentYawRad);
  appendJsonDoubleField(out, "weapon_range", packet.weaponRange);
  appendJsonDoubleField(out, "attack_range", packet.attackRange);
  appendJsonDoubleField(out, "opponent_attack_range", packet.opponentAttackRange);
  appendJsonDoubleField(out, "attacker_fight_range_base", packet.attackerFightRangeBase);
  appendJsonDoubleField(out, "opponent_fight_range_base", packet.opponentFightRangeBase);

  appendJsonOptionalStringField(out, "combat_action", packet.combatAction);
  appendJsonOptionalStringField(out, "intent_state", packet.intentState);

  if(hasNpcStateFlag(packet, Mmo::Net::ClientNpcStateHasPosition)) {
    appendJsonDoubleField(out, "pos_x", packet.posX);
    appendJsonDoubleField(out, "pos_y", packet.posY);
    appendJsonDoubleField(out, "pos_z", packet.posZ);
    appendNpcStatePositionObject(out, "position", packet.posX, packet.posY, packet.posZ);
    appendNpcStatePositionObject(out, "actor_position", packet.posX, packet.posY, packet.posZ);
  }
  if(hasNpcStateFlag(packet, Mmo::Net::ClientNpcStateHasTargetPosition)) {
    appendNpcStatePositionObject(out, "target_position", packet.targetPosX, packet.targetPosY, packet.targetPosZ);
    appendNpcStatePositionObject(out, "npc_position", packet.targetPosX, packet.targetPosY, packet.targetPosZ);
  }
  if(hasNpcStateFlag(packet, Mmo::Net::ClientNpcStateHasAttackerCenter))
    appendNpcStatePositionObject(out, "attacker_center", packet.attackerCenterX, packet.attackerCenterY, packet.attackerCenterZ);
  if(hasNpcStateFlag(packet, Mmo::Net::ClientNpcStateHasOpponentCenter))
    appendNpcStatePositionObject(out, "opponent_center", packet.opponentCenterX, packet.opponentCenterY, packet.opponentCenterZ);
  if(hasNpcStateFlag(packet, Mmo::Net::ClientNpcStateHasFightDistance))
    appendNpcStatePositionObject(out, "fight_distance", packet.fightDistanceX, packet.fightDistanceY, packet.fightDistanceZ);

  out.push_back('}');
  return out;
}

[[nodiscard]] Mmo::Net::ClientActionPacket typedNpcStateAsClientAction(
    const Mmo::Net::ClientNpcStatePacket& packet) {
  Mmo::Net::ClientActionPacket out;
  out.kind = packet.kind;
  out.packetSequence = packet.packetSequence;
  out.clientTick = packet.clientTick;
  out.localSequence = packet.localSequence;
  out.sessionKey = packet.sessionKey;
  out.targetKey = packet.targetKey;
  out.idempotencyKey = packet.idempotencyKey;
  out.payloadJson = typedNpcStatePayloadJson(packet);
  return out;
}

[[nodiscard]] bool hasEconomyFlag(const Mmo::Net::ClientEconomyPacket& packet, std::uint32_t flag) noexcept {
  return (packet.flags & flag) != 0;
}

[[nodiscard]] bool hasSessionControlFlag(const Mmo::Net::ClientSessionControlPacket& packet, std::uint32_t flag) noexcept {
  return (packet.flags & flag) != 0;
}

[[nodiscard]] bool hasDialogStateFlag(const Mmo::Net::ClientDialogStatePacket& packet, std::uint32_t flag) noexcept {
  return (packet.flags & flag) != 0;
}

[[nodiscard]] bool hasCharacterEventFlag(const Mmo::Net::ClientCharacterEventPacket& packet, std::uint32_t flag) noexcept {
  return (packet.flags & flag) != 0;
}

[[nodiscard]] std::string typedEconomyPayloadJson(const Mmo::Net::ClientEconomyPacket& packet) {
  std::string out;
  out.reserve(900);
  out += "{\"source\":\"typed_udp_client_economy\"";
  appendJsonOptionalStringField(out, "client_source", packet.source);
  appendJsonField(out, "target_key", packet.targetKey);
  appendJsonOptionalStringField(out, "actor_key", packet.actorKey);
  appendJsonOptionalStringField(out, "character_key", packet.characterKey);
  appendJsonField(out, "currency_key", packet.currencyKey.empty() ? std::string("g2notr:gold") : packet.currencyKey);
  appendJsonOptionalStringField(out, "currency_display_name", packet.currencyDisplayName);
  appendJsonOptionalStringField(out, "world", packet.world);
  appendJsonOptionalStringField(out, "reason", packet.reason);
  appendJsonSignedField(out, "amount", packet.amount);
  appendJsonSignedField(out, "delta_amount", packet.deltaAmount);
  appendJsonSignedField(out, "delta", packet.deltaAmount);
  appendJsonSignedField(out, "value_before", packet.walletBefore);
  appendJsonSignedField(out, "value_after", packet.walletAfter);
  appendJsonSignedField(out, "wallet_before", packet.walletBefore);
  appendJsonSignedField(out, "wallet_after", packet.walletAfter);
  appendJsonOptionalSignedField(out, "item_template_symbol", packet.itemTemplateSymbol);
  appendJsonBoolField(out, "has_wallet_before", hasEconomyFlag(packet, Mmo::Net::ClientEconomyHasWalletBefore));
  appendJsonBoolField(out, "has_wallet_after", hasEconomyFlag(packet, Mmo::Net::ClientEconomyHasWalletAfter));
  if(hasEconomyFlag(packet, Mmo::Net::ClientEconomyHasActorPosition)) {
    appendJsonRawField(out,
                       "actor_position",
                       std::string("{\"x\":") + std::to_string(packet.actorPosX) +
                         ",\"y\":" + std::to_string(packet.actorPosY) +
                         ",\"z\":" + std::to_string(packet.actorPosZ) + "}");
  }
  out.push_back('}');
  return out;
}

[[nodiscard]] Mmo::Net::ClientActionPacket typedEconomyAsClientAction(
    const Mmo::Net::ClientEconomyPacket& packet) {
  Mmo::Net::ClientActionPacket out;
  out.kind = packet.kind;
  out.packetSequence = packet.packetSequence;
  out.clientTick = packet.clientTick;
  out.localSequence = packet.localSequence;
  out.sessionKey = packet.sessionKey;
  out.targetKey = packet.targetKey;
  out.idempotencyKey = packet.idempotencyKey;
  out.payloadJson = typedEconomyPayloadJson(packet);
  return out;
}

[[nodiscard]] std::string typedSessionControlPayloadJson(const Mmo::Net::ClientSessionControlPacket& packet) {
  std::string out;
  out.reserve(1400);
  out += "{\"source\":\"typed_udp_client_session_control\"";
  appendJsonOptionalStringField(out, "client_source", packet.source);
  appendJsonOptionalStringField(out, "source_location", packet.sourceLocation);
  appendJsonField(out, "target_key", packet.targetKey);
  appendJsonOptionalStringField(out, "actor_key", packet.actorKey);
  appendJsonOptionalStringField(out, "character_key", packet.characterKey);
  appendJsonOptionalStringField(out, "display_name", packet.displayName);
  appendJsonOptionalStringField(out, "world", packet.world);
  appendJsonSignedField(out, "server_tick", packet.serverTick);
  appendJsonOptionalStringField(out, "server_endpoint", packet.serverEndpoint);
  appendJsonOptionalStringField(out, "client_content_manifest_hash", packet.clientContentManifestHash);
  appendJsonOptionalStringField(out, "reason", packet.reason);
  appendJsonBoolField(out,
                      "server_bound_client_mode",
                      hasSessionControlFlag(packet, Mmo::Net::ClientSessionControlServerBoundClientMode));
  appendJsonOptionalStringField(out, "action_kind", packet.actionKind);
  appendJsonSignedField(out, "client_local_sequence", packet.acknowledgedLocalSequence);
  appendJsonOptionalStringField(out, "manifest_key", packet.manifestKey);
  appendJsonOptionalStringField(out, "checkpoint_kind", packet.checkpointKind);
  appendJsonOptionalStringField(out, "save_slot_key", packet.saveSlotKey);
  appendJsonOptionalStringField(out, "slot_path", packet.slotPath);
  appendJsonOptionalStringField(out, "native_save_path", packet.nativeSavePath);
  appendJsonOptionalStringField(out, "slot_display_name", packet.slotDisplayName);
  appendJsonOptionalStringField(out, "client_world_name", packet.clientWorldName);
  appendJsonBoolField(out,
                      "native_save_present",
                      hasSessionControlFlag(packet, Mmo::Net::ClientSessionControlNativeSavePresent));
  appendJsonBoolField(out,
                      "db_save_snapshot_requested",
                      hasSessionControlFlag(packet, Mmo::Net::ClientSessionControlDbSaveSnapshotRequested));
  out.push_back('}');
  return out;
}

[[nodiscard]] Mmo::Net::ClientActionPacket typedSessionControlAsClientAction(
    const Mmo::Net::ClientSessionControlPacket& packet) {
  Mmo::Net::ClientActionPacket out;
  out.kind = packet.kind;
  out.packetSequence = packet.packetSequence;
  out.clientTick = packet.clientTick;
  out.localSequence = packet.localSequence;
  out.sessionKey = packet.sessionKey;
  out.targetKey = packet.targetKey;
  out.idempotencyKey = packet.idempotencyKey;
  out.payloadJson = typedSessionControlPayloadJson(packet);
  return out;
}

[[nodiscard]] std::string typedDialogStatePayloadJson(const Mmo::Net::ClientDialogStatePacket& packet) {
  std::string out;
  out.reserve(packet.subtitleText.size() + 1200);
  out += "{\"source\":\"typed_udp_client_dialog_state\"";
  appendJsonOptionalStringField(out, "client_source", packet.source);
  appendJsonField(out, "target_key", packet.targetKey);
  appendJsonOptionalStringField(out, "actor_key", packet.actorKey);
  appendJsonOptionalStringField(out, "character_key", packet.characterKey);
  appendJsonOptionalStringField(out, "world", packet.world);
  appendJsonOptionalStringField(out, "reason", packet.reason);
  appendJsonOptionalStringField(out, "npc_key", packet.npcKey);
  appendJsonOptionalStringField(out, "speaker_npc_key", packet.npcKey);
  appendJsonOptionalStringField(out, "npc_symbol_name", packet.npcSymbolName);
  appendJsonOptionalSignedField(out, "npc_symbol", packet.npcSymbol);
  appendJsonOptionalStringField(out, "info_key", packet.infoKey);
  appendJsonOptionalStringField(out, "dialog_key", packet.infoKey);
  appendJsonOptionalStringField(out, "info_symbol_name", packet.infoSymbolName);
  appendJsonOptionalSignedField(out, "info_symbol", packet.infoSymbol);
  appendJsonOptionalStringField(out, "conversation_key", packet.conversationKey);
  appendJsonOptionalStringField(out, "sync_group", packet.syncGroup);
  appendJsonOptionalStringField(out, "speaker_key", packet.speakerKey);
  appendJsonOptionalStringField(out, "listener_key", packet.listenerKey);
  appendJsonOptionalStringField(out, "output_name", packet.outputName);
  appendJsonOptionalStringField(out, "message_name", packet.messageName);
  appendJsonOptionalStringField(out, "subtitle_text", packet.subtitleText);
  appendJsonOptionalStringField(out, "line_text", packet.subtitleText);
  appendJsonOptionalStringField(out, "dialog_state", packet.dialogState);
  appendJsonOptionalStringField(out, "state", packet.dialogState);
  appendJsonOptionalStringField(out, "topic_key", packet.topicKey);
  appendJsonOptionalSignedField(out, "line_index", packet.lineIndex);
  appendJsonOptionalSignedField(out, "output_index", packet.outputIndex);
  appendJsonSignedField(out, "duration_ms", packet.durationMs);
  appendJsonBoolField(out, "known", hasDialogStateFlag(packet, Mmo::Net::ClientDialogStateKnown));
  appendJsonBoolField(out, "player_line", hasDialogStateFlag(packet, Mmo::Net::ClientDialogStatePlayerLine));
  appendJsonBoolField(out, "npc_line", hasDialogStateFlag(packet, Mmo::Net::ClientDialogStateNpcLine));
  appendJsonBoolField(out, "important", hasDialogStateFlag(packet, Mmo::Net::ClientDialogStateImportant));
  appendJsonBoolField(out, "ambient", hasDialogStateFlag(packet, Mmo::Net::ClientDialogStateAmbient));
  appendJsonBoolField(out, "has_line_index", hasDialogStateFlag(packet, Mmo::Net::ClientDialogStateHasLineIndex));
  appendJsonBoolField(out, "has_output_index", hasDialogStateFlag(packet, Mmo::Net::ClientDialogStateHasOutputIndex));
  out.push_back('}');
  return out;
}

[[nodiscard]] Mmo::Net::ClientActionPacket typedDialogStateAsClientAction(
    const Mmo::Net::ClientDialogStatePacket& packet) {
  Mmo::Net::ClientActionPacket out;
  out.kind = packet.kind;
  out.packetSequence = packet.packetSequence;
  out.clientTick = packet.clientTick;
  out.localSequence = packet.localSequence;
  out.sessionKey = packet.sessionKey;
  out.targetKey = packet.targetKey;
  out.idempotencyKey = packet.idempotencyKey;
  out.payloadJson = typedDialogStatePayloadJson(packet);
  return out;
}

[[nodiscard]] std::string typedCharacterEventPayloadJson(const Mmo::Net::ClientCharacterEventPacket& packet) {
  std::string out;
  out.reserve(1200);
  out += "{\"source\":\"typed_udp_client_character_event\"";
  appendJsonBoolField(out, "server_authoritative", true);
  appendJsonOptionalStringField(out, "client_source", packet.source);
  appendJsonField(out, "target_key", packet.targetKey);
  appendJsonOptionalStringField(out, "actor_key", packet.actorKey);
  appendJsonOptionalStringField(out, "character_key", packet.characterKey);
  appendJsonOptionalStringField(out, "world", packet.world);
  appendJsonOptionalStringField(out, "reason", packet.reason);
  appendJsonOptionalStringField(out, "resource_key", packet.resourceKey);
  appendJsonOptionalStringField(out, "resource_display_name", packet.resourceDisplayName);
  appendJsonOptionalStringField(out, "progression_key", packet.progressionKey);
  appendJsonOptionalStringField(out, "reward_key", packet.rewardKey);
  appendJsonOptionalStringField(out, "reward_source", packet.rewardKey);
  appendJsonOptionalStringField(out, "source_actor_key", packet.sourceActorKey);
  appendJsonOptionalStringField(out, "source_entity_key", packet.sourceEntityKey);

  appendJsonSignedField(out, "requested_delta", packet.requestedDelta);
  appendJsonSignedField(out, "delta_amount", packet.requestedDelta);
  appendJsonSignedField(out, "delta", packet.requestedDelta);
  appendJsonSignedField(out, "requested_amount", packet.requestedAmount);
  appendJsonSignedField(out, "amount", packet.requestedAmount);
  appendJsonSignedField(out, "mana_amount", packet.manaAmount);
  appendJsonSignedField(out, "mana_cost", packet.manaAmount);
  appendJsonSignedField(out, "reward_experience", packet.experienceReward);
  appendJsonSignedField(out, "experience_reward", packet.experienceReward);
  appendJsonSignedField(out, "experience_delta", packet.experienceReward);
  appendJsonSignedField(out, "reward_learning_points", packet.learningPointsReward);
  appendJsonSignedField(out, "learning_points_delta", packet.learningPointsReward);
  appendJsonOptionalSignedField(out, "attribute_symbol", packet.attributeSymbol);
  appendJsonOptionalSignedField(out, "skill_symbol", packet.skillSymbol);

  appendJsonBoolField(out,
                      "server_must_calculate",
                      hasCharacterEventFlag(packet, Mmo::Net::ClientCharacterEventServerMustCalculate));
  appendJsonBoolField(out,
                      "has_requested_delta",
                      hasCharacterEventFlag(packet, Mmo::Net::ClientCharacterEventHasRequestedDelta));
  appendJsonBoolField(out,
                      "has_requested_amount",
                      hasCharacterEventFlag(packet, Mmo::Net::ClientCharacterEventHasRequestedAmount));
  appendJsonBoolField(out,
                      "has_mana_amount",
                      hasCharacterEventFlag(packet, Mmo::Net::ClientCharacterEventHasManaAmount));
  appendJsonBoolField(out,
                      "has_experience_reward",
                      hasCharacterEventFlag(packet, Mmo::Net::ClientCharacterEventHasExperienceReward));
  appendJsonBoolField(out,
                      "has_learning_reward",
                      hasCharacterEventFlag(packet, Mmo::Net::ClientCharacterEventHasLearningReward));
  appendJsonBoolField(out,
                      "explicit_resource",
                      hasCharacterEventFlag(packet, Mmo::Net::ClientCharacterEventHasExplicitResource));
  if(hasCharacterEventFlag(packet, Mmo::Net::ClientCharacterEventHasActorPosition))
    appendWorldStatePositionObject(out, "actor_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ);
  out.push_back('}');
  return out;
}

[[nodiscard]] Mmo::Net::ClientActionPacket typedCharacterEventAsClientAction(
    const Mmo::Net::ClientCharacterEventPacket& packet) {
  Mmo::Net::ClientActionPacket out;
  out.kind = packet.kind;
  out.packetSequence = packet.packetSequence;
  out.clientTick = packet.clientTick;
  out.localSequence = packet.localSequence;
  out.sessionKey = packet.sessionKey;
  out.targetKey = packet.targetKey;
  out.idempotencyKey = packet.idempotencyKey;
  out.payloadJson = typedCharacterEventPayloadJson(packet);
  return out;
}

} // namespace

int main(int argc, char** argv) {
  try {
    const Options opt = parseArgs(argc, argv);
    Options activeOpt = opt;
    std::optional<MySqlTarget> mysql;
    std::string sessionUuid = opt.dbSessionUuid;
    if(opt.directDb || opt.enqueueOutbox) {
      if(opt.mysqlUrl.empty())
        throw std::runtime_error("--mysql-url is required when direct DB or outbox mode is enabled");
      mysql = parseMysqlUrl(opt.mysqlUrl);
      if(sessionUuid.empty())
        sessionUuid = dbLogin(*mysql, activeOpt);
      else
        (void)ensureActiveDbSession(*mysql, activeOpt, sessionUuid, "startup");
      validateClientContentManifestForSession(*mysql, sessionUuid, activeOpt, "startup");
      std::cout << "db_session=" << sessionUuid
                << " direct_db=" << (opt.directDb ? "on" : "off")
                << " enqueue_outbox=" << (opt.enqueueOutbox ? "on" : "off")
                << " npc_action_worker=" << (opt.npcActionWorker ? "on" : "off")
                << " npc_action_worker_max_per_packet=" << opt.npcActionWorkerMaxPerPacket
                << " content_manifest_gate=" << (opt.requireClientContentManifest ? "required" : (!opt.clientContentManifestHash.empty() ? "warn" : "off"))
                << " require_db_save_checkpoint_restore=" << (opt.requireDbSaveCheckpointRestore ? "on" : "off")
                << "\n";
    }

    const auto [bindHost, bindPort] = parseBind(opt.bind);
    asio::io_context io;
    asio::ip::udp::resolver resolver(io);
    asio::error_code ec;
    auto results = resolver.resolve(asio::ip::udp::v4(), bindHost, bindPort, ec);
    if(ec || results.empty())
      throw std::runtime_error("bind resolve failed: " + ec.message());

    asio::ip::udp::socket socket(io, results.begin()->endpoint());
    socket.non_blocking(true);
    std::signal(SIGINT, stopHandler);
    std::signal(SIGTERM, stopHandler);

    std::unordered_set<std::string> seen;
    std::array<char, Mmo::Net::MaxDatagramBytes> buffer {};
    std::uint64_t received = 0;
    std::uint64_t accepted = 0;
    std::uint64_t invalid = 0;
    std::uint64_t duplicate = 0;
    std::uint64_t enqueued = 0;
    std::uint64_t directDb = 0;
    std::uint64_t unhandled = 0;
    std::uint64_t failed = 0;
    std::uint64_t npcActionWorkerClaimed = 0;
    std::uint64_t npcActionWorkerApplied = 0;
    std::uint64_t npcActionWorkerFailed = 0;
    std::uint32_t nextSnapshotId = 1;
    ServerPacketLogState logState;
    LiveWorldSnapshotState liveWorldSnapshotState;

    std::cout << "listening udp://" << opt.bind << " binary_protocol=v1\n";
    while(gRunning.load(std::memory_order_relaxed)) {
      if(opt.maxPackets > 0 && static_cast<int>(received) >= opt.maxPackets)
        break;

      asio::ip::udp::endpoint remote;
      const auto n = socket.receive_from(asio::buffer(buffer), remote, 0, ec);
      if(ec) {
        if(ec == asio::error::would_block || ec == asio::error::try_again) {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
          continue;
        }
        if(ec == asio::error::connection_reset) {
          std::cout << "[udp_receive_ignored] error=connection_reset message=" << ec.message() << "\n";
          continue;
        }
        throw std::runtime_error("receive_from failed: " + ec.message());
      }
      ++received;

      const std::string_view bytes(buffer.data(), n);
      auto decoded = Mmo::Net::decodeClientActionPacket(bytes);
      Mmo::Net::ClientActionPacket packet;
      if(decoded.ok()) {
        packet = std::move(decoded.clientAction);
      } else if(auto combatDamage = Mmo::Net::decodeClientCombatDamagePacket(bytes); combatDamage.ok()) {
        packet = typedCombatDamageAsClientAction(combatDamage.combatDamage);
      } else if(auto movement = Mmo::Net::decodeClientMovementPacket(bytes); movement.ok()) {
        packet = typedMovementAsClientAction(movement.movement);
      } else if(auto inventory = Mmo::Net::decodeClientInventoryPacket(bytes); inventory.ok()) {
        packet = typedInventoryAsClientAction(inventory.inventory);
      } else if(auto dialogState = Mmo::Net::decodeClientDialogStatePacket(bytes); dialogState.ok()) {
        packet = typedDialogStateAsClientAction(dialogState.dialogState);
      } else if(auto characterEvent = Mmo::Net::decodeClientCharacterEventPacket(bytes); characterEvent.ok()) {
        packet = typedCharacterEventAsClientAction(characterEvent.characterEvent);
      } else if(auto worldState = Mmo::Net::decodeClientWorldStatePacket(bytes); worldState.ok()) {
        packet = typedWorldStateAsClientAction(worldState.worldState);
      } else if(auto npcState = Mmo::Net::decodeClientNpcStatePacket(bytes); npcState.ok()) {
        packet = typedNpcStateAsClientAction(npcState.npcState);
      } else if(auto economy = Mmo::Net::decodeClientEconomyPacket(bytes); economy.ok()) {
        packet = typedEconomyAsClientAction(economy.economy);
      } else if(auto sessionControl = Mmo::Net::decodeClientSessionControlPacket(bytes); sessionControl.ok()) {
        packet = typedSessionControlAsClientAction(sessionControl.sessionControl);
      } else {
        ++invalid;
        std::cout << "[invalid] remote=" << remote << " error=" << Mmo::Net::decodeErrorName(decoded.error);
        if(decoded.error == Mmo::Net::DecodeError::BadActionKind) {
          if(const auto raw = rawClientActionKind(bytes))
            std::cout << " raw_action_kind=" << *raw << " known_actions=" << Mmo::SemanticActionDefs.size();
        }
        std::cout << " datagram_bytes=" << n << "\n";
        continue;
      }

      const auto* def = Mmo::findSemanticAction(packet.kind);
      const bool isBootstrap = packet.kind == Mmo::SemanticActionKind::ClientBootstrapRequest;
      const bool isMovement = packet.kind == Mmo::SemanticActionKind::MovementProposal ||
                              packet.kind == Mmo::SemanticActionKind::CharacterCheckpoint;
      const bool isWeaponState = packet.kind == Mmo::SemanticActionKind::ReadyWeapon ||
                                 packet.kind == Mmo::SemanticActionKind::HolsterWeapon;
      if(!seen.insert(packet.idempotencyKey).second) {
        if(isBootstrap) {
          seen.clear();
          seen.insert(packet.idempotencyKey);
          std::cout << "[bootstrap_restarts_dedupe] session=" << packet.sessionKey << "\n";
        } else {
          ++duplicate;
          continue;
        }
      }

      bool packetAccepted = true;
      bool packetReady = false;
      std::string bootstrapSnapshotJson;
      std::string liveWorldSnapshotJson;
      std::string diagnosticReason;
      std::string diagnosticMessage;
      std::uint16_t diagnosticSeverity = 0;
      const std::string_view actionName = def ? def->actionKind : std::string_view("unknown");
      const std::string remoteText = remote.address().to_string() + ":" + std::to_string(remote.port());
      if(isBootstrap) {
        BootstrapReadiness readiness;
        const std::string characterKey = jsonStringField(packet.payloadJson, "character_key").value_or(activeOpt.characterKey);
        const std::string displayName = jsonStringField(packet.payloadJson, "display_name").value_or(characterKey);
        std::string worldName = jsonStringField(packet.payloadJson, "world").value_or("UNKNOWN");
        if(const auto clientHash = jsonStringField(packet.payloadJson, "client_content_manifest_hash"))
          activeOpt.clientContentManifestHash = *clientHash;
        if(mysql) {
          try {
            if(characterKey != activeOpt.characterKey) {
              activeOpt.characterKey = characterKey;
              activeOpt.characterDisplayName = displayName.empty() ? characterKey : displayName;
              sessionUuid = dbLogin(*mysql, activeOpt);
              seen.clear();
              seen.insert(packet.idempotencyKey);
              std::cout << "[db_session_character_selected]"
                        << " character=" << activeOpt.characterKey
                        << " session=" << sessionUuid << "\n";
            } else if(ensureActiveDbSession(*mysql, activeOpt, sessionUuid, "bootstrap")) {
              seen.clear();
              seen.insert(packet.idempotencyKey);
            }
            validateClientContentManifestForSession(*mysql, sessionUuid, activeOpt, "bootstrap");
            readiness = readBootstrapReadinessWithFallback(*mysql, characterKey, worldName, sessionUuid, worldName);
            packetReady = readiness.ready;
            printBootstrapAck(packet, characterKey, worldName, readiness, true);
            if(packetReady) {
              try {
                bootstrapSnapshotJson = buildBootstrapSnapshotJson(*mysql, sessionUuid, characterKey, worldName, readiness, true, opt.requireDbSaveCheckpointRestore);
              } catch(const std::exception& exc) {
                diagnosticSeverity = 2;
                diagnosticReason = opt.requireDbSaveCheckpointRestore ? "db_save_checkpoint_restore_required" : "bootstrap_snapshot_build_failed";
                diagnosticMessage = exc.what();
                if(opt.requireDbSaveCheckpointRestore) {
                  packetAccepted = false;
                  packetReady = false;
                  ++failed;
                }
                std::cerr << "[bootstrap_snapshot_build_failed] error=" << exc.what()
                          << " strict_db_save_checkpoint_restore=" << (opt.requireDbSaveCheckpointRestore ? 1 : 0) << "\n";
              }
            }
          } catch(const Mmo::Server::ContentManifestValidationError& exc) {
            packetAccepted = false;
            ++failed;
            diagnosticSeverity = 2;
            diagnosticReason = contentManifestDiagnosticReason(exc.decision);
            diagnosticMessage = contentManifestDiagnosticMessage(exc);
            appendContentManifestRejectAudit(remoteText, packet, sessionUuid, exc);
            try {
              Mmo::Server::recordContentManifestRejectAudit(*mysql, Mmo::Server::ContentManifestRejectAuditRecord {
                .sessionUuid = sessionUuid,
                .remoteEndpoint = remoteText,
                .packetSessionKey = packet.sessionKey,
                .targetKey = packet.targetKey,
                .packetSequence = packet.packetSequence,
                .localSequence = packet.localSequence,
                .phase = exc.phase,
                .reason = exc.decision,
                .clientManifestHash = exc.clientHash,
                .serverManifestHash = exc.serverHash,
                .contentRevisionKey = exc.revisionKey,
                .message = exc.what(),
                .payloadJson = diagnosticMessage,
              });
            } catch(const std::exception& auditError) {
              std::cerr << "[content_manifest_reject_db_audit_failed]"
                        << " reason=" << auditError.what()
                        << "\n";
            }
            std::cerr << "[bootstrap_rejected]"
                      << " reason=" << diagnosticReason
                      << " phase=" << exc.phase
                      << " client_hash=" << (exc.clientHash.empty() ? "<empty>" : exc.clientHash)
                      << " server_hash=" << (exc.serverHash.empty() ? "<empty>" : exc.serverHash)
                      << " revision=" << (exc.revisionKey.empty() ? "<empty>" : exc.revisionKey)
                      << "\n";
          } catch(const std::exception& exc) {
            packetAccepted = false;
            ++failed;
            diagnosticSeverity = 2;
            diagnosticReason = bootstrapFailureDiagnosticReason(exc.what());
            diagnosticMessage = exc.what();
            std::cerr << "[" << (diagnosticReason == "bootstrap_failed" ? "bootstrap_failed" : "bootstrap_rejected")
                      << "] reason=" << diagnosticReason
                      << " error=" << exc.what() << "\n";
          }
        } else {
          readiness.ready = true;
          packetReady = true;
          printBootstrapAck(packet, characterKey, worldName, readiness, false);
        }
      }

      const auto dbPayload = mysql ? makeDbPayload(packet, remoteText) : std::string();
      DirectApplyResult direct;
      if(mysql && opt.directDb && !isBootstrap) {
        try {
          if(!isActiveDbSession(*mysql, sessionUuid)) {
            (void)ensureActiveDbSession(*mysql, activeOpt, sessionUuid, "direct_db");
            seen.clear();
            seen.insert(packet.idempotencyKey);
          }
          direct = applyDirectDb(Mmo::Server::DirectApplyRequest {
            .target = *mysql,
            .sessionUuid = sessionUuid,
            .packet = packet,
            .dbPayload = dbPayload,
          });
          if(direct.handled) {
            ++directDb;
            packetAccepted = direct.accepted;
            packetReady = packetReady || direct.ready;
            if(!direct.accepted) {
              diagnosticSeverity = 1;
              diagnosticReason = direct.label;
              diagnosticMessage = "direct DB rejected semantic action";
            }
          }
        } catch(const std::exception& exc) {
          direct.handled = true;
          if(isFailOpenNpcObservationAction(packet.kind)) {
            direct.accepted = true;
            packetAccepted = true;
            std::cerr << "[direct_db_observation_failed_accepted] action=" << actionName
                      << " target=" << packet.targetKey
                      << " error=" << exc.what()
                      << " payload=" << packet.payloadJson
                      << "\n";
          } else {
            packetAccepted = false;
            ++failed;
            direct.accepted = false;
            diagnosticSeverity = 2;
            diagnosticReason = "direct_db_failed";
            diagnosticMessage = exc.what();
            std::cerr << "[direct_db_failed] action=" << actionName
                      << " target=" << packet.targetKey
                      << " error=" << exc.what()
                      << " payload=" << packet.payloadJson
                      << "\n";
          }
        }
      }

      if(mysql && opt.directDb && direct.handled && !direct.accepted) {
        try {
          recordClientActionCorrection(*mysql, sessionUuid, packet, actionName, direct.label, dbPayload);
          const std::string characterKey = jsonStringField(packet.payloadJson, "character_key").value_or(activeOpt.characterKey);
          std::string worldName = jsonStringField(packet.payloadJson, "world").value_or("UNKNOWN");
          auto readiness = readBootstrapReadinessWithFallback(*mysql, characterKey, worldName, sessionUuid, worldName);
          if(readiness.ready) {
            liveWorldSnapshotJson = buildBootstrapSnapshotJson(*mysql, sessionUuid, characterKey, worldName, readiness, false, false);
            std::cout << "[client_correction_snapshot_queued]"
                      << " action=" << actionName
                      << " reason=" << direct.label
                      << " local_sequence=" << packet.localSequence
                      << " bytes=" << liveWorldSnapshotJson.size()
                      << "\n";
          }
        } catch(const std::exception& exc) {
          std::cerr << "[client_correction_snapshot_failed] action=" << actionName
                    << " reason=" << direct.label
                    << " error=" << exc.what() << "\n";
        }
      }

      if(mysql && opt.directDb && shouldSendLiveWorldSnapshot(liveWorldSnapshotState, packet, packetAccepted, direct)) {
        try {
          const std::string characterKey = jsonStringField(packet.payloadJson, "character_key").value_or(activeOpt.characterKey);
          std::string worldName = jsonStringField(packet.payloadJson, "world").value_or("UNKNOWN");
          auto readiness = readBootstrapReadinessWithFallback(*mysql, characterKey, worldName, sessionUuid, worldName);
          if(readiness.ready) {
            liveWorldSnapshotJson = buildBootstrapSnapshotJson(*mysql, sessionUuid, characterKey, worldName, readiness, false, false);
            if(const auto pos = movementToPosition(packet.payloadJson)) {
              std::cout << "[live_world_item_snapshot_queued] reason=movement_interest"
                        << " x=" << pos->x
                        << " y=" << pos->y
                        << " z=" << pos->z
                        << " bytes=" << liveWorldSnapshotJson.size()
                        << "\n";
            } else {
              std::cout << "[live_world_item_snapshot_queued] reason=movement_interest bytes="
                        << liveWorldSnapshotJson.size() << "\n";
            }
          }
        } catch(const std::exception& exc) {
          std::cerr << "[live_world_item_snapshot_build_failed] action=" << actionName
                    << " error=" << exc.what() << "\n";
        }
      }

      if(mysql && opt.enqueueOutbox && !direct.handled && isFailOpenNpcObservationAction(packet.kind)) {
        direct.handled = true;
        direct.accepted = true;
        direct.ready = true;
        packetAccepted = true;
        packetReady = true;
        std::cerr << "[outbox_observation_ignored]"
                  << " action=" << actionName
                  << " target=" << packet.targetKey
                  << " reason=content_authority_db_contract_pending"
                  << "\n";
      } else if(mysql && opt.enqueueOutbox && !direct.handled && (!isBootstrap || opt.forwardBootstrapOutbox)) {
        try {
          enqueueOutbox(*mysql, sessionUuid, packet, dbPayload, opt.outboxPriority, opt.outboxMaxAttempts);
          ++enqueued;
        } catch(const std::exception& exc) {
          packetAccepted = false;
          ++failed;
          diagnosticSeverity = 2;
          diagnosticReason = "enqueue_failed";
          diagnosticMessage = exc.what();
          std::cerr << "[enqueue_failed] action=" << actionName << " error=" << exc.what() << "\n";
        }
      } else if(mysql && opt.directDb && !isBootstrap && !direct.handled && !opt.enqueueOutbox) {
        if(isFailOpenNpcObservationAction(packet.kind)) {
          direct.handled = true;
          direct.accepted = true;
          direct.ready = true;
          packetAccepted = true;
          packetReady = true;
          std::cerr << "[direct_db_observation_unhandled_accepted]"
                    << " action=" << actionName
                    << " target=" << packet.targetKey
                    << " payload=" << packet.payloadJson
                    << "\n";
        } else {
          packetAccepted = false;
          ++unhandled;
          diagnosticSeverity = 2;
          diagnosticReason = "direct_db_unhandled";
          diagnosticMessage = "semantic action has no direct C++ DB handler and outbox fallback is disabled";
          std::cerr << "[direct_db_unhandled] action=" << actionName << "\n";
        }
      }

      if(mysql && opt.directDb && opt.npcActionWorker && opt.npcActionWorkerMaxPerPacket > 0 && !isBootstrap) {
        try {
          for(int workerStep = 0; workerStep < opt.npcActionWorkerMaxPerPacket; ++workerStep) {
            const auto workerResult = runNpcActionRequestWorkerOnce(*mysql, sessionUuid);
            if(!workerResult.claimed)
              break;
            ++npcActionWorkerClaimed;
            if(workerResult.applied)
              ++npcActionWorkerApplied;
            if(workerResult.failed)
              ++npcActionWorkerFailed;
            std::cout << "[npc_action_worker_result]"
                      << " status=" << workerResult.status
                      << " retryable=" << (workerResult.retryable ? 1 : 0)
                      << " action_uuid=" << workerResult.actionUuid
                      << " actor=" << workerResult.actorKey
                      << " action=" << workerResult.actionKey
                      << " target=" << workerResult.targetKey
                      << " reason=" << workerResult.reason
                      << "\n";
          }
        } catch(const std::exception& exc) {
          std::cerr << "[npc_action_worker_failed_open]"
                    << " action=" << actionName
                    << " error=" << exc.what()
                    << "\n";
        }
      }

      ++accepted;
      const auto ackKind = isBootstrap ? Mmo::Net::ServerAckKind::Bootstrap :
                           (isMovement ? Mmo::Net::ServerAckKind::Movement : Mmo::Net::ServerAckKind::GenericAction);
      const auto ack = Mmo::Net::encodeServerAckPacket({packet.packetSequence, packet.localSequence, ackKind, packetAccepted, packetReady});
      socket.send_to(asio::buffer(ack), remote, 0, ec);
      if(!diagnosticReason.empty()) {
        sendServerDiagnostic(socket, remote, packet, diagnosticSeverity, actionName, diagnosticReason, diagnosticMessage);
      }
      bool liveDeltaSent = false;
      if(mysql && !isBootstrap && shouldSendLiveDelta(packet, packetAccepted, direct)) {
        liveDeltaSent = sendServerLiveDelta(socket, remote, packet, actionName, direct);
      }

      bool snapshotSent = false;
      if(isBootstrap && packetAccepted && !bootstrapSnapshotJson.empty()) {
        try {
          sendBootstrapSnapshot(socket, remote, packet, nextSnapshotId++, bootstrapSnapshotJson);
          snapshotSent = true;
        } catch(const std::exception& exc) {
          ++failed;
          std::cerr << "[bootstrap_snapshot_send_failed] error=" << exc.what() << "\n";
          sendServerDiagnostic(socket, remote, packet, 2, actionName, "bootstrap_snapshot_send_failed", exc.what());
        }
      }
      if(!isBootstrap && packetAccepted && !liveWorldSnapshotJson.empty()) {
        try {
          sendBootstrapSnapshot(socket, remote, packet, nextSnapshotId++, liveWorldSnapshotJson);
          snapshotSent = true;
        } catch(const std::exception& exc) {
          ++failed;
          std::cerr << "[live_world_item_snapshot_send_failed] error=" << exc.what() << "\n";
          sendServerDiagnostic(socket, remote, packet, 2, actionName, "live_world_item_snapshot_send_failed", exc.what());
        }
      }
      printPacketProgress(logState, accepted, received, invalid, duplicate, enqueued, directDb, unhandled, failed,
                          actionName, packetAccepted, !diagnosticReason.empty(), snapshotSent, liveDeltaSent, isMovement, isWeaponState);
    }

    std::cout << "summary:\n"
              << "received=" << received << "\n"
              << "accepted=" << accepted << "\n"
              << "invalid=" << invalid << "\n"
              << "duplicate=" << duplicate << "\n"
              << "enqueued=" << enqueued << "\n"
              << "direct_db=" << directDb << "\n"
              << "npc_action_worker_claimed=" << npcActionWorkerClaimed << "\n"
              << "npc_action_worker_applied=" << npcActionWorkerApplied << "\n"
              << "npc_action_worker_failed=" << npcActionWorkerFailed << "\n"
              << "unhandled=" << unhandled << "\n"
              << "failed=" << failed << "\n";
    return invalid == 0 && failed == 0 && unhandled == 0 ? 0 : 2;
  } catch(const std::exception& exc) {
    std::cerr << "ERROR: " << exc.what() << "\n";
    return 2;
  }
}























