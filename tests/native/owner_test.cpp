#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/layouts/character.h"
#include "wiz8/xstatus.h"
#include "wiz8/video_object_catalog.h"
#include "wiz8/utility.h"
#include "Button System.h"
#include "input.h"
#include "mousesystem_macros.h"
#include "vsurface_private.h"
#include "imgfmt.h"
#include "FileMan.h"
#include "temporary_directory.h"
#include "wiz8/asset_paths.h"
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
extern HVOBJECT GenericButtonGrayed[], GenericButtonOffNormal[], GenericButtonOffHilite[];
extern HVOBJECT GenericButtonOnNormal[], GenericButtonOnHilite[], GenericButtonBackground[];
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
    image.pPalette = palette.data();
    image.pPixData8 = &pixels;
    image.uiSizePixData = 1;
    image.pETRLEObject = images.data();
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
    GUI_BUTTON button{};
    button.IDNum = 0;
    button.ClickCallback = click;
    button.uiFlags = BUTTON_ENABLED | BUTTON_CLICK_CALLBACK;
    ButtonList[0] = &button;
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
    MSYS_RemoveRegion(&high);
    MSYS_RemoveRegion(&low);
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

void button_image_fixture(const std::filesystem::path& root, std::string name, unsigned count = 9)
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
}

void generic_button_images()
{
    const std::filesystem::path root = make_temporary_directory("wiz8-button-images");
    const auto assets = root / "assets";
    button_image_fixture(assets, DEFAULT_GENERIC_BUTTON_OFF);
    button_image_fixture(assets, "button.sti");
    button_image_fixture(assets, "short.sti", 8);
    w8_native::configure_paths({assets.string(), (root / "user").string(), {"", "", ""}});
    CHECK(InitializeFileManager(nullptr));
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
    ShutdownButtonImageManager();
    CHECK(FindFreeGenericSlot() == 0 && !GenericButtonOnNormal[39]);
    ShutdownFileManager();
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
    formatted_strings();
    generic_button_images();
    return 0;
}
