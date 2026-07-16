#pragma once

#include "mmo_presentation_catalog_codec.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

namespace Mmo::ClientPresentation {

enum class ClientPresentationCatalogStatus : std::uint8_t {
  Applied,
  EmptyPath,
  FileOpenFailed,
  FileTooLarge,
  FileReadFailed,
  DecodeFailed,
  ManifestMismatch,
  AllocationFailure,
};

struct ClientPresentationCatalogResult final {
  ClientPresentationCatalogStatus status =
      ClientPresentationCatalogStatus::DecodeFailed;
  Mmo::Presentation::PresentationCatalogCodecStatus codecStatus =
      Mmo::Presentation::PresentationCatalogCodecStatus::InvalidValue;

  [[nodiscard]] constexpr bool applied() const noexcept {
    return status == ClientPresentationCatalogStatus::Applied;
  }
};

// Read-only, authority-neutral runtime view of an installed presentation
// catalog. It resolves opaque server identities only to symbolic resources;
// the full client remains responsible for looking up the Daedalus symbol.
class ClientPresentationCatalogRuntime final {
public:
  [[nodiscard]] ClientPresentationCatalogResult install(
      std::span<const std::byte> payload,
      Mmo::Presentation::ContentManifestId expectedManifest = {}) noexcept;
  [[nodiscard]] ClientPresentationCatalogResult load(
      const std::filesystem::path& path,
      Mmo::Presentation::ContentManifestId expectedManifest = {}) noexcept;

  [[nodiscard]] bool installed() const noexcept;
  [[nodiscard]] Mmo::Presentation::ContentManifestId manifest() const noexcept;
  [[nodiscard]] std::uint64_t contentFingerprint() const noexcept;
  [[nodiscard]] std::optional<std::string_view> npcInstanceName(
      std::uint64_t archetypeId,
      std::uint64_t presentationId) const noexcept;
  [[nodiscard]] std::optional<std::string_view> playerInstanceName(
      std::uint64_t archetypeId,
      std::uint64_t presentationId) const noexcept;
  [[nodiscard]] std::optional<std::string_view> itemInstanceName(
      std::uint64_t archetypeId,
      std::uint64_t presentationId) const noexcept;
  [[nodiscard]] std::optional<std::string_view> itemDisplayName(
      std::uint64_t archetypeId,
      std::uint64_t presentationId) const noexcept;
  [[nodiscard]] std::optional<std::string_view> worldItemVisual(
      std::uint64_t archetypeId,
      std::uint64_t presentationId) const noexcept;
  [[nodiscard]] std::optional<std::string_view> equippedWeaponVisual(
      std::uint64_t archetypeId,
      std::uint64_t presentationId) const noexcept;

  // Returned views point into the installed immutable catalog and remain valid
  // only until the next install/load call or destruction of this runtime.

private:
  template <typename Descriptor>
  [[nodiscard]] std::optional<std::string_view> instanceName(
      const Descriptor* descriptor,
      std::uint64_t archetypeId) const noexcept;
  [[nodiscard]] std::optional<std::string_view> worldObjectResource(
      std::uint64_t archetypeId,
      std::uint64_t presentationId,
      Mmo::Presentation::PresentationResourceId
          Mmo::Presentation::WorldObjectPresentationDescriptor::*member,
      Mmo::Presentation::PresentationResourceKind expectedKind) const noexcept;

  std::optional<Mmo::Presentation::PresentationCatalog> catalog_;
};

[[nodiscard]] const char* toString(
    ClientPresentationCatalogStatus value) noexcept;

} // namespace Mmo::ClientPresentation
