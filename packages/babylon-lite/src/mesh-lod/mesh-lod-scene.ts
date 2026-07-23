/** MeshLoD scene integration — the scene-owned registry, batching, and per-frame
 *  CPU selection.
 *
 *  Assets and instances never reference a scene (one-way ownership, GUIDANCE 4b):
 *  the scene alone owns a `MeshLoDSceneRegistry` that groups instances into batches
 *  by exact asset + exact material identity. Registration installs one deferred
 *  scene builder (via `addDeferredSceneRenderables`) that the PBR MeshLoD material
 *  module fills with a material-owned renderable — this module never touches
 *  WGSL, pipelines, or bind groups. Add/remove are idempotent and take effect
 *  before the next selection. */

import type { SceneContext, DeferredSceneGpuRecoverable } from "../scene/scene-core.js";
import { addDeferredSceneRenderables, addDeferredSceneGpuRecoverable } from "../scene/scene-core.js";
import type { EngineContext } from "../engine/engine.js";
import type { Renderable, DrawUpdateContext } from "../render/renderable.js";
import type { Camera } from "../camera/camera.js";
import { getCameraPosition } from "../camera/camera.js";
import type { PbrMaterialProps } from "../material/pbr/pbr-material.js";
import type { MeshLoDAsset, MeshLoDInstance } from "./mesh-lod.js";
import type { MeshLoDAssetRuntime, MeshLoDStreamSelectionStats } from "./mesh-lod-runtime.js";
import { stepMeshLoDStreaming, _recoverMeshLoDAsset } from "./mesh-lod-runtime.js";
import type { MeshLoDPageDemand } from "./mesh-lod-scheduler.js";
import { createMeshLoDError } from "./mesh-lod-errors.js";
import type { MeshLoDError } from "./mesh-lod-errors.js";
import type { MeshLoDCamera, MeshLoDSelectionResult } from "./mesh-lod-selection-cpu.js";
import { selectMeshLoDCpu } from "./mesh-lod-selection-cpu.js";

/** One scene-owned draw batch: all instances that share an exact asset and exact
 *  material object. Selection and expansion run per batch; the PBR module attaches
 *  a single indirect-draw `Renderable` (Task 4.4). */
export interface MeshLoDSceneBatch {
    readonly asset: MeshLoDAsset;
    readonly material: PbrMaterialProps;
    readonly instances: MeshLoDInstance[];
    /** Prior per-instance `wasFineRequired` bitsets keyed by stable instance id, for
     *  hysteresis continuity across frames. */
    readonly priorFineRequired: Map<number, Uint8Array>;
    /** Material-owned indirect renderable, attached by the PBR module at build time. */
    renderable?: Renderable;
    /** @internal Feature-owned per-batch GPU/CPU state, owned by the PBR module. */
    _packet?: unknown;
}

/** Scene-owned MeshLoD registry. Stored only on `SceneContext._meshLoDRegistry`;
 *  neither assets nor instances hold a back-reference to it. */
export interface MeshLoDSceneRegistry {
    /** Batches grouped by asset then by exact material object identity. */
    readonly byAsset: Map<MeshLoDAsset, Map<PbrMaterialProps, MeshLoDSceneBatch>>;
    /** Flat batch list in insertion order for deterministic iteration. */
    readonly batches: MeshLoDSceneBatch[];
    /** True once the single deferred builder has been registered on the scene. */
    builderRegistered: boolean;
}

/** Sub-feature objects and flags outside the guaranteed opaque metallic-roughness
 *  subset (architecture §2, decision 5). Any of these rejects the instance. */
function validateSupportedPbrSubset(material: PbrMaterialProps): void {
    const reject = (feature: string): never => {
        throw createMeshLoDError("MLOD_UNSUPPORTED_MATERIAL", `MeshLoD v1 does not support ${feature}`, { expected: "opaque metallic-roughness", actual: feature });
    };
    if (material.alphaBlend === true) {
        reject("alpha blending");
    }
    if (material.alphaCutOff !== undefined) {
        reject("alpha masking");
    }
    if (material.transmissive) {
        reject("transmission");
    }
    if (material.clearCoat) {
        reject("clearcoat");
    }
    if (material.sheen) {
        reject("sheen");
    }
    if (material.iridescence) {
        reject("iridescence");
    }
    if (material.anisotropy) {
        reject("anisotropy");
    }
    if (material.subsurface) {
        reject("subsurface");
    }
    if (material.specGlossTexture) {
        reject("specular-glossiness");
    }
    if (material.plugins?.length) {
        reject("material plugins");
    }
    if (material.occlusionTexCoord === 1 || material._uv2Mask) {
        reject("a second UV set");
    }
    if (material.shadowOnly || material.skyboxMode) {
        reject("shadow-only or skybox materials");
    }
}

function getOrCreateRegistry(scene: SceneContext): MeshLoDSceneRegistry {
    let registry = scene._meshLoDRegistry;
    if (!registry) {
        registry = { byAsset: new Map(), batches: [], builderRegistered: false };
        scene._meshLoDRegistry = registry;
    }
    return registry;
}

function getOrCreateBatch(registry: MeshLoDSceneRegistry, asset: MeshLoDAsset, material: PbrMaterialProps): MeshLoDSceneBatch {
    let byMaterial = registry.byAsset.get(asset);
    if (!byMaterial) {
        byMaterial = new Map();
        registry.byAsset.set(asset, byMaterial);
    }
    let batch = byMaterial.get(material);
    if (!batch) {
        batch = { asset, material, instances: [], priorFineRequired: new Map() };
        byMaterial.set(material, batch);
        registry.batches.push(batch);
    }
    return batch;
}

/** @internal Cached PBR MeshLoD material module — dynamically imported on the first
 *  scene build so this scene module never statically references the PBR MeshLoD
 *  chunk (and non-MeshLoD scenes fetch none of it). Nullable lazy cache only. */
let _pbrMeshLoDModule: typeof import("../material/pbr/pbr-mesh-lod-renderable.js") | null = null;

async function getPbrMeshLoDModule(): Promise<typeof import("../material/pbr/pbr-mesh-lod-renderable.js")> {
    if (!_pbrMeshLoDModule) {
        _pbrMeshLoDModule = await import("../material/pbr/pbr-mesh-lod-renderable.js");
    }
    return _pbrMeshLoDModule;
}

/** Build the material-owned indirect renderable for every batch in the scene, skipping an
 *  asset that failed device recovery (its GPU arena is gone) or was disposed. Shared by the
 *  initial deferred build and the device-recovery rebuild. */
async function buildMeshLoDBatchRenderables(engine: EngineContext, scene: SceneContext): Promise<Renderable[]> {
    const registry = scene._meshLoDRegistry;
    const renderables: Renderable[] = [];
    if (!registry) {
        return renderables;
    }
    const pbr = await getPbrMeshLoDModule();
    for (const batch of registry.batches) {
        if (batch.asset.state === "failed" || batch.asset._runtime.disposed) {
            continue;
        }
        const renderable = pbr.buildMeshLoDBatchRenderable(engine, scene, batch);
        if (renderable) {
            batch.renderable = renderable;
            renderables.push(renderable);
        }
    }
    return renderables;
}

/** Dispose every batch's material-owned GPU packet (scene teardown). Idempotent. */
function disposeMeshLoDBatchPackets(scene: SceneContext): void {
    const registry = scene._meshLoDRegistry;
    if (!registry) {
        return;
    }
    for (const batch of registry.batches) {
        (batch as { _packet?: { dispose?: () => void } })._packet?.dispose?.();
        batch._packet = undefined;
        batch.renderable = undefined;
    }
}

/** Recover every asset registered in the scene onto the current device (idempotent per
 *  device, so a shared asset recovers once). A pinned-coarse recovery failure marks that
 *  asset failed with `MLOD_DEVICE_RECOVERY`; its batches are then skipped when renderables
 *  are rebuilt, so recovery cannot claim success for it. */
function recoverMeshLoDAssets(engine: EngineContext, scene: SceneContext): void {
    const registry = scene._meshLoDRegistry;
    if (!registry) {
        return;
    }
    for (const asset of registry.byAsset.keys()) {
        if (asset._runtime.disposed || asset._runtime.gpuDevice === engine._device) {
            continue;
        }
        asset.state = "recovering";
        try {
            _recoverMeshLoDAsset(engine, asset._runtime);
            asset.state = "ready";
        } catch (error) {
            asset.state = "failed";
            asset.error = error as MeshLoDError;
        }
    }
}

function registerDeferredBuilder(scene: SceneContext): void {
    // One recoverable owns rebuild + dispose; the initial deferred build reuses the same
    // rebuild path (device recovery just re-runs it against the recovered device).
    const recoverable: DeferredSceneGpuRecoverable = {
        async rebuild(engine, sc) {
            recoverMeshLoDAssets(engine, sc);
            return buildMeshLoDBatchRenderables(engine, sc);
        },
        dispose() {
            disposeMeshLoDBatchPackets(scene);
        },
    };
    addDeferredSceneGpuRecoverable(scene, recoverable);
    addDeferredSceneRenderables(scene, async (engine, sc) => ({
        renderables: await buildMeshLoDBatchRenderables(engine, sc),
        dispose: () => recoverable.dispose(),
    }));
}

/** @internal Register an instance into its scene-owned batch. Idempotent for the
 *  same scene/instance; validates the guaranteed opaque PBR subset immediately and
 *  writes no scene reference into the instance. */
export function addMeshLoDInstanceToScene(scene: SceneContext, instance: MeshLoDInstance): void {
    validateSupportedPbrSubset(instance._material);
    const registry = getOrCreateRegistry(scene);
    const batch = getOrCreateBatch(registry, instance._asset, instance._material);
    if (!batch.instances.includes(instance)) {
        batch.instances.push(instance);
    }
    if (!registry.builderRegistered) {
        registry.builderRegistered = true;
        registerDeferredBuilder(scene);
    }
}

/** @internal Remove an instance from its scene-owned batch. Idempotent; the
 *  instance stops being selected and submitted immediately. */
export function removeMeshLoDInstanceFromScene(scene: SceneContext, instance: MeshLoDInstance): void {
    const registry = scene._meshLoDRegistry;
    const batch = registry?.byAsset.get(instance._asset)?.get(instance._material);
    if (!batch) {
        return;
    }
    const index = batch.instances.indexOf(instance);
    if (index !== -1) {
        batch.instances.splice(index, 1);
        batch.priorFineRequired.delete(instance._instanceId);
    }
}

/** One instance's per-frame selection output. */
export interface MeshLoDInstanceSelection {
    readonly instance: MeshLoDInstance;
    readonly result: MeshLoDSelectionResult;
}

function toOracleCamera(camera: Camera, context: DrawUpdateContext): MeshLoDCamera {
    const position = getCameraPosition(camera);
    return {
        position: [position.x, position.y, position.z],
        verticalFov: camera.fov,
        near: camera.nearPlane,
        targetWidth: context.targetWidth,
        targetHeight: context.targetHeight,
    };
}

/** Run the deterministic CPU selection oracle for every visible instance in a
 *  batch. Coarse-only Phase 4 leaves frustum culling disabled (empty plane list);
 *  GPU selection adds it. Returns one result per visible instance, updating each
 *  instance's prior hysteresis state. */
export function selectMeshLoDBatch(batch: MeshLoDSceneBatch, context: DrawUpdateContext): MeshLoDInstanceSelection[] {
    const camera = context._camera;
    if (!camera || batch.instances.length === 0) {
        return [];
    }
    const runtime: MeshLoDAssetRuntime = batch.asset._runtime;
    const oracleCamera = toOracleCamera(camera, context);
    const isPageResident = (pageId: number): boolean => runtime.gpu.pages[pageId]?.state === "gpu-resident";
    const groupCount = runtime.groups.length;

    const selections: MeshLoDInstanceSelection[] = [];
    for (const instance of batch.instances) {
        if (!instance.visible) {
            continue;
        }
        let prior = batch.priorFineRequired.get(instance._instanceId);
        if (!prior || prior.length !== groupCount) {
            prior = new Uint8Array(groupCount);
        }
        const result = selectMeshLoDCpu({
            groups: runtime.groups,
            clusters: runtime.clusters,
            nodes: runtime.hierarchyNodes,
            pageRecords: runtime.pageRecords,
            groupPageRefs: runtime.groupPageRefs,
            levelCount: runtime.header.levelCount,
            worldMatrix: instance.worldMatrix as unknown as readonly number[],
            camera: oracleCamera,
            frustumPlanes: [],
            screenSpaceError: instance.screenSpaceError ?? runtime.settings.screenSpaceError,
            lodHysteresis: runtime.settings.lodHysteresis,
            isPageResident,
            wasFineRequired: prior,
        });
        batch.priorFineRequired.set(instance._instanceId, result.fineRequired);
        selections.push({ instance, result });
    }
    return selections;
}

/** Aggregate one batch's per-instance selection into asset-level fine-page demand and
 *  drive the asset's streaming step. Unions each instance's desired pages (keeping the
 *  highest per-page priority), collects the pages every selected cluster references (so
 *  they hold a frame reference and are never evicted while the built frame is in flight),
 *  and publishes the visible/fallback/error selection diagnostics. Called once per frame
 *  by the material renderable after selection, before it writes the draw stream. */
export function driveMeshLoDStreaming(batch: MeshLoDSceneBatch, selections: readonly MeshLoDInstanceSelection[]): void {
    const runtime: MeshLoDAssetRuntime = batch.asset._runtime;
    const priorityByPage = new Map<number, number>();
    const referenced = new Set<number>();
    let visibleGroupCount = 0;
    let fallbackGroupCount = 0;
    let maximumSelectedErrorPixels = 0;
    let maximumUnmetErrorPixels = 0;
    for (const { result } of selections) {
        for (const page of result.desiredPages) {
            const existing = priorityByPage.get(page.pageId);
            if (existing === undefined || page.priority > existing) {
                priorityByPage.set(page.pageId, page.priority);
            }
        }
        for (const clusterId of result.selectedClusterIds) {
            referenced.add(runtime.clusters[clusterId]!.pageId);
        }
        visibleGroupCount = Math.max(visibleGroupCount, result.visibleGroupCount);
        fallbackGroupCount = Math.max(fallbackGroupCount, result.fallbackGroupCount);
        maximumSelectedErrorPixels = Math.max(maximumSelectedErrorPixels, result.maximumSelectedErrorPixels);
        maximumUnmetErrorPixels = Math.max(maximumUnmetErrorPixels, result.maximumUnmetErrorPixels);
    }
    const demand: MeshLoDPageDemand[] = [];
    for (const [pageId, priority] of priorityByPage) {
        demand.push({ pageId, priority });
    }
    demand.sort((a, b) => (b.priority !== a.priority ? b.priority - a.priority : a.pageId - b.pageId));
    const stats: MeshLoDStreamSelectionStats = { visibleGroupCount, fallbackGroupCount, maximumSelectedErrorPixels, maximumUnmetErrorPixels };
    stepMeshLoDStreaming(runtime, demand, [...referenced], stats);
}
