#include "wiz8/engine_code/materials.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>
#include <fmt/printf.h>
#include <wiz8/filesystem.h>

#include "Font.h"
#include "OctBuildTree.h"
#include "OctMeshModel.h"
#include "compat/compiler.h"
#include "geometry.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/geometric.hpp" // NOLINT(misc-include-cleaner)
#include "himage.h"
#include "input.h"
#include "surrender/lighting_math.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srCore.h"
#include "surrender/srGERD.h"
#include "surrender/srMaterial.h"
#include "surrender/srMath.h"
#include "surrender/srMeshModel.h"
#include "surrender/srModelInstance.h"
#include "surrender/srStatisticsManager.h"
#include "surrender/srTexture.h"
#include "surrender/srTextureIFace.h"
#include "surrender/srVertexPipe.h"
#include "wiz8/application.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GDFileIO.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/LevelFile.h"
#include "wiz8/engine_code/OctBuildPreTree.h"
#include "wiz8/engine_code/OctPreTree.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/geometry_repack.h"
#include "wiz8/engine_code/stTextureAnim.h"
#include "wiz8/engine_code/stTextureFile.h"
#include "wiz8/float_constants.h"
#include "wiz8/fonts.h"
#include "wiz8/index_algorithms.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/PleaseWaitScreen.h"
#include "wiz8/packed_bits.h"
#include "wiz8/resource_cache.h"
#include "wiz8/unicode.h"
#include "wiz8/virtual_file.h"

#define MATERIALS_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\materials.cpp"

/* The global at 0x0065BEA8 is a real zero-storage srVertexProcessor subclass:
   its non-template process body establishes the boundary independently of its
   constructor and vtable, and that body maps eye-space normals into the first
   texture-coordinate set. No source or export name survives, so the class
   keeps a descriptive name anchored at its constructor address. */
// VTABLE: WIZ8 0x005ED0D0
// class W8NormalTexcoordMapper
class W8NormalTexcoordMapper : public srVertexProcessor {
public:
    // FUNCTION: WIZ8 0x004D6190
    virtual int isActive(srVertexPipe&) override { return 1; }
    virtual void process(srVertexPipe& pipe) override;
};

// GLOBAL: WIZ8 0x0065BEA8
static W8NormalTexcoordMapper g_normal_texcoord_mapper;

// GLOBAL: WIZ8 0x0065BA9E
bool g_material_diffuse_scale_enabled;
// GLOBAL: WIZ8 0x0065BAA0
float g_material_diffuse_scale;
// GLOBAL: WIZ8 0x0065BAA4
bool g_material_emissive_override_enabled;
// GLOBAL: WIZ8 0x0065BAA8
float g_material_emissive_override;

/* Convert eye-space normals to the material's first texture-coordinate set.
   The exported srVertexPipe queries preserve the closed renderer's ownership
   of its internal workspace while expressing every operation in this body. */
// FUNCTION: WIZ8 0x004B89B0
void W8NormalTexcoordMapper::process(srVertexPipe& pipe)
{
    glm::vec2* coordinates;
    w8_ulong count;
    w8_ulong index;

    if (!pipe.isChannelAvailable(srVertexProcessor::CHANNEL_ST0)) {
        return;
    }
    const auto normals = pipe.getEyeSpaceNormal();
    coordinates = pipe.getST(0, 1);
    count = pipe.getVertexCount();
    srCore.getStatisticsManager()->statistics.texture_coordinate_operations += count;
    for (index = 0; index < count; ++index) {
        coordinates[index] = glm::vec2((normals[index].x + g_float_one) * g_float_half,
                                       (normals[index].y + g_float_one) * g_float_half);
    }
}

// FUNCTION: WIZ8 0x004925B0
stMaterial::stMaterial()
{
    m_surface_flags = 0;
}

// FUNCTION: WIZ8 0x004928F0
srVertexProcessor::MaterialInfo stMaterial::getMaterialInfo()
{
    auto info = srMaterial::getMaterialInfo();
    if (g_material_diffuse_scale_enabled) {
        info.diffuse.w *= g_material_diffuse_scale;
    }
    if (g_material_emissive_override_enabled) {
        info.emissive = glm::vec4(g_material_emissive_override, g_material_emissive_override,
                                  g_material_emissive_override, 1.0f);
    }
    return info;
}

/* ===== OctBuild level preprocessing =====
   The retail level preprocessor lives in this TU between the stMaterial
   cluster and LoadMaterial. Its option globals are plain .data and its
   build state is TU-private .bss. */

// GLOBAL: WIZ8 0x0060AC70
static bool g_option_pathing = true;
// GLOBAL: WIZ8 0x0060AC71
static bool g_option_shadow_test = true;
// GLOBAL: WIZ8 0x0060AC72
static bool g_option_logging = true;
// GLOBAL: WIZ8 0x0060AC73
static bool g_option_mesh_linking = true;
// GLOBAL: WIZ8 0x0060AC74
static float g_option_path_node_spacing = 500.0f;
// GLOBAL: WIZ8 0x0060AC78
static float g_option_path_head_room = 1000.0f;
// GLOBAL: WIZ8 0x0060AC7C
static int g_option_delete_percentage = 10;
// GLOBAL: WIZ8 0x0060AC80
static float g_option_min_leaf_size = 1000.0f;
// GLOBAL: WIZ8 0x0060AC84
static int g_option_max_path_nodes = 64;
// GLOBAL: WIZ8 0x0060AC88
static int g_option_max_leaf_count = 20000;
// GLOBAL: WIZ8 0x0060AC8C
static bool g_status_scroll = true;
// GLOBAL: WIZ8 0x0060AC8D
static bool g_status_buffers_freed = true;

// GLOBAL: WIZ8 0x0065BAB0
static glm::vec3 g_weld_min;
// GLOBAL: WIZ8 0x0065BACC
static glm::vec3 g_weld_max;
// GLOBAL: WIZ8 0x0065BADC
static std::string g_log_path;
// GLOBAL: WIZ8 0x0065BCDC
static unsigned int g_weld_stride_x;
// GLOBAL: WIZ8 0x0065BCE0
static unsigned int g_weld_stride_y;
// GLOBAL: WIZ8 0x0065BCE4
static unsigned int g_weld_stride_z;
// GLOBAL: WIZ8 0x0065BCE8
static std::string g_status_lines[6];
// GLOBAL: WIZ8 0x0065BD0C
static int g_oct_node_count;
// GLOBAL: WIZ8 0x0065BD10
static int g_oct_max_objects;
// GLOBAL: WIZ8 0x0065BD14
static int g_lights_unblocked;
// GLOBAL: WIZ8 0x0065BD18
static int g_light_candidates;
// GLOBAL: WIZ8 0x0065BD1C
static int g_lights_facing;
// GLOBAL: WIZ8 0x0065BD2D
static bool g_option_rename_alphas;
// GLOBAL: WIZ8 0x0065BD30
static float g_option_auto_region_size;
// GLOBAL: WIZ8 0x0065BD34
W8OctPreTreeVertex* g_gd_vertices;
// GLOBAL: WIZ8 0x0065BD38
W8OctRegionPolygon* g_gd_polygons;
// GLOBAL: WIZ8 0x0065BD3C
/* Material slots whose texture file is missing. */
static std::vector<bool> g_missing_textures;
// GLOBAL: WIZ8 0x0065BD44
static short g_status_cursor;
// GLOBAL: WIZ8 0x0065BD48
static int g_progress_total;
// GLOBAL: WIZ8 0x0065BD4C
static unsigned int g_progress_done;
// GLOBAL: WIZ8 0x0065BD50
static unsigned int g_progress_mark;
// GLOBAL: WIZ8 0x0065BD54
static std::unique_ptr<wiz8::File> g_log_file;

// GLOBAL: WIZ8 0x005ECBB0
const float g_weld_grid_key_scale = 268435456.0f;
// GLOBAL: WIZ8 0x005ECBB8
const float g_weld_position_tolerance = 2.5f;
// GLOBAL: WIZ8 0x005ECBBC
const float g_material_opposed_normal_threshold = -0.995f;

static unsigned char PreprocessLevel(wiz8::File& handle, char* stem);
/* The following helpers retain distinct retail call boundaries. Their source
   names, internal linkage and placement here are provisional; lack of /Ob2
   PDB procedures does not establish that they were authored inline. */
static int WeldVertex(std::unordered_multimap<unsigned int, int>* table,
                      W8OctPreTreeVertex* vertices, unsigned int index, unsigned int link);
static int AccumulateVertexLight(OctPreTree* tree, W8OctPreTreeVertex* vertex,
                                 std::span<const W8LevelFileLight> lights, int* sun_map);
static unsigned char PropReceivesLight(OctPreTree* tree, const W8LevelFileProp& prop,
                                       const W8LevelFileLight& light);
static int BuildRegionPolygons(W8LevelFile& level, W8OctPreTreeGeometry* geometry,
                               unsigned char* classify);
static unsigned char* ClassifyTextures(std::span<W8MaterialRecord> textures, char* stem);
static unsigned char MaterialSort(W8OctPreTreeGeometry* geometry,
                                  std::span<W8MaterialRecord> textures, unsigned char* classify);

// FUNCTION: WIZ8 0x00492E60
char W8Octree::BuildPreprocessedFiles(const char* level_path)
{
    char level_name[1024];
    char stem[1024];
    char line[1024];
    char* extension;
    unsigned char result;
    std::unique_ptr<wiz8::File> handle;
    int move_pvl;
    int move_oct;

    strcpy(level_name, level_path);
    snprintf(stem, sizeof(stem), "OctBuild Version %d\n\n", 0x22);
    ReportStartupMessage(stem);
    extension = strrchr(level_name, '.');
    if (extension != 0) {
        *extension = '\0';
    }
    OctBuildOptions(level_name);
    strcpy(stem, level_name);
    strcat(level_name, ".lvl");
    snprintf(line, sizeof(line), "Processing level %s.\n", level_name);
    ReportBuildStatus(6, line);
    snprintf(line, sizeof(line), "%s.log", stem);
    ReportBuildStatus(0, line);
    ReportBuildStatus(6, "Now chewing level.\n\n");
    handle = [&]() {
        try {
            return wiz8::open_file(level_name, wiz8::OpenMode::read);
        } catch (const std::exception&) {
            return std::unique_ptr<wiz8::File>{};
        }
    }();
    result = 0;
    if (handle == 0) {
        ReportBuildStatus(7, "Could not find and\\or open level file.\n");
    } else {
        unsigned char built = PreprocessLevel(*handle, stem);
        if (handle) handle->close();
        handle.reset();
        if (built != 0) {
            snprintf(line, sizeof(line), "%s.pvl", stem);
            if ([&]() {
                    const auto status = wiz8::file_status(line);
                    return status && status->info.type == SDL_PATHTYPE_FILE;
                }() != 0) {
                wiz8::clear_read_only(line);
                wiz8::remove_file(line);
            }
            move_pvl = (wiz8::copy_file("NewLevel.lvl", line, wiz8::CopyMode::replace), true);
            if (move_pvl) wiz8::remove_file("NewLevel.lvl");
            snprintf(line, sizeof(line), "%s.rlk", stem);
            if ([&]() {
                    const auto status = wiz8::file_status(line);
                    return status && status->info.type == SDL_PATHTYPE_FILE;
                }() != 0) {
                ReportStartupMessage(
                    "LINK FILE (.RLK) FOUND! IF THE GEOMETRY HAS CHANGED, THE LINK FILE MAY BE "
                    "OBSOLETE!!!\n");
            }
            snprintf(line, sizeof(line), "%s.oct", stem);
            if ([&]() {
                    const auto status = wiz8::file_status(line);
                    return status && status->info.type == SDL_PATHTYPE_FILE;
                }() != 0) {
                wiz8::clear_read_only(line);
                wiz8::remove_file(line);
            }
            move_oct = (wiz8::copy_file("NewLevel.oct", line, wiz8::CopyMode::replace), true);
            if (move_oct) wiz8::remove_file("NewLevel.oct");
            result =
                built & static_cast<unsigned char>(move_pvl) & static_cast<unsigned char>(move_oct);
            if (result == 0) {
                ReportBuildStatus(6,
                                  "File copying failed!  OCT or PVL files may be write protected.");
            } else {
                ReportStartupMessage("PREPROCESSING SUCCESSFUL -- NOW LOADING LEVEL.");
            }
        }
    }
    ReportBuildStatus(8, {});
    ReportStartupMessage({});
    return result;
}

/* The level-preprocessing driver: reads the .lvl, welds and re-scales the
   mesh vertices, reads the .wgd game data, builds the OctBuildPreTree,
   computes vertex lighting and sun visibility, sorts materials, splits
   vertices, writes NewLevel.lvl/.oct and releases everything it made. */
// FUNCTION: WIZ8 0x00493120
static unsigned char PreprocessLevel(wiz8::File& handle, char* stem)
{
    char message[1024];
    char name[20];
    glm::vec3 minimum;
    glm::vec3 maximum;
    W8OctPreTreeGeometry geometry;
    std::vector<W8LevelFileLight> lights;
    int* sun_map;
    float* sun_pool;
    unsigned char* classify;
    W8OctPreTreeVertex* vertices;
    std::unique_ptr<W8LevelFile> level;
    W8GameData* value;
    OctBuildPreTree* build_tree;
    OctPreTree* tree;
    std::vector<OctMeshModel> submeshes;
    unsigned char result;
    bool report;
    int redundant;
    int lit_vertices;
    int last_sun;
    int sun_count;
    int percent;
    int mark;
    int i;
    int j;
    int lit;
    std::unique_ptr<wiz8::File> file;
    W8LevelFileMesh* mesh;

    result = 0;
    sun_map = 0;
    {
        std::unordered_multimap<unsigned int, int> weld;
        sun_pool = 0;
        level = ReadLevelFile(handle);
        if (level == 0) {
            ReportBuildStatus(7, "Could not process LVL file.\n");
            return 0;
        }
        memset(&geometry, 0, sizeof(geometry));
        mesh = &level->mesh;
        if (mesh->num_vertices < 1 || mesh->num_faces < 1) {
            return 0;
        }
        ++mesh->num_vertices;
        classify = ClassifyTextures(level->textures, stem);
        vertices =
            static_cast<W8OctPreTreeVertex*>(malloc(mesh->num_vertices * 2 * sizeof(*vertices)));
        if (vertices != 0) {
            memset(vertices, 0, mesh->num_vertices * 2 * sizeof(*vertices));
            minimum = glm::vec3(1e7f);
            maximum = glm::vec3(-1e7f);
            redundant = 0;
            for (i = 1; i < mesh->num_vertices; ++i) {
                const glm::vec3& source = mesh->vertices[i - 1];
                vertices[i].position = source * g_world_scale;
                vertices[i].m_original_position = source;
                vertices[i].m_visited = false;
                for (j = 0; j < 3; ++j) {
                    float v = (&vertices[i].position.x)[j];
                    if (v <= (&maximum.x)[j]) {
                        if (v < (&minimum.x)[j]) {
                            (&minimum.x)[j] = v;
                            (&g_weld_min.x)[j] = v;
                        }
                    } else {
                        (&maximum.x)[j] = v;
                        (&g_weld_max.x)[j] = v;
                    }
                }
            }
            geometry.m_vertices = vertices;
            g_weld_stride_z = static_cast<unsigned int>(
                (g_weld_grid_key_scale / ((g_weld_max.x - g_weld_min.x) * g_float_two_hundredths)));
            g_weld_stride_y = static_cast<unsigned int>(sqrt(static_cast<double>(g_weld_stride_z)));
            g_weld_stride_x = static_cast<unsigned int>(sqrt(static_cast<double>(g_weld_stride_y)));
            ReportStartupMessage("Welding vertices and discarding redundant vertices.\n");
            mark = 0;
            report = true;
            if (1 < mesh->num_vertices) {
                i = 1;
                do {
                    percent =
                        static_cast<unsigned int>((i * g_octree_cell_scale / mesh->num_vertices));
                    if (mark + 10 < percent) {
                        report = true;
                        mark += 10;
                    }
                    lit = WeldVertex(&weld, vertices, i, 0xffffffff);
                    ++i;
                    if (lit != i) {
                        ++redundant;
                    }
                    if (report) {
                        snprintf(message, sizeof(message),
                                 "  %d%% Complete:  %d Redundant Vertices  \r", mark, redundant);
                        ReportStartupMessage(message);
                    }
                    report = false;
                    vertices[i - 1].m_visited = false;
                } while (i < mesh->num_vertices);
            }
            snprintf(message, sizeof(message),
                     "\n\nNumber of Verticies: %d   Number of polygons: %d\n", mesh->num_vertices,
                     mesh->num_faces);
            ReportBuildStatus(6, message);
            snprintf(message, sizeof(message), "Number of Redundant Verticies: %d\n", redundant);
            ReportBuildStatus(6, message);
            ReportBuildStatus(6, "\nReading GameData...\n");
            snprintf(message, sizeof(message), "%s.wgd", stem);
            value = ReadGameData(message, true);
            if (value == 0) {
                ReportBuildStatus(7, "\n Error -- Cannot load or find game data.\n");
            } else {
                for (i = 0; i < 3; ++i) {
                    if ((&value->minimum.x)[i] < (&minimum.x)[i]) {
                        (&minimum.x)[i] = (&value->minimum.x)[i];
                    }
                    if ((&maximum.x)[i] < (&value->maximum.x)[i]) {
                        (&maximum.x)[i] = (&value->maximum.x)[i];
                    }
                }
                ReportBuildStatus(6, "\nBuilding OctBuildPreTree ---------------------\n");
                build_tree =
                    new OctBuildPreTree(g_option_min_leaf_size, srBoundingBox{minimum, maximum},
                                        g_option_max_path_nodes, g_option_max_leaf_count, 0);
                if (build_tree != 0) {
                    build_tree->LoadRegionFile(stem);
                    if (!g_option_mesh_linking) {
                        build_tree->mesh_linking = false;
                    }
                    build_tree->spatial.m_region_grid_cell = g_option_auto_region_size;
                    int alpha_polys = BuildRegionPolygons(*level, &geometry, classify);
                    build_tree->SortGeometry(&geometry);
                    short light_total = static_cast<short>(level->lights.size());
                    last_sun = -1;
                    if (light_total != 0) {
                        // The bake works on world-scaled copies of the lights.
                        lights = level->lights;
                        sun_map = static_cast<int*>(malloc(light_total * sizeof(int)));
                        memset(sun_map, 0, light_total * sizeof(int));
                        sun_count = 1;
                        for (i = 0; i < static_cast<int>(light_total); ++i) {
                            auto& light = lights[i];
                            light.position *= g_world_scale;
                            light.colour.x = light.colour.x * g_world_scale;
                            strcpy(name, light.name);
                            name[19] = 0;
                            TrimAndLowercaseString(name);
                            if (strstr(name, "sun") != 0 || strstr(name, "moon") != 0 ||
                                strstr(name, "lightning") != 0) {
                                sun_map[i] = sun_count;
                                ++sun_count;
                                last_sun = i;
                            }
                        }
                        --sun_count;
                        if (sun_count != 0) {
                            sun_pool = static_cast<float*>(
                                malloc(geometry.vertex_count * sun_count * sizeof(*sun_pool)));
                            if (sun_pool == 0) {
                                ReportBuildStatus(7, "Could not allocate pflSunLights!\n");
                            }
                            memset(sun_pool, 0,
                                   geometry.vertex_count * sun_count * sizeof(*sun_pool));
                            float* run = sun_pool;
                            for (i = 0; i < static_cast<int>(geometry.vertex_count); ++i) {
                                vertices[i].m_sun_lights = run;
                                run += sun_count;
                            }
                        }
                    } else {
                        sun_count = last_sun;
                    }
                    ReportBuildStatus(6, "\nOctree Statistics:\n==========================\n");
                    snprintf(message, sizeof(message), "Width of Level: %f\t\tTree Depth: %d\n",
                             build_tree->spatial.m_extent, build_tree->spatial.m_depth);
                    ReportBuildStatus(6, message);
                    snprintf(message, sizeof(message), "Width of Leaves: %f,  %f metres\n",
                             build_tree->spatial.m_node_extent,
                             (build_tree->spatial.m_node_extent * g_float_one_five_hundredth));
                    ReportBuildStatus(6, message);
                    snprintf(message, sizeof(message),
                             "Width of auto-generated regions: %f metres\n",
                             (build_tree->spatial.m_region_grid_cell * g_float_one_five_hundredth));
                    g_oct_node_count = GetBuildNodeInstanceCount();
                    g_oct_max_objects = build_tree->deepest_link_list;
                    ReportBuildStatus(3, message);
                    snprintf(message, sizeof(message), "World Minimum Corner: \t%f  \t%f  \t%f\n",
                             build_tree->spatial.m_bounds.minimum.x,
                             build_tree->spatial.m_bounds.minimum.y,
                             build_tree->spatial.m_bounds.minimum.z);
                    ReportBuildStatus(6, message);
                    snprintf(message, sizeof(message), "World Maximum Corner: \t%f  \t%f  \t%f\n",
                             build_tree->spatial.m_bounds.maximum.x,
                             build_tree->spatial.m_bounds.maximum.y,
                             build_tree->spatial.m_bounds.maximum.z);
                    ReportBuildStatus(6, message);
                    snprintf(message, sizeof(message),
                             "World Dimensions:\n\tX: %fm  \tY: %fm  \tZ: %fm\n",
                             ((build_tree->spatial.m_clipped_bounds.maximum.x -
                               build_tree->spatial.m_clipped_bounds.minimum.x) *
                              g_float_one_five_hundredth),
                             ((build_tree->spatial.m_clipped_bounds.maximum.y -
                               build_tree->spatial.m_clipped_bounds.minimum.y) *
                              g_float_one_five_hundredth),
                             ((build_tree->spatial.m_clipped_bounds.maximum.z -
                               build_tree->spatial.m_clipped_bounds.minimum.z) *
                              g_float_one_five_hundredth));
                    ReportBuildStatus(6, message);
                    const auto trigger_parts = CollectTriggerParts(*level);
                    for (auto* trigger : trigger_parts.switches) {
                        // The switch's surface name is replaced by its surface slot.
                        int slot = value->FindPointerByName(trigger->surface_id + 1);
                        trigger->surface_id[0] = '\0';
                        sprintf(trigger->surface_id + 1, "%d", slot);
                    }
                    for (const auto* plane : trigger_parts.planes) {
                        value->AddLevelPlane(*plane);
                    }
                    for (auto* record : trigger_parts.linked_records) {
                        value->AddLinkedRecord(record->vertices, record->normal_scale,
                                               record->forward_scale, &record->linked_face);
                    }
                    value->geometry_index.reset(build_tree);
                    value->CompileGameData();
                    for (i = 0; i < value->SurfacesCount(); ++i) {
                        W8OctRegionPolygon* surface = g_gd_polygons + i;
                        if (build_tree->InsertSurface(surface, 3) == 0) {
                            snprintf(message, sizeof(message),
                                     "Warning: GD Polygon %d cannot be inserted into tree\n", i);
                            ReportBuildStatus(6, message);
                            snprintf(message, sizeof(message), "\tVertex 1: \t%f  \t%f  \t%f\n",
                                     surface->vertices[0]->position.x,
                                     surface->vertices[0]->position.y,
                                     surface->vertices[0]->position.z);
                            ReportBuildStatus(6, message);
                            snprintf(message, sizeof(message), "\tVertex 2: \t%f  \t%f  \t%f\n",
                                     surface->vertices[1]->position.x,
                                     surface->vertices[1]->position.y,
                                     surface->vertices[1]->position.z);
                            ReportBuildStatus(6, message);
                            snprintf(message, sizeof(message), "\tVertex 3: \t%f  \t%f  \t%f\n\n",
                                     surface->vertices[2]->position.x,
                                     surface->vertices[2]->position.y,
                                     surface->vertices[2]->position.z);
                            ReportBuildStatus(6, message);
                        }
                    }
                    build_tree->RemapNodeRegions(0, 0);
                    ReportBuildStatus(6, "\nInserting props and particles into regions... \n");
                    build_tree->BuildParticleRegions(level->particle_systems);
                    build_tree->BuildGeometryRegions(level->props, 0, false);
                    build_tree->BuildGeometryRegions(level->bitmaps,
                                                     static_cast<int>(level->props.size()), true);
                    build_tree->spatial.m_root->RearrangeNodePolys(0, build_tree->spatial.m_depth);
                    /* Both of these walk their count unsigned - the retail
                       guards with jbe at 0x00493D24 and 0x00493D45 and closes
                       each with jc at 0x00493D3B and 0x00493D5C - so the index
                       is unsigned and the static_cast<int> that was here made
                       the guard a jle. A scoped index, because i is int and the
                       surrounding loops compare it signed. */
                    unsigned int index;
                    for (index = 0; index < geometry.vertex_count; ++index) {
                        vertices[index].m_visited = false;
                    }
                    for (index = 0; index < geometry.m_polygon_count; ++index) {
                        geometry.m_polygons[index].visited = false;
                    }
                    ReportBuildStatus(6, "\nCompiling OctPreTree --------------------------\n");
                    tree = build_tree->BuildOctPreTree();
                    if (tree == 0) {
                        ReportBuildStatus(7, "Could not create OctPreTree.\n");
                        return 0;
                    }
                    tree->SetPathStem(stem);
                    tree->m_spatial.setWorkingBounds({g_weld_min, g_weld_max});
                    tree->m_alpha_polygon_count = alpha_polys;
                    value->octree = tree;
                    snprintf(message, sizeof(message), "Poly List Len: %d\n",
                             static_cast<int>(tree->polygon_cursor));
                    ReportBuildStatus(6, message);
                    tree->m_spatial.m_polygon_count = geometry.m_polygon_count;
                    SetOctreeGameData(value);
                    if (light_total != 0) {
                        tree->m_sun_count = static_cast<unsigned short>(sun_count);
                        ReportBuildStatus(6, "\nCalculating Vertex Lighting  --------------\n");
                        mark = 0;
                        lit_vertices = 0;
                        for (i = 1; i < static_cast<int>(geometry.vertex_count); ++i) {
                            percent = static_cast<unsigned int>(
                                (i * g_octree_cell_scale / geometry.vertex_count));
                            if (mark + 10 < percent) {
                                mark += 10;
                                snprintf(message, sizeof(message),
                                         "  %d%% Complete:  %d Vertices Lit \r", mark, i);
                                ReportStartupMessage(message);
                            }
                            if (AccumulateVertexLight(tree, vertices + i, lights, sun_map) != 0) {
                                ++lit_vertices;
                            }
                        }
                        short live_lights = 0;
                        for (const auto& light : lights) {
                            if (light.version < 2 || (light.create == 0 && light.visible != 0)) {
                                ++live_lights;
                            }
                        }
                        i = 0;
                        int combinations = live_lights * i;
                        snprintf(message, sizeof(message), "\n\n%d vertices, %d lights\n",
                                 static_cast<int>(geometry.vertex_count), light_total);
                        ReportBuildStatus(6, message);
                        snprintf(message, sizeof(message),
                                 "%d possible light/vertex combinations \n", combinations);
                        ReportBuildStatus(6, message);
                        if (combinations != 0) {
                            snprintf(message, sizeof(message),
                                     "%d (%d percent) combinations within lighting "
                                     "distance\n",
                                     g_light_candidates, g_light_candidates * 100 / combinations);
                            ReportBuildStatus(6, message);
                        }
                        if (g_light_candidates != 0) {
                            snprintf(message, sizeof(message),
                                     "%d (%d percent) of these face light\n", g_lights_facing,
                                     g_lights_facing * 100 / g_light_candidates);
                            ReportBuildStatus(6, message);
                        }
                        if (g_lights_facing != 0) {
                            snprintf(message, sizeof(message),
                                     "%d (%d percent) of these were blocked by vertex "
                                     "shadowing\n",
                                     g_lights_facing - g_lights_unblocked,
                                     100 - g_lights_unblocked * 100 / g_lights_facing);
                            ReportBuildStatus(6, message);
                            snprintf(message, sizeof(message),
                                     "%d (%d percent) submitted were not blocked by "
                                     "shadowing\n",
                                     g_lights_unblocked,
                                     g_lights_unblocked * 100 / g_lights_facing);
                            ReportBuildStatus(6, message);
                        }
                        if (geometry.vertex_count != 0) {
                            snprintf(message, sizeof(message),
                                     "%d (%d percent) of vertices actually receive "
                                     "light\n",
                                     lit_vertices,
                                     lit_vertices * 100 / static_cast<int>(geometry.vertex_count));
                            ReportBuildStatus(6, message);
                        }
                        const int prop_count = static_cast<int>(level->props.size());
                        std::vector<bool> sun_bits(level->bitmaps.size() + level->props.size());
                        if (prop_count != 0 && last_sun >= 0) {
                            int* read_entry = sun_map;
                            int* write_entry = sun_map;
                            for (i = light_total; i > 0; --i) {
                                if (*read_entry != 0) {
                                    *write_entry = *read_entry;
                                    ++write_entry;
                                    *read_entry = 0;
                                }
                                ++read_entry;
                            }
                            for (int* sun_entry = sun_map; sun_count-- > 0; ++sun_entry) {
                                const auto& sun = lights[*sun_entry];
                                for (i = 0; i < prop_count; ++i) {
                                    if (PropReceivesLight(tree, level->props[i], sun) != 0) {
                                        SetBit(sun_bits, i);
                                    }
                                }
                                for (i = 0; i < static_cast<int>(level->bitmaps.size()); ++i) {
                                    if (PropReceivesLight(tree, level->bitmaps[i], sun) != 0) {
                                        SetBit(sun_bits, prop_count + i);
                                    }
                                }
                            }
                            snprintf(message, sizeof(message), "%d of %d props receive sunlight\n",
                                     static_cast<int>(std::ranges::count(sun_bits, true)),
                                     static_cast<char>((prop_count << 1)));
                            ReportBuildStatus(6, message);
                        }
                        tree->SetPropSunBits(std::move(sun_bits));
                    }
                    ReportBuildStatus(6, "\nSorting Materials and Textures...\n");
                    result = MaterialSort(&geometry, level->textures, classify);
                    if (result == 0) {
                        ReportBuildStatus(7, "Could not generate polygon list by regions.\n");
                    } else {
                        snprintf(message, sizeof(message), "%d Textures, \t%d Materials\n",
                                 static_cast<int>(geometry.m_texture_count),
                                 static_cast<int>(geometry.m_material_count));
                        ReportBuildStatus(6, message);
                        const auto original_vertex_count = geometry.vertex_count;
                        ReportBuildStatus(6, "Splitting vertices by Material...\n");
                        result = geometry.splitVerticesByMaterial();
                        if (result == 0) {
                            ReportBuildStatus(7,
                                              "Could not allocate material-split vertex table.\n");
                        } else {
                            snprintf(
                                message, sizeof(message),
                                "Split %d vertices into %d new vertices--ratio is 1 to %.1f.\n\n",
                                static_cast<int>(original_vertex_count),
                                static_cast<int>(geometry.vertex_count),
                                geometry.vertex_count / static_cast<double>(original_vertex_count));
                            ReportBuildStatus(6, message);
                            ReportBuildStatus(6, "\nCreating Submeshes ------------------------\n");
                            submeshes = tree->CreateSubMeshes(&geometry);
                            snprintf(message, sizeof(message), "Number of Submeshes: %d\n",
                                     static_cast<int>(tree->GetMeshCount()));
                            ReportBuildStatus(6, message);
                            snprintf(message, sizeof(message),
                                     "   %d Normal,   %d Sutractive Alpha,   %d "
                                     "Additive Alpha\n",
                                     static_cast<int>(tree->m_root_mesh_count),
                                     static_cast<int>(
                                         (tree->m_kind1_submesh_count - tree->m_root_mesh_count)),
                                     static_cast<int>(
                                         (tree->GetMeshCount() - tree->m_kind1_submesh_count)));
                            ReportBuildStatus(6, message);
                            if (g_option_pathing) {
                                tree->m_region_cell = g_option_path_node_spacing;
                                tree->m_path_clearance = g_option_path_head_room;
                                tree->BuildPathLists(value, *level, g_option_delete_percentage);
                            }
                            if (submeshes.empty()) {
                                if (tree->GetMeshCount() == 0) {
                                    ReportBuildStatus(7, "Could not create submesh data.\n");
                                    result = 0;
                                }
                            } else if (tree->GetMeshCount() == 0) {
                                ReportBuildStatus(7, "Could not create submesh data.\n");
                                result = 0;
                            } else {
                                const auto bounds = tree->m_spatial.GetWorkingBounds();
                                ReportBuildStatus(6, "Graphic Data Bounding Box:\n");
                                snprintf(message, sizeof(message), "     Minimum: %f   %f   %f\n",
                                         bounds.minimum.x, bounds.minimum.y, bounds.minimum.z);
                                ReportBuildStatus(6, message);
                                snprintf(message, sizeof(message), "     Maximum: %f   %f   %f\n\n",
                                         bounds.maximum.x, bounds.maximum.y, bounds.maximum.z);
                                ReportBuildStatus(6, message);
                                ReportBuildStatus(6, "\nWriting Oct File...\n");
                                result = tree->WriteOctFile(&geometry, value);
                            }
                        }
                    }
                    file = 0;
                    if (result != 0) {
                        int total_faces = 0;
                        int total_vertices = 0;
                        for (i = 0; i < static_cast<int>(tree->GetMeshCount()); ++i) {
                            const char* fmt;
                            if (submeshes[i].m_packed_header == 0) {
                                fmt = "Submesh %d:  \t%d Faces,  \t%d Vertices  "
                                      "\t(Opaque)";
                            } else if (submeshes[i].m_packed_header == 1) {
                                fmt = "Submesh %d:  \t%d Faces,  \t%d Vertices  "
                                      "\t(Subtractive Alpha)";
                            } else {
                                fmt = "Submesh %d:  \t%d Faces,  \t%d Vertices  "
                                      "\t(Additive Alpha)";
                            }
                            snprintf(message, sizeof(message), fmt, i, submeshes[i].m_polygon_count,
                                     submeshes[i].m_vertex_count);
                            strcat(message, static_cast<int>(tree->m_spatial.m_region_count) <= i
                                                ? "\n"
                                                : " Regioned Manually\n");
                            ReportBuildStatus(5, message);
                            total_vertices += submeshes[i].m_vertex_count;
                            total_faces += submeshes[i].m_polygon_count;
                        }
                        snprintf(message, sizeof(message),
                                 "Totals:      \t%d Faces,  \t%d Vertices\n", total_faces,
                                 total_vertices);
                        ReportBuildStatus(6, message);
                        snprintf(message, sizeof(message), "Total duplicated vertices: %d\n",
                                 total_vertices - static_cast<int>(geometry.vertex_count));
                        ReportBuildStatus(6, message);
                        file = [&]() {
                            try {
                                return wiz8::open_file("NewLevel.lvl", wiz8::OpenMode::replace);
                            } catch (const std::exception&) {
                                return std::unique_ptr<wiz8::File>{};
                            }
                        }();
                        if (file == 0) {
                            ReportBuildStatus(7, "Could not open new level file.\n");
                            result = 0;
                        } else {
                            ReportBuildStatus(6, "\nWriting PVL File...\n");
                            result = result & WriteLevelFile(*file, handle, *level, submeshes,
                                                             tree->m_meshCount);
                            if (file) file->close();
                            file.reset();
                            if (result != 0) {
                                goto write_done;
                            }
                        }
                        ReportBuildStatus(7, "Could not write new level file.\n");
                    }
                write_done:
                    if (file != 0) {
                        if (file) file->close();
                        file.reset();
                    }
                    ReportStartupMessage("Cleaning up preprocessing data...");
                    geometry.Release();
                    if (g_gd_polygons != 0) {
                        free(g_gd_polygons);
                    }
                    if (g_gd_vertices != 0) {
                        free(g_gd_vertices);
                    }
                    if (classify != 0) {
                        free(classify);
                    }
                    delete tree;
                    delete value;
                    g_missing_textures.clear();
                    if (sun_pool != 0) {
                        free(sun_pool);
                    }
                    if (sun_map != 0) {
                        free(sun_map);
                    }
                    return result;
                }
                ReportBuildStatus(7, "Could not create OctPreTree.\n");
            }
        }
        return 0;
    }
}

// FUNCTION: WIZ8 0x00497690
void ReportBuildStatus(short channel, std::string_view message)
{
    std::string line;

    auto log = [](std::string_view text) { g_log_file->write(text.data(), text.size()); };
    try {
        switch (channel) {
        case 0:
            if (g_log_file == 0) {
                g_progress_total = 0;
                g_progress_done = 0;
                if (message.empty()) {
                    g_log_file = wiz8::open_file(g_log_path, wiz8::OpenMode::replace);
                    return;
                }
                if (!message.empty()) {
                    g_log_path = message;
                    if (g_option_logging) {
                        g_log_file = wiz8::open_file(message, wiz8::OpenMode::replace);
                        return;
                    }
                }
            }
            break;
        case 6:
            ReportStartupMessage(message);
            if (g_log_file != 0) {
                log(message);
                return;
            }
            break;
        case 7:
            if (g_log_file != 0) {
                log("\n\n");
                log(message);
                if (g_log_file) g_log_file->close();
                g_log_file.reset();
            }
            ShutdownWithErrorBox(message);
            return;
        case 1:
            ++g_progress_done;
            if (g_progress_mark + 10 <
                static_cast<unsigned int>((g_progress_done * 100.0f / g_progress_total))) {
                g_progress_mark += 10;
                line = fmt::format("  {:d}% Complete \r", g_progress_mark);
                ReportStartupMessage(line);
                return;
            }
            break;
        case 2:
            ++g_progress_total;
            return;
        case 3:
            if (g_log_file != 0) {
                log(message);
                line = fmt::format("Number of Nodes: {:d}             Number of Leaves: {:d}\n",
                                   g_oct_node_count, g_progress_total);
                log(line);
                line = fmt::format("Most Objects in Any Node: {:d}\n\n", g_oct_max_objects);
                log(line);
                return;
            }
            break;
        case 4:
            ReportStartupMessage(message);
            return;
        case 5:
            if (g_log_file != 0) {
                log(message);
                return;
            }
            break;
        case 8:
            if (g_log_file != 0) {
                if (g_log_file) g_log_file->close();
                g_log_file.reset();
            }
            break;
        }
    } catch (const std::exception&) {
        g_log_file.reset();
    }
}

// FUNCTION: WIZ8 0x004969D0
void ReportStartupMessage(std::string_view message)
{
    static glm::vec3 s_black{1.0f};
    static glm::vec3 s_saved_colour{1.0f};
    std::string* line;
    bool scroll;
    bool scrolled;
    int index;
    int top;

    scrolled = false;
    if (message.empty()) {
        if (!g_status_buffers_freed) {
            for (index = 0; index < 6; ++index) {
                g_status_lines[index].clear();
            }
            g_status_buffers_freed = true;
        }
        PublishLightDirection(&s_saved_colour);
        return;
    }
    if (g_status_buffers_freed) {
        for (index = 0; index < 6; ++index) {
            g_status_lines[index].clear();
        }
        GetWorldColour(&s_saved_colour);
        PublishLightDirection(&s_black);
        g_status_buffers_freed = false;
    }
    if (g_status_cursor < 6) {
        index = g_status_cursor;
        ++g_status_cursor;
        line = &g_status_lines[index];
    } else {
        line = &g_status_lines[5];
        if (g_status_scroll) {
            std::rotate(std::begin(g_status_lines), std::begin(g_status_lines) + 1,
                        std::end(g_status_lines));
            scrolled = true;
            line = &g_status_lines[5];
        }
    }
    scroll = !message.ends_with('\r');
    line->clear();
    for (const unsigned char byte : message) {
        if (byte > 0x1f) line->push_back(static_cast<char>(byte));
    }
    g_smfnt_font.setPalette(g_font_state_palettes[W8_FONT_PALETTE_YELLOW]);
    const TextOptions text_options{
        .colors = {.shadow = Get16BPPColor(SDL_Color{0, 0, 0, SDL_ALPHA_OPAQUE})}};
    if (scrolled) {
        ClearSurfaceRect(0, 400, 0x27f, 0x1df);
        index = 0x191;
        for (top = 0; top < 6; ++top) {
            DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font, {1, index},
                                  fmt::sprintf(const_cast<char*>("%s"), g_status_lines[top]),
                                  text_options);
            index += 0xd;
        }
        InvalidateRegion(0, 400, 0x27f, 0x1df);
    } else {
        index = (g_status_cursor - 1) * 0xd;
        top = index + 400;
        ClearSurfaceRect(0, top, 0x27f, g_status_cursor * 0xd + 400);
        DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font, {1, index + 0x191}, *line,
                              text_options);
        InvalidateRegion(0, top, 0x27f, g_status_cursor * 0xd + 400);
    }
    g_status_scroll = scroll;
    RenderFrame();
    RenderFrame();
}

/* Quantize the vertex position into the weld grid and scan the 27 neighbouring
   cells for a positional match within 2.5 units; first hit wins and reports
   the stored 1-based index.  Otherwise the vertex is entered under its cell
   key with value index + 1.  A `link` of -1 maintains the vertex self-link or
   redirect fields. */
// FUNCTION: WIZ8 0x00494800
static int WeldVertex(std::unordered_multimap<unsigned int, int>* table,
                      W8OctPreTreeVertex* vertices, unsigned int index, unsigned int link)
{
    unsigned int vertex = link;
    unsigned int cell_x;
    unsigned int cell_y;
    unsigned int cell_z;
    SDL_Keycode key;
    unsigned int start_x;
    unsigned int start_y;
    unsigned int start_z;

    bool found;
    bool matched;
    unsigned int scan_x;
    unsigned int scan_y;
    unsigned int scan_z;
    unsigned int scan_key;
    int match_index;
    W8OctPreTreeVertex* current;
    W8OctPreTreeVertex* candidate;

    matched = false;
    found = false;
    if (link == 0xffffffff) {
        vertex = index;
    }
    current = vertices + vertex;
    cell_x =
        static_cast<unsigned int>(((current->position.x - g_weld_min.x) * g_float_two_hundredths));
    cell_y =
        static_cast<unsigned int>(((current->position.y - g_weld_min.y) * g_float_two_hundredths));
    cell_z =
        static_cast<unsigned int>(((current->position.z - g_weld_min.z) * g_float_two_hundredths));
    key = cell_z * g_weld_stride_z + cell_y * g_weld_stride_y + cell_x * g_weld_stride_x;
    start_x = cell_x;
    if (cell_x != 0) {
        start_x = cell_x - 1;
    }
    start_y = cell_y;
    if (cell_y != 0) {
        start_y = cell_y - 1;
    }
    start_z = cell_z;
    if (cell_z != 0) {
        start_z = cell_z - 1;
    }
    for (scan_x = start_x; scan_x <= cell_x + 1; ++scan_x) {
        for (scan_y = start_y; scan_y <= cell_y + 1; ++scan_y) {
            for (scan_z = start_z; scan_z <= cell_z + 1; ++scan_z) {
                scan_key =
                    scan_z * g_weld_stride_z + scan_y * g_weld_stride_y + scan_x * g_weld_stride_x;
                auto slot = table->equal_range(scan_key).first;
                while (slot != table->end()) {
                    match_index = slot->second - 1;
                    if (match_index >= 0 && !matched) {
                        candidate = vertices + match_index;
                        if (fabs(current->position.x - candidate->position.x) <
                                g_weld_position_tolerance &&
                            fabs(current->position.y - candidate->position.y) <
                                g_weld_position_tolerance &&
                            fabs(current->position.z - candidate->position.z) <
                                g_weld_position_tolerance) {
                            if (link == 0xffffffff) {
                                vertices[index].m_vertex_index = match_index;
                                vertices[index].flags |= W8OctPreTreeVertex::EXCLUDED;
                            }
                            found = true;
                        }
                    }
                    slot = NextIndexValue(*table, scan_key, slot);
                }
            }
        }
    }
    if (found) {
        return match_index + 1;
    }
    int vertex_id = index + 1;
    InsertIndexValue(*table, key, vertex_id);
    if (link == 0xffffffff) {
        vertices[index].m_vertex_index = index;
    }
    return index + 1;
}

/* Converts the level mesh faces into the shared polygon array: each face's
   1-based corner indices are bounds-checked back into the file record, the
   polygon keeps an embedded copy of the face, plane/centroid are derived and
   two-sided or opposing-normal polygons weld their corners and emit a mirrored
   backface. Returns the alpha polygon count. */
// FUNCTION: WIZ8 0x00494B90
static int BuildRegionPolygons(W8LevelFile& level, W8OctPreTreeGeometry* geometry,
                               unsigned char* classify)
{
    W8LevelFileMesh* mesh = &level.mesh;
    W8MaterialRecord* materials = level.textures.data();
    W8OctPreTreeVertex* vertices = geometry->m_vertices;
    char message[1024];
    int vertex_index[3];
    int kind_counts[3];
    int corner;
    int degenerate_count;
    int ordinal;
    int last_percent;
    w8_ulong axis;
    float offset;
    bool opposing;
    glm::vec3 normal;
    W8OctRegionPolygon* polygons;
    W8OctRegionPolygon* polygon;
    W8OctRegionPolygon* back;
    W8SerializedMeshFace* face;
    W8OctPreTreeVertex* current;
    W8OctPreTreeVertex* created;

    std::unordered_multimap<unsigned int, int> weld_table;
    kind_counts[0] = 0;
    kind_counts[1] = 0;
    kind_counts[2] = 0;
    ++mesh->num_faces;
    int poly_total = mesh->num_faces;
    polygons =
        static_cast<W8OctRegionPolygon*>(malloc(poly_total * 2 * sizeof(W8OctRegionPolygon)));
    if (polygons == 0) {
        return 0;
    }
    memset(polygons, 0, poly_total * 2 * sizeof(W8OctRegionPolygon));
    degenerate_count = 0;
    last_percent = 0;
    ReportBuildStatus(6, "\nSorting and optimizing graphic polygons... \n");
    ordinal = 1;
    if (1 < poly_total) {
        polygon = polygons + 1;
        face = mesh->faces.data();
        do {
            if (last_percent < static_cast<int>((ordinal * 100.0f / poly_total))) {
                ++last_percent;
                snprintf(message, sizeof(message), "  %d%% Complete:  %d Polygons processed \r",
                         last_percent, ordinal);
                ReportStartupMessage(message);
            }
            ++face->vertices[0];
            ++face->vertices[1];
            ++face->vertices[2];
            if (face->vertices[0] < 1 || mesh->num_vertices <= face->vertices[0]) {
                snprintf(message, sizeof(message),
                         "Mesh Read: Polygon %d Vertex 0 has invalid index: %d.", ordinal,
                         face->vertices[0]);
                goto invalid;
            }
            if (face->vertices[1] < 1 || mesh->num_vertices <= face->vertices[1]) {
                snprintf(message, sizeof(message),
                         "Mesh Read: Polygon %d Vertex 1 has invalid index: %d.", ordinal,
                         face->vertices[1]);
                goto invalid;
            }
            if (face->vertices[2] < 1 || mesh->num_vertices <= face->vertices[2]) {
                snprintf(message, sizeof(message),
                         "Mesh Read: Polygon %d Vertex 2 has invalid index: %d.", ordinal,
                         face->vertices[2]);
                goto invalid;
            }
            polygon->face = DecodeMeshFace(*face);
            polygon->ordinal = ordinal;
            polygon->material = face->material_index;
            for (corner = 0; corner < 3; ++corner) {
                int index = face->vertices[corner];
                if (vertices[index].m_vertex_index != static_cast<unsigned int>(index)) {
                    index = vertices[index].m_vertex_index;
                }
                vertex_index[corner] = index;
                polygon->vertices[corner] = vertices + index;
                polygon->position += vertices[index].position * g_float_one_third;
            }
            polygon->degenerate = 0;
            if (vertex_index[0] == vertex_index[1] || vertex_index[0] == vertex_index[2] ||
                vertex_index[1] == vertex_index[2]) {
                ++degenerate_count;
                polygon->degenerate = 1;
            }
            if (polygon->degenerate == 0) {
                normal = trianglePlane(vertices[vertex_index[0]].m_original_position,
                                       vertices[vertex_index[1]].m_original_position,
                                       vertices[vertex_index[2]].m_original_position)
                             .normal;
                offset = 0.0f;
                for (corner = 0; corner < 3; ++corner) {
                    current = vertices + vertex_index[corner];
                    offset = normal.z * current->position.z + normal.y * current->position.y +
                             normal.x * current->position.x + offset;
                }
                offset *= g_float_negative_one_third;
                polygon->plane.normal = normal;
                axis = dominantPlaneAxis(normal);
                polygon->plane.w = offset;
                polygon->flags = axis;
                opposing = false;
                for (corner = 0; corner < 3; ++corner) {
                    current = vertices + vertex_index[corner];
                    if (current->m_normal_count != 0) {
                        // NOLINTNEXTLINE(misc-include-cleaner)
                        if (glm::dot(normalizedOrZero(current->m_normal), normal) <
                            g_material_opposed_normal_threshold) {
                            opposing = true;
                        }
                    }
                }
                if (!opposing) {
                    for (corner = 0; corner < 3; ++corner) {
                        current = vertices + vertex_index[corner];
                        current->m_normal = normal + current->m_normal;
                        ++current->m_normal_count;
                    }
                }
                if ((materials[face->material_index].surface_flags & W8_MATERIAL_TWO_SIDED) != 0) {
                    opposing = true;
                }
                polygon->visited = false;
                if (face->material_index == 0) {
                    polygon->kind = 3;
                } else {
                    polygon->kind = classify[face->material_index];
                }
                ++kind_counts[polygon->kind];
                if (ordinal < poly_total && opposing) {
                    for (corner = 0; corner < 3; ++corner) {
                        int found = WeldVertex(&weld_table, vertices, mesh->num_vertices,
                                               vertex_index[corner]);
                        if (found == mesh->num_vertices + 1) {
                            current = vertices + vertex_index[corner];
                            created = vertices + mesh->num_vertices;
                            *created = *current;
                            created->m_vertex_index = mesh->num_vertices;
                            current->flags |= W8OctPreTreeVertex::NORMAL_SPLIT;
                            vertex_index[corner] = mesh->num_vertices;
                            ++mesh->num_vertices;
                            created->flags |= W8OctPreTreeVertex::NORMAL_SPLIT;
                            created->m_normal = glm::vec3(0);
                            created->m_normal_count = 0;
                        } else {
                            vertex_index[corner] = found - 1;
                        }
                        if ((materials[face->material_index].surface_flags &
                             W8_MATERIAL_TWO_SIDED) == 0) {
                            polygon->vertices[corner] = vertices + vertex_index[corner];
                        }
                        current = vertices + vertex_index[corner];
                        current->m_normal = normal + current->m_normal;
                        ++current->m_normal_count;
                    }
                    if ((materials[face->material_index].surface_flags & W8_MATERIAL_TWO_SIDED) !=
                        0) {
                        back = polygons + mesh->num_faces;
                        *back = *polygon;
                        back->ordinal = mesh->num_faces;
                        back->vertices[0] = vertices + vertex_index[1];
                        back->vertices[1] = vertices + vertex_index[0];
                        back->vertices[2] = vertices + vertex_index[2];
                        back->plane.normal *= g_negative_one;
                        back->plane.w *= g_negative_one;
                        back->face.vertices[0] = vertex_index[1];
                        back->face.vertices[1] = vertex_index[0];
                        back->face.vertices[2] = vertex_index[2];
                        back->face.texture_coordinates[0] = polygon->face.texture_coordinates[1];
                        back->face.texture_coordinates[1] = polygon->face.texture_coordinates[0];
                        ++mesh->num_faces;
                    }
                }
            }
            ++ordinal;
            ++face;
            ++polygon;
        } while (ordinal < poly_total);
    }
    geometry->vertex_count = mesh->num_vertices;
    geometry->m_polygon_count = mesh->num_faces;
    geometry->m_polygons = polygons;
    snprintf(message, sizeof(message), "  100%% Complete:  %d Polygons processed \r", ordinal);
    ReportBuildStatus(6, message);
    snprintf(message, sizeof(message), "Number of Two-sided Polys (Polys added): %d\n",
             mesh->num_faces - poly_total);
    ReportBuildStatus(6, message);
    snprintf(message, sizeof(message), "Number of Non-Alpha Polys: %d\n", kind_counts[0]);
    ReportBuildStatus(6, message);
    snprintf(message, sizeof(message), "Number of Subtractive Alpha Polys: %d\n", kind_counts[1]);
    ReportBuildStatus(6, message);
    snprintf(message, sizeof(message), "Number of Additive Alpha Polys: %d\n", kind_counts[2]);
    ReportBuildStatus(6, message);
    if (degenerate_count != 0) {
        snprintf(message, sizeof(message), "Number of Degenerate Polys: %d\n", degenerate_count);
        ReportBuildStatus(6, message);
    }
    ReportBuildStatus(6, "\n");
    return kind_counts[1] + kind_counts[2];
invalid:
    ReportBuildStatus(7, message);
    return 0;
}

/* Duplicates build vertices whose polygons disagree on material so every
   polygon corner can point at a vertex carrying that polygon's material and
   uv, then repacks the vertex array and repoints the corners. */
// FUNCTION: WIZ8 0x00495860
bool W8OctPreTreeGeometry::splitVerticesByMaterial()
{
    // Keep indices, rather than temporary pointers, until the complete table
    // is allocated. Material runs retain first-face ordering and UV selection.
    std::vector<W8OctPreTreeVertex> splits(1);
    std::vector<geometryRepack::CornerIndices> corners(m_polygon_count);
    for (w8_ulong source = 1; source < vertex_count; ++source) {
        const auto& original = m_vertices[source];
        std::unordered_map<w8_ulong, w8_ulong> material_splits;
        for (const auto face : std::span(original.face_indices, original.face_count)) {
            auto& polygon = m_polygons[face];
            const auto [entry, fresh] =
                material_splits.try_emplace(polygon.material, static_cast<w8_ulong>(splits.size()));
            if (fresh) {
                auto record = original;
                record.m_vertex_index = entry->second;
                record.m_visited = true;
                record.m_material = polygon.material;
                record.m_kind = polygon.kind;
                splits.push_back(record);
            }
            for (unsigned corner = 0; corner < 3; ++corner) {
                if (polygon.vertices[corner] == &original) {
                    corners[face][corner] = entry->second;
                    if (fresh)
                        splits[entry->second].m_uv = polygon.face.texture_coordinates[corner];
                }
            }
        }
    }
    auto vertices = geometryRepack::copyTable<W8OctPreTreeVertex>(splits);
    if (!vertices) return false;
    geometryRepack::repointCorners({m_polygons, m_polygon_count}, corners, vertices.get());
    std::free(m_vertices);
    m_vertices = vertices.release();
    vertex_count = static_cast<w8_ulong>(splits.size());
    return true;
}

// FUNCTION: WIZ8 0x00495CF0
static int AccumulateVertexLight(OctPreTree* tree, W8OctPreTreeVertex* vertex,
                                 std::span<const W8LevelFileLight> lights, int* sun_map)
{
    short lit = 0;
    for (std::size_t index = 0; index < lights.size(); ++index) {
        const auto& light = lights[index];
        const bool is_sun = sun_map && sun_map[index] != 0;
        if (light.version >= 2 && (light.create != 0 || light.visible == 0)) continue;
        const glm::vec3 delta = light.position - vertex->position;
        // NOLINTNEXTLINE(misc-include-cleaner)
        const float distance = glm::length(delta);
        if (distance >= light.range && !is_sun) continue;
        ++g_light_candidates;
        const float dot = glm::dot(delta / distance, vertex->m_normal);
        if (dot <= g_float_zero) continue;
        ++g_lights_facing;
        if (g_option_shadow_test && !tree->SegmentClear(light.position, vertex->position)) continue;
        ++g_lights_unblocked;
        ++lit;
        if (is_sun) {
            *vertex->m_sun_lights = dot * light.intensity + *vertex->m_sun_lights;
        } else {
            float scale = dot * light.intensity * (g_float_one - distance / light.range);
            srLightingMath::accumulateLight(std::span{&vertex->m_light, 1}, light.colour,
                                            std::span{&scale, 1});
        }
    }
    return lit;
}

/* Tests whether a prop/bitmap record sees a given sun light: the segment from
   the light to the record position must be clear, else each corner of each
   recorded bounds pair is tried. */
// FUNCTION: WIZ8 0x00495E90
static unsigned char PropReceivesLight(OctPreTree* tree, const W8LevelFileProp& prop,
                                       const W8LevelFileLight& light)
{
    const auto origin = light.position;
    if (tree->SegmentClear(origin, prop.position * g_world_scale)) return 1;
    for (const auto& box : prop.anim_obj.bounds) {
        const srBoundingBox bounds{
            glm::vec3(box.minimum.x, box.minimum.y, box.minimum.z) * g_world_scale,
            glm::vec3(box.maximum.x, box.maximum.y, box.maximum.z) * g_world_scale};
        // Bounding-box corners retain the original x/y/z probe order.
        for (unsigned corner = 0; corner < 8; ++corner)
            if (tree->SegmentClear(origin, bounds.corner(corner))) return 1;
    }
    return 0;
}

/* Classifies every material's automesh kind: picks the first populated texture
   name, probes the bitmap (following .ifl indirection) for a real alpha channel
   when the record claims full opacity, and records missing textures in the
   prop/sun bit array. Returns the per-material kind byte array. */
// FUNCTION: WIZ8 0x00496000
static unsigned char* ClassifyTextures(std::span<W8MaterialRecord> textures, char* stem)
{
    const int count = static_cast<int>(textures.size());
    char folder[1024];
    char texture[1024];
    std::string path;
    char message[1024];
    bool more;
    unsigned char* kinds;
    int missing;
    int index;
    std::unique_ptr<wiz8::File> file;
    unsigned char kind;
    stTextureFile* probe;

    missing = 0;
    probe = 0;
    kinds = static_cast<unsigned char*>(malloc(count));
    memset(kinds, 0, count);
    strcpy(folder, stem);
    char* slash = strrchr(folder, '\\');
    if (slash != 0) {
        slash[1] = '\0';
    }
    strcat(folder, "Bitmaps\\");
    if ([&]() {
            const auto status = wiz8::file_status(folder);
            return status && status->info.type == SDL_PATHTYPE_DIRECTORY;
        }() == 0) {
        ReportBuildStatus(7, "Couldn't find bitmaps directory--cannot check texture types.\n\n");
        kinds = 0;
    } else {
        g_missing_textures.assign(count, false);
        for (index = 0; index < count; ++index) {
            W8MaterialRecord* record = &textures[index];
            bool opaque = 1.0f <= record->opacity;
            texture[0] = '\0';
            if (record->texture_name[0] != 0) {
                strcpy(texture, record->texture_name);
            }
            if (record->texture_names[0][0] == '\0') {
                if (record->texture_names[1][0] == '\0') {
                    if (record->texture_names[2][0] == '\0') {
                        if (record->texture_names[3][0] == '\0') {
                            goto probe;
                        }
                        strcpy(texture, record->texture_names[3]);
                        kind = 2;
                    } else {
                        strcpy(texture, record->texture_names[2]);
                        kind = 2;
                    }
                } else {
                    strcpy(texture, record->texture_names[1]);
                    kind = 1;
                }
            } else {
                strcpy(texture, record->texture_names[0]);
                if (opaque) {
                    goto probe;
                }
                kind = 1;
            }
            goto store;
        probe:
            kind = 0;
            if (texture[0] == '\0') {
                kind = 3;
            } else {
                path = fmt::format("{}{}", folder, texture);
                file = [&]() {
                    try {
                        return wiz8::open_file(path, wiz8::OpenMode::read);
                    } catch (const std::exception&) {
                        return std::unique_ptr<wiz8::File>{};
                    }
                }();
                if (file == 0) {
                    SetBit(g_missing_textures, index);
                    ++missing;
                    snprintf(message, sizeof(message), "Could not find texture file %s.\n",
                             path.c_str());
                    ReportBuildStatus(5, message);
                } else {
                    if (path.size() >= 4 &&
                        SDL_strcasecmp(path.c_str() + path.size() - 4, ".ifl") == 0) {
                        ReadTextLine(file.get(), texture, 0x3ff, more);
                        if (file) file->close();
                        file.reset();
                        path = fmt::format("{}{}", folder, texture);
                        file = [&]() {
                            try {
                                return wiz8::open_file(path, wiz8::OpenMode::read);
                            } catch (const std::exception&) {
                                return std::unique_ptr<wiz8::File>{};
                            }
                        }();
                    }
                    if (file == 0) {
                        SetBit(g_missing_textures, index);
                        ++missing;
                        snprintf(message, sizeof(message), "Could not find texture file %s.\n",
                                 path.c_str());
                        ReportBuildStatus(5, message);
                    } else {
                        if (file) file->close();
                        file.reset();
                        if (probe == 0) {
                            probe = new stTextureFile(path.c_str(), 1);
                        } else {
                            probe->setFileName(path.c_str());
                        }
                        if (probe == 0) {
                            snprintf(message, sizeof(message),
                                     "Could not create stTextureFile object.\n");
                            ReportBuildStatus(7, message);
                            return 0;
                        }
                        probe->loadSurface();
                        kind = probe->hasAlpha() != 0;
                        probe->releaseSurface();
                    }
                }
            }
        store:
            kinds[index] = kind;
        }
        if (missing != 0) {
            snprintf(message, sizeof(message), "%d missing texture names found!\n", missing);
            ReportBuildStatus(6, message);
        }
    }
    return kinds;
}

/* Groups materials by canonical texture name and by their rendered signature
   string, remaps each polygon's material index at its group's representative
   and moves the old index into the texture slot. */
// FUNCTION: WIZ8 0x00496500
static unsigned char MaterialSort(W8OctPreTreeGeometry* geometry,
                                  std::span<W8MaterialRecord> textures, unsigned char* classify)
{
    const int count = static_cast<int>(textures.size());
    char name[516];
    char* material_names;
    char* texture_names;
    int* material_lookup;
    int* texture_lookup;
    int material_count;
    int texture_count;
    int missing;
    int index;
    int group;
    int canonical;
    int scan;
    const char* texture_name;
    W8OctRegionPolygon* polygon;

    missing = 0;
    material_names = static_cast<char*>(malloc(count << 9));
    if (material_names == 0) {
        ReportBuildStatus(7, "MaterialSort: Could not allocate acMatNames\n");
    }
    memset(material_names, 0, count << 9);
    texture_names = static_cast<char*>(malloc(count << 9));
    if (texture_names == 0) {
        ReportBuildStatus(7, "MaterialSort: Could not allocate acTextNames\n");
        return 0;
    }
    memset(texture_names, 0, count << 9);
    material_lookup = static_cast<int*>(malloc(count * sizeof(int)));
    if (material_lookup == 0) {
        ReportBuildStatus(7, "MaterialSort: Could not allocate piMatLookup\n");
        return 0;
    }
    memset(material_lookup, 0, count * 4);
    texture_lookup = static_cast<int*>(malloc(count * sizeof(int)));
    if (texture_lookup == 0) {
        ReportBuildStatus(7, "MaterialSort: Could not allocate piTextLookup\n");
        return 0;
    }
    memset(texture_lookup, 0, count * 4);
    material_count = 0;
    texture_count = 0;
    if (1 < count) {
        W8MaterialRecord* record = &textures[1];
        int* material_slot = material_lookup;
        for (index = 1; index < count; ++index, ++record) {
            ++material_slot;
            texture_name = record->texture_name;
            if (*texture_name == '\0') {
                texture_name = record->texture_names[0];
            }
            if (*texture_name == '\0') {
                texture_name = record->texture_names[1];
            }
            if (*texture_name == '\0') {
                texture_name = record->texture_names[2];
            }
            if (*texture_name == '\0') {
                texture_name = record->texture_names[3];
            }
            if (*texture_name == '\0') {
                ReportBuildStatus(7, "MaterialSort: Missing Texture Name\n");
                return 0;
            }
            strcpy(name, texture_name);
            for (scan = 0; scan < texture_count; ++scan) {
                if (strcmp(texture_names + scan * 0x200, name) == 0) {
                    break;
                }
            }
            texture_lookup[index] = scan;
            if (scan == texture_count) {
                strcpy(texture_names + scan * 0x200, name);
                ++texture_count;
            }
            snprintf(name, sizeof(name),
                     "Mat:%1.2f %1.2f %1.2f %1.2f %1.2f %1.2f %1.2f %1.2f %1.2f %d %c",
                     record->diffuse.x, record->diffuse.y, record->diffuse.z, record->specular.x,
                     record->specular.y, record->specular.z, record->shininess, record->opacity,
                     record->emission, static_cast<int>(record->surface_flags), classify[index]);
            for (scan = 0; scan < material_count; ++scan) {
                if (strcmp(material_names + scan * 0x200, name) == 0) {
                    break;
                }
            }
            *material_slot = scan;
            if (scan == material_count) {
                strcpy(material_names + scan * 0x200, name);
                ++material_count;
            }
        }
    }
    for (group = 0; group < material_count; ++group) {
        canonical = -1;
        for (scan = 1; scan < count; ++scan) {
            if (material_lookup[scan] == group) {
                if (canonical < 0) {
                    canonical = scan + 1 + count;
                }
                material_lookup[scan] = canonical;
            }
        }
    }
    if (1 < count) {
        for (index = 1; index < count; ++index) {
            material_lookup[index] += -1 - count;
        }
    }
    if (1 < static_cast<int>(geometry->m_polygon_count)) {
        polygon = geometry->m_polygons + 1;
        for (index = 1; index < static_cast<int>(geometry->m_polygon_count); ++index, ++polygon) {
            if (TestBit(g_missing_textures, index)) {
                ++missing;
            }
            polygon->texture = polygon->material;
            polygon->material = material_lookup[polygon->material];
        }
        if (missing != 0) {
            snprintf(name, sizeof(name), "\nWARNING!!! Missing textures assigned to %d polys!\n",
                     missing);
            ReportBuildStatus(6, name);
        }
    }
    geometry->m_material_count = material_count;
    geometry->m_texture_count = texture_count;
    free(material_names);
    free(texture_names);
    free(material_lookup);
    free(texture_lookup);
    return 1;
}

/* Flips an editor option and writes its ON/OFF label. */
static void ToggleOption(bool& option, char* label)
{
    if (!option) {
        strcpy(label, "ON ");
        option = true;
    } else {
        strcpy(label, "OFF");
        option = false;
    }
}

/* Interactive OctBuild option screen: draws the seven status lines, handles the
   toggle keys and the six numeric editors, then prints the preprocessing banner
   line and restores the saved light direction. */
// FUNCTION: WIZ8 0x00496CD0
void W8Octree::OctBuildOptions(char* stem)
{
    bool done = g_build_level_links;
    glm::vec3 colour_saved(0);
    glm::vec3 colour_backup(0);
    std::string lines[7];
    char log_state[8];
    char rename_state[8];
    char mesh_state[8];
    char pathing_state[8];
    char spare_state[8];
    char edit_buffer[20];
    char edit_char[8];
    int edit_mode;
    int line;

    GetWorldColour(&colour_backup);
    PublishLightDirection(&colour_saved);
    g_smfnt_font.setPalette(g_font_state_palettes[W8_FONT_PALETTE_YELLOW]);
    const TextOptions text_options{
        .colors = {.shadow = Get16BPPColor(SDL_Color{0, 0, 0, SDL_ALPHA_OPAQUE})}};
    edit_char[0] = 0;
    edit_char[4] = 0;
    strcpy(log_state, "OFF");
    if (!g_option_pathing) {
        strcpy(pathing_state, "OFF");
    } else {
        strcpy(pathing_state, "ON");
    }
    strcpy(rename_state, "OFF");
    strcpy(mesh_state, "ON ");
    strcpy(spare_state, "OFF");
    strcpy(edit_buffer, "");
    while (!done) {
        edit_mode = 0;
        for (;;) {
            const char* prompt;
            lines[0] = fmt::format("OCTBUILD VERSION {:d} -- OPTIONS: ", 0x22);
            lines[1] = fmt::format("(L)og {}               ", log_state);
            if (!g_option_pathing) {
                lines[2] = fmt::format("(P)athing {}                                         ",
                                       pathing_state);
            } else {
                lines[2] = fmt::format("(P)athing {}    (N)ode Spacing: {:5.2f}m    (H)ead Room:   "
                                       "{:5.2f}m    (D)elete Percentage: {:d}",
                                       pathing_state,
                                       (g_option_path_node_spacing * g_float_one_five_hundredth),
                                       (g_option_path_head_room * g_float_one_five_hundredth),
                                       g_option_delete_percentage);
            }
            lines[3] =
                fmt::format("(R)ename Alphas {}    (M)esh Linking {}", rename_state, mesh_state);
            lines[4] = fmt::format("Min. Leaf (S)ize {:5.2f}m    Max. Leaf (C)ount {:d}    (A)uto "
                                   "Region Size {:5.2f}m     ",
                                   (g_option_min_leaf_size * g_float_one_five_hundredth),
                                   g_option_max_leaf_count,
                                   (g_option_auto_region_size * g_float_one_five_hundredth));
            lines[5] = "Hit ENTER to accept,  ESC to cancel and exit";
            if (edit_mode == 0) {
                lines[6] = "                                            ";
            } else {
                if (edit_mode == 1) {
                    prompt = " Path Node Spacing: %s"
                             "                          ";
                } else if (edit_mode == 2) {
                    prompt = " Minimum Leaf Size: %s"
                             "                           ";
                } else if (edit_mode == 3) {
                    prompt = " Auto Region Size: %s"
                             "                                             ";
                } else if (edit_mode == 4) {
                    prompt = " Maximum Number of Octree Leaves: %s"
                             "                              ";
                } else if (edit_mode == 5) {
                    prompt = " Path Node Head Room: %s"
                             "                                          ";
                } else {
                    prompt = " Delete path node groups less than this %% of the total: %s"
                             "       ";
                }
                lines[6] = fmt::sprintf(prompt, edit_buffer);
            }
            ClearSurfaceRect(0, 0x183, 0x27f, 0x1df);
            for (line = 0; line < 7; ++line) {
                auto display = lines[line].substr(0, wiz8::text::codepoint_prefix(lines[line], 90));
                if (display.size() < 90) display.append(90 - display.size(), ' ');
                DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font,
                                      {1, 0x184 + line * 0xd}, display, text_options);
            }
            InvalidateRegion(0, 0x183, 0x27f, 0x1df);
            auto next_input = PollInputEvent();
            while (gfProgramIsRunning && !next_input) {
                RenderFrame();
                RenderFrame();
                PumpGameEvents(true);
                next_input = PollInputEvent();
            }
            if (!gfProgramIsRunning) {
                goto accepted;
            }
            const InputEvent atom = *next_input;
            if (atom.type != InputEventType::KeyDown) {
                continue;
            }
            if (atom.key == SDLK_ESCAPE) {
                ShutdownWithErrorBox("Cancelled--Program exiting.\n");
                goto accepted;
            }
            if (atom.key == SDLK_RETURN) {
                break;
            }
            if (edit_mode == 0) {
                switch (
                    (atom.key <= 0x7f ? toupper(static_cast<unsigned char>(atom.key)) : atom.key)) {
                case 'A':
                    edit_mode = 3;
                    break;
                case 'C':
                    edit_mode = 4;
                    break;
                case 'D':
                    if (g_option_pathing) {
                        edit_mode = 6;
                    }
                    break;
                case 'H':
                    if (g_option_pathing) {
                        edit_mode = 5;
                    }
                    break;
                case 'L':
                    ToggleOption(g_option_logging, log_state);
                    break;
                case 'M':
                    ToggleOption(g_option_mesh_linking, mesh_state);
                    break;
                case 'N':
                    edit_mode = 1;
                    break;
                case 'P':
                    ToggleOption(g_option_pathing, pathing_state);
                    break;
                case 'R':
                    ToggleOption(g_option_rename_alphas, rename_state);
                    break;
                case 'S':
                    edit_mode = 2;
                    break;
                }
            } else {
                char key = static_cast<char>(
                    (atom.key <= 0x7f ? toupper(static_cast<unsigned char>(atom.key)) : atom.key));
                if ((key < '0' || '9' < key) && key != '.') {
                    if (key == '\b') {
                        edit_buffer[strlen(edit_buffer) - 1] = '\0';
                    }
                } else {
                    edit_char[0] = key;
                    strcat(edit_buffer, edit_char);
                }
            }
        }
        if (edit_mode == 0) {
            done = true;
        } else if (edit_mode == 1) {
            g_option_path_node_spacing = static_cast<float>(atof(edit_buffer)) * g_world_scale;
        } else if (edit_mode == 2) {
            g_option_min_leaf_size = static_cast<float>(atof(edit_buffer)) * g_world_scale;
        } else if (edit_mode == 3) {
            g_option_auto_region_size = static_cast<float>(atof(edit_buffer)) * g_world_scale;
        } else if (edit_mode == 4) {
            g_option_max_leaf_count = atoi(edit_buffer);
        } else if (edit_mode == 5) {
            g_option_path_head_room = static_cast<float>(atof(edit_buffer)) * g_world_scale;
        } else if (edit_mode == 6) {
            g_option_delete_percentage = atoi(edit_buffer);
        }
        edit_buffer[0] = '\0';
    }
accepted:
    ClearSurfaceRect(0, 0x183, 0x27f, 0x1df);
    lines[1] = fmt::format("OCTBUILD VERSION {:d} -- Preprocessing {}: ", 0x22, stem);
    auto display = lines[1].substr(0, wiz8::text::codepoint_prefix(lines[1], 90));
    if (display.size() < 90) display.append(90 - display.size(), ' ');
    DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font, {1, 0x184}, display, text_options);
    InvalidateRegion(0, 0x183, 0x27f, 0x1df);
    RenderFrame();
    RenderFrame();
    PublishLightDirection(&colour_saved);
}

ResourceCache<stMaterial> g_materials;
ResourceCache<stTextureFile> g_texture_files;
ResourceCache<stTextureAnim> g_texture_animations;

/* Select the first populated texture layer, load its file or IFL animation,
   derive the renderer flags, and share a cached stMaterial keyed by all
   serialized parameters that affect it. */
// FUNCTION: WIZ8 0x004B8A70
W8MeshMaterial LoadMaterial(const char* bitmap_folder, const W8MaterialRecord& record)
{
    const W8MaterialRecord* source = &record;
    W8MeshMaterial result;
    std::shared_ptr<srTextureIFace>& texture = result.texture;
    srShader* render_flags = &result.render_flags;
    char texture_path[80] = "";
    char material_name[80] = "";
    int texture_index = -1;
    bool has_alpha = false;
    int index;

    render_flags->value = 0x0100a51b; /* packed srShader; TEXTURING set until no texture */
    for (index = 0; index < 4; ++index) {
        if (source->texture_names[index][0] != '\0') {
            if (bitmap_folder[0] == '\0') {
                strcpy(texture_path, source->texture_names[index]);
            } else {
                snprintf(texture_path, sizeof(texture_path), "%s\\%s", bitmap_folder,
                         source->texture_names[index]);
            }
            texture_index = index;
            /* Retail 0x004b8c93: a texture found only in the fourth slot marks
               the material as alpha-carrying before any texture is loaded. */
            if (index == 3) {
                has_alpha = true;
            }
            break;
        }
    }

    if (texture_path[0] == '\0') {
        render_flags->value &= ~srShader::MASK_TEXTURING;
    } else {
        const std::string_view path = texture_path;
        const auto separator = path.find_last_of("\\/");
        const std::size_t file_start = separator == std::string_view::npos ? 0 : separator + 1;
        const std::string texture_folder(path.substr(0, file_start));
        const std::string texture_file(path.substr(file_start));
        const auto dot = texture_file.rfind('.');
        const char* extension =
            texture_file.c_str() + (dot == std::string::npos ? texture_file.size() : dot);

        if (path.size() > 3 && SDL_strncasecmp(extension, ".IFL", 4) == 0) {
            texture =
                LoadAnimatedTexture(texture_folder.c_str(), texture_file.c_str(), source, true);
        } else {
            texture = LoadTextureFromFolder(texture_folder.c_str(), texture_file.c_str(), true);
        }
        if (texture == 0) {
            return result;
        }

        if (auto* file = dynamic_cast<stTextureFile*>(texture.get()); file && file->hasAlpha()) {
            has_alpha = true;
        }
        if (auto* animation = dynamic_cast<stTextureAnim*>(texture.get())) {
            if (animation->Prepare()) {
                has_alpha = true;
            }
            if (source->version > 3 && source->texture_modes[texture_index] > 0.0f) {
                float mode = source->texture_modes[texture_index];
                if (mode <= 1.0f) {
                    animation->trigger_mode = W8_TEXTURE_TRIGGER_RANDOM_FRAME;
                } else {
                    animation->trigger_mode = W8_TEXTURE_TRIGGER_RANDOM_START;
                    mode -= 1.0f;
                }
                animation->probability = mode;
            }
        }
    }

    if (source->opacity < 1.0f || has_alpha) {
        if (texture_index == 0 || texture_index == 1) {
            render_flags->value = (render_flags->value & 0xffffdfbfUL) | 0x40a0;
        } else if (texture_index == 2 || texture_index == 3) {
            render_flags->value = (render_flags->value & 0xffffdc3fUL) | 0x4020;
        }
        render_flags->value &= ~8UL;
    }

    snprintf(material_name, sizeof(material_name),
             "Mt%1.2f%1.2f%1.2f%1.2f%1.2f%1.2f%1.2f%1.2f%1.2f"
             "%1.2f%1.2f%1.2f%1.2f%1.2f%d%c",
             source->ambient.x, source->ambient.y, source->ambient.z, source->diffuse.x,
             source->diffuse.y, source->diffuse.z, source->specular.x, source->specular.y,
             source->specular.z, source->shininess, source->opacity, source->emission,
             source->emission, source->emission, static_cast<int>(source->surface_flags),
             /* Retail passes the string's address for %c, so the name ends in
               that address's low byte. */
             static_cast<int>(
                 reinterpret_cast<w8_ulong_ptr>( // reinterpret-ok: retail formats the pointer
                     texture_path[0] == '\0' ? "F" : "T")));

    std::shared_ptr<stMaterial> concrete = g_materials.find(material_name);
    if (concrete == 0) {
        concrete = std::make_shared<stMaterial>();
        concrete->setName(material_name);
        g_materials.insert(material_name, concrete);
        concrete->parms.specular =
            glm::vec4(source->specular.x, source->specular.y, source->specular.z, 0.0f);
        concrete->dirty = 1;
        concrete->parms.shininess = 1.0f;
        concrete->dirty = 1;

        concrete->parms.diffuse.x = source->diffuse.x;
        concrete->parms.diffuse.y = source->diffuse.y;
        concrete->parms.diffuse.z = source->diffuse.z;
        concrete->parms.diffuse.w = source->opacity == 0.0f ? 0.7f : source->opacity;
        concrete->dirty = 1;
        concrete->setOpacity(source->opacity == 0.0f ? 0.7f : source->opacity);

        if (texture_path[0] == '\0') {
            concrete->parms.ambient =
                glm::vec4(source->diffuse.x, source->diffuse.y, source->diffuse.z, 1.0f);
            concrete->dirty = 1;
            concrete->parms.emissive = glm::vec4(0.0f);
        } else {
            concrete->parms.ambient =
                glm::vec4(source->ambient.x, source->ambient.y, source->ambient.z, 0.0f);
            concrete->dirty = 1;
            concrete->parms.emissive =
                glm::vec4(source->emission, source->emission, source->emission, 1.0f);
        }
        concrete->dirty = 1;
        concrete->m_surface_flags = source->surface_flags;
        if ((source->surface_flags & W8_MATERIAL_NORMAL_TEXCOORD_MASK) != 0) {
            concrete->setMapper(&g_normal_texcoord_mapper);
        }
    }
    result.material = std::move(concrete);
    return result;
}

// STRING: WIZ8 0x0060e0f4
#define DEFAULT_MATERIAL_NAME "Default Material"

// FUNCTION: WIZ8 0x004B9280
W8MeshMaterial CreateDefaultMaterial()
{
    W8MeshMaterial result;
    result.render_flags.value = 0x0100251b;

    std::shared_ptr<stMaterial> concrete = g_materials.find(DEFAULT_MATERIAL_NAME);
    if (concrete == 0) {
        concrete = std::make_shared<stMaterial>();
        concrete->setName(DEFAULT_MATERIAL_NAME);
        g_materials.insert(DEFAULT_MATERIAL_NAME, concrete);

        glm::vec4 color;
        color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        concrete->setAmbient(color);
        concrete->setDiffuse(color);
        color = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        concrete->setSpecular(color);
        concrete->parms.shininess = 1.0f;
        concrete->parms.diffuse.w = 1.0f;
        concrete->parms.emissive = glm::vec4(0.0f);
        concrete->dirty = 1;
        concrete->m_surface_flags = 0;
    }
    result.material = std::move(concrete);
    return result;
}

/* Load a texture by full path: split it into folder and file, then take the
   animated loader for .IFL names and the plain one for everything else. */
// FUNCTION: WIZ8 0x004B9460
std::shared_ptr<srTextureIFace> LoadTextureFromPath(const char* path,
                                                    const W8MaterialRecord* source, bool required)
{
    const std::string_view full_path(path);
    const auto split = full_path.find_last_of("/\\:");
    const std::string folder(full_path.substr(0, split == full_path.npos ? 0 : split + 1));
    const std::string name(full_path.substr(split == full_path.npos ? 0 : split + 1));
    if (name.size() >= 4 && SDL_strcasecmp(name.c_str() + name.size() - 4, ".IFL") == 0)
        return LoadAnimatedTexture(folder.c_str(), name.c_str(), source, required);
    return LoadTextureFromFolder(folder.c_str(), name.c_str(), required);
}

// FUNCTION: WIZ8 0x004B95D0
std::shared_ptr<srTexture> LoadTextureFromFolder(const char* folder, const char* name,
                                                 bool required)
{
    std::string path;

    if (g_current_screen_state.id == W8_SCREEN_PLEASE_WAIT) {
        UpdatePleaseWaitLoadFrame();
    }
    path = folder;
    path += name;
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) path += '.';
    const auto extension = dot == std::string::npos ? path.size() : dot + 1;
    path.resize(extension);

    std::shared_ptr<stTextureFile> texture = g_texture_files.find(name);
    if (texture == 0) {
        path += "tga";
        if (required) {
            texture = std::make_shared<stTextureFile>(path.c_str(), g_texture_cache_enabled);
            if (g_gerd != 0) {
                g_gerd->setTexture(texture.get(), 0);
                g_gerd->setTexture(0, 0);
            }
            if (texture->getTextureFrameHandle() == 0) {
                path.replace(extension, std::string::npos, "jpg");
                texture->setFileName(path.c_str());
                if (g_gerd != 0) {
                    g_gerd->setTexture(texture.get(), 0);
                    g_gerd->setTexture(0, 0);
                }
                if (texture->getTextureFrameHandle() == 0) {
                    ShutdownWithErrorBox(fmt::format("Missing texture file: {}", path));
                }
            }
        } else {
            if (![&]() {
                    const auto status = wiz8::file_status(path);
                    return status && status->info.type == SDL_PATHTYPE_FILE;
                }()) {
                path.replace(extension, std::string::npos, "jpg");
                if (![&]() {
                        const auto status = wiz8::file_status(path);
                        return status && status->info.type == SDL_PATHTYPE_FILE;
                    }()) {
                    ShutdownWithErrorBox(fmt::format("Missing texture file: {}", path));
                }
            }
            texture = std::make_shared<stTextureFile>(path.c_str(), g_texture_cache_enabled);
        }
        texture->setName(name);
        g_texture_files.insert(name, texture);
    }
    return texture;
}

// FUNCTION: WIZ8 0x004B98F0
std::shared_ptr<stTextureAnim> LoadAnimatedTexture(const char* folder, const char* name,
                                                   const W8MaterialRecord* source, bool required)
{
    const auto path = std::string(folder) + name;
    std::unique_ptr<wiz8::File> handle;
    try {
        handle = wiz8::open_file(path);
    } catch (const std::exception&) {
        ShutdownWithErrorBox(fmt::format("Cannot load/find material: {}", path));
        return {};
    }
    std::istringstream frames(handle->read_text());
    auto animation = std::make_shared<stTextureAnim>();
    animation->setName(name);
    g_texture_animations.insert(name, animation);
    for (std::string frame; std::getline(frames, frame);) {
        if (frame.ends_with('\r')) frame.pop_back();
        if (frame.size() <= 2) break;
        if (auto texture = LoadTextureFromFolder(folder, frame.c_str(), required))
            animation->AddTexture(std::move(texture));
    }
    animation->setupDefaultValues();
    if (source != 0) {
        animation->animation_mode = source->animation_mode;
        int frame = source->animation_frame;
        animation->initial_frame = frame;
        animation->frame = frame;
        animation->frame_rate = source->animation_rate;
    }
    return animation;
}

/* getPolyTexture selects the layer and table up front, then returns one smart
   pointer per polygon. Report whether any selected texture is animated. */
// FUNCTION: WIZ8 0x004b9aa0
bool MeshHasAnimatedTexture(srMeshModel* model)
{
    if (model != 0) {
        std::shared_ptr<srTextureIFace>* textures = model->getPolyTexture(0, 0, 0);

        if (textures != 0) {
            int polygon;

            for (polygon = 0; polygon < model->polygon_count; ++polygon) {
                if (dynamic_cast<stTextureAnim*>(textures[polygon].get())) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* The model instance supplies its mesh model. The first
   polygon texture is the shared animation object whose frame is restarted. */
// FUNCTION: WIZ8 0x004b9b00
void SetModelAnimatedTextureFrame(srModelInstance* instance, int frame)
{
    if (instance != 0) {
        srMeshModel* model = static_cast<srMeshModel*>(instance->getModel());

        if (model != 0) {
            std::shared_ptr<srTextureIFace>* textures = model->getPolyTexture(0, 0, 0);

            if (textures != 0) {
                if (auto* animation = dynamic_cast<stTextureAnim*>(textures[0].get())) {
                    animation->SetFrame(frame);
                }
            }
        }
    }
}

// FUNCTION: WIZ8 0x004B9B50
stTextureAnim* GetModelAnimatedTexture(srModelInstance* instance)
{
    if (instance != 0) {
        srMeshModel* model = static_cast<srMeshModel*>(instance->getModel());

        if (model != 0) {
            std::shared_ptr<srTextureIFace>* textures = model->getPolyTexture(0, 0, 0);

            if (textures != 0) {
                return dynamic_cast<stTextureAnim*>(textures[0].get());
            }
        }
    }
    return 0;
}
