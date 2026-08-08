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
keep the existing prefix behavior. Descriptor-relative lookup rejects traversal,
empty path components, slash-separated aliases, and embedded NUL bytes before
consulting the pack repository.

The vehicle loader now also threads the descriptor's selected archive payload
into body and wheel extraction. This removes a latent payload-zero assumption
that could otherwise combine the selected render model with collision geometry
from another payload.

## Validation

- a focused synthetic archive test verifies the exact resolved path, linked
  `CPlugMaterial`, retained bitmap source, and readable texture bytes;
- malformed relative-path cases are rejected before pack lookup while the
  legacy bare-name form remains covered;
- a two-payload regression proves all four wheel radii and the body bounds come
  from the selected descriptor payload;
- the real TMNF Stadium vehicle fixture resolves 87 meshes, 4 materials, 11
  authored bitmap bindings, and 9 readable textures;
- the focused render-scene test suite passes.

This branch is stacked on `renderer-material-semantic-paths` so it can be
reviewed without duplicating or conflicting with the readable-path contract.
