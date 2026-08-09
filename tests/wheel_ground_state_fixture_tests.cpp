#include <forevervalidator/experimental/physics_sandbox.h>
#include <forevervalidator/native.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
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

float LengthSquared(const Vector3 &value) {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

bool IsUnit(const Vector3 &value) {
    return IsFinite(value) &&
           std::abs(LengthSquared(value) - 1.0f) <= 2.0e-3f;
}

bool IsDistinct(const Vector3 &first, const Vector3 &second) {
    return LengthSquared({first.x - second.x,
                          first.y - second.y,
                          first.z - second.z}) > 1.0e-8f;
}

float DistanceSquared(const Vector3 &first, const Vector3 &second) {
    return LengthSquared({first.x - second.x,
                          first.y - second.y,
                          first.z - second.z});
}

bool IsPlausiblyNearWheelBottom(const Vector3 &contact,
                                const Vector3 &wheelBottom) {
    // The accepted collision point and the wheel-bottom presentation point
    // describe the same wheel/surface pair. Keep this deliberately generous
    // enough for deep suspension compression and wall contacts while still
    // catching a local-space point accidentally exported as world-space.
    constexpr float maximumSeparationSquared = 2.25f;
    return DistanceSquared(contact, wheelBottom) <=
           maximumSeparationSquared;
}

Vector3 CarUp(const PhysicsSandboxCarState &car) {
    return {
            2.0f * (car.rotationX * car.rotationY -
                    car.rotationW * car.rotationZ),
            1.0f - 2.0f * (car.rotationX * car.rotationX +
                           car.rotationZ * car.rotationZ),
            2.0f * (car.rotationY * car.rotationZ +
                    car.rotationW * car.rotationX),
    };
}

bool AlignsWithCarUp(const Vector3 &normal,
                     const PhysicsSandboxCarState &car) {
    const Vector3 up = CarUp(car);
    return normal.x * up.x + normal.y * up.y + normal.z * up.z > 0.1f;
}

void ReportInvalidSlidingContact(const PhysicsSandboxCarState &car,
                                 std::size_t wheel,
                                 std::uint64_t timeMs) {
    const Vector3 &ground = car.wheelGroundPosition[wheel];
    const Vector3 &contact = car.wheelContactPoint[wheel];
    const Vector3 &normal = car.wheelContactNormal[wheel];
    std::cerr << "invalid sliding wheel export at " << timeMs
              << " ms, wheel=" << wheel
              << " hasSurface=" << car.wheelHasSurface[wheel]
              << " ground=(" << ground.x << ", " << ground.y << ", "
              << ground.z << ") contact=(" << contact.x << ", "
              << contact.y << ", " << contact.z << ") normal=("
              << normal.x << ", " << normal.y << ", " << normal.z
              << ") car=(" << car.position.x << ", "
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
    std::uint64_t finiteContactPointSamples = 0u;
    std::uint64_t nearbyContactPointSamples = 0u;
    std::uint64_t unitContactNormalSamples = 0u;
    std::uint64_t outwardContactNormalSamples = 0u;
    std::uint64_t distinctContactPointSamples = 0u;
    std::uint64_t distinctContactNormalSamples = 0u;
    std::uint64_t nearbyWheelBottomContactSamples = 0u;
    std::uint64_t invalidSlidingContactSamples = 0u;
    std::uint64_t firstSlidingContactTimeMs =
            std::numeric_limits<std::uint64_t>::max();
    std::uint64_t lastSlidingContactTimeMs = 0u;
    double minimumContactGroundNormalOffset =
            std::numeric_limits<double>::infinity();
    double maximumContactGroundNormalOffset =
            -std::numeric_limits<double>::infinity();
    double contactGroundNormalOffsetSum = 0.0;
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
            firstSlidingContactTimeMs =
                    std::min(firstSlidingContactTimeMs, state.timeMs);
            lastSlidingContactTimeMs =
                    std::max(lastSlidingContactTimeMs, state.timeMs);
            const Vector3 &ground = state.car.wheelGroundPosition[wheel];
            const Vector3 &contact = state.car.wheelContactPoint[wheel];
            const Vector3 &normal = state.car.wheelContactNormal[wheel];
            const bool hasSurface = state.car.wheelHasSurface[wheel];
            const bool finite = IsFinite(ground);
            const bool nonzero = IsNonzero(ground);
            const bool nearby = IsNearCar(ground, state.car.position);
            const bool finiteContactPoint = IsFinite(contact);
            const bool nearbyContactPoint =
                    IsNearCar(contact, state.car.position);
            const bool unitContactNormal = IsUnit(normal);
            const bool outwardContactNormal =
                    AlignsWithCarUp(normal, state.car);
            const Vector3 carUp = CarUp(state.car);
            const bool distinctContactNormal =
                    IsDistinct(normal, carUp);
            const bool nearbyWheelBottomContact =
                    IsPlausiblyNearWheelBottom(contact, ground);
            const double contactGroundNormalOffset =
                    (static_cast<double>(contact.x) - ground.x) * normal.x +
                    (static_cast<double>(contact.y) - ground.y) * normal.y +
                    (static_cast<double>(contact.z) - ground.z) * normal.z;
            slidingContactSurfaceSamples += hasSurface ? 1u : 0u;
            finiteGroundSamples += finite ? 1u : 0u;
            nonzeroGroundSamples += nonzero ? 1u : 0u;
            nearbyGroundSamples += nearby ? 1u : 0u;
            finiteContactPointSamples += finiteContactPoint ? 1u : 0u;
            nearbyContactPointSamples += nearbyContactPoint ? 1u : 0u;
            unitContactNormalSamples += unitContactNormal ? 1u : 0u;
            outwardContactNormalSamples += outwardContactNormal ? 1u : 0u;
            distinctContactPointSamples +=
                    IsDistinct(contact, ground) ? 1u : 0u;
            distinctContactNormalSamples +=
                    distinctContactNormal ? 1u : 0u;
            nearbyWheelBottomContactSamples +=
                    nearbyWheelBottomContact ? 1u : 0u;
            minimumContactGroundNormalOffset = std::min(
                    minimumContactGroundNormalOffset,
                    contactGroundNormalOffset);
            maximumContactGroundNormalOffset = std::max(
                    maximumContactGroundNormalOffset,
                    contactGroundNormalOffset);
            contactGroundNormalOffsetSum += contactGroundNormalOffset;
            if (!hasSurface || !finite || !nonzero || !nearby ||
                !finiteContactPoint || !nearbyContactPoint ||
                !unitContactNormal || !outwardContactNormal ||
                !nearbyWheelBottomContact) {
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
              << " finiteContactPointSamples=" << finiteContactPointSamples
              << " nearbyContactPointSamples=" << nearbyContactPointSamples
              << " unitContactNormalSamples=" << unitContactNormalSamples
              << " outwardContactNormalSamples="
              << outwardContactNormalSamples
              << " distinctContactPointSamples="
              << distinctContactPointSamples
              << " distinctContactNormalSamples="
              << distinctContactNormalSamples
              << " nearbyWheelBottomContactSamples="
              << nearbyWheelBottomContactSamples
              << " firstSlidingContactTimeMs="
              << (slidingContactSamples == 0u ? 0u
                                              : firstSlidingContactTimeMs)
              << " lastSlidingContactTimeMs=" << lastSlidingContactTimeMs
              << " contactGroundNormalOffsetMin="
              << minimumContactGroundNormalOffset
              << " contactGroundNormalOffsetMean="
              << contactGroundNormalOffsetSum /
                            static_cast<double>(
                                    std::max<std::uint64_t>(
                                            slidingContactSamples, 1u))
              << " contactGroundNormalOffsetMax="
              << maximumContactGroundNormalOffset
              << " invalidSlidingContactSamples="
              << invalidSlidingContactSamples
              << '\n';
    if (slidingContactSamples == 0u) {
        std::cerr << "fixture did not produce a sliding wheel contact\n";
        return 1;
    }
    if (distinctContactPointSamples == 0u) {
        std::cerr << "fixture never exported an accepted contact point distinct "
                     "from the wheel-bottom fallback\n";
        return 1;
    }
    if (distinctContactNormalSamples == 0u) {
        std::cerr << "fixture never exported an accepted contact normal "
                     "distinct from the car-up fallback\n";
        return 1;
    }
    return invalidSlidingContactSamples == 0u ? 0 : 1;
}
