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
entry. That decode supplies both the authoritative physics definition and, when
available, a cloned visual prototype. The existing static render-scene builder
then flattens that prototype and retains resolved material texture sources in
the immutable scene handle. Sandboxes and optimized-CPU clones share the cached
handle rather than decoding or copying mesh buffers again.

Vehicle visuals are deliberately best-effort. Failure to create the vehicle
material repository, resolve a visual material, clone the visual tree, or build
render geometry does not block validation or sandbox loading. The original
physics-only load remains authoritative; `ReadVehicleRenderScene()` returns an
error when no visual is available so clients can keep their ellipsoid fallback.

## Validation

- Full non-CUDA build completed successfully.
- All 14 configured CPU tests pass.
- `forevervalidator-render-scene` now checks reusable local-space scene
  building, immutable shared scene handles, and the public wheel-position
  field.

