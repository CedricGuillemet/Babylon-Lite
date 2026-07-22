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

> Note: this scaffold implements `--help` and `--version` and validates its
> argument surface. Geometry normalization, hierarchy generation, page packing,
> and `.mlod` writing are delivered by subsequent tasks.

## Test

```powershell
ctest --test-dir mesh-lod-tool/build --output-on-failure
```

CTest runs in-process smoke assertions plus end-to-end checks of the built
executable's `--help`, `--version`, pinned revisions, and unknown-argument
handling.
