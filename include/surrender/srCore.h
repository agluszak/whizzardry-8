#pragma once

#include <iosfwd>
#include <memory>
#include <string_view>

#include "srClock.h"
#include "srStatisticsManager.h"

class srColorSurfaceIFace;
class srFilter;
class srMaterial;
class srNode;
class srPalette;
class srRegistry;
class srTexture;

class srCore {
public:
    srCore();
    ~srCore();

    void dump(std::ostream& stream);
    std::string_view getBuildTime() const;
    std::string_view getCopyright() const;
    std::string_view getVersion() const;
    unsigned char getDebugLevel() const;
    srFilter* getFilter() const;
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
    w8_ulong getUniqueID();
    int isInitialized() const;
    void setDebugLevel(unsigned char level);
    void setFilter(srFilter* filter);
    int supportMultiThread();
    void supportMultiThread(int enabled);

    // FUNCTION: SURRENDER 0x10015760
    // RECOMP: ?getRegistry@srCore@@QBEPAVsrRegistry@@XZ
    srRegistry* getRegistry() const { return registry_.get(); }

private:
    friend w8_long srDebugPrintf(w8_ulong level, const char* format, ...);
    /* srInit/srExit drive library lifecycle: they write the private field
       block and run the private reset() directly. */
    friend int srInit(void);
    friend int srExit(void);

    void reset();

    static int initialized;

    // Declared first and reset last: registered resources need it during release.
    std::unique_ptr<srRegistry> registry_;
    std::unique_ptr<srClock> timer;
    std::shared_ptr<srColorSurfaceIFace> surface;
    srFilter* filter = nullptr;
    std::unique_ptr<srStatisticsManager> statistics_manager;
    std::shared_ptr<srPalette> palette;
    w8_ulong next_unique_id = 0;
    w8_ulong debug_level = 1;
    int multi_thread = 0;
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
