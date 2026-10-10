#include <algorithm>
#include <cctype>
#include <memory>
#include <utility>

#include "wiz8/engine_code/stMeshModel.h"

#include "wiz8/float_constants.h"
#include "wiz8/sr_api.h"
#include "surrender/srCore.h"
#include "surrender/srGERD.h"
#include "surrender/srMaterial.h"
#include "surrender/srTriangleCuller.h"

#include "surrender/srTriMeshPipeline.h"
#include "surrender/srTypeRegistry.h"
#include "surrender/srVectorMath.h"
#include "wiz8/engine_code/Octree.h"

#include <math.h>
#include <string.h>

extern srVector3T<float> g_environment_offset;
extern float g_monster_light_scale;

// GLOBAL: WIZ8 0x00659cb8
std::vector<stMeshModel*> g_mesh_models;

// GLOBAL: WIZ8 0x0065a0e8
int g_decompressed_mesh_bytes;

/* Active-polygon scratch for the optional software backface pass in
   RenderTriMeshWithEquations. */
// GLOBAL: WIZ8 0x00659ce0
std::vector<w8_ulong> g_software_cull_active_polygons;

/* Byte budget for the decompressed per-frame caches; AllocateFrameBuffers
   reclaims least-recently-used frames past it. */
// GLOBAL: WIZ8 0x00609d34
static int g_decompressed_mesh_byte_limit = 0x800000;

// FUNCTION: WIZ8 0x00470B00
stMeshModel::stMeshModel(w8_long polygons, w8_long vertices)
    : srClassSupport<stMeshModel, srMeshModel, false, 0x10003>(polygons, vertices)
{
    duplicate_on_reuse = 1;
    vertex_lighting_ready = false;
    previous = 0;
    next = 0;
    flags = W8_MESH_VERTEX_LIGHTING_DIRTY;
    ambient_color = -1.0f;
    vertex_light_table = 0;
    vertex_compression_scale = 1.0f;
    last_decompress_release_tick = w8_get_ticks();
    render_control.value &= ~0x10UL;
    setDirty(DIRTY_BOUNDS);
    automap_filter_active = false;
}

// FUNCTION: WIZ8 0x00470ED0
stMeshModel::~stMeshModel()
{
    if (next != 0) {
        stMeshModel* linked = next;
        next = 0;
        delete linked;
    }
    ReleaseDecompressedFrames();
    std::erase(g_mesh_models, this);
}

stMeshModel& stMeshModel::operator=(const stMeshModel& other)
{
    if (this == &other) {
        return *this;
    }
    // Prepare independent ownership before replacing this registered model.
    // In particular, a registry clone must not share the owned successor chain
    // or count the source's decompressed allocations a second time.
    auto copied_frames = other.frames;
    auto copied_skins = other.skins;
    auto copied_mappings = other.mapped_vertices;
    auto copied_lerp = other.lerp_buffer;
    auto copied_automap = other.automap_polygons;
    auto copied_lights_0 = other.vertex_lights[0];
    auto copied_lights_1 = other.vertex_lights[1];
    auto copied_sunlight = other.vertex_sunlight;
    std::unique_ptr<stMeshModel> linked;
    if (other.next != nullptr) {
        linked = std::make_unique<stMeshModel>(0, 0);
        *linked = *other.next;
    }
    srMeshModel::operator=(other);
    if (frames.empty() && !copied_frames.empty()) {
        g_mesh_models.push_back(this);
    } else if (copied_frames.empty()) {
        std::erase(g_mesh_models, this);
    }
    ReleaseDecompressedFrames();
    frames = std::move(copied_frames);
    for (const auto& frame : frames) {
        for (const auto* cache : {&frame.locations, &frame.vertex_normals, &frame.polygon_normals}) {
            if (*cache) {
                g_decompressed_mesh_bytes += (*cache)->capacity() * sizeof(srVector3T<float>);
            }
        }
    }
    skins = std::move(copied_skins);
    mapped_vertices = std::move(copied_mappings);
    lerp_buffer = std::move(copied_lerp);
    automap_polygons = std::move(copied_automap);
    vertex_lights[0] = std::move(copied_lights_0);
    vertex_lights[1] = std::move(copied_lights_1);
    vertex_sunlight = std::move(copied_sunlight);
    flags = other.flags;
    ambient_color = other.ambient_color;
    vertex_light_table = other.vertex_light_table;
    duplicate_on_reuse = other.duplicate_on_reuse;
    vertex_lighting_ready = other.vertex_lighting_ready;
    vertex_compression_scale = other.vertex_compression_scale;
    automap_filter_active = other.automap_filter_active;
    delete next;
    next = linked.release();
    if (next != nullptr) {
        next->previous = this;
    }
    return *this;
}

// FUNCTION: WIZ8 0x004748c0
srClass* stMeshModel::vInstance()
{
    return new stMeshModel(0, 0);
}

// FUNCTION: WIZ8 0x00471dd0
int stMeshModel::getBoundingSphere(srVector3T<float>& center, float& radius)
{
    if ((dirty_flags.value & 1) != 0) {
        CalculateLinkedBounds();
    }
    center = bounds_center;
    radius = bounds_radius;
    return 1;
}

// FUNCTION: WIZ8 0x00471d80
int stMeshModel::getBoundingBox(srVector3T<float>& minimum, srVector3T<float>& maximum)
{
    if ((dirty_flags.value & 1) != 0) {
        CalculateLinkedBounds();
    }
    minimum = bounds_minimum;
    maximum = bounds_maximum;
    return 1;
}

/* Recompute the linked group's union bounds: clear this model's cached box,
   accumulate every frame's bounds from the whole previous/next chain, then
   push the resulting box, center and radius to each member. Models whose
   flags bit 2 is clear answer from the base-class bounding box; the rest
   decompress each frame's vertex table. */
// FUNCTION: WIZ8 0x00471E10
void stMeshModel::CalculateLinkedBounds()
{
    bounds_minimum.SetZero();
    bounds_maximum.SetZero();
    bounds_center.SetZero();
    bounds_radius = 0;

    stMeshModel* head = this;
    while (head->previous != 0) {
        head = head->previous;
    }

    stMeshModel* model;
    srVector3T<float> minimum;
    srVector3T<float> maximum;
    minimum.SetZero();
    maximum.SetZero();
    for (model = head; model != 0; model = model->next) {
        for (unsigned int frame = 0; frame < std::max(size_t{1}, model->frames.size()); ++frame) {
            srVector3T<float> frame_minimum;
            srVector3T<float> frame_maximum;
            if ((model->flags & W8_MESH_HAS_FRAME_STORAGE) == 0) {
                model->srMeshModel::getBoundingBox(frame_minimum, frame_maximum);
            } else {
                model->GetFrameBounds(frame, &frame_minimum, &frame_maximum);
            }
            if (frame_minimum.x < minimum.x) {
                minimum.x = frame_minimum.x;
            }
            if (frame_minimum.y < minimum.y) {
                minimum.y = frame_minimum.y;
            }
            if (frame_minimum.z < minimum.z) {
                minimum.z = frame_minimum.z;
            }
            if (maximum.x < frame_maximum.x) {
                maximum.x = frame_maximum.x;
            }
            if (maximum.y < frame_maximum.y) {
                maximum.y = frame_maximum.y;
            }
            if (maximum.z < frame_maximum.z) {
                maximum.z = frame_maximum.z;
            }
        }
    }

    srVector3T<float> center;
    center.Set((maximum.x + minimum.x) * g_double_half, (maximum.y + minimum.y) * g_double_half,
               (maximum.z + minimum.z) * g_double_half);
    float radius = static_cast<float>(sqrt((maximum.x - center.x) * (maximum.x - center.x) +
                                           (maximum.y - center.y) * (maximum.y - center.y) +
                                           (maximum.z - center.z) * (maximum.z - center.z)));
    float minimum_distance =
        static_cast<float>(sqrt((minimum.x - center.x) * (minimum.x - center.x) +
                                (minimum.y - center.y) * (minimum.y - center.y) +
                                (minimum.z - center.z) * (minimum.z - center.z)));
    if (radius < minimum_distance) {
        radius = minimum_distance;
    }
    head->setBounds(minimum, maximum, center, radius);
    for (model = head->next; model != 0; model = model->next) {
        model->setBounds(minimum, maximum, center, radius);
    }
}

/* Bounds `frame`'s vertex table into `minimum`/`maximum`. When the frame is
   not resident it is decompressed into a scratch array that is released
   afterward; empty tables produce the zero vector on both outputs. */
// FUNCTION: WIZ8 0x00473190
void stMeshModel::GetFrameBounds(int frame, srVector3T<float>* minimum, srVector3T<float>* maximum)
{
    minimum->SetZero();
    maximum->SetZero();
    if (static_cast<unsigned int>(frame) >= frames.size() || vertex_location_count == 0) {
        return;
    }
    std::vector<srVector3T<float>> scratch;
    const auto& cache = frames[frame].locations;
    const srVector3T<float>* vertices;
    if (cache) {
        vertices = cache->data();
    } else {
        scratch.resize(vertex_location_count);
        DecompressFrame(frame, W8_MESH_FRAME_LOCATIONS, scratch.data());
        vertices = scratch.data();
    }
    srMath::minMax({vertices, static_cast<std::size_t>(vertex_location_count)},
                   *minimum, *maximum);
}

/* Apply pending vertex DIG lighting when flags bit 1 is set, then return
   the SurRender TriMesh cache. */
// FUNCTION: WIZ8 0x00472270
const srMeshModel::TriMesh& stMeshModel::getTriMesh()
{
    float light_scale = g_monster_light_scale;
    srVector3T<float>* dig;
    srVector3T<float>* lights;
    float* sunlight;
    srPtr<srMaterialIFace>* vertex_materials;
    int count;
    int index;
    int run;
    int next_index;
    srMaterialIFace* material_iface;
    srMaterial* material;
    srVector3T<float> scaled;
    srVector3T<float> ambient_rgb;

    if ((flags & W8_MESH_VERTEX_LIGHTING_DIRTY) != 0 && !g_render_unlit) {
        lights = vertex_lights[vertex_light_table].data();
        sunlight = vertex_sunlight.data();
        if (!vertex_lights[vertex_light_table].empty() && !vertex_sunlight.empty()) {
            dig = getVertexDIG(0, 1);
            vertex_materials = getVertexMaterial(0, SIDE_FRONT, 0);
            if (vertex_materials == 0) {
                if ((IsZeroVector(&ambient_color) != 0) || vertex_light_table == 1) {
                    if (dig != lights) {
                        std::copy_n(lights, vertex_location_count, dig);
                    }
                    if ((g_environment_offset.x != g_float_zero ||
                         g_environment_offset.y != g_float_zero ||
                         g_environment_offset.z != g_float_zero) &&
                        vertex_light_table != 1 && (count = vertex_location_count, count != 0) &&
                        IsZeroVector(&g_environment_offset) == 0) {
                        srMath::add({dig, static_cast<std::size_t>(static_cast<SRDWORD>(count))},
                                    g_environment_offset, dig);
                    }
                } else {
                    /* Retail indexes material ambient at +0x28; that is
                       srMaterial::parms.ambient on the concrete type. */
                    material = static_cast<srMaterial*>(getMaterial(0, SIDE_FRONT));
                    ambient_rgb = material->parms.ambient.xyz();
                    count = vertex_location_count;
                    scaled = ambient_color;
                    scaled *= ambient_rgb;
                    if (count != 0) {
                        if (scaled.x == scaled.y && scaled.x == scaled.z) {
                            std::fill_n(dig, count,
                                        srVector3T<float>(scaled.x, scaled.x, scaled.x));
                        } else {
                            std::fill_n(dig, count, scaled);
                        }
                    }
                    if (vertex_location_count != 0) {
                        srMath::mul({dig, static_cast<std::size_t>(
                                              static_cast<SRDWORD>(vertex_location_count))},
                                    dig, sunlight);
                    }
                    std::transform(dig, dig + vertex_location_count, lights, dig,
                                   [](const auto& value, const auto& light) {
                                       return value + light;
                                   });
                    if ((g_environment_offset.x != g_float_zero ||
                         g_environment_offset.y != g_float_zero ||
                         g_environment_offset.z != g_float_zero) &&
                        (count = vertex_location_count, count != 0) &&
                        IsZeroVector(&g_environment_offset) == 0) {
                        srMath::add({dig, static_cast<std::size_t>(static_cast<SRDWORD>(count))},
                                    g_environment_offset, dig);
                    }
                }
            } else {
                if ((IsZeroVector(&ambient_color) != 0) || vertex_light_table == 1) {
                    if (vertex_location_count != 0) {
                        std::fill_n(dig, vertex_location_count,
                                    srVector3T<float>(0.0f, 0.0f, 0.0f));
                    }
                } else {
                    count = vertex_location_count;
                    if (count != 0) {
                        if (ambient_color.x == ambient_color.y &&
                            ambient_color.x == ambient_color.z) {
                            std::fill_n(dig, count,
                                        srVector3T<float>(ambient_color.x, ambient_color.x,
                                                          ambient_color.x));
                        } else {
                            std::fill_n(dig, count, ambient_color);
                        }
                    }
                    if (vertex_location_count != 0) {
                        srMath::mul({dig, static_cast<std::size_t>(
                                              static_cast<SRDWORD>(vertex_location_count))},
                                    dig, sunlight);
                    }
                }
                index = 0;
                do {
                    next_index = index + 1;
                    run = 1;
                    if (next_index < vertex_location_count) {
                        do {
                            if (static_cast<srMaterialIFace*>(vertex_materials[index]) !=
                                static_cast<srMaterialIFace*>(vertex_materials[next_index])) {
                                break;
                            }
                            ++run;
                            ++next_index;
                        } while (next_index < vertex_location_count);
                    }
                    material_iface = vertex_materials[index];
                    if (material_iface != 0) {
                        material = static_cast<srMaterial*>(material_iface);
                        scaled = material->parms.ambient.xyz();
                        if (run != 0) {
                            if (IsZeroVector(&scaled) == 0) {
                                srMath::mul({dig + index,
                                             static_cast<std::size_t>(static_cast<SRDWORD>(run))},
                                            scaled, dig + index);
                            } else {
                                std::fill_n(dig + index, run,
                                            srVector3T<float>(0.0f, 0.0f, 0.0f));
                            }
                        }
                    }
                    index += run;
                } while (index < vertex_location_count);
                std::transform(dig, dig + vertex_location_count, lights, dig,
                               [](const auto& value, const auto& light) {
                                   return value + light;
                               });
                if ((g_environment_offset.x != g_float_zero ||
                     g_environment_offset.y != g_float_zero ||
                     g_environment_offset.z != g_float_zero) &&
                    vertex_light_table != 1 && (count = vertex_location_count, count != 0) &&
                    IsZeroVector(&g_environment_offset) == 0) {
                    srMath::add({dig, static_cast<std::size_t>(static_cast<SRDWORD>(count))},
                                g_environment_offset, dig);
                }
            }
            if (light_scale != g_float_one && (count = vertex_location_count, count != 0)) {
                if (light_scale == g_float_zero) {
                    std::fill_n(dig, count, srVector3T<float>(0.0f, 0.0f, 0.0f));
                } else {
                    srMath::mul({reinterpret_cast<float*>(dig),
                                 static_cast<std::size_t>( // reinterpret-ok: packed DIG as float*
                                     static_cast<SRDWORD>(count) *
                                     3)}, // reinterpret-ok: packed DIG as float*
                                light_scale, reinterpret_cast<float*>(dig));
                }
            }
        }
        flags &= ~static_cast<unsigned int>(W8_MESH_VERTEX_LIGHTING_DIRTY);
    }
    return srMeshModel::getTriMesh();
}

// FUNCTION: WIZ8 0x004727e0
void stMeshModel::getTriMesh(TriMesh& mesh)
{
    mesh = getTriMesh();
}

/* Thin vtable wrapper: submit with no external poly-equation table. */
// FUNCTION: WIZ8 0x00470360
void stMeshModel::renderTriMesh(srGERD& renderer, const TriMesh& mesh)
{
    RenderTriMeshWithEquations(renderer, mesh, 0);
}

/* Wizardry-extended srMeshModel::renderTriMesh. Optional polygon normals enable
   a software backface cull into g_software_cull_active_polygons and
   forces CULL_NONE; a null table leaves hardware cull at CULL_BACK unless
   g_render_cull_front already requested front culling. */
// FUNCTION: WIZ8 0x00470380
void stMeshModel::RenderTriMeshWithEquations(srGERD& renderer, const TriMesh& mesh,
                                             const srVector3T<float>* arg_poly_equations)
{
    w8_ulong active_count = 0;
    srShader shader;

    if (mesh.polygon_count != 0 && mesh.vertex_count != 0) {
        renderer.pushEnable();
        if ((g_inverted_depth_render ||
             (mesh.control_flags & (1UL << srMeshModel::CONTROL_SORTED_RENDERING)) != 0) &&
            !renderer.isEnabled(srGERD::ENABLE_SORTED_RENDERING)) {
            renderer.toggle(srGERD::ENABLE_SORTED_RENDERING);
        }

        if (g_render_cull_front) {
            renderer.setCullMode(srGERD::CULL_NONE);
        } else if (arg_poly_equations != 0) {
            renderer.setCullMode(srGERD::CULL_NONE);

            g_software_cull_active_polygons.resize(mesh.polygon_count);

            srMatrix4T<float> inverse_model_view;
            renderer.getInverseModelViewMatrix(inverse_model_view);
            srVector3T<float> eye(inverse_model_view.vectors[0].w, inverse_model_view.vectors[1].w,
                                  inverse_model_view.vectors[2].w);

            if (mesh.active_polygons == 0) {
                if (renderer.getWinding() == srGERD::WINDING_POSITIONAL_0) {
                    for (w8_long polygon = 0; polygon < mesh.polygon_count; ++polygon) {
                        int vertex = mesh.poly_vertices[polygon].y;
                        const srVector3T<float>& position = mesh.positions[vertex];
                        const srVector3T<float>& equation = arg_poly_equations[polygon];
                        float facing = (eye.x - position.x) * equation.x +
                                       (eye.y - position.y) * equation.y +
                                       (eye.z - position.z) * equation.z;
                        if (static_cast<float>(g_double_zero) <= facing) {
                            g_software_cull_active_polygons[active_count] =
                                static_cast<w8_ulong>(polygon);
                            ++active_count;
                        }
                    }
                } else {
                    for (w8_long polygon = 0; polygon < mesh.polygon_count; ++polygon) {
                        int vertex = mesh.poly_vertices[polygon].y;
                        const srVector3T<float>& position = mesh.positions[vertex];
                        const srVector3T<float>& equation = arg_poly_equations[polygon];
                        float facing = (eye.x - position.x) * equation.x +
                                       (eye.y - position.y) * equation.y +
                                       (eye.z - position.z) * equation.z;
                        if (facing <= static_cast<float>(g_double_zero)) {
                            g_software_cull_active_polygons[active_count] =
                                static_cast<w8_ulong>(polygon);
                            ++active_count;
                        }
                    }
                }
            } else if (renderer.getWinding() == srGERD::WINDING_POSITIONAL_0) {
                for (w8_long index = 0; index < mesh.active_polygon_count; ++index) {
                    w8_ulong polygon = mesh.active_polygons[index];
                    int vertex = mesh.poly_vertices[polygon].y;
                    const srVector3T<float>& position = mesh.positions[vertex];
                    const srVector3T<float>& equation = arg_poly_equations[polygon];
                    float facing = (eye.x - position.x) * equation.x +
                                   (eye.y - position.y) * equation.y +
                                   (eye.z - position.z) * equation.z;
                    if (static_cast<float>(g_double_zero) <= facing) {
                        g_software_cull_active_polygons[active_count] = polygon;
                        ++active_count;
                    }
                }
            } else {
                for (w8_long index = 0; index < mesh.active_polygon_count; ++index) {
                    w8_ulong polygon = mesh.active_polygons[index];
                    int vertex = mesh.poly_vertices[polygon].y;
                    const srVector3T<float>& position = mesh.positions[vertex];
                    const srVector3T<float>& equation = arg_poly_equations[polygon];
                    float facing = (eye.x - position.x) * equation.x +
                                   (eye.y - position.y) * equation.y +
                                   (eye.z - position.z) * equation.z;
                    if (facing <= static_cast<float>(g_double_zero)) {
                        g_software_cull_active_polygons[active_count] = polygon;
                        ++active_count;
                    }
                }
            }
        } else {
            renderer.setCullMode(srGERD::CULL_BACK);
        }

        for (int side = 1; side >= 0; --side) {
            if ((mesh.control_flags & (1u << side)) != 0) {
                srTriMeshPipeline* pipeline = srTriMeshPipeline::Get(&renderer);
                pipeline->sort_bias = mesh.sort_bias;
                pipeline->triangles = mesh.poly_vertices;
                pipeline->triangle_count = static_cast<w8_ulong>(mesh.polygon_count);
                pipeline->positions = mesh.positions;
                pipeline->vertex_count = static_cast<w8_ulong>(mesh.vertex_count);
                pipeline->vertex_extras = mesh.normals;

                if (arg_poly_equations != 0) {
                    pipeline->projected_vertices = 0;
                } else {
                    pipeline->projected_vertices = mesh.poly_equations;
                }

                if (arg_poly_equations != 0) {
                    pipeline->active_triangles = g_software_cull_active_polygons.data();
                    pipeline->active_triangle_count = active_count;
                } else if (mesh.active_polygons != 0) {
                    pipeline->active_triangles = mesh.active_polygons;
                    pipeline->active_triangle_count = mesh.active_polygon_count;
                }

                if ((mesh.control_flags & (1UL << srMeshModel::CONTROL_SKIP_AUTO_BOX)) == 0) {
                    pipeline->bounds_minimum = mesh.bounds_minimum;
                    pipeline->bounds_maximum = mesh.bounds_maximum;
                    if (pipeline->bounds_source == srTriMeshPipeline::BOUNDS_FROM_VERTICES) {
                        pipeline->bounds_source = srTriMeshPipeline::BOUNDS_BOX;
                    }
                }
                if ((mesh.control_flags & (1UL << srMeshModel::CONTROL_SKIP_AUTO_SPHERE)) == 0) {
                    pipeline->bounds_center = mesh.bounds_center;
                    pipeline->bounds_radius = mesh.bounds_radius;
                    pipeline->bounds_source = srTriMeshPipeline::BOUNDS_SPHERE;
                }

                pipeline->records.resize(mesh.pass_count + 1);
                pipeline->passes.resize(mesh.pass_count + 1);
                pipeline->PrepareSlot();
                for (w8_long pass = 0; pass < mesh.pass_count; ++pass) {
                    pipeline->current_record->flags = 0;
                    pipeline->current_pass->shaders = 0;
                    pipeline->current_pass->texture_tables[0] = 0;
                    pipeline->current_pass->texture_tables[1] = 0;

                    if (mesh.dig[pass] != 0) {
                        pipeline->current_record->colors = mesh.dig[pass];
                        pipeline->current_record->color_format =
                            srVertexPipe::Record::ColorSource::FORMAT_VECTOR3;
                        pipeline->current_record->flags |= srVertexPipe::Record::HAS_COLORS;
                    }
                    if (mesh.dcg[pass] != 0) {
                        pipeline->current_record->dcg = mesh.dcg[pass];
                        pipeline->current_record->flags |=
                            srVertexPipe::Record::HAS_DIFFUSE_MULTIPLIERS;
                    }
                    if (mesh.scg[pass] != 0) {
                        pipeline->current_record->scg = mesh.scg[pass];
                        pipeline->current_record->flags |=
                            srVertexPipe::Record::HAS_SPECULAR_MULTIPLIERS;
                    }

                    if (mesh.vertex_materials[pass][side] == 0) {
                        srMaterialIFace* material = mesh.materials[pass][side];
                        pipeline->material = material;
                        pipeline->current_record->material = material;
                    } else {
                        pipeline->current_record->vertex_materials =
                            mesh.vertex_materials[pass][side];
                        pipeline->current_record->flags |=
                            srVertexPipe::Record::HAS_VERTEX_MATERIALS;
                    }

                    if (mesh.poly_uv[pass] != 0) {
                        pipeline->current_pass->poly_uv = mesh.poly_uv[pass];
                    }

                    if (mesh.poly_shaders[pass] == 0) {
                        shader.value = mesh.shaders[pass].value;
                        if (g_inverted_depth_render) {
                            shader.value = (shader.value & 0xfffffffeUL) | 6UL;
                        }
                        pipeline->SetFlags(shader);
                    } else {
                        pipeline->current_pass->shaders = mesh.poly_shaders[pass];
                    }

                    if (mesh.texcoords[pass][0] != 0) {
                        pipeline->current_record->st0 = mesh.texcoords[pass][0];
                        pipeline->current_record->flags |= srVertexPipe::Record::HAS_TEXCOORD0;
                    }
                    if (mesh.texcoords[pass][1] != 0) {
                        pipeline->current_record->flags |= srVertexPipe::Record::HAS_TEXCOORD1;
                        pipeline->current_record->st1 = mesh.texcoords[pass][1];
                    }

                    for (int layer = 0; layer < 2; ++layer) {
                        if (mesh.poly_textures[pass][layer] == 0) {
                            srTextureIFace* texture = mesh.textures[pass][layer];
                            (&pipeline->texture0)[layer] = texture;
                            pipeline->current_pass->textures[layer] = texture;
                        } else {
                            pipeline->current_pass->texture_tables[layer] =
                                mesh.poly_textures[pass][layer];
                        }
                    }

                    ++pipeline->slot_count;
                    pipeline->PrepareSlot();
                }

                pipeline->FlushIfCurrent();
            }
        }

        renderer.popEnable();
        last_decompress_release_tick = w8_get_ticks();
    }
}

// FUNCTION: WIZ8 0x0046ffa0
int __fastcall IsZeroVector(const srVector3T<float>* vector)
{
    if (vector->x == g_float_zero && vector->y == g_float_zero && vector->z == g_float_zero) {
        return 1;
    }
    return 0;
}

/* Copy or translate `count` vertices: a zero offset is a plain copy and a
   nonzero one goes through the vp constant-vector add. */
// FUNCTION: WIZ8 0x00470040
void OffsetVertices(srVector3T<float>* destination, const srVector3T<float>* source,
                    const srVector3T<float>* offset, int count)
{
    if (count != 0) {
        if (IsZeroVector(offset) != 0) {
            if (destination != source) {
                std::copy_n(source, count, destination);
            }
        } else {
            srMath::add({destination, static_cast<std::size_t>(static_cast<SRDWORD>(count))},
                        *offset, source);
        }
    }
}

static bool MeshNameEquals(std::string_view left, std::string_view right)
{
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [](unsigned char a, unsigned char b) {
               return std::tolower(a) == std::tolower(b);
           });
}

// FUNCTION: WIZ8 0x00473fa0
void stMeshModel::ApplyAutomapPolygonFilter(std::span<const std::string_view> excluded_textures)
{
    std::vector<w8_ulong> active;
    active.reserve(polygon_count);
    auto* textures = getPolyTexture(0, 0, 0);
    if (textures != nullptr) {
        for (unsigned int polygon = 0; polygon < static_cast<unsigned int>(polygon_count); ++polygon) {
            if (textures[polygon] == nullptr ||
                std::none_of(excluded_textures.begin(), excluded_textures.end(), [&](auto name) {
                    return MeshNameEquals(name, textures[polygon]->getName());
                })) {
                active.push_back(polygon);
            }
        }
    }
    if (active.size() == static_cast<unsigned int>(polygon_count)) {
        automap_polygons.reset();
    } else {
        automap_polygons = std::move(active);
    }
    automap_filter_active = true;
}

// FUNCTION: WIZ8 0x00474120
void stMeshModel::ClearAutomapPolygonFilter()
{
    automap_polygons.reset();
    // Clearing the automap override must allow the regular blank-texture scan
    // on the next draw, rather than leaving a cached "all polygons" result.
    automap_filter_active = false;
}

/* The active-polygon table selects which polygons a texture-table draw
   submits. Skin scans drop textures whose name starts with "blank"; a table
   not found among the skins uses the base/automap selection. */
// FUNCTION: WIZ8 0x00473CD0
w8_ulong* stMeshModel::GetActivePolygons(w8_long* count_out, int table, bool flag)
{
    auto skin = std::find_if(skins.begin(), skins.end(),
                            [table](const auto& skin) { return skin.id == table; });
    auto& selection = skin != skins.end() ? skin->active_polygons : automap_polygons;
    auto& checked = skin != skins.end() ? skin->blanking_checked : automap_filter_active;
    if (!checked && flag) {
        std::vector<w8_ulong> active;
        active.reserve(polygon_count);
        auto* textures = skin != skins.end() ? skin->textures.data() : getPolyTexture(0, 0, 0);
        if (textures != nullptr) {
            for (unsigned int polygon = 0; polygon < static_cast<unsigned int>(polygon_count); ++polygon) {
                if (textures[polygon] == nullptr ||
                    !MeshNameEquals(std::string_view(textures[polygon]->getName()).substr(0, 5), "blank")) {
                    active.push_back(polygon);
                }
            }
            if (active.size() != static_cast<unsigned int>(polygon_count)) {
                selection = std::move(active);
            }
        }
        checked = true;
    }
    *count_out = selection ? static_cast<w8_long>(selection->size()) : 0;
    if (!selection) {
        return nullptr; // No selection means the renderer submits every polygon.
    }
    // The TriMesh consumer uses a non-null pointer to recognize filtering.
    // An engaged empty vector therefore needs a non-null, never-dereferenced
    // sentinel so "none selected" cannot accidentally turn into "draw all".
    static w8_ulong empty_selection;
    return selection->empty() ? &empty_selection : selection->data();
}

// FUNCTION: WIZ8 0x00471160
void stMeshModel::SetMappedVertex(short vertex, short key)
{
    mapped_vertices.insert_or_assign(key, vertex);
}

// FUNCTION: WIZ8 0x004712d0
int stMeshModel::FindMappedIndex(short key)
{
    if (key < 0) {
        return -1;
    }
    auto entry = mapped_vertices.find(key);
    return entry != mapped_vertices.end() ? entry->second : -1;
}

/* Link one model onto another, setting both ends - so the two pointers are one
   link rather than two independent  Unlinking passes nothing. */
// FUNCTION: WIZ8 0x00471d60
void stMeshModel::LinkTo(stMeshModel* other)
{
    next = other;
    if (other != 0) {
        other->previous = this;
    }
}

/* One frame's compressed vertex table, refused outright when there is no
   table at all. */
// FUNCTION: WIZ8 0x00471aa0
short* stMeshModel::GetVertex(unsigned int frame)
{
    return frame < frames.size() ? frames[frame].compressed_locations.data() : nullptr;
}

// FUNCTION: WIZ8 0x00473b00
void stMeshModel::InitializeVertexFrames(int count)
{
    if (count <= 0) {
        srAssertFail("uiFrames", "C:\\Projects\\Wizardry 8\\Engine Code\\stMeshModel.cpp", 0x6e8,
                     0);
        return;
    }
    // ReadMesh calls this for each frame; it must not discard earlier frames.
    if (!frames.empty()) {
        return;
    }
    std::vector<Frame> storage(count);
    for (auto& frame : storage) {
        frame.compressed_locations.resize(vertex_location_count * 3);
        frame.compressed_vertex_normals.resize(vertex_location_count * 3);
        frame.compressed_polygon_normals.resize(polygon_count * 3);
    }
    g_mesh_models.push_back(this);
    frames = std::move(storage);
    flags |= W8_MESH_HAS_FRAME_STORAGE;
}

/* Drop every decompressed float cache, returning the bytes actually released.
   Resetting the optional destroys its vector allocation, unlike vector::clear. */
// FUNCTION: WIZ8 0x004739e0
int stMeshModel::ReleaseDecompressedFrames()
{
    int released = 0;
    for (auto& frame : frames) {
        for (auto* cache : {&frame.locations, &frame.vertex_normals, &frame.polygon_normals}) {
            if (*cache) {
                released += (*cache)->capacity() * sizeof(srVector3T<float>);
                cache->reset();
            }
        }
    }
    last_decompress_release_tick = w8_get_ticks();
    g_decompressed_mesh_bytes -= released;
    return released;
}

/* Evict least-recently-used decompressed frames until `needed` bytes are
   available, or give up when every eligible model has been drained. */
// FUNCTION: WIZ8 0x00473BF0
unsigned char ReclaimDecompressedBytes(unsigned int needed, const stMeshModel* retained_model)
{
    unsigned int released = 0;
    while (released < needed) {
        stMeshModel* oldest = nullptr;
        for (stMeshModel* model : g_mesh_models) {
            if (model == retained_model) {
                continue;
            }
            bool resident = std::any_of(model->frames.begin(), model->frames.end(),
                                       [](const auto& frame) {
                                           return (frame.locations && !frame.locations->empty()) ||
                                                  (frame.vertex_normals && !frame.vertex_normals->empty()) ||
                                                  (frame.polygon_normals && !frame.polygon_normals->empty());
                                       });
            if (resident && (oldest == nullptr ||
                             model->last_decompress_release_tick < oldest->last_decompress_release_tick)) {
                oldest = model;
            }
        }
        if (oldest == nullptr) {
            return 0;
        }
        released += oldest->ReleaseDecompressedFrames();
    }
    return 1;
}

// FUNCTION: WIZ8 0x004736d0
int stMeshModel::FindSkinTable(std::string_view name)
{
    for (const auto& skin : skins) {
        if (MeshNameEquals(name, skin.name)) {
            // AddExistingDamageStage stores this as the GetTextureTable ID,
            // not the record's changing position after another skin is removed.
            return skin.id;
        }
    }
    return -1;
}

// FUNCTION: WIZ8 0x00473720
srPtr<srTextureIFace>* stMeshModel::GetTextureTable(int table)
{
    auto skin = std::find_if(skins.begin(), skins.end(),
                            [table](const auto& skin) { return skin.id == table; });
    return skin != skins.end() ? skin->textures.data() : nullptr;
}

/* Clone one polygon-texture table under an owned name. Table ids are the lowest
   free non-negative integer and remain stable independently of vector position. */
// FUNCTION: WIZ8 0x00473260
int stMeshModel::CreateSkinTable(std::string_view name, int base_table)
{
    if (FindSkinTable(name) != -1) {
        return -1;
    }
    auto* source = GetTextureTable(base_table);
    if (source == nullptr) {
        source = getPolyTexture(0, 0, 0);
    }
    if (source == nullptr) {
        return -1;
    }
    int table = 0;
    while (std::any_of(skins.begin(), skins.end(),
                       [table](const auto& skin) { return skin.id == table; })) {
        ++table;
    }
    Skin skin{table, std::string(name), {source, source + polygon_count}, std::nullopt, false};
    skins.push_back(std::move(skin));
    return table;
}

// FUNCTION: WIZ8 0x00473830
void stMeshModel::RemoveSkinTable(int index)
{
    if (static_cast<unsigned int>(index) < skins.size()) {
        skins.erase(skins.begin() + index);
    }
}

/* Skin tables are named with the owning cycle plus a one-character suffix.
   Final teardown removes every table whose name has that cycle prefix. */
// FUNCTION: WIZ8 0x00473780
void stMeshModel::RemoveSkinTablesForCycle(std::string_view cycle_name)
{
    if (!cycle_name.empty()) {
        std::erase_if(skins, [cycle_name](const auto& skin) {
            std::string_view name = skin.name;
            return name.size() > 1 && MeshNameEquals(cycle_name, name.substr(0, name.size() - 1));
        });
    }
}

static void DecompressMeshNormals(const unsigned char* normals, int count,
                                  srVector3T<float>* destination)
{
    // The former lookup table (WIZ8 0x00659ce8, ready flag 0x0065a0ef) only decoded signed bytes.
    const auto expand = [](unsigned char component) {
        return std::clamp(static_cast<signed char>(component) * (1.0f / 127.0f), -1.0f, 1.0f);
    };
    for (int index = 0; index < count; ++index) {
        const unsigned char* source = &normals[index * 3];
        destination[index].Set(expand(source[0]), expand(source[1]), expand(source[2]));
    }
}

/* Expand one frame's compressed table into `destination`: bit 1 the vertex
   locations, bit 2 the vertex normals, bit 4 the polygon normals. */
// FUNCTION: WIZ8 0x00471930
unsigned char stMeshModel::DecompressFrame(int frame, unsigned char flags,
                                           srVector3T<float>* destination)
{
    if (static_cast<unsigned int>(frame) >= frames.size()) {
        return 0;
    }
    const auto& storage = frames[frame];
    if (flags & W8_MESH_FRAME_LOCATIONS) {
        for (int index = 0; index < vertex_location_count; ++index) {
            const short* source = &storage.compressed_locations[index * 3];
            destination[index].Set(source[0] * vertex_compression_scale,
                                   source[1] * vertex_compression_scale,
                                   source[2] * vertex_compression_scale);
        }
        return 1;
    }
    if (flags & W8_MESH_FRAME_VERTEX_NORMALS) {
        DecompressMeshNormals(storage.compressed_vertex_normals.data(), vertex_location_count,
                              destination);
        return 1;
    }
    if (flags & W8_MESH_FRAME_POLYGON_NORMALS) {
        DecompressMeshNormals(storage.compressed_polygon_normals.data(), polygon_count,
                              destination);
        return 1;
    }
    return 0;
}

/* Allocate one frame's decompressed caches for the tables named by `flags`
   (bit 0 locations, bit 1 vertex normals, bit 2 polygon normals), reclaiming
   least-recently-used frames when the byte budget would overflow. */
// FUNCTION: WIZ8 0x00471720
unsigned char stMeshModel::AllocateFrameBuffers(unsigned int uiFrame, unsigned char flags)
{
    if (uiFrame >= frames.size()) {
        return 0;
    }
    auto& frame = frames[uiFrame];
    auto allocate = [&](auto& cache, int count, unsigned char bit) {
        if ((flags & bit) == 0 || cache) {
            return;
        }
        int needed = count * sizeof(srVector3T<float>);
        if (g_decompressed_mesh_byte_limit <= g_decompressed_mesh_bytes + needed) {
            // Never evict this model between returning positions and normals,
            // or between loading the two frames of an interpolation. The budget
            // is soft only when that working set alone cannot fit; drain other
            // models first even if they cannot supply the entire request.
            ReclaimDecompressedBytes(needed, this);
        }
        cache.emplace(count, srVector3T<float>(0.0f, 0.0f, 0.0f));
        g_decompressed_mesh_bytes += cache->capacity() * sizeof(srVector3T<float>);
    };
    allocate(frame.locations, vertex_location_count, W8_MESH_FRAME_LOCATIONS);
    allocate(frame.vertex_normals, vertex_location_count, W8_MESH_FRAME_VERTEX_NORMALS);
    allocate(frame.polygon_normals, polygon_count, W8_MESH_FRAME_POLYGON_NORMALS);
    last_decompress_release_tick = w8_get_ticks();
    return 1;
}

/* Return frame `frame`'s vertex locations, decompressing on demand. When
   `interpolation` is positive and another frame follows, both frames are
   decompressed and lerped into lerp_buffer (m_pLerpBuffer). */
// FUNCTION: WIZ8 0x00471AD0
srVector3T<float>* stMeshModel::GetVertexLocations(unsigned int frame, bool load,
                                                   float interpolation)
{
    if (frame >= frames.size()) {
        return nullptr;
    }
    if (g_float_zero < interpolation && frame + 1 < frames.size()) {
        const auto* current = GetVertexLocations(frame, true, 0.0f);
        const auto* next = GetVertexLocations(frame + 1, true, 0.0f);
        lerp_buffer.resize(vertex_location_count);
        if (vertex_location_count != 0) {
            if (interpolation == g_float_one) {
                std::copy_n(next, vertex_location_count, lerp_buffer.data());
            } else {
                srMath::lerp({&lerp_buffer[0].x, static_cast<std::size_t>(vertex_location_count * 3)},
                             &next->x, &current->x, interpolation);
            }
        }
        return lerp_buffer.data();
    }
    auto& cache = frames[frame].locations;
    if (!cache) {
        AllocateFrameBuffers(frame, W8_MESH_FRAME_LOCATIONS);
        if (load) {
            DecompressFrame(frame, W8_MESH_FRAME_LOCATIONS, cache->data());
        }
    }
    return cache->data();
}

/* Return frame `frame`'s vertex normals, decompressing on demand when `load`
   is set. */
// FUNCTION: WIZ8 0x00471CA0
srVector3T<float>* stMeshModel::GetVertexNormals(unsigned int frame, bool load)
{
    if (frame >= frames.size()) {
        return nullptr;
    }
    auto& cache = frames[frame].vertex_normals;
    if (!cache) {
        AllocateFrameBuffers(frame, W8_MESH_FRAME_VERTEX_NORMALS);
        if (load) {
            DecompressFrame(frame, W8_MESH_FRAME_VERTEX_NORMALS, cache->data());
        }
    }
    return cache->data();
}

// FUNCTION: WIZ8 0x00471D00
srVector3T<float>* stMeshModel::GetPolygonNormals(unsigned int frame, bool load)
{
    if (frame >= frames.size()) {
        return nullptr;
    }
    auto& cache = frames[frame].polygon_normals;
    if (!cache) {
        AllocateFrameBuffers(frame, W8_MESH_FRAME_POLYGON_NORMALS);
        if (load) {
            DecompressFrame(frame, W8_MESH_FRAME_POLYGON_NORMALS, cache->data());
        }
    }
    return cache->data();
}

void stMeshModel::ClearVertexLights()
{
    srVector3T<float>* lights = GetVertexLights(true, 1);
    int count = vertex_location_count;
    if (count != 0) {
        std::fill_n(lights, count, srVector3T<float>(0.0f, 0.0f, 0.0f));
    }
    flags |= W8_MESH_VERTEX_LIGHTING_DIRTY;
}

// FUNCTION: WIZ8 0x00472990
void stMeshModel::SetAmbientColor(const srVector3T<float>& color)
{
    if (!(ambient_color == color)) {
        ambient_color = color;
        flags |= W8_MESH_VERTEX_LIGHTING_DIRTY;
    }
}

static inline char CompressNormalByte(float value)
{
    // A zero-length normal becomes NaN. MSVC's x87 integer-indefinite result
    // has a zero low byte; express that result without an undefined FP cast.
    if (!_finite(value)) {
        return 0;
    }
    return static_cast<char>(value);
}

/* Build one frame's compressed polygon and vertex normals from its vertex
   locations. Vertex normals are summed per polygon corner, remapped through
   the shade index table when there is one, unitized, and stored as signed
   bytes scaled by 127. */
// FUNCTION: WIZ8 0x004729F0
void stMeshModel::ComputeFrameNormals(int frame)
{
    if (static_cast<unsigned int>(frame) >= frames.size() || polygon_count == 0 ||
        vertex_location_count == 0) {
        return;
    }

    srVector3i* poly_vertex = getPolyVertex();
    if (poly_vertex == nullptr) {
        return;
    }
    std::vector<srVector3T<float>> pnorm(polygon_count);
    auto& storage = frames[frame];
    std::vector<srVector3T<float>> scratch;
    const srVector3T<float>* l;
    if (storage.locations) {
        l = storage.locations->data();
    } else {
        scratch.resize(vertex_location_count);
        DecompressFrame(frame, W8_MESH_FRAME_LOCATIONS, scratch.data());
        l = scratch.data();
    }

    for (int poly = 0; poly < polygon_count; ++poly) {
        const srVector3T<float>& origin = l[poly_vertex[poly].x];
        srVector3T<float> edge_0 = l[poly_vertex[poly].y] - origin;
        srVector3T<float> edge_1 = l[poly_vertex[poly].z] - origin;
        pnorm[poly] = CrossProduct(edge_0, edge_1);
    }

    std::vector<srVector3T<float>> vnorm(vertex_location_count,
                                       srVector3T<float>(0.0f, 0.0f, 0.0f));
    w8_ulong* shade_index = getVertexShadeIndex(0);
    if (shade_index == 0) {
        for (int corner_poly = 0; corner_poly < polygon_count; ++corner_poly) {
            vnorm[poly_vertex[corner_poly].x] += pnorm[corner_poly];
            vnorm[poly_vertex[corner_poly].y] += pnorm[corner_poly];
            vnorm[poly_vertex[corner_poly].z] += pnorm[corner_poly];
        }
    } else {
        std::vector<srVector3T<float>> shaded(vertex_location_count,
                                            srVector3T<float>(0.0f, 0.0f, 0.0f));
        for (int corner_poly = 0; corner_poly < polygon_count; ++corner_poly) {
            shaded[shade_index[poly_vertex[corner_poly].x]] += pnorm[corner_poly];
            shaded[shade_index[poly_vertex[corner_poly].y]] += pnorm[corner_poly];
            shaded[shade_index[poly_vertex[corner_poly].z]] += pnorm[corner_poly];
        }
        srMath::copyIndexed(vnorm, shaded.data(),
                            {shade_index, static_cast<std::size_t>(vertex_location_count)});
    }

    srMath::normalize(vnorm, vnorm.data(), 1.0f);
    for (int vertex = 0; vertex < vertex_location_count; ++vertex) {
        if (IsZeroVector(&vnorm[vertex]) != 0) {
            vnorm[vertex] = 1e-6f;
        }
    }
    srMath::mul({&vnorm[0].x, static_cast<std::size_t>(vertex_location_count * 3)},
                127.0f, &vnorm[0].x);
    for (int index = 0; index < vertex_location_count; ++index) {
        storage.compressed_vertex_normals[index * 3] = CompressNormalByte(vnorm[index].x);
        storage.compressed_vertex_normals[index * 3 + 1] = CompressNormalByte(vnorm[index].y);
        storage.compressed_vertex_normals[index * 3 + 2] = CompressNormalByte(vnorm[index].z);
    }

    srMath::normalize(pnorm, pnorm.data(), 1.0f);
    srMath::mul({&pnorm[0].x, static_cast<std::size_t>(polygon_count * 3)},
                127.0f, &pnorm[0].x);
    for (int polygon = 0; polygon < polygon_count; ++polygon) {
        storage.compressed_polygon_normals[polygon * 3] = CompressNormalByte(pnorm[polygon].x);
        storage.compressed_polygon_normals[polygon * 3 + 1] = CompressNormalByte(pnorm[polygon].y);
        storage.compressed_polygon_normals[polygon * 3 + 2] = CompressNormalByte(pnorm[polygon].z);
    }

    if (storage.vertex_normals) {
        DecompressFrame(frame, W8_MESH_FRAME_VERTEX_NORMALS, storage.vertex_normals->data());
    }
    if (storage.polygon_normals) {
        DecompressFrame(frame, W8_MESH_FRAME_POLYGON_NORMALS, storage.polygon_normals->data());
    }
}

/* Thirteen-byte forwarder onto the per-frame normal builder. */
// FUNCTION: WIZ8 0x00472100
srVector3T<float>* stMeshModel::GetVertexLights(bool initialize, int table)
{
    if (table == -1) {
        table = vertex_light_table;
    }
    auto& lights = vertex_lights[table];
    if (lights.empty() && initialize) {
        lights.resize(vertex_location_count, srVector3T<float>(0.0f, 0.0f, 0.0f));
        if (!vertex_sunlight.empty()) {
            vertex_lighting_ready = true;
        }
    }
    return lights.empty() ? nullptr : lights.data();
}

// FUNCTION: WIZ8 0x004721E0
float* stMeshModel::GetVertexSunlight(bool initialize)
{
    if (vertex_sunlight.empty() && initialize) {
        vertex_sunlight.resize(vertex_location_count, 1.0f);
        vertex_lighting_ready = true;
    }
    return vertex_sunlight.empty() ? nullptr : vertex_sunlight.data();
}

// FUNCTION: WIZ8 0x00473180
void stMeshModel::FinalizeVertexFrame(int frame)
{
    ComputeFrameNormals(frame);
}

/* Retail ICF folds this empty thiscall onto W8OptionsGraphicsPanel::OnDragEnd
   at 0x005AA400 (OptionsScreen.cpp). This source function has no separately
   retained retail address, so it intentionally has no FUNCTION marker. */
void stMeshModel::NotifyLinkedModel(stMeshModel*) {}
