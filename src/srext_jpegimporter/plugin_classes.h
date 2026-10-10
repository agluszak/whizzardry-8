#pragma once

#include "surrender/srCore.h"
#include "surrender/srExporter.h"
#include "surrender/srPlugin.h"

class srJPEGImporter : public srSurfaceIOManager::SurfaceImporter,
                       public srSurfaceIOManager::SurfaceExporter {
public:
    srJPEGImporter();
    virtual ~srJPEGImporter();

    virtual const char* getTypeName() const;
    virtual int getSurfaceDesc(srColorSurfaceIFace::SurfaceDesc& description, srBinIStream& stream,
                               const srSurfaceIOManager::ImportInfo& options);
    virtual srColorSurfaceIFace* importSurface(srBinIStream& stream,
                                               const srSurfaceIOManager::ImportInfo& options);
    virtual void exportSurface(srBinOStream& stream, srColorSurfaceIFace& surface,
                               const srSurfaceIOManager::ExportInfo& options);
};

class srJPEGPlugin : public srPlugin {
public:
    virtual ~srJPEGPlugin();
    virtual const char* getDescription() const;

private:
    srJPEGImporter jpeg_importer_;
};
