#include "wiz8/entities/native_object.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <utility>

#include <glm/ext/matrix_double4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/geometric.hpp>

#include "wiz8/filesystem.h"
#include "wiz8/game.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/material.h"
#include "wiz8/render/native_content.h"
#include "wiz8/render/vsg_scene.h"
#include "wiz8/rules/events.h"
#include "wiz8/world/world.h"

NativeObject::NativeObject(World& world, const std::filesystem::path& file)
    : name(wiz8::path_to_utf8(file.stem())), world(world), material(std::make_shared<Material>())
{
    material->setAmbient({.2f, .7f, .9f, 1});
    material->setDiffuse({.2f, .7f, .9f, 1});
    material->setSpecular(glm::vec4(0));
    material->setEmissive(glm::vec4(0));
    node = render::loadGameContent(file, material);
    // A world-fixed object ahead of the party at entry, clear of the view near plane.
    const auto near = world.camera->getClipRange().first;
    const auto camera = glm::translate(glm::dmat4(1), glm::dvec3(world.party.eye())) *
                        glm::dmat4(world.party.rotation());
    node->matrix = render::toVsg(
        glm::scale(glm::translate(camera, glm::dvec3(0, 0, near * 2)), glm::dvec3(near * .5)));
    world.scene->root->addChild(node);
}

NativeObject::~NativeObject()
{
    world.picked = {}; // Its node path borrows nodes until graph mutation.
    std::erase(world.scene->root->children, node);
}

void NativeObject::activate()
{
    illuminated = !illuminated;
    material->setEmissive(illuminated ? glm::vec4(.15f, .3f, .4f, 1) : glm::vec4(0));
}

NativeObject* SelectedNativeObject(const World& world)
{
    if (!world.picked.hit) return nullptr;
    const auto& hit = *world.picked.hit;
    const auto point = hit.worldIntersection;
    if (glm::distance(glm::dvec3(point.x, point.y, point.z), glm::dvec3(world.party.eye())) > 5000)
        return nullptr;
    for (const auto& object : world.native_objects)
        if (std::ranges::find(hit.nodePath, object->node.get()) != hit.nodePath.end())
            return object.get();
    return nullptr;
}

void LoadNativeLevelObject(Game& game)
{
    if (game.native_object_file.empty() || !game.level.primary) return;
    auto& world = *game.level.primary;
    world.native_objects.push_back(std::make_unique<NativeObject>(world, game.native_object_file));
}

NativeObject* Game::interactiveNativeObject()
{
    if (!activity.in_world() || advancement.held || party.held.item.iItemNo != -1 || !level.primary)
        return nullptr;
    return SelectedNativeObject(*level.primary);
}

bool Game::activateSelectedNativeObject()
{
    auto* object = interactiveNativeObject();
    if (!object) return false;
    object->activate();
    events.push(TextNotice{W8_FONT_PALETTE_BEIGE,
                           "The " + object->name + (object->illuminated ? " glows." : " dims.")});
    return true;
}
