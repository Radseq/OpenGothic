#include "mmoclientpresentationcatalog.h"

#include <fstream>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace Mmo::ClientPresentation {

ClientPresentationCatalogResult ClientPresentationCatalogRuntime::install(
    const std::span<const std::byte> payload,
    const Mmo::Presentation::ContentManifestId expectedManifest) noexcept {
  catalog_.reset();
  try {
    auto decoded = Mmo::Presentation::decodePresentationCatalog(payload);
    if(!decoded.applied() || !decoded.value.has_value()) {
      return {
          .status = ClientPresentationCatalogStatus::DecodeFailed,
          .codecStatus = decoded.status,
      };
    }
    if(expectedManifest.valid() &&
       decoded.value->contentManifest != expectedManifest) {
      return {
          .status = ClientPresentationCatalogStatus::ManifestMismatch,
          .codecStatus = Mmo::Presentation::PresentationCatalogCodecStatus::Applied,
      };
    }
    catalog_ = std::move(*decoded.value);
    return {
        .status = ClientPresentationCatalogStatus::Applied,
        .codecStatus = Mmo::Presentation::PresentationCatalogCodecStatus::Applied,
    };
  } catch(const std::bad_alloc&) {
    return {.status = ClientPresentationCatalogStatus::AllocationFailure};
  } catch(...) {
    return {.status = ClientPresentationCatalogStatus::DecodeFailed};
  }
}

ClientPresentationCatalogResult ClientPresentationCatalogRuntime::load(
    const std::filesystem::path& path,
    const Mmo::Presentation::ContentManifestId expectedManifest) noexcept {
  if(path.empty())
    return {.status = ClientPresentationCatalogStatus::EmptyPath};
  try {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if(!input)
      return {.status = ClientPresentationCatalogStatus::FileOpenFailed};
    const auto end = input.tellg();
    if(end < 0)
      return {.status = ClientPresentationCatalogStatus::FileReadFailed};
    const auto size = static_cast<std::uintmax_t>(end);
    if(size == 0U ||
       size > Mmo::Presentation::MaximumPresentationCatalogBytes ||
       size > static_cast<std::uintmax_t>(
                  std::numeric_limits<std::streamsize>::max())) {
      return {.status = ClientPresentationCatalogStatus::FileTooLarge};
    }
    std::vector<std::byte> payload(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    input.read(
        reinterpret_cast<char*>(payload.data()),
        static_cast<std::streamsize>(payload.size()));
    if(!input || input.gcount() != static_cast<std::streamsize>(payload.size()))
      return {.status = ClientPresentationCatalogStatus::FileReadFailed};
    return install(payload, expectedManifest);
  } catch(const std::bad_alloc&) {
    return {.status = ClientPresentationCatalogStatus::AllocationFailure};
  } catch(...) {
    return {.status = ClientPresentationCatalogStatus::FileReadFailed};
  }
}

bool ClientPresentationCatalogRuntime::installed() const noexcept {
  return catalog_.has_value();
}

Mmo::Presentation::ContentManifestId
ClientPresentationCatalogRuntime::manifest() const noexcept {
  return catalog_.has_value() ? catalog_->contentManifest
                              : Mmo::Presentation::ContentManifestId{};
}

std::uint64_t ClientPresentationCatalogRuntime::contentFingerprint() const noexcept {
  return catalog_.has_value() ? catalog_->contentFingerprint : 0U;
}

template <typename Descriptor>
std::optional<std::string_view> ClientPresentationCatalogRuntime::instanceName(
    const Descriptor* descriptor,
    const std::uint64_t archetypeId) const noexcept {
  if(!catalog_.has_value() || descriptor == nullptr || archetypeId == 0U ||
     descriptor->archetype.value != archetypeId) {
    return std::nullopt;
  }
  const auto* resource = Mmo::Presentation::findPresentationResource(
      *catalog_, descriptor->daedalusInstanceName);
  if(resource == nullptr ||
     resource->kind !=
         Mmo::Presentation::PresentationResourceKind::DaedalusNpcInstanceName ||
     resource->keyUtf8.empty()) {
    return std::nullopt;
  }
  return resource->keyUtf8;
}

std::optional<std::string_view>
ClientPresentationCatalogRuntime::npcInstanceName(
    const std::uint64_t archetypeId,
    const std::uint64_t presentationId) const noexcept {
  if(!catalog_.has_value() || presentationId == 0U)
    return std::nullopt;
  return instanceName(
      Mmo::Presentation::findNpcPresentation(
          *catalog_, Mmo::Presentation::PresentationId{presentationId}),
      archetypeId);
}

std::optional<std::string_view>
ClientPresentationCatalogRuntime::playerInstanceName(
    const std::uint64_t archetypeId,
    const std::uint64_t presentationId) const noexcept {
  if(!catalog_.has_value() || presentationId == 0U)
    return std::nullopt;
  return instanceName(
      Mmo::Presentation::findPlayerPresentation(
          *catalog_, Mmo::Presentation::PresentationId{presentationId}),
      archetypeId);
}

std::optional<std::string_view>
ClientPresentationCatalogRuntime::equippedWeaponVisual(
    const std::uint64_t archetypeId,
    const std::uint64_t presentationId) const noexcept {
  if(!catalog_.has_value() || archetypeId == 0U || presentationId == 0U)
    return std::nullopt;
  const auto* descriptor = Mmo::Presentation::findWorldObjectPresentation(
      *catalog_, Mmo::Presentation::PresentationId{presentationId});
  if(descriptor == nullptr ||
     descriptor->kind != Mmo::Presentation::WorldObjectPresentationKind::Weapon ||
     descriptor->archetype.value != archetypeId ||
     !descriptor->equippedVisual.valid()) {
    return std::nullopt;
  }
  const auto* resource = Mmo::Presentation::findPresentationResource(
      *catalog_, descriptor->equippedVisual);
  if(resource == nullptr ||
     resource->kind != Mmo::Presentation::PresentationResourceKind::WeaponVisual ||
     resource->keyUtf8.empty()) {
    return std::nullopt;
  }
  return resource->keyUtf8;
}

const char* toString(const ClientPresentationCatalogStatus value) noexcept {
  switch(value) {
    case ClientPresentationCatalogStatus::Applied: return "applied";
    case ClientPresentationCatalogStatus::EmptyPath: return "empty_path";
    case ClientPresentationCatalogStatus::FileOpenFailed: return "file_open_failed";
    case ClientPresentationCatalogStatus::FileTooLarge: return "file_too_large";
    case ClientPresentationCatalogStatus::FileReadFailed: return "file_read_failed";
    case ClientPresentationCatalogStatus::DecodeFailed: return "decode_failed";
    case ClientPresentationCatalogStatus::ManifestMismatch: return "manifest_mismatch";
    case ClientPresentationCatalogStatus::AllocationFailure: return "allocation_failure";
  }
  return "unknown";
}

} // namespace Mmo::ClientPresentation
