#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <new>

#include "codec_adapter.h"
#include "plugin_classes.h"


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
    initializeCodecOptions();
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

// FUNCTION: SREXT_JPEGIMPORTER 0x10014E10
void srJPEGImporter::initializeCodecOptions()
{
    export_options_.limit = 200;
    export_options_.quality = 75;
    export_options_.smoothing_factor = 0;
    export_options_.pointer = 0;
}

// FUNCTION: SREXT_JPEGIMPORTER 0x10015420
const char* srJPEGImporter::getTypeName() const
{
    return "JPEG";
}

// FUNCTION: SREXT_JPEGIMPORTER 0x10014E30
bool srJPEGImporter::readHeader(void* input_cookie)
{
    memset(&codec_, 0, sizeof(codec_));
    codec_.input_stdio_cookie = input_cookie;
    srJPEG_read_header_adapter(&codec_);
    return codec_.failed == 0;
}

W8_ABI_ASSERT((sizeof(srJPEGImporter) == 0x44), "srJPEGImporter_must_be_0x44");
