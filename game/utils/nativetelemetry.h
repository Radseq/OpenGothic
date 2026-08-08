#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace NativeTelemetry {

[[nodiscard]] bool configure(std::string_view outputPath);
void disable();
[[nodiscard]] bool configured();
[[nodiscard]] bool start();
void stop();
[[nodiscard]] bool enabled();

void mouseButton(std::uint32_t button, bool pressed);
void playerPosition(float x,
                    float y,
                    float z,
                    float yawDegrees);
void itemPickupAttempt(std::string_view itemName,
                       std::size_t itemSymbol,
                       std::size_t amount,
                       bool accepted,
                       float playerX,
                       float playerY,
                       float playerZ,
                       float playerYawDegrees,
                       float itemX,
                       float itemY,
                       float itemZ);
void itemPickedUp(std::string_view itemName,
                  std::size_t itemSymbol,
                  std::size_t amount,
                  float playerX,
                  float playerY,
                  float playerZ,
                  float playerYawDegrees,
                  float itemX,
                  float itemY,
                  float itemZ);

} // namespace NativeTelemetry
