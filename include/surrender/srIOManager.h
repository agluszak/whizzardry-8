#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

// VTABLE: SURRENDER 0x10076960
// class srIOManager
class srIOManager {
public:
    class Error;
    class Importer;
    class Exporter;

    friend class Importer;
    friend class Exporter;

    srIOManager(const srIOManager&) = delete;
    srIOManager& operator=(const srIOManager&) = delete;
    SR_DLL_IMPORT void dump();
    SR_DLL_IMPORT std::string_view getExtension(std::string_view path);

protected:
    // FUNCTION: SURRENDER 0x1002C890
    srIOManager() = default;
    // FUNCTION: SURRENDER 0x1002C920
    virtual ~srIOManager() = default;

    SR_DLL_IMPORT void addImporter(Importer* importer, std::string_view extension);
    SR_DLL_IMPORT void addExporter(Exporter* exporter, std::string_view extension);
    SR_DLL_IMPORT Importer* findImporter(std::string_view extension);
    SR_DLL_IMPORT Exporter* findExporter(std::string_view extension);
    SR_DLL_IMPORT void removeImporter(Importer* importer);
    SR_DLL_IMPORT void removeExporter(Exporter* exporter);

private:
    // Registrations borrow their handlers. Earlier registrations take precedence;
    // each extension is owned here until its handler unregisters or the manager dies.
    std::vector<std::pair<std::string, Importer*>> importers;
    std::vector<std::pair<std::string, Exporter*>> exporters;
};

class srIOManager::Error {
public:
    // FUNCTION: SURRENDER 0x1002CB00
    // RECOMP: ??0Error@srIOManager@@QAE@PBD@Z
    explicit Error(std::string_view description) : description(description) {}
    SR_DLL_IMPORT std::string_view getDescription() const;

private:
    std::string description;
};

class srIOManager::Importer {
public:
    virtual const char* getTypeName() const = 0;

    // FUNCTION: SURRENDER 0x1002CC50
    // RECOMP: ??1Importer@srIOManager@@UAE@XZ
    virtual ~Importer() = default;

protected:
    SR_DLL_IMPORT void addToImporters(srIOManager* manager, std::string_view extension);
    SR_DLL_IMPORT void addToImporters(srIOManager* manager, srIOManager::Importer* importer,
                                      std::string_view extension);
    SR_DLL_IMPORT void removeFromImporters(srIOManager* manager);
};

class srIOManager::Exporter {
public:
    virtual const char* getTypeName() const = 0;

    // FUNCTION: SURRENDER 0x1002CC60
    // RECOMP: ??1Exporter@srIOManager@@UAE@XZ
    virtual ~Exporter() = default;

protected:
    SR_DLL_IMPORT void addToExporters(srIOManager* manager, std::string_view extension);
    SR_DLL_IMPORT void addToExporters(srIOManager* manager, srIOManager::Exporter* exporter,
                                      std::string_view extension);
    SR_DLL_IMPORT void removeFromExporters(srIOManager* manager);
};

W8_ABI_ASSERT(sizeof(srIOManager) == 0x1c, "srIOManager_must_be_0x1c");
W8_ABI_ASSERT(sizeof(srIOManager::Error) == 0x04, "srIOManager_Error_must_be_0x04");
W8_ABI_ASSERT(sizeof(srIOManager::Importer) == 0x04, "srIOManager_Importer_must_be_0x04");
W8_ABI_ASSERT(sizeof(srIOManager::Exporter) == 0x04, "srIOManager_Exporter_must_be_0x04");
