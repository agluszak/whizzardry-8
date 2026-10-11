#include <fmt/format.h>
#include <glm/gtx/euler_angles.hpp>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include "surrender/srBoundingBox.h"
#include <string>

#include "wiz8/engine_code/Item.h"
#include "glm/ext/vector_float3.hpp"
#include "glm/geometric.hpp"
#include "wiz8/filesystem.h"
#include "random.h"
#include "wiz8/application.h"
#include "input.h"
#include "surrender/srCamera.h"
#include "surrender/srNode.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/sr_api.h"
#include "wiz8/utility.h"
#include "timer.h"
#include "AnimRep.hpp"
#include "GrObject.h"
#include "compat/native.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "layouts/world.h"
#include "surrender/srMath.h"
#include "surrender/srModel.h"
#include "surrender/srModelInstance.h"

#define ITEM_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\Item.cpp"

// VTABLE: WIZ8 0x005ECD78 W8Item
// class W8Item

// FUNCTION: WIZ8 0x0049F440
W8Item::~W8Item()
{
    delete m_pRep;
}

/* Detach the item's mesh from the world graph. The world parameter is asserted
   even though the body needs no field from it. */
// FUNCTION: WIZ8 0x0049fa30
void W8Item::DetachMesh(W8World* world)
{
    srNode* mesh;

    if (world == 0) {
        srAssertFail("pWorld", "C:\\Projects\\Wizardry 8\\Engine Code\\Item.cpp", 0x24a, 0);
    }
    mesh = static_cast<W8ItemRep*>(m_pRep)->m_psrMesh;
    if (mesh == 0) {
        srAssertFail("psrMesh", "C:\\Projects\\Wizardry 8\\Engine Code\\Item.cpp", 0x24f, 0);
    }
    mesh->setFlag(srNode::FLAG_DISABLE);
    mesh->setParent(0, 0);
}

/* Copy the representation's float transform onto its SurRender node. The
   location is widened because the exported node setter takes doubles. */
// FUNCTION: WIZ8 0x0049faa0
void W8Item::ApplyRepTransform()
{
    srVector3T<float> location;
    srVector3T<double> widened;
    glm::mat3 rotation;
    srNode* mesh;

    if (static_cast<W8ItemRep*>(m_pRep)->m_psrMesh == 0) {
        srAssertFail("m_pRep->m_psrMesh", "C:\\Projects\\Wizardry 8\\Engine Code\\Item.cpp", 0x266,
                     0);
    }
    mesh = static_cast<W8ItemRep*>(m_pRep)->m_psrMesh;
    m_pRep->GetLocation(&location);
    widened = glm::dvec3(location.glmValue());
    mesh->setLocation(widened);
    m_pRep->GetRotation(&rotation);
    mesh->setRotation(rotation);
}

// FUNCTION: WIZ8 0x0049F900
void W8Item::AttachMesh(W8World* world)
{
    srNode* mesh;
    srNode* child;
    srVector3T<float> location;
    srVector3T<double> widened;
    glm::mat3 rotation;

    if (world == 0) {
        srAssertFail("pWorld", "C:\\Projects\\Wizardry 8\\Engine Code\\Item.cpp", 0x211, 0);
    }
    if ((static_cast<W8ItemRep*>(m_pRep)->flags & W8_ITEM_ENTITY_MESH_LOADED) == 0) {
        return;
    }
    mesh = static_cast<W8ItemRep*>(m_pRep)->m_psrMesh;
    if (mesh == 0) {
        srAssertFail("psrMesh", "C:\\Projects\\Wizardry 8\\Engine Code\\Item.cpp", 0x219, 0);
    }
    if (mesh->first_child_ == 0) {
        mesh->clearFlag(srNode::FLAG_DISABLE);
    }
    mesh->setParent(world->dynamic_scene, 0);
    m_pRep->GetLocation(&location);
    m_pRep->GetRotation(&rotation);
    child = mesh->first_child_;
    if (child == 0) {
        widened = glm::dvec3(location.glmValue());
        mesh->setLocation(widened);
        mesh->setRotation(rotation);
        return;
    }
    do {
        widened = glm::dvec3(location.glmValue());
        child->setLocation(widened);
        child->setRotation(rotation);
        child = child->next_sibling_;
    } while (child != 0);
}

// FUNCTION: WIZ8 0x0049fb20
srNode* W8Item::GetMesh()
{
    return static_cast<W8ItemRep*>(m_pRep)->m_psrMesh;
}

// FUNCTION: WIZ8 0x0049F310
unsigned int W8ItemRep::SetFlags(unsigned int mask, bool enabled)
{
    if (enabled) {
        flags |= mask;
    } else {
        flags &= ~mask;
    }
    return flags;
}

// FUNCTION: WIZ8 0x0049F720
void W8Item::SetLocation(const srVector3T<float>* location)
{
    m_pRep->SetLocation(location);
}

// FUNCTION: WIZ8 0x004A0050
bool W8Item::IsRadarBlipLit()
{
    return TimeUntilDeadline(countdown) != 0;
}

// VTABLE: WIZ8 0x005ECD70 W8ItemRep
// class W8ItemRep

/* Inlined into both item construction paths. Retail leaves the mesh pointer
   and bounds untouched until ReadFromFile; do not initialize them here. */
W8ItemRep::W8ItemRep()
{
    flags = 0;
    pulse_level = static_cast<float>(Random(20) * 0.05);
    flags |= W8_ITEM_ENTITY_PULSE_INCREASING;
}

// FUNCTION: WIZ8 0x0049F120
W8ItemRep::~W8ItemRep()
{
    if (m_psrMesh != 0) {
        delete m_psrMesh;
    }
}

// FUNCTION: WIZ8 0x0049F170
bool W8ItemRep::ReadFromFile(W8ReadLevelInfo* info, W8Item* item, bool anonymous_mesh)
{
    const char* bitmap_folder = info->bitmap_folder;
    srModelInstance* mesh = 0;
    if (info == 0 || info->hFile == 0 || item == 0) {
        srAssertFail("pInfo && pInfo->hFile && pItem", ITEM_CPP, 0x7e, 0);
    }

    info->bitmap_folder = "Data\\Items3D\\Bitmaps";
    bool success =
        ReadSingleLevelMesh(info, &mesh, 0, 0, anonymous_mesh ? 0 : info->mesh_filename, true) != 0;
    if (!success || mesh == 0) {
        srAssertFail("fSuccess && psrMesh", ITEM_CPP, 0x8c, 0);
    }
    mesh->setName("ItemRep::ReadFromFile");
    static_cast<stModelInstance*>(mesh)->render_flags |= stModelInstance::RENDER_SHADOW;
    SetModelInstanceChainExclusionMask(mesh, 4);
    info->bitmap_folder = bitmap_folder;
    flags |= W8_ITEM_ENTITY_MESH_LOADED;
    m_psrMesh = mesh;
    RefreshBounds();
    return success;
}

// FUNCTION: WIZ8 0x0049F2B0
void W8ItemRep::RefreshBounds()
{
    const auto bounds = m_psrMesh->getModel()->getBoundingBox();
    bounds_minimum = bounds.minimum;
    bounds_maximum = bounds.maximum;
    bounds_radius = static_cast<float>(glm::distance(bounds_maximum, bounds_minimum) * 0.5);
}

// FUNCTION: WIZ8 0x0049F350
W8Item::W8Item()
{
    trigger = 0;
    kind = 2;
    m_pRep = new W8ItemRep;
    id = AllocateGrObjectId();
    countdown = w8_get_ticks() + (0);
}

// FUNCTION: WIZ8 0x0049F4A0
bool LoadItemFromFile(const W8ReadLevelInfo* context, const char* name, W8Item** output,
                      bool anonymous_mesh)
try
{
    W8Item* item = 0;
    if (name == 0 || output == 0) {
        srAssertFail("pacName && ppItem", ITEM_CPP, 0x11a, 0);
    }
    char filename[52];
    sprintf(filename, "%s\\%s.itm", "Data\\Items3D", name);
    std::unique_ptr<wiz8::File> file = [&]() { try { return wiz8::open_file(filename, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
    if (file == 0) {
        ShutdownWithErrorBox(fmt::format("Couldn't load {}", filename));
        return false;
    }

    W8ReadLevelInfo info;
    info.world = context->world;
    info.hFile = file.get();
    info.bitmap_folder = context->bitmap_folder;
    char* mesh_name = new char[1024];
    strcpy(mesh_name, name);
    info.mesh_filename = mesh_name;
    unsigned char version;
    bool success = false;
    if ((file->read(&version, 1).bytes == static_cast<std::size_t>(1)) && version == 2 &&
        ReadItemFromFile(&info, &item, anonymous_mesh)) {
        *output = item;
        success = true;
    }
    if (file) file->close();
    file.reset();
    delete[] mesh_name;
    return success;
} catch (const std::exception&) {
    return false;
}

/* On failure retail leaves the output alone and does not delete the allocated
   item. This is not an owning smart-pointer factory. */
// FUNCTION: WIZ8 0x0049F5D0
bool ReadItemFromFile(W8ReadLevelInfo* info, W8Item** output, bool anonymous_mesh)
{
    if (info == 0) {
        srAssertFail("pInfo", ITEM_CPP, 0x15d, 0);
    }
    W8Item* item = new W8Item;
    if (item == 0) {
        srAssertFail("pItem", ITEM_CPP, 0x161, 0);
    }
    bool success = static_cast<W8ItemRep*>(item->m_pRep)->ReadFromFile(info, item, anonymous_mesh);
    if (success) {
        *output = item;
    }
    return success;
}

// FUNCTION: WIZ8 0x0049F730
void W8Item::UpdateAnimation()
{
    if (m_pRep == 0) {
        srAssertFail("m_pRep", ITEM_CPP, 0x1ca, 0);
    }
    W8ItemRep* rep = static_cast<W8ItemRep*>(m_pRep);
    if ((rep->flags & W8_ITEM_ENTITY_MESH_LOADED) == 0) {
        return;
    }
    if ((rep->flags & W8_ITEM_ENTITY_ROTATE) != 0 && gfKeyState[VK_CONTROL] == 0) {
        glm::mat3 rotation;
        m_pRep->GetRotation(&rotation);
        rotation *= glm::mat3(glm::eulerAngleY(-0.1963495375f));
        srNode* mesh = rep->m_psrMesh;
        m_pRep->SetRotation(&rotation);
        if (mesh->first_child_ == 0) {
            mesh->setRotation(rotation);
        } else {
            for (srNode* child = mesh->first_child_; child != 0; child = child->next_sibling_) {
                child->setRotation(rotation);
            }
        }
    }

    rep = static_cast<W8ItemRep*>(m_pRep);
    if ((rep->flags & W8_ITEM_ENTITY_PULSE) != 0) {
        if ((rep->flags & W8_ITEM_ENTITY_PULSE_INCREASING) != 0) {
            rep->pulse_level += 0.05f;
            if (rep->pulse_level >= 1.0f) {
                rep->pulse_level = 1.0f;
                rep->flags &= ~W8_ITEM_ENTITY_PULSE_INCREASING;
            }
        } else {
            rep->pulse_level -= 0.05f;
            if (rep->pulse_level <= 0.25f) {
                rep->pulse_level = 0.25f;
                rep->flags |= W8_ITEM_ENTITY_PULSE_INCREASING;
            }
        }
        SetHighlight(true);
    }
}

// FUNCTION: WIZ8 0x0049FB30
srBoundingBox W8Item::GetCachedLocalBounds() const
{
    const auto* rep = static_cast<W8ItemRep*>(m_pRep);
    return {rep->bounds_minimum, rep->bounds_maximum};
}

// FUNCTION: WIZ8 0x0049FB80
float W8Item::GetBoundsRadius() const
{
    return static_cast<W8ItemRep*>(m_pRep)->bounds_radius;
}

/* Raise the representation location by two thirds of the cached local height.
   Searchables, the automap, and the inlined selection test all use this point. */
static const float g_item_bounds_vertical_factor = 0.66f;

// FUNCTION: WIZ8 0x0049FBA0
unsigned char W8Item::GetSearchPosition(srVector3T<float>* location)
{
    const auto bounds = GetCachedLocalBounds();
    m_pRep->GetLocation(location);
    location->y += (bounds.maximum.y - bounds.minimum.y) * g_item_bounds_vertical_factor;
    return 1;
}

// FUNCTION: WIZ8 0x0049FBF0
void W8Item::SetYaw(float angle)
{
    glm::mat3 rotation(glm::eulerAngleY(static_cast<float>(NormalizeAngle(angle))));
    srNode* mesh = static_cast<W8ItemRep*>(m_pRep)->m_psrMesh;
    m_pRep->SetRotation(&rotation);
    if (mesh->first_child_ == 0) {
        mesh->setRotation(rotation);
    } else {
        for (srNode* child = mesh->first_child_; child != 0; child = child->next_sibling_) {
            child->setRotation(rotation);
        }
    }
}

/* The receiver is the item, not its representation. Blue takes precedence
   over green, and retail does not multiply either colour by pulse_level. */
// FUNCTION: WIZ8 0x0049FDB0
void W8Item::SetHighlight(bool enabled)
{
    W8ItemRep* rep = static_cast<W8ItemRep*>(m_pRep);
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 0.0f;
    if (enabled) {
        if ((rep->flags & W8_ITEM_ENTITY_HIGHLIGHT_BLUE) != 0) {
            blue = 1.0f;
            alpha = 1.0f;
        } else if ((rep->flags & W8_ITEM_ENTITY_HIGHLIGHT_GREEN) != 0) {
            green = 1.0f;
            alpha = 1.0f;
        }
    }
    rep->highlight_colour.x = 0.0f;
    rep->highlight_colour.y = green;
    rep->highlight_colour.z = blue;
    rep->highlight_colour.w = alpha;
}

// FUNCTION: WIZ8 0x0049FF40
bool W8Item::IsSelected()
{
    srModelInstance* selected = GetPickedModelInstance();
    srVector3T<float> location;
    /* The selection-point helper is inlined here even though its result is
       not used by the final mesh-identity test. */
    GetSearchPosition(&location);
    return selected == static_cast<W8ItemRep*>(m_pRep)->m_psrMesh;
}

// FUNCTION: WIZ8 0x0049FFA0
float W8Item::DistanceToCamera(W8World* world)
{
    const auto bounds = GetCachedLocalBounds();
    srVector3T<double> camera = world->camera->getLocation();
    srVector3T<float> camera_location;
    camera_location = glm::vec3(camera.glmValue());
    srVector3T<float> center = m_pRep->location;
    /* Unlike the selection-point helper, retail offsets Z by half the Z
       extent here, not Y by 0.66 of the Y extent. */
    center.z += (bounds.maximum.z - bounds.minimum.z) * 0.5f;
    return (camera_location - center).Length();
}

// FUNCTION: WIZ8 0x004A0030
void W8Item::LightRadarBlip()
{
    countdown = w8_get_ticks() + (10000);
}

/* Run an item's trigger without a source. Action states 1 and 4 return zero
   so a world-item interaction waits instead of taking the item. */
// FUNCTION: WIZ8 0x004A0070
unsigned char RunItemTrigger(W8Item* item)
{
    Trigger* trigger;

    if (item == 0) {
        return 0;
    }
    trigger = item->trigger;
    if (trigger == 0) {
        return 1;
    }
    trigger->Run(-1);
    if (trigger->action_state == 1 || trigger->action_state == 4) {
        return 0;
    }
    return 1;
}

// FUNCTION: WIZ8 0x004A00C0
std::optional<srBoundingBox> GetItemWorldBounds(const W8Item* item)
{
    if (item == 0) {
        return std::nullopt;
    }
    auto bounds = item->GetCachedLocalBounds();
    srVector3T<float> location;
    item->m_pRep->GetLocation(&location);
    bounds.minimum += location.glmValue();
    bounds.maximum += location.glmValue();
    return bounds;
}
