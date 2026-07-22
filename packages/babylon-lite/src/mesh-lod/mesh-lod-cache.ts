/** MeshLoD GPU cache — the immutable geometry arena and its 64 KiB block allocator.
 *
 *  One storage arena is allocated at the immutable rounded `cacheCapacityBytes`
 *  (architecture 11.4). Residency is tracked in fixed 64 KiB blocks: pinned coarse
 *  pages are allocated first at the start of the arena, and later fine pages take
 *  64–256 KiB contiguous runs via deterministic first-fit. The allocator only owns
 *  block bookkeeping and the arena `GPUBuffer`; page decode/upload and eviction
 *  policy live in the runtime/scheduler. */

import { BU } from "../engine/gpu-flags.js";
import type { EngineContext } from "../engine/engine.js";
import type { MeshLoDPageRecord } from "./mesh-lod-runtime.js";
import { PAGE_ALIGNMENT, PAGE_MAX_BYTES } from "./mesh-lod-format.js";
import { createMeshLoDError } from "./mesh-lod-errors.js";

/** 64 KiB allocation block — the arena's granularity. */
export const ARENA_BLOCK_BYTES = PAGE_ALIGNMENT;

/** Round a byte length down to a whole number of 64 KiB blocks. */
export function floorToBlocks(bytes: number): number {
    return Math.floor(bytes / ARENA_BLOCK_BYTES) * ARENA_BLOCK_BYTES;
}

/** Round a page's meaningful byte length up to its 64 KiB-multiple allocation,
 *  with a 64 KiB minimum — mirrors the converter's `pageAllocation`. */
export function roundPageAllocation(bytes: number): number {
    const rounded = Math.ceil(bytes / ARENA_BLOCK_BYTES) * ARENA_BLOCK_BYTES;
    return Math.max(rounded, ARENA_BLOCK_BYTES);
}

/** Total rounded arena bytes required by every pinned page. This is the minimum
 *  viable capacity/budget: loading fails with `MLOD_BUDGET_TOO_SMALL` below it. */
export function pinnedAllocationBytes(pageRecords: readonly MeshLoDPageRecord[]): number {
    let total = 0;
    for (const page of pageRecords) {
        if (page.pinned) {
            total += roundPageAllocation(page.decodedBytes);
        }
    }
    return total;
}

/** The immutable geometry arena: a fixed storage `GPUBuffer` plus a per-block
 *  residency bitmap. Never reallocated for the asset's lifetime (only recreated
 *  on device recovery). */
export interface MeshLoDArena {
    /** Immutable arena capacity in bytes, a multiple of 64 KiB. */
    readonly capacityBytes: number;
    /** Number of 64 KiB blocks in the arena. */
    readonly blockCount: number;
    /** The raw geometry-arena storage buffer (read-only storage + copy dest). */
    readonly buffer: GPUBuffer;
    /** Per-block state: `0` = free, `1` = allocated. */
    readonly blocks: Uint8Array;
    /** Per-block pinned flag: pinned blocks are never evicted or relocated. */
    readonly pinnedBlocks: Uint8Array;
    /** Count of currently allocated blocks (pinned + resident fine). */
    committedBlocks: number;
    /** Count of blocks reserved by pinned pages. */
    pinnedCount: number;
}

/** Create the immutable geometry arena. `capacityBytes` is rounded down to a whole
 *  number of 64 KiB blocks and clamped to the device's storage-buffer limits; a
 *  capacity that cannot hold `minimumBytes` (the pinned allocation) throws
 *  `MLOD_DEVICE_LIMIT`. */
export function createMeshLoDArena(engine: EngineContext, capacityBytes: number, minimumBytes: number, label?: string): MeshLoDArena {
    const limits = engine._device.limits;
    const deviceCap = Math.min(limits?.maxStorageBufferBindingSize ?? capacityBytes, limits?.maxBufferSize ?? capacityBytes);
    const rounded = floorToBlocks(Math.min(capacityBytes, deviceCap));
    if (rounded < minimumBytes || rounded < ARENA_BLOCK_BYTES) {
        throw createMeshLoDError("MLOD_DEVICE_LIMIT", "device storage limits cannot hold the pinned geometry arena", { expected: minimumBytes, actual: rounded });
    }
    const blockCount = rounded / ARENA_BLOCK_BYTES;
    const buffer = engine._device.createBuffer({ label: label ?? "mesh-lod-arena", size: rounded, usage: BU.STORAGE | BU.COPY_DST });
    return {
        capacityBytes: rounded,
        blockCount,
        buffer,
        blocks: new Uint8Array(blockCount),
        pinnedBlocks: new Uint8Array(blockCount),
        committedBlocks: 0,
        pinnedCount: 0,
    };
}

/** Reserve a contiguous 64–256 KiB run for a decoded page using deterministic
 *  first-fit. Returns the byte offset of the run, or `null` when no contiguous run
 *  of the required block count is free. Pinned pages must allocate before any fine
 *  pages so they occupy the arena prefix. */
export function allocateArenaRun(arena: MeshLoDArena, decodedBytes: number, pinned: boolean): number | null {
    const runBytes = roundPageAllocation(decodedBytes);
    if (runBytes > PAGE_MAX_BYTES) {
        return null;
    }
    const need = runBytes / ARENA_BLOCK_BYTES;
    const { blocks, pinnedBlocks, blockCount } = arena;
    let start = 0;
    while (start + need <= blockCount) {
        let free = 0;
        while (free < need && blocks[start + free] === 0) {
            free++;
        }
        if (free === need) {
            for (let i = 0; i < need; i++) {
                blocks[start + i] = 1;
                pinnedBlocks[start + i] = pinned ? 1 : 0;
            }
            arena.committedBlocks += need;
            if (pinned) {
                arena.pinnedCount += need;
            }
            return start * ARENA_BLOCK_BYTES;
        }
        // Skip past the blocked cell (start + free is the first occupied block).
        start += free + 1;
    }
    return null;
}

/** Release a previously allocated run. Pinned runs are never freed except on
 *  disposal/device loss. */
export function freeArenaRun(arena: MeshLoDArena, byteOffset: number, decodedBytes: number): void {
    const runBytes = roundPageAllocation(decodedBytes);
    const need = runBytes / ARENA_BLOCK_BYTES;
    const startBlock = byteOffset / ARENA_BLOCK_BYTES;
    for (let i = 0; i < need; i++) {
        const b = startBlock + i;
        if (arena.blocks[b] === 1) {
            arena.blocks[b] = 0;
            arena.committedBlocks--;
            if (arena.pinnedBlocks[b] === 1) {
                arena.pinnedCount--;
            }
            arena.pinnedBlocks[b] = 0;
        }
    }
}

/** Bytes currently committed to resident blocks. */
export function arenaUsedBytes(arena: MeshLoDArena): number {
    return arena.committedBlocks * ARENA_BLOCK_BYTES;
}

/** Destroy the arena's GPU buffer. Idempotent-safe when called once during disposal. */
export function destroyMeshLoDArena(arena: MeshLoDArena): void {
    arena.buffer.destroy();
    arena.blocks.fill(0);
    arena.pinnedBlocks.fill(0);
    arena.committedBlocks = 0;
    arena.pinnedCount = 0;
}
