#pragma once

#include <string>

#include "format/static_solid/static_solid_archive_id.h"
#include "format/static_solid/static_solid_archive_identity.h"
class CGameCtnReplayStaticSolidArchiveNodeGraph;
class StaticSolidArchiveLoadSession;
struct SceneDescriptorFolderPaths;

class StaticSolidMaterialReference {
public:
    enum class Source {
        Embedded,
        ExternalAsset,
        Unsupported,
    };

    void InstallEmbedded(
            CGameCtnReplayStaticSolidArchiveNodeIdentity material);
    void InstallExternal(
            CGameCtnReplayStaticSolidArchiveNodeIdentity material,
            std::string identifier);
    void InstallUnsupported(
            CGameCtnReplayStaticSolidArchiveNodeIdentity material);

    CGameCtnReplayStaticSolidArchiveNodeIdentity Material() const;
    bool RequiresAssetResolution() const;
    const std::string &Identifier() const;

private:
    CGameCtnReplayStaticSolidArchiveNodeIdentity material_ =
            CGameCtnReplayStaticSolidArchiveNodeIdentity::Invalid();
    Source source_ = Source::Unsupported;
    std::string identifier_;
};

class StaticSolidMaterialAssetLinker {
public:
    static bool ResolveAndAppend(
            CGameCtnReplayStaticSolidArchiveNodeGraph *archiveNodeGraph,
            const SceneDescriptorFolderPaths *externalFolders,
            StaticSolidArchiveLoadSession *store,
            StaticSolidArchiveId payload,
            u32 materialNodeIndex);
    static bool ResolveAndAppend(
            CGameCtnReplayStaticSolidArchiveNodeGraph *archiveNodeGraph,
            const SceneDescriptorFolderPaths *externalFolders,
            StaticSolidArchiveLoadSession *store,
            StaticSolidArchiveId payload,
            u32 materialNodeIndex,
            const char *sourceDescriptorPath);
    static bool ResolveAndAppend(
            CGameCtnReplayStaticSolidArchiveNodeGraph *archiveNodeGraph,
            const SceneDescriptorFolderPaths *externalFolders,
            StaticSolidArchiveLoadSession *store,
            CGameCtnReplayStaticSolidArchiveNodeIdentity material);
    static bool ResolveAndAppend(
            CGameCtnReplayStaticSolidArchiveNodeGraph *archiveNodeGraph,
            const SceneDescriptorFolderPaths *externalFolders,
            StaticSolidArchiveLoadSession *store,
            CGameCtnReplayStaticSolidArchiveNodeIdentity material,
            const char *sourceDescriptorPath);
};
