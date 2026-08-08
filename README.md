# ForeverValidator

ForeverValidator is a deterministic C++17 replay validator for TrackMania
United Forever. It decodes a replay and its embedded challenge, loads the
original installed game assets, and reruns the recorded inputs through
reconstructed game physics.

## Supported United content

The validator supports the seven vanilla map environments and vehicles:

| Map environment | Vehicle |
| --- | --- |
| Alpine / Snow | SnowCar |
| Speed / Desert | DesertCar |
| Rally | RallyCar |
| Island | IslandCar |
| Coast | CoastCar |
| Bay | BayCar |
| Stadium | StadiumCar |

All 49 vanilla map-environment and vehicle combinations are routed from the
identifiers embedded in the replay. TMUnlimiter environments, custom vehicles,
and custom physics are outside the supported scope.

Race, Platform, Puzzle, and Stunts replays are supported. Shortcut and
multilap challenges use Race completion and checkpoint semantics. Race,
Platform, and Puzzle validate completion, time, and respawns where recorded.
Stunts validates completion, respawns, and the independently simulated stunt
score.

Scripted-input replays are detected from their input clock marker. ForeverValidator normalizes their recorded input timeline and
checks whether those inputs reproduce the stored ghost, while clearly marking
the replay as script-generated. These replays remain invalid for
competitive validation even when the inputs and ghost match.

## Game files

ForeverValidator requires the `Packs` directory from a complete TrackMania
United Forever installation. A TrackMania Nations Forever installation is not
sufficient: it does not contain the six non-Stadium environments or their
vehicles.

The directory must contain `packlist.dat` and the installed United packs,
including Alpine, Speed, Rally, Island, Coast, Bay, Stadium, Game, and Resource.

## Linux build

Dependencies are GNU Make, a C++17 compiler, zlib development files, and
OpenSSL libcrypto development files.

```sh
make -j"$(nproc)"
```

This creates:

```text
build/native/forevervalidator
build/native/libforevervalidator_core.a
build/native/libforevervalidator_json.a
build/native/libforevervalidator_native.a
```

The optional NVIDIA CUDA Stadium/StadiumCar backend is built with
`-DFOREVERVALIDATOR_ENABLE_CUDA=ON`. Hardware requirements, build commands,
architecture, correctness evidence, benchmarks, and limitations are in
[docs/cuda-backend.md](docs/cuda-backend.md).

## Windows build

### From Linux

The reproducible cross-build requires a MinGW-w64 C++ toolchain, `curl`,
`perl`, `tar`, and `sha256sum`:

```sh
JOBS="$(nproc)" make -j"$(nproc)" windows
```

The build downloads the pinned OpenSSL 3.5.7 and zlib 1.3.2 source archives,
verifies their SHA-256 checksums, builds static Windows libraries, and creates
`build/windows/forevervalidator.exe`. The executable does not depend on MinGW
runtime DLLs.

### On Windows

Windows users can download a ready-to-run 64-bit executable from the
[GitHub releases](https://github.com/Skycrafter-dev/ForeverValidator/releases);
building from source is optional.

To build from source, open PowerShell in the repository and run:

```powershell
.\build-windows.bat
```

The script downloads and verifies pinned portable build tools and dependency
sources, then creates `dist\forevervalidator.exe`. Downloads and compiled
dependencies are cached for later builds. The first build requires an internet
connection, but no administrator privileges, package manager, preinstalled
compiler, or manual `PATH` changes.

## Command line

Validate one replay and write its JSON report to standard output:

```sh
build/native/forevervalidator \
  --pak-dir "/path/to/TmUnitedForever/Packs" \
  "/path/to/run.Replay.Gbx"
```

Use `--out` for one output file, or validate files and directories in a batch
with `--out-dir`:

```sh
build/native/forevervalidator \
  --pak-dir "/path/to/TmUnitedForever/Packs" \
  --out-dir /tmp/validation-results \
  "/path/to/replays"
```

Select a simulation backend at runtime with `--backend reference`,
`--backend optimized-cpu`, `--backend speculative-ticking`, or
`--backend batched`, or `--backend cuda`. Single-replay validation defaults
to the authoritative reference backend. Multi-replay runs default to the
ordered batched backend.
`--batch-size N` controls how many replay files are loaded and submitted
together, and defaults to 10.
`--requested-samples N` limits trajectory comparison to `N` evenly selected
ghost samples while preserving complete timeline simulation and final outcome
evaluation. This is useful for finish/outcome corpus sweeps; omit it for the
default full trajectory comparison.

A single replay returns exit status 0 for valid, 1 for invalid, and a distinct
nonzero error code when replay decoding, asset loading, or simulation cannot
be completed. JSON includes typed map environment, vehicle, play mode,
replay provenance, input/ghost match, expected and observed outcomes,
trajectory counts, maximum deviation, and the first divergence when present.
Scripted replays use the `scripted_replay` status and expose
`replay_file_metadata.replay_provenance` plus `input_ghost_match`; the CLI also
prints the scripted-input classification to stderr.

## Container image

The multi-stage `Dockerfile` builds `forevervalidator` on Debian and runs it
as an unprivileged, non-root user. Build the image from the repository root:

```sh
docker build -t forevervalidator .
```

The image's `ENTRYPOINT` is the `forevervalidator` binary, so any arguments
given to `docker run` are the same command-line
arguments documented above. Mount the installation's `Packs` and sibling
`GameData` directories plus your replay(s), then reference the in-container
mount paths. `GameData` supplies loose authored assets such as material
textures; validation itself continues to use `--pak-dir`:

```sh
docker run --rm \
  -v "/path/to/TmUnitedForever/Packs:/game/Packs:ro" \
  -v "/path/to/TmUnitedForever/GameData:/game/GameData:ro" \
  -v "/path/to/replays:/replays:ro" \
  forevervalidator \
  --pak-dir /game/Packs \
  "/replays/run.Replay.Gbx"
```

To batch-validate a directory and write JSON reports back to the host, mount
an output directory and use `--out-dir` (and optionally `--backend` /
`--batch-size`) exactly as on the command line:

```sh
docker run --rm \
  -v "/path/to/TmUnitedForever/Packs:/game/Packs:ro" \
  -v "/path/to/TmUnitedForever/GameData:/game/GameData:ro" \
  -v "/path/to/replays:/replays:ro" \
  -v "/path/to/results:/results" \
  forevervalidator \
  --pak-dir /game/Packs \
  --out-dir /results \
  --backend batched \
  "/replays"
```

Because the container runs as a non-root user, host-mounted directories that
the process writes to (such as `/results`) must be writable by that user.
Either relax the host directory permissions or run the container with
`--user "$(id -u):$(id -g)"` to match your host user.

## Library API

The public API separates portable validation from native file access:

```cpp
#include <forevervalidator/native.h>
#include <forevervalidator/validation.h>

#include <utility>

using namespace forevervalidator;

void validateOneReplay() {
    auto source = OpenInstalledPackDirectory(
        "/path/to/TmUnitedForever/Packs");
    if (!source) {
        return; // Inspect source.Error().
    }

    auto context = CreateValidationContext(std::move(source).Value());
    auto replay = ReadNativeReplayFile(
        "run.Replay.Gbx", {"run.Replay.Gbx"});
    if (!context || !replay) {
        return;
    }

    auto report = ValidateReplay(
        context.Value(),
        {replay.Value().data(), replay.Value().size()},
        {"run.Replay.Gbx"});
}
```

Applications can instead provide assets from memory with `AssetProvider` and
`CreateAssetSource`. A validation context lazily caches pack and vehicle data,
so it can be reused for replays from different United environments. Every
public operation returns a typed `Result<T>`.

`ValidationOptions::backend` selects the backend for one library call. The
optimized CPU backend uses a dedicated deterministic execution path with cached
static-scene transforms, precomputed mesh data, hierarchical broad-phase
queries, native binary32 arithmetic, compiled tuning curves, and specialized
Model3 and Model6 vehicle physics.
`ValidateReplayBatch` executes an ordered list of independent replay requests
and preserves an individual report or error for every item.

## Experimental physics sandbox

`<forevervalidator/experimental/physics_sandbox.h>` provides the deliberately
unstable `PhysicsSandbox` integration surface for simulation and backend
development. A sandbox consumes its own `AssetSource`, loads the embedded map
and input timeline from a replay, advances by an exact tick count, and captures
or restores opaque deterministic states. Static map collision structures are
owned by the sandbox rather than copied into state snapshots.

`PhysicsSandboxTimelineMode::Canonical` instead loads only the map and required
scenario context. It creates a fresh race-start timeline bounded by the
explicit `simulationHorizonMs`; recorded controls, finish markers, duration,
and outcomes are excluded. Commands after the horizon remain readable but do
not enter CPU or CUDA control plans. The default recorded-replay mode preserves
the validation behavior of existing callers.

The API exposes replay-level input events and search-relevant state such as car
dynamics, race-relative time, checkpoints, laps, respawns, and completion. It
does not expose arbitrary engine objects, per-tick callbacks, or TAS search
concepts. Snapshot restoration clones the complete evolving runtime state at
a completed-tick boundary.
Capture and restore execute no physics ticks; runtime-owned pointers are
rebound to the target sandbox while static map collision data stays in place.

```text
libforevervalidator_core.a    portable validation backend
libforevervalidator_json.a    JSON serialization
libforevervalidator_native.a  native file and pack-directory adapters
forevervalidator              command-line frontend
```

## Repository layout

```text
include/forevervalidator  public API
src/app/cli               command-line frontend
src/validation            validation and JSON output
src/simulation            controls and deterministic simulation runtime
src/engine                game physics and scene runtime
src/format                replay, GBX, pack, tuning, and solid decoding
src/platform/native       native filesystem and pack-directory integration
```

## Acknowledgements

Development was accelerated with LLM assistance.
