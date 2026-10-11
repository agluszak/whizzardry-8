#include "wiz8/ui/rmlui_poc.h"

#include <SDL3/SDL.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <memory>

#include "RmlUi_Renderer_SDL.h"
#include "wiz8/engine_code/Video2.h"
#include "vsurface.h"

// An experimental, game-embedded RmlUi dialog. The framebuffer is an SDL CPU
// surface; the existing SurRender frame uploads it to SDL GPU. No second window
// or second GPU device is created. This is intentionally not a new UI layer.
namespace wiz8::ui_poc {
namespace {

constexpr SDL_Rect dialog_bounds{96, 80, 448, 336};
constexpr auto document_markup = R"rml(
<rml>
<head>
<title>RmlUi evaluation</title>
<style>
body {
    margin: 0;
    font-family: PocFont;
    color: #e9dcc2;
    font-size: 17px;
}
#dialog {
    position: absolute;
    left: 145px;
    top: 128px;
    width: 350px;
    padding: 20px;
    background-color: #20242d;
    border: 2px solid #aa9068;
}
h1 {
    font-size: 21px;
    margin: 0 0 12px 0;
    color: #f5d99a;
}
p { margin: 0 0 16px 0; }
#actions { display: flex; justify-content: flex-end; gap: 12px; }
button {
    min-width: 100px;
    padding: 8px 12px;
    background-color: #373e4c;
    border: 1px solid #bcaa86;
    color: #f5e9d0;
    font-family: PocFont;
    font-size: 16px;
}
button:hover { background-color: #596273; }
button:focus { border-color: #ffe1a1; }
button:active { background-color: #806643; }
</style>
</head>
<body>
<div id="dialog">
    <h1>RmlUi proof of concept</h1>
    <p>This dialog is laid out and driven by RmlUi, rendered through the existing game framebuffer.</p>
    <div id="actions">
        <button id="cancel">Cancel</button>
        <button id="accept" autofocus>Confirm</button>
    </div>
</div>
</body>
</rml>
)rml";

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
    std::unique_ptr<RenderInterface_SDL> render_interface;
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

bool load_font()
{
    const std::array<const char*, 5> candidates{
        std::getenv("WIZ8_RMLUI_FONT"),
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "C:/Windows/Fonts/arial.ttf"
    };
    for (const char* font : candidates) {
        if (font && std::filesystem::is_regular_file(font) &&
            Rml::LoadFontFace(font, "PocFont", Rml::Style::FontStyle::Normal))
            return true;
    }
    return false;
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

    dialog->render_interface = std::make_unique<RenderInterface_SDL>(dialog->renderer.get());
    Rml::SetSystemInterface(&dialog->system);
    Rml::SetRenderInterface(dialog->render_interface.get());

    if (!Rml::Initialise()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RmlUi POC: initialization failed");
        destroy_dialog();
        return false;
    }
    dialog->initialized = true;
    if (!load_font()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "RmlUi POC: set WIZ8_RMLUI_FONT to a TrueType font");
        destroy_dialog();
        return false;
    }

    dialog->context = Rml::CreateContext("whizzardry-poc", {640, 480});
    if (!dialog->context) {
        destroy_dialog();
        return false;
    }
    auto* document = dialog->context->LoadDocumentFromMemory(document_markup);
    if (!document || !document->GetElementById("accept") ||
        !document->GetElementById("cancel")) {
        destroy_dialog();
        return false;
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
    return true;
}

} // namespace wiz8::ui_poc
