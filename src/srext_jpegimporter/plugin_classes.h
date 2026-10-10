#pragma once

#include "surrender/srCore.h"
#include "surrender/srExporter.h"

class srJPEGImporter : public srSurfaceIOManager::SurfaceImporter,
                       public srSurfaceIOManager::SurfaceExporter {
public:
    srJPEGImporter();
    virtual ~srJPEGImporter();

    const char* getTypeName() const override;
    int getSurfaceDesc(srColorSurfaceIFace::SurfaceDesc& description, srBinIStream& stream,
                       const srSurfaceIOManager::ImportInfo& options) override;
    srColorSurfaceIFace* importSurface(srBinIStream& stream,
                                       const srSurfaceIOManager::ImportInfo& options) override;
    void exportSurface(srBinOStream& stream, srColorSurfaceIFace& surface,
                       const srSurfaceIOManager::ExportInfo& options) override;
};

class srTGAImporter : public srSurfaceIOManager::SurfaceImporter {
public:
    srTGAImporter();
    ~srTGAImporter() override;
    const char* getTypeName() const override;
    srColorSurfaceIFace* importSurface(srBinIStream& stream,
                                       const srSurfaceIOManager::ImportInfo& options) override;
};
