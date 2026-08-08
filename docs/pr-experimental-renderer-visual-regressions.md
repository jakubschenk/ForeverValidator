# Integrate renderer asset fixes with the experimental CUDA branch

## Summary

This integration branch applies the isolated renderer material-path and vehicle
asset fixes to `experimental`, preserving its CUDA search implementation while
giving ForeverTAS the complete render-scene contract it needs.

The combined changes retain readable material/model/shader paths alongside the
selected archive assets, resolve vehicle materials from their descriptor media
root, reject malformed relative paths, and use the selected vehicle archive
payload for body and wheel physics extraction.

## Branch structure

- `renderer-material-semantic-paths` remains the isolated readable-path change;
- `vehicle-material-relative-resolution` remains the stacked vehicle loader
  change;
- this branch is the runnable integration target based directly on
  `experimental`, so the CUDA/search files are not replaced by an older base.

## Validation

- focused render-scene, CUDA state-layout, and CUDA candidate-event tests pass;
- a two-payload regression verifies selected vehicle collision geometry;
- the installed TMNF Stadium vehicle fixture resolves 87 meshes, 4 materials,
  11 bitmap bindings, and 9 readable textures;
- the diff from the experimental CUDA source contains no CUDA backend files.
