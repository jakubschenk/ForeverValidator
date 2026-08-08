#include <forevervalidator/experimental/physics_sandbox.h>

#include "engine/game/game_ctn_block_info.h"
#include "engine/game/material_definition.h"
#include "engine/game/material_render_definition.h"
#include "engine/game/material_texture_asset_source.h"
#include "engine/rendering/plug_tree.h"
#include "engine/scene/plug_solid.h"
#include "engine/scene/static_scene_model.h"
#include "format/static_solid/static_solid_geometry_decoder.h"
#include "format/static_solid/static_scene_archive_loader.h"
#include "simulation/replay/replay_scene_surface_resolution.h"
#include "simulation/runtime/replay_simulation_session.h"
#include "simulation/runtime/physics_sandbox_texture_assets.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

bool Check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

class TestBlockInfoAssetRegistry : public BlockInfoAssetRegistry {
public:
    static BlockInfoAssetHandle Handle(u32 index) {
        return HandleForStorageIndex(index);
    }
};

class JunctionResolverRepository final : public CatalogAssetRepository {
public:
    JunctionResolverRepository(
            BlockInfoAssetHandle sourceAsset,
            CGameCtnBlockInfoClip &sourceClip)
            : sourceAsset_(sourceAsset), sourceClip_(&sourceClip) {}

    const BlockInfoCatalog *Catalog() override { return nullptr; }

    CGameCtnBlockInfo *BlockInfo(BlockInfoAssetHandle asset) override {
        return asset == sourceAsset_ ? sourceClip_.Get() : nullptr;
    }

    CSceneMobil *Mobil(
            BlockInfoAssetHandle,
            bool,
            u32) override {
        return nullptr;
    }

    std::optional<std::string> FirstGroundSurface(
            BlockInfoAssetHandle) override {
        return std::nullopt;
    }

    std::optional<CatalogCollectionDefinition> Collection(
            std::string_view) override {
        return std::nullopt;
    }

    std::optional<CatalogDecorationSizeDefinition> DecorationSize(
            const CGameCtnReplayMapInput &) override {
        return std::nullopt;
    }

    bool HasSurfaceReplacement(
            std::string_view,
            std::string_view,
            std::string_view) override {
        return false;
    }

private:
    BlockInfoAssetHandle sourceAsset_;
    CMwNodRef<CGameCtnBlockInfoClip> sourceClip_;
};

class TestTextureSource final : public MaterialTextureAssetSource {
public:
    TestTextureSource(std::string sourceName,
                      std::string expectedPath,
                      std::vector<std::byte> bytes,
                      bool fail = false)
            : sourceName_(std::move(sourceName)),
              expectedPath_(std::move(expectedPath)),
              bytes_(std::move(bytes)),
              fail_(fail) {}

    std::string_view StableNamespace(void) const noexcept override {
        return sourceName_;
    }

    MaterialTextureAssetSourceResult ReadEncodedBytes(
            std::string_view selectedPath) const noexcept override {
        ++readCount_;
        try {
            if (selectedPath != expectedPath_) {
                MaterialTextureAssetSourceError error;
                error.code =
                        MaterialTextureAssetSourceErrorCode::SourceNotFound;
                error.diagnostic = "test texture path was not found";
                return MaterialTextureAssetSourceResult::Failure(
                        std::move(error));
            }
            if (fail_) {
                MaterialTextureAssetSourceError error;
                error.code =
                        MaterialTextureAssetSourceErrorCode::ExtractionFailed;
                error.diagnostic =
                        "test texture extraction failed for " +
                        expectedPath_;
                return MaterialTextureAssetSourceResult::Failure(
                        std::move(error));
            }
            return MaterialTextureAssetSourceResult::Success(bytes_);
        } catch (const std::bad_alloc &) {
            MaterialTextureAssetSourceError error;
            error.code =
                    MaterialTextureAssetSourceErrorCode::AllocationFailed;
            return MaterialTextureAssetSourceResult::Failure(
                    std::move(error));
        }
    }

    std::uint32_t ReadCount(void) const noexcept { return readCount_; }

private:
    std::string sourceName_;
    std::string expectedPath_;
    std::vector<std::byte> bytes_;
    bool fail_ = false;
    mutable std::uint32_t readCount_ = 0u;
};

class EmptyMaterialRepository final : public MaterialAssetRepository {
public:
    std::optional<ResolvedMaterialDefinition> ResolveMaterial(
            std::string_view) override {
        return std::nullopt;
    }

    std::optional<ResolvedMaterialDefinition> ResolveMaterialPath(
            std::string_view) override {
        return std::nullopt;
    }
};

void AppendFloat(std::vector<std::uint8_t> *bytes, float value) {
    const std::size_t offset = bytes->size();
    bytes->resize(offset + sizeof(value));
    std::memcpy(bytes->data() + offset, &value, sizeof(value));
}

bool NearlyEqual(float lhs, float rhs) {
    return std::fabs(lhs - rhs) < 0.0001f;
}

bool TestUvDecoding() {
    std::vector<std::uint8_t> uv2Bytes;
    for (float value : {0.25f, 0.75f, -1.0f, 2.0f}) {
        AppendFloat(&uv2Bytes, value);
    }
    GxTexCoordSet uv2;
    bool okay = Check(
            DecodeStaticSolidTexCoordStream(
                    uv2Bytes.data(), 2u, 2u, 8u, &uv2),
            "2D UV stream was rejected");
    const GxTexCoord4 first = uv2.Coordinate4At(0u);
    const GxTexCoord4 second = uv2.Coordinate4At(1u);
    okay &= Check(
            uv2.Dimension() == GxTexCoordDimension::Two &&
                    uv2.Count() == 2u &&
                    NearlyEqual(first.u, 0.25f) &&
                    NearlyEqual(first.v, 0.75f) &&
                    NearlyEqual(second.u, -1.0f) &&
                    NearlyEqual(second.v, 2.0f),
            "2D UV values were not preserved");

    std::vector<std::uint8_t> uv3Bytes;
    for (float value : {-2.5f, 0.125f, 7.75f}) {
        AppendFloat(&uv3Bytes, value);
    }
    GxTexCoordSet uv3;
    okay &= Check(
            DecodeStaticSolidTexCoordStream(
                    uv3Bytes.data(), 1u, 3u, 12u, &uv3),
            "3D UV stream was rejected");
    const GxTexCoord4 threeDimensional = uv3.Coordinate4At(0u);
    okay &= Check(
            uv3.Dimension() == GxTexCoordDimension::Three &&
                    uv3.Count() == 1u &&
                    NearlyEqual(threeDimensional.u, -2.5f) &&
                    NearlyEqual(threeDimensional.v, 0.125f) &&
                    NearlyEqual(threeDimensional.w, 7.75f) &&
                    NearlyEqual(threeDimensional.q, 1.0f),
            "3D UV values were not preserved");

    std::vector<std::uint8_t> uv4Bytes;
    for (float value : {1.0f, 2.0f, 3.0f, 4.0f}) {
        AppendFloat(&uv4Bytes, value);
    }
    GxTexCoordSet uv4;
    okay &= Check(
            DecodeStaticSolidTexCoordStream(
                    uv4Bytes.data(), 1u, 4u, 16u, &uv4),
            "4D UV stream was rejected");
    const GxTexCoord4 expanded = uv4.Coordinate4At(0u);
    okay &= Check(
            uv4.Dimension() == GxTexCoordDimension::Four &&
                    NearlyEqual(expanded.u, 1.0f) &&
                    NearlyEqual(expanded.v, 2.0f) &&
                    NearlyEqual(expanded.w, 3.0f) &&
                    NearlyEqual(expanded.q, 4.0f),
            "4D UV values were not preserved");
    okay &= Check(
            !DecodeStaticSolidTexCoordStream(
                    uv2Bytes.data(), 2u, 2u, 12u, &uv2),
            "invalid UV stride was accepted");
    return okay;
}

bool TestTransformComposition() {
    GmIso4 parent;
    parent.SetIdentity();
    parent.SetTranslation({10.0f, 20.0f, 30.0f});
    GmIso4 local;
    local.SetIdentity();
    local.SetTranslation({1.0f, 2.0f, 3.0f});

    CPlugTree tree;
    tree.SetUseLocation(1);
    tree.SetLocation(local);
    GmIso4 world;
    tree.ComposeCollisionIso(parent, world);
    return Check(
            NearlyEqual(world.translation.x, 11.0f) &&
                    NearlyEqual(world.translation.y, 22.0f) &&
                    NearlyEqual(world.translation.z, 33.0f),
            "tree local transform was not composed with its parent");
}

bool TestProvenanceAndImmutableScene() {
    GmIso4 identity;
    identity.SetIdentity();
    StaticSceneModel model(
            StaticSolidPrototype{},
            identity,
            StaticScenePurpose::SubMobil);
    StaticSceneProvenance provenance;
    provenance.blockName = "StadiumRoadMain";
    provenance.collection = "Stadium";
    provenance.descriptorPath = "GameData/StadiumRoad.Block.Gbx";
    provenance.sceneObjectId = "mobil-3";
    provenance.placementIdentity = 42u;
    provenance.blockInstanceId = 7u;
    provenance.variant = 2u;
    provenance.componentIndex = 3u;
    provenance.authored = true;
    model.SetProvenance(provenance);

    StaticSceneModelCollection models;
    bool okay = Check(models.Add(std::move(model)),
                      "scene model could not be stored");
    const StaticSceneModel &stored = models.Models().front();
    okay &= Check(
            stored.Purpose() == StaticScenePurpose::SubMobil &&
                    stored.Provenance().blockName == "StadiumRoadMain" &&
                    stored.Provenance().placementIdentity == 42u &&
                    stored.Provenance().blockInstanceId == 7u &&
                    stored.Provenance().variant == 2u &&
                    stored.Provenance().componentIndex == 3u &&
                    stored.Provenance().authored,
            "authored provenance changed after scene-model storage");

    using Scene =
            forevervalidator::experimental::PhysicsSandboxRenderScene;
    using Handle =
            forevervalidator::experimental::
                    PhysicsSandboxRenderSceneHandle;
    static_assert(std::is_same_v<Handle, std::shared_ptr<const Scene>>);
    const Handle scene = std::make_shared<const Scene>();
    const Handle clonedVehicleScene = scene;
    okay &= Check(scene->meshes.empty() && scene->instances.empty(),
                  "immutable render-scene handle was not readable");
    okay &= Check(
            clonedVehicleScene.get() == scene.get() &&
                    clonedVehicleScene.use_count() == scene.use_count(),
            "cloned sandbox presentation state did not share its immutable "
            "vehicle scene");
    return okay;
}

bool TestReusableLocalRenderSceneBuilder() {
    std::vector<GxVertex> vertices(3u);
    vertices[0].position = {-1.0f, 0.0f, 0.0f};
    vertices[1].position = {1.0f, 0.0f, 0.0f};
    vertices[2].position = {0.0f, 1.0f, 0.0f};
    for (GxVertex &vertex : vertices) {
        vertex.normal = {0.0f, 0.0f, 1.0f};
    }
    CMwNodRef<CPlugVisualIndexedTriangles> visual =
            MakeMwNod<CPlugVisualIndexedTriangles>();
    visual->SetOwnedGeometry(
            std::move(vertices), {0u, 1u, 2u});
    visual->SetBoundingMinMax(
            {-1.0f, 0.0f, 0.0f},
            {1.0f, 1.0f, 0.0f});

    auto root = std::make_unique<CPlugTree>();
    root->SetIsRooted(1);
    GmIso4 local;
    local.SetIdentity();
    local.SetTranslation({1.0f, 2.0f, 3.0f});
    root->SetUseLocation(1);
    root->SetLocation(local);
    root->SetVisual(visual.Get(), nullptr, nullptr, 0);

    CMwNodRef<CPlugSolid> solid = MakeMwNod<CPlugSolid>();
    solid->SetOwnedTree(std::move(root), 0);
    GmIso4 identity;
    identity.SetIdentity();
    StaticSceneModel model(
            StaticSolidPrototype(solid.Get()),
            identity,
            StaticScenePurpose::Generated);
    StaticSceneModelCollection models;
    bool okay = Check(
            models.Add(std::move(model)),
            "local visual model could not be stored");
    const auto scene = BuildPhysicsSandboxRenderScene(models);
    okay &= Check(
            scene && scene->meshes.size() == 1u &&
                    scene->instances.size() == 1u &&
                    scene->meshes[0].vertices.size() == 3u &&
                    scene->instances[0].meshIndex == 0u &&
                    NearlyEqual(
                            scene->instances[0].worldTransform.translation.x,
                            1.0f) &&
                    NearlyEqual(
                            scene->instances[0].worldTransform.translation.y,
                            2.0f) &&
                    NearlyEqual(
                            scene->instances[0].worldTransform.translation.z,
                            3.0f),
            "reusable render-scene builder did not preserve local geometry");

    forevervalidator::experimental::PhysicsSandboxCarState car;
    car.wheelGroundPosition[2] = {4.0f, 5.0f, 6.0f};
    okay &= Check(
            NearlyEqual(car.wheelGroundPosition[2].x, 4.0f) &&
                    NearlyEqual(car.wheelGroundPosition[2].y, 5.0f) &&
                    NearlyEqual(car.wheelGroundPosition[2].z, 6.0f),
            "public wheel-ground position state was not writable");

    EmptyMaterialRepository materialRepository;
    StaticSolidArchiveLoadSession archive;
    archive.InstallMaterialAssets(materialRepository);
    okay &= Check(
            archive.MaterialAssets() == &materialRepository,
            "vehicle archive did not retain its material repository");
    return okay;
}

bool TestLazyTextureAssetResolver() {
    using forevervalidator::experimental::
            PhysicsSandboxTextureAssetEncoding;
    using forevervalidator::experimental::
            PhysicsSandboxTextureAssetErrorCode;
    using forevervalidator::experimental::texture_assets_internal::
            PhysicsSandboxTextureAssetRegistry;

    constexpr const char *TexturePath =
            "Stadium\\Media\\Texture\\RoadDiffuse.DDS";
    const std::vector<std::byte> expected{
            std::byte{0x44}, std::byte{0x44},
            std::byte{0x53}, std::byte{0x20}};
    auto source = std::make_shared<TestTextureSource>(
            "Stadium", TexturePath, expected);
    std::weak_ptr<TestTextureSource> retainedSource = source;

    MaterialRenderBitmapDefinition bitmap;
    bitmap.imagePlainPath = TexturePath;
    bitmap.imageSelectedPath = TexturePath;
    bitmap.imageEncodedByteCount = expected.size();
    bitmap.imageSource = source;

    PhysicsSandboxTextureAssetRegistry registry;
    std::string diagnostic;
    const auto id = registry.Add(bitmap, &diagnostic);
    MaterialRenderBitmapDefinition duplicate = bitmap;
    duplicate.imagePlainPath =
            "stadium\\media\\texture\\roaddiffuse.dds";
    duplicate.imageSelectedPath =
            "stadium\\media\\texture\\roaddiffuse.dds";
    const auto duplicateId = registry.Add(duplicate, &diagnostic);

    MaterialRenderBitmapDefinition other = bitmap;
    other.imageSelectedPath =
            "Stadium\\Media\\Texture\\RoadNormal.dds";
    const auto otherId = registry.Add(other, &diagnostic);
    auto resolver = std::move(registry).Build();

    bitmap.imageSource.reset();
    duplicate.imageSource.reset();
    other.imageSource.reset();
    source.reset();

    bool okay = Check(
            id != 0u && duplicateId == id && otherId != id &&
                    resolver.Assets().size() == 2u && diagnostic.empty(),
            "texture assets were not deterministically deduplicated");
    okay &= Check(
            !retainedSource.expired(),
            "texture resolver did not retain its encoded-byte source");
    okay &= Check(
            resolver.Assets()[0].id == id &&
                    resolver.Assets()[0].logicalPath == TexturePath &&
                    resolver.Assets()[0].sourcePath == TexturePath &&
                    resolver.Assets()[0].sourceName == "Stadium" &&
                    resolver.Assets()[0].encoding ==
                            PhysicsSandboxTextureAssetEncoding::Dds &&
                    resolver.Assets()[0].mediaType == "image/vnd-ms.dds" &&
                    resolver.Assets()[0].encodedByteCount == expected.size(),
            "texture metadata did not preserve the decoded image reference");

    const auto firstRead = resolver.Read(id);
    const auto secondRead = resolver.Read(id);
    okay &= Check(
            firstRead && secondRead &&
                    firstRead.Value()->encodedBytes == expected &&
                    firstRead.Value().get() == secondRead.Value().get() &&
                    retainedSource.lock()->ReadCount() == 1u,
            "texture bytes were not loaded lazily and cached");

    const auto unknown = resolver.Read(UINT64_C(0xffffffffffffffff));
    okay &= Check(
            !unknown &&
                    unknown.Error().code ==
                            PhysicsSandboxTextureAssetErrorCode::UnknownAsset &&
                    !unknown.Error().diagnostic.empty(),
            "unknown texture ID did not return an actionable error");

    PhysicsSandboxTextureAssetRegistry stableRegistry;
    MaterialRenderBitmapDefinition stableBitmap;
    stableBitmap.imagePlainPath = TexturePath;
    stableBitmap.imageSelectedPath = TexturePath;
    stableBitmap.imageEncodedByteCount = expected.size();
    stableBitmap.imageSource = std::make_shared<TestTextureSource>(
            "Stadium", TexturePath, expected);
    okay &= Check(
            stableRegistry.Add(stableBitmap) == id,
            "texture asset ID changed across equivalent registries");

    auto failingSource = std::make_shared<TestTextureSource>(
            "Stadium", TexturePath, expected, true);
    PhysicsSandboxTextureAssetRegistry failingRegistry;
    stableBitmap.imageSource = failingSource;
    const auto failingId = failingRegistry.Add(stableBitmap);
    auto failingResolver = std::move(failingRegistry).Build();
    const auto failedRead = failingResolver.Read(failingId);
    const auto repeatedFailure = failingResolver.Read(failingId);
    okay &= Check(
            !failedRead && !repeatedFailure &&
                    failedRead.Error().code ==
                            PhysicsSandboxTextureAssetErrorCode::
                                    ExtractionFailed &&
                    failedRead.Error().sourcePath == TexturePath &&
                    !failedRead.Error().diagnostic.empty() &&
                    failingSource->ReadCount() == 1u,
            "texture extraction error was not actionable or cached");

    forevervalidator::experimental::PhysicsSandboxTextureAssetResolver empty;
    const auto invalid = empty.Read(id);
    okay &= Check(
            empty.Assets().empty() && !invalid &&
                    invalid.Error().code ==
                            PhysicsSandboxTextureAssetErrorCode::
                                    InvalidResolver,
            "empty texture resolver did not report its state");
    return okay;
}

bool TestGenericBackgroundLayerClassification() {
    using forevervalidator::experimental::PhysicsSandboxRenderInstance;
    using forevervalidator::experimental::PhysicsSandboxRenderLayer;
    using forevervalidator::experimental::PhysicsSandboxRenderMesh;
    using forevervalidator::experimental::PhysicsSandboxRenderScene;
    using forevervalidator::experimental::PhysicsSandboxScenePurpose;

    const auto mesh = [](forevervalidator::Vector3 minimum,
                         forevervalidator::Vector3 maximum) {
        PhysicsSandboxRenderMesh result;
        result.boundsMin = minimum;
        result.boundsMax = maximum;
        return result;
    };
    PhysicsSandboxRenderScene scene;
    scene.meshes.push_back(mesh(
            {-10.0f, 0.0f, -10.0f}, {10.0f, 10.0f, 10.0f}));
    scene.meshes.push_back(mesh(
            {-100.0f, -100.0f, -100.0f}, {100.0f, 0.0f, 100.0f}));
    scene.meshes.push_back(mesh(
            {-100.0f, 0.0f, -100.0f}, {100.0f, 100.0f, 100.0f}));
    scene.meshes.push_back(mesh(
            {-20.0f, -5.0f, -20.0f}, {20.0f, 20.0f, 20.0f}));

    PhysicsSandboxRenderInstance foreground;
    foreground.meshIndex = 0u;
    foreground.purpose = PhysicsSandboxScenePurpose::PlacedBlock;
    scene.instances.push_back(foreground);

    PhysicsSandboxRenderInstance lowerBackground;
    lowerBackground.meshIndex = 1u;
    lowerBackground.purpose = PhysicsSandboxScenePurpose::Environment;
    lowerBackground.provenance.descriptorPath = "Shared/Backdrop";
    scene.instances.push_back(lowerBackground);
    PhysicsSandboxRenderInstance upperBackground = lowerBackground;
    upperBackground.meshIndex = 2u;
    scene.instances.push_back(upperBackground);

    PhysicsSandboxRenderInstance nearbyEnvironment;
    nearbyEnvironment.meshIndex = 3u;
    nearbyEnvironment.purpose = PhysicsSandboxScenePurpose::Environment;
    nearbyEnvironment.provenance.descriptorPath = "Shared/Scenery";
    scene.instances.push_back(nearbyEnvironment);

    ClassifyPhysicsSandboxRenderLayers(scene);
    return Check(
            scene.instances[0].renderLayer ==
                            PhysicsSandboxRenderLayer::World &&
                    scene.instances[1].renderLayer ==
                            PhysicsSandboxRenderLayer::Background &&
                    scene.instances[2].renderLayer ==
                            PhysicsSandboxRenderLayer::Background &&
                    !scene.instances[1].castsShadows &&
                    !scene.instances[2].castsShadows &&
                    scene.instances[3].renderLayer ==
                            PhysicsSandboxRenderLayer::World,
            "generic enclosing backdrop was not separated from world geometry");
}

bool TestClipJunctionSourceResolution() {
    const BlockInfoAssetHandle sourceAsset =
            TestBlockInfoAssetRegistry::Handle(7u);
    CMwNodRef<CGameCtnBlockInfoClip> unresolved =
            MakeMwNod<CGameCtnBlockInfoClip>();
    CMwNodRef<CGameCtnBlockInfoClip> resolved =
            MakeMwNod<CGameCtnBlockInfoClip>();
    unresolved->SetSourceAsset(sourceAsset);
    resolved->SetSourceAsset(sourceAsset);
    resolved->ResetMobilVariants(CGameCtnBlockInfo::GroundMobilFamily, 1u);
    resolved->AddMobil(
            CGameCtnBlockInfo::GroundMobilFamily,
            0u,
            new CSceneMobil());

    CGameCtnBlockInfo owner;
    auto unit = std::make_unique<CGameCtnBlockUnitInfo>();
    unit->InitializeUnitFields({0u, 0u, 0u}, 0u, 0u, &owner);
    unit->SetJunction(ECardinalDir::West, unresolved.Get());
    CGameCtnBlockUnitInfo *installedUnit = unit.get();
    owner.AddBlockUnitInfo(true, std::move(unit));

    JunctionResolverRepository repository(sourceAsset, *resolved);
    ReplaySceneAssetResolver resolver;
    resolver.assets = &repository;
    resolver.ResolveJunctionSources(owner);

    CGameCtnBlockInfoClip *junction =
            installedUnit->JunctionAt(ECardinalDir::West);
    return Check(
            junction == resolved.Get() &&
                    junction->GetMobil(
                            CGameCtnBlockInfo::GroundMobilFamily,
                            0u,
                            0u) != nullptr,
            "deferred clip junction kept its empty placeholder");
}

}  // namespace

int main() {
    bool okay = TestUvDecoding();
    okay &= TestTransformComposition();
    okay &= TestProvenanceAndImmutableScene();
    okay &= TestReusableLocalRenderSceneBuilder();
    okay &= TestLazyTextureAssetResolver();
    okay &= TestGenericBackgroundLayerClassification();
    okay &= TestClipJunctionSourceResolution();
    return okay ? 0 : 1;
}
