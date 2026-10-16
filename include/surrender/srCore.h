#pragma once

#include <memory>

struct SDL_Surface;
class srMaterial;
struct SDL_Palette;
class srTexture;

class srCore {
public:
    srCore();
    ~srCore();

    // FUNCTION: SURRENDER 0x10015730
    // RECOMP: ?getMaterial@srCore@@QBEPAVsrMaterial@@XZ
    srMaterial* getMaterial() const { return material.get(); }
    std::shared_ptr<SDL_Palette> getPalette() const;
    SDL_Surface* getSurface() const;
    srTexture* getTexture() const;
    int isInitialized() const;

private:
    /* srInit/srExit drive library lifecycle: they write the private field
       block and run the private reset() directly. */
    friend int srInit(void);
    friend int srExit(void);

    void reset();

    static int initialized;

    std::shared_ptr<SDL_Surface> surface;
    std::shared_ptr<SDL_Palette> palette;
    std::unique_ptr<srMaterial> material;
    std::unique_ptr<srTexture> texture;
};

extern class srCore srCore;

int srInit(void);
int srExit(void);

/* Library initialization hook. */

extern const unsigned char srLogo[0x1000];
