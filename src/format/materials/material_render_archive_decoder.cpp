#include "format/materials/material_render_archive_decoder.h"

#include <algorithm>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "format/archive/archive_binary.h"
#include "format/archive/archive_class_ids.h"
#include "format/archive/mw_id_archive_codec.h"
#include "format/archive/tmnf_archive_ids.h"
#include "format/archive/tmnf_gbx_body_reader.h"
#include "format/materials/material_archive_schema.h"
#include "format/pack/installed/byte_buffer.h"
#include "format/pack/installed/plug_file_pack.h"

namespace {

constexpr u32 CPlugMaterialCustomClassId = 0x0903a000u;
constexpr u32 CPlugMaterialCustomChunkIntArray = 0x0903a004u;
constexpr u32 CPlugMaterialCustomChunkBitmaps = 0x0903a006u;
constexpr u32 CPlugMaterialCustomChunkGpuFx = 0x0903a00au;
constexpr u32 CPlugMaterialCustomChunkBitmapSkip = 0x0903a00cu;
constexpr u32 CPlugMaterialCustomChunkFlags = 0x0903a00du;
constexpr u32 CPlugMaterialCustomChunkFloats = 0x0903a00fu;
constexpr u32 CPlugMaterialCustomChunkLegacySkip = 0x0903a011u;

constexpr u32 CPlugBitmapChunkImage14 = 0x09011014u;
constexpr u32 CPlugBitmapChunkImage15 = 0x09011015u;
constexpr u32 CPlugBitmapChunkImage18 = 0x09011018u;
constexpr u32 CPlugBitmapChunkFloat19 = 0x09011019u;
constexpr u32 CPlugBitmapChunkFlags1b = 0x0901101bu;
constexpr u32 CPlugBitmapChunkUv1c = 0x0901101cu;
constexpr u32 CPlugBitmapChunkShorts1d = 0x0901101du;
constexpr u32 CPlugBitmapChunkVec2Array1e = 0x0901101eu;
constexpr u32 CPlugBitmapChunkFlags1f = 0x0901101fu;
constexpr u32 CPlugBitmapChunkFloatArray20 = 0x09011020u;
constexpr u32 CPlugBitmapChunkWord21 = 0x09011021u;
constexpr u32 CPlugBitmapChunkImage22 = 0x09011022u;
constexpr u32 CPlugBitmapChunkWord23 = 0x09011023u;
constexpr u32 CPlugBitmapChunkFlags24 = 0x09011024u;
constexpr u32 CPlugBitmapChunkUv25 = 0x09011025u;
constexpr u32 CPlugBitmapChunkInt2_28 = 0x09011028u;
constexpr u32 CPlugBitmapChunkWord2e = 0x0901102eu;
constexpr u32 CPlugBitmapChunkEmpty30 = 0x09011030u;

constexpr u32 CPlugBitmapRenderChunkShorts03 = 0x09086003u;
constexpr u32 CPlugBitmapRenderChunkSkip08 = 0x09086008u;
constexpr u32 CPlugBitmapRenderChunkFloat0a = 0x0908600au;
constexpr u32 CPlugBitmapRenderChunkClear0b = 0x0908600bu;
constexpr u32 CPlugBitmapRenderChunkSub0c = 0x0908600cu;
constexpr u32 CPlugBitmapRenderChunkWord0d = 0x0908600du;
constexpr u32 CPlugBitmapRenderChunkWord0e = 0x0908600eu;
constexpr u32 CPlugBitmapRenderWaterChunkState05 = 0x09087005u;
constexpr u32 CPlugBitmapRenderWaterChunkState06 = 0x09087006u;

constexpr u32 CPlugFileGenClassId = 0x0902f000u;
constexpr u32 CPlugBitmapAddressChunkState04 = 0x09012004u;
constexpr u32 CPlugBitmapAddressChunkState07 = 0x09047007u;
constexpr u32 CPlugBitmapAddressChunkState09 = 0x09047009u;
constexpr u32 CPlugBitmapSamplerChunkState08 = 0x0907e008u;
constexpr u32 CPlugShaderChunkLegacyCustom = 0x0900200eu;
constexpr u32 CPlugShaderChunkRequirements = 0x09002016u;
constexpr u32 CPlugShaderGenericChunkMaterial = 0x09004003u;
constexpr u32 CPlugShaderApplyChunkTextureRefs = 0x09026002u;
constexpr u32 CPlugShaderApplyChunkField04 = 0x09026004u;
constexpr u32 CPlugShaderApplyChunkFields08 = 0x09026008u;
constexpr u32 CPlugShaderPassChunkBitmapSamplers = 0x09067006u;
constexpr u32 CPlugShaderPassChunkRenderState = 0x09067007u;
constexpr u32 CPlugShaderPassChunkPipelines = 0x0906700au;
constexpr u32 SkipBlockMarker = 0x534b4950u;
constexpr u32 MaxArchiveArrayCount = 0x100000u;
constexpr u32 MaxArchiveStringBytes = 0x10000u;

class ArchiveCursor {
public:
    ArchiveCursor(const unsigned char *bytes, u32 byteCount, u32 offset)
            : bytes_(bytes), byteCount_(byteCount), offset_(offset) {}

    bool ReadU32(u32 *out) {
        if (out == nullptr || Remaining() < 4u) {
            return false;
        }
        *out = TmnfFormat::ArchiveBinary::ReadU32LE(bytes_ + offset_);
        offset_ += 4u;
        return true;
    }

    bool PeekU32(u32 *out) const {
        if (out == nullptr || Remaining() < 4u) {
            return false;
        }
        *out = TmnfFormat::ArchiveBinary::ReadU32LE(bytes_ + offset_);
        return true;
    }

    bool ReadU8(unsigned char *out) {
        if (out == nullptr || Remaining() == 0u) {
            return false;
        }
        *out = bytes_[offset_++];
        return true;
    }

    bool ReadString(std::string *out) {
        u32 byteCount = 0u;
        if (out == nullptr || !ReadU32(&byteCount) ||
            byteCount > MaxArchiveStringBytes || byteCount > Remaining()) {
            return false;
        }
        try {
            out->assign(reinterpret_cast<const char *>(bytes_ + offset_),
                        byteCount);
        } catch (const std::bad_alloc &) {
            return false;
        }
        offset_ += byteCount;
        return true;
    }

    bool Skip(u32 byteCount) {
        if (byteCount > Remaining()) {
            return false;
        }
        offset_ += byteCount;
        return true;
    }

    bool SkipCounted(u32 count, u32 stride) {
        return stride != 0u && count <= UINT32_MAX / stride &&
               Skip(count * stride);
    }

    u32 Remaining(void) const {
        return offset_ <= byteCount_ ? byteCount_ - offset_ : 0u;
    }

    u32 Offset(void) const { return offset_; }

private:
    const unsigned char *bytes_ = nullptr;
    u32 byteCount_ = 0u;
    u32 offset_ = 0u;
};

enum class ArchiveIdEncodingMode : u32 {
    Unknown = 0u,
    TextTagged = 1u,
    InlineNames = 2u,
    SharedNames = 3u,
};

class ArchiveIdState {
public:
    bool ReadText(ArchiveCursor &cursor, std::string *out) {
        if (out == nullptr) {
            return false;
        }
        out->clear();
        u32 word = 0u;
        if (!cursor.ReadU32(&word)) {
            return false;
        }
        if (mode_ == ArchiveIdEncodingMode::Unknown &&
            word >= 1u && word <= 3u) {
            mode_ = static_cast<ArchiveIdEncodingMode>(word);
            if (!cursor.ReadU32(&word)) {
                return false;
            }
        }
        if (mode_ == ArchiveIdEncodingMode::TextTagged) {
            if (word == 0u) {
                return true;
            }
            return word <= 3u && cursor.ReadString(out);
        }

        const TmnfFormat::ArchiveIdentifierWord id =
                TmnfFormat::CMwIdArchiveCodec::ParseWord(word);
        if (!id.IsNamed()) {
            return true;
        }
        if (mode_ == ArchiveIdEncodingMode::SharedNames &&
            id.payload != 0u) {
            if (id.payload > sharedNames_.size()) {
                return false;
            }
            *out = sharedNames_[id.payload - 1u];
            return true;
        }
        if (mode_ == ArchiveIdEncodingMode::Unknown && id.payload != 0u) {
            return false;
        }
        if (!cursor.ReadString(out)) {
            return false;
        }
        if (mode_ == ArchiveIdEncodingMode::SharedNames) {
            if (sharedNames_.size() >= MaxArchiveArrayCount) {
                return false;
            }
            try {
                sharedNames_.push_back(*out);
            } catch (const std::bad_alloc &) {
                return false;
            }
        }
        return true;
    }

private:
    ArchiveIdEncodingMode mode_ = ArchiveIdEncodingMode::Unknown;
    std::vector<std::string> sharedNames_;
};

const GbxBodyExternalReference *ExternalReference(
        const GbxBodyReferenceTable &references,
        u32 nodeIndex) {
    for (const GbxBodyExternalReference &reference :
         references.externalReferences) {
        if (reference.nodeIndex == nodeIndex) {
            return &reference;
        }
    }
    return nullptr;
}

bool ReadSizedBlock(ArchiveCursor &cursor, u32 expectedByteCount) {
    u32 marker = 0u;
    u32 byteCount = 0u;
    return cursor.ReadU32(&marker) && marker == SkipBlockMarker &&
           cursor.ReadU32(&byteCount) && byteCount == expectedByteCount &&
           cursor.Skip(byteCount);
}

bool SkipU32Array(ArchiveCursor &cursor) {
    u32 count = 0u;
    return cursor.ReadU32(&count) && count <= MaxArchiveArrayCount &&
           cursor.SkipCounted(count, 4u);
}

bool SkipVec4Array(ArchiveCursor &cursor) {
    u32 count = 0u;
    return cursor.ReadU32(&count) && count <= MaxArchiveArrayCount &&
           cursor.SkipCounted(count, 16u);
}

struct NodeReference {
    u32 index = UINT32_MAX;
    const GbxBodyExternalReference *external = nullptr;

    bool IsNull(void) const { return index == UINT32_MAX; }
    bool IsExternal(void) const { return external != nullptr; }
};

bool ReadNodeReference(ArchiveCursor &cursor,
                       const GbxBodyReferenceTable &references,
                       NodeReference *out) {
    u32 index = 0u;
    if (out == nullptr || !cursor.ReadU32(&index) || index == 0u ||
        (index != UINT32_MAX && index > references.nodeCount)) {
        return false;
    }
    out->index = index;
    out->external = index != UINT32_MAX
            ? ExternalReference(references, index)
            : nullptr;
    return true;
}

bool ParseCPlugFileGen(ArchiveCursor &cursor,
                       const GbxBodyReferenceTable &references) {
    u32 archivedVersion = 0u;
    if (!cursor.ReadU32(&archivedVersion)) {
        return false;
    }
    u32 genKind = archivedVersion;
    if ((archivedVersion & 0x80000000u) != 0u) {
        const u32 version = archivedVersion & 0x7fffffffu;
        if (version > 5u || !cursor.ReadU32(&genKind) ||
            !SkipU32Array(cursor) || !SkipVec4Array(cursor) ||
            !SkipU32Array(cursor)) {
            return false;
        }
        if (version > 2u) {
            std::string ignored;
            if (!cursor.ReadString(&ignored)) {
                return false;
            }
        }
        if (version > 3u) {
            u32 count = 0u;
            if (!cursor.ReadU32(&count) || count > MaxArchiveArrayCount) {
                return false;
            }
            for (u32 index = 0u; index < count; ++index) {
                NodeReference reference;
                if (!ReadNodeReference(cursor, references, &reference) ||
                    (!reference.IsNull() && !reference.IsExternal())) {
                    return false;
                }
            }
        }
    } else {
        if (archivedVersion > 0x32u || !SkipU32Array(cursor) ||
            !SkipVec4Array(cursor)) {
            return false;
        }
    }
    return genKind != 9u;
}

bool ParseCPlugBitmapRenderWater(ArchiveCursor &cursor,
                                 const GbxBodyReferenceTable &references) {
    for (;;) {
        u32 chunk = 0u;
        if (!cursor.ReadU32(&chunk)) {
            return false;
        }
        if (chunk == CMwNodArchiveFacadeSentinel) {
            return true;
        }
        switch (chunk) {
        case CPlugBitmapRenderChunkShorts03:
            if (!cursor.Skip(8u)) {
                return false;
            }
            break;
        case CPlugBitmapRenderChunkSkip08:
            if (!ReadSizedBlock(cursor, 8u)) {
                return false;
            }
            break;
        case CPlugBitmapRenderChunkFloat0a:
        case CPlugBitmapRenderChunkWord0d:
        case CPlugBitmapRenderChunkWord0e:
            if (!cursor.Skip(4u)) {
                return false;
            }
            break;
        case CPlugBitmapRenderChunkClear0b: {
            NodeReference bitmap;
            if (!cursor.Skip(8u) ||
                !ReadNodeReference(cursor, references, &bitmap) ||
                (!bitmap.IsNull() && !bitmap.IsExternal()) ||
                !cursor.Skip(8u)) {
                return false;
            }
            break;
        }
        case CPlugBitmapRenderChunkSub0c: {
            NodeReference renderSub;
            if (!ReadNodeReference(cursor, references, &renderSub) ||
                (!renderSub.IsNull() && !renderSub.IsExternal())) {
                return false;
            }
            break;
        }
        case CPlugBitmapRenderWaterChunkState05:
            if (!ReadSizedBlock(cursor, 92u)) {
                return false;
            }
            break;
        case CPlugBitmapRenderWaterChunkState06:
            if (!ReadSizedBlock(cursor, 4u)) {
                return false;
            }
            break;
        default:
            return false;
        }
    }
}

bool ParseBitmapNodeReference(ArchiveCursor &cursor,
                               const GbxBodyReferenceTable &references,
                               bool *inlineImageOut,
                               const GbxBodyExternalReference **externalOut) {
    NodeReference image;
    if (inlineImageOut == nullptr || externalOut == nullptr ||
        !ReadNodeReference(cursor, references, &image)) {
        return false;
    }
    *inlineImageOut = !image.IsNull() && !image.IsExternal();
    *externalOut = image.external;
    if (image.IsNull() || image.IsExternal()) {
        return true;
    }
    u32 classId = 0u;
    return cursor.ReadU32(&classId) && classId == CPlugFileGenClassId &&
           ParseCPlugFileGen(cursor, references);
}

bool ParseBitmapRenderNodeReference(ArchiveCursor &cursor,
                                    const GbxBodyReferenceTable &references,
                                    u32 *renderClassIdOut) {
    NodeReference render;
    if (renderClassIdOut == nullptr ||
        !ReadNodeReference(cursor, references, &render)) {
        return false;
    }
    if (render.IsNull()) {
        *renderClassIdOut = 0u;
        return true;
    }
    if (render.IsExternal()) {
        return false;
    }
    u32 classId = 0u;
    if (!cursor.ReadU32(&classId) ||
        classId != TMNF_CLASS_CPlugBitmapRenderWater ||
        !ParseCPlugBitmapRenderWater(cursor, references)) {
        return false;
    }
    *renderClassIdOut = classId;
    return true;
}

void ResolveBitmapImageReference(
        const CPlugFilePack &pack,
        const GbxBodyReferenceTable &references,
        const char *bitmapPlainPath,
        const GbxBodyExternalReference &reference,
        MaterialRenderBitmapDefinition *out) {
    std::string plainPath;
    if (!references.ResolvePlainPathForReference(
                bitmapPlainPath, reference, &plainPath)) {
        out->imageDiagnostic =
                "CPlugBitmap image reference could not be resolved relative "
                "to " + std::string(bitmapPlainPath);
        return;
    }
    out->imagePlainPath = std::move(plainPath);

    char selectedPath[512]{};
    if (!pack.SelectedPathForPlainRef(
                out->imagePlainPath.c_str(),
                selectedPath,
                sizeof(selectedPath))) {
        // TMUF keeps many authored DDS/TGA files loose under GameData while
        // their CPlugBitmap wrappers live in the pack. Preserve the logical
        // path so the retained asset provider can resolve that loose file.
        out->imageSelectedPath = out->imagePlainPath;
        out->imageClassId = 0u;
        out->imageEncodedByteCount = 0u;
        out->imageDiagnostic.clear();
        return;
    }
    const CPlugFileFidContainer_SFileDesc *descriptor =
            pack.FindFileDescByPath(selectedPath);
    if (descriptor == nullptr) {
        out->imageDiagnostic =
                "CPlugBitmap image selected an unavailable pack path: " +
                std::string(selectedPath);
        return;
    }
    out->imageSelectedPath = selectedPath;
    out->imageClassId = descriptor->classId;
    out->imageEncodedByteCount = descriptor->uncompressedSize;
    out->imageDiagnostic.clear();
}

bool ParseCPlugBitmap(const unsigned char *bytes,
                      u32 byteCount,
                      const CPlugFilePack &pack,
                      const char *bitmapPlainPath,
                      MaterialRenderBitmapDefinition *out) {
    u32 classId = 0u;
    GbxBodyReferenceTable references;
    if (bytes == nullptr || bitmapPlainPath == nullptr || out == nullptr ||
        !GbxBodyOffsetReader::TryParseWithReferences(
                bytes, byteCount, &classId, &references) ||
        classId != TMNF_CLASS_CPlugBitmap) {
        return false;
    }
    ArchiveCursor cursor(bytes, byteCount, references.bodyOffset);
    u32 renderClassId = 0u;
    const GbxBodyExternalReference *externalImage = nullptr;
    bool hasInlineImage = false;
    for (;;) {
        u32 chunk = 0u;
        if (!cursor.ReadU32(&chunk)) {
            return false;
        }
        if (chunk == CMwNodArchiveFacadeSentinel) {
            if (cursor.Remaining() != 0u) {
                return false;
            }
            out->renderClassId = renderClassId;
            if (externalImage != nullptr) {
                ResolveBitmapImageReference(
                        pack,
                        references,
                        bitmapPlainPath,
                        *externalImage,
                        out);
            } else if (hasInlineImage) {
                out->imageDiagnostic =
                        "CPlugBitmap embeds a generated image; encoded "
                        "external image bytes are unavailable";
            }
            return true;
        }
        switch (chunk) {
        case CPlugBitmapChunkFloat19:
        case CPlugBitmapChunkFlags1b:
        case CPlugBitmapChunkFlags1f:
        case CPlugBitmapChunkWord21:
        case CPlugBitmapChunkWord23:
        case CPlugBitmapChunkFlags24:
        case CPlugBitmapChunkWord2e:
            if (!cursor.Skip(4u)) {
                return false;
            }
            break;
        case CPlugBitmapChunkUv1c:
            if (!cursor.Skip(20u)) {
                return false;
            }
            break;
        case CPlugBitmapChunkShorts1d:
            if (!cursor.Skip(4u)) {
                return false;
            }
            break;
        case CPlugBitmapChunkVec2Array1e: {
            u32 count = 0u;
            if (!cursor.ReadU32(&count) || count > MaxArchiveArrayCount ||
                !cursor.SkipCounted(count, 8u)) {
                return false;
            }
            break;
        }
        case CPlugBitmapChunkFloatArray20:
            if (!SkipU32Array(cursor)) {
                return false;
            }
            break;
        case CPlugBitmapChunkImage14:
        case CPlugBitmapChunkImage15:
        case CPlugBitmapChunkImage18:
        case CPlugBitmapChunkImage22: {
            bool hasInlineImageNode = false;
            const GbxBodyExternalReference *candidateImage = nullptr;
            if (!ParseBitmapNodeReference(cursor,
                                          references,
                                          &hasInlineImageNode,
                                          &candidateImage) ||
                !cursor.Skip(24u)) {
                return false;
            }
            if (candidateImage != nullptr) {
                externalImage = candidateImage;
            }
            hasInlineImage = hasInlineImage || hasInlineImageNode;
            u32 nextWord = 0u;
            const bool hasOptionalExternalRenderNode =
                    candidateImage != nullptr && cursor.PeekU32(&nextWord) &&
                    (nextWord == UINT32_MAX ||
                     (nextWord != 0u && nextWord <= references.nodeCount));
            if ((chunk == CPlugBitmapChunkImage18 ||
                 chunk == CPlugBitmapChunkImage22) &&
                (hasInlineImageNode || hasOptionalExternalRenderNode)) {
                u32 candidateRenderClassId = 0u;
                if (!ParseBitmapRenderNodeReference(
                            cursor, references, &candidateRenderClassId) ||
                    (renderClassId != 0u &&
                     candidateRenderClassId != 0u &&
                     renderClassId != candidateRenderClassId)) {
                    return false;
                }
                if (candidateRenderClassId != 0u) {
                    renderClassId = candidateRenderClassId;
                }
            }
            break;
        }
        case CPlugBitmapChunkUv25:
            if (!cursor.Skip(24u)) {
                return false;
            }
            break;
        case CPlugBitmapChunkInt2_28:
            if (!cursor.Skip(8u)) {
                return false;
            }
            break;
        case CPlugBitmapChunkEmpty30:
            break;
        default:
            return false;
        }
    }
}

bool DecodeExternalBitmap(
        const CPlugFilePack &pack,
        const GbxBodyReferenceTable &references,
        const char *ownerPlainPath,
        const GbxBodyExternalReference &reference,
        std::string samplerName,
        MaterialRenderBitmapDefinition *out) {
    if (ownerPlainPath == nullptr || out == nullptr) {
        return false;
    }
    std::string plainPath;
    if (!references.ResolvePlainPathForReference(
                ownerPlainPath, reference, &plainPath)) {
        return false;
    }
    char selectedPath[512]{};
    if (!pack.SelectedPathForPlainRef(
                plainPath.c_str(), selectedPath, sizeof(selectedPath))) {
        return false;
    }
    const CPlugFileFidContainer_SFileDesc *descriptor =
            pack.FindFileDescByPath(selectedPath);
    if (descriptor == nullptr ||
        descriptor->classId != TMNF_CLASS_CPlugBitmap) {
        return false;
    }
    MaterialRenderBitmapDefinition bitmap;
    bitmap.samplerName = std::move(samplerName);
    bitmap.plainPath = std::move(plainPath);
    bitmap.selectedPath = selectedPath;
    bitmap.bitmapClassId = descriptor->classId;
    ByteBuffer bitmapBytes;
    const bool extracted = pack.ExtractPathWithStreamFeedbackStrict(
            selectedPath, &bitmapBytes);
    const bool parsed = extracted && !bitmapBytes.Empty() &&
            bitmapBytes.Size() <= UINT32_MAX &&
            ParseCPlugBitmap(bitmapBytes.Data(),
                             static_cast<u32>(bitmapBytes.Size()),
                             pack,
                             bitmap.plainPath.c_str(),
                             &bitmap);
    if (!parsed) {
        if (!extracted) {
            bitmap.imageDiagnostic =
                    "CPlugBitmap archive could not be extracted from the "
                    "installed pack: " + bitmap.selectedPath;
        } else if (bitmapBytes.Empty()) {
            bitmap.imageDiagnostic =
                    "CPlugBitmap archive is empty: " + bitmap.selectedPath;
        } else if (bitmapBytes.Size() > UINT32_MAX) {
            bitmap.imageDiagnostic =
                    "CPlugBitmap archive is too large to decode: " +
                    bitmap.selectedPath;
        } else {
            bitmap.imageDiagnostic =
                    "CPlugBitmap archive format is unsupported; its encoded "
                    "image reference is unavailable: " + bitmap.plainPath;
        }
    }
    *out = std::move(bitmap);
    return true;
}

bool ParseGpuFxArray(ArchiveCursor &cursor, ArchiveIdState &ids) {
    u32 count = 0u;
    if (!cursor.ReadU32(&count) || count > MaxArchiveArrayCount) {
        return false;
    }
    for (u32 index = 0u; index < count; ++index) {
        std::string ignoredName;
        u32 componentCount = 0u;
        u32 registerCount = 0u;
        u32 ignoredBool = 0u;
        if (!ids.ReadText(cursor, &ignoredName) ||
            !cursor.ReadU32(&componentCount) ||
            !cursor.ReadU32(&registerCount) ||
            !cursor.ReadU32(&ignoredBool) || ignoredBool > 1u ||
            componentCount > MaxArchiveArrayCount ||
            registerCount > MaxArchiveArrayCount ||
            (componentCount != 0u &&
             registerCount > UINT32_MAX / componentCount) ||
            !cursor.SkipCounted(componentCount * registerCount, 4u)) {
            return false;
        }
    }
    return true;
}

bool ParseCPlugMaterialCustom(
        ArchiveCursor &cursor,
        const GbxBodyReferenceTable &references,
        const CPlugFilePack &pack,
        const char *materialPlainPath,
        MaterialRenderDefinition *definition) {
    ArchiveIdState ids;
    for (;;) {
        u32 chunk = 0u;
        if (!cursor.ReadU32(&chunk)) {
            return false;
        }
        if (chunk == CMwNodArchiveFacadeSentinel) {
            return true;
        }
        switch (chunk) {
        case CPlugMaterialCustomChunkIntArray:
            if (!SkipU32Array(cursor)) {
                return false;
            }
            break;
        case CPlugMaterialCustomChunkBitmaps: {
            u32 count = 0u;
            if (!cursor.ReadU32(&count) || count > MaxArchiveArrayCount) {
                return false;
            }
            for (u32 index = 0u; index < count; ++index) {
                std::string sampler;
                u32 ignored = 0u;
                NodeReference bitmap;
                MaterialRenderBitmapDefinition decodedBitmap;
                if (!ids.ReadText(cursor, &sampler) ||
                    !cursor.ReadU32(&ignored) ||
                    !ReadNodeReference(cursor, references, &bitmap) ||
                    bitmap.IsNull() || !bitmap.IsExternal() ||
                    !DecodeExternalBitmap(pack,
                                          references,
                                          materialPlainPath,
                                          *bitmap.external,
                                          std::move(sampler),
                                          &decodedBitmap) ||
                    !definition->AppendBitmap(std::move(decodedBitmap))) {
                    return false;
                }
            }
            break;
        }
        case CPlugMaterialCustomChunkGpuFx:
            if (!ParseGpuFxArray(cursor, ids) ||
                !ParseGpuFxArray(cursor, ids)) {
                return false;
            }
            break;
        case CPlugMaterialCustomChunkBitmapSkip: {
            u32 count = 0u;
            if (!cursor.ReadU32(&count) || count > MaxArchiveArrayCount) {
                return false;
            }
            for (u32 index = 0u; index < count; ++index) {
                std::string ignoredSampler;
                u32 ignoredBool = 0u;
                if (!ids.ReadText(cursor, &ignoredSampler) ||
                    !cursor.ReadU32(&ignoredBool) || ignoredBool > 1u) {
                    return false;
                }
            }
            break;
        }
        case CPlugMaterialCustomChunkFlags: {
            u32 lowFlags = 0u;
            if (!cursor.ReadU32(&lowFlags) || !cursor.Skip(12u) ||
                ((lowFlags & 1u) != 0u && !cursor.Skip(4u))) {
                return false;
            }
            break;
        }
        case CPlugMaterialCustomChunkFloats: {
            u32 marker = 0u;
            u32 byteCount = 0u;
            if (!cursor.ReadU32(&marker) || marker != SkipBlockMarker ||
                !cursor.ReadU32(&byteCount) || byteCount > cursor.Remaining() ||
                !cursor.Skip(byteCount)) {
                return false;
            }
            break;
        }
        case CPlugMaterialCustomChunkLegacySkip:
            if (!ReadSizedBlock(cursor, 4u)) {
                return false;
            }
            break;
        default:
            return false;
        }
    }
}

bool ReadExternalOrNullNodeReference(
        ArchiveCursor &cursor,
        const GbxBodyReferenceTable &references) {
    NodeReference reference;
    return ReadNodeReference(cursor, references, &reference) &&
           (reference.IsNull() || reference.IsExternal());
}

bool ReadExternalOrNullNodeReferenceArray(
        ArchiveCursor &cursor,
        const GbxBodyReferenceTable &references) {
    u32 count = 0u;
    if (!cursor.ReadU32(&count) || count > MaxArchiveArrayCount) {
        return false;
    }
    for (u32 index = 0u; index < count; ++index) {
        if (!ReadExternalOrNullNodeReference(cursor, references)) {
            return false;
        }
    }
    return true;
}

bool ParseShaderPassGpuPipeline(
        ArchiveCursor &cursor,
        ArchiveIdState &ids,
        const GbxBodyReferenceTable &references) {
    u32 enabled = 0u;
    NodeReference script;
    if (!cursor.ReadU32(&enabled) || enabled > 1u ||
        !ReadNodeReference(cursor, references, &script) ||
        (!script.IsNull() && !script.IsExternal())) {
        return false;
    }
    if (enabled == 0u) {
        return true;
    }
    u32 loadFxCount = 0u;
    if (!cursor.ReadU32(&loadFxCount) ||
        loadFxCount > MaxArchiveArrayCount) {
        return false;
    }
    for (u32 index = 0u; index < loadFxCount; ++index) {
        std::string ignoredId;
        if (!ids.ReadText(cursor, &ignoredId) || !cursor.Skip(16u)) {
            return false;
        }
    }
    return SkipVec4Array(cursor);
}

bool ParseCPlugShaderPass(
        ArchiveCursor &cursor,
        const GbxBodyReferenceTable &references) {
    ArchiveIdState ids;
    for (;;) {
        u32 chunk = 0u;
        if (!cursor.ReadU32(&chunk)) {
            return false;
        }
        if (chunk == CMwNodArchiveFacadeSentinel) {
            return true;
        }
        switch (chunk) {
        case CPlugShaderPassChunkBitmapSamplers:
            if (!ReadExternalOrNullNodeReferenceArray(
                        cursor, references)) {
                return false;
            }
            break;
        case CPlugShaderPassChunkRenderState:
            if (!cursor.Skip(4u)) {
                return false;
            }
            break;
        case CPlugShaderPassChunkPipelines: {
            u32 idCount = 0u;
            if (!cursor.ReadU32(&idCount) ||
                idCount > MaxArchiveArrayCount) {
                return false;
            }
            for (u32 index = 0u; index < idCount; ++index) {
                std::string ignoredId;
                if (!ids.ReadText(cursor, &ignoredId)) {
                    return false;
                }
            }
            if (!ParseShaderPassGpuPipeline(cursor, ids, references) ||
                !ParseShaderPassGpuPipeline(cursor, ids, references)) {
                return false;
            }
            break;
        }
        default:
            return false;
        }
    }
}

bool ReadShaderPassNodeReferenceArray(
        ArchiveCursor &cursor,
        const GbxBodyReferenceTable &references) {
    u32 count = 0u;
    if (!cursor.ReadU32(&count) || count > MaxArchiveArrayCount) {
        return false;
    }
    for (u32 index = 0u; index < count; ++index) {
        NodeReference pass;
        if (!ReadNodeReference(cursor, references, &pass)) {
            return false;
        }
        if (pass.IsNull() || pass.IsExternal()) {
            continue;
        }
        u32 classId = 0u;
        if (!cursor.ReadU32(&classId) ||
            classId != TMNF_CLASS_CPlugShaderPass ||
            !ParseCPlugShaderPass(cursor, references)) {
            return false;
        }
    }
    return true;
}

bool ParseLegacyCustomShader(
        ArchiveCursor &cursor,
        const GbxBodyReferenceTable &references) {
    return ReadExternalOrNullNodeReference(cursor, references) &&
           ReadShaderPassNodeReferenceArray(cursor, references) &&
           ReadExternalOrNullNodeReference(cursor, references) &&
           ReadExternalOrNullNodeReferenceArray(cursor, references);
}

struct ShaderBitmapAddressDefinition {
    std::string samplerName;
    const GbxBodyExternalReference *bitmap = nullptr;
};

bool ParseCPlugBitmapAddress(
        ArchiveCursor &cursor,
        const GbxBodyReferenceTable &references,
        ShaderBitmapAddressDefinition *out) {
    if (out == nullptr) {
        return false;
    }
    u32 chunk = 0u;
    ArchiveIdState ids;
    NodeReference bitmap;
    if (!cursor.ReadU32(&chunk) ||
        chunk != CPlugBitmapSamplerChunkState08 ||
        !ids.ReadText(cursor, &out->samplerName) ||
        !ReadNodeReference(cursor, references, &bitmap) ||
        bitmap.IsNull() || !bitmap.IsExternal() ||
        !cursor.Skip(8u) ||
        !cursor.ReadU32(&chunk) ||
        chunk != CPlugBitmapAddressChunkState07) {
        return false;
    }
    // State07 stores two opaque words followed by a UV transform variant.
    // It is not a node reference even though the second word is commonly 0.
    if (!cursor.Skip(8u)) {
        return false;
    }
    unsigned char transformKind = 0u;
    if (!cursor.ReadU8(&transformKind) || transformKind > 2u ||
        (transformKind == 1u && !cursor.Skip(24u)) ||
        (transformKind == 2u && !cursor.Skip(64u)) ||
        !cursor.ReadU32(&chunk) ||
        chunk != CPlugBitmapAddressChunkState09 ||
        !cursor.Skip(4u) ||
        !cursor.ReadU32(&chunk) ||
        chunk != CPlugBitmapAddressChunkState04 ||
        !cursor.Skip(4u) ||
        !cursor.ReadU32(&chunk) ||
        chunk != CMwNodArchiveFacadeSentinel) {
        return false;
    }
    out->bitmap = bitmap.external;
    return true;
}

bool SkipInlineCPlugShaderApply(
        ArchiveCursor &cursor,
        const GbxBodyReferenceTable &references) {
    u32 classId = 0u;
    if (!cursor.ReadU32(&classId) ||
        classId != TMNF_CLASS_CPlugShaderApply) {
        return false;
    }
    for (;;) {
        u32 chunk = 0u;
        if (!cursor.ReadU32(&chunk)) {
            return false;
        }
        if (chunk == CMwNodArchiveFacadeSentinel) {
            return true;
        }
        switch (chunk) {
        case CPlugShaderChunkLegacyCustom:
            if (!ParseLegacyCustomShader(cursor, references)) {
                return false;
            }
            break;
        case CPlugShaderChunkRequirements: {
            NodeReference requirement;
            if (!cursor.Skip(12u) ||
                !ReadNodeReference(cursor, references, &requirement) ||
                (!requirement.IsNull() && !requirement.IsExternal()) ||
                !cursor.Skip(2u)) {
                return false;
            }
            break;
        }
        case CPlugShaderGenericChunkMaterial:
            if (!cursor.Skip(0x58u)) {
                return false;
            }
            break;
        case CPlugShaderApplyChunkTextureRefs: {
            u32 count = 0u;
            if (!cursor.ReadU32(&count) || count > MaxArchiveArrayCount) {
                return false;
            }
            for (u32 index = 0u; index < count; ++index) {
                NodeReference address;
                u32 addressClassId = 0u;
                ShaderBitmapAddressDefinition ignored;
                if (!ReadNodeReference(cursor, references, &address) ||
                    address.IsNull() || address.IsExternal() ||
                    !cursor.ReadU32(&addressClassId) ||
                    addressClassId != TMNF_CLASS_CPlugBitmapApply ||
                    !ParseCPlugBitmapAddress(
                            cursor, references, &ignored)) {
                    return false;
                }
            }
            break;
        }
        case CPlugShaderApplyChunkField04:
            if (!cursor.Skip(4u)) {
                return false;
            }
            break;
        case CPlugShaderApplyChunkFields08:
            if (!cursor.Skip(8u)) {
                return false;
            }
            break;
        default:
            return false;
        }
    }
}

bool ReadCPlugShaderNodeReference(
        ArchiveCursor &cursor,
        const GbxBodyReferenceTable &references,
        std::vector<unsigned char> *internalNodesSeen,
        NodeReference *out) {
    if (internalNodesSeen == nullptr || out == nullptr ||
        !ReadNodeReference(cursor, references, out)) {
        return false;
    }
    if (out->IsNull() || out->IsExternal()) {
        return true;
    }
    if (out->index >= internalNodesSeen->size()) {
        return false;
    }
    if ((*internalNodesSeen)[out->index] != 0u) {
        return true;
    }
    (*internalNodesSeen)[out->index] = 1u;
    return SkipInlineCPlugShaderApply(cursor, references);
}

struct DecodedShaderGraph {
    u32 archiveFlags = 0u;
    std::vector<MaterialRenderBitmapDefinition> bitmaps;
};

bool ParseCPlugShaderApply(
        const unsigned char *bytes,
        u32 byteCount,
        const GbxBodyReferenceTable &references,
        const CPlugFilePack &pack,
        const char *shaderPlainPath,
        const std::vector<MaterialRenderBitmapDefinition> &customBitmaps,
        DecodedShaderGraph *out) {
    if (bytes == nullptr || shaderPlainPath == nullptr || out == nullptr) {
        return false;
    }
    ArchiveCursor cursor(bytes, byteCount, references.bodyOffset);
    bool requirementsParsed = false;
    bool textureRefsParsed = false;
    DecodedShaderGraph decoded;
    for (;;) {
        u32 chunk = 0u;
        if (!cursor.ReadU32(&chunk)) {
            return false;
        }
        if (chunk == CMwNodArchiveFacadeSentinel) {
            if (cursor.Remaining() != 0u || !requirementsParsed ||
                !textureRefsParsed) {
                return false;
            }
            *out = std::move(decoded);
            return true;
        }
        switch (chunk) {
        case CPlugShaderChunkLegacyCustom:
            if (!ParseLegacyCustomShader(cursor, references)) {
                return false;
            }
            break;
        case CPlugShaderChunkRequirements: {
            u32 requirementFlags = 0u;
            NodeReference requirementNode;
            if (requirementsParsed ||
                !cursor.ReadU32(&requirementFlags) ||
                !cursor.ReadU32(&decoded.archiveFlags) ||
                !cursor.Skip(4u) ||
                !ReadNodeReference(cursor, references, &requirementNode) ||
                (!requirementNode.IsNull() &&
                 !requirementNode.IsExternal()) ||
                !cursor.Skip(2u)) {
                return false;
            }
            (void)requirementFlags;
            requirementsParsed = true;
            break;
        }
        case CPlugShaderGenericChunkMaterial:
            if (!cursor.Skip(0x58u)) {
                return false;
            }
            break;
        case CPlugShaderApplyChunkTextureRefs: {
            u32 count = 0u;
            if (textureRefsParsed || !cursor.ReadU32(&count) ||
                count > MaxArchiveArrayCount) {
                return false;
            }
            for (u32 index = 0u; index < count; ++index) {
                NodeReference address;
                u32 addressClassId = 0u;
                ShaderBitmapAddressDefinition shaderBitmap;
                if (!ReadNodeReference(cursor, references, &address) ||
                    address.IsNull() || address.IsExternal() ||
                    !cursor.ReadU32(&addressClassId) ||
                    addressClassId != TMNF_CLASS_CPlugBitmapApply ||
                    !ParseCPlugBitmapAddress(
                            cursor, references, &shaderBitmap)) {
                    return false;
                }
                const auto custom = std::find_if(
                        customBitmaps.begin(),
                        customBitmaps.end(),
                        [&shaderBitmap](
                                const MaterialRenderBitmapDefinition &bitmap) {
                            return bitmap.samplerName ==
                                   shaderBitmap.samplerName;
                        });
                MaterialRenderBitmapDefinition bitmap;
                if (custom != customBitmaps.end()) {
                    bitmap = *custom;
                    bitmap.samplerName = shaderBitmap.samplerName;
                } else if (!DecodeExternalBitmap(
                                   pack,
                                   references,
                                   shaderPlainPath,
                                   *shaderBitmap.bitmap,
                                   shaderBitmap.samplerName,
                                   &bitmap)) {
                    return false;
                }
                try {
                    decoded.bitmaps.push_back(std::move(bitmap));
                } catch (const std::bad_alloc &) {
                    return false;
                }
            }
            textureRefsParsed = true;
            break;
        }
        case CPlugShaderApplyChunkField04:
            if (!cursor.Skip(4u)) {
                return false;
            }
            break;
        case CPlugShaderApplyChunkFields08:
            if (!cursor.Skip(8u)) {
                return false;
            }
            break;
        default:
            return false;
        }
    }
}

bool DecodeCPlugShaderApply(
        const CPlugFilePack &pack,
        const char *shaderPlainPath,
        const std::vector<MaterialRenderBitmapDefinition> &customBitmaps,
        std::string *selectedPathOut,
        DecodedShaderGraph *out) {
    if (shaderPlainPath == nullptr || selectedPathOut == nullptr ||
        out == nullptr) {
        return false;
    }
    char selectedPath[512]{};
    const CPlugFileFidContainer_SFileDesc *descriptor = nullptr;
    if (!pack.SelectedPathForPlainRef(
                shaderPlainPath, selectedPath, sizeof(selectedPath)) ||
        (descriptor = pack.FindFileDescByPath(selectedPath)) == nullptr ||
        descriptor->classId != TMNF_CLASS_CPlugShaderApply) {
        return false;
    }
    ByteBuffer shaderBytes;
    u32 classId = 0u;
    GbxBodyReferenceTable references;
    const bool extracted = pack.ExtractPathWithStreamFeedbackStrict(
            selectedPath, &shaderBytes);
    const bool referenced = extracted && !shaderBytes.Empty() &&
            shaderBytes.Size() <= UINT32_MAX &&
            GbxBodyOffsetReader::TryParseWithReferences(
                    shaderBytes.Data(), static_cast<u32>(shaderBytes.Size()),
                    &classId, &references);
    const bool parsed = referenced && classId == TMNF_CLASS_CPlugShaderApply &&
            ParseCPlugShaderApply(
                    shaderBytes.Data(), static_cast<u32>(shaderBytes.Size()),
                    references, pack, shaderPlainPath, customBitmaps, out);
    if (!extracted ||
        shaderBytes.Empty() || shaderBytes.Size() > UINT32_MAX ||
        !referenced || classId != TMNF_CLASS_CPlugShaderApply || !parsed) {
        return false;
    }
    try {
        *selectedPathOut = selectedPath;
    } catch (const std::bad_alloc &) {
        return false;
    }
    return true;
}

struct MaterialModelDeviceShader {
    u32 deviceWord = 0u;
    const GbxBodyExternalReference *shader = nullptr;
};

bool DecodeMaterialModelRenderGraph(
        const CPlugFilePack &pack,
        const char *modelPlainPath,
        const char *modelSelectedPath,
        MaterialRenderDefinition *definition) {
    if (modelPlainPath == nullptr || modelSelectedPath == nullptr ||
        definition == nullptr) {
        return false;
    }
    ByteBuffer modelBytes;
    u32 classId = 0u;
    GbxBodyReferenceTable references;
    if (!pack.ExtractPathWithStreamFeedbackStrict(
                modelSelectedPath, &modelBytes) ||
        modelBytes.Empty() || modelBytes.Size() > UINT32_MAX ||
        !GbxBodyOffsetReader::TryParseWithReferences(
                modelBytes.Data(),
                static_cast<u32>(modelBytes.Size()),
                &classId,
                &references) ||
        classId != TMNF_CLASS_CPlugMaterial) {
        return false;
    }

    ArchiveCursor cursor(modelBytes.Data(),
                         static_cast<u32>(modelBytes.Size()),
                         references.bodyOffset);
    std::vector<MaterialModelDeviceShader> deviceShaders;
    std::vector<unsigned char> internalShaderNodesSeen;
    bool deviceSetsParsed = false;
    try {
        internalShaderNodesSeen.resize(
                static_cast<std::size_t>(references.nodeCount) + 1u, 0u);
    } catch (const std::bad_alloc &) {
        return false;
    }
    for (;;) {
        u32 chunk = 0u;
        if (!cursor.ReadU32(&chunk)) {
            return false;
        }
        if (chunk == CMwNodArchiveFacadeSentinel) {
            if (cursor.Remaining() != 0u || !deviceSetsParsed) {
                return false;
            }
            break;
        }
        if (chunk == MaterialArchiveChunkValue(
                             MaterialArchiveChunk::MaterialFxRef) ||
            chunk == MaterialArchiveChunkValue(
                             MaterialArchiveChunk::CustomMaterialRef)) {
            if (!ReadExternalOrNullNodeReference(cursor, references)) {
                return false;
            }
            continue;
        }
        if (chunk == MaterialArchiveChunkValue(
                             MaterialArchiveChunk::
                                     DeviceSetsWithShaderRefsAndFormats)) {
            NodeReference parentModel;
            u32 count = 0u;
            if (deviceSetsParsed ||
                !ReadNodeReference(cursor, references, &parentModel) ||
                !parentModel.IsNull() || !cursor.ReadU32(&count) ||
                count > MaxArchiveArrayCount) {
                return false;
            }
            try {
                deviceShaders.reserve(count);
            } catch (const std::bad_alloc &) {
                return false;
            }
            for (u32 index = 0u; index < count; ++index) {
                MaterialModelDeviceShader entry;
                NodeReference shader;
                u32 activeShaderIsFid = 0u;
                u32 archivedDevice = 0u;
                if (!cursor.ReadU32(&archivedDevice) ||
                    !cursor.ReadU32(&activeShaderIsFid) ||
                    activeShaderIsFid > 1u) {
                    return false;
                }
                if (activeShaderIsFid != 0u) {
                    if (!ReadNodeReference(cursor, references, &shader) ||
                        (!shader.IsNull() && !shader.IsExternal())) {
                        return false;
                    }
                } else if (!ReadCPlugShaderNodeReference(
                                   cursor,
                                   references,
                                   &internalShaderNodesSeen,
                                   &shader)) {
                    return false;
                }
                if (!ReadExternalOrNullNodeReference(cursor, references) ||
                    !ReadExternalOrNullNodeReference(cursor, references)) {
                    return false;
                }
                // UPlugRenderDevice::Archive writes Nat16 device, Nat8
                // sub-device, then Nat8 quality. The game compares the
                // in-memory key quality|(subDevice<<8)|(device<<16).
                entry.deviceWord =
                        ((archivedDevice & 0x0000ffffu) << 16u) |
                        ((archivedDevice & 0x00ff0000u) >> 8u) |
                        ((archivedDevice & 0xff000000u) >> 24u);
                entry.shader = shader.external;
                deviceShaders.push_back(entry);
            }
            u32 formatCount = 0u;
            if (!cursor.ReadU32(&formatCount) ||
                formatCount > MaxArchiveArrayCount ||
                !cursor.SkipCounted(formatCount, 4u)) {
                return false;
            }
            deviceSetsParsed = true;
            continue;
        }
        if (chunk == MaterialArchiveChunkValue(
                             MaterialArchiveChunk::SurfaceFlags) ||
            chunk == MaterialArchiveChunkValue(
                             MaterialArchiveChunk::FourByteLegacyPayload)) {
            if (!cursor.Skip(4u)) {
                return false;
            }
            continue;
        }
        return false;
    }

    std::vector<MaterialRenderBitmapDefinition> customBitmaps;
    try {
        customBitmaps = definition->Bitmaps();
        definition->SetMaterialModelPaths(
                modelPlainPath, modelSelectedPath);
    } catch (const std::bad_alloc &) {
        return false;
    }
    definition->ReplaceBitmaps({});
    if (deviceShaders.empty()) {
        return false;
    }

    // CPlugMaterial::GetSupportedDeviceMatIndex clears the non-PC2 sub-device
    // before selecting the last archived key not newer than the supported
    // device. United's file default therefore compares as PC3/0/VHigh.
    constexpr u32 supportedDeviceWord = 0x00030004u;
    std::size_t selectedDevice = deviceShaders.size() - 1u;
    while (deviceShaders[selectedDevice].deviceWord > supportedDeviceWord &&
           selectedDevice != 0u) {
        --selectedDevice;
    }
    const MaterialModelDeviceShader &entry = deviceShaders[selectedDevice];
    {
        std::string shaderPlainPath;
        std::string shaderSelectedPath;
        DecodedShaderGraph graph;
        if (entry.shader == nullptr ||
            !references.ResolvePlainPathForReference(
                    modelPlainPath, *entry.shader, &shaderPlainPath) ||
            !DecodeCPlugShaderApply(pack,
                                    shaderPlainPath.c_str(),
                                    customBitmaps,
                                    &shaderSelectedPath,
                                    &graph)) {
            return false;
        }
        try {
            definition->SetShaderPaths(
                    std::move(shaderPlainPath),
                    std::move(shaderSelectedPath));
        } catch (const std::bad_alloc &) {
            return false;
        }
        definition->SetShaderFlags(graph.archiveFlags);
        definition->SetShaderRenderDeviceWord(entry.deviceWord);
        definition->ReplaceBitmaps(std::move(graph.bitmaps));
    }
    return true;
}

bool ParseMaterialRoot(
        const unsigned char *bytes,
        u32 byteCount,
        const GbxBodyReferenceTable &references,
        const CPlugFilePack &pack,
        const char *materialPlainPath,
        MaterialRenderDefinition *definition) {
    ArchiveCursor cursor(bytes, byteCount, references.bodyOffset);
    bool customParsed = false;
    std::string modelPlainPath;
    std::string modelSelectedPath;
    for (;;) {
        u32 chunk = 0u;
        if (!cursor.ReadU32(&chunk)) {
            return false;
        }
        if (chunk == CMwNodArchiveFacadeSentinel) {
            const bool graphDecoded =
                    cursor.Remaining() == 0u && customParsed &&
                    !modelPlainPath.empty() &&
                    DecodeMaterialModelRenderGraph(
                            pack,
                            modelPlainPath.c_str(),
                            modelSelectedPath.c_str(),
                            definition);
            return graphDecoded;
        }
        if (chunk == MaterialArchiveChunkValue(
                             MaterialArchiveChunk::MaterialFxRef)) {
            NodeReference materialFx;
            if (!ReadNodeReference(cursor, references, &materialFx) ||
                (!materialFx.IsNull() && !materialFx.IsExternal())) {
                return false;
            }
            continue;
        }
        if (chunk == MaterialArchiveChunkValue(
                             MaterialArchiveChunk::CustomMaterialRef)) {
            NodeReference custom;
            u32 customClassId = 0u;
            if (customParsed ||
                !ReadNodeReference(cursor, references, &custom) ||
                custom.IsNull() || custom.IsExternal() ||
                !cursor.ReadU32(&customClassId) ||
                customClassId != CPlugMaterialCustomClassId ||
                !ParseCPlugMaterialCustom(cursor,
                                          references,
                                          pack,
                                          materialPlainPath,
                                          definition)) {
                return false;
            }
            customParsed = true;
            continue;
        }
        if (chunk == MaterialArchiveChunkValue(
                             MaterialArchiveChunk::
                                     DeviceSetsWithShaderRefsAndFormats)) {
            NodeReference model;
            char selectedPath[512]{};
            if (!modelPlainPath.empty() ||
                !ReadNodeReference(cursor, references, &model) ||
                model.IsNull() || !model.IsExternal() ||
                !references.ResolvePlainPathForReference(
                        materialPlainPath,
                        *model.external,
                        &modelPlainPath) ||
                !pack.SelectedPathForPlainRef(
                        modelPlainPath.c_str(),
                        selectedPath,
                        sizeof(selectedPath))) {
                return false;
            }
            try {
                modelSelectedPath = selectedPath;
            } catch (const std::bad_alloc &) {
                return false;
            }
            continue;
        }
        if (chunk == MaterialArchiveChunkValue(
                             MaterialArchiveChunk::SurfaceFlags) ||
            chunk == MaterialArchiveChunkValue(
                             MaterialArchiveChunk::FourByteLegacyPayload)) {
            if (!cursor.Skip(4u)) {
                return false;
            }
            continue;
        }
        return false;
    }
}

}  // namespace

std::optional<MaterialRenderDefinition> DecodeMaterialRenderArchive(
        const CPlugFilePack &pack,
        const char *materialPlainPath) {
    return DecodeMaterialRenderArchive(pack, materialPlainPath, {});
}

std::optional<MaterialRenderDefinition> DecodeMaterialRenderArchive(
        const CPlugFilePack &pack,
        const char *materialPlainPath,
        std::shared_ptr<const MaterialTextureAssetSource> textureSource) {
    if (materialPlainPath == nullptr || materialPlainPath[0] == '\0') {
        return std::nullopt;
    }
    char selectedPath[512]{};
    ByteBuffer materialBytes;
    u32 classId = 0u;
    GbxBodyReferenceTable references;
    if (!pack.SelectedPathForPlainRef(materialPlainPath,
                                      selectedPath,
                                      sizeof(selectedPath)) ||
        !pack.ExtractPathWithStreamFeedbackStrict(
                selectedPath, &materialBytes) ||
        materialBytes.Empty() || materialBytes.Size() > UINT32_MAX ||
        !GbxBodyOffsetReader::TryParseWithReferences(
                materialBytes.Data(),
                static_cast<u32>(materialBytes.Size()),
                &classId,
                &references) ||
        classId != TMNF_CLASS_CPlugMaterial) {
        return std::nullopt;
    }

    MaterialRenderDefinition definition;
    try {
        definition.SetMaterialPaths(materialPlainPath, selectedPath);
    } catch (const std::bad_alloc &) {
        return std::nullopt;
    }
    if (!ParseMaterialRoot(materialBytes.Data(),
                           static_cast<u32>(materialBytes.Size()),
                           references,
                           pack,
                           materialPlainPath,
                           &definition)) {
        return std::nullopt;
    }
    if (textureSource) {
        try {
            std::vector<MaterialRenderBitmapDefinition> bitmaps =
                    definition.Bitmaps();
            for (MaterialRenderBitmapDefinition &bitmap : bitmaps) {
                if (!bitmap.imageSelectedPath.empty()) {
                    bitmap.imageSource = textureSource;
                }
            }
            definition.ReplaceBitmaps(std::move(bitmaps));
        } catch (const std::bad_alloc &) {
            return std::nullopt;
        }
    }
    return definition;
}
