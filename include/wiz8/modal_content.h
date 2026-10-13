#pragma once

struct SDL_Surface;

// The main screen owns a modal's lifetime. Content owns its input and painting;
// this interface only lets the screen drive and repaint the active content.
class ModalContent {
public:
    virtual ~ModalContent() = default;
    virtual bool ProcessInput() = 0;
    virtual void DrawModal(SDL_Surface& target) = 0;
    virtual void RequestModalRedraw() = 0;
};
