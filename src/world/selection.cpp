#include <algorithm>
#include <functional>
#include <string_view>

#include "wiz8/render/clip_plane.h"
#include "wiz8/render/light.h"
#include "wiz8/world/world.h"

Light* FindWorldLight(World& world, std::string_view name)
{
    if (!world.scene) return nullptr;
    const auto& lights = world.scene->lights;
    const auto found = std::find_if(lights.rbegin(), lights.rend(),
                                    [&](const auto* light) { return light->name == name; });
    return found != lights.rend() ? *found : nullptr;
}

Light* FindWorldLight(std::string_view name)
{
    for (auto* world : {GetWorld(), GetSecondaryWorld()})
        if (world)
            if (auto* light = FindWorldLight(*world, name)) return light;
    return nullptr;
}

ClipPlane* FindWorldClipPlane(std::string_view name)
{
    for (auto* world : {GetWorld(), GetSecondaryWorld()}) {
        if (!world || !world->scene) continue;
        const auto& planes = world->scene->clip_planes;
        const auto found = std::find_if(planes.rbegin(), planes.rend(),
                                        [&](const auto& plane) { return plane->name == name; });
        if (found != planes.rend()) return found->get();
    }
    return nullptr;
}

void ForEachWorldLight(std::function_ref<void(Light*)> visit)
{
    for (auto* world : {GetWorld(), GetSecondaryWorld()})
        if (world && world->scene)
            for (auto* light : world->scene->lights) visit(light);
}
