#include "format/pack/block_info_catalog/installed_pack_asset_repository.h"
#include <cmath>
#include <utility>
#include <vector>

#include "format/pack/block_info_catalog/block_info_asset_store.h"
#include "engine/resources/block_info_catalog.h"
#include "engine/game/replay_challenge_map_input.h"
#include "format/pack/block_info_catalog/block_info_catalog_loader.h"
#include "format/pack/block_info_catalog/collection_surface_replacement_pairs.h"
#include "format/pack/block_info_catalog/installed_pack_asset_repository_internal.h"
#include "format/materials/material_pack_repository.h"
#include "format/pack/installed/plug_file_pack.h"
#include "format/pack/installed/installed_pack_key_catalog.h"
#include "format/pack/block_info_catalog/replay_collection_zone_sources.h"
#include "format/pack/block_info_catalog/replay_decoration_size.h"
#include "format/static_solid/static_solid_archive_reference.h"
#include <new>
namespace {

CatalogConstructionZoneKind ToSemanticZoneKind(
        CGameCtnReplayConstructionZoneKind kind) {
    switch (kind) {
        case CGameCtnReplayConstructionZoneKind::Flat:
            return CatalogConstructionZoneKind::Flat;
        case CGameCtnReplayConstructionZoneKind::Frontier:
            return CatalogConstructionZoneKind::Frontier;
        case CGameCtnReplayConstructionZoneKind::Other:
            return CatalogConstructionZoneKind::Other;
        case CGameCtnReplayConstructionZoneKind::None:
            return CatalogConstructionZoneKind::None;
    }
    return CatalogConstructionZoneKind::None;
}

bool IsValidCollectionGridDimension(float value) {
    return std::isfinite(value) && value > 0.0f;
}

float ResolveCollectionSquareHeight(
        std::string_view collectionName,
        float squareSize,
        float archivedSquareHeight) {
    if (IsValidCollectionGridDimension(archivedSquareHeight)) {
        return archivedSquareHeight;
    }
    // Coast's protected TMCollection tail does not expose a usable height in
    // the installed pack. The game's legacy-map conversion and canonical
    // replay spawn coordinates both establish its 16 x 4 x 16 grid.
    if (collectionName == "Coast" && squareSize == 16.0f) {
        return 4.0f;
    }
    return 0.0f;
}

struct LoadedCollection {
    std::string name;
    CGameCtnReplayCollectionZoneSources zoneSources;
    CollectionReplacementPairs replacementPairs;
    CatalogCollectionDefinition definition;
};

struct LoadedDecorationSize {
    std::string mood;
    std::string collection;
    std::string author;
    GmNat3 mapSize{};
    CatalogDecorationSizeDefinition definition;

    bool Matches(const CGameCtnReplayMapInput &mapInput) const {
        const GmNat3 &requestedSize = mapInput.Size();
        return mood == mapInput.DecorationMood().Name() &&
               collection == mapInput.DecorationCollection().Name() &&
               author == mapInput.DecorationAuthor().Name() &&
               mapSize.x == requestedSize.x &&
               mapSize.y == requestedSize.y &&
               mapSize.z == requestedSize.z &&
               ReplayDecorationSizeMatchesMap(
                       mapInput, definition.mapSize);
    }
};

}  // namespace

struct InstalledPackAssetRepository::Impl {
    std::vector<std::byte> pakBytes;
    std::vector<std::byte> packlistBytes;
    InstalledPackKeyCatalog keyCatalog;
    bool hasKeyCatalog = false;
    std::string packName = "Stadium";
    std::shared_ptr<CPlugFilePack> pack =
            std::make_shared<CPlugFilePack>();
    forevervalidator::AssetProvider looseAssetProvider;
    bool packReady = false;
    BlockInfoCatalog catalog;
    bool catalogReady = false;
    std::unique_ptr<BlockInfoAssetStore> blockInfos;
    std::unique_ptr<MaterialPackRepository> materials;
    StaticSolidArchiveReferenceCatalog solidReferences;
    std::vector<std::unique_ptr<LoadedCollection>> collections;
    std::vector<LoadedDecorationSize> decorationSizes;
};

InstalledPackAssetRepository::InstalledPackAssetRepository()
        : impl_(std::make_unique<Impl>()) {}

InstalledPackAssetRepository::~InstalledPackAssetRepository() = default;

std::unique_ptr<ReplayAssetRepository> OpenReplayAssetRepository(
        const std::byte *pakBytes,
        std::size_t pakByteCount,
        const std::byte *packlistBytes,
        std::size_t packlistByteCount) {
    return OpenReplayAssetRepository(
            pakBytes, pakByteCount,
            packlistBytes, packlistByteCount,
            "Stadium");
}

std::unique_ptr<ReplayAssetRepository> OpenReplayAssetRepository(
        const std::byte *pakBytes,
        std::size_t pakByteCount,
        const std::byte *packlistBytes,
        std::size_t packlistByteCount,
        const char *packName) {
    try {
        auto repository = std::make_unique<InstalledPackAssetRepository>();
        if (!repository->Configure(pakBytes,
                                   pakByteCount,
                                   packlistBytes,
                                   packlistByteCount,
                                   packName)) {
            return {};
        }
        return repository;
    } catch (const std::bad_alloc &) {
        return {};
    }
}

std::unique_ptr<ReplayAssetRepository> OpenReplayAssetRepository(
        const std::byte *pakBytes,
        std::size_t pakByteCount,
        const InstalledPackKeyCatalog &keyCatalog,
        const char *packName) {
    return OpenReplayAssetRepository(
            pakBytes,
            pakByteCount,
            keyCatalog,
            packName,
            {});
}

std::unique_ptr<ReplayAssetRepository> OpenReplayAssetRepository(
        const std::byte *pakBytes,
        std::size_t pakByteCount,
        const InstalledPackKeyCatalog &keyCatalog,
        const char *packName,
        forevervalidator::AssetProvider looseAssetProvider) {
    try {
        auto repository = std::make_unique<InstalledPackAssetRepository>();
        if (!repository->Configure(
                    pakBytes,
                    pakByteCount,
                    keyCatalog,
                    packName,
                    std::move(looseAssetProvider))) {
            return {};
        }
        return repository;
    } catch (const std::bad_alloc &) {
        return {};
    }
}

bool InstalledPackAssetRepository::Configure(
        const std::byte *pakBytes,
        std::size_t pakByteCount,
        const std::byte *packlistBytes,
        std::size_t packlistByteCount) {
    return Configure(
            pakBytes, pakByteCount,
            packlistBytes, packlistByteCount,
            "Stadium");
}

bool InstalledPackAssetRepository::Configure(
        const std::byte *pakBytes,
        std::size_t pakByteCount,
        const std::byte *packlistBytes,
        std::size_t packlistByteCount,
        const char *packName) {
    if (pakBytes == nullptr || pakByteCount == 0u ||
        packlistBytes == nullptr || packlistByteCount == 0u ||
        packName == nullptr || *packName == '\0') {
        Clear();
        return false;
    }
    try {
        auto next = std::make_unique<Impl>();
        next->pakBytes.assign(pakBytes, pakBytes + pakByteCount);
        next->packlistBytes.assign(
                packlistBytes, packlistBytes + packlistByteCount);
        next->packName = packName;
        impl_ = std::move(next);
        return true;
    } catch (const std::bad_alloc &) {
        Clear();
        return false;
    }
}

bool InstalledPackAssetRepository::Configure(
        const std::byte *pakBytes,
        std::size_t pakByteCount,
        const InstalledPackKeyCatalog &keyCatalog,
        const char *packName) {
    return Configure(
            pakBytes,
            pakByteCount,
            keyCatalog,
            packName,
            {});
}

bool InstalledPackAssetRepository::Configure(
        const std::byte *pakBytes,
        std::size_t pakByteCount,
        const InstalledPackKeyCatalog &keyCatalog,
        const char *packName,
        forevervalidator::AssetProvider looseAssetProvider) {
    if (pakBytes == nullptr || pakByteCount == 0u ||
        packName == nullptr || *packName == '\0' ||
        keyCatalog.Find(packName) == nullptr) {
        Clear();
        return false;
    }
    try {
        auto next = std::make_unique<Impl>();
        next->pakBytes.assign(pakBytes, pakBytes + pakByteCount);
        next->keyCatalog = keyCatalog;
        next->hasKeyCatalog = true;
        next->packName = packName;
        next->looseAssetProvider = std::move(looseAssetProvider);
        impl_ = std::move(next);
        return true;
    } catch (const std::bad_alloc &) {
        Clear();
        return false;
    }
}

void InstalledPackAssetRepository::Clear() {
    impl_ = std::make_unique<Impl>();
}

bool InstalledPackAssetRepository::IsConfigured() const {
    return impl_ != nullptr && !impl_->pakBytes.empty() &&
           (impl_->hasKeyCatalog || !impl_->packlistBytes.empty()) &&
           !impl_->packName.empty();
}

bool InstalledPackAssetRepository::EnsurePack() {
    if (!IsConfigured()) {
        return false;
    }
    if (impl_->packReady) {
        return true;
    }
    const int opened = impl_->hasKeyCatalog
            ? impl_->pack->OpenFromMemory(
                    impl_->pakBytes.data(),
                    impl_->pakBytes.size(),
                    impl_->keyCatalog,
                    impl_->packName.c_str())
            : impl_->pack->OpenFromMemory(
                    impl_->pakBytes.data(),
                    impl_->pakBytes.size(),
                    impl_->packlistBytes.data(),
                    impl_->packlistBytes.size(),
                    impl_->packName.c_str());
    if (!opened) {
        return false;
    }
    impl_->packReady = true;
    impl_->blockInfos = std::make_unique<BlockInfoAssetStore>(
            *impl_->pack, impl_->solidReferences);
    impl_->materials = std::make_unique<MaterialPackRepository>(
            impl_->pack, impl_->looseAssetProvider);
    return true;
}

const BlockInfoCatalog *InstalledPackAssetRepository::Catalog() {
    if (!EnsurePack() || impl_->blockInfos == nullptr) {
        return nullptr;
    }
    if (!impl_->catalogReady) {
        if (!LoadBlockInfoCatalog(
                    *impl_->pack, *impl_->blockInfos, impl_->catalog)) {
            return nullptr;
        }
        CGameCtnReplayCollectionZoneSources zoneSources;
        if (!zoneSources.LoadFromCatalogCollection(
                    impl_->pack.get(), impl_->packName.c_str()) ||
            !zoneSources.ApplyCollectionLandZoneHeights(
                    impl_->pack.get(),
                    impl_->packName.c_str(),
                    &impl_->catalog)) {
            impl_->catalog.Clear();
            return nullptr;
        }
        impl_->catalogReady = true;
    }
    return &impl_->catalog;
}

CGameCtnBlockInfo *InstalledPackAssetRepository::BlockInfo(
        BlockInfoAssetHandle asset) {
    return Catalog() != nullptr ? impl_->blockInfos->Load(asset) : nullptr;
}

CGameCtnBlockInfo *InstalledPackAssetRepository::ArchiveBlockInfo(
        BlockInfoAssetHandle asset) {
    return Catalog() != nullptr
            ? impl_->blockInfos->Load(asset, true)
            : nullptr;
}

CSceneMobil *InstalledPackAssetRepository::Mobil(
        BlockInfoAssetHandle asset,
        bool ground,
        u32 variant) {
    return Catalog() != nullptr
            ? impl_->blockInfos->Mobil(asset, ground, variant)
            : nullptr;
}

std::optional<std::string>
InstalledPackAssetRepository::FirstGroundSurface(
        BlockInfoAssetHandle asset) {
    return Catalog() != nullptr
            ? impl_->blockInfos->FirstGroundSurface(asset)
            : std::nullopt;
}

std::optional<CatalogCollectionDefinition>
InstalledPackAssetRepository::Collection(std::string_view name) {
    if (name.empty() || Catalog() == nullptr) {
        return std::nullopt;
    }
    for (const auto &collection : impl_->collections) {
        if (collection->name == name) {
            return collection->definition;
        }
    }

    try {
        auto collection = std::make_unique<LoadedCollection>();
        collection->name.assign(name.data(), name.size());
        if (!collection->zoneSources.LoadFromCatalogCollection(
                    impl_->pack.get(), collection->name.c_str()) ||
            !collection->replacementPairs.LoadCatalogCollection(
                    impl_->pack.get(), collection->name.c_str()) ||
            collection->replacementPairs.OverflowCount() != 0u) {
            return std::nullopt;
        }
        collection->definition.name = collection->name;
        collection->definition.defaultBaseIdentifier =
                collection->zoneSources.DefaultBaseIdentifier();
        collection->definition.defaultPylonIdentifier =
                collection->zoneSources.DefaultPylonIdentifier();
        collection->definition.defaultZoneKind = ToSemanticZoneKind(
                collection->zoneSources.DefaultZoneKind());
        collection->definition.squareSize =
                collection->zoneSources.SquareSize();
        collection->definition.squareHeight = ResolveCollectionSquareHeight(
                collection->name,
                collection->definition.squareSize,
                collection->zoneSources.SquareHeight());
        for (const CollectionReplacementPair &pair :
             collection->replacementPairs.Pairs()) {
            collection->definition.surfaceReplacements.push_back(
                    {pair.sourceSurfaceIdName,
                     pair.targetSurfaceIdName});
        }
        if (!IsValidCollectionGridDimension(
                    collection->definition.squareSize) ||
            !IsValidCollectionGridDimension(
                    collection->definition.squareHeight)) {
            return std::nullopt;
        }
        if (collection->zoneSources.HasWaterDefinition()) {
            CatalogCollectionWaterDefinition water;
            if (!collection->zoneSources.BuildWaterDefinition(
                        impl_->pack.get(),
                        collection->name.c_str(),
                        &water)) {
                return std::nullopt;
            }
            collection->definition.water = std::move(water);
        }
        CatalogCollectionDefinition result = collection->definition;
        impl_->collections.push_back(std::move(collection));
        return result;
    } catch (const std::bad_alloc &) {
        return std::nullopt;
    }
}

std::optional<CatalogDecorationSizeDefinition>
InstalledPackAssetRepository::DecorationSize(
        const CGameCtnReplayMapInput &mapInput) {
    if (!EnsurePack()) {
        return std::nullopt;
    }
    for (const LoadedDecorationSize &loaded : impl_->decorationSizes) {
        if (loaded.Matches(mapInput)) {
            return loaded.definition;
        }
    }
    const auto decoded = ResolveReplayDecorationSize(*impl_->pack, mapInput);
    if (!decoded) {
        return std::nullopt;
    }
    try {
        LoadedDecorationSize loaded;
        loaded.mood = mapInput.DecorationMood().Name();
        loaded.collection = mapInput.DecorationCollection().Name();
        loaded.author = mapInput.DecorationAuthor().Name();
        loaded.mapSize = mapInput.Size();
        loaded.definition = *decoded;
        impl_->decorationSizes.push_back(std::move(loaded));
        return decoded;
    } catch (const std::bad_alloc &) {
        return std::nullopt;
    }
}

bool InstalledPackAssetRepository::HasSurfaceReplacement(
        std::string_view collection,
        std::string_view sourceId,
        std::string_view targetId) {
    if (!Collection(collection) || sourceId.empty() || targetId.empty()) {
        return false;
    }
    for (const auto &loaded : impl_->collections) {
        if (loaded->name != collection) {
            continue;
        }
        const std::string source(sourceId);
        const std::string target(targetId);
        return loaded->replacementPairs.Contains(source.c_str(),
                                                  target.c_str()) != 0;
    }
    return false;
}

std::optional<ResolvedMaterialDefinition>
InstalledPackAssetRepository::ResolveMaterial(std::string_view identifier) {
    return EnsurePack() && impl_->materials != nullptr
            ? impl_->materials->Resolve(identifier)
            : std::nullopt;
}

std::optional<ResolvedMaterialDefinition>
InstalledPackAssetRepository::ResolveMaterialPath(std::string_view plainPath) {
    return EnsurePack() && impl_->materials != nullptr
            ? impl_->materials->ResolvePath(plainPath)
            : std::nullopt;
}

const CPlugFilePack *InstalledPackAssetRepositoryAccess::Pack(
        InstalledPackAssetRepository &repository) {
    return repository.EnsurePack() ? repository.impl_->pack.get() : nullptr;
}

StaticSolidArchiveReferenceCatalog &
InstalledPackAssetRepositoryAccess::StaticSolidReferences(
        InstalledPackAssetRepository &repository) {
    return repository.impl_->solidReferences;
}
