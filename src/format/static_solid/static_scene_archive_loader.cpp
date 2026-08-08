// Replay static-solid descriptor inventory and payload store.
//
// This module owns installed-pack descriptor records, packed payload bytes, and
// the decoded archive graph used to build typed scene assets.

#include "engine/game/game_ctn_decoration.h"
#include "engine/scene/replay_scene_placements.h"
#include <algorithm>
#include <stddef.h>
#include <stdlib.h>
#include <utility>

#include "format/pack/installed/plug_file_pack.h"
#include "format/static_solid/static_scene_archive_loader.h"
#include "format/pack/block_info_catalog/installed_pack_asset_repository.h"
#include "format/pack/block_info_catalog/installed_pack_asset_repository_internal.h"
#include "format/pack/installed/scene_descriptor_folder_paths.h"
#include "format/static_solid/static_solid_descriptor_dependency_queue.h"
#include "format/archive/tmnf_archive_ids.h"
#include "format/archive/fixed_c_string.h"
#include "format/static_solid/static_scene_asset_linker.h"
#include <new>
namespace {

int LoadArchiveCatalog(InstalledPackAssetRepository &source,
                       StaticSolidArchiveCatalog &catalog,
                       const CPlugFilePack **packOut = nullptr) {
    const CPlugFilePack *pack = nullptr;
    pack = InstalledPackAssetRepositoryAccess::Pack(source);
    if (pack == nullptr ||
        !catalog.LoadFromInstalledPack(pack)) {
        return 0;
    }
    if (packOut != nullptr) {
        *packOut = pack;
    }
    return 1;
}

int InstallArchiveSource(InstalledPackAssetRepository &source,
                         StaticSolidArchiveCatalog &catalog,
                         StaticSolidArchiveLoadSession &session) {
    const CPlugFilePack *pack = nullptr;
    if (!LoadArchiveCatalog(source, catalog, &pack) ||
        !session.InstallPackSource(*pack)) {
        catalog.Free();
        return 0;
    }
    return 1;
}

}  // namespace

StaticSolidArchiveRawBytes
StaticSolidArchiveRawBytes::Missing() {
    return StaticSolidArchiveRawBytes();
}

StaticSolidArchiveRawBytes
StaticSolidArchiveRawBytes::FromBytes(
        const unsigned char *newBytes,
        u32 newByteCount) {
    StaticSolidArchiveRawBytes span;
    if (newBytes == nullptr) {
        return span;
    }
    span.bytes = newBytes;
    span.byteCount = newByteCount;
    return span;
}

int StaticSolidArchiveRawBytes::IsAvailable() const {
    return bytes != nullptr;
}

const unsigned char *StaticSolidArchiveRawBytes::Bytes() const {
    return bytes;
}

u32 StaticSolidArchiveRawBytes::ByteCount() const {
    return byteCount;
}

StaticSolidArchiveDecodedBytes
StaticSolidArchiveDecodedBytes::Missing() {
    return StaticSolidArchiveDecodedBytes{};
}

StaticSolidArchiveDecodedBytes
StaticSolidArchiveDecodedBytes::Empty() {
    StaticSolidArchiveDecodedBytes span;
    span.available = true;
    return span;
}

StaticSolidArchiveDecodedBytes
StaticSolidArchiveDecodedBytes::FromBytes(
        const unsigned char *newBytes,
        u32 newByteCount) {
    if (newByteCount == 0u && newBytes == nullptr) {
        return Empty();
    }
    if (newBytes == nullptr) {
        return Missing();
    }
    StaticSolidArchiveDecodedBytes span;
    span.bytes = newBytes;
    span.byteCount = newByteCount;
    span.available = true;
    return span;
}

int StaticSolidArchiveDecodedBytes::IsAvailable() const {
    return available;
}

const unsigned char *
StaticSolidArchiveDecodedBytes::Bytes() const {
    return bytes;
}

u32 StaticSolidArchiveDecodedBytes::ByteCount() const {
    return byteCount;
}

void StaticSolidArchiveCatalog::Free() {
    assets.clear();
}

u32 StaticSolidArchiveCatalog::Count() const {
    return assets.size() <= UINT32_MAX ? (u32)assets.size() : UINT32_MAX;
}

const StaticSolidArchiveCatalogEntry *
StaticSolidArchiveCatalog::Data() const {
    return assets.empty() ? nullptr : assets.data();
}

int StaticSolidArchiveCatalog::CopyFrom(
        const StaticSolidArchiveCatalog &source) {
    try {
        assets = source.assets;
        return 1;
    } catch (const std::bad_alloc &) {
        Free();
        return 0;
    }
}

int StaticSolidArchiveCatalog::AddDescriptorAsset(
        const StaticSolidArchiveCatalogEntry &asset) {
    if (assets.size() >= UINT32_MAX) {
        return 0;
    }
    try {
        assets.push_back(asset);
        return 1;
    } catch (const std::bad_alloc &) {
        return 0;
    }
}

int StaticSolidArchiveCatalogEntry::
AcceptsPackFileDescriptor(
        const CPlugFileFidContainer_SFileDesc &file,
        const char *plainPath) {
    return (file.classId == TMNF_CLASS_CPlugSolid &&
            SceneDescriptorFolderPaths::IsMediaSolidPath(plainPath)) ||
           file.classId == TMNF_CLASS_CPlugSurface ||
           file.classId == TMNF_CLASS_CPlugDecoratorSolid ||
           file.classId == TMNF_CLASS_CScene3d ||
           file.classId == TMNF_CLASS_CGameCtnDecoration ||
           file.classId == 0x0303b000u;
}

int StaticSolidArchiveCatalogEntry::HasPackPayload() const {
    return payloadCompressedSize != 0u || payloadUncompressedSize != 0u;
}

int StaticSolidArchiveCatalogEntry::IsCompressed() const {
    return payloadIsCompressed != 0u;
}

int StaticSolidArchiveCatalogEntry::IsEncrypted() const {
    return payloadIsEncrypted != 0u;
}

u32 StaticSolidArchiveCatalogEntry::PackedByteCount() const {
    return IsCompressed() ? payloadCompressedSize : payloadUncompressedSize;
}

u32 StaticSolidArchiveCatalogEntry::CompressedByteCount() const {
    return payloadCompressedSize;
}

u32 StaticSolidArchiveCatalogEntry::UncompressedByteCount() const {
    return payloadUncompressedSize;
}

u32 StaticSolidArchiveCatalogEntry::PackPayloadOffset() const {
    return payloadOffsetRel;
}

int StaticSolidArchiveCatalogEntry::IsDecoratorSolid() const {
    return descriptorClassId == TMNF_CLASS_CPlugDecoratorSolid;
}

int StaticSolidArchiveSource::Set(
        const char *plainPath,
        const char *selectedPath) {
    return (TmnfFormat::FixedCString::Copy((plainPackPath), (sizeof(plainPackPath)), (plainPath))) &&
           (TmnfFormat::FixedCString::Copy((selectedDescriptorPath), (sizeof(selectedDescriptorPath)), (selectedPath)));
}

int StaticSolidArchiveSource::SetSamePath(
        const char *path) {
    return Set(path, path);
}

int StaticSolidArchiveSource::
        HasSelectedDescriptor() const {
    return selectedDescriptorPath[0] != '\0';
}

int StaticSolidArchiveSource::
        MatchesSelectedDescriptor(const char *path) const {
    return path != nullptr &&
           path[0] != '\0' &&
           (TmnfFormat::FixedCString::Equals((selectedDescriptorPath), (path)));
}

const char *StaticSolidArchiveSource::
        PlainPackPath() const {
    return plainPackPath;
}

const char *StaticSolidArchiveSource::
        SelectedDescriptorPath() const {
    return selectedDescriptorPath;
}

int StaticSolidArchiveCatalogEntry::
BuildFromPackFileDescriptor(
        u32 fileIndex,
        const CPlugFileFidContainer_SFileDesc &file,
        const char *plainPath) {
    if (!AcceptsPackFileDescriptor(file, plainPath)) {
        return 0;
    }
    *this = StaticSolidArchiveCatalogEntry{};
    descriptorIndex = fileIndex;
    descriptorClassId = file.classId;
    descriptorFlagsLow = (u32)file.flags;
    payloadOffsetRel = file.offsetRel;
    payloadCompressedSize = file.compressedSize;
    payloadUncompressedSize = file.uncompressedSize;
    payloadIsCompressed = file.IsCompressed() ? 1u : 0u;
    payloadIsEncrypted = file.IsEncryptedPayload() ? 1u : 0u;
    loaderArgument = fileIndex + 1u;
    return descriptorRef.SetSamePath(plainPath);
}

int StaticSolidArchiveCatalog::LoadFromInstalledPack(
        const CPlugFilePack *pack) {
    if (pack == nullptr) {
        return 0;
    }
    Free();
    for (u32 fileIndex = 0u; fileIndex < pack->files.size(); fileIndex++) {
        const CPlugFileFidContainer_SFileDesc *file = &pack->files[fileIndex];
        char plainPath[CGameCtnReplayStaticSolidDescriptorPathCapacity];
        plainPath[0] = '\0';
        if (!pack->FileDescPlainPath(file, plainPath, sizeof(plainPath))) {
            Free();
            return 0;
        }
        if (!StaticSolidArchiveCatalogEntry::
                    AcceptsPackFileDescriptor(*file, plainPath)) {
            continue;
        }
        StaticSolidArchiveCatalogEntry asset;
        if (!asset.BuildFromPackFileDescriptor(fileIndex, *file, plainPath)) {
            Free();
            return 0;
        }
        if (!AddDescriptorAsset(asset)) {
            Free();
            return 0;
        }
    }
    return Count() != 0u;
}

const StaticSolidArchiveCatalogEntry *
StaticSolidArchiveCatalog::Find(
        const char *selectedDescriptorPath) const {
    if (selectedDescriptorPath == nullptr ||
        selectedDescriptorPath[0] == '\0') {
        return nullptr;
    }
    for (const StaticSolidArchiveCatalogEntry &asset : assets) {
        if (asset.descriptorRef.MatchesSelectedDescriptor(
                    selectedDescriptorPath)) {
            return &asset;
        }
    }
    return nullptr;
}

int StaticSolidArchiveCatalog::
RequestDecoratorSolidDependencies(
        CGameCtnReplayStaticSolidDescriptorDependencyQueue &dependencyQueue) const {
    for (const StaticSolidArchiveCatalogEntry &asset : assets) {
        /*
         * Decorator-solid descriptors carry a collection of tree decorators;
         * request their descriptor dependencies before graph assembly.
         */
        if (asset.IsDecoratorSolid() &&
            !dependencyQueue.RequestOptionalDescriptor(
                    asset.descriptorRef.SelectedDescriptorPath())) {
            return 0;
        }
    }
    return 1;
}

void StaticSolidArchiveLoadSession::Free() {
    *this = StaticSolidArchiveLoadSession{};
}

void StaticSolidArchivePackSource::Clear() {
    pack = nullptr;
}

int StaticSolidArchivePayload::BuildFromInventory(
        const StaticSolidArchiveCatalogEntry &descriptorAsset,
        u32 packDataStart) {
    const u32 packedByteCount = descriptorAsset.PackedByteCount();
    if (packedByteCount > UINT32_MAX - 7u ||
        packDataStart > UINT32_MAX - descriptorAsset.PackPayloadOffset()) {
        return 0;
    }
    const u32 encryptedByteCount = (packedByteCount + 7u) & ~7u;
    if (packDataStart > UINT32_MAX - descriptorAsset.PackPayloadOffset() ||
        (descriptorAsset.IsEncrypted() &&
         encryptedByteCount > UINT32_MAX - 8u)) {
        return 0;
    }

    *this = StaticSolidArchivePayload{};
    descriptorIndex = descriptorAsset.descriptorIndex;
    loaderArgument = descriptorAsset.loaderArgument;
    descriptorClassId = descriptorAsset.descriptorClassId;
    payloadOffsetRel = descriptorAsset.PackPayloadOffset();
    payloadOffsetAbs = packDataStart + descriptorAsset.PackPayloadOffset();
    sourceByteCount = descriptorAsset.IsEncrypted()
            ? encryptedByteCount + 8u
            : packedByteCount;
    payloadCompressedSize = descriptorAsset.CompressedByteCount();
    payloadUncompressedSize = descriptorAsset.UncompressedByteCount();
    payloadIsCompressed = descriptorAsset.IsCompressed() ? 1u : 0u;
    payloadIsEncrypted = descriptorAsset.IsEncrypted() ? 1u : 0u;
    descriptorRef = descriptorAsset.descriptorRef;
    return 1;
}

int StaticSolidArchivePayload::BuildEncryptedPackFilePayload(
        const EncryptedPackFileRequest &request) {
    const CPlugFileFidContainer_SFileDesc &file = request.file;
    const u32 payloadByteCount = file.PackedPayloadByteCount();
    if (payloadByteCount > UINT32_MAX - 7u ||
        request.packDataStart > UINT32_MAX - file.offsetRel) {
        return 0;
    }
    const u32 encryptedByteCount = (payloadByteCount + 7u) & ~7u;
    if (encryptedByteCount > UINT32_MAX - 8u) {
        return 0;
    }

    *this = StaticSolidArchivePayload{};
    descriptorIndex = request.descriptorIndex;
    loaderArgument = request.loaderArgument;
    descriptorClassId = file.classId;
    payloadOffsetRel = file.offsetRel;
    payloadOffsetAbs = request.packDataStart + file.offsetRel;
    sourceByteCount = encryptedByteCount + 8u;
    payloadCompressedSize = file.compressedSize;
    payloadUncompressedSize = file.uncompressedSize;
    payloadIsCompressed = file.IsCompressed() ? 1u : 0u;
    payloadIsEncrypted = 1u;
    return descriptorRef.Set(
            request.plainPath,
            request.selectedDescriptorPath);
}

int StaticSolidArchivePayload::HasSelectedDescriptor() const {
    return descriptorRef.HasSelectedDescriptor();
}

int StaticSolidArchivePayload::MatchesSelectedDescriptor(
        const char *selectedDescriptorPath) const {
    return descriptorRef.MatchesSelectedDescriptor(selectedDescriptorPath);
}

const char *StaticSolidArchivePayload::PlainPackPath() const {
    return descriptorRef.PlainPackPath();
}

const char *StaticSolidArchivePayload::SelectedDescriptorPath()
        const {
    return descriptorRef.SelectedDescriptorPath();
}

u32 StaticSolidArchivePayload::DescriptorClassId() const {
    return descriptorClassId;
}

int StaticSolidArchivePayload::IsEncrypted() const {
    return payloadIsEncrypted != 0u;
}

int StaticSolidArchivePayload::IsCompressed() const {
    return payloadIsCompressed != 0u;
}

int StaticSolidArchivePayload::IsDecoded() const {
    return decodedStatus == 1u;
}

u32 StaticSolidArchivePayload::RawByteCount() const {
    return sourceByteCount;
}

u32 StaticSolidArchivePayload::PackedByteCount() const {
    return IsCompressed() ? payloadCompressedSize : payloadUncompressedSize;
}

u32 StaticSolidArchivePayload::CompressedByteCount() const {
    return payloadCompressedSize;
}

u32 StaticSolidArchivePayload::UncompressedByteCount() const {
    return payloadUncompressedSize;
}

u32 StaticSolidArchivePayload::PackPayloadOffset() const {
    return payloadOffsetAbs;
}


int StaticSolidArchivePackSource::BuildPayloadAsset(
        const StaticSolidArchiveCatalogEntry &descriptorAsset,
        StaticSolidArchivePayload *payloadAsset) const {
    return pack != nullptr && payloadAsset != nullptr &&
           payloadAsset->BuildFromInventory(
                   descriptorAsset, pack->dataStart);
}

void StaticSolidArchivePayload::MarkDecodeUnavailable() {
    decodedStatus = 2u;
}



int StaticSolidArchiveLoadSession::HasDescriptor(
        const char *selectedDescriptorPath) const {
    return FindArchiveIndex(selectedDescriptorPath) !=
           UINT32_MAX;
}

StaticSolidArchiveId
StaticSolidArchiveLoadSession::SelectPayloadForDescriptor(
        const char *selectedDescriptorPath) const {
    return StaticSolidArchiveId::FromIndex(
            FindArchiveIndex(selectedDescriptorPath));
}

int StaticSolidArchiveLoadSession::PayloadMatchesSelectedDescriptor(
        StaticSolidArchiveId payload,
        const char *selectedDescriptorPath) const {
    return payload.IsValid() &&
           payload.MatchesIndex(
                   FindArchiveIndex(selectedDescriptorPath));
}

u32 StaticSolidArchiveLoadSession::FindArchiveIndex(
        const char *selectedDescriptorPath) const {
    if (selectedDescriptorPath == nullptr ||
        selectedDescriptorPath[0] == '\0') {
        return UINT32_MAX;
    }
    for (u32 i = 0; i < payloads.size(); i++) {
        const StaticSolidArchivePayload &payloadAsset = payloads[i];
        if (payloadAsset.MatchesSelectedDescriptor(selectedDescriptorPath)) {
            return i;
        }
    }
    return UINT32_MAX;
}

int StaticSolidArchiveLoadSession::HasPayloads() const {
    return !payloads.empty();
}

u32 StaticSolidArchiveLoadSession::ArchiveCount() const {
    return static_cast<u32>(payloads.size());
}

const StaticSolidArchivePayload *
StaticSolidArchiveLoadSession::PayloadFor(
        StaticSolidArchiveId payload) const {
    if (!payload.FitsWithin(static_cast<u32>(payloads.size()))) {
        return nullptr;
    }
    return &payloads[payload.Index()];
}

StaticSolidArchiveDecodedBytes
StaticSolidArchiveLoadSession::DecodedPayloadBytes(
        StaticSolidArchiveId payload,
        u32 decodedByteOffset,
        u32 byteCount) const {
    if (!payload.IsValid()) {
        return StaticSolidArchiveDecodedBytes::Missing();
    }
    const StaticSolidArchivePayload *payloadAsset =
            PayloadFor(payload);
    if (payloadAsset == nullptr) {
        return StaticSolidArchiveDecodedBytes::Missing();
    }
    if (!payloadAsset->IsDecoded() ||
        decodedByteOffset > payloadAsset->decodedBytes.size() ||
        byteCount > payloadAsset->decodedBytes.size() - decodedByteOffset) {
        return StaticSolidArchiveDecodedBytes::Missing();
    }
    return StaticSolidArchiveDecodedBytes::FromBytes(
            payloadAsset->decodedBytes.data() + decodedByteOffset,
            byteCount);
}

int StaticSolidArchiveLoadSession::AddPayloadAsset(
        const StaticSolidArchivePayload &payloadAsset) {
    try {
        payloads.push_back(payloadAsset);
        return 1;
    } catch (const std::bad_alloc &) {
        return 0;
    }
}

int StaticSolidArchiveLoadSession::DecodeDescriptorDependency(
        const CGameCtnReplayStaticSolidDescriptorDependency &dependency,
        const StaticSolidArchiveCatalog *inventory,
        CGameCtnReplayArchiveStaticModelCollection *archiveModels,
        CGameCtnReplayStaticSolidDescriptorDependencyQueue *dependencyQueue,
        u32 *selectedMissingOut) {
    if (inventory == nullptr || dependencyQueue == nullptr) {
        return 0;
    }
    const char *selectedDescriptorPath = dependency.SelectedDescriptorPath();
    if (selectedDescriptorPath == nullptr ||
        selectedDescriptorPath[0] == '\0' ||
        HasDescriptor(selectedDescriptorPath)) {
        return 1;
    }
    const StaticSolidArchiveCatalogEntry *descriptorAsset =
            inventory->Find(selectedDescriptorPath);
    if (descriptorAsset == nullptr) {
        if (dependency.IsRequired() && selectedMissingOut != nullptr) {
            (*selectedMissingOut)++;
        }
        return 1;
    }
    StaticSolidArchivePayload payloadAsset;
    return BuildPayloadAssetFromDescriptor(*descriptorAsset, &payloadAsset) &&
           DecodeAndAppendArchive(payloadAsset,
                                  archiveModels,
                                  dependencyQueue,
                                  dependency.IsRequired());
}

int StaticSolidArchiveLoadSession::InstallPackSource(
        const CPlugFilePack &pack) {
    return payloadPackSource.Install(pack);
}

void StaticSolidArchiveLoadSession::InstallMaterialAssets(
        MaterialAssetRepository &assets) {
    materialAssets = &assets;
}

int StaticSolidArchivePackSource::Install(
        const CPlugFilePack &newPack) {
    pack = &newPack;
    return 1;
}

const SNat128 *StaticSolidArchivePackSource::Key() const {
    return pack != nullptr ? &pack->key : nullptr;
}

const CPlugFilePack *StaticSolidArchivePackSource::ExternalPack()
        const {
    return pack;
}

const CPlugFilePack *StaticSolidArchiveLoadSession::ExternalPack()
        const {
    return payloadPackSource.ExternalPack();
}

MaterialAssetRepository *StaticSolidArchiveLoadSession::MaterialAssets()
        const {
    return materialAssets;
}

StaticSolidArchiveReferenceCatalog *
StaticSolidArchiveLoadSession::SolidReferences() const {
    return solidReferences;
}

CGameCtnReplayStaticSolidArchiveGraph &
StaticSolidArchiveLoadSession::MutableArchiveGraph() {
    return archiveGraph;
}

const CGameCtnReplayStaticSolidArchiveGraph &
StaticSolidArchiveLoadSession::ArchiveGraph() const {
    return archiveGraph;
}

int StaticSolidArchiveLoadSession::LoadRawPayloadBytesFromPack(
        StaticSolidArchivePayload *payloadAsset) {
    const CPlugFilePack *pack = payloadPackSource.ExternalPack();
    if (pack == nullptr || payloadAsset == nullptr ||
        payloadAsset->PackPayloadOffset() > pack->bytes.Size() ||
        payloadAsset->RawByteCount() >
                pack->bytes.Size() - payloadAsset->PackPayloadOffset()) {
        return 0;
    }
    try {
        payloadAsset->rawBytes.resize(payloadAsset->RawByteCount());
    } catch (const std::bad_alloc &) {
        return 0;
    }
    if (!payloadAsset->rawBytes.empty()) {
        const unsigned char *source = pack->bytes.Data() +
                payloadAsset->PackPayloadOffset();
        std::copy(source,
                  source + payloadAsset->RawByteCount(),
                  payloadAsset->rawBytes.begin());
    }
    return 1;
}

StaticSolidArchiveRawBytes
StaticSolidArchiveLoadSession::RawPayloadBytesForPayload(
        const StaticSolidArchivePayload &payloadAsset) const {
    if (payloadAsset.rawBytes.size() != payloadAsset.RawByteCount()) {
        return StaticSolidArchiveRawBytes::Missing();
    }
    static const unsigned char emptyPayloadByte = 0u;
    return StaticSolidArchiveRawBytes::FromBytes(
            payloadAsset.rawBytes.empty()
                    ? &emptyPayloadByte
                    : payloadAsset.rawBytes.data(),
            payloadAsset.RawByteCount());
}

int StaticSolidArchiveLoadSession::AppendDecodedPayload(
        const unsigned char *bytes,
        u32 byteCount,
        StaticSolidArchivePayload *payloadAsset) {
    if (payloadAsset == nullptr) {
        return 0;
    }
    if (byteCount != 0u && bytes == nullptr) {
        return 0;
    }
    try {
        if (byteCount == 0u) {
            payloadAsset->decodedBytes.clear();
        } else {
            payloadAsset->decodedBytes.assign(bytes, bytes + byteCount);
        }
    } catch (const std::bad_alloc &) {
        payloadAsset->decodedBytes.clear();
        return 0;
    }
    payloadAsset->decodedStatus = 1u;
    return 1;
}

StaticSolidArchiveRollback
StaticSolidArchiveLoadSession::MarkDecodedArchiveTail(
        const CGameCtnReplayArchiveStaticModelCollection *archiveModels) const {
    StaticSolidArchiveRollback mark;
    mark.archiveGraphMark = archiveGraph.MarkRollback();
    if (archiveModels != nullptr) {
        mark.archiveModelCount = archiveModels->Count();
    }
    return mark;
}

int StaticSolidArchiveLoadSession::RestoreDecodedArchiveTail(
        const StaticSolidArchiveRollback &mark,
        CGameCtnReplayArchiveStaticModelCollection *archiveModels) {
    if (!archiveGraph.CanRestoreRollback(mark.archiveGraphMark) ||
        mark.archiveModelCount.has_value() != (archiveModels != nullptr) ||
        (archiveModels != nullptr &&
         *mark.archiveModelCount > archiveModels->Count())) {
        return 0;
    }
    if (archiveModels != nullptr &&
        !archiveModels->ResizePrefix(*mark.archiveModelCount)) {
        return 0;
    }
    return archiveGraph.RestoreRollback(mark.archiveGraphMark);
}


// Static-solid preload orchestration and typed scene publication.

StaticSceneArchiveLoad::~StaticSceneArchiveLoad() {
    Free();
}

void StaticSceneArchiveLoad::Free() {
    sceneModels.Clear();
    archive.Free();
    archiveModels.Free();
    selectedMissing = 0u;
}

StaticSolidArchiveLoadSession::ReplayArchiveResult
StaticSolidArchiveLoadSession::LoadReplayArchives(
        const ReplayArchiveRequest &request) {
    ReplayArchiveResult result;
    Free();
    materialAssets = &request.installedPackAssets;
    solidReferences =
            &InstalledPackAssetRepositoryAccess::StaticSolidReferences(
                    request.installedPackAssets);

    StaticSolidArchiveCatalog inventory;
    auto failWithStoreReset = [&]() {
        Free();
        return result;
    };
    if (!InstallArchiveSource(request.installedPackAssets,
                              inventory,
                              *this)) {
        return result;
    }
    std::optional<CatalogDecorationSizeDefinition> decorationSize;
    if (request.role != ReplayStaticArchiveRole::Map) {
        decorationSize = request.installedPackAssets.DecorationSize(
                request.mapInput);
        if (!decorationSize) {
            return failWithStoreReset();
        }
    }

    CGameCtnReplayStaticSolidDescriptorDependencyQueue dependencyQueue;
    int seeded = 0;
    switch (request.role) {
    case ReplayStaticArchiveRole::Complete:
        seeded = dependencyQueue.SeedFromReplayStaticInputs(
                &request.mapInput,
                &request.placements,
                &request.archiveModels,
                &inventory,
                solidReferences,
                &*decorationSize);
        break;
    case ReplayStaticArchiveRole::Map:
        seeded = dependencyQueue.SeedFromReplayMapStaticInputs(
                &request.placements,
                &request.archiveModels,
                solidReferences);
        break;
    case ReplayStaticArchiveRole::Decoration:
        seeded = dependencyQueue.SeedFromReplayDecorationStaticInputs(
                &inventory, &*decorationSize);
        break;
    }
    if (!seeded ||
        !dependencyQueue.DecodeReachablePayloadGraph(
                &inventory,
                this,
                &request.archiveModels,
                &result.selectedMissing)) {
        return failWithStoreReset();
    }
    result.loaded = HasPayloads();
    if (!result.loaded) {
        Free();
    }
    return result;
}

int StaticSceneArchiveLoad::Build(
        InstalledPackAssetRepository &installedPackAssets,
        const CGameCtnReplayMapInput &mapInput,
        const ReplaySceneBlockPlacements &placements) {
    Free();
    if (InstalledPackAssetRepositoryAccess::Pack(installedPackAssets) == nullptr ||
        !LoadCatalogArchiveStaticModels(
                installedPackAssets,
                archiveModels,
                CatalogArchiveStaticModelUsage::DependencyOnly)) {
        return 0;
    }

    const StaticSolidArchiveLoadSession::ReplayArchiveResult archiveResult =
            archive.LoadReplayArchives({
                    installedPackAssets,
                    mapInput,
                    placements,
                    archiveModels,
                    ReplayStaticArchiveRole::Complete});
    selectedMissing = archiveResult.selectedMissing;
    if (!archiveResult.loaded) {
        return 0;
    }
    return selectedMissing != 0u ||
           BuildStaticSceneFromArchive(archive,
                                       archiveModels,
                                       placements,
                                       sceneModels);
}

int StaticSceneArchiveLoad::BuildMap(
        InstalledPackAssetRepository &installedPackAssets,
        const CGameCtnReplayMapInput &mapInput,
        const ReplaySceneBlockPlacements &placements) {
    Free();
    if (InstalledPackAssetRepositoryAccess::Pack(installedPackAssets) ==
                nullptr ||
        !LoadCatalogArchiveStaticModels(
                installedPackAssets,
                archiveModels,
                CatalogArchiveStaticModelUsage::SceneModel)) {
        return 0;
    }
    const StaticSolidArchiveLoadSession::ReplayArchiveResult archiveResult =
            archive.LoadReplayArchives({
                    installedPackAssets,
                    mapInput,
                    placements,
                    archiveModels,
                    ReplayStaticArchiveRole::Map});
    selectedMissing = archiveResult.selectedMissing;
    if (!archiveResult.loaded) {
        return 0;
    }
    return selectedMissing != 0u ||
           BuildStaticSceneFromArchive(archive,
                                       archiveModels,
                                       placements,
                                       sceneModels);
}

int StaticSceneArchiveLoad::BuildDecoration(
        InstalledPackAssetRepository &installedPackAssets,
        const CGameCtnReplayMapInput &mapInput) {
    Free();
    ReplaySceneBlockPlacements placements;
    const StaticSolidArchiveLoadSession::ReplayArchiveResult archiveResult =
            archive.LoadReplayArchives({
                    installedPackAssets,
                    mapInput,
                    placements,
                    archiveModels,
                    ReplayStaticArchiveRole::Decoration});
    selectedMissing = archiveResult.selectedMissing;
    if (!archiveResult.loaded) {
        return 0;
    }
    return selectedMissing != 0u ||
           BuildStaticSceneFromArchive(archive,
                                       archiveModels,
                                       placements,
                                       sceneModels);
}

namespace {

bool MergeRoutedStaticSceneModels(
        const StaticSceneModelCollection &decorationModels,
        const StaticSceneModelCollection &mapModels,
        StaticSceneModelCollection *out) {
    if (out == nullptr || !decorationModels.IsComplete() ||
        !mapModels.IsComplete()) {
        return false;
    }
    out->Clear();
    if (!out->Reserve(decorationModels.Models().size() +
                      mapModels.Models().size())) {
        return false;
    }
    for (const StaticSceneModel &model : decorationModels.Models()) {
        if (!out->Add(model)) {
            out->Clear();
            return false;
        }
    }
    for (const StaticSceneModel &model : mapModels.Models()) {
        if (!out->Add(model)) {
            out->Clear();
            return false;
        }
    }
    return true;
}

}  // namespace

bool InstalledPackAssetRepository::BuildStaticScene(
        const CGameCtnReplayMapInput &mapInput,
        const ReplaySceneBlockPlacements &placements,
        StaticSceneModelCollection *out) {
    if (out == nullptr) {
        return false;
    }
    StaticSceneArchiveLoad load;
    if (!load.Build(*this, mapInput, placements) ||
        load.selectedMissing != 0u) {
        return false;
    }
    *out = std::move(load.sceneModels);
    return true;
}

bool InstalledPackAssetRepository::BuildStaticSceneWithDecorationAssets(
        ReplayAssetRepository &decorationAssets,
        const CGameCtnReplayMapInput &mapInput,
        const ReplaySceneBlockPlacements &placements,
        StaticSceneModelCollection *out) {
    if (&decorationAssets == this) {
        return BuildStaticScene(mapInput, placements, out);
    }
    auto *installedDecorationAssets =
            dynamic_cast<InstalledPackAssetRepository *>(
                    &decorationAssets);
    if (out == nullptr || installedDecorationAssets == nullptr) {
        return false;
    }
    const CPlugFilePack *mapPack =
            InstalledPackAssetRepositoryAccess::Pack(*this);
    const CPlugFilePack *decorationPack =
            InstalledPackAssetRepositoryAccess::Pack(
                    *installedDecorationAssets);
    if (mapPack == nullptr || decorationPack == nullptr) {
        return false;
    }
    if (mapPack->PackName() == decorationPack->PackName()) {
        return BuildStaticScene(mapInput, placements, out);
    }

    StaticSceneArchiveLoad mapLoad;
    StaticSceneArchiveLoad decorationLoad;
    if (!mapLoad.BuildMap(*this, mapInput, placements) ||
        mapLoad.selectedMissing != 0u ||
        !decorationLoad.BuildDecoration(
                *installedDecorationAssets, mapInput) ||
        decorationLoad.selectedMissing != 0u ||
        !MergeRoutedStaticSceneModels(decorationLoad.sceneModels,
                                      mapLoad.sceneModels,
                                      out)) {
        out->Clear();
        return false;
    }
    return true;
}
