# PR: Expose complete raster texture and vehicle visual assets

## Summary

This branch combines the renderer-facing asset work into one main-derived,
conflict-free change set. It exposes authored map material textures, resolves
loose installed `GameData` assets, publishes the installed vehicle's immutable
local-space render scene, and includes the per-wheel ground/slip state needed
for skidmark reconstruction.

## Root causes

- The public render scene described material bindings but did not provide a
  lazy way to read their encoded texture payloads.
- Legacy material references can be relative to the descriptor's `Media` root,
  so resolving them through the pack name alone misses real vehicle materials.
- The default car's collision solid reaches its visual meshes through external
  descriptor providers; reading only the collision root yields no body mesh.
- Viewer snapshots lacked wheel ground points even though the simulator already
  maintained them internally.

## Implementation

- Adds cached, thread-safe material texture asset sources to immutable render
  scenes without copying encoded payloads into every sandbox.
- Resolves packed and loose installed textures with normalized legacy paths.
- Traverses the complete reachable vehicle descriptor graph and resolves
  vehicle material references relative to the active `Vehicles\\Media` root.
- Exposes `ReadVehicleRenderScene()` plus wheel ground position, contact,
  sliding, and surface values across CPU and direct CUDA winner conversion.
- Keeps all vehicle visual work best-effort so physics loading and validation
  retain their existing behavior when optional art assets are unavailable.

## Validation

- 14 regular CPU tests pass; optional installed-game fixtures skip cleanly when
  explicit paths are absent.
- The strict TMNF fixture produces 87 vehicle meshes and instances, three
  authored materials, 11 bitmap bindings, and 9/9 readable texture assets.
- The native CUDA library builds for SM120 with the extended public state.
