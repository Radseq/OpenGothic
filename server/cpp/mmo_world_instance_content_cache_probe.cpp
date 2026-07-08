#include "mmo_world_instance_content_cache.h"

#include <iostream>
#include <string_view>
#include <vector>

namespace {

void jsonEscape(std::ostream& out, std::string_view text) {
  out << '"';
  for(const unsigned char c : text) {
    switch(c) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\b':
        out << "\\b";
        break;
      case '\f':
        out << "\\f";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if(c < 0x20U) {
          static constexpr char Hex[] = "0123456789abcdef";
          out << "\\u00" << Hex[(c >> 4U) & 0xFU] << Hex[c & 0xFU];
        } else {
          out << static_cast<char>(c);
        }
        break;
    }
  }
  out << '"';
}

void jsonField(std::ostream& out, std::string_view name, std::string_view value, bool comma = true) {
  jsonEscape(out, name);
  out << ": ";
  jsonEscape(out, value);
  if(comma) {
    out << ",";
  }
  out << "\n";
}

void jsonCountField(std::ostream& out, std::string_view name, std::size_t value, bool comma = true) {
  jsonEscape(out, name);
  out << ": " << value;
  if(comma) {
    out << ",";
  }
  out << "\n";
}

void jsonBoolField(std::ostream& out, std::string_view name, bool value, bool comma = true) {
  jsonEscape(out, name);
  out << ": " << (value ? "true" : "false");
  if(comma) {
    out << ",";
  }
  out << "\n";
}

void jsonIndexCounts(std::ostream& out, const Mmo::RuntimeReadModel::RuntimeIndexCounts& counts, bool comma = true) {
  out << "{\n";
  jsonCountField(out, "world_zen_entity_by_key", counts.worldZenEntityByKey);
  jsonCountField(out, "waypoint_edge_by_route", counts.waypointEdgeByRoute);
  jsonCountField(out, "npc_template_by_instance", counts.npcTemplateByInstance);
  jsonCountField(out, "item_template_by_instance", counts.itemTemplateByInstance);
  jsonCountField(out, "routine_by_npc_instance", counts.routineByNpcInstance);
  jsonCountField(out, "routine_by_symbol", counts.routineBySymbol);
  jsonCountField(out, "perception_binding_by_kind", counts.perceptionBindingByKind);
  jsonCountField(out, "perception_binding_by_owner", counts.perceptionBindingByOwner);
  jsonCountField(out, "dialog_info_by_symbol", counts.dialogInfoBySymbol);
  jsonCountField(out, "dialog_output_by_name", counts.dialogOutputByName, false);
  out << "}";
  if(comma) {
    out << ",";
  }
  out << "\n";
}

void jsonStats(std::ostream& out, const Mmo::WorldInstanceContent::WorldInstanceContentCacheStats& stats, bool comma = true) {
  out << "{\n";
  jsonCountField(out, "world_zen_entities_in_world", stats.worldZenEntitiesInWorld);
  jsonCountField(out, "waypoint_edges_in_world", stats.waypointEdgesInWorld);
  jsonCountField(out, "npc_templates", stats.npcTemplates);
  jsonCountField(out, "item_templates", stats.itemTemplates);
  jsonCountField(out, "routines", stats.routines);
  jsonCountField(out, "perception_bindings", stats.perceptionBindings);
  jsonCountField(out, "dialog_infos", stats.dialogInfos);
  jsonCountField(out, "dialog_outputs", stats.dialogOutputs, false);
  out << "}";
  if(comma) {
    out << ",";
  }
  out << "\n";
}

void jsonLookupChecks(
    std::ostream& out,
    const Mmo::WorldInstanceContent::WorldInstanceContentCacheLookupChecks& checks,
    bool comma = true) {
  out << "{\n";
  jsonBoolField(out, "first_world_zen_entity_in_world", checks.firstWorldZenEntityInWorld);
  jsonBoolField(out, "first_waypoint_edge_in_world", checks.firstWaypointEdgeInWorld);
  jsonBoolField(out, "first_npc_template_by_instance", checks.firstNpcTemplateByInstance);
  jsonBoolField(out, "first_item_template_by_instance", checks.firstItemTemplateByInstance);
  jsonBoolField(out, "first_routine_by_npc_instance", checks.firstRoutineByNpcInstance);
  jsonBoolField(out, "first_routine_by_symbol", checks.firstRoutineBySymbol);
  jsonBoolField(out, "first_perception_binding_by_kind", checks.firstPerceptionBindingByKind);
  jsonBoolField(out, "first_perception_binding_by_owner", checks.firstPerceptionBindingByOwner);
  jsonBoolField(out, "first_dialog_info_by_symbol", checks.firstDialogInfoBySymbol);
  jsonBoolField(out, "first_dialog_output_by_name", checks.firstDialogOutputByName, false);
  out << "}";
  if(comma) {
    out << ",";
  }
  out << "\n";
}

void jsonStringArray(std::ostream& out, std::string_view name, const std::vector<std::string>& values, bool comma = true) {
  jsonEscape(out, name);
  out << ": [";
  for(std::size_t i = 0; i < values.size(); ++i) {
    if(i != 0) {
      out << ", ";
    }
    jsonEscape(out, values[i]);
  }
  out << "]";
  if(comma) {
    out << ",";
  }
  out << "\n";
}

int usage(const char* argv0) {
  std::cerr << "usage: " << argv0
            << " <runtime_read_model.json> <content_revision_key> <world_instance_key> <world_name>\n";
  return 64;
}

} // namespace

int main(int argc, char** argv) {
  if(argc != 5) {
    return usage(argv[0]);
  }

  try {
    Mmo::WorldInstanceContent::WorldInstanceContentCacheOptions options;
    options.runtimeReadModelPath = argv[1];
    options.contentRevisionKey = argv[2];
    options.worldInstanceKey = argv[3];
    options.worldName = argv[4];

    const auto cache = Mmo::WorldInstanceContent::WorldInstanceContentCache::load(options);
    const auto checks = Mmo::WorldInstanceContent::runDeterministicLookupChecks(cache);

    std::cout << "{\n";
    jsonField(std::cout, "status", "ready");
    jsonField(std::cout, "path", cache.readModel().inspection.sourcePath.generic_string());
    jsonField(std::cout, "content_revision_key", cache.contentRevisionKey());
    jsonField(std::cout, "read_model_content_revision_key", cache.readModel().inspection.contentRevisionKey);
    jsonField(std::cout, "world_instance_key", cache.worldInstanceKey());
    jsonField(std::cout, "world_name", cache.worldName());
    std::cout << "\"index_counts\": ";
    jsonIndexCounts(std::cout, cache.stats().indexCounts);
    std::cout << "\"cache_stats\": ";
    jsonStats(std::cout, cache.stats());
    std::cout << "\"lookup_checks\": ";
    jsonLookupChecks(std::cout, checks);
    jsonStringArray(std::cout, "warnings", cache.readModel().inspection.warnings);
    jsonStringArray(std::cout, "validation_errors", {}, false);
    std::cout << "}\n";
    return 0;
  } catch(const std::exception& e) {
    std::cout << "{\n";
    jsonField(std::cout, "status", "error");
    jsonField(std::cout, "path", argc > 1 ? argv[1] : "");
    jsonField(std::cout, "message", e.what(), false);
    std::cout << "}\n";
    return 1;
  }
}
