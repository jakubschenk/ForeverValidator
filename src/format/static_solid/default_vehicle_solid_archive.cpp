#include "format/static_solid/default_vehicle_solid_archive.h"
#include <array>
#include <cstdint>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/rendering/plug_tree.h"
#include "engine/scene/plug_solid.h"
#include "format/pack/installed/plug_file_pack.h"
#include "format/pack/installed_vehicle_asset_graph.h"
#include "format/static_solid/static_solid_archive_assembler.h"
#include "format/static_solid/static_scene_archive_loader.h"
#include "format/static_solid/static_solid_descriptor_dependency_queue.h"

namespace {

struct DefaultVehicleWheelSurface {
    std::size_t wheelIndex;
    VehicleWheelSurfaceId surfaceId;
    VehicleCollisionRole collisionRole;
};

std::optional<DefaultVehicleWheelSurface> WheelSurfaceForTreeName(
        const char *name) {
    if (name == nullptr) {
        return std::nullopt;
    }

    struct NamedWheelRole {
        std::string_view name;
        VehicleCollisionRole role;
    };
    static constexpr std::array<NamedWheelRole,
                                OfficialVehicleWheelCount> SurfaceRoles{
            NamedWheelRole{"FLSurf", VehicleCollisionRole::FrontLeftWheel},
            NamedWheelRole{"FRSurf", VehicleCollisionRole::FrontRightWheel},
            NamedWheelRole{"RRSurf", VehicleCollisionRole::RearRightWheel},
            NamedWheelRole{"RLSurf", VehicleCollisionRole::RearLeftWheel},
    };
    for (std::size_t wheelIndex = 0u;
         wheelIndex < SurfaceRoles.size();
         ++wheelIndex) {
        if (name == SurfaceRoles[wheelIndex].name) {
            return DefaultVehicleWheelSurface{
                    wheelIndex,
                    VehicleWheelSurfaceIdForIndex(wheelIndex),
                    SurfaceRoles[wheelIndex].role,
            };
        }
    }
    return std::nullopt;
}

std::optional<VehicleCollisionRole> CollisionRoleForTreeName(
        const char *name) {
    if (name == nullptr) {
        return std::nullopt;
    }
    const std::optional<DefaultVehicleWheelSurface> wheel =
            WheelSurfaceForTreeName(name);
    return wheel.has_value()
            ? std::optional<VehicleCollisionRole>(wheel->collisionRole)
            : std::nullopt;
}

bool IsBodyCollisionTreeName(const char *name) {
    if (name == nullptr) {
        return false;
    }
    const std::string_view value(name);
    return value.rfind("BodySurf", 0u) == 0u ||
           value.rfind("Sphere", 0u) == 0u;
}

bool IsVehicleCollisionTreeName(const char *name) {
    return CollisionRoleForTreeName(name).has_value() ||
           IsBodyCollisionTreeName(name);
}

std::optional<std::size_t> SelectedParentIndex(
        const CPlugTree &tree,
        const std::vector<CPlugTree *> &selectedTrees) {
    for (CPlugTree *parent = tree.ParentTree();
         parent != nullptr;
         parent = parent->ParentTree()) {
        for (std::size_t index = 0u; index < selectedTrees.size(); index++) {
            if (selectedTrees[index] == parent) {
                return index;
            }
        }
    }
    return std::nullopt;
}

bool AddTreeDefinition(
        const CGameCtnReplayStaticSolidArchiveNamedTree &namedTree,
        CPlugTree *collisionRoot,
        const std::vector<CPlugTree *> &selectedTrees,
        ReplayVehicleSolidDefinition &definitions) {
    const std::optional<DefaultVehicleWheelSurface> wheelSurface =
            WheelSurfaceForTreeName(namedTree.Name());
    const std::optional<VehicleCollisionRole> collisionRole =
            CollisionRoleForTreeName(namedTree.Name());
    const bool isBody = IsBodyCollisionTreeName(namedTree.Name());
    if (!collisionRole.has_value() && !isBody) {
        return true;
    }
    if (!namedTree.HasSurfaceGeometry()) {
        return false;
    }

    const CGameCtnReplayStaticSolidArchiveSurfaceGeometryDefinition *geom =
            namedTree.SurfaceGeom();
    if (collisionRoot == nullptr || namedTree.Name() == nullptr ||
        geom == nullptr) {
        return false;
    }

    const std::uint16_t materialId = geom->MaterialId();
    if (materialId >= EPlugSurfaceMaterialId_Count) {
        return false;
    }

    const CMwId id = CMwId::CreateFromLocalName(namedTree.Name());
    CPlugTree *assembled = collisionRoot->GetPlugFromId(id);
    if (assembled == nullptr) {
        return false;
    }
    const std::optional<std::size_t> parentShapeIndex =
            SelectedParentIndex(*assembled, selectedTrees);
    CPlugTree *shapeParent = parentShapeIndex.has_value()
            ? selectedTrees[*parentShapeIndex]
            : nullptr;
    GmIso4 collisionPose;
    assembled->GetThisToRootTransfo(collisionPose, 1, shapeParent);
    GmIso4 rootPose;
    assembled->GetThisToRootTransfo(rootPose, 1, nullptr);

    VehicleCollisionShapeDefinition shape{
            geom->SurfType(),
            collisionPose,
            geom->BoundingBox(),
            GmLocalMaterialIndex::FromIndex(materialId),
            static_cast<EPlugSurfaceMaterialId>(materialId),
    };
    if ((isBody && !definitions.collisionModel.AddBodyShape(
                           shape, parentShapeIndex)) ||
        (collisionRole.has_value() &&
         !definitions.collisionModel.SetWheelShape(
                 *collisionRole,
                 std::move(shape),
                 parentShapeIndex))) {
        return false;
    }

    if (!wheelSurface.has_value()) {
        return true;
    }
    if (wheelSurface->wheelIndex >= definitions.wheels.size() ||
        definitions.wheels[wheelSurface->wheelIndex].has_value()) {
        return false;
    }

    VehicleWheelDefinition wheel;
    wheel.surfaceId = wheelSurface->surfaceId;
    wheel.rollingRadius = geom->BoundingBox().halfExtents.y;
    wheel.restSurfacePose = rootPose;
    wheel.forceApplicationPoint = wheel.restSurfacePose.Translation();
    wheel.initialSurfacePoint = wheel.forceApplicationPoint;
    wheel.collisionRole = wheelSurface->collisionRole;
    definitions.wheels[wheelSurface->wheelIndex] = std::move(wheel);
    return true;
}

}  // namespace

bool default_vehicle_solid_archive_detail::ExtractWheelDefinitions(
        const CGameCtnReplayStaticSolidArchiveGraph &graph,
        StaticSolidArchiveId payload,
        CPlugTree *collisionRoot,
        ReplayVehicleSolidDefinition &definitions) {
    std::vector<CPlugTree *> selectedTrees;
    try {
        if (!graph.ForEachNamedTree(
                    payload,
                    [&](const CGameCtnReplayStaticSolidArchiveNamedTree
                                &namedTree) {
                        if (!IsVehicleCollisionTreeName(namedTree.Name())) {
                            return true;
                        }
                        const CMwId id =
                                CMwId::CreateFromLocalName(namedTree.Name());
                        CPlugTree *tree = collisionRoot != nullptr
                                ? collisionRoot->GetPlugFromId(id)
                                : nullptr;
                        if (tree == nullptr) {
                            return false;
                        }
                        selectedTrees.push_back(tree);
                        return true;
                    })) {
            return false;
        }
    } catch (const std::bad_alloc &) {
        return false;
    }
    if (!graph.ForEachNamedTree(
                payload,
                [&](const CGameCtnReplayStaticSolidArchiveNamedTree
                            &namedTree) {
                    return AddTreeDefinition(
                            namedTree,
                            collisionRoot,
                            selectedTrees,
                            definitions);
                })) {
        return false;
    }
    return definitions.IsComplete();
}

namespace {

void RootDecodedTree(CPlugTree *tree) {
    if (tree == nullptr) {
        return;
    }
    tree->SetIsRooted(1);
    for (u32 index = 0u; index < tree->GetChildCount(); ++index) {
        RootDecodedTree(tree->GetChild(index));
    }
}

StaticSolidPrototype BuildVisualPrototype(
        CPlugTree *sourceRoot,
        const CGameCtnReplayStaticSolidArchiveSolidPhysicsDefinition
                *physical) {
    if (sourceRoot == nullptr) {
        return {};
    }
    RootDecodedTree(sourceRoot);
    std::unique_ptr<CPlugTree> root(
            sourceRoot->InternalCreateSolidModelInstance());
    if (!root) {
        return {};
    }
    CMwNodRef<CPlugSolid> solid = MakeMwNod<CPlugSolid>();
    if (physical != nullptr) {
        physical->ApplyToSolid(solid.Get());
    }
    solid->SetOwnedTree(std::move(root), 0);
    return StaticSolidPrototype(solid.Get());
}

bool TreeHasVisual(const CPlugTree *tree) {
    if (tree == nullptr) {
        return false;
    }
    if (tree->Visual() != nullptr) {
        return true;
    }
    for (u32 childIndex = 0u; childIndex < tree->GetChildCount();
         ++childIndex) {
        if (TreeHasVisual(tree->GetChild(childIndex))) {
            return true;
        }
    }
    return false;
}

CPlugTree *FindVehicleVisualRoot(
        StaticSolidArchiveAssembler &assembler,
        const StaticSolidArchiveLoadSession &archive,
        StaticSolidArchiveId collisionPayload) {
    CPlugTree *collisionRoot = assembler.CollisionRoot(collisionPayload);
    if (TreeHasVisual(collisionRoot)) {
        return collisionRoot;
    }

    for (u32 payloadIndex = 0u; payloadIndex < archive.ArchiveCount();
         ++payloadIndex) {
        const StaticSolidArchiveId payload =
                StaticSolidArchiveId::FromIndex(payloadIndex);
        if (payload.Matches(collisionPayload)) {
            continue;
        }
        CPlugTree *candidate = assembler.CollisionRoot(payload);
        if (TreeHasVisual(candidate)) {
            return candidate;
        }
    }
    return nullptr;
}

std::optional<DefaultVehicleSolidAssets> LoadVehicleAssets(
        CPlugFilePack &pack,
        const InstalledVehicleAssetGraph &assets,
        MaterialAssetRepository *materialAssets,
        bool buildVisual,
        std::string *diagnostic) {
    const auto fail = [&](const char *message) {
        if (diagnostic != nullptr) {
            *diagnostic = message;
        }
        return std::optional<DefaultVehicleSolidAssets>{};
    };
    if (diagnostic != nullptr) {
        diagnostic->clear();
    }
    if (!assets.IsComplete()) {
        return fail("installed vehicle asset graph is incomplete");
    }
    const CPlugFileFidContainer_SFileDesc *file =
            pack.FindFileDescByPath(assets.solid.selectedPath.c_str());
    if (file == nullptr) {
        return fail("vehicle solid descriptor is absent from the pack");
    }

    StaticSolidArchiveLoadSession archive;
    if (!archive.InstallPackSource(pack)) {
        return fail("vehicle solid pack source could not be installed");
    }
    if (materialAssets != nullptr) {
        archive.InstallMaterialAssets(*materialAssets);
    }

    if (buildVisual) {
        StaticSolidArchiveCatalog inventory;
        CGameCtnReplayStaticSolidDescriptorDependencyQueue dependencyQueue;
        u32 missingDependencies = 0u;
        if (!inventory.LoadFromInstalledPack(&pack) ||
            inventory.Find(assets.solid.selectedPath.c_str()) == nullptr) {
            return fail(
                    "vehicle solid descriptor is absent from the "
                    "static-solid inventory");
        }
        if (!dependencyQueue.RequireDescriptor(
                    assets.solid.selectedPath.c_str()) ||
            !dependencyQueue.DecodeReachablePayloadGraph(
                    &inventory,
                    &archive,
                    nullptr,
                    &missingDependencies) ||
            missingDependencies != 0u ||
            !archive.HasDescriptor(assets.solid.selectedPath.c_str())) {
            return fail("vehicle solid descriptor graph decode failed");
        }
    } else {
        CGameCtnReplayStaticSolidDecodedPayload decodedPayload;
        CGameCtnReplayStaticSolidArchiveDecodeStats stats;
        if (!archive.DecodePackFilePayloadWithStreamFeedback(
                    pack,
                    *file,
                    0u,
                    1u,
                    assets.solid.logicalPath.c_str(),
                    assets.solid.selectedPath.c_str(),
                    &decodedPayload,
                    &stats) ||
            !decodedPayload.IsReady() ||
            decodedPayload.ByteCount() != file->uncompressedSize) {
            return fail("vehicle solid descriptor decode failed");
        }
    }

    StaticSolidArchiveAssembler assembler;
    if (!assembler.Assemble(archive.ArchiveGraph(), archive)) {
        return fail("vehicle solid graph assembly failed");
    }
    const StaticSolidArchiveId payload =
            archive.SelectPayloadForDescriptor(
                    assets.solid.selectedPath.c_str());
    if (!payload.IsValid()) {
        return fail("vehicle solid payload was not retained after decode");
    }
    CPlugTree *collisionRoot = assembler.CollisionRoot(payload);
    if (collisionRoot == nullptr) {
        return fail("vehicle solid has no assembled root tree");
    }

    DefaultVehicleSolidAssets result;
    if (!default_vehicle_solid_archive_detail::ExtractWheelDefinitions(
                archive.ArchiveGraph(),
                payload,
                collisionRoot,
                result.definition)) {
        return fail("vehicle solid wheel definition extraction failed");
    }
    if (buildVisual) {
        CPlugTree *visualRoot =
                FindVehicleVisualRoot(assembler, archive, payload);
        result.visualPrototype = BuildVisualPrototype(
                visualRoot, assembler.Physics(payload));
        if (!result.visualPrototype.IsValid() && diagnostic != nullptr) {
            *diagnostic = visualRoot == nullptr
                    ? "vehicle solid descriptor graph has no visual tree"
                    : "vehicle solid visual tree could not be cloned";
        }
    }
    return result;
}

}  // namespace

std::optional<ReplayVehicleSolidDefinition>
DefaultVehicleSolidArchive::LoadFromPack(
        CPlugFilePack &pack) {
    std::optional<InstalledVehicleAssetGraph> assets =
            InstalledVehicleAssetGraph::ResolveFromPack(pack);
    return assets.has_value()
            ? LoadFromPack(pack, *assets)
            : std::nullopt;
}

std::optional<ReplayVehicleSolidDefinition>
DefaultVehicleSolidArchive::LoadFromPack(
        CPlugFilePack &pack,
        const InstalledVehicleAssetGraph &assets) {
    std::optional<DefaultVehicleSolidAssets> loaded =
            LoadVehicleAssets(pack, assets, nullptr, false, nullptr);
    return loaded.has_value()
            ? std::optional<ReplayVehicleSolidDefinition>(
                      std::move(loaded->definition))
            : std::nullopt;
}

std::optional<DefaultVehicleSolidAssets>
DefaultVehicleSolidArchive::LoadAssetsFromPack(
        CPlugFilePack &pack,
        const InstalledVehicleAssetGraph &assets,
        MaterialAssetRepository &materialAssets,
        std::string *diagnostic) {
    return LoadVehicleAssets(
            pack, assets, &materialAssets, true, diagnostic);
}
