# MeshLoD Task Board

## Queue

| M | ID | Task | Skill | Notes |
|---|---|---|---|---|
|  | T-16 | Execute implementation-plan/03_03_range_source_bootstrap.md | execute-implementation-plan |  |
|  | T-17 | Execute implementation-plan/03_04_cpu_selection_oracle.md | execute-implementation-plan |  |
|  | T-18 | Execute implementation-plan/04_01_lazy_page_decoder.md | execute-implementation-plan |  |
|  | T-19 | Execute implementation-plan/04_02_load_pinned_pages.md | execute-implementation-plan |  |
|  | T-20 | Execute implementation-plan/04_03_scene_registry_cpu_selection.md | execute-implementation-plan |  |
|  | T-21 | Execute implementation-plan/04_04_pbr_coarse_indirect_rendering.md | execute-implementation-plan |  |
|  | T-22 | Execute implementation-plan/04_05_verify_lazy_coarse_path.md | execute-implementation-plan |  |
|  | T-23 | Execute implementation-plan/05_01_gpu_metadata_buffers.md | execute-implementation-plan |  |
|  | T-24 | Execute implementation-plan/05_02_gpu_selection_compute.md | execute-implementation-plan |  |
|  | T-25 | Execute implementation-plan/05_03_gpu_expansion_indirect_draw.md | execute-implementation-plan |  |
|  | T-26 | Execute implementation-plan/05_04_cpu_gpu_equivalence.md | execute-implementation-plan |  |
|  | T-27 | Execute implementation-plan/06_01_page_request_scheduler.md | execute-implementation-plan |  |
|  | T-28 | Execute implementation-plan/06_02_bounded_page_caches.md | execute-implementation-plan |  |
|  | T-29 | Execute implementation-plan/06_03_connect_streaming_residency.md | execute-implementation-plan |  |
|  | T-30 | Execute implementation-plan/06_04_streaming_cache_fixtures.md | execute-implementation-plan |  |
|  | T-31 | Execute implementation-plan/07_01_disposal_and_scene_pruning.md | execute-implementation-plan |  |
|  | T-32 | Execute implementation-plan/07_02_deferred_device_recovery.md | execute-implementation-plan |  |
|  | T-33 | Execute implementation-plan/07_03_lifecycle_recovery_fixtures.md | execute-implementation-plan |  |
|  | T-34 | Execute implementation-plan/08_01_demo_shell_and_assets.md | execute-implementation-plan |  |
|  | T-35 | Execute implementation-plan/08_02_demo_controls_network_camera.md | execute-implementation-plan |  |
|  | T-36 | Execute implementation-plan/08_03_demo_diagnostics_debug.md | execute-implementation-plan |  |
|  | T-37 | Execute implementation-plan/08_04_verify_demo_workflows.md | execute-implementation-plan |  |
|  | T-38 | Execute implementation-plan/09_01_full_validation_traceability.md | execute-implementation-plan |  |

## Completed

| M | ID | Task | Skill | Notes |
|---|---|---|---|---|
|  | T-01 | Goals - review or create goals.md | review-goals | done 2026-07-22 - goals.md |
|  | T-02 | Visual mocks (optional) | create-html-mock | done 2026-07-22 - skipped, use existing demo styling |
|  | T-03 | Requirements | write-requirements | done 2026-07-22 - requirements.md |
|  | T-04 | Architecture | write-architecture | done 2026-07-22 - architecture.md |
|  | T-05 | Implementation plan | write-implementation-plan | done 2026-07-22 - implementation-plan/ (33 tasks) |
|  | T-06 | Execute implementation-plan/01_01_scaffold_converter.md | execute-implementation-plan | done 2026-07-22 - configure+build+CTest 6/6; meshoptimizer+cgltf pinned to exact SHAs |
|  | T-07 | Execute implementation-plan/01_02_cli_and_primitive_selection.md | execute-implementation-plan | done 2026-07-22 - full option parse/validation + deterministic primitive naming; CTest 6/6 |
|  | T-08 | Execute implementation-plan/01_03_normalize_gltf_geometry.md | execute-implementation-plan | done 2026-07-22 - cgltf ingest/validate/select + normalize (indices, angle-weighted normals, bounds); 15 fixtures; CTest 6/6 |
|  | T-09 | Execute implementation-plan/01_04_build_cluster_hierarchy.md | execute-implementation-plan | done 2026-07-22 - clusterlod DAG + 8-wide forest; exact terminal coverage + reachability validation; grid fixture 6 groups/5 levels; CTest 6/6 |
|  | T-10 | Execute implementation-plan/02_01_define_binary_contract.md | execute-implementation-plan | done 2026-07-22 - mlod_format.h layout constants + LE/checked helpers; CRC32C + SHA-256 with published vectors; source/build fingerprints; format_vectors test; CTest 7/7 |
|  | T-11 | Execute implementation-plan/02_02_pack_deterministic_pages.md | execute-implementation-plan | done 2026-07-22 - deterministic 64-256KiB page packing (meshopt vertex/index streams, oct-normal+half UV), pinned prefix, decode round-trip + byte-identical repack; CTest 7/7 |
|  | T-12 | Execute implementation-plan/02_03_write_validate_publish.md | execute-implementation-plan | done 2026-07-22 - full container writer (header/dir/sections/pages + CRCs), independent validator, canonical stats, atomic multi-output publish; end-to-end grid->.mlod validates; CTest 7/7 |
|  | T-13 | Execute implementation-plan/02_04_converter_fixtures_and_statue.md | execute-implementation-plan | done 2026-07-22 - statue converted to 3 .mlod (98366/119201/93519 src tris, 12 levels, 40/46/38 pages, exact terminal coverage); byte-identical repeat; --validate-only passes; assets+stats committed; CTest 7/7 |
|  | T-14 | Execute implementation-plan/03_01_public_api_and_errors.md | execute-implementation-plan | done 2026-07-22 - c9016474; facade+errors+runtime types, option validation, no-side-effect lazy imports; lint(eslint+tsc)/26 unit tests green |
|  | T-15 | Execute implementation-plan/03_02_parse_mlod_buffers.md | execute-implementation-plan | done 2026-07-22 - d0816a75; full LE parser/validator mirroring C++ validateContainer, CRC32C, record interfaces; 25 tests incl. 3 real statue assets vs statue-stats.json + 20-case mutation matrix; lint green |

## Untriaged

- T-14 created `mesh-lod-runtime.ts` early (plan lists it first under T-15) because the facade must `import type { MeshLoDAssetRuntime }` from it; `_loadMeshLoD`/`_addMeshLoDToScene`/`_removeMeshLoDFromScene` are scaffold throws replaced by the Phase 3/4 loading (T-16) and scene-registry (T-20) tasks.
- T-14 deferred the `package.json` internal testing subpath to T-17 (where `mesh-lod-testing.ts` is created) to avoid a dangling export target; the CPU-oracle resolver `_resolveMeshLoDLoadOptions` is exported `@internal` from the facade for unit testing in the meantime.
- Incidental pre-existing repo-wide `tsc` failures under the installed toolchain (TypeScript 6.0.3 / @webgpu/types 0.1.69) were fixed to get a green `pnpm run lint`: `picking-advanced-pipeline.ts` readonly vertex buffers, `scripts/lite-error-plugin.ts` undefined guards. Also added `mesh-lod-tool/build/**` to ESLint ignores.

<!-- END OF BOARD -->
