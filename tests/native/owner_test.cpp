#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include <fmt/printf.h>
#include "wiz8/fonts.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/layouts/character.h"
#include "wiz8/xstatus.h"
#include "wiz8/video_object_catalog.h"
#include "wiz8/utility.h"
#include "Button System.h"
#include "wiz8/dialog_code/DialogButton.h"
#include "wiz8/dialog_code/ButtonUserData.h"
#include "wiz8/local_code/ButtonSound.h"
#include "Font.h"
#include "input.h"
#include "mousesystem_macros.h"
#include "imgfmt.h"
#include "wiz8/filesystem.h"
#include "temporary_directory.h"
#include "wiz8/asset_paths.h"
#include "wiz8/text_input.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>


extern GUI_BUTTON* gpAnchoredButton;

namespace {
std::vector<int> screen_leaves;
int screen_entries, screen_frames;
unsigned char enter() { ++screen_entries; return 1; }
unsigned char leave(int discard) { screen_leaves.push_back(discard); return 1; }
void frame() { ++screen_frames; }

void screen_stack()
{
    W8CharacterEventQueue queue;
    gXStatus.character_event_queue = &queue;
    const auto saved_intro = g_screen_handlers[W8_SCREEN_INTRO];
    const auto saved_options = g_screen_handlers[W8_SCREEN_OPTIONS];
    for (auto id : {W8_SCREEN_INTRO, W8_SCREEN_OPTIONS})
        g_screen_handlers[id] = {nullptr, enter, frame, leave, nullptr};
    g_current_screen_state = {};
    g_current_screen_state.id = W8_SCREEN_NONE;
    g_pending_screen_state.id = W8_SCREEN_NONE;
    g_screen_return_requested = false;
    REQUIRE(GetPendingScreenState() == W8_SCREEN_NONE);
    int payload;
    for (int index = 0; index < 40; ++index) {
        g_pending_screen_state = {};
        g_pending_screen_state.id = index % 2 ? W8_SCREEN_OPTIONS : W8_SCREEN_INTRO;
        g_pending_screen_state.parameter = index;
        g_pending_screen_state.parameter_3 = &payload;
        g_pending_screen_state.name = "screen " + std::to_string(index);
        REQUIRE(GetPendingScreenState() == g_pending_screen_state.id);
        GameLoop();
        REQUIRE(g_current_screen_state.parameter == index);
        REQUIRE(g_screen_return_stack.size() == static_cast<size_t>(index));
    }
    REQUIRE((screen_entries == 40 && screen_frames == 40));
    REQUIRE((screen_leaves.size() == 39 && screen_leaves.back() == 0));
    REQUIRE(GetPendingScreenState() == W8_SCREEN_INTRO);
    for (int index = 38; index >= 0; --index) {
        RequestScreenTransition();
        GameLoop();
        REQUIRE(g_current_screen_state.parameter == index);
        REQUIRE(g_current_screen_state.parameter_3 == &payload);
        REQUIRE(g_current_screen_state.name == std::string("screen ") + std::to_string(index));
        REQUIRE(g_screen_return_stack.size() == static_cast<size_t>(index));
        REQUIRE(screen_leaves.back() == 1);
    }
    g_screen_return_stack.push_back(g_current_screen_state);
    g_pending_screen_state = g_current_screen_state;
    g_pending_screen_state.id = W8_SCREEN_OPTIONS;
    g_pending_screen_state.parameter = 99;
    RequestScreenTransition();
    GameLoop();
    REQUIRE((g_current_screen_state.parameter == 99 && g_screen_return_stack.size() == 1));
    RequestScreenTransition();
    GameLoop();
    REQUIRE((g_current_screen_state.parameter == 0 && g_screen_return_stack.empty()));
    gfProgramIsRunning = true;
    RequestScreenTransition();
    GameLoop();
    REQUIRE((!gfProgramIsRunning && g_current_screen_state.id == W8_SCREEN_NONE));
    g_screen_return_requested = false;
    gXStatus.character_event_queue = nullptr;
    g_screen_handlers[W8_SCREEN_INTRO] = saved_intro;
    g_screen_handlers[W8_SCREEN_OPTIONS] = saved_options;
}

void surface_regions()
{
    VSURFACE_DESC desc{};
    desc.fCreateFlags = VSURFACE_CREATE_DEFAULT;
    desc.usWidth = desc.usHeight = 16;
    desc.ubBitDepth = 16;
    auto* surface = CreateVideoSurface(&desc);
    REQUIRE((surface && surface->RegionList.empty()));
    VSURFACE_REGION result{};
    result.ubHitMask = 99;
    REQUIRE((!GetVSurfaceRegion(surface, 0, &result) && result.ubHitMask == 99));
    for (int index = 0; index < 80; ++index) {
        VSURFACE_REGION region{};
        region.Origin.x = index;
        region.RegionCoords.w = (index + 1) - region.RegionCoords.x;
        region.ubHitMask = static_cast<UINT8>(index);
        surface->RegionList.push_back(region);
    }
    REQUIRE(GetVSurfaceRegion(surface, 79, &result));
    REQUIRE((result.Origin.x == 79 && (result.RegionCoords.x + result.RegionCoords.w) == 80 && result.ubHitMask == 79));
    REQUIRE(!GetVSurfaceRegion(surface, 80, &result));
    REQUIRE(!GetVSurfaceRegion(surface, 1, nullptr));
    auto* borrowed = CreateVideoSurfaceFromCpuSurface(surface->surface);
    REQUIRE((borrowed && borrowed->RegionList.empty()));
    borrowed->RegionList.push_back(result);
    surface->RegionList.clear();
    REQUIRE(GetVSurfaceRegion(borrowed, 0, &result));
    REQUIRE(DeleteVideoSurface(borrowed));
    REQUIRE((surface->surface && DeleteVideoSurface(surface)));
}

void camp_effects()
{
    REQUIRE(InitializeVideoObjectManager());
    std::array<SGPPaletteEntry, 256> palette{};
    std::array<ETRLEObject, 12> images{};
    for (auto& image : images) { image.usWidth = 16; image.usHeight = 8; }
    image_type image{};
    image.ubBitDepth = 8;
    image.fFlags = IMAGE_TRLECOMPRESSED;
    image.pPalette = std::make_unique<SGPPaletteEntry[]>(256);
    std::copy_n(palette.data(), palette.size(), image.pPalette.get());
    // Each frame is eight scanlines of one transparent run of 16 then the
    // row terminator: 0x90 0x00 repeated.
    image.pImageData.resize(images.size() * 16);

    image.pETRLEObject.resize(images.size());
    std::copy_n(images.data(), images.size(), image.pETRLEObject.data());
    for (std::size_t i = 0; i < images.size(); ++i) {
        image.pETRLEObject[i].uiDataOffset = i * 16;
        image.pETRLEObject[i].uiDataLength = 16;
        for (unsigned row = 0; row < 8; ++row) {
            image.pImageData[i * 16 + row * 2] = 0x90;
        }
    }

    VOBJECT_DESC desc{};
    desc.fCreateFlags = VOBJECT_CREATE_FROMHIMAGE;
    desc.hImage = &image;
    UINT32 handle;
    REQUIRE(AddVideoObject(&desc, &handle));
    const auto saved_slot = g_video_slots[0x86];
    const auto saved_frame = g_video_frames[0];
    g_video_slots[0x86] = {0, 0};
    g_video_frames[0].loaded = true;
    g_video_frames[0].storage_kind = W8_VIDEO_STORAGE_OBJECT;
    g_video_frames[0].handle = handle;
    {
        auto state = std::make_unique<W8CampScreenState>();
        REQUIRE((state->effect_list.empty() && state->stats_range == nullptr));
        g_camp_screen = state.get();
        W8CampStatsRange range;
        state->stats_range = &range;
        state->effect_list = {
            {false, false, true, false, 0, 0, 0, 3, 10}, {false, false, false, true, 0, 1, 0, 3, 9},
            {true, false, true, false, 2, 2, 0, 9999, 3}, {false, false, true, true, 0, 3, 0, 3, 2}};
        state->effect_filter = W8_CAMP_EFFECT_FILTER_ALL;
        state->effect_scroll = 100;
        FilterCampEffectList();
        REQUIRE((state->effect_visible_lines == 24 && state->effect_scroll == 7));
        REQUIRE((state->effect_first_visible == 0 && state->effect_last_visible == 3));
        REQUIRE((range.m_range->m_enabled && range.m_range->m_value == 7));
        state->effect_filter = W8_CAMP_EFFECT_FILTER_BENEFICIAL;
        FilterCampEffectList();
        REQUIRE((state->effect_visible_lines == 14 && state->effect_scroll == 0));
        REQUIRE((!range.m_range->m_enabled && !state->effect_list[1].visible));
        state->effect_filter = W8_CAMP_EFFECT_FILTER_DETRIMENTAL;
        FilterCampEffectList();
        REQUIRE((state->effect_visible_lines == 13 && state->effect_list[3].visible));
        state->effect_items_only = true;
        state->effect_filter = W8_CAMP_EFFECT_FILTER_ALL;
        FilterCampEffectList();
        REQUIRE((state->effect_visible_lines == 4 && state->effect_first_visible == 2));
        W8Character character{};
        for (auto& condition : character.uiCondition) condition = 1;
        for (auto& enchantment : character.enchantments) enchantment.turns = 2;
        g_review_character = &character;
        state->effect_items_only = false;
        RebuildCampEffectList();
        REQUIRE(state->effect_list.size() == 28);
        REQUIRE((state->effect_detrimental_count == 20 && state->effect_beneficial_count == 8));
        REQUIRE((state->effect_list.front().index == 19 && state->effect_list.back().index == 0));
        REQUIRE((state->effect_visible_lines == 64 && range.m_range->m_enabled));
        character = {};
        RebuildCampEffectList();
        REQUIRE((state->effect_list.empty() && state->effect_visible_lines == 0));
        REQUIRE((state->effect_beneficial_count == 0 && !range.m_range->m_enabled));
        g_review_character = nullptr;
    }
    g_camp_screen = nullptr;
    g_video_slots[0x86] = saved_slot;
    g_video_frames[0] = saved_frame;
    REQUIRE(ShutdownVideoObjectManager());
}

int clicks, click_reason;
MOUSE_REGION* clicked_region;
void region_click(MOUSE_REGION* region, INT32) { clicked_region = region; }
void click(GUI_BUTTON*, INT32 reason) { ++clicks; click_reason = reason; }

GUIButton nested_outer, nested_inner, removable_button;
void nested_click(GUI_BUTTON* button, INT32 reason)
{
    if (button == nested_inner.get()) {
        // The outer handler must remain alive while a nested callback destroys
        // the last external owner.
        nested_outer.reset();
        return;
    }
    REQUIRE(button == nested_outer.get());
    QuickButtonCallbackMButn(&nested_inner->Area, reason);
    REQUIRE(!nested_outer);
    REQUIRE(button->ClickCallback == nested_click);
    nested_inner.reset();
}
void remove_on_move(MOUSE_REGION* region, INT32 reason)
{
    if (reason & MSYS_CALLBACK_REASON_GAIN_MOUSE)
        MSYS_RemoveRegion(region);
}
void remove_button_on_callback(GUI_BUTTON* button, INT32)
{
    removable_button.reset();
    // QuickButtonCallback pins the object through the callback.
    REQUIRE(button->ClickCallback == remove_button_on_callback);
}

void region_registration_and_callback_lifetimes()
{
    REQUIRE(MSYS_Init());
    MOUSE_REGION first{}, latest{}, temporary{};
    MSYS_DefineRegion(&first, 0, 0, 40, 40, MSYS_PRIORITY_NORMAL, nullptr, region_click);
    {
        MOUSE_REGION scoped{};
        MSYS_DefineRegion(&scoped, 0, 0, 40, 40, MSYS_PRIORITY_HIGH, nullptr, region_click);
        MSYS_SGP_Mouse_Handler_Hook(LEFT_BUTTON_DOWN, 10, 10, true, false);
        REQUIRE(clicked_region == &scoped);
    } // Registration and click capture expire with the control.
    MSYS_SGP_Mouse_Handler_Hook(LEFT_BUTTON_UP, 10, 10, false, false);
    REQUIRE(clicked_region == &first);
    MSYS_DefineRegion(&latest, 0, 0, 40, 40, MSYS_PRIORITY_NORMAL, nullptr, region_click);
    MSYS_SGP_Mouse_Handler_Hook(LEFT_BUTTON_DOWN, 10, 10, true, false);
    REQUIRE(clicked_region == &latest);
    MSYS_SGP_Mouse_Handler_Hook(LEFT_BUTTON_UP, 10, 10, false, false);
    MSYS_RemoveRegion(&latest);
    MSYS_DefineRegion(&temporary, 0, 0, 40, 40, MSYS_PRIORITY_HIGH, remove_on_move, region_click);
    MSYS_SGP_Mouse_Handler_Hook(MOUSE_POS, 11, 11, false, false);
    REQUIRE(!(temporary.uiFlags & MSYS_REGION_EXISTS));
    MSYS_SGP_Mouse_Handler_Hook(LEFT_BUTTON_DOWN, 11, 11, true, false);
    REQUIRE(clicked_region == &first);
    MSYS_SGP_Mouse_Handler_Hook(LEFT_BUTTON_UP, 11, 11, false, false);
    MSYS_RemoveRegion(&first);

    nested_outer = CreateTextButton("nested", 0, 0, 0, {}, 0, 0, 40, 20,
                                    BUTTON_TOGGLE, MSYS_PRIORITY_NORMAL, nullptr, nested_click);
    nested_inner = CreateTextButton("nested", 0, 0, 0, {}, 0, 20, 40, 20,
                                    BUTTON_TOGGLE, MSYS_PRIORITY_NORMAL, nullptr, nested_click);
    REQUIRE((nested_outer && nested_inner));
    QuickButtonCallbackMButn(&nested_outer->Area, MSYS_CALLBACK_REASON_LBUTTON_DWN);
    REQUIRE((!nested_outer && !nested_inner));

    for (bool disabled : {false, true}) {
        removable_button = CreateTextButton("remove", 0, 0, 0, {}, 0, 0, 40, 20,
                                            BUTTON_TOGGLE, MSYS_PRIORITY_NORMAL,
                                            remove_button_on_callback, remove_button_on_callback);
        REQUIRE(removable_button);
        auto weak = std::weak_ptr<GUI_BUTTON>(removable_button);
        if (disabled) {
            removable_button->uiFlags &= ~BUTTON_ENABLED;
            removable_button->uiFlags |= BUTTON_ALLOW_DISABLED_CALLBACK;
            QuickButtonCallbackMButn(&removable_button->Area, MSYS_CALLBACK_REASON_LBUTTON_DWN);
        } else {
            QuickButtonCallbackMMove(&removable_button->Area, MSYS_CALLBACK_REASON_GAIN_MOUSE);
        }
        REQUIRE(weak.expired());
    }
    REQUIRE(RegisteredButtonCount() == 0);
    MSYS_Shutdown();
    REQUIRE(MSYS_Init());
    MSYS_Shutdown();
}

void buttons_and_regions()
{
    REQUIRE(MSYS_Init());
    MOUSE_REGION low{}, high{};
    MSYS_DefineRegion(&low, 0, 0, 40, 40, MSYS_PRIORITY_LOW, nullptr, region_click);
    MSYS_DefineRegion(&high, 0, 0, 20, 20, MSYS_PRIORITY_HIGH, nullptr, region_click);
    MSYS_SGP_Mouse_Handler_Hook(LEFT_BUTTON_DOWN, 10, 10, true, false);
    REQUIRE(clicked_region == &high);
    MSYS_SGP_Mouse_Handler_Hook(LEFT_BUTTON_UP, 10, 10, false, false);
    MSYS_SGP_Mouse_Handler_Hook(LEFT_BUTTON_DOWN, 30, 30, true, false);
    REQUIRE(clicked_region == &low);
    MSYS_SGP_Mouse_Handler_Hook(LEFT_BUTTON_UP, 30, 30, false, false);
    MSYS_SetRegionUserData(&low, 0, 17);
    REQUIRE(MSYS_GetRegionUserData(&low, 0) == 17);
    REQUIRE(MSYS_GrabMouse(&high) == MSYS_GRABBED_OK);
    MSYS_ReleaseMouse(&high);

    auto owned_button = CreateTextButton("direct", 0, 0, 0, {}, 0, 0, 40, 20,
                                         BUTTON_TOGGLE, MSYS_PRIORITY_NORMAL, nullptr, click);
    REQUIRE(owned_button);
    auto& button = *owned_button;
    button.ClickCallback = click;
    button.uiFlags = BUTTON_ENABLED | BUTTON_CLICK_CALLBACK;
    QuickButtonCallbackMButn(&button.Area, MSYS_CALLBACK_REASON_LBUTTON_DWN);
    REQUIRE((clicks == 1 && click_reason == MSYS_CALLBACK_REASON_LBUTTON_DWN));
    button.uiFlags &= ~BUTTON_ENABLED;
    QuickButtonCallbackMButn(&button.Area, MSYS_CALLBACK_REASON_LBUTTON_DWN);
    REQUIRE(clicks == 1);
    button.uiFlags |= BUTTON_ALLOW_DISABLED_CALLBACK;
    QuickButtonCallbackMButn(&button.Area, MSYS_CALLBACK_REASON_LBUTTON_DWN);
    REQUIRE((clicks == 2 && (click_reason & BUTTON_DISABLED_CALLBACK)));
    button.uiFlags = BUTTON_ENABLED;
    QuickButtonCallbackMButn(&button.Area, MSYS_CALLBACK_REASON_LBUTTON_DWN);
    REQUIRE(button.uiFlags & BUTTON_CLICKED_ON);
    button.uiFlags = BUTTON_ENABLED | BUTTON_CHECKBOX | BUTTON_CLICK_CALLBACK;
    QuickButtonCallbackMButn(&button.Area, MSYS_CALLBACK_REASON_LBUTTON_DWN);
    REQUIRE(!(button.uiFlags & BUTTON_CLICKED_ON));
    QuickButtonCallbackMButn(&button.Area, MSYS_CALLBACK_REASON_LBUTTON_UP);
    REQUIRE(button.uiFlags & BUTTON_CLICKED_ON);
    REQUIRE(gpAnchoredButton == &button);
    gpAnchoredButton = nullptr;
    owned_button.reset();

    for (unsigned repeat = 0; repeat < 32; ++repeat) {
        char label[] = "owned button";
        auto object = CreateTextButton(label, 0, 0, 0, {}, 0, 0, 40, 20,
                                       BUTTON_TOGGLE, MSYS_PRIORITY_NORMAL, nullptr, click);
        REQUIRE(object);
        auto* owned = object.get();
        REQUIRE(owned->string[0] == 'o');
        label[0] = 'x';
        REQUIRE(owned->string[0] == 'o');
        SpecifyButtonText(object, owned->string);
        REQUIRE((object.get() == owned && owned->string[0] == 'o'));
        char tooltip[] = "tooltip";
        SetButtonFastHelpText(object, tooltip);
        tooltip[0] = 'x';
        REQUIRE(owned->Area.FastHelpText[0] == 't');
        SetButtonFastHelpText(object, owned->Area.FastHelpText);
        REQUIRE(owned->Area.FastHelpText[0] == 't');
        SetButtonFastHelpText(object, {});
        REQUIRE(owned->Area.FastHelpText.empty());
        SetButtonFastHelpText(object, label);
        SpecifyButtonText(object, {});
        REQUIRE(owned->string.empty());
        auto weak = std::weak_ptr<GUI_BUTTON>(object);
        object.reset();
        REQUIRE(weak.expired());
    }
    REQUIRE(RegisteredButtonCount() == 0);
    MSYS_RemoveRegion(&high);
    MSYS_RemoveRegion(&low);
    MSYS_Shutdown();
}

void image_and_sprite_ownership()
{
    REQUIRE(InitializeVideoObjectManager());
    image_type image{};
    image.ubBitDepth = 8;
    image.fFlags = IMAGE_TRLECOMPRESSED | IMAGE_BITMAPDATA | IMAGE_PALETTE;
    image.pPalette = std::make_unique<SGPPaletteEntry[]>(256);
    image.pPalette[1] = {255, 0, 0, 0};
    image.pImageData.resize(3);
    image.pImageData[0] = 1;
    image.pImageData[1] = 1;
    image.pETRLEObject.resize(1);
    image.pETRLEObject[0] = {0, 3, 0, 0, 1, 1};

    VOBJECT_DESC desc{};
    desc.fCreateFlags = VOBJECT_CREATE_FROMHIMAGE;
    desc.hImage = &image;
    std::array<UINT32, 64> ids{};
    std::array<HVOBJECT, 64> views{};
    for (unsigned i = 0; i < ids.size(); ++i) {
        REQUIRE(AddVideoObject(&desc, &ids[i]));
        REQUIRE(GetVideoObject(&views[i], ids[i]));
        REQUIRE((views[i]->sprites.size() == 1 && views[i]->sprites[0].pixels.size() == 1));
        REQUIRE(reinterpret_cast<const UINT8*>(views[i]->sprites[0].pixels.data()) !=
              image.pImageData.data());
        REQUIRE(views[i]->pPaletteEntry.get() != image.pPalette.get());
        (void)CreateObjectPaletteTables(views[i], HVOBJECT_GLOW_GREEN);
        REQUIRE((views[i]->pShades[0] && views[i]->pShades[15]));
        REQUIRE(views[i]->pShades[4].get() == views[i]->ownedPalette.get());
        REQUIRE(SetObjectShade(views[i], 6));
        auto retained = views[i]->pShades[4];
        REQUIRE(SetVideoObjectPalette(views[i], image.pPalette.get()));
        REQUIRE((retained && views[i]->ownedPalette.get() != retained.get()));
        REQUIRE(views[i]->pShades[4] == views[i]->ownedPalette);
        REQUIRE(SetObjectShade(views[i], 4));
        (void)CreateObjectPaletteTables(views[i], HVOBJECT_GLOW_RED);
        REQUIRE(views[i]->pShadeCurrent == views[i]->pShades[4].get());
    }
    image.pImageData[1] = 2;
    image.pETRLEObject[0].usWidth = 2;
    REQUIRE((views[0]->sprites[0].pixels[0] == 1 && views[0]->sprites[0].usWidth == 1));
    image.pETRLEObject[0].uiDataOffset = 0xffffffffu;
    REQUIRE(!CreateVideoObject(&desc));
    REQUIRE((image.pImageData[1] == 2 && image.pETRLEObject[0].uiDataOffset == 0xffffffffu));
    image = {};
    REQUIRE((image.pImageData.empty() && !image.pPalette && image.pETRLEObject.empty()));
    for (auto id : {ids[0], ids[31], ids[63]}) {
        REQUIRE(DeleteVideoObjectFromIndex(id));
        HVOBJECT missing = nullptr;
        REQUIRE(!GetVideoObject(&missing, id));
    }
    HVOBJECT surviving = nullptr;
    REQUIRE((GetVideoObject(&surviving, ids[32]) && surviving == views[32]));
    REQUIRE((surviving->sprites[0].pixels[0] == 1 && surviving->sprites[0].usWidth == 1));
    UINT16 borrowed[256]{};
    surviving->p16BPPPalette = borrowed;
    REQUIRE(DestroyObjectPaletteTables(surviving));
    REQUIRE((!surviving->ownedPalette && !surviving->p16BPPPalette));
    for (const auto& shade : surviving->pShades) REQUIRE(!shade);
    REQUIRE(ShutdownVideoObjectManager());
    REQUIRE((InitializeVideoObjectManager() && ShutdownVideoObjectManager()));

    VSURFACE_DESC surface_desc{};
    surface_desc.fCreateFlags = VSURFACE_CREATE_DEFAULT;
    surface_desc.usWidth = 8;
    surface_desc.usHeight = 8;
    surface_desc.ubBitDepth = 16;
    std::array<HVSURFACE, 64> surfaces{};
    for (unsigned i = 0; i < ids.size(); ++i) {
        REQUIRE(AddVideoSurface(&surface_desc, &ids[i]));
        REQUIRE(GetVideoSurface(&surfaces[i], ids[i]));
    }
    for (auto id : {ids[0], ids[31], ids[63]}) {
        REQUIRE(DeleteVideoSurfaceFromIndex(id));
        HVSURFACE missing = nullptr;
        REQUIRE(!GetVideoSurface(&missing, id));
    }
    HVSURFACE surface = nullptr;
    REQUIRE((GetVideoSurface(&surface, ids[32]) && surface == surfaces[32]));
    REQUIRE((surface->surface && surface->usWidth == 8));
    REQUIRE(ShutdownVideoSurfaceManager());
}

void font_table_ownership()
{
    for (unsigned repeat = 0; repeat < 32; ++repeat) {
        auto translation = CreateEnglishTransTable();
        REQUIRE(translation.size() == 252);
        REQUIRE(InitializeFontManager(translation));
        translation[1] = '?';
        REQUIRE(GetIndex('B') == 1);
        translation = {};
        REQUIRE(GetIndex('B') == 1);
        REQUIRE(InitializeFontManager(CreateEnglishTransTable()));
        REQUIRE(GetIndex('Z') == 25);
        ShutdownFontManager();
        ShutdownFontManager();
    }
}

void text_input_ownership()
{
    MSYS_Init();
    for (unsigned repeat = 0; repeat < 32; ++repeat) {
        InitTextInputMode();
        const auto first = AddTextInputField(0, 0, 80, 20, MSYS_PRIORITY_NORMAL,
                                             "alpha", 32, 0, 0);
        const auto middle = AddTextInputField(0, 20, 80, 20, MSYS_PRIORITY_NORMAL,
                                              "beta", 32, 0, 0);
        const auto last = AddTextInputField(0, 40, 80, 20, MSYS_PRIORITY_NORMAL,
                                            "gamma", 32, 0, 0);
        REQUIRE((first == 0 && middle == 1 && last == 2));
        std::string text;
        text = GetTextFromField(first);
        REQUIRE((text[0] == 'a' && GetTextInputFieldByteLength(first) == 5));
        RemoveTextInputField(middle);
        REQUIRE(GetTextInputFieldByteLength(last) == 5);
        InitTextInputMode();
        REQUIRE(AddTextInputField(0, 0, 80, 20, MSYS_PRIORITY_NORMAL,
                                "inner", 32, 0, 0) == 0);
        KillTextInputMode();
        text = GetTextFromField(first);
        REQUIRE((text[0] == 'a' && GetTextInputFieldByteLength(last) == 5));
        REQUIRE(AddTextInputField(0, 60, 80, 20, MSYS_PRIORITY_NORMAL,
                                "delta", 32, 0, 0) == 3);
        RemoveTextInputField(first);
        RemoveTextInputField(3);
        RemoveTextInputField(last);
        KillTextInputMode();
        REQUIRE(GetActiveTextInputField() == -1);
        InitTextInputMode();
        KillTextInputMode();
    }
    MSYS_Shutdown();
}

void formatted_strings()
{
    const auto first = fmt::format("{} {:d}", "first", 7);
    const auto second = fmt::format("{} {:d}", "second", 9);
    REQUIRE((first == "first 7" && second == "second 9"));
    const std::string long_text(1024, 'x');
    REQUIRE(fmt::format("{}", long_text) == long_text);
    REQUIRE(fmt::format("{:d}", 42) == "42");
    REQUIRE(fmt::sprintf("%ls %hs", "é", "中") == "é 中");
    REQUIRE(std::string("100% literal") == "100% literal");
}

void button_image_fixture(const std::filesystem::path& root, std::string name, unsigned count = 9,
                           bool app_data = false)
{
    std::replace(name.begin(), name.end(), '\\', '/');
    const auto path = root / name;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    const auto write = [&file](const void* data, size_t size) {
        file.write(static_cast<const char*>(data), size);
        REQUIRE(file.good());
    };
    STCIHeader header{};
    memcpy(header.cID, STCI_ID_STRING, STCI_ID_LEN);
    header.uiOriginalSize = count;
    header.uiStoredSize = count * 3;
    header.fFlags = STCI_INDEXED | STCI_ETRLE_COMPRESSED;
    header.usWidth = header.usHeight = 1;
    header.Indexed.uiNumberOfColours = 256;
    header.Indexed.usNumberOfSubImages = count;
    header.ubDepth = 8;
    header.uiAppDataSize = app_data ? 4 : 0;
    write(&header, STCI_HEADER_SIZE);
    std::array<STCIPaletteElement, 256> palette{};
    palette[1].ubRed = 255;
    write(palette.data(), STCI_8BIT_PALETTE_SIZE);
    std::vector<STCISubImage> regions(count);
    for (unsigned index = 0; index < count; ++index) {
        regions[index].uiDataOffset = index * 3;
        regions[index].uiDataLength = 3;
        regions[index].usWidth = regions[index].usHeight = 1;
    }
    write(regions.data(), regions.size() * sizeof(STCISubImage));
    for (unsigned index = 0; index < count; ++index) {
        const UINT8 pixels[]{1, 1, 0};
        write(pixels, sizeof(pixels));
    }
    if (app_data) {
        const UINT8 bytes[]{7, 8, 9, 10};
        write(bytes, sizeof(bytes));
    }
}

void generic_button_images()
{
    const std::filesystem::path root = make_temporary_directory("wiz8-button-images");
    const auto assets = root / "assets";
    button_image_fixture(assets, DEFAULT_GENERIC_BUTTON_OFF);
    button_image_fixture(assets, "button.sti");
    button_image_fixture(assets, "short.sti", 8);
    w8_native::configure_paths({assets.string(), (root / "user").string(), {"", "", ""}});

    button_image_fixture(assets, "metadata.sti", 3, true);
    {
        char filename[] = "metadata.sti";
        std::unique_ptr<image_type> image(CreateImage(filename, IMAGE_ALLDATA));
        REQUIRE((image && image->pImageData.size() == 9 && image->pETRLEObject.size() == 3));
        auto* pixels = image->pImageData.data();
        auto* palette = image->pPalette.get();
        REQUIRE(LoadImageData(image.get(), IMAGE_APPDATA));
        REQUIRE((image->pImageData.data() == pixels && image->pPalette.get() == palette));
        REQUIRE((image->pAppData.size() == 4 && image->pAppData[0] == 7 && image->pAppData[3] == 10));
        auto* app = image->pAppData.data();
        REQUIRE(LoadImageData(image.get(), IMAGE_PALETTE));
        REQUIRE((image->pImageData.data() == pixels && image->pAppData.data() == app));
        REQUIRE((image->pPalette.get() != palette && image->pPalette[1].peRed == 255));
        palette = image->pPalette.get();
        REQUIRE(LoadImageData(image.get(), IMAGE_BITMAPDATA));
        REQUIRE((image->pPalette.get() == palette && image->pAppData.data() == app));
        REQUIRE((image->pImageData[1] == 1 && image->pETRLEObject.size() == 3));
        pixels = image->pImageData.data();
        auto* objects = image->pETRLEObject.data();
        const auto flags = image->fFlags;
        std::filesystem::resize_file(assets / "metadata.sti",
            STCI_HEADER_SIZE + STCI_8BIT_PALETTE_SIZE + 3 * STCI_SUBIMAGE_SIZE + 7);
        REQUIRE(!LoadImageData(image.get(), IMAGE_ALLDATA));
        REQUIRE((image->pImageData.data() == pixels && image->pETRLEObject.data() == objects));
        REQUIRE((image->pPalette.get() == palette && image->pAppData.data() == app));
        REQUIRE((image->fFlags == flags && image->pAppData.size() == 4 && image->pImageData.size() == 9));

        const auto pixel_offset = STCI_HEADER_SIZE + STCI_8BIT_PALETTE_SIZE + 3 * STCI_SUBIMAGE_SIZE;
        for (std::uintmax_t cut : {0u, STCI_HEADER_SIZE - 1u,
             STCI_HEADER_SIZE + STCI_8BIT_PALETTE_SIZE - 1u,
             pixel_offset - 1u, pixel_offset + 8u, pixel_offset + 12u}) {
            button_image_fixture(assets, "metadata.sti", 3, true);
            std::filesystem::resize_file(assets / "metadata.sti", cut);
            REQUIRE(!LoadImageData(image.get(), IMAGE_ALLDATA));
            REQUIRE((image->pImageData.data() == pixels && image->pETRLEObject.data() == objects));
            REQUIRE((image->pPalette.get() == palette && image->pAppData.data() == app));
            REQUIRE((image->fFlags == flags && image->pAppData.size() == 4 && image->pImageData[1] == 1));
        }
        button_image_fixture(assets, "metadata.sti", 3, true);
        std::filesystem::resize_file(assets / "metadata.sti", pixel_offset);
        REQUIRE(LoadImageData(image.get(), IMAGE_PALETTE));
        REQUIRE((image->pImageData.data() == pixels && image->pAppData.data() == app));
        REQUIRE(!LoadImageData(image.get(), IMAGE_BITMAPDATA));
    }
    std::string long_image_path;
    for (int i = 0; i < 8; ++i)
        long_image_path += "long-component-for-a-native-image-path/";
    long_image_path += "button.sti";
    REQUIRE(long_image_path.size() > 260);
    button_image_fixture(assets, long_image_path, 3);
    {
        VOBJECT_DESC desc{};
        desc.fCreateFlags = VOBJECT_CREATE_FROMFILE;
        desc.ImageFile = long_image_path;
        std::unique_ptr<SGPVObject> sprite(CreateVideoObject(&desc));
        REQUIRE((sprite && sprite->sprites.size() == 3));
        REQUIRE((sprite->sprites[2].usWidth == 1 && sprite->sprites[2].pixels[0] == 1));
        REQUIRE(sprite->pPaletteEntry[1].peRed == 255);
    }
    for (bool corrupt_length : {false, true}) {
        button_image_fixture(assets, "bad-extent.sti", 3);
        {
            std::fstream file(assets / "bad-extent.sti", std::ios::in | std::ios::out | std::ios::binary);
            STCISubImage region{};
            region.uiDataOffset = corrupt_length ? 1 : 0xffffffffu;
            region.uiDataLength = corrupt_length ? 9 : 3;
            region.usWidth = region.usHeight = 1;
            file.seekp(STCI_HEADER_SIZE + STCI_8BIT_PALETTE_SIZE);
            file.write(reinterpret_cast<const char*>(&region), STCI_SUBIMAGE_SIZE);
            REQUIRE(file.good());
        }
        REQUIRE(!CreateImage("bad-extent.sti", IMAGE_ALLIMAGEDATA));
        REQUIRE(CreateImage("bad-extent.sti", IMAGE_PALETTE));
    }
    for (bool corrupt_app_data : {false, true}) {
        button_image_fixture(assets, "bad-size.sti", 3, true);
        {
            std::fstream file(assets / "bad-size.sti", std::ios::in | std::ios::out | std::ios::binary);
            STCIHeader header{};
            file.read(reinterpret_cast<char*>(&header), STCI_HEADER_SIZE);
            if (corrupt_app_data)
                header.uiAppDataSize = 0xffffffffu;
            else
                header.uiStoredSize = 0xffffffffu;
            file.seekp(0);
            file.write(reinterpret_cast<const char*>(&header), STCI_HEADER_SIZE);
            REQUIRE(file.good());
        }
        REQUIRE(!CreateImage("bad-size.sti", IMAGE_ALLDATA));
        REQUIRE(CreateImage("bad-size.sti", IMAGE_PALETTE));
    }
    // A button retains its graphics after the loader's reference and the
    // image manager are released; the last button releases the resource.
    REQUIRE(MSYS_Init());
    auto graphics = ButtonGraphics::Load("button.sti", 0, 1, 2, 3, 4);
    REQUIRE(graphics);
    REQUIRE((graphics->MaxWidth == 1 && graphics->MaxHeight == 1));
    std::weak_ptr<const ButtonGraphics> lifetime = graphics;
    auto first_button = QuickCreateButton(graphics, 0, 0, BUTTON_NO_TOGGLE,
                                          MSYS_PRIORITY_NORMAL, nullptr, nullptr);
    auto second_button = QuickCreateButton(graphics, 2, 0, BUTTON_NO_TOGGLE,
                                           MSYS_PRIORITY_NORMAL, nullptr, nullptr);
    REQUIRE((first_button && second_button));
    // Give each frame a distinct color and verify actual blits for both
    // button kinds, including checkbox-only mouse-down highlights.
    auto* sprite = graphics->object();
    for (unsigned frame = 0; frame < 5; ++frame) {
        sprite->sprites[frame].pixels[0] = frame + 1;
        sprite->p16BPPPalette[frame + 1] = 0x1000 + frame;
    }
    VSURFACE_DESC target_desc{};
    target_desc.fCreateFlags = VSURFACE_CREATE_DEFAULT;
    target_desc.usWidth = target_desc.usHeight = 8;
    target_desc.ubBitDepth = 16;
    UINT32 target;
    REQUIRE(AddVideoSurface(&target_desc, &target));
    const auto old_target = ButtonDestBuffer;
    const auto old_hilights = gfRenderHilights;
    const auto old_left_down = gfLeftButtonState;
    ButtonDestBuffer = target;
    gfRenderHilights = true;
    for (bool checkbox : {false, true}) {
        for (bool enabled : {false, true}) {
            for (bool pressed : {false, true}) {
                for (bool hover : {false, true}) {
                    for (bool mouse_down : {false, true}) {
                        first_button->uiFlags = (checkbox ? BUTTON_CHECKBOX : BUTTON_QUICK) |
                            (enabled ? BUTTON_ENABLED : 0) | (pressed ? BUTTON_CLICKED_ON : 0);
                        if (hover) first_button->Area.uiFlags |= MSYS_MOUSE_IN_AREA;
                        else first_button->Area.uiFlags &= ~MSYS_MOUSE_IN_AREA;
                        gfLeftButtonState = mouse_down;
                        DrawButton(first_button);
                        UINT32 pitch;
                        auto* pixels = reinterpret_cast<UINT16*>(LockVideoSurface(target, &pitch));
                        REQUIRE(pixels);
                        const auto color = pixels[0];
                        UnLockVideoSurface(target);
                        const bool highlight = hover && (!checkbox || mouse_down);
                        const unsigned expected_frame = !enabled ? 0 :
                            pressed ? (highlight ? 4 : 3) : (highlight ? 2 : 1);
                        REQUIRE(color == 0x1000 + expected_frame);
                    }
                }
            }
        }
    }
    ButtonDestBuffer = old_target;
    gfRenderHilights = old_hilights;
    gfLeftButtonState = old_left_down;
    REQUIRE(DeleteVideoSurfaceFromIndex(target));
    graphics.reset();
    ShutdownButtonImageManager();
    REQUIRE(!lifetime.expired());
    first_button.reset();
    REQUIRE(second_button->quick_image->object()->sprites[4].pixels[0] == 5);
    second_button.reset();
    REQUIRE(lifetime.expired());
    REQUIRE(RegisteredButtonCount() == 0);
    // Validate each of the five states, including invalid negative indices.
    for (int bad : {-2, 9, 65536}) {
        for (size_t state = 0; state < 5; ++state) {
            std::array<int, 5> frames{0, 1, 2, 3, 4};
            frames[state] = bad;
            REQUIRE(!ButtonGraphics::Load("button.sti", frames[0], frames[1], frames[2],
                                           frames[3], frames[4]));
        }
    }
    REQUIRE(!ButtonGraphics::Load("missing.sti", 0, 1, 2, 3, 4));
    REQUIRE(!ButtonGraphics::Load("button.sti", -1, -1, -1, -1, -1));
    REQUIRE(ButtonGraphics::Load("button.sti", -1, 0, -1, 1, -1));
    std::vector<ButtonImage> quick_images;
    for (unsigned index = 0; index < 300; ++index)
        quick_images.push_back(ButtonGraphics::Load("button.sti", 0, 1, 2, 3, 4));
    REQUIRE(std::all_of(quick_images.begin(), quick_images.end(),
                        [](const auto& item) { return item != nullptr; }));
    quick_images.clear();
    {
        auto dialog = std::make_unique<W8DialogButton>();
        REQUIRE(dialog->ConfigureIcon("button.sti", 0, 1, MSYS_PRIORITY_NORMAL,
                                      W8_DIALOG_BUTTON_NO_TOOLTIP));
        dialog->SetPosition(12, 13);
        REQUIRE(!dialog->ConfigureIcon("missing.sti", 0, 1, MSYS_PRIORITY_NORMAL,
                                       W8_DIALOG_BUTTON_NO_TOOLTIP));
        REQUIRE((dialog->GetX() == 12 && dialog->GetY() == 13 && dialog->GetWidth() == 1));
        REQUIRE(RegisteredButtonCount() == 1);
    }
    REQUIRE(RegisteredButtonCount() == 0);
    // A dialog's click callback can close it. The dispatcher must not read
    // the released dialog while completing the click (including its sound).
    auto* closing_dialog = new W8DialogButton;
    REQUIRE(closing_dialog->Configure("button.sti", 0, 1, 2, 3, 4,
        [](W8DialogButton* dialog) { delete dialog; }, nullptr, false,
        MSYS_PRIORITY_NORMAL, W8_DIALOG_BUTTON_NO_TOOLTIP, nullptr, nullptr));
    auto dispatch_button = std::make_shared<GUI_BUTTON>();
    SetButtonUserDataPointer(dispatch_button, closing_dialog);
    dispatch_button->uiFlags = BUTTON_CLICKED_ON;
    PushButtonSoundScheme(0, true);
    DialogButtonCallback(dispatch_button.get(), MSYS_CALLBACK_REASON_LBUTTON_UP);
    REQUIRE(RegisteredButtonCount() == 0);
    MSYS_Shutdown();

    REQUIRE(!InitializeButtonImageManager());
    button_image_fixture(assets, DEFAULT_GENERIC_BUTTON_ON);
    REQUIRE(InitializeButtonImageManager());
    const std::string_view filename = "button.sti", missing = "missing.sti";
    const auto load = [](const std::array<std::string_view, 6>& names) {
        return LoadGenericButtonImages(names[2], names[0], names[3], names[1],
                                       names[4], names[5], 3, 4, 5);
    };
    for (size_t index = 0; index < 6; ++index) {
        std::array<std::string_view, 6> names;
        names.fill(filename);
        names[index] = missing;
        REQUIRE(!load(names));
    }
    REQUIRE(!load({std::string_view{}, filename, {}, {}, {}, {}}));
    REQUIRE(!load({filename, {}, {}, {}, {}, {}}));
    REQUIRE(!load({"short.sti", filename, {}, {}, {}, {}}));
    // Every border state needs all nine tiles, not only the off-normal state.
    for (size_t state = 0; state < 5; ++state) {
        std::array<std::string_view, 6> names;
        names.fill(filename);
        names[state] = "short.sti";
        REQUIRE(!load(names));
    }
    REQUIRE(!LoadGenericButtonImages({}, filename, {}, filename, {}, filename, -1, 0, 0));
    REQUIRE(!LoadGenericButtonImages({}, filename, {}, filename, {}, filename, 9, 0, 0));
    // A zero-sized first tile would cause division by zero while drawing borders.
    button_image_fixture(assets, "zero-tile.sti");
    {
        std::fstream file(assets / "zero-tile.sti", std::ios::in | std::ios::out | std::ios::binary);
        STCISubImage frame{};
        file.seekg(STCI_HEADER_SIZE + STCI_8BIT_PALETTE_SIZE);
        file.read(reinterpret_cast<char*>(&frame), sizeof(frame));
        frame.usWidth = 0;
        file.seekp(STCI_HEADER_SIZE + STCI_8BIT_PALETTE_SIZE);
        file.write(reinterpret_cast<const char*>(&frame), sizeof(frame));
        REQUIRE(file.good());
    }
    REQUIRE(!load({filename, "zero-tile.sti", {}, {}, {}, {}}));
    auto image = load({filename, filename, filename, filename, filename, filename});
    REQUIRE(image);
    REQUIRE((image->frames[2] && image->frames[3] && image->frames[4]));
    REQUIRE((image->frames[5] && image->background_frame == 3));
    REQUIRE((image->offset_x == 4 && image->offset_y == 5));
    REQUIRE(image->fill_color == image->frames[0]->p16BPPPalette[1]);
    auto another_owner = image;
    image.reset();
    REQUIRE(another_owner->frames[0]);
    another_owner.reset();

    // There is no fixed image slot limit. Lifetime is owned by the image.
    std::vector<GenericButtonImage> images;
    for (int index = 0; index < 45; ++index)
        images.push_back(load({filename, filename, {}, {}, {}, {}}));
    REQUIRE(std::all_of(images.begin(), images.end(), [](const auto& item) { return item != nullptr; }));
    images.clear();

    for (unsigned repeat = 0; repeat < 32; ++repeat) {
        REQUIRE(InitializeFontManager(CreateEnglishTransTable()));
        REQUIRE(LoadFontFile(std::string(filename).c_str()) == 0);
        auto* font = GetFontObject(0);
        REQUIRE((font && font->ownedPalette && font->sprites.size() == 9));
        UINT16 borrowed[256]{};
        REQUIRE(SetFontObjectPalette16BPP(0, borrowed) == borrowed);
        REQUIRE(GetFontObjectPalette16BPP(0) == borrowed);
        REQUIRE(GetFontObject(0) == font);
        ShutdownFontManager();
        ShutdownFontManager();
    }
    REQUIRE((InitializeFontManager(std::vector<UINT16>{'?', 0x00e9, 0x4e2d, 'e', 0x0301, 'a', 'b', ' ', 'c'})));
    REQUIRE(LoadFontFile(std::string(filename).c_str()) == 0);
    REQUIRE(StringPixLength("é", 0) > 0);
    REQUIRE(StringCodepointPrefixPixelWidth(0, 1, "é中") == StringPixLength("é", 0));
    REQUIRE(StringCodepointPrefixPixelWidth(0, 2, "é中") == StringPixLength("é中", 0));
    // Render borrowed substrings without depending on a trailing NUL or
    // accidentally consuming the next glyph in the backing string.
    SetFont(0);
    FontDestRegion = {0, 0, 64, 32};
    std::vector<UINT16> expected_pixels(64 * 32), view_pixels(64 * 32);
    DrawFontTextToBuffer(reinterpret_cast<UINT8*>(expected_pixels.data()), 64 * 2, 0, 0, 0, "é");
    const std::string backing = "é中";
    DrawFontTextToBuffer(reinterpret_cast<UINT8*>(view_pixels.data()), 64 * 2, 0, 0, 0,
                         std::string_view(backing).substr(0, 2));
    REQUIRE(view_pixels == expected_pixels);
    REQUIRE(
        std::any_of(view_pixels.begin(), view_pixels.end(), [](auto pixel) { return pixel != 0; }));
    // Explicit-buffer rendering must use its font argument, even when the
    // current surface font has a different palette.
    REQUIRE(LoadFontFile(std::string(filename).c_str()) == 1);
    auto* alternate = GetFontObjectPalette16BPP(1);
    std::fill(alternate, alternate + 256, UINT16{0x1234});
    std::fill(expected_pixels.begin(), expected_pixels.end(), 0);
    std::fill(view_pixels.begin(), view_pixels.end(), 0);
    SetFont(1);
    DrawFontTextToBuffer(reinterpret_cast<UINT8*>(expected_pixels.data()), 128, 1, 0, 0, "é中");
    SetFont(0);
    DrawFontTextToBuffer(reinterpret_cast<UINT8*>(view_pixels.data()), 128, 1, 0, 0, "é中");
    REQUIRE(view_pixels == expected_pixels);
    // Rebuilding word ranges preserves UTF-8 byte boundaries and discards
    // old hover state, including when a line becomes empty.
    auto level = std::make_unique<W8LevelRuntimeBlock>();
    auto* previous_level = g_level_block;
    g_level_block = level.get();
    level->text_box_font = 0;
    W8MessageStorageRecord words{};
    words.text = "  é 中  ";
    words.RebuildEntries();
    REQUIRE(words.entries.size() == 2);
    REQUIRE((words.entries[0].start == 2 && words.entries[0].end == 3));
    REQUIRE((words.entries[1].start == 5 && words.entries[1].end == 7));
    REQUIRE(words.entries[1].x_start == StringPixLength("  é ", 0));
    words.entries[0].keyword = W8_NOTICE_WORD_SELECTED;
    words.text = "中";
    words.RebuildEntries();
    REQUIRE((words.entries.size() == 1 && words.entries[0].keyword == W8_NOTICE_WORD_NORMAL));
    words.text.clear();
    words.RebuildEntries();
    REQUIRE(words.entries.empty());
    g_level_block = previous_level;
    g_font12point1 = 0;
    MSYS_Init();
    InitTextInputMode();
    const auto field = AddTextInputField(0, 0, 80, 20, MSYS_PRIORITY_NORMAL, "", 1000, 0x0f, 0);
    SetActiveField(field);
    const std::string clusters = "é👩‍👩‍👧‍👦中";
    SetInputFieldText(field, clusters);
    SetTextInputCursor(clusters.size());
    InputAtom backspace{};
    backspace.usEvent = KEY_DOWN;
    backspace.usParam = 8;
    REQUIRE(HandleTextInput(&backspace));
    REQUIRE(GetTextFromField(field) == "é👩‍👩‍👧‍👦");
    REQUIRE(HandleTextInput(&backspace));
    REQUIRE(GetTextFromField(field) == "é");
    REQUIRE(HandleTextInput(&backspace));
    REQUIRE(GetTextFromField(field).empty());
    InputAtom committed{};
    committed.usEvent = KEY_DOWN;
    committed.usParam = 0x4e2d;
    committed.uiParam = NATIVE_TEXT_INPUT_FLAG;
    REQUIRE(HandleTextInput(&committed));
    REQUIRE(GetTextFromField(field) == "中");
    committed.usParam = 0x1f600;
    REQUIRE(HandleTextInput(&committed));
    REQUIRE(GetTextFromField(field) == "中😀");
    SetInputFieldText(field, "");
    const std::array<char32_t, 7> family = {0x1f469, 0x200d, 0x1f469, 0x200d, 0x1f467, 0x200d, 0x1f466};
    for (const auto scalar : family) {
        committed.usParam = scalar;
        REQUIRE(HandleTextInput(&committed));
    }
    REQUIRE(GetTextFromField(field) == "👩‍👩‍👧‍👦");
    REQUIRE(HandleTextInput(&backspace));
    REQUIRE(GetTextFromField(field).empty());
    std::string long_text;
    for (unsigned n = 0; n < 100; ++n) long_text += "中";
    SetInputFieldText(field, long_text);
    SetTextInputCursor(long_text.size());
    REQUIRE(GetTextInputByteCursor() == 300);
    REQUIRE(GetTextInputFieldByteLength(field) == 300);
    REQUIRE(HandleTextInput(&backspace));
    REQUIRE((GetTextInputByteCursor() == 297 && GetTextFromField(field).size() == 297));
    KillTextInputMode();
    MSYS_Shutdown();
    ShutdownFontManager();
    ShutdownButtonImageManager();

    std::filesystem::remove_all(root);
}
}

TEST_CASE("Screen stack ownership", "[owner]")
{
    screen_stack();
}

TEST_CASE("Surface region ownership", "[owner]")
{
    surface_regions();
}

TEST_CASE("Camp effect ownership", "[owner]")
{
    camp_effects();
}

TEST_CASE("Button and mouse region ownership", "[owner]")
{
    buttons_and_regions();
}

TEST_CASE("Callback reentrancy and region teardown", "[owner]")
{
    region_registration_and_callback_lifetimes();
}

TEST_CASE("Sprite ownership", "[owner]")
{
    image_and_sprite_ownership();
}

TEST_CASE("Font table ownership", "[owner]")
{
    font_table_ownership();
}

TEST_CASE("Text input ownership", "[owner]")
{
    text_input_ownership();
}

TEST_CASE("Formatting contracts", "[owner]")
{
    formatted_strings();
}

TEST_CASE("Generic button image ownership", "[owner]")
{
    generic_button_images();
}

TEST_CASE("assert-plain", "[.death]")
{
    SetVideoObjectPalette(nullptr, nullptr);
    FAIL("SetVideoObjectPalette did not abort");
}

TEST_CASE("assert-message", "[.death]")
{
    CreateTextButton("invalid", 0, 0, 0, {}, -1, 0, 40, 20,
                     BUTTON_TOGGLE, MSYS_PRIORITY_NORMAL, nullptr, nullptr);
    FAIL("CreateTextButton did not abort");
}
