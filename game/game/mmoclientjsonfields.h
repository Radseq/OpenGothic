#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Mmo::ClientJson {

[[nodiscard]] std::optional<std::string> jsonStringField(
    std::string_view json,
    std::string_view key);
[[nodiscard]] std::optional<std::string> jsonNumberTextField(
    std::string_view json,
    std::string_view key);
[[nodiscard]] std::optional<std::string_view> jsonObjectField(
    std::string_view json,
    std::string_view key);
[[nodiscard]] std::optional<bool> jsonBoolField(
    std::string_view json,
    std::string_view key);

[[nodiscard]] std::int32_t optionalJsonI32(
    std::string_view json,
    std::string_view key,
    std::int32_t fallback) noexcept;
[[nodiscard]] std::uint64_t optionalJsonU64(
    std::string_view json,
    std::string_view key,
    std::uint64_t fallback) noexcept;
[[nodiscard]] std::int64_t optionalJsonI64(
    std::string_view json,
    std::string_view key,
    std::int64_t fallback) noexcept;
[[nodiscard]] double optionalJsonDouble(
    std::string_view json,
    std::string_view key,
    double fallback) noexcept;
[[nodiscard]] std::string optionalJsonString(
    std::string_view json,
    std::string_view key,
    std::string fallback = {});

} // namespace Mmo::ClientJson
