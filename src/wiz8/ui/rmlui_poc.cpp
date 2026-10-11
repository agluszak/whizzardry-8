#include "wiz8/ui/rmlui_poc.h"

#include <SDL3/SDL.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "RmlUi_Renderer_SDL.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "Font.h"
#include "vobject.h"
#include "vsurface.h"

// An experimental, game-embedded RmlUi dialog. The framebuffer is an SDL CPU
// surface; the existing SurRender frame uploads it to SDL GPU. No second window
// or second GPU device is created. This is intentionally not a new UI layer.
namespace wiz8::ui_poc {
namespace {

// Original Wizardry dialog dimensions and sprite placements are in 640x480
// coordinates. Only layout is specified in RCSS; all visual assets are retail.
constexpr SDL_Rect dialog_bounds{160, 145, 320, 190};
constexpr int border_inset = 9;
constexpr std::string_view message_text = "Wizardry 8 dialog artwork";

constexpr auto document_markup = R"rml(
<rml>
<head>
<title>Wizardry 8 dialog</title>
<style>
body { margin: 0; }
#dialog {
    position: absolute;
    left: 160px;
    top: 145px;
    width: 320px;
    height: 190px;
    decorator: tiled-box(
        "wiz8-border-0.sti", "wiz8-border-1.sti", "wiz8-border-2.sti",
        "wiz8-border-3.sti", "wiz8-transparent.sti", "wiz8-border-4.sti",
        "wiz8-border-5.sti", "wiz8-border-6.sti", "wiz8-border-7.sti"
    ), image("wiz8-background-0.sti" repeat);
}
#inner {
    position: absolute;
    left: 9px;
    top: 9px;
    width: 302px;
    height: 172px;
    decorator: tiled-box(
        "wiz8-edge-0.sti", "wiz8-edge-1.sti", "wiz8-edge-2.sti",
        "wiz8-edge-3.sti", "wiz8-transparent.sti", "wiz8-edge-4.sti",
        "wiz8-edge-5.sti", "wiz8-edge-6.sti", "wiz8-edge-7.sti"
    );
}
#message {
    position: absolute;
    left: 14px;
    top: 36px;
    width: 274px;
    height: 80px;
}
#actions {
    position: absolute;
    bottom: 6px;
    display: flex;
    justify-content: space-between;
}
button {
    margin: 0;
    padding: 0;
    border-width: 0;
    background-color: transparent;
}
#accept { decorator: image("wiz8-confirmation-0.sti" scale-none 0% 0%); }
#accept:hover, #accept:focus { decorator: image("wiz8-confirmation-1.sti" scale-none 0% 0%); }
#accept:active { decorator: image("wiz8-confirmation-2.sti" scale-none 0% 0%); }
#cancel { decorator: image("wiz8-confirmation-4.sti" scale-none 0% 0%); }
#cancel:hover, #cancel:focus { decorator: image("wiz8-confirmation-5.sti" scale-none 0% 0%); }
#cancel:active { decorator: image("wiz8-confirmation-6.sti" scale-none 0% 0%); }
</style>
</head>
<body>
<div id="dialog">
    <div id="inner">
        <div id="message"></div>
        <div id="actions">
            <button id="accept" autofocus title="Confirm"></button>
            <button id="cancel" title="Cancel"></button>
        </div>
    </div>
</div>
</body>
</rml>
)rml";

// These are not PNG copies. RmlUi's virtual filenames resolve through
// CreateVideoObject to the original STI assets inside the installed game data.
class GameArtRenderer final : public RenderInterface_SDL {
public:
    explicit GameArtRenderer(SDL_Renderer* renderer) : RenderInterface_SDL(renderer) {}

    bool LoadArtwork()
    {
        for (size_t i = 0; i < artwork.size(); ++i) {
            VOBJECT_DESC descriptor{};
            descriptor.fCreateFlags = VOBJECT_CREATE_FROMFILE;
            descriptor.ImageFile = paths[i];
            artwork[i] = CreateVideoObject(descriptor);
            const size_t required = i == 2 ? 1 : i == 3 ? 8 : 9;
            if (!artwork[i] || artwork[i]->sprites.size() < required) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "RmlUi POC: cannot load Wizardry art: %s", paths[i]);
                return false;
            }
        }
        const auto fill_index = artwork[0]->sprites[8].paletteIndexAt(0, 0);
        if (!fill_index)
            return false;
        fill = artwork[0]->pPaletteEntry[*fill_index];
        for (size_t i = 0; i < 8; ++i) {
            const auto& sprite = artwork[3]->sprites[i];
            button_width = std::max(button_width, int(sprite.usWidth) + sprite.sOffsetX);
            button_height = std::max(button_height, int(sprite.usHeight) + sprite.sOffsetY);
        }
        return button_width > 0 && button_height > 0;
    }

    int ButtonWidth() const { return button_width; }
    int ButtonHeight() const { return button_height; }
    std::string FillColor() const
    {
        return "rgb(" + std::to_string(fill.peRed) + "," +
               std::to_string(fill.peGreen) + "," +
               std::to_string(fill.peBlue) + ")";
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override
    {
        // RmlUi may prepend the RML source directory to the virtual filename.
        const std::string_view filename(source);
        if (filename.find("wiz8-transparent.sti") != std::string_view::npos) {
            dimensions = {1, 1};
            constexpr std::array<Rml::byte, 4> transparent{};
            return GenerateTexture({transparent.data(), transparent.size()}, dimensions);
        }
        for (size_t i = 0; i < names.size(); ++i) {
            const std::string marker = "wiz8-" + std::string(names[i]) + "-";
            const auto begin = filename.rfind(marker);
            if (begin == std::string_view::npos)
                continue;
            const auto digit = begin + marker.size();
            if (digit >= filename.size() || !std::isdigit(static_cast<unsigned char>(filename[digit])))
                return {};
            const size_t frame = filename[digit] - '0';
            if (frame >= artwork[i]->sprites.size() || filename.substr(digit + 1, 4) != ".sti")
                return {};
            const auto& sprite = artwork[i]->sprites[frame];
            const int width = i == 3 ? button_width : int(sprite.usWidth) + std::max(0, int(sprite.sOffsetX));
            const int height = i == 3 ? button_height : int(sprite.usHeight) + std::max(0, int(sprite.sOffsetY));
            if (width <= 0 || height <= 0)
                return {};
            dimensions = {width, height};
            std::vector<Rml::byte> pixels(size_t(width) * height * 4, 0);
            for (int y = 0; y < sprite.usHeight; ++y) {
                for (int x = 0; x < sprite.usWidth; ++x) {
                    const auto color_index = sprite.pixels[size_t(y) * sprite.usWidth + x];
                    if (color_index == SpriteFrame::kTransparent)
                        continue;
                    const int dx = x + sprite.sOffsetX, dy = y + sprite.sOffsetY;
                    if (dx < 0 || dy < 0 || dx >= width || dy >= height)
                        continue;
                    const auto& color = artwork[i]->pPaletteEntry[color_index];
                    const size_t offset = (size_t(dy) * width + dx) * 4;
                    pixels[offset + 0] = color.peRed;
                    pixels[offset + 1] = color.peGreen;
                    pixels[offset + 2] = color.peBlue;
                    pixels[offset + 3] = 255;
                }
            }
            // Opaque palette entries need no premultiplication. Transparent
            // ETRLE runs remain RGBA zero for RmlUi's premultiplied blend.
            return GenerateTexture(pixels, dimensions);
        }
        return RenderInterface_SDL::LoadTexture(dimensions, source);
    }

private:
    inline static constexpr std::array<const char*, 4> paths{
        "Data\\Dialogs\\DialogBorder.STI",
        "Data\\Dialogs\\DialogEdge.STI",
        "Data\\Dialogs\\DialogBackground.sti",
        "Data\\Dialogs\\DialogConfirmation.STI"
    };
    inline static constexpr std::array<std::string_view, 4> names{
        "border", "edge", "background", "confirmation"
    };
    std::array<std::unique_ptr<SGPVObject>, 4> artwork;
    SGPPaletteEntry fill{};
    int button_width = 0, button_height = 0;
};

class GameSystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override { return double(SDL_GetTicksNS()) / 1'000'000'000.0; }

    // Keep the original game's cursor. The production integration should have
    // one owner for cursor appearance rather than competing SDL cursor states.
    void SetMouseCursor(const Rml::String&) override {}
};

struct Dialog {
    class ClickListener final : public Rml::EventListener {
    public:
        explicit ClickListener(Dialog& dialog, bool accepted)
            : dialog_(dialog), accepted_(accepted) {}

        void ProcessEvent(Rml::Event&) override {
            dialog_.accepted = accepted_;
            dialog_.finished = true;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "RmlUi POC: %s",
                        accepted_ ? "confirmed" : "cancelled");
        }

    private:
        Dialog& dialog_;
        bool accepted_;
    };

    GameSystemInterface system;
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer{nullptr, SDL_DestroyRenderer};
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> background{nullptr, SDL_DestroySurface};
    std::unique_ptr<GameArtRenderer> render_interface;
    Rml::Context* context = nullptr;
    ClickListener confirm{*this, true};
    ClickListener cancel{*this, false};
    bool initialized = false;
    bool finished = false;
    bool accepted = false;
};

std::unique_ptr<Dialog> dialog;

SDL_Surface* framebuffer()
{
    return GetFrameBufferSurface().cpu().surface.get();
}

void restore_background()
{
    if (!dialog || !dialog->background)
        return;
    SDL_Rect destination = dialog_bounds;
    SDL_BlitSurface(dialog->background.get(), &dialog_bounds, framebuffer(), &destination);
    InvalidateRegion(dialog_bounds.x, dialog_bounds.y,
                     dialog_bounds.x + dialog_bounds.w, dialog_bounds.y + dialog_bounds.h, 0);
}

void destroy_dialog()
{
    if (!dialog)
        return;
    restore_background();
    if (dialog->initialized)
        Rml::Shutdown();
    dialog.reset();
    Rml::SetRenderInterface(nullptr);
    Rml::SetSystemInterface(nullptr);
}

void draw_dialog_text()
{
    if (!dialog || !dialog->context || !g_dialog_interface_font)
        return;
    auto* document = dialog->context->GetDocument(0);
    auto* message = document ? document->GetElementById("message") : nullptr;
    if (!message)
        return;
    auto& font = *g_dialog_interface_font;
    const int x = int(message->GetAbsoluteLeft()) +
                  (int(message->GetClientWidth()) - font.textWidth(message_text)) / 2;
    const int y = int(message->GetAbsoluteTop()) +
                  (int(message->GetClientHeight()) - font.height()) / 2;
    FontRenderScope saved;
    SetFont(font);
    SetFontForeground(g_dialog_font_foreground);
    SetFontBackground(g_dialog_font_background);
    DrawFontText(x, y, message_text);
}

Rml::Input::KeyIdentifier key_identifier(SDL_Keycode key)
{
    using namespace Rml::Input;
    switch (key) {
    case SDLK_ESCAPE: return KI_ESCAPE;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return KI_RETURN;
    case SDLK_TAB: return KI_TAB;
    case SDLK_SPACE: return KI_SPACE;
    case SDLK_LEFT: return KI_LEFT;
    case SDLK_RIGHT: return KI_RIGHT;
    case SDLK_UP: return KI_UP;
    case SDLK_DOWN: return KI_DOWN;
    case SDLK_BACKSPACE: return KI_BACK;
    default: return KI_UNKNOWN;
    }
}

int modifiers()
{
    const auto state = SDL_GetModState();
    return ((state & SDL_KMOD_SHIFT) ? Rml::Input::KM_SHIFT : 0) |
           ((state & SDL_KMOD_CTRL) ? Rml::Input::KM_CTRL : 0) |
           ((state & SDL_KMOD_ALT) ? Rml::Input::KM_ALT : 0) |
           ((state & SDL_KMOD_GUI) ? Rml::Input::KM_META : 0);
}

void move_mouse(float x, float y)
{
    int width = 0, height = 0;
    if (!SDL_GetWindowSize(ghWindow, &width, &height) || width <= 0 || height <= 0)
        return;
    dialog->context->ProcessMouseMove(
        std::clamp(int(x * 640 / width), 0, 639),
        std::clamp(int(y * 480 / height), 0, 479), modifiers());
}

int mouse_button(Uint8 button)
{
    switch (button) {
    case SDL_BUTTON_LEFT: return 0;
    case SDL_BUTTON_RIGHT: return 1;
    case SDL_BUTTON_MIDDLE: return 2;
    default: return -1;
    }
}

} // namespace

bool active() noexcept { return bool(dialog); }

bool open()
{
    if (dialog)
        return true;
    if (!ghWindow)
        return false;

    dialog = std::make_unique<Dialog>();
    dialog->background.reset(SDL_DuplicateSurface(framebuffer()));
    dialog->renderer.reset(SDL_CreateSoftwareRenderer(framebuffer()));
    if (!dialog->background || !dialog->renderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RmlUi POC: SDL surface renderer: %s", SDL_GetError());
        destroy_dialog();
        return false;
    }
    SDL_SetSurfaceBlendMode(dialog->background.get(), SDL_BLENDMODE_NONE);

    dialog->render_interface = std::make_unique<GameArtRenderer>(dialog->renderer.get());
    if (!dialog->render_interface->LoadArtwork()) {
        destroy_dialog();
        return false;
    }
    if (!g_dialog_interface_font) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RmlUi POC: Wizardry dialog font not initialized");
        destroy_dialog();
        return false;
    }
    Rml::SetSystemInterface(&dialog->system);
    Rml::SetRenderInterface(dialog->render_interface.get());

    if (!Rml::Initialise()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RmlUi POC: initialization failed");
        destroy_dialog();
        return false;
    }
    dialog->initialized = true;

    dialog->context = Rml::CreateContext("whizzardry-poc", {640, 480});
    if (!dialog->context) {
        destroy_dialog();
        return false;
    }
    auto* document = dialog->context->LoadDocumentFromMemory(document_markup);
    if (!document || !document->GetElementById("accept") ||
        !document->GetElementById("cancel") || !document->GetElementById("actions") ||
        !document->GetElementById("message") || !document->GetElementById("dialog")) {
        destroy_dialog();
        return false;
    }
    // The original painter fills the dialog with the palette colour from
    // Border.STI frame 8, then tiles the Background.STI pattern over it.
    document->GetElementById("dialog")->SetProperty(
        "background-color", dialog->render_interface->FillColor());

    // The original message dialog places its two buttons with one
    // button-width of space between them, 15 pixels above the bottom.
    const int width = dialog->render_interface->ButtonWidth();
    const int height = dialog->render_interface->ButtonHeight();
    auto* actions = document->GetElementById("actions");
    actions->SetProperty("left", std::to_string((dialog_bounds.w - 3 * width) / 2 - border_inset) + "px");
    actions->SetProperty("width", std::to_string(3 * width) + "px");
    actions->SetProperty("height", std::to_string(height) + "px");
    for (const char* id : {"accept", "cancel"}) {
        auto* button = document->GetElementById(id);
        button->SetProperty("width", std::to_string(width) + "px");
        button->SetProperty("height", std::to_string(height) + "px");
    }
    document->GetElementById("accept")->AddEventListener("click", &dialog->confirm);
    document->GetElementById("cancel")->AddEventListener("click", &dialog->cancel);
    document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Auto);
    return true;
}

void close() { destroy_dialog(); }

bool handle_event(const SDL_Event& event)
{
    if (!dialog || SDL_GetWindowFromEvent(&event) != ghWindow)
        return false;
    auto* context = dialog->context;
    if (!context)
        return false;
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        move_mouse(event.motion.x, event.motion.y);
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        move_mouse(event.button.x, event.button.y);
        const int button = mouse_button(event.button.button);
        if (button >= 0) {
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                context->ProcessMouseButtonDown(button, modifiers());
            else
                context->ProcessMouseButtonUp(button, modifiers());
        }
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        context->ProcessMouseWheel(-event.wheel.y, modifiers());
        return true;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F9) {
            dialog->finished = true;
            return true;
        }
        const auto key = key_identifier(event.key.key);
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
            dialog->finished = true;
            return true;
        }
        if (event.type == SDL_EVENT_KEY_DOWN)
            context->ProcessKeyDown(key, modifiers());
        else
            context->ProcessKeyUp(key, modifiers());
        return true;
    }
    case SDL_EVENT_TEXT_INPUT:
        context->ProcessTextInput(event.text.text);
        return true;
    default:
        return false;
    }
}

bool frame()
{
    if (!dialog)
        return false;
    if (dialog->finished) {
        destroy_dialog();
        return false;
    }

    // Without a separate GPU UI pass, redraw only the small saved panel
    // rectangle before alpha blending. Otherwise it darkens each frame.
    restore_background();

    // The upstream SDL renderer's BeginFrame() clears the target. We must not
    // clear the game framebuffer, so configure its premultiplied blend mode here.
    const auto blend = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    SDL_SetRenderDrawBlendMode(dialog->renderer.get(), blend);
    dialog->context->Update();
    dialog->context->Render();
    SDL_RenderPresent(dialog->renderer.get());
    draw_dialog_text();
    return true;
}

} // namespace wiz8::ui_poc
