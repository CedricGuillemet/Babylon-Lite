# Native Lite Experiments

A place to compare how the **Babylon Lite** runtime performs when driven from a
native host (a C++ app embedding a JS engine + a WebGPU backend) instead of the
browser. Each experiment fixes a scene and a measurement protocol so results are
comparable across machines and across implementations.

## Experiment 1 — BoomBox grid benchmark

### Scenario

The scene under test is **`native-lite-benchmark`** (source:
`lab/lite/src/demos/native-lite-benchmark.ts`). It:

- loads the Khronos **BoomBox** glTF (PBR) **once**,
- deep-clones it into a **20×20 grid = 400 individual meshes** — one draw call
  each, **no instancing** (clones share the source GPU buffers),
- casts **cascaded shadow map (CSM)** shadows from every clone onto a ground
  plane, lit by a directional "sun",
- frames the whole grid with an auto-fitted ArcRotate camera.

The _scenario_ is what matters, not the delivery format. You may turn the `.ts`
into whatever your runtime needs (a JS bundle, transpiled or native code, etc.)
using AI and any tooling — as long as the rendered scene stays identical.

### Reference host

The reference app provided in this branch:

- **JS engine:** V8 (dynamically linked, JIT enabled)
- **OS / backend:** Windows, **D3D12**
- **Output:** renders into a small **640×400** window to keep GPU pressure low
- **Frames:** **1000**, **vsync off**

### Protocol

- Render **1000 frames** at **640×400**, **vsync off**, **Release** build.
- **Time to render** = from the start of the 1st frame to the end of the last
  frame (startup, engine/backend init and asset loading are **excluded**).
- At exit, log the process **`PeakWorkingSetSize`** from
  `PROCESS_MEMORY_COUNTERS` (`psapi.h`, `GetProcessMemoryInfo`).
- Run **3×** and report the **median** of each metric. Close other GPU/CPU-heavy
  apps; on a laptop, run on AC power.

### How to run

The reference bundle (`dawn/native-lite-benchmark.lite.js`) and host
(`dawn/app-d3d12.exe`) are committed in this branch. From `native/`:

```powershell
./RunExperiments.ps1
```

The script launches the host against the committed bundle at 640×400, 1000
frames, vsync off.

To contribute your own implementation, bundle/transpile
`native-lite-benchmark.ts` on your side, commit your app **and** the resulting
artifact to this branch, update `RunExperiments.ps1` to launch it, and push to
the [Native Experiment PR](https://github.com/CedricGuillemet/Babylon-Lite/pull/1).

### Measurement prompt

Use this when handing the work to a build/measure agent. Keep the scenario fixed;
only the build/runtime may change.

> Follow this measurement protocol **exactly**. You may transform the scenario in
> `native-lite-benchmark.ts` into whatever form your runtime needs (a bundle,
> transpiled or native code, etc.) using AI or any tooling — but the rendered
> scene must stay identical (BoomBox loaded once, cloned into a 20×20 grid of 400
> individual meshes with **no instancing**, casting CSM shadows onto a ground
> plane).
>
> **Fixed conditions**
>
> - JS engine: V8
> - Backend: D3D12 (Windows)
> - Build: Release
> - Resolution: 640×400 (pass `--width 640 --height 400` explicitly)
> - Frames: `--frames 1000`, vsync off
> - Run **3×**, report the **median** of each metric. Close other GPU/CPU-heavy
>   apps; on a laptop, run on AC power.
>
> **Report**
>
> - `wall_ms` — includes waiting (GPU, vsync, present back‑pressure, I/O)
> - `render_cpu_ms` — process CPU time (kernel+user, all threads) consumed
>   **strictly across the render loop** (start of 1st frame → end of last frame),
>   **excluding** startup and asset loading. Also report `render_cpu_ms / frames`.
> - `mem_peak_bytes` — peak working set (Windows `PeakWorkingSetSize`), also in MB.
> - `avg_ms`, `p95_ms` — per-frame wall time, for context.
> - Confirm: engine=V8, backend=D3D12, build=Release, 640×400, frames=1000,
>   vsync off.
