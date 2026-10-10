#include "wiz8/filesystem.h"
#include "wiz8/engine_code/3d.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "wiz8/utility.h"
#include "wiz8/geometry.h"
#include "wiz8/3d_code/IList.h"

#include "wiz8/engine_code/AnimObj.h"
#include "wiz8/engine_code/PathAI.h"
#include "surrender/srClipPlane.h"
#include "wiz8/engine_code/Environment.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/Level.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/quad.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stParticle.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/engine_code/stTextureAnim.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/item_spawning.h"
#include "wiz8/sr_api.h"
#include "wiz8/virtual_file.h"
#include "wiz8/float_constants.h"
#include "surrender/srCamera.h"
#include "surrender/srGERD.h"
#include "surrender/srScene.h"
#include "wiz8/engine_code/Camera.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/3dapi.h"
#include "wiz8/local_screens/AutomapScreen.h"

#define READ_LEVEL_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\ReadLevel.cpp"

// FUNCTION: WIZ8 0x004B9C00
stLevel::stLevel(srNode* parent)
    : srClassSupport<stLevel, srNode, false, 0x10007>(static_cast<srNode*>(0))
{
    if (parent != 0) {
        setParent(parent, 1);
    }
    m_render_exclusion_mask = 0;
}

// FUNCTION: WIZ8 0x004BA3D0
srClass* stLevel::vInstance()
{
    return new stLevel(0);
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
    applyWorldSpaceMatrix(renderer);

    w8_ulong old_exclusion_mask = 0;
    if (m_render_exclusion_mask != 0) {
        old_exclusion_mask = renderer.getExclusionMask();
        renderer.setExclusionMask(m_render_exclusion_mask | old_exclusion_mask);
    }

    srVector4T<float> ambient;
    renderer.getAmbientLight(ambient);
    srVector3T<float> ambient_color(ambient.x, ambient.y, ambient.z);
    m_submitted_polygons = 0;

    for (srNode* child = first_child_; child != 0; child = child->next_sibling_) {
        stModelInstance* instance = static_cast<stModelInstance*>(child);
        stMeshModel* model = static_cast<stMeshModel*>(instance->getModel());
        if (instance->mesh_index >= 0 && instance->testFlag(FLAG_DISABLE) != 0) {
            continue;
        }

        if ((model->render_control.value & 0x20) == 0) {
            srVector3T<float> center;
            float radius;
            model->getBoundingSphere(center, radius);
            if (renderer.testBoundingSphere(center, radius) == srGERD::VISIBILITY_OUTSIDE) {
                continue;
            }
        }
        if ((model->render_control.value & 0x10) == 0 && model->vertex_location_count >= 8) {
            srVector3T<float> minimum;
            srVector3T<float> maximum;
            model->getBoundingBox(minimum, maximum);
            if (renderer.testBoundingBox(minimum, maximum) == srGERD::VISIBILITY_OUTSIDE) {
                continue;
            }
        }

        srNode* linked_child = child;
        while (model != 0) {
            if (model->getActivePolygonTable(0) != 0) {
                m_submitted_polygons += model->getActivePolygonCount();
            } else {
                m_submitted_polygons += model->polygon_count;
            }

            if (model->vertex_lighting_ready) {
                srVector4T<float> dark;
                dark.Set(0.0f, 0.0f, 0.0f, 0.0f);
                renderer.setAmbientLight(dark);
                model->SetAmbientColor(ambient_color);
            }

            srMeshModel::TriMesh mesh;
            model->getTriMesh(mesh);
            if (g_render_untextured) {
                mesh.shaders[0].value &= 0xffff7fff;
                mesh.poly_shaders[0] = 0;
            }
            srPtr<srTextureIFace>*(*poly_textures)[2] = mesh.poly_textures;
            if (poly_textures != 0 && mesh.active_polygons == 0) {
                w8_long active_count;
                w8_ulong* active = model->GetActivePolygons(&active_count, -1, false);
                if (active != 0) {
                    mesh.active_polygons = active;
                    mesh.active_polygon_count = active_count;
                }
            }

            if ((model->flags & W8_MESH_SORTED_RENDERING) != 0 && !renderer.isPickStackEmpty()) {
                srGERD::Pick pick;
                renderer.popPick(pick);
                model->renderTriMesh(renderer, mesh);
                renderer.pushPick(pick);
            } else {
                model->renderTriMesh(renderer, mesh);
            }

            if (model->vertex_lighting_ready) {
                renderer.setAmbientLight(ambient);
            }
            if (linked_child != 0 && linked_child->testFlag(FLAG_TERMINATE) != 0) {
                break;
            }
            model = model->next;
            if (linked_child != 0) {
                linked_child = linked_child->first_child_;
            }
        }
    }

    if (m_render_exclusion_mask != 0) {
        renderer.setExclusionMask(old_exclusion_mask);
    }
    renderer.popMatrix();
}

namespace {

struct W8LevelItemRecord {
    int positional0;
    srVector3T<float> position;
    int positional2;
    int positional3;
    int positional4;
    char item_name[20];
};

W8_ABI_ASSERT(sizeof(W8LevelItemRecord) == 0x30, "W8LevelItemRecord_size_must_be_0x30");

struct W8LevelLightRecord {
    short version;
    unsigned char create;
    bool visible;
    unsigned int flags;
    srVector3T<float> location;
    srVector3T<float> colour;
    float intensity;
    float range;
};

W8_ABI_ASSERT(sizeof(W8LevelLightRecord) == 0x28, "W8LevelLightRecord_size_must_be_0x28");

} // namespace

// GLOBAL: WIZ8 0x005ec0b0
const float g_environment_near_scale = 2.0f;
// GLOBAL: WIZ8 0x00659cd0
srVector3T<float> g_environment_offset;

// FUNCTION: WIZ8 0x004BC060
void AssociateWorldLights(W8World* world)
{
    int light_index;

    for (light_index = 0; light_index < world->lights_to_update->GetCount(); ++light_index) {
        stLight* light = *world->lights_to_update->GetAt(light_index);
        stLightDefinition* definition = light->m_definition;

        if (definition != 0 && definition->kind == W8_LIGHT_DEFINITION_PARAMETRIC &&
            (static_cast<stParametricLightDefinition*>(definition)->flags &
             W8_PARAM_LIGHT_FLICKER) != 0) {
            int prop_count = PLLength(world->plsProps);
            int prop_index;

            for (prop_index = 0; prop_index < prop_count; ++prop_index) {
                W8Prop* prop = GetWorldProp(world, prop_index);

                if (prop->m_name != 0 && _stricmp(prop->m_name, light->getName().c_str()) == 0) {
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
try
{
    short light_count;
    int index;
    unsigned char success;
    success = (hFile->read(&light_count, sizeof(light_count)).bytes == static_cast<std::size_t>(sizeof(light_count)));
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
            _strupr(name);

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
                    PathAISetAnimated(path, 0);
                    PathAISetScale(path, definition->path_speed);
                }
            }
        }

        if (record.version >= 2 && record.create != 0) {
            if (definition == 0) {
                light = CreateRangedWorldLight(world, name);
            } else {
                light = CreateWorldLight(world, name);
                light->m_definition = definition;
                world->lights_to_update->Add(light);
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
            light = CreateLight(world->dynamic_scene, name);
            if (light == 0) {
                srAssertFail("pstLight", READ_LEVEL_CPP, 593, 0);
            }
            light->setGroupMask(1);
        } else {
            delete definition;
        }

        if (light != 0) {
            if (_strnicmp(light->getName().c_str(), "Sun", 3) == 0) {
                light->diffuse.SetZero();
                light->ambient = record.colour;
                light->setGroupMask(light->getGroupMask() | 4);
                AddEnvironmentLight(light);
            } else {
                light->diffuse = record.colour;
                light->ambient.SetZero();
            }

            light->specular.SetZero();
            ConfigureWorldLight(light, record.range * g_world_scale);
            light->intensity = record.intensity;
            light->setLocation(record.location.x * g_world_scale, record.location.y * g_world_scale,
                               record.location.z * g_world_scale);
        }
    }

    return success;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004BC9D0
unsigned char ReadWorldEnvironment(W8ReadLevelInfo* pInfo, W8World* pWorld)
try
{
    /* Retail read these uninitialised when a FileRead chain short-circuited;
       natively the scalars start at zero. */
    srVector3T<float> environment_range;
    EnvironmentColour white;
    srVector3T<float> position;
    srVector3T<float> axis;
    srMatrix3T<float> rotation;
    float intensity = 0.0f;
    float view_distance = 0.0f;
    float angle = 0.0f;
    float distance_scale;
    unsigned char camera_mode = 0;
    unsigned char has_light_colours = 0;
    unsigned char has_environment_colours = 0;
    unsigned char fog_enabled = 0;
    bool success;

    success = (pInfo->hFile->read(&fog_enabled, sizeof(fog_enabled)).bytes == static_cast<std::size_t>(sizeof(fog_enabled))) &&
              (pInfo->hFile->read(&environment_range.x, sizeof(environment_range.x)).bytes == static_cast<std::size_t>(sizeof(environment_range.x))) &&
              (pInfo->hFile->read(&environment_range.y, sizeof(environment_range.y)).bytes == static_cast<std::size_t>(sizeof(environment_range.y))) &&
              (pInfo->hFile->read(&environment_range.z, sizeof(environment_range.z)).bytes == static_cast<std::size_t>(sizeof(environment_range.z))) &&
              (pInfo->hFile->read(&intensity, sizeof(intensity)).bytes == static_cast<std::size_t>(sizeof(intensity))) &&
              (pInfo->hFile->read(&view_distance, sizeof(view_distance)).bytes == static_cast<std::size_t>(sizeof(view_distance))) &&
              (pInfo->hFile->read(&camera_mode, sizeof(camera_mode)).bytes == static_cast<std::size_t>(sizeof(camera_mode)));

    if (camera_mode == 1) {
        success = success && (pInfo->hFile->read(&position, sizeof(position)).bytes == static_cast<std::size_t>(sizeof(position)));
        position *= g_world_scale;
        SetWorldScenePosition(pWorld, &position);
    } else if (camera_mode == 2) {
        success = success && (pInfo->hFile->read(&position, sizeof(position)).bytes == static_cast<std::size_t>(sizeof(position))) &&
                  (pInfo->hFile->read(&angle, sizeof(angle)).bytes == static_cast<std::size_t>(sizeof(angle))) &&
                  (pInfo->hFile->read(&axis.x, sizeof(axis.x)).bytes == static_cast<std::size_t>(sizeof(axis.x))) &&
                  (pInfo->hFile->read(&axis.y, sizeof(axis.y)).bytes == static_cast<std::size_t>(sizeof(axis.y))) &&
                  (pInfo->hFile->read(&axis.z, sizeof(axis.z)).bytes == static_cast<std::size_t>(sizeof(axis.z)));
        position *= g_world_scale;
        SetWorldScenePosition(GetWorld(), &position);

        rotation.SetIdentity();
        if (angle != 0.0f) {
            rotation.RotateAroundAxis(sin(angle), cos(angle), axis);
        }
        ApplyCameraRotation(&rotation);
    }

    success = success && (pInfo->hFile->read(&has_light_colours, sizeof(has_light_colours)).bytes == static_cast<std::size_t>(sizeof(has_light_colours)));
    if (has_light_colours != 0) {
        ReadLightColourTable(pInfo->hFile);
    } else {
        BuildLightColourRamp();
    }

    success = success &&
              (pInfo->hFile->read(&has_environment_colours, sizeof(has_environment_colours)).bytes == static_cast<std::size_t>(sizeof(has_environment_colours)));
    if (has_environment_colours != 0) {
        ReadEnvironmentColourTable(pInfo->hFile);
    } else {
        BuildEnvironmentColourRamp();
    }

    g_environment_offset.SetZero();
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
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004BCE20
static unsigned char ReadWorldClipPlanes(W8ReadLevelInfo* pInfo, W8World* pWorld)
try
{
    W8GrowableVector<srClipPlane*> clip_planes(5);
    srVector4T<float> plane;
    srVector4T<float> serialized_position;
    srVector3T<double> position;
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

    success = (pInfo->hFile->read(&count, sizeof(count)).bytes == static_cast<std::size_t>(sizeof(count)));
    if (!success) {
        srAssertFail("fSuccess", READ_LEVEL_CPP, 0x5a2, "Error reading num clipping planes");
        return 0;
    }
    if (count == 0) {
        return 1;
    }

    (pInfo->hFile->read(&version, sizeof(version)).bytes == static_cast<std::size_t>(sizeof(version)));
    plane.Set(0.0f, 1.0f, 0.0f, 0.0f);
    for (index = 0; index < count; ++index) {
        clip_plane = SR_NEW(srClipPlane)(pWorld->static_scene);
        if (clip_plane == 0) {
            srAssertFail("psrClipPlane", READ_LEVEL_CPP, 0x5ae,
                         "out of memory creating clip plane");
        }

        (pInfo->hFile->read(name, sizeof(name)).bytes == static_cast<std::size_t>(sizeof(name)));
        (pInfo->hFile->read(&serialized_position, sizeof(serialized_position)).bytes == static_cast<std::size_t>(sizeof(serialized_position)));
        _strupr(name);
        clip_plane->setName(name);
        clip_plane->setClipPlane(plane);

        position.Set(serialized_position.x * g_world_scale, serialized_position.y * g_world_scale,
                     serialized_position.z * g_world_scale);
        clip_plane->setLocation(position);
        clip_plane->setFlag(srNode::FLAG_GLOBAL);
        clip_plane->setClipType(srClipPlane::CLIP_POSITIONAL_0);
        clip_plane->setFlag(srNode::FLAG_DISABLE);
    }
    return 1;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004BC5E0
static unsigned char ReadWorldProps(W8ReadLevelInfo* pInfo, W8World* pWorld,
                                    bool mark_model_instances)
try
{
    /* CollectModelInstances appends. The canonical body deliberately keeps
       this one vector across the complete prop loop. */
    W8Vector<stModelInstance*> model_instances(5);
    W8Prop* prop;
    W8BoundingBox bounds;
    int count;
    int index;
    int collidable_index;
    int model_index;
    unsigned char success;

    collidable_index = 0;
    if (pInfo == 0 || pInfo->hFile == 0 || pWorld == 0) {
        return 0;
    }
    success = (pInfo->hFile->read(&count, sizeof(count)).bytes == static_cast<std::size_t>(sizeof(count)));
    if (!success || count >= 100000) {
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
            PLAdoptAppend(pWorld->plsProps, prop);
            if ((prop->flags & W8_PROP_COLLIDABLE) != 0) {
                prop->GetBounds(&bounds.minimum, &bounds.maximum);
                pWorld->collidable_props->Add(prop);
                if (pWorld->octree != 0) {
                    pWorld->octree->AddCollidablePropBounds(collidable_index, &bounds);
                    ++collidable_index;
                }
            }
        }

        if (mark_model_instances && prop != 0) {
            prop->CollectModelInstances(&model_instances);
            for (model_index = 0; model_index < model_instances.GetCount(); ++model_index) {
                stModelInstance* instance = *model_instances.GetAt(model_index);
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
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004BC380
unsigned char ReadWorldItems(W8ReadLevelInfo* pInfo, W8World* pWorld)
try
{
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
    success = (pInfo->hFile->read(&count, sizeof(count)).bytes == static_cast<std::size_t>(sizeof(count)));
    if (!success || count >= 100000) {
        return 0;
    }
    if (count == 0) {
        return 1;
    }

    for (index = 0; index < count; ++index) {
        item = 0;
        trigger = 0;
        success = (pInfo->hFile->read(record.item_name, sizeof(record.item_name)).bytes == static_cast<std::size_t>(sizeof(record.item_name)));
        if (success) {
            (pInfo->hFile->read(&record.position, sizeof(record.position)).bytes == static_cast<std::size_t>(sizeof(record.position)));
            record.position *= g_world_scale;
            (pInfo->hFile->read(&record.positional0, sizeof(int)).bytes == static_cast<std::size_t>(sizeof(int)));
            (pInfo->hFile->read(&record.positional2, sizeof(int)).bytes == static_cast<std::size_t>(sizeof(int)));
            (pInfo->hFile->read(&record.positional3, sizeof(int)).bytes == static_cast<std::size_t>(sizeof(int)));
            (pInfo->hFile->read(&record.positional4, sizeof(int)).bytes == static_cast<std::size_t>(sizeof(int)));
            (pInfo->hFile->read(&has_trigger, sizeof(has_trigger)).bytes == static_cast<std::size_t>(sizeof(has_trigger)));
            if (has_trigger != 0) {
                trigger = Trigger::CreateAndLoadLevelTrigger(pInfo->hFile, pInfo->world);
            }
            (pInfo->hFile->read(&positional_byte, sizeof(positional_byte)).bytes == static_cast<std::size_t>(sizeof(positional_byte)));
            (pInfo->hFile->read(&positional_byte, sizeof(positional_byte)).bytes == static_cast<std::size_t>(sizeof(positional_byte)));
            (pInfo->hFile->read(&positional_byte, sizeof(positional_byte)).bytes == static_cast<std::size_t>(sizeof(positional_byte)));
            (pInfo->hFile->read(&positional_value, sizeof(positional_value)).bytes == static_cast<std::size_t>(sizeof(positional_value)));
            (pInfo->hFile->read(&positional_value, sizeof(positional_value)).bytes == static_cast<std::size_t>(sizeof(positional_value)));
            (pInfo->hFile->read(&positional_value, sizeof(positional_value)).bytes == static_cast<std::size_t>(sizeof(positional_value)));
            (pInfo->hFile->read(&positional_value, sizeof(positional_value)).bytes == static_cast<std::size_t>(sizeof(positional_value)));

            if (record.item_name[0] >= '0' && record.item_name[0] <= '9') {
                item_id = atoi(record.item_name);
            } else {
                item_id = FindItemRecordByName(record.item_name);
            }
            if (item_id >= 0) {
                world_item = SpawnItem(item_id, &record.position,
                                       W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE, true);
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
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004BC140
unsigned char ReadMonsterPaths(W8ReadLevelInfo* pInfo, W8World* pWorld)
try
{
    int count;
    int index;
    unsigned char success;
    bool has_options;
    bool update_representation;
    bool active;
    char monster_name[20];
    char options[12];
    char* separator;
    srVector3T<float> origin;
    srVector3T<float> camera_position;
    W8MonsterGroup* group;
    int monster_id;
    int location_id;
    unsigned int monster_index;
    W8MonsterInfo* monster_info;
    W8Monster* monster;
    W8PathAI* path;

    /* Canonical 0x004BC140 initializes this once, not once per record. A
       colon-free record after an option-bearing one therefore reuses options. */
    has_options = false;
    if (pInfo == 0 || pInfo->hFile == 0 || pWorld == 0) {
        return 0;
    }
    success = (pInfo->hFile->read(&count, sizeof(count)).bytes == static_cast<std::size_t>(sizeof(count)));
    if (!success || count >= 100000) {
        return 0;
    }
    if (count == 0) {
        return 1;
    }

    origin.SetZero();
    for (index = 0; index < count; ++index) {
        update_representation = true;
        active = true;
        success = success && (pInfo->hFile->read(monster_name, sizeof(monster_name)).bytes == static_cast<std::size_t>(sizeof(monster_name)));
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
        location_id = IListGetAt(group->monsters, 0);
        monster_index = MonsterGetIndexByLocationID(0x315, READ_LEVEL_CPP, location_id, true);
        monster_info = MonsterGetScriptPartByLocationIndex(monster_index);
        ActivateMonster(monster_info, W8_MONSTER_LOAD_ALL_CYCLES);
        monster = monster_info->p3D;

        {
            srVector3T<double> camera_location = pWorld->camera->getLocation();
            camera_position = camera_location;
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
                PathAISetAnimated(path, 1);
            }
            if (options[3] == '1') {
                PathAISetLooping(path, 1);
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
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004BC850
unsigned char ReadWorldCameras(W8ReadLevelInfo* pInfo, W8World* pWorld)
try
{
    int count;
    int index;
    int positional_0;
    int positional_1;
    unsigned char has_scale;
    float scale;
    unsigned char success;
    W8CameraPath* entry;

    if (pInfo == 0 || (pInfo->hFile == 0 | pWorld == 0)) {
        return 0;
    }
    success = (pInfo->hFile->read(&count, sizeof(count)).bytes == static_cast<std::size_t>(sizeof(count)));
    if (!success || count >= 100000) {
        return 0;
    }
    if (count == 0) {
        return 1;
    }

    for (index = 0; index < count; ++index) {
        entry = static_cast<W8CameraPath*>(malloc(sizeof(W8CameraPath)));
        if (entry == 0) {
            return 0;
        }
        memset(entry, 0, sizeof(W8CameraPath));
        (pInfo->hFile->read(&positional_0, sizeof(positional_0)).bytes == static_cast<std::size_t>(sizeof(positional_0)));
        (pInfo->hFile->read(&positional_1, sizeof(positional_1)).bytes == static_cast<std::size_t>(sizeof(positional_1)));
        (pInfo->hFile->read(&has_scale, sizeof(has_scale)).bytes == static_cast<std::size_t>(sizeof(has_scale)));
        (pInfo->hFile->read(entry->name0, sizeof(entry->name0)).bytes == static_cast<std::size_t>(sizeof(entry->name0)));
        if (has_scale > 0) {
            (pInfo->hFile->read(&scale, sizeof(scale)).bytes == static_cast<std::size_t>(sizeof(scale)));
        } else {
            scale = 15.0f;
        }

        entry->path = 0;
        success = success && LoadPathAI(&entry->path, pInfo->hFile);
        PathAIEnableTimedMode(entry->path);
        PLAdoptAppend(pWorld->plsCameras, entry);
        entry->path->entry_index = index;
        PathAISetScale(entry->path, scale);
    }
    return 1;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004BD0D0
unsigned char ReadWorldParticles(W8ReadLevelInfo* pInfo, srNode* pScene,
                                 W8GrowableVector<stParticle*>* pParticles)
try
{
    W8LevelParticleRecord record;
    srMaterialIFace* material;
    srTextureIFace* texture;
    srShader render_flags;
    int count;
    int index;

    material = 0;
    texture = 0;
    (pInfo->hFile->read(&count, sizeof(count)).bytes == static_cast<std::size_t>(sizeof(count)));
    for (index = 0; index < count; ++index) {
        unsigned char version;
        stParticle* particle;
        srVector3T<double> axis;
        srVector3T<double> location;

        (pInfo->hFile->read(&version, sizeof(version)).bytes == static_cast<std::size_t>(sizeof(version)));
        if (version == 4) {
            (pInfo->hFile->read(&record, sizeof(record)).bytes == static_cast<std::size_t>(sizeof(record)));
        } else if (version == 3) {
            (pInfo->hFile->read(&record, 0x21d).bytes == static_cast<std::size_t>(0x21d));
            record.start_frame = -1;
            record.end_frame = -1;
        } else if (version == 2) {
            (pInfo->hFile->read(&record, 0x218).bytes == static_cast<std::size_t>(0x218));
            record.emission_limit = 0;
            record.requires_sorted_renderer = 0;
            record.start_frame = -1;
            record.end_frame = -1;
        } else if (version == 1) {
            (pInfo->hFile->read(&record, 0x216).bytes == static_cast<std::size_t>(0x216));
            record.attachment_key = -1;
            record.emission_limit = 0;
            record.requires_sorted_renderer = 0;
            record.start_frame = -1;
            record.end_frame = -1;
        } else {
            srAssertFail("0", READ_LEVEL_CPP, 0x5fe, "Unknown particle structure version");
        }

        particle = new stParticle(pScene, record.particle_count);
        if (particle == 0) {
            srAssertFail("pParticle", READ_LEVEL_CPP, 0x603,
                         "Error creating particle in ReadParticles()");
        }

        _strupr(record.name);
        particle->setName(record.name);
        axis.SetFromFloat(&record.rotation_axis);
        particle->rotate(record.rotation_angle, axis);
        location.SetFromFloat(&record.location);
        location *= g_world_scale;
        particle->setLocation(location);
        particle->rotateX(1.5707963);

        if (record.bounds_origin.x != 0.0f) {
            particle->replace_when_full = true;
            record.bounds_origin.x = 0.0f;
        }
        if (record.bounds_mode == W8_PARTICLE_BOUNDS_BOX) {
            srVector3T<float> center;
            srVector3T<float> extent;

            center = record.bounds_origin * g_world_scale;
            extent = record.bounds_extent * 250.0f;
            particle->bounds_mode = W8_PARTICLE_BOUNDS_BOX;
            particle->lifetime_minimum = center - extent;
            particle->lifetime_maximum = center + extent;
        } else if (record.bounds_mode == W8_PARTICLE_BOUNDS_SPHERE && record.bounds_radius > 0.0f) {
            particle->bounds_mode = W8_PARTICLE_BOUNDS_SPHERE;
            particle->bounds_origin = record.bounds_origin * g_world_scale;
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
            particle->acceleration = record.acceleration * g_world_scale;
        }

        if (record.emission_mode == W8_PARTICLE_EMISSION_NONE) {
            particle->emission_mode = W8_PARTICLE_EMISSION_NONE;
        } else if (record.emission_mode == W8_PARTICLE_EMISSION_SINGLE) {
            particle->emission_mode = W8_PARTICLE_EMISSION_SINGLE;
        } else {
            particle->emission_mode = W8_PARTICLE_EMISSION_CATCH_UP;
            particle->emission_minimum.Set(-record.spread.x * 250.0f, -record.spread.y * 250.0f,
                                           0.0f);
            particle->emission_maximum.Set(record.spread.x * 250.0f, record.spread.y * 250.0f,
                                           record.spread.z * g_world_scale);
        }

        if (record.direction_mode == W8_PARTICLE_DIRECTION_STATIONARY) {
            particle->direction_mode = W8_PARTICLE_DIRECTION_STATIONARY;
        } else if (record.direction_mode == W8_PARTICLE_DIRECTION_FIXED) {
            srMatrix3T<float> rotation;
            srVector3T<float> direction;
            srVector3T<float> transformed;
            double angle = -1.5707963;

            rotation.SetIdentity();
            if (record.rotation_angle != 0.0f) {
                rotation.RotateAroundAxis(sin(record.rotation_angle), cos(record.rotation_angle),
                                          record.rotation_axis);
            }
            rotation.RotateAboutX(sin(angle), cos(angle));
            direction.Set(0.0, 0.0, -1.0);
            transformed = rotation.Transform(direction);
            transformed.Unitize();
            particle->direction_mode = W8_PARTICLE_DIRECTION_FIXED;
            particle->direction = transformed;
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

        LoadMaterial(pInfo->bitmap_folder, &record.material, &material, &texture, &render_flags, 1);
        particle->SetMaterial(material);
        srShader shader;
        shader.value = render_flags.value;
        particle->SetRenderFlags(shader);
        particle->SetTexture(texture);

        if (strncmp(record.name, "CLOUD", 5) == 0) {
            srVector3T<double> current = particle->getLocation();
            particle->camera_offset = current;
            particle->camera_relative = 1;
            particle->SetActive(1);
        } else if (g_octree != 0) {
            g_octree->AddLoadedParticle(particle);
        }
        pParticles->Add(particle);
    }
    return 1;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004BDC90
static unsigned char ReadNamedPositions(W8ReadLevelInfo* pInfo,
                                        W8GrowableVector<W8NamedPosition*>* named_positions)
try
{
    wiz8::File* hFile;
    int count;
    int index;
    unsigned char version;
    W8NamedPosition* pNamedPos;

    hFile = pInfo->hFile;
    hFile->read_exact(&count, sizeof(count));
    for (index = 0; index < count; ++index) {
        pNamedPos = new W8NamedPosition;
        if (pNamedPos == 0) {
            srAssertFail("pNamedPos", READ_LEVEL_CPP, 0x6d3, "out of memory creating NamedPos");
        }

        hFile->read_exact(&version, sizeof(version));
        if (version != 1) {
            srAssertFail("bVersion == 1", READ_LEVEL_CPP, 0x6d6, "Unknown Named Position version");
        }

        hFile->read_exact(pNamedPos->name, sizeof(pNamedPos->name));
        hFile->read_exact(&pNamedPos->position.x, sizeof(pNamedPos->position.x));
        hFile->read_exact(&pNamedPos->position.y, sizeof(pNamedPos->position.y));
        hFile->read_exact(&pNamedPos->position.z, sizeof(pNamedPos->position.z));
        pNamedPos->position *= 500.0;
        hFile->read_exact(&pNamedPos->angle, sizeof(pNamedPos->angle));
        hFile->read_exact(&pNamedPos->direction.x, sizeof(pNamedPos->direction.x));
        hFile->read_exact(&pNamedPos->direction.y, sizeof(pNamedPos->direction.y));
        hFile->read_exact(&pNamedPos->direction.z, sizeof(pNamedPos->direction.z));
        named_positions->Add(pNamedPos);
    }
    return 1;
}
catch (const std::exception&) { return false; }

#define CHECK_PVL_OFFSET(message)                                                                  \
    if (world->octree != 0 &&                                                                      \
        (!(handle->read(&section_end, sizeof(section_end)).bytes == static_cast<std::size_t>(sizeof(section_end))) || section_end != -1)) {          \
        sprintf(error_message, "%s\nTry deleting .PVL file and reloading.", message);              \
        ShutdownWithErrorBox(error_message);                                                       \
    }

// FUNCTION: WIZ8 0x004BAFF0
unsigned char ReadLevel(W8World* world, wiz8::File* handle, bool use_octree, const char* bitmap_folder)
try
{
    W8ReadLevelInfo info;
    srModelInstance* level_mesh;
    srVector3T<float> minimum;
    srVector3T<float> maximum;
    srVector3T<float> environment_offset;
    char error_message[512];
    int section_end;
    int section_count;
    int camera_mode;
    int index;
    unsigned int prop_count;
    unsigned char success;

    level_mesh = 0;
    if (world->update_mesh_source != 0) {
        world->update_mesh_source->release();
    }
    if (world->psrMeshes != 0) {
        while (world->psrMeshes[0] != 0) {
            world->psrMeshes[0]->release();
            world->psrMeshes[0] = 0;
        }
        if (world->octree == 0) {
            free(world->psrMeshes);
            world->psrMeshes = 0;
        }
    }

    if (world == 0 || world->static_scene == 0 || handle == 0) {
        return 0;
    }

    info.world = world;
    info.hFile = handle;
    info.bitmap_folder = bitmap_folder;
    w8_get_ticks();

    if (world->psrMeshes == 0 || world->octree == 0) {
        if (!ReadSingleLevelMesh(&info, &level_mesh, 0, 0, 0, true)) {
            return 0;
        }
        if (level_mesh == 0) {
            srAssertFail("psrMesh", READ_LEVEL_CPP, 0xfa, 0);
        }
        level_mesh->setName("ReadLevel");
        world->update_mesh_source = level_mesh;
        level_mesh->setParent(world->level, 1);
        SetModelInstanceChainExclusionMask(level_mesh, 1);
    } else {
        if (!ReadMultipleLevelMeshes(&info, world->psrMeshes, world->octree->GetMeshCount(), 0)) {
            ShutdownWithErrorBox("ReadLevel: Error reading multi-meshes.");
        }
        for (unsigned int mesh_index = 0; mesh_index < world->octree->m_meshCount; ++mesh_index) {
            if (world->psrMeshes[mesh_index] != 0) {
                world->psrMeshes[mesh_index]->setParent(world->level, 1);
                SetModelInstanceChainExclusionMask(world->psrMeshes[mesh_index], 1);
            }
        }
    }

    success = ReadWorldLights(world, handle);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after lights.");
    success = success && ReadMonsterPaths(&info, world);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after monsters.");
    success = success && ReadWorldItems(&info, world);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after items.");

    if (!success || info.hFile == 0 ||
        !(info.hFile->read(&section_count, sizeof(section_count)).bytes == static_cast<std::size_t>(sizeof(section_count))) ||
        section_count >= 100000) {
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

    (info.hFile->read(&section_count, sizeof(section_count)).bytes == static_cast<std::size_t>(sizeof(section_count)));
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
        (info.hFile->read(&section_count, sizeof(section_count)).bytes == static_cast<std::size_t>(sizeof(section_count)));
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
    (info.hFile->read(&camera_mode, sizeof(camera_mode)).bytes == static_cast<std::size_t>(sizeof(camera_mode)));
    SetCameraSwayMode(world->camera, camera_mode == 0 ? -1 : 1);
    if (world->octree == 0 && world != g_secondary_world) {
        FinalizeWorldScenes(world->static_scene, world->dynamic_scene);
    }

    success = ReadWorldClipPlanes(&info, world);
    if (!success) {
        srAssertFail("fSuccess", READ_LEVEL_CPP, 0x15c, 0);
    }
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after Clipping Planes.");

    if (use_octree) {
        srMeshModel* model = static_cast<srMeshModel*>(level_mesh->getModel());
        model->getBoundingBox(minimum, maximum);
        world->quads = BuildWorldQuad(level_mesh, 0, minimum.x, minimum.y, minimum.z, maximum.x,
                                      maximum.y, maximum.z, world->static_scene, 0);
    }
    RefreshEnvironment();
    FinalizeStaticScene(world->static_scene);

    if (!success || !(handle->read(&environment_offset.x, sizeof(environment_offset.x)).bytes == static_cast<std::size_t>(sizeof(environment_offset.x))) ||
        !(handle->read(&environment_offset.y, sizeof(environment_offset.y)).bytes == static_cast<std::size_t>(sizeof(environment_offset.y))) ||
        !(handle->read(&environment_offset.z, sizeof(environment_offset.z)).bytes == static_cast<std::size_t>(sizeof(environment_offset.z)))) {
        success = 0;
    } else {
        success = ReadWorldParticles(&info, world->dynamic_scene, world->particles);
    }
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after particles.");
    success = success && ReadNamedPositions(&info, world->named_positions);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after named position points.");
    success = success && ReadAutomapNodes(handle);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after automap nodes.");

    g_environment_offset = environment_offset;
    prop_count = PLLength(world->plsProps);
    for (index = 0; index < static_cast<int>(prop_count); ++index) {
        W8Prop* prop = GetWorldProp(world, index);
        W8AnimObj* animation = prop->Rep()->animation;

        if ((prop->flags & W8_PROP_NO_DIRECT_SUN) != 0) {
            unsigned int animation_count = AnimObjListCount(animation, 2);
            for (unsigned int animation_index = 0; animation_index < animation_count;
                 ++animation_index) {
                srModelInstance* instance = prop->ToggleRepAnimation(animation_index);
                stMeshModel* mesh = static_cast<stMeshModel*>(instance->getModel());

                for (; mesh != 0; mesh = mesh->next) {
                    if (!AnimationIsRunning(animation)) {
                        mesh->GetVertexSunlight(true);
                        SetModelInstanceChainExclusionMask(instance, 5);
                    } else if (AnimationIsRunning(animation) == 1) {
                        SetModelInstanceChainExclusionMask(instance, 4);
                    }

                    srMaterialIFace* material_iface = mesh->getMaterial(0, srMeshModel::SIDE_FRONT);
                    if (material_iface != 0) {
                        srMaterial* material = static_cast<srMaterial*>(material_iface);
                        srMaterial* copy = static_cast<srMaterial*>(material->clone());
                        copy->setName("Unsunlit Prop Material");
                        copy->autoRelease();
                        copy->parms.ambient = 0.0f;
                        copy->dirty = 1;
                        copy->parms.emissive.x += g_environment_offset.x;
                        copy->parms.emissive.y += g_environment_offset.y;
                        copy->parms.emissive.z += g_environment_offset.z;
                        copy->dirty = 1;
                        mesh->setMaterial(copy, 0, srMeshModel::SIDE_FRONT);
                    }
                }
            }
        }
    }
    if (g_octree != 0) {
        g_octree->m_fAccumulating = false;
    }
    return success;
}
catch (const std::exception&) { return false; }

// VTABLE: WIZ8 0x005ED180
// class srClientSupport<srClipPlane,5376>

#undef CHECK_PVL_OFFSET
