# bgfx native-lite host (Babylon-Lite Native Layer)

An alternative native host for the **Experiment 1 — BoomBox grid benchmark** (see
`../NativeLiteExperiments.md`). Same scenario as the reference, different delivery:

- **JS engine:** V8 (dynamically linked, JIT enabled)
- **Backend:** Windows, **D3D12** (`app-d3d12.exe`) and **D3D11** (`app-d3d11.exe`) — two
  single-backend Release builds
- **Renderer:** **bgfx** (the same renderer Babylon Native uses) instead of Dawn/WebGPU
- **Approach:** the JS keeps the **Babylon-Lite public API** (`import { ... } from
  "babylon-lite"`) but the implementation forwards each call to a thin C++ seam; scene
  traversal, culling, animation, skinning, morph, IBL, and the 4-cascade CSM all run in C++
  over bgfx. The scene `native-lite-benchmark.lite.js` is `native-lite-benchmark.ts` bundled
  with esbuild against that native-forwarding mirror.

The rendered scene is identical to the reference: BoomBox loaded once, cloned into a 20×20
grid of 400 individual meshes (no instancing), casting CSM shadows onto a ground plane.

## Run

From `native/` run `./RunExperiments.ps1` (it launches all hosts), or directly:

```powershell
cd bgfx
./app-d3d12.exe native-lite-benchmark.lite.js --width 640 --height 400 --no-vsync --frames 1000
./app-d3d11.exe native-lite-benchmark.lite.js --width 640 --height 400 --no-vsync --frames 1000
```

Each prints one `BENCH …` line on exit with `total_ms` (time to render = start of frame 1 →
end of the last frame, excluding startup + asset load), `ms_per_frame`, `fps`, `avg_ms`,
`p95_ms`, `render_cpu_ms` (process CPU strictly across the render loop) and `mem_peak_bytes`
(`PeakWorkingSetSize`).

## Self-contained

The host needs no network: the BoomBox glTF + `.env` are committed under `assets/` and loaded
from disk (so the timed render isn't polluted by downloads). The compiled bgfx shaders are in
`shaders/`. The V8 runtime ships as the sibling `v8.dll` + `icudtl.dat` + helper DLLs.

`app.exe` itself is ~0.95 MB (D3D11) / ~0.98 MB (D3D12); the bulk of the footprint is the V8
redistributable, which is inherent to the engine.

Source: <https://github.com/SergioRZMasson/Babylon-Lite-Native-Layer>
