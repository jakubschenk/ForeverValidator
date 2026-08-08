#include "format/static_solid/static_solid_archive_node_graph.h"
#include <string.h>

#include "format/archive/archive_class_ids.h"
#include <new>
int CGameCtnReplayStaticSolidArchiveNodeGraph::Node::IsPresent() const {
    return present != 0u;
}

int CGameCtnReplayStaticSolidArchiveNodeGraph::Node::IsExternal() const {
    return external != 0u;
}

int CGameCtnReplayStaticSolidArchiveNodeGraph::Node::IsLoadable() const {
    return loadable != 0u;
}

u32 CGameCtnReplayStaticSolidArchiveNodeGraph::Node::ClassId() const {
    return classId;
}

u32 CGameCtnReplayStaticSolidArchiveNodeGraph::Node::FolderIndex() const {
    return folderIndex;
}

const std::string &CGameCtnReplayStaticSolidArchiveNodeGraph::Node::Name()
        const {
    return name;
}

int CGameCtnReplayStaticSolidArchiveNodeGraph::Node::HasExternalName() const {
    return IsExternal() && !name.empty() && name[0] != '\0';
}

int CGameCtnReplayStaticSolidArchiveNodeGraph::Node::SetName(
        const std::string &newName) {
    try {
        const size_t cStringLength = strlen(newName.c_str());
        name.assign(newName.c_str(),
                    cStringLength < CGameCtnReplayStaticMobilModelNameCapacity
                            ? cStringLength
                            : CGameCtnReplayStaticMobilModelNameCapacity - 1u);
    } catch (const std::bad_alloc &) {
        return 0;
    }
    return 1;
}

void CGameCtnReplayStaticSolidArchiveNodeGraph::Node::SetHmsItemState(
        const HmsItemArchiveWords &newState) {
    hmsItemState = newState;
}

const std::optional<HmsItemArchiveWords> &
CGameCtnReplayStaticSolidArchiveNodeGraph::Node::HmsItemState() const {
    return hmsItemState;
}

void CGameCtnReplayStaticSolidArchiveNodeGraph::Node::MarkInline(
        u32 newClassId) {
    present = 1u;
    external = 0u;
    loadable = 0u;
    folderIndex = 0xffffffffu;
    classId = newClassId;
    hmsItemState.reset();
}

int CGameCtnReplayStaticSolidArchiveNodeGraph::Node::MarkExternal(
        u32 newLoadable,
        u32 newFolderIndex,
        const std::string &newName) {
    if (newName.find('\0') != std::string::npos) {
        return 0;
    }
    present = 1u;
    external = 1u;
    loadable = newLoadable;
    folderIndex = newFolderIndex;
    hmsItemState.reset();
    try {
        name = newName.c_str();
    } catch (const std::bad_alloc &) {
        return 0;
    }
    return 1;
}

void CGameCtnReplayStaticSolidArchiveNodeGraph::Node::SetClassId(
        u32 newClassId) {
    classId = newClassId;
}

CGameCtnReplayStaticSolidExternalNodeRef
CGameCtnReplayStaticSolidArchiveNodeGraph::Node::ExternalNodeRef() const {
    CGameCtnReplayStaticSolidExternalNodeRef ref{};
    ref.Install(present, external, folderIndex, name);
    return ref;
}

void CGameCtnReplayStaticSolidArchiveNodeGraph::MobilSolidLink::Install(
        ArchiveNodeReference newMobilNode,
        ArchiveNodeReference newSolidNode) {
    mobilNode = newMobilNode;
    solidNode = newSolidNode;
}

int CGameCtnReplayStaticSolidArchiveNodeGraph::MobilSolidLink::MatchesMobil(
        ArchiveNodeReference queryMobilNode) const {
    return mobilNode.Matches(queryMobilNode);
}

void CGameCtnReplayStaticSolidArchiveNodeGraph::MobilSolidLink::
        SetSolidNode(ArchiveNodeReference newSolidNode) {
    solidNode = newSolidNode;
}

ArchiveNodeReference
CGameCtnReplayStaticSolidArchiveNodeGraph::MobilSolidLink::SolidNode() const {
    return solidNode;
}

void CGameCtnReplayStaticSolidArchiveNodeGraph::SolidModelFidLink::Install(
        ArchiveNodeReference newSolidNode,
        ArchiveNodeReference newModelFidNode) {
    solidNode = newSolidNode;
    modelFidNode = newModelFidNode;
}

int CGameCtnReplayStaticSolidArchiveNodeGraph::SolidModelFidLink::MatchesSolid(
        ArchiveNodeReference querySolidNode) const {
    return solidNode.Matches(querySolidNode);
}

void CGameCtnReplayStaticSolidArchiveNodeGraph::SolidModelFidLink::
        SetModelFidNode(
                ArchiveNodeReference newModelFidNode) {
    modelFidNode = newModelFidNode;
}

ArchiveNodeReference
CGameCtnReplayStaticSolidArchiveNodeGraph::SolidModelFidLink::ModelFidNode()
        const {
    return modelFidNode;
}

void CGameCtnReplayStaticSolidArchiveNodeGraph::SceneMobilInternalRef::Install(
        ArchiveNodeReference newMobilNode) {
    mobilNode = newMobilNode;
}

ArchiveNodeReference
CGameCtnReplayStaticSolidArchiveNodeGraph::SceneMobilInternalRef::MobilNode()
        const {
    return mobilNode;
}

int CGameCtnReplayStaticSolidArchiveNodeGraph::EnsureNodeCapacity(u32 index) {
    if ((size_t)index < nodes.size()) {
        return 1;
    }
    size_t next = !nodes.empty() ? nodes.size() : 64u;
    while (next <= index) {
        if (next > 0x80000000u) {
            return 0;
        }
        next *= 2u;
    }
    try {
        nodes.resize(next);
    } catch (const std::bad_alloc &) {
        return 0;
    }
    return 1;
}

u32 CGameCtnReplayStaticSolidArchiveNodeGraph::NodeCount() const {
    return nodes.size() <= UINT32_MAX ? (u32)nodes.size() : UINT32_MAX;
}

int CGameCtnReplayStaticSolidArchiveNodeGraph::HasNode(
        ArchiveNodeReference nodeRef) const {
    const u32 index = nodeRef.Index();
    return (size_t)index < nodes.size() && nodes[index].IsPresent();
}

const CGameCtnReplayStaticSolidArchiveNodeGraph::Node *
CGameCtnReplayStaticSolidArchiveNodeGraph::FindNode(
        ArchiveNodeReference nodeRef) const {
    const u32 index = nodeRef.Index();
    return (size_t)index < nodes.size() ? &nodes[index] : nullptr;
}

int CGameCtnReplayStaticSolidArchiveNodeGraph::SetNodeName(
        ArchiveNodeReference nodeRef,
        const std::string &name) {
    const u32 index = nodeRef.Index();
    Node *node = (size_t)index < nodes.size() ? &nodes[index] : nullptr;
    if (node == nullptr) {
        return 0;
    }
    return node->SetName(name);
}

int CGameCtnReplayStaticSolidArchiveNodeGraph::RememberHmsItemState(
        ArchiveNodeReference nodeRef,
        const HmsItemArchiveWords &state) {
    const u32 index = nodeRef.Index();
    Node *node = (size_t)index < nodes.size() ? &nodes[index] : nullptr;
    if (node == nullptr) {
        return 0;
    }
    node->SetHmsItemState(state);
    return 1;
}

const HmsItemArchiveWords *
CGameCtnReplayStaticSolidArchiveNodeGraph::HmsItemStateForSceneMobil(
        ArchiveNodeReference nodeRef) const {
    const Node *node = FindNode(nodeRef);
    return node != nullptr && node->HmsItemState().has_value()
            ? &*node->HmsItemState()
            : nullptr;
}

void CGameCtnReplayStaticSolidArchiveNodeGraph::MarkInlineNode(
        ArchiveNodeReference nodeRef,
        u32 classId) {
    const u32 index = nodeRef.Index();
    Node *node = (size_t)index < nodes.size() ? &nodes[index] : nullptr;
    if (node == nullptr) {
        return;
    }
    node->MarkInline(classId);
}

int CGameCtnReplayStaticSolidArchiveNodeGraph::MarkExternalNode(
        ArchiveNodeReference nodeRef,
        u32 loadable,
        u32 folderIndex,
        const std::string &name) {
    const u32 index = nodeRef.Index();
    Node *node = (size_t)index < nodes.size() ? &nodes[index] : nullptr;
    if (node == nullptr) {
        return 0;
    }
    return node->MarkExternal(loadable, folderIndex, name);
}

void CGameCtnReplayStaticSolidArchiveNodeGraph::SetNodeClassId(
        ArchiveNodeReference nodeRef,
        u32 classId) {
    const u32 index = nodeRef.Index();
    Node *node = (size_t)index < nodes.size() ? &nodes[index] : nullptr;
    if (node != nullptr) {
        node->SetClassId(classId);
    }
}

CGameCtnReplayStaticSolidExternalNodeRef
CGameCtnReplayStaticSolidArchiveNodeGraph::ExternalNodeRef(
        ArchiveNodeReference nodeRef) const {
    CGameCtnReplayStaticSolidExternalNodeRef ref{};
    const Node *node = FindNode(nodeRef);
    if (node != nullptr) {
        ref = node->ExternalNodeRef();
    }
    return ref;
}

int CGameCtnReplayStaticSolidArchiveNodeGraph::CopyExternalNodeRefs(
        std::vector<CGameCtnReplayStaticSolidExternalNodeRef> *refsOut) const {
    if (refsOut == nullptr) {
        return 0;
    }
    try {
        refsOut->clear();
        refsOut->reserve(nodes.size());
        for (u32 i = 0u; i < NodeCount(); i++) {
            refsOut->push_back(ExternalNodeRef(
                    ArchiveNodeReference::
                            FromIndex(i)));
        }
    } catch (const std::bad_alloc &) {
        return 0;
    }
    return 1;
}

int CGameCtnReplayStaticSolidArchiveNodeGraph::EnsureSceneMobilRefCapacity(
        u32 index) {
    if ((size_t)index < sceneMobilInternalRefs.size()) {
        return 1;
    }
    size_t next = !sceneMobilInternalRefs.empty()
            ? sceneMobilInternalRefs.size()
            : 16u;
    while (next <= index) {
        if (next > 0x80000000u) {
            return 0;
        }
        next *= 2u;
    }
    try {
        sceneMobilInternalRefs.resize(next);
    } catch (const std::bad_alloc &) {
        return 0;
    }
    return 1;
}

int CGameCtnReplayStaticSolidArchiveNodeGraph::RememberSceneMobilRef(
        SceneMobilInternalRefKey ref,
        ArchiveNodeReference mobilNode) {
    if (!ref.IsStoredRef()) {
        return 1;
    }
    const u32 index = ref.StorageIndex();
    if (!EnsureSceneMobilRefCapacity(index)) {
        return 0;
    }
    sceneMobilInternalRefs[index].Install(mobilNode);
    return 1;
}

ArchiveNodeReference
CGameCtnReplayStaticSolidArchiveNodeGraph::SceneMobilRefNode(
        SceneMobilInternalRefKey ref) const {
    if (!ref.IsStoredRef() ||
        (size_t)ref.StorageIndex() >= sceneMobilInternalRefs.size()) {
        return ArchiveNodeReference::Invalid();
    }
    return sceneMobilInternalRefs[ref.StorageIndex()].MobilNode();
}

int CGameCtnReplayStaticSolidArchiveNodeGraph::RememberMobilSolidLink(
        ArchiveNodeReference mobilNode,
        ArchiveNodeReference solidNode) {
    if (!solidNode.IsValid()) {
        return 1;
    }
    for (MobilSolidLink &link : mobilSolidLinks) {
        if (link.MatchesMobil(mobilNode)) {
            link.SetSolidNode(solidNode);
            return 1;
        }
    }
    MobilSolidLink link;
    link.Install(mobilNode, solidNode);
    try {
        mobilSolidLinks.push_back(link);
    } catch (const std::bad_alloc &) {
        return 0;
    }
    return 1;
}

ArchiveNodeReference
CGameCtnReplayStaticSolidArchiveNodeGraph::SolidForSceneMobil(
        ArchiveNodeReference mobilNode) const {
    if (!mobilNode.IsValid()) {
        return ArchiveNodeReference::Invalid();
    }
    for (const MobilSolidLink &link : mobilSolidLinks) {
        if (link.MatchesMobil(mobilNode)) {
            return link.SolidNode();
        }
    }
    return ArchiveNodeReference::Invalid();
}

int CGameCtnReplayStaticSolidArchiveNodeGraph::RememberSolidModelFidLink(
        ArchiveNodeReference solidNode,
        ArchiveNodeReference modelFidNode) {
    if (!solidNode.IsValid() || !modelFidNode.IsValid()) {
        return 1;
    }
    for (SolidModelFidLink &link : solidModelFidLinks) {
        if (link.MatchesSolid(solidNode)) {
            link.SetModelFidNode(modelFidNode);
            return 1;
        }
    }
    SolidModelFidLink link;
    link.Install(solidNode, modelFidNode);
    try {
        solidModelFidLinks.push_back(link);
    } catch (const std::bad_alloc &) {
        return 0;
    }
    return 1;
}

ArchiveNodeReference
CGameCtnReplayStaticSolidArchiveNodeGraph::ModelFidForSolid(
        ArchiveNodeReference solidNode) const {
    if (!solidNode.IsValid()) {
        return ArchiveNodeReference::Invalid();
    }
    for (const SolidModelFidLink &link : solidModelFidLinks) {
        if (link.MatchesSolid(solidNode)) {
            return link.ModelFidNode();
        }
    }
    return ArchiveNodeReference::Invalid();
}

ArchiveNodeReference
CGameCtnReplayStaticSolidArchiveNodeGraph::TreeForSolid(
        const StaticSolidArchiveLoadSession *store,
        StaticSolidArchiveId payload,
        ArchiveNodeReference solidNode) const {
    if (store == nullptr) {
        return ArchiveNodeReference::Invalid();
    }
    const CGameCtnReplayStaticSolidArchiveNodeIdentity tree =
            store->ArchiveGraph().FindTreeForSolid(
            CGameCtnReplayStaticSolidArchiveNodeIdentity::FromPayloadAndArchiveIndex(
                    payload,
                    solidNode.Index()));
    return tree.ArchiveNode();
}

int CGameCtnReplayStaticSolidArchiveNodeGraph::BuildMaterialRef(
        StaticSolidArchiveId payload,
        ArchiveNodeReference materialNode,
        StaticSolidMaterialReference *materialRefOut) const {
    const Node *node = FindNode(materialNode);
    if (materialRefOut == nullptr ||
        !materialNode.IsValid() ||
        node == nullptr) {
        return 0;
    }
    const auto material =
            CGameCtnReplayStaticSolidArchiveNodeIdentity::
                    FromPayloadAndArchiveIndex(
                            payload,
                            materialNode.Index());
    if (node->ClassId() == TMNF_CLASS_CPlugMaterial) {
        materialRefOut->InstallEmbedded(material);
    } else if (node->IsExternal() && node->HasExternalName()) {
        materialRefOut->InstallExternal(material, node->Name());
    } else {
        materialRefOut->InstallUnsupported(material);
    }
    return 1;
}
