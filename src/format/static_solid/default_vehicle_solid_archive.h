#pragma once

#include <optional>
#include <string>

#include "engine/game/replay_vehicle_solid_definition.h"
#include "engine/resources/static_solid_asset.h"
#include "format/static_solid/static_solid_archive_id.h"
struct CPlugTree;
struct CPlugFilePack;
struct InstalledVehicleAssetGraph;
class CGameCtnReplayStaticSolidArchiveGraph;
class MaterialAssetRepository;

namespace default_vehicle_solid_archive_detail {

bool ExtractWheelDefinitions(
        const CGameCtnReplayStaticSolidArchiveGraph &graph,
        StaticSolidArchiveId payload,
        CPlugTree *collisionRoot,
        ReplayVehicleSolidDefinition &definitions);

}  // namespace default_vehicle_solid_archive_detail

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
            MaterialAssetRepository &materialAssets,
            std::string *diagnostic = nullptr);
};
