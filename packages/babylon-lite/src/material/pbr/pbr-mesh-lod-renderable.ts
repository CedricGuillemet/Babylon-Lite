/** PBR-owned MeshLoD renderable (scaffold).
 *
 *  This module owns every MeshLoD rendering decision — supported-material
 *  validation, feature detection, the storage-fetch vertex + PBR/unlit fragment
 *  WGSL, pipeline descriptors, bind-group layouts/creation, fallback textures, and
 *  the indirect-draw `Renderable`. Task 4.3 wires the scene registry to it through
 *  `buildMeshLoDBatchRenderable`; Task 4.4 fills in the actual pipeline, WGSL, and
 *  CPU expansion. It is imported only via the MeshLoD scene path, so no non-MeshLoD
 *  scene ever fetches its bytes. No module-level side effects. */

import type { EngineContext } from "../../engine/engine.js";
import type { SceneContext } from "../../scene/scene-core.js";
import type { Renderable } from "../../render/renderable.js";
import type { MeshLoDSceneBatch } from "../../mesh-lod/mesh-lod-scene.js";

/** Build the single indirect-draw `Renderable` for one MeshLoD batch, or `null`
 *  when the batch has no instances. Task 4.4 implements the pipeline, WGSL, bind
 *  groups, and CPU expansion; the scaffold produces no renderable yet. */
export function buildMeshLoDBatchRenderable(_engine: EngineContext, _scene: SceneContext, _batch: MeshLoDSceneBatch): Renderable | null {
    return null;
}
