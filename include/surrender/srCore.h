#pragma once

#include <memory>

#include "srClock.h"
#include "srStatisticsManager.h"

class srColorSurfaceIFace;
class srFilter;
class srMaterial;
class srNode;
class srPalette;
class srTexture;

class srCore {
public:
    SR_DLL_IMPORT srCore();
    ~srCore();

    SR_DLL_IMPORT srFilter* getFilter() const;
    // FUNCTION: SURRENDER 0x10015730
    // RECOMP: ?getMaterial@srCore@@QBEPAVsrMaterial@@XZ
    srMaterial* getMaterial() const { return material.get(); }
    SR_DLL_IMPORT std::shared_ptr<srPalette> getPalette() const;
    SR_DLL_IMPORT srNode* getRootNode() const;
    // FUNCTION: SURRENDER 0x100156B0
    // RECOMP: ?getStatisticsManager@srCore@@QBEPAVsrStatisticsManager@@XZ
    srStatisticsManager* getStatisticsManager() const { return statistics_manager.get(); }
    SR_DLL_IMPORT srColorSurfaceIFace* getSurface() const;
    SR_DLL_IMPORT srTexture* getTexture() const;
    // FUNCTION: SURRENDER 0x100156C0
    // RECOMP: ?getTimer@srCore@@QBEPAVsrVariableTimer@@XZ
    srClock* getTimer() const { return timer.get(); }
    SR_DLL_IMPORT int isInitialized() const;
    SR_DLL_IMPORT void setFilter(srFilter* filter);

private:
    /* srInit/srExit drive library lifecycle: they write the private field
       block and run the private reset() directly. */
    friend SR_DLL_IMPORT int __cdecl srInit(void);
    friend SR_DLL_IMPORT int __cdecl srExit(void);

    SR_DLL_IMPORT void reset();

    static SR_DLL_IMPORT int initialized;

    std::unique_ptr<srClock> timer;
    std::shared_ptr<srColorSurfaceIFace> surface;
    srFilter* filter = nullptr;
    std::unique_ptr<srStatisticsManager> statistics_manager;
    std::shared_ptr<srPalette> palette;
    std::unique_ptr<srNode> root_node;
    std::unique_ptr<srMaterial> material;
    std::unique_ptr<srTexture> texture;
};

extern SR_DLL_IMPORT class srCore srCore;

SR_DLL_IMPORT int __cdecl srInit(void);
SR_DLL_IMPORT int __cdecl srExit(void);

/* Library initialization hook. */
void __cdecl _srLibraryInit(void);

extern const unsigned char srLogo[0x1000];
