#pragma once

#include <optional>

#include "engine/game/replay_vehicle_solid_definition.h"
#include "engine/resources/static_solid_asset.h"
struct CPlugFilePack;
struct InstalledVehicleAssetGraph;
class MaterialAssetRepository;

struct DefaultVehicleSolidAssets {
    ReplayVehicleSolidDefinition definition;
    StaticSolidPrototype visualPrototype;
};

class DefaultVehicleSolidArchive {
public:
    static std::optional<ReplayVehicleSolidDefinition> LoadFromPack(
            CPlugFilePack &pack);
    static std::optional<ReplayVehicleSolidDefinition> LoadFromPack(
            CPlugFilePack &pack,
            const InstalledVehicleAssetGraph &assets);
    static std::optional<DefaultVehicleSolidAssets> LoadAssetsFromPack(
            CPlugFilePack &pack,
            const InstalledVehicleAssetGraph &assets,
            MaterialAssetRepository &materialAssets);
};
