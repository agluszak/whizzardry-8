#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_video.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <vsg/nodes/Group.h>
#include <vsg/state/ViewportState.h>

#include "harness.h"
#include "wiz8/application.h"
#include "wiz8/cursor.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/dialog_code/AssayDialog.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/entities/native_object.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/game.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/live_party.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/AutomapScreen.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/MGSFormation.h"
#include "wiz8/local_screens/MGSPortraits.h"
#include "wiz8/local_screens/MGSRadarMap.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MGSSpellIcons.h"
#include "wiz8/local_screens/MGSUseItemSelect.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/MainMenuScreen.h"
#include "wiz8/local_screens/OptionsScreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/mgs_commands.h"
#include "wiz8/pointer.h"
#include "wiz8/render/compositor.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/material_loader.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/renderer.h"
#include "wiz8/render/vsg_scene.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/runtime_test_hooks.h"
#include "wiz8/screen.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/ui_input.h"
#include "wiz8/world/camera_pose.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/world.h"

namespace {

/* Controls whose owners keep them private, by layout position. The camp's
   items page: its first pool cell (a 2x4 grid of 49x57 cells), the first two
   backpack cells, the wielded weapon and the action strip's buttons. */
constexpr SDL_Point kCampPoolCell{535, 219};
constexpr SDL_Point kCampBackpackCells[] = {{34, 219}, {83, 219}};
constexpr SDL_Point kCampPrimaryWeapon{175, 360};
constexpr SDL_Point kCampUseItem{145, 466};
constexpr SDL_Point kCampCastOnItem{240, 466}; // Not the artifacts-skill button beside it.
constexpr SDL_Point kCampCastOnCharacter{286, 466};
/* The camp header's party portraits. */
SDL_Point CampPortrait(int slot)
{
    return {(slot & 1) * 48 + 28, (slot / 2) * 39 + 23};
}
/* The main game's level-up button on a party portrait. */
SDL_Point PortraitAdvanceButton(int slot)
{
    return {((slot & 1) ? 0x23b : 0) + 0x22, (slot / 2) * 0x55 + 0x4f};
}
/* The first row of the spell list in the main game's text box. */
constexpr SDL_Point kSpellListFirstRow{250, 372};
/* The journal's page toggle and close buttons. */
constexpr SDL_Point kJournalToggle{540, 450};
constexpr SDL_Point kJournalClose{600, 450};

void InventoryChestScenario()
{
    const float motion_clamp = g_camera_motion_clamp;
    const float motion_divisor = g_camera_motion_divisor;
    GameHarness game(false);
    StartPremadeGame(game);
    Settle(game, "settle-before-chest");
    Trigger* chest = nullptr;
    for (const auto& trigger : GetWorld()->triggers)
        if (trigger->initial_action == 3 && trigger->inline_action_data[0]) {
            trigger->GenerateItemGroup();
            if (trigger->world_item_group && !trigger->world_item_group->contents.empty()) {
                chest = trigger.get();
                break;
            }
        }
    REQUIRE(chest);
    // Loot is created at its owning chest, never at an uninitialized vector.
    const auto check_position = [&](const W8WorldItem& item) {
        REQUIRE(item.position.x == chest->position.x);
        REQUIRE(std::isfinite(item.position.y));
        REQUIRE(item.position.z == chest->position.z);
    };
    check_position(*chest->world_item_group);
    for (const auto& item : chest->world_item_group->contents) check_position(*item);
    // Request the same picker that an opened chest raises. This isolates loot
    // handling from the chest's lock/trap skill rolls and prop animation.
    chest->flags |= W8_TRIGGER_ITEM_PICKER;
    game.wait("chest-picker", [] { return g_modal_owner != nullptr; });
    game.frame();
    REQUIRE(CurrentGame().advancement.held);
    auto* picker = static_cast<W8TriggerItemPickerDialog*>(g_modal_owner.get());
    REQUIRE_FALSE(picker->entries.empty());
    game.click("assay-picked-item", *picker->m_buttons[5], SDL_BUTTON_RIGHT);
    REQUIRE(game.within_frames(
        4, [] { return dynamic_cast<W8AssayDialog*>(g_modal_owner.get()) != nullptr; }));
    game.key(SDLK_ESCAPE);
    REQUIRE(game.within_frames(4, [&] { return g_modal_owner.get() == picker; }));
    REQUIRE_FALSE(picker->entries.empty());
    const auto click_button = [&](int index) { game.click("picker", *picker->m_buttons[index]); };
    // A full pool can still absorb part of an ammunition stack. The picker
    // must retain only the remainder, including after a second failed attempt.
    const auto pool = std::vector<W8ItemInstance>(std::begin(CurrentGame().party.item_pool),
                                                  std::end(CurrentGame().party.item_pool));
    const auto pool_count = CurrentGame().party.item_count;
    const auto original_item = picker->entries.front().item->item;
    const auto picker_count = picker->entries.size();
    int stack_id = -1, nonstack_id = -1;
    for (unsigned i = 0; i < Data().items.size(); ++i) {
        const auto& record = Data().items[i];
        if (record.quantity_kind == W8_ITEM_QUANTITY_STACK && record.maximum_quantity >= 5)
            stack_id = i;
        else if (record.quantity_kind != W8_ITEM_QUANTITY_STACK)
            nonstack_id = i;
        if (stack_id >= 0 && nonstack_id >= 0) break;
    }
    REQUIRE(stack_id >= 0);
    REQUIRE(nonstack_id >= 0);
    auto& remainder = picker->entries.front().item->item;
    remainder = MakeItem(Data(), CurrentGame().rng, stack_id, true, false, false);
    remainder.stack_count = 5;
    W8ItemInstance filler{};
    filler = MakeItem(Data(), CurrentGame().rng, nonstack_id, true, false, false);
    std::fill(std::begin(CurrentGame().party.item_pool), std::end(CurrentGame().party.item_pool),
              filler);
    CurrentGame().party.item_count = 500;
    CurrentGame().party.item_pool[0] = remainder;
    const auto maximum = Data().items[stack_id].maximum_quantity;
    CurrentGame().party.item_pool[0].stack_count = maximum - 2;
    for (auto& entry : picker->entries) entry.selected = false;
    picker->entries.front().selected = true;
    click_button(1);
    REQUIRE(picker->entries.size() == picker_count);
    REQUIRE(CurrentGame().party.item_count == 500);
    REQUIRE(CurrentGame().party.item_pool[0].stack_count == maximum);
    REQUIRE(remainder.stack_count == 3);
    click_button(1);
    REQUIRE(remainder.stack_count == 3);
    remainder = original_item;
    std::copy(pool.begin(), pool.end(), std::begin(CurrentGame().party.item_pool));
    CurrentGame().party.item_count = pool_count;
    SettleActiveParty(CurrentGame());
    for (auto& entry : picker->entries) entry.selected = false;
    click_button(0); // Select all.
    click_button(1); // Take selected items into the party pool.
    game.wait("close-chest-picker", [] { return g_modal_owner == nullptr; });
    REQUIRE(g_modal_owner == nullptr);
    REQUIRE_FALSE(CurrentGame().advancement.held);
    REQUIRE((chest->flags & W8_TRIGGER_ITEM_PICKER) == 0);
    REQUIRE(g_camera_motion_clamp == motion_clamp);
    REQUIRE(g_camera_motion_divisor == motion_divisor);
    Walk(game, "walk-after-chest", 5);
    int potion_slot = -1;
    for (int i = 0; i < 500; ++i)
        if (CurrentGame().party.item_pool[i].iItemNo >= 0 &&
            Data().items[CurrentGame().party.item_pool[i].iItemNo].unidentified_name_index == 102) {
            potion_slot = i;
            break;
        }
    REQUIRE(potion_slot >= 0);
    // Exercise the purple-potion icon with the unidentified overlay as well.
    CurrentGame().party.item_pool[potion_slot].identified = false;
    game.command(W8_MGS_COMMAND_INVENTORY);
    game.wait_screen("inventory", W8_SCREEN_CAMP);
    REQUIRE(g_camera_motion_clamp == motion_clamp);
    REQUIRE(g_camera_motion_divisor == motion_divisor);
    REQUIRE(g_pending_main_menu_message.empty());
    int potion_cell = -1;
    const auto listed = FindScreen<CampScreen>()->items.ListedPoolEntries();
    for (unsigned i = 0; i < std::min<std::size_t>(8, listed.size()); ++i)
        if (listed[i] == potion_slot) potion_cell = i;
    REQUIRE(potion_cell >= 0);
    game.click("pick-up-potion", {kCampPoolCell.x + (potion_cell & 1) * 49,
                                  kCampPoolCell.y + (potion_cell >> 1) * 57});
    REQUIRE((CurrentGame().party.held.item.iItemNo != -1));
    game.click("drop-potion", kCampBackpackCells[0]);
    REQUIRE_FALSE((CurrentGame().party.held.item.iItemNo != -1));
    game.click_at(320, 170); // Keep the cursor outside the grids for pixel comparisons.
    const auto rendered = game.read_frame();
    const auto expected = FrameBufferPixels();
    REQUIRE(!expected.empty());
    unsigned mismatches = 0;
    for (int y = 192; y < 417; ++y)
        for (int x : {11, 60, 512, 561})
            for (int column = x; column < x + 46; ++column)
                for (unsigned shift : {0u, 8u, 16u}) {
                    const auto index = y * SCREEN_WIDTH + column;
                    mismatches += std::abs(int(rendered[index] >> shift & 255) -
                                           int(expected[index] >> shift & 255)) > 1;
                }
    REQUIRE(mismatches == 0);
    Snapshot("inventory", rendered);
    // Right-clicking the wielded longsword opens its item information dialog.
    game.click("assay-sword", kCampPrimaryWeapon, SDL_BUTTON_RIGHT);
    REQUIRE(game.within_frames(4, [] {
        return dynamic_cast<W8AssayDialog*>(FindScreen<CampScreen>()->dialogs.open()) != nullptr;
    }));
    game.key(SDLK_ESCAPE);
    REQUIRE(
        game.within_frames(4, [] { return FindScreen<CampScreen>()->dialogs.open() == nullptr; }));

    const auto slot = PremadeSlot();
    auto& caster = CurrentGame().party.characters[slot];

    // Use a real healing consumable through inventory. It must reach gameplay,
    // heal the user and consume exactly one unit from the carried stack.
    int healing_potion = -1;
    for (unsigned id = 0; id < Data().items.size(); ++id)
        if (Data().items[id].equip_class == W8_ITEM_EQUIP_CLASS_POTION &&
            Data().items[id].spell_id == W8_SPELL_HEAL_WOUNDS &&
            Data().items[id].quantity_kind == W8_ITEM_QUANTITY_STACK) {
            healing_potion = id;
            break;
        }
    REQUIRE(healing_potion >= 0);
    auto& potion = caster.backpack[1];
    potion = MakeItem(Data(), CurrentGame().rng, healing_potion, true, true, false);
    potion.stack_count = 2;
    caster.hp_current = 1;
    FindScreen<CampScreen>()->InvalidateAll();
    game.frame();
    game.click("use-item", kCampUseItem);
    REQUIRE(FindScreen<CampScreen>()->items.action() == W8_CAMP_ITEM_ACTION_USE);
    game.click("use-potion", kCampBackpackCells[1]);
    game.wait_screen("use-potion-return", W8_SCREEN_MAIN_GAME);
    const bool healed = game.within_frames(600, [&] { return caster.hp_current > 1; });
    CAPTURE(ItemUseDraft(), ScreenPointer().targeting(),
            (CurrentGame().party.held.item.iItemNo != -1), potion.stack_count,
            CurrentGame().party.rows[slot].executing.kind);
    Snapshot("potion-use", game.read_frame());
    REQUIRE(healed);
    REQUIRE(potion.iItemNo == healing_potion);
    REQUIRE(potion.stack_count == 1);
    REQUIRE_FALSE(ItemUseDraft());
    Walk(game, "walk-after-inventory", 5);
    const auto start = PartyPosition();
    game.hold_key(SDLK_DOWN, true);
    game.wait(
        "walk-back-after-inventory", [&] { return HorizontalDistance(PartyPosition(), start) > 5; },
        20);
    game.hold_key(SDLK_DOWN, false);
    CameraPose before{}, after{};
    before = partyPose(*GetWorld());
    game.hold_key(SDLK_LEFT, true);
    game.wait(
        "turn-left-after-inventory",
        [&] {
            after = partyPose(*GetWorld());
            return std::abs(after.orientation.yaw - before.orientation.yaw) > 0.1f;
        },
        20);
    game.hold_key(SDLK_LEFT, false);
}

void CampSpellsScenario()
{
    GameHarness game(false);
    StartPremadeGame(game);
    ExpectScreen(game, "premade-main-game");
    game.command(W8_MGS_COMMAND_INVENTORY);
    game.wait_screen("camp-spells", W8_SCREEN_CAMP);
    ExpectScreen(game, "camp");
    // Projecting the model every frame must not erase a captured press, and
    // clicking the selected action cancels the action rather than a widget flag.
    auto* camp = FindScreen<CampScreen>();
    game.press_at(66, 466);
    game.frame();
    REQUIRE(camp->items.action() == W8_CAMP_ITEM_ACTION_NONE);
    game.release_at(66, 466);
    REQUIRE(camp->items.action() == W8_CAMP_ITEM_ACTION_MOVE);
    game.click_at(66, 466);
    REQUIRE(camp->items.action() == W8_CAMP_ITEM_ACTION_NONE);

    // Identify from the inventory action strip must cast on the clicked item,
    // spend mana and show its information, rather than book an empty target.
    auto& caster = CurrentGame().party.characters[PremadeSlot()];
    caster.iProfession = W8_PROFESSION_BISHOP;
    caster.uiExpLevel = 4;
    caster.profession_levels[W8_PROFESSION_BISHOP] = 4;
    for (const auto skill : {W8_SKILL_SPELLBOOK_WIZARDRY, W8_SKILL_MENTAL_MAGIC})
        caster.skills[skill].base_level = caster.skills[skill].level = 100;
    LearnSpell(&caster, W8_SPELL_IDENTIFY_ITEM, false);
    const auto realm = Data().spells[W8_SPELL_IDENTIFY_ITEM].realm;
    caster.sp_max[realm] = caster.iSPLeft[realm] = 100;
    auto& sword = caster.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON];
    REQUIRE(sword.iItemNo >= 0);
    sword.identified = false;
    FindScreen<CampScreen>()->InvalidateAll();
    game.frame();
    game.click("identify-spell", kCampCastOnItem);
    REQUIRE(FindScreen<CampScreen>()->items.action() == W8_CAMP_ITEM_ACTION_USE_ON_ITEM);
    game.click("identify-sword", kCampPrimaryWeapon);
    CAPTURE(caster.profession_levels[W8_PROFESSION_BISHOP],
            caster.skills[W8_SKILL_SPELLBOOK_WIZARDRY].level,
            caster.skills[W8_SKILL_MENTAL_MAGIC].level,
            GetSpellCastRating(&caster, W8_SPELL_IDENTIFY_ITEM, 2), caster.iSPLeft[realm]);
    REQUIRE(sword.identified);
    REQUIRE(caster.iSPLeft[realm] < 100);
    REQUIRE(game.within_frames(4, [] {
        return dynamic_cast<W8AssayDialog*>(FindScreen<CampScreen>()->dialogs.open()) != nullptr;
    }));
    game.key(SDLK_ESCAPE);
    REQUIRE(
        game.within_frames(4, [] { return FindScreen<CampScreen>()->dialogs.open() == nullptr; }));

    // The next automatic cast targets a character, not the remembered item.
    // Automatic power and immediate retries must respect the mana budget.
    LearnSpell(&caster, W8_SPELL_REMOVE_CURSE, false);
    caster.skills[W8_SKILL_SPELLBOOK_DIVINITY].base_level =
        caster.skills[W8_SKILL_SPELLBOOK_DIVINITY].level = 100;
    const auto curse_realm = Data().spells[W8_SPELL_REMOVE_CURSE].realm;
    caster.sp_max[curse_realm] = caster.iSPLeft[curse_realm] = 100;
    caster.uiCondition[W8_CONDITION_HEXED] = 20;
    FindScreen<CampScreen>()->InvalidateAll();
    game.frame();
    // Fix the roll sequence so successful but insufficient attempts repeat.
    CurrentGame().rng.seed(1);
    game.click("remove-curse-spell", kCampCastOnCharacter);
    REQUIRE(FindScreen<CampScreen>()->items.action() == W8_CAMP_ITEM_ACTION_USE_ON_CHARACTER);
    const auto slot = PremadeSlot();
    game.click("remove-curse-target", CampPortrait(slot));
    REQUIRE(caster.iSPLeft[curse_realm] < 100);
    REQUIRE(caster.iSPLeft[curse_realm] >= 0);
    REQUIRE(FindScreen<CampScreen>()->items.action() == W8_CAMP_ITEM_ACTION_NONE);
    REQUIRE(CurrentGame().party.rows[slot].remembered.spell.target.iType ==
            W8_TARGET_KIND_CHARACTER);
    REQUIRE(CurrentGame().party.rows[slot].remembered.spell.target.iChar == slot);
    caster.uiCondition[W8_CONDITION_HEXED] = 0;
}

void CheckWorldRender(GameHarness& game)
{
    World* world = GetWorld();
    unsigned enabled = 0;
    for (unsigned i = 0; i <= world->compiledIndex()->GetMeshCount(); ++i)
        if (world->psrMeshes[i] && world->psrMeshes[i]->visible) ++enabled;
    REQUIRE(enabled > 0);

    // The same scene and UI with only the world pass switched off is the control.
    REQUIRE((g_world_render_enabled && !g_world_blacked_out));
    RenderFrame();
    const unsigned polygons = world->scene->submitted_polygons;
    const auto visible = game.read_frame();
    g_world_render_enabled = 0;
    RenderFrame();
    const auto control = game.read_frame();
    g_world_render_enabled = 1;

    Snapshot("world", visible);
    Snapshot("world-control", control);
    unsigned changed = 0, viewport = 0;
    for (int y = g_viewport.top; y < g_viewport.bottom; ++y)
        for (int x = g_viewport.left; x < g_viewport.right; ++x, ++viewport)
            changed += visible[y * SCREEN_WIDTH + x] != control[y * SCREEN_WIDTH + x];
    std::printf("world: %u meshes, %u polygons, %u/%u viewport pixels from the world\n", enabled,
                polygons, changed, viewport);
    REQUIRE(polygons > 0);
    // The sky is a separate pass and stays in the control frame; the world
    // (ground, cliffs, water at the Monastery start) covers about half the view.
    REQUIRE(changed > viewport / 4);
}

// Both cameras traverse the same retail graph in one visual frame. Neither
// view may age particles or advance a creature's animation a second time.
void CheckTwoWorldViews(GameHarness& game)
{
    auto& scene = *GetWorld()->scene;
    struct ParticleState {
        ParticleSystem* system;
        std::ptrdiff_t active;
        unsigned emitted, integration_tick, emission_tick;
        std::vector<glm::vec3> positions;
    };
    std::vector<ParticleState> particles;
    for (auto* system : scene.particles) {
        particles.push_back(
            {system,
             std::ranges::count(system->particles, true, &ParticleSystem::Particle::active),
             system->emission_count,
             system->last_integration_tick,
             system->last_emission_tick,
             {}});
        for (const auto& particle : system->particles)
            particles.back().positions.push_back(particle.position);
    }
    std::vector<std::pair<MeshInstance*, std::pair<unsigned, float>>> poses;
    for (auto* mesh : scene.dynamic_meshes)
        poses.push_back({mesh, {mesh->frame_index, mesh->frame_interpolation}});
    g_renderer->beginFrame({0, 0, 0, 1});
    const auto stamp = g_renderer->frameNumber();
    const SceneFrame frame{.time = RenderClockSeconds()};
    for (int x : {0, SCREEN_WIDTH / 2})
        g_renderer->addView(scene.createView(
            g_renderer->context(), GetWorld()->camera, frame,
            vsg::ViewportState::create(x, 0, SCREEN_WIDTH / 2, SCREEN_HEIGHT), stamp));
    g_renderer->endFrame(false, true);
    const auto pixels = game.read_frame();
    unsigned colored = 0;
    int max_channel_difference = 0;
    for (int y = 0; y < SCREEN_HEIGHT; ++y)
        for (int x = 0; x < SCREEN_WIDTH / 2; ++x) {
            const auto left = pixels[y * SCREEN_WIDTH + x];
            const auto right = pixels[y * SCREEN_WIDTH + x + SCREEN_WIDTH / 2];
            for (int shift : {0, 8, 16})
                max_channel_difference = std::max(
                    max_channel_difference, std::abs(static_cast<int>((left >> shift) & 255) -
                                                     static_cast<int>((right >> shift) & 255)));
            colored += left != 0;
        }
    // A shifted viewport can round interpolated texture/color values to an
    // adjacent byte even with identical geometry and camera matrices.
    REQUIRE(max_channel_difference <= 1);
    REQUIRE(colored > SCREEN_WIDTH * SCREEN_HEIGHT / 16);
    for (const auto& saved : particles) {
        REQUIRE(std::ranges::count(saved.system->particles, true,
                                   &ParticleSystem::Particle::active) == saved.active);
        REQUIRE(saved.system->emission_count == saved.emitted);
        REQUIRE(saved.system->last_integration_tick == saved.integration_tick);
        REQUIRE(saved.system->last_emission_tick == saved.emission_tick);
        REQUIRE(std::ranges::equal(saved.system->particles, saved.positions, {},
                                   &ParticleSystem::Particle::position));
    }
    for (const auto& [mesh, pose] : poses) {
        REQUIRE(mesh->frame_index == pose.first);
        REQUIRE(mesh->frame_interpolation == pose.second);
    }
}

// Exercise the screen-template behaviours that generic widget dispatch cannot replace.
void CheckGameplayUi(GameHarness& game)
{
    const int slot = PremadeSlot();
    const auto original_formation = CurrentGame().party.formation;
    OpenFormationPanel();
    game.frame();
    const auto seat = CurrentGame().party.formation.cellOf(slot);
    REQUIRE(seat);
    const int source = seat->row * 3 + seat->column;
    const int destination = ((seat->row + 1) % 5) * 3;
    constexpr std::array<SDL_Point, 15> cells{{
        {90, 28},
        {61, 37},
        {119, 37},
        {152, 90},
        {143, 61},
        {143, 119},
        {90, 152},
        {119, 143},
        {61, 143},
        {28, 90},
        {37, 119},
        {37, 61},
        {90, 72},
        {72, 97},
        {108, 97},
    }};
    game.drag({214 + cells[source].x, 60 + cells[source].y, 32, 32},
              {214 + cells[destination].x, 60 + cells[destination].y, 32, 32});
    REQUIRE(CurrentGame().planned_formation.rowOf(slot) == destination / 3);
    game.click("formation-accept", kFormationAccept);
    REQUIRE_FALSE(FormationPanelOpen());
    REQUIRE(CurrentGame().party.formation.rowOf(slot) == destination / 3);
    CurrentGame().party.formation = original_formation;
    RefreshFormationBoard();

    // Give the fixture a real learned spell and mana; exercise pointer routing,
    // realm tooltips and power selection with the licensed spell data.
    auto& character = CurrentGame().party.characters[slot];
    const auto original_character = character;
    character.iProfession = W8_PROFESSION_MAGE;
    character.skills[W8_SKILL_FIRE_MAGIC].level = 100;
    LearnSpell(&character, W8_SPELL_LIGHT, false);
    const auto realm = Data().spells[W8_SPELL_LIGHT].realm;
    character.sp_max[realm] = character.iSPLeft[realm] = 100;
    CurrentGame().advancement.paused = true;
    REQUIRE(OpenSpellCastingView(slot));
    game.frame();
    const auto realm_bounds = gpSCSV->realm_buttons[realm]->Bounds();
    game.move_pointer(0, 200);
    VideoRemoveToolTip();
    game.move_pointer(realm_bounds.x + 5, realm_bounds.y + 5);
    ResetUiHelp(false);
    REQUIRE(HasScreenTransitionObjects());
    game.click("realm", *gpSCSV->realm_buttons[realm]);
    REQUIRE(gpSCSV->iSpellRealm == realm);
    REQUIRE(gpSCSV->spells.size() == 1);
    game.click("choose-spell", kSpellListFirstRow);
    REQUIRE(gpSCSV->draft.detail == W8_SPELL_LIGHT);
    game.click("power-pip", *gpSCSV->power_controls[1]);
    if (gpSCSV) {
        REQUIRE(gpSCSV->draft.detail_block.spell.power_level == 2);
        game.click("cancel-cast", *gpSCSV->cancel);
        REQUIRE(!gpSCSV);
    } else {
        // Target-free spells may commit in the same frame as power selection.
        REQUIRE(CurrentGame().party.rows[slot].remembered.spell.detail == W8_SPELL_LIGHT);
        REQUIRE(CurrentGame().party.rows[slot].remembered.spell.detail_block.spell.power_level ==
                2);
    }
    REQUIRE(CurrentGame().advancement.paused);
    REQUIRE(CurrentGame().advancement.held);
    ToggleMainGamePause();
    REQUIRE_FALSE(CurrentGame().advancement.paused);
    character = original_character;

    const auto original_effect = CurrentGame().status.effect_slots[0];
    CurrentGame().status.effect_slots[0].active = true;
    CurrentGame().status.effect_slots[0].effect_id = W8_SPELL_LIGHT;
    CurrentGame().status.effect_slots[0].amount = 1;
    CurrentGame().status.effect_slots[0].duration = 600;
    RefreshSpellIconHudRows();
    game.frame();
    game.move_pointer(0, 200);
    VideoRemoveToolTip();
    game.move_pointer(0x277, 9);
    ResetUiHelp(false);
    REQUIRE(HasScreenTransitionObjects());
    CurrentGame().status.effect_slots[0] = original_effect;
    RefreshSpellIconHudRows();

    const auto original_experience = character.experience;
    const auto original_notice = CurrentGame().party.members[slot]->level_up_announced;
    character.experience = character.experience_goal;
    CurrentGame().party.members[slot]->level_up_announced = true;
    EnablePortraitAdvanceRegions();
    UpdatePortraitAdvanceButtons();
    game.click("level-up", PortraitAdvanceButton(slot));
    game.wait_screen("level-up-character", W8_SCREEN_CHARACTER);
    REQUIRE(FindScreen<CharacterScreen>()->m_mode == W8_CHARACTER_LEVEL_UP);
    REQUIRE(FindScreen<CharacterScreen>()->m_original == &CurrentGame().party.characters[slot]);
    game.key(SDLK_ESCAPE);
    game.key(SDLK_RETURN); // Confirm abandoning the level-up edit.
    game.wait_screen("cancel-level-up", W8_SCREEN_MAIN_GAME);
    character.experience = original_experience;
    CurrentGame().party.members[slot]->level_up_announced = original_notice;
    UpdatePortraitAdvanceButtons();
}

// Load and render the retail level, recreate presentation, then reload the
// saved level through the shipped loading screen. Leave gameplay resident at
// scope exit so Application shutdown exercises the same owners as gameplay.
void ResourceLifecycleScenario()
{
    GameHarness game(false, true);
    StartPremadeGame(game);
    REQUIRE(GetWorld()->native_objects.size() == 1);
    // Exercise the retail scene and UI before the imported-object interaction.
    GetWorld()->native_objects.clear();
    // Radar redraws replace artwork without leaking overlays, and dropping it from
    // the layout removes the backdrop and every retained blip together.
    auto& screen = *MainGame();
    const auto preferred = g_settings.main_ui_mode;
    screen.setPreferredMode(W8_MAIN_UI_MODE_PORTRAITS);
    screen.closeOverlays();
    const auto overlay_counts = [] {
        return std::array{g_compositor.layerGroup(OverlayLayer::OverUi).children.size(),
                          g_compositor.layerGroup(OverlayLayer::SquareAspect).children.size()};
    };
    const auto without_radar = overlay_counts();
    screen.refreshLayout();
    REQUIRE(screen.radar_map);
    screen.radar_map->captureBackdrop();
    const auto with_radar = overlay_counts();
    REQUIRE(with_radar[0] > without_radar[0]);
    REQUIRE(with_radar[1] > without_radar[1]);
    for (int refresh = 0; refresh < 2; ++refresh) {
        screen.radar_map->refresh();
        screen.radar_map->captureBackdrop();
        REQUIRE(overlay_counts() == with_radar);
    }
    screen.closeOverlays();
    REQUIRE(overlay_counts() == without_radar);
    screen.setPreferredMode(preferred);
    REQUIRE(g_renderer);
    REQUIRE(g_renderer->isOpen());
    int original_width = 0, original_height = 0;
    REQUIRE(SDL_GetWindowSize(ghWindow, &original_width, &original_height));
    REQUIRE(SDL_SetWindowSize(ghWindow, 960, 720));
    REQUIRE(SDL_SyncWindow(ghWindow));
    SDL_Event focus{};
    focus.window.windowID = SDL_GetWindowID(ghWindow);
    const auto before_focus_loss = CurrentGame().time;
    focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    HandleGameEvent(focus);
    g_runtime_test_hooks.clock_us += 30'000'000;
    REQUIRE(IsVideoActive());
    REQUIRE(g_renderer->isOpen());
    focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    HandleGameEvent(focus);
    game.frame();
    REQUIRE(CurrentGame().time - before_focus_loss < std::chrono::milliseconds(100));
    g_renderer->close();
    REQUIRE(OpenRendererWindow());
    REQUIRE(g_renderer->isOpen());
    SuspendVideoManager();
    REQUIRE_FALSE(IsVideoActive());
    REQUIRE_FALSE(g_renderer->isOpen());
    REQUIRE(RestoreVideoManager());
    REQUIRE(IsVideoActive());
    REQUIRE(g_renderer->isOpen());
    int width = 0, height = 0;
    REQUIRE(SDL_GetWindowSize(ghWindow, &width, &height));
    REQUIRE(width == 960);
    REQUIRE(height == 720);
    REQUIRE(SDL_SetWindowSize(ghWindow, original_width, original_height));
    REQUIRE(SDL_SyncWindow(ghWindow));
    REQUIRE(VideoResizeWindow());
    // Native cursor textures keep rectangular artwork, including images larger
    // than the former 256-pixel limit, without changing the explicit hotspot.
    const auto hotspot = SDL_Point{g_cursor_hotspot_x, g_cursor_hotspot_y};
    REQUIRE(ResizeMouseCursorSurface(301, 37));
    REQUIRE(g_mouse_surface->w == 301);
    REQUIRE(g_mouse_surface->h == 37);
    REQUIRE(g_cursor_hotspot_x == hotspot.x);
    REQUIRE(g_cursor_hotspot_y == hotspot.y);
    REQUIRE(ResizeMouseCursorSurface(23, 41));
    REQUIRE(g_mouse_surface->w == 23);
    REQUIRE(g_mouse_surface->h == 41);
    // Rendering must retain the world graph when presentation is reopened.
    CheckWorldRender(game);
    CheckTwoWorldViews(game);
    CheckGameplayUi(game);
    // The shipped import option attaches a real game object at level entry. Use
    // normal pointer movement and clicks, rather than a test-only activation path.
    {
        auto& world = *GetWorld();
        REQUIRE(world.native_objects.empty());
        REQUIRE(CurrentGame().native_object_file == NativeDemoFile());
        // Re-anchor after the preceding camera/UI exercises, using the entry path.
        LoadNativeLevelObject(CurrentGame());
        REQUIRE(world.native_objects.size() == 1);
        auto& object = *world.native_objects.front();
        const int x = (g_viewport.left + g_viewport.right) / 2;
        const int y = (g_viewport.top + g_viewport.bottom) / 2;
        SetTargetingMode(W8_TARGET_NEED_NONE);
        game.move_pointer(x, y);
        game.move_pointer(x + 1, y);
        REQUIRE(world.selectedNativeObject() == &object);
        REQUIRE(render::gameOwner(world.picked.intersection) == nullptr);
        REQUIRE(ScreenPointer().shownSlot() == 13);
        REQUIRE_FALSE(object.illuminated);
        game.click_at(x, y);
        REQUIRE(object.illuminated);
        const auto held = std::exchange(CurrentGame().advancement.held, true);
        REQUIRE_FALSE(CurrentGame().activateSelectedNativeObject());
        REQUIRE(object.illuminated);
        CurrentGame().advancement.held = held;
        game.click_at(x, y);
        REQUIRE_FALSE(object.illuminated);
        world.native_objects.clear();
        REQUIRE_FALSE(world.picked.intersection.hit);
        game.frame();
    }
    // Automap buttons own their sprite sheets. Re-entering the screen must
    // rebuild all sixteen controls after the previous owners are destroyed.
    for (int visit = 0; visit != 2; ++visit) {
        game.command(W8_MGS_COMMAND_AUTOMAP);
        game.wait_screen("automap-buttons", W8_SCREEN_AUTOMAP);
        REQUIRE(g_automap_buttons);
        for (int index = 0; index != 16; ++index) {
            REQUIRE(g_automap_buttons->buttons[index]);
            REQUIRE(g_automap_buttons->buttons[index]->Bounds().w > 0);
            REQUIRE(g_automap_buttons->buttons[index]->Bounds().h > 0);
            g_automap_buttons->buttons[index]->m_dirty = true;
            g_automap_buttons->buttons[index]->Draw(GetFrameBufferSurface());
        }
        game.key(SDLK_ESCAPE);
        game.wait_screen("automap-buttons-released", W8_SCREEN_MAIN_GAME);
        REQUIRE(!g_automap_buttons);
    }
    game.command(W8_MGS_COMMAND_OPTIONS);
    game.wait_screen("options-menu", W8_SCREEN_OPTIONS);
    game.click("options-panel", *FindScreen<OptionsScreen>()->m_menu_buttons[2]);
    REQUIRE(FindScreen<OptionsScreen>()->m_selected_panel == 2);
    game.click("options-next", *FindScreen<OptionsScreen>()->m_navigation[1]);
    REQUIRE(FindScreen<OptionsScreen>()->m_panel[2]->m_current == 1);
    game.click("options-previous", *FindScreen<OptionsScreen>()->m_navigation[0]);
    REQUIRE(FindScreen<OptionsScreen>()->m_panel[2]->m_current == 0);
    game.press_at(30, 60);
    game.release_at(200, 190);
    REQUIRE(FindScreen<OptionsScreen>()->m_selected_panel == 2);
    // The artwork hotspot selects the same panel as its button.
    game.click("options-artwork", {30, 60});
    REQUIRE(FindScreen<OptionsScreen>()->m_selected_panel == 0);
    const float combat_speed = FindScreen<OptionsScreen>()->values.combat_speed;
    game.click_at(480, 50);
    const float clicked_speed = FindScreen<OptionsScreen>()->values.combat_speed;
    REQUIRE(clicked_speed < 0.5f);
    game.press_at(480, 50);
    game.move_pointer(520, 50);
    REQUIRE(FindScreen<OptionsScreen>()->values.combat_speed > clicked_speed);
    game.release_at(200, 190); // Captured release outside ends the drag.
    const float released_speed = FindScreen<OptionsScreen>()->values.combat_speed;
    game.move_pointer(490, 50);
    REQUIRE(FindScreen<OptionsScreen>()->values.combat_speed == released_speed);
    // An inactive panel must not retain slider input.
    game.click("options-panel", *FindScreen<OptionsScreen>()->m_menu_buttons[2]);
    game.click_at(480, 50);
    REQUIRE(FindScreen<OptionsScreen>()->values.combat_speed == released_speed);
    FindScreen<OptionsScreen>()->values.combat_speed = combat_speed;
    game.click("options-return", *FindScreen<OptionsScreen>()->m_menu_buttons[7]);
    game.wait_screen("options-menu-return", W8_SCREEN_MAIN_GAME);
    // Exercise a second screen's input, teardown and return-stack lifecycle.
    const std::size_t return_depth = SuspendedScreenCount();
    // Retail saves can retain entries whose fact no longer exists. Opening,
    // painting and toggling the journal must ignore them without deleting them.
    const auto journal_size = CurrentGame().story.journal.size();
    const auto invalid_fact = static_cast<W8FactId>(-1);
    CurrentGame().story.journal.push_back({GetLoadedLevelID(), invalid_fact, false});
    game.command(W8_MGS_COMMAND_JOURNAL);
    game.wait_screen("journal", W8_SCREEN_JOURNAL);
    REQUIRE(SuspendedScreenCount() == return_depth + 1);
    REQUIRE(PendingScreen() == W8_SCREEN_MAIN_GAME);
    // Exercise the ordinary toggle and close buttons.
    game.click("journal-toggle", kJournalToggle);
    game.click("journal-toggle", kJournalToggle);
    game.click("journal-close", kJournalClose);
    game.wait_screen("journal-return", W8_SCREEN_MAIN_GAME);
    REQUIRE(SuspendedScreenCount() == return_depth);
    REQUIRE(CurrentGame().story.journal.size() == journal_size + 1);
    REQUIRE(CurrentGame().story.journal.back().fact == invalid_fact);
    CurrentGame().story.journal.pop_back();
    // Returning re-arms the interface buttons; the roof buttons switch views.
    const auto click_roof = [&](int index) {
        game.click("roof-button", MainGame()->hud_buttons.roof[index]);
    };
    click_roof(2);
    REQUIRE(g_settings.main_ui_mode == W8_MAIN_UI_MODE_RADAR);
    click_roof(0);
    REQUIRE(g_settings.main_ui_mode == W8_MAIN_UI_MODE_PORTRAITS);
    REQUIRE((GetWorld() && GetWorld()->scene));

    const int level = GetLoadedLevelID();
    const auto anchor = Walk(game, "settle-before-save", 2.0f);
    QuickSave(game);
    { // One save/load cycle covers reconstruction and live teardown.
        auto* world = GetWorld();
        REQUIRE((world && world->compiledIndex() && world->scene));
        unsigned linked = 0, animated = 0;
        for (unsigned i = 0; i <= world->compiledIndex()->GetMeshCount(); ++i) {
            auto* instance = world->psrMeshes[i];
            if (!instance) continue;
            auto* model = static_cast<Mesh*>(instance->getModel());
            if (model && !model->parts.empty()) ++linked;
            if (model && MeshHasAnimatedTexture(model)) ++animated;
        }
        std::printf("resource-lifecycle: %u linked heads, %u animated textures\n", linked,
                    animated);
        REQUIRE(linked > 0);
        REQUIRE(animated > 0);
        for (int frame = 0; frame != 4; ++frame) game.frame();
        RenderFrame();
        REQUIRE(world->scene->submitted_polygons > 0);
        game.command(W8_MGS_COMMAND_QUICK_LOAD);
        bool loading = false;
        game.wait("reload-level", [&] {
            loading = loading || CurrentScreenState().id == W8_SCREEN_PLEASE_WAIT;
            return loading && CurrentScreenState().id == W8_SCREEN_MAIN_GAME && game.settled();
        });
        Settle(game, "reload-level");
        REQUIRE(GetLoadedLevelID() == level);
        REQUIRE(GetWorld()->native_objects.size() == 1);
        ExpectAt("reloaded-position", anchor);
    }
    RenderFrame();
    REQUIRE(GetWorld()->scene->submitted_polygons > 0);
}

} // namespace

TEST_CASE("resource-lifecycle", "[runtime]")
{
    RunScenario([&] { ResourceLifecycleScenario(); });
}

TEST_CASE("inventory-chest", "[runtime]")
{
    RunScenario([&] { InventoryChestScenario(); });
}

TEST_CASE("camp-spells", "[runtime]")
{
    RunScenario(CampSpellsScenario);
}
