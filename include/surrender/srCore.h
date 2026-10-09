#pragma once

#include <iosfwd>

#include "srFileManager.h"
#include "srGlobalRecycler.h"
#include "srHeap.h"
#include "srMemoryAllocator.h"
#include "srScheduler.h"
#include "srStatisticsManager.h"
#include "srVariableTimer.h"

class srColorSurfaceIFace;
class srFilter;
class srFStreamOpener;
class srHierarchyIOManager;
class srIStreamOpener;
class srMaterial;
class srModelIOManager;
class srNode;
class srPalette;
class srRegistry;
class srSurfaceIOManager;
class srTexture;
class srVideoManager;

class
#if defined(SURRENDER_BUILD)

#endif
    srCore {
public:
    srCore();

    void dump(std::ostream& stream);
    const char* getBuildTime() const;
    srSurfaceIOManager* getSurfaceIOManager() const;
    srIStreamOpener* getIStreamOpener() const;
    const char* getCopyright() const;
    const char* getVersion() const;
    unsigned char getDebugLevel() const;
    srFileManager* getFileManager() const;
    srFilter* getFilter() const;
    srGlobalRecycler* getGlobalRecycler() const;
    srHierarchyIOManager* getHierarchyIOManager() const;
    // FUNCTION: SURRENDER 0x10015730
    // RECOMP: ?getMaterial@srCore@@QBEPAVsrMaterial@@XZ
    srMaterial* getMaterial() const
    {
        return material;
    }
    srMemoryAllocator* getMemoryAllocator() const;
    srModelIOManager* getModelIOManager() const;
    srPalette* getPalette() const;
    srNode* getRootNode() const;
    // FUNCTION: SURRENDER 0x100156A0
    // RECOMP: ?getScheduler@srCore@@QBEPAVsrScheduler@@XZ
    srScheduler* getScheduler() const
    {
        return scheduler;
    }
    // FUNCTION: SURRENDER 0x100156B0
    // RECOMP: ?getStatisticsManager@srCore@@QBEPAVsrStatisticsManager@@XZ
    srStatisticsManager* getStatisticsManager() const
    {
        return statistics_manager;
    }
    srColorSurfaceIFace* getSurface() const;
    srTexture* getTexture() const;
    // FUNCTION: SURRENDER 0x100156C0
    // RECOMP: ?getTimer@srCore@@QBEPAVsrVariableTimer@@XZ
    srVariableTimer* getTimer() const
    {
        return timer;
    }
    w8_ulong getUniqueID();
    srVideoManager* getVideoManager() const;
    int isInitialized() const;
    void setDebugLevel(unsigned char level);
    void setFileManager(srFileManager* manager);
    void setFilter(srFilter* filter);
    int supportMultiThread();
    void supportMultiThread(int enabled);

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
    friend int __cdecl srInit(void);
    friend int __cdecl srExit(void);

    void reset();

    static int initialized;

    srScheduler* scheduler;
    srGlobalRecycler* global_recycler;
    srVariableTimer* timer;
    srColorSurfaceIFace* surface;
    srSurfaceIOManager* surface_io_manager;
    srIStreamOpener* stream_opener;
    srFStreamOpener* file_stream_opener;
    srFilter* filter;
    srMemoryAllocator* memory_allocator;
    srFileManager* file_manager;
    srStatisticsManager* statistics_manager;
    srRegistry* registry_;
    srFileManager* default_file_manager;
    srPalette* palette;
    w8_ulong next_unique_id;
    char version_[0x20];
    char copyright_[0x100];
    w8_ulong debug_level;
    int multi_thread;
    srNode* root_node;
    srModelIOManager* model_io_manager;
    srHierarchyIOManager* hierarchy_io_manager;
    srMaterial* material;
    srTexture* texture;
    srVideoManager* video_manager;
};

W8_ABI_ASSERT(sizeof(srCore) == 0x17c, "srCore_must_be_0x17c");

extern class srCore srCore;

int __cdecl srInit(void);
int __cdecl srExit(void);

/* DLL attach/detach hooks called by the library entry wrapper. */
void __cdecl _srLibraryInit(void);
void __cdecl _srLibraryExit(void);

extern const unsigned char srLogo[0x1000];
