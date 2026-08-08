#include "nativetelemetry.h"

#include <chrono>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>

namespace NativeTelemetry {
namespace {

struct State final {
  std::mutex mutex;
  std::ofstream output;
  std::string outputPath;
  std::uint64_t sequence = 0U;
};

State& state() {
  static State value;
  return value;
}

[[nodiscard]] std::uint64_t unixTimeMs() noexcept {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

void appendEscaped(std::string& out, const std::string_view value) {
  for(const char ch : value) {
    if(ch == '\\' || ch == '"')
      out.push_back('\\');
    if(ch == '\n') {
      out.append("\\n");
    } else if(ch == '\r') {
      out.append("\\r");
    } else if(ch == '\t') {
      out.append("\\t");
    } else {
      out.push_back(ch);
    }
  }
}

void writeLineLocked(State& value, const std::string_view type, std::string payload) {
  if(!value.output.is_open())
    return;

  std::string line;
  line.reserve(type.size() + payload.size() + 96U);
  line.append("{\"type\":\"");
  line.append(type);
  line.append("\",\"sequence\":");
  line.append(std::to_string(++value.sequence));
  line.append(",\"time_unix_ms\":");
  line.append(std::to_string(unixTimeMs()));
  line.append(payload);
  line.append("}\n");
  value.output.write(line.data(), static_cast<std::streamsize>(line.size()));
  value.output.flush();
}

void writeLine(const std::string_view type, std::string payload) {
  auto& value = state();
  std::lock_guard lock(value.mutex);
  writeLineLocked(value, type, std::move(payload));
}

} // namespace

bool configure(const std::string_view outputPath) {
  auto& value = state();
  std::lock_guard lock(value.mutex);
  value.output.close();
  value.sequence = 0U;
  value.outputPath = std::string(outputPath);
  return !value.outputPath.empty();
}

bool configured() {
  auto& value = state();
  std::lock_guard lock(value.mutex);
  return !value.outputPath.empty();
}

bool start() {
  auto& value = state();
  std::lock_guard lock(value.mutex);
  if(value.outputPath.empty())
    return false;

  value.output.close();
  value.sequence = 0U;
  value.output.open(value.outputPath, std::ios::out | std::ios::trunc);
  if(!value.output.is_open())
    return false;
  writeLineLocked(value, "telemetry_started", {});
  return true;
}

void stop() {
  auto& value = state();
  std::lock_guard lock(value.mutex);
  writeLineLocked(value, "telemetry_stopped", {});
  value.output.close();
}

void disable() {
  auto& value = state();
  std::lock_guard lock(value.mutex);
  value.output.close();
  value.outputPath.clear();
  value.sequence = 0U;
}

bool enabled() {
  auto& value = state();
  std::lock_guard lock(value.mutex);
  return value.output.is_open();
}

void mouseButton(const std::uint32_t button, const bool pressed) {
  writeLine("mouse_button", ",\"button\":" + std::to_string(button) +
                                ",\"pressed\":" +
                                (pressed ? "true" : "false"));
}

void playerPosition(const float x,
                    const float y,
                    const float z,
                    const float yawDegrees) {
  std::string payload;
  payload.reserve(96U);
  payload.append(",\"x\":");
  payload.append(std::to_string(x));
  payload.append(",\"y\":");
  payload.append(std::to_string(y));
  payload.append(",\"z\":");
  payload.append(std::to_string(z));
  payload.append(",\"yaw_degrees\":");
  payload.append(std::to_string(yawDegrees));
  writeLine("player_position", std::move(payload));
}

void itemPickupAttempt(const std::string_view itemName,
                       const std::size_t itemSymbol,
                       const std::size_t amount,
                       const bool accepted,
                       const float playerX,
                       const float playerY,
                       const float playerZ,
                       const float playerYawDegrees,
                       const float itemX,
                       const float itemY,
                       const float itemZ) {
  std::string payload;
  payload.reserve(itemName.size() + 240U);
  payload.append(",\"item_name\":\"");
  appendEscaped(payload, itemName);
  payload.append("\",\"item_symbol\":");
  payload.append(std::to_string(itemSymbol));
  payload.append(",\"amount\":");
  payload.append(std::to_string(amount));
  payload.append(",\"player_x\":");
  payload.append(std::to_string(playerX));
  payload.append(",\"player_y\":");
  payload.append(std::to_string(playerY));
  payload.append(",\"player_z\":");
  payload.append(std::to_string(playerZ));
  payload.append(",\"player_yaw_degrees\":");
  payload.append(std::to_string(playerYawDegrees));
  payload.append(",\"item_x\":");
  payload.append(std::to_string(itemX));
  payload.append(",\"item_y\":");
  payload.append(std::to_string(itemY));
  payload.append(",\"item_z\":");
  payload.append(std::to_string(itemZ));
  payload.append(",\"accepted\":");
  payload.append(accepted ? "true" : "false");
  writeLine("item_pickup_attempt", std::move(payload));
}

void itemPickedUp(const std::string_view itemName,
                  const std::size_t itemSymbol,
                  const std::size_t amount,
                  const float playerX,
                  const float playerY,
                  const float playerZ,
                  const float playerYawDegrees,
                  const float itemX,
                  const float itemY,
                  const float itemZ) {
  std::string payload;
  payload.reserve(itemName.size() + 224U);
  payload.append(",\"item_name\":\"");
  appendEscaped(payload, itemName);
  payload.append("\",\"item_symbol\":");
  payload.append(std::to_string(itemSymbol));
  payload.append(",\"amount\":");
  payload.append(std::to_string(amount));
  payload.append(",\"player_x\":");
  payload.append(std::to_string(playerX));
  payload.append(",\"player_y\":");
  payload.append(std::to_string(playerY));
  payload.append(",\"player_z\":");
  payload.append(std::to_string(playerZ));
  payload.append(",\"player_yaw_degrees\":");
  payload.append(std::to_string(playerYawDegrees));
  payload.append(",\"item_x\":");
  payload.append(std::to_string(itemX));
  payload.append(",\"item_y\":");
  payload.append(std::to_string(itemY));
  payload.append(",\"item_z\":");
  payload.append(std::to_string(itemZ));
  writeLine("item_picked_up", std::move(payload));
}

} // namespace NativeTelemetry
