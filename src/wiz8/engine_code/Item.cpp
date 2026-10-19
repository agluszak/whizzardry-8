#include "wiz8/engine_code/Item.h"

#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>
#include <glm/geometric.hpp> // NOLINT(misc-include-cleaner)
#include <glm/gtx/euler_angles.hpp>

#include "GrObject.h"
#include "asset_paths.h"
#include "compat/native.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "input.h"
#include "layouts/world.h"
#include "random.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srDebug.h"
#include "surrender/srModel.h"
#include "surrender/srModelInstance.h"
#include "surrender/srNode.h"
#include "timer.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/filesystem.h"
#include "wiz8/utility.h"

// VTABLE: WIZ8 0x005ECD78 W8Item
// class W8Item

// FUNCTION: WIZ8 0x0049F440
W8Item::~W8Item() = default;

/* Detach the owned mesh from its actual scene parent. */
// FUNCTION: WIZ8 0x0049fa30
void W8Item::DetachMesh()
{
    srNode* mesh;

    mesh = m_pRep->m_psrMesh.get();
    if (mesh == 0) {
        srAssertFail("psrMesh");
    }
    mesh->setFlag(srNode::FLAG_DISABLE);
    mesh->setParent(0, 0);
}

/* Copy the representation's float transform onto its SurRender node. The
   location is widened because the exported node setter takes doubles. */
// FUNCTION: WIZ8 0x0049faa0
void W8Item::ApplyRepTransform()
{
    glm::vec3 location;
    glm::dvec3 widened;
    glm::mat3 rotation;
    srNode* mesh;

    if (m_pRep->m_psrMesh == 0) {
        srAssertFail("m_pRep->m_psrMesh");
    }
    mesh = m_pRep->m_psrMesh.get();
    location = m_pRep->GetLocation();
    widened = glm::dvec3(location);
    mesh->setLocation(widened);
    rotation = m_pRep->GetRotation();
    mesh->setRotation(rotation);
}

// FUNCTION: WIZ8 0x0049F900
void W8Item::AttachMesh(W8World* world)
{
    srNode* mesh;
    srNode* child;
    glm::vec3 location;
    glm::dvec3 widened;
    glm::mat3 rotation;

    if (world == 0) {
        srAssertFail("pWorld");
    }
    if ((m_pRep->flags & W8_ITEM_ENTITY_MESH_LOADED) == 0) {
        return;
    }
    mesh = m_pRep->m_psrMesh.get();
    if (mesh == 0) {
        srAssertFail("psrMesh");
    }
    if (mesh->getChild() == 0) {
        mesh->clearFlag(srNode::FLAG_DISABLE);
    }
    mesh->setParent(world->dynamic_scene, 0);
    location = m_pRep->GetLocation();
    rotation = m_pRep->GetRotation();
    child = mesh->getChild();
    if (child == 0) {
        widened = glm::dvec3(location);
        mesh->setLocation(widened);
        mesh->setRotation(rotation);
        return;
    }
    do {
        widened = glm::dvec3(location);
        child->setLocation(widened);
        child->setRotation(rotation);
        child = child->getNext();
    } while (child != 0);
}

// FUNCTION: WIZ8 0x0049fb20
srNode* W8Item::GetMesh()
{
    return m_pRep->m_psrMesh.get();
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
void W8Item::SetLocation(const glm::vec3* location)
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

/* Mesh ownership starts empty and is committed by ReadFromFile. */
W8ItemRep::W8ItemRep()
{
    flags = 0;
    pulse_level = static_cast<float>(Random(20) * 0.05);
    flags |= W8_ITEM_ENTITY_PULSE_INCREASING;
}

// FUNCTION: WIZ8 0x0049F120
W8ItemRep::~W8ItemRep() = default;

// FUNCTION: WIZ8 0x0049F2B0
void W8ItemRep::RefreshBounds()
{
    const auto box = m_psrMesh->getModel()->getBounds().box;
    bounds_minimum = box.minimum;
    bounds_maximum = box.maximum;
    bounds_radius = static_cast<float>(glm::distance(bounds_maximum, bounds_minimum) * 0.5);
}

// FUNCTION: WIZ8 0x0049F350
W8Item::W8Item()
{
    trigger = 0;
    kind = 2;
    id = AllocateGrObjectId();
    countdown = w8_get_ticks() + (0);
}

// Decode the item resource directly; no level context or temporary directory mutation.
std::unique_ptr<W8Item> LoadItemFromFile(std::string_view name)
try {
    const auto filename = fmt::format("data/items3d/{}.itm", name);
    auto file = wiz8::open_file(wiz8::asset_path(filename));
    unsigned char version;
    file->read_exact(&version, sizeof(version));
    if (version != 2) return {};
    auto mesh = ReadSingleLevelMesh(*file, wiz8::asset_path("data/items3d/bitmaps"));
    if (!mesh) return {};
    mesh->setName("Item mesh instance");
    mesh->render_flags |= stModelInstance::RENDER_SHADOW;
    SetModelInstanceChainExclusionMask(mesh.get(), 4);
    auto item = std::make_unique<W8Item>();
    auto& representation = *item->m_pRep;
    representation.m_psrMesh = std::move(mesh);
    representation.flags |= W8_ITEM_ENTITY_MESH_LOADED;
    representation.RefreshBounds();
    return item;
} catch (const std::exception&) {
    return {};
}

// FUNCTION: WIZ8 0x0049F730
void W8Item::UpdateAnimation()
{
    if (m_pRep == 0) {
        srAssertFail("m_pRep");
    }
    W8ItemRep* rep = m_pRep.get();
    if ((rep->flags & W8_ITEM_ENTITY_MESH_LOADED) == 0) {
        return;
    }
    if ((rep->flags & W8_ITEM_ENTITY_ROTATE) != 0 &&
        ((GetInputState().modifiers & CTRL_DOWN) == 0)) {
        glm::mat3 rotation;
        rotation = m_pRep->GetRotation();
        rotation *= glm::mat3(glm::eulerAngleY(-0.1963495375f));
        srNode* mesh = rep->m_psrMesh.get();
        m_pRep->SetRotation(&rotation);
        if (mesh->getChild() == 0) {
            mesh->setRotation(rotation);
        } else {
            for (srNode* child = mesh->getChild(); child != 0; child = child->getNext()) {
                child->setRotation(rotation);
            }
        }
    }

    rep = m_pRep.get();
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
    const auto* rep = m_pRep.get();
    return {rep->bounds_minimum, rep->bounds_maximum};
}

// FUNCTION: WIZ8 0x0049FB80
float W8Item::GetBoundsRadius() const
{
    return m_pRep->bounds_radius;
}

/* Raise the representation location by two thirds of the cached local height.
   Searchables, the automap, and the inlined selection test all use this point. */
static const float g_item_bounds_vertical_factor = 0.66f;

// FUNCTION: WIZ8 0x0049FBA0
glm::vec3 W8Item::GetSearchPosition() const
{
    const auto bounds = GetCachedLocalBounds();
    auto location = m_pRep->GetLocation();
    location.y += (bounds.maximum.y - bounds.minimum.y) * g_item_bounds_vertical_factor;
    return location;
}

// FUNCTION: WIZ8 0x0049FBF0
void W8Item::SetYaw(float angle)
{
    glm::mat3 rotation(glm::eulerAngleY(static_cast<float>(NormalizeAngle(angle))));
    srNode* mesh = m_pRep->m_psrMesh.get();
    m_pRep->SetRotation(&rotation);
    if (mesh->getChild() == 0) {
        mesh->setRotation(rotation);
    } else {
        for (srNode* child = mesh->getChild(); child != 0; child = child->getNext()) {
            child->setRotation(rotation);
        }
    }
}

/* The receiver is the item, not its representation. Blue takes precedence
   over green, and retail does not multiply either colour by pulse_level. */
// FUNCTION: WIZ8 0x0049FDB0
void W8Item::SetHighlight(bool enabled, bool hovered)
{
    W8ItemRep* rep = m_pRep.get();
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 0.0f;
    if (enabled) {
        if ((rep->flags & W8_ITEM_ENTITY_HIGHLIGHT_BLUE) != 0) {
            blue = 1.0f;
            alpha = 1.0f;
        } else if (hovered) {
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
    srModelInstance* selected = g_current_model_instance;
    return selected == m_pRep->m_psrMesh.get();
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
    const auto location = item->m_pRep->GetLocation();
    bounds.minimum += location;
    bounds.maximum += location;
    return bounds;
}
