#pragma once

#include <cstddef>
#include <memory>

#include <forevervalidator/validation.h>

#include "engine/resources/catalog_asset_repository.h"
#include "engine/game/material_definition.h"
#include "engine/scene/static_scene_model.h"
struct CGameCtnReplayMapInput;
class ReplaySceneBlockPlacements;
class InstalledPackKeyCatalog;

// Runtime-facing access to the assets needed to construct one replay scene.
// Installed-pack decoding stays behind this interface in src/format.
class ReplayAssetRepository : public CatalogAssetRepository,
                              public MaterialAssetRepository {
public:
    ~ReplayAssetRepository() override = default;

    virtual bool BuildStaticScene(
            const CGameCtnReplayMapInput &mapInput,
            const ReplaySceneBlockPlacements &placements,
            StaticSceneModelCollection *out) = 0;

    virtual bool BuildStaticSceneWithDecorationAssets(
            ReplayAssetRepository &decorationAssets,
            const CGameCtnReplayMapInput &mapInput,
            const ReplaySceneBlockPlacements &placements,
            StaticSceneModelCollection *out) {
        return this == &decorationAssets &&
               BuildStaticScene(mapInput, placements, out);
    }
};

std::unique_ptr<ReplayAssetRepository> OpenReplayAssetRepository(
        const std::byte *pakBytes,
        std::size_t pakByteCount,
        const std::byte *packlistBytes,
        std::size_t packlistByteCount);

std::unique_ptr<ReplayAssetRepository> OpenReplayAssetRepository(
        const std::byte *pakBytes,
        std::size_t pakByteCount,
        const std::byte *packlistBytes,
        std::size_t packlistByteCount,
        const char *packName);

std::unique_ptr<ReplayAssetRepository> OpenReplayAssetRepository(
        const std::byte *pakBytes,
        std::size_t pakByteCount,
        const InstalledPackKeyCatalog &keyCatalog,
        const char *packName);

std::unique_ptr<ReplayAssetRepository> OpenReplayAssetRepository(
        const std::byte *pakBytes,
        std::size_t pakByteCount,
        const InstalledPackKeyCatalog &keyCatalog,
        const char *packName,
        forevervalidator::AssetProvider looseAssetProvider);
