# PR: Export exact wheel-contact state for renderer skidmarks

## Summary

Headless replay simulation now exports each wheel's accepted collision point
and normalized surface normal in world space. This lets renderer clients place
skidmarks on the real contact plane instead of approximating it from wheel
bottom position and car up.

## Root cause

The existing renderer state already repaired headless contact flags and
wheel-bottom positions, but those points do not describe the collision plane.
They can differ from the accepted contact by centimetres and have no per-wheel
normal, so a car-up offset buries or floats marks on banks, walls, and inverted
surfaces. `cameraSupportUp` is also a car-level camera value and is not populated
by the canonical headless runtime, so it is not a valid substitute.

## Fix

The runtime now:

- reads accepted contact data from `wheel.realTimeState`;
- transforms `latestContactPoint` with the live body frame;
- rotates and normalizes `accumulatedContactNormal` with the same frame;
- falls back independently to the exported wheel-bottom point and live car-up
  only when the corresponding accepted value is absent or non-finite; and
- applies the same conversion to a captured CUDA winning state.

The fields propagate through runtime/session state and the public
`PhysicsSandboxCarState`. They are appended to the public car struct so offsets
of older fields remain unchanged. Validator and binary clients must still be
rebuilt together because the structure grows by 96 bytes. No raw public-state
serialization was found, so this API extension does not change the sandbox
cache schema.

## Regression coverage

The optional real-replay fixture test simulates a recorded replay with the
Optimized CPU backend and checks every sliding contact for:

- a matching surface flag;
- finite, nearby contact and wheel-bottom coordinates;
- a unit contact normal pointing outward with car up; and
- proximity to the corresponding wheel-bottom presentation point; and
- at least one accepted point and one accepted normal distinct from their
  wheel-bottom/car-up fallbacks.

The fixture paths can be supplied through CMake cache variables so the test is
part of an ordinary CTest run on machines with installed assets. A separate
deterministic CUDA-state-layout test verifies that the accepted contact point,
normal accumulator, and sample count survive candidate encoding and decoding.

On `Abuk.Replay.Gbx`, the regression covers 1,739 contacted wheel samples and
440 sliding-contact samples from 3,070 through 7,460 ms. All 440 have a surface,
finite nearby accepted points, unit outward normals, and accepted points
and normals distinct from the old fallbacks; zero samples are invalid.

The test skips with code 77 when external Packs/replay fixture arguments are
not supplied, matching the repository's other installed-asset integration
tests.
