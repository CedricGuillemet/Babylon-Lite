/** MeshLoD device-recovery integration tests (Task 7.2 — architecture §14.3).
 *
 *  Device loss must restore MeshLoD coarse rendering through the GENERIC scene-owned
 *  recoverable seam (no MeshLoD branch in the recovery module): the geometry arena and
 *  shared page state are recreated on the new device, retained pinned coarse pages are
 *  re-decoded + re-uploaded, fine pages reset for opportunistic streaming, and the PBR
 *  packets rebuild. A pinned-coarse failure marks the asset failed with
 *  MLOD_DEVICE_RECOVERY and cannot claim success. Recovery is idempotent per device.
 *  The full device-lost flow is browser-validated (scene164); here the recoverable is
 *  driven directly with a swapped mock device. */

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { loadMeshLoD, createMeshLoDInstance } from "../../../../packages/babylon-lite/src/mesh-lod/mesh-lod.js";
import { addMeshLoDInstanceToScene } from "../../../../packages/babylon-lite/src/mesh-lod/mesh-lod-scene.js";
import { _setMeshLoDPageDecoder } from "../../../../packages/babylon-lite/src/mesh-lod/mesh-lod-page-decoder.js";
import { clearMeshLoDCpuPageCache } from "../../../../packages/babylon-lite/src/mesh-lod/mesh-lod-cache.js";
import type { MeshLoDAsset } from "../../../../packages/babylon-lite/src/mesh-lod/mesh-lod.js";
import type { SceneContext } from "../../../../packages/babylon-lite/src/scene/scene-core.js";
import type { Camera } from "../../../../packages/babylon-lite/src/camera/camera.js";
import type { PbrMaterialProps } from "../../../../packages/babylon-lite/src/material/pbr/pbr-material.js";
import type { EngineContext } from "../../../../packages/babylon-lite/src/engine/engine.js";
import type { RenderTargetSignature } from "../../../../packages/babylon-lite/src/engine/render-target.js";
import { createFillDecoder, createMockDevice, createMockEngine, createMockRenderPass } from "../../unit/mesh-lod/fixtures/gpu-mock.js";
import type { MockDevice } from "../../unit/mesh-lod/fixtures/gpu-mock.js";

const STATUE = fileURLToPath(new URL("../../../../lab/public/mesh-lod/harvard-yenching_institute_statue.mesh000.prim000.mlod", import.meta.url));
const statueSource = (): ArrayBuffer => new Uint8Array(readFileSync(STATUE)).slice().buffer as ArrayBuffer;
const SIG: RenderTargetSignature = { _colorFormat: "rgba8unorm", _depthStencilFormat: "depth24plus-stencil8", _sampleCount: 1 };

function fakeScene(engine: EngineContext): SceneContext {
    return { _deferredBuilders: [], _renderables: [], _disposables: [], surface: { engine } } as unknown as SceneContext;
}

function fakeCamera(): Camera {
    return {
        fov: 0.8,
        nearPlane: 0.1,
        farPlane: 100,
        worldMatrix: new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -10, 1]),
        worldMatrixVersion: 1,
        _viewCache: new Float32Array(16),
        _projCache: new Float32Array(16),
        _vpCache: new Float32Array(16),
        _viewVer: -1,
        _projVer: -1,
        _projAspect: -1,
        _vpVer: -1,
        _vpAspect: -1,
        _useFloatingOrigin: false,
    } as unknown as Camera;
}

const CONTEXT = { targetWidth: 800, targetHeight: 600, _camera: fakeCamera() };

interface Harness {
    engine: EngineContext;
    asset: MeshLoDAsset;
    scene: SceneContext;
}

async function setup(): Promise<Harness> {
    const engine = createMockEngine().engine;
    const asset = await loadMeshLoD(engine, statueSource(), { selectionMode: "gpu" });
    const scene = fakeScene(engine);
    addMeshLoDInstanceToScene(scene, createMeshLoDInstance(asset, {} as PbrMaterialProps));
    for (const builder of scene._deferredBuilders) {
        await builder();
    }
    scene._renderables[0]!.bind(engine, SIG).update!(CONTEXT); // one live frame on device A
    return { engine, asset, scene };
}

/** Simulate device loss + recreation, then run the generic recoverable seam (the branch the
 *  device-recovery flow executes after ordinary groups), pushing rebuilt renderables. */
async function recover(engine: EngineContext, scene: SceneContext): Promise<void> {
    const deviceB = createMockDevice();
    (engine as unknown as { _device: MockDevice })._device = deviceB;
    scene._renderables.length = 0;
    for (const r of scene._deferredGpuRecoverables!) {
        scene._renderables.push(...(await r.rebuild(engine, scene)));
    }
}

beforeEach(() => {
    _setMeshLoDPageDecoder(createFillDecoder().decoder);
});
afterEach(() => {
    _setMeshLoDPageDecoder(null);
});

describe("MeshLoD device recovery", () => {
    it("registers exactly one generic recoverable for the MeshLoD feature", async () => {
        const { scene } = await setup();
        expect(scene._deferredGpuRecoverables).toHaveLength(1);
    });

    it("rebuilds coarse residency + renderables on the recovered device", async () => {
        const { engine, asset, scene } = await setup();
        const runtime = asset._runtime;
        const oldArena = runtime.gpu.arena.buffer;
        const oldDevice = runtime.gpuDevice;
        const pinnedCount = runtime.pageRecords.filter((r) => r.pinned).length;

        await recover(engine, scene);

        expect(asset.state).toBe("ready");
        expect(runtime.gpuDevice).not.toBe(oldDevice);
        expect(runtime.gpuDevice).toBe(engine._device);
        expect(runtime.gpu.arena.buffer).not.toBe(oldArena); // a fresh arena on the new device
        expect(runtime.gpu.residentPageCount).toBe(pinnedCount); // pinned coarse fully restored
        expect(runtime.gpuSelection).toBeNull(); // shared selection buffers rebuild lazily
        expect(scene._renderables).toHaveLength(1);

        // The recovered coarse geometry renders on the new device.
        const binding = scene._renderables[0]!.bind(engine, SIG);
        binding.update!(CONTEXT);
        const pass = createMockRenderPass();
        expect(binding.draw(pass as unknown as GPURenderPassEncoder, engine)).toBe(1);
    });

    it("is idempotent per device (a second rebuild does not re-recover)", async () => {
        const { engine, asset, scene } = await setup();
        await recover(engine, scene);
        const arenaAfterFirst = asset._runtime.gpu.arena.buffer;
        const genAfterFirst = asset._runtime.generation;

        // Re-running the recoverable on the same device rebuilds renderables but not residency.
        scene._renderables.length = 0;
        for (const r of scene._deferredGpuRecoverables!) {
            scene._renderables.push(...(await r.rebuild(engine, scene)));
        }
        expect(asset._runtime.gpu.arena.buffer).toBe(arenaAfterFirst);
        expect(asset._runtime.generation).toBe(genAfterFirst);
    });

    it("marks the asset failed with MLOD_DEVICE_RECOVERY when retained pinned bytes are gone", async () => {
        const { engine, asset, scene } = await setup();
        // The retained pinned coarse bytes vanish before recovery (e.g. dropped cache).
        clearMeshLoDCpuPageCache(asset._runtime.cpuPageCache);

        await recover(engine, scene);

        expect(asset.state).toBe("failed");
        expect(asset.error?.code).toBe("MLOD_DEVICE_RECOVERY");
        expect(scene._renderables).toHaveLength(0); // the failed asset's batch is skipped
    });
});
