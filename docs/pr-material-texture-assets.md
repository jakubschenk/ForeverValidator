# Expose lazy material texture assets

## Summary

This change carries external `CPlugBitmap` image references through material decoding and exposes their original encoded bytes through each experimental physics-sandbox render scene. It also removes the water-only material-render gate so ordinary Stadium materials retain their model, shader, sampler, and texture bindings. Packed bitmap wrappers can resolve authored image files from the installation's sibling `GameData` tree while retaining `Packs` precedence.

## Public API

- `PhysicsSandboxMaterialBitmap` now exposes a deterministic `textureAssetId`, selected texture path, and an actionable diagnostic when bytes are unavailable.
- `PhysicsSandboxRenderScene::textureAssets` lists immutable metadata and lazily reads original encoded payloads such as DDS, TGA, PNG, JPEG, and BMP.
- Successful reads and failures are cached per asset. Resolver copies share the cache and immutable payload handles.
- IDs deduplicate case-insensitively by stable pack namespace plus selected pack path. Zero remains the unavailable-asset sentinel.

## Lifetime and concurrency

The installed pack is shared through the material repository, texture source, and scene resolver, so a scene remains readable after its sandbox/repository is destroyed. Resolver metadata is immutable, and each asset has an independent mutex; concurrent reads of different assets do not serialize behind one global lock, while duplicate reads load once.

## Validation

- Clang/Windows CPU build completed successfully.
- `ctest --test-dir build/cpu-texture-clang -C RelWithDebInfo --output-on-failure`: 13/13 tests passed.
- A real Stadium replay produced 430 render materials, 3,625 bitmap bindings, 3,227 referenced bindings, and 133 unique texture assets; all 133 encoded payloads (70,940,264 bytes) were read successfully.
- Its primary shader inputs were fully resolved: Diffuse 382/382, Normal 381/381, Specular 382/382, Blend1/Blend2/BlendI 18/18 each, and Advert 17/17.
- Unit coverage verifies deterministic IDs, case-insensitive deduplication, lazy loading, cache reuse, retained source lifetime, typed unknown-ID/extraction failures, invalid-resolver behavior, sibling `GameData` discovery/fallback, `Packs` precedence, and path confinement.

## Known limitations

- The API returns original encoded files, not decoded RGBA pixels or complete shader/sampler semantics.
- Inline/generated images and bitmap archives that cannot be extracted or decoded expose diagnostics and no asset ID; they no longer discard an otherwise usable material graph.
- The material decoder still scopes `CMwId` dictionary state more narrowly than the file-global GBX dictionary, so rare shared-ID shader variants may be skipped.
- Internal requirement nodes, repeated/null/external bitmap-address entries, and non-water inline bitmap-render node classes are not yet decoded.
