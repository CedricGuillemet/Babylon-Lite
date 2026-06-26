# Polyfills — Dawn native test app (Windows)

This document lists the browser/web APIs the Dawn native test app
(`app-d3d11.exe` / `app-d3d12.exe`) provides to a Babylon‑Lite scene bundle so
it can run outside a browser.

The app runs a WebGPU‑only Babylon‑Lite bundle on a chosen JS engine
(V8 / Chakra / QuickJS / Hermes / JSC) and bridges WebGPU calls to **Dawn**
(D3D11 or D3D12 on Windows). The polyfills come from three layers:

| Layer | Source | What it provides |
| --- | --- | --- |
| C++ DOM shim | `src/appRuntime/Shared/dom_shim.cpp` | global objects/functions installed **before** the bundle runs |
| JS runtime shim | `js/runtime-shim.js` | additional web APIs, loaded before the bundle (ES5, classic script) |
| WebGPU bridge | `src/appRuntime/Shared/wgpu_bridge.cpp` | `navigator.gpu` + all `GPU*` classes, routed to Dawn via `__wgpu` |

The **JS API surface is identical across platforms** (it lives in `Shared/`).
What is *Windows‑specific* is the native plumbing behind some of these APIs:
- Image decoding (`createImageBitmap`) → **WIC**
- Remote `fetch` (`http(s)`) → **WinHTTP**
- Window / input / surface → **Win32** + **Dawn D3D11/D3D12**

---

## Environment / globals

- `globalThis`, `self`, `global`
- `window`, `document`, `navigator`, `location`
- `navigator.userAgent = "DawnTestHost/1.0"`, `navigator.platform = "Win32"`, `navigator.gpu`
- `performance` (`performance.now()`)
- `console`
- `devicePixelRatio = 1`

## Timers & scheduling

- `requestAnimationFrame` / `cancelAnimationFrame` (driven by the native render loop)
- `setTimeout` / `clearTimeout`
- `setInterval` / `clearInterval`
- `queueMicrotask`

## Networking / fetch

- `fetch` — resolves:
  - local files (via the asset resolver / bundle + public asset tree)
  - remote `http(s)` URLs (native download — **WinHTTP** on Windows)
  - `data:` URLs (base64 + percent‑encoded)
- `Response`, `Request`, `Headers`
- **Not provided:** `XMLHttpRequest`

## Encoding / data utilities

- `TextEncoder` / `TextDecoder` (UTF‑8 only)
- `atob` / `btoa`
- `URL` (minimal parser; `createObjectURL` / `revokeObjectURL` are stubs)
- `URLSearchParams`
- `Blob` (+ `arrayBuffer`, `text`, `slice`, `stream`)
- `structuredClone` (arrays / plain objects / typed arrays)
- `BigInt` (degrades to `Number`)
- `AggregateError`
- `DOMException`

## Console

`log`, `info`, `warn`, `error`, `debug`, `trace`, `group`, `groupEnd`,
`time`, `timeEnd` — all written to `stderr`.

## DOM

- `document`: `getElementById`, `createElement`, `createElementNS`,
  `body` / `head` / `documentElement`, `querySelector` (→ `null`),
  `querySelectorAll` (→ empty array)
- A real **canvas** element: `getContext('webgpu')`, `getBoundingClientRect`,
  `width` / `height`, `toDataURL`, `toBlob`, `setAttribute` / `getAttribute`,
  pointer‑capture / focus no‑ops
- Generic element stubs: `appendChild`, `removeChild`, `insertBefore`,
  `contains`, attributes, etc.
- A **2D canvas context stub**: `measureText`, `getImageData`,
  `createImageData`, drawing methods as no‑ops

## Events & input

- `addEventListener` / `removeEventListener` on `window`, `document`, `canvas`
- **Pointer + wheel events** dispatched from native **Win32** input
- **Resize** events
- Event no‑ops: `preventDefault`, `stopPropagation`, `stopImmediatePropagation`

## Images

- `Image` (stub)
- `createImageBitmap` — backed by **native decode (WIC on Windows)**
- `ImageData` (via the 2D canvas stub)

## Observers / misc stubs (no‑ops)

- `ResizeObserver`
- `IntersectionObserver`
- `MutationObserver`
- `matchMedia`

## Crypto

- `crypto.getRandomValues`
- `crypto.randomUUID`

> Note: these use `Math.random()` and are **not** cryptographically secure.

## Streams / compression

- `DecompressionStream` / `CompressionStream` (gzip)
- Minimal `ReadableStream`‑like with `pipeThrough` / `pipeTo`
- `Blob.stream()`

## WebGPU (`navigator.gpu`, via Dawn)

All `GPU*` classes are wired to Dawn through the native `__wgpu` dispatch:

`GPUAdapter`, `GPUDevice`, `GPUQueue`, `GPUBuffer`, `GPUTexture`,
`GPUTextureView`, `GPUSampler`, `GPUShaderModule`, `GPUBindGroupLayout`,
`GPUBindGroup`, `GPUPipelineLayout`, `GPURenderPipeline`, `GPUComputePipeline`,
`GPUCommandEncoder`, `GPUCommandBuffer`, `GPURenderPassEncoder`,
`GPUComputePassEncoder`, `GPURenderBundleEncoder`, `GPURenderBundle`,
`GPUCanvasContext`, `GPUQuerySet`.

Bitflag enums: `GPUBufferUsage`, `GPUTextureUsage`, `GPUShaderStage`,
`GPUColorWrite`, `GPUMapMode`.

## Workers

- `Worker` — **real** workers (via `worker_shim`): each runs on its own OS
  thread with an isolated JS engine, communicating with the main thread through
  JSON `postMessage` / `onmessage`.

## Intentionally NOT polyfilled

- **`WebAssembly`** — a stub that throws and flags the scene as
  `__wasmUnsupported` / `__fatalError`; wasm‑dependent scenes are skipped, not run.
- No `OffscreenCanvas`, `FontFace`, `XMLHttpRequest`, `IndexedDB`, or `localStorage`.
