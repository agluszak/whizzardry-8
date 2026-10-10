#include <algorithm>
#include <cctype>
#include <memory>

#include "surrender/srBinFStream.h"
#include "surrender/srCore.h"
#include "surrender/srDebug.h"
#include "surrender/srExporter.h"
#include "surrender/srIStreamOpener.h"
#include "surrender/srImporter.h"

namespace {
std::string upperExtension(std::string_view extension)
{
    std::string result(extension);
    for (char& character : result) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return result;
}
} // namespace

// FUNCTION: SURRENDER 0x1002CB10
std::string_view srIOManager::Error::getDescription() const
{
    return description;
}

// FUNCTION: SURRENDER 0x1002D1C0
std::string_view srIOManager::getExtension(std::string_view path)
{
    const auto delimiter = path.find_last_of("./\\");
    if (delimiter != std::string_view::npos && path[delimiter] == '.') {
        return path.substr(delimiter + 1);
    }
    return {};
}

// FUNCTION: SURRENDER 0x1002C9D0
srIOManager::Importer* srIOManager::findImporter(std::string_view extension)
{
    if (!extension.empty()) {
        const auto upper = upperExtension(extension);
        for (const auto& [registered_extension, importer] : importers) {
            if (registered_extension == upper) return importer;
        }
    }
    return nullptr;
}

// FUNCTION: SURRENDER 0x1002CB20
srIOManager::Exporter* srIOManager::findExporter(std::string_view extension)
{
    if (!extension.empty()) {
        const auto upper = upperExtension(extension);
        for (const auto& [registered_extension, exporter] : exporters) {
            if (registered_extension == upper) return exporter;
        }
    }
    return nullptr;
}

// FUNCTION: SURRENDER 0x1002CCC0
void srIOManager::addImporter(Importer* importer, std::string_view extension)
{
    if (importer != nullptr && !extension.empty()) {
        importers.emplace_back(upperExtension(extension), importer);
    }
}

// FUNCTION: SURRENDER 0x1002CE30
void srIOManager::addExporter(Exporter* exporter, std::string_view extension)
{
    if (exporter != nullptr && !extension.empty()) {
        exporters.emplace_back(upperExtension(extension), exporter);
    }
}

// FUNCTION: SURRENDER 0x1002CFA0
void srIOManager::removeImporter(Importer* importer)
{
    if (importer == 0) {
        return;
    }
    std::erase_if(importers, [importer](const auto& registration) {
        return registration.second == importer;
    });
}

// FUNCTION: SURRENDER 0x1002D090
void srIOManager::removeExporter(Exporter* exporter)
{
    if (exporter == 0) {
        return;
    }
    std::erase_if(exporters, [exporter](const auto& registration) {
        return registration.second == exporter;
    });
}

// FUNCTION: SURRENDER 0x1002D100
void srIOManager::dump()
{
    srPrintf("extension   importer\n");
    srPrintf("-----------------------------------------------------------------\n");
    for (const auto& [extension, importer] : importers) {
        srPrintf("%-8s    '%s'\n", extension.c_str(), importer->getTypeName());
    }
    srPrintf("-----------------------------------------------------------------\n");
    srPrintf("total %zu instances\n", importers.size());
    srPrintf("extension   exporter\n");
    srPrintf("-----------------------------------------------------------------\n");
    for (const auto& [extension, exporter] : exporters) {
        srPrintf("%-8s    '%s'\n", extension.c_str(), exporter->getTypeName());
    }
    srPrintf("-----------------------------------------------------------------\n");
    srPrintf("total %zu instances\n", exporters.size());
}

// FUNCTION: SURRENDER 0x1002D200
void srIOManager::Importer::addToImporters(srIOManager* manager, std::string_view extension)
{
    manager->addImporter(this, extension);
}

// FUNCTION: SURRENDER 0x1002D220
void srIOManager::Exporter::addToExporters(srIOManager* manager, std::string_view extension)
{
    manager->addExporter(this, extension);
}

// FUNCTION: SURRENDER 0x1002D240
void srIOManager::Importer::addToImporters(srIOManager* manager, srIOManager::Importer* importer,
                                           std::string_view extension)
{
    manager->addImporter(importer, extension);
}

// FUNCTION: SURRENDER 0x1002D260
void srIOManager::Exporter::addToExporters(srIOManager* manager, srIOManager::Exporter* exporter,
                                           std::string_view extension)
{
    manager->addExporter(exporter, extension);
}

// FUNCTION: SURRENDER 0x1002D280
void srIOManager::Importer::removeFromImporters(srIOManager* manager)
{
    manager->removeImporter(this);
}

// FUNCTION: SURRENDER 0x1002D290
void srIOManager::Exporter::removeFromExporters(srIOManager* manager)
{
    manager->removeExporter(this);
}

// FUNCTION: SURRENDER 0x1002D440
void srHierarchyIOManager::importHierarchy(const char* path, const ImportInfo& options)
{
    if (path != 0 && *path != '\0') {
        HierarchyImporter* importer =
            static_cast<HierarchyImporter*>(findImporter(getExtension(path)));
        if (importer == 0) {
            throw Error("srHierarchyIOManager::importHierarchy: Importer could not be found");
        }
        std::unique_ptr<srBinIStream> stream(srCore.getIStreamOpener()->open(path));
        if (stream != 0 && stream->good()) {
            importer->importHierarchy(*stream, options);
            return;
        }
        throw Error("srHierarchyIOManager::importHierarchy: File could not be opened");
    }
    throw Error("srHierarchyIOManager::importHierarchy: Given filename is NULL or empty");
}

// FUNCTION: SURRENDER 0x1002D540
void srHierarchyIOManager::exportHierarchy(const char* path, const ExportInfo& options)
{
    if (path == 0 || *path == '\0') {
        throw Error("srHierarchyIOManager::exportHierarchy: Given filename is NULL or empty");
    }
    HierarchyExporter* exporter = static_cast<HierarchyExporter*>(findExporter(getExtension(path)));
    if (exporter == 0) {
        throw Error("srHierarchyIOManager::exportHierarchy: Exporter could not be found");
    }
    srBinOFStream stream(path);
    if (stream.good()) {
        exporter->exportHierarchy(stream, options);
        return;
    }
    throw Error("srHierarchyIOManager::exportHierarchy: File could not be opened");
}

// FUNCTION: SURRENDER 0x1002D6A0
srModel* srModelIOManager::importModel(const char* path, const ImportInfo& options)
{
    if (path != 0 && *path != '\0') {
        ModelImporter* importer = static_cast<ModelImporter*>(findImporter(getExtension(path)));
        if (importer == 0) {
            throw Error("srModelIOManager::importModel: Importer could not be found");
        }
        std::unique_ptr<srBinIStream> stream(srCore.getIStreamOpener()->open(path));
        if (stream != 0 && stream->good()) {
            return importer->importModel(*stream, options);
        }
        throw Error("srModelIOManager::importModel: File could not be opened");
    }
    throw Error("srModelIOManager::import: Given filename is NULL or empty");
}

// FUNCTION: SURRENDER 0x1002D7A0
void srModelIOManager::exportModel(const char* path, srModel& model, const ExportInfo& options)
{
    if (path != 0 && *path != '\0') {
        ModelExporter* exporter = static_cast<ModelExporter*>(findExporter(getExtension(path)));
        if (exporter != 0) {
            srBinOFStream stream(path);
            if (stream.good()) {
                exporter->exportModel(stream, model, options);
                return;
            }
            throw Error("srModelIOManager::exportModel: File could not be opened");
        }
    }
    throw Error("srModelIOManager::exportModel: Exporter could not be found");
}

// FUNCTION: SURRENDER 0x1002D890
srSurfaceIOManager::SurfaceImporter* srSurfaceIOManager::getImporter(const char* path)
{
    if (path == 0) {
        return 0;
    }
    return static_cast<SurfaceImporter*>(findImporter(getExtension(path)));
}

// FUNCTION: SURRENDER 0x1002D8C0
srSurfaceIOManager::SurfaceExporter* srSurfaceIOManager::getExporter(const char* path)
{
    if (path == 0) {
        return 0;
    }
    return static_cast<SurfaceExporter*>(findExporter(getExtension(path)));
}

// FUNCTION: SURRENDER 0x1002D8F0
void srSurfaceIOManager::getSurfaceDesc(srColorSurfaceIFace::SurfaceDesc& description,
                                        const char* path, const ImportInfo& options)
{
    if (path != 0 && *path != '\0') {
        SurfaceImporter* importer = static_cast<SurfaceImporter*>(findImporter(getExtension(path)));
        if (importer != 0) {
            std::unique_ptr<srBinIStream> stream(srCore.getIStreamOpener()->open(path));
            if (stream != 0 && stream->good()) {
                if (importer->getSurfaceDesc(description, *stream, options) == 0) {
                    throw Error("srSurfaceIOManager::getSurfaceDesc() - Importer failed to "
                                "load surface!");
                }
                return;
            }
            throw Error("srSurfaceIOManager::getSurfaceDesc(): File could not be opened");
        }
    }
    throw Error("srSurfaceIOManager::getSurfaceDesc() - Importer for this file extension "
                "not found.");
}

// FUNCTION: SURRENDER 0x1002DD70
int srSurfaceIOManager::SurfaceImporter::getSurfaceDesc(
    srColorSurfaceIFace::SurfaceDesc& description, srBinIStream& stream,
    const srSurfaceIOManager::ImportInfo& options)
{
    srColorSurfaceIFace* surface = importSurface(stream, options);
    if (surface == 0) {
        return 0;
    }
    surface->getSurfaceDesc(description);
    surface->release();
    return 1;
}

// FUNCTION: SURRENDER 0x1002DB20
srColorSurfaceIFace* srSurfaceIOManager::importSurface(const char* path, srBinIStream& stream,
                                                       const ImportInfo& options)
{
    if (path != 0 && *path != '\0') {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-undefined-compare"
        if (&stream != 0 && stream.good()) {
#pragma clang diagnostic pop
            Importer* importer = findImporter(getExtension(path));
            if (importer != 0) {
                return static_cast<SurfaceImporter*>(importer)->importSurface(stream, options);
            }
            throw Error("srSurfaceIOManager::importSurface() - Importer for this file "
                        "extension not found");
        }
        throw Error("srSurfaceIOManager::importSurface() - corrupt input stream");
    }
    throw Error("srSurfaceIOManager::importSurface() - given filename is NULL or empty");
}

// FUNCTION: SURRENDER 0x1002DA00
srColorSurfaceIFace* srSurfaceIOManager::importSurface(const char* path, const ImportInfo& options)
{
    if (path != 0 && *path != '\0') {
        Importer* importer = findImporter(getExtension(path));
        if (importer == 0) {
            throw Error("srSurfaceIOManager::importSurface() - Importer for this file "
                        "extension not found");
        }
        std::unique_ptr<srBinIStream> stream(srCore.getIStreamOpener()->open(path));
        if (stream != 0 && stream->good()) {
            srColorSurfaceIFace* surface =
                static_cast<SurfaceImporter*>(importer)->importSurface(*stream, options);
            if (surface == 0) {
                throw Error("srSurfaceIOManager::importSurface() - Importer failed to "
                            "load surface!");
            }
            return surface;
        }
        throw Error("srSurfaceIOManager::importSurface: File could not be opened");
    }
    throw Error("srSurfaceIOManager::importSurface: Given filename is NULL or empty");
}

// FUNCTION: SURRENDER 0x1002DBC0
void srSurfaceIOManager::exportSurface(const char* path, srColorSurfaceIFace& surface,
                                       const ExportInfo& options)
{
    if (path != 0 && *path != '\0') {
        Exporter* exporter = findExporter(getExtension(path));
        if (exporter == 0) {
            throw Error("srSurfaceIOManager::exportSurface() - Exporter not found");
        }
        srBinOFStream stream(path);
        if (stream.good()) {
            static_cast<SurfaceExporter*>(exporter)->exportSurface(stream, surface, options);
            return;
        }
        throw Error("srSurfaceIOManager::exportSurface() - File could not be opened");
    }
    throw Error("srSurfaceIOManager::exportSurface() - Given filename is NULL or empty");
}

// FUNCTION: SURRENDER 0x1002DCD0
void srSurfaceIOManager::exportSurface(const char* path, srBinOStream& stream,
                                       srColorSurfaceIFace& surface, const ExportInfo& options)
{
    if (path != 0 && *path != '\0') {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-undefined-compare"
        if (&stream != 0 && stream.good()) {
#pragma clang diagnostic pop
            Exporter* exporter = findExporter(getExtension(path));
            if (exporter != 0) {
                static_cast<SurfaceExporter*>(exporter)->exportSurface(stream, surface, options);
                return;
            }
            throw Error("srSurfaceIOManager::exportSurface() - exporter not found");
        }
        throw Error("srSurfaceIOManager::exportSurface() - output stream is corrupt");
    }
    throw Error("srSurfaceIOManager::exportSurface() - given filename is NULL or empty");
}
