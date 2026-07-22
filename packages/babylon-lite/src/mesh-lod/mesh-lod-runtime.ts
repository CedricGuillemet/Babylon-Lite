/** MeshLoD runtime — internal orchestration and runtime record types.
 *
 *  This module owns the mutable runtime state behind a {@link MeshLoDAsset} and the
 *  heavy orchestration entry points the public facade dynamically imports on first
 *  {@link loadMeshLoD} call. Keeping the orchestration here (never statically
 *  imported by the facade) is what lets non-MeshLoD bundles fetch zero MeshLoD
 *  runtime bytes.
 *
 *  The concrete loading/selection/streaming behavior is filled in across the
 *  Phase 3–7 tasks; this file grows with each. The type surface below is the
 *  stable internal contract those tasks target. */

import type { EngineContext } from "../engine/engine.js";
import type { SceneContext } from "../scene/scene-core.js";
import type { MeshLoDAsset, MeshLoDDebugView, MeshLoDDiagnostics, MeshLoDInstance, MeshLoDRequestOptions, MeshLoDSelectionMode, MeshLoDSource } from "./mesh-lod.js";
import type { ParsedMeshLoDContainer } from "./mesh-lod-format.js";
import type { MeshLoDRangeSource } from "./mesh-lod-range-source.js";
import { parseMeshLoDContainer, readBootstrapExtent, toMeshLoDMetadata } from "./mesh-lod-format.js";
import { BOOTSTRAP_FIRST_END, concatBytes, createMeshLoDRangeSource, throwIfAborted } from "./mesh-lod-range-source.js";
import { createMeshLoDError } from "./mesh-lod-errors.js";
/** Effective, fully-resolved runtime settings (defaults applied, values validated). */
export interface MeshLoDEffectiveSettings {
    screenSpaceError: number;
    lodHysteresis: number;
    residencyHoldFrames: number;
    obsoleteRequestGraceFrames: number;
    cacheBudgetBytes: number;
    cacheCapacityBytes: number;
    cpuPageCacheBytes: number;
    maxConcurrentRequests: number;
    retryCount: number;
    retryDelaysMs: readonly number[];
}

// ─── Parsed immutable `.mlod` records (see architecture section 8) ────

/** Parsed 256-byte container header. Offsets/sizes are absolute file bytes. */
export interface MeshLoDHeader {
    readonly formatMajor: number;
    readonly formatMinor: number;
    readonly minReaderMajor: number;
    readonly minReaderMinor: number;
    readonly containerFlags: number;
    readonly sectionCount: number;
    readonly directoryOffset: number;
    readonly directoryBytes: number;
    readonly bootstrapBytes: number;
    readonly totalFileBytes: number;
    /** Lowercase hex of the 32-byte source-bundle SHA-256. */
    readonly sourceSha256: string;
    /** Lowercase hex of the 32-byte build fingerprint. */
    readonly buildFingerprintSha256: string;
    /** Lowercase hex of the 16-byte deterministic hierarchy id. */
    readonly hierarchyId: string;
    readonly meshIndex: number;
    readonly primitiveIndex: number;
    readonly sourceTriangleCount: number;
    readonly hierarchyTriangleCount: number;
    readonly clusterCount: number;
    readonly groupCount: number;
    readonly hierarchyNodeCount: number;
    readonly pageCount: number;
    readonly pinnedPageCount: number;
    readonly levelCount: number;
    readonly attributeMask: number;
    readonly vertexStride: number;
    readonly boundsMin: readonly [number, number, number];
    readonly boundsMax: readonly [number, number, number];
    readonly maxNonterminalError: number;
}

/** Parsed 64-byte section-directory entry. */
export interface MeshLoDSectionEntry {
    readonly type: number;
    readonly flags: number;
    readonly required: boolean;
    readonly optional: boolean;
    readonly perItemCrc: boolean;
    readonly pageData: boolean;
    readonly offset: number;
    readonly storedBytes: number;
    readonly decodedBytes: number;
    readonly elementCount: number;
    readonly elementStride: number;
    readonly crc: number;
    readonly alignment: number;
}

/** Parsed hierarchy group (64-byte record). */
export interface MeshLoDGroup {
    readonly center: readonly [number, number, number];
    readonly radius: number;
    /** Simplified screen-space error; non-finite (`FLT_MAX`) for terminal groups. */
    readonly simplifiedError: number;
    readonly depth: number;
    readonly firstCluster: number;
    readonly clusterCount: number;
    readonly firstPageRef: number;
    readonly pageRefCount: number;
    readonly terminal: boolean;
    readonly pinned: boolean;
    readonly sourceTriangleCount: number;
    readonly outputTriangleCount: number;
}

/** Parsed cluster/meshlet (64-byte record). */
export interface MeshLoDCluster {
    readonly center: readonly [number, number, number];
    readonly radius: number;
    readonly error: number;
    readonly groupId: number;
    /** Finer group this cluster simplifies, or `-1` for original finest geometry. */
    readonly refinedGroupId: number;
    readonly pageId: number;
    /** First vertex within the decoded page. */
    readonly vertexOffset: number;
    /** First local index within the decoded page, in `u16` elements. */
    readonly indexOffset: number;
    readonly vertexCount: number;
    readonly triangleCount: number;
    readonly sourceTriangleCount: number;
}

/** Parsed 8-wide hierarchy node (32-byte record). */
export interface MeshLoDHierarchyNode {
    readonly center: readonly [number, number, number];
    readonly radius: number;
    readonly error: number;
    /** Leaf group id, or `-1` for an internal node. */
    readonly groupId: number;
    readonly childOffset: number;
    readonly childCount: number;
}

/** Parsed page-table record (64-byte record). */
export interface MeshLoDPageRecord {
    readonly offset: number;
    readonly storedBytes: number;
    readonly meaningfulBytes: number;
    readonly decodedBytes: number;
    readonly crc: number;
    readonly vertexCount: number;
    readonly localIndexCount: number;
    readonly vertexByteOffset: number;
    readonly indexByteOffset: number;
    readonly firstCluster: number;
    readonly clusterCount: number;
    readonly pinned: boolean;
    readonly coarse: boolean;
    readonly minDepth: number;
    readonly maxDepth: number;
}

/** Mutable runtime state referenced by `MeshLoDAsset._runtime`.
 *
 *  Only the fields required so far exist today; the page cache, scheduler, and GPU
 *  state fields defined in the architecture are added by the Phase 4–6
 *  loading/streaming/GPU tasks. */
export interface MeshLoDAssetRuntime {
    readonly engine: EngineContext;
    readonly source: MeshLoDRangeSource;
    /** Bytes covering `[0, bootstrapBytes)`: header, directory, metadata, and the
     *  pinned coarse pages. The complete file for in-memory/200 sources. */
    readonly coarseBytes: Uint8Array;
    readonly header: MeshLoDHeader;
    readonly sections: readonly MeshLoDSectionEntry[];
    readonly groups: readonly MeshLoDGroup[];
    readonly clusters: readonly MeshLoDCluster[];
    readonly hierarchyNodes: readonly MeshLoDHierarchyNode[];
    readonly pageRecords: readonly MeshLoDPageRecord[];
    readonly groupPageRefs: Uint32Array;
    readonly settings: MeshLoDEffectiveSettings;
    /** Live diagnostics object also referenced by `MeshLoDAsset.diagnostics`. */
    readonly diagnostics: MeshLoDDiagnostics;
    readonly abortController: AbortController;
    /** Bumped on disposal and device recovery to invalidate stale completions. */
    generation: number;
    /** Monotonic frame counter used by selection/streaming hysteresis. */
    frameIndex: number;
    streamingPaused: boolean;
    debugView: MeshLoDDebugView;
    selectionMode: MeshLoDSelectionMode;
    /** Next id handed to `createMeshLoDInstance`. */
    nextInstanceId: number;
    disposed: boolean;
}

/** Writable diagnostics view used while updating the live snapshot. */
type MeshLoDMutableDiagnostics = { -readonly [K in keyof MeshLoDDiagnostics]: MeshLoDDiagnostics[K] };

function createDiagnostics(
    parsed: ParsedMeshLoDContainer,
    settings: MeshLoDEffectiveSettings,
    selectionMode: MeshLoDSelectionMode,
    downloadedBytes: number
): MeshLoDMutableDiagnostics {
    return {
        frameIndex: 0,
        sourceTriangleCount: parsed.header.sourceTriangleCount,
        renderedTriangleCount: 0,
        selectedMeshletCount: 0,
        visibleGroupCount: 0,
        fallbackGroupCount: 0,
        maximumSelectedErrorPixels: 0,
        maximumUnmetErrorPixels: 0,
        requestedPageCount: 0,
        queuedPageCount: 0,
        inFlightPageCount: 0,
        residentPageCount: 0,
        pinnedPageCount: parsed.header.pinnedPageCount,
        terminalFailedPageCount: 0,
        downloadedBytes,
        gpuCacheUsedBytes: 0,
        gpuCacheBudgetBytes: settings.cacheBudgetBytes,
        gpuCacheCapacityBytes: settings.cacheCapacityBytes,
        cpuPageCacheUsedBytes: 0,
        maxConcurrentRequests: settings.maxConcurrentRequests,
        streamingPaused: false,
        selectionMode,
    };
}

/** Load and validate a MeshLoD asset from a resolved settings object.
 *
 *  Runs the coarse-first bootstrap: for a complete-file source it validates the
 *  whole container; for a URL it reads the header (bytes `0-65535`), then reads at
 *  most one continuation through `bootstrapBytes`, and validates the metadata plus
 *  pinned pages while deferring fine pages to streaming. Pinned-page GPU decode and
 *  upload are added by the Phase 4 loading tasks. */
export async function _loadMeshLoD(
    engine: EngineContext,
    source: MeshLoDSource,
    settings: MeshLoDEffectiveSettings,
    selectionMode: MeshLoDSelectionMode,
    request: MeshLoDRequestOptions | undefined,
    signal: AbortSignal | undefined
): Promise<MeshLoDAsset> {
    throwIfAborted(signal);
    const src = await createMeshLoDRangeSource(source, request);

    let parsed: ParsedMeshLoDContainer;
    let coarseBytes: Uint8Array;
    if (src.completeBytes) {
        coarseBytes = src.completeBytes;
        parsed = parseMeshLoDContainer(coarseBytes);
    } else {
        const head = await src.read(0, BOOTSTRAP_FIRST_END, signal);
        if (src.completeBytes) {
            // The first response was a full-body 200; use the retained file.
            coarseBytes = src.completeBytes;
            parsed = parseMeshLoDContainer(coarseBytes);
        } else {
            const extent = readBootstrapExtent(head);
            if (src.totalBytes !== null && src.totalBytes !== extent.totalFileBytes) {
                throw createMeshLoDError("MLOD_INVALID_LAYOUT", "transport total disagrees with the header", { expected: extent.totalFileBytes, actual: src.totalBytes });
            }
            coarseBytes =
                head.length >= extent.bootstrapBytes ? head.subarray(0, extent.bootstrapBytes) : concatBytes(head, await src.read(head.length, extent.bootstrapBytes - 1, signal));
            parsed = parseMeshLoDContainer(coarseBytes);
        }
    }

    if (src.totalBytes !== null && src.totalBytes !== parsed.header.totalFileBytes) {
        throw createMeshLoDError("MLOD_TRUNCATED", "transport total disagrees with the declared file size", { expected: parsed.header.totalFileBytes, actual: src.totalBytes });
    }

    const diagnostics = createDiagnostics(parsed, settings, selectionMode, src.downloadedBytes);
    const runtime: MeshLoDAssetRuntime = {
        engine,
        source: src,
        coarseBytes,
        header: parsed.header,
        sections: parsed.sections,
        groups: parsed.groups,
        clusters: parsed.clusters,
        hierarchyNodes: parsed.hierarchyNodes,
        pageRecords: parsed.pageRecords,
        groupPageRefs: parsed.groupPageRefs,
        settings,
        diagnostics,
        abortController: new AbortController(),
        generation: 0,
        frameIndex: 0,
        streamingPaused: false,
        debugView: "none",
        selectionMode,
        nextInstanceId: 0,
        disposed: false,
    };

    return {
        metadata: toMeshLoDMetadata(parsed),
        diagnostics,
        state: "ready",
        _runtime: runtime,
    };
}

/** Register an instance into its scene-owned MeshLoD batch. Implemented by the
 *  scene-registry task (T-20). */
export function _addMeshLoDToScene(_scene: SceneContext, _instance: MeshLoDInstance): void {
    // Scaffold: replaced by the lazy scene registry (T-20).
    throw new Error("MeshLoD scene registration is not yet implemented in this build");
}

/** Remove an instance from its scene-owned MeshLoD batch. Implemented by the
 *  scene-registry task (T-20). */
export function _removeMeshLoDFromScene(_scene: SceneContext, _instance: MeshLoDInstance): void {
    // Scaffold: replaced by the lazy scene registry (T-20).
    throw new Error("MeshLoD scene removal is not yet implemented in this build");
}
