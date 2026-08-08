#include <forevervalidator/native.h>

#include "platform/native/native_system_file_operations.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <utility>

#include "platform/native/installed_pack_asset_source_internal.h"

namespace forevervalidator {
namespace {

ValidationError AssetReadError(
        ValidationErrorCode code,
        ValidationFailureReason reason,
        const std::string &path,
        const char *diagnostic) {
    ValidationError error;
    error.category = code == ValidationErrorCode::AllocationFailed
            ? ValidationErrorCategory::Allocation
            : code == ValidationErrorCode::InvalidArgument
                    ? ValidationErrorCategory::InvalidInput
                    : ValidationErrorCategory::Asset;
    error.code = code;
    error.stage = ValidationStage::AssetLoading;
    error.reason = reason;
    error.relatedAsset = path;
    error.diagnostic = diagnostic;
    return error;
}

ValidationFailureReason MissingAssetReason(std::string_view identifier) {
    if (identifier == "Stadium.pak") {
        return ValidationFailureReason::StadiumPackMissing;
    }
    if (identifier == "packlist.dat") {
        return ValidationFailureReason::PacklistMissing;
    }
    if (identifier.size() > 4u &&
        identifier.substr(identifier.size() - 4u) == ".pak") {
        return ValidationFailureReason::InstalledPackMissing;
    }
    return ValidationFailureReason::RequiredAssetMissing;
}

bool IsPathWithin(
        const std::filesystem::path &root,
        const std::filesystem::path &candidate) {
    auto rootPart = root.begin();
    auto candidatePart = candidate.begin();
    for (; rootPart != root.end(); ++rootPart, ++candidatePart) {
        if (candidatePart == candidate.end() || *rootPart != *candidatePart) {
            return false;
        }
    }
    return true;
}

}  // namespace

namespace native_detail {

Result<InstalledPackRoot> ResolveInstalledPackRoot(
        const std::string &packDirectory) noexcept {
    try {
        if (packDirectory.empty()) {
            return Result<InstalledPackRoot>::Failure(AssetReadError(
                    ValidationErrorCode::InvalidArgument,
                    ValidationFailureReason::EmptyInstalledPackDirectory,
                    packDirectory,
                    "installed pack directory is empty"));
        }
        std::error_code error;
        const std::filesystem::path canonicalRoot =
                std::filesystem::canonical(
                        std::filesystem::u8path(packDirectory), error);
        if (error || !std::filesystem::is_directory(canonicalRoot, error) ||
            error) {
            return Result<InstalledPackRoot>::Failure(AssetReadError(
                    ValidationErrorCode::AssetSourceUnavailable,
                    ValidationFailureReason::AssetRepositoryUnavailable,
                    packDirectory,
                    "installed pack directory could not be resolved safely"));
        }
        std::string gameDataPath;
        const std::filesystem::path gameDataCandidate =
                canonicalRoot.parent_path() / "GameData";
        error.clear();
        if (std::filesystem::is_directory(gameDataCandidate, error) &&
            !error) {
            const std::filesystem::path canonicalGameData =
                    std::filesystem::canonical(gameDataCandidate, error);
            if (!error) {
                gameDataPath = canonicalGameData.string();
            }
        }
        return Result<InstalledPackRoot>::Success(
                InstalledPackRoot{
                        canonicalRoot.string(), std::move(gameDataPath)});
    } catch (const std::bad_alloc &) {
        return Result<InstalledPackRoot>::Failure(AssetReadError(
                ValidationErrorCode::AllocationFailed,
                ValidationFailureReason::AllocationFailed,
                packDirectory,
                "allocation failed while resolving installed pack directory"));
    } catch (...) {
        return Result<InstalledPackRoot>::Failure(AssetReadError(
                ValidationErrorCode::UnexpectedFailure,
                ValidationFailureReason::UnexpectedFailure,
                packDirectory,
                "unexpected failure while resolving installed pack directory"));
    }
}

Result<AssetBytes> ReadInstalledPackAsset(
        const InstalledPackRoot &root,
        const AssetRequest &request) noexcept {
    try {
        if (!IsNormalizedAssetIdentifier(request.logicalIdentifier)) {
            return Result<AssetBytes>::Failure(AssetReadError(
                    ValidationErrorCode::InvalidArgument,
                    ValidationFailureReason::InvalidAssetIdentifier,
                    request.logicalIdentifier,
                    "asset identifier is not a portable relative identifier"));
        }

        std::filesystem::path canonicalRoot(root.canonicalPath);
        const std::filesystem::path relative =
                std::filesystem::u8path(request.logicalIdentifier);
        if (relative.empty() || relative.is_absolute() ||
            relative.has_root_name() || relative.has_root_directory()) {
            return Result<AssetBytes>::Failure(AssetReadError(
                    ValidationErrorCode::InvalidArgument,
                    ValidationFailureReason::InvalidAssetIdentifier,
                    request.logicalIdentifier,
                    "asset identifier is not relative"));
        }

        std::filesystem::path candidate =
                (canonicalRoot / relative).lexically_normal();
        if (!IsPathWithin(canonicalRoot, candidate)) {
            return Result<AssetBytes>::Failure(AssetReadError(
                    ValidationErrorCode::AssetLoadingFailed,
                    ValidationFailureReason::AssetPathEscapesRoot,
                    request.logicalIdentifier,
                    "asset path escapes the installed pack directory"));
        }

        std::error_code error;
        std::filesystem::path resolvedCandidate =
                std::filesystem::weakly_canonical(candidate, error);
        if (error || !IsPathWithin(canonicalRoot, resolvedCandidate)) {
            return Result<AssetBytes>::Failure(AssetReadError(
                    ValidationErrorCode::AssetLoadingFailed,
                    ValidationFailureReason::AssetPathEscapesRoot,
                    request.logicalIdentifier,
                    "asset path containment could not be established"));
        }

        bool exists = std::filesystem::exists(candidate, error);
        if (error) {
            return Result<AssetBytes>::Failure(AssetReadError(
                    ValidationErrorCode::AssetLoadingFailed,
                    ValidationFailureReason::AssetProviderFailed,
                    request.logicalIdentifier,
                    "installed-pack asset status could not be read"));
        }
        // Keep the historical Packs-root behavior and use the sibling
        // GameData tree only as a fallback for nested authored asset paths.
        // This lets packed wrappers resolve loose DDS/TGA files without
        // changing precedence for existing callers or pack assets.
        if (!exists &&
            request.logicalIdentifier.find('/') != std::string::npos &&
            !root.canonicalGameDataPath.empty()) {
            canonicalRoot = root.canonicalGameDataPath;
            candidate = (canonicalRoot / relative).lexically_normal();
            if (!IsPathWithin(canonicalRoot, candidate)) {
                return Result<AssetBytes>::Failure(AssetReadError(
                        ValidationErrorCode::AssetLoadingFailed,
                        ValidationFailureReason::AssetPathEscapesRoot,
                        request.logicalIdentifier,
                        "asset path escapes the installed GameData directory"));
            }
            error.clear();
            resolvedCandidate =
                    std::filesystem::weakly_canonical(candidate, error);
            if (error || !IsPathWithin(canonicalRoot, resolvedCandidate)) {
                return Result<AssetBytes>::Failure(AssetReadError(
                        ValidationErrorCode::AssetLoadingFailed,
                        ValidationFailureReason::AssetPathEscapesRoot,
                        request.logicalIdentifier,
                        "asset path containment could not be established in "
                        "the installed GameData directory"));
            }
            error.clear();
            exists = std::filesystem::exists(candidate, error);
            if (error) {
                return Result<AssetBytes>::Failure(AssetReadError(
                        ValidationErrorCode::AssetLoadingFailed,
                        ValidationFailureReason::AssetProviderFailed,
                        request.logicalIdentifier,
                        "GameData asset status could not be read"));
            }
        }
        if (!exists) {
            return Result<AssetBytes>::Failure(AssetReadError(
                    ValidationErrorCode::AssetLoadingFailed,
                    MissingAssetReason(request.logicalIdentifier),
                    request.logicalIdentifier,
                    "installed-pack asset does not exist"));
        }

        const std::filesystem::path actualPath =
                std::filesystem::canonical(candidate, error);
        if (error || !IsPathWithin(canonicalRoot, actualPath)) {
            return Result<AssetBytes>::Failure(AssetReadError(
                    ValidationErrorCode::AssetLoadingFailed,
                    ValidationFailureReason::AssetPathEscapesRoot,
                    request.logicalIdentifier,
                    "installed-pack asset resolves outside the configured root"));
        }
        if (!std::filesystem::is_regular_file(actualPath, error) || error) {
            return Result<AssetBytes>::Failure(AssetReadError(
                    ValidationErrorCode::AssetLoadingFailed,
                    MissingAssetReason(request.logicalIdentifier),
                    request.logicalIdentifier,
                    "installed-pack asset is not a regular file"));
        }

        std::ifstream stream(actualPath, std::ios::binary | std::ios::ate);
        if (!stream) {
            return Result<AssetBytes>::Failure(AssetReadError(
                    ValidationErrorCode::AssetLoadingFailed,
                    MissingAssetReason(request.logicalIdentifier),
                    request.logicalIdentifier,
                    "could not open installed-pack asset"));
        }
        const std::streamoff length = stream.tellg();
        if (length <= 0 ||
            static_cast<std::uintmax_t>(length) >
                    std::numeric_limits<std::size_t>::max()) {
            return Result<AssetBytes>::Failure(AssetReadError(
                    ValidationErrorCode::AssetLoadingFailed,
                    MissingAssetReason(request.logicalIdentifier),
                    request.logicalIdentifier,
                    "installed-pack asset has invalid length"));
        }
        stream.seekg(0, std::ios::beg);
        AssetBytes bytes(static_cast<std::size_t>(length));
        if (!stream.read(reinterpret_cast<char *>(bytes.data()), length)) {
            return Result<AssetBytes>::Failure(AssetReadError(
                    ValidationErrorCode::AssetLoadingFailed,
                    ValidationFailureReason::AssetProviderFailed,
                    request.logicalIdentifier,
                    "could not read complete installed-pack asset"));
        }
        return Result<AssetBytes>::Success(std::move(bytes));
    } catch (const std::bad_alloc &) {
        return Result<AssetBytes>::Failure(AssetReadError(
                ValidationErrorCode::AllocationFailed,
                ValidationFailureReason::AllocationFailed,
                request.logicalIdentifier,
                "allocation failed while reading installed-pack asset"));
    } catch (...) {
        return Result<AssetBytes>::Failure(AssetReadError(
                ValidationErrorCode::UnexpectedFailure,
                ValidationFailureReason::UnexpectedFailure,
                request.logicalIdentifier,
                "unexpected installed-pack asset read failure"));
    }
}

}  // namespace native_detail

Result<AssetSource> OpenInstalledPackDirectory(
        const std::string &packDirectory) noexcept {
    tmnf::platform::InstallNativeSystemFileOperations();
    Result<native_detail::InstalledPackRoot> resolved =
            native_detail::ResolveInstalledPackRoot(packDirectory);
    if (!resolved) {
        return Result<AssetSource>::Failure(std::move(resolved).Error());
    }
    try {
        native_detail::InstalledPackRoot root = std::move(resolved).Value();
        return CreateAssetSource(
                [root = std::move(root)](const AssetRequest &request) {
                    return native_detail::ReadInstalledPackAsset(root, request);
                });
    } catch (const std::bad_alloc &) {
        return Result<AssetSource>::Failure(AssetReadError(
                ValidationErrorCode::AllocationFailed,
                ValidationFailureReason::AllocationFailed,
                packDirectory,
                "allocation failed while creating native asset source"));
    } catch (...) {
        return Result<AssetSource>::Failure(AssetReadError(
                ValidationErrorCode::UnexpectedFailure,
                ValidationFailureReason::UnexpectedFailure,
                packDirectory,
                "unexpected failure while creating native asset source"));
    }
}

}  // namespace forevervalidator
