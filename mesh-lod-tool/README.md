# mesh-lod-tool

`mesh-lod-tool` is the MeshLoD offline converter. It reads a glTF or GLB
primitive and produces a versioned, range-addressable `.mlod` sidecar for the
Babylon Lite MeshLoD runtime.

This directory is self-contained: a clean checkout configures and builds with
CMake using only the sources here plus the pinned dependencies fetched by
`FetchContent`. No source outside `mesh-lod-tool/` is required; repository input
assets are supplied by path.

## Pinned dependencies

`FetchContent` resolves immutable full-commit revisions (never branches or
tags):

| Dependency | Revision |
| --- | --- |
| meshoptimizer | `f843aae0b3070306bd2aeef43ffcf09509fee526` |
| cgltf | `85cd62382dfea638278962690cf515023f33ed00` |

These same revisions are reported by `--version` and, in later tasks, embedded
in the generated `.mlod` provenance.

## Requirements

- CMake 3.24 or newer.
- A C++17 compiler (MSVC, Clang, or GCC).
- Network access on first configure so `FetchContent` can clone the pinned
  dependencies.

## Build (Windows)

### Single-config generator (Ninja)

```powershell
cmake -S mesh-lod-tool -B mesh-lod-tool/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build mesh-lod-tool/build
# Executable:
mesh-lod-tool\build\mesh-lod-tool.exe --version
```

### Multi-config generator (Visual Studio 2022)

```powershell
cmake -S mesh-lod-tool -B mesh-lod-tool/build -G "Visual Studio 17 2022" -A x64
cmake --build mesh-lod-tool/build --config Release
# Executable:
mesh-lod-tool\build\Release\mesh-lod-tool.exe --version
```

When Ninja is used with MSVC, run the commands from a Developer prompt or after
sourcing `vcvars64.bat` so `cl` is on `PATH`.

## Run

```powershell
mesh-lod-tool --input harvard-yenching_institute_statue.glb --output harvard-yenching_institute_statue.mlod
```

Run `mesh-lod-tool --help` for the full option list and defaults. `--version`
prints machine-readable `key=value` provenance (tool version, `.mlod` format
version, both dependency revisions, and the compiler target) and requires no
input asset.

When a conversion selects more than one primitive, the tool writes one container
per primitive, inserting `.meshNNN.primNNN` (three-digit, zero-padded source
indices) before the `.mlod` extension. Outputs are built and validated fully in
memory and published atomically: nothing is renamed into place unless every
selected primitive validates, so a failure leaves no partial output.

## Statue asset (first end-to-end deliverable)

`harvard-yenching_institute_statue.glb` at the repository root is three meshes
with one primitive each. The committed `.mlod` deliverables and their canonical
statistics are produced with the default (canonical) options:

```powershell
mesh-lod-tool --input harvard-yenching_institute_statue.glb `
  --output lab/public/mesh-lod/harvard-yenching_institute_statue.mlod `
  --stats-json lab/public/mesh-lod/statue-stats.json
```

This emits three sibling containers under `lab/public/mesh-lod/`
(`...mesh000.prim000.mlod`, `...mesh001.prim000.mlod`, `...mesh002.prim000.mlod`)
plus `statue-stats.json`. Conversion is deterministic: repeating it in a clean
location produces byte-identical files. Do not modify the source GLB; the
generated `.mlod` files are deliverables, not build products.

## Serving `.mlod`

`.mlod` containers are range-addressable and MUST be served with:

- `Content-Encoding: identity` (no gzip/br/transfer transformation that changes
  byte offsets), and
- HTTP byte-range support (`Accept-Ranges: bytes`).

The runtime bootstraps by reading the header and section directory from the
first 64 KiB, then the contiguous metadata and pinned coarse pages inside
`bootstrapBytes`, and finally requests individual fine pages by their
page-table byte ranges. Any intermediary that re-encodes or coalesces the body
so that byte offsets no longer match `fileBytes` is incompatible. A server that
ignores a range request and returns the complete `200` body is tolerated (the
loader retains the full file); other range/protocol failures are surfaced
explicitly.

## Test

```powershell
ctest --test-dir mesh-lod-tool/build --output-on-failure
```

CTest runs in-process smoke assertions plus end-to-end checks of the built
executable's `--help`, `--version`, pinned revisions, and unknown-argument
handling.
