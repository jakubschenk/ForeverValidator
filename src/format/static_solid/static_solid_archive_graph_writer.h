#ifndef TMNF_STATIC_SOLID_ARCHIVE_GRAPH_WRITER_H
#define TMNF_STATIC_SOLID_ARCHIVE_GRAPH_WRITER_H

#include "engine/core/engine_types.h"
#include "engine/game/material_definition.h"
#include "format/archive/archive_node_reference.h"
#include "format/static_solid/static_solid_archive_id.h"
class CMwId;
class CGameCtnReplayStaticSolidArchiveGraph;

class CGameCtnReplayStaticSolidArchiveTreeSourceLocalRefs {
public:
    void Clear();
    void SetSurfaceRefs(
            ArchiveNodeReference visualNode,
            ArchiveNodeReference shaderNode,
            ArchiveNodeReference surfaceSourceNode);
    void SetSurfaceSource(
            ArchiveNodeReference surfaceSourceNode);
    void SetSurfaceAndGenerator(
            ArchiveNodeReference surfaceSourceNode,
            ArchiveNodeReference generatorNode);
    void SetSurfaceRefsAndGenerator(
            ArchiveNodeReference visualNode,
            ArchiveNodeReference shaderNode,
            ArchiveNodeReference surfaceSourceNode,
            ArchiveNodeReference generatorNode);
    void SetMaterialSurfaceGenerator(
            ArchiveNodeReference visualNode,
            ArchiveNodeReference shaderNode,
            ArchiveNodeReference materialNode,
            ArchiveNodeReference surfaceSourceNode,
            ArchiveNodeReference generatorNode);
    ArchiveNodeReference ShaderNode() const;
    ArchiveNodeReference MaterialNode() const;
    ArchiveNodeReference SurfaceSourceNode() const;

private:
    friend struct CGameCtnReplayStaticSolidArchiveGraphWriter;

    ArchiveNodeReference visualNode =
            ArchiveNodeReference::Invalid();
    ArchiveNodeReference shaderNode =
            ArchiveNodeReference::Invalid();
    ArchiveNodeReference materialNode =
            ArchiveNodeReference::Invalid();
    ArchiveNodeReference surfaceSourceNode =
            ArchiveNodeReference::Invalid();
    ArchiveNodeReference generatorNode =
            ArchiveNodeReference::Invalid();
};

struct CGameCtnReplayStaticSolidArchiveGraphWriter {
    CGameCtnReplayStaticSolidArchiveGraph *graph;
    StaticSolidArchiveId payload;

    CGameCtnReplayStaticSolidArchiveGraphWriter(
            CGameCtnReplayStaticSolidArchiveGraph *graph,
            StaticSolidArchiveId payload);

    int AppendNode(ArchiveNodeReference node,
                   u32 classId);
    void SetTreeId(ArchiveNodeReference treeNode,
                   const CMwId &treeId,
                   const char *treeIdName);
    int AddTreeChildLink(
            ArchiveNodeReference parentNode,
            ArchiveNodeReference childNode,
            u32 linkKind);
    int AddTreeSurfaceLink(
            ArchiveNodeReference treeNode,
            ArchiveNodeReference surfaceNode);
    int AddTreeSourceLink(
            ArchiveNodeReference treeNode,
            const CGameCtnReplayStaticSolidArchiveTreeSourceLocalRefs &refs,
            u32 chunkId);
    int AddSolidRootLink(
            ArchiveNodeReference solidNode,
            ArchiveNodeReference treeNode,
            u32 chunkId);
    int AddSurfaceGeometryLink(
            ArchiveNodeReference surfaceNode,
            ArchiveNodeReference geomNode,
            u32 materialEntryCount,
            u32 explicitMaterialRefCount,
            u32 defaultMaterialIdCount);
    int AddMaterialDefinition(
            ArchiveNodeReference materialNode,
            MaterialSurfaceDefinition surface);
};

#endif
