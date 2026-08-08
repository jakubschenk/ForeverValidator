#pragma once

#include <string>

#include <forevervalidator/validation.h>

namespace forevervalidator::native_detail {

struct InstalledPackRoot {
    std::string canonicalPath;
    // Loose game assets referenced by packed GBX files live beside Packs.
    std::string canonicalGameDataPath;
};

Result<InstalledPackRoot> ResolveInstalledPackRoot(
        const std::string &packDirectory) noexcept;
Result<AssetBytes> ReadInstalledPackAsset(
        const InstalledPackRoot &root,
        const AssetRequest &request) noexcept;

}  // namespace forevervalidator::native_detail
