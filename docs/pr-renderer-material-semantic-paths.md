# Preserve renderer material semantic paths

## Summary

- Preserve the readable plain and selected archive paths for render materials,
  material models, and shaders in `PhysicsSandboxRenderMaterial`.
- Keep `sourcePath`, `modelPath`, and `shaderPath` as selected-first
  compatibility aliases.
- Add a render-scene regression using the real Stadium PDiff Grass model
  identity and hashed selected paths.

## Why

Installed Stadium assets commonly select hashed model and shader paths. The
previous public render scene exposed only that selected path, erasing the plain
`PDiff ... Grass.Material.Gbx` identity consumers need for material semantics.
ForeverTAS therefore failed to enable the game's `world.xz / 16` grass mapping
and stretched a tiny authored UV sliver across every grass instance.

## Impact

Consumers can classify materials from readable authored identities without
losing the exact installed-asset provenance. Existing users of the three
selected-first aliases remain source compatible.

## Validation

- Full CPU build succeeded.
- CTest: 13 passed, 0 failed, 1 installed-fixture test skipped because no
  explicit fixture arguments were supplied.
- `git diff --check` passed.
