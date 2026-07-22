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
#include <fstream>
#include <ostream>
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
    return name != nullptr && std::string(name) == "KHR_materials_unlit";
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
        {material->has_specular, "KHR_materials_specular"},
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

} // namespace

int loadSourcePrimitives(const ConversionOptions& options, std::vector<SourcePrimitive>& out,
                         std::ostream& err, std::array<std::uint8_t, 32>* sourceDigest) {
    const std::string& input = options.inputPath;

    cgltf_options parseOptions = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&parseOptions, input.c_str(), &data);
    if (result != cgltf_result_success) {
        err << "error: " << input << ": failed to parse glTF/GLB\n";
        return mapParseResult(result);
    }

    result = cgltf_load_buffers(&parseOptions, data, input.c_str());
    if (result != cgltf_result_success) {
        cgltf_free(data);
        err << "error: " << input << ": failed to load buffers\n";
        return mapParseResult(result);
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

    std::vector<SelectedPrimitive> selection;
    if (options.hasMesh) {
        if (options.meshIndex >= data->meshes_count) {
            err << "error: " << input << ": mesh index " << options.meshIndex
                << " is out of range (" << data->meshes_count << " meshes)\n";
            cgltf_free(data);
            return kExitCli;
        }
        const cgltf_mesh& mesh = data->meshes[options.meshIndex];
        if (options.hasPrimitive) {
            if (options.primitiveIndex >= mesh.primitives_count) {
                err << "error: " << input << ": primitive index " << options.primitiveIndex
                    << " is out of range (" << mesh.primitives_count << " primitives in mesh "
                    << options.meshIndex << ")\n";
                cgltf_free(data);
                return kExitCli;
            }
            selection.push_back({options.meshIndex, options.primitiveIndex});
        } else {
            for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
                selection.push_back({options.meshIndex, static_cast<std::uint32_t>(p)});
            }
        }
    } else {
        for (cgltf_size m = 0; m < data->meshes_count; ++m) {
            for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p) {
                selection.push_back(
                    {static_cast<std::uint32_t>(m), static_cast<std::uint32_t>(p)});
            }
        }
    }

    out.clear();
    out.reserve(selection.size());
    for (const SelectedPrimitive& selected : selection) {
        const cgltf_primitive& primitive =
            data->meshes[selected.meshIndex].primitives[selected.primitiveIndex];
        SourcePrimitive primitiveOut;
        const int rc = readPrimitive(selected.meshIndex, selected.primitiveIndex, primitive, input,
                                     primitiveOut, err);
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
        std::ifstream file(input, std::ios::binary);
        if (!file) {
            cgltf_free(data);
            err << "error: " << input << ": could not reopen input for hashing\n";
            return kExitIo;
        }
        std::vector<unsigned char> mainBytes((std::istreambuf_iterator<char>(file)),
                                             std::istreambuf_iterator<char>());
        std::vector<SourcePart> parts;
        parts.push_back({mainBytes.data(), mainBytes.size()});
        // External geometry buffers (not embedded data URIs, not GLB binary) are
        // hashed after the main file; images are not cgltf buffers and so are
        // excluded automatically.
        for (cgltf_size b = 0; b < data->buffers_count; ++b) {
            const cgltf_buffer& buffer = data->buffers[b];
            if (buffer.uri != nullptr && std::strncmp(buffer.uri, "data:", 5) != 0 &&
                buffer.data != nullptr) {
                parts.push_back({buffer.data, buffer.size});
            }
        }
        *sourceDigest = computeSourceDigest(parts);
    }

    cgltf_free(data);
    return kExitSuccess;
}

} // namespace mlod
