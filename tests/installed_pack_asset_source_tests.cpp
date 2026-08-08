#include "platform/native/installed_pack_asset_source_internal.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

namespace fs = std::filesystem;

using forevervalidator::AssetBytes;
using forevervalidator::AssetRequest;
using forevervalidator::Result;
using forevervalidator::ValidationErrorCode;
using forevervalidator::ValidationFailureReason;
using forevervalidator::native_detail::InstalledPackRoot;
using forevervalidator::native_detail::ReadInstalledPackAsset;
using forevervalidator::native_detail::ResolveInstalledPackRoot;

constexpr char LooseAssetPath[] =
        "Media/Texture/Image/Stadium/Advert/Panel.dds";

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::error_code error;
        const fs::path parent = fs::temp_directory_path(error);
        if (error) {
            throw std::runtime_error(
                    "could not resolve the system temporary directory");
        }

        for (unsigned int candidate = 0u; candidate < 4096u; candidate++) {
            const fs::path path = parent /
                    ("forevervalidator-installed-pack-asset-tests-" +
                     std::to_string(candidate));
            error.clear();
            if (fs::create_directory(path, error)) {
                path_ = path;
                return;
            }
            if (error && error != std::errc::file_exists) {
                throw std::runtime_error(
                        "could not create a temporary test directory");
            }
        }
        throw std::runtime_error(
                "could not reserve a unique temporary test directory");
    }

    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;

    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path &Path() const noexcept { return path_; }

private:
    fs::path path_;
};

struct InstalledPackLayout {
    fs::path root;
    fs::path gameData;
    fs::path packs;
    InstalledPackRoot installedRoot;
};

void RequireFileOperation(bool succeeded, const char *message,
                          const std::error_code &error) {
    if (!succeeded || error) {
        throw std::runtime_error(
                std::string(message) + ": " + error.message());
    }
}

InstalledPackLayout CreateLayout(const fs::path &root) {
    InstalledPackLayout layout;
    layout.root = root;
    layout.gameData = root / "GameData";
    layout.packs = root / "Packs";

    std::error_code error;
    const bool created = fs::create_directories(layout.packs, error);
    RequireFileOperation(created || fs::is_directory(layout.packs),
                         "could not create the installed-pack layout", error);
    error.clear();
    const bool createdGameData =
            fs::create_directories(layout.gameData, error);
    RequireFileOperation(
            createdGameData || fs::is_directory(layout.gameData),
            "could not create the GameData directory", error);

    error.clear();
    const fs::path canonicalPacks = fs::canonical(layout.packs, error);
    RequireFileOperation(!canonicalPacks.empty(),
                         "could not canonicalize the Packs directory", error);
    error.clear();
    const fs::path canonicalGameData = fs::canonical(layout.gameData, error);
    RequireFileOperation(!canonicalGameData.empty(),
                         "could not canonicalize the GameData directory", error);

    layout.installedRoot.canonicalPath = canonicalPacks.string();
    layout.installedRoot.canonicalGameDataPath = canonicalGameData.string();
    return layout;
}

void WriteFile(const fs::path &path, const std::string &contents) {
    std::error_code error;
    const bool created = fs::create_directories(path.parent_path(), error);
    RequireFileOperation(created || fs::is_directory(path.parent_path()),
                         "could not create an asset directory", error);

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(contents.data(),
                 static_cast<std::streamsize>(contents.size()));
    if (!stream) {
        throw std::runtime_error("could not write a temporary asset");
    }
}

std::string BytesAsString(const AssetBytes &bytes) {
    std::string text;
    text.reserve(bytes.size());
    for (const std::byte value : bytes) {
        text.push_back(
                static_cast<char>(std::to_integer<unsigned char>(value)));
    }
    return text;
}

bool ExpectPayload(const Result<AssetBytes> &result,
                   const std::string &expected,
                   const char *testName) {
    if (!result) {
        std::cerr << testName << " failed to read the asset: "
                  << result.Error().diagnostic << '\n';
        return false;
    }
    if (BytesAsString(result.Value()) != expected) {
        std::cerr << testName << " read the wrong asset payload\n";
        return false;
    }
    return true;
}

bool ExpectError(const Result<AssetBytes> &result,
                 ValidationErrorCode expectedCode,
                 ValidationFailureReason expectedReason,
                 const char *testName) {
    if (result) {
        std::cerr << testName << " unexpectedly read an asset\n";
        return false;
    }
    if (result.Error().code != expectedCode ||
        result.Error().reason != expectedReason) {
        std::cerr << testName << " returned the wrong error (code "
                  << static_cast<unsigned int>(result.Error().code)
                  << ", reason "
                  << static_cast<unsigned int>(result.Error().reason)
                  << ")\n";
        return false;
    }
    return true;
}

bool ExpectSiblingGameDataDiscovery(const InstalledPackLayout &layout) {
    const Result<InstalledPackRoot> resolved =
            ResolveInstalledPackRoot(layout.packs.string());
    if (!resolved) {
        std::cerr << "installed-pack root discovery failed: "
                  << resolved.Error().diagnostic << '\n';
        return false;
    }
    if (resolved.Value().canonicalPath !=
                layout.installedRoot.canonicalPath ||
        resolved.Value().canonicalGameDataPath !=
                layout.installedRoot.canonicalGameDataPath) {
        std::cerr << "installed-pack root discovery did not find sibling "
                     "GameData\n";
        return false;
    }
    return true;
}

bool TestLooseAssetFallsBackToGameData(const fs::path &testRoot) {
    const InstalledPackLayout layout = CreateLayout(testRoot);
    WriteFile(layout.gameData / fs::u8path(LooseAssetPath),
              "game-data-fallback");

    const Result<AssetBytes> result = ReadInstalledPackAsset(
            layout.installedRoot, AssetRequest{LooseAssetPath});
    return ExpectSiblingGameDataDiscovery(layout) &&
            ExpectPayload(result, "game-data-fallback",
                          "loose GameData fallback");
}

bool TestPacksAssetTakesPrecedence(const fs::path &testRoot) {
    const InstalledPackLayout layout = CreateLayout(testRoot);
    WriteFile(layout.gameData / fs::u8path(LooseAssetPath),
              "game-data-copy");
    WriteFile(layout.packs / fs::u8path(LooseAssetPath), "packs-copy");

    const Result<AssetBytes> result = ReadInstalledPackAsset(
            layout.installedRoot, AssetRequest{LooseAssetPath});
    return ExpectPayload(result, "packs-copy", "Packs precedence");
}

bool TestMissingAndConfinedPathsRemainErrors(const fs::path &testRoot) {
    const InstalledPackLayout layout = CreateLayout(testRoot);

    const Result<AssetBytes> missing = ReadInstalledPackAsset(
            layout.installedRoot,
            AssetRequest{"Media/Texture/Image/Stadium/Missing.dds"});
    bool passed = ExpectError(
            missing, ValidationErrorCode::AssetLoadingFailed,
            ValidationFailureReason::RequiredAssetMissing,
            "missing loose asset");

    WriteFile(layout.root / "outside.bin", "outside-root");
    const Result<AssetBytes> traversal = ReadInstalledPackAsset(
            layout.installedRoot, AssetRequest{"../outside.bin"});
    passed = ExpectError(
                     traversal, ValidationErrorCode::InvalidArgument,
                     ValidationFailureReason::InvalidAssetIdentifier,
                     "parent traversal") &&
            passed;

    // A symlink check covers canonical containment in addition to the
    // portable lexical traversal check above. Creating symlinks may require
    // an OS-specific privilege, so lack of that capability is not a failure.
    const fs::path packLink = layout.packs / "Media" / "escape.bin";
    std::error_code error;
    const bool created = fs::create_directories(packLink.parent_path(), error);
    RequireFileOperation(created || fs::is_directory(packLink.parent_path()),
                         "could not create the symlink test directory", error);
    WriteFile(layout.gameData / "Media" / "escape.bin",
              "fallback-must-not-hide-an-escape");
    error.clear();
    fs::create_symlink(layout.root / "outside.bin", packLink, error);
    if (!error) {
        const Result<AssetBytes> symlinkEscape = ReadInstalledPackAsset(
                layout.installedRoot, AssetRequest{"Media/escape.bin"});
        passed = ExpectError(
                         symlinkEscape,
                         ValidationErrorCode::AssetLoadingFailed,
                         ValidationFailureReason::AssetPathEscapesRoot,
                         "symlink escape") &&
                passed;
    }

    return passed;
}

}  // namespace

int main() {
    try {
        const TemporaryDirectory temporary;
        bool passed = TestLooseAssetFallsBackToGameData(
                temporary.Path() / "fallback");
        passed = TestPacksAssetTakesPrecedence(
                         temporary.Path() / "precedence") &&
                passed;
        passed = TestMissingAndConfinedPathsRemainErrors(
                         temporary.Path() / "errors") &&
                passed;
        return passed ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << "installed-pack asset source test setup failed: "
                  << error.what() << '\n';
        return 1;
    }
}
