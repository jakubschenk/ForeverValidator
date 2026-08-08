#include "format/materials/material_pack_repository.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "engine/game/material_texture_asset_source.h"
#include "format/pack/installed/byte_buffer.h"
#include "format/materials/material_archive_decoder.h"
#include "format/materials/material_render_archive_decoder.h"
#include "format/pack/installed/plug_file_pack.h"
#include "format/pack/installed/scene_descriptor_folder_paths.h"
#include "format/materials/skin_material_remap_catalog.h"
#include <new>
namespace {

MaterialTextureAssetSourceResult TextureSourceFailure(
        MaterialTextureAssetSourceErrorCode code,
        std::string diagnostic) {
    MaterialTextureAssetSourceError error;
    error.code = code;
    error.diagnostic = std::move(diagnostic);
    return MaterialTextureAssetSourceResult::Failure(std::move(error));
}

class PackMaterialTextureAssetSource final
        : public MaterialTextureAssetSource {
public:
    PackMaterialTextureAssetSource(
            std::shared_ptr<const CPlugFilePack> pack,
            forevervalidator::AssetProvider looseAssetProvider)
            : pack_(std::move(pack)),
              looseAssetProvider_(std::move(looseAssetProvider)),
              stableNamespace_(pack_ ? pack_->PackName() : std::string{}) {}

    std::string_view StableNamespace(void) const noexcept override {
        return stableNamespace_;
    }

    MaterialTextureAssetSourceResult ReadEncodedBytes(
            std::string_view selectedPath) const noexcept override {
        try {
            if (!pack_) {
                return TextureSourceFailure(
                        MaterialTextureAssetSourceErrorCode::
                                SourceUnavailable,
                        "installed pack storage is no longer available");
            }
            if (selectedPath.empty()) {
                return TextureSourceFailure(
                        MaterialTextureAssetSourceErrorCode::SourceNotFound,
                        "texture source path is empty");
            }
            const std::string path(selectedPath);
            const CPlugFileFidContainer_SFileDesc *descriptor =
                    pack_->FindFileDescByPath(path.c_str());
            if (descriptor == nullptr && !looseAssetProvider_) {
                return TextureSourceFailure(
                        MaterialTextureAssetSourceErrorCode::SourceNotFound,
                        "texture is not present in installed pack '" +
                                stableNamespace_ + "': " + path);
            }
            if (descriptor == nullptr) {
                forevervalidator::AssetRequest request;
                request.logicalIdentifier = path;
                std::replace(request.logicalIdentifier.begin(),
                             request.logicalIdentifier.end(),
                             '\\', '/');
                forevervalidator::Result<forevervalidator::AssetBytes> loose =
                        looseAssetProvider_(request);
                if (loose) {
                    return MaterialTextureAssetSourceResult::Success(
                            std::move(loose).Value());
                }
                forevervalidator::ValidationError error =
                        std::move(loose).Error();
                MaterialTextureAssetSourceErrorCode code =
                        MaterialTextureAssetSourceErrorCode::ExtractionFailed;
                if (error.code ==
                    forevervalidator::ValidationErrorCode::AllocationFailed) {
                    code = MaterialTextureAssetSourceErrorCode::
                            AllocationFailed;
                } else if (
                        error.code == forevervalidator::ValidationErrorCode::
                                              AssetSourceUnavailable) {
                    code = MaterialTextureAssetSourceErrorCode::
                            SourceUnavailable;
                } else if (
                        error.code == forevervalidator::ValidationErrorCode::
                                              AssetLoadingFailed &&
                        (error.reason == forevervalidator::
                                                 ValidationFailureReason::
                                                         RequiredAssetMissing ||
                         error.reason == forevervalidator::
                                                 ValidationFailureReason::
                                                         InstalledPackMissing ||
                         error.reason == forevervalidator::
                                                 ValidationFailureReason::
                                                         StadiumPackMissing ||
                         error.reason == forevervalidator::
                                                 ValidationFailureReason::
                                                         PacklistMissing)) {
                    code = MaterialTextureAssetSourceErrorCode::SourceNotFound;
                }
                if (error.diagnostic.empty()) {
                    error.diagnostic =
                            "loose texture provider could not read: " + path;
                }
                return TextureSourceFailure(
                        code, std::move(error.diagnostic));
            }
            ByteBuffer bytes;
            if (!pack_->ExtractPath(path.c_str(), &bytes) || bytes.Empty()) {
                return TextureSourceFailure(
                        MaterialTextureAssetSourceErrorCode::ExtractionFailed,
                        "failed to decode texture payload from installed "
                        "pack '" + stableNamespace_ + "': " + path);
            }
            if (bytes.Size() != descriptor->uncompressedSize) {
                return TextureSourceFailure(
                        MaterialTextureAssetSourceErrorCode::ExtractionFailed,
                        "decoded texture byte count does not match the pack "
                        "descriptor: " + path);
            }
            std::vector<std::byte> encoded(bytes.Size());
            std::memcpy(encoded.data(), bytes.Data(), bytes.Size());
            return MaterialTextureAssetSourceResult::Success(
                    std::move(encoded));
        } catch (const std::bad_alloc &) {
            MaterialTextureAssetSourceError error;
            error.code =
                    MaterialTextureAssetSourceErrorCode::AllocationFailed;
            return MaterialTextureAssetSourceResult::Failure(
                    std::move(error));
        } catch (...) {
            MaterialTextureAssetSourceError error;
            error.code =
                    MaterialTextureAssetSourceErrorCode::UnexpectedFailure;
            return MaterialTextureAssetSourceResult::Failure(
                    std::move(error));
        }
    }

private:
    std::shared_ptr<const CPlugFilePack> pack_;
    forevervalidator::AssetProvider looseAssetProvider_;
    std::string stableNamespace_;
};

struct StoredMaterialAsset {
    std::string path;
    MaterialSurfaceDefinition surface;
    MaterialRenderDefinition render;
};

std::optional<MaterialAssetDefinition> CopyMaterialAssetDefinition(
        u32 index,
        const StoredMaterialAsset &stored) noexcept {
    try {
        return MaterialAssetDefinition{
                MaterialAssetHandle::FromRepositoryIndex(index),
                stored.surface,
                stored.render};
    } catch (const std::bad_alloc &) {
        return std::nullopt;
    }
}

bool ExtractMaterialBytes(const CPlugFilePack &pack,
                          const std::string &path,
                          ByteBuffer &bytes) {
    if (pack.ExtractPath(path.c_str(), &bytes)) {
        return true;
    }
    const std::size_t slash = path.find_last_of('\\');
    if (slash == std::string::npos) {
        return false;
    }
    const std::string base = path.substr(0u, slash + 1u);
    char hashedPath[512]{};
    return SceneDescriptorFolderPaths::HashFileNameDescriptorPathWithBase(
                   path.c_str(),
                   base.c_str(),
                   hashedPath,
                   sizeof(hashedPath)) &&
           pack.ExtractPath(hashedPath, &bytes);
}

}  // namespace

struct MaterialPackRepository::Impl {
    explicit Impl(CPlugFilePack &sourcePack) : pack(&sourcePack) {}

    Impl(std::shared_ptr<const CPlugFilePack> sourcePack,
         forevervalidator::AssetProvider looseAssetProvider)
            : ownedPack(std::move(sourcePack)),
              pack(ownedPack.get()),
              textureSource(
                      std::make_shared<PackMaterialTextureAssetSource>(
                              ownedPack,
                              std::move(looseAssetProvider))) {}

    std::shared_ptr<const CPlugFilePack> ownedPack;
    const CPlugFilePack *pack = nullptr;
    std::shared_ptr<const MaterialTextureAssetSource> textureSource;
    std::vector<StoredMaterialAsset> assets;
    SkinMaterialRemapCatalog remaps;
    bool remapsLoaded = false;

    std::optional<MaterialAssetDefinition> Load(
            const std::string &path) noexcept;
};

std::optional<MaterialAssetDefinition> MaterialPackRepository::Impl::Load(
        const std::string &path) noexcept {
    try {
        for (u32 index = 0u; index < assets.size(); ++index) {
            if (assets[index].path == path) {
                return CopyMaterialAssetDefinition(index, assets[index]);
            }
        }
        if (assets.size() >= UINT32_MAX) {
            return std::nullopt;
        }
        ByteBuffer bytes;
        if (pack == nullptr || !ExtractMaterialBytes(*pack, path, bytes) ||
            bytes.Empty() || bytes.Size() > UINT32_MAX) {
            return std::nullopt;
        }
        std::optional<MaterialSurfaceDefinition> surface =
                DecodeMaterialArchive(
                        bytes.Data(), static_cast<u32>(bytes.Size()));
        if (!surface) {
            return std::nullopt;
        }
        MaterialRenderDefinition render;
        std::optional<MaterialRenderDefinition> decodedRender =
                DecodeMaterialRenderArchive(
                        *pack, path.c_str(), textureSource);
        if (decodedRender) {
            render = std::move(*decodedRender);
        }
        assets.push_back({path, *surface, std::move(render)});
        return CopyMaterialAssetDefinition(
                static_cast<u32>(assets.size() - 1u), assets.back());
    } catch (const std::bad_alloc &) {
        return std::nullopt;
    }
}

MaterialPackRepository::MaterialPackRepository(CPlugFilePack &pack)
        : impl_(std::make_unique<Impl>(pack)) {}

MaterialPackRepository::MaterialPackRepository(
        std::shared_ptr<const CPlugFilePack> pack)
        : MaterialPackRepository(std::move(pack), {}) {}

MaterialPackRepository::MaterialPackRepository(
        std::shared_ptr<const CPlugFilePack> pack,
        forevervalidator::AssetProvider looseAssetProvider)
        : impl_(std::make_unique<Impl>(
                  std::move(pack), std::move(looseAssetProvider))) {}

MaterialPackRepository::~MaterialPackRepository() = default;

std::optional<ResolvedMaterialDefinition> MaterialPackRepository::Resolve(
        std::string_view identifier) {
    if (identifier.empty()) {
        return std::nullopt;
    }
    std::string sourcePath;
    try {
        if (impl_->pack == nullptr) {
            return std::nullopt;
        }
        sourcePath = impl_->pack->PackName();
        sourcePath.append("\\Media\\Material\\");
        sourcePath.append(identifier.data(), identifier.size());
    } catch (const std::bad_alloc &) {
        return std::nullopt;
    }
    return ResolvePath(sourcePath);
}

std::optional<ResolvedMaterialDefinition> MaterialPackRepository::ResolvePath(
        std::string_view plainPath) {
    if (plainPath.empty()) {
        return std::nullopt;
    }
    std::string sourcePath;
    try {
        sourcePath.assign(plainPath.data(), plainPath.size());
    } catch (const std::bad_alloc &) {
        return std::nullopt;
    }
    if (sourcePath.find(".Material.Gbx") == std::string::npos) {
        return std::nullopt;
    }
    std::optional<MaterialAssetDefinition> source = impl_->Load(sourcePath);
    if (!source) {
        return std::nullopt;
    }

    if (!impl_->remapsLoaded) {
        if (impl_->pack == nullptr || !impl_->remaps.Load(*impl_->pack)) {
            return std::nullopt;
        }
        impl_->remapsLoaded = true;
    }

    try {
        ResolvedMaterialDefinition resolved;
        resolved.material = *source;
        const std::optional<DecorationTerrainModifierRemapPath> remap =
                impl_->remaps.FindDecorationTerrainModifier(sourcePath);
        if (!remap) {
            return resolved;
        }
        std::optional<MaterialAssetDefinition> target =
                impl_->Load(remap->target);
        resolved.remaps.terrainModifierDirt = std::move(target);
        return resolved;
    } catch (const std::bad_alloc &) {
        return std::nullopt;
    }
}
