// Internal implementation partition for mmo_udp_server.cpp.
// Kept include-based during the monolith split to preserve behavior.

[[nodiscard]] std::string makeDbPayload(const Mmo::Net::ClientActionPacket& p, std::string_view remote) {
  const auto* def = Mmo::findSemanticAction(p.kind);
  const std::string_view actionName = def ? def->actionKind : std::string_view("unknown");
  const std::string_view eventType = def ? def->eventType : std::string_view("unknown");
  const std::string_view eventClass = def ? def->eventClass : std::string_view("unknown");
  const std::string_view procedure = def ? def->procedureName : std::string_view("unknown");
  const std::string_view payload = p.payloadJson;

  std::string out;
  out.reserve(payload.size() + 1400);
  out.push_back('{');
  out += "\"server_tick\":";
  out += std::to_string(p.clientTick);
  appendJsonNumberField(out, "client_tick", p.clientTick);
  appendJsonNumberField(out, "client_local_sequence", p.localSequence);
  appendJsonField(out, "client_idempotency_key", p.idempotencyKey);
  appendJsonField(out, "client_target_key", p.targetKey);
  appendJsonField(out, "client_action_kind", actionName);
  appendJsonField(out, "client_event_type", eventType);
  appendJsonField(out, "client_event_class", eventClass);
  appendJsonField(out, "client_procedure", procedure);
  appendJsonRawField(out, "client_payload", payload);
  appendJsonRawField(out, "metadata",
                     std::string("{\"source\":\"mmo_udp_server_cpp\",\"transport\":\"asio-udp-binary\",\"remote\":") +
                       jsonEscape(remote) + ",\"db_bridge_version\":" + std::to_string(DbBridgeVersion) + "}");

  appendPayloadStringAlias(out, payload, "actor_key", "actor_key");
  appendPayloadStringAlias(out, payload, "world", "world");
  appendPayloadStringAlias(out, payload, "item_symbol", "item_symbol");
  appendPayloadStringAlias(out, payload, "item_template_key", "item_template_key");
  appendPayloadStringAlias(out, payload, "item_persistent_id", "item_persistent_id");
  appendPayloadNumberAlias(out, payload, "amount", "amount");

  const std::string_view action = actionName;
  if(action == "client_bootstrap_request") {
    appendJsonField(out, "character_key", jsonStringField(payload, "character_key").value_or("PC_HERO"));
    appendPayloadStringAlias(out, payload, "server_endpoint", "server_endpoint");
    appendPayloadStringAlias(out, payload, "client_content_manifest_hash", "client_content_manifest_hash");
    appendJsonRawField(out, "server_bound_client_mode", jsonBoolField(payload, "server_bound_client_mode").value_or(true) ? "true" : "false");
    appendJsonField(out, "reason", jsonStringField(payload, "reason").value_or("client_bootstrap_request"));
  } else if(action == "movement_proposal" || action == "character_checkpoint") {
    appendJsonField(out, "character_key", jsonStringField(payload, "character_key").value_or("PC_HERO"));
    appendPayloadNumberAlias(out, payload, "pos_x", "pos_x");
    appendPayloadNumberAlias(out, payload, "pos_y", "pos_y");
    appendPayloadNumberAlias(out, payload, "pos_z", "pos_z");
    appendPayloadNumberAlias(out, payload, "rotation_yaw", "rotation_yaw");
    appendPayloadNumberAlias(out, payload, "yaw", "yaw");
    appendPayloadNumberAlias(out, payload, "from_tick", "from_tick");
    appendPayloadNumberAlias(out, payload, "to_tick", "to_tick");
    appendPayloadStringAlias(out, payload, "current_waypoint_key", "current_waypoint_key");
    appendPayloadNumberAlias(out, payload, "level", "level");
    appendPayloadNumberAlias(out, payload, "experience", "experience");
    appendPayloadNumberAlias(out, payload, "experience_next", "experience_next");
    appendPayloadNumberAlias(out, payload, "learning_points", "learning_points");
    appendPayloadNumberAlias(out, payload, "health_current", "health_current");
    appendPayloadNumberAlias(out, payload, "health_max", "health_max");
    appendPayloadNumberAlias(out, payload, "mana_current", "mana_current");
    appendPayloadNumberAlias(out, payload, "mana_max", "mana_max");
    appendPayloadNumberAlias(out, payload, "strength", "strength");
    appendPayloadNumberAlias(out, payload, "dexterity", "dexterity");
    appendJsonField(out, "reason", jsonStringField(payload, "reason").value_or(std::string(action)));
  } else if(action == "pickup_world_item" || action == "remove_world_item") {
    const auto target = jsonStringField(payload, "target_key").value_or(p.targetKey);
    appendJsonField(out, "world_item_entity_key", target);
    appendJsonField(out, "engine_world_item_key", target);
    appendPayloadStringAlias(out, payload, "source_world_item_persistent_id", "source_world_item_persistent_id");
    appendPayloadNumberAlias(out, payload, "bag_index", "bag_index");
    appendJsonField(out, "reason", jsonStringField(payload, "reason").value_or("semantic_action"));
  } else if(action == "drop_character_item" || action == "loot_npc_inventory") {
    appendPayloadStringAlias(out, payload, "source_item_persistent_id", "source_item_persistent_id");
    appendPayloadStringAlias(out, payload, "target_npc_entity_key", "target_npc_entity_key");
    appendPayloadStringAlias(out, payload, "npc_key", "npc_key");
    appendPayloadNumberAlias(out, payload, "bag_index", "bag_index");
    appendJsonField(out, "reason", jsonStringField(payload, "reason").value_or(std::string(action)));
  } else if(action == "equip_character_item" || action == "unequip_character_item") {
    appendPayloadStringAlias(out, payload, "item_instance_id", "item_instance_id");
    appendPayloadStringAlias(out, payload, "item_persistent_id", "item_persistent_id");
    if(auto slot = jsonStringField(payload, "equipment_slot"); slot && !slot->empty()) {
      appendJsonField(out, "equipment_slot", equipmentSlotName(*slot));
      appendJsonField(out, "engine_equipment_slot", *slot);
    } else if(auto slot = jsonStringField(payload, "slot"); slot && !slot->empty()) {
      appendJsonField(out, "equipment_slot", equipmentSlotName(*slot));
      appendJsonField(out, "engine_equipment_slot", *slot);
    } else if(auto slot = jsonNumberTextField(payload, "slot")) {
      appendJsonField(out, "equipment_slot", equipmentSlotName(*slot));
      appendJsonRawField(out, "engine_equipment_slot", *slot);
    }
    appendPayloadNumberAlias(out, payload, "target_bag_index", "target_bag_index");
  } else if(action == "use_interactive" || action == "update_interactive_state") {
    appendPayloadStringAlias(out, payload, "interactive_key", "interactive_key");
    appendPayloadStringAlias(out, payload, "target_key", "target_key");
    appendPayloadStringAlias(out, payload, "state_after", "state_after");
    appendJsonField(out, "reason", jsonStringField(payload, "reason").value_or(std::string(action)));
  } else if(action == "set_script_int") {
    appendJsonField(out, "script_key", jsonStringField(payload, "script_key").value_or(jsonStringField(payload, "symbol_name").value_or(p.targetKey)));
    appendPayloadNumberAlias(out, payload, "value_index", "value_index");
    appendPayloadNumberAlias(out, payload, "value_before", "value_before");
    appendPayloadNumberAlias(out, payload, "value_after", "value_after");
    appendJsonField(out, "reason", jsonStringField(payload, "reason").value_or("script_int_changed"));
  } else if(action == "update_quest") {
    appendJsonField(out, "quest_key", jsonStringField(payload, "quest_key").value_or(jsonStringField(payload, "topic").value_or(p.targetKey)));
    appendPayloadStringAlias(out, payload, "quest_name", "quest_name");
    appendPayloadStringAlias(out, payload, "status", "status");
    appendPayloadNumberAlias(out, payload, "entry_count", "entry_count");
  } else if(action == "set_known_dialog") {
    appendPayloadStringAlias(out, payload, "npc_key", "npc_key");
    appendPayloadStringAlias(out, payload, "info_key", "info_key");
    appendPayloadBoolAlias(out, payload, "known", "known");
    appendPayloadBoolAlias(out, payload, "removed", "removed");
    appendJsonField(out, "reason", jsonStringField(payload, "reason").value_or("script_dialog_known"));
  } else if(action == "adjust_progression" || action == "apply_experience_reward") {
    appendPayloadNumberAlias(out, payload, "experience_delta", "experience_delta");
    appendPayloadNumberAlias(out, payload, "learning_points_delta", "learning_points_delta");
    appendJsonField(out, "reason", jsonStringField(payload, "reason").value_or("script_progression"));
  }

  out += ",\"resolver_ready\":true,\"resolver_missing_fields\":[],\"dispatch_ready\":true,\"dispatch_missing_fields\":[]}";
  return out;
}

[[nodiscard]] bool isFailOpenNpcObservationAction(Mmo::SemanticActionKind kind) noexcept {
  return kind == Mmo::SemanticActionKind::RecordNpcRoutineState ||
         kind == Mmo::SemanticActionKind::RecordNpcAiState ||
         kind == Mmo::SemanticActionKind::RecordNpcPathState ||
         kind == Mmo::SemanticActionKind::RecordNpcFightState;
}

[[nodiscard]] DirectApplyResult ignoreNpcObservation(std::string_view actionName,
                                                     std::string_view targetKey,
                                                     const Mmo::Server::Gameplay::ValidationResult& validation) {
  std::cout << "[npc_observation_ignored]"
            << " action=" << actionName
            << " target=" << targetKey
            << " reason=" << validation.reason
            << "\n";
  return {true, true, true, validation.reason};
}



