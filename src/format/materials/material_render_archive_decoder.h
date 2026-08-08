#pragma once

#include <memory>
#include <optional>

#include "engine/game/material_render_definition.h"

struct CPlugFilePack;
class MaterialTextureAssetSource;

std::optional<MaterialRenderDefinition> DecodeMaterialRenderArchive(
        const CPlugFilePack &pack,
        const char *materialPlainPath);

std::optional<MaterialRenderDefinition> DecodeMaterialRenderArchive(
        const CPlugFilePack &pack,
        const char *materialPlainPath,
        std::shared_ptr<const MaterialTextureAssetSource> textureSource);
