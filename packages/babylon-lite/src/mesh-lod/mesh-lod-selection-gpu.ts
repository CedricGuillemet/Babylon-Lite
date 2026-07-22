/** MeshLoD GPU selection buffers — persistent packed metadata and per-batch state.
 *
 *  Phase 5 moves selection/expansion to compute. This module owns the CPU→GPU data
 *  contract only (architecture §12.1): it packs the parsed immutable hierarchy into
 *  the exact `Uint32Array` word layouts the selection WGSL reads, allocates the
 *  device-limit-checked persistent storage buffers, keeps the mutable page-state and
 *  per-instance/prior-state buffers in sync, and grows the transient-capacity buffers
 *  make-before-break. It never parses WGSL strings and never exposes a raw GPU handle
 *  outside the internal runtime/material records.
 *
 *  Buffer ownership mirrors the sharing model:
 *  - immutable node/group/cluster/page-ref buffers and the mutable page-state buffer
 *    are per **asset** (`MeshLoDGpuAssetBuffers`), so every scene batch and instance
 *    that shares one hierarchy reuses them;
 *  - the instance transform buffer and per-instance/group prior-hysteresis bitset are
 *    per **batch** (`MeshLoDGpuInstanceState`), so instance state stays independent. */

import { BU } from "../engine/gpu-flags.js";
import type { EngineContext } from "../engine/engine.js";
import { retireGpuResources } from "../engine/gpu-resource-retirement.js";
import { createMeshLoDError } from "./mesh-lod-errors.js";
import { maxColumnScale } from "./mesh-lod-selection-math.js";
import type { MeshLoDAssetRuntime, MeshLoDCluster, MeshLoDGroup, MeshLoDHierarchyNode, MeshLoDPageRecord, MeshLoDPageRuntime } from "./mesh-lod-runtime.js";

// ─── Record word/byte layouts (architecture §12.1) ───────────────────

/** 8 × u32 words per 32-byte hierarchy node. */
export const NODE_WORDS = 8;
/** 16 × u32 words per 64-byte group. */
export const GROUP_WORDS = 16;
/** 16 × u32 words per 64-byte cluster. */
export const CLUSTER_WORDS = 16;
/** 8 × u32 words per 32-byte page-state record. */
export const PAGE_STATE_WORDS = 8;
/** 128-byte / 32-word instance record. */
export const INSTANCE_BYTES = 128;
export const INSTANCE_WORDS = INSTANCE_BYTES / 4;

/** Page-state flag bits (word 0). */
export const PAGE_FLAG_RESIDENT = 0x1;
export const PAGE_FLAG_PINNED = 0x2;
export const PAGE_FLAG_FAILED = 0x4;
export const PAGE_FLAG_UPLOADING = 0x8;

const F32_SCRATCH = new Float32Array(1);
const U32_SCRATCH = new Uint32Array(F32_SCRATCH.buffer);

/** Reinterpret a float32 as its u32 bit pattern (the WGSL `bitcast<u32>` a shader
 *  reverses with `bitcast<f32>`). */
function f32Bits(value: number): number {
    F32_SCRATCH[0] = value;
    return U32_SCRATCH[0]!;
}

// ─── Immutable metadata packing ──────────────────────────────────────

/** Pack the 8-wide hierarchy nodes into `NODE_WORDS` words each: center xyz + radius
 *  + error as float bits, then `groupId` (i32, `-1` internal), child offset, child
 *  count. */
export function packHierarchyNodes(nodes: readonly MeshLoDHierarchyNode[]): Uint32Array {
    const out = new Uint32Array(Math.max(nodes.length, 1) * NODE_WORDS);
    for (let i = 0; i < nodes.length; i++) {
        const n = nodes[i]!;
        const b = i * NODE_WORDS;
        out[b] = f32Bits(n.center[0]);
        out[b + 1] = f32Bits(n.center[1]);
        out[b + 2] = f32Bits(n.center[2]);
        out[b + 3] = f32Bits(n.radius);
        out[b + 4] = f32Bits(n.error);
        out[b + 5] = n.groupId | 0;
        out[b + 6] = n.childOffset;
        out[b + 7] = n.childCount;
    }
    return out;
}

/** Pack groups into `GROUP_WORDS` words: center/radius/simplifiedError float bits,
 *  depth, cluster range, page-ref range, terminal/pinned flags, and triangle counts. */
export function packGroups(groups: readonly MeshLoDGroup[]): Uint32Array {
    const out = new Uint32Array(Math.max(groups.length, 1) * GROUP_WORDS);
    for (let i = 0; i < groups.length; i++) {
        const g = groups[i]!;
        const b = i * GROUP_WORDS;
        out[b] = f32Bits(g.center[0]);
        out[b + 1] = f32Bits(g.center[1]);
        out[b + 2] = f32Bits(g.center[2]);
        out[b + 3] = f32Bits(g.radius);
        out[b + 4] = f32Bits(g.simplifiedError);
        out[b + 5] = g.depth;
        out[b + 6] = g.firstCluster;
        out[b + 7] = g.clusterCount;
        out[b + 8] = g.firstPageRef;
        out[b + 9] = g.pageRefCount;
        out[b + 10] = (g.terminal ? 0x1 : 0) | (g.pinned ? 0x2 : 0);
        out[b + 11] = g.sourceTriangleCount;
        out[b + 12] = g.outputTriangleCount;
    }
    return out;
}

/** Pack clusters into `CLUSTER_WORDS` words: center/radius/error float bits, group
 *  id, `refinedGroupId` (i32, `-1` finest), page id, vertex/index offsets, vertex and
 *  triangle counts. */
export function packClusters(clusters: readonly MeshLoDCluster[]): Uint32Array {
    const out = new Uint32Array(Math.max(clusters.length, 1) * CLUSTER_WORDS);
    for (let i = 0; i < clusters.length; i++) {
        const c = clusters[i]!;
        const b = i * CLUSTER_WORDS;
        out[b] = f32Bits(c.center[0]);
        out[b + 1] = f32Bits(c.center[1]);
        out[b + 2] = f32Bits(c.center[2]);
        out[b + 3] = f32Bits(c.radius);
        out[b + 4] = f32Bits(c.error);
        out[b + 5] = c.groupId;
        out[b + 6] = c.refinedGroupId | 0;
        out[b + 7] = c.pageId;
        out[b + 8] = c.vertexOffset;
        out[b + 9] = c.indexOffset;
        out[b + 10] = c.vertexCount;
        out[b + 11] = c.triangleCount;
        out[b + 12] = c.sourceTriangleCount;
    }
    return out;
}

/** Pack the flat group→page-ref table as raw `u32` page IDs. */
export function packGroupPageRefs(refs: Uint32Array): Uint32Array {
    return refs.length > 0 ? Uint32Array.from(refs) : new Uint32Array(1);
}

/** Write one page-state record (`PAGE_STATE_WORDS` words) from a runtime page and its
 *  immutable record. Vertex/index byte offsets are absolute within the arena so the
 *  expansion kernel can index the raw geometry directly; they read as zero until the
 *  page is resident. */
export function writePageStateRecord(out: Uint32Array, wordBase: number, page: MeshLoDPageRuntime, record: MeshLoDPageRecord, generation: number): void {
    const resident = page.state === "gpu-resident" && page.arenaOffset >= 0;
    const base = resident ? page.arenaOffset : 0;
    out[wordBase] = (resident ? PAGE_FLAG_RESIDENT : 0) | (record.pinned ? PAGE_FLAG_PINNED : 0) | (page.state === "terminal-failed" ? PAGE_FLAG_FAILED : 0);
    out[wordBase + 1] = base;
    out[wordBase + 2] = resident ? base + record.vertexByteOffset : 0;
    out[wordBase + 3] = resident ? base + record.indexByteOffset : 0;
    out[wordBase + 4] = record.vertexCount;
    out[wordBase + 5] = record.localIndexCount;
    out[wordBase + 6] = resident ? generation : 0;
    out[wordBase + 7] = 0;
}

/** Build the full page-state array from the current runtime residency. */
export function buildPageStateData(pages: readonly MeshLoDPageRuntime[], records: readonly MeshLoDPageRecord[], generation: number): Uint32Array {
    const out = new Uint32Array(Math.max(pages.length, 1) * PAGE_STATE_WORDS);
    for (let i = 0; i < pages.length; i++) {
        writePageStateRecord(out, i * PAGE_STATE_WORDS, pages[i]!, records[i]!, generation);
    }
    return out;
}

// ─── Instance record packing ─────────────────────────────────────────

/** Structural instance input consumed by {@link uploadMeshLoDInstances}; avoids a
 *  runtime dependency on the public `MeshLoDInstance` type. */
export interface MeshLoDGpuInstanceInput {
    readonly worldMatrix: ArrayLike<number>;
    readonly worldMatrixVersion: number;
    readonly visible: boolean;
    /** @internal Stable per-asset instance ID (slot identity for version gating). */
    readonly _instanceId: number;
}

/** Write one 128-byte instance record: world matrix, cofactor normal matrix as three
 *  padded `vec4` rows, maximum world scale, visibility flag, and stable instance ID.
 *  The cofactor matrix `(c1×c2, c2×c0, c0×c1)` is the inverse-transpose up to
 *  determinant — correct after the shader's `normalize()`. */
export function packInstanceRecord(f32: Float32Array, u32: Uint32Array, wordBase: number, world: ArrayLike<number>, visible: boolean, instanceId: number): void {
    for (let i = 0; i < 16; i++) {
        f32[wordBase + i] = world[i]!;
    }
    const c0x = world[0]!,
        c0y = world[1]!,
        c0z = world[2]!;
    const c1x = world[4]!,
        c1y = world[5]!,
        c1z = world[6]!;
    const c2x = world[8]!,
        c2y = world[9]!,
        c2z = world[10]!;
    f32[wordBase + 16] = c1y * c2z - c1z * c2y;
    f32[wordBase + 17] = c1z * c2x - c1x * c2z;
    f32[wordBase + 18] = c1x * c2y - c1y * c2x;
    f32[wordBase + 19] = 0;
    f32[wordBase + 20] = c2y * c0z - c2z * c0y;
    f32[wordBase + 21] = c2z * c0x - c2x * c0z;
    f32[wordBase + 22] = c2x * c0y - c2y * c0x;
    f32[wordBase + 23] = 0;
    f32[wordBase + 24] = c0y * c1z - c0z * c1y;
    f32[wordBase + 25] = c0z * c1x - c0x * c1z;
    f32[wordBase + 26] = c0x * c1y - c0y * c1x;
    f32[wordBase + 27] = 0;
    f32[wordBase + 28] = maxColumnScale(world); // bytes 112–115: maximum world scale
    u32[wordBase + 29] = visible ? 1 : 0; // bytes 116–119: visibility flags
    u32[wordBase + 30] = instanceId >>> 0; // bytes 120–123: stable instance ID
    u32[wordBase + 31] = 0; // bytes 124–127: zero
}

// ─── Device-limit-checked storage buffers ────────────────────────────

function checkedStorageBuffer(engine: EngineContext, byteLength: number, label: string, extraUsage = 0): GPUBuffer {
    const size = Math.max(byteLength, 4);
    const limits = engine._device.limits;
    const cap = Math.min(limits?.maxStorageBufferBindingSize ?? size, limits?.maxBufferSize ?? size);
    if (size > cap) {
        throw createMeshLoDError("MLOD_DEVICE_LIMIT", `MeshLoD ${label} exceeds the device storage-buffer limit`, { expected: cap, actual: size });
    }
    return engine._device.createBuffer({ label, size, usage: BU.STORAGE | BU.COPY_DST | extraUsage });
}

/** Cheap content-sensitive signature over (generation, per-page residency + arena
 *  offset). It changes whenever which pages are resident or where they live changes,
 *  so the page-state upload can be skipped when nothing moved — including the
 *  evict-A/load-B case where the resident count is unchanged. */
function residencySignature(runtime: MeshLoDAssetRuntime): number {
    let sig = Math.imul(runtime.generation + 1, 2654435761) >>> 0;
    const pages = runtime.gpu.pages;
    for (let i = 0; i < pages.length; i++) {
        const p = pages[i]!;
        const resident = p.state === "gpu-resident" && p.arenaOffset >= 0;
        const failed = p.state === "terminal-failed" ? 0x40000000 : 0;
        sig = (sig ^ ((resident ? p.arenaOffset + 1 : 0) | failed)) >>> 0;
        sig = Math.imul(sig ^ i, 2246822519) >>> 0;
    }
    return sig;
}

// ─── Per-asset persistent buffers ────────────────────────────────────

/** Immutable per-asset hierarchy buffers plus the mutable page-state buffer. Shared
 *  by every scene batch and instance referencing the same asset. */
export interface MeshLoDGpuAssetBuffers {
    readonly device: GPUDevice;
    readonly nodeBuffer: GPUBuffer;
    readonly groupBuffer: GPUBuffer;
    readonly clusterBuffer: GPUBuffer;
    readonly pageRefBuffer: GPUBuffer;
    readonly pageStateBuffer: GPUBuffer;
    /** CPU mirror of the page-state words, re-derived and re-uploaded on residency change. */
    readonly pageStateData: Uint32Array;
    readonly nodeCount: number;
    readonly groupCount: number;
    readonly clusterCount: number;
    readonly pageCount: number;
    /** Residency generation of the last page-state upload, to skip redundant writes. */
    residencyEpoch: number;
    /** Total bytes across the owned buffers, for diagnostics only. */
    readonly byteLength: number;
}

/** Build (once) and return the shared per-asset GPU buffers, caching them on the
 *  runtime. Recreated after a device change so caches auto-invalidate. */
export function getMeshLoDGpuAssetBuffers(engine: EngineContext, runtime: MeshLoDAssetRuntime): MeshLoDGpuAssetBuffers {
    const existing = runtime.gpuSelection;
    if (existing && existing.device === engine._device) {
        return existing;
    }
    if (existing) {
        disposeMeshLoDGpuAssetBuffers(existing);
    }

    const nodes = packHierarchyNodes(runtime.hierarchyNodes);
    const groups = packGroups(runtime.groups);
    const clusters = packClusters(runtime.clusters);
    const refs = packGroupPageRefs(runtime.groupPageRefs);
    const pageState = buildPageStateData(runtime.gpu.pages, runtime.pageRecords, runtime.generation);

    const nodeBuffer = checkedStorageBuffer(engine, nodes.byteLength, "mesh-lod-nodes");
    const groupBuffer = checkedStorageBuffer(engine, groups.byteLength, "mesh-lod-groups");
    const clusterBuffer = checkedStorageBuffer(engine, clusters.byteLength, "mesh-lod-clusters");
    const pageRefBuffer = checkedStorageBuffer(engine, refs.byteLength, "mesh-lod-page-refs");
    const pageStateBuffer = checkedStorageBuffer(engine, pageState.byteLength, "mesh-lod-page-state");

    const q = engine._device.queue;
    q.writeBuffer(nodeBuffer, 0, nodes.buffer, nodes.byteOffset, nodes.byteLength);
    q.writeBuffer(groupBuffer, 0, groups.buffer, groups.byteOffset, groups.byteLength);
    q.writeBuffer(clusterBuffer, 0, clusters.buffer, clusters.byteOffset, clusters.byteLength);
    q.writeBuffer(pageRefBuffer, 0, refs.buffer, refs.byteOffset, refs.byteLength);
    q.writeBuffer(pageStateBuffer, 0, pageState.buffer, pageState.byteOffset, pageState.byteLength);

    const buffers: MeshLoDGpuAssetBuffers = {
        device: engine._device,
        nodeBuffer,
        groupBuffer,
        clusterBuffer,
        pageRefBuffer,
        pageStateBuffer,
        pageStateData: pageState,
        nodeCount: runtime.hierarchyNodes.length,
        groupCount: runtime.groups.length,
        clusterCount: runtime.clusters.length,
        pageCount: runtime.gpu.pages.length,
        residencyEpoch: residencySignature(runtime),
        byteLength: nodes.byteLength + groups.byteLength + clusters.byteLength + refs.byteLength + pageState.byteLength,
    };
    runtime.gpuSelection = buffers;
    return buffers;
}

/** Re-derive and re-upload the mutable page-state buffer from current residency.
 *  A monotonically bumped `residencyEpoch` skips the upload when nothing changed. */
export function syncMeshLoDPageState(engine: EngineContext, buffers: MeshLoDGpuAssetBuffers, runtime: MeshLoDAssetRuntime, force = false): void {
    const signature = residencySignature(runtime);
    if (!force && buffers.residencyEpoch === signature) {
        return;
    }
    const data = buffers.pageStateData;
    for (let i = 0; i < runtime.gpu.pages.length; i++) {
        writePageStateRecord(data, i * PAGE_STATE_WORDS, runtime.gpu.pages[i]!, runtime.pageRecords[i]!, runtime.generation);
    }
    engine._device.queue.writeBuffer(buffers.pageStateBuffer, 0, data.buffer, data.byteOffset, data.byteLength);
    buffers.residencyEpoch = signature;
}

/** Destroy all buffers owned by a per-asset record. Idempotent-safe. */
export function disposeMeshLoDGpuAssetBuffers(buffers: MeshLoDGpuAssetBuffers): void {
    buffers.nodeBuffer.destroy();
    buffers.groupBuffer.destroy();
    buffers.clusterBuffer.destroy();
    buffers.pageRefBuffer.destroy();
    buffers.pageStateBuffer.destroy();
}

// ─── Per-batch instance + prior-hysteresis state ─────────────────────

/** Per-batch instance transform buffer and per-instance/group prior-hysteresis
 *  bitset. Both grow make-before-break as the batch's instance count rises. */
export interface MeshLoDGpuInstanceState {
    readonly groupCount: number;
    /** `u32` words of prior-state bitset per instance (`ceil(groupCount / 32)`). */
    readonly wordsPerInstance: number;
    capacity: number;
    instanceCount: number;
    device: GPUDevice | null;
    instanceBuffer: GPUBuffer | null;
    priorStateBuffer: GPUBuffer | null;
    /** CPU scratch for packing instance records (`capacity * INSTANCE_WORDS`). */
    scratchF32: Float32Array;
    scratchU32: Uint32Array;
    /** Last-uploaded `worldMatrixVersion` per slot (`-1` = dirty). */
    slotVersion: Int32Array;
    /** Last-uploaded stable instance ID per slot (`-1` = empty). */
    slotId: Int32Array;
    /** Last-uploaded visibility per slot. */
    slotVisible: Uint8Array;
}

/** Create empty per-batch instance state for a hierarchy with `groupCount` groups. */
export function createMeshLoDGpuInstanceState(groupCount: number): MeshLoDGpuInstanceState {
    return {
        groupCount,
        wordsPerInstance: Math.max(Math.ceil(groupCount / 32), 1),
        capacity: 0,
        instanceCount: 0,
        device: null,
        instanceBuffer: null,
        priorStateBuffer: null,
        scratchF32: new Float32Array(0),
        scratchU32: new Uint32Array(0),
        slotVersion: new Int32Array(0),
        slotId: new Int32Array(0),
        slotVisible: new Uint8Array(0),
    };
}

/** Grow (never shrink) the instance and prior-state buffers to hold `count` slots,
 *  following make-before-break: allocate the replacements, copy the retained
 *  prior-state bits, switch the state to the new buffers, and retire the old ones
 *  after the next frame drains. All slot version tracking resets so the next upload
 *  fully repopulates the new instance buffer. */
export function ensureMeshLoDInstanceCapacity(engine: EngineContext, state: MeshLoDGpuInstanceState, count: number): void {
    const device = engine._device;
    if (state.device !== device) {
        // Device change: drop stale handles; caches auto-invalidate.
        state.instanceBuffer = null;
        state.priorStateBuffer = null;
        state.capacity = 0;
        state.device = device;
    }
    if (count <= state.capacity && state.instanceBuffer && state.priorStateBuffer) {
        return;
    }

    let capacity = Math.max(state.capacity, 1);
    while (capacity < count) {
        capacity *= 2;
    }

    const oldInstance = state.instanceBuffer;
    const oldPrior = state.priorStateBuffer;
    const oldCapacity = state.capacity;

    const newInstance = device.createBuffer({ label: "mesh-lod-instances", size: capacity * INSTANCE_BYTES, usage: BU.STORAGE | BU.COPY_DST });
    const priorBytes = capacity * state.wordsPerInstance * 4;
    const newPrior = device.createBuffer({ label: "mesh-lod-prior-state", size: Math.max(priorBytes, 4), usage: BU.STORAGE | BU.COPY_DST | BU.COPY_SRC });

    // Preserve resident hysteresis bits for the slots that already existed.
    if (oldPrior && oldCapacity > 0) {
        engine._currentEncoder.copyBufferToBuffer(oldPrior, 0, newPrior, 0, oldCapacity * state.wordsPerInstance * 4);
    }

    state.instanceBuffer = newInstance;
    state.priorStateBuffer = newPrior;
    state.capacity = capacity;
    state.scratchF32 = new Float32Array(capacity * INSTANCE_WORDS);
    state.scratchU32 = new Uint32Array(state.scratchF32.buffer);
    state.slotVersion = new Int32Array(capacity).fill(-1);
    state.slotId = new Int32Array(capacity).fill(-1);
    state.slotVisible = new Uint8Array(capacity);

    if (oldInstance || oldPrior) {
        retireGpuResources(engine, () => {
            oldInstance?.destroy();
            oldPrior?.destroy();
        });
    }
}

/** Version-gate and upload changed instance records. Each instance occupies a stable
 *  slot equal to its index; a slot is re-packed only when its transform version,
 *  stable ID, or visibility changed since the last upload. Contiguous dirty slots are
 *  coalesced into one `writeBuffer`. Returns the active instance count. */
export function uploadMeshLoDInstances(engine: EngineContext, state: MeshLoDGpuInstanceState, instances: readonly MeshLoDGpuInstanceInput[]): number {
    ensureMeshLoDInstanceCapacity(engine, state, Math.max(instances.length, 1));
    const f32 = state.scratchF32;
    const u32 = state.scratchU32;
    let runStart = -1;
    const flush = (endExclusive: number): void => {
        if (runStart < 0) {
            return;
        }
        engine._device.queue.writeBuffer(
            state.instanceBuffer!,
            runStart * INSTANCE_BYTES,
            f32.buffer,
            f32.byteOffset + runStart * INSTANCE_BYTES,
            (endExclusive - runStart) * INSTANCE_BYTES
        );
        runStart = -1;
    };
    for (let i = 0; i < instances.length; i++) {
        const inst = instances[i]!;
        const visible = inst.visible ? 1 : 0;
        const dirty = state.slotVersion[i] !== inst.worldMatrixVersion || state.slotId[i] !== inst._instanceId || state.slotVisible[i] !== visible;
        if (dirty) {
            packInstanceRecord(f32, u32, i * INSTANCE_WORDS, inst.worldMatrix, inst.visible, inst._instanceId);
            state.slotVersion[i] = inst.worldMatrixVersion;
            state.slotId[i] = inst._instanceId;
            state.slotVisible[i] = visible;
            if (runStart < 0) {
                runStart = i;
            }
        } else {
            flush(i);
        }
    }
    flush(instances.length);
    state.instanceCount = instances.length;
    return instances.length;
}

/** Destroy the per-batch instance/prior-state buffers. Idempotent-safe. */
export function disposeMeshLoDGpuInstanceState(state: MeshLoDGpuInstanceState): void {
    state.instanceBuffer?.destroy();
    state.priorStateBuffer?.destroy();
    state.instanceBuffer = null;
    state.priorStateBuffer = null;
    state.capacity = 0;
    state.instanceCount = 0;
}
