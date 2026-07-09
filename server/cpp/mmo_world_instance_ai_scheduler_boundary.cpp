#include "mmo_world_instance_ai_scheduler_boundary.h"

#include <cctype>
#include <string_view>

namespace Mmo::WorldInstanceAiTick {
namespace {

[[nodiscard]] std::string jsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  out.push_back('"');
  for(const unsigned char c : text) {
    switch(c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(c < 0x20U ? ' ' : static_cast<char>(c));
        break;
    }
  }
  out.push_back('"');
  return out;
}

void appendField(std::string& out, std::string_view key, std::string_view value) {
  if(out.back() != '{') {
    out.push_back(',');
  }
  out += jsonEscape(key);
  out.push_back(':');
  out += jsonEscape(value);
}

void appendBool(std::string& out, std::string_view key, bool value) {
  if(out.back() != '{') {
    out.push_back(',');
  }
  out += jsonEscape(key);
  out.push_back(':');
  out += value ? "true" : "false";
}

void appendU64(std::string& out, std::string_view key, std::uint64_t value) {
  if(out.back() != '{') {
    out.push_back(',');
  }
  out += jsonEscape(key);
  out.push_back(':');
  out += std::to_string(value);
}

void appendSize(std::string& out, std::string_view key, std::size_t value) {
  appendU64(out, key, static_cast<std::uint64_t>(value));
}

} // namespace

WorldInstanceAiSchedulerPlan buildWorldInstanceAiSchedulerPlan(
    const WorldInstanceAiSchedulerPlanOptions& options) {
  WorldInstanceAiSchedulerPlan out;
  out.built = true;
  out.enabled = options.enabled;
  out.schedulerMode = options.schedulerMode;
  out.intervalMs = options.intervalMs;
  out.maxWorldInstances = options.maxWorldInstances;
  out.tickOncePerWorldInstance = options.tickOncePerWorldInstance;
  out.writeAllowed = options.allowWrites;

  if(!options.enabled) {
    out.status = "scheduler_disabled";
    return out;
  }
  if(options.schedulerMode != "plan_only_no_timer_no_db") {
    out.issues.emplace_back("unsupported_scheduler_mode");
  }
  if(options.intervalMs == 0) {
    out.issues.emplace_back("interval_ms_must_be_positive");
  }
  if(options.maxWorldInstances == 0) {
    out.issues.emplace_back("max_world_instances_must_be_positive");
  }
  if(!options.tickOncePerWorldInstance) {
    out.issues.emplace_back("scheduler_must_tick_once_per_world_instance");
  }
  if(options.allowWrites) {
    out.issues.emplace_back("scheduler_writes_not_allowed_in_plan_only_mode");
  }

  out.status = out.issues.empty() ? "planned_no_timer_no_db" : "invalid_scheduler_plan";
  return out;
}

std::string worldInstanceAiSchedulerPlanJson(
    const WorldInstanceAiSchedulerPlan& plan) {
  std::string out;
  out.reserve(512 + plan.issues.size() * 48);
  out.push_back('{');
  appendField(out, "status", plan.status);
  appendBool(out, "built", plan.built);
  appendBool(out, "enabled", plan.enabled);
  appendField(out, "scheduler_mode", plan.schedulerMode);
  appendBool(out, "tick_once_per_world_instance", plan.tickOncePerWorldInstance);
  appendBool(out, "timer_scheduled", plan.timerScheduled);
  appendBool(out, "tick_executed", plan.tickExecuted);
  appendBool(out, "db_mutated", plan.dbMutated);
  appendBool(out, "write_allowed", plan.writeAllowed);
  appendU64(out, "interval_ms", plan.intervalMs);
  appendSize(out, "max_world_instances", plan.maxWorldInstances);
  out += ",\"issues\":[";
  for(std::size_t i = 0; i < plan.issues.size(); ++i) {
    if(i != 0) {
      out.push_back(',');
    }
    out += jsonEscape(plan.issues[i]);
  }
  out += "]}";
  return out;
}

WorldInstanceAiSchedulerBoundaryResult runWorldInstanceAiSchedulerBoundary(
    const Server::MySqlTarget& target,
    const WorldInstanceAiSchedulerBoundaryOptions& options) {
  WorldInstanceAiSchedulerBoundaryResult result;
  result.enabled = options.enabled;
  result.evidence.accepted = false;
  result.evidence.status = "scheduler_disabled";
  result.evidence.reason = "world_instance AI scheduler boundary is disabled";

  if(!options.enabled) {
    return result;
  }

  WorldInstanceAiTickOptions dryRunOptions = options.tick;
  dryRunOptions.dryRun = true;
  result.dryRunTick = runWorldInstanceAiTick(target, dryRunOptions);
  result.executed = true;
  result.evidence = evaluateWorldInstanceAiTickEvidence(result.dryRunTick, options.evidence);

  if(!result.evidence.accepted || options.tick.dryRun) {
    return result;
  }

  result.writeTick = runWorldInstanceAiTick(target, options.tick);
  result.writeExecuted = true;
  return result;
}

} // namespace Mmo::WorldInstanceAiTick
