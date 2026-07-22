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
