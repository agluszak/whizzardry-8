#include "plugin_classes.h"
#include "tga_import.h"

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
