/** MeshLoD selection math — the shared float32 primitives used by the CPU oracle,
 *  the GPU selection model, and (mirrored line-for-line) the selection WGSL.
 *
 *  Every comparison-sensitive value is rounded through `Math.fround` so the three
 *  selectors make bit-identical decisions (architecture §10.1). Keeping this math in
 *  one module is what lets the GPU path be accepted only after deterministic
 *  equivalence with the CPU oracle. The world matrix is column-major (WebGPU/WGSL
 *  order): columns 0-2 are the basis and column 3 (`m[12..14]`) is the translation. */

export const f32 = Math.fround;
export const fadd = (a: number, b: number): number => f32(a + b);
export const fsub = (a: number, b: number): number => f32(a - b);
export const fmul = (a: number, b: number): number => f32(a * b);
export const fdiv = (a: number, b: number): number => f32(a / b);
export const fsqrt = (a: number): number => f32(Math.sqrt(a));

/** A normalized frustum plane `[nx, ny, nz, d]`; the inside half-space is
 *  `n·p + d >= 0`. */
export type MeshLoDFrustumPlane = readonly [number, number, number, number];

/** Result of projecting a primitive-local sphere by an instance transform. */
export interface ProjectedSphere {
    readonly worldCenter: readonly [number, number, number];
    readonly worldRadius: number;
    readonly errorPx: number;
    readonly projectedRadiusPx: number;
}

/** Maximum column length of the upper-left 3×3 of a column-major world matrix. */
export function maxColumnScale(m: ArrayLike<number>): number {
    const c0 = fsqrt(fadd(fadd(fmul(m[0]!, m[0]!), fmul(m[1]!, m[1]!)), fmul(m[2]!, m[2]!)));
    const c1 = fsqrt(fadd(fadd(fmul(m[4]!, m[4]!), fmul(m[5]!, m[5]!)), fmul(m[6]!, m[6]!)));
    const c2 = fsqrt(fadd(fadd(fmul(m[8]!, m[8]!), fmul(m[9]!, m[9]!)), fmul(m[10]!, m[10]!)));
    return Math.max(c0, c1, c2);
}

/** Perspective pixel scale `targetHeight / (2 * tan(verticalFov / 2))`. */
export function perspectivePixelScale(targetHeight: number, verticalFov: number): number {
    return fdiv(targetHeight, fmul(2, f32(Math.tan(f32(verticalFov / 2)))));
}

/** Project a primitive-local sphere (center/radius/error) through a column-major
 *  world matrix and camera, returning the screen-space error and radius in pixels.
 *  `orthographicHeight > 0` selects the orthographic screen-space-error equation. */
export function projectSphere(
    m: ArrayLike<number>,
    cameraPos: readonly [number, number, number],
    cameraNear: number,
    orthographicHeight: number | undefined,
    targetHeight: number,
    worldScale: number,
    pixelScale: number,
    center: readonly [number, number, number],
    radius: number,
    error: number
): ProjectedSphere {
    const wx = fadd(fadd(fmul(m[0]!, center[0]), fmul(m[4]!, center[1])), fadd(fmul(m[8]!, center[2]), m[12]!));
    const wy = fadd(fadd(fmul(m[1]!, center[0]), fmul(m[5]!, center[1])), fadd(fmul(m[9]!, center[2]), m[13]!));
    const wz = fadd(fadd(fmul(m[2]!, center[0]), fmul(m[6]!, center[1])), fadd(fmul(m[10]!, center[2]), m[14]!));
    const worldRadius = fmul(radius, worldScale);
    const worldError = fmul(error, worldScale);
    const dx = fsub(cameraPos[0], wx);
    const dy = fsub(cameraPos[1], wy);
    const dz = fsub(cameraPos[2], wz);
    const distance = fsqrt(fadd(fadd(fmul(dx, dx), fmul(dy, dy)), fmul(dz, dz)));
    const surfaceDistance = Math.max(fsub(distance, worldRadius), cameraNear);

    const ortho = orthographicHeight !== undefined && orthographicHeight > 0;
    let errorPx: number;
    let projectedRadiusPx: number;
    if (ortho) {
        const scale = fdiv(targetHeight, orthographicHeight!);
        errorPx = fmul(fmul(worldError, worldScale), scale);
        projectedRadiusPx = fmul(worldRadius, scale);
    } else {
        errorPx = fdiv(fmul(worldError, pixelScale), surfaceDistance);
        projectedRadiusPx = fdiv(fmul(worldRadius, pixelScale), surfaceDistance);
    }
    return { worldCenter: [wx, wy, wz], worldRadius, errorPx, projectedRadiusPx };
}

/** True when a world-space sphere is strictly outside any normalized frustum plane.
 *  An empty plane list is never outside (culling disabled). */
export function sphereOutsidePlanes(planes: readonly MeshLoDFrustumPlane[], center: readonly [number, number, number], radius: number): boolean {
    for (const [nx, ny, nz, d] of planes) {
        const signed = fadd(fadd(fmul(nx, center[0]), fmul(ny, center[1])), fadd(fmul(nz, center[2]), d));
        if (signed < f32(-radius)) {
            return true;
        }
    }
    return false;
}
