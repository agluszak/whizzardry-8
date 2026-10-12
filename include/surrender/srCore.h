#pragma once

#include <cstdint>
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
    SR_DLL_IMPORT std::uint8_t getDebugLevel() const;
    SR_DLL_IMPORT srFilter* getFilter() const;
    // FUNCTION: SURRENDER 0x10015730
    // RECOMP: ?getMaterial@srCore@@QBEPAVsrMaterial@@XZ
    srMaterial* getMaterial() const
    {
        return material.get();
    }
    SR_DLL_IMPORT std::shared_ptr<srPalette> getPalette() const;
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
    SR_DLL_IMPORT std::uint32_t getUniqueID();
    SR_DLL_IMPORT std::int32_t isInitialized() const;
    SR_DLL_IMPORT void setDebugLevel(std::uint8_t level);
    SR_DLL_IMPORT void setFilter(srFilter* filter);
    SR_DLL_IMPORT std::int32_t supportMultiThread();
    SR_DLL_IMPORT void supportMultiThread(std::int32_t enabled);

    // FUNCTION: SURRENDER 0x10015760
    // RECOMP: ?getRegistry@srCore@@QBEPAVsrRegistry@@XZ
    srRegistry* getRegistry() const
    {
        return registry_.get();
    }

private:
    friend std::int32_t __cdecl srDebugPrintf(std::uint32_t level, const char* format, ...);
    /* srInit/srExit drive library lifecycle: they write the private field
       block and run the private reset() directly. */
    friend SR_DLL_IMPORT std::int32_t __cdecl srInit(void);
    friend SR_DLL_IMPORT std::int32_t __cdecl srExit(void);

    SR_DLL_IMPORT void reset();

    static SR_DLL_IMPORT std::int32_t initialized;

    // Declared first and reset last: registered resources need it during release.
    std::unique_ptr<srRegistry> registry_;
    std::unique_ptr<srClock> timer;
    std::shared_ptr<srColorSurfaceIFace> surface;
    srFilter* filter = nullptr;
    std::unique_ptr<srStatisticsManager> statistics_manager;
    std::shared_ptr<srPalette> palette;
    std::uint32_t next_unique_id = 0;
    std::uint32_t debug_level = 1;
    std::int32_t multi_thread = 0;
    std::unique_ptr<srNode> root_node;
    std::unique_ptr<srMaterial> material;
    std::unique_ptr<srTexture> texture;
};

extern SR_DLL_IMPORT class srCore srCore;

SR_DLL_IMPORT std::int32_t __cdecl srInit(void);
SR_DLL_IMPORT std::int32_t __cdecl srExit(void);

/* Library initialization hook. */
void __cdecl _srLibraryInit(void);

extern const std::uint8_t srLogo[0x1000];
