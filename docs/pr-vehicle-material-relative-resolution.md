# Resolve vehicle materials from their descriptor media root

## Summary

Vehicle solids can reference a material as
`Material\StadiumCarSkin.Material.Gbx` relative to
`Vehicles\Media\Solid\StadiumCar.Solid.Gbx`. The static-solid linker used to
prepend a second `Material\` component before resolving that reference, so the
installed `Vehicles\Media\Material\...` asset was never found. The resulting
vehicle scene had geometry but fell back to untextured materials.

This change recognizes identifiers that already begin with `Material\` and
resolves them once against the descriptor's `\Media\` root. Bare material names
keep the existing prefix behavior.

## Validation

- a focused synthetic archive test verifies the exact resolved path, linked
  `CPlugMaterial`, retained bitmap source, and readable texture bytes;
- the real TMNF Stadium vehicle fixture resolves 87 meshes, 4 materials, 11
  authored bitmap bindings, and 9 readable textures;
- the focused render-scene test suite passes.

This branch is stacked on `renderer-material-semantic-paths` so it can be
reviewed without duplicating or conflicting with the readable-path contract.
