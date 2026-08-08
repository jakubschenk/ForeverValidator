#include "format/static_solid/static_solid_archive_graph_writer.h"
#include "format/static_solid/static_solid_archive_graph.h"
namespace {

CGameCtnReplayStaticSolidArchiveNodeIdentity ArchiveNodeIdentity(
        StaticSolidArchiveId payload,
        ArchiveNodeReference node) {
    return CGameCtnReplayStaticSolidArchiveNodeIdentity::FromPayloadAndArchiveIndex(
            payload,
            node.Index());
}

}  // namespace

void CGameCtnReplayStaticSolidArchiveTreeSourceLocalRefs::Clear() {
    *this = CGameCtnReplayStaticSolidArchiveTreeSourceLocalRefs{};
}

void CGameCtnReplayStaticSolidArchiveTreeSourceLocalRefs::SetSurfaceRefs(
        ArchiveNodeReference
                newVisualNode,
        ArchiveNodeReference newShaderNode,
        ArchiveNodeReference newSurfaceSourceNode) {
    Clear();
    visualNode = newVisualNode;
    shaderNode = newShaderNode;
    surfaceSourceNode = newSurfaceSourceNode;
}

void CGameCtnReplayStaticSolidArchiveTreeSourceLocalRefs::SetSurfaceSource(
        ArchiveNodeReference newSurfaceSourceNode) {
    Clear();
    surfaceSourceNode = newSurfaceSourceNode;
}

void CGameCtnReplayStaticSolidArchiveTreeSourceLocalRefs::
        SetSurfaceAndGenerator(
                ArchiveNodeReference
                        newSurfaceSourceNode,
                ArchiveNodeReference
                        newGeneratorNode) {
    Clear();
    surfaceSourceNode = newSurfaceSourceNode;
    generatorNode = newGeneratorNode;
}

void CGameCtnReplayStaticSolidArchiveTreeSourceLocalRefs::
        SetSurfaceRefsAndGenerator(
                ArchiveNodeReference
                        newVisualNode,
                ArchiveNodeReference newShaderNode,
                ArchiveNodeReference
                        newSurfaceSourceNode,
                ArchiveNodeReference
                        newGeneratorNode) {
    Clear();
    visualNode = newVisualNode;
    shaderNode = newShaderNode;
    surfaceSourceNode = newSurfaceSourceNode;
    generatorNode = newGeneratorNode;
}

void CGameCtnReplayStaticSolidArchiveTreeSourceLocalRefs::
        SetMaterialSurfaceGenerator(
                ArchiveNodeReference
                        newVisualNode,
                ArchiveNodeReference newShaderNode,
                ArchiveNodeReference newMaterialNode,
                ArchiveNodeReference
                        newSurfaceSourceNode,
                ArchiveNodeReference
                        newGeneratorNode) {
    Clear();
    visualNode = newVisualNode;
    shaderNode = newShaderNode;
    materialNode = newMaterialNode;
    surfaceSourceNode = newSurfaceSourceNode;
    generatorNode = newGeneratorNode;
}

ArchiveNodeReference
CGameCtnReplayStaticSolidArchiveTreeSourceLocalRefs::ShaderNode()
        const {
    return shaderNode;
}

ArchiveNodeReference
CGameCtnReplayStaticSolidArchiveTreeSourceLocalRefs::MaterialNode()
        const {
    return materialNode;
}

ArchiveNodeReference
CGameCtnReplayStaticSolidArchiveTreeSourceLocalRefs::SurfaceSourceNode()
        const {
    return surfaceSourceNode;
}

CGameCtnReplayStaticSolidArchiveGraphWriter::
CGameCtnReplayStaticSolidArchiveGraphWriter(
        CGameCtnReplayStaticSolidArchiveGraph *newGraph,
        StaticSolidArchiveId newPayload)
    : graph(newGraph),
      payload(newPayload) {
}

int CGameCtnReplayStaticSolidArchiveGraphWriter::AppendNode(
        ArchiveNodeReference nodeRef,
        u32 classId) {
    if (graph == nullptr || !payload.IsValid() || !nodeRef.IsValid()) {
        return 1;
    }
    CGameCtnReplayStaticSolidArchiveNode node;
    node.Install(ArchiveNodeIdentity(payload, nodeRef), classId);
    return graph->Nodes().Add(node);
}

void CGameCtnReplayStaticSolidArchiveGraphWriter::SetTreeId(
        ArchiveNodeReference treeNode,
        const CMwId &treeId,
        const char *treeIdName) {
    if (graph == nullptr || !payload.IsValid() || !treeNode.IsValid()) {
        return;
    }
    graph->Nodes().SetTreeId(ArchiveNodeIdentity(payload, treeNode),
                             treeId,
                             treeIdName);
}

int CGameCtnReplayStaticSolidArchiveGraphWriter::AddTreeChildLink(
        ArchiveNodeReference parentNode,
        ArchiveNodeReference childNode,
        u32 linkKind) {
    if (graph == nullptr || !payload.IsValid() ||
        !parentNode.IsValid() || !childNode.IsValid()) {
        return 1;
    }
    CGameCtnReplayStaticSolidArchiveTreeChildLink link;
    link.Install(ArchiveNodeIdentity(payload, parentNode),
                 ArchiveNodeIdentity(payload, childNode),
                 linkKind);
    return graph->TreeGraph().AddChildLink(link);
}

int CGameCtnReplayStaticSolidArchiveGraphWriter::AddTreeSurfaceLink(
        ArchiveNodeReference treeNode,
        ArchiveNodeReference surfaceNode) {
    if (graph == nullptr || !payload.IsValid() ||
        !treeNode.IsValid() || !surfaceNode.IsValid()) {
        return 1;
    }
    CGameCtnReplayStaticSolidArchiveTreeSurfaceLink link;
    link.Install(ArchiveNodeIdentity(payload, treeNode),
                 ArchiveNodeIdentity(payload, surfaceNode));
    return graph->TreeGraph().AddTreeSurfaceLink(link);
}

int CGameCtnReplayStaticSolidArchiveGraphWriter::AddTreeSourceLink(
        ArchiveNodeReference treeNode,
        const CGameCtnReplayStaticSolidArchiveTreeSourceLocalRefs &refs,
        u32 chunkId) {
    if (graph == nullptr || !payload.IsValid() ||
        !treeNode.IsValid()) {
        return 1;
    }
    CGameCtnReplayStaticSolidArchiveTreeSourceLink link;
    link.Install(ArchiveNodeIdentity(payload, treeNode),
                 ArchiveNodeIdentity(payload, refs.visualNode),
                 ArchiveNodeIdentity(payload, refs.shaderNode),
                 ArchiveNodeIdentity(payload, refs.materialNode),
                 ArchiveNodeIdentity(payload, refs.surfaceSourceNode),
                 ArchiveNodeIdentity(payload, refs.generatorNode),
                 chunkId);
    return graph->TreeGraph().AddTreeSourceLink(link);
}

int CGameCtnReplayStaticSolidArchiveGraphWriter::AddSolidRootLink(
        ArchiveNodeReference solidNode,
        ArchiveNodeReference treeNode,
        u32 chunkId) {
    if (graph == nullptr || !payload.IsValid() ||
        !solidNode.IsValid() || !treeNode.IsValid()) {
        return 1;
    }
    CGameCtnReplayStaticSolidArchiveSolidTreeLink link;
    link.Install(ArchiveNodeIdentity(payload, solidNode),
                 ArchiveNodeIdentity(payload, treeNode),
                 chunkId);
    return graph->TreeGraph().AddSolidRootLink(link);
}

int CGameCtnReplayStaticSolidArchiveGraphWriter::AddSurfaceGeometryLink(
        ArchiveNodeReference surfaceNode,
        ArchiveNodeReference geomNode,
        u32 materialEntryCount,
        u32 explicitMaterialRefCount,
        u32 defaultMaterialIdCount) {
    if (graph == nullptr || !payload.IsValid() ||
        !surfaceNode.IsValid() || !geomNode.IsValid()) {
        return 1;
    }
    CGameCtnReplayStaticSolidArchiveSurfaceGeomLink link;
    link.Install(ArchiveNodeIdentity(payload, surfaceNode),
                 ArchiveNodeIdentity(payload, geomNode),
                 materialEntryCount,
                 explicitMaterialRefCount,
                 defaultMaterialIdCount);
    return graph->SurfaceGraph().AddSurfaceGeometryLink(link);
}

int CGameCtnReplayStaticSolidArchiveGraphWriter::AddMaterialDefinition(
        ArchiveNodeReference materialNode,
        MaterialSurfaceDefinition surface) {
    if (graph == nullptr || !payload.IsValid() ||
        !materialNode.IsValid()) {
        return 1;
    }
    CGameCtnReplayStaticSolidArchiveMaterialDefinition definition;
    definition.InstallEmbedded(ArchiveNodeIdentity(payload, materialNode),
                               surface);
    return graph->SurfaceGraph().AddMaterialDefinition(definition);
}
