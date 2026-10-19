#pragma once

#include <memory>

#include "surrender/srNode.h"
#include "surrender/srTexture.h"

struct SDL_Surface;
class srGERD;

class stSurface2D : public srNode {
public:
    stSurface2D(std::shared_ptr<SDL_Surface> surface, srNode* parent);

    void traverse(TraverseInfo& info) override;
    void process(const ProcessInfo& info, e_processType type) override;
    void draw(srGERD* renderer);
    // The blurred-text option shifts sampling by half a source pixel.
    void setScale(float offset) { pixel_offset_ = offset; }
    void setAlphaTestEnabled(bool enabled);
    void updateRectangle(srGERD* renderer, int left, int top, int right, int bottom);

private:
    srTexture texture_;
    unsigned shader_bits_ = 0x100a017;
    float pixel_offset_ = 0;
};
