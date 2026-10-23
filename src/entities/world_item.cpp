#include "wiz8/entities/world_item.h"

#include <cassert>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>
#include <glm/geometric.hpp> // NOLINT(misc-include-cleaner)
#include <glm/gtx/euler_angles.hpp>

#include "asset_paths.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/filesystem.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/platform/input.h"
#include "wiz8/platform/random.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/mesh_loader.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/utility.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

static constexpr float kItemBoundsVerticalFactor = 0.66f;

WorldItemEntity::WorldItemEntity()
{
    flags = W8_ITEM_ENTITY_PULSE_INCREASING;
    pulse_level = static_cast<float>(PresentationRng().below(20) * 0.05);
    countdown = WorldClockMilliseconds();
}

WorldItemEntity::~WorldItemEntity() = default;

void WorldItemEntity::RefreshBounds()
{
    const auto box = m_psrMesh->getModel()->getBounds().box;
    bounds_minimum = box.minimum;
    bounds_maximum = box.maximum;
    bounds_radius = static_cast<float>(glm::distance(bounds_maximum, bounds_minimum) * 0.5);
}

unsigned int WorldItemEntity::SetFlags(unsigned int mask, bool enabled)
{
    if (enabled) {
        flags |= mask;
    } else {
        flags &= ~mask;
    }
    return flags;
}

void WorldItemEntity::SetLocation(const glm::vec3* new_location)
{
    local_location = *new_location;
    location = parent_location + local_location;
}

void WorldItemEntity::DetachMesh()
{
    MeshInstance* mesh = m_psrMesh.get();
    assert(mesh);
    mesh->visible = false;
    mesh->setScene(nullptr);
}

void WorldItemEntity::ApplyRepTransform()
{
    assert(m_psrMesh);
    m_psrMesh->transform.setLocation(glm::dvec3(location));
    m_psrMesh->transform.setRotation(rotation);
}

void WorldItemEntity::AttachMesh(World* world)
{
    assert(world);
    if ((flags & W8_ITEM_ENTITY_MESH_LOADED) == 0) {
        return;
    }
    MeshInstance* mesh = m_psrMesh.get();
    assert(mesh);
    mesh->visible = true;
    mesh->setScene((world->scene.get()));
    const glm::dvec3 widened(location);

    mesh->transform.setLocation(widened);
    mesh->transform.setRotation(rotation);
}

MeshInstance* WorldItemEntity::GetMesh()
{
    return m_psrMesh.get();
}

bool WorldItemEntity::IsRadarBlipLit()
{
    return WorldClockMilliseconds() < countdown;
}

std::unique_ptr<WorldItemEntity> LoadItemFromFile(std::string_view name)
try {
    const auto filename = fmt::format("data/items3d/{}.itm", name);
    auto file = wiz8::open_file(wiz8::asset_path(filename));
    unsigned char version;
    file->read_exact(&version, sizeof(version));
    if (version != 2) return {};
    auto mesh = ReadSingleLevelMesh(*file, wiz8::asset_path("data/items3d/bitmaps"));
    if (!mesh) return {};
    mesh->setName("Item mesh instance");
    mesh->render_flags |= MeshInstance::RENDER_SHADOW;
    SetModelInstanceExclusionMask(mesh.get(), 4);
    auto item = std::make_unique<WorldItemEntity>();
    item->m_psrMesh = std::move(mesh);
    item->flags |= W8_ITEM_ENTITY_MESH_LOADED;
    item->RefreshBounds();
    return item;
} catch (const std::exception&) {
    return {};
}

void WorldItemEntity::UpdateAnimation()
{
    if ((flags & W8_ITEM_ENTITY_MESH_LOADED) == 0) {
        return;
    }
    if ((flags & W8_ITEM_ENTITY_ROTATE) != 0 && ((GetInputState().modifiers & CTRL_DOWN) == 0)) {
        rotation *= glm::mat3(glm::eulerAngleY(-0.1963495375f));
        MeshInstance* mesh = m_psrMesh.get();

        mesh->transform.setRotation(rotation);
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

BoundingBox WorldItemEntity::GetCachedLocalBounds() const
{
    return {bounds_minimum, bounds_maximum};
}

glm::vec3 WorldItemEntity::GetSearchPosition() const
{
    const auto bounds = GetCachedLocalBounds();
    auto pos = location;
    pos.y += (bounds.maximum.y - bounds.minimum.y) * kItemBoundsVerticalFactor;
    return pos;
}

void WorldItemEntity::SetYaw(float angle)
{
    rotation = glm::mat3(glm::eulerAngleY(static_cast<float>(NormalizeAngle(angle))));
    MeshInstance* mesh = m_psrMesh.get();

    mesh->transform.setRotation(rotation);
}

void WorldItemEntity::SetHighlight(bool enabled, bool hovered)
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

void WorldItemEntity::LightRadarBlip()
{
    countdown = WorldClockMilliseconds() + 10000;
}

BoundingBox GetItemWorldBounds(const WorldItemEntity& item)
{
    auto bounds = item.GetCachedLocalBounds();
    bounds.minimum += item.location;
    bounds.maximum += item.location;
    return bounds;
}
