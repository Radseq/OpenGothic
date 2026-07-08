#include "mmo_runtime_read_model_loader.h"

#include <iostream>
#include <string_view>

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

void jsonCounts(std::ostream& out, const Mmo::RuntimeReadModel::SectionCounts& counts, bool comma = true) {
  out << "{\n";
  jsonCountField(out, "world_zen_entity_count", counts.worldZenEntityCount);
  jsonCountField(out, "waypoint_edge_count", counts.waypointEdgeCount);
  jsonCountField(out, "npc_template_count", counts.npcTemplateCount);
  jsonCountField(out, "item_template_count", counts.itemTemplateCount);
  jsonCountField(out, "routine_count", counts.routineCount);
  jsonCountField(out, "perception_binding_count", counts.perceptionBindingCount);
  jsonCountField(out, "dialog_info_count", counts.dialogInfoCount);
  jsonCountField(out, "dialog_output_count", counts.dialogOutputCount, false);
  out << "}";
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

void jsonLookupChecks(std::ostream& out, const Mmo::RuntimeReadModel::RuntimeLookupChecks& checks, bool comma = true) {
  out << "{\n";
  jsonBoolField(out, "first_world_zen_entity_by_key", checks.firstWorldZenEntityByKey);
  jsonBoolField(out, "first_waypoint_edge_by_route", checks.firstWaypointEdgeByRoute);
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
  std::cerr << "usage: " << argv0 << " <runtime_read_model.json>\n";
  return 64;
}

} // namespace

int main(int argc, char** argv) {
  if(argc != 2) {
    return usage(argv[0]);
  }

  try {
    const auto cache = Mmo::RuntimeReadModel::loadRuntimeReadModel(argv[1]);
    const auto errors = Mmo::RuntimeReadModel::validateRuntimeReadModel(cache.inspection);
    const auto indexes = Mmo::RuntimeReadModel::indexCounts(cache);
    const auto lookups = Mmo::RuntimeReadModel::runDeterministicLookupChecks(cache);
    const bool ready = errors.empty();

    std::cout << "{\n";
    jsonField(std::cout, "status", ready ? "ready" : "failed");
    jsonField(std::cout, "path", cache.inspection.sourcePath.generic_string());
    jsonField(std::cout, "schema", cache.inspection.schema);
    jsonField(std::cout, "content_revision_key", cache.inspection.contentRevisionKey);
    jsonField(std::cout, "game_code", cache.inspection.gameCode);
    jsonField(std::cout, "payload_sha256", cache.inspection.payloadSha256);
    jsonField(std::cout, "source_kind", cache.inspection.sourceKind);
    jsonField(std::cout, "snapshot_path", cache.inspection.snapshotPath);
    std::cout << "\"summary\": ";
    jsonCounts(std::cout, cache.inspection.summary);
    std::cout << "\"section_counts\": ";
    jsonCounts(std::cout, cache.inspection.sectionCounts);
    std::cout << "\"index_counts\": ";
    jsonIndexCounts(std::cout, indexes);
    std::cout << "\"lookup_checks\": ";
    jsonLookupChecks(std::cout, lookups);
    jsonStringArray(std::cout, "warnings", cache.inspection.warnings);
    jsonStringArray(std::cout, "validation_errors", errors, false);
    std::cout << "}\n";
    return ready ? 0 : 2;
  } catch(const std::exception& e) {
    std::cout << "{\n";
    jsonField(std::cout, "status", "error");
    jsonField(std::cout, "path", argv[1]);
    jsonField(std::cout, "message", e.what(), false);
    std::cout << "}\n";
    return 1;
  }
}
