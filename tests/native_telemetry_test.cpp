#include "utils/nativetelemetry.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

[[nodiscard]] bool check(const bool condition, const char* const message) {
  if(condition)
    return true;
  std::cerr << "check failed: " << message << '\n';
  return false;
}

[[nodiscard]] bool recordsNativeSinglePlayerEvents() {
  const auto path = std::filesystem::temp_directory_path() /
                    "opengothic-native-telemetry-test.jsonl";
  std::error_code error;
  std::filesystem::remove(path, error);

  if(!check(NativeTelemetry::configure(path.string()), "open telemetry file"))
    return false;
  NativeTelemetry::mouseButton(1U, true);
  if(!check(NativeTelemetry::start(), "start telemetry file"))
    return false;
  NativeTelemetry::mouseButton(1U, true);
  NativeTelemetry::mouseButton(1U, false);
  NativeTelemetry::playerPosition(10.f, 20.f, 30.f, 45.f);
  NativeTelemetry::itemPickupAttempt(
      "It\"Mw", 42U, 3U, false, 10.f, 20.f, 30.f, 45.f, 80.f, 20.f, 30.f);
  NativeTelemetry::itemPickedUp(
      "It\"Mw", 42U, 3U, 10.f, 20.f, 30.f, 45.f, 80.f, 20.f, 30.f);
  NativeTelemetry::stop();
  NativeTelemetry::mouseButton(1U, false);
  NativeTelemetry::disable();

  std::ifstream input(path);
  std::string content((std::istreambuf_iterator<char>(input)),
                      std::istreambuf_iterator<char>());
  std::filesystem::remove(path, error);

  return check(content.find("\"type\":\"mouse_button\"") != std::string::npos,
               "mouse button record") &&
         check(content.find("\"type\":\"telemetry_started\"") != std::string::npos,
               "start record") &&
         check(content.find("\"type\":\"telemetry_stopped\"") != std::string::npos,
               "stop record") &&
         check(content.find("\"type\":\"player_position\"") != std::string::npos,
               "player position record") &&
         check(content.find("\"type\":\"item_picked_up\"") != std::string::npos,
               "item pickup record") &&
         check(content.find("\"type\":\"item_pickup_attempt\"") != std::string::npos,
               "item pickup attempt record") &&
         check(content.find("\"time_unix_ms\":") != std::string::npos,
               "millisecond timestamp") &&
         check(content.find("\"yaw_degrees\":45.000000") != std::string::npos,
               "player facing angle") &&
         check(content.find("\"movement_state\":") == std::string::npos,
               "movement state is not recorded") &&
         check(content.find("\"type\":\"interaction_too_far\"") == std::string::npos,
               "mob distance feedback is not recorded") &&
         check(content.find("\"accepted\":false") != std::string::npos,
               "rejected pickup attempt") &&
         check(content.find("\"item_x\":80.000000") != std::string::npos,
               "item position") &&
         check(content.find("It\\\"Mw") != std::string::npos,
               "escaped item name") &&
         check(content.find("\"sequence\":9") == std::string::npos,
               "records after stop are ignored");
}

} // namespace

int main() {
  return recordsNativeSinglePlayerEvents() ? 0 : 1;
}
