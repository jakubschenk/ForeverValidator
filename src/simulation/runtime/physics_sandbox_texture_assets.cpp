#include "simulation/runtime/physics_sandbox_texture_assets.h"

#include <algorithm>
#include <mutex>
#include <new>
#include <optional>
#include <utility>

#include "engine/game/material_texture_asset_source.h"

namespace forevervalidator::experimental {
namespace {

using texture_assets_internal::PhysicsSandboxTextureAssetRegistration;

char AsciiLower(char value) {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

std::string CanonicalAssetKey(std::string_view sourceName,
                              std::string_view sourcePath) {
    std::string key;
    key.reserve(sourceName.size() + sourcePath.size() + 1u);
    for (char value : sourceName) {
        key.push_back(AsciiLower(value));
    }
    key.push_back('\0');
    for (char value : sourcePath) {
        key.push_back(AsciiLower(value));
    }
    return key;
}

PhysicsSandboxTextureAssetId StableAssetId(std::string_view key) {
    // FNV-1a is deliberately implemented here rather than using std::hash so
    // IDs remain identical across processes, standard libraries, and builds.
    constexpr std::uint64_t Offset = UINT64_C(14695981039346656037);
    constexpr std::uint64_t Prime = UINT64_C(1099511628211);
    std::uint64_t hash = Offset;
    constexpr std::string_view Domain =
            "forevervalidator:texture-asset:v1";
    for (char value : Domain) {
        hash = (hash ^ static_cast<unsigned char>(value)) * Prime;
    }
    for (char value : key) {
        hash = (hash ^ static_cast<unsigned char>(value)) * Prime;
    }
    return hash != 0u ? hash : 1u;
}

std::string LowerExtension(std::string_view path) {
    const std::size_t slash = path.find_last_of("\\/");
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string_view::npos ||
        (slash != std::string_view::npos && dot < slash)) {
        return {};
    }
    std::string extension(path.substr(dot));
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   AsciiLower);
    return extension;
}

void ClassifyEncoding(std::string_view logicalPath,
                      std::string_view sourcePath,
                      PhysicsSandboxTextureAssetEncoding *encoding,
                      std::string *mediaType) {
    std::string extension = LowerExtension(logicalPath);
    if (extension.empty()) {
        extension = LowerExtension(sourcePath);
    }
    if (extension == ".dds") {
        *encoding = PhysicsSandboxTextureAssetEncoding::Dds;
        *mediaType = "image/vnd-ms.dds";
    } else if (extension == ".tga") {
        *encoding = PhysicsSandboxTextureAssetEncoding::Tga;
        *mediaType = "image/x-tga";
    } else if (extension == ".png") {
        *encoding = PhysicsSandboxTextureAssetEncoding::Png;
        *mediaType = "image/png";
    } else if (extension == ".jpg" || extension == ".jpeg") {
        *encoding = PhysicsSandboxTextureAssetEncoding::Jpeg;
        *mediaType = "image/jpeg";
    } else if (extension == ".bmp") {
        *encoding = PhysicsSandboxTextureAssetEncoding::Bmp;
        *mediaType = "image/bmp";
    } else {
        *encoding = PhysicsSandboxTextureAssetEncoding::Unknown;
        *mediaType = "application/octet-stream";
    }
}

PhysicsSandboxTextureAssetError TextureError(
        PhysicsSandboxTextureAssetErrorCode code,
        PhysicsSandboxTextureAssetId id,
        std::string sourcePath,
        std::string diagnostic) {
    PhysicsSandboxTextureAssetError error;
    error.code = code;
    error.id = id;
    error.sourcePath = std::move(sourcePath);
    error.diagnostic = std::move(diagnostic);
    return error;
}

PhysicsSandboxTextureAssetErrorCode PublicErrorCode(
        MaterialTextureAssetSourceErrorCode code) {
    switch (code) {
    case MaterialTextureAssetSourceErrorCode::SourceUnavailable:
        return PhysicsSandboxTextureAssetErrorCode::SourceUnavailable;
    case MaterialTextureAssetSourceErrorCode::SourceNotFound:
        return PhysicsSandboxTextureAssetErrorCode::SourceNotFound;
    case MaterialTextureAssetSourceErrorCode::ExtractionFailed:
        return PhysicsSandboxTextureAssetErrorCode::ExtractionFailed;
    case MaterialTextureAssetSourceErrorCode::AllocationFailed:
        return PhysicsSandboxTextureAssetErrorCode::AllocationFailed;
    case MaterialTextureAssetSourceErrorCode::UnexpectedFailure:
        return PhysicsSandboxTextureAssetErrorCode::UnexpectedFailure;
    }
    return PhysicsSandboxTextureAssetErrorCode::UnexpectedFailure;
}

}  // namespace

struct PhysicsSandboxTextureAssetResolver::Impl {
    struct AssetState {
        explicit AssetState(
                std::shared_ptr<const MaterialTextureAssetSource> value)
                : source(std::move(value)) {}

        std::shared_ptr<const MaterialTextureAssetSource> source;
        std::mutex mutex;
        PhysicsSandboxTextureAssetHandle payload;
        std::optional<PhysicsSandboxTextureAssetError> error;
    };

    explicit Impl(
            std::vector<PhysicsSandboxTextureAssetRegistration>
                    registrations) {
        metadata.reserve(registrations.size());
        states.reserve(registrations.size());
        indices.reserve(registrations.size());
        for (PhysicsSandboxTextureAssetRegistration &registration :
             registrations) {
            const std::size_t index = metadata.size();
            indices.emplace(registration.metadata.id, index);
            metadata.push_back(std::move(registration.metadata));
            states.push_back(std::make_unique<AssetState>(
                    std::move(registration.source)));
        }
    }

    PhysicsSandboxTextureAssetReadResult Read(
            PhysicsSandboxTextureAssetId id) const {
        const auto found = indices.find(id);
        if (found == indices.end()) {
            return PhysicsSandboxTextureAssetReadResult::Failure(
                    TextureError(
                            PhysicsSandboxTextureAssetErrorCode::UnknownAsset,
                            id,
                            {},
                            "texture asset ID is not present in this scene"));
        }
        const std::size_t index = found->second;
        AssetState &state = *states[index];
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.payload) {
            return PhysicsSandboxTextureAssetReadResult::Success(
                    state.payload);
        }
        if (state.error.has_value()) {
            return PhysicsSandboxTextureAssetReadResult::Failure(
                    *state.error);
        }

        if (!state.source) {
            state.error = TextureError(
                    PhysicsSandboxTextureAssetErrorCode::SourceUnavailable,
                    id,
                    metadata[index].sourcePath,
                    "the scene did not retain a source for this texture");
            return PhysicsSandboxTextureAssetReadResult::Failure(
                    *state.error);
        }

        MaterialTextureAssetSourceResult bytes =
                state.source->ReadEncodedBytes(metadata[index].sourcePath);
        if (!bytes) {
            MaterialTextureAssetSourceError sourceError =
                    std::move(bytes).Error();
            state.error = TextureError(
                    PublicErrorCode(sourceError.code),
                    id,
                    metadata[index].sourcePath,
                    std::move(sourceError.diagnostic));
            return PhysicsSandboxTextureAssetReadResult::Failure(
                    *state.error);
        }

        auto payload = std::make_shared<PhysicsSandboxTextureAsset>();
        payload->metadata = metadata[index];
        payload->encodedBytes = std::move(bytes).Value();
        state.payload = std::move(payload);
        return PhysicsSandboxTextureAssetReadResult::Success(
                state.payload);
    }

    std::vector<PhysicsSandboxTextureAssetMetadata> metadata;
    std::vector<std::unique_ptr<AssetState>> states;
    std::unordered_map<PhysicsSandboxTextureAssetId, std::size_t> indices;
};

PhysicsSandboxTextureAssetResolver::PhysicsSandboxTextureAssetResolver()
        noexcept = default;
PhysicsSandboxTextureAssetResolver::PhysicsSandboxTextureAssetResolver(
        const PhysicsSandboxTextureAssetResolver &) noexcept = default;
PhysicsSandboxTextureAssetResolver &
PhysicsSandboxTextureAssetResolver::operator=(
        const PhysicsSandboxTextureAssetResolver &) noexcept = default;
PhysicsSandboxTextureAssetResolver::PhysicsSandboxTextureAssetResolver(
        PhysicsSandboxTextureAssetResolver &&) noexcept = default;
PhysicsSandboxTextureAssetResolver &
PhysicsSandboxTextureAssetResolver::operator=(
        PhysicsSandboxTextureAssetResolver &&) noexcept = default;
PhysicsSandboxTextureAssetResolver::~PhysicsSandboxTextureAssetResolver() =
        default;

PhysicsSandboxTextureAssetResolver::PhysicsSandboxTextureAssetResolver(
        std::shared_ptr<const Impl> impl) noexcept
        : impl_(std::move(impl)) {}

const std::vector<PhysicsSandboxTextureAssetMetadata> &
PhysicsSandboxTextureAssetResolver::Assets() const noexcept {
    static const std::vector<PhysicsSandboxTextureAssetMetadata> Empty;
    return impl_ ? impl_->metadata : Empty;
}

PhysicsSandboxTextureAssetReadResult
PhysicsSandboxTextureAssetResolver::Read(
        PhysicsSandboxTextureAssetId id) const noexcept {
    try {
        if (!impl_) {
            return PhysicsSandboxTextureAssetReadResult::Failure(
                    TextureError(
                            PhysicsSandboxTextureAssetErrorCode::
                                    InvalidResolver,
                            id,
                            {},
                            "texture asset resolver is empty"));
        }
        return impl_->Read(id);
    } catch (const std::bad_alloc &) {
        PhysicsSandboxTextureAssetError error;
        error.code = PhysicsSandboxTextureAssetErrorCode::AllocationFailed;
        error.id = id;
        return PhysicsSandboxTextureAssetReadResult::Failure(
                std::move(error));
    } catch (...) {
        PhysicsSandboxTextureAssetError error;
        error.code = PhysicsSandboxTextureAssetErrorCode::UnexpectedFailure;
        error.id = id;
        return PhysicsSandboxTextureAssetReadResult::Failure(
                std::move(error));
    }
}

namespace texture_assets_internal {

PhysicsSandboxTextureAssetResolver
PhysicsSandboxTextureAssetResolverFactory::Create(
        std::vector<PhysicsSandboxTextureAssetRegistration> registrations) {
    if (registrations.empty()) {
        return {};
    }
    return PhysicsSandboxTextureAssetResolver(
            std::make_shared<PhysicsSandboxTextureAssetResolver::Impl>(
                    std::move(registrations)));
}

PhysicsSandboxTextureAssetId PhysicsSandboxTextureAssetRegistry::Add(
        const MaterialRenderBitmapDefinition &bitmap,
        std::string *diagnostic) {
    if (diagnostic != nullptr) {
        *diagnostic = bitmap.imageDiagnostic;
    }
    if (bitmap.imageSelectedPath.empty()) {
        return 0u;
    }
    if (!bitmap.imageSource) {
        if (diagnostic != nullptr && diagnostic->empty()) {
            *diagnostic =
                    "texture path was decoded, but its pack source was not "
                    "retained";
        }
        return 0u;
    }
    const std::string_view sourceName =
            bitmap.imageSource->StableNamespace();
    if (sourceName.empty()) {
        if (diagnostic != nullptr && diagnostic->empty()) {
            *diagnostic = "texture source has no stable namespace";
        }
        return 0u;
    }

    std::string key = CanonicalAssetKey(
            sourceName, bitmap.imageSelectedPath);
    const auto existing = idsByKey_.find(key);
    if (existing != idsByKey_.end()) {
        return existing->second;
    }
    const PhysicsSandboxTextureAssetId id = StableAssetId(key);
    const auto collision = keysById_.find(id);
    if (collision != keysById_.end() && collision->second != key) {
        if (diagnostic != nullptr) {
            *diagnostic =
                    "deterministic texture asset ID collision; the asset was "
                    "not registered";
        }
        return 0u;
    }

    PhysicsSandboxTextureAssetRegistration registration;
    registration.metadata.id = id;
    registration.metadata.logicalPath = bitmap.imagePlainPath;
    registration.metadata.sourcePath = bitmap.imageSelectedPath;
    registration.metadata.sourceName.assign(
            sourceName.data(), sourceName.size());
    registration.metadata.encodedByteCount =
            bitmap.imageEncodedByteCount;
    registration.metadata.imageClassId = bitmap.imageClassId;
    ClassifyEncoding(registration.metadata.logicalPath,
                     registration.metadata.sourcePath,
                     &registration.metadata.encoding,
                     &registration.metadata.mediaType);
    registration.source = bitmap.imageSource;
    registrations_.push_back(std::move(registration));
    idsByKey_.emplace(key, id);
    keysById_.emplace(id, std::move(key));
    return id;
}

PhysicsSandboxTextureAssetResolver
PhysicsSandboxTextureAssetRegistry::Build() && {
    return PhysicsSandboxTextureAssetResolverFactory::Create(
            std::move(registrations_));
}

}  // namespace texture_assets_internal
}  // namespace forevervalidator::experimental
