# Vehicle render scene and wheel-ground state

## Summary

This branch exposes the default installed vehicle's visual geometry alongside
the existing map render scene and publishes four world-space wheel-ground
positions in each sandbox car state.

## Public API

- `PhysicsSandbox::ReadVehicleRenderScene()` returns an immutable
  `PhysicsSandboxRenderSceneHandle` whose transforms are vehicle-local. A
  renderer places its instances beneath the simulated car transform.
- `PhysicsSandboxCarState::wheelGroundPosition` contains four world-space
  bottom-of-wheel points from the current physics snapshot.
- Direct CUDA winner conversion also fills wheel positions plus the matching
  contact, surface-presence, sliding, and material fields.

## Loading, caching, and fallback

The installed vehicle solid is decoded once per cached `(vehicle model, pack)`
entry. The visual load follows the complete reachable descriptor graph, so
external visual providers referenced by the collision solid contribute their
real vertex and index payloads. Legacy tree shader slots that actually refer to
materials are resolved relative to the current descriptor's media root (for
example, `Vehicles\Media\Solid` to `Vehicles\Media\Material`) rather than an
incorrect pack-name prefix. The existing static render-scene builder then
flattens the cloned prototype and retains resolved material texture sources in
the immutable scene handle. Sandboxes and optimized-CPU clones share the cached
handle rather than decoding or copying mesh buffers again.

Vehicle visuals are deliberately best-effort. Failure to create the vehicle
material repository, resolve a visual material, clone the visual tree, or build
render geometry does not block validation or sandbox loading. The original
physics-only load remains authoritative; `ReadVehicleRenderScene()` returns an
error when no visual is available so clients can keep their ellipsoid fallback.

## Validation

- Full non-CUDA build completed successfully.
- All 14 regular CPU tests pass; the installed-game integration fixture skips
  when its explicit pack/replay arguments are not supplied.
- `forevervalidator-render-scene` now checks reusable local-space scene
  building, immutable shared scene handles, and the public wheel-position
  field.
- The strict installed TMNF fixture produces 87 meshes and instances, three
  authored materials, 11 bound material bitmaps, and nine readable encoded
  texture assets. It fails if geometry is present without authored materials
  or readable texture bytes.
