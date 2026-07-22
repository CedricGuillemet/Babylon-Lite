/** Minimal WebGPU device + meshopt-decoder mocks for MeshLoD loader tests.
 *
 *  The MeshLoD loader (Task 4.2 onward) decodes pinned pages and uploads them into a
 *  GPU arena, so Node tests need a stand-in device and decoder. The mock device
 *  records every `createBuffer`/`writeBuffer` so tests can assert the pinned prefix
 *  layout; the fill decoder produces in-range indices without WASM. */

import type { EngineContext } from "../../../../../packages/babylon-lite/src/engine/engine.js";
import type { MeshoptDecoderModule } from "../../../../../packages/babylon-lite/src/loader-gltf/meshopt-decode.js";

export interface MockBuffer {
    readonly label?: string;
    readonly size: number;
    readonly usage: number;
    destroyed: boolean;
    destroy(): void;
}

export interface BufferWrite {
    readonly buffer: MockBuffer;
    readonly offset: number;
    readonly byteLength: number;
}

export interface MockDevice {
    readonly limits: { maxStorageBufferBindingSize: number; maxBufferSize: number };
    readonly buffers: MockBuffer[];
    readonly writes: BufferWrite[];
    createBuffer(desc: { label?: string; size: number; usage: number }): MockBuffer;
    readonly queue: { writeBuffer(buffer: MockBuffer, offset: number, data: ArrayBuffer | ArrayBufferView, dataOffset?: number, size?: number): void };
}

/** Create a mock GPU device. `limitBytes` overrides the reported storage/buffer
 *  limits so device-limit boundaries can be exercised. */
export function createMockDevice(limitBytes = 1024 * 1024 * 1024): MockDevice {
    const buffers: MockBuffer[] = [];
    const writes: BufferWrite[] = [];
    const device: MockDevice = {
        limits: { maxStorageBufferBindingSize: limitBytes, maxBufferSize: limitBytes },
        buffers,
        writes,
        createBuffer(desc) {
            const buffer: MockBuffer = {
                label: desc.label,
                size: desc.size,
                usage: desc.usage,
                destroyed: false,
                destroy() {
                    this.destroyed = true;
                },
            };
            buffers.push(buffer);
            return buffer;
        },
        queue: {
            writeBuffer(buffer, offset, data, dataOffset = 0, size) {
                const byteLength = size ?? (ArrayBuffer.isView(data) ? data.byteLength - dataOffset : (data as ArrayBuffer).byteLength - dataOffset);
                writes.push({ buffer, offset, byteLength });
            },
        },
    };
    return device;
}

/** Wrap a mock device as an `EngineContext` for the loader. */
export function createMockEngine(device: MockDevice = createMockDevice()): { engine: EngineContext; device: MockDevice } {
    return { engine: { _device: device } as unknown as EngineContext, device };
}

/** A decoder that fills decoded vertex bytes with zeros and index bytes with a
 *  valid in-range pattern. Records the modes it was asked to decode. */
export function createFillDecoder(): { decoder: MeshoptDecoderModule; modes: string[] } {
    const modes: string[] = [];
    const decoder: MeshoptDecoderModule = {
        ready: Promise.resolve(),
        decodeGltfBuffer(target, count, size, _source, mode) {
            modes.push(mode);
            if (mode === "TRIANGLES") {
                const view = new DataView(target.buffer, target.byteOffset, target.byteLength);
                for (let i = 0; i < count; i++) {
                    view.setUint16(i * size, i % 3, true); // always in range for a >=3-vertex page
                }
            }
        },
    };
    return { decoder, modes };
}
