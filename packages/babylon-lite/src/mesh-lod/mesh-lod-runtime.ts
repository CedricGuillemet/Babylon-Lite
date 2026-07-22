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
import type { MeshLoDAsset, MeshLoDDebugView, MeshLoDInstance, MeshLoDRequestOptions, MeshLoDSelectionMode, MeshLoDSource } from "./mesh-lod.js";
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
 *  Only the fields required by the public facade exist today; the resident
 *  metadata, page cache, scheduler, and GPU state fields defined in the
 *  architecture are added by the Phase 3–5 loading/streaming/GPU tasks. */
export interface MeshLoDAssetRuntime {
    readonly engine: EngineContext;
    readonly settings: MeshLoDEffectiveSettings;
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

/** Load, validate, and register a MeshLoD asset from a resolved settings object.
 *
 *  Bootstrap (range source), metadata parse/validation, pinned-page decode, and
 *  GPU upload are implemented by the Phase 3/4 loading tasks. Until then this
 *  entry point is not reachable through a completed happy path. */
export function _loadMeshLoD(
    _engine: EngineContext,
    _source: MeshLoDSource,
    _settings: MeshLoDEffectiveSettings,
    _selectionMode: MeshLoDSelectionMode,
    _request: MeshLoDRequestOptions | undefined,
    _signal: AbortSignal | undefined
): Promise<MeshLoDAsset> {
    // Scaffold: replaced by the range-source bootstrap (T-16) and pinned-page
    // loading (T-19) tasks, which construct and validate the asset before it
    // resolves. No completed workflow reaches this throw.
    throw new Error("MeshLoD loading is not yet implemented in this build");
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
