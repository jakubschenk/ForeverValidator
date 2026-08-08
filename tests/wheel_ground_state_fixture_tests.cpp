#include <forevervalidator/experimental/physics_sandbox.h>
#include <forevervalidator/native.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

using forevervalidator::Vector3;
using forevervalidator::experimental::PhysicsSandboxCarState;

bool IsFinite(const Vector3 &value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool IsNonzero(const Vector3 &value) {
    constexpr float tolerance = 1.0e-4f;
    return std::abs(value.x) > tolerance ||
           std::abs(value.y) > tolerance ||
           std::abs(value.z) > tolerance;
}

bool IsNearCar(const Vector3 &point, const Vector3 &carPosition) {
    const double x = static_cast<double>(point.x) - carPosition.x;
    const double y = static_cast<double>(point.y) - carPosition.y;
    const double z = static_cast<double>(point.z) - carPosition.z;
    constexpr double maximumWheelDistanceSquared = 25.0;
    return x * x + y * y + z * z <= maximumWheelDistanceSquared;
}

void ReportInvalidSlidingContact(const PhysicsSandboxCarState &car,
                                 std::size_t wheel,
                                 std::uint64_t timeMs) {
    const Vector3 &ground = car.wheelGroundPosition[wheel];
    std::cerr << "invalid sliding wheel export at " << timeMs
              << " ms, wheel=" << wheel
              << " hasSurface=" << car.wheelHasSurface[wheel]
              << " ground=(" << ground.x << ", " << ground.y << ", "
              << ground.z << ") car=(" << car.position.x << ", "
              << car.position.y << ", " << car.position.z << ")\n";
}

}  // namespace

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
        std::cerr << "pack source failed: " << source.Error().diagnostic
                  << '\n';
        return 1;
    }
    const ReplayIdentity identity{replayPath};
    Result<AssetBytes> replay = ReadNativeReplayFile(replayPath, identity);
    if (!replay) {
        std::cerr << "replay read failed: " << replay.Error().diagnostic
                  << '\n';
        return 1;
    }

    PhysicsSandboxOptions options;
    options.backend = SimulationBackend::OptimizedCpu;
    options.timelineMode = PhysicsSandboxTimelineMode::RecordedReplay;
    PhysicsSandboxResult<PhysicsSandbox> sandboxResult =
            CreatePhysicsSandbox(std::move(source).Value(), options);
    if (!sandboxResult) {
        std::cerr << "sandbox creation failed: "
                  << sandboxResult.Error().diagnostic << '\n';
        return 1;
    }
    PhysicsSandbox sandbox = std::move(sandboxResult).Value();
    const ByteView replayView{replay.Value().data(), replay.Value().size()};
    PhysicsSandboxResult<PhysicsSandboxStateView> loaded =
            sandbox.LoadReplay(replayView, identity);
    if (!loaded) {
        std::cerr << "replay load failed: " << loaded.Error().diagnostic
                  << ": " << loaded.Error().validationError.diagnostic
                  << '\n';
        return 1;
    }

    PhysicsSandboxStateView state = loaded.Value();
    std::uint64_t contactedWheelSamples = 0u;
    std::uint64_t slidingWheelSamples = 0u;
    std::uint64_t slidingContactSamples = 0u;
    std::uint64_t slidingContactSurfaceSamples = 0u;
    std::uint64_t finiteGroundSamples = 0u;
    std::uint64_t nonzeroGroundSamples = 0u;
    std::uint64_t nearbyGroundSamples = 0u;
    std::uint64_t invalidSlidingContactSamples = 0u;
    for (;;) {
        for (std::size_t wheel = 0u;
             wheel < state.car.wheelContact.size(); ++wheel) {
            contactedWheelSamples += state.car.wheelContact[wheel] ? 1u : 0u;
            slidingWheelSamples += state.car.wheelSliding[wheel] ? 1u : 0u;
            if (!state.car.wheelContact[wheel] ||
                !state.car.wheelSliding[wheel]) {
                continue;
            }
            ++slidingContactSamples;
            const Vector3 &ground = state.car.wheelGroundPosition[wheel];
            const bool hasSurface = state.car.wheelHasSurface[wheel];
            const bool finite = IsFinite(ground);
            const bool nonzero = IsNonzero(ground);
            const bool nearby = IsNearCar(ground, state.car.position);
            slidingContactSurfaceSamples += hasSurface ? 1u : 0u;
            finiteGroundSamples += finite ? 1u : 0u;
            nonzeroGroundSamples += nonzero ? 1u : 0u;
            nearbyGroundSamples += nearby ? 1u : 0u;
            if (!hasSurface || !finite || !nonzero || !nearby) {
                if (invalidSlidingContactSamples < 8u) {
                    ReportInvalidSlidingContact(
                            state.car, wheel, state.timeMs);
                }
                ++invalidSlidingContactSamples;
            }
        }

        if (state.timeMs >= state.durationMs) {
            break;
        }
        PhysicsSandboxResult<PhysicsSandboxStateView> advanced =
                sandbox.AdvanceTicks(1u);
        if (!advanced) {
            std::cerr << "tick advance failed at " << state.timeMs
                      << " ms: " << advanced.Error().diagnostic << '\n';
            return 1;
        }
        state = advanced.Value();
    }

    std::cout << "wheel export durationMs=" << state.durationMs
              << " contactSamples=" << contactedWheelSamples
              << " slidingSamples=" << slidingWheelSamples
              << " slidingContactSamples=" << slidingContactSamples
              << " hasSurfaceSamples=" << slidingContactSurfaceSamples
              << " finiteGroundSamples=" << finiteGroundSamples
              << " nonzeroGroundSamples=" << nonzeroGroundSamples
              << " nearbyGroundSamples=" << nearbyGroundSamples
              << " invalidSlidingContactSamples="
              << invalidSlidingContactSamples
              << '\n';
    if (slidingContactSamples == 0u) {
        std::cerr << "fixture did not produce a sliding wheel contact\n";
        return 1;
    }
    return invalidSlidingContactSamples == 0u ? 0 : 1;
}
