# bgfx native-lite — implementation notes & assumptions

This is an alternative native host for the *Experiment 1 — BoomBox grid* benchmark. It runs the
**same Babylon-Lite scene** as the Dawn/WebGPU reference, but takes a fundamentally different
delivery approach. This document records how it is built and the trade-offs we accepted, so the
numbers can be read in context.

## Approach: a native twin of Babylon-Lite

Rather than implementing the WebGPU spec natively (Dawn) and running Babylon-Lite's JS unchanged on
top of it, we did a **native re-implementation of the engine** and exposed it through a *twin* of
the Babylon-Lite public API:

- **Thin JS layer.** Consumers still write `import { createEngine, loadGltf, … } from
  "babylon-lite"`. Our `babylon-lite` module has the *same API surface*, but every call is a thin
  shim that forwards scene data and commands to C++ through a small `__bl_*` native seam.
- **Native scene management + rendering.** Scene graph construction, world-matrix evaluation,
  frustum culling, glTF/animation/skinning/morph evaluation, image-based lighting and the
  4-cascade cascaded shadow map (CSM) **all run in C++** over **bgfx** (the same renderer
  Babylon Native uses). The JS never touches a GPU API.
- **No WGSL / no JS shaders.** The materials' shaders were **hand-ported to bgfx** (`.sc`,
  HLSL-style) and compiled **offline** with `shaderc` to backend bytecode (`shaders/*.bin`). The
  shipped app therefore contains **no shader compiler and no shader source** — a deliberate choice
  to keep the executable as small as possible.

The result is a self-contained scene (`native-lite-benchmark.lite.js`) that is the benchmark's
`native-lite-benchmark.ts` bundled with esbuild against this native-forwarding mirror.

## Built on Babylon Native

The host re-uses Babylon Native's foundations rather than inventing its own:

- **JsRuntimeHost** provides the N-API (`Napi`) layer over the JS engine and the `JsRuntime`
  dispatch used to marshal async results back onto the JS thread.
- **Threading model is Babylon Native's.** JavaScript executes on a single **JS thread**; bgfx runs
  on its own **render thread** (`BGFX_CONFIG_MULTITHREADED`); HTTP/asset I/O runs on **background
  worker threads** (UrlLib), and each completion is posted back to the JS thread through the
  `JsRuntime` dispatch queue (drained per frame). This mirrors how Babylon Native keeps script,
  rendering and I/O isolated.
- **JS engine:** V8 (dynamically linked, JIT enabled), via the JsRuntimeHost V8 package.

### Polyfills currently in use

Just enough of a browser-like environment for self-contained, web-style scene bundles to run
unchanged:

| Polyfill | Source | Notes |
| --- | --- | --- |
| `URL` | JsRuntimeHost (`Babylon::Polyfills::URL`) | full URL parsing |
| `XMLHttpRequest` | JsRuntimeHost (`Babylon::Polyfills::XMLHttpRequest`) | async asset fetch (UrlLib) |
| `console.{log,info,warn,error,debug}` | host shim | prints to stderr |
| `performance.now` | host shim | `Date.now()`-based monotonic clock |
| `document` / `window` / `window.location` | host shim | minimal stubs (`getElementById` returns a fake canvas) |
| `URLSearchParams` | host shim | only installed when absent (the URL polyfill provides the real one) |
| `setFrameCallback(fn)` | native seam | per-frame render callback (our `requestAnimationFrame` equivalent) |

## JS bundle size

Because the engine lives in C++ and **no shader code ships in JS**, our scene bundles are far
smaller than Babylon-Lite's WebGPU bundles for the *same scene*:

| Scene bundle | bgfx native twin | Babylon-Lite (WebGPU) |
| --- | --- | --- |
| `native-lite-benchmark` | **~21 KB** | ~1,385 KB |

That is ~66× smaller — the WebGPU bundle carries the full engine + WGSL shader strings, while ours
carries only the thin forwarding layer and the scene.

## Performance — and a warning

Benchmark protocol: V8, Windows, **640 × 400**, **vsync off**, **1000 frames**; `ms/frame` is the
average per-frame wall time (time-to-render from the first rendered frame to the end of the last).
All numbers below were measured **on the same machine** (NVIDIA Quadro P620).

| Host | Backend | ms/frame | app.exe size |
| --- | --- | ---: | ---: |
| **bgfx native twin** | D3D11 | **3.53** | **0.95 MB** |
| **bgfx native twin** | D3D12 | **3.61** | **0.98 MB** |
| Dawn reference | D3D11 | 2.64 | 4.24 MB |
| Dawn reference | D3D12 | 3.48 | 4.26 MB |

(The full shipped footprint is dominated by the V8 redistributable — `v8.dll` + ICU data + helper
DLLs ≈ 40 MB — which is inherent to the engine and identical for both hosts. The numbers above are
the *application* binary, the part the implementation actually controls.)

> ### ⚠️ A native port can be *slower* than the JS original
>
> Our **first** working native implementation measured **~9.8 ms/frame on D3D12 — about 2.8× slower
> than the Dawn reference (3.48 ms)**, even though the binary was 4× smaller. It is tempting to
> assume "native + bgfx must beat JS + WebGPU", but that assumption was wrong.
>
> The cause was an *implementation* gap, not a platform one: we were **re-rendering the whole CSM
> shadow map (≈1,600 caster draws, ~9.7 M triangles) every single frame**, while Babylon-Lite
> **caches the shadow map** and only regenerates it when the camera, casters or light actually
> change. With the scene static, the reference renders the shadow map *once*; we were redoing it
> 1,000 times. Adding the same shadow-map cache brought us from 9.8 ms to **3.6 ms — to parity on
> D3D12**.
>
> The lesson for this exploration: **matching functionality is not enough.** A native layer only
> wins if it is *also* implemented well; a naive native port can easily be much worse than the
> mature JS engine it replaces. Every hot path has to be measured against the reference, not
> assumed faster.

## The biggest downside: maintenance cost

The defining trade-off of this approach is **duplication**. Babylon-Lite's engine is a living,
fast-moving JS codebase; our twin must re-implement, in C++, everything that JS layer does and keep
matching its behaviour as it evolves:

- Every new Babylon-Lite feature, material, loader extension, fix or behavioural tweak has to be
  **ported again** into the native layer to stay in sync.
- Parity (visual + behavioural) must be continuously re-verified against upstream.
- The shaders are hand-ported, so any WGSL/material change upstream means re-porting the
  corresponding bgfx shader.

In other words, we trade a small binary and native control for an **ongoing, substantial
maintenance burden** of keeping the native twin current with the Babylon-Lite JS repository. For a
single static benchmark this is cheap; for the full, continuously-developed engine it is the
dominant long-term cost of the approach.

## Source

<https://github.com/SergioRZMasson/Babylon-Lite-Native-Layer>
