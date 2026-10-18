#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "compat/compiler.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "surrender/srBoundingBox.h"
#include "wiz8/engine_code/GrObject.h"

struct W8World;
struct W8Item;
class Trigger;
class srModelInstance;
class stModelInstance;
class srNode;

/* Bit masks in W8Item::flags and the saved W8WorldItem::entity_flags. */
enum {
    W8_ITEM_ENTITY_PULSE = 0x01u,
    W8_ITEM_ENTITY_ROTATE = 0x02u,
    W8_ITEM_ENTITY_NO_PICKUP = 0x04u,
    W8_ITEM_ENTITY_RADAR_SEEN = 0x08u,
    W8_ITEM_ENTITY_HIGHLIGHT_GREEN = 0x10u,
    W8_ITEM_ENTITY_PULSE_INCREASING = 0x20u,
    W8_ITEM_ENTITY_MESH_LOADED = 0x40u,
    W8_ITEM_ENTITY_HIGHLIGHT_BLUE = 0x80u
};

struct W8Item : public W8GrObject {
    W8Item();
    virtual ~W8Item() override;
    W8Item(const W8Item&) = delete;
    W8Item& operator=(const W8Item&) = delete;

    glm::vec3 location{0.0f};
    glm::vec3 local_location{0.0f};
    glm::vec3 parent_location{0.0f};
    glm::mat3 rotation{1.0f};
    glm::vec4 highlight_colour{0.0f};
    std::unique_ptr<stModelInstance> m_psrMesh;
    glm::vec3 bounds_minimum{0.0f};
    glm::vec3 bounds_maximum{0.0f};
    float bounds_radius = 0.0f;
    float pulse_level = 0.0f;
    unsigned int flags = 0;
    Trigger* trigger = nullptr;
    uint64_t countdown = 0;

    void RefreshBounds();
    unsigned int SetFlags(unsigned int mask, bool enabled);
    void SetLocation(const glm::vec3* new_location);
    glm::vec3 GetLocation() const { return location; }
    glm::vec3 GetLocalLocation() const { return local_location; }
    void SetRotation(const glm::mat3* new_rotation) { rotation = *new_rotation; }
    glm::mat3 GetRotation() const { return rotation; }

    void DetachMesh();
    void ApplyRepTransform();
    void AttachMesh(W8World* world);
    void UpdateAnimation();
    srNode* GetMesh();
    srBoundingBox GetCachedLocalBounds() const;
    float GetBoundsRadius() const { return bounds_radius; }
    glm::vec3 GetSearchPosition() const;
    void SetYaw(float angle);
    void SetHighlight(bool enabled, bool hovered = false);
    bool IsSelected();
    void LightRadarBlip();
    bool IsRadarBlipLit();
};

unsigned char RunItemTrigger(W8Item* item);
std::optional<srBoundingBox> GetItemWorldBounds(const W8Item* item);
std::unique_ptr<W8Item> LoadItemFromFile(std::string_view name);
