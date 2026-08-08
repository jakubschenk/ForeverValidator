#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include <forevervalidator/validation.h>

#include "engine/game/material_definition.h"
struct CPlugFilePack;

class MaterialPackRepository {
public:
    explicit MaterialPackRepository(CPlugFilePack &pack);
    explicit MaterialPackRepository(
            std::shared_ptr<const CPlugFilePack> pack);
    MaterialPackRepository(
            std::shared_ptr<const CPlugFilePack> pack,
            forevervalidator::AssetProvider looseAssetProvider);
    ~MaterialPackRepository();

    MaterialPackRepository(const MaterialPackRepository &) = delete;
    MaterialPackRepository &operator=(const MaterialPackRepository &) = delete;

    std::optional<ResolvedMaterialDefinition> Resolve(
            std::string_view identifier);
    std::optional<ResolvedMaterialDefinition> ResolvePath(
            std::string_view plainPath);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
