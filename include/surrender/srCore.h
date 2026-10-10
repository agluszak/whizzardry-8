#pragma once

#include <iosfwd>

#include "srStatisticsManager.h"
#include "srVariableTimer.h"

class srColorSurfaceIFace;
class srFilter;
class srMaterial;
class srNode;
class srPalette;
class srRegistry;
class srTexture;

class srCore {
public:
    SR_DLL_IMPORT srCore();

    SR_DLL_IMPORT void dump(std::ostream& stream);
    SR_DLL_IMPORT const char* getBuildTime() const;
    SR_DLL_IMPORT const char* getCopyright() const;
    SR_DLL_IMPORT const char* getVersion() const;
    SR_DLL_IMPORT unsigned char getDebugLevel() const;
    SR_DLL_IMPORT srFilter* getFilter() const;
    // FUNCTION: SURRENDER 0x10015730
    // RECOMP: ?getMaterial@srCore@@QBEPAVsrMaterial@@XZ
    srMaterial* getMaterial() const
    {
        return material;
    }
    SR_DLL_IMPORT srPalette* getPalette() const;
    SR_DLL_IMPORT srNode* getRootNode() const;
    // FUNCTION: SURRENDER 0x100156B0
    // RECOMP: ?getStatisticsManager@srCore@@QBEPAVsrStatisticsManager@@XZ
    srStatisticsManager* getStatisticsManager() const
    {
        return statistics_manager;
    }
    SR_DLL_IMPORT srColorSurfaceIFace* getSurface() const;
    SR_DLL_IMPORT srTexture* getTexture() const;
    // FUNCTION: SURRENDER 0x100156C0
    // RECOMP: ?getTimer@srCore@@QBEPAVsrVariableTimer@@XZ
    srVariableTimer* getTimer() const
    {
        return timer;
    }
    SR_DLL_IMPORT w8_ulong getUniqueID();
    SR_DLL_IMPORT int isInitialized() const;
    SR_DLL_IMPORT void setDebugLevel(unsigned char level);
    SR_DLL_IMPORT void setFilter(srFilter* filter);
    SR_DLL_IMPORT int supportMultiThread();
    SR_DLL_IMPORT void supportMultiThread(int enabled);

    // FUNCTION: SURRENDER 0x10015760
    // RECOMP: ?getRegistry@srCore@@QBEPAVsrRegistry@@XZ
    srRegistry* getRegistry() const
    {
        return registry_;
    }

private:
    friend w8_long __cdecl srDebugPrintf(w8_ulong level, const char* format, ...);
    /* srInit/srExit drive library lifecycle: they write the private field
       block and run the private reset() directly. */
    friend SR_DLL_IMPORT int __cdecl srInit(void);
    friend SR_DLL_IMPORT int __cdecl srExit(void);

    SR_DLL_IMPORT void reset();

    static SR_DLL_IMPORT int initialized;

    srVariableTimer* timer;
    srColorSurfaceIFace* surface;
    srFilter* filter;
    srStatisticsManager* statistics_manager;
    srRegistry* registry_;
    srPalette* palette;
    w8_ulong next_unique_id;
    char version_[0x20];
    char copyright_[0x100];
    w8_ulong debug_level;
    int multi_thread;
    srNode* root_node;
    srMaterial* material;
    srTexture* texture;
};

W8_ABI_ASSERT(sizeof(srCore) == 0x17c, "srCore_must_be_0x17c");

extern SR_DLL_IMPORT class srCore srCore;

SR_DLL_IMPORT int __cdecl srInit(void);
SR_DLL_IMPORT int __cdecl srExit(void);

/* Library initialization hook. */
void __cdecl _srLibraryInit(void);

extern const unsigned char srLogo[0x1000];
