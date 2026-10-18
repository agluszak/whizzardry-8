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

static constexpr float kItemBoundsVerticalFactor = 0.66f;

W8Item::W8Item()
{
    flags = W8_ITEM_ENTITY_PULSE_INCREASING;
    pulse_level = static_cast<float>(Random(20) * 0.05);
    trigger = nullptr;
    kind = 2;
    id = AllocateGrObjectId();
    countdown = w8_get_ticks();
}

W8Item::~W8Item() = default;

void W8Item::RefreshBounds()
{
    const auto box = m_psrMesh->getModel()->getBounds().box;
    bounds_minimum = box.minimum;
    bounds_maximum = box.maximum;
    bounds_radius = static_cast<float>(glm::distance(bounds_maximum, bounds_minimum) * 0.5);
}

unsigned int W8Item::SetFlags(unsigned int mask, bool enabled)
{
    if (enabled) {
        flags |= mask;
    } else {
        flags &= ~mask;
    }
    return flags;
}

void W8Item::SetLocation(const glm::vec3* new_location)
{
    local_location = *new_location;
    location = parent_location + local_location;
}

void W8Item::DetachMesh()
{
    srNode* mesh = m_psrMesh.get();
    if (!mesh) {
        Fail("psrMesh");
    }
    mesh->setFlag(srNode::FLAG_DISABLE);
    mesh->setParent(nullptr, 0);
}

void W8Item::ApplyRepTransform()
{
    if (!m_psrMesh) {
        Fail("m_psrMesh");
    }
    m_psrMesh->setLocation(glm::dvec3(location));
    m_psrMesh->setRotation(rotation);
}

void W8Item::AttachMesh(W8World* world)
{
    if (!world) {
        Fail("pWorld");
    }
    if ((flags & W8_ITEM_ENTITY_MESH_LOADED) == 0) {
        return;
    }
    srNode* mesh = m_psrMesh.get();
    if (!mesh) {
        Fail("psrMesh");
    }
    if (mesh->getChild() == nullptr) {
        mesh->clearFlag(srNode::FLAG_DISABLE);
    }
    mesh->setParent(world->dynamic_scene, 0);
    const glm::dvec3 widened(location);
    if (srNode* child = mesh->getChild(); child == nullptr) {
        mesh->setLocation(widened);
        mesh->setRotation(rotation);
    } else {
        for (; child != nullptr; child = child->getNext()) {
            child->setLocation(widened);
            child->setRotation(rotation);
        }
    }
}

srNode* W8Item::GetMesh()
{
    return m_psrMesh.get();
}

bool W8Item::IsRadarBlipLit()
{
    return TimeUntilDeadline(countdown) != 0;
}

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
    item->m_psrMesh = std::move(mesh);
    item->flags |= W8_ITEM_ENTITY_MESH_LOADED;
    item->RefreshBounds();
    return item;
} catch (const std::exception&) {
    return {};
}

void W8Item::UpdateAnimation()
{
    if ((flags & W8_ITEM_ENTITY_MESH_LOADED) == 0) {
        return;
    }
    if ((flags & W8_ITEM_ENTITY_ROTATE) != 0 && ((GetInputState().modifiers & CTRL_DOWN) == 0)) {
        rotation *= glm::mat3(glm::eulerAngleY(-0.1963495375f));
        srNode* mesh = m_psrMesh.get();
        if (mesh->getChild() == nullptr) {
            mesh->setRotation(rotation);
        } else {
            for (srNode* child = mesh->getChild(); child != nullptr; child = child->getNext()) {
                child->setRotation(rotation);
            }
        }
    }

    if ((flags & W8_ITEM_ENTITY_PULSE) != 0) {
        if ((flags & W8_ITEM_ENTITY_PULSE_INCREASING) != 0) {
            pulse_level += 0.05f;
            if (pulse_level >= 1.0f) {
                pulse_level = 1.0f;
                flags &= ~W8_ITEM_ENTITY_PULSE_INCREASING;
            }
        } else {
            pulse_level -= 0.05f;
            if (pulse_level <= 0.25f) {
                pulse_level = 0.25f;
                flags |= W8_ITEM_ENTITY_PULSE_INCREASING;
            }
        }
        SetHighlight(true);
    }
}

srBoundingBox W8Item::GetCachedLocalBounds() const
{
    return {bounds_minimum, bounds_maximum};
}

glm::vec3 W8Item::GetSearchPosition() const
{
    const auto bounds = GetCachedLocalBounds();
    auto pos = location;
    pos.y += (bounds.maximum.y - bounds.minimum.y) * kItemBoundsVerticalFactor;
    return pos;
}

void W8Item::SetYaw(float angle)
{
    rotation = glm::mat3(glm::eulerAngleY(static_cast<float>(NormalizeAngle(angle))));
    srNode* mesh = m_psrMesh.get();
    if (mesh->getChild() == nullptr) {
        mesh->setRotation(rotation);
    } else {
        for (srNode* child = mesh->getChild(); child != nullptr; child = child->getNext()) {
            child->setRotation(rotation);
        }
    }
}

void W8Item::SetHighlight(bool enabled, bool hovered)
{
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 0.0f;
    if (enabled) {
        if ((flags & W8_ITEM_ENTITY_HIGHLIGHT_BLUE) != 0) {
            blue = 1.0f;
            alpha = 1.0f;
        } else if (hovered) {
            green = 1.0f;
            alpha = 1.0f;
        }
    }
    highlight_colour = glm::vec4(0.0f, green, blue, alpha);
}

bool W8Item::IsSelected()
{
    return g_current_model_instance == m_psrMesh.get();
}

void W8Item::LightRadarBlip()
{
    countdown = w8_get_ticks() + 10000;
}

unsigned char RunItemTrigger(W8Item* item)
{
    if (!item) {
        return 0;
    }
    Trigger* trigger = item->trigger;
    if (!trigger) {
        return 1;
    }
    trigger->Run(-1);
    if (trigger->action_state == 1 || trigger->action_state == 4) {
        return 0;
    }
    return 1;
}

std::optional<srBoundingBox> GetItemWorldBounds(const W8Item* item)
{
    if (!item) {
        return std::nullopt;
    }
    auto bounds = item->GetCachedLocalBounds();
    bounds.minimum += item->location;
    bounds.maximum += item->location;
    return bounds;
}
