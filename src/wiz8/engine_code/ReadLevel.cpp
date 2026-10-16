#include "wiz8/engine_code/ReadLevel.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Item.h"
#include "LevelFile.h"
#include "ReadMesh.h"
#include "application.h"
#include "compat/compiler.h"
#include "compat/native.h"
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
#include "surrender/srGERD.h"
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
#include "wiz8/float_constants.h"
#include "wiz8/item_spawning.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_screens/AutomapScreen.h"
#include "wiz8/retail_geometry_types.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/sr_api.h"

class srTextureIFace;

#define READ_LEVEL_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\ReadLevel.cpp"

// FUNCTION: WIZ8 0x004B9C00
stLevel::stLevel(srNode* parent) : srNode(static_cast<srNode*>(0))
{
    if (parent != 0) {
        setParent(parent, 1);
    }
    m_render_exclusion_mask = 0;
}

// FUNCTION: WIZ8 0x004BA0E0
void stLevel::traverse(TraverseInfo& info)
{
    if (testFlag(FLAG_DISABLE) == 0) {
        info.entries.push_back({this, 0});
    }
    if (next_sibling_ != 0) {
        next_sibling_->traverse(info);
    }
}

// FUNCTION: WIZ8 0x004B9DD0
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
            if (g_render_untextured) {
                mesh.shaders[0].value &= 0xffff7fff;
                mesh.poly_shaders[0] = 0;
            }
            std::shared_ptr<srTextureIFace>*(*poly_textures)[2] = mesh.poly_textures;
            if (poly_textures != 0 && !mesh.active_polygons) {
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
    unsigned int flags;
    wiz8::retail::Vec3f location;
    wiz8::retail::Vec3f colour;
    float intensity;
    float range;
};
#pragma pack(pop)
static_assert(sizeof(W8LevelLightRecord) == 40);

} // namespace

// GLOBAL: WIZ8 0x005ec0b0
const float g_environment_near_scale = 2.0f;
// GLOBAL: WIZ8 0x00659cd0
glm::vec3 g_environment_offset{0};

// FUNCTION: WIZ8 0x004BC060
void AssociateWorldLights(W8World* world)
{
    int light_index;

    for (light_index = 0; light_index < static_cast<int>(world->lights_to_update.size());
         ++light_index) {
        stLight* light = world->lights_to_update[light_index];
        stLightDefinition* definition = light->m_definition;

        if (definition != 0 && definition->kind == W8_LIGHT_DEFINITION_PARAMETRIC &&
            (static_cast<stParametricLightDefinition*>(definition)->flags &
             W8_PARAM_LIGHT_FLICKER) != 0) {
            int prop_count = world->plsProps.size();
            int prop_index;

            for (prop_index = 0; prop_index < prop_count; ++prop_index) {
                W8Prop* prop = GetWorldProp(world, prop_index);

                if (prop->m_name != 0 &&
                    SDL_strcasecmp(prop->m_name, light->getName().c_str()) == 0) {
                    srModelInstance* instance = prop->ToggleRepAnimationDefault();
                    light->m_prop = prop;
                    GetModelAnimatedTexture(instance)->animation_mode = W8_TEXTURE_ANIM_MANUAL;
                }
            }
        }
    }
}

// FUNCTION: WIZ8 0x004BBAD0
static unsigned char ReadWorldLights(W8World* world, wiz8::File* hFile)
try {
    short light_count;
    int index;
    unsigned char success;
    success = (hFile->read(&light_count, sizeof(light_count)).bytes ==
               static_cast<std::size_t>(sizeof(light_count)));
    if (!success) {
        srAssertFail("fSuccess", READ_LEVEL_CPP, 486, "Couldn't read number of lights");
    }

    for (index = 0; index < light_count; ++index) {
        W8LevelLightRecord record;
        stParametricLightDefinition* definition = 0;
        W8PathAI* path = 0;
        stLight* light = 0;
        char name[20];

        hFile->read_exact(&record, sizeof(record));
        if (record.version >= 2) {
            hFile->read_exact(name, sizeof(name));
            SDL_strupr(name);

            if ((record.flags & W8_LEVEL_LIGHT_HAS_DEFINITION) != 0) {
                definition = new stParametricLightDefinition;
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
                    success = LoadPathAI(&path, hFile);
                    if (!success) {
                        srAssertFail("fSuccess", READ_LEVEL_CPP, 532, 0);
                    }
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
                light->m_definition = definition;
                world->lights_to_update.push_back(light);
                if (path != 0) {
                    light->path_ai = path;
                }
                record.intensity = definition->intensity;
                if (definition->intensity_to < definition->intensity) {
                    float swap = definition->intensity;
                    definition->intensity = definition->intensity_to;
                    definition->intensity_to = swap;
                }
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
                srAssertFail("pstLight", READ_LEVEL_CPP, 593, 0);
            }
            light->setGroupMask(1);
        } else {
            delete definition;
        }

        if (light != 0) {
            if (SDL_strncasecmp(light->getName().c_str(), "Sun", 3) == 0) {
                light->diffuse = glm::vec3(0);
                light->ambient = {record.colour.x, record.colour.y, record.colour.z};
                light->setGroupMask(light->getGroupMask() | 4);
                AddEnvironmentLight(light);
            } else {
                light->diffuse = {record.colour.x, record.colour.y, record.colour.z};
                light->ambient = glm::vec3(0);
            }

            light->specular = glm::vec3(0);
            ConfigureWorldLight(light, record.range * g_world_scale);
            light->intensity = record.intensity;
            light->setLocation(record.location.x * g_world_scale, record.location.y * g_world_scale,
                               record.location.z * g_world_scale);
        }
    }

    return success;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x004BC9D0
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
        position =
            glm::vec3(stored_position.x, stored_position.y, stored_position.z) * g_world_scale;
        SetWorldScenePosition(pWorld, &position);
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
        position =
            glm::vec3(stored_position.x, stored_position.y, stored_position.z) * g_world_scale;
        SetWorldScenePosition(GetWorld(), &position);

        rotation = glm::mat3(1);
        if (angle != 0.0f) {
            rotation *= glm::mat3_cast(glm::angleAxis(float(std::atan2(sin(angle), cos(angle))),
                                                      normalizedOrZero(glm::vec3(axis))));
        }
        ApplyCameraRotation(&rotation);
    }

    success = success && (pInfo->hFile->read(&has_light_colours, sizeof(has_light_colours)).bytes ==
                          static_cast<std::size_t>(sizeof(has_light_colours)));
    if (has_light_colours != 0) {
        ReadLightColourTable(pInfo->hFile);
    } else {
        BuildLightColourRamp();
    }

    success =
        success &&
        (pInfo->hFile->read(&has_environment_colours, sizeof(has_environment_colours)).bytes ==
         static_cast<std::size_t>(sizeof(has_environment_colours)));
    if (has_environment_colours != 0) {
        ReadEnvironmentColourTable(pInfo->hFile);
    } else {
        BuildEnvironmentColourRamp();
    }

    g_environment_offset = glm::vec3(0);
    pWorld->view_distance = view_distance * g_world_scale;
    ApplyEnvironmentColour(pWorld, intensity, &white);
    WorldSetFarClip(pWorld, pWorld->view_distance);
    distance_scale =
        view_distance < g_octree_cell_scale ? g_environment_near_scale : g_float_one_and_a_half;
    WorldSetRenderRange(pWorld, distance_scale * pWorld->view_distance);
    pWorld->environment_range_start = environment_range.x;
    pWorld->environment_range_end = environment_range.y;
    pWorld->environment_range_blue = environment_range.z;

    if (fog_enabled == 0) {
        SetFogEnabled(false);
    } else {
        SetFogEnabled(true);
        UpdateEnvironmentLight();
    }
    return success;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x004BCE20
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
        srAssertFail("pInfo", READ_LEVEL_CPP, 0x59b, 0);
    }
    if (pInfo->hFile == 0) {
        srAssertFail("pInfo->hFile", READ_LEVEL_CPP, 0x59c, 0);
    }
    if (pWorld == 0) {
        srAssertFail("pWorld", READ_LEVEL_CPP, 0x59d, 0);
    }

    success = (pInfo->hFile->read(&count, sizeof(count)).bytes ==
               static_cast<std::size_t>(sizeof(count)));
    if (!success) {
        srAssertFail("fSuccess", READ_LEVEL_CPP, 0x5a2, "Error reading num clipping planes");
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
            srAssertFail("psrClipPlane", READ_LEVEL_CPP, 0x5ae,
                         "out of memory creating clip plane");
        }

        pInfo->hFile->read(name, sizeof(name));
        pInfo->hFile->read(&serialized_position, sizeof(serialized_position));
        SDL_strupr(name);
        clip_plane->setName(name);
        clip_plane->setClipPlane(plane);

        position =
            glm::dvec3(serialized_position.x * g_world_scale, serialized_position.y * g_world_scale,
                       serialized_position.z * g_world_scale);
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

// FUNCTION: WIZ8 0x004BC5E0
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
        prop = 0;
        if (!success || !CreateAndLoadProp(pInfo, &prop)) {
            success = 0;
        } else {
            success = 1;
            if (g_octree != 0) {
                if (!g_octree->TestPropSunBit(index)) {
                    prop->flags |= W8_PROP_NO_DIRECT_SUN;
                }
                g_octree->AddLoadedProp(prop);
            }
            pWorld->plsProps.push_back(prop);
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
    if (g_octree != 0) {
        g_octree->TestPropSunBit(-1);
    }
    return success;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x004BC380
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
    W8Item* item;

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
            record.position *= g_world_scale;
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

// FUNCTION: WIZ8 0x004BC140
unsigned char ReadMonsterPaths(W8ReadLevelInfo* pInfo, W8World* pWorld)
try {
    int count;
    int index;
    unsigned char success = 1;
    bool has_options;
    bool update_representation;
    bool active;
    char monster_name[20];
    char options[12];
    char* separator;
    glm::vec3 origin;
    glm::vec3 camera_position;
    W8MonsterGroup* group;
    int monster_id;
    int location_id;
    W8MonsterInfo* monster_info;
    W8Monster* monster;
    W8PathAI* path;

    /* Canonical 0x004BC140 initializes this once, not once per record. A
       colon-free record after an option-bearing one therefore reuses options. */
    has_options = false;
    if (pInfo == 0 || pInfo->hFile == 0 || pWorld == 0) {
        return 0;
    }
    if (!ReadRecordCount(*pInfo->hFile, count)) {
        return 0;
    }
    if (count == 0) {
        return 1;
    }

    origin = glm::vec3(0);
    for (index = 0; index < count; ++index) {
        update_representation = true;
        active = true;
        success = success && (pInfo->hFile->read(monster_name, sizeof(monster_name)).bytes ==
                              static_cast<std::size_t>(sizeof(monster_name)));
        separator = strchr(monster_name, ':');
        if (separator != 0) {
            has_options = true;
            strncpy(options, separator + 1, sizeof(options));
            *separator = '\0';
        }

        monster_id = atoi(monster_name);
        group = CreateGroup(monster_id, 1, &origin, true, false, true);
        if (group == 0) {
            continue;
        }
        location_id = group->MemberLocationId(0);
        monster_info = FindMonster(location_id);
        ActivateMonster(monster_info, W8_MONSTER_LOAD_ALL_CYCLES);
        monster = monster_info->p3D;

        {
            glm::dvec3 camera_location = pWorld->camera->getLocation();
            camera_position = glm::vec3(camera_location);
        }
        monster->SelectLOD(&camera_position);

        if (LoadPathAI(&path, pInfo->hFile)) {
            monster->SetPathAI(path);
        }
        /* The image also leaves the option-controlled path calls outside the
           successful-load branch. Preserve that behavior rather than adding
           a speculative null guard. */
        if (has_options) {
            if (options[0] == '0' || options[0] == '\0') {
                update_representation = false;
            }
            if (options[1] == '0' || options[1] == '\0') {
                active = false;
            }
            if (options[2] == '1') {
                path->animated = true;
            }
            if (options[3] == '1') {
                path->looping = true;
            }
            if (!active) {
                group->members_active = false;
                monster->m_pRep->active = 0;
            }
            if (!update_representation) {
                continue;
            }
        }
        monster->UpdateRepresentation(pWorld);
    }
    return success;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x004BC850
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

        // LoadPathAI publishes only on success. Adopt its result immediately;
        // empty paths are invalid for cameras, which always use a live path.
        W8PathAI* path = nullptr;
        if (!LoadPathAI(&path, file)) {
            return false;
        }
        entry->path.reset(path);
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

// FUNCTION: WIZ8 0x004BD0D0
unsigned char ReadWorldParticles(W8ReadLevelInfo* pInfo, srNode* pScene,
                                 std::vector<std::unique_ptr<stParticle>>* pParticles)
try {
    W8LevelParticleRecord record;
    int count;
    int index;

    pInfo->hFile->read(&count, sizeof(count));
    for (index = 0; index < count; ++index) {
        unsigned char version;
        stParticle* particle;
        glm::dvec3 axis;
        glm::dvec3 location;

        pInfo->hFile->read(&version, sizeof(version));
        if (version == 4) {
            pInfo->hFile->read(&record, sizeof(record));
        } else if (version == 3) {
            pInfo->hFile->read(&record, 0x21d);
            record.start_frame = -1;
            record.end_frame = -1;
        } else if (version == 2) {
            pInfo->hFile->read(&record, 0x218);
            record.emission_limit = 0;
            record.requires_sorted_renderer = 0;
            record.start_frame = -1;
            record.end_frame = -1;
        } else if (version == 1) {
            pInfo->hFile->read(&record, 0x216);
            record.attachment_key = -1;
            record.emission_limit = 0;
            record.requires_sorted_renderer = 0;
            record.start_frame = -1;
            record.end_frame = -1;
        } else {
            srAssertFail("0", READ_LEVEL_CPP, 0x5fe, "Unknown particle structure version");
        }

        auto owned = std::make_unique<stParticle>(pScene, record.particle_count);
        particle = owned.get();

        SDL_strupr(record.name);
        particle->setName(record.name);
        axis = glm::dvec3(record.rotation_axis.x, record.rotation_axis.y, record.rotation_axis.z);
        particle->rotate(record.rotation_angle, axis);
        location = glm::dvec3(record.location.x, record.location.y, record.location.z);
        location *= g_world_scale;
        particle->setLocation(location);
        particle->rotateX(1.5707963);

        if (record.bounds_origin.x != 0.0f) {
            particle->replace_when_full = true;
            record.bounds_origin.x = 0.0f;
        }
        if (record.bounds_mode == W8_PARTICLE_BOUNDS_BOX) {
            const auto center =
                glm::vec3(record.bounds_origin.x, record.bounds_origin.y, record.bounds_origin.z) *
                g_world_scale;
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
                g_world_scale;
            particle->bounds_radius = record.bounds_radius * g_world_scale;
        } else {
            particle->bounds_mode = W8_PARTICLE_BOUNDS_NONE;
        }

        if (record.initially_active == 0) {
            particle->SetActive(0);
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
            particle->has_acceleration = 1;
            particle->acceleration =
                glm::vec3(record.acceleration.x, record.acceleration.y, record.acceleration.z) *
                g_world_scale;
        }

        if (record.emission_mode == W8_PARTICLE_EMISSION_NONE) {
            particle->emission_mode = W8_PARTICLE_EMISSION_NONE;
        } else if (record.emission_mode == W8_PARTICLE_EMISSION_SINGLE) {
            particle->emission_mode = W8_PARTICLE_EMISSION_SINGLE;
        } else {
            particle->emission_mode = W8_PARTICLE_EMISSION_CATCH_UP;
            particle->emission_minimum = {-record.spread.x * 250.0f, -record.spread.y * 250.0f, 0};
            particle->emission_maximum = {record.spread.x * 250.0f, record.spread.y * 250.0f,
                                          record.spread.z * g_world_scale};
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
            particle->initial_speed = record.initial_speed * g_world_scale;
        } else {
            particle->speed_mode = W8_PARTICLE_SPEED_RANDOM;
            particle->speed_min = record.speed_min * g_world_scale;
            particle->speed_max = record.speed_max * g_world_scale;
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
            particle->camera_relative = 1;
            particle->SetActive(1);
        } else if (g_octree != 0) {
            g_octree->AddLoadedParticle(particle);
        }
        pParticles->push_back(std::move(owned));
    }
    return 1;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x004BDC90
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

// FUNCTION: WIZ8 0x004BAFF0
unsigned char ReadLevel(W8World* world, wiz8::File* handle, bool use_octree,
                        const char* bitmap_folder)
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
    info.bitmap_folder = bitmap_folder;
    w8_get_ticks();

    if (world->psrMeshes.empty() || world->octree == 0) {
        level_mesh = ReadSingleLevelMesh(&info, nullptr, true);
        if (!level_mesh) {
            return 0;
        }
        level_mesh->setName("ReadLevel");
        SetModelInstanceChainExclusionMask(level_mesh.get(), 1);
        world->update_mesh_source = world->level->adoptChild(std::move(level_mesh), 1);
    } else {
        auto meshes = ReadMultipleLevelMeshes(&info, world->octree->GetMeshCount(), nullptr);
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
        if (!IsSkyEnabled()) {
            DisableSky();
        } else {
            EnableSky();
        }
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
    SetCameraSwayMode(world->camera, camera_mode == 0 ? -1 : 1);
    if (world->octree == 0 && world != g_secondary_world) {
        FinalizeWorldScenes(world->static_scene.get(), world->dynamic_scene);
    }

    success = ReadWorldClipPlanes(&info, world);
    if (!success) {
        srAssertFail("fSuccess", READ_LEVEL_CPP, 0x15c, 0);
    }
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after Clipping Planes.");

    if (use_octree) {
        srMeshModel* model = static_cast<srMeshModel*>(world->update_mesh_source->getModel());
        const auto [minimum, maximum] = model->getBounds().box;
        world->quads = BuildWorldQuad(world->update_mesh_source, minimum, maximum);
    }
    RefreshEnvironment();
    FinalizeStaticScene(world->static_scene.get());

    if (!success ||
        !(handle->read(&environment_offset.x, sizeof(environment_offset.x)).bytes ==
          static_cast<std::size_t>(sizeof(environment_offset.x))) ||
        !(handle->read(&environment_offset.y, sizeof(environment_offset.y)).bytes ==
          static_cast<std::size_t>(sizeof(environment_offset.y))) ||
        !(handle->read(&environment_offset.z, sizeof(environment_offset.z)).bytes ==
          static_cast<std::size_t>(sizeof(environment_offset.z)))) {
        success = 0;
    } else {
        success = ReadWorldParticles(&info, world->dynamic_scene, &world->particles);
    }
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after particles.");
    success = success && ReadNamedPositions(info.hFile, world->named_positions);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after named position points.");
    success = success && ReadAutomapNodes(handle);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after automap nodes.");

    g_environment_offset = environment_offset;
    prop_count = world->plsProps.size();
    for (index = 0; index < static_cast<int>(prop_count); ++index) {
        W8Prop* prop = GetWorldProp(world, index);
        W8AnimObj* animation = prop->Rep()->animation;

        if ((prop->flags & W8_PROP_NO_DIRECT_SUN) != 0) {
            unsigned int animation_count = AnimObjListCount(animation, 2);
            for (unsigned int animation_index = 0; animation_index < animation_count;
                 ++animation_index) {
                srModelInstance* instance = prop->ToggleRepAnimation(animation_index);
                stMeshModel* mesh = static_cast<stMeshModel*>(instance->getModel());

                for (; mesh != 0; mesh = mesh->Next()) {
                    if (!AnimationIsRunning(animation)) {
                        mesh->GetVertexSunlight(true);
                        SetModelInstanceChainExclusionMask(instance, 5);
                    } else if (AnimationIsRunning(animation) == 1) {
                        SetModelInstanceChainExclusionMask(instance, 4);
                    }

                    auto* material =
                        dynamic_cast<stMaterial*>(mesh->getMaterial(0, srMeshModel::SIDE_FRONT));
                    if (material != 0) {
                        auto copy = std::make_shared<stMaterial>(*material);
                        copy->setName("Unsunlit Prop Material");
                        copy->parms.ambient = glm::vec4(0.0f);
                        copy->dirty = 1;
                        copy->parms.emissive += glm::vec4(g_environment_offset, 0);
                        copy->dirty = 1;
                        mesh->setMaterial(std::move(copy), 0, srMeshModel::SIDE_FRONT);
                    }
                }
            }
        }
    }
    if (g_octree != 0) {
        g_octree->m_fAccumulating = false;
    }
    return success;
} catch (const std::exception&) {
    return false;
}

// VTABLE: WIZ8 0x005ED180
// class srClientSupport<srClipPlane,5376>

#undef CHECK_PVL_OFFSET
