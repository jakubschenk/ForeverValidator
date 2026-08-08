#include "format/static_solid/static_solid_archive_tree_reader.h"
#include "format/archive/gm_wire_conversion.h"
#include "format/static_solid/static_scene_archive_loader.h"
#include "format/static_solid/static_solid_archive_byte_stream.h"
#include "format/static_solid/static_solid_archive_chunk_dispatcher.h"
#include "format/static_solid/static_solid_archive_cmwid_state.h"
#include "format/static_solid/static_solid_archive_decode_progress.h"
#include "format/static_solid/static_solid_archive_graph_writer.h"
#include "format/static_solid/static_solid_material_definition_resolver.h"
#include "format/static_solid/static_solid_archive_primitive_reader.h"
#include "format/static_solid/static_solid_archive_tree_chunk_ids.h"
#include "format/static_solid/static_solid_archive_tree_state_flags.h"
namespace {

CPlugTree::SFlags DecodeTreeState(u32 encodedStateFlags) {
  CPlugTree::SFlags state;
  state.groupingBoundary = (encodedStateFlags & 0x0001u) != 0u;
  state.visible = (encodedStateFlags & 0x0008u) != 0u;
  state.requiresMatchingShaderState = (encodedStateFlags & 0x0010u) != 0u;
  state.requiresMatchingMaterialState = (encodedStateFlags & 0x0020u) != 0u;
  state.modelInstance = (encodedStateFlags & 0x0040u) != 0u;
  state.collisionEnabled = (encodedStateFlags & 0x0080u) != 0u;
  state.groupingMarker = (encodedStateFlags & 0x0100u) != 0u;
  state.pickableVisual = (encodedStateFlags & 0x0800u) != 0u;
  state.renderBeforeFidParametrization = (encodedStateFlags & 0x1000u) != 0u;
  state.castsShadows = (encodedStateFlags & 0x4000u) != 0u;
  state.rooted = (encodedStateFlags & 0x8000u) != 0u;
  state.locationDirty = (encodedStateFlags & 0x10000u) != 0u;
  state.usesLocation = (encodedStateFlags & 0x0004u) != 0u;
  return state;
}

} // namespace

void CPlugTreeStateArchivePayload::Reset(StaticSolidArchiveId newPayload,
                                         u32 newTreeNodeIndex, u32 newChunkId) {
  payload = newPayload;
  treeNodeIndex = newTreeNodeIndex;
  chunkId = newChunkId;
  stateScope =
      CGameCtnReplayStaticSolidArchiveTreeStateDefinition::Scope::Complete;
  state = CPlugTree::SFlags{};
  hasLocalIso = 0u;
  ClearLocalIso();
}

void CPlugTreeStateArchivePayload::ClearLocalIso() {
  for (u32 i = 0u; i < 12u; i++) {
    localIso[i] = 0.0f;
  }
}

int CPlugTreeStateArchivePayload::ReadLocalIsoIfPresent(
    CGameCtnReplayStaticSolidArchiveByteStream *byteStream) {
  if (hasLocalIso == 0u) {
    return 1;
  }
  return CGameCtnReplayStaticSolidArchivePrimitiveReader::ReadIso4(byteStream,
                                                                   localIso);
}

int CPlugTreeStateArchivePayload::ReadStateAndTransform(
    CGameCtnReplayStaticSolidArchiveByteStream *byteStream,
    u32 encodedStateFlags) {
  if (byteStream == nullptr) {
    return 0;
  }
  stateScope =
      CGameCtnReplayStaticSolidArchiveTreeStateDefinition::Scope::Complete;
  state = DecodeTreeState(encodedStateFlags);
  hasLocalIso = (encodedStateFlags & HasLocalIsoFlag) != 0u;
  ClearLocalIso();
  return ReadLocalIsoIfPresent(byteStream);
}

int CPlugTreeStateArchivePayload::ReadHasIsoBool(
    CGameCtnReplayStaticSolidArchiveByteStream *byteStream, int withParentRefs,
    CGameCtnReplayStaticSolidArchiveNodeRefReader *nodeRefs) {
  if (byteStream == nullptr || nodeRefs == nullptr) {
    return 0;
  }
  u32 hasIso = 0u;
  if (!byteStream->ReadBool(&hasIso)) {
    return 0;
  }
  stateScope = CGameCtnReplayStaticSolidArchiveTreeStateDefinition::Scope::
      LocalTransform;
  state = CPlugTree::SFlags{};
  state.usesLocation = hasIso != 0u;
  hasLocalIso = hasIso;
  ClearLocalIso();
  if (!ReadLocalIsoIfPresent(byteStream)) {
    return 0;
  }
  if (withParentRefs &&
      (!nodeRefs->ReadNodPtr(nullptr) || !nodeRefs->ReadNodPtr(nullptr))) {
    return 0;
  }
  return 1;
}

int CPlugTreeStateArchivePayload::Install(
    StaticSolidArchiveLoadSession *store) const {
  if (store == nullptr) {
    return 1;
  }
  const GmIso4 loadedLocation = GmWire::DecodeIso4(localIso);
  CGameCtnReplayStaticSolidArchiveTreeStateDefinition definition = {};
  definition.Install(
      CGameCtnReplayStaticSolidArchiveNodeIdentity::FromPayloadAndArchiveIndex(
          payload, treeNodeIndex),
      chunkId, stateScope, state, hasLocalIso, &loadedLocation);
  return store->MutableArchiveGraph().TreeGraph().AddTreeState(definition);
}

void CPlugTreeVisualArchivePayload::Reset(StaticSolidArchiveId newPayload,
                                          u32 newTreeNodeIndex,
                                          u32 newChunkId) {
  payload = newPayload;
  treeNode = ArchiveNodeReference::FromIndex(newTreeNodeIndex);
  chunkId = newChunkId;
  treeSourceRefs.Clear();
}

int CPlugTreeVisualArchivePayload::ReadSurfaceRefs(
    CGameCtnReplayStaticSolidArchiveNodeRefReader *nodeRefs) {
  ArchiveNodeReference visualNode = ArchiveNodeReference::Invalid();
  ArchiveNodeReference shaderNode = ArchiveNodeReference::Invalid();
  ArchiveNodeReference surfaceSourceNode = ArchiveNodeReference::Invalid();
  if (nodeRefs == nullptr || !nodeRefs->ReadNodeRef(&visualNode) ||
      !nodeRefs->ReadNodeRef(&shaderNode) ||
      !nodeRefs->ReadNodeRef(&surfaceSourceNode)) {
    return 0;
  }
  treeSourceRefs.SetSurfaceRefs(visualNode, shaderNode, surfaceSourceNode);
  return 1;
}

int CPlugTreeVisualArchivePayload::ReadSurfaceGeneratorRefs(
    CGameCtnReplayStaticSolidArchiveNodeRefReader *nodeRefs) {
  ArchiveNodeReference visualNode = ArchiveNodeReference::Invalid();
  ArchiveNodeReference shaderNode = ArchiveNodeReference::Invalid();
  ArchiveNodeReference surfaceSourceNode = ArchiveNodeReference::Invalid();
  ArchiveNodeReference generatorNode = ArchiveNodeReference::Invalid();
  if (nodeRefs == nullptr || !nodeRefs->ReadNodeRef(&visualNode) ||
      !nodeRefs->ReadNodeRef(&shaderNode) ||
      !nodeRefs->ReadNodeRef(&surfaceSourceNode) ||
      !nodeRefs->ReadNodeRef(&generatorNode)) {
    return 0;
  }
  treeSourceRefs.SetSurfaceRefsAndGenerator(visualNode, shaderNode,
                                            surfaceSourceNode, generatorNode);
  return 1;
}

int CPlugTreeVisualArchivePayload::ReadMaterialSurfaceGeneratorRefs(
    CGameCtnReplayStaticSolidArchiveNodeRefReader *nodeRefs) {
  ArchiveNodeReference visualNode = ArchiveNodeReference::Invalid();
  ArchiveNodeReference shaderNode = ArchiveNodeReference::Invalid();
  ArchiveNodeReference materialNode = ArchiveNodeReference::Invalid();
  ArchiveNodeReference surfaceSourceNode = ArchiveNodeReference::Invalid();
  ArchiveNodeReference generatorNode = ArchiveNodeReference::Invalid();
  if (nodeRefs == nullptr || !nodeRefs->ReadNodeRef(&visualNode) ||
      !nodeRefs->ReadNodeRef(&shaderNode) ||
      !nodeRefs->ReadNodeRef(&materialNode) ||
      !nodeRefs->ReadNodeRef(&surfaceSourceNode) ||
      !nodeRefs->ReadNodeRef(&generatorNode)) {
    return 0;
  }
  treeSourceRefs.SetMaterialSurfaceGenerator(
      visualNode, shaderNode, materialNode, surfaceSourceNode, generatorNode);
  return 1;
}

int CPlugTreeVisualArchivePayload::ReadSurfaceOnly(
    CGameCtnReplayStaticSolidArchiveNodeRefReader *nodeRefs) {
  ArchiveNodeReference surfaceSourceNode = ArchiveNodeReference::Invalid();
  if (nodeRefs == nullptr || !nodeRefs->ReadNodeRef(&surfaceSourceNode)) {
    return 0;
  }
  treeSourceRefs.SetSurfaceSource(surfaceSourceNode);
  return 1;
}

int CPlugTreeVisualArchivePayload::Install(
    CGameCtnReplayStaticSolidArchiveNodeGraph *archiveNodeGraph,
    const SceneDescriptorFolderPaths *externalFolders,
    const char *sourceDescriptorPath,
    StaticSolidArchiveLoadSession *store) const {
  /*
   * Older vehicle solids store a CPlugMaterial in the tree's legacy shader
   * slot.  Resolve both candidate slots best-effort: real shader nodes simply
   * remain shaders, while material assets become usable render materials.
   */
  (void)StaticSolidMaterialAssetLinker::ResolveAndAppend(
      archiveNodeGraph, externalFolders, store, payload,
      treeSourceRefs.ShaderNode().Index(), sourceDescriptorPath);
  (void)StaticSolidMaterialAssetLinker::ResolveAndAppend(
      archiveNodeGraph, externalFolders, store, payload,
      treeSourceRefs.MaterialNode().Index(), sourceDescriptorPath);
  CGameCtnReplayStaticSolidArchiveGraphWriter writer(
      store != nullptr ? &store->MutableArchiveGraph() : nullptr, payload);
  return writer.AddTreeSourceLink(treeNode, treeSourceRefs, chunkId) &&
         writer.AddTreeSurfaceLink(treeNode,
                                   treeSourceRefs.SurfaceSourceNode());
}

void CPlugTreeChildLinksArchivePayload::Reset(StaticSolidArchiveId newPayload,
                                              u32 newParentTreeNodeIndex,
                                              u32 newLinkKind) {
  payload = newPayload;
  parentTreeNode = ArchiveNodeReference::FromIndex(newParentTreeNodeIndex);
  linkKind = newLinkKind;
  progressLinkCount = 0u;
  childNodes.clear();
}

int CPlugTreeChildLinksArchivePayload::AppendChildLink(
    ArchiveNodeReference childNode) {
  if (childNode.IsInvalid()) {
    return 1;
  }
  childNodes.push_back(childNode);
  return 1;
}

int CPlugTreeChildLinksArchivePayload::ReadTreeChildFastBuffer(
    CGameCtnReplayStaticSolidArchiveByteStream *byteStream,
    CGameCtnReplayStaticSolidArchiveNodeRefReader *nodeRefs) {
  if (byteStream == nullptr || nodeRefs == nullptr) {
    return 0;
  }
  u32 ignoredReservedWord = 0u;
  u32 count = 0u;
  if (!byteStream->ReadU32(&ignoredReservedWord) ||
      !byteStream->ReadU32(&count) || count > MaxChildLinkCount) {
    return 0;
  }
  progressLinkCount = count;
  childNodes.clear();
  childNodes.reserve(count);
  for (u32 i = 0u; i < count; i++) {
    ArchiveNodeReference childNode = ArchiveNodeReference::Invalid();
    if (!nodeRefs->ReadNodeRef(&childNode)) {
      return 0;
    }
    if (!AppendChildLink(childNode)) {
      return 0;
    }
  }
  return 1;
}

int CPlugTreeChildLinksArchivePayload::ReadVisualMipChildLinks(
    CGameCtnReplayStaticSolidArchiveByteStream *byteStream,
    CGameCtnReplayStaticSolidArchiveNodeRefReader *nodeRefs) {
  if (byteStream == nullptr || nodeRefs == nullptr) {
    return 0;
  }
  u32 count = 0u;
  if (!byteStream->ReadU32(&count) || count > MaxChildLinkCount) {
    return 0;
  }
  progressLinkCount = 0u;
  childNodes.clear();
  childNodes.reserve(count);
  for (u32 i = 0u; i < count; i++) {
    ArchiveNodeReference childNode = ArchiveNodeReference::Invalid();
    if (!byteStream->Skip(4u) || !nodeRefs->ReadNodeRef(&childNode)) {
      return 0;
    }
    if (!childNode.IsInvalid()) {
      progressLinkCount++;
    }
    if (!AppendChildLink(childNode)) {
      return 0;
    }
  }
  return 1;
}

int CPlugTreeChildLinksArchivePayload::Install(
    CGameCtnReplayStaticSolidArchiveDecodeProgress *decodeProgress,
    StaticSolidArchiveLoadSession *store) const {
  if (decodeProgress != nullptr) {
    decodeProgress->RecordArchiveTreeChildLinks(progressLinkCount);
  }
  CGameCtnReplayStaticSolidArchiveGraphWriter writer(
      store != nullptr ? &store->MutableArchiveGraph() : nullptr, payload);
  for (ArchiveNodeReference childNode : childNodes) {
    if (!writer.AddTreeChildLink(parentTreeNode, childNode, linkKind)) {
      return 0;
    }
  }
  return 1;
}

void CPlugTreeIdArchivePayload::Reset(StaticSolidArchiveId newPayload,
                                      u32 newTreeNodeIndex) {
  payload = newPayload;
  treeNode = ArchiveNodeReference::FromIndex(newTreeNodeIndex);
  treeId.SetInvalid();
  objectNode = ArchiveNodeReference::Invalid();
  treeIdName.clear();
}

int CPlugTreeIdArchivePayload::Read(
    CGameCtnReplayStaticSolidArchiveByteStream *byteStream,
    CGameCtnReplayStaticSolidArchiveCMwIdState *cmwIdState,
    CGameCtnReplayStaticSolidArchiveNodeRefReader *nodeRefs) {
  if (byteStream == nullptr || cmwIdState == nullptr || nodeRefs == nullptr) {
    return 0;
  }
  if (!cmwIdState->ReadId(*byteStream, &treeId)) {
    return 0;
  }
  const char *lastTreeIdText = cmwIdState->LastTextOrNull();
  treeIdName = lastTreeIdText != nullptr ? lastTreeIdText : "";
  if (!nodeRefs->ReadNodeRef(&objectNode)) {
    return 0;
  }
  if (objectNode.IsInvalid()) {
    return 1;
  }
  return cmwIdState->ReadSkip(*byteStream);
}

int CPlugTreeIdArchivePayload::Install(
    StaticSolidArchiveLoadSession *store) const {
  CGameCtnReplayStaticSolidArchiveGraphWriter writer(
      store != nullptr ? &store->MutableArchiveGraph() : nullptr, payload);
  writer.SetTreeId(treeNode, treeId,
                   treeIdName.empty() ? nullptr : treeIdName.c_str());
  return 1;
}

int CPlugTreeDiscardedRefArchivePayload::ReadSingleNodRef(
    CGameCtnReplayStaticSolidArchiveNodeRefReader *nodeRefs) {
  return nodeRefs != nullptr && nodeRefs->ReadNodPtr(nullptr);
}

int CGameCtnReplayStaticSolidArchiveTreeReader::ParseTreeChunk(
    const CGameCtnReplayStaticSolidArchiveChunkDispatchContext &context,
    u32 classId, u32 chunkId) {
  if (context.byteStream == nullptr || context.cmwIdState == nullptr ||
      context.nodeRefs == nullptr) {
    return 0;
  }

  const u32 treeNodeIndex = context.currentArchiveNode.Index();
  CGameCtnReplayStaticSolidArchiveByteStream *byteStream = context.byteStream;
  StaticSolidArchiveLoadSession *store = context.materialStore;
  const StaticSolidArchiveId selectedPayload = context.payload;
  CGameCtnReplayStaticSolidArchiveNodeRefReader *nodeRefs = context.nodeRefs;
  const StaticSolidArchivePayload *sourcePayload =
      byteStream->PayloadAsset();
  const char *sourceDescriptorPath = sourcePayload != nullptr
      ? sourcePayload->PlainPackPath()
      : nullptr;

  if (classId == TMNF_CLASS_CPlugTreeLight) {
    if (IsCPlugTreeLightBaseArchiveChunk(chunkId)) {
      return 1;
    }
    if (chunkId ==
        ArchiveChunkIdValue(CPlugTreeLightArchiveChunkId::LightRef)) {
      CPlugTreeDiscardedRefArchivePayload payload;
      return payload.ReadSingleNodRef(context.nodeRefs);
    }
  }

  if (classId == TMNF_CLASS_CPlugTreeVisualMip) {
    if (IsCPlugTreeVisualMipBaseArchiveChunk(chunkId)) {
      return 1;
    }
    if (chunkId ==
        ArchiveChunkIdValue(CPlugTreeVisualMipArchiveChunkId::ChildLinks)) {
      CPlugTreeChildLinksArchivePayload payload;
      payload.Reset(context.payload, treeNodeIndex, 1u);
      return payload.ReadVisualMipChildLinks(context.byteStream,
                                             context.nodeRefs) &&
             payload.Install(context.decodeProgress, context.materialStore);
    }
  }

  if (chunkId ==
      ArchiveChunkIdValue(CPlugTreeArchiveChunkId::SourceSurfaceRefs)) {
    CPlugTreeVisualArchivePayload payload;
    payload.Reset(context.payload, treeNodeIndex, chunkId);
    return payload.ReadSurfaceRefs(context.nodeRefs) &&
           payload.Install(context.archiveNodeGraph,
                           context.externalFolders,
                           sourceDescriptorPath,
                           context.materialStore);
  }
  if (chunkId == ArchiveChunkIdValue(CPlugTreeArchiveChunkId::ChildBuffer)) {
    CPlugTreeChildLinksArchivePayload payload;
    payload.Reset(context.payload, treeNodeIndex, 0u);
    return payload.ReadTreeChildFastBuffer(context.byteStream,
                                           context.nodeRefs) &&
           payload.Install(context.decodeProgress, context.materialStore);
  }
  if (chunkId ==
      ArchiveChunkIdValue(CPlugTreeArchiveChunkId::TreeIdAndObject)) {
    CPlugTreeIdArchivePayload payload;
    payload.Reset(context.payload, treeNodeIndex);
    return payload.Read(context.byteStream, context.cmwIdState,
                        context.nodeRefs) &&
           payload.Install(context.materialStore);
  }
  if (chunkId == ArchiveChunkIdValue(CPlugTreeArchiveChunkId::FuncTreeRef)) {
    CPlugTreeDiscardedRefArchivePayload payload;
    return payload.ReadSingleNodRef(nodeRefs);
  }
  if (chunkId == ArchiveChunkIdValue(
                     CPlugTreeArchiveChunkId::SourceSurfaceGeneratorRefs)) {
    CPlugTreeVisualArchivePayload payload;
    payload.Reset(selectedPayload, treeNodeIndex, chunkId);
    return payload.ReadSurfaceGeneratorRefs(nodeRefs) &&
           payload.Install(context.archiveNodeGraph,
                           context.externalFolders,
                           sourceDescriptorPath,
                           store);
  }
  if (chunkId ==
      ArchiveChunkIdValue(
          CPlugTreeArchiveChunkId::SourceMaterialSurfaceGeneratorRefs)) {
    CPlugTreeVisualArchivePayload payload;
    payload.Reset(selectedPayload, treeNodeIndex, chunkId);
    return payload.ReadMaterialSurfaceGeneratorRefs(nodeRefs) &&
           payload.Install(context.archiveNodeGraph,
                           context.externalFolders,
                           sourceDescriptorPath,
                           store);
  }
  if (chunkId ==
      ArchiveChunkIdValue(CPlugTreeArchiveChunkId::StateAndTransformA)) {
    u32 encodedFlags = 0u;
    CPlugTreeStateArchivePayload payload;
    payload.Reset(selectedPayload, treeNodeIndex, chunkId);
    return byteStream->ReadU32(&encodedFlags) &&
           payload.ReadStateAndTransform(
               byteStream,
               StaticSolidArchiveTreeStateFlags::FromCPlugTreeChunk0904f010(
                   encodedFlags)) &&
           payload.Install(store);
  }
  if (chunkId ==
      ArchiveChunkIdValue(CPlugTreeArchiveChunkId::StateAndTransformB)) {
    u32 encodedFlags = 0u;
    if (!byteStream->ReadU32(&encodedFlags) || !byteStream->Skip(4u)) {
      return 0;
    }
    CPlugTreeStateArchivePayload payload;
    payload.Reset(selectedPayload, treeNodeIndex, chunkId);
    return payload.ReadStateAndTransform(
               byteStream,
               StaticSolidArchiveTreeStateFlags::Low17Or2800(encodedFlags)) &&
           payload.Install(store);
  }
  if (chunkId ==
      ArchiveChunkIdValue(CPlugTreeArchiveChunkId::StateAndTransformC)) {
    u32 encodedFlags = 0u;
    CPlugTreeStateArchivePayload payload;
    payload.Reset(selectedPayload, treeNodeIndex, chunkId);
    return byteStream->ReadU32(&encodedFlags) &&
           payload.ReadStateAndTransform(
               byteStream,
               StaticSolidArchiveTreeStateFlags::FullOr2800(encodedFlags)) &&
           payload.Install(store);
  }
  if (chunkId == ArchiveChunkIdValue(
                     CPlugTreeArchiveChunkId::SourceSurfaceGeneratorRefs2)) {
    CPlugTreeVisualArchivePayload payload;
    payload.Reset(selectedPayload, treeNodeIndex, chunkId);
    return payload.ReadSurfaceGeneratorRefs(nodeRefs) &&
           payload.Install(context.archiveNodeGraph,
                           context.externalFolders,
                           sourceDescriptorPath,
                           store);
  }
  if (chunkId ==
      ArchiveChunkIdValue(CPlugTreeArchiveChunkId::StateAndTransformD)) {
    u32 encodedFlags = 0u;
    CPlugTreeStateArchivePayload payload;
    payload.Reset(selectedPayload, treeNodeIndex, chunkId);
    return byteStream->ReadU32(&encodedFlags) &&
           payload.ReadStateAndTransform(
               byteStream,
               StaticSolidArchiveTreeStateFlags::FullOr2000(encodedFlags)) &&
           payload.Install(store);
  }
  if (IsCPlugTreeIgnoredStandaloneChunk(chunkId)) {
    return 1;
  }
  if (chunkId == ArchiveChunkIdValue(
                     CPlugTreeGeneratedArchiveChunkId::LocalBoxProviderRef)) {
    CPlugTreeDiscardedRefArchivePayload payload;
    return payload.ReadSingleNodRef(nodeRefs);
  }
  if (chunkId ==
      ArchiveChunkIdValue(
          CPlugTreeGeneratedArchiveChunkId::IsoBoolParentSolidRefs)) {
    CPlugTreeStateArchivePayload payload;
    payload.Reset(selectedPayload, treeNodeIndex, chunkId);
    return payload.ReadHasIsoBool(byteStream, 1, nodeRefs) &&
           payload.Install(store);
  }
  if (chunkId ==
      ArchiveChunkIdValue(CPlugTreeGeneratedArchiveChunkId::IsoBoolOnly)) {
    CPlugTreeStateArchivePayload payload;
    payload.Reset(selectedPayload, treeNodeIndex, chunkId);
    return payload.ReadHasIsoBool(byteStream, 0, nodeRefs) &&
           payload.Install(store);
  }
  if (chunkId == ArchiveChunkIdValue(
                     CPlugTreeGeneratedArchiveChunkId::SurfaceOnlySource)) {
    CPlugTreeVisualArchivePayload payload;
    payload.Reset(selectedPayload, treeNodeIndex, chunkId);
    return payload.ReadSurfaceOnly(nodeRefs) &&
           payload.Install(context.archiveNodeGraph,
                           context.externalFolders,
                           sourceDescriptorPath,
                           store);
  }
  return 0;
}
