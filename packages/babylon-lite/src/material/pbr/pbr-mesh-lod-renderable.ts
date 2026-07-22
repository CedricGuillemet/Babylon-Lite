/** PBR-owned MeshLoD renderable — coarse CPU-selected indirect rendering.
 *
 *  Owns every MeshLoD rendering decision for the guaranteed opaque metallic-roughness
 *  subset: material validation + feature detection, the storage-fetch vertex + PBR/
 *  unlit fragment pipeline (WGSL in `pbr-mesh-lod-compose.ts`), the fixed v1 group-1
 *  bind group, fallback textures for absent guaranteed channels, per-frame CPU
 *  selection + expansion of pinned clusters into a draw-vertex stream, and one
 *  `drawIndirect` per material/hierarchy batch. The generic renderer only ever sees
 *  an ordinary opaque `Renderable`; no WGSL, pipeline, or bind-group code leaks out
 *  of `material/pbr`. Imported only through the MeshLoD scene path — no module-level
 *  side effects, so non-MeshLoD scenes fetch none of it. */

import { BU } from "../../engine/gpu-flags.js";
import type { EngineContext } from "../../engine/engine.js";
import type { SceneContext } from "../../scene/scene-core.js";
import type { Mat4 } from "../../math/types.js";
import type { DrawBinding, DrawUpdateContext, Renderable } from "../../render/renderable.js";
import type { RenderTargetSignature } from "../../engine/render-target.js";
import { REVERSE_DEPTH_COMPARE, targetSignatureKey } from "../../engine/render-target.js";
import { getSceneBindGroupLayout } from "../../render/scene-helpers.js";
import { createEmptyUniformBuffer } from "../../resource/gpu-buffers.js";
import type { Texture2D } from "../../texture/texture-2d.js";
import { createSolidTexture2D } from "../../texture/solid-texture.js";
import type { PbrMaterialProps } from "./pbr-material.js";
import type { MeshLoDSceneBatch } from "../../mesh-lod/mesh-lod-scene.js";
import { selectMeshLoDBatch } from "../../mesh-lod/mesh-lod-scene.js";
import { createMeshLoDError } from "../../mesh-lod/mesh-lod-errors.js";
import type { MeshLoDShaderFeatures } from "./pbr-mesh-lod-compose.js";
import { composeMeshLoDWgsl, meshLoDShaderKey } from "./pbr-mesh-lod-compose.js";

const DRAW_VERTEX_STRIDE = 16; // 4 × u32
const INSTANCE_STRIDE = 128; // world mat4 (64) + 3 normal-matrix vec4 (48) + pad (16)
const VERTEX_WORDS = 6; // 24-byte packed vertex / 4
const MATERIAL_UBO_BYTES = 80; // 5 × vec4

// ─── Fallback textures (device-keyed lazy cache, no module-level allocation) ──

interface MeshLoDFallbacks {
    readonly white: Texture2D;
    readonly normal: Texture2D;
    readonly black: Texture2D;
}

let _fallbacks: MeshLoDFallbacks | null = null;
let _fallbackDevice: GPUDevice | null = null;

function getFallbacks(engine: EngineContext): MeshLoDFallbacks {
    if (!_fallbacks || _fallbackDevice !== engine._device) {
        _fallbacks = {
            white: createSolidTexture2D(engine, 1, 1, 1, 1),
            normal: createSolidTexture2D(engine, 0.5, 0.5, 1, 1),
            black: createSolidTexture2D(engine, 0, 0, 0, 1),
        };
        _fallbackDevice = engine._device;
    }
    return _fallbacks;
}

// ─── Feature detection + validation ──────────────────────────────────

function detectFeatures(material: PbrMaterialProps): MeshLoDShaderFeatures {
    // The PBR module owns supported-material validation (architecture 13.2). Reject
    // anything outside the guaranteed opaque metallic-roughness subset even though
    // the scene registry validated it — this module is the authoritative gate.
    const unsupported =
        material.alphaBlend === true ||
        material.alphaCutOff !== undefined ||
        !!material.transmissive ||
        !!material.clearCoat ||
        !!material.sheen ||
        !!material.iridescence ||
        !!material.anisotropy ||
        !!material.subsurface ||
        !!material.specGlossTexture ||
        !!material.plugins?.length ||
        material.occlusionTexCoord === 1 ||
        !!material._uv2Mask ||
        !!material.shadowOnly ||
        !!material.skyboxMode;
    if (unsupported) {
        throw createMeshLoDError("MLOD_UNSUPPORTED_MATERIAL", "MeshLoD material is outside the guaranteed opaque metallic-roughness subset");
    }
    return {
        hasNormalMap: !!material.normalTexture,
        hasEmissiveTexture: !!material.emissiveTexture,
        doubleSided: material.doubleSided === true,
        unlit: material.unlit === true,
    };
}

// ─── Material UBO packing ────────────────────────────────────────────

function packMaterialUbo(material: PbrMaterialProps, features: MeshLoDShaderFeatures): Float32Array {
    const data = new Float32Array(MATERIAL_UBO_BYTES / 4);
    const bcf = material.baseColorFactor ?? [1, 1, 1, 1];
    const unlitColor = features.unlit ? (material.unlitColor ?? [1, 1, 1]) : [1, 1, 1];
    data[0] = bcf[0]! * unlitColor[0]!;
    data[1] = bcf[1]! * unlitColor[1]!;
    data[2] = bcf[2]! * unlitColor[2]!;
    data[3] = bcf[3]!;
    const emissive = material.emissiveColor ?? (features.hasEmissiveTexture ? [1, 1, 1] : [0, 0, 0]);
    data[4] = emissive[0]!;
    data[5] = emissive[1]!;
    data[6] = emissive[2]!;
    data[7] = 0;
    data[8] = material.metallicFactor ?? 1;
    data[9] = material.roughnessFactor ?? 1;
    data[10] = material.normalTextureScale ?? 1;
    data[11] = material.occlusionStrength ?? 1;
    data[12] = material.environmentIntensity ?? 1;
    data[13] = material.directIntensity ?? 1;
    data[14] = material.reflectance ?? 0.04;
    data[15] = material.alpha ?? 1;
    data[16] = material.usePhysicalLightFalloff === false ? 0 : 1;
    return data;
}

// ─── Per-batch GPU packet ────────────────────────────────────────────

interface MeshLoDBatchPacket {
    readonly features: MeshLoDShaderFeatures;
    readonly shaderModule: GPUShaderModule;
    readonly bindGroupLayout: GPUBindGroupLayout;
    readonly bindGroup: GPUBindGroup;
    readonly pipelines: Map<string, GPURenderPipeline>;
    readonly drawVertexBuffer: GPUBuffer;
    readonly instanceBuffer: GPUBuffer;
    readonly indirectBuffer: GPUBuffer;
    readonly drawScratch: Uint32Array;
    readonly instanceScratch: Float32Array;
    readonly indirectScratch: Uint32Array;
    readonly maxDrawVertices: number;
    readonly maxInstances: number;
    lastVertexCount: number;
    dispose(): void;
}

function buildBindGroup(
    engine: EngineContext,
    layout: GPUBindGroupLayout,
    material: PbrMaterialProps,
    drawVertexBuffer: GPUBuffer,
    instanceBuffer: GPUBuffer,
    arena: GPUBuffer,
    materialUbo: GPUBuffer
): GPUBindGroup {
    const fb = getFallbacks(engine);
    const base = material.baseColorTexture ?? fb.white;
    const normal = material.normalTexture ?? fb.normal;
    const orm = material.ormTexture ?? fb.white;
    const emissive = material.emissiveTexture ?? fb.black;
    return engine._device.createBindGroup({
        layout,
        entries: [
            { binding: 0, resource: { buffer: materialUbo } },
            { binding: 1, resource: base.view },
            { binding: 2, resource: base.sampler },
            { binding: 3, resource: normal.view },
            { binding: 4, resource: normal.sampler },
            { binding: 5, resource: orm.view },
            { binding: 6, resource: orm.sampler },
            { binding: 7, resource: emissive.view },
            { binding: 8, resource: emissive.sampler },
            { binding: 9, resource: { buffer: arena } },
            { binding: 10, resource: { buffer: drawVertexBuffer } },
            { binding: 11, resource: { buffer: instanceBuffer } },
        ],
    });
}

function meshLoDBindGroupLayout(engine: EngineContext): GPUBindGroupLayout {
    const V = 0x1;
    const F = 0x2;
    const tex = (binding: number): GPUBindGroupLayoutEntry[] => [
        { binding, visibility: F, texture: { sampleType: "float" } },
        { binding: binding + 1, visibility: F, sampler: { type: "filtering" } },
    ];
    return engine._device.createBindGroupLayout({
        label: "mesh-lod-material",
        entries: [
            { binding: 0, visibility: V | F, buffer: { type: "uniform" } },
            ...tex(1),
            ...tex(3),
            ...tex(5),
            ...tex(7),
            { binding: 9, visibility: V, buffer: { type: "read-only-storage" } },
            { binding: 10, visibility: V, buffer: { type: "read-only-storage" } },
            { binding: 11, visibility: V, buffer: { type: "read-only-storage" } },
        ],
    });
}

function getPipeline(engine: EngineContext, packet: MeshLoDBatchPacket, sig: RenderTargetSignature): GPURenderPipeline {
    const key = targetSignatureKey(sig);
    const cached = packet.pipelines.get(key);
    if (cached) {
        return cached;
    }
    const device = engine._device;
    const layout = device.createPipelineLayout({ bindGroupLayouts: [getSceneBindGroupLayout(engine), packet.bindGroupLayout] });
    const pipeline = device.createRenderPipeline({
        label: "mesh-lod",
        layout,
        vertex: { module: packet.shaderModule, entryPoint: "vs", buffers: [] },
        fragment: sig._colorFormat ? { module: packet.shaderModule, entryPoint: "fs", targets: [{ format: sig._colorFormat }] } : undefined,
        depthStencil: sig._depthStencilFormat ? { format: sig._depthStencilFormat, depthCompare: sig._depthCompare ?? REVERSE_DEPTH_COMPARE, depthWriteEnabled: true } : undefined,
        multisample: { count: sig._sampleCount },
        primitive: { topology: "triangle-list", cullMode: packet.features.doubleSided ? "none" : "back", frontFace: "ccw" },
    });
    packet.pipelines.set(key, pipeline);
    return pipeline;
}

function writeInstanceRecord(out: Float32Array, floatOffset: number, world: Mat4): void {
    for (let i = 0; i < 16; i++) {
        out[floatOffset + i] = world[i]!;
    }
    // Normal matrix ∝ cofactor matrix = (c1×c2, c2×c0, c0×c1) as columns — the
    // inverse-transpose up to determinant, correct after the shader's normalize().
    const c0x = world[0]!,
        c0y = world[1]!,
        c0z = world[2]!;
    const c1x = world[4]!,
        c1y = world[5]!,
        c1z = world[6]!;
    const c2x = world[8]!,
        c2y = world[9]!,
        c2z = world[10]!;
    // n0 = c1 × c2
    out[floatOffset + 16] = c1y * c2z - c1z * c2y;
    out[floatOffset + 17] = c1z * c2x - c1x * c2z;
    out[floatOffset + 18] = c1x * c2y - c1y * c2x;
    out[floatOffset + 19] = 0;
    // n1 = c2 × c0
    out[floatOffset + 20] = c2y * c0z - c2z * c0y;
    out[floatOffset + 21] = c2z * c0x - c2x * c0z;
    out[floatOffset + 22] = c2x * c0y - c2y * c0x;
    out[floatOffset + 23] = 0;
    // n2 = c0 × c1
    out[floatOffset + 24] = c0y * c1z - c0z * c1y;
    out[floatOffset + 25] = c0z * c1x - c0x * c1z;
    out[floatOffset + 26] = c0x * c1y - c0y * c1x;
    out[floatOffset + 27] = 0;
}

/** Per-frame CPU selection + expansion: run the oracle for each visible instance,
 *  write its world/normal matrices, and flatten selected pinned clusters into the
 *  draw-vertex stream, then publish the single indirect vertex count. */
function updatePacket(engine: EngineContext, batch: MeshLoDSceneBatch, packet: MeshLoDBatchPacket, context: DrawUpdateContext): void {
    const runtime = batch.asset._runtime;
    const selections = selectMeshLoDBatch(batch, context);
    const draw = packet.drawScratch;
    const inst = packet.instanceScratch;
    let vertexCount = 0;
    let instanceIndex = 0;
    let selectedMeshlets = 0;

    outer: for (const selection of selections) {
        if (instanceIndex >= packet.maxInstances) {
            break;
        }
        const localInstance = instanceIndex;
        writeInstanceRecord(inst, localInstance * (INSTANCE_STRIDE / 4), selection.instance.worldMatrix);
        instanceIndex++;
        for (const clusterId of selection.result.selectedClusterIds) {
            const cluster = runtime.clusters[clusterId]!;
            const page = runtime.gpu.pages[cluster.pageId];
            if (!page || page.state !== "gpu-resident" || !page.indices || page.arenaOffset < 0) {
                continue;
            }
            const arenaWordBase = (page.arenaOffset + page.vertexByteOffset) / 4;
            const indices = page.indices;
            const start = cluster.indexOffset;
            const count = cluster.triangleCount * 3;
            selectedMeshlets++;
            for (let k = 0; k < count; k++) {
                if (vertexCount >= packet.maxDrawVertices) {
                    break outer; // capacity guard — never write out of bounds
                }
                const localVertex = indices[start + k]!;
                const o = vertexCount * 4;
                draw[o] = arenaWordBase + localVertex * VERTEX_WORDS;
                draw[o + 1] = clusterId;
                draw[o + 2] = localInstance;
                draw[o + 3] = 0;
                vertexCount++;
            }
        }
    }

    if (instanceIndex > 0) {
        engine._device.queue.writeBuffer(packet.instanceBuffer, 0, inst.buffer, inst.byteOffset, instanceIndex * INSTANCE_STRIDE);
    }
    if (vertexCount > 0) {
        engine._device.queue.writeBuffer(packet.drawVertexBuffer, 0, draw.buffer, draw.byteOffset, vertexCount * DRAW_VERTEX_STRIDE);
    }
    packet.indirectScratch[0] = vertexCount;
    packet.indirectScratch[1] = 1;
    packet.indirectScratch[2] = 0;
    packet.indirectScratch[3] = 0;
    engine._device.queue.writeBuffer(packet.indirectBuffer, 0, packet.indirectScratch.buffer, packet.indirectScratch.byteOffset, 16);
    packet.lastVertexCount = vertexCount;

    const diag = runtime.diagnostics as { renderedTriangleCount: number; selectedMeshletCount: number };
    diag.renderedTriangleCount = vertexCount / 3;
    diag.selectedMeshletCount = selectedMeshlets;
}

/** Build the single indirect-draw `Renderable` for one MeshLoD batch, or `null`
 *  when the batch has no instances. */
export function buildMeshLoDBatchRenderable(engine: EngineContext, _scene: SceneContext, batch: MeshLoDSceneBatch): Renderable | null {
    if (batch.instances.length === 0) {
        return null;
    }
    const runtime = batch.asset._runtime;
    const features = detectFeatures(batch.material);

    // Coarse capacity: total expanded vertices across every pinned (resident) cluster.
    let coarseVertices = 0;
    for (const cluster of runtime.clusters) {
        if (runtime.pageRecords[cluster.pageId]?.pinned) {
            coarseVertices += cluster.triangleCount * 3;
        }
    }
    const maxInstances = Math.max(batch.instances.length, 1);
    const maxDrawVertices = Math.max(coarseVertices * maxInstances, 3);

    const device = engine._device;
    const materialUbo = createEmptyUniformBuffer(engine, MATERIAL_UBO_BYTES, "mesh-lod-material");
    device.queue.writeBuffer(materialUbo, 0, packMaterialUbo(batch.material, features) as Float32Array<ArrayBuffer>);

    const drawVertexBuffer = device.createBuffer({ label: "mesh-lod-draw-vertices", size: maxDrawVertices * DRAW_VERTEX_STRIDE, usage: BU.STORAGE | BU.COPY_DST });
    const instanceBuffer = device.createBuffer({ label: "mesh-lod-instances", size: maxInstances * INSTANCE_STRIDE, usage: BU.STORAGE | BU.COPY_DST });
    const indirectBuffer = device.createBuffer({ label: "mesh-lod-indirect", size: 16, usage: BU.INDIRECT | BU.COPY_DST });

    const bindGroupLayout = meshLoDBindGroupLayout(engine);
    const shaderModule = device.createShaderModule({ label: `mesh-lod-${meshLoDShaderKey(features)}`, code: composeMeshLoDWgsl(features) });

    const packet: MeshLoDBatchPacket = {
        features,
        shaderModule,
        bindGroupLayout,
        bindGroup: buildBindGroup(engine, bindGroupLayout, batch.material, drawVertexBuffer, instanceBuffer, runtime.gpu.arena.buffer, materialUbo),
        pipelines: new Map(),
        drawVertexBuffer,
        instanceBuffer,
        indirectBuffer,
        drawScratch: new Uint32Array(maxDrawVertices * 4),
        instanceScratch: new Float32Array(maxInstances * (INSTANCE_STRIDE / 4)),
        indirectScratch: new Uint32Array(4),
        maxDrawVertices,
        maxInstances,
        lastVertexCount: 0,
        dispose: () => {
            materialUbo.destroy();
            drawVertexBuffer.destroy();
            instanceBuffer.destroy();
            indirectBuffer.destroy();
        },
    };
    (batch as { _packet?: unknown })._packet = packet;

    const renderable: Renderable = {
        order: 100,
        isTransparent: false,
        bind(eng: EngineContext, sig: RenderTargetSignature): DrawBinding {
            const pipeline = getPipeline(eng, packet, sig);
            return {
                renderable,
                pipeline,
                update: (context: DrawUpdateContext) => updatePacket(eng, batch, packet, context),
                draw: (pass: GPURenderPassEncoder | GPURenderBundleEncoder): number => {
                    pass.setBindGroup(1, packet.bindGroup);
                    pass.drawIndirect(packet.indirectBuffer, 0);
                    return 1;
                },
            };
        },
    };
    return renderable;
}
