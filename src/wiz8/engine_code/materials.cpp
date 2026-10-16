#include "wiz8/engine_code/materials.h"

#include <algorithm>
#include <array>
#include <cctype>
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
#include "asset_paths.h"
#include "compat/compiler.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "himage.h"
#include "input.h"
#include "surrender/srGERD.h"
#include "surrender/srMaterial.h"
#include "surrender/srMeshModel.h"
#include "surrender/srModelInstance.h"
#include "surrender/srTexture.h"
#include "surrender/srVertexPipe.h"
#include "wiz8/application.h"
#include "wiz8/engine_code/3d.h"
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
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/PleaseWaitScreen.h"
#include "wiz8/packed_bits.h"
#include "wiz8/resource_cache.h"
#include "wiz8/unicode.h"

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
// GLOBAL: WIZ8 0x0060AC88
static int g_option_max_leaf_count = 20000;
// GLOBAL: WIZ8 0x0060AC8C
static bool g_status_scroll = true;
// GLOBAL: WIZ8 0x0060AC8D
static bool g_status_buffers_freed = true;

// GLOBAL: WIZ8 0x0065BAB0
// GLOBAL: WIZ8 0x0065BACC
// GLOBAL: WIZ8 0x0065BADC
static std::string g_log_path;
// GLOBAL: WIZ8 0x0065BCDC
// GLOBAL: WIZ8 0x0065BCE0
// GLOBAL: WIZ8 0x0065BCE4
// GLOBAL: WIZ8 0x0065BCE8
static std::string g_status_lines[6];
// GLOBAL: WIZ8 0x0065BD0C
static int g_oct_node_count;
// GLOBAL: WIZ8 0x0065BD10
static int g_oct_max_objects;
// GLOBAL: WIZ8 0x0065BD14
// GLOBAL: WIZ8 0x0065BD18
// GLOBAL: WIZ8 0x0065BD1C
// GLOBAL: WIZ8 0x0065BD2D
static bool g_option_rename_alphas;
// GLOBAL: WIZ8 0x0065BD30
static float g_option_auto_region_size;
// GLOBAL: WIZ8 0x0065BD34
// GLOBAL: WIZ8 0x0065BD38
// GLOBAL: WIZ8 0x0065BD3C
/* Material slots whose texture file is missing. */
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
// GLOBAL: WIZ8 0x005ECBB8
// GLOBAL: WIZ8 0x005ECBBC

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
                    g_log_file =
                        wiz8::open_file(wiz8::user_path(g_log_path), wiz8::OpenMode::replace);
                    return;
                }
                if (!message.empty()) {
                    g_log_path = message;
                    if (g_option_logging) {
                        g_log_file =
                            wiz8::open_file(wiz8::user_path(message), wiz8::OpenMode::replace);
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
    // Build logging also runs without an initialized video window.
    if (!ghWindow) return;
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

/* Duplicates build vertices whose polygons disagree on material so every
   polygon corner can point at a vertex carrying that polygon's material and
   uv, then repacks the vertex array and repoints the corners. */
// FUNCTION: WIZ8 0x00495860
bool W8OctPreTreeGeometry::splitVerticesByMaterial()
{
    // Keep indices, rather than temporary pointers, until the complete table
    // is allocated. Material runs retain first-face ordering and UV selection.
    std::vector<W8OctPreTreeVertex> splits(1);
    std::vector<geometryRepack::CornerIndices> corners(m_polygons.size());
    for (w8_ulong source = 1; source < m_vertices.size(); ++source) {
        const auto& original = m_vertices[source];
        std::unordered_map<w8_ulong, w8_ulong> material_splits;
        for (const auto face : original.face_indices) {
            auto& polygon = m_polygons[face];
            const auto [entry, fresh] =
                material_splits.try_emplace(polygon.material, static_cast<w8_ulong>(splits.size()));
            if (fresh) {
                auto record = original;
                record.m_vertex_index = entry->second;
                record.m_visited = true;
                record.m_material = polygon.material;
                record.m_kind = polygon.kind;
                splits.push_back(std::move(record));
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
    geometryRepack::repointCorners(m_polygons, corners, splits.data());
    m_vertices = std::move(splits);
    return true;
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
    std::shared_ptr<srTexture>& texture = result.texture;
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
std::shared_ptr<srTexture> LoadTextureFromPath(const char* path, const W8MaterialRecord* source,
                                               bool required)
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
            if (texture->resolve() == 0) {
                path.replace(extension, std::string::npos, "jpg");
                texture->setFileName(path.c_str());
                if (g_gerd != 0) {
                    g_gerd->setTexture(texture.get(), 0);
                    g_gerd->setTexture(0, 0);
                }
                if (texture->resolve() == 0) {
                    ShutdownWithErrorBox(fmt::format("Missing texture file: {}", path));
                }
            }
        } else {
            if (![&]() {
                    const auto status = wiz8::file_status(wiz8::asset_path(path));
                    return status && status->info.type == SDL_PATHTYPE_FILE;
                }()) {
                path.replace(extension, std::string::npos, "jpg");
                if (![&]() {
                        const auto status = wiz8::file_status(wiz8::asset_path(path));
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
        handle = wiz8::open_file(wiz8::asset_path(path));
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
        std::shared_ptr<srTexture>* textures = model->getPolyTexture(0, 0, 0);

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
            std::shared_ptr<srTexture>* textures = model->getPolyTexture(0, 0, 0);

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
            std::shared_ptr<srTexture>* textures = model->getPolyTexture(0, 0, 0);

            if (textures != 0) {
                return dynamic_cast<stTextureAnim*>(textures[0].get());
            }
        }
    }
    return 0;
}
