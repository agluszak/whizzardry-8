#include "plugin_classes.h"
#include "tga_import.h"

srJPEGPlugin::~srJPEGPlugin() {}

srTGAImporter::srTGAImporter()
{
    addToImporters(srCore.getSurfaceIOManager(), "tga");
}

srTGAImporter::~srTGAImporter()
{
    if (auto* manager = srCore.getSurfaceIOManager())
        removeFromImporters(manager);
}

const char* srTGAImporter::getTypeName() const { return "TGA"; }

srColorSurfaceIFace* srTGAImporter::importSurface(srBinIStream& stream,
                                               const srSurfaceIOManager::ImportInfo&)
{
    return srImage::loadTga(stream);
}

// FUNCTION: SREXT_JPEGIMPORTER 0x10014D40
srJPEGImporter::srJPEGImporter()
{
    srSurfaceIOManager* manager = srCore.getSurfaceIOManager();
    if (manager != 0) {
        addToImporters(manager, "jpg");
        addToImporters(manager, "jpeg");
        addToExporters(manager, "jpg");
        addToExporters(manager, "jpeg");
    }
}

// FUNCTION: SREXT_JPEGIMPORTER 0x10014DD0
srJPEGImporter::~srJPEGImporter()
{
    srSurfaceIOManager* manager = srCore.getSurfaceIOManager();
    if (manager != 0) {
        removeFromImporters(manager);
        removeFromExporters(manager);
    }
}

// FUNCTION: SREXT_JPEGIMPORTER 0x10015420
const char* srJPEGImporter::getTypeName() const
{
    return "JPEG";
}

// FUNCTION: SREXT_JPEGIMPORTER 0x10014BA0
const char* srJPEGPlugin::getDescription() const
{
    return "SurRender JPEG-importer/exporter plug-in";
}

// FUNCTION: SREXT_JPEGIMPORTER 0x100155B0

// FUNCTION: SREXT_JPEGIMPORTER 0x100155D0
extern "C" w8_ulong __cdecl srGetLibraryVersion()
{
    return 0x012A0209UL;
}

// FUNCTION: SREXT_JPEGIMPORTER 0x10014B70
extern "C" srPlugin* __cdecl srInitPlugin()
{
    return new srJPEGPlugin;
}
