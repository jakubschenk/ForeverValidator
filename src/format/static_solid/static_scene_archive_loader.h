#pragma once

#include "engine/core/engine_types.h"
#include <array>
#include <optional>
#include <vector>

#include "format/pack/installed/plug_file_pack.h"
#include "format/replay/replay_static_descriptor_limits.h"
#include "format/static_solid/static_scene_archive_models.h"
#include "engine/scene/static_scene_model.h"
#include "format/static_solid/static_solid_archive_definitions.h"
#include "format/static_solid/static_solid_archive_graph.h"
#include "format/static_solid/static_solid_archive_payload_decode.h"
#include "format/static_solid/static_solid_archive_id.h"
struct CPlugFilePack;
struct CPlugFileFidContainer_SFileDesc;
class InstalledPackAssetRepository;
struct CGameCtnReplayMapInput;
struct CGameCtnReplayStaticSolidDescriptorDependency;
struct CGameCtnReplayStaticSolidDescriptorDependencyQueue;

enum class ReplayStaticArchiveRole {
    Complete,
    Map,
    Decoration,
};
class ReplaySceneBlockPlacements;
class StaticSolidArchiveLoadSession;
class MaterialAssetRepository;
class StaticSolidDecodedPayloads;
class ReplaySimulationSession;
class StaticSolidArchiveReferenceCatalog;
class StaticSolidArchiveSource {
public:
    int Set(const char *plainPath, const char *selectedPath);
    int SetSamePath(const char *path);
    int HasSelectedDescriptor() const;
    int MatchesSelectedDescriptor(const char *path) const;
    const char *PlainPackPath() const;
    const char *SelectedDescriptorPath() const;

private:
    char plainPackPath[CGameCtnReplayStaticSolidDescriptorPathCapacity]{};
    char selectedDescriptorPath[
            CGameCtnReplayStaticSolidDescriptorPathCapacity]{};
};

struct StaticSolidArchiveCatalogEntry {
    u32 descriptorIndex;
    u32 loaderArgument;
    u32 descriptorClassId;
    u32 descriptorFlagsLow;
    u32 payloadOffsetRel;
    u32 payloadCompressedSize;
    u32 payloadUncompressedSize;
    u32 payloadIsCompressed;
    u32 payloadIsEncrypted;
    StaticSolidArchiveSource descriptorRef;

    static int AcceptsPackFileDescriptor(
            const CPlugFileFidContainer_SFileDesc &file,
            const char *plainPath);
    int BuildFromPackFileDescriptor(
            u32 fileIndex,
            const CPlugFileFidContainer_SFileDesc &file,
            const char *plainPath);
    int HasPackPayload() const;
    int IsCompressed() const;
    int IsEncrypted() const;
    u32 PackedByteCount() const;
    u32 CompressedByteCount() const;
    u32 UncompressedByteCount() const;
    u32 PackPayloadOffset() const;
    int IsDecoratorSolid() const;
};

struct StaticSolidArchiveCatalog {
private:
    std::vector<StaticSolidArchiveCatalogEntry> assets;
    const StaticSolidArchiveCatalogEntry *AssetAt(
            u32 index) const {
        return index < assets.size() ? &assets[index] : nullptr;
    }
    int AddDescriptorAsset(
            const StaticSolidArchiveCatalogEntry &asset);

public:
    u32 Count() const;
    const StaticSolidArchiveCatalogEntry *Data() const;
    int CopyFrom(const StaticSolidArchiveCatalog &source);
    int LoadFromInstalledPack(const CPlugFilePack *pack);
    const StaticSolidArchiveCatalogEntry *Find(
            const char *selectedDescriptorPath) const;
    int RequestDecoratorSolidDependencies(
            CGameCtnReplayStaticSolidDescriptorDependencyQueue &dependencyQueue) const;
    void Free();
};

struct StaticSolidArchivePayload {
    u32 descriptorIndex;
    u32 loaderArgument;
    u32 descriptorClassId;
    u32 payloadOffsetRel;
    u32 payloadOffsetAbs;
    u32 sourceByteCount;
    u32 decodedStatus;
    u32 payloadCompressedSize;
    u32 payloadUncompressedSize;
    u32 payloadIsCompressed;
    u32 payloadIsEncrypted;
    StaticSolidArchiveSource descriptorRef;
    std::vector<unsigned char> rawBytes;
    std::vector<unsigned char> decodedBytes;

    int BuildFromInventory(
            const StaticSolidArchiveCatalogEntry &descriptorAsset,
            u32 packDataStart);
    struct EncryptedPackFileRequest {
        const CPlugFileFidContainer_SFileDesc &file;
        u32 packDataStart = 0u;
        u32 descriptorIndex = 0u;
        u32 loaderArgument = 0u;
        const char *plainPath = "";
        const char *selectedDescriptorPath = "";
    };

    int BuildEncryptedPackFilePayload(
            const EncryptedPackFileRequest &request);
    int HasSelectedDescriptor() const;
    int MatchesSelectedDescriptor(const char *selectedDescriptorPath) const;
    const char *PlainPackPath() const;
    const char *SelectedDescriptorPath() const;
    u32 DescriptorClassId() const;
    int IsEncrypted() const;
    int IsCompressed() const;
    int IsDecoded() const;
    u32 RawByteCount() const;
    u32 PackedByteCount() const;
    u32 CompressedByteCount() const;
    u32 UncompressedByteCount() const;
    u32 PackPayloadOffset() const;
    void MarkDecodeUnavailable();
};

struct StaticSolidArchivePackSource {
    void Clear();
    int Install(const CPlugFilePack &pack);
    const SNat128 *Key() const;
    int BuildPayloadAsset(
            const StaticSolidArchiveCatalogEntry &descriptorAsset,
            StaticSolidArchivePayload *payloadAsset) const;
    const CPlugFilePack *ExternalPack() const;

private:
    const CPlugFilePack *pack = nullptr;
};

struct StaticSolidArchiveRollback {
private:
    CGameCtnReplayStaticSolidArchiveGraphRollbackMark archiveGraphMark;
    std::optional<u32> archiveModelCount;

    friend class StaticSolidArchiveLoadSession;
};

class StaticSolidArchiveRawBytes {
public:
    static StaticSolidArchiveRawBytes Missing();
    static StaticSolidArchiveRawBytes FromBytes(
            const unsigned char *bytes,
            u32 byteCount);

    int IsAvailable() const;
    const unsigned char *Bytes() const;
    u32 ByteCount() const;

private:
    const unsigned char *bytes = nullptr;
    u32 byteCount = 0u;
};

class StaticSolidArchiveDecodedBytes {
public:
    static StaticSolidArchiveDecodedBytes Missing();
    static StaticSolidArchiveDecodedBytes Empty();
    static StaticSolidArchiveDecodedBytes FromBytes(
            const unsigned char *bytes,
            u32 byteCount);

    int IsAvailable() const;
    const unsigned char *Bytes() const;
    u32 ByteCount() const;

private:
    const unsigned char *bytes = nullptr;
    u32 byteCount = 0u;
    bool available = false;
};

struct CGameCtnReplayStaticSolidDescriptorDependencyQueue;

class StaticSolidArchiveLoadSession {
    friend class StaticSolidDecodedPayloads;

    std::vector<StaticSolidArchivePayload> payloads;
    StaticSolidArchivePackSource payloadPackSource;
    CGameCtnReplayStaticSolidArchiveGraph archiveGraph;
    MaterialAssetRepository *materialAssets = nullptr;
    StaticSolidArchiveReferenceCatalog *solidReferences = nullptr;
    int LoadRawPayloadBytesFromPack(
            StaticSolidArchivePayload *payloadAsset);
    StaticSolidArchiveRawBytes RawPayloadBytesForPayload(
            const StaticSolidArchivePayload &payloadAsset) const;
    int AppendDecodedPayload(
            const unsigned char *bytes,
            u32 byteCount,
            StaticSolidArchivePayload *payloadAsset);
    int BuildPayloadAssetFromDescriptor(
            const StaticSolidArchiveCatalogEntry
                    &descriptorAsset,
            StaticSolidArchivePayload *payloadAsset) const {
        return payloadPackSource.BuildPayloadAsset(descriptorAsset, payloadAsset);
    }
    int AddPayloadAsset(const StaticSolidArchivePayload &payloadAsset);
    int DecodeAndAppendArchive(
            StaticSolidArchivePayload payloadAsset,
            CGameCtnReplayArchiveStaticModelCollection *archiveModels,
            CGameCtnReplayStaticSolidDescriptorDependencyQueue *dependencyQueue,
            int required);
    const StaticSolidArchivePayload *PayloadFor(
            StaticSolidArchiveId payload) const;
    StaticSolidArchiveDecodedBytes DecodedPayloadBytes(
            StaticSolidArchiveId payload,
            u32 decodedByteOffset,
            u32 byteCount) const;
    u32 FindArchiveIndex(
            const char *selectedDescriptorPath) const;

public:
    void Free();
    int HasDescriptor(const char *selectedDescriptorPath) const;
    StaticSolidArchiveId SelectPayloadForDescriptor(
            const char *selectedDescriptorPath) const;
    int PayloadMatchesSelectedDescriptor(
            StaticSolidArchiveId payload,
            const char *selectedDescriptorPath) const;
    int HasPayloads() const;
    u32 ArchiveCount() const;
    template<typename Visitor>
    int ForEachPayload(Visitor visitor) const {
        for (u32 i = 0; i < payloads.size(); i++) {
            const StaticSolidArchivePayload *payloadAsset = &payloads[i];
            if (!visitor(StaticSolidArchiveId::
                                 FromIndex(i),
                         *payloadAsset)) {
                return 0;
            }
        }
        return 1;
    }
    int DecodeDescriptorDependency(
            const CGameCtnReplayStaticSolidDescriptorDependency &dependency,
            const StaticSolidArchiveCatalog *inventory,
            CGameCtnReplayArchiveStaticModelCollection *archiveModels,
            CGameCtnReplayStaticSolidDescriptorDependencyQueue *dependencyQueue,
            u32 *selectedMissingOut);
    int DecodePackFilePayloadWithStreamFeedback(
            const CPlugFilePack &pack,
            const CPlugFileFidContainer_SFileDesc &file,
            u32 descriptorIndex,
            u32 loaderArgument,
            const char *plainPath,
            const char *selectedDescriptorPath,
            CGameCtnReplayStaticSolidDecodedPayload *decodedOut,
            CGameCtnReplayStaticSolidArchiveDecodeStats *statsOut);
    int InstallPackSource(const CPlugFilePack &pack);
    void InstallMaterialAssets(MaterialAssetRepository &assets);
    CGameCtnReplayStaticSolidArchiveGraph &MutableArchiveGraph();
    const CGameCtnReplayStaticSolidArchiveGraph &ArchiveGraph() const;
    MaterialAssetRepository *MaterialAssets() const;
    StaticSolidArchiveReferenceCatalog *SolidReferences() const;
    struct ReplayArchiveRequest {
        InstalledPackAssetRepository &installedPackAssets;
        const CGameCtnReplayMapInput &mapInput;
        const ReplaySceneBlockPlacements &placements;
        CGameCtnReplayArchiveStaticModelCollection &archiveModels;
        ReplayStaticArchiveRole role = ReplayStaticArchiveRole::Complete;
    };

    struct ReplayArchiveResult {
        bool loaded = false;
        u32 selectedMissing = 0u;
    };

    ReplayArchiveResult LoadReplayArchives(
            const ReplayArchiveRequest &request);
    StaticSolidArchiveRollback
    MarkDecodedArchiveTail(
            const CGameCtnReplayArchiveStaticModelCollection *archiveModels =
                    nullptr) const;
    int RestoreDecodedArchiveTail(
            const StaticSolidArchiveRollback &mark,
            CGameCtnReplayArchiveStaticModelCollection *archiveModels = nullptr);
    const CPlugFilePack *ExternalPack() const;
};

struct StaticSceneArchiveLoad {
    CGameCtnReplayArchiveStaticModelCollection archiveModels;
    StaticSolidArchiveLoadSession archive;
    u32 selectedMissing = 0u;
    StaticSceneModelCollection sceneModels;

    ~StaticSceneArchiveLoad();
    void Free();
    int Build(
            InstalledPackAssetRepository &installedPackAssets,
            const CGameCtnReplayMapInput &mapInput,
            const ReplaySceneBlockPlacements &placements);
    int BuildMap(
            InstalledPackAssetRepository &installedPackAssets,
            const CGameCtnReplayMapInput &mapInput,
            const ReplaySceneBlockPlacements &placements);
    int BuildDecoration(
            InstalledPackAssetRepository &installedPackAssets,
            const CGameCtnReplayMapInput &mapInput);
};
