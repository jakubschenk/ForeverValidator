#ifndef TMNF_STATIC_SOLID_ARCHIVE_TREE_READER_H
#define TMNF_STATIC_SOLID_ARCHIVE_TREE_READER_H

#include "engine/core/engine_types.h"
#include "engine/rendering/plug_tree.h"
#include <string>
#include <vector>

#include "format/archive/archive_node_reference.h"
#include "format/static_solid/static_solid_archive_definitions.h"
#include "format/static_solid/static_solid_archive_graph_writer.h"
#include "format/static_solid/static_solid_archive_id.h"
#include "format/static_solid/static_solid_archive_node_ref_reader.h"
class CGameCtnReplayStaticSolidArchiveByteStream;
class CGameCtnReplayStaticSolidArchiveNodeGraph;
struct CGameCtnReplayStaticSolidArchiveCMwIdState;
struct CGameCtnReplayStaticSolidArchiveDecodeProgress;
class StaticSolidArchiveLoadSession;
struct CGameCtnReplayStaticSolidArchiveChunkDispatchContext;
struct SceneDescriptorFolderPaths;

class CPlugTreeStateArchivePayload {
public:
  void Reset(StaticSolidArchiveId payload, u32 treeNodeIndex, u32 chunkId);
  int ReadStateAndTransform(
      CGameCtnReplayStaticSolidArchiveByteStream *byteStream,
      u32 encodedStateFlags);
  int ReadHasIsoBool(CGameCtnReplayStaticSolidArchiveByteStream *byteStream,
                     int withParentRefs,
                     CGameCtnReplayStaticSolidArchiveNodeRefReader *nodeRefs);
  int Install(StaticSolidArchiveLoadSession *store) const;

private:
  static constexpr u32 HasLocalIsoFlag = 4u;

  StaticSolidArchiveId payload = StaticSolidArchiveId::Invalid();
  u32 treeNodeIndex = 0xffffffffu;
  u32 chunkId = 0u;
  CGameCtnReplayStaticSolidArchiveTreeStateDefinition::Scope stateScope =
      CGameCtnReplayStaticSolidArchiveTreeStateDefinition::Scope::Complete;
  CPlugTree::SFlags state{};
  u32 hasLocalIso = 0u;
  float localIso[12] = {};

  void ClearLocalIso();
  int ReadLocalIsoIfPresent(
      CGameCtnReplayStaticSolidArchiveByteStream *byteStream);
};

class CPlugTreeVisualArchivePayload {
public:
  void Reset(StaticSolidArchiveId payload, u32 treeNodeIndex, u32 chunkId);
  int ReadSurfaceRefs(CGameCtnReplayStaticSolidArchiveNodeRefReader *nodeRefs);
  int ReadSurfaceGeneratorRefs(
      CGameCtnReplayStaticSolidArchiveNodeRefReader *nodeRefs);
  int ReadMaterialSurfaceGeneratorRefs(
      CGameCtnReplayStaticSolidArchiveNodeRefReader *nodeRefs);
  int ReadSurfaceOnly(CGameCtnReplayStaticSolidArchiveNodeRefReader *nodeRefs);
  int Install(
      CGameCtnReplayStaticSolidArchiveNodeGraph *archiveNodeGraph,
      const SceneDescriptorFolderPaths *externalFolders,
      const char *sourceDescriptorPath,
      StaticSolidArchiveLoadSession *store) const;

private:
  StaticSolidArchiveId payload = StaticSolidArchiveId::Invalid();
  ArchiveNodeReference treeNode = ArchiveNodeReference::Invalid();
  u32 chunkId = 0u;
  CGameCtnReplayStaticSolidArchiveTreeSourceLocalRefs treeSourceRefs;
};

class CPlugTreeChildLinksArchivePayload {
public:
  void Reset(StaticSolidArchiveId payload, u32 parentTreeNodeIndex,
             u32 linkKind);
  int ReadTreeChildFastBuffer(
      CGameCtnReplayStaticSolidArchiveByteStream *byteStream,
      CGameCtnReplayStaticSolidArchiveNodeRefReader *nodeRefs);
  int ReadVisualMipChildLinks(
      CGameCtnReplayStaticSolidArchiveByteStream *byteStream,
      CGameCtnReplayStaticSolidArchiveNodeRefReader *nodeRefs);
  int Install(CGameCtnReplayStaticSolidArchiveDecodeProgress *decodeProgress,
              StaticSolidArchiveLoadSession *store) const;

private:
  static constexpr u32 MaxChildLinkCount = 0x100000u;

  StaticSolidArchiveId payload = StaticSolidArchiveId::Invalid();
  ArchiveNodeReference parentTreeNode = ArchiveNodeReference::Invalid();
  u32 linkKind = 0u;
  u32 progressLinkCount = 0u;
  std::vector<ArchiveNodeReference> childNodes;

  int AppendChildLink(ArchiveNodeReference childNode);
};

class CPlugTreeIdArchivePayload {
public:
  void Reset(StaticSolidArchiveId payload, u32 treeNodeIndex);
  int Read(CGameCtnReplayStaticSolidArchiveByteStream *byteStream,
           CGameCtnReplayStaticSolidArchiveCMwIdState *cmwIdState,
           CGameCtnReplayStaticSolidArchiveNodeRefReader *nodeRefs);
  int Install(StaticSolidArchiveLoadSession *store) const;

private:
  StaticSolidArchiveId payload = StaticSolidArchiveId::Invalid();
  ArchiveNodeReference treeNode = ArchiveNodeReference::Invalid();
  CMwId treeId;
  ArchiveNodeReference objectNode = ArchiveNodeReference::Invalid();
  std::string treeIdName;
};

class CPlugTreeDiscardedRefArchivePayload {
public:
  int ReadSingleNodRef(CGameCtnReplayStaticSolidArchiveNodeRefReader *nodeRefs);
};

struct CGameCtnReplayStaticSolidArchiveTreeReader {
  static int ParseTreeChunk(
      const CGameCtnReplayStaticSolidArchiveChunkDispatchContext &context,
      u32 classId, u32 chunkId);
};

#endif
