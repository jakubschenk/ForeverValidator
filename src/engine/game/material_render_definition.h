#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/engine_types.h"

class MaterialTextureAssetSource;

struct MaterialRenderBitmapDefinition {
    std::string samplerName;
    std::string plainPath;
    std::string selectedPath;
    u32 bitmapClassId = 0u;
    u32 renderClassId = 0u;
    std::string imagePlainPath;
    std::string imageSelectedPath;
    u32 imageClassId = 0u;
    std::size_t imageEncodedByteCount = 0u;
    std::shared_ptr<const MaterialTextureAssetSource> imageSource;
    std::string imageDiagnostic;
};

class MaterialRenderDefinition {
public:
    bool HasBitmapRenderWater(void) const;
    const std::string &MaterialPlainPath(void) const;
    const std::string &MaterialSelectedPath(void) const;
    const std::string &MaterialModelPlainPath(void) const;
    const std::string &MaterialModelSelectedPath(void) const;
    const std::string &ShaderPlainPath(void) const;
    const std::string &ShaderSelectedPath(void) const;
    const std::vector<MaterialRenderBitmapDefinition> &Bitmaps(void) const;
    u32 ShaderFlags(void) const;
    u32 ShaderRenderDeviceWord(void) const;

    void SetMaterialPaths(std::string plainPath,
                          std::string selectedPath);
    void SetMaterialModelPaths(std::string plainPath,
                               std::string selectedPath);
    void SetShaderPaths(std::string plainPath,
                        std::string selectedPath);
    void SetShaderFlags(u32 flags);
    void SetShaderRenderDeviceWord(u32 deviceWord);
    bool AppendBitmap(MaterialRenderBitmapDefinition bitmap);
    void ReplaceBitmaps(
            std::vector<MaterialRenderBitmapDefinition> bitmaps);

private:
    std::string materialPlainPath_;
    std::string materialSelectedPath_;
    std::string materialModelPlainPath_;
    std::string materialModelSelectedPath_;
    std::string shaderPlainPath_;
    std::string shaderSelectedPath_;
    std::vector<MaterialRenderBitmapDefinition> bitmaps_;
    u32 shaderFlags_ = 0u;
    u32 shaderRenderDeviceWord_ = 0u;
};
