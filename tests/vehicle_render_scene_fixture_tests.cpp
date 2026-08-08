#include <forevervalidator/experimental/physics_sandbox.h>
#include <forevervalidator/native.h>

#include <cstddef>
#include <iostream>
#include <set>
#include <string>

int main(int argc, char **argv) {
    using namespace forevervalidator;
    using namespace forevervalidator::experimental;

    if (argc != 3) {
        std::cout << "skipped: pass <Packs directory> <Replay.Gbx>\n";
        return 77;
    }
    const std::string packs = argv[1];
    const std::string replayPath = argv[2];
    Result<AssetSource> source = OpenInstalledPackDirectory(packs);
    if (!source) {
        std::cerr << "pack source failed: "
                  << source.Error().diagnostic << '\n';
        return 1;
    }
    const ReplayIdentity identity{replayPath};
    Result<AssetBytes> replay = ReadNativeReplayFile(replayPath, identity);
    if (!replay) {
        std::cerr << "replay read failed: "
                  << replay.Error().diagnostic << '\n';
        return 1;
    }

    PhysicsSandboxOptions options;
    options.backend = SimulationBackend::OptimizedCpu;
    options.timelineMode = PhysicsSandboxTimelineMode::Canonical;
    options.simulationHorizonMs = options.tickDurationMs;
    PhysicsSandboxResult<PhysicsSandbox> sandboxResult =
            CreatePhysicsSandbox(std::move(source).Value(), options);
    if (!sandboxResult) {
        std::cerr << "sandbox creation failed: "
                  << sandboxResult.Error().diagnostic << '\n';
        return 1;
    }
    PhysicsSandbox sandbox = std::move(sandboxResult).Value();
    const ByteView replayView{
            replay.Value().data(), replay.Value().size()};
    PhysicsSandboxResult<PhysicsSandboxStateView> loaded =
            sandbox.LoadScenario(replayView, identity);
    if (!loaded) {
        std::cerr << "scenario load failed: "
                  << loaded.Error().diagnostic << ": "
                  << loaded.Error().validationError.diagnostic << '\n';
        return 1;
    }
    PhysicsSandboxResult<PhysicsSandboxRenderSceneHandle> sceneResult =
            sandbox.ReadVehicleRenderScene();
    if (!sceneResult) {
        std::cerr << "vehicle scene failed: "
                  << sceneResult.Error().diagnostic << '\n';
        return 1;
    }
    const PhysicsSandboxRenderSceneHandle &scene = sceneResult.Value();
    std::size_t authoredMaterialCount = 0u;
    std::size_t authoredBitmapCount = 0u;
    for (const PhysicsSandboxRenderMaterial &material : scene->materials) {
        if (!material.sourcePath.empty() || !material.modelPath.empty() ||
            !material.shaderPath.empty()) {
            authoredMaterialCount++;
        }
        for (const PhysicsSandboxMaterialBitmap &bitmap : material.bitmaps) {
            if (bitmap.textureAssetId != 0u) {
                authoredBitmapCount++;
            }
        }
    }
    std::size_t readableTextureCount = 0u;
    for (const PhysicsSandboxTextureAssetMetadata &texture :
         scene->textureAssets.Assets()) {
        PhysicsSandboxTextureAssetReadResult read =
                scene->textureAssets.Read(texture.id);
        if (read && read.Value() && !read.Value()->encodedBytes.empty()) {
            readableTextureCount++;
        }
    }
    std::cout << "vehicle scene meshes=" << scene->meshes.size()
              << " materials=" << scene->materials.size()
              << " authoredMaterials=" << authoredMaterialCount
              << " instances=" << scene->instances.size()
              << " textures=" << scene->textureAssets.Assets().size()
              << " authoredBitmaps=" << authoredBitmapCount
              << " readableTextures=" << readableTextureCount
              << " diagnostics=" << scene->diagnostics.size() << '\n';
    for (std::size_t materialIndex = 0u;
         materialIndex < scene->materials.size(); ++materialIndex) {
        const PhysicsSandboxRenderMaterial &material =
                scene->materials[materialIndex];
        std::cout << "material[" << materialIndex << "] source='"
                  << material.sourcePath << "' model='"
                  << material.modelPath << "' shader='"
                  << material.shaderPath << "' bitmaps="
                  << material.bitmaps.size() << '\n';
    }
    std::set<std::string> diagnosticMessages;
    for (const PhysicsSandboxRenderDiagnostic &diagnostic :
         scene->diagnostics) {
        diagnosticMessages.insert(diagnostic.message);
    }
    for (const std::string &message : diagnosticMessages) {
        std::cout << "diagnostic: " << message << '\n';
    }
    return !scene->meshes.empty() && !scene->instances.empty() &&
                   authoredMaterialCount >= 2u && authoredBitmapCount != 0u &&
                   readableTextureCount != 0u
            ? 0
            : 1;
}
