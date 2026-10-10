#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/layouts/character.h"
#include "wiz8/xstatus.h"
#include "wiz8/video_object_catalog.h"
#include "wiz8/utility.h"
#include "Button System.h"
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

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #expression); exit(1); } } while (false)

extern GUI_BUTTON* gpAnchoredButton;
extern std::array<std::unique_ptr<SGPVObject>, 40> GenericButtonGrayed, GenericButtonOffNormal, GenericButtonOffHilite;
extern std::array<std::unique_ptr<SGPVObject>, 40> GenericButtonOnNormal, GenericButtonOnHilite, GenericButtonBackground;
extern UINT16 GenericButtonFillColors[], GenericButtonBackgroundIndex[];
extern INT16 GenericButtonOffsetX[], GenericButtonOffsetY[];

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
    CHECK(GetPendingScreenState() == W8_SCREEN_NONE);
    int payload;
    for (int index = 0; index < 40; ++index) {
        g_pending_screen_state = {};
        g_pending_screen_state.id = index % 2 ? W8_SCREEN_OPTIONS : W8_SCREEN_INTRO;
        g_pending_screen_state.parameter = index;
        g_pending_screen_state.parameter_3 = &payload;
        snprintf(g_pending_screen_state.name, sizeof(g_pending_screen_state.name), "screen %d", index);
        CHECK(GetPendingScreenState() == g_pending_screen_state.id);
        GameLoop();
        CHECK(g_current_screen_state.parameter == index);
        CHECK(g_screen_return_stack.size() == static_cast<size_t>(index));
    }
    CHECK(screen_entries == 40 && screen_frames == 40);
    CHECK(screen_leaves.size() == 39 && screen_leaves.back() == 0);
    CHECK(GetPendingScreenState() == W8_SCREEN_INTRO);
    for (int index = 38; index >= 0; --index) {
        RequestScreenTransition();
        GameLoop();
        CHECK(g_current_screen_state.parameter == index);
        CHECK(g_current_screen_state.parameter_3 == &payload);
        CHECK(g_current_screen_state.name == std::string("screen ") + std::to_string(index));
        CHECK(g_screen_return_stack.size() == static_cast<size_t>(index));
        CHECK(screen_leaves.back() == 1);
    }
    g_screen_return_stack.push_back(g_current_screen_state);
    g_pending_screen_state = g_current_screen_state;
    g_pending_screen_state.id = W8_SCREEN_OPTIONS;
    g_pending_screen_state.parameter = 99;
    RequestScreenTransition();
    GameLoop();
    CHECK(g_current_screen_state.parameter == 99 && g_screen_return_stack.size() == 1);
    RequestScreenTransition();
    GameLoop();
    CHECK(g_current_screen_state.parameter == 0 && g_screen_return_stack.empty());
    gfProgramIsRunning = true;
    RequestScreenTransition();
    GameLoop();
    CHECK(!gfProgramIsRunning && g_current_screen_state.id == W8_SCREEN_NONE);
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
    CHECK(surface && surface->RegionList.empty());
    VSURFACE_REGION result{};
    result.ubHitMask = 99;
    CHECK(!GetVSurfaceRegion(surface, 0, &result) && result.ubHitMask == 99);
    for (int index = 0; index < 80; ++index) {
        VSURFACE_REGION region{};
        region.Origin.iX = index;
        region.RegionCoords.iRight = index + 1;
        region.ubHitMask = static_cast<UINT8>(index);
        surface->RegionList.push_back(region);
    }
    CHECK(GetVSurfaceRegion(surface, 79, &result));
    CHECK(result.Origin.iX == 79 && result.RegionCoords.iRight == 80 && result.ubHitMask == 79);
    CHECK(!GetVSurfaceRegion(surface, 80, &result));
    CHECK(!GetVSurfaceRegion(surface, 1, nullptr));
    auto* borrowed = CreateVideoSurfaceFromCpuSurface(surface->surface);
    CHECK(borrowed && borrowed->RegionList.empty());
    borrowed->RegionList.push_back(result);
    surface->RegionList.clear();
    CHECK(GetVSurfaceRegion(borrowed, 0, &result));
    CHECK(DeleteVideoSurface(borrowed));
    CHECK(surface->surface && DeleteVideoSurface(surface));
}

void camp_effects()
{
    CHECK(InitializeVideoObjectManager());
    std::array<SGPPaletteEntry, 256> palette{};
    std::array<ETRLEObject, 12> images{};
    for (auto& image : images) { image.usWidth = 16; image.usHeight = 8; }
    UINT8 pixels = 0;
    image_type image{};
    image.ubBitDepth = 8;
    image.fFlags = IMAGE_TRLECOMPRESSED;
    image.pPalette = std::make_unique<SGPPaletteEntry[]>(256);
    std::copy_n(palette.data(), palette.size(), image.pPalette.get());
    image.pImageData = std::make_unique<UINT8[]>(1);
    image.pImageData[0] = pixels;
    image.uiSizePixData = 1;
    image.pETRLEObject = std::make_unique<ETRLEObject[]>(images.size());
    std::copy_n(images.data(), images.size(), image.pETRLEObject.get());
    image.usNumberOfObjects = images.size();
    VOBJECT_DESC desc{};
    desc.fCreateFlags = VOBJECT_CREATE_FROMHIMAGE;
    desc.hImage = &image;
    UINT32 handle;
    CHECK(AddVideoObject(&desc, &handle));
    const auto saved_slot = g_video_slots[0x86];
    const auto saved_frame = g_video_frames[0];
    g_video_slots[0x86] = {0, 0};
    g_video_frames[0].loaded = true;
    g_video_frames[0].storage_kind = W8_VIDEO_STORAGE_OBJECT;
    g_video_frames[0].handle = handle;
    {
        auto state = std::make_unique<W8CampScreenState>();
        CHECK(state->effect_list.empty() && state->stats_range == nullptr);
        g_camp_screen = state.get();
        W8CampStatsRange range;
        state->stats_range = &range;
        state->effect_list = {
            {false, false, true, false, 0, 0, 0, 3, 10}, {false, false, false, true, 0, 1, 0, 3, 9},
            {true, false, true, false, 2, 2, 0, 9999, 3}, {false, false, true, true, 0, 3, 0, 3, 2}};
        state->effect_filter = W8_CAMP_EFFECT_FILTER_ALL;
        state->effect_scroll = 100;
        FilterCampEffectList();
        CHECK(state->effect_visible_lines == 24 && state->effect_scroll == 7);
        CHECK(state->effect_first_visible == 0 && state->effect_last_visible == 3);
        CHECK(range.m_range->m_enabled && range.m_range->m_value == 7);
        state->effect_filter = W8_CAMP_EFFECT_FILTER_BENEFICIAL;
        FilterCampEffectList();
        CHECK(state->effect_visible_lines == 14 && state->effect_scroll == 0);
        CHECK(!range.m_range->m_enabled && !state->effect_list[1].visible);
        state->effect_filter = W8_CAMP_EFFECT_FILTER_DETRIMENTAL;
        FilterCampEffectList();
        CHECK(state->effect_visible_lines == 13 && state->effect_list[3].visible);
        state->effect_items_only = true;
        state->effect_filter = W8_CAMP_EFFECT_FILTER_ALL;
        FilterCampEffectList();
        CHECK(state->effect_visible_lines == 4 && state->effect_first_visible == 2);
        W8Character character{};
        for (auto& condition : character.uiCondition) condition = 1;
        for (auto& enchantment : character.enchantments) enchantment.turns = 2;
        g_review_character = &character;
        state->effect_items_only = false;
        RebuildCampEffectList();
        CHECK(state->effect_list.size() == 28);
        CHECK(state->effect_detrimental_count == 20 && state->effect_beneficial_count == 8);
        CHECK(state->effect_list.front().index == 19 && state->effect_list.back().index == 0);
        CHECK(state->effect_visible_lines == 64 && range.m_range->m_enabled);
        character = {};
        RebuildCampEffectList();
        CHECK(state->effect_list.empty() && state->effect_visible_lines == 0);
        CHECK(state->effect_beneficial_count == 0 && !range.m_range->m_enabled);
        g_review_character = nullptr;
    }
    g_camp_screen = nullptr;
    g_video_slots[0x86] = saved_slot;
    g_video_frames[0] = saved_frame;
    CHECK(ShutdownVideoObjectManager());
}

int clicks, click_reason;
MOUSE_REGION* clicked_region;
void region_click(MOUSE_REGION* region, INT32) { clicked_region = region; }
void click(GUI_BUTTON*, INT32 reason) { ++clicks; click_reason = reason; }
int nested_outer, nested_inner;
void nested_click(GUI_BUTTON* button, INT32 reason)
{
    if (button->IDNum == nested_inner) {
        RemoveButton(nested_outer);
        return;
    }
    QuickButtonCallbackMButn(&ButtonList[nested_inner]->Area, reason);
    CHECK(ButtonList[nested_outer].get() == button);
    CHECK(button->uiFlags & BUTTON_DELETION_PENDING);
    RemoveButton(nested_inner);
}
void remove_on_move(MOUSE_REGION* region, INT32 reason)
{
    if (reason & MSYS_CALLBACK_REASON_GAIN_MOUSE)
        MSYS_RemoveRegion(region);
}
void remove_button_on_callback(GUI_BUTTON* button, INT32)
{
    RemoveButton(button->IDNum);
    CHECK(ButtonList[button->IDNum].get() == button);
}

void region_registration_and_callback_lifetimes()
{
    CHECK(MSYS_Init());
    MOUSE_REGION first{}, latest{}, temporary{};
    MSYS_DefineRegion(&first, 0, 0, 40, 40, MSYS_PRIORITY_NORMAL, nullptr, region_click);
    const auto first_id = first.IDNumber;
    for (unsigned registration = 0; registration < 65536; ++registration) {
        MSYS_DefineRegion(&temporary, 0, 0, 40, 40, MSYS_PRIORITY_NORMAL, nullptr, region_click);
        CHECK(temporary.IDNumber != 0 && temporary.IDNumber != first_id);
        MSYS_RemoveRegion(&temporary);
    }
    MSYS_DefineRegion(&latest, 0, 0, 40, 40, MSYS_PRIORITY_NORMAL, nullptr, region_click);
    MSYS_SGP_Mouse_Handler_Hook(LEFT_BUTTON_DOWN, 10, 10, true, false);
    CHECK(clicked_region == &latest);
    MSYS_SGP_Mouse_Handler_Hook(LEFT_BUTTON_UP, 10, 10, false, false);
    MSYS_RemoveRegion(&latest);
    MSYS_DefineRegion(&temporary, 0, 0, 40, 40, MSYS_PRIORITY_HIGH, remove_on_move, region_click);
    MSYS_SGP_Mouse_Handler_Hook(MOUSE_POS, 11, 11, false, false);
    CHECK(!(temporary.uiFlags & MSYS_REGION_EXISTS));
    MSYS_SGP_Mouse_Handler_Hook(LEFT_BUTTON_DOWN, 11, 11, true, false);
    CHECK(clicked_region == &first);
    MSYS_SGP_Mouse_Handler_Hook(LEFT_BUTTON_UP, 11, 11, false, false);
    MSYS_RemoveRegion(&first);

    wchar_t label[] = L"nested";
    nested_outer = CreateTextButton(label, 0, 0, 0, -1, 0, 0, 40, 20,
                                   BUTTON_TOGGLE, MSYS_PRIORITY_NORMAL, nullptr, nested_click);
    nested_inner = CreateTextButton(label, 0, 0, 0, -1, 0, 20, 40, 20,
                                   BUTTON_TOGGLE, MSYS_PRIORITY_NORMAL, nullptr, nested_click);
    CHECK(nested_outer >= 0 && nested_inner >= 0);
    QuickButtonCallbackMButn(&ButtonList[nested_outer]->Area, MSYS_CALLBACK_REASON_LBUTTON_DWN);
    CHECK(!ButtonList[nested_outer] && !ButtonList[nested_inner]);
    for (const bool disabled : {false, true}) {
        const auto id = CreateTextButton(label, 0, 0, 0, -1, 0, 0, 40, 20,
                                        BUTTON_TOGGLE, MSYS_PRIORITY_NORMAL,
                                        remove_button_on_callback, remove_button_on_callback);
        CHECK(id >= 0);
        if (disabled) {
            ButtonList[id]->uiFlags &= ~BUTTON_ENABLED;
            ButtonList[id]->uiFlags |= BUTTON_ALLOW_DISABLED_CALLBACK;
            QuickButtonCallbackMButn(&ButtonList[id]->Area, MSYS_CALLBACK_REASON_LBUTTON_DWN);
        } else {
            QuickButtonCallbackMMove(&ButtonList[id]->Area, MSYS_CALLBACK_REASON_GAIN_MOUSE);
        }
        CHECK(!ButtonList[id]);
    }
    MSYS_Shutdown();
    CHECK(MSYS_Init());
    MSYS_Shutdown();
}

void buttons_and_regions()
{
    CHECK(MSYS_Init());
    MOUSE_REGION low{}, high{};
    MSYS_DefineRegion(&low, 0, 0, 40, 40, MSYS_PRIORITY_LOW, nullptr, region_click);
    MSYS_DefineRegion(&high, 0, 0, 20, 20, MSYS_PRIORITY_HIGH, nullptr, region_click);
    MSYS_SGP_Mouse_Handler_Hook(LEFT_BUTTON_DOWN, 10, 10, true, false);
    CHECK(clicked_region == &high);
    MSYS_SGP_Mouse_Handler_Hook(LEFT_BUTTON_UP, 10, 10, false, false);
    MSYS_SGP_Mouse_Handler_Hook(LEFT_BUTTON_DOWN, 30, 30, true, false);
    CHECK(clicked_region == &low);
    MSYS_SGP_Mouse_Handler_Hook(LEFT_BUTTON_UP, 30, 30, false, false);
    MSYS_SetRegionUserData(&low, 0, 17);
    CHECK(MSYS_GetRegionUserData(&low, 0) == 17);
    CHECK(MSYS_GrabMouse(&high) == MSYS_GRABBED_OK);
    MSYS_ReleaseMouse(&high);
    ButtonList[0] = std::make_unique<GUI_BUTTON>();
    auto& button = *ButtonList[0];
    button.IDNum = 0;
    button.ClickCallback = click;
    button.uiFlags = BUTTON_ENABLED | BUTTON_CLICK_CALLBACK;
    QuickButtonCallbackMButn(&button.Area, MSYS_CALLBACK_REASON_LBUTTON_DWN);
    CHECK(clicks == 1 && click_reason == MSYS_CALLBACK_REASON_LBUTTON_DWN);
    button.uiFlags &= ~BUTTON_ENABLED;
    QuickButtonCallbackMButn(&button.Area, MSYS_CALLBACK_REASON_LBUTTON_DWN);
    CHECK(clicks == 1);
    button.uiFlags |= BUTTON_ALLOW_DISABLED_CALLBACK;
    QuickButtonCallbackMButn(&button.Area, MSYS_CALLBACK_REASON_LBUTTON_DWN);
    CHECK(clicks == 2 && (click_reason & BUTTON_DISABLED_CALLBACK));
    button.uiFlags = BUTTON_ENABLED;
    QuickButtonCallbackMButn(&button.Area, MSYS_CALLBACK_REASON_LBUTTON_DWN);
    CHECK(button.uiFlags & BUTTON_CLICKED_ON);
    button.uiFlags = BUTTON_ENABLED | BUTTON_CHECKBOX | BUTTON_CLICK_CALLBACK;
    QuickButtonCallbackMButn(&button.Area, MSYS_CALLBACK_REASON_LBUTTON_DWN);
    CHECK(!(button.uiFlags & BUTTON_CLICKED_ON));
    QuickButtonCallbackMButn(&button.Area, MSYS_CALLBACK_REASON_LBUTTON_UP);
    CHECK(button.uiFlags & BUTTON_CLICKED_ON);
    CHECK(gpAnchoredButton == &button);
    gpAnchoredButton = nullptr;
    ButtonList[0] = nullptr;
    for (unsigned repeat = 0; repeat < 32; ++repeat) {
        wchar_t label[] = L"owned button";
        const auto id = CreateTextButton(label, 0, 0, 0, -1, 0, 0, 40, 20,
                                         BUTTON_TOGGLE, MSYS_PRIORITY_NORMAL, nullptr, click);
        CHECK(id >= 0);
        auto* owned = ButtonList[id].get();
        CHECK(owned && owned->string[0] == L'o');
        label[0] = L'x';
        CHECK(owned->string[0] == L'o');
        SpecifyButtonText(id, owned->string.get());
        CHECK(ButtonList[id].get() == owned && owned->string[0] == L'o');
        wchar_t tooltip[] = L"tooltip";
        SetButtonFastHelpText(id, tooltip);
        tooltip[0] = L'x';
        CHECK(owned->Area.FastHelpText[0] == L't');
        SetButtonFastHelpText(id, owned->Area.FastHelpText.get());
        CHECK(owned->Area.FastHelpText[0] == L't');
        SetButtonFastHelpText(id, nullptr);
        CHECK(!owned->Area.FastHelpText);
        SetButtonFastHelpText(id, label);
        SpecifyButtonText(id, nullptr);
        CHECK(!owned->string);
        RemoveButton(id);
        CHECK(!ButtonList[id]);
    }
    MSYS_RemoveRegion(&high);
    MSYS_RemoveRegion(&low);
    MSYS_Shutdown();
}

void image_and_sprite_ownership()
{
    CHECK(InitializeVideoObjectManager());
    image_type image{};
    image.ubBitDepth = 8;
    image.fFlags = IMAGE_TRLECOMPRESSED | IMAGE_BITMAPDATA | IMAGE_PALETTE;
    image.pPalette = std::make_unique<SGPPaletteEntry[]>(256);
    image.pPalette[1] = {255, 0, 0, 0};
    image.pImageData = std::make_unique<UINT8[]>(3);
    image.pImageData[0] = 1;
    image.pImageData[1] = 1;
    image.pETRLEObject = std::make_unique<ETRLEObject[]>(1);
    image.pETRLEObject[0] = {0, 3, 0, 0, 1, 1};
    image.uiSizePixData = 3;
    image.usNumberOfObjects = 1;
    VOBJECT_DESC desc{};
    desc.fCreateFlags = VOBJECT_CREATE_FROMHIMAGE;
    desc.hImage = &image;
    std::array<UINT32, 64> ids{};
    std::array<HVOBJECT, 64> views{};
    for (unsigned i = 0; i < ids.size(); ++i) {
        CHECK(AddVideoObject(&desc, &ids[i]));
        CHECK(GetVideoObject(&views[i], ids[i]));
        CHECK(views[i]->pPixData.get() != image.pImageData.get());
        CHECK(views[i]->pETRLEObject.get() != image.pETRLEObject.get());
        CHECK(views[i]->pPaletteEntry.get() != image.pPalette.get());
        (void)CreateObjectPaletteTables(views[i], HVOBJECT_GLOW_GREEN);
        CHECK(views[i]->pShades[0] && views[i]->pShades[15]);
        CHECK(views[i]->pShades[4].get() == views[i]->ownedPalette.get());
        CHECK(SetObjectShade(views[i], 6));
        auto retained = views[i]->pShades[4];
        CHECK(SetVideoObjectPalette(views[i], image.pPalette.get()));
        CHECK(retained && views[i]->ownedPalette.get() != retained.get());
        CHECK(views[i]->pShades[4] == views[i]->ownedPalette);
        CHECK(SetObjectShade(views[i], 4));
        (void)CreateObjectPaletteTables(views[i], HVOBJECT_GLOW_RED);
        CHECK(views[i]->pShadeCurrent == views[i]->pShades[4].get());
    }
    CHECK(ReleaseImageData(&image, IMAGE_ALLDATA));
    CHECK(!image.pImageData && !image.pPalette && !image.pETRLEObject);
    CHECK(!image.uiSizePixData && !image.usNumberOfObjects);
    for (auto id : {ids[0], ids[31], ids[63]}) {
        CHECK(DeleteVideoObjectFromIndex(id));
        HVOBJECT missing = nullptr;
        CHECK(!GetVideoObject(&missing, id));
    }
    HVOBJECT surviving = nullptr;
    CHECK(GetVideoObject(&surviving, ids[32]) && surviving == views[32]);
    CHECK(surviving->pPixData[1] == 1 && surviving->pETRLEObject[0].usWidth == 1);
    UINT16 borrowed[256]{};
    surviving->p16BPPPalette = borrowed;
    CHECK(DestroyObjectPaletteTables(surviving));
    CHECK(!surviving->ownedPalette && !surviving->p16BPPPalette);
    for (const auto& shade : surviving->pShades) CHECK(!shade);
    CHECK(ShutdownVideoObjectManager());
    CHECK(InitializeVideoObjectManager() && ShutdownVideoObjectManager());

    VSURFACE_DESC surface_desc{};
    surface_desc.fCreateFlags = VSURFACE_CREATE_DEFAULT;
    surface_desc.usWidth = 8;
    surface_desc.usHeight = 8;
    surface_desc.ubBitDepth = 16;
    std::array<HVSURFACE, 64> surfaces{};
    for (unsigned i = 0; i < ids.size(); ++i) {
        CHECK(AddVideoSurface(&surface_desc, &ids[i]));
        CHECK(GetVideoSurface(&surfaces[i], ids[i]));
    }
    for (auto id : {ids[0], ids[31], ids[63]}) {
        CHECK(DeleteVideoSurfaceFromIndex(id));
        HVSURFACE missing = nullptr;
        CHECK(!GetVideoSurface(&missing, id));
    }
    HVSURFACE surface = nullptr;
    CHECK(GetVideoSurface(&surface, ids[32]) && surface == surfaces[32]);
    CHECK(surface->surface && surface->usWidth == 8);
    CHECK(ShutdownVideoSurfaceManager());
}

void font_table_ownership()
{
    for (unsigned repeat = 0; repeat < 32; ++repeat) {
        auto translation = CreateEnglishTransTable();
        CHECK(translation.size() == 252);
        CHECK(InitializeFontManager(translation));
        translation[1] = '?';
        CHECK(GetIndex('B') == 1);
        translation = {};
        CHECK(GetIndex('B') == 1);
        CHECK(InitializeFontManager(CreateEnglishTransTable()));
        CHECK(GetIndex('Z') == 25);
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
                                             L"alpha", 32, 0, 0);
        const auto middle = AddTextInputField(0, 20, 80, 20, MSYS_PRIORITY_NORMAL,
                                              L"beta", 32, 0, 0);
        const auto last = AddTextInputField(0, 40, 80, 20, MSYS_PRIORITY_NORMAL,
                                            L"gamma", 32, 0, 0);
        CHECK(first == 0 && middle == 1 && last == 2);
        wchar_t text[33]{};
        Get16BitStringFromField(first, text);
        CHECK(text[0] == L'a' && GetTextInputFieldLength(first) == 5);
        RemoveTextInputField(middle);
        CHECK(GetTextInputFieldLength(last) == 5);
        InitTextInputMode();
        CHECK(AddTextInputField(0, 0, 80, 20, MSYS_PRIORITY_NORMAL,
                                L"inner", 32, 0, 0) == 0);
        KillTextInputMode();
        Get16BitStringFromField(first, text);
        CHECK(text[0] == L'a' && GetTextInputFieldLength(last) == 5);
        CHECK(AddTextInputField(0, 60, 80, 20, MSYS_PRIORITY_NORMAL,
                                L"delta", 32, 0, 0) == 3);
        RemoveTextInputField(first);
        RemoveTextInputField(3);
        RemoveTextInputField(last);
        KillTextInputMode();
        CHECK(GetActiveTextInputField() == -1);
        InitTextInputMode();
        KillTextInputMode();
    }
    MSYS_Shutdown();
}

void formatted_strings()
{
    char* first = FormatString("%s %d", "first", 7);
    char* second = FormatString("%s %d", "second", 9);
    CHECK(strcmp(first, "first 7") == 0 && strcmp(second, "second 9") == 0);
    std::string oversized(1024, 'x');
    CHECK(strlen(FormatString("%s", oversized.c_str())) == 511);
    CHECK(strcmp(FormatString("%d", 42), "42") == 0);
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
        CHECK(file.good());
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
        CHECK(image && image->uiSizePixData == 9 && image->usNumberOfObjects == 3);
        auto* pixels = image->pImageData.get();
        auto* palette = image->pPalette.get();
        CHECK(LoadImageData(image.get(), IMAGE_APPDATA));
        CHECK(image->pImageData.get() == pixels && image->pPalette.get() == palette);
        CHECK(image->uiAppDataSize == 4 && image->pAppData[0] == 7 && image->pAppData[3] == 10);
        auto* app = image->pAppData.get();
        CHECK(LoadImageData(image.get(), IMAGE_PALETTE));
        CHECK(image->pImageData.get() == pixels && image->pAppData.get() == app);
        CHECK(image->pPalette.get() != palette && image->pPalette[1].peRed == 255);
        palette = image->pPalette.get();
        CHECK(ReleaseImageData(image.get(), IMAGE_BITMAPDATA));
        CHECK(image->pPalette.get() == palette && image->pAppData.get() == app);
        CHECK(!image->pImageData && !image->pETRLEObject && !image->usNumberOfObjects);
        CHECK(LoadImageData(image.get(), IMAGE_BITMAPDATA));
        CHECK(image->pPalette.get() == palette && image->pAppData.get() == app);
        CHECK(image->pImageData[1] == 1 && image->usNumberOfObjects == 3);
        pixels = image->pImageData.get();
        auto* objects = image->pETRLEObject.get();
        const auto flags = image->fFlags;
        std::filesystem::resize_file(assets / "metadata.sti",
            STCI_HEADER_SIZE + STCI_8BIT_PALETTE_SIZE + 3 * STCI_SUBIMAGE_SIZE + 7);
        CHECK(!LoadImageData(image.get(), IMAGE_ALLDATA));
        CHECK(image->pImageData.get() == pixels && image->pETRLEObject.get() == objects);
        CHECK(image->pPalette.get() == palette && image->pAppData.get() == app);
        CHECK(image->fFlags == flags && image->uiAppDataSize == 4 && image->uiSizePixData == 9);
    }
    CHECK(!InitializeButtonImageManager(-1, -1, -1));
    CHECK(FindFreeGenericSlot() == 0 && GenericButtonOffNormal[0] == nullptr);
    button_image_fixture(assets, DEFAULT_GENERIC_BUTTON_ON);
    CHECK(InitializeButtonImageManager(-1, -1, -1));
    CHECK(FindFreeGenericSlot() == 1 && GenericButtonOnNormal[0]);
    CHECK(!GenericButtonOffHilite[0] && !GenericButtonOnHilite[0]);
    CHECK(UnloadGenericButtonImage(0));
    UINT8 filename[] = "button.sti", missing[] = "missing.sti", short_image[] = "short.sti";
    const auto load = [](const std::array<UINT8*, 6>& names) {
        return LoadGenericButtonImages(names[2], names[0], names[3], names[1], names[4], names[5], 3, 4, 5);
    };
    for (size_t index = 0; index < 6; ++index) {
        std::array<UINT8*, 6> names;
        names.fill(filename);
        names[index] = missing;
        CHECK(load(names) == -1 && FindFreeGenericSlot() == 0);
        CHECK(!GenericButtonOffNormal[0] && !GenericButtonOnNormal[0]);
    }
    CHECK(load({nullptr, filename, nullptr, nullptr, nullptr, nullptr}) == -1);
    CHECK(load({filename, nullptr, nullptr, nullptr, nullptr, nullptr}) == -1);
    CHECK(load({short_image, filename, nullptr, nullptr, nullptr, nullptr}) == -1);
    CHECK(FindFreeGenericSlot() == 0);
    CHECK(load({filename, filename, filename, filename, filename, filename}) == 0);
    CHECK(GenericButtonGrayed[0] && GenericButtonOffHilite[0] && GenericButtonOnHilite[0]);
    CHECK(GenericButtonBackground[0] && GenericButtonBackgroundIndex[0] == 3);
    CHECK(GenericButtonOffsetX[0] == 4 && GenericButtonOffsetY[0] == 5);
    CHECK(GenericButtonFillColors[0] == GenericButtonOffNormal[0]->p16BPPPalette[1]);
    CHECK(UnloadGenericButtonImage(0));
    CHECK(!GenericButtonBackground[0] && GenericButtonOffsetX[0] == 0);
    for (int slot = 0; slot < 40; ++slot) {
        CHECK(load({filename, filename, nullptr, nullptr, nullptr, nullptr}) == slot);
        CHECK(!GenericButtonGrayed[slot] && !GenericButtonBackground[slot]);
    }
    CHECK(load({filename, filename, nullptr, nullptr, nullptr, nullptr}) == -1);
    for (unsigned repeat = 0; repeat < 32; ++repeat) {
        CHECK(InitializeFontManager(CreateEnglishTransTable()));
        CHECK(LoadFontFile(reinterpret_cast<const char*>(filename)) == 0);
        auto* font = GetFontObject(0);
        CHECK(font && font->ownedPalette && font->usNumberOfObjects == 9);
        UINT16 borrowed[256]{};
        CHECK(SetFontObjectPalette16BPP(0, borrowed) == borrowed);
        CHECK(GetFontObjectPalette16BPP(0) == borrowed);
        CHECK(GetFontObject(0) == font);
        ShutdownFontManager();
        ShutdownFontManager();
    }
    ShutdownButtonImageManager();
    CHECK(FindFreeGenericSlot() == 0 && !GenericButtonOnNormal[39]);

    std::filesystem::remove_all(root);
}
}

int main(int argc, char** argv)
{
    if (argc == 2 && strcmp(argv[1], "--assert-plain") == 0) {
        SetVideoObjectPalette(nullptr, nullptr);
    }
    if (argc == 2 && strcmp(argv[1], "--assert-message") == 0) {
        MOUSE_REGION region{};
        region.UserData[0] = MAX_BUTTONS;
        QuickButtonCallbackMButn(&region, MSYS_CALLBACK_REASON_LBUTTON_DWN);
    }
    screen_stack();
    surface_regions();
    camp_effects();
    buttons_and_regions();
    region_registration_and_callback_lifetimes();
    image_and_sprite_ownership();
    font_table_ownership();
    text_input_ownership();
    formatted_strings();
    generic_button_images();
    return 0;
}
