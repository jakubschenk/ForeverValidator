#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <forevervalidator/experimental/physics_sandbox.h>

#include "engine/game/material_render_definition.h"

namespace forevervalidator::experimental::texture_assets_internal {

struct PhysicsSandboxTextureAssetRegistration {
    PhysicsSandboxTextureAssetMetadata metadata;
    std::shared_ptr<const MaterialTextureAssetSource> source;
};

struct PhysicsSandboxTextureAssetResolverFactory {
    static PhysicsSandboxTextureAssetResolver Create(
            std::vector<PhysicsSandboxTextureAssetRegistration>
                    registrations);
};

class PhysicsSandboxTextureAssetRegistry {
public:
    PhysicsSandboxTextureAssetId Add(
            const MaterialRenderBitmapDefinition &bitmap,
            std::string *diagnostic = nullptr);
    PhysicsSandboxTextureAssetResolver Build() &&;

private:
    std::vector<PhysicsSandboxTextureAssetRegistration> registrations_;
    std::unordered_map<std::string, PhysicsSandboxTextureAssetId>
            idsByKey_;
    std::unordered_map<PhysicsSandboxTextureAssetId, std::string>
            keysById_;
};

}  // namespace forevervalidator::experimental::texture_assets_internal
