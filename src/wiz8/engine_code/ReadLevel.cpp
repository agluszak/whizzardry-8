#include "wiz8/engine_code/ReadLevel.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <glm/common.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Item.h"
#include "ReadMesh.h"
#include "application.h"
#include "compat/compiler.h"
#include "compat/native.h"
#include "glm/ext/matrix_double3x3.hpp"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "layouts/world.h"
#include "local_code/MonsterGroup.h"
#include "monster_actions.h"
#include "named_position.h"
#include "particle_modes.h"
#include "surrender/mesh_submission.h"
#include "surrender/srCamera.h"
#include "surrender/srClipPlane.h"
#include "surrender/srDebug.h"
#include "surrender/srGERD.h"
#include "surrender/srMaterial.h"
#include "surrender/srMath.h"
#include "surrender/srMeshModel.h"
#include "surrender/srModelInstance.h"
#include "surrender/srNode.h"
#include "surrender/srScene.h"
#include "surrender/srShader.h"
#include "texture_animation.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/3dapi.h"
#include "wiz8/engine_code/AnimObj.h"
#include "wiz8/engine_code/Camera.h"
#include "wiz8/engine_code/Environment.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Level.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/quad.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/engine_code/stParticle.h"
#include "wiz8/engine_code/stTextureAnim.h"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/item_spawning.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_screens/AutomapScreen.h"
#include "wiz8/retail_geometry_types.h"
#include "wiz8/retail_text_records.h"

class srTexture;

stLevel::stLevel(srNode* parent) : srNode(static_cast<srNode*>(0))
{
    if (parent != 0) {
        setParent(parent, 1);
    }
    m_render_exclusion_mask = 0;
}

void stLevel::traverse(TraverseInfo& info)
{
    if (testFlag(FLAG_DISABLE) == 0) {
        info.entries.push_back({this, 0});
    }
    if (next_sibling_ != 0) {
        next_sibling_->traverse(info);
    }
}

void stLevel::process(const ProcessInfo& info, e_processType)
{
    srGERD& renderer = *info.renderer;
    const auto transform = scopedWorldSpaceMatrix(renderer);

    w8_ulong old_exclusion_mask = 0;
    if (m_render_exclusion_mask != 0) {
        old_exclusion_mask = renderer.getExclusionMask();
        renderer.setExclusionMask(m_render_exclusion_mask | old_exclusion_mask);
    }

    const srMeshSubmission::AmbientScope lighting(renderer);
    const auto ambient = lighting.ambient();
    const glm::vec3 ambient_color(ambient);
    m_submitted_polygons = 0;

    for (srNode* child = first_child_; child != 0; child = child->getNext()) {
        stModelInstance* instance = static_cast<stModelInstance*>(child);
        stMeshModel* model = static_cast<stMeshModel*>(instance->getModel());
        if (instance->mesh_index >= 0 && instance->testFlag(FLAG_DISABLE) != 0) {
            continue;
        }

        if (!model->isVisible(renderer)) continue;

        srNode* linked_child = child;
        while (model != 0) {
            if (model->getActivePolygonTable(0) != 0) {
                m_submitted_polygons += model->getActivePolygonCount();
            } else {
                m_submitted_polygons += model->polygon_count;
            }

            if (model->vertex_lighting_ready) {
                glm::vec4 dark;
                dark = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
                renderer.setAmbientLight(dark);
                model->SetAmbientColor(ambient_color);
            }

            srMeshModel::TriMesh mesh;
            mesh = model->getTriMesh();
            if (!mesh.active_polygons) {
                const auto active = model->GetActivePolygons(-1, false);
                if (active) {
                    mesh.active_polygons = active;
                }
            }

            {
                const srMeshSubmission::PickScope picking(
                    renderer, (model->flags & W8_MESH_SORTED_RENDERING) != 0);
                model->renderTriMesh(renderer, mesh);
            }

            if (model->vertex_lighting_ready) {
                renderer.setAmbientLight(ambient);
            }
            if (linked_child != 0 && linked_child->testFlag(FLAG_TERMINATE) != 0) {
                break;
            }
            model = model->Next();
            if (linked_child != 0) {
                linked_child = linked_child->getChild();
            }
        }
    }

    if (m_render_exclusion_mask != 0) {
        renderer.setExclusionMask(old_exclusion_mask);
    }
}

namespace {

struct W8LevelItemRecord {
    int positional0;
    glm::vec3 position;
    int positional2;
    int positional3;
    int positional4;
    char item_name[20];
};

#pragma pack(push, 1)
struct W8LevelLightRecord {
    short version;
    unsigned char create;
    bool visible;
    unsigned int flags; /* 2: a parametric light definition follows the name */
    wiz8::retail::Vec3f location;
    wiz8::retail::Vec3f colour;
    float intensity;
    float range;
};
#pragma pack(pop)
static_assert(sizeof(W8LevelLightRecord) == 40);

} // namespace

glm::vec3 CurrentEnvironmentOffset()
{
    return GetWorld() ? GetWorld()->environment_offset : glm::vec3(0.0f);
}

void AssociateWorldLights(W8World* world)
{
    for (const auto& owned : world->transient_lights) {
        stLight* light = owned.get();
        stLightDefinition* definition = light->m_definition.get();

        if (definition != 0 && definition->kind == W8_LIGHT_DEFINITION_PARAMETRIC &&
            (static_cast<stParametricLightDefinition*>(definition)->flags &
             W8_PARAM_LIGHT_FLICKER) != 0) {
            int prop_count = world->plsProps.size();
            int prop_index;

            for (prop_index = 0; prop_index < prop_count; ++prop_index) {
                W8Prop* prop = GetWorldProp(world, prop_index);

                if (!prop->name.empty() &&
                    SDL_strcasecmp(prop->name.c_str(), light->getName().c_str()) == 0) {
                    srModelInstance* instance = prop->ToggleRepAnimationDefault();
                    light->m_prop = prop;
                    GetModelAnimatedTexture(instance)->animation_mode = W8_TEXTURE_ANIM_MANUAL;
                }
            }
        }
    }
}

static unsigned char ReadWorldLights(W8World* world, wiz8::File* hFile)
try {
    short light_count;
    int index;
    unsigned char success;
    success = (hFile->read(&light_count, sizeof(light_count)).bytes ==
               static_cast<std::size_t>(sizeof(light_count)));
    if (!success) {
        Fail("fSuccess", "Couldn't read number of lights");
    }

    for (index = 0; index < light_count; ++index) {
        W8LevelLightRecord record;
        std::unique_ptr<stParametricLightDefinition> definition;
        std::unique_ptr<W8PathAI> path;
        stLight* light = 0;
        char name[20];

        hFile->read_exact(&record, sizeof(record));
        if (record.version >= 2) {
            hFile->read_exact(name, sizeof(name));
            SDL_strupr(name);

            if ((record.flags & 2) != 0) {
                definition = std::make_unique<stParametricLightDefinition>();
                record.create = 1;

                hFile->read_exact(&definition->flags, 4);
                hFile->read_exact(&definition->flicker_chance, 4);
                hFile->read_exact(&definition->color.x, 4);
                hFile->read_exact(&definition->color.y, 4);
                hFile->read_exact(&definition->color.z, 4);
                hFile->read_exact(&definition->color_to.x, 4);
                hFile->read_exact(&definition->color_to.y, 4);
                hFile->read_exact(&definition->color_to.z, 4);
                hFile->read_exact(&definition->intensity, 4);
                hFile->read_exact(&definition->intensity_to, 4);
                hFile->read_exact(&definition->period, 4);
                hFile->read_exact(&definition->rate, 4);
                hFile->read_exact(&definition->path_speed, 4);
                hFile->read_exact(&definition->subcycle_min, 4);
                hFile->read_exact(&definition->subcycle_max, 4);

                if ((definition->flags & W8_PARAM_LIGHT_HAS_PATH) != 0) {
                    path = LoadPathAI(*hFile);
                    if (!path) return false;
                    path->discrete_mode = 1;
                    path->animated = false;
                    path->speed = definition->path_speed;
                }
            }
        }

        if (record.version >= 2 && record.create != 0) {
            if (definition == 0) {
                light = CreateRangedWorldLight(world, name);
            } else {
                light = CreateWorldLight(world, name);
                if (path != 0) {
                    light->path_ai = std::move(path);
                }
                record.intensity = definition->intensity;
                if (definition->intensity_to < definition->intensity) {
                    float swap = definition->intensity;
                    definition->intensity = definition->intensity_to;
                    definition->intensity_to = swap;
                }
                light->m_definition = std::move(definition);
            }
            if (!record.visible) {
                light->setFlag(srNode::FLAG_DISABLE);
            }
            light->setGroupMask(2);
        } else if (record.version < 2 || record.visible) {
            record.visible = true;
            if (record.version < 2) {
                strcpy(name, "Static Point Light");
            }
            light = world->dynamic_scene->adoptChild(CreateLight(nullptr, name));
            if (light == 0) {
                Fail("pstLight");
            }
            light->setGroupMask(1);
        }

        if (light != 0) {
            if (SDL_strncasecmp(light->getName().c_str(), "Sun", 3) == 0) {
                light->diffuse = glm::vec3(0);
                light->ambient = {record.colour.x, record.colour.y, record.colour.z};
                light->setGroupMask(light->getGroupMask() | 4);
                world->environment_lights.push_back(light);
            } else {
                light->diffuse = {record.colour.x, record.colour.y, record.colour.z};
                light->ambient = glm::vec3(0);
            }

            light->specular = glm::vec3(0);
            ConfigureWorldLight(light, record.range * kWorldScale);
            light->intensity = record.intensity;
            light->setLocation(record.location.x * kWorldScale, record.location.y * kWorldScale,
                               record.location.z * kWorldScale);
        }
    }

    return success;
} catch (const std::exception&) {
    return false;
}

unsigned char ReadWorldEnvironment(W8ReadLevelInfo* pInfo, W8World* pWorld)
try {
    /* Retail read these uninitialised when a FileRead chain short-circuited;
       natively the scalars start at zero. */
    glm::vec3 environment_range;
    glm::vec3 white{1.0f};
    glm::vec3 position{0.0f};
    wiz8::retail::Vec3f stored_position{};
    glm::vec3 axis;
    glm::mat3 rotation;
    float intensity = 0.0f;
    float view_distance = 0.0f;
    float angle = 0.0f;
    float distance_scale;
    unsigned char camera_mode = 0;
    unsigned char has_light_colours = 0;
    unsigned char has_environment_colours = 0;
    unsigned char fog_enabled = 0;
    bool success;

    success = (pInfo->hFile->read(&fog_enabled, sizeof(fog_enabled)).bytes ==
               static_cast<std::size_t>(sizeof(fog_enabled))) &&
              (pInfo->hFile->read(&environment_range.x, sizeof(environment_range.x)).bytes ==
               static_cast<std::size_t>(sizeof(environment_range.x))) &&
              (pInfo->hFile->read(&environment_range.y, sizeof(environment_range.y)).bytes ==
               static_cast<std::size_t>(sizeof(environment_range.y))) &&
              (pInfo->hFile->read(&environment_range.z, sizeof(environment_range.z)).bytes ==
               static_cast<std::size_t>(sizeof(environment_range.z))) &&
              (pInfo->hFile->read(&intensity, sizeof(intensity)).bytes ==
               static_cast<std::size_t>(sizeof(intensity))) &&
              (pInfo->hFile->read(&view_distance, sizeof(view_distance)).bytes ==
               static_cast<std::size_t>(sizeof(view_distance))) &&
              (pInfo->hFile->read(&camera_mode, sizeof(camera_mode)).bytes ==
               static_cast<std::size_t>(sizeof(camera_mode)));

    if (camera_mode == 1) {
        success = success && (pInfo->hFile->read(&stored_position, sizeof(stored_position)).bytes ==
                              static_cast<std::size_t>(sizeof(stored_position)));
        position = glm::vec3(stored_position.x, stored_position.y, stored_position.z) * kWorldScale;
        pWorld->camera->setLocation(glm::dvec3(position));
    } else if (camera_mode == 2) {
        success = success &&
                  (pInfo->hFile->read(&stored_position, sizeof(stored_position)).bytes ==
                   static_cast<std::size_t>(sizeof(stored_position))) &&
                  (pInfo->hFile->read(&angle, sizeof(angle)).bytes ==
                   static_cast<std::size_t>(sizeof(angle))) &&
                  (pInfo->hFile->read(&axis.x, sizeof(axis.x)).bytes ==
                   static_cast<std::size_t>(sizeof(axis.x))) &&
                  (pInfo->hFile->read(&axis.y, sizeof(axis.y)).bytes ==
                   static_cast<std::size_t>(sizeof(axis.y))) &&
                  (pInfo->hFile->read(&axis.z, sizeof(axis.z)).bytes ==
                   static_cast<std::size_t>(sizeof(axis.z)));
        position = glm::vec3(stored_position.x, stored_position.y, stored_position.z) * kWorldScale;
        pWorld->camera->setLocation(glm::dvec3(position));

        rotation = glm::mat3(1);
        if (angle != 0.0f) {
            rotation *= glm::mat3_cast(glm::angleAxis(float(std::atan2(sin(angle), cos(angle))),
                                                      normalizedOrZero(glm::vec3(axis))));
        }
        pWorld->initial_camera_rotation = rotation;
        pWorld->camera->setRotation(glm::dmat3(rotation));
    }

    // Absent tables keep the default ramps the world was constructed with.
    const auto read_flag = [&](unsigned char& flag) {
        return pInfo->hFile->read(&flag, sizeof(flag)).bytes == sizeof(flag);
    };
    success = success && read_flag(has_light_colours);
    if (has_light_colours != 0)
        success = success && ReadEnvironmentColours(pInfo->hFile, pWorld->light_colours);
    success = success && read_flag(has_environment_colours);
    if (has_environment_colours != 0)
        success = success && ReadEnvironmentColours(pInfo->hFile, pWorld->environment_colours);

    pWorld->view_distance = view_distance * kWorldScale;
    pWorld->environment_intensity = intensity;
    pWorld->environment_colour = white;
    pWorld->static_scene->setAmbientLight(glm::clamp(white * intensity, 0.0f, 1.0f));
    WorldSetFarClip(pWorld, pWorld->view_distance);
    distance_scale = view_distance < g_octree_cell_scale ? kEnvironmentNearScale : 1.5f;
    WorldSetRenderRange(pWorld, distance_scale * pWorld->view_distance);
    pWorld->environment_range_start = environment_range.x;
    pWorld->environment_range_end = environment_range.y;

    pWorld->fog_enabled = fog_enabled != 0;
    return success;
} catch (const std::exception&) {
    return false;
}

static unsigned char ReadWorldClipPlanes(W8ReadLevelInfo* pInfo, W8World* pWorld)
try {
    std::vector<srClipPlane*> clip_planes;
    glm::vec4 plane;
    struct {
        float x, y, z, w;
    } serialized_position;
    glm::dvec3 position;
    srClipPlane* clip_plane;
    char name[64];
    int count;
    int index;
    unsigned char version;
    unsigned char success;

    if (pInfo == 0) {
        Fail("pInfo");
    }
    if (pInfo->hFile == 0) {
        Fail("pInfo->hFile");
    }
    if (pWorld == 0) {
        Fail("pWorld");
    }

    success = (pInfo->hFile->read(&count, sizeof(count)).bytes ==
               static_cast<std::size_t>(sizeof(count)));
    if (!success) {
        Fail("fSuccess", "Error reading num clipping planes");
        return 0;
    }
    if (count == 0) {
        return 1;
    }

    pInfo->hFile->read(&version, sizeof(version));
    plane = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
    for (index = 0; index < count; ++index) {
        clip_plane = pWorld->static_scene->adoptChild(std::make_unique<srClipPlane>());
        if (clip_plane == 0) {
            Fail("psrClipPlane", "out of memory creating clip plane");
        }

        pInfo->hFile->read(name, sizeof(name));
        pInfo->hFile->read(&serialized_position, sizeof(serialized_position));
        SDL_strupr(name);
        clip_plane->setName(name);
        clip_plane->setClipPlane(plane);

        position =
            glm::dvec3(serialized_position.x * kWorldScale, serialized_position.y * kWorldScale,
                       serialized_position.z * kWorldScale);
        clip_plane->setLocation(position);
        clip_plane->setFlag(srNode::FLAG_GLOBAL);
        clip_plane->setClipType(srClipPlane::CLIP_POSITIONAL_0);
        clip_plane->setFlag(srNode::FLAG_DISABLE);
    }
    return 1;
} catch (const std::exception&) {
    return false;
}

/* Validate a signed count before publishing records from the section. */
static bool ReadRecordCount(wiz8::File& file, int& count)
{
    const bool read =
        file.read(&count, sizeof(count)).bytes == static_cast<std::size_t>(sizeof(count));
    return read && count >= 0 && count < 100000;
}

static unsigned char ReadWorldProps(W8ReadLevelInfo* pInfo, W8World* pWorld,
                                    bool mark_model_instances)
try {
    /* CollectModelInstances appends. The canonical body deliberately keeps
       this one vector across the complete prop loop. */
    std::vector<stModelInstance*> model_instances;
    W8Prop* prop;
    int count;
    int index;
    int collidable_index;
    int model_index;
    unsigned char success = 1;

    collidable_index = 0;
    if (pInfo == 0 || pInfo->hFile == 0 || pWorld == 0) {
        return 0;
    }
    if (!ReadRecordCount(*pInfo->hFile, count)) {
        return 0;
    }
    if (count == 0) {
        return 1;
    }

    for (index = 0; index < count; ++index) {
        auto owner = success ? CreateAndLoadProp(pInfo) : nullptr;
        prop = owner.get();
        if (!prop) {
            success = 0;
        } else {
            success = 1;
            if (pWorld->octree != 0) {
                if (!pWorld->octree->TestPropSunBit(index)) {
                    prop->flags |= W8_PROP_NO_DIRECT_SUN;
                }
                pWorld->octree->AddLoadedProp(prop);
            }
            pWorld->plsProps.push_back(std::move(owner));
            if ((prop->flags & W8_PROP_COLLIDABLE) != 0) {
                const auto prop_bounds = prop->GetBounds();
                if (!prop_bounds) return false;
                pWorld->collidable_props.push_back(prop);
                if (pWorld->octree != 0) {
                    pWorld->octree->AddCollidablePropBounds(collidable_index, *prop_bounds);
                    ++collidable_index;
                }
            }
        }

        if (mark_model_instances && prop != 0) {
            prop->CollectModelInstances(model_instances);
            for (model_index = 0; model_index < static_cast<int>(model_instances.size());
                 ++model_index) {
                stModelInstance* instance = model_instances[model_index];
                if (instance != 0) {
                    instance->render_flags |= stModelInstance::RENDER_NO_PICK;
                }
            }
        }
    }
    if (pWorld->octree != 0) {
        pWorld->octree->TestPropSunBit(-1);
    }
    return success;
} catch (const std::exception&) {
    return false;
}

unsigned char ReadWorldItems(W8ReadLevelInfo* pInfo, W8World* pWorld)
try {
    unsigned char has_trigger;
    int positional_value;
    int index;
    int count;
    W8LevelItemRecord record;
    unsigned char positional_byte;
    unsigned char success;
    Trigger* trigger;
    int item_id;
    W8WorldItem* world_item;
    WorldItemEntity* item;

    if (pInfo == 0 || pInfo->hFile == 0 || pWorld == 0) {
        return 0;
    }
    if (!ReadRecordCount(*pInfo->hFile, count)) {
        return 0;
    }
    if (count == 0) {
        return 1;
    }

    for (index = 0; index < count; ++index) {
        item = 0;
        trigger = 0;
        success = (pInfo->hFile->read(record.item_name, sizeof(record.item_name)).bytes ==
                   static_cast<std::size_t>(sizeof(record.item_name)));
        if (success) {
            wiz8::retail::read_exact(*pInfo->hFile, record.position);
            record.position *= kWorldScale;
            pInfo->hFile->read(&record.positional0, sizeof(int));
            pInfo->hFile->read(&record.positional2, sizeof(int));
            pInfo->hFile->read(&record.positional3, sizeof(int));
            pInfo->hFile->read(&record.positional4, sizeof(int));
            pInfo->hFile->read(&has_trigger, sizeof(has_trigger));
            if (has_trigger != 0) {
                trigger = Trigger::CreateAndLoadLevelTrigger(pInfo->hFile, pInfo->world);
            }
            pInfo->hFile->read(&positional_byte, sizeof(positional_byte));
            pInfo->hFile->read(&positional_byte, sizeof(positional_byte));
            pInfo->hFile->read(&positional_byte, sizeof(positional_byte));
            pInfo->hFile->read(&positional_value, sizeof(positional_value));
            pInfo->hFile->read(&positional_value, sizeof(positional_value));
            pInfo->hFile->read(&positional_value, sizeof(positional_value));
            pInfo->hFile->read(&positional_value, sizeof(positional_value));

            if (record.item_name[0] >= '0' && record.item_name[0] <= '9') {
                item_id = atoi(record.item_name);
            } else {
                item_id = FindItemRecordByName(record.item_name);
            }
            if (item_id >= 0) {
                world_item = AddWorldItem(SpawnItem(item_id, &record.position,
                                                    W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));
                if (world_item != 0) {
                    success = 1;
                    ActivateItem(world_item);
                    item = world_item->p3D;
                }
            }
            if (trigger != 0 && item != 0) {
                trigger->m_bRepType = W8_TRIGGER_REP_ITEM;
                trigger->rep_item = item;
                item->trigger = trigger;
            }
        }
    }
    return success;
} catch (const std::exception&) {
    return false;
}

unsigned char ReadMonsterPaths(W8ReadLevelInfo* info, W8World* world)
try {
    if (!info || !info->hFile || !world) return false;
    auto& file = *info->hFile;
    int count;
    if (!ReadRecordCount(file, count)) return false;
    // Retail options persist when the next record omits the colon.
    std::array<char, 12> options{};
    bool has_options = false;
    for (int index = 0; index < count; ++index) {
        std::array<char, 20> name;
        file.read_exact(name.data(), name.size());
        if (std::find(name.begin(), name.end(), '\0') == name.end()) return false;
        if (auto* separator = std::strchr(name.data(), ':')) {
            has_options = true;
            SDL_strlcpy(options.data(), separator + 1, options.size());
            *separator = '\0';
        }
        int monster_id;
        const auto end = name.data() + std::strlen(name.data());
        const auto parsed = std::from_chars(name.data(), end, monster_id);
        if (parsed.ec != std::errc{} || parsed.ptr != end) return false;
        auto path = LoadPathAI(file);
        if (has_options && (options[2] == '1' || options[3] == '1')) {
            if (!path) return false;
            path->animated = options[2] == '1';
            path->looping = options[3] == '1';
        }
        const glm::vec3 origin(0);
        auto* group = CreateGroup(monster_id, 1, &origin, true, false, true);
        if (!group) return false;
        auto* monster_info = FindMonster(group->MemberLocationId(0));
        ActivateMonster(monster_info, W8_MONSTER_LOAD_ALL_CYCLES);
        auto* monster = monster_info->p3D;
        const glm::vec3 camera_position(world->camera->getLocation());
        monster->SelectLOD(&camera_position);
        monster->SetPathAI(std::move(path));
        if (has_options) {
            if (options[1] == '0' || options[1] == '\0') {
                group->members_active = false;
                monster->rep_active = 0;
            }
            if (options[0] == '0' || options[0] == '\0') continue;
        }
        monster->UpdateRepresentation(world);
    }
    return true;
} catch (const std::exception&) {
    return false;
}

unsigned char ReadWorldCameras(W8ReadLevelInfo* info, W8World* world)
try {
    if (!info || !info->hFile || !world) {
        return false;
    }
    auto* file = info->hFile;
    int count;
    file->read_exact(&count, sizeof(count));
    if (count < 0 || count >= 100000) {
        return false;
    }
    std::vector<std::unique_ptr<W8CameraPath>> pending;
    for (int index = 0; index < count; ++index) {
        auto entry = std::make_unique<W8CameraPath>();
        int positional_0, positional_1;
        unsigned char has_scale;
        float scale = 15.0f;
        file->read_exact(&positional_0, sizeof(positional_0));
        file->read_exact(&positional_1, sizeof(positional_1));
        file->read_exact(&has_scale, sizeof(has_scale));
        file->read_exact(entry->name0, sizeof(entry->name0));
        if (memchr(entry->name0, '\0', sizeof(entry->name0)) == nullptr) {
            return false;
        }
        if (has_scale > 0) {
            file->read_exact(&scale, sizeof(scale));
        }

        entry->path = LoadPathAI(*file);
        if (!entry->path) {
            return false;
        }
        PathAIEnableTimedMode(entry->path.get());
        entry->path->entry_index = index;
        entry->path->speed = scale;
        pending.push_back(std::move(entry));
    }
    world->plsCameras.insert(world->plsCameras.end(), std::make_move_iterator(pending.begin()),
                             std::make_move_iterator(pending.end()));
    return true;
} catch (const std::exception&) {
    return false;
}

unsigned char ReadWorldParticles(W8ReadLevelInfo* pInfo, srNode* pScene,
                                 std::vector<std::unique_ptr<stParticle>>* pParticles)
try {
    if (!pInfo || !pInfo->hFile || !pParticles) return false;
    W8Octree* const octree = pInfo->world ? pInfo->world->octree.get() : nullptr;
    auto& file = *pInfo->hFile;
    std::int32_t count;
    file.read_exact(&count, sizeof(count));
    constexpr std::int64_t smallest_record_bytes = 1 + 0x216;
    if (count < 0 || count > (file.size() - file.tell()) / smallest_record_bytes) return false;
    std::vector<W8LevelParticleRecord> records;
    records.reserve(count);
    for (std::int32_t index = 0; index < count; ++index) {
        W8LevelParticleRecord record{};
        record.attachment_key = -1;
        record.start_frame = record.end_frame = -1;
        std::uint8_t version;
        file.read_exact(&version, sizeof(version));
        std::size_t record_bytes;
        switch (version) {
        case 1:
            record_bytes = 0x216;
            break;
        case 2:
            record_bytes = 0x218;
            break;
        case 3:
            record_bytes = 0x21d;
            break;
        case 4:
            record_bytes = sizeof(record);
            break;
        default:
            return false;
        }
        file.read_exact(&record, record_bytes);
        if (!std::memchr(record.name, '\0', sizeof(record.name))) return false;
        if (record.flutter_mode != 0 &&
            (!std::isfinite(record.flutter_period) || record.flutter_period < 0 ||
             static_cast<double>(record.flutter_period) > std::numeric_limits<unsigned int>::max()))
            return false;
        records.push_back(record);
    }
    std::vector<std::unique_ptr<stParticle>> pending;
    std::vector<stParticle*> octree_particles;
    pending.reserve(records.size());
    for (auto& record : records) {
        stParticle* particle;
        glm::dvec3 axis;
        glm::dvec3 location;
        auto owned = std::make_unique<stParticle>(pScene, record.particle_count);
        particle = owned.get();

        SDL_strupr(record.name);
        particle->setName(record.name);
        axis = glm::dvec3(record.rotation_axis.x, record.rotation_axis.y, record.rotation_axis.z);
        particle->rotate(record.rotation_angle, axis);
        location = glm::dvec3(record.location.x, record.location.y, record.location.z);
        location *= kWorldScale;
        particle->setLocation(location);
        particle->rotateX(1.5707963);

        if (record.bounds_origin.x != 0.0f) {
            particle->replace_when_full = true;
            record.bounds_origin.x = 0.0f;
        }
        if (record.bounds_mode == W8_PARTICLE_BOUNDS_BOX) {
            const auto center =
                glm::vec3(record.bounds_origin.x, record.bounds_origin.y, record.bounds_origin.z) *
                kWorldScale;
            const auto extent =
                glm::vec3(record.bounds_extent.x, record.bounds_extent.y, record.bounds_extent.z) *
                250.0f;
            particle->bounds_mode = W8_PARTICLE_BOUNDS_BOX;
            particle->lifetime_minimum = center - extent;
            particle->lifetime_maximum = center + extent;
        } else if (record.bounds_mode == W8_PARTICLE_BOUNDS_SPHERE && record.bounds_radius > 0.0f) {
            particle->bounds_mode = W8_PARTICLE_BOUNDS_SPHERE;
            particle->bounds_origin =
                glm::vec3(record.bounds_origin.x, record.bounds_origin.y, record.bounds_origin.z) *
                kWorldScale;
            particle->bounds_radius = record.bounds_radius * kWorldScale;
        } else {
            particle->bounds_mode = W8_PARTICLE_BOUNDS_NONE;
        }

        if (record.initially_active == 0) {
            particle->SetActive(false);
        }
        particle->particle_size = record.particle_size;
        particle->expiry_mode =
            record.expiry_mode != 0 ? W8_PARTICLE_EXPIRY_TEXTURE : W8_PARTICLE_EXPIRY_TIMED;
        particle->lifetime_ms = record.lifetime;
        particle->los_check_enabled = record.los_check != 0;
        particle->emission_interval = record.emission_interval < 2 ? 1 : record.emission_interval;
        particle->start_frame = record.start_frame;
        particle->end_frame = record.end_frame;

        if (record.has_acceleration != 0) {
            particle->has_acceleration = true;
            particle->acceleration =
                glm::vec3(record.acceleration.x, record.acceleration.y, record.acceleration.z) *
                kWorldScale;
        }

        if (record.emission_mode == W8_PARTICLE_EMISSION_NONE) {
            particle->emission_mode = W8_PARTICLE_EMISSION_NONE;
        } else if (record.emission_mode == W8_PARTICLE_EMISSION_SINGLE) {
            particle->emission_mode = W8_PARTICLE_EMISSION_SINGLE;
        } else {
            particle->emission_mode = W8_PARTICLE_EMISSION_CATCH_UP;
            particle->emission_minimum = {-record.spread.x * 250.0f, -record.spread.y * 250.0f, 0};
            particle->emission_maximum = {record.spread.x * 250.0f, record.spread.y * 250.0f,
                                          record.spread.z * kWorldScale};
        }

        if (record.direction_mode == W8_PARTICLE_DIRECTION_STATIONARY) {
            particle->direction_mode = W8_PARTICLE_DIRECTION_STATIONARY;
        } else if (record.direction_mode == W8_PARTICLE_DIRECTION_FIXED) {
            glm::mat3 rotation;
            double angle = -1.5707963;

            rotation = glm::mat3(1);
            if (record.rotation_angle != 0.0f) {
                rotation *= glm::mat3_cast(glm::angleAxis(
                    float(std::atan2(sin(record.rotation_angle), cos(record.rotation_angle))),
                    normalizedOrZero(glm::vec3(record.rotation_axis.x, record.rotation_axis.y,
                                               record.rotation_axis.z))));
            }
            rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(1, 0, 0)));

            particle->direction_mode = W8_PARTICLE_DIRECTION_FIXED;
            particle->direction = normalizedOrZero(rotation * glm::vec3(0, 0, -1));
        } else if (record.direction_mode == W8_PARTICLE_DIRECTION_NODE_FORWARD) {
            particle->direction_mode = W8_PARTICLE_DIRECTION_NODE_FORWARD;
        } else if (record.direction_mode == W8_PARTICLE_DIRECTION_CONE) {
            particle->direction_mode = W8_PARTICLE_DIRECTION_CONE;
            particle->cone_yaw = record.direction0 * 0.017453292519943295f;
            particle->cone_pitch = record.direction1 * 0.017453292519943295f;
        } else {
            particle->direction_mode = W8_PARTICLE_DIRECTION_RANDOM;
        }

        if (record.speed_mode == W8_PARTICLE_SPEED_ZERO) {
            particle->speed_mode = W8_PARTICLE_SPEED_ZERO;
        } else if (record.speed_mode == W8_PARTICLE_SPEED_FIXED) {
            particle->speed_mode = W8_PARTICLE_SPEED_FIXED;
            particle->initial_speed = record.initial_speed * kWorldScale;
        } else {
            particle->speed_mode = W8_PARTICLE_SPEED_RANDOM;
            particle->speed_min = record.speed_min * kWorldScale;
            particle->speed_max = record.speed_max * kWorldScale;
        }

        if (record.flutter_mode == 0) {
            particle->SetFlutter(W8_PARTICLE_FLUTTER_NONE);
        } else {
            particle->SetFlutter(W8_PARTICLE_FLUTTER_VELOCITY_SCALED);
            particle->flutter_amplitude = record.flutter_value;
            particle->flutter_period = static_cast<unsigned int>(record.flutter_period);
        }
        if (record.attachment_key >= 0) {
            particle->attachment_key = record.attachment_key;
        }
        particle->requires_sorted_renderer = record.requires_sorted_renderer;
        particle->emission_limit = record.emission_limit;
        particle->release_when_done = false;

        W8MeshMaterial material = LoadMaterial(pInfo->bitmap_folder, record.material);
        particle->SetMaterial(std::move(material.material));
        particle->SetRenderFlags(material.render_flags);
        particle->SetTexture(std::move(material.texture));

        if (strncmp(record.name, "CLOUD", 5) == 0) {
            particle->camera_offset = glm::vec3(particle->getLocation());
            particle->camera_relative = true;
            particle->SetActive(true);
        } else if (octree) {
            octree_particles.push_back(particle);
        }
        pending.push_back(std::move(owned));
    }
    pParticles->reserve(pParticles->size() + pending.size());
    if (octree) octree->AddLoadedParticles(octree_particles);
    pParticles->insert(pParticles->end(), std::make_move_iterator(pending.begin()),
                       std::make_move_iterator(pending.end()));
    return 1;
} catch (const std::exception&) {
    return false;
}

bool ReadNamedPositions(wiz8::File* hFile, std::vector<W8NamedPosition>& named_positions)
try {
    int count;
    int index;
    unsigned char version;

    hFile->read_exact(&count, sizeof(count));
    if (count < 0) return false;
    std::vector<W8NamedPosition> pending;
    for (index = 0; index < count; ++index) {
        W8NamedPosition position;

        hFile->read_exact(&version, sizeof(version));
        if (version != 1) {
            return false;
        }

        hFile->read_exact(position.name, sizeof(position.name));
        if (memchr(position.name, '\0', sizeof(position.name)) == nullptr) {
            return false;
        }
        hFile->read_exact(&position.position.x, sizeof(position.position.x));
        hFile->read_exact(&position.position.y, sizeof(position.position.y));
        hFile->read_exact(&position.position.z, sizeof(position.position.z));
        position.position *= 500.0;
        hFile->read_exact(&position.angle, sizeof(position.angle));
        hFile->read_exact(&position.direction.x, sizeof(position.direction.x));
        hFile->read_exact(&position.direction.y, sizeof(position.direction.y));
        hFile->read_exact(&position.direction.z, sizeof(position.direction.z));
        pending.push_back(position);
    }
    named_positions.insert(named_positions.end(), pending.begin(), pending.end());
    return true;
} catch (const std::exception&) {
    return false;
}

#define CHECK_PVL_OFFSET(message)                                                                  \
    if (world->octree != 0 && (!(handle->read(&section_end, sizeof(section_end)).bytes ==          \
                                 static_cast<std::size_t>(sizeof(section_end))) ||                 \
                               section_end != -1)) {                                               \
        sprintf(error_message, "%s\nTry deleting .PVL file and reloading.", message);              \
        ShutdownWithErrorBox(error_message);                                                       \
    }

unsigned char ReadLevel(W8World* world, wiz8::File* handle, bool use_octree,
                        const std::filesystem::path& texture_directory)
try {
    W8ReadLevelInfo info;
    std::unique_ptr<stModelInstance> level_mesh;
    glm::vec3 environment_offset;
    char error_message[512];
    int section_end;
    int section_count;
    int camera_mode;
    int index;
    unsigned int prop_count;
    unsigned char success;

    if (world == nullptr || world->static_scene == nullptr || handle == nullptr) {
        return false;
    }

    level_mesh = nullptr;
    world->level->eraseOwnedChild(std::exchange(world->update_mesh_source, nullptr));
    // Each non-null entry is a distinct scene-owned root instance. Replacing
    // the level must unlink all old roots, not just the first table entry.
    for (auto*& instance : world->psrMeshes) {
        world->level->eraseOwnedChild(std::exchange(instance, nullptr));
    }
    if (world->octree == nullptr) {
        world->psrMeshes.clear();
    }

    info.world = world;
    info.hFile = handle;
    info.bitmap_folder = texture_directory;
    w8_get_ticks();

    if (world->psrMeshes.empty() || world->octree == 0) {
        level_mesh = ReadSingleLevelMesh(*handle, texture_directory);
        if (!level_mesh) {
            return 0;
        }
        level_mesh->setName("ReadLevel");
        SetModelInstanceChainExclusionMask(level_mesh.get(), 1);
        world->update_mesh_source = world->level->adoptChild(std::move(level_mesh), 1);
    } else {
        auto meshes =
            ReadMultipleLevelMeshes(*handle, world->octree->GetMeshCount(), texture_directory);
        if (meshes.empty()) {
            ShutdownWithErrorBox("ReadLevel: Error reading multi-meshes.");
        }
        for (unsigned int mesh_index = 0; mesh_index < world->octree->m_meshCount; ++mesh_index) {
            if (meshes[mesh_index]) {
                SetModelInstanceChainExclusionMask(meshes[mesh_index].get(), 1);
                world->psrMeshes[mesh_index] =
                    world->level->adoptChild(std::move(meshes[mesh_index]), 1);
            }
        }
    }

    success = ReadWorldLights(world, handle);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after lights.");
    success = success && ReadMonsterPaths(&info, world);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after monsters.");
    success = success && ReadWorldItems(&info, world);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after items.");

    if (!success || info.hFile == 0 || !ReadRecordCount(*info.hFile, section_count)) {
        success = 0;
    }
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after missiles.");
    success = success && ReadWorldProps(&info, world, false);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after props.");
    success = success && ReadWorldProps(&info, world, true);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after bitmaps.");
    success = success && ReadWorldCameras(&info, world);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after cameras.");
    AssociateWorldLights(world);
    if (!success) {
        return 0;
    }

    info.hFile->read(&section_count, sizeof(section_count));
    if (section_count == 0) {
        WorldSetFarClip(world, 42500.0f);
        WorldSetRenderRange(world, 37500.0f);
    } else {
        success = ReadWorldEnvironment(&info, world);
    }
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after fog options.");

    if (success && info.hFile != 0) {
        info.hFile->read(&section_count, sizeof(section_count));
        if (section_count != 0) {
            for (index = 0; index < section_count; ++index) {
                Trigger::CreateAndLoadLevelTrigger(info.hFile, world);
            }
            /* Retail 0x004bb565 integrates whenever the world has game data;
               IntegrateTriggers returns at once when the octree path took the
               triggers (no trigger vertices were banked). */
            if (world->game_data != 0) {
                world->game_data->IntegrateTriggers();
            }
        }
    }
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after triggers.");

    UpdateWorldProps(world);
    info.hFile->read(&camera_mode, sizeof(camera_mode));
    world->initial_camera_sway = camera_mode != 0;
    if (world->octree == 0 && world->level_id >= 0) {
        FinalizeWorldScenes(world->static_scene.get(), world->dynamic_scene);
    }

    success = ReadWorldClipPlanes(&info, world);
    if (!success) {
        Fail("fSuccess");
    }
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after Clipping Planes.");

    if (use_octree) {
        srMeshModel* model = static_cast<srMeshModel*>(world->update_mesh_source->getModel());
        const auto [minimum, maximum] = model->getBounds().box;
        world->quads = BuildWorldQuad(world->update_mesh_source, minimum, maximum);
    }
    FinalizeStaticScene(*world);

    if (!success || handle->read(&environment_offset, sizeof(environment_offset)).bytes !=
                        sizeof(environment_offset)) {
        success = 0;
    } else {
        success = ReadWorldParticles(&info, world->dynamic_scene, &world->particles);
    }
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after particles.");
    success = success && ReadNamedPositions(info.hFile, world->named_positions);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after named position points.");
    success = success && ReadAutomapNodes(handle, world->automap);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after automap nodes.");

    world->environment_offset = environment_offset;

    prop_count = world->plsProps.size();
    for (index = 0; index < static_cast<int>(prop_count); ++index) {
        W8Prop* prop = GetWorldProp(world, index);
        W8AnimObj* animation = prop->Animation();

        if ((prop->flags & W8_PROP_NO_DIRECT_SUN) != 0) {
            unsigned int animation_count = AnimObjListCount(animation, 2);
            for (unsigned int animation_index = 0; animation_index < animation_count;
                 ++animation_index) {
                srModelInstance* instance = prop->ToggleRepAnimation(animation_index);
                stMeshModel* mesh = static_cast<stMeshModel*>(instance->getModel());

                for (; mesh != 0; mesh = mesh->Next()) {
                    if (!animation->path_lists) {
                        mesh->GetVertexSunlight(true);
                        SetModelInstanceChainExclusionMask(instance, 5);
                    } else if (animation->path_lists == 1) {
                        SetModelInstanceChainExclusionMask(instance, 4);
                    }

                    auto* material = mesh->getMaterial(0, srMeshModel::SIDE_FRONT);
                    if (material != 0) {
                        auto copy = std::make_shared<srMaterial>(*material);
                        copy->setName("Unsunlit Prop Material");
                        copy->parms.ambient = glm::vec4(0.0f);
                        copy->dirty = 1;
                        copy->parms.emissive += glm::vec4(environment_offset, 0);
                        copy->dirty = 1;
                        mesh->setMaterial(std::move(copy), 0, srMeshModel::SIDE_FRONT);
                    }
                }
            }
        }
    }
    if (world->octree != 0) {
        world->octree->m_fAccumulating = false;
    }
    return success;
} catch (const std::exception&) {
    return false;
}

#undef CHECK_PVL_OFFSET
