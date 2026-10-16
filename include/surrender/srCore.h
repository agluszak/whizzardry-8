#pragma once

#include <memory>

#include "srClock.h"
#include "srStatisticsManager.h"

class srColorSurfaceIFace;
class srMaterial;
class srNode;
class srPalette;
class srTexture;

class srCore {
public:
    srCore();
    ~srCore();

    // FUNCTION: SURRENDER 0x10015730
    // RECOMP: ?getMaterial@srCore@@QBEPAVsrMaterial@@XZ
    srMaterial* getMaterial() const { return material.get(); }
    std::shared_ptr<srPalette> getPalette() const;
    srNode* getRootNode() const;
    // FUNCTION: SURRENDER 0x100156B0
    // RECOMP: ?getStatisticsManager@srCore@@QBEPAVsrStatisticsManager@@XZ
    srStatisticsManager* getStatisticsManager() const { return statistics_manager.get(); }
    srColorSurfaceIFace* getSurface() const;
    srTexture* getTexture() const;
    // FUNCTION: SURRENDER 0x100156C0
    // RECOMP: ?getTimer@srCore@@QBEPAVsrVariableTimer@@XZ
    srClock* getTimer() const { return timer.get(); }
    int isInitialized() const;

private:
    /* srInit/srExit drive library lifecycle: they write the private field
       block and run the private reset() directly. */
    friend int srInit(void);
    friend int srExit(void);

    void reset();

    static int initialized;

    std::unique_ptr<srClock> timer;
    std::shared_ptr<srColorSurfaceIFace> surface;
    std::unique_ptr<srStatisticsManager> statistics_manager;
    std::shared_ptr<srPalette> palette;
    std::unique_ptr<srNode> root_node;
    std::unique_ptr<srMaterial> material;
    std::unique_ptr<srTexture> texture;
};

extern class srCore srCore;

int srInit(void);
int srExit(void);

/* Library initialization hook. */
void _srLibraryInit(void);

extern const unsigned char srLogo[0x1000];
