#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <forevervalidator/result.h>

enum class MaterialTextureAssetSourceErrorCode {
    SourceUnavailable,
    SourceNotFound,
    ExtractionFailed,
    AllocationFailed,
    UnexpectedFailure,
};

struct MaterialTextureAssetSourceError {
    MaterialTextureAssetSourceErrorCode code =
            MaterialTextureAssetSourceErrorCode::UnexpectedFailure;
    std::string diagnostic;
};

using MaterialTextureAssetSourceResult =
        forevervalidator::DiscriminatedResult<
                std::vector<std::byte>,
                MaterialTextureAssetSourceError>;

// Immutable source for encoded image files referenced by decoded materials.
// Implementations must retain their backing storage for the source lifetime.
class MaterialTextureAssetSource {
public:
    virtual ~MaterialTextureAssetSource() = default;

    virtual std::string_view StableNamespace(void) const noexcept = 0;
    virtual MaterialTextureAssetSourceResult ReadEncodedBytes(
            std::string_view selectedPath) const noexcept = 0;
};
