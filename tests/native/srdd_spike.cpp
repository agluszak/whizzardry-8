// Feasibility spike: srGERD renders a textured, vertex-coloured triangle through
// the SDL3 GPU device, then reads the frame back through lockBuffer.
//
// Usage: srdd_spike [frames] [screenshot.ppm]
#include "surrender/srColorSurface.h"
#include "surrender/srCore.h"
#include "surrender/srDD_SDLGPU.h"
#include "surrender/srGERD.h"
#include "surrender/srTextureMap.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

namespace {
const int WIDTH = 640;
const int HEIGHT = 480;

int fail(const char* step, srGERD* gerd)
{
    fprintf(stderr, "%s failed: %s\n", step, gerd != 0 ? gerd->getErrorString(gerd->getError()) : SDL_GetError());
    return 1;
}

void writePpm(const char* path, const std::vector<w8_ulong>& pixels)
{
    FILE* file = fopen(path, "wb");
    if (file == 0) {
        return;
    }
    fprintf(file, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    for (size_t index = 0; index < pixels.size(); ++index) {
        const unsigned char rgb[3] = {(unsigned char)(pixels[index] >> 16),
                                      (unsigned char)(pixels[index] >> 8),
                                      (unsigned char)pixels[index]};
        fwrite(rgb, 1, 3, file);
    }
    fclose(file);
}
} // namespace

int main(int argc, char** argv)
{
    const int frames = argc > 1 ? atoi(argv[1]) : 180;
    const char* screenshot = argc > 2 ? argv[2] : 0;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return fail("SDL_Init", 0);
    }
    SDL_Window* window = SDL_CreateWindow("srGERD on SDL GPU", WIDTH, HEIGHT, 0);
    if (window == 0) {
        return fail("SDL_CreateWindow", 0);
    }

    if (srInit() == 0) {
        return fail("srInit", 0);
    }
    srGERD* gerd = new srGERD(srCreateSDLGPUDevice(), 0, "SDLGPU");
    if (gerd->createContext(reinterpret_cast<w8_ulong_ptr>(window)) != srGERD::ERROR_NONE) {
        return fail("createContext", gerd);
    }
    if (gerd->openWindow(WIDTH, HEIGHT) != srGERD::ERROR_NONE) {
        return fail("openWindow", gerd);
    }

    /* 64x64 checkerboard: orange and blue 8-pixel squares. */
    srColorSurface* surface = new srColorSurface(srPixelConvert::SURFACE_ARGB32, 64, 64);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            surface->setPixel(x, y, ((x / 8 + y / 8) & 1) != 0 ? 0xffff8000u : 0xff2040ffu);
        }
    }
    srTextureMap* texture = new srTextureMap(surface);

    /* The textured shader: retail default plus TEXTURING_ENABLE. */
    srShader shader;
    shader.value |= srShader::MASK_TEXTURING;

    const float corners[3][2] = {{0.0f, 0.9f}, {-0.9f, -0.7f}, {0.9f, -0.7f}};
    const float colors[3][4] = {{1, 1, 1, 1}, {1, 0.4f, 0.4f, 1}, {0.4f, 1, 0.4f, 1}};
    const float texcoords[3][2] = {{0.5f, 0.0f}, {0.0f, 2.0f}, {2.0f, 2.0f}};
    float positions[3][4];
    std::vector<w8_ulong> pixels;

    bool running = true;
    for (int frame = 0; running && frame < frames; ++frame) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }
        /* srGERD hands the device eye-space positions; spin about the view axis. */
        const float angle = frame * 0.02f;
        for (int vertex = 0; vertex < 3; ++vertex) {
            positions[vertex][0] =
                corners[vertex][0] * cosf(angle) - corners[vertex][1] * sinf(angle);
            positions[vertex][1] =
                corners[vertex][0] * sinf(angle) + corners[vertex][1] * cosf(angle);
            positions[vertex][2] = -2.0f;
            positions[vertex][3] = 1.0f;
        }

        if (gerd->beginFrame() != srGERD::ERROR_NONE) {
            return fail("beginFrame", gerd);
        }
        gerd->setClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        gerd->clear(srFlags<srGERD::e_buffer>(srGERD::BUFFER_COLOR | srGERD::BUFFER_DEPTH));
        gerd->matrixMode(srGERD::MATRIX_PROJECTION);
        gerd->loadIdentity();
        /* srGERD::perspective takes the vertical field of view in radians. */
        gerd->perspective(60.0 * M_PI / 180.0, (double)WIDTH / HEIGHT, 0.1, 100.0);
        gerd->matrixMode(srGERD::MATRIX_MODELVIEW);
        gerd->loadIdentity();
        gerd->setShader(shader);
        gerd->setTexture(texture, 0);
        gerd->setVertexPointer(4, srRendererDefs::TYPE_FLOAT, 16, positions, 3);
        gerd->setDiffusePointer(4, srRendererDefs::TYPE_FLOAT, 16, colors);
        gerd->setTexCoordPointer(2, srRendererDefs::TYPE_FLOAT, 8, texcoords, 0);
        gerd->setVertexArrayMask(srFlags<srRendererDefs::e_vertexArray>(
            1u << srRendererDefs::VERTEX_ARRAY_POSITIONS | 1u << srRendererDefs::VERTEX_ARRAY_DIFFUSE |
            1u << srRendererDefs::VERTEX_ARRAY_TEXCOORD0));
        gerd->drawArrays(srRendererDefs::PRIMITIVE_TRIANGLES, 0, 3);
        gerd->endFrame();

        if (frame == frames - 1) {
            srColorSurfaceIFace* buffer = gerd->lockBuffer();
            if (buffer == 0) {
                return fail("lockBuffer", gerd);
            }
            pixels.assign(WIDTH * HEIGHT, 0);
            for (int y = 0; y < HEIGHT; ++y) {
                buffer->getPixelRow(&pixels[y * WIDTH], y, 0, WIDTH);
            }
            gerd->unlockBuffer();
        }
        gerd->flipFrame();
    }

    int status = 0;
    if (!pixels.empty()) {
        /* The background corner keeps the clear colour; the centre is textured. */
        const w8_ulong corner = pixels[0] & 0xffffff;
        const w8_ulong centre = pixels[(HEIGHT / 2) * WIDTH + WIDTH / 2] & 0xffffff;
        int textured = 0;
        for (size_t index = 0; index < pixels.size(); ++index) {
            const w8_ulong pixel = pixels[index];
            const int red = (pixel >> 16) & 0xff;
            const int blue = pixel & 0xff;
            textured += red > 0xc0 || blue > 0xc0;
        }
        printf("corner %06x centre %06x textured pixels %d\n", corner, centre, textured);
        const int corner_red = (corner >> 16) & 0xff;
        const int corner_blue = corner & 0xff;
        if (abs(corner_red - 0x1a) > 1 || abs(corner_blue - 0x26) > 1) {
            printf("unexpected clear colour\n");
            status = 1;
        }
        if (centre == corner || textured < 10000) {
            printf("triangle missing\n");
            status = 1;
        }
        if (screenshot != 0) {
            writePpm(screenshot, pixels);
        }
    }

    texture->release();
    gerd->deleteContext();
    srExit();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return status;
}
