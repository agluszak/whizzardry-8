#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_timer.h>
#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include <fmt/printf.h>

#include "Font.h"
#include "application.h"
#include "himage.h"
#include "image.h"
#include "image_assets.h"
#include "imgfmt.h"
#include "input.h"
#include "layouts/main_game_screen.h"
#include "layouts/screen_state.h"
#include "local_code/RangeControl.h"
#include "local_screens/RCSStatsPage.h"
#include "scoped_asset_roots.h"
#include "temporary_directory.h"
#include "wiz8/assertions.h"
#include "wiz8/asset_paths.h"
#include "wiz8/button_interaction.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/compat/compiler.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/CharacterSummaryDialog.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/dialog_code/DialogScrollBar.h"
#include "wiz8/dialog_code/DialogTextArea.h"
#include "wiz8/dialog_code/DialogTextEntry.h"
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/fonts.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/MonsterGenerator.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_screens/MGSButtons.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/local_screens/mipe.h"
#include "wiz8/modal_content.h"
#include "wiz8/sr_api.h"
#include "wiz8/surface_storage.h"
#include "wiz8/text_input.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"
#include "wiz8/utility.h"
#include "wiz8/xstatus.h"

namespace {
std::vector<int> screen_leaves;
int screen_entries, screen_frames;
unsigned char enter()
{
    ++screen_entries;
    return 1;
}
unsigned char leave(int discard)
{
    screen_leaves.push_back(discard);
    return 1;
}
void frame()
{
    ++screen_frames;
}

void button_image_fixture(const std::filesystem::path& root, std::string name, unsigned count = 9,
                          bool app_data = false, unsigned width = 1, unsigned height = 1,
                          std::array<std::uint8_t, 5> frame_palette_indices = {},
                          short offset_x = 0, short offset_y = 0);

void initialize_test_framebuffer()
{
    ResetFontRendering();
    g_pixel_format = 9;
    Initialize16BitPixelFormatMasks();
    REQUIRE(InitializePrimarySurface());
}

void screen_stack()
{
    gXStatus.character_event_queue = std::make_unique<W8CharacterEventQueue>();
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

void camp_effects()
{
    ScopedTemporaryDirectory directory{"wiz8-camp-effects"};
    const auto assets = directory.path / "assets";
    const auto previous_roots = w8_native::path_roots();
    button_image_fixture(assets, "data/main interface/main_scroll.sti", 12, false, 16, 8);
    g_image_assets.clear();
    w8_native::configure_paths({assets.string(), (directory.path / "user").string()});
    struct Restore {
        w8_native::Roots roots;
        ~Restore()
        {
            g_camp_screen.reset();
            g_image_assets.clear();
            w8_native::configure_paths(roots);
        }
    } restore{previous_roots};
    {
        g_camp_screen = std::make_unique<W8CampScreenState>();
        auto* state = g_camp_screen.get();
        REQUIRE((state->effect_list.empty() && state->stats_range == nullptr));
        const auto layer_count = LiveUiLayerCount();
        state->stats_range =
            std::make_unique<W8RangeControl>(state->scroll_input, 0x264, 0xbe, 0x276, 0x1b5);
        REQUIRE(LiveUiLayerCount() == layer_count);
        // Disabling one scrollbar must not disable unrelated targets in its layer.
        const auto consume = [](const UiPointerEvent&) { return UiResult::Handled; };
        auto& sibling = state->scroll_input.add(SDL_Rect{0, 0, 640, 480}, consume);
        state->stats_range->SetActive(true);
        state->stats_range->SetActive(false);
        REQUIRE(sibling.hitAt({0, 0}));
        state->scroll_input.remove(sibling);
        auto& range = *state->stats_range;
        state->effect_list = {{false, false, true, false, 0, 0, 0, 3, 10},
                              {false, false, false, true, 0, 1, 0, 3, 9},
                              {true, false, true, false, 2, 2, 0, 9999, 3},
                              {false, false, true, true, 0, 3, 0, 3, 2}};
        state->effect_filter = W8_CAMP_EFFECT_FILTER_ALL;
        state->effect_scroll = 100;
        FilterCampEffectList();
        REQUIRE((state->effect_visible_lines == 24 && state->effect_scroll == 7));
        REQUIRE((state->effect_first_visible == 0 && state->effect_last_visible == 3));
        REQUIRE((range.m_enabled && range.m_value == 7));
        state->effect_filter = W8_CAMP_EFFECT_FILTER_BENEFICIAL;
        FilterCampEffectList();
        REQUIRE((state->effect_visible_lines == 14 && state->effect_scroll == 0));
        REQUIRE((!range.m_enabled && !state->effect_list[1].visible));
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
        REQUIRE((state->effect_visible_lines == 64 && range.m_enabled));
        character = {};
        RebuildCampEffectList();
        REQUIRE((state->effect_list.empty() && state->effect_visible_lines == 0));
        REQUIRE((state->effect_beneficial_count == 0 && !range.m_enabled));
        g_review_character = nullptr;
    }
}

std::uint64_t next_mouse_timestamp = 1;
InputEvent mouse_atom(InputEventType event, std::uint16_t x, std::uint16_t y, bool left = false,
                      bool right = false, std::uint8_t click_count = 0)
{
    InputEvent input{};
    input.timestamp = next_mouse_timestamp++;
    input.type = event;
    input.position = {x, y};
    input.buttons = (left ? SDL_BUTTON_LMASK : 0) | (right ? SDL_BUTTON_RMASK : 0);
    input.clicks = click_count;
    return input;
}
void dispatch_mouse(InputEventType event, std::uint16_t x, std::uint16_t y, bool left = false,
                    bool right = false, std::uint8_t click_count = 0)
{
    const auto input = mouse_atom(event, x, y, left, right, click_count);
    if (event == InputEventType::MouseMove) UpdateUiPointer(x, y);
    DispatchUiInput(&input);
}

// Each test takes input as a fresh owner, so layers left by earlier tests stay inactive.
struct UiStateSnapshot {
    int help_delay = g_ui_help_delay;
    bool force = g_ui_help_forced;
    int owner = UiInputOwner();
    UiStateSnapshot()
    {
        static int next_owner = 1000;
        SelectUiInputOwner(next_owner++);
    }
    ~UiStateSnapshot()
    {
        ClearUiHover();
        ReleaseUiCapture();
        SelectUiInputOwner(owner);
        g_ui_help_delay = help_delay;
        g_ui_help_forced = force;
    }
};

void image_and_sprite_ownership()
{
    SpriteSheet image;
    image.palette[1] = {255, 0, 0, 0};
    image.colors = Create16BPPPalette(image.palette);
    image.frames.resize(1);
    image.frames[0].indices = {1};
    image.frames[0].width = image.frames[0].height = 1;

    std::array<std::unique_ptr<SpriteSheet>, 64> objects{};
    for (unsigned i = 0; i < objects.size(); ++i) {
        objects[i] = std::make_unique<SpriteSheet>(image);
        REQUIRE(objects[i]);
        REQUIRE((objects[i]->frames.size() == 1 && objects[i]->frames[0].indices.size() == 1));
        REQUIRE(objects[i]->frames[0].indices.data() != image.frames[0].indices.data());
        REQUIRE(objects[i]->palette.data() != image.palette.data());
        REQUIRE(objects[i]->colors.data() != image.colors.data());
    }
    image.frames[0].indices[0] = 2;
    image.frames[0].width = 2;
    REQUIRE((objects[0]->frames[0].paletteIndexAt(0, 0) == 1 && objects[0]->frames[0].width == 1));
    const auto* owned_pixels = image.frames[0].indices.data();
    SpriteSheet moved(std::move(image));
    REQUIRE(moved.frames[0].indices.data() == owned_pixels);
    REQUIRE(moved.frames[0].paletteIndexAt(0, 0) == 2);
    image = {};
    REQUIRE(image.frames.empty());
    for (unsigned index : {0u, 31u, 63u}) {
        objects[index].reset();
    }
    auto& surviving = *objects[32];
    REQUIRE((surviving.frames[0].paletteIndexAt(0, 0) == 1 && surviving.frames[0].width == 1));
    std::vector<SDLSurfaceOwner> surfaces;
    surfaces.reserve(64);
    for (unsigned index = 0; index < 64; ++index) {
        surfaces.push_back(CreateCpuStorage(8, 8, SDL_PIXELFORMAT_RGB565));
        REQUIRE(surfaces.back());
    }
    auto& surviving_surface = *surfaces[32];
    for (unsigned index : {0u, 31u, 63u}) surfaces[index].reset();
    REQUIRE(surviving_surface.w == 8);
}

void font_resource_ownership()
{
    initialize_test_framebuffer();
    for (unsigned repeat = 0; repeat < 32; ++repeat) {
        auto translation = CreateEnglishTransTable();
        REQUIRE(translation.size() == 252);
        ConfigureFontRendering(translation);
        translation[1] = '?';
        REQUIRE(GetIndex('B') == 1);
        translation = {};
        REQUIRE(GetIndex('B') == 1);
        ConfigureFontRendering(CreateEnglishTransTable());
        REQUIRE(GetIndex('Z') == 25);
        ResetFontRendering();
        ResetFontRendering();
    }
}

void text_input_ownership()
{
    Font font{};
    for (unsigned repeat = 0; repeat < 32; ++repeat) {
        UiLayer outer_input;
        TextFieldGroup outer(outer_input, font);
        const auto first = outer.Add(0, 0, 80, 20, UI_PRIORITY_NORMAL, "alpha", 32, 0, false);
        const auto middle = outer.Add(0, 20, 80, 20, UI_PRIORITY_NORMAL, "beta", 32, 0, false);
        const auto last = outer.Add(0, 40, 80, 20, UI_PRIORITY_NORMAL, "gamma", 32, 0, false);
        REQUIRE((first == 0 && middle == 1 && last == 2));
        REQUIRE((outer.Text(first)[0] == 'a' && outer.Text(first).size() == 5));
        if (repeat == 0) {
            const auto* target = UiTargetAt({5, 5});
            REQUIRE(target != nullptr);
            dispatch_mouse(InputEventType::LeftButtonDown, 80, 10);
            REQUIRE(g_ui.captured == target); // Inclusive retail text-field edge.
            dispatch_mouse(InputEventType::LeftButtonUp, 100, 100);
            REQUIRE(g_ui.captured == nullptr);
            UiLayer panel(UiLayerOrder::Combat);
            panel.add(SDL_Rect{0, 0, 640, 480}, {});
            dispatch_mouse(InputEventType::LeftButtonDown, 5, 5);
            REQUIRE(g_ui.captured == target); // Editor precedes its hosting plate.
            dispatch_mouse(InputEventType::LeftButtonUp, 100, 100);
        }

        outer.Remove(middle);
        REQUIRE(outer.Text(last).size() == 5);
        outer.Focus(last);
        {
            UiLayer inner_input;
            TextFieldGroup inner(inner_input, font);
            REQUIRE(TextFieldGroup::Focused() == &inner);
            REQUIRE(inner.Add(0, 0, 80, 20, UI_PRIORITY_NORMAL, "inner", 32, 0, false) == 0);
            // An older group keeps its focus but no longer accepts typing.
            const InputEvent letter{.type = InputEventType::Text, .text = U'x'};
            REQUIRE_FALSE(outer.HandleInput(letter));
        }
        REQUIRE(TextFieldGroup::Focused() == &outer);
        REQUIRE((outer.FocusedId() == last && outer.Editing()));
        REQUIRE((outer.Text(first)[0] == 'a' && outer.Text(last).size() == 5));
        REQUIRE(outer.Add(0, 60, 80, 20, UI_PRIORITY_NORMAL, "delta", 32, 0, false) == 3);
        outer.Remove(first);
        outer.Remove(3);
        outer.Remove(last);
        REQUIRE(outer.FocusedId() == -1);
    }
    REQUIRE(TextFieldGroup::Focused() == nullptr);
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

void button_image_fixture(const std::filesystem::path& root, std::string name, unsigned count,
                          bool app_data, unsigned width, unsigned height,
                          std::array<std::uint8_t, 5> frame_palette_indices, short offset_x,
                          short offset_y)
{
    REQUIRE((width > 0 && width < 128 && height > 0));
    std::replace(name.begin(), name.end(), '\\', '/');
    const auto path = root / wiz8::normalize_asset_path(name);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    const auto write = [&file](const void* data, size_t size) {
        file.write(static_cast<const char*>(data), size);
        REQUIRE(file.good());
    };
    STCIHeader header{};
    memcpy(header.cID, STCI_ID_STRING, STCI_ID_LEN);
    const unsigned image_size = (width + 2) * height;
    header.uiOriginalSize = count * width * height;
    header.uiStoredSize = count * image_size;
    header.fFlags = STCI_INDEXED | STCI_ETRLE_COMPRESSED;
    header.usWidth = width;
    header.usHeight = height;
    header.Indexed.uiNumberOfColours = 256;
    header.Indexed.usNumberOfSubImages = count;
    header.ubDepth = 8;
    header.uiAppDataSize = app_data ? 4 : 0;
    write(&header, STCI_HEADER_SIZE);
    std::array<STCIPaletteElement, 256> palette{};
    if (frame_palette_indices[0]) {
        const std::array<SDL_Color, 5> colors{{
            {255, 0, 0, SDL_ALPHA_OPAQUE},
            {0, 255, 0, SDL_ALPHA_OPAQUE},
            {0, 0, 255, SDL_ALPHA_OPAQUE},
            {255, 255, 255, SDL_ALPHA_OPAQUE},
            {255, 255, 0, SDL_ALPHA_OPAQUE},
        }};
        for (std::size_t index = 0; index < colors.size(); ++index) {
            auto& entry = palette[frame_palette_indices[index]];
            entry.ubRed = colors[index].r;
            entry.ubGreen = colors[index].g;
            entry.ubBlue = colors[index].b;
        }
    } else {
        palette[1].ubRed = 255;
    }
    write(palette.data(), STCI_8BIT_PALETTE_SIZE);
    std::vector<STCISubImage> regions(count);
    for (unsigned index = 0; index < count; ++index) {
        regions[index].uiDataOffset = index * image_size;
        regions[index].uiDataLength = image_size;
        regions[index].sOffsetX = offset_x;
        regions[index].sOffsetY = offset_y;
        regions[index].usWidth = width;
        regions[index].usHeight = height;
    }
    write(regions.data(), regions.size() * sizeof(STCISubImage));
    std::vector<std::uint8_t> pixels(width + 2, 1);
    pixels.front() = width;
    pixels.back() = 0;
    for (unsigned index = 0; index < count; ++index) {
        if (frame_palette_indices[0])
            pixels[1] = frame_palette_indices[index % frame_palette_indices.size()];
        for (unsigned row = 0; row < height; ++row) write(pixels.data(), pixels.size());
    }
    if (app_data) {
        const std::uint8_t bytes[]{7, 8, 9, 10};
        write(bytes, sizeof(bytes));
    }
}

void bitmap_image_fixture(const std::filesystem::path& root, std::string name)
{
    std::replace(name.begin(), name.end(), '\\', '/');
    const auto path = root / wiz8::normalize_asset_path(name);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    const auto write = [&file](const void* data, size_t size) {
        file.write(static_cast<const char*>(data), size);
        REQUIRE(file.good());
    };
    STCIHeader header{};
    memcpy(header.cID, STCI_ID_STRING, STCI_ID_LEN);
    header.uiOriginalSize = 2;
    header.uiStoredSize = 2;
    header.fFlags = STCI_INDEXED;
    header.usWidth = 2;
    header.usHeight = 1;
    header.Indexed.uiNumberOfColours = 256;
    header.ubDepth = 8;
    write(&header, STCI_HEADER_SIZE);
    std::array<STCIPaletteElement, 256> palette{};
    palette[1].ubRed = 255;
    write(palette.data(), STCI_8BIT_PALETTE_SIZE);
    const std::uint8_t pixels[]{0, 1};
    write(pixels, sizeof(pixels));
}

std::uint16_t surface_pixel(const SDL_Surface& surface, int x, int y)
{
    const auto* row = reinterpret_cast<const std::uint16_t*>(
        static_cast<const std::uint8_t*>(surface.pixels) + y * surface.pitch);
    return row[x];
}

class CatalogArtworkAssets {
public:
    CatalogArtworkAssets() : previous_roots_(w8_native::path_roots())
    {
        const auto assets = directory_.path / "assets";
        button_image_fixture(assets, "data/main interface/main_scroll.sti", 12, false, 4, 2, {}, 2,
                             3);
        bitmap_image_fixture(assets, "data/portraits/large/lhummf.sti");
        g_image_assets.clear();
        w8_native::configure_paths({assets.string(), (directory_.path / "user").string()});
    }

    ~CatalogArtworkAssets()
    {
        g_image_assets.clear();
        w8_native::configure_paths(previous_roots_);
    }

private:
    ScopedTemporaryDirectory directory_{"wiz8-catalog-artwork"};
    w8_native::Roots previous_roots_;
};

void generic_button_images()
{
    initialize_test_framebuffer();
    UiStateSnapshot saved_regions;
    ScopedTemporaryDirectory temporary{"wiz8-button-images"};
    const auto& root = temporary.path;
    const auto assets = root / "assets";
    button_image_fixture(assets, std::string(DEFAULT_GENERIC_BUTTON_OFF));
    button_image_fixture(assets, "button.sti", 9, false, 1, 1, {1, 2, 3, 4, 5});
    button_image_fixture(assets, "short.sti", 8);
    g_image_assets.clear();
    ScopedAssetRoots scoped_roots{{assets.string(), (root / "user").string()}};

    button_image_fixture(assets, "metadata.sti", 3, true);
    {
        auto image = LoadImage("metadata.sti");
        REQUIRE(image);
        const auto* sheet = std::get_if<SpriteSheet>(&*image);
        REQUIRE(sheet);
        REQUIRE(sheet->frames.size() == 3);
        REQUIRE(sheet->frames[2].paletteIndexAt(0, 0) == 1);
        REQUIRE(sheet->palette[1].r == 255);

        const auto pixel_offset =
            STCI_HEADER_SIZE + STCI_8BIT_PALETTE_SIZE + 3 * STCI_SUBIMAGE_SIZE;
        for (std::uintmax_t cut :
             {std::size_t{0}, STCI_HEADER_SIZE - 1u, STCI_HEADER_SIZE + STCI_8BIT_PALETTE_SIZE - 1u,
              pixel_offset - 1u, pixel_offset + 8u, pixel_offset + 12u}) {
            button_image_fixture(assets, "metadata.sti", 3, true);
            std::filesystem::resize_file(assets / "metadata.sti", cut);
            REQUIRE(!LoadImage("metadata.sti"));
        }
    }
    std::string long_image_path;
    for (int i = 0; i < 8; ++i) long_image_path += "long-component-for-a-native-image-path/";
    long_image_path += "button.sti";
    REQUIRE(long_image_path.size() > 260);
    button_image_fixture(assets, long_image_path, 3);
    {
        auto sprite = g_image_assets.sprites(long_image_path);
        REQUIRE((sprite && sprite->frames.size() == 3));
        REQUIRE((sprite->frames[2].width == 1 && sprite->frames[2].paletteIndexAt(0, 0) == 1));
        REQUIRE(sprite->palette[1].r == 255);
    }
    for (bool corrupt_length : {false, true}) {
        button_image_fixture(assets, "bad-extent.sti", 3);
        {
            std::fstream file(assets / "bad-extent.sti",
                              std::ios::in | std::ios::out | std::ios::binary);
            STCISubImage region{};
            region.uiDataOffset = corrupt_length ? 1 : 0xffffffffu;
            region.uiDataLength = corrupt_length ? 9 : 3;
            region.usWidth = region.usHeight = 1;
            file.seekp(STCI_HEADER_SIZE + STCI_8BIT_PALETTE_SIZE);
            file.write(reinterpret_cast<const char*>(&region), STCI_SUBIMAGE_SIZE);
            REQUIRE(file.good());
        }
        REQUIRE(!LoadImage("bad-extent.sti"));
    }
    for (bool corrupt_app_data : {false, true}) {
        button_image_fixture(assets, "bad-size.sti", 3, true);
        {
            std::fstream file(assets / "bad-size.sti",
                              std::ios::in | std::ios::out | std::ios::binary);
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
        REQUIRE(!LoadImage("bad-size.sti"));
    }
    // Appearance instances retain their graphics independently of the loader
    // and image manager.
    auto graphics = ButtonGraphics::Load("button.sti", 0, 1, 2, 3, 4);
    REQUIRE(graphics);
    REQUIRE((graphics->MaxWidth == 1 && graphics->MaxHeight == 1));
    std::weak_ptr<const ButtonGraphics> lifetime = graphics;
    ButtonAppearance first_appearance;
    ButtonAppearance second_appearance;
    first_appearance.quick_image = second_appearance.quick_image = graphics;
    const auto& frame_colors = graphics->object().colors;
    auto target_owner = CreateCpuStorage(8, 8, SDL_PIXELFORMAT_RGB565);
    REQUIRE(target_owner);
    auto& target = *target_owner;
    for (bool enabled : {false, true}) {
        for (bool pressed : {false, true}) {
            for (bool hover : {false, true}) {
                DrawButtonAppearance(target, first_appearance, {0, 0, 8, 8},
                                     {enabled, pressed, hover});
                const auto* pixels = static_cast<const std::uint16_t*>(target.pixels);
                const unsigned frame = !enabled ? 0 : pressed ? (hover ? 4 : 3) : (hover ? 2 : 1);
                REQUIRE(pixels[0] == frame_colors[frame + 1]);
            }
        }
    }
    // Menu artwork preserves blank states and its enlarged-hover positioning.
    auto menu_sprites = std::make_shared<SpriteSheet>(graphics->object());
    menu_sprites->frames[2].width = 3;
    menu_sprites->frames[2].indices.assign(3, 3);
    ButtonAppearance menu{.quick_image = ButtonGraphics::Load(menu_sprites, 0, 1, 2, -1, -1)};
    SDL_FillSurfaceRect(&target, nullptr, 0);
    DrawButtonAppearance(target, menu, {1, 1, 1, 1}, {.hovered = true, .menu_artwork = true});
    const auto* menu_pixels = static_cast<const std::uint16_t*>(target.pixels);
    REQUIRE(menu_pixels[target.pitch / 2 + 1] == 0);
    REQUIRE(menu_pixels[target.pitch / 2 + 2] == frame_colors[3]);
    SDL_FillSurfaceRect(&target, nullptr, 0);
    DrawButtonAppearance(target, menu, {1, 1, 1, 1}, {.pressed = true, .menu_artwork = true});
    REQUIRE(menu_pixels[target.pitch / 2 + 1] == 0);
    // Ordinary buttons paint to the supplied canvas, including forced repaints.
    {
        UiLayer layer;
        W8ButtonWidget button(layer);
        REQUIRE(button.Configure(
            ButtonGraphics::Load("button.sti", 0, 1, BUTTON_NO_IMAGE, 1, BUTTON_NO_IMAGE),
            {.priority = UI_PRIORITY_NORMAL}));
        button.SetPosition(2, 3);
        SDL_FillSurfaceRect(&target, nullptr, 0);
        button.Draw(target);
        auto* pixels = static_cast<std::uint16_t*>(target.pixels);
        REQUIRE(pixels[3 * (target.pitch / 2) + 2] == frame_colors[2]);
        SDL_FillSurfaceRect(&target, nullptr, 0);
        button.m_dirty = true;
        button.Draw(target);
        REQUIRE(pixels[3 * (target.pitch / 2) + 2] == frame_colors[2]);
    }
    // Painting auxiliary canvases cannot commit the main framebuffer or clear
    // screen dirtiness; clean buttons must also repaint into a fresh canvas.
    {
        UiLayer layer;
        W8ButtonWidget button(layer);
        REQUIRE(button.Configure(
            ButtonGraphics::Load("button.sti", 0, 1, BUTTON_NO_IMAGE, 1, BUTTON_NO_IMAGE),
            {.priority = UI_PRIORITY_NORMAL}));
        button.SetPosition(2, 3);
        const auto previous_upload = TakeUiUpload();
        InvalidateRegion(4, 4, 5, 5);
        SDL_FillSurfaceRect(&target, nullptr, 0);
        button.Draw(target);
        REQUIRE(button.m_dirty);
        const auto upload = TakeUiUpload();
        REQUIRE((upload.x == 4 && upload.y == 4 && upload.w == 1 && upload.h == 1));
        auto* pixels = static_cast<std::uint16_t*>(target.pixels);
        REQUIRE(pixels[3 * (target.pitch / 2) + 2] == frame_colors[2]);
        SDL_FillSurfaceRect(&target, nullptr, 0);
        button.m_dirty = false;
        button.Draw(target);
        REQUIRE_FALSE(button.m_dirty);
        REQUIRE(TakeUiUpload().w == 0);
        REQUIRE(pixels[3 * (target.pitch / 2) + 2] == frame_colors[2]);
        // Auxiliary painting leaves a pending screen update intact.
        button.m_dirty = true;
        button.Draw(target);
        REQUIRE(button.m_dirty);
        REQUIRE(TakeUiUpload().w == 0);
        auto& framebuffer = GetFrameBufferSurface();
        button.Refresh();
        REQUIRE_FALSE(button.m_dirty);
        const auto committed = TakeUiUpload();
        REQUIRE(SDL_RectsEqual(&committed, &button.Bounds()));
        REQUIRE(surface_pixel(framebuffer, 2, 3) == frame_colors[2]);
        REQUIRE(SDL_FillSurfaceRect(&framebuffer, nullptr, 0));
        button.Refresh();
        REQUIRE(surface_pixel(framebuffer, 2, 3) == 0);
        REQUIRE(TakeUiUpload().w == 0);
        button.Refresh(true);
        REQUIRE(surface_pixel(framebuffer, 2, 3) == frame_colors[2]);
        REQUIRE(TakeUiUpload().w == button.Bounds().w);
        button.SetVisible(false);
        button.Refresh();
        REQUIRE_FALSE(button.m_dirty);
        REQUIRE(TakeUiUpload().w == 0);
        if (previous_upload.w > 0) {
            InvalidateRegion(previous_upload);
        }
    }
    target_owner.reset();
    graphics.reset();
    ShutdownButtonImageManager();
    REQUIRE(!lifetime.expired());
    first_appearance.quick_image.reset();
    REQUIRE(!lifetime.expired());
    second_appearance.quick_image.reset();
    REQUIRE(lifetime.expired());

    // Validate each of the five states, including invalid negative indices.
    for (int bad : {-2, 9, 65536}) {
        for (size_t state = 0; state < 5; ++state) {
            std::array<int, 5> frames{0, 1, 2, 3, 4};
            frames[state] = bad;
            REQUIRE(!ButtonGraphics::Load("button.sti", frames[0], frames[1], frames[2], frames[3],
                                          frames[4]));
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
    const auto widget_count = LiveUiTargetCount();
    UiLayer buttons;
    {
        auto dialog = std::make_unique<W8ButtonWidget>(buttons);
        REQUIRE(dialog->Configure(
            ButtonGraphics::Load("button.sti", 0, 1, BUTTON_NO_IMAGE, 1, BUTTON_NO_IMAGE),
            {.priority = UI_PRIORITY_NORMAL}));
        dialog->SetPosition(12, 13);
        REQUIRE(!dialog->Configure(
            ButtonGraphics::Load("missing.sti", 0, 1, BUTTON_NO_IMAGE, 1, BUTTON_NO_IMAGE),
            {.priority = UI_PRIORITY_NORMAL}));
        REQUIRE((dialog->Bounds().x == 12 && dialog->Bounds().y == 13 && dialog->Bounds().w == 1));
        REQUIRE(UiTargetAt({12, 13}) != nullptr);
        REQUIRE(LiveUiTargetCount() == widget_count + 1);
    }
    REQUIRE(LiveUiTargetCount() == widget_count);
    // A dialog's click callback can close it. The dispatcher must not read
    // the released dialog while completing the click (including its sound).
    auto closing_dialog = std::make_unique<W8ButtonWidget>(buttons);
    REQUIRE(closing_dialog->Configure(
        ButtonGraphics::Load("button.sti", 0, 1, 2, 3, 4),
        {.on_click = [&](W8ButtonWidget*) { AfterUiDispatch([&] { closing_dialog.reset(); }); },
         .priority = UI_PRIORITY_NORMAL}));
    closing_dialog->SetPosition(12, 13);

    {
        dispatch_mouse(InputEventType::LeftButtonDown, 12, 13, true, false, 1);
        dispatch_mouse(InputEventType::LeftButtonUp, 12, 13, false, false, 1);
    }
    REQUIRE_FALSE(closing_dialog);

    REQUIRE(!InitializeButtonImageManager());
    button_image_fixture(assets, std::string(DEFAULT_GENERIC_BUTTON_ON));
    REQUIRE(InitializeButtonImageManager());
    const std::string_view filename = "button.sti", missing = "missing.sti";
    const auto load = [](const std::array<std::string_view, 6>& names) {
        return LoadGenericButtonImages(names[2], names[0], names[3], names[1], names[4], names[5],
                                       3, 4, 5);
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
        std::fstream file(assets / "zero-tile.sti",
                          std::ios::in | std::ios::out | std::ios::binary);
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
    const auto background_pixel = image->frames[0]->frames[8].paletteIndexAt(0, 0);
    REQUIRE(background_pixel);
    REQUIRE(image->fill_color == image->frames[0]->colors[*background_pixel]);
    auto another_owner = image;
    image.reset();
    REQUIRE(another_owner->frames[0]);
    another_owner.reset();

    // There is no fixed image slot limit. Lifetime is owned by the image.
    std::vector<GenericButtonImage> images;
    for (int index = 0; index < 45; ++index)
        images.push_back(load({filename, filename, {}, {}, {}, {}}));
    REQUIRE(std::all_of(images.begin(), images.end(),
                        [](const auto& item) { return item != nullptr; }));
    images.clear();

    for (unsigned repeat = 0; repeat < 32; ++repeat) {
        ConfigureFontRendering(CreateEnglishTransTable());
        Font primary;
        REQUIRE(primary.load(filename));
        auto& font = primary.glyphs;
        REQUIRE((font->frames.size() == 9));
        Palette16 colours{};
        colours[1] = 0x1234;
        primary.setPalette(colours);
        colours[1] = 0;
        REQUIRE(primary.palette()[1] == 0x1234);
        REQUIRE(primary.createShades(FONT_GLOW_GREEN));
        REQUIRE(primary.setShade(15));
        REQUIRE(primary.currentPalette()[1] != primary.palette()[1]);
        REQUIRE(primary.setShade(Font::kNormalShade));
        REQUIRE(primary.currentPalette()[1] == 0x1234);
        Font secondary;
        REQUIRE(secondary.load(filename));
        REQUIRE(secondary.createShades(FONT_GLOW_RED));
        REQUIRE(secondary.shades[0] != primary.shades[0]);
        primary.clear();
        ResetFontRendering();
    }
    // Font lifetime is independent of rendering setup and has no slot limit.
    std::vector<Font> many_fonts;
    for (unsigned index = 0; index < 32; ++index) {
        many_fonts.emplace_back();
        REQUIRE(many_fonts.back().load(filename));
    }
    ResetFontRendering();
    REQUIRE(many_fonts.back().height() > 0);
    many_fonts.clear();
    // Failed loads retain resources; moves preserve their ownership.
    // Font's move operations explicitly leave the source empty, so checking its
    // moved-from state is part of the contract rather than an accidental use.
    {
        Font source, moved;
        REQUIRE(source.height() == 0);
        REQUIRE(source.textWidth("empty") == 0);
        REQUIRE(source.load(filename));
        const auto height = source.height();
        REQUIRE_FALSE(source.load("missing-font.sti"));
        REQUIRE(source.height() == height);
        Font temporary(std::move(source));
        REQUIRE(temporary.height() == height);
        REQUIRE(source.height() == 0); // NOLINT(bugprone-use-after-move)
        moved = std::move(temporary);
        REQUIRE(moved.height() == height);
        REQUIRE(temporary.height() == 0); // NOLINT(bugprone-use-after-move)
        REQUIRE(moved.load(filename));
        source = std::move(moved);
        REQUIRE(source.height() == height);
        REQUIRE(moved.height() == 0); // NOLINT(bugprone-use-after-move)
    }
    ConfigureFontRendering(
        std::vector<std::uint16_t>{'?', 0x00e9, 0x4e2d, 'e', 0x0301, 'a', 'b', ' ', 'c'});
    Font primary;
    REQUIRE(primary.load(filename));
    REQUIRE(primary.textWidth("é") > 0);
    REQUIRE(primary.codepointPrefixWidth("é中", 1) == primary.textWidth("é"));
    REQUIRE(primary.codepointPrefixWidth("é中", 2) == primary.textWidth("é中"));
    // Render borrowed substrings without depending on a trailing NUL or
    // accidentally consuming the next glyph in the backing string.
    auto expected_surface = CreateCpuStorage(64, 32, SDL_PIXELFORMAT_RGB565);
    auto view_surface = CreateCpuStorage(64, 32, SDL_PIXELFORMAT_RGB565);
    REQUIRE(expected_surface);
    REQUIRE(view_surface);
    REQUIRE(SDL_FillSurfaceRect(expected_surface.get(), nullptr, 0));
    REQUIRE(SDL_FillSurfaceRect(view_surface.get(), nullptr, 0));
    DrawText(*expected_surface, primary, {0, 0}, "é");
    const std::string backing = "é中";
    DrawText(*view_surface, primary, {0, 0}, std::string_view(backing).substr(0, 2));
    const auto surface_pixels = [](const SDL_Surface& surface) {
        std::vector<std::uint16_t> pixels(static_cast<std::size_t>(surface.w * surface.h));
        const auto* bytes = static_cast<const std::uint8_t*>(surface.pixels);
        for (int y = 0; y < surface.h; ++y) {
            const auto* row = reinterpret_cast<const std::uint16_t*>(bytes + y * surface.pitch);
            std::copy_n(row, surface.w, pixels.begin() + y * surface.w);
        }
        return pixels;
    };
    auto expected_pixels = surface_pixels(*expected_surface);
    auto view_pixels = surface_pixels(*view_surface);
    REQUIRE(view_pixels == expected_pixels);
    REQUIRE(
        std::any_of(view_pixels.begin(), view_pixels.end(), [](auto pixel) { return pixel != 0; }));
    auto clipped_surface = CreateCpuStorage(64, 32, SDL_PIXELFORMAT_RGB565);
    REQUIRE(clipped_surface);
    REQUIRE(SDL_FillSurfaceRect(clipped_surface.get(), nullptr, 0));
    const auto first_drawn = std::find_if(expected_pixels.begin(), expected_pixels.end(),
                                          [](auto pixel) { return pixel != 0; });
    REQUIRE(first_drawn != expected_pixels.end());
    const int first_drawn_x = static_cast<int>(first_drawn - expected_pixels.begin()) % 64;
    const SDL_Rect text_clip{first_drawn_x + 1, 0, 64 - first_drawn_x - 1, 32};
    {
        ScopedClipRect clipping(*clipped_surface, text_clip);
        DrawText(*clipped_surface, primary, {0, 0}, "é");
    }
    const auto clipped_pixels = surface_pixels(*clipped_surface);
    REQUIRE(clipped_pixels[first_drawn - expected_pixels.begin()] == 0);
    for (int y = 0; y < 32; ++y) {
        for (int x = text_clip.x; x < 64; ++x) {
            REQUIRE(clipped_pixels[static_cast<std::size_t>(y * 64 + x)] ==
                    expected_pixels[static_cast<std::size_t>(y * 64 + x)]);
        }
    }

    auto wrapped_surface = CreateCpuStorage(64, 32, SDL_PIXELFORMAT_RGB565);
    REQUIRE(wrapped_surface);
    REQUIRE(primary.height() < 32);
    REQUIRE(SDL_FillSurfaceRect(wrapped_surface.get(), nullptr, 0));
    const int glyph_width = primary.textWidth("é");
    const SDL_Rect wrap_clip{0, 0, glyph_width, 32};
    {
        ScopedClipRect clipping(*wrapped_surface, wrap_clip);
        DrawText(*wrapped_surface, primary, {0, 0}, "éé", {.wrap = true});
    }
    const auto wrapped_pixels = surface_pixels(*wrapped_surface);
    REQUIRE(std::any_of(wrapped_pixels.begin() + primary.height() * 64, wrapped_pixels.end(),
                        [](auto pixel) { return pixel != 0; }));

    auto mono_surface = CreateCpuStorage(64, 32, SDL_PIXELFORMAT_RGB565);
    REQUIRE(mono_surface);
    REQUIRE(SDL_FillSurfaceRect(mono_surface.get(), nullptr, 0));
    DrawText(*mono_surface, primary, {0, 0}, "é",
             {.mono = true, .colors = {.foreground = 0xf81f, .background = 0, .shadow = 0x07e0}});
    const auto mono_pixels = surface_pixels(*mono_surface);
    REQUIRE(std::find(mono_pixels.begin(), mono_pixels.end(), 0xf81f) != mono_pixels.end());

    initialize_test_framebuffer();
    ConfigureFontRendering(CreateEnglishTransTable());
    TakeUiUpload();
    const SDL_Point invalidation_at{16, 16};
    const int invalidated_width = primary.textWidth("A");
    const int invalidated_height = primary.height();
    REQUIRE(invalidated_width > 0);
    REQUIRE(invalidated_height > 0);
    DrawTextAndInvalidate(GetFrameBufferSurface(), primary, invalidation_at, "A");
    const SDL_Rect drawn{invalidation_at.x, invalidation_at.y, invalidated_width,
                         invalidated_height};
    const auto upload = TakeUiUpload();
    SDL_Rect covered;
    REQUIRE(SDL_GetRectIntersection(&upload, &drawn, &covered));
    REQUIRE(SDL_RectsEqual(&covered, &drawn));

    // Explicit-buffer rendering must use its font argument, even when the
    // current surface font has a different palette.
    Font secondary;
    REQUIRE(secondary.load(filename));
    Palette16 alternate;
    alternate.fill(0x1234);
    secondary.setPalette(alternate);
    REQUIRE(SDL_FillSurfaceRect(expected_surface.get(), nullptr, 0));
    REQUIRE(SDL_FillSurfaceRect(view_surface.get(), nullptr, 0));
    DrawText(*expected_surface, secondary, {0, 0}, "é中");
    DrawText(*view_surface, secondary, {0, 0}, "é中");
    expected_pixels = surface_pixels(*expected_surface);
    view_pixels = surface_pixels(*view_surface);
    REQUIRE(view_pixels == expected_pixels);
    // Rebuilding word ranges preserves UTF-8 byte boundaries and discards
    // old hover state, including when a line becomes empty.
    auto previous_level = std::exchange(g_level_block, std::make_unique<W8LevelRuntimeBlock>());
    g_level_block->text_box_font = &primary;
    W8MessageStorageRecord words{};
    words.text = "  é 中  ";
    words.RebuildEntries();
    REQUIRE(words.entries.size() == 2);
    REQUIRE((words.entries[0].start == 2 && words.entries[0].end == 3));
    REQUIRE((words.entries[1].start == 5 && words.entries[1].end == 7));
    REQUIRE(words.entries[1].x_start == primary.textWidth("  é "));
    words.entries[0].keyword = W8_NOTICE_WORD_SELECTED;
    words.text = "中";
    words.RebuildEntries();
    REQUIRE((words.entries.size() == 1 && words.entries[0].keyword == W8_NOTICE_WORD_NORMAL));
    words.text.clear();
    words.RebuildEntries();
    REQUIRE(words.entries.empty());
    g_level_block = std::move(previous_level);
    REQUIRE(g_font12point1.load(filename));
    {
        UiLayer fields_input;
        TextFieldGroup fields(fields_input, g_font12point1);
        const auto field = fields.Add(0, 0, 80, 20, UI_PRIORITY_NORMAL, "", 1000, 0x0f, false);
        fields.Focus(field);
        const std::string clusters = "é👩‍👩‍👧‍👦中";
        fields.SetText(field, clusters);
        fields.SetCursor(clusters.size());
        InputEvent backspace{};
        backspace.type = InputEventType::KeyDown;
        backspace.key = 8;
        REQUIRE(fields.HandleInput(backspace));
        REQUIRE(fields.Text(field) == "é👩‍👩‍👧‍👦");
        REQUIRE(fields.HandleInput(backspace));
        REQUIRE(fields.Text(field) == "é");
        REQUIRE(fields.HandleInput(backspace));
        REQUIRE(fields.Text(field).empty());
        // Editing commands never guess text, even when no SDL input window is active.
        SetNativeTextInputEnabled(false);
        REQUIRE(fields.HandleInput(
            InputEvent{.type = InputEventType::KeyDown, .modifiers = SHIFT_DOWN, .key = SDLK_A}));
        REQUIRE(fields.Text(field).empty());
        SetNativeTextInputEnabled(true);
        InputEvent committed{};
        committed.type = InputEventType::Text;
        committed.text = 0x4e2d;
        REQUIRE(fields.HandleInput(committed));
        REQUIRE(fields.Text(field) == "中");
        committed.text = 0x1f600;
        REQUIRE(fields.HandleInput(committed));
        REQUIRE(fields.Text(field) == "中😀");
        fields.SetText(field, "");
        const std::array<char32_t, 7> family = {0x1f469, 0x200d, 0x1f469, 0x200d,
                                                0x1f467, 0x200d, 0x1f466};
        for (const auto scalar : family) {
            committed.text = scalar;
            REQUIRE(fields.HandleInput(committed));
        }
        REQUIRE(fields.Text(field) == "👩‍👩‍👧‍👦");
        REQUIRE(fields.HandleInput(backspace));
        REQUIRE(fields.Text(field).empty());
        std::string long_text;
        for (unsigned n = 0; n < 100; ++n) long_text += "中";
        fields.SetText(field, long_text);
        fields.SetCursor(long_text.size());
        REQUIRE(fields.Cursor() == 300);
        REQUIRE(fields.Text(field).size() == 300);
        REQUIRE(fields.HandleInput(backspace));
        REQUIRE((fields.Cursor() == 297 && fields.Text(field).size() == 297));
    }
    ResetFontRendering();
    g_font12point1.clear();
    ShutdownButtonImageManager();

    g_image_assets.clear();
}
}

TEST_CASE("Screen stack ownership", "[owner]")
{
    screen_stack();
}

TEST_CASE("Camp effect ownership", "[owner]")
{
    camp_effects();
}

TEST_CASE("Input layers preserve backend ordering and shape boundaries", "[owner][ui-layer]")
{
    UiStateSnapshot saved_state;
    std::vector<int> hits;
    const auto record = [&](int id) {
        return [&, id](const UiPointerEvent& event) {
            if (event.action == UiPointerAction::Wheel) hits.push_back(id);
            return UiResult::Handled;
        };
    };
    UiLayer later(UiLayerOrder::KeyboardMenu);
    later.add(SDL_Rect{10, 10, 11, 11}, record(1), 100);
    UiLayer earlier(UiLayerOrder::Combat);
    auto& first = earlier.add(SDL_Rect{10, 10, 11, 11}, record(2), 5);
    auto& tie = earlier.add(SDL_Rect{10, 10, 11, 11}, record(3), 5);
    auto& circle = earlier.add(UiCircle{{15, 15}, 5}, record(4), 6);
    dispatch_mouse(InputEventType::MouseWheel, 20, 15); // Circumference included.
    dispatch_mouse(InputEventType::MouseWheel, 20, 20); // Rectangle's last pixel.
    dispatch_mouse(InputEventType::MouseWheel, 21, 20); // Outside SDL extent.
    REQUIRE(hits == std::vector<int>{4, 2});
    first.setEnabled(false);
    dispatch_mouse(InputEventType::MouseWheel, 20, 20);
    earlier.remove(tie);
    dispatch_mouse(InputEventType::MouseWheel, 20, 20);
    circle.setShape(SDL_Rect{20, 20, 1, 1});
    dispatch_mouse(InputEventType::MouseWheel, 20, 20);
    earlier.setEnabled(false);
    dispatch_mouse(InputEventType::MouseWheel, 20, 20);
    REQUIRE(hits == std::vector<int>{4, 2, 3, 1, 4, 1});

    earlier.setEnabled(true);
    first.setEnabled(true);
    circle.setEnabled(false);
    UiLayer same_order(UiLayerOrder::Combat);
    same_order.add(SDL_Rect{10, 10, 11, 11}, record(5), 1000);
    dispatch_mouse(InputEventType::MouseWheel, 15, 15);
    REQUIRE(hits.back() == 2); // Equal layer orders use creation order.
}

TEST_CASE("Input layers keep capture outside and release it for gameplay drops",
          "[owner][ui-layer]")
{
    UiStateSnapshot saved_state;
    std::vector<UiPointerEvent> events;
    int drops = 0;
    HitTarget* drag_source = nullptr;
    UiLayer layer;
    auto& source = layer.add(SDL_Rect{10, 10, 20, 20}, [&](const UiPointerEvent& event) {
        events.push_back(event);
        if (drag_source && event.action == UiPointerAction::Leave) drag_source->releaseCapture();
        return UiResult::Handled;
    });
    layer.add(SDL_Rect{40, 10, 20, 20}, [&](const UiPointerEvent& event) {
        if (event.action == UiPointerAction::Up) ++drops;
        return UiResult::Handled;
    });
    dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true, false, 2);
    REQUIRE(events.back().clicks == 2);
    REQUIRE(events.back().local.x == 5);
    REQUIRE(events.back().local.y == 5);
    UpdateUiPointer(45, 15);
    REQUIRE(events.back().action == UiPointerAction::Leave);
    dispatch_mouse(InputEventType::LeftButtonUp, 45, 15);
    REQUIRE(events.back().action == UiPointerAction::Up);
    REQUIRE_FALSE(events.back().inside);
    REQUIRE(events.back().captured_buttons == SDL_BUTTON_LMASK);
    REQUIRE(g_ui.captured == 0);
    REQUIRE(drops == 0);

    dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
    drag_source = &source;
    UpdateUiPointer(45, 15);
    REQUIRE(g_ui.captured == 0);
    REQUIRE(source.capturedButtons() == 0);
    dispatch_mouse(InputEventType::LeftButtonUp, 45, 15);
    REQUIRE(drops == 1);
    dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
    source.setEnabled(false);
    REQUIRE(g_ui.captured == 0);
    dispatch_mouse(InputEventType::LeftButtonUp, 45, 15);
    REQUIRE(drops == 2);
}

TEST_CASE("Text fields paint only into their supplied surface", "[owner][ui-layer]")
{
    initialize_test_framebuffer();
    auto& framebuffer = GetFrameBufferSurface();
    SDL_FillSurfaceRect(&framebuffer, nullptr, 0x1234);
    SDLSurfaceOwner before(SDL_DuplicateSurface(&framebuffer), SDL_DestroySurface);
    auto target = CreateCpuStorage(100, 50, SDL_PIXELFORMAT_RGB565);
    REQUIRE(before);
    REQUIRE(target);
    SDL_FillSurfaceRect(target.get(), nullptr, 0xffff);
    Font font{};
    UiLayer fields_input;
    TextFieldGroup fields(fields_input, font);
    const auto field = fields.Add(0, 0, 80, 20, 0, "", 32, 0, false);
    fields.Focus(field);
    fields.ClearFocus();
    fields.RenderAll(*target);
    REQUIRE(*static_cast<std::uint16_t*>(target->pixels) == 0);
    REQUIRE(std::memcmp(framebuffer.pixels, before->pixels, framebuffer.pitch * framebuffer.h) ==
            0);
}

TEST_CASE("Modal layers isolate editors and restore focus after deferred close",
          "[owner][ui-layer]")
{
    UiStateSnapshot saved_state;
    Font font{};
    UiLayer outer_input;
    TextFieldGroup outer(outer_input, font);
    const int outer_field = outer.Add(0, 0, 80, 20, 0, "outer", 32, 0xf, false);
    outer.Focus(outer_field);
    outer.SetCursor(2);
    struct Editor {
        UiLayer input{UiLayerMode::Modal};
        TextFieldGroup fields;
        explicit Editor(Font& font) : fields(input, font)
        {
            fields.Focus(fields.Add(0, 30, 80, 20, 0, "inner", 32, 0xf, false));
        }
    };
    auto editor = std::make_unique<Editor>(font);
    REQUIRE(TextFieldGroup::Focused() == &editor->fields);
    REQUIRE_FALSE(outer.HandleInput(InputEvent{.type = InputEventType::Text, .text = U'x'}));
    REQUIRE(editor->fields.HandleInput(InputEvent{.type = InputEventType::Text, .text = U'é'}));
    REQUIRE(editor->fields.Text(0) == "é");
    {
        Editor nested(font);
        REQUIRE(TextFieldGroup::Focused() == &nested.fields);
        REQUIRE_FALSE(
            editor->fields.HandleInput(InputEvent{.type = InputEventType::Text, .text = U'x'}));
    }
    REQUIRE(TextFieldGroup::Focused() == &editor->fields);
    editor->input.add(SDL_Rect{90, 30, 20, 20}, [&](const UiPointerEvent& event) {
        if (event.action == UiPointerAction::Up) {
            AfterUiDispatch([&] { editor.reset(); });
            REQUIRE(editor != nullptr);
        }
        return UiResult::Handled;
    });
    dispatch_mouse(InputEventType::LeftButtonDown, 95, 35, true);
    dispatch_mouse(InputEventType::LeftButtonUp, 95, 35);
    REQUIRE_FALSE(editor);
    REQUIRE(TextFieldGroup::Focused() == &outer);
    REQUIRE(outer.FocusedId() == outer_field);
    REQUIRE(outer.Cursor() == 2);
    REQUIRE(outer.Text(outer_field) == "outer");
    REQUIRE(g_ui.captured == 0);
}

TEST_CASE("Modal layers block underlying input and cancel a held press", "[owner][ui-layer]")
{
    UiStateSnapshot saved_state;
    int releases = 0;
    UiLayer screen;
    auto& target = screen.add(SDL_Rect{10, 10, 20, 20}, [&](const UiPointerEvent& event) {
        if (event.action == UiPointerAction::Up && event.captured_buttons) ++releases;
        return UiResult::Handled;
    });
    dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
    REQUIRE(g_ui.captured == &target);
    {
        UiLayer modal(UiLayerMode::Modal);
        dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
        REQUIRE(releases == 0);
        REQUIRE(g_ui.captured == 0);
        {
            UiLayer nested(UiLayerMode::Modal);
            dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
            REQUIRE(g_ui.captured == 0);
        }
    }
    // The press the modal interrupted stays cancelled after it closes.
    REQUIRE(g_ui.captured == 0);
    REQUIRE(g_ui.hover == 0);
    dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
    REQUIRE(releases == 0);
    dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
    dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
    REQUIRE(releases == 1);
}

TEST_CASE("Split fields share editing and restore focus after rejecting excessive amounts",
          "[owner][text-input]")
{
    UiStateSnapshot saved_state;
    Font font{};
    UiLayer outer_input;
    TextFieldGroup outer(outer_input, font);
    outer.Add(0, 0, 80, 20, 0, "name", 20, 2, false);
    outer.Focus(0);
    W8DialogBase dialog;
    W8ControlsRect bounds{10, 10, 110, 30};
    int changed = 0;
    W8DialogNumericInput amount(dialog.InputLayer(), &bounds, 12, font,
                                [&](int value) { changed = value; });
    amount.m_maximum = 200;
    const SDL_Point point{110, 20};
    amount.StartEditing(point);
    // An active numeric editor must receive pointer hits in its dialog layer.
    REQUIRE(UiTargetAt({95, 20}) != nullptr);
    const InputEvent digit{.type = InputEventType::Text, .text = U'3'};
    REQUIRE(amount.HandleInput(&digit));
    REQUIRE(amount.m_value == 123);
    REQUIRE(amount.HandleInput(&digit)); // 1233 is rejected without moving the caret.
    REQUIRE(amount.m_value == 123);
    const InputEvent backspace{.type = InputEventType::KeyDown, .key = SDLK_BACKSPACE};
    REQUIRE(amount.HandleInput(&backspace));
    REQUIRE(amount.m_value == 12);
    const InputEvent letter{.type = InputEventType::Text, .text = U'x'};
    REQUIRE(amount.HandleInput(&letter));
    REQUIRE(amount.m_value == 12);
    const InputEvent escape{.type = InputEventType::KeyDown, .key = SDLK_ESCAPE};
    REQUIRE(amount.HandleInput(&escape));
    REQUIRE(changed == 12);
    REQUIRE_FALSE(amount.HandleInput(&digit));
    REQUIRE(TextFieldGroup::Focused() == &outer);
    REQUIRE(outer.Editing());
}

TEST_CASE("Input-layer owners close only after the outer dispatch completes", "[owner][ui-layer]")
{
    UiStateSnapshot saved_state;
    const auto region_count = LiveUiTargetCount();
    const auto set_count = LiveUiLayerCount();
    auto layer = std::make_unique<UiLayer>();
    bool finished = false;
    layer->add(SDL_Rect{10, 10, 20, 20}, [&](const UiPointerEvent& event) {
        if (event.action == UiPointerAction::Down) {
            AfterUiDispatch([&] {
                REQUIRE(finished);
                layer.reset();
            });
            // Reentrant dispatch must not flush the outer callback's close.
            dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
            REQUIRE(layer != nullptr);
            finished = true;
        }
        return UiResult::Handled;
    });
    dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
    REQUIRE(layer == nullptr);
    REQUIRE(g_ui.hover == 0);
    REQUIRE(g_ui.captured == 0);
    REQUIRE(LiveUiTargetCount() == region_count);
    REQUIRE(LiveUiLayerCount() == set_count);

    layer = std::make_unique<UiLayer>();
    layer->add(SDL_Rect{10, 10, 20, 20}, [&](const UiPointerEvent& event) {
        if (event.action == UiPointerAction::Enter) AfterUiDispatch([&] { layer.reset(); });
        return UiResult::Handled;
    });
    UpdateUiPointer(15, 15);
    REQUIRE(layer == nullptr);
    REQUIRE(g_ui.hover == 0);
}

TEST_CASE("The input pump finishes its policy before committing a layer close", "[owner][ui-layer]")
{
    UiStateSnapshot saved_state;
    while (PollInputEvent()) {
    }
    auto layer = std::make_unique<UiLayer>();
    layer->add(SDL_Rect{10, 10, 20, 20}, [&](const UiPointerEvent& event) {
        if (event.action == UiPointerAction::Down) AfterUiDispatch([&] { layer.reset(); });
        return UiResult::Handled;
    });
    PostInputEvent({.type = InputEventType::LeftButtonDown, .position = {15, 15}});
    PostInputEvent({.type = InputEventType::LeftButtonUp, .position = {15, 15}});
    int consumed = 0;
    UiInputPolicy policy;
    policy.track_hover = false;
    policy.consumed = [&](const InputEvent&) {
        REQUIRE(layer != nullptr);
        ++consumed;
        return InputRoute::Handled;
    };
    REQUIRE(PumpUiInput(policy));
    REQUIRE(consumed == 1);
    REQUIRE(layer == nullptr);
    auto pending = PollInputEvent();
    REQUIRE(pending);
    REQUIRE(pending->type == InputEventType::LeftButtonUp);
    REQUIRE_FALSE(PollInputEvent());
}

TEST_CASE("A handler may remove its own target or layer synchronously", "[owner][ui-layer]")
{
    UiStateSnapshot saved_state;
    auto layer = std::make_unique<UiLayer>();
    HitTarget* target = nullptr;
    int seen = 0;
    int calls = 0;
    target = &layer->add(SDL_Rect{10, 10, 20, 20}, [&, expected = 7](const UiPointerEvent& event) {
        if (event.action == UiPointerAction::Down) {
            layer->remove(*target);
            // The handler's captures stay valid after its own removal.
            seen = expected;
            ++calls;
        }
        return UiResult::Handled;
    });
    dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
    REQUIRE(seen == 7);
    REQUIRE(g_ui.captured == nullptr);
    dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
    REQUIRE(calls == 1);

    layer->add(SDL_Rect{10, 10, 20, 20}, [&, expected = 9](const UiPointerEvent& event) {
        if (event.action == UiPointerAction::Down) {
            layer.reset();
            seen = expected;
        }
        return UiResult::Handled;
    });
    dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
    REQUIRE(seen == 9);
    REQUIRE(layer == nullptr);
}

TEST_CASE("Only non-passive targets capture the pointer on press", "[owner][ui-layer]")
{
    UiStateSnapshot saved_state;
    UiLayer layer;
    auto& gameplay =
        layer.add(SDL_Rect{0, 0, 20, 20}, [](const UiPointerEvent&) { return UiResult::Pass; });
    auto& tooltip =
        layer.add(SDL_Rect{40, 0, 20, 20}, [](const UiPointerEvent&) { return UiResult::Pass; });
    tooltip.setPassive();
    // A gameplay control that passes the press still receives the release.
    dispatch_mouse(InputEventType::LeftButtonDown, 5, 5, true);
    REQUIRE(g_ui.captured == &gameplay);
    dispatch_mouse(InputEventType::LeftButtonUp, 5, 5);
    REQUIRE(g_ui.captured == nullptr);
    dispatch_mouse(InputEventType::LeftButtonDown, 45, 5, true);
    REQUIRE(g_ui.captured == nullptr);
    REQUIRE(tooltip.capturedButtons() == 0);
}

namespace {
struct ChainedModal final : ModalContent {
    ChainedModal(std::unique_ptr<ModalContent> next, bool* destroyed, int* later_use)
        : next(std::move(next)), destroyed(destroyed), later_use(later_use)
    {
    }
    ~ChainedModal() override
    {
        if (destroyed) *destroyed = true;
    }
    bool ProcessInput() override
    {
        if (next) OpenModal(std::move(next));
        // Still executing after opening a replacement: members must be alive.
        *later_use = *destroyed ? -1 : 1;
        return false;
    }
    void DrawModal(SDL_Surface&) override {}
    void RequestModalRedraw() override {}
    std::unique_ptr<ModalContent> next;
    bool* destroyed;
    int* later_use;
};
}

TEST_CASE("A modal opened from a completion callback survives its predecessor's close",
          "[owner][modal]")
{
    g_modal_owner.reset();
    g_pending_main_game_dialog.reset();
    bool first_destroyed = false, second_destroyed = false;
    int first_use = 0, second_use = 0;
    auto second = std::make_unique<ChainedModal>(nullptr, &second_destroyed, &second_use);
    OpenModal(std::make_unique<ChainedModal>(std::move(second), &first_destroyed, &first_use));
    // The first modal reports "closed" after opening the second; the second stays.
    REQUIRE_FALSE(ProcessModalOwner());
    REQUIRE(first_use == 1);
    REQUIRE(first_destroyed);
    REQUIRE_FALSE(second_destroyed);
    REQUIRE(g_modal_owner != nullptr);
    // The replacement is processed on its own turn.
    REQUIRE(ProcessModalOwner());
    REQUIRE(second_use == 1);
    REQUIRE(g_modal_owner == nullptr);
    REQUIRE(second_destroyed);
}

TEST_CASE("Sprite ownership", "[owner]")
{
    image_and_sprite_ownership();
}

TEST_CASE("Font resource ownership", "[owner]")
{
    font_resource_ownership();
}

TEST_CASE("Font measurements skip glyphs outside the sprite sheet", "[font][owner]")
{
    initialize_test_framebuffer();
    ScopedTemporaryDirectory directory{"wiz8-font-glyph-range"};
    const auto assets = directory.path / "assets";
    const auto previous_roots = w8_native::path_roots();
    button_image_fixture(assets, "font.sti", 1, false, 1, 1);
    g_image_assets.clear();
    w8_native::configure_paths({assets.string(), (directory.path / "user").string()});
    struct Restore {
        w8_native::Roots roots;
        ~Restore()
        {
            ResetFontRendering();
            g_image_assets.clear();
            w8_native::configure_paths(roots);
        }
    } restore{previous_roots};

    ConfigureFontRendering(CreateEnglishTransTable());
    Font font;
    REQUIRE(font.load("font.sti"));
    REQUIRE(font.glyphs->frames.size() == 1);
    REQUIRE(GetIndex(U'é') >= static_cast<std::int16_t>(font.glyphs->frames.size()));
    REQUIRE(font.textWidth("é") == 0);
    REQUIRE(font.prefixWidth("é", 2) == 0);
    W8ControlsRect tooltip_bounds{0, 0, 250, 250};
    W8TextBuffer tooltip(&tooltip_bounds, "é", font,
                         g_W8TextBufferAlignTop | g_W8TextBufferAlignLeft, 4);
    REQUIRE(tooltip.m_maxLineWidth == 0);
    REQUIRE(tooltip.m_lineCount == 1);
}

TEST_CASE("Text input ownership", "[owner]")
{
    text_input_ownership();
}

TEST_CASE("MIPE text editors insert committed text and keep deletion on key presses", "[owner]")
{
    struct RestoreEditor {
        std::unique_ptr<W8LevelRuntimeBlock> level = std::move(g_level_block);
        std::unique_ptr<W8MipeState> state = std::move(g_mipe_state);
        int mode = g_mipe_mode;
        int base = g_mipe_table_base;
        decltype(g_current_screen_state.id) screen = g_current_screen_state.id;
        decltype(g_status.text_line_cursor) line = g_status.text_line_cursor;
        bool text_input = NativeTextInputEnabled();
        ~RestoreEditor()
        {
            g_level_block = std::move(level);
            g_mipe_state = std::move(state);
            g_mipe_mode = mode;
            g_mipe_table_base = base;
            g_current_screen_state.id = screen;
            g_status.text_line_cursor = line;
            SetNativeTextInputEnabled(text_input);
        }
    } restore;
    g_level_block = std::make_unique<W8LevelRuntimeBlock>();
    g_mipe_state = std::make_unique<W8MipeState>();
    g_current_screen_state.id = W8_SCREEN_MAIN_MENU;
    g_status.text_line_cursor = 0;
    g_mipe_table_base = 0;
    W8MipeEditField field{.type = 6};
    g_mipe_state->edit_fields = &field;
    g_mipe_state->edit_field_count = 1;
    g_mipe_state->edit_selection = -1;
    g_mipe_mode = 0xe;
    HandleMipeEditPropKey(SDLK_0);
    const InputEvent key{.type = InputEventType::KeyDown, .key = SDLK_A};
    HandleMipeKey(&key);
    REQUIRE(NativeTextInputEnabled());
    REQUIRE(field.text.empty());
    const InputEvent text{.type = InputEventType::Text, .text = U'é'};
    HandleMipeKey(&text);
    REQUIRE(field.text == "é");
    const InputEvent punctuation{.type = InputEventType::Text, .text = U'!'};
    HandleMipeKey(&punctuation);
    REQUIRE(field.text == "é");
    const InputEvent backspace{.type = InputEventType::KeyDown, .key = SDLK_BACKSPACE};
    HandleMipeKey(&backspace);
    REQUIRE(field.text.empty());
    MonGen generator;
    g_mipe_state->generator = &generator;
    g_mipe_mode = 0x19;
    HandleMipeKey(&text);
    HandleMipeKey(&key);
    REQUIRE(std::string(generator.name) == "é");
    HandleMipeKey(&backspace);
    REQUIRE(std::string(generator.name).empty());
}

TEST_CASE("Formatting contracts", "[owner]")
{
    formatted_strings();
}

TEST_CASE("Generic button image ownership", "[owner]")
{
    generic_button_images();
}

TEST_CASE("Dialog controls own interaction without legacy GUI buttons", "[owner]")
{
    UiStateSnapshot saved_state;
    struct TooltipStateSnapshot {
        bool tooltips_enabled = g_settings.tooltips_enabled;
        std::vector<std::string> localized_strings = std::move(g_localized_strings);
        ~TooltipStateSnapshot()
        {
            g_settings.tooltips_enabled = tooltips_enabled;
            g_localized_strings = std::move(localized_strings);
        }
    } tooltip_state;
    g_settings.tooltips_enabled = true;
    g_localized_strings.emplace_back("Button tooltip");
    UiLayer layer;
    Font font{};

    int activations = 0;
    {
        W8ButtonWidget button(layer);
        REQUIRE(button.Configure(ButtonAppearance{.generic_image = DefaultGenericButtonImage(),
                                                  .string = std::string("button"),
                                                  .usFont = &(font),
                                                  .sForeColor = 0,
                                                  .sShadowColor = 0},
                                 {10, 10, 40, 20},
                                 {.on_click = [&](W8ButtonWidget* control) { ++activations; }}));
        button.SetTooltipIndex(0);
        button.SetTooltipEnabled(true);
        REQUIRE(UiTargetAt({15, 15}) != nullptr);
        button.SetTooltipEnabled(false);
        GetInputState().left_button = false;
        GetInputState().mouse = {400, 400};
        dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true, false, 1);
        REQUIRE((button.IsPressed() && activations == 0));
        REQUIRE((button.GetMousePosition().x == 15 && button.GetMousePosition().y == 15));
        dispatch_mouse(InputEventType::LeftButtonUp, 15, 15, false, false, 1);
        REQUIRE(!button.IsPressed());
        REQUIRE(activations == 1);
        dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
        REQUIRE(activations == 1);
        dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
        dispatch_mouse(InputEventType::MouseMove, 80, 80, true);
        dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
        REQUIRE((!button.IsPressed() && activations == 1));
        dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
        button.SetEnabled(false);
        button.SetEnabled(true);
        dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
        REQUIRE((!button.IsPressed() && activations == 1));
        dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
        button.SetVisible(false);
        button.SetVisible(true);
        dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
        REQUIRE(activations == 1);
        REQUIRE(button.IsConfigured());
        button.Resize(60, 30);
        button.SetPosition(20, 20);
        REQUIRE((button.Bounds().w == 60 && button.Bounds().h == 30));
        dispatch_mouse(InputEventType::LeftButtonDown, 75, 45, true);
        REQUIRE(button.IsLeftButtonHeld());
        dispatch_mouse(InputEventType::LeftButtonUp, 75, 45);
        REQUIRE(!button.IsLeftButtonHeld());
        REQUIRE(activations == 2);
        button.SetPosition(10, 10);
        button.Resize(40, 20);
        button.Policy().repeat = true;
        dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
        dispatch_mouse(InputEventType::LeftButtonRepeat, 15, 15, true);
        dispatch_mouse(InputEventType::LeftButtonRepeat, 15, 15, true);
        REQUIRE(activations == 4);
        dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
        REQUIRE((!button.IsPressed() && activations == 4));
        button.Policy().repeat = false;
        button.Policy().activation = ButtonActivation::Press;
        dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
        REQUIRE(activations == 5);
        dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
        REQUIRE(activations == 5);
        // Reconfiguration replaces the entire behavior, including callbacks
        // that the old button used for secondary actions and hover.
        int stale_callbacks = 0;
        const auto stale = [&](W8ButtonWidget*) { ++stale_callbacks; };
        REQUIRE(button.Configure(ButtonAppearance{}, {10, 10, 40, 20},
                                 {.on_click = stale,
                                  .on_right_click = stale,
                                  .on_move = stale,
                                  .on_double_click = stale,
                                  .on_hover = [&](bool) { ++stale_callbacks; },
                                  .policy = {.activation = ButtonActivation::Press},
                                  .tooltip = 0,
                                  .menu_artwork = true}));
        REQUIRE_FALSE(button.Configure(ButtonImage{}));
        dispatch_mouse(InputEventType::RightButtonDown, 15, 15, false, true);
        dispatch_mouse(InputEventType::RightButtonUp, 15, 15);
        REQUIRE(stale_callbacks > 0); // Failed loads keep the previous setup.
        REQUIRE(button.Configure(ButtonAppearance{}, {10, 10, 40, 20},
                                 {.on_click = [&](W8ButtonWidget*) { ++activations; }}));
        const int stale_count = stale_callbacks;
        dispatch_mouse(InputEventType::MouseMove, 80, 80);
        dispatch_mouse(InputEventType::MouseMove, 15, 15);
        dispatch_mouse(InputEventType::RightButtonDown, 15, 15, false, true);
        dispatch_mouse(InputEventType::RightButtonUp, 15, 15);
        dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true, false, 2);
        REQUIRE(activations == 5); // Default activation is on release again.
        dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
        REQUIRE(activations == 6);
        REQUIRE(stale_callbacks == stale_count);
        button.Reset();
        REQUIRE((!button.IsEnabled() && !button.IsPressed() && button.Bounds().w == 0));
        dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
        dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
        REQUIRE(activations == 6);
    }

    auto closing = std::make_unique<W8ButtonWidget>(layer);
    closing->Configure(ButtonAppearance{.generic_image = DefaultGenericButtonImage(),
                                        .string = std::string("close"),
                                        .usFont = &(font),
                                        .sForeColor = 0,
                                        .sShadowColor = 0},
                       {10, 10, 40, 20}, {.on_click = [&](W8ButtonWidget*) {
                           AfterUiDispatch([&] { closing.reset(); });
                       }});
    dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
    dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
    REQUIRE(!closing);
    // Hover callbacks obey the same owner lifetime as click callbacks.
    closing = std::make_unique<W8ButtonWidget>(layer);
    REQUIRE(
        closing->Configure(ButtonAppearance{}, {10, 10, 40, 20}, {.on_hover = [&](bool entered) {
                               if (entered) {
                                   AfterUiDispatch([&] { closing.reset(); });
                               }
                           }}));
    UpdateUiPointer(80, 80);
    UpdateUiPointer(15, 15);
    REQUIRE_FALSE(closing);
}

TEST_CASE("Modal confirmations and scrollbars dispatch through their native owners", "[owner]")
{
    initialize_test_framebuffer();
    UiStateSnapshot saved_state;
    ScopedTemporaryDirectory temporary{"wiz8-modal-controls"};
    const auto& root = temporary.path;
    const auto assets = root / "assets";
    for (const auto* image : {"dialogborder.sti", "dialogedge.sti", "dialogconfirmation.sti"})
        button_image_fixture(assets / "data" / "dialogs", image);
    for (const auto* image : {"dialoginlay.sti", "dialoguparrow.sti", "dialogdownarrow.sti",
                              "dialogslidebar.sti", "dialogbackground_dark.sti"})
        button_image_fixture(assets / "data" / "dialogs", image);
    button_image_fixture(assets, "font.sti", 128, false, 1, 10);
    button_image_fixture(assets, "background.sti");
    button_image_fixture(assets, "arrows.sti", 12, false, 2, 3);
    button_image_fixture(assets, "track.sti", 1, false, 5, 100);
    g_image_assets.clear();
    ScopedAssetRoots scoped_roots{{assets.string(), (root / "user").string()}};

    std::vector<int> positions;
    {
        UiLayer input;
        const W8DialogScrollBar::Resources resources{
            "arrows.sti", "track.sti", 0,
            [&positions](int position) { positions.push_back(position); }};
        const auto targets = LiveUiTargetCount();
        REQUIRE_THROWS(W8DialogScrollBar(input, {"arrows.sti", "missing.sti", 0, {}}));
        REQUIRE(LiveUiTargetCount() == targets);
        W8DialogScrollBar bar(input, resources);
        bar.SetLayout(100, 20, 20, 5, 10, 30);
        auto auxiliary = CreateCpuStorage(640, 480, SDL_PIXELFORMAT_RGB565);
        REQUIRE(auxiliary);
        TakeUiUpload();
        InvalidateRegion(4, 4, 5, 5);
        bar.Draw(*auxiliary);
        const auto pending = TakeUiUpload();
        REQUIRE((pending.x == 4 && pending.y == 4 && pending.w == 1 && pending.h == 1));
        bar.Refresh();
        REQUIRE_FALSE(bar.TakeDirty());
        const auto uploaded = TakeUiUpload();
        REQUIRE((uploaded.x == 100 && uploaded.y == 20 && uploaded.w == 5 && uploaded.h == 100));
        // A fresh auxiliary canvas still needs the clean scrollbar's artwork.
        REQUIRE(SDL_FillSurfaceRect(auxiliary.get(), nullptr, 0x07e0));
        bar.Draw(*auxiliary);
        REQUIRE(surface_pixel(*auxiliary, 100, 20) != 0x07e0);
        REQUIRE_FALSE(bar.TakeDirty());
        REQUIRE(TakeUiUpload().w == 0);
        bar.SetLayout(100, 20, 20, 5, 10, 30);
        bar.Refresh();
        REQUIRE(TakeUiUpload().w == 0);
        GetInputState().mouse.y = 400;
        dispatch_mouse(InputEventType::LeftButtonDown, 101, 116, true);
        dispatch_mouse(InputEventType::LeftButtonUp, 101, 116);
        REQUIRE(positions == std::vector<int>{17});
        dispatch_mouse(InputEventType::LeftButtonDown, 101, 21, true);
        dispatch_mouse(InputEventType::LeftButtonRepeat, 101, 21, true);
        dispatch_mouse(InputEventType::LeftButtonUp, 101, 21);
        REQUIRE(positions == std::vector<int>{17, 16, 15});
        bar.SetLayout(100, 20, 1, 0, 10, 30);
        dispatch_mouse(InputEventType::LeftButtonDown, 101, 21, true);
        dispatch_mouse(InputEventType::LeftButtonUp, 101, 21);
        REQUIRE(positions.size() == 3);
        bar.SetLayout(100, 20, 20, 5, 10, 30);
        dispatch_mouse(InputEventType::LeftButtonDown, 101, 118, true);
        dispatch_mouse(InputEventType::LeftButtonUp, 101, 118);
        REQUIRE(positions.back() == 6);
    }

    auto old_font = std::move(g_wiz_dialog_font);
    auto& font = g_wiz_dialog_font;
    REQUIRE(font.load("font.sti"));
    class MessageDialog final : public W8MessageDialogBase {
    public:
        using W8MessageDialogBase::m_cancel_button;
        using W8MessageDialogBase::m_confirm_button;
    };
    for (const bool accept : {false, true}) {
        MessageDialog dialog;
        dialog.SetOrigin(10, 10);
        dialog.SetExtent(100, 80);
        dialog.SetBackground("background.sti", 0);
        dialog.SetMessage("", 0, 0, true, true, false, false, 100, 80);
        auto& control = accept ? dialog.m_confirm_button : dialog.m_cancel_button;
        {
            // A dialog opened on top owns the modal scope: the one beneath
            // keeps its controls but takes no input until it is closed.
            MessageDialog inner;
            inner.InputLayer().setModal(true);
            dispatch_mouse(InputEventType::LeftButtonDown, control.Bounds().x, control.Bounds().y,
                           true);
            dispatch_mouse(InputEventType::LeftButtonUp, control.Bounds().x, control.Bounds().y);
            REQUIRE(dialog.is_open);
        }
        dialog.InputLayer().setModal(true);
        dispatch_mouse(InputEventType::LeftButtonDown, control.Bounds().x, control.Bounds().y,
                       true);
        REQUIRE(dialog.is_open);
        dispatch_mouse(InputEventType::LeftButtonUp, control.Bounds().x, control.Bounds().y);
        REQUIRE(!dialog.is_open);
        REQUIRE(dialog.accepted == accept);
    }

    REQUIRE(font.load("font.sti"));
    {
        W8DialogTextArea area;
        const W8ControlsRect bounds{0, 0, 100, 100};
        area.Configure(&bounds, font, g_W8DialogTextAreaPointSelection);
        area.AddEntry("", "Z", 0, 0, 0);
        area.AddEntry("", "A\nB", 0, 0, 1);
        area.AddEntry("", "C", 0, 0, 1);
        area.SetCategoryFilter(1);
        REQUIRE(area.GetOwningEntryIndex(0) == 1);
        REQUIRE(area.UpdateSelectionFromPoint(1, 1));
        REQUIRE_FALSE(area.UpdateSelectionFromPoint(1, font.height() + 1));
        REQUIRE(area.UpdateSelectionFromPoint(1, 2 * font.height() + 1));
        area.SetEntrySpacing(1);
        area.SetLineHeight(12);
        REQUIRE(area.GetEntry(0)->GetLineHeight() == 12);
        REQUIRE(area.GetEntry(2)->GetLineHeight() == 12);
        REQUIRE(area.GetTotalLineCount() == 4); // Two wrapped lines, a gap, then C.
        REQUIRE(area.HitTestEntry(1, 25) == 0);
        REQUIRE(area.HitTestEntry(1, 37) == 1);
        area.SetFirstVisibleLine(1);
        REQUIRE(area.HitTestEntry(1, 1) == 0);
        REQUIRE(area.HitTestEntry(1, 25) == 1);
        REQUIRE(area.ScrollUp(false));
        REQUIRE(area.HitTestEntry(1, 25) == 0);
        area.SetCategoryFilter(-1);
        area.SetSorted(true);
        REQUIRE(area.GetOwningEntryIndex(0) == 1);
        REQUIRE(area.GetOwningEntryIndex(2) == 0);
        area.RemoveEntry(1);
        REQUIRE(area.GetOwningEntryIndex(0) == 1);
    }
    // Empty item rows can still receive a click after a transfer, before the
    // next screen recomposition hides their targets.
    for (const auto* image :
         {"dialogbackground.sti", "popup_chest_selectionbuttons.sti",
          "chest_confirmationbuttons.sti", "popup_chest2.sti", "maininterface_scroll.sti"}) {
        button_image_fixture(assets / "data" / "dialogs", image, 16);
    }
    {
        W8TriggerItemPickerDialog picker;
        for (int row = 0; row < 4; ++row) {
            auto& button = *picker.m_buttons[5 + row];
            button.SetPosition(20, 20);
            dispatch_mouse(InputEventType::RightButtonDown, 20, 20, false, true);
            dispatch_mouse(InputEventType::RightButtonUp, 20, 20);
            button.SetVisible(false);
            REQUIRE_FALSE(g_modal_owner);
        }
    }
    {
        W8ListBoxDialog list;
        list.SetOrigin(10, 10);
        list.SetExtent(100, 80);
        list.SetBackground("background.sti", 0);
        for (int index = 0; index < 20; ++index) list.m_lines.emplace_back("X");
        list.Configure();
        auto canvas = CreateCpuStorage(640, 480, SDL_PIXELFORMAT_RGB565);
        REQUIRE(canvas);
        TakeUiUpload();
        list.Draw(*canvas);
        list.Draw(*canvas);
        REQUIRE(TakeUiUpload().w == 0);
        REQUIRE(list.m_area_button.m_dirty);
        list.RefreshModal();
        REQUIRE(TakeUiUpload().w > 0);
        list.InputLayer().setModal(true);
        GetInputState().mouse.y = 400;
        dispatch_mouse(InputEventType::LeftButtonDown, list.m_area_button.Bounds().x + 3,
                       list.m_area_button.Bounds().y + 21, true);
        dispatch_mouse(InputEventType::LeftButtonUp, list.m_area_button.Bounds().x + 3,
                       list.m_area_button.Bounds().y + 21);
        REQUIRE(list.m_selected_line == 2);
        dispatch_mouse(InputEventType::LeftButtonDown, list.m_down_button.Bounds().x,
                       list.m_down_button.Bounds().y, true);
        dispatch_mouse(InputEventType::LeftButtonRepeat, list.m_down_button.Bounds().x,
                       list.m_down_button.Bounds().y, true);
        dispatch_mouse(InputEventType::LeftButtonUp, list.m_down_button.Bounds().x,
                       list.m_down_button.Bounds().y);
        REQUIRE(list.m_selected_line == 4);
        dispatch_mouse(InputEventType::LeftButtonDown, list.m_third_text_button.Bounds().x + 1,
                       list.m_third_text_button.Bounds().y + 30, true);
        REQUIRE(list.m_selected_line == 15);
        dispatch_mouse(InputEventType::LeftButtonRepeat, list.m_third_text_button.Bounds().x + 1,
                       list.m_third_text_button.Bounds().y + 20, true);
        dispatch_mouse(InputEventType::LeftButtonUp, list.m_third_text_button.Bounds().x + 1,
                       list.m_third_text_button.Bounds().y + 20);
        REQUIRE(list.m_selected_line == 10);
    }
    {
        W8ListBoxDialog short_list;
        short_list.SetOrigin(10, 10);
        short_list.SetExtent(100, 80);
        short_list.SetBackground("background.sti", 0);
        short_list.m_lines.emplace_back("X");
        short_list.Configure();
        short_list.RefreshModal();
        REQUIRE_FALSE(short_list.m_scrollable);
        for (const auto* button : {&short_list.m_up_button, &short_list.m_down_button,
                                   &short_list.m_slider_button, &short_list.m_third_text_button}) {
            REQUIRE_FALSE(button->IsVisible());
            REQUIRE_FALSE(button->m_dirty);
        }
    }
    button_image_fixture(assets, "data/dialogs/popup_quote.sti");
    auto old_secondary = std::move(g_wiz_text_font_secondary);
    REQUIRE(g_wiz_text_font_secondary.load("font.sti"));
    {
        W8Character character{};
        struct RestoreParty {
            W8StatusBuffers buffers = g_status.buffers;
            std::unique_ptr<W8CharacterEventQueue> queue =
                std::move(gXStatus.character_event_queue);
            ~RestoreParty()
            {
                g_status.buffers = buffers;
                gXStatus.character_event_queue = std::move(queue);
            }
        } restore_party;
        character.fInParty = true; // Quote access must still use the previewed character directly.
        gXStatus.character_event_queue = std::make_unique<W8CharacterEventQueue>();
        auto& queue = *gXStatus.character_event_queue;
        queue.active_events.push_back(std::make_unique<W8CharacterEvent>(&character, 0, 0, 0, 127));
        auto* unrelated = queue.active_events.front().get();
        g_status.buffers.Char = nullptr;
        g_status.buffers.XChar = nullptr;
        {
            W8CharacterSummaryDialog summary(&character);
            REQUIRE(summary.ProcessInput()); // Waiting for its own 750 ms playback delay.
            SDL_Delay(760);
            REQUIRE(summary.ProcessInput()); // Missing audio uses its own quote-duration fallback.
            PostInputEvent({.type = InputEventType::KeyDown, .key = SDLK_ESCAPE});
            REQUIRE_FALSE(summary.ProcessInput());
        }
        REQUIRE(queue.active_events.size() == 1);
        REQUIRE(queue.active_events.front().get() == unrelated);
        REQUIRE(character.fInParty);
        queue.active_events.clear(); // Release the test entry without gameplay completion.
    }
    g_wiz_text_font_secondary = std::move(old_secondary);
    g_wiz_dialog_font = std::move(old_font);
    g_image_assets.clear();
}

TEST_CASE("assert-plain", "[.death]")
{
    W8_ASSERT(false);
    FAIL("Assert did not abort");
}

namespace {
void assertion_message_handler(const char* expression, const char*, w8_long, const char*)
{
    std::fprintf(stderr, "Assertion failed: %s\n", expression);
    std::abort();
}
}

TEST_CASE("assert-message", "[.death]")
{
    srAssertSetFunc(assertion_message_handler);
    srAssertFail("handler installed", __FILE__, __LINE__, 0);
    FAIL("srAssertFail did not abort");
}

TEST_CASE("assert-button-self-destroy", "[.death]")
{
    UiLayer layer;
    Font font{};
    auto button = std::make_unique<W8ButtonWidget>(layer);
    button->Configure(ButtonAppearance{.generic_image = DefaultGenericButtonImage(),
                                       .string = std::string("close"),
                                       .usFont = &(font),
                                       .sForeColor = 0,
                                       .sShadowColor = 0},
                      {10, 10, 40, 20}, {.on_click = [&](W8ButtonWidget*) { button.reset(); }});
    dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
    dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
    FAIL("Destroying a button from its own callback did not abort");
}

TEST_CASE("Image assets share normalized paths and retain resources across clearing",
          "[owner][image]")
{
    initialize_test_framebuffer();
    ScopedTemporaryDirectory directory{"wiz8-image-assets"};
    const auto assets = directory.path / "assets";
    const auto previous_roots = w8_native::path_roots();
    button_image_fixture(assets, "data/items/x.sti", 1);
    button_image_fixture(assets, "data/items/late.sti", 1);

    const auto bitmap_path = assets / "data" / "items" / "screen.pcx";
    std::filesystem::create_directories(bitmap_path.parent_path());
    std::vector<std::uint8_t> pcx(128);
    pcx[0] = 10;
    pcx[1] = 5;
    pcx[2] = 1;
    pcx[3] = 8;
    pcx[65] = 1;
    pcx[66] = 1;
    pcx.push_back(1);
    pcx.push_back(12);
    pcx.resize(pcx.size() + 256 * 3);
    {
        std::ofstream file(bitmap_path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(pcx.data()),
                   static_cast<std::streamsize>(pcx.size()));
        REQUIRE(file.good());
    }

    g_image_assets.clear();
    w8_native::configure_paths({assets.string(), (directory.path / "user").string()});
    struct Restore {
        w8_native::Roots roots;
        ~Restore()
        {
            g_image_assets.clear();
            w8_native::configure_paths(roots);
        }
    } restore{previous_roots};

    const auto original = g_image_assets.sprites("data\\items\\x.sti");
    REQUIRE(original);
    REQUIRE(g_image_assets.sprites("data/items/x.sti") == original);
    REQUIRE_FALSE(g_image_assets.bitmap("data/items/x.sti"));
    REQUIRE(g_image_assets.sprites("data/items/x.sti") == original);
    REQUIRE_FALSE(g_image_assets.sprites("data/items/missing.sti"));
    button_image_fixture(assets, "data/items/missing.sti", 1);
    const auto retried = g_image_assets.sprites("data/items/missing.sti");
    REQUIRE(retried);

    const auto bitmap = g_image_assets.bitmap("data\\items\\screen.pcx");
    REQUIRE(bitmap);
    REQUIRE(g_image_assets.bitmap("data/items/screen.pcx") == bitmap);
    REQUIRE_FALSE(g_image_assets.sprites("data/items/screen.pcx"));

    g_image_assets.clear();
    const auto reloaded = g_image_assets.sprites("data/items/x.sti");
    const auto bitmap_reloaded = g_image_assets.bitmap("data/items/screen.pcx");
    REQUIRE(reloaded);
    REQUIRE(bitmap_reloaded);
    REQUIRE(reloaded != original);
    REQUIRE(bitmap_reloaded != bitmap);
    REQUIRE(original->frames.size() == 1);
    REQUIRE(bitmap->w == 1);
}

TEST_CASE("CatalogArtwork renders sprite storage", "[owner][image][catalog]")
{
    initialize_test_framebuffer();
    CatalogArtworkAssets assets;

    auto& framebuffer = GetFrameBufferSurface();
    const auto sprites = CatalogArtwork::Load(0x86);
    REQUIRE(sprites.spriteSheet());
    REQUIRE_FALSE(sprites.bitmap());
    REQUIRE((sprites.size(0).x == 4 && sprites.size(0).y == 2));
    REQUIRE((sprites.position(0).x == 2 && sprites.position(0).y == 3));
    REQUIRE(SDL_FillSurfaceRect(&framebuffer, nullptr, 0x07e0));
    sprites.draw(framebuffer, 0, 10, 10);
    REQUIRE(surface_pixel(framebuffer, 12, 13) == sprites.spriteSheet()->colors[1]);
    TakeUiUpload();
    sprites.drawAndInvalidate(framebuffer, 0, 20, 20);
    {
        const auto upload = TakeUiUpload();
        const SDL_Point corner{20, 20};
        REQUIRE(SDL_PointInRect(&corner, &upload));
    }
    TakeUiUpload();
    sprites.invalidate(0, 30, 30);
    {
        const auto upload = TakeUiUpload();
        const SDL_Point corner{30, 30};
        REQUIRE(SDL_PointInRect(&corner, &upload));
    }
    REQUIRE_THROWS_AS(sprites.drawRect(framebuffer, {0, 0, 1, 1}, 40, 40), std::logic_error);
}

TEST_CASE("CatalogArtwork renders bitmap storage with mirror key semantics",
          "[owner][image][catalog]")
{
    initialize_test_framebuffer();
    CatalogArtworkAssets assets;
    auto& framebuffer = GetFrameBufferSurface();
    const auto bitmap = CatalogArtwork::Load(0x11);
    REQUIRE_FALSE(bitmap.spriteSheet());
    REQUIRE(bitmap.bitmap());
    REQUIRE((bitmap.size(0).x == 2 && bitmap.size(0).y == 1));
    REQUIRE((bitmap.position(0).x == 0 && bitmap.position(0).y == 0));
    const auto red = Get16BPPColor({255, 0, 0, 255});
    REQUIRE(SDL_FillSurfaceRect(&framebuffer, nullptr, 0x07e0));
    bitmap.draw(framebuffer, 0, 50, 50);
    REQUIRE(surface_pixel(framebuffer, 50, 50) == 0x07e0);
    REQUIRE(surface_pixel(framebuffer, 51, 50) == red);
    TakeUiUpload();
    bitmap.drawAndInvalidate(framebuffer, 0, 60, 60);
    {
        const auto upload = TakeUiUpload();
        const SDL_Point corner{60, 60};
        REQUIRE(SDL_PointInRect(&corner, &upload));
    }

    REQUIRE(SDL_FillSurfaceRect(&framebuffer, nullptr, 0x07e0));
    bitmap.draw(framebuffer, 0, 70, 70, {.mirror = true});
    REQUIRE(surface_pixel(framebuffer, 70, 70) == red);
    REQUIRE(surface_pixel(framebuffer, 71, 70) == 0);
    bitmap.drawRect(framebuffer, {0, 0, 1, 1}, 80, 80);
    REQUIRE(surface_pixel(framebuffer, 80, 80) == 0);
}

TEST_CASE("Item images come from the shared image cache", "[owner][catalog]")
{
    ScopedTemporaryDirectory directory{"wiz8-item-cache"};
    const auto assets = directory.path / "assets";
    const auto previous_roots = w8_native::path_roots();
    button_image_fixture(assets, "data/items/icon.sti", 1);
    std::vector<W8ItemDatabaseRecord> records(1005);
    for (auto& record : records) record.video_object_name = "icon.sti";
    struct Restore {
        W8ItemDatabaseRecord* records;
        unsigned int count;
        w8_native::Roots roots;
        ~Restore()
        {
            g_image_assets.clear();
            g_item_records = records;
            gXStatus.uiItemsInDatabase = count;
            w8_native::configure_paths(roots);
        }
    } restore{g_item_records, gXStatus.uiItemsInDatabase, previous_roots};

    g_image_assets.clear();
    w8_native::configure_paths({assets.string(), (directory.path / "user").string()});
    g_item_records = records.data();
    gXStatus.uiItemsInDatabase = static_cast<unsigned int>(records.size());
    REQUIRE_THROWS_AS(ItemSprites(-1), std::out_of_range);
    REQUIRE_THROWS_AS(ItemSprites(static_cast<int>(records.size())), std::out_of_range);
    records[0].video_object_name = "missing.sti";
    REQUIRE_THROWS_AS(ItemSprites(0), std::runtime_error);
    records[0].video_object_name.clear();
    records[0].unidentified_name_index = 145;
    REQUIRE_THROWS_AS(ItemSprites(0), std::out_of_range);
    records[0].video_object_name = "icon.sti";
    records[0].unidentified_name_index = 0;

    // Items naming one file share one decoded sheet.
    const auto held = ItemSprites(0);
    REQUIRE(held->frames.size() == 1);
    REQUIRE(ItemSprites(1004) == held);
    // A held sheet outlives the cache; the next lookup decodes a fresh one.
    g_image_assets.clear();
    REQUIRE(held->frames.size() == 1);
    REQUIRE(ItemSprites(1004) != held);
    REQUIRE_THROWS_AS(CatalogArtwork::Load(-1, 0), std::out_of_range);
    REQUIRE_THROWS_AS(CatalogArtwork::Load(0, -1), std::out_of_range);
    REQUIRE_THROWS_AS(CatalogArtwork::Load(0x86, std::numeric_limits<int>::max()),
                      std::out_of_range);
}

TEST_CASE("Main-game screen state owns HUD input through suspension and teardown",
          "[owner][ui-layer]")
{
    UiStateSnapshot saved_state;
    const auto layers = LiveUiLayerCount();
    const auto targets = LiveUiTargetCount();

    {
        MainGameScreenState screen;
        auto& hud = screen.hud_buttons;
        int clicks = 0;
        REQUIRE(hud.options_disk.Configure(ButtonAppearance{}, {100, 100, 20, 20},
                                           {.on_click = [&](W8ButtonWidget*) { ++clicks; }}));
        REQUIRE(LiveUiTargetCount() > targets);
        REQUIRE(UiTargetAt({105, 105}) == nullptr);
        hud.input.setEnabled(true);
        screen.world.add(SDL_Rect{100, 100, 20, 20}, [](const UiPointerEvent&) {
            FAIL("World input intercepted a HUD button");
            return UiResult::Handled;
        });
        screen.world.setEnabled(true);
        dispatch_mouse(InputEventType::LeftButtonDown, 105, 105, true);
        dispatch_mouse(InputEventType::LeftButtonUp, 105, 105);
        REQUIRE(clicks == 1);
        // Another screen taking input leaves this screen's layers untouched.
        const int screen_owner = UiInputOwner();
        SelectUiInputOwner(screen_owner + 1);
        REQUIRE(UiTargetAt({105, 105}) == nullptr);
        SelectUiInputOwner(screen_owner);
        dispatch_mouse(InputEventType::LeftButtonDown, 105, 105, true);
        dispatch_mouse(InputEventType::LeftButtonUp, 105, 105);
        REQUIRE(clicks == 2);
        screen.submenu = std::make_unique<MainGameSubMenu>();
        auto& panel = *screen.submenu;
        panel.buttons.push_back(std::make_unique<W8ButtonWidget>(panel.input));
        REQUIRE(panel.buttons.back()->Configure(ButtonAppearance{}, {100, 100, 20, 20}));
        dispatch_mouse(InputEventType::LeftButtonDown, 105, 105, true);
        dispatch_mouse(InputEventType::LeftButtonUp, 105, 105);
        REQUIRE(clicks == 2); // The submenu, in turn, wins over the HUD.
        dispatch_mouse(InputEventType::LeftButtonDown, 105, 105, true);
        REQUIRE(g_ui.captured != nullptr);
        // State destruction must remove captured submenu rows as well as HUD input.
    }
    REQUIRE(g_ui.captured == nullptr);
    REQUIRE(g_ui.hover == nullptr);
    REQUIRE(LiveUiTargetCount() == targets);
    REQUIRE(LiveUiLayerCount() == layers);
    {
        MainGameScreenState reentered;
        REQUIRE_FALSE(reentered.hud_buttons.options_disk.IsConfigured());
    }
    REQUIRE(LiveUiTargetCount() == targets);
    REQUIRE(LiveUiLayerCount() == layers);
}

TEST_CASE("Button release and repeat rules preserve independent and primary state activation",
          "[owner][input]")
{
    const ButtonPolicy pointer{.repeat = true};
    const ButtonPolicy primary{.repeat = true, .input_rule = ButtonInputRule::PrimaryPressedState};
    ButtonInteraction interaction;
    REQUIRE_FALSE(interaction.release(SDL_BUTTON_LMASK, pointer).activate);
    REQUIRE_FALSE(interaction.release(SDL_BUTTON_RMASK, pointer).activate);
    REQUIRE_FALSE(interaction.repeat(SDL_BUTTON_RMASK, pointer).activate);
    REQUIRE_FALSE(interaction.release(SDL_BUTTON_LMASK, primary).activate);
    REQUIRE(interaction.release(SDL_BUTTON_RMASK, primary).activate);
    REQUIRE(interaction.repeat(SDL_BUTTON_RMASK, primary).activate);
    interaction.cancel(false);

    // A controller can supply primary pressed state without a pointer press.
    interaction.pressed = true;
    REQUIRE(interaction.release(SDL_BUTTON_LMASK, primary).activate);
    interaction.pressed = true;
    REQUIRE_FALSE(interaction.release(SDL_BUTTON_LMASK, pointer).activate);
    interaction.cancel(false);

    // Pointer buttons suppress their own repeated release independently.
    interaction.press(SDL_BUTTON_LMASK, pointer);
    interaction.press(SDL_BUTTON_RMASK, pointer);
    REQUIRE(interaction.repeat(SDL_BUTTON_RMASK, pointer).activate);
    REQUIRE(interaction.release(SDL_BUTTON_LMASK, pointer).activate);
    REQUIRE(interaction.pressed); // Right remains armed and depressed.
    REQUIRE(interaction.release(SDL_BUTTON_RMASK, pointer).repeated_release);
    REQUIRE_FALSE(interaction.pressed);

    // Primary-state controls share repeat suppression and depress artwork
    // only on left press, including when a controller drives right repeat.
    interaction.press(SDL_BUTTON_RMASK, primary);
    REQUIRE_FALSE(interaction.pressed);
    interaction.press(SDL_BUTTON_LMASK, primary);
    REQUIRE(interaction.repeat(SDL_BUTTON_RMASK, primary).activate);
    REQUIRE(interaction.release(SDL_BUTTON_LMASK, primary).repeated_release);
    REQUIRE_FALSE(interaction.pressed);
    REQUIRE(interaction.release(SDL_BUTTON_RMASK, primary).activate);
}

TEST_CASE("Dialog completion extracts a result before retirement and preserves replacements",
          "[owner]")
{
    UiStateSnapshot saved_state;
    bool accepted = false;
    ModalContent* replacement = nullptr;
    auto dialog = std::make_unique<W8MessageDialogBase>();
    dialog->accepted = true;
    dialog->is_open = false;
    g_pending_main_game_dialog = std::make_unique<W8ListBoxDialog>();
    auto* suspended = g_pending_main_game_dialog.get();
    dialog->on_complete = [&](bool result) {
        accepted = result;
        REQUIRE(g_modal_owner.get() == suspended);
        auto next = std::make_unique<W8ListBoxDialog>();
        replacement = next.get();
        OpenModal(std::move(next));
    };
    g_modal_owner = std::move(dialog);
    REQUIRE_FALSE(ProcessModalOwner());
    REQUIRE(accepted);
    REQUIRE(g_modal_owner.get() == replacement);
    OpenModal(nullptr);
}
