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
    SR_DLL_IMPORT srCore();
    ~srCore();

    SR_DLL_IMPORT void dump(std::ostream& stream);
    SR_DLL_IMPORT std::string_view getBuildTime() const;
    SR_DLL_IMPORT std::string_view getCopyright() const;
    SR_DLL_IMPORT std::string_view getVersion() const;
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
        return statistics_manager.get();
    }
    SR_DLL_IMPORT srColorSurfaceIFace* getSurface() const;
    SR_DLL_IMPORT srTexture* getTexture() const;
    // FUNCTION: SURRENDER 0x100156C0
    // RECOMP: ?getTimer@srCore@@QBEPAVsrVariableTimer@@XZ
    srClock* getTimer() const
    {
        return timer.get();
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
        return registry_.get();
    }

private:
    friend w8_long __cdecl srDebugPrintf(w8_ulong level, const char* format, ...);
    /* srInit/srExit drive library lifecycle: they write the private field
       block and run the private reset() directly. */
    friend SR_DLL_IMPORT int __cdecl srInit(void);
    friend SR_DLL_IMPORT int __cdecl srExit(void);

    SR_DLL_IMPORT void reset();

    static SR_DLL_IMPORT int initialized;

    // Declared first and reset last: registered resources need it during release.
    std::unique_ptr<srRegistry> registry_;
    std::unique_ptr<srClock> timer;
    srColorSurfaceIFace* surface = nullptr;
    srFilter* filter = nullptr;
    std::unique_ptr<srStatisticsManager> statistics_manager;
    srPalette* palette = nullptr;
    w8_ulong next_unique_id = 0;
    w8_ulong debug_level = 1;
    int multi_thread = 0;
    srNode* root_node = nullptr;
    srMaterial* material = nullptr;
    srTexture* texture = nullptr;
};

W8_ABI_ASSERT(sizeof(srCore) == 0x17c, "srCore_must_be_0x17c");

extern SR_DLL_IMPORT class srCore srCore;

SR_DLL_IMPORT int __cdecl srInit(void);
SR_DLL_IMPORT int __cdecl srExit(void);

/* Library initialization hook. */
void __cdecl _srLibraryInit(void);

extern const unsigned char srLogo[0x1000];
