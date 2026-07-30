#include "input.h"

#include "cli.h"
#include "normalize.h"
#include "sha256.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <array>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace mlod {
namespace {

int mapParseResult(cgltf_result result) {
    switch (result) {
    case cgltf_result_success:
        return kExitSuccess;
    case cgltf_result_file_not_found:
    case cgltf_result_io_error:
    case cgltf_result_out_of_memory:
        return kExitIo;
    default:
        return kExitMalformed;
    }
}

bool isAllowedRequiredExtension(const char* name) {
    return name != nullptr &&
           (std::string(name) == "KHR_materials_unlit" ||
            std::string(name) == "KHR_materials_specular");
}

void primitiveContext(std::ostream& err, std::uint32_t meshIndex, std::uint32_t primitiveIndex,
                      const std::string& input) {
    err << "error: " << input << ": mesh " << meshIndex << " primitive " << primitiveIndex << ": ";
}

int rejectUnsupportedTexture(const cgltf_texture_view& view, const char* usage, bool& requiresUv,
                             std::uint32_t meshIndex, std::uint32_t primitiveIndex,
                             const std::string& input, std::ostream& err) {
    if (view.texture == nullptr) {
        return kExitSuccess;
    }
    if (view.has_transform) {
        primitiveContext(err, meshIndex, primitiveIndex, input);
        err << usage << " uses unsupported KHR_texture_transform\n";
        return kExitUnsupported;
    }
    if (view.texcoord != 0) {
        primitiveContext(err, meshIndex, primitiveIndex, input);
        err << usage << " uses unsupported TEXCOORD_" << view.texcoord << " (only TEXCOORD_0)\n";
        return kExitUnsupported;
    }
    requiresUv = true;
    return kExitSuccess;
}

int readMaterialFacts(const cgltf_material* material, MaterialFacts& facts, std::uint32_t meshIndex,
                      std::uint32_t primitiveIndex, const std::string& input, std::ostream& err) {
    if (material == nullptr) {
        facts = MaterialFacts{};
        return kExitSuccess;
    }

    facts.hasMaterial = true;
    facts.doubleSided = material->double_sided != 0;
    facts.unlit = material->unlit != 0;

    if (material->alpha_mode != cgltf_alpha_mode_opaque) {
        primitiveContext(err, meshIndex, primitiveIndex, input);
        err << "unsupported alpha mode (only OPAQUE is supported)\n";
        return kExitUnsupported;
    }

    struct Flag {
        cgltf_bool present;
        const char* name;
    };
    const Flag unsupported[] = {
        {material->has_pbr_specular_glossiness, "KHR_materials_pbrSpecularGlossiness"},
        {material->has_clearcoat, "KHR_materials_clearcoat"},
        {material->has_transmission, "KHR_materials_transmission"},
        {material->has_volume, "KHR_materials_volume"},
        {material->has_sheen, "KHR_materials_sheen"},
        {material->has_iridescence, "KHR_materials_iridescence"},
        {material->has_anisotropy, "KHR_materials_anisotropy"},
        {material->has_diffuse_transmission, "KHR_materials_diffuse_transmission"},
        {material->has_dispersion, "KHR_materials_dispersion"},
    };
    for (const Flag& flag : unsupported) {
        if (flag.present) {
            primitiveContext(err, meshIndex, primitiveIndex, input);
            err << "unsupported material extension " << flag.name << "\n";
            return kExitUnsupported;
        }
    }

    if (material->has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness& pbr = material->pbr_metallic_roughness;
        int rc = rejectUnsupportedTexture(pbr.base_color_texture, "base color texture",
                                          facts.requiresUv, meshIndex, primitiveIndex, input, err);
        if (rc != kExitSuccess) {
            return rc;
        }
        rc = rejectUnsupportedTexture(pbr.metallic_roughness_texture, "metallic-roughness texture",
                                      facts.requiresUv, meshIndex, primitiveIndex, input, err);
        if (rc != kExitSuccess) {
            return rc;
        }
    }

    if (material->has_specular) {
        int rc = rejectUnsupportedTexture(material->specular.specular_texture, "specular texture",
                                          facts.requiresUv, meshIndex, primitiveIndex, input, err);
        if (rc != kExitSuccess) {
            return rc;
        }
        rc = rejectUnsupportedTexture(material->specular.specular_color_texture,
                                      "specular color texture", facts.requiresUv, meshIndex,
                                      primitiveIndex, input, err);
        if (rc != kExitSuccess) {
            return rc;
        }
    }

    int rc = rejectUnsupportedTexture(material->normal_texture, "normal texture", facts.requiresUv,
                                      meshIndex, primitiveIndex, input, err);
    if (rc != kExitSuccess) {
        return rc;
    }
    rc = rejectUnsupportedTexture(material->occlusion_texture, "occlusion texture", facts.requiresUv,
                                  meshIndex, primitiveIndex, input, err);
    if (rc != kExitSuccess) {
        return rc;
    }
    rc = rejectUnsupportedTexture(material->emissive_texture, "emissive texture", facts.requiresUv,
                                  meshIndex, primitiveIndex, input, err);
    if (rc != kExitSuccess) {
        return rc;
    }

    return kExitSuccess;
}

int readFloatAccessor(const cgltf_accessor* accessor, cgltf_type expectedType, const char* attribute,
                      std::vector<float>& out, std::uint32_t meshIndex,
                      std::uint32_t primitiveIndex, const std::string& input, std::ostream& err) {
    if (accessor->is_sparse) {
        primitiveContext(err, meshIndex, primitiveIndex, input);
        err << attribute << " uses an unsupported sparse accessor\n";
        return kExitUnsupported;
    }
    if (accessor->buffer_view == nullptr || accessor->buffer_view->has_meshopt_compression) {
        primitiveContext(err, meshIndex, primitiveIndex, input);
        err << attribute << " uses unsupported EXT_meshopt_compression or has no buffer view\n";
        return kExitUnsupported;
    }
    if (accessor->type != expectedType) {
        primitiveContext(err, meshIndex, primitiveIndex, input);
        err << attribute << " has an unexpected component layout\n";
        return kExitMalformed;
    }
    const cgltf_size components = cgltf_num_components(accessor->type);
    const cgltf_size floatCount = accessor->count * components;
    out.resize(static_cast<std::size_t>(floatCount));
    const cgltf_size written = cgltf_accessor_unpack_floats(accessor, out.data(), floatCount);
    if (written != floatCount) {
        primitiveContext(err, meshIndex, primitiveIndex, input);
        err << attribute << " could not be read as floats\n";
        return kExitMalformed;
    }
    return kExitSuccess;
}

int readPrimitive(std::uint32_t meshIndex, std::uint32_t primitiveIndex,
                  const cgltf_primitive& primitive, const std::string& input, SourcePrimitive& out,
                  std::ostream& err) {
    if (primitive.type != cgltf_primitive_type_triangles) {
        primitiveContext(err, meshIndex, primitiveIndex, input);
        err << "unsupported primitive mode (only TRIANGLES is supported)\n";
        return kExitUnsupported;
    }
    if (primitive.has_draco_mesh_compression) {
        primitiveContext(err, meshIndex, primitiveIndex, input);
        err << "unsupported KHR_draco_mesh_compression\n";
        return kExitUnsupported;
    }
    if (primitive.targets_count > 0) {
        primitiveContext(err, meshIndex, primitiveIndex, input);
        err << "unsupported morph targets\n";
        return kExitUnsupported;
    }

    const cgltf_accessor* positionAccessor = nullptr;
    const cgltf_accessor* normalAccessor = nullptr;
    const cgltf_accessor* uvAccessor = nullptr;
    for (cgltf_size a = 0; a < primitive.attributes_count; ++a) {
        const cgltf_attribute& attribute = primitive.attributes[a];
        switch (attribute.type) {
        case cgltf_attribute_type_position:
            if (attribute.index == 0) {
                positionAccessor = attribute.data;
            }
            break;
        case cgltf_attribute_type_normal:
            if (attribute.index == 0) {
                normalAccessor = attribute.data;
            }
            break;
        case cgltf_attribute_type_texcoord:
            if (attribute.index == 0) {
                uvAccessor = attribute.data;
            }
            break;
        case cgltf_attribute_type_joints:
        case cgltf_attribute_type_weights:
            primitiveContext(err, meshIndex, primitiveIndex, input);
            err << "unsupported skinned primitive (JOINTS_0/WEIGHTS_0)\n";
            return kExitUnsupported;
        default:
            break;
        }
    }

    if (positionAccessor == nullptr) {
        primitiveContext(err, meshIndex, primitiveIndex, input);
        err << "missing required POSITION attribute\n";
        return kExitMalformed;
    }

    out = SourcePrimitive{};
    out.meshIndex = meshIndex;
    out.primitiveIndex = primitiveIndex;
    out.vertexCount = static_cast<std::uint32_t>(positionAccessor->count);

    int rc = readMaterialFacts(primitive.material, out.material, meshIndex, primitiveIndex, input, err);
    if (rc != kExitSuccess) {
        return rc;
    }

    rc = readFloatAccessor(positionAccessor, cgltf_type_vec3, "POSITION", out.positions, meshIndex,
                           primitiveIndex, input, err);
    if (rc != kExitSuccess) {
        return rc;
    }

    if (normalAccessor != nullptr) {
        rc = readFloatAccessor(normalAccessor, cgltf_type_vec3, "NORMAL", out.normals, meshIndex,
                               primitiveIndex, input, err);
        if (rc != kExitSuccess) {
            return rc;
        }
    }

    if (uvAccessor != nullptr) {
        rc = readFloatAccessor(uvAccessor, cgltf_type_vec2, "TEXCOORD_0", out.uvs, meshIndex,
                               primitiveIndex, input, err);
        if (rc != kExitSuccess) {
            return rc;
        }
    }

    if (out.material.requiresUv && out.uvs.empty()) {
        primitiveContext(err, meshIndex, primitiveIndex, input);
        err << "material requires TEXCOORD_0 but the primitive has none\n";
        return kExitUnsupported;
    }

    if (primitive.indices != nullptr) {
        const cgltf_accessor* indexAccessor = primitive.indices;
        if (indexAccessor->is_sparse) {
            primitiveContext(err, meshIndex, primitiveIndex, input);
            err << "indices use an unsupported sparse accessor\n";
            return kExitUnsupported;
        }
        if (indexAccessor->buffer_view == nullptr ||
            indexAccessor->buffer_view->has_meshopt_compression) {
            primitiveContext(err, meshIndex, primitiveIndex, input);
            err << "indices use unsupported EXT_meshopt_compression or have no buffer view\n";
            return kExitUnsupported;
        }
        if (indexAccessor->type != cgltf_type_scalar) {
            primitiveContext(err, meshIndex, primitiveIndex, input);
            err << "indices have an unexpected component layout\n";
            return kExitMalformed;
        }
        out.indices.resize(static_cast<std::size_t>(indexAccessor->count));
        for (cgltf_size i = 0; i < indexAccessor->count; ++i) {
            out.indices[i] = static_cast<std::uint32_t>(cgltf_accessor_read_index(indexAccessor, i));
        }
    }

    return kExitSuccess;
}

bool isDataUri(const char* uri) {
    return uri != nullptr && std::strncmp(uri, "data:", 5) == 0;
}

// Resolves every glTF buffer's bytes in-place on `data`: the GLB BIN chunk and
// data URIs are decoded internally (no host I/O); every other buffer is
// materialized through `bundle.resolver`. Externally resolved buffer bytes are
// kept alive in `ownedBuffers` (indexed by buffer index) for the remainder of
// ingestion, and each is appended to `digestParts` in buffer-index order for
// the caller to hash. `bundle.entryBytes`/`ownedBuffers` must outlive `data`'s use.
int resolveBuffers(cgltf_data* data, const cgltf_options& parseOptions, const InputBundle& bundle,
                   std::vector<std::vector<unsigned char>>& ownedBuffers,
                   std::vector<SourcePart>& digestParts, std::ostream& err) {
    ownedBuffers.assign(data->buffers_count, std::vector<unsigned char>());

    for (cgltf_size i = 0; i < data->buffers_count; ++i) {
        cgltf_buffer& buffer = data->buffers[i];
        const char* uri = buffer.uri;

        if (uri == nullptr) {
            // Embedded GLB BIN chunk (buffer 0 only, by glTF convention).
            if (data->bin == nullptr || data->bin_size < buffer.size) {
                err << "error: " << bundle.sourceDisplayName << ": buffer " << i
                    << " has no URI and no embedded GLB binary chunk\n";
                return kExitMalformed;
            }
            buffer.data = const_cast<void*>(data->bin);
            buffer.data_free_method = cgltf_data_free_method_none;
            continue;
        }

        if (isDataUri(uri)) {
            const char* comma = std::strchr(uri, ',');
            if (comma == nullptr || comma - uri < 7 || std::strncmp(comma - 7, ";base64", 7) != 0) {
                err << "error: " << bundle.sourceDisplayName << ": buffer " << i
                    << " uses an unsupported data URI (only base64 is supported)\n";
                return kExitMalformed;
            }
            const cgltf_result decodeResult =
                cgltf_load_buffer_base64(&parseOptions, buffer.size, comma + 1, &buffer.data);
            if (decodeResult != cgltf_result_success) {
                err << "error: " << bundle.sourceDisplayName << ": buffer " << i
                    << " could not be decoded from its data URI\n";
                return mapParseResult(decodeResult);
            }
            buffer.data_free_method = cgltf_data_free_method_memory_free;
            continue;
        }

        // External resource: ask the resolver (native filesystem, or the
        // caller-provided virtual file set) rather than touching a host API
        // directly.
        if (!bundle.resolver) {
            err << "error: " << bundle.sourceDisplayName << ": buffer " << i
                << " references external resource '" << uri << "' but no resolver is available\n";
            return kExitIo;
        }
        ResolvedResource resource;
        Diagnostic error;
        char property[32];
        std::snprintf(property, sizeof(property), "buffers[%llu].uri", static_cast<unsigned long long>(i));
        if (!bundle.resolver(uri, property, ResourceKind::kBuffer, resource, error)) {
            err << "error: " << bundle.sourceDisplayName << ": " << property << ": " << error.message << "\n";
            return static_cast<int>(error.nativeExitCategory);
        }
        if (resource.bytes.size() < buffer.size) {
            err << "error: " << bundle.sourceDisplayName << ": buffer " << i << " ('" << uri
                << "') is shorter than its declared size\n";
            return kExitMalformed;
        }
        ownedBuffers[i] = std::move(resource.bytes);
        buffer.data = ownedBuffers[i].data();
        buffer.data_free_method = cgltf_data_free_method_none;
        digestParts.push_back({ownedBuffers[i].data(), static_cast<std::size_t>(buffer.size)});
    }

    return kExitSuccess;
}

// Validates that every referenced image resolves, without retaining its
// bytes: images never affect normalized geometry or the source digest
// (architecture section 7.11), but a missing image must still fail before
// conversion since materials reference them. When `outTotalBytes` is non-null
// it accumulates each resolved image's byte size (for inspection's `F`
// preflight term) without keeping the bytes themselves.
int validateImages(const cgltf_data* data, const InputBundle& bundle, std::ostream& err,
                   std::uint64_t* outTotalBytes = nullptr) {
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        const cgltf_image& image = data->images[i];
        if (image.uri == nullptr || isDataUri(image.uri) || image.buffer_view != nullptr) {
            continue; // embedded (data URI or GLB bufferView); no external resolution needed
        }
        if (!bundle.resolver) {
            err << "error: " << bundle.sourceDisplayName << ": image " << i << " references external resource '"
                << image.uri << "' but no resolver is available\n";
            return kExitIo;
        }
        ResolvedResource resource;
        Diagnostic error;
        char property[32];
        std::snprintf(property, sizeof(property), "images[%llu].uri", static_cast<unsigned long long>(i));
        if (!bundle.resolver(image.uri, property, ResourceKind::kImage, resource, error)) {
            err << "error: " << bundle.sourceDisplayName << ": " << property << ": " << error.message << "\n";
            return static_cast<int>(error.nativeExitCategory);
        }
        if (outTotalBytes != nullptr) {
            *outTotalBytes += resource.bytes.size();
        }
    }
    return kExitSuccess;
}

// Expands `selection` into concrete mesh/primitive identities in document
// order, validating indices exactly as the native CLI parser did.
int resolveSelection(const cgltf_data* data, const PrimitiveSelection& selection, const InputBundle& bundle,
                     std::vector<PrimitiveIdentity>& out, std::ostream& err) {
    out.clear();
    switch (selection.mode) {
    case PrimitiveSelectionMode::kSinglePrimitive: {
        if (selection.meshIndex >= data->meshes_count) {
            err << "error: " << bundle.sourceDisplayName << ": mesh index " << selection.meshIndex
                << " is out of range (" << data->meshes_count << " meshes)\n";
            return kExitCli;
        }
        const cgltf_mesh& mesh = data->meshes[selection.meshIndex];
        if (selection.primitiveIndex >= mesh.primitives_count) {
            err << "error: " << bundle.sourceDisplayName << ": primitive index " << selection.primitiveIndex
                << " is out of range (" << mesh.primitives_count << " primitives in mesh " << selection.meshIndex
                << ")\n";
            return kExitCli;
        }
        out.push_back({selection.meshIndex, selection.primitiveIndex});
        break;
    }
    case PrimitiveSelectionMode::kWholeMesh: {
        if (selection.meshIndex >= data->meshes_count) {
            err << "error: " << bundle.sourceDisplayName << ": mesh index " << selection.meshIndex
                << " is out of range (" << data->meshes_count << " meshes)\n";
            return kExitCli;
        }
        const cgltf_mesh& mesh = data->meshes[selection.meshIndex];
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            out.push_back({selection.meshIndex, static_cast<std::uint32_t>(p)});
        }
        break;
    }
    case PrimitiveSelectionMode::kAllPrimitives:
    default:
        for (cgltf_size m = 0; m < data->meshes_count; ++m) {
            for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p) {
                out.push_back({static_cast<std::uint32_t>(m), static_cast<std::uint32_t>(p)});
            }
        }
        break;
    }
    return kExitSuccess;
}

} // namespace

int loadSourcePrimitivesFromBundle(const InputBundle& bundle, const PrimitiveSelection& selection,
                                   std::vector<SourcePrimitive>& out, std::ostream& err,
                                   std::array<std::uint8_t, 32>* sourceDigest) {
    const std::string& input = bundle.sourceDisplayName;

    cgltf_options parseOptions = {};
    cgltf_data* data = nullptr;
    cgltf_result result =
        cgltf_parse(&parseOptions, bundle.entryBytes.data(), bundle.entryBytes.size(), &data);
    if (result != cgltf_result_success) {
        err << "error: " << input << ": failed to parse glTF/GLB\n";
        return mapParseResult(result);
    }

    std::vector<std::vector<unsigned char>> ownedBuffers;
    std::vector<SourcePart> digestParts;
    digestParts.push_back({bundle.entryBytes.data(), bundle.entryBytes.size()});

    int rc = resolveBuffers(data, parseOptions, bundle, ownedBuffers, digestParts, err);
    if (rc != kExitSuccess) {
        cgltf_free(data);
        return rc;
    }

    result = cgltf_validate(data);
    if (result != cgltf_result_success) {
        cgltf_free(data);
        err << "error: " << input << ": glTF validation failed\n";
        return kExitMalformed;
    }

    for (cgltf_size e = 0; e < data->extensions_required_count; ++e) {
        if (!isAllowedRequiredExtension(data->extensions_required[e])) {
            err << "error: " << input << ": unsupported required extension "
                << data->extensions_required[e] << "\n";
            cgltf_free(data);
            return kExitUnsupported;
        }
    }

    rc = validateImages(data, bundle, err);
    if (rc != kExitSuccess) {
        cgltf_free(data);
        return rc;
    }

    std::vector<PrimitiveIdentity> selectionResult;
    rc = resolveSelection(data, selection, bundle, selectionResult, err);
    if (rc != kExitSuccess) {
        cgltf_free(data);
        return rc;
    }

    out.clear();
    out.reserve(selectionResult.size());
    for (const PrimitiveIdentity& selected : selectionResult) {
        const cgltf_primitive& primitive = data->meshes[selected.meshIndex].primitives[selected.primitiveIndex];
        SourcePrimitive primitiveOut;
        rc = readPrimitive(selected.meshIndex, selected.primitiveIndex, primitive, input, primitiveOut, err);
        if (rc != kExitSuccess) {
            cgltf_free(data);
            return rc;
        }
        out.push_back(std::move(primitiveOut));
    }

    if (out.empty()) {
        cgltf_free(data);
        err << "error: " << input << ": no primitives found to convert\n";
        return kExitMalformed;
    }

    if (sourceDigest != nullptr) {
        *sourceDigest = computeSourceDigest(digestParts);
    }

    cgltf_free(data);
    return kExitSuccess;
}

namespace {
// Detects "glb" vs "gltf" from the entry bytes' leading magic (glTF binary
// files begin with the 4-byte ASCII magic "glTF"; JSON documents do not).
std::string detectEntryType(const std::vector<unsigned char>& entryBytes) {
    if (entryBytes.size() >= 4 && entryBytes[0] == 'g' && entryBytes[1] == 'l' && entryBytes[2] == 'T' &&
        entryBytes[3] == 'F') {
        return "glb";
    }
    return "gltf";
}
} // namespace

int inspectDocument(const InputBundle& bundle, DocumentInventory& out, const DiagnosticSink& diagnostics,
                   const CancellationProbe& cancel) {
    out = DocumentInventory{};
    out.entryType = detectEntryType(bundle.entryBytes);

    cgltf_options parseOptions = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse(&parseOptions, bundle.entryBytes.data(), bundle.entryBytes.size(), &data);
    if (result != cgltf_result_success) {
        emitDiagnostic(diagnostics, makeDiagnostic(diag_code::kMalformed, kExitMalformed, DiagnosticSeverity::kError,
                                                   "failed to parse glTF/GLB"));
        return mapParseResult(result);
    }

    if (cancel && cancel()) {
        cgltf_free(data);
        return kExitCli;
    }

    std::vector<std::vector<unsigned char>> ownedBuffers;
    std::vector<SourcePart> digestPartsUnused; // inspection never needs the source digest
    std::ostringstream resolveErr;
    int rc = resolveBuffers(data, parseOptions, bundle, ownedBuffers, digestPartsUnused, resolveErr);
    if (rc != kExitSuccess) {
        emitDiagnostic(diagnostics, makeDiagnostic(diag_code::kIoRead, static_cast<ExitCode>(rc),
                                                   DiagnosticSeverity::kError, resolveErr.str()));
        cgltf_free(data);
        return rc;
    }
    for (cgltf_size i = 0; i < data->buffers_count; ++i) {
        const cgltf_buffer& buffer = data->buffers[i];
        if (buffer.uri == nullptr) {
            continue; // GLB BIN chunk: already counted once as part of the entry bytes below
        }
        out.copiedResourceBytes += buffer.size; // B: decoded data URIs and external buffers are both copied
        if (!isDataUri(buffer.uri)) {
            out.resolvedResourcePaths.push_back(buffer.uri);
            out.selectedBytes += buffer.size; // F: only externally *selected* files count, not decoded data URIs
        }
    }

    result = cgltf_validate(data);
    if (result != cgltf_result_success) {
        emitDiagnostic(diagnostics,
                       makeDiagnostic(diag_code::kMalformed, kExitMalformed, DiagnosticSeverity::kError,
                                     "glTF validation failed"));
        cgltf_free(data);
        return kExitMalformed;
    }

    for (cgltf_size e = 0; e < data->extensions_required_count; ++e) {
        if (!isAllowedRequiredExtension(data->extensions_required[e])) {
            DiagnosticContext context;
            context.extensionName = data->extensions_required[e];
            emitDiagnostic(diagnostics, makeDiagnostic(diag_code::kUnsupported, kExitUnsupported,
                                                       DiagnosticSeverity::kError,
                                                       "unsupported required extension", context));
            cgltf_free(data);
            return kExitUnsupported;
        }
    }

    std::uint64_t imageBytes = 0;
    std::ostringstream imageErr;
    rc = validateImages(data, bundle, imageErr, &imageBytes);
    if (rc != kExitSuccess) {
        emitDiagnostic(diagnostics,
                       makeDiagnostic(diag_code::kIoRead, static_cast<ExitCode>(rc), DiagnosticSeverity::kError,
                                     imageErr.str()));
        cgltf_free(data);
        return rc;
    }
    out.selectedBytes += imageBytes;
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        if (data->images[i].uri != nullptr && !isDataUri(data->images[i].uri)) {
            out.resolvedResourcePaths.push_back(data->images[i].uri);
        }
    }
    out.selectedBytes += bundle.entryBytes.size(); // F always includes the entry document itself

    out.meshPrimitiveCounts.reserve(data->meshes_count);
    out.primitives.reserve(data->meshes_count > 0 ? data->meshes_count * data->meshes[0].primitives_count : 0);
    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        const cgltf_mesh& mesh = data->meshes[m];
        out.meshPrimitiveCounts.push_back(static_cast<std::uint32_t>(mesh.primitives_count));
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            if (cancel && cancel()) {
                cgltf_free(data);
                return kExitCli;
            }

            InspectedPrimitive inspected;
            inspected.identity = {static_cast<std::uint32_t>(m), static_cast<std::uint32_t>(p)};

            std::ostringstream primitiveErr;
            SourcePrimitive probe;
            // Reuse the exact same per-primitive validation conversion uses
            // (no independent copy of the rejection rules): a primitive is
            // "supported" precisely when readPrimitive would accept it.
            const int primitiveRc = readPrimitive(inspected.identity.meshIndex, inspected.identity.primitiveIndex,
                                                  mesh.primitives[p], bundle.sourceDisplayName, probe, primitiveErr);
            if (primitiveRc == kExitSuccess) {
                inspected.supported = true;
                inspected.sourceVertexCount = probe.vertexCount;
                inspected.sourceTriangleCount =
                    probe.indices.empty() ? probe.vertexCount / 3 : static_cast<std::uint32_t>(probe.indices.size() / 3);
            } else {
                inspected.supported = false;
                const cgltf_primitive& primitive = mesh.primitives[p];
                for (cgltf_size a = 0; a < primitive.attributes_count; ++a) {
                    if (primitive.attributes[a].type == cgltf_attribute_type_position) {
                        inspected.sourceVertexCount = static_cast<std::uint32_t>(primitive.attributes[a].data->count);
                        break;
                    }
                }
                inspected.sourceTriangleCount =
                    primitive.indices != nullptr ? static_cast<std::uint32_t>(primitive.indices->count / 3)
                                                 : inspected.sourceVertexCount / 3;

                DiagnosticContext context;
                context.hasMesh = true;
                context.meshIndex = inspected.identity.meshIndex;
                context.hasPrimitive = true;
                context.primitiveIndex = inspected.identity.primitiveIndex;
                const ExitCode category = (primitiveRc == kExitMalformed) ? kExitMalformed : kExitUnsupported;
                out.warnings.push_back(makeDiagnostic(diag_code::kUnsupported, category, DiagnosticSeverity::kWarning,
                                                      primitiveErr.str(), context));
            }
            out.primitives.push_back(inspected);
        }
    }

    cgltf_free(data);
    return kExitSuccess;
}

// NOTE: derivePrimitiveOutputPath/deriveOutputPaths are declared in input.h
// but intentionally NOT defined here -- cli.cpp already provides the sole
// definition (pre-existing repo structure, unrelated to tasks 10.1/10.2).
// Adding a second definition here would violate the one-definition rule.

} // namespace mlod
