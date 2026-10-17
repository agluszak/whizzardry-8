#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "compat/compiler.h"
#include "glm/ext/vector_float3.hpp"
#include "surrender/srBoundingBox.h"
#include "wiz8/engine_code/AnimRep.hpp"
#include "wiz8/engine_code/GrObject.h"

struct W8World;
struct W8Item;
class Trigger;
class srModelInstance;
class stModelInstance;
class srNode;

/* Bit masks in W8ItemRep::flags and the saved W8WorldItem::entity_flags.
   The world item's own flags are a separate domain. */
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

/* Engine Code\Item.cpp. The assertion expressions establish the original
   m_pRep and m_psrMesh names; the bodies establish their offsets. */
struct W8ItemRep : public W8AnimRepBase {
    W8ItemRep();
    virtual ~W8ItemRep() override;

    std::unique_ptr<stModelInstance> m_psrMesh;
    glm::vec3 bounds_minimum;         /* 0x68 */
    glm::vec3 bounds_maximum;         /* 0x74 */
    float bounds_radius;              /* 0x80 */
    float pulse_level; /* 0x88 */
    unsigned int flags; /* 0x90 */

    void RefreshBounds();
    unsigned int SetFlags(unsigned int mask, bool enabled); /* 0x0049F310 */
};

struct W8Item : public W8GrObject {
    W8Item();
    virtual ~W8Item() override;
    W8Item(const W8Item&) = default;
    W8Item& operator=(const W8Item&) = delete;

    Trigger* trigger;
    /* 0x1c: countdown clock; IsTicking reports whether it is running. */
    uint64_t countdown;

    void DetachMesh();
    void ApplyRepTransform();
    void AttachMesh(W8World* world);
    void UpdateAnimation();
    void SetLocation(const glm::vec3* location);
    srNode* GetMesh();
    /* Model-local cached bounds. GetItemWorldBounds translates them by the
       representation's location without rotating them. */
    srBoundingBox GetCachedLocalBounds() const;
    float GetBoundsRadius() const;
    /* Live search/interaction point: representation location raised by 0.66 of
       the cached local Y extent. */
    glm::vec3 GetSearchPosition() const;
    void SetYaw(float angle);
    void SetHighlight(bool enabled);
    bool IsSelected();
    void LightRadarBlip();
    bool IsRadarBlipLit();
};

/* Run the item's trigger, if any, and report its action state. */
unsigned char RunItemTrigger(W8Item* item); /* 0x004A0070 */
std::optional<srBoundingBox> GetItemWorldBounds(const W8Item* item);

std::unique_ptr<W8Item> LoadItemFromFile(std::string_view name);
