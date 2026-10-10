#include "wiz8/utility.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/LevelFile.h"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/engine_code/OctMeshModel.h"
#include "wiz8/sr_api.h"
#include "wiz8/local_screens/AutomapScreen.h"
#include "wiz8/float_constants.h"

#include "wiz8/filesystem.h"

#include <stdio.h>
#include <stdlib.h>
#include <memory>
#include <stdexcept>
#include <climits>
#include <cmath>
#include <utility>
#include <string.h>

#define LEVELFILE_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\LevelFile.cpp"

// GLOBAL: WIZ8 0x006833fc
static W8LevelFile* g_level_file;

/* Scratch message buffer for the mesh/anim-object load failures; original name
   unknown. The next defined symbol is g_level_file at 0x006833FC, so the
   buffer is at most 0x404 bytes; 0x400 matches this file's other 0x400 sprintf
   buffers. */
// GLOBAL: WIZ8 0x00682ff8
static char g_level_file_error[0x400];

namespace {

class LevelRegistryTransaction {
  public:
    LevelRegistryTransaction() : level_(g_level_file)
    {
        if (level_) {
            switches_ = level_->num_switch_triggers;
            planes_ = level_->num_invisible_planes;
            linked_ = level_->num_linked_records;
        }
    }

    ~LevelRegistryTransaction()
    {
        if (level_) {
            while (level_->num_switch_triggers > switches_) {
                level_->switch_triggers[--level_->num_switch_triggers] = nullptr;
            }
            while (level_->num_invisible_planes > planes_) {
                level_->invisible_planes[--level_->num_invisible_planes] = nullptr;
            }
            while (level_->num_linked_records > linked_) {
                level_->linked_records[--level_->num_linked_records] = nullptr;
            }
        }
    }

    void commit() { level_ = nullptr; }

  private:
    W8LevelFile* level_;
    int switches_ = 0;
    int planes_ = 0;
    int linked_ = 0;
};

void ValidateRecordCount(std::int64_t count, std::size_t stride, std::int64_t remaining)
{
    if (count < 0 || remaining < 0 || count > remaining ||
        static_cast<std::uint64_t>(count) > static_cast<std::uint64_t>(INT_MAX) / stride) {
        throw std::runtime_error("Level file: Invalid record count.");
    }
}

void ReleaseLevelRecord(W8LevelFilePathAI& record)
{
    free(record.pPaths);
    free(record.pScaledPaths);
    record.pPaths = nullptr;
    record.pScaledPaths = nullptr;
}

void ReleaseLevelRecord(W8LevelFileDoorRef& record)
{
    free(record.door);
    record.door = nullptr;
}

void ReleaseLevelRecord(W8LevelFileMesh& record)
{
    for (int index = 0; index < record.num_lods; ++index) {
        if (record.lods)
            free(record.lods[index]);
        if (record.lod_shorts)
            free(record.lod_shorts[index]);
    }
    free(record.lods);
    free(record.lod_shorts);
    free(record.pstVertices);
    free(record.pstCompFaces);
    free(record.pstFaces);
    record.lods = nullptr;
    record.lod_shorts = nullptr;
    record.pstVertices = nullptr;
    record.pstCompFaces = nullptr;
    record.pstFaces = nullptr;
}

void ReleaseLevelRecord(W8LevelFileLight& record)
{
    if (record.pPathAI)
        ReleaseLevelRecord(*record.pPathAI);
    free(record.pPathAI);
    free(record.pExtra);
    record.pPathAI = nullptr;
    record.pExtra = nullptr;
}

void ReleaseLevelRecord(W8LevelFileAnimLight& record)
{
    free(record.pExtra);
    record.pExtra = nullptr;
}

void ReleaseLevelRecord(W8LevelFileTrigger& record)
{
    if (record.pData == nullptr)
        return;
    switch (record.type) {
    case 1:
        ReleaseLevelRecord(static_cast<W8LevelFileSwitch*>(record.pData)->door);
        break;
    case 2: {
        auto& invisible = *static_cast<W8LevelFileInvisible*>(record.pData);
        free(invisible.pPlane);
        free(invisible.pRecord);
        break;
    }
    case 4: {
        auto& super = *static_cast<W8LevelFileSuperTrigger*>(record.pData);
        free(super.pPosition);
        free(super.pPlane);
        free(super.pHotSpot);
        ReleaseLevelRecord(super.door);
        free(super.pRecord);
        break;
    }
    }
    free(record.pData);
    record.pData = nullptr;
}

void ReleaseLevelFrames(W8LevelFileFrame* frames, int count)
{
    if (frames == nullptr)
        return;
    for (int index = 0; index < count; ++index) {
        ReleaseLevelRecord(frames[index].mesh);
        free(frames[index].pTextures);
    }
    free(frames);
}

void ReleaseLevelRecord(W8LevelFileAnimObj& record)
{
    if (record.pAnimLights) {
        for (int index = 0; index < record.num_anim_lights; ++index) {
            ReleaseLevelRecord(record.pAnimLights[index]);
        }
    }
    if (record.pMorphs) {
        for (int index = 0; index < record.num_anims; ++index) {
            auto& morph = record.pMorphs[index];
            ReleaseLevelFrames(morph.LODMesh.pFrames, morph.num_frames);
        }
    }
    if (record.pTransforms) {
        for (int index = 0; index < record.num_transforms; ++index) {
            auto& transform = record.pTransforms[index];
            ReleaseLevelFrames(transform.LODMesh.pFrames, transform.num_frames);
            ReleaseLevelRecord(transform.pathAI);
        }
    }
    if (record.pPathAI)
        ReleaseLevelRecord(*record.pPathAI);
    free(record.pPathAI);
    free(record.pAnimLights);
    free(record.pBoundBox);
    free(record.abHowMany);
    free(record.pMorphs);
    free(record.pTransforms);
    record.pPathAI = nullptr;
    record.pAnimLights = nullptr;
    record.pBoundBox = nullptr;
    record.abHowMany = nullptr;
    record.pMorphs = nullptr;
    record.pTransforms = nullptr;
}

void ReleaseLevelProps(W8LevelFileProp* props, int count)
{
    if (props == nullptr)
        return;
    for (int index = 0; index < count; ++index) {
        auto& prop = props[index];
        ReleaseLevelRecord(prop.anim_obj);
        if (prop.pTrigger)
            ReleaseLevelRecord(*prop.pTrigger);
        free(prop.pTrigger);
        free(prop.usFrame_Pos);
    }
    free(props);
}

void ReleaseLevelFile(W8LevelFile* level)
{
    if (level == nullptr)
        return;
    if (g_level_file == level)
        g_level_file = nullptr;
    if (level->pMeshes)
        ReleaseLevelRecord(*level->pMeshes);
    if (level->pLights) {
        for (int index = 0; index < level->nLights; ++index) {
            ReleaseLevelRecord(level->pLights[index]);
        }
    }
    if (level->pMonsters) {
        for (int index = 0; index < level->nMonsters; ++index)
            free(level->pMonsters[index].MonPath);
    }
    if (level->pCameras) {
        for (int index = 0; index < level->nCameras; ++index)
            ReleaseLevelRecord(level->pCameras[index].pathAI);
    }
    if (level->pTriggers) {
        for (int index = 0; index < level->nTriggers; ++index)
            ReleaseLevelRecord(level->pTriggers[index]);
    }
    ReleaseLevelProps(level->pProps, level->nProps);
    ReleaseLevelProps(level->pBitmaps, level->nBitmaps);
    delete[] level->pModels;
    free(level->pMeshes);
    free(level->pTextures);
    free(level->pLights);
    free(level->pMonsters);
    free(level->pItems);
    free(level->pCameras);
    free(level->pTriggers);
    free(level->pClippingPlanes);
    free(level->pParticleSystems);
    free(level->pNamedPositions);
    free(level->automap_nodes);
    free(level);
}

} // namespace

static unsigned char ReadMaterialRecord(wiz8::File* file, W8MaterialRecord* material)
try {
    file->read_exact(material, 0x11a);
    if (material->version >= 4) {
        file->read_exact(material->texture_modes, 0x10);
    }
    return TRUE;
} catch (const std::exception&) {
    return false;
}

static unsigned char WriteMaterialRecord(wiz8::File* file, W8MaterialRecord* material)
{
    file->write(material, 0x11a);
    if (material->version >= 4) {
        file->write(material->texture_modes, 0x10);
    }
    return TRUE;
}

// FUNCTION: WIZ8 0x004CFDC0
W8LevelFile* ReadLevelFile(wiz8::File* hFile)
try {
    W8LevelFile* pLevel = static_cast<W8LevelFile*>(calloc(1, sizeof(W8LevelFile)));
    if (pLevel == 0) {
        throw std::bad_alloc();
    }

    pLevel->submesh_count = 1;
    pLevel->mesh_count = 1;
    pLevel->num_switch_triggers = 0;

    pLevel->num_invisible_planes = 0;

    pLevel->num_linked_records = 0;

    W8LevelFile* previous_level = g_level_file;
    const auto cleanup = [previous_level](W8LevelFile* record) {
        ReleaseLevelFile(record);
        g_level_file = previous_level;
    };
    std::unique_ptr<W8LevelFile, decltype(cleanup)> owner(pLevel, cleanup);
    g_level_file = pLevel;

    pLevel->pMeshes = static_cast<W8LevelFileMesh*>(calloc(1, sizeof(W8LevelFileMesh)));
    if (pLevel->pMeshes == 0) {
        throw std::bad_alloc();
    }

    if (!ReadMeshFile(hFile, pLevel->pMeshes))
        return nullptr;

    hFile->read_exact(&pLevel->nTextures, sizeof(pLevel->nTextures));
    ValidateRecordCount(pLevel->nTextures, sizeof(W8MaterialRecord), hFile->size() - hFile->tell());
    if (pLevel->nTextures == 0) {
        return 0;
    }
    pLevel->pTextures =
        static_cast<W8MaterialRecord*>(calloc(1, pLevel->nTextures * sizeof(W8MaterialRecord)));
    if (pLevel->pTextures == 0) {
        throw std::bad_alloc();
    }

    int i;
    for (i = 0; i < pLevel->nTextures; ++i) {
        W8MaterialRecord* pTexture = pLevel->pTextures + i;
        if (!ReadMaterialRecord(hFile, pTexture))
            return nullptr;
    }

    hFile->read_exact(&pLevel->nLights, sizeof(pLevel->nLights));
    ValidateRecordCount(pLevel->nLights, sizeof(W8LevelFileLight), hFile->size() - hFile->tell());
    if (pLevel->nLights != 0) {
        pLevel->pLights =
            static_cast<W8LevelFileLight*>(calloc(1, pLevel->nLights * sizeof(W8LevelFileLight)));
        if (pLevel->pLights == 0) {
            throw std::bad_alloc();
        }

        for (i = 0; i < pLevel->nLights; ++i) {
            if (ReadLightFile(hFile, pLevel->pLights + i) == 0) {
                return 0;
            }
        }
    }

    hFile->read_exact(&pLevel->nMonsters, sizeof(pLevel->nMonsters));
    ValidateRecordCount(pLevel->nMonsters, sizeof(W8LevelFileMonster),
                        hFile->size() - hFile->tell());
    if (pLevel->nMonsters != 0) {
        pLevel->pMonsters = static_cast<W8LevelFileMonster*>(
            calloc(1, pLevel->nMonsters * sizeof(W8LevelFileMonster)));
        if (pLevel->pMonsters == 0) {
            throw std::bad_alloc();
        }

        for (i = 0; i < pLevel->nMonsters; ++i) {
            W8LevelFileMonster* pMonster = pLevel->pMonsters + i;
            hFile->read_exact(pMonster, 0x22);
            ValidateRecordCount(pMonster->num_mon_path, sizeof(W8LevelFilePathNode),
                                hFile->size() - hFile->tell());

            if (pMonster->num_mon_path != 0) {
                pMonster->MonPath = static_cast<W8LevelFilePathNode*>(
                    calloc(1, pMonster->num_mon_path * sizeof(W8LevelFilePathNode)));
                if (pMonster->MonPath == 0) {
                    throw std::bad_alloc();
                }
                memset(pMonster->MonPath, 0, pMonster->num_mon_path * sizeof(W8LevelFilePathNode));
                hFile->read_exact(pMonster->MonPath,
                                  pMonster->num_mon_path * sizeof(W8LevelFilePathNode));
            }
        }
    }

    hFile->read_exact(&pLevel->nItems, sizeof(pLevel->nItems));
    ValidateRecordCount(pLevel->nItems, sizeof(W8LevelFileItemRecord),
                        hFile->size() - hFile->tell());
    if (pLevel->nItems != 0) {
        pLevel->pItems = static_cast<W8LevelFileItemRecord*>(
            calloc(1, pLevel->nItems * sizeof(W8LevelFileItemRecord)));
        if (pLevel->pItems == 0) {
            throw std::bad_alloc();
        }

        hFile->read_exact(pLevel->pItems, pLevel->nItems * sizeof(W8LevelFileItemRecord));
    }

    hFile->read_exact(&pLevel->missile_count, sizeof(pLevel->missile_count));
    ValidateRecordCount(pLevel->missile_count, 1, hFile->size() - hFile->tell());
    hFile->read_exact(&pLevel->nProps, sizeof(pLevel->nProps));
    ValidateRecordCount(pLevel->nProps, sizeof(W8LevelFileProp), hFile->size() - hFile->tell());
    if (pLevel->nProps != 0) {
        pLevel->pProps = ReadPropsFile(hFile, pLevel->nProps);
        if (pLevel->pProps == 0) {
            throw std::bad_alloc();
        }
    }
    hFile->read_exact(&pLevel->nBitmaps, sizeof(pLevel->nBitmaps));
    ValidateRecordCount(pLevel->nBitmaps, sizeof(W8LevelFileProp), hFile->size() - hFile->tell());
    if (pLevel->nBitmaps != 0) {
        pLevel->pBitmaps = ReadPropsFile(hFile, pLevel->nBitmaps);
        if (pLevel->pBitmaps == 0) {
            throw std::bad_alloc();
        }
    }

    hFile->read_exact(&pLevel->nCameras, sizeof(pLevel->nCameras));
    ValidateRecordCount(pLevel->nCameras, sizeof(W8LevelFileCamera), hFile->size() - hFile->tell());

    if (pLevel->nCameras != 0) {
        pLevel->pCameras = static_cast<W8LevelFileCamera*>(
            calloc(1, pLevel->nCameras * sizeof(W8LevelFileCamera)));
        if (pLevel->pCameras == 0) {
            throw std::bad_alloc();
        }

        for (i = 0; i < pLevel->nCameras; ++i) {
            W8LevelFileCamera* pCamera = pLevel->pCameras + i;
            memset(pCamera, 0, sizeof(W8LevelFileCamera));
            hFile->read_exact(&pCamera->positional0, 4);
            hFile->read_exact(&pCamera->positional1, 4);
            hFile->read_exact(&pCamera->has_scale, 1);
            hFile->read_exact(pCamera->name, sizeof(pCamera->name));
            if (pCamera->has_scale != 0) {
                hFile->read_exact(&pCamera->scale, 4);
            }
            if (!ReadPathAIFile(hFile, &pCamera->pathAI))
                return nullptr;
        }
    }

    hFile->read_exact(&pLevel->has_block, sizeof(pLevel->has_block));
    if (pLevel->has_block != 0) {
        if (!ReadLevelFileBlock(hFile, &pLevel->block))
            return nullptr;
    }

    hFile->read_exact(&pLevel->nTriggers, sizeof(pLevel->nTriggers));
    ValidateRecordCount(pLevel->nTriggers, sizeof(W8LevelFileTrigger),
                        hFile->size() - hFile->tell());
    if (pLevel->nTriggers != 0) {
        pLevel->pTriggers = static_cast<W8LevelFileTrigger*>(
            calloc(1, pLevel->nTriggers * sizeof(W8LevelFileTrigger)));
        if (pLevel->pTriggers == 0) {
            throw std::bad_alloc();
        }

        for (i = 0; i < pLevel->nTriggers; ++i) {
            if (!ReadTriggerFile(hFile, pLevel->pTriggers + i))
                return nullptr;
        }
    }

    hFile->read_exact(&pLevel->camera_mode, sizeof(pLevel->camera_mode));
    hFile->read_exact(&pLevel->nClippingPlanes, sizeof(pLevel->nClippingPlanes));
    ValidateRecordCount(pLevel->nClippingPlanes, sizeof(W8LevelFileClippingPlaneRecord),
                        hFile->size() - hFile->tell());

    if (pLevel->nClippingPlanes != 0) {
        hFile->read_exact(&pLevel->clipping_plane_version, 1);
        pLevel->pClippingPlanes = static_cast<W8LevelFileClippingPlaneRecord*>(
            calloc(1, pLevel->nClippingPlanes * sizeof(W8LevelFileClippingPlaneRecord)));
        if (pLevel->pClippingPlanes == 0) {
            throw std::bad_alloc();
        }
        hFile->read_exact(pLevel->pClippingPlanes,
                          pLevel->nClippingPlanes * sizeof(W8LevelFileClippingPlaneRecord));
    }

    hFile->read_exact(&pLevel->environment_offset, sizeof(pLevel->environment_offset));
    hFile->read_exact(&pLevel->nParticleSystems, sizeof(pLevel->nParticleSystems));
    ValidateRecordCount(pLevel->nParticleSystems, sizeof(W8LevelFileParticleSystem),
                        hFile->size() - hFile->tell());
    if (pLevel->nParticleSystems != 0) {
        pLevel->pParticleSystems = static_cast<W8LevelFileParticleSystem*>(
            calloc(1, pLevel->nParticleSystems * sizeof(W8LevelFileParticleSystem)));
        if (pLevel->pParticleSystems == 0) {
            throw std::bad_alloc();
        }
        for (i = 0; i < pLevel->nParticleSystems; ++i) {

            if (!ReadParticleSystemFile(hFile, pLevel->pParticleSystems + i))
                return nullptr;
        }
    }

    hFile->read_exact(&pLevel->nNamedPositions, sizeof(pLevel->nNamedPositions));
    ValidateRecordCount(pLevel->nNamedPositions, sizeof(W8LevelFileNamedPosition),
                        hFile->size() - hFile->tell());
    if (pLevel->nNamedPositions != 0) {
        pLevel->pNamedPositions = static_cast<W8LevelFileNamedPosition*>(
            calloc(1, pLevel->nNamedPositions * sizeof(W8LevelFileNamedPosition)));
        if (pLevel->pNamedPositions == 0) {
            throw std::bad_alloc();
        }

        for (i = 0; i < pLevel->nNamedPositions; ++i) {
            hFile->read_exact(pLevel->pNamedPositions + i, sizeof(W8LevelFileNamedPosition));
        }

        for (i = 0; i < pLevel->nNamedPositions; ++i) {
            W8LevelFileNamedPosition* pPosition = pLevel->pNamedPositions + i;
            if (!memchr(pPosition->name, '\0', sizeof(pPosition->name)))
                return nullptr;
            ReportBuildStatus(5, FormatString("Named Position: %s (%f, %f, %f)\n", pPosition->name,
                                              pPosition->position.x, pPosition->position.y,
                                              pPosition->position.z));
        }
    }

    hFile->read_exact(pLevel->unknown_6b9, sizeof(pLevel->unknown_6b9));
    pLevel->read_end_position = hFile->tell();
    return owner.release();
} catch (const std::exception&) {
    return nullptr;
}

// FUNCTION: WIZ8 0x004D07C0
BOOLEAN WriteLevelFile(wiz8::File* hFile, wiz8::File* hFileIn, W8LevelFile* pLevel)
try {
    std::unique_ptr<W8LevelFile, decltype(&ReleaseLevelFile)> owner(pLevel, &ReleaseLevelFile);

    int iCount;
    int i;
    char buffer[0x400];

    if (pLevel == 0) {
        throw std::bad_alloc();
    }
    if (hFile == 0) {
        srAssertFail("hFile", LEVELFILE_CPP, 0x10d, 0);
    }
    if (pLevel->pMeshes == 0) {
        throw std::bad_alloc();
    }
    hFile->write(&pLevel->submesh_count, 4);
    hFile->write(&pLevel->mesh_count, 4);
    hFile->write(&pLevel->nTextures, 2);
    iCount = pLevel->nTextures;
    if (iCount == 0) {
        return FALSE;
    }

    for (i = 0; i < static_cast<short>(iCount); ++i) {
        W8MaterialRecord* pTexture = pLevel->pTextures + i;
        if (!WriteMaterialRecord(hFile, pTexture))
            return FALSE;
    }

    hFile->write(&iCount, 4);

    if (pLevel->submesh_count != 0) {
        OctMeshModel* pModel = pLevel->pModels;
        for (i = 0; static_cast<unsigned int>(i) < pLevel->submesh_count; ++i) {
            pModel->Write(hFile);
            ++pModel;
        }
    }
    hFile->write(&iCount, 4);

    hFile->write(&pLevel->nLights, 2);
    if (pLevel->nLights != 0) {
        for (i = 0; i < pLevel->nLights; ++i) {
            if (WriteLightFile(hFile, pLevel->pLights + i) == 0) {
                return FALSE;
            }
        }
    }
    hFile->write(&iCount, 4);
    hFile->write(&pLevel->nMonsters, 4);
    if (pLevel->nMonsters != 0) {
        for (i = 0; i < pLevel->nMonsters; ++i) {
            W8LevelFileMonster* pMonster = pLevel->pMonsters + i;
            hFile->write(pMonster, 0x22);

            if (pMonster->num_mon_path != 0) {
                hFile->write(pMonster->MonPath,
                             pMonster->num_mon_path * sizeof(W8LevelFilePathNode));
            }
        }
    }
    hFile->write(&iCount, 4);
    hFile->write(&pLevel->nItems, 4);
    if (pLevel->nItems != 0) {
        hFile->write(pLevel->pItems, pLevel->nItems * sizeof(W8LevelFileItemRecord));
    }
    hFile->write(&iCount, 4);
    hFile->write(&pLevel->missile_count, 4);
    hFile->write(&iCount, 4);
    hFile->write(&pLevel->nProps, 4);
    if ((pLevel->nProps != 0) &&
        (WritePropsFile(hFile, pLevel->nProps, std::exchange(pLevel->pProps, nullptr)) == 0)) {
        return FALSE;
    }
    hFile->write(&iCount, 4);
    hFile->write(&pLevel->nBitmaps, 4);
    if ((pLevel->nBitmaps != 0) &&
        !WritePropsFile(hFile, pLevel->nBitmaps, std::exchange(pLevel->pBitmaps, nullptr))) {
        return FALSE;
    }
    hFile->write(&iCount, 4);
    hFile->write(&pLevel->nCameras, 4);
    if (pLevel->nCameras != 0) {
        if (pLevel->pCameras == 0) {
            throw std::bad_alloc();
        }
        for (i = 0; i < pLevel->nCameras; ++i) {
            W8LevelFileCamera* pCamera = pLevel->pCameras + i;
            hFile->write(&pCamera->positional0, 4);
            hFile->write(&pCamera->positional1, 4);
            hFile->write(&pCamera->has_scale, 1);
            hFile->write(pCamera->name, sizeof(pCamera->name));
            if (pCamera->has_scale != 0) {
                hFile->write(&pCamera->scale, 4);
            }
            if (!WritePathAIFile(hFile, &pCamera->pathAI))
                return FALSE;
        }
    }
    hFile->write(&iCount, 4);
    hFile->write(&pLevel->has_block, 4);
    if (pLevel->has_block != 0) {
        if (!WriteLevelFileBlock(hFile, &pLevel->block))
            return FALSE;
    }
    hFile->write(&iCount, 4);
    hFile->write(&pLevel->nTriggers, 4);
    if (pLevel->nTriggers != 0) {
        if (pLevel->pTriggers == 0) {
            throw std::bad_alloc();
        }
        for (i = 0; i < pLevel->nTriggers; ++i) {
            if (!WriteTriggerFile(hFile, pLevel->pTriggers + i))
                return FALSE;
        }
    }
    hFile->write(&iCount, 4);
    hFile->write(&pLevel->camera_mode, 4);
    hFile->write(&pLevel->nClippingPlanes, 4);
    if (pLevel->nClippingPlanes != 0) {
        hFile->write(&pLevel->clipping_plane_version, 1);
        hFile->write(pLevel->pClippingPlanes,
                     pLevel->nClippingPlanes * sizeof(W8LevelFileClippingPlaneRecord));
    }
    hFile->write(&iCount, 4);
    hFile->write(&pLevel->environment_offset, sizeof(pLevel->environment_offset));
    hFile->write(&pLevel->nParticleSystems, 4);
    if (pLevel->nParticleSystems != 0) {
        for (i = 0; i < pLevel->nParticleSystems; ++i) {
            if (!WriteParticleSystemFile(hFile, pLevel->pParticleSystems + i))
                return FALSE;
        }
    }
    hFile->write(&iCount, 4);
    hFile->write(&pLevel->nNamedPositions, 4);
    if (pLevel->nNamedPositions != 0) {
        hFile->write(pLevel->pNamedPositions,
                     pLevel->nNamedPositions * sizeof(W8LevelFileNamedPosition));
    }
    hFile->write(&iCount, 4);
    float level_scale = GetAutomapGridCellSize();
    hFile->write(&level_scale, 4);
    hFile->write(&pLevel->num_automap_nodes, 4);
    if (pLevel->num_automap_nodes != 0) {
        hFile->write(pLevel->automap_nodes, pLevel->num_automap_nodes * 4);
    }
    hFile->write(&iCount, 4);
    hFile->write(pLevel->unknown_6b9, 4);
    for (;;) {
        const auto chunk = hFileIn->read(buffer, sizeof(buffer)).bytes;
        hFile->write(buffer, chunk);
        if (chunk < sizeof(buffer))
            break;
    }

    return TRUE;
} catch (const std::exception&) {
    return FALSE;
}

// FUNCTION: WIZ8 0x004D1110
BOOLEAN ReadMeshFile(wiz8::File* hFile, W8LevelFileMesh* pMesh)
try {
    memset(pMesh, 0, sizeof(*pMesh));

    const auto cleanup = [](W8LevelFileMesh* record) { ReleaseLevelRecord(*record); };
    std::unique_ptr<W8LevelFileMesh, decltype(cleanup)> owner(pMesh, cleanup);

    int i;

    hFile->read_exact(&pMesh->version, 4);
    hFile->read_exact(&pMesh->num_vertices, 4);
    hFile->read_exact(&pMesh->num_faces, 4);
    ValidateRecordCount(pMesh->num_vertices, 2 * sizeof(srVector3T<float>),
                        hFile->size() - hFile->tell());
    ValidateRecordCount(pMesh->num_faces, 2 * sizeof(W8ReadMeshFace),
                        hFile->size() - hFile->tell());

    int count = pMesh->num_vertices;
    if (count >= 0x186a1 || count <= 0) {
        sprintf(g_level_file_error, "Invalid number of vertices in mesh (%d vertices).\n", count);
        ReportBuildStatus(7, g_level_file_error);
        return FALSE;
    }
    count = pMesh->num_faces;
    if (count >= 0x30d41 || count <= 0) {
        sprintf(g_level_file_error, "Invalid number of faces in mesh (%d faces).\n", count);
        ReportBuildStatus(7, g_level_file_error);
        return FALSE;
    }
    if (pMesh->version >= 3) {
        hFile->read_exact(&pMesh->flags, 1);
    }
    if (pMesh->version >= 2) {
        hFile->read_exact(&pMesh->location, sizeof(pMesh->location));
        hFile->read_exact(&pMesh->rotation_angle, 0x10);
        hFile->read_exact(&pMesh->scale, sizeof(pMesh->scale));
    }
    if (pMesh->version >= 4) {
        hFile->read_exact(&pMesh->mapping_count, 1);
        ValidateRecordCount(pMesh->mapping_count, 4, hFile->size() - hFile->tell());
        if (pMesh->mapping_count > 1) {
            throw std::runtime_error(
                "ReadMeshFile: Multiple mapping records need an expanded mesh record.");
        }
        if (pMesh->mapping_count != 0) {
            hFile->read_exact(&pMesh->mapped_value, 4);
        }
    }

    if ((pMesh->flags & W8_LEVEL_MESH_LOD_VERTICES) == 0) {
        pMesh->pstVertices = static_cast<srVector3T<float>*>(
            calloc(1, pMesh->num_vertices * 2 * sizeof(*pMesh->pstVertices)));
        if (pMesh->pstVertices == 0) {
            throw std::bad_alloc();
        }

        hFile->read_exact(pMesh->pstVertices, pMesh->num_vertices * sizeof(*pMesh->pstVertices));
    } else {
        hFile->read_exact(&pMesh->lod_mode, 1);
        hFile->read_exact(&pMesh->num_lods, 2);
        ValidateRecordCount(pMesh->num_lods, sizeof(void*), hFile->size() - hFile->tell());
        if ((pMesh->flags & W8_LEVEL_MESH_SHORT_LOD_VERTICES) == 0) {
            srVector3T<float>** pLods =
                static_cast<srVector3T<float>**>(calloc(1, pMesh->num_lods * sizeof(*pLods)));
            if (pLods == 0) {
                return FALSE;
            }
            pMesh->lods = pLods;
            for (i = 0; i < pMesh->num_lods; ++i) {
                pLods[i] = static_cast<srVector3T<float>*>(
                    calloc(1, pMesh->num_vertices * sizeof(*pLods[i])));
                if (pLods[i] == 0) {
                    return FALSE;
                }
                hFile->read_exact(pLods[i], pMesh->num_vertices * sizeof(*pLods[i]));
            }
            pMesh->lods = pLods;
        } else {
            if (pMesh->lod_mode >= 2) {
                hFile->read_exact(&pMesh->lod_scale, 4);
            }
            short** pLods = static_cast<short**>(calloc(1, pMesh->num_lods * sizeof(short*)));
            if (pLods == 0) {
                return FALSE;
            }
            pMesh->lod_shorts = pLods;
            for (i = 0; i < pMesh->num_lods; ++i) {
                pLods[i] = static_cast<short*>(calloc(1, pMesh->num_vertices * 3 * sizeof(short)));
                if (pLods[i] == 0) {
                    return FALSE;
                }
                hFile->read_exact(pLods[i], pMesh->num_vertices * 3 * sizeof(short));
            }
            pMesh->lod_shorts = pLods;
        }
    }
    if ((pMesh->flags & W8_LEVEL_MESH_COMPRESSED_FACES) != 0) {
        pMesh->pstCompFaces = static_cast<W8LevelFileCompressedFace*>(
            calloc(1, pMesh->num_faces * sizeof(W8LevelFileCompressedFace)));
        if (pMesh->pstCompFaces == 0) {
            throw std::bad_alloc();
        }

        hFile->read_exact(pMesh->pstCompFaces,
                          pMesh->num_faces * sizeof(W8LevelFileCompressedFace));
    } else {
        pMesh->pstFaces = static_cast<W8ReadMeshFace*>(
            calloc(1, pMesh->num_faces * 2 * sizeof(*pMesh->pstFaces)));
        if (pMesh->pstFaces == 0) {
            throw std::bad_alloc();
        }

        hFile->read_exact(pMesh->pstFaces, pMesh->num_faces * sizeof(*pMesh->pstFaces));
    }
    static_cast<void>(owner.release());
    return TRUE;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x004D1510
BOOLEAN WriteMeshFile(wiz8::File* hFile, W8LevelFileMesh* pMesh)
{
    const auto cleanup = [](W8LevelFileMesh* record) { ReleaseLevelRecord(*record); };
    std::unique_ptr<W8LevelFileMesh, decltype(cleanup)> owner(pMesh, cleanup);

    if (pMesh->version >= 4 && (pMesh->mapping_count < 0 || pMesh->mapping_count > 1)) {
        return FALSE;
    }
    hFile->write(&pMesh->version, 4);
    hFile->write(&pMesh->num_vertices, 4);
    hFile->write(&pMesh->num_faces, 4);

    if (pMesh->version >= 3) {
        hFile->write(&pMesh->flags, 1);
    }
    if (pMesh->version >= 2) {
        hFile->write(&pMesh->location, sizeof(pMesh->location));
        hFile->write(&pMesh->rotation_angle, 0x10);
        hFile->write(&pMesh->scale, sizeof(pMesh->scale));
    }
    if (pMesh->version >= 4) {
        hFile->write(&pMesh->mapping_count, 1);
        if (pMesh->mapping_count != 0) {
            hFile->write(&pMesh->mapped_value, 4);
        }
    }

    if ((pMesh->flags & W8_LEVEL_MESH_LOD_VERTICES) == 0) {
        if (pMesh->pstVertices == 0) {
            throw std::bad_alloc();
        }
        hFile->write(pMesh->pstVertices, pMesh->num_vertices * sizeof(*pMesh->pstVertices));
    } else {
        hFile->write(&pMesh->lod_mode, 1);
        hFile->write(&pMesh->num_lods, 2);
        if (pMesh->lod_mode >= 2) {
            hFile->write(&pMesh->lod_scale, 4);
        }
        if ((pMesh->flags & W8_LEVEL_MESH_SHORT_LOD_VERTICES) == 0) {
            srVector3T<float>** pLods = pMesh->lods;
            if (pLods == 0) {
                return FALSE;
            }
            for (int i = 0; i < pMesh->num_lods; ++i) {
                if (pLods[i] == 0) {
                    return FALSE;
                }
                hFile->write(pLods[i], pMesh->num_vertices * sizeof(*pLods[i]));
            }
        } else {
            short** pLods = pMesh->lod_shorts;
            if (pLods == 0) {
                return FALSE;
            }
            for (int i = 0; i < pMesh->num_lods; ++i) {
                if (pLods[i] == 0) {
                    return FALSE;
                }
                hFile->write(pLods[i], pMesh->num_vertices * 3 * sizeof(short));
            }
        }
    }

    if ((pMesh->flags & W8_LEVEL_MESH_COMPRESSED_FACES) != 0) {
        if (pMesh->pstCompFaces == 0) {
            throw std::bad_alloc();
        }
        hFile->write(pMesh->pstCompFaces, pMesh->num_faces * sizeof(W8LevelFileCompressedFace));

        return TRUE;
    }
    if (pMesh->pstFaces == 0) {
        throw std::bad_alloc();
    }
    hFile->write(pMesh->pstFaces, pMesh->num_faces * sizeof(*pMesh->pstFaces));

    return TRUE;
}

// FUNCTION: WIZ8 0x004D1820
BOOLEAN ReadLightFile(wiz8::File* hFile, W8LevelFileLight* pLight)
try {
    memset(pLight, 0, sizeof(*pLight));
    const auto cleanup = [](W8LevelFileLight* record) { ReleaseLevelRecord(*record); };
    std::unique_ptr<W8LevelFileLight, decltype(cleanup)> owner(pLight, cleanup);

    hFile->read_exact(&pLight->version, 2);
    hFile->read_exact(&pLight->create, 4);
    hFile->read_exact(pLight->unknown_06, 2);
    hFile->read_exact(&pLight->position, sizeof(pLight->position));
    hFile->read_exact(&pLight->colour, sizeof(pLight->colour));
    hFile->read_exact(&pLight->intensity, 4);
    hFile->read_exact(&pLight->range, 4);

    if (pLight->version >= 2) {
        hFile->read_exact(pLight->name, 0x14);
        if ((pLight->flags & W8_LEVEL_LIGHT_HAS_DEFINITION) != 0) {
            pLight->create = 1;
            pLight->pExtra =
                static_cast<W8LevelFileLightExtra*>(calloc(1, sizeof(W8LevelFileLightExtra)));
            if (pLight->pExtra == 0) {
                return FALSE;
            }
            hFile->read_exact(pLight->pExtra, sizeof(W8LevelFileLightExtra));

            if ((pLight->pExtra->flags & W8_PARAM_LIGHT_HAS_PATH) != 0) {
                pLight->pPathAI =
                    static_cast<W8LevelFilePathAI*>(calloc(1, sizeof(W8LevelFilePathAI)));
                if (pLight->pPathAI == 0) {
                    return FALSE;
                }
                if (!ReadPathAIFile(hFile, pLight->pPathAI))
                    return FALSE;
            }
        }
    }
    static_cast<void>(owner.release());
    return TRUE;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x004D1960
BOOLEAN WriteLightFile(wiz8::File* hFile, W8LevelFileLight* pLight)
{
    const auto cleanup = [](W8LevelFileLight* record) { ReleaseLevelRecord(*record); };
    std::unique_ptr<W8LevelFileLight, decltype(cleanup)> owner(pLight, cleanup);

    hFile->write(&pLight->version, 2);
    hFile->write(&pLight->create, 4);
    hFile->write(pLight->unknown_06, 2);
    hFile->write(&pLight->position, sizeof(pLight->position));
    hFile->write(&pLight->colour, sizeof(pLight->colour));
    hFile->write(&pLight->intensity, 4);
    hFile->write(&pLight->range, 4);

    if (pLight->version >= 2) {
        hFile->write(pLight->name, 0x14);
        if (((pLight->flags & W8_LEVEL_LIGHT_HAS_DEFINITION) != 0) && (pLight->pExtra != 0)) {
            hFile->write(pLight->pExtra, sizeof(W8LevelFileLightExtra));

            if (((pLight->pExtra->flags & W8_PARAM_LIGHT_HAS_PATH) != 0) &&
                (pLight->pPathAI != 0)) {
                if (!WritePathAIFile(hFile, pLight->pPathAI))
                    return FALSE;
            }
        }
    }
    return TRUE;
}

// FUNCTION: WIZ8 0x004D1A90
BOOLEAN ReadAnimLightFile(wiz8::File* hFile, W8LevelFileAnimLight* pLight)
try {
    memset(pLight, 0, sizeof(*pLight));
    const auto cleanup = [](W8LevelFileAnimLight* record) { ReleaseLevelRecord(*record); };
    std::unique_ptr<W8LevelFileAnimLight, decltype(cleanup)> owner(pLight, cleanup);

    hFile->read_exact(&pLight->version, 1);
    hFile->read_exact(&pLight->position, sizeof(pLight->position));
    hFile->read_exact(&pLight->color, sizeof(pLight->color));
    hFile->read_exact(&pLight->intensity, 4);
    hFile->read_exact(&pLight->range, 4);

    if (pLight->version >= 2) {
        pLight->pExtra =
            static_cast<W8LevelFileLightExtra*>(calloc(1, sizeof(W8LevelFileLightExtra)));
        if (pLight->pExtra == 0) {
            return FALSE;
        }
        hFile->read_exact(pLight->pExtra, sizeof(W8LevelFileLightExtra));
    }
    static_cast<void>(owner.release());
    return TRUE;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x004D1B50
BOOLEAN WriteAnimLightFile(wiz8::File* hFile, W8LevelFileAnimLight* pLight)
{
    const auto cleanup = [](W8LevelFileAnimLight* record) { ReleaseLevelRecord(*record); };
    std::unique_ptr<W8LevelFileAnimLight, decltype(cleanup)> owner(pLight, cleanup);

    hFile->write(&pLight->version, 1);
    hFile->write(&pLight->position, sizeof(pLight->position));
    hFile->write(&pLight->color, sizeof(pLight->color));
    hFile->write(&pLight->intensity, 4);
    hFile->write(&pLight->range, 4);

    if ((pLight->version >= 2) && (pLight->pExtra != 0)) {
        hFile->write(pLight->pExtra, sizeof(W8LevelFileLightExtra));
    }
    return TRUE;
}

// FUNCTION: WIZ8 0x004D1C10
BOOLEAN ReadTriggerFile(wiz8::File* hFile, W8LevelFileTrigger* pTrigger)
try {
    LevelRegistryTransaction registries;

    memset(pTrigger, 0, sizeof(*pTrigger));
    const auto cleanup = [](W8LevelFileTrigger* record) { ReleaseLevelRecord(*record); };
    std::unique_ptr<W8LevelFileTrigger, decltype(cleanup)> owner(pTrigger, cleanup);

    hFile->read_exact(&pTrigger->version, 1);
    hFile->read_exact(&pTrigger->type, 1);
    switch (pTrigger->type) {
    case 1: {
        W8LevelFileSwitch* pSwitch =
            static_cast<W8LevelFileSwitch*>(calloc(1, sizeof(W8LevelFileSwitch)));
        if (pSwitch == 0) {
            throw std::bad_alloc();
        }

        pTrigger->pData = pSwitch;
        hFile->read_exact(&pSwitch->version, 1);
        hFile->read_exact(&pSwitch->cycle_bounce, 4);
        hFile->read_exact(&pSwitch->state_count, 4);
        hFile->read_exact(&pSwitch->animate_states, 4);
        hFile->read_exact(&pSwitch->range, 4);
        hFile->read_exact(&pSwitch->action, 4);
        hFile->read_exact(&pSwitch->value, 4);
        hFile->read_exact(&pSwitch->animate_action, 4);
        hFile->read_exact(&pSwitch->packed_flags, 1);
        hFile->read_exact(&pSwitch->enabled, 1);
        hFile->read_exact(pSwitch->name, sizeof(pSwitch->name));
        if (!memchr(pSwitch->name, '\0', sizeof(pSwitch->name)))
            return FALSE;
        hFile->read_exact(pSwitch->recipients, sizeof(pSwitch->recipients));
        if (!memchr(pSwitch->recipients, '\0', sizeof(pSwitch->recipients)))
            return FALSE;
        hFile->read_exact(pSwitch->sound, sizeof(pSwitch->sound));
        ReportBuildStatus(5, FormatString("Switch Trigger: %s, recipients: %s\n", pSwitch->name,
                                          pSwitch->recipients));
        if (pSwitch->version > 1) {
            hFile->read_exact(&pSwitch->minimum_range, 4);
            hFile->read_exact(pSwitch->surface_id, sizeof(pSwitch->surface_id));
            if (!memchr(pSwitch->surface_id, '\0', sizeof(pSwitch->surface_id)))
                return FALSE;
            ReportBuildStatus(5, FormatString("Switch Trigger name: %s\n", pSwitch->surface_id));
        }
        if (pSwitch->version > 2) {
            hFile->read_exact(&pSwitch->has_door_trigger, 1);
            if (pSwitch->has_door_trigger != 0) {
                hFile->read_exact(&pSwitch->door.kind, 1);
                if (pSwitch->door.kind == 1) {
                    if (!ReadDoorTriggerFile(hFile, &pSwitch->door))
                        return FALSE;
                }
            }
        }
        if (pSwitch->version > 3) {
            hFile->read_exact(&pSwitch->action_value, 4);
        }
        pTrigger->pData = pSwitch;
        if (g_level_file) {
            if (g_level_file->num_switch_triggers >= 1000)
                return FALSE;
            g_level_file->switch_triggers[g_level_file->num_switch_triggers++] = pSwitch;
        }
        registries.commit();
        static_cast<void>(owner.release());
        return TRUE;
    }
    case 2: {
        W8LevelFileInvisible* pInvis =
            static_cast<W8LevelFileInvisible*>(calloc(1, sizeof(W8LevelFileInvisible)));
        if (pInvis == 0) {
            throw std::bad_alloc();
        }

        pTrigger->pData = pInvis;
        hFile->read_exact(&pInvis->version, 1);
        hFile->read_exact(&pInvis->range, 4);
        hFile->read_exact(&pInvis->position, sizeof(pInvis->position));
        hFile->read_exact(&pInvis->action, 4);
        hFile->read_exact(&pInvis->searchable, 4);
        hFile->read_exact(&pInvis->fire_linked, 1);
        hFile->read_exact(&pInvis->enabled, 1);
        hFile->read_exact(pInvis->name, sizeof(pInvis->name));
        if (!memchr(pInvis->name, '\0', sizeof(pInvis->name)))
            return FALSE;
        hFile->read_exact(pInvis->recipients, sizeof(pInvis->recipients));
        if (!memchr(pInvis->recipients, '\0', sizeof(pInvis->recipients)))
            return FALSE;
        ReportBuildStatus(5, FormatString("Invisible Trigger: %s, recipients: %s\n", pInvis->name,
                                          pInvis->recipients));
        if (pInvis->version > 1) {
            hFile->read_exact(&pInvis->plane_flag, 1);
            pInvis->pPlane = static_cast<W8LevelFilePlane*>(calloc(1, sizeof(W8LevelFilePlane)));
            if (pInvis->pPlane == 0) {
                throw std::bad_alloc();
            }

            hFile->read_exact(pInvis->pPlane, sizeof(W8LevelFilePlane));
        }
        if (pInvis->version > 2) {
            hFile->read_exact(&pInvis->angle, 4);
            hFile->read_exact(&pInvis->direction, sizeof(pInvis->direction));
            hFile->read_exact(&pInvis->unused, 1);
            hFile->read_exact(pInvis->action_string, sizeof(pInvis->action_string));
        }
        if (pInvis->version > 3) {
            hFile->read_exact(&pInvis->flag, 1);
            hFile->read_exact(&pInvis->action_value, 4);
        }
        if (pInvis->version > 4) {
            hFile->read_exact(&pInvis->has_legacy_geometry, 1);
            if (pInvis->has_legacy_geometry != 0) {
                hFile->read_exact(&pInvis->geometry_kind, 1);
                if (pInvis->linked_record_kind_gate == 2) {
                    W8LevelFileLinkedRecord* pRecord = static_cast<W8LevelFileLinkedRecord*>(
                        calloc(1, sizeof(W8LevelFileLinkedRecord)));
                    if (!pRecord)
                        throw std::bad_alloc();
                    pInvis->pRecord = pRecord;
                    hFile->read_exact(&pRecord->kind, 1);
                    hFile->read_exact(pRecord->vertices, sizeof(pRecord->vertices));
                    hFile->read_exact(&pRecord->linked_face, 2);
                    if (g_level_file) {
                        if (g_level_file->num_linked_records >= 100)
                            return FALSE;
                        g_level_file->linked_records[g_level_file->num_linked_records++] = pRecord;
                    }
                    pRecord->normal_scale = pInvis->range;
                    pRecord->forward_scale = 1.0f;
                    registries.commit();
                    static_cast<void>(owner.release());
                    return TRUE;
                }
            }
        }
        if (g_level_file) {
            if (g_level_file->num_invisible_planes >= 1000)
                return FALSE;
            g_level_file->invisible_planes[g_level_file->num_invisible_planes++] = pInvis->pPlane;
        }
        pTrigger->pData = pInvis;
        registries.commit();
        static_cast<void>(owner.release());
        return TRUE;
    }
    case 3: {
        W8LevelFileSound* pSound =
            static_cast<W8LevelFileSound*>(calloc(1, sizeof(W8LevelFileSound)));
        if (pSound == 0) {
            throw std::bad_alloc();
        }

        pTrigger->pData = pSound;
        hFile->read_exact(&pSound->version, 1);
        hFile->read_exact(&pSound->volume_min, 4);
        hFile->read_exact(&pSound->volume_max, 4);
        hFile->read_exact(&pSound->speed_min, 4);
        hFile->read_exact(&pSound->speed_max, 4);
        hFile->read_exact(&pSound->time_min, 4);
        hFile->read_exact(&pSound->time_max, 4);
        hFile->read_exact(&pSound->unbounded, 4);
        hFile->read_exact(&pSound->radius, 4);
        hFile->read_exact(&pSound->position, sizeof(pSound->position));
        hFile->read_exact(&pSound->region_u, sizeof(pSound->region_u));
        hFile->read_exact(&pSound->region_v, sizeof(pSound->region_v));
        hFile->read_exact(pSound->wave, sizeof(pSound->wave));
        if (pSound->version > 1) {
            hFile->read_exact(&pSound->has_position, 1);
            hFile->read_exact(&pSound->looping, 1);
        }
        if (pSound->version > 2) {
            hFile->read_exact(&pSound->region_center, sizeof(pSound->region_center));
            hFile->read_exact(&pSound->region_angle, 4);
            hFile->read_exact(&pSound->region_min, sizeof(pSound->region_min));
            hFile->read_exact(&pSound->region_max, sizeof(pSound->region_max));
        }
        if (pSound->version > 3) {
            hFile->read_exact(pSound->name, sizeof(pSound->name));
            ReportBuildStatus(5, FormatString("Sound Trigger: %s\n", pSound->name));
        }
        if (pSound->version > 4) {
            hFile->read_exact(&pSound->shared, 1);
        }
        pTrigger->pData = pSound;
        registries.commit();
        static_cast<void>(owner.release());
        return TRUE;
    }
    case 4:
        if (!ReadSuperTriggerFile(hFile, pTrigger))
            return FALSE;
        registries.commit();
        static_cast<void>(owner.release());
        return TRUE;
    default:
        registries.commit();
        static_cast<void>(owner.release());
        return TRUE;
    }
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x004D23F0
BOOLEAN WriteTriggerFile(wiz8::File* hFile, W8LevelFileTrigger* pTrigger)
{
    const auto cleanup = [](W8LevelFileTrigger* record) { ReleaseLevelRecord(*record); };
    std::unique_ptr<W8LevelFileTrigger, decltype(cleanup)> owner(pTrigger, cleanup);

    hFile->write(&pTrigger->version, 1);
    hFile->write(&pTrigger->type, 1);
    switch (pTrigger->type) {
    case 1: {
        W8LevelFileSwitch* pSwitch = static_cast<W8LevelFileSwitch*>(pTrigger->pData);
        if (pSwitch == 0) {
            throw std::bad_alloc();
        }
        hFile->write(&pSwitch->version, 1);
        hFile->write(&pSwitch->cycle_bounce, 4);
        hFile->write(&pSwitch->state_count, 4);
        hFile->write(&pSwitch->animate_states, 4);
        hFile->write(&pSwitch->range, 4);
        hFile->write(&pSwitch->action, 4);
        hFile->write(&pSwitch->value, 4);
        hFile->write(&pSwitch->animate_action, 4);
        hFile->write(&pSwitch->packed_flags, 1);
        hFile->write(&pSwitch->enabled, 1);
        hFile->write(pSwitch->name, sizeof(pSwitch->name));
        hFile->write(pSwitch->recipients, sizeof(pSwitch->recipients));
        hFile->write(pSwitch->sound, sizeof(pSwitch->sound));
        if (pSwitch->version > 1) {
            hFile->write(&pSwitch->minimum_range, 4);
            hFile->write(pSwitch->surface_id, sizeof(pSwitch->surface_id));
        }
        if (pSwitch->version > 2) {
            hFile->write(&pSwitch->has_door_trigger, 1);
            if (pSwitch->has_door_trigger != 0) {
                hFile->write(&pSwitch->door.kind, 1);
                if (pSwitch->door.kind == 1) {
                    if (!WriteDoorTriggerFile(hFile, &pSwitch->door))
                        return FALSE;
                }
            }
        }
        if (pSwitch->version > 3) {
            hFile->write(&pSwitch->action_value, 4);
        }

        return TRUE;
    }
    case 2: {
        W8LevelFileInvisible* pInvis = static_cast<W8LevelFileInvisible*>(pTrigger->pData);
        if (pInvis == 0) {
            throw std::bad_alloc();
        }
        hFile->write(&pInvis->version, 1);
        hFile->write(&pInvis->range, 4);
        hFile->write(&pInvis->position, sizeof(pInvis->position));
        hFile->write(&pInvis->action, 4);
        hFile->write(&pInvis->searchable, 4);
        hFile->write(&pInvis->fire_linked, 1);
        hFile->write(&pInvis->enabled, 1);
        hFile->write(pInvis->name, sizeof(pInvis->name));
        hFile->write(pInvis->recipients, sizeof(pInvis->recipients));
        if (pInvis->version > 1) {
            hFile->write(&pInvis->plane_flag, 1);
            hFile->write(pInvis->pPlane, sizeof(W8LevelFilePlane));
        }
        if (pInvis->version > 2) {
            hFile->write(&pInvis->angle, 4);
            hFile->write(&pInvis->direction, sizeof(pInvis->direction));
            hFile->write(&pInvis->unused, 1);
            hFile->write(pInvis->action_string, sizeof(pInvis->action_string));
        }
        if (pInvis->version > 3) {
            hFile->write(&pInvis->flag, 1);
            hFile->write(&pInvis->action_value, 4);
        }
        if (pInvis->version > 4) {
            hFile->write(&pInvis->has_legacy_geometry, 1);
            if (pInvis->has_legacy_geometry != 0) {
                hFile->write(&pInvis->geometry_kind, 1);
                if (pInvis->linked_record_kind_gate == 2) {
                    W8LevelFileLinkedRecord* pRecord = pInvis->pRecord;
                    if (pRecord != 0) {
                        hFile->write(&pRecord->kind, 1);
                        hFile->write(pRecord->vertices, sizeof(pRecord->vertices));
                        hFile->write(&pRecord->linked_face, 2);
                    }
                }
            }
        }

        return TRUE;
    }
    case 3: {
        W8LevelFileSound* pSound = static_cast<W8LevelFileSound*>(pTrigger->pData);
        if (pSound == 0) {
            throw std::bad_alloc();
        }
        hFile->write(&pSound->version, 1);
        hFile->write(&pSound->volume_min, 4);
        hFile->write(&pSound->volume_max, 4);
        hFile->write(&pSound->speed_min, 4);
        hFile->write(&pSound->speed_max, 4);
        hFile->write(&pSound->time_min, 4);
        hFile->write(&pSound->time_max, 4);
        hFile->write(&pSound->unbounded, 4);
        hFile->write(&pSound->radius, 4);
        hFile->write(&pSound->position, sizeof(pSound->position));
        hFile->write(&pSound->region_u, sizeof(pSound->region_u));
        hFile->write(&pSound->region_v, sizeof(pSound->region_v));
        hFile->write(pSound->wave, sizeof(pSound->wave));
        if (pSound->version > 1) {
            hFile->write(&pSound->has_position, 1);
            hFile->write(&pSound->looping, 1);
        }
        if (pSound->version > 2) {
            hFile->write(&pSound->region_center, sizeof(pSound->region_center));
            hFile->write(&pSound->region_angle, 4);
            hFile->write(&pSound->region_min, sizeof(pSound->region_min));
            hFile->write(&pSound->region_max, sizeof(pSound->region_max));
        }
        if (pSound->version > 3) {
            hFile->write(pSound->name, sizeof(pSound->name));
        }
        if (pSound->version > 4) {
            hFile->write(&pSound->shared, 1);
        }

        return TRUE;
    }
    case 4:
        return WriteSuperTriggerFile(hFile, pTrigger);
    default:
        return TRUE;
    }
}

// FUNCTION: WIZ8 0x004D2A30
BOOLEAN ReadSuperTriggerFile(wiz8::File* hFile, W8LevelFileTrigger* pTrigger)
try {
    LevelRegistryTransaction registries;

    pTrigger->type = 4;
    pTrigger->pData = nullptr;
    const auto cleanup = [](W8LevelFileTrigger* record) { ReleaseLevelRecord(*record); };
    std::unique_ptr<W8LevelFileTrigger, decltype(cleanup)> owner(pTrigger, cleanup);

    W8LevelFileSuperTrigger* pSuper =
        static_cast<W8LevelFileSuperTrigger*>(calloc(1, sizeof(W8LevelFileSuperTrigger)));
    if (pSuper == 0) {
        ReportBuildStatus(7, "ReadSuperTrigger: Could not allocate SuperTrigger strucutre.\n");
        return FALSE;
    }
    pTrigger->pData = pSuper;
    hFile->read_exact(&pSuper->version, 1);
    hFile->read_exact(pSuper->name, sizeof(pSuper->name));
    if (!memchr(pSuper->name, '\0', sizeof(pSuper->name)))
        return FALSE;
    hFile->read_exact(&pSuper->flags, 1);
    hFile->read_exact(&pSuper->active, 1);
    hFile->read_exact(&pSuper->kind, 1);
    hFile->read_exact(&pSuper->when_active, 1);
    hFile->read_exact(&pSuper->prop_index, 1);
    hFile->read_exact(&pSuper->activation_count, 1);
    hFile->read_exact(&pSuper->inactive_count, 1);
    hFile->read_exact(&pSuper->trigger, 4);
    hFile->read_exact(&pSuper->trigger_on, 4);
    hFile->read_exact(&pSuper->trigger_off, 4);
    hFile->read_exact(pSuper->recipients, sizeof(pSuper->recipients));
    if (!memchr(pSuper->recipients, '\0', sizeof(pSuper->recipients)))
        return FALSE;
    hFile->read_exact(&pSuper->ataxia_or_cure, 1);
    hFile->read_exact(pSuper->ps_events, sizeof(pSuper->ps_events));
    hFile->read_exact(&pSuper->allow_save, 1);
    hFile->read_exact(&pSuper->price, 4);
    hFile->read_exact(&pSuper->door_kind, 1);
    hFile->read_exact(pSuper->animation, sizeof(pSuper->animation));

    ReportBuildStatus(
        5, FormatString("Super Trigger: %s, recipients: %s\n", pSuper->name, pSuper->recipients));
    if (pSuper->version >= 2) {
        hFile->read_exact(pSuper->size, sizeof(pSuper->size));
        hFile->read_exact(&pSuper->direction, 4);
        hFile->read_exact(&pSuper->wait0, 1);
        hFile->read_exact(&pSuper->wait1, 1);
        hFile->read_exact(&pSuper->wait2, 1);
        hFile->read_exact(&pSuper->loop, 1);
        hFile->read_exact(&pSuper->speed, 0x10);
    }
    hFile->read_exact(&pSuper->ignore, 1);
    hFile->read_exact(&pSuper->group, 1);
    hFile->read_exact(&pSuper->set_group, 1);
    hFile->read_exact(pSuper->groups, sizeof(pSuper->groups));
    hFile->read_exact(pSuper->objects, sizeof(pSuper->objects));
    hFile->read_exact(&pSuper->close_door, 1);

    hFile->read_exact(&pSuper->wait3, 4);
    hFile->read_exact(&pSuper->field, 4);
    hFile->read_exact(pSuper->event, sizeof(pSuper->event));
    hFile->read_exact(&pSuper->normal_scale, 4);

    if (pSuper->version >= 3) {
        hFile->read_exact(pSuper->particle_system, sizeof(pSuper->particle_system));
    }
    if ((pSuper->flags & 1) == 0) {
        hFile->read_exact(&pSuper->placement_kind, 1);
        if (pSuper->placement_kind == 1) {
            pSuper->pPosition = static_cast<W8LevelFileTriggerPosition*>(
                calloc(1, sizeof(W8LevelFileTriggerPosition)));
            if (pSuper->pPosition == 0) {
                ReportBuildStatus(
                    7, "ReadSuperTrigger: Could not allocate Trigger Position structure.\n");
                return FALSE;
            }
            hFile->read_exact(pSuper->pPosition, sizeof(W8LevelFileTriggerPosition));
        } else if (pSuper->placement_kind == 2) {
            pSuper->pPlane = static_cast<W8LevelFilePlane*>(calloc(1, sizeof(W8LevelFilePlane)));
            if (pSuper->pPlane == 0) {
                ReportBuildStatus(
                    7, "ReadSuperTrigger: Could not allocate Trigger Plane structure.\n");
                return FALSE;
            }
            hFile->read_exact(pSuper->pPlane, sizeof(W8LevelFilePlane));
            if (g_level_file) {
                if (g_level_file->num_invisible_planes >= 1000)
                    return FALSE;
                g_level_file->invisible_planes[g_level_file->num_invisible_planes++] =
                    pSuper->pPlane;
            }
        }

        hFile->read_exact(&pSuper->has_hotspot, 1);
        if (pSuper->has_hotspot != 0) {
            pSuper->pHotSpot = static_cast<W8LevelFileTriggerHotSpot*>(
                calloc(1, sizeof(W8LevelFileTriggerHotSpot)));
            if (pSuper->pHotSpot == 0) {
                ReportBuildStatus(
                    7, "ReadSuperTrigger: Could not allocate Trigger HotSpot structure.\n");
                return FALSE;
            }
            hFile->read_exact(pSuper->pHotSpot, sizeof(W8LevelFileTriggerHotSpot));
        }
    }

    hFile->read_exact(&pSuper->has_door, 1);
    if (pSuper->has_door != 0) {
        hFile->read_exact(&pSuper->door.kind, 1);
        if (pSuper->door.kind == 1) {
            if (!ReadDoorTriggerFile(hFile, &pSuper->door))
                return FALSE;
        } else if (pSuper->door.kind == 2) {
            W8LevelFileLinkedRecord* pRecord =
                static_cast<W8LevelFileLinkedRecord*>(calloc(1, sizeof(W8LevelFileLinkedRecord)));

            if (!pRecord)
                throw std::bad_alloc();
            pSuper->pRecord = pRecord;
            {
                hFile->read_exact(&pRecord->kind, 1);
                hFile->read_exact(pRecord->vertices, sizeof(pRecord->vertices));
                hFile->read_exact(&pRecord->linked_face, 2);
                if (g_level_file) {
                    if (g_level_file->num_linked_records >= 100)
                        return FALSE;
                    g_level_file->linked_records[g_level_file->num_linked_records++] = pRecord;
                }
                pSuper->pRecord = pRecord;
            }

            pSuper->pRecord->normal_scale = pSuper->normal_scale;
            pSuper->pRecord->forward_scale = pSuper->direction;
        }
    }
    pTrigger->pData = pSuper;
    registries.commit();
    static_cast<void>(owner.release());
    return TRUE;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x004D3000
BOOLEAN WriteSuperTriggerFile(wiz8::File* hFile, W8LevelFileTrigger* pTrigger)
{
    const auto cleanup = [](W8LevelFileTrigger* record) { ReleaseLevelRecord(*record); };
    std::unique_ptr<W8LevelFileTrigger, decltype(cleanup)> owner(pTrigger, cleanup);

    W8LevelFileSuperTrigger* pSuper = static_cast<W8LevelFileSuperTrigger*>(pTrigger->pData);
    if (pSuper == 0) {
        ReportBuildStatus(7, "WriteSuperTrigger: Couldn't create SuperTrigger structure.\n");
        return 0;
    }
    hFile->write(&pSuper->version, 1);
    hFile->write(pSuper->name, sizeof(pSuper->name));
    hFile->write(&pSuper->flags, 1);
    hFile->write(&pSuper->active, 1);
    hFile->write(&pSuper->kind, 1);
    hFile->write(&pSuper->when_active, 1);
    hFile->write(&pSuper->prop_index, 1);
    hFile->write(&pSuper->activation_count, 1);
    hFile->write(&pSuper->inactive_count, 1);
    hFile->write(&pSuper->trigger, 4);
    hFile->write(&pSuper->trigger_on, 4);
    hFile->write(&pSuper->trigger_off, 4);
    hFile->write(pSuper->recipients, sizeof(pSuper->recipients));
    hFile->write(&pSuper->ataxia_or_cure, 1);
    hFile->write(pSuper->ps_events, sizeof(pSuper->ps_events));
    hFile->write(&pSuper->allow_save, 1);
    hFile->write(&pSuper->price, 4);
    hFile->write(&pSuper->door_kind, 1);
    hFile->write(pSuper->animation, sizeof(pSuper->animation));

    if (pSuper->version >= 2) {
        hFile->write(pSuper->size, sizeof(pSuper->size));
        hFile->write(&pSuper->direction, 4);
        hFile->write(&pSuper->wait0, 1);
        hFile->write(&pSuper->wait1, 1);
        hFile->write(&pSuper->wait2, 1);
        hFile->write(&pSuper->loop, 1);
        hFile->write(&pSuper->speed, 0x10);
    }
    hFile->write(&pSuper->ignore, 1);
    hFile->write(&pSuper->group, 1);
    hFile->write(&pSuper->set_group, 1);
    hFile->write(pSuper->groups, sizeof(pSuper->groups));
    hFile->write(pSuper->objects, sizeof(pSuper->objects));
    hFile->write(&pSuper->close_door, 1);

    hFile->write(&pSuper->wait3, 4);
    hFile->write(&pSuper->field, 4);
    hFile->write(pSuper->event, sizeof(pSuper->event));
    hFile->write(&pSuper->normal_scale, 4);

    if (pSuper->version >= 3) {
        hFile->write(pSuper->particle_system, sizeof(pSuper->particle_system));
    }
    if ((pSuper->flags & 1) == 0) {
        hFile->write(&pSuper->placement_kind, 1);
        if (pSuper->placement_kind == 1) {
            if (pSuper->pPosition == 0) {
                ReportBuildStatus(7, "WriteSuperTrigger: No Trigger Position structure.\n");
                return 0;
            }
            hFile->write(pSuper->pPosition, sizeof(W8LevelFileTriggerPosition));

        } else if (pSuper->placement_kind == 2) {
            if (pSuper->pPlane == 0) {
                ReportBuildStatus(7, "WriteSuperTrigger: No FileTriggerPlane structure.\n");
                return 0;
            }
            hFile->write(pSuper->pPlane, sizeof(W8LevelFilePlane));
        }

        hFile->write(&pSuper->has_hotspot, 1);
        if (pSuper->has_hotspot != 0) {
            if (pSuper->pHotSpot == 0) {
                ReportBuildStatus(7, "WriteSuperTrigger: No Trigger HotSpot structure.\n");
                return 0;
            }
            hFile->write(pSuper->pHotSpot, sizeof(W8LevelFileTriggerHotSpot));
        }
    }

    hFile->write(&pSuper->has_door, 1);
    if (pSuper->has_door != 0) {
        hFile->write(&pSuper->door.kind, 1);
        if (pSuper->door.kind == 1) {
            if (!WriteDoorTriggerFile(hFile, &pSuper->door))
                return FALSE;
        } else if (pSuper->door.kind == 2) {
            W8LevelFileLinkedRecord* pRecord = pSuper->pRecord;

            if (pRecord == 0) {

            } else {
                hFile->write(&pRecord->kind, 1);
                hFile->write(pRecord->vertices, sizeof(pRecord->vertices));
                hFile->write(&pRecord->linked_face, 2);
            }
        }
    }

    return TRUE;
}

// FUNCTION: WIZ8 0x004D3540
BOOLEAN ReadDoorTriggerFile(wiz8::File* hFile, W8LevelFileDoorRef* pDoor)
try {
    pDoor->door = nullptr;
    const auto cleanup = [](W8LevelFileDoorRef* record) { ReleaseLevelRecord(*record); };
    std::unique_ptr<W8LevelFileDoorRef, decltype(cleanup)> owner(pDoor, cleanup);

    W8LevelFileDoor* pDoorRec = static_cast<W8LevelFileDoor*>(calloc(1, sizeof(W8LevelFileDoor)));
    if (pDoorRec == 0) {
        return 0;
    }
    pDoor->door = pDoorRec;
    hFile->read_exact(&pDoorRec->version, 1);
    hFile->read_exact(&pDoorRec->flags[0], 1);
    hFile->read_exact(&pDoorRec->flags[1], 1);
    hFile->read_exact(&pDoorRec->flags[2], 1);
    hFile->read_exact(&pDoorRec->flags[3], 1);
    hFile->read_exact(&pDoorRec->flags[4], 1);
    hFile->read_exact(&pDoorRec->flags[5], 1);
    hFile->read_exact(&pDoorRec->flags[6], 1);
    hFile->read_exact(&pDoorRec->flags[7], 1);
    hFile->read_exact(&pDoorRec->flags[8], 1);
    hFile->read_exact(&pDoorRec->item, 2);
    hFile->read_exact(&pDoorRec->has_position, 1);
    hFile->read_exact(&pDoorRec->position, sizeof(pDoorRec->position));
    hFile->read_exact(pDoorRec->linked_trigger, 0x80);
    pDoor->door = pDoorRec;
    static_cast<void>(owner.release());
    return TRUE;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x004D3660
BOOLEAN WriteDoorTriggerFile(wiz8::File* hFile, W8LevelFileDoorRef* pDoor)
{
    const auto cleanup = [](W8LevelFileDoorRef* record) { ReleaseLevelRecord(*record); };
    std::unique_ptr<W8LevelFileDoorRef, decltype(cleanup)> owner(pDoor, cleanup);

    W8LevelFileDoor* pDoorRec = pDoor->door;
    if (pDoorRec == 0) {
        return 0;
    }
    hFile->write(&pDoorRec->version, 1);
    hFile->write(&pDoorRec->flags[0], 1);
    hFile->write(&pDoorRec->flags[1], 1);
    hFile->write(&pDoorRec->flags[2], 1);
    hFile->write(&pDoorRec->flags[3], 1);
    hFile->write(&pDoorRec->flags[4], 1);
    hFile->write(&pDoorRec->flags[5], 1);
    hFile->write(&pDoorRec->flags[6], 1);
    hFile->write(&pDoorRec->flags[7], 1);
    hFile->write(&pDoorRec->flags[8], 1);
    hFile->write(&pDoorRec->item, 2);
    hFile->write(&pDoorRec->has_position, 1);
    hFile->write(&pDoorRec->position, sizeof(pDoorRec->position));
    hFile->write(pDoorRec->linked_trigger, 0x80);

    return TRUE;
}

// FUNCTION: WIZ8 0x004D3770
BOOLEAN ReadPathAIFile(wiz8::File* hFile, W8LevelFilePathAI* pPathAI)
try {
    memset(pPathAI, 0, sizeof(*pPathAI));
    const auto cleanup = [](W8LevelFilePathAI* record) { ReleaseLevelRecord(*record); };
    std::unique_ptr<W8LevelFilePathAI, decltype(cleanup)> owner(pPathAI, cleanup);

    hFile->read_exact(&pPathAI->version, 1);
    hFile->read_exact(&pPathAI->scaled, 1);
    hFile->read_exact(&pPathAI->position, 4);
    hFile->read_exact(pPathAI->unknown_06, 4);
    hFile->read_exact(&pPathAI->path_count, 4);
    ValidateRecordCount(pPathAI->path_count, sizeof(W8LevelFileScaledPathNode),
                        hFile->size() - hFile->tell());
    if (pPathAI->scaled == 2) {
        if (pPathAI->path_count != 0) {
            pPathAI->pScaledPaths = static_cast<W8LevelFileScaledPathNode*>(
                calloc(1, pPathAI->path_count * sizeof(W8LevelFileScaledPathNode)));
            if (pPathAI->pScaledPaths == 0) {
                throw std::bad_alloc();
            }
            hFile->read_exact(pPathAI->pScaledPaths,
                              pPathAI->path_count * sizeof(W8LevelFileScaledPathNode));
        }
    } else if (pPathAI->path_count != 0) {
        pPathAI->pPaths = static_cast<W8LevelFilePathNode*>(
            calloc(1, pPathAI->path_count * sizeof(W8LevelFilePathNode)));
        if (pPathAI->pPaths == 0) {
            throw std::bad_alloc();
        }
        hFile->read_exact(pPathAI->pPaths, pPathAI->path_count * sizeof(W8LevelFilePathNode));
    }
    static_cast<void>(owner.release());
    return TRUE;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x004D38E0
BOOLEAN WritePathAIFile(wiz8::File* hFile, W8LevelFilePathAI* pPathAI)
{
    const auto cleanup = [](W8LevelFilePathAI* record) { ReleaseLevelRecord(*record); };
    std::unique_ptr<W8LevelFilePathAI, decltype(cleanup)> owner(pPathAI, cleanup);

    hFile->write(&pPathAI->version, 1);
    hFile->write(&pPathAI->scaled, 1);
    hFile->write(&pPathAI->position, 4);
    hFile->write(pPathAI->unknown_06, 4);
    hFile->write(&pPathAI->path_count, 4);
    if (pPathAI->scaled == 2) {
        if (pPathAI->path_count != 0) {
            if (pPathAI->pScaledPaths == 0) {
                throw std::bad_alloc();
            }
            hFile->write(pPathAI->pScaledPaths,
                         pPathAI->path_count * sizeof(W8LevelFileScaledPathNode));
        }
    } else if (pPathAI->path_count != 0) {
        if (pPathAI->pPaths == 0) {
            throw std::bad_alloc();
        }
        hFile->write(pPathAI->pPaths, pPathAI->path_count * sizeof(W8LevelFilePathNode));
    }
    return TRUE;
}

// FUNCTION: WIZ8 0x004D3A10
BOOLEAN ReadAnimObjFile(wiz8::File* hFile, W8LevelFileAnimObj* pAnimObj)
try {
    memset(pAnimObj, 0, sizeof(*pAnimObj));

    const auto cleanup = [](W8LevelFileAnimObj* record) { ReleaseLevelRecord(*record); };
    std::unique_ptr<W8LevelFileAnimObj, decltype(cleanup)> owner(pAnimObj, cleanup);

    unsigned short usFrame;
    short i;
    short j;

    hFile->read_exact(&pAnimObj->version, 1);
    hFile->read_exact(&pAnimObj->num_anims, 1);
    ValidateRecordCount(pAnimObj->num_anims, sizeof(W8LevelFileMorph),
                        hFile->size() - hFile->tell());
    hFile->read_exact(&pAnimObj->animation_playing, 1);
    hFile->read_exact(&pAnimObj->frame_method, 1);
    hFile->read_exact(&pAnimObj->behaviour, 1);
    hFile->read_exact(&pAnimObj->cycle, 1);
    hFile->read_exact(&pAnimObj->path_lists, 1);
    if (pAnimObj->version >= 3) {
        hFile->read_exact(&pAnimObj->playback_scale, 4);
    } else {
        pAnimObj->playback_scale = 15.0f;
    }
    if (pAnimObj->version >= 5) {
        hFile->read_exact(&pAnimObj->start_frame, 1);
    } else {
        pAnimObj->start_frame = 0;
    }
    if (pAnimObj->version >= 6) {
        hFile->read_exact(&pAnimObj->random_play, 1);
        hFile->read_exact(&pAnimObj->play_chance, 4);
    } else {
        pAnimObj->random_play = 0;
        pAnimObj->play_chance = 1.0f;
    }
    hFile->read_exact(pAnimObj->discarded, 0x32);
    if (pAnimObj->num_anims != 0) {
        pAnimObj->abHowMany = static_cast<char*>(calloc(1, pAnimObj->num_anims));
        if (pAnimObj->abHowMany == 0) {
            throw std::bad_alloc();
        }
        hFile->read_exact(pAnimObj->abHowMany, pAnimObj->num_anims);
    }
    if (pAnimObj->version >= 7) {
        hFile->read_exact(&pAnimObj->num_bound_box, 1);
        ValidateRecordCount(pAnimObj->num_bound_box, sizeof(W8LevelFileBounds),
                            hFile->size() - hFile->tell());
        if (pAnimObj->num_bound_box != 0) {
            pAnimObj->pBoundBox = static_cast<W8LevelFileBounds*>(
                calloc(1, pAnimObj->num_bound_box * sizeof(W8LevelFileBounds)));
            if (pAnimObj->pBoundBox == 0) {
                throw std::bad_alloc();
            }
            hFile->read_exact(pAnimObj->pBoundBox,
                              pAnimObj->num_bound_box * sizeof(W8LevelFileBounds));
        }
    }

    if (pAnimObj->version >= 8) {
        hFile->read_exact(&pAnimObj->num_anim_lights, 1);
        ValidateRecordCount(pAnimObj->num_anim_lights, sizeof(W8LevelFileAnimLight),
                            hFile->size() - hFile->tell());

        if (pAnimObj->num_anim_lights != 0) {
            pAnimObj->pAnimLights = static_cast<W8LevelFileAnimLight*>(
                calloc(1, pAnimObj->num_anim_lights * sizeof(W8LevelFileAnimLight)));
            if (pAnimObj->pAnimLights == 0) {
                return FALSE;
            }
            memset(pAnimObj->pAnimLights, 0,
                   pAnimObj->num_anim_lights * sizeof(W8LevelFileAnimLight));
            for (i = 0; i < pAnimObj->num_anim_lights; ++i) {
                if (!ReadAnimLightFile(hFile, pAnimObj->pAnimLights + i))
                    return FALSE;
            }
        }
    }
    if (pAnimObj->path_lists == 0) {
        if (pAnimObj->version >= 9) {
            hFile->read_exact(&pAnimObj->has_path_ai, 1);
        }
        if (pAnimObj->has_path_ai != 0) {
            pAnimObj->pPathAI =
                static_cast<W8LevelFilePathAI*>(calloc(1, sizeof(W8LevelFilePathAI)));
            if (pAnimObj->pPathAI == 0) {
                return FALSE;
            }
            if (!ReadPathAIFile(hFile, pAnimObj->pPathAI))
                return FALSE;
        }
        if (pAnimObj->num_anims != 0) {
            pAnimObj->pMorphs = static_cast<W8LevelFileMorph*>(
                calloc(1, pAnimObj->num_anims * sizeof(W8LevelFileMorph)));
            if (pAnimObj->pMorphs == 0) {
                throw std::bad_alloc();
            }

            for (i = 0; i < pAnimObj->num_anims; ++i) {
                W8LevelFileMorph* pMorph = pAnimObj->pMorphs + i;
                hFile->read_exact(&pMorph->channel, 1);
                hFile->read_exact(&pMorph->num_frames, 1);
                ValidateRecordCount(pMorph->num_frames, sizeof(W8LevelFileFrame),
                                    hFile->size() - hFile->tell());
                if (pMorph->num_frames != 0) {
                    pMorph->LODMesh.pFrames = static_cast<W8LevelFileFrame*>(
                        calloc(1, pMorph->num_frames * sizeof(W8LevelFileFrame)));
                    if (pMorph->LODMesh.pFrames == 0) {
                        throw std::bad_alloc();
                    }
                    memset(pMorph->LODMesh.pFrames, 0,
                           pMorph->num_frames * sizeof(W8LevelFileFrame));
                    if (pMorph->num_frames != 0) {
                        usFrame = 0;
                        do {
                            W8LevelFileFrame* pFrame =
                                pMorph->LODMesh.pFrames + static_cast<short>(usFrame);
                            hFile->read_exact(&pFrame->flags, 1);
                            if (!ReadMeshFile(hFile, &pFrame->mesh))
                                return FALSE;
                            hFile->read_exact(&pFrame->num_textures, 2);
                            ValidateRecordCount(pFrame->num_textures, sizeof(W8MaterialRecord),
                                                hFile->size() - hFile->tell());
                            if (pFrame->num_textures != 0) {
                                pFrame->pTextures = static_cast<W8MaterialRecord*>(
                                    calloc(1, pFrame->num_textures * sizeof(W8MaterialRecord)));
                                if (pFrame->pTextures == 0) {
                                    throw std::bad_alloc();
                                }
                                memset(pFrame->pTextures, 0,
                                       pFrame->num_textures * sizeof(W8MaterialRecord));
                                for (j = 0; j < pFrame->num_textures; ++j) {
                                    W8MaterialRecord* pTexture = pFrame->pTextures + j;
                                    if (!ReadMaterialRecord(hFile, pTexture))
                                        return FALSE;
                                }
                            }
                            if ((usFrame == 0) && ((pMorph->LODMesh.pFrames->mesh.flags &
                                                    W8_LEVEL_MESH_LOD_VERTICES) != 0)) {
                                usFrame = pMorph->num_frames;
                            }
                            ++usFrame;
                        } while (static_cast<short>(usFrame) <
                                 static_cast<short>(pMorph->num_frames));
                    }
                }
            }
        }
    } else {
        hFile->read_exact(&pAnimObj->num_transforms, 1);
        ValidateRecordCount(pAnimObj->num_transforms, sizeof(W8LevelFileTransform),
                            hFile->size() - hFile->tell());
        if (pAnimObj->num_transforms != 0) {
            pAnimObj->pTransforms = static_cast<W8LevelFileTransform*>(
                calloc(1, pAnimObj->num_transforms * sizeof(W8LevelFileTransform)));
            if (pAnimObj->pTransforms == 0) {
                throw std::bad_alloc();
            }
            memset(pAnimObj->pTransforms, 0,
                   pAnimObj->num_transforms * sizeof(W8LevelFileTransform));
            for (i = 0; i < pAnimObj->num_transforms; ++i) {
                W8LevelFileTransform* pTransform = pAnimObj->pTransforms + i;
                hFile->read_exact(&pTransform->channel, 1);
                hFile->read_exact(&pTransform->num_frames, 1);
                ValidateRecordCount(pTransform->num_frames, sizeof(W8LevelFileFrame),
                                    hFile->size() - hFile->tell());
                if (pTransform->num_frames != 0) {
                    pTransform->LODMesh.pFrames = static_cast<W8LevelFileFrame*>(
                        calloc(1, pTransform->num_frames * sizeof(W8LevelFileFrame)));
                    if (pTransform->LODMesh.pFrames == 0) {
                        throw std::bad_alloc();
                    }
                    memset(pTransform->LODMesh.pFrames, 0,
                           pTransform->num_frames * sizeof(W8LevelFileFrame));
                    if (pTransform->num_frames != 0) {
                        short iFrame = 0;
                        do {
                            W8LevelFileFrame* pFrame = pTransform->LODMesh.pFrames + iFrame;
                            hFile->read_exact(&pFrame->flags, 1);
                            if (!ReadMeshFile(hFile, &pFrame->mesh))
                                return FALSE;
                            hFile->read_exact(&pFrame->num_textures, 2);
                            ValidateRecordCount(pFrame->num_textures, sizeof(W8MaterialRecord),
                                                hFile->size() - hFile->tell());
                            if ((pFrame->num_textures < 0) || (pFrame->num_textures > 500)) {
                                sprintf(g_level_file_error,
                                        "Invalid number of materials in mesh (%d materials).\n",
                                        static_cast<int>(pFrame->num_textures));
                                ReportBuildStatus(7, g_level_file_error);
                                return FALSE;
                            }
                            if (pFrame->num_textures != 0) {
                                pFrame->pTextures = static_cast<W8MaterialRecord*>(
                                    calloc(1, pFrame->num_textures * sizeof(W8MaterialRecord)));
                                if (pFrame->pTextures == 0) {
                                    throw std::bad_alloc();
                                }
                                memset(pFrame->pTextures, 0,
                                       pFrame->num_textures * sizeof(W8MaterialRecord));
                                for (j = 0; j < pFrame->num_textures; ++j) {
                                    W8MaterialRecord* pTexture = pFrame->pTextures + j;
                                    if (!ReadMaterialRecord(hFile, pTexture))
                                        return FALSE;
                                }
                            }
                            ++iFrame;
                        } while (iFrame < static_cast<short>(pTransform->num_frames));
                    }
                }
                if (!ReadPathAIFile(hFile, &pTransform->pathAI))
                    return FALSE;
            }
        }
    }
    static_cast<void>(owner.release());
    return TRUE;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x004D4480
BOOLEAN WriteAnimObjFile(wiz8::File* hFile, W8LevelFileAnimObj* pAnimObj)
{
    const auto cleanup = [](W8LevelFileAnimObj* record) { ReleaseLevelRecord(*record); };
    std::unique_ptr<W8LevelFileAnimObj, decltype(cleanup)> owner(pAnimObj, cleanup);

    unsigned short usFrame;
    short i;
    short j;

    hFile->write(&pAnimObj->version, 1);
    hFile->write(&pAnimObj->num_anims, 1);
    hFile->write(&pAnimObj->animation_playing, 1);
    hFile->write(&pAnimObj->frame_method, 1);
    hFile->write(&pAnimObj->behaviour, 1);
    hFile->write(&pAnimObj->cycle, 1);
    hFile->write(&pAnimObj->path_lists, 1);
    if (pAnimObj->version >= 3) {
        hFile->write(&pAnimObj->playback_scale, 4);
    }
    if (pAnimObj->version >= 5) {
        hFile->write(&pAnimObj->start_frame, 1);
    }
    if (pAnimObj->version >= 6) {
        hFile->write(&pAnimObj->random_play, 1);
        hFile->write(&pAnimObj->play_chance, 4);
    }
    hFile->write(pAnimObj->discarded, 0x32);
    if (pAnimObj->num_anims != 0) {
        if (pAnimObj->abHowMany == 0) {
            throw std::bad_alloc();
        }
        hFile->write(pAnimObj->abHowMany, pAnimObj->num_anims);
    }
    if (pAnimObj->version >= 7) {
        hFile->write(&pAnimObj->num_bound_box, 1);
        if (pAnimObj->num_bound_box != 0) {
            if (pAnimObj->pBoundBox == 0) {
                throw std::bad_alloc();
            }
            hFile->write(pAnimObj->pBoundBox, pAnimObj->num_bound_box * sizeof(W8LevelFileBounds));
        }
    }

    if (pAnimObj->version >= 8) {
        hFile->write(&pAnimObj->num_anim_lights, 1);
        if ((pAnimObj->num_anim_lights != 0) && (pAnimObj->pAnimLights != 0)) {
            for (i = 0; i < pAnimObj->num_anim_lights; ++i) {
                if (!WriteAnimLightFile(hFile, pAnimObj->pAnimLights + i))
                    return FALSE;
            }
        }
    }
    if (pAnimObj->path_lists == 0) {
        if (pAnimObj->version >= 9) {
            hFile->write(&pAnimObj->has_path_ai, 1);
            if (pAnimObj->has_path_ai != 0 &&
                (!pAnimObj->pPathAI || !WritePathAIFile(hFile, pAnimObj->pPathAI)))
                return FALSE;
        }
        if (pAnimObj->num_anims != 0) {
            if (pAnimObj->pMorphs == 0) {
                throw std::bad_alloc();
            }
            for (i = 0; i < pAnimObj->num_anims; ++i) {
                W8LevelFileMorph* pMorph = pAnimObj->pMorphs + i;
                hFile->write(&pMorph->channel, 1);
                hFile->write(&pMorph->num_frames, 1);
                if (pMorph->num_frames != 0) {
                    if (pMorph->LODMesh.pFrames == 0) {
                        throw std::bad_alloc();
                    }
                    usFrame = 0;
                    do {
                        W8LevelFileFrame* pFrame =
                            pMorph->LODMesh.pFrames + static_cast<short>(usFrame);
                        hFile->write(&pFrame->flags, 1);
                        if (!WriteMeshFile(hFile, &pFrame->mesh))
                            return FALSE;
                        hFile->write(&pFrame->num_textures, 2);
                        if (pFrame->num_textures != 0) {
                            if (pFrame->pTextures == 0) {
                                throw std::bad_alloc();
                            }

                            for (j = 0; j < pFrame->num_textures; ++j) {
                                W8MaterialRecord* pTexture = pFrame->pTextures + j;
                                if (!WriteMaterialRecord(hFile, pTexture))
                                    return FALSE;
                            }
                        }
                        if ((usFrame == 0) && ((pMorph->LODMesh.pFrames->mesh.flags &
                                                W8_LEVEL_MESH_LOD_VERTICES) != 0)) {
                            usFrame = pMorph->num_frames;
                        }
                        ++usFrame;
                    } while (static_cast<short>(usFrame) < static_cast<short>(pMorph->num_frames));
                }
            }

            return TRUE;
        }
    } else {
        hFile->write(&pAnimObj->num_transforms, 1);
        if (pAnimObj->num_transforms != 0) {
            if (pAnimObj->pTransforms == 0) {
                throw std::bad_alloc();
            }
            for (i = 0; i < pAnimObj->num_transforms; ++i) {
                W8LevelFileTransform* pTransform = pAnimObj->pTransforms + i;
                hFile->write(&pTransform->channel, 1);
                hFile->write(&pTransform->num_frames, 1);
                if (pTransform->num_frames != 0) {
                    if (pTransform->LODMesh.pFrames == 0) {
                        throw std::bad_alloc();
                    }
                    short iFrame = 0;
                    do {
                        W8LevelFileFrame* pFrame = pTransform->LODMesh.pFrames + iFrame;
                        hFile->write(&pFrame->flags, 1);
                        if (!WriteMeshFile(hFile, &pFrame->mesh))
                            return FALSE;
                        hFile->write(&pFrame->num_textures, 2);
                        if (pFrame->num_textures != 0) {
                            if (pFrame->pTextures == 0) {
                                throw std::bad_alloc();
                            }

                            for (j = 0; j < pFrame->num_textures; ++j) {
                                W8MaterialRecord* pTexture = pFrame->pTextures + j;
                                if (!WriteMaterialRecord(hFile, pTexture))
                                    return FALSE;
                            }
                        }
                        ++iFrame;
                    } while (iFrame < static_cast<short>(pTransform->num_frames));
                }
                if (!WritePathAIFile(hFile, &pTransform->pathAI))
                    return FALSE;
            }
        }
    }
    return TRUE;
}

// FUNCTION: WIZ8 0x004D4CB0
W8LevelFileProp* ReadPropsFile(wiz8::File* hFile, int count)
try {
    LevelRegistryTransaction registries;

    ValidateRecordCount(count, sizeof(W8LevelFileProp), hFile->size() - hFile->tell());
    if (count == 0) {
        return 0;
    }
    W8LevelFileProp* pProps =
        static_cast<W8LevelFileProp*>(calloc(1, count * sizeof(W8LevelFileProp)));
    if (pProps == 0) {
        throw std::bad_alloc();
    }
    const auto cleanup = [count](W8LevelFileProp* props) { ReleaseLevelProps(props, count); };
    std::unique_ptr<W8LevelFileProp, decltype(cleanup)> owner(pProps, cleanup);

    for (int i = 0; i < count; ++i) {
        W8LevelFileProp* pProp = pProps + i;
        hFile->read_exact(&pProp->version, 1);
        hFile->read_exact(&pProp->bNumFrames, 1);
        if (pProp->version >= 5) {
            hFile->read_exact(&pProp->option, 1);
            hFile->read_exact(&pProp->position, sizeof(pProp->position));
        }
        if (pProp->version >= 6) {
            hFile->read_exact(&pProp->flags, 4);
        }
        if (pProp->version >= 7) {
            hFile->read_exact(pProp->name, sizeof(pProp->name));
            ReportBuildStatus(5, FormatString("Prop: %s\n", pProp->name));
        }
        if (pProp->version >= 8) {
            hFile->read_exact(&pProp->num_frame_pos, 1);
            ValidateRecordCount(pProp->num_frame_pos, sizeof(W8LevelFileFramePosition),
                                hFile->size() - hFile->tell());
            if (pProp->num_frame_pos != 0) {
                pProp->usFrame_Pos = static_cast<W8LevelFileFramePosition*>(
                    calloc(1, pProp->num_frame_pos * sizeof(W8LevelFileFramePosition)));
                if (pProp->usFrame_Pos == 0) {
                    throw std::bad_alloc();
                }
                hFile->read_exact(pProp->usFrame_Pos, pProp->num_frame_pos << 2);
            }
        }
        if (!ReadAnimObjFile(hFile, &pProp->anim_obj))
            return nullptr;
        hFile->read_exact(&pProp->has_trigger, 1);
        if (pProp->has_trigger != 0) {
            pProp->pTrigger = static_cast<W8LevelFileTrigger*>(calloc(1, sizeof(*pProp->pTrigger)));
            if (pProp->pTrigger == 0) {
                throw std::bad_alloc();
            }
            memset(pProp->pTrigger, 0, sizeof(W8LevelFileTrigger));
            if (!ReadTriggerFile(hFile, pProp->pTrigger))
                return nullptr;
        }
        if (pProp->version >= 9) {
            hFile->read_exact(&pProp->has_footsteps, 1);
            if (pProp->has_footsteps != 0) {
                hFile->read_exact(&pProp->footstep_surface, 1);
                hFile->read_exact(&pProp->footstep_material, 1);
            }
        }
    }

    registries.commit();
    return owner.release();
} catch (const std::exception&) {
    return nullptr;
}

// FUNCTION: WIZ8 0x004D4FC0
BOOLEAN WritePropsFile(wiz8::File* hFile, int count, W8LevelFileProp* pProps)
try {
    const auto cleanup = [count](W8LevelFileProp* props) { ReleaseLevelProps(props, count); };
    std::unique_ptr<W8LevelFileProp, decltype(cleanup)> owner(pProps, cleanup);

    if (count == 0) {
        return TRUE;
    }
    if (pProps == 0) {
        throw std::bad_alloc();
    }
    for (int i = 0; i < count; ++i) {
        W8LevelFileProp* pProp = pProps + i;
        hFile->write(&pProp->version, 1);
        hFile->write(&pProp->bNumFrames, 1);
        if (pProp->version >= 5) {
            hFile->write(&pProp->option, 1);
            hFile->write(&pProp->position, sizeof(pProp->position));
        }
        if (pProp->version >= 6) {
            hFile->write(&pProp->flags, 4);
        }
        if (pProp->version >= 7) {
            hFile->write(pProp->name, sizeof(pProp->name));
        }
        if (pProp->version >= 8) {
            hFile->write(&pProp->num_frame_pos, 1);
            if (pProp->num_frame_pos != 0) {
                if (pProp->usFrame_Pos == 0) {
                    throw std::bad_alloc();
                }
                hFile->write(pProp->usFrame_Pos, pProp->num_frame_pos << 2);
            }
        }
        if (!WriteAnimObjFile(hFile, &pProp->anim_obj))
            return FALSE;
        hFile->write(&pProp->has_trigger, 1);
        if (pProp->has_trigger != 0) {
            if (!WriteTriggerFile(hFile, pProp->pTrigger))
                return FALSE;
        }
        if (pProp->version >= 9) {
            hFile->write(&pProp->has_footsteps, 1);
            if (pProp->has_footsteps != 0) {
                hFile->write(&pProp->footstep_surface, 1);
                hFile->write(&pProp->footstep_material, 1);
            }
        }
    }

    return TRUE;
} catch (const std::exception&) {
    return FALSE;
}

// FUNCTION: WIZ8 0x004D5240
BOOLEAN ReadParticleSystemFile(wiz8::File* hFile, W8LevelFileParticleSystem* pSystem)
try {

    hFile->read_exact(pSystem, 0x217);
    if (pSystem->version > 1) {
        hFile->read_exact(&pSystem->particle.attachment_key, 2);
    } else {
        pSystem->particle.attachment_key = 0;
    }
    if (pSystem->version > 2) {
        hFile->read_exact(&pSystem->particle.emission_limit, 4);
        hFile->read_exact(&pSystem->particle.requires_sorted_renderer, 1);
    } else {
        pSystem->particle.emission_limit = 0;
        pSystem->particle.requires_sorted_renderer = 0;
    }
    if (pSystem->version > 3) {
        hFile->read_exact(&pSystem->particle.start_frame, 4);
        hFile->read_exact(&pSystem->particle.end_frame, 4);
    } else {
        pSystem->particle.start_frame = 0;
    }
    ReportBuildStatus(5, FormatString("Particle System: %s, Position %f, %f, %f\n",
                                      pSystem->particle.name,
                                      pSystem->particle.location.x * g_world_scale,
                                      pSystem->particle.location.y * g_world_scale,
                                      pSystem->particle.location.z * g_world_scale));
    return TRUE;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x004D5370
BOOLEAN WriteParticleSystemFile(wiz8::File* hFile, W8LevelFileParticleSystem* pSystem)
{

    hFile->write(pSystem, 0x217);
    if (pSystem->version > 1) {
        hFile->write(&pSystem->particle.attachment_key, 2);
    }
    if (pSystem->version > 2) {
        hFile->write(&pSystem->particle.emission_limit, 4);
        hFile->write(&pSystem->particle.requires_sorted_renderer, 1);
    }
    if (pSystem->version > 3) {
        hFile->write(&pSystem->particle.start_frame, 4);
        hFile->write(&pSystem->particle.end_frame, 4);
    }
    return TRUE;
}

// FUNCTION: WIZ8 0x004D5430
BOOLEAN ReadLevelFileBlock(wiz8::File* hFile, W8LevelFileBlock* pBlock)
try {

    hFile->read_exact(&pBlock->fog_enabled, 1);
    hFile->read_exact(&pBlock->environment_red, 4);
    hFile->read_exact(&pBlock->environment_green, 4);
    hFile->read_exact(&pBlock->environment_blue, 4);
    hFile->read_exact(&pBlock->intensity, 4);
    hFile->read_exact(&pBlock->view_distance, 4);
    hFile->read_exact(&pBlock->camera_mode, 1);
    if (pBlock->camera_mode >= 1) {
        hFile->read_exact(&pBlock->camera_position, sizeof(pBlock->camera_position));
    }
    if (pBlock->camera_mode >= 2) {
        hFile->read_exact(&pBlock->camera_angle, 4);
        hFile->read_exact(&pBlock->camera_axis, sizeof(pBlock->camera_axis));
    }
    hFile->read_exact(&pBlock->has_light_colours, 1);
    if (pBlock->has_light_colours != 0) {
        hFile->read_exact(pBlock->light_colours, 0x300);
    }
    hFile->read_exact(&pBlock->has_environment_colours, 1);
    if (pBlock->has_environment_colours != 0) {
        hFile->read_exact(pBlock->environment_colours, 0x300);
    }
    return TRUE;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x004D5580
BOOLEAN WriteLevelFileBlock(wiz8::File* hFile, W8LevelFileBlock* pBlock)
{

    hFile->write(&pBlock->fog_enabled, 1);
    hFile->write(&pBlock->environment_red, 4);
    hFile->write(&pBlock->environment_green, 4);
    hFile->write(&pBlock->environment_blue, 4);
    hFile->write(&pBlock->intensity, 4);
    hFile->write(&pBlock->view_distance, 4);
    hFile->write(&pBlock->camera_mode, 1);
    if (pBlock->camera_mode >= 1) {
        hFile->write(&pBlock->camera_position, sizeof(pBlock->camera_position));
    }
    if (pBlock->camera_mode >= 2) {
        hFile->write(&pBlock->camera_angle, 4);
        hFile->write(&pBlock->camera_axis, sizeof(pBlock->camera_axis));
    }
    hFile->write(&pBlock->has_light_colours, 1);
    if (pBlock->has_light_colours != 0) {
        hFile->write(pBlock->light_colours, 0x300);
    }
    hFile->write(&pBlock->has_environment_colours, 1);
    if (pBlock->has_environment_colours != 0) {
        hFile->write(pBlock->environment_colours, 0x300);
    }
    return TRUE;
}
