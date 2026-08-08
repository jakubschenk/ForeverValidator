#include "format/static_solid/static_solid_material_definition_resolver.h"
#include <new>
#include <string>
#include <string_view>
#include <utility>

#include "engine/game/material_definition.h"
#include "format/static_solid/static_scene_archive_loader.h"
#include "format/static_solid/static_solid_archive_graph_writer.h"
#include "format/static_solid/static_solid_archive_node_graph.h"
#include "format/static_solid/static_solid_external_node_paths.h"
#include "format/archive/archive_class_ids.h"

namespace {

std::optional<ResolvedMaterialDefinition> ResolveRelativeToDescriptor(
        MaterialAssetRepository &assets,
        const StaticSolidArchiveLoadSession &store,
        StaticSolidArchiveId payload,
        const CGameCtnReplayStaticSolidExternalNodePathResult &path,
        const std::string &identifier,
        const char *knownDescriptorPath) {
    const char *descriptorPath = knownDescriptorPath;
    if (descriptorPath == nullptr || descriptorPath[0] == '\0') {
        store.ForEachPayload([&](StaticSolidArchiveId candidateId,
                                 const StaticSolidArchivePayload &candidate) {
            if (payload.Matches(candidateId)) {
                descriptorPath = candidate.PlainPackPath();
                return 0;
            }
            return 1;
        });
    }
    if (descriptorPath == nullptr || descriptorPath[0] == '\0') {
        return std::nullopt;
    }

    try {
        const std::string source(descriptorPath);
        constexpr std::string_view MediaMarker("\\Media\\");
        const std::size_t media = source.find(MediaMarker);
        if (media == std::string::npos) {
            return std::nullopt;
        }
        std::string relative = path.HasPlainPath()
                ? std::string(path.PlainPath())
                : std::string("Material\\") + identifier;
        if (relative.rfind("Material\\", 0u) != 0u) {
            return std::nullopt;
        }
        std::string contextual = source.substr(
                0u, media + MediaMarker.size());
        contextual += relative;
        return assets.ResolveMaterialPath(contextual);
    } catch (const std::bad_alloc &) {
        return std::nullopt;
    }
}

}  // namespace

void StaticSolidMaterialReference::InstallEmbedded(
        CGameCtnReplayStaticSolidArchiveNodeIdentity material) {
    material_ = material;
    source_ = Source::Embedded;
    identifier_.clear();
}

void StaticSolidMaterialReference::InstallExternal(
        CGameCtnReplayStaticSolidArchiveNodeIdentity material,
        std::string identifier) {
    material_ = material;
    source_ = Source::ExternalAsset;
    identifier_ = std::move(identifier);
}

void StaticSolidMaterialReference::InstallUnsupported(
        CGameCtnReplayStaticSolidArchiveNodeIdentity material) {
    material_ = material;
    source_ = Source::Unsupported;
    identifier_.clear();
}

CGameCtnReplayStaticSolidArchiveNodeIdentity
StaticSolidMaterialReference::Material() const {
    return material_;
}

bool StaticSolidMaterialReference::RequiresAssetResolution() const {
    return source_ == Source::ExternalAsset && !identifier_.empty();
}

const std::string &StaticSolidMaterialReference::Identifier() const {
    return identifier_;
}

bool StaticSolidMaterialAssetLinker::ResolveAndAppend(
        CGameCtnReplayStaticSolidArchiveNodeGraph *archiveNodeGraph,
        const SceneDescriptorFolderPaths *externalFolders,
        StaticSolidArchiveLoadSession *store,
        StaticSolidArchiveId payload,
        u32 materialNodeIndex) {
    return ResolveAndAppend(archiveNodeGraph,
                            externalFolders,
                            store,
                            payload,
                            materialNodeIndex,
                            nullptr);
}

bool StaticSolidMaterialAssetLinker::ResolveAndAppend(
        CGameCtnReplayStaticSolidArchiveNodeGraph *archiveNodeGraph,
        const SceneDescriptorFolderPaths *externalFolders,
        StaticSolidArchiveLoadSession *store,
        StaticSolidArchiveId payload,
        u32 materialNodeIndex,
        const char *sourceDescriptorPath) {
    return ResolveAndAppend(
            archiveNodeGraph,
            externalFolders,
            store,
            CGameCtnReplayStaticSolidArchiveNodeIdentity::
                    FromPayloadAndArchiveIndex(payload, materialNodeIndex),
            sourceDescriptorPath);
}

bool StaticSolidMaterialAssetLinker::ResolveAndAppend(
        CGameCtnReplayStaticSolidArchiveNodeGraph *archiveNodeGraph,
        const SceneDescriptorFolderPaths *externalFolders,
        StaticSolidArchiveLoadSession *store,
        CGameCtnReplayStaticSolidArchiveNodeIdentity material) {
    return ResolveAndAppend(archiveNodeGraph,
                            externalFolders,
                            store,
                            material,
                            nullptr);
}

bool StaticSolidMaterialAssetLinker::ResolveAndAppend(
        CGameCtnReplayStaticSolidArchiveNodeGraph *archiveNodeGraph,
        const SceneDescriptorFolderPaths *externalFolders,
        StaticSolidArchiveLoadSession *store,
        CGameCtnReplayStaticSolidArchiveNodeIdentity material,
        const char *sourceDescriptorPath) {
    if (store == nullptr || archiveNodeGraph == nullptr) {
        return true;
    }
    const ArchiveNodeReference materialNode =
            material.ArchiveNode();
    StaticSolidMaterialReference reference;
    if (!archiveNodeGraph->BuildMaterialRef(
                material.Payload(), materialNode, &reference) ||
        !reference.RequiresAssetResolution()) {
        return true;
    }
    MaterialAssetRepository *assets = store->MaterialAssets();
    if (assets == nullptr) {
        return true;
    }
    std::optional<ResolvedMaterialDefinition> resolved;
    CGameCtnReplayStaticSolidExternalNodePathResult path;
    const CGameCtnReplayStaticSolidExternalNodeRef externalRef =
            archiveNodeGraph->ExternalNodeRef(materialNode);
    if (CGameCtnReplayStaticSolidExternalNodePathResolver::BuildPlainPath(
                externalFolders,
                store->ExternalPack(),
                externalRef,
                0,
                &path)) {
        resolved = assets->ResolveMaterialPath(path.PlainPath());
    }
    if (!resolved) {
        resolved = ResolveRelativeToDescriptor(
                *assets, *store, material.Payload(), path,
                reference.Identifier(), sourceDescriptorPath);
    }
    if (!resolved) {
        resolved = assets->ResolveMaterial(reference.Identifier());
    }
    if (!resolved) {
        return false;
    }

    CGameCtnReplayStaticSolidArchiveMaterialDefinition definition;
    definition.InstallResolved(reference.Material(), *resolved);
    archiveNodeGraph->SetNodeClassId(materialNode,
                                     TMNF_CLASS_CPlugMaterial);
    CGameCtnReplayStaticSolidArchiveGraphWriter writer(
            &store->MutableArchiveGraph(), material.Payload());
    return writer.AppendNode(materialNode, TMNF_CLASS_CPlugMaterial) &&
           store->MutableArchiveGraph()
                   .SurfaceGraph()
                   .AddMaterialDefinition(definition);
}
