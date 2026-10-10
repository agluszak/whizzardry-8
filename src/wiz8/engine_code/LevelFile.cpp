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

static unsigned char ReadMaterialRecord(wiz8::File* file, W8MaterialRecord* material)
try
{
    unsigned char success = (file->read(material, 0x11a).bytes == static_cast<std::size_t>(0x11a));
    if (material->version >= 4) {
        success &= (file->read(material->texture_modes, 0x10).bytes == static_cast<std::size_t>(0x10));
    }
    return success;
}
catch (const std::exception&) { return false; }

static unsigned char WriteMaterialRecord(wiz8::File* file, W8MaterialRecord* material)
{
    unsigned char success = (file->write(material, 0x11a), true);
    if (material->version >= 4) {
        success &= (file->write(material->texture_modes, 0x10), true);
    }
    return success;
}

// FUNCTION: WIZ8 0x004CFDC0
W8LevelFile* ReadLevelFile(wiz8::File* hFile)
{
    W8LevelFile* pLevel = static_cast<W8LevelFile*>(malloc(sizeof(W8LevelFile)));
    if (pLevel == 0) {
        srAssertFail("pLevel", LEVELFILE_CPP, 0x2b, 0);
    }
    memset(pLevel, 0, sizeof(W8LevelFile));
    pLevel->submesh_count = 1;
    pLevel->mesh_count = 1;
    pLevel->num_switch_triggers = 0;
    memset(pLevel->switch_triggers, 0, sizeof(pLevel->switch_triggers));
    pLevel->num_invisible_planes = 0;
    memset(pLevel->invisible_planes, 0, sizeof(pLevel->invisible_planes));
    pLevel->num_linked_records = 0;
    memset(pLevel->linked_records, 0, sizeof(pLevel->linked_records));
    g_level_file = pLevel;

    pLevel->pMeshes = static_cast<W8LevelFileMesh*>(malloc(sizeof(W8LevelFileMesh)));
    if (pLevel->pMeshes == 0) {
        srAssertFail("pLevel->pMeshes", LEVELFILE_CPP, 0x38, 0);
    }
    memset(pLevel->pMeshes, 0, sizeof(W8LevelFileMesh));
    ReadMeshFile(hFile, pLevel->pMeshes);

    hFile->read_exact(&pLevel->nTextures, sizeof(pLevel->nTextures));
    if (pLevel->nTextures == 0) {
        return 0;
    }
    pLevel->pTextures =
        static_cast<W8MaterialRecord*>(malloc(pLevel->nTextures * sizeof(W8MaterialRecord)));
    if (pLevel->pTextures == 0) {
        srAssertFail("pLevel->pTextures", LEVELFILE_CPP, 0x41, 0);
    }
    memset(pLevel->pTextures, 0, pLevel->nTextures * sizeof(W8MaterialRecord));
    unsigned char fSuccess = 1;
    unsigned char ok;
    int i;
    for (i = 0; i < pLevel->nTextures; ++i) {
        W8MaterialRecord* pTexture = pLevel->pTextures + i;
        ok = ReadMaterialRecord(hFile, pTexture);
        if (ok == 0) {
            return 0;
        }
    }
    if (ok == 0) {
        return 0;
    }

    hFile->read_exact(&pLevel->nLights, sizeof(pLevel->nLights));
    if (pLevel->nLights != 0) {
        pLevel->pLights =
            static_cast<W8LevelFileLight*>(malloc(pLevel->nLights * sizeof(W8LevelFileLight)));
        if (pLevel->pLights == 0) {
            srAssertFail("pLevel->pLights", LEVELFILE_CPP, 0x4d, 0);
        }
        memset(pLevel->pLights, 0, pLevel->nLights * sizeof(W8LevelFileLight));
        for (i = 0; i < pLevel->nLights; ++i) {
            if (ReadLightFile(hFile, pLevel->pLights + i) == 0) {
                return 0;
            }
        }
    }

    fSuccess = (hFile->read(&pLevel->nMonsters, sizeof(pLevel->nMonsters)).bytes == static_cast<std::size_t>(sizeof(pLevel->nMonsters)));
    if (pLevel->nMonsters != 0) {
        pLevel->pMonsters = static_cast<W8LevelFileMonster*>(
            malloc(pLevel->nMonsters * sizeof(W8LevelFileMonster)));
        if (pLevel->pMonsters == 0) {
            srAssertFail("pLevel->pMonsters", LEVELFILE_CPP, 0x5d, 0);
        }
        memset(pLevel->pMonsters, 0, pLevel->nMonsters * sizeof(W8LevelFileMonster));
        for (i = 0; i < pLevel->nMonsters; ++i) {
            W8LevelFileMonster* pMonster = pLevel->pMonsters + i;
            fSuccess &= (hFile->read(pMonster, 0x22).bytes == static_cast<std::size_t>(0x22));
            if (fSuccess == 0) {
                return 0;
            }
            if (pMonster->num_mon_path != 0) {
                pMonster->MonPath = static_cast<W8LevelFilePathNode*>(
                    malloc(pMonster->num_mon_path * sizeof(W8LevelFilePathNode)));
                if (pMonster->MonPath == 0) {
                    srAssertFail("pLevel->pMonsters[i1].MonPath", LEVELFILE_CPP, 0x69, 0);
                }
                memset(pMonster->MonPath, 0, pMonster->num_mon_path * sizeof(W8LevelFilePathNode));
                fSuccess &= (hFile->read(pMonster->MonPath, pMonster->num_mon_path * sizeof(W8LevelFilePathNode)).bytes == static_cast<std::size_t>(pMonster->num_mon_path * sizeof(W8LevelFilePathNode)));
                if (fSuccess == 0) {
                    return 0;
                }
            }
        }
    }

    hFile->read_exact(&pLevel->nItems, sizeof(pLevel->nItems));
    if (pLevel->nItems != 0) {
        pLevel->pItems = static_cast<W8LevelFileItemRecord*>(
            malloc(pLevel->nItems * sizeof(W8LevelFileItemRecord)));
        if (pLevel->pItems == 0) {
            srAssertFail("pLevel->pItems", LEVELFILE_CPP, 0x79, 0);
        }
        memset(pLevel->pItems, 0, pLevel->nItems * sizeof(W8LevelFileItemRecord));
        if ((hFile->read(pLevel->pItems, pLevel->nItems * sizeof(W8LevelFileItemRecord)).bytes == static_cast<std::size_t>(pLevel->nItems * sizeof(W8LevelFileItemRecord))) ==
            0) {
            return 0;
        }
    }

    hFile->read_exact(&pLevel->missile_count, sizeof(pLevel->missile_count));
    fSuccess = (hFile->read(&pLevel->nProps, sizeof(pLevel->nProps)).bytes == static_cast<std::size_t>(sizeof(pLevel->nProps)));
    if (pLevel->nProps != 0) {
        pLevel->pProps = ReadPropsFile(hFile, pLevel->nProps);
        if (pLevel->pProps == 0) {
            srAssertFail("pLevel->pProps", LEVELFILE_CPP, 0x89, 0);
        }
    }
    unsigned char fSuccess2 = (hFile->read(&pLevel->nBitmaps, sizeof(pLevel->nBitmaps)).bytes == static_cast<std::size_t>(sizeof(pLevel->nBitmaps)));
    if (pLevel->nBitmaps != 0) {
        pLevel->pBitmaps = ReadPropsFile(hFile, pLevel->nBitmaps);
        if (pLevel->pBitmaps == 0) {
            srAssertFail("pLevel->pBitmaps", LEVELFILE_CPP, 0x91, 0);
        }
    }

    fSuccess = (hFile->read(&pLevel->nCameras, sizeof(pLevel->nCameras)).bytes == static_cast<std::size_t>(sizeof(pLevel->nCameras)));
    fSuccess &= fSuccess2;
    if (pLevel->nCameras != 0) {
        pLevel->pCameras =
            static_cast<W8LevelFileCamera*>(malloc(pLevel->nCameras * sizeof(W8LevelFileCamera)));
        if (pLevel->pCameras == 0) {
            srAssertFail("pLevel->pCameras", LEVELFILE_CPP, 0xa7, 0);
        }
        memset(pLevel->pCameras, 0, pLevel->nCameras * sizeof(W8LevelFileCamera));
        for (i = 0; i < pLevel->nCameras; ++i) {
            W8LevelFileCamera* pCamera = pLevel->pCameras + i;
            memset(pCamera, 0, sizeof(W8LevelFileCamera));
            unsigned char ok = (hFile->read(&pCamera->positional0, 4).bytes == static_cast<std::size_t>(4));
            ok &= (hFile->read(&pCamera->positional1, 4).bytes == static_cast<std::size_t>(4));
            ok &= (hFile->read(&pCamera->has_scale, 1).bytes == static_cast<std::size_t>(1));
            ok &= (hFile->read(pCamera->name, sizeof(pCamera->name)).bytes == static_cast<std::size_t>(sizeof(pCamera->name)));
            if (pCamera->has_scale != 0) {
                ok &= (hFile->read(&pCamera->scale, 4).bytes == static_cast<std::size_t>(4));
            }
            fSuccess &= ReadPathAIFile(hFile, &pCamera->pathAI) & ok;
        }
        if (fSuccess == 0) {
            return 0;
        }
    }

    fSuccess &= (hFile->read(&pLevel->has_block, sizeof(pLevel->has_block)).bytes == static_cast<std::size_t>(sizeof(pLevel->has_block)));
    if (pLevel->has_block != 0) {
        fSuccess &= ReadLevelFileBlock(hFile, &pLevel->block);
        if (fSuccess == 0) {
            return 0;
        }
    }

    fSuccess &= (hFile->read(&pLevel->nTriggers, sizeof(pLevel->nTriggers)).bytes == static_cast<std::size_t>(sizeof(pLevel->nTriggers)));
    if (pLevel->nTriggers != 0) {
        pLevel->pTriggers = static_cast<W8LevelFileTrigger*>(
            malloc(pLevel->nTriggers * sizeof(W8LevelFileTrigger)));
        if (pLevel->pTriggers == 0) {
            srAssertFail("pLevel->pTriggers", LEVELFILE_CPP, 0xbc, 0);
        }
        memset(pLevel->pTriggers, 0, pLevel->nTriggers * sizeof(W8LevelFileTrigger));
        for (i = 0; i < pLevel->nTriggers; ++i) {
            fSuccess &= ReadTriggerFile(hFile, pLevel->pTriggers + i);
        }
        if (fSuccess == 0) {
            return 0;
        }
    }

    ok = (hFile->read(&pLevel->camera_mode, sizeof(pLevel->camera_mode)).bytes == static_cast<std::size_t>(sizeof(pLevel->camera_mode)));
    ok &= (hFile->read(&pLevel->nClippingPlanes, sizeof(pLevel->nClippingPlanes)).bytes == static_cast<std::size_t>(sizeof(pLevel->nClippingPlanes)));
    ok &= fSuccess;
    if (pLevel->nClippingPlanes != 0) {
        ok &= (hFile->read(&pLevel->clipping_plane_version, 1).bytes == static_cast<std::size_t>(1));
        pLevel->pClippingPlanes = static_cast<W8LevelFileClippingPlaneRecord*>(
            malloc(pLevel->nClippingPlanes * sizeof(W8LevelFileClippingPlaneRecord)));
        if (pLevel->pClippingPlanes == 0) {
            srAssertFail("pLevel->pClippingPlanes", LEVELFILE_CPP, 0xcb, 0);
        }
        ok &= (hFile->read(pLevel->pClippingPlanes, pLevel->nClippingPlanes * sizeof(W8LevelFileClippingPlaneRecord)).bytes == static_cast<std::size_t>(pLevel->nClippingPlanes * sizeof(W8LevelFileClippingPlaneRecord)));
        if (ok == 0) {
            return 0;
        }
    }

    ok &= (hFile->read(&pLevel->environment_offset, sizeof(pLevel->environment_offset)).bytes == static_cast<std::size_t>(sizeof(pLevel->environment_offset)));
    ok &= (hFile->read(&pLevel->nParticleSystems, sizeof(pLevel->nParticleSystems)).bytes == static_cast<std::size_t>(sizeof(pLevel->nParticleSystems)));
    if (pLevel->nParticleSystems != 0) {
        pLevel->pParticleSystems = static_cast<W8LevelFileParticleSystem*>(
            malloc(pLevel->nParticleSystems * sizeof(W8LevelFileParticleSystem)));
        if (pLevel->pParticleSystems == 0) {
            srAssertFail("pLevel->pParticleSystems", LEVELFILE_CPP, 0xd6, 0);
        }
        for (i = 0; i < pLevel->nParticleSystems; ++i) {
            if (ok == 0) {
                return 0;
            }
            ok &= ReadParticleSystemFile(hFile, pLevel->pParticleSystems + i);
        }
        if (ok == 0) {
            return 0;
        }
    }

    ok &= (hFile->read(&pLevel->nNamedPositions, sizeof(pLevel->nNamedPositions)).bytes == static_cast<std::size_t>(sizeof(pLevel->nNamedPositions)));
    if (pLevel->nNamedPositions != 0) {
        pLevel->pNamedPositions = static_cast<W8LevelFileNamedPosition*>(
            malloc(pLevel->nNamedPositions * sizeof(W8LevelFileNamedPosition)));
        if (pLevel->pNamedPositions == 0) {
            srAssertFail("pLevel->pNamedPositions", LEVELFILE_CPP, 0xe3, 0);
        }
        unsigned int uiBytesRead;
        for (i = 0; i < pLevel->nNamedPositions; ++i) {
            ok &= ((uiBytesRead = hFile->read(pLevel->pNamedPositions + i, sizeof(W8LevelFileNamedPosition)).bytes) == static_cast<std::size_t>(sizeof(W8LevelFileNamedPosition)));
        }
        if (ok == 0) {
            return 0;
        }
        for (i = 0; i < pLevel->nNamedPositions; ++i) {
            W8LevelFileNamedPosition* pPosition = pLevel->pNamedPositions + i;
            ReportBuildStatus(
                5,
                FormatString("Named Position: %s (%f, %f, %f)\n", pPosition->name,
                           pPosition->position.x, pPosition->position.y, pPosition->position.z));
        }
    }

    hFile->read_exact(pLevel->unknown_6b9, sizeof(pLevel->unknown_6b9));
    pLevel->read_end_position = hFile->tell();
    return pLevel;
}

// FUNCTION: WIZ8 0x004D07C0
BOOLEAN WriteLevelFile(wiz8::File* hFile, wiz8::File* hFileIn, W8LevelFile* pLevel)
{
    unsigned int uiBytes;
    unsigned char fSuccess;
    unsigned char ok;
    int iCount;
    int i;
    char buffer[0x400];
    unsigned int chunk;

    if (pLevel == 0) {
        srAssertFail("pLevel", LEVELFILE_CPP, 0x10c, 0);
    }
    if (hFile == 0) {
        srAssertFail("hFile", LEVELFILE_CPP, 0x10d, 0);
    }
    if (pLevel->pMeshes == 0) {
        srAssertFail("pLevel->pMeshes", LEVELFILE_CPP, 0x10e, 0);
    }
    hFile->write(&pLevel->submesh_count, 4);
    hFile->write(&pLevel->mesh_count, 4);
    hFile->write(&pLevel->nTextures, 2);
    iCount = pLevel->nTextures;
    if (iCount == 0) {
        return FALSE;
    }
    ok = 1;
    for (i = 0; i < static_cast<short>(iCount); ++i) {
        W8MaterialRecord* pTexture = pLevel->pTextures + i;
        ok = WriteMaterialRecord(hFile, pTexture);
        if (ok == 0) {
            return FALSE;
        }
    }
    if (ok == 0) {
        return FALSE;
    }
    if (((hFile->write(&iCount, 4), true) & ok) == 0) {
        return FALSE;
    }
    free(pLevel->pTextures);
    if (pLevel->submesh_count != 0) {
        OctMeshModel* pModel = pLevel->pModels;
        for (i = 0; static_cast<unsigned int>(i) < pLevel->submesh_count; ++i) {
            pModel->Write(hFile);
            ++pModel;
        }
    }
    hFile->write(&iCount, 4);
    if (pLevel->pModels != 0) {
        delete[] pLevel->pModels;
    }
    if (pLevel->pMeshes != 0) {
        if (pLevel->pMeshes->pstVertices != 0) {
            free(pLevel->pMeshes->pstVertices);
        }
        if (pLevel->pMeshes->pstFaces != 0) {
            free(pLevel->pMeshes->pstFaces);
        }
        free(pLevel->pMeshes);
    }
    hFile->write(&pLevel->nLights, 2);
    if (pLevel->nLights != 0) {
        for (i = 0; i < pLevel->nLights; ++i) {
            if (WriteLightFile(hFile, pLevel->pLights + i) == 0) {
                return FALSE;
            }
        }
        free(pLevel->pLights);
    }
    hFile->write(&iCount, 4);
    fSuccess = (hFile->write(&pLevel->nMonsters, 4), true);
    if (pLevel->nMonsters != 0) {
        for (i = 0; i < pLevel->nMonsters; ++i) {
            W8LevelFileMonster* pMonster = pLevel->pMonsters + i;
            fSuccess &= (hFile->write(pMonster, 0x22), true);
            if (fSuccess == 0) {
                return FALSE;
            }
            if (pMonster->num_mon_path != 0) {
                fSuccess &= (hFile->write(pMonster->MonPath, pMonster->num_mon_path * sizeof(W8LevelFilePathNode)), true);
                if (fSuccess == 0) {
                    return FALSE;
                }
                free(pMonster->MonPath);
            }
        }
        free(pLevel->pMonsters);
    }
    hFile->write(&iCount, 4);
    hFile->write(&pLevel->nItems, 4);
    if (pLevel->nItems != 0) {
        if ((hFile->write(pLevel->pItems, pLevel->nItems * sizeof(W8LevelFileItemRecord)), true) ==
            0) {
            return FALSE;
        }
        free(pLevel->pItems);
    }
    hFile->write(&iCount, 4);
    hFile->write(&pLevel->missile_count, 4);
    hFile->write(&iCount, 4);
    hFile->write(&pLevel->nProps, 4);
    if ((pLevel->nProps != 0) && (WritePropsFile(hFile, pLevel->nProps, pLevel->pProps) == 0)) {
        return FALSE;
    }
    hFile->write(&iCount, 4);
    ok = (hFile->write(&pLevel->nBitmaps, 4), true);
    if ((pLevel->nBitmaps != 0) &&
        (ok = WritePropsFile(hFile, pLevel->nBitmaps, pLevel->pBitmaps), ok == 0)) {
        return FALSE;
    }
    fSuccess = (hFile->write(&iCount, 4), true);
    fSuccess = (hFile->write(&pLevel->nCameras, 4), true) & fSuccess & ok;
    if (pLevel->nCameras != 0) {
        if (pLevel->pCameras == 0) {
            srAssertFail("pLevel->pCameras", LEVELFILE_CPP, 0x189, 0);
        }
        for (i = 0; i < pLevel->nCameras; ++i) {
            W8LevelFileCamera* pCamera = pLevel->pCameras + i;
            unsigned char okCam = (hFile->write(&pCamera->positional0, 4), true);
            okCam &= (hFile->write(&pCamera->positional1, 4), true);
            okCam &= (hFile->write(&pCamera->has_scale, 1), true);
            okCam &= (hFile->write(pCamera->name, sizeof(pCamera->name)), true);
            if (pCamera->has_scale != 0) {
                okCam &= (hFile->write(&pCamera->scale, 4), true);
            }
            fSuccess &= WritePathAIFile(hFile, &pCamera->pathAI) & okCam;
        }
        if (fSuccess == 0) {
            return FALSE;
        }
        free(pLevel->pCameras);
    }
    fSuccess = (hFile->write(&iCount, 4), true);
    fSuccess = (hFile->write(&pLevel->has_block, 4), true) & fSuccess;
    if (pLevel->has_block != 0) {
        fSuccess &= WriteLevelFileBlock(hFile, &pLevel->block);
        if (fSuccess == 0) {
            return FALSE;
        }
    }
    fSuccess = (hFile->write(&iCount, 4), true);
    fSuccess = (hFile->write(&pLevel->nTriggers, 4), true) & fSuccess;
    if (pLevel->nTriggers != 0) {
        if (pLevel->pTriggers == 0) {
            srAssertFail("pLevel->pTriggers", LEVELFILE_CPP, 0x1a0, 0);
        }
        for (i = 0; i < pLevel->nTriggers; ++i) {
            fSuccess &= WriteTriggerFile(hFile, pLevel->pTriggers + i);
        }
        if (fSuccess == 0) {
            return FALSE;
        }
        free(pLevel->pTriggers);
    }
    fSuccess = (hFile->write(&iCount, 4), true);
    fSuccess = (hFile->write(&pLevel->camera_mode, 4), true) & fSuccess;
    fSuccess = (hFile->write(&pLevel->nClippingPlanes, 4), true) & fSuccess;
    if (pLevel->nClippingPlanes != 0) {
        fSuccess &= (hFile->write(&pLevel->clipping_plane_version, 1), true);
        fSuccess &= (hFile->write(pLevel->pClippingPlanes, pLevel->nClippingPlanes * sizeof(W8LevelFileClippingPlaneRecord)), true);
        free(pLevel->pClippingPlanes);
        if (fSuccess == 0) {
            return FALSE;
        }
    }
    fSuccess = (hFile->write(&iCount, 4), true);
    fSuccess =
        (hFile->write(&pLevel->environment_offset, sizeof(pLevel->environment_offset)), true) &
        fSuccess;
    fSuccess = (hFile->write(&pLevel->nParticleSystems, 4), true) & fSuccess;
    if (pLevel->nParticleSystems != 0) {
        for (i = 0; i < pLevel->nParticleSystems; ++i) {
            if (fSuccess == 0) {
                break;
            }
            fSuccess &= WriteParticleSystemFile(hFile, pLevel->pParticleSystems + i);
        }
        free(pLevel->pParticleSystems);
        if (fSuccess == 0) {
            return FALSE;
        }
    }
    fSuccess = (hFile->write(&iCount, 4), true);
    fSuccess = (hFile->write(&pLevel->nNamedPositions, 4), true) & fSuccess;
    if (pLevel->nNamedPositions != 0) {
        fSuccess &= (hFile->write(pLevel->pNamedPositions, pLevel->nNamedPositions * sizeof(W8LevelFileNamedPosition)), true);
        free(pLevel->pNamedPositions);
        if (fSuccess == 0) {
            return FALSE;
        }
    }
    hFile->write(&iCount, 4);
    float level_scale = GetAutomapGridCellSize();
    fSuccess = (hFile->write(&level_scale, 4), true) & fSuccess;
    fSuccess = (hFile->write(&pLevel->num_automap_nodes, 4), true) & fSuccess;
    if (pLevel->num_automap_nodes != 0) {
        fSuccess &= (hFile->write(pLevel->automap_nodes, pLevel->num_automap_nodes * 4), true);
        free(pLevel->automap_nodes);
        if (fSuccess == 0) {
            return FALSE;
        }
    }
    fSuccess = (hFile->write(&iCount, 4), true);
    fSuccess = (hFile->write(pLevel->unknown_6b9, 4), true) & fSuccess;
    chunk = 0x400;
    unsigned char fDone;
    do {
        fDone = 0;
        if (fSuccess == 0) {
            break;
        }
        fDone = ((uiBytes = hFileIn->read(buffer, 0x400).bytes) == static_cast<std::size_t>(0x400));
        fSuccess &= fDone;
        if (uiBytes < 0x400) {
            fSuccess = 1;
            chunk = uiBytes;
        }
        fDone = (hFile->write(buffer, chunk), uiBytes = chunk, true);
        fSuccess &= fDone;
    } while (chunk == 0x400);
    pLevel->num_switch_triggers = 0;
    pLevel->num_invisible_planes = 0;
    free(pLevel);
    return fSuccess;
}

// FUNCTION: WIZ8 0x004D1110
BOOLEAN ReadMeshFile(wiz8::File* hFile, W8LevelFileMesh* pMesh)
try
{
    int i;
    memset(pMesh, 0, sizeof(W8LevelFileMesh));
    BOOLEAN fSuccess = TRUE;
    fSuccess &= (hFile->read(&pMesh->version, 4).bytes == static_cast<std::size_t>(4));
    fSuccess &= (hFile->read(&pMesh->num_vertices, 4).bytes == static_cast<std::size_t>(4));
    fSuccess &= (hFile->read(&pMesh->num_faces, 4).bytes == static_cast<std::size_t>(4));
    if (fSuccess == 0) {
        return FALSE;
    }
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
        fSuccess = (hFile->read(&pMesh->flags, 1).bytes == static_cast<std::size_t>(1));
    }
    if (pMesh->version >= 2) {
        fSuccess &= (hFile->read(&pMesh->location, sizeof(pMesh->location)).bytes == static_cast<std::size_t>(sizeof(pMesh->location)));
        fSuccess &= (hFile->read(&pMesh->rotation_angle, 0x10).bytes == static_cast<std::size_t>(0x10));
        fSuccess &= (hFile->read(&pMesh->scale, sizeof(pMesh->scale)).bytes == static_cast<std::size_t>(sizeof(pMesh->scale)));
    }
    if (pMesh->version >= 4) {
        fSuccess &= (hFile->read(&pMesh->mapping_count, 1).bytes == static_cast<std::size_t>(1));
        if (pMesh->mapping_count != 0) {
            fSuccess &= (hFile->read(&pMesh->mapped_value, 4).bytes == static_cast<std::size_t>(4));
        }
    }
    if (fSuccess == 0) {
        return FALSE;
    }
    if ((pMesh->flags & W8_LEVEL_MESH_LOD_VERTICES) == 0) {
        pMesh->pstVertices = static_cast<srVector3T<float>*>(
            malloc(pMesh->num_vertices * 2 * sizeof(*pMesh->pstVertices)));
        if (pMesh->pstVertices == 0) {
            srAssertFail("pMesh->pstVertices", LEVELFILE_CPP, 0x29a, 0);
        }
        memset(pMesh->pstVertices, 0, pMesh->num_vertices * 2 * sizeof(*pMesh->pstVertices));
        if ((hFile->read(pMesh->pstVertices, pMesh->num_vertices * sizeof(*pMesh->pstVertices)).bytes == static_cast<std::size_t>(pMesh->num_vertices * sizeof(*pMesh->pstVertices))) == 0) {
            return FALSE;
        }
    } else {
        fSuccess &= (hFile->read(&pMesh->lod_mode, 1).bytes == static_cast<std::size_t>(1));
        fSuccess &= (hFile->read(&pMesh->num_lods, 2).bytes == static_cast<std::size_t>(2));
        if ((pMesh->flags & W8_LEVEL_MESH_SHORT_LOD_VERTICES) == 0) {
            srVector3T<float>** pLods =
                static_cast<srVector3T<float>**>(malloc(pMesh->num_lods * sizeof(*pLods)));
            if (pLods == 0) {
                return FALSE;
            }
            for (i = 0; i < pMesh->num_lods; ++i) {
                pLods[i] = static_cast<srVector3T<float>*>(
                    malloc(pMesh->num_vertices * sizeof(*pLods[i])));
                if (pLods[i] == 0) {
                    return FALSE;
                }
                fSuccess &= (hFile->read(pLods[i], pMesh->num_vertices * sizeof(*pLods[i])).bytes == static_cast<std::size_t>(pMesh->num_vertices * sizeof(*pLods[i])));
                if (fSuccess == 0) {
                    return FALSE;
                }
            }
            pMesh->lods = pLods;
        } else {
            if (pMesh->lod_mode >= 2) {
                hFile->read_exact(&pMesh->lod_scale, 4);
            }
            short** pLods = static_cast<short**>(malloc(pMesh->num_lods * sizeof(short*)));
            if (pLods == 0) {
                return FALSE;
            }
            for (i = 0; i < pMesh->num_lods; ++i) {
                pLods[i] = static_cast<short*>(malloc(pMesh->num_vertices * 3 * sizeof(short)));
                if (pLods[i] == 0) {
                    return FALSE;
                }
                fSuccess &= (hFile->read(pLods[i], pMesh->num_vertices * 3 * sizeof(short)).bytes == static_cast<std::size_t>(pMesh->num_vertices * 3 * sizeof(short)));
                if (fSuccess == 0) {
                    return FALSE;
                }
            }
            pMesh->lod_shorts = pLods;
        }
    }
    if ((pMesh->flags & W8_LEVEL_MESH_COMPRESSED_FACES) != 0) {
        pMesh->pstCompFaces = static_cast<W8LevelFileCompressedFace*>(
            malloc(pMesh->num_faces * sizeof(W8LevelFileCompressedFace)));
        if (pMesh->pstCompFaces == 0) {
            srAssertFail("pMesh->pstCompFaces", LEVELFILE_CPP, 0x2a7, 0);
        }
        memset(pMesh->pstCompFaces, 0, pMesh->num_faces * sizeof(W8LevelFileCompressedFace));
        return (hFile->read(pMesh->pstCompFaces, pMesh->num_faces * sizeof(W8LevelFileCompressedFace)).bytes == static_cast<std::size_t>(pMesh->num_faces * sizeof(W8LevelFileCompressedFace)));
    }
    pMesh->pstFaces =
        static_cast<W8ReadMeshFace*>(malloc(pMesh->num_faces * 2 * sizeof(*pMesh->pstFaces)));
    if (pMesh->pstFaces == 0) {
        srAssertFail("pMesh->pstFaces", LEVELFILE_CPP, 0x2b2, 0);
    }
    memset(pMesh->pstFaces, 0, pMesh->num_faces * 2 * sizeof(*pMesh->pstFaces));
    return (hFile->read(pMesh->pstFaces, pMesh->num_faces * sizeof(*pMesh->pstFaces)).bytes == static_cast<std::size_t>(pMesh->num_faces * sizeof(*pMesh->pstFaces)));
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004D1510
BOOLEAN WriteMeshFile(wiz8::File* hFile, W8LevelFileMesh* pMesh)
{
    BOOLEAN fSuccess = TRUE;
    fSuccess &= (hFile->write(&pMesh->version, 4), true);
    fSuccess &= (hFile->write(&pMesh->num_vertices, 4), true);
    fSuccess &= (hFile->write(&pMesh->num_faces, 4), true);
    if (fSuccess == 0) {
        return FALSE;
    }
    if (pMesh->version >= 3) {
        fSuccess = (hFile->write(&pMesh->flags, 1), true);
    }
    if (pMesh->version >= 2) {
        fSuccess &= (hFile->write(&pMesh->location, sizeof(pMesh->location)), true);
        fSuccess &= (hFile->write(&pMesh->rotation_angle, 0x10), true);
        fSuccess &= (hFile->write(&pMesh->scale, sizeof(pMesh->scale)), true);
    }
    if (pMesh->version >= 4) {
        fSuccess &= (hFile->write(&pMesh->mapping_count, 1), true);
        if (pMesh->mapping_count != 0) {
            fSuccess &= (hFile->write(&pMesh->mapped_value, 4), true);
        }
    }
    if (fSuccess == 0) {
        return FALSE;
    }
    if ((pMesh->flags & W8_LEVEL_MESH_LOD_VERTICES) == 0) {
        if (pMesh->pstVertices == 0) {
            srAssertFail("pMesh->pstVertices", LEVELFILE_CPP, 0x315, 0);
        }
        if ((hFile->write(pMesh->pstVertices, pMesh->num_vertices * sizeof(*pMesh->pstVertices)), true) == 0) {
            ReportBuildStatus(7, "WriteFileMesh: Could not write mesh vertices.\n");
        }
    } else {
        fSuccess &= (hFile->write(&pMesh->lod_mode, 1), true);
        fSuccess &= (hFile->write(&pMesh->num_lods, 2), true);
        if (pMesh->lod_mode >= 2) {
            fSuccess &= (hFile->write(&pMesh->lod_scale, 4), true);
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
                fSuccess &= (hFile->write(pLods[i], pMesh->num_vertices * sizeof(*pLods[i])), true);
                if (fSuccess == 0) {
                    ReportBuildStatus(7, "WriteFileMesh: Could not write mesh vertices.\n");
                }
                free(pLods[i]);
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
                fSuccess &= (hFile->write(pLods[i], pMesh->num_vertices * 3 * sizeof(short)), true);
                if (fSuccess == 0) {
                    return FALSE;
                }
                free(pLods[i]);
            }
        }
    }
    free(pMesh->pstVertices);
    if ((pMesh->flags & W8_LEVEL_MESH_COMPRESSED_FACES) != 0) {
        if (pMesh->pstCompFaces == 0) {
            srAssertFail("pMesh->pstCompFaces", LEVELFILE_CPP, 799, 0);
        }
        unsigned char ok = (hFile->write(pMesh->pstCompFaces, pMesh->num_faces * sizeof(W8LevelFileCompressedFace)), true);
        free(pMesh->pstCompFaces);
        return ok;
    }
    if (pMesh->pstFaces == 0) {
        srAssertFail("pMesh->pstFaces", LEVELFILE_CPP, 0x326, 0);
    }
    unsigned char ok =
        (hFile->write(pMesh->pstFaces, pMesh->num_faces * sizeof(*pMesh->pstFaces)), true);
    free(pMesh->pstFaces);
    return ok;
}

// FUNCTION: WIZ8 0x004D1820
BOOLEAN ReadLightFile(wiz8::File* hFile, W8LevelFileLight* pLight)
try
{
    BOOLEAN fSuccess = TRUE;
    fSuccess &= (hFile->read(&pLight->version, 2).bytes == static_cast<std::size_t>(2));
    fSuccess &= (hFile->read(&pLight->create, 4).bytes == static_cast<std::size_t>(4));
    fSuccess &= (hFile->read(pLight->unknown_06, 2).bytes == static_cast<std::size_t>(2));
    fSuccess &= (hFile->read(&pLight->position, sizeof(pLight->position)).bytes == static_cast<std::size_t>(sizeof(pLight->position)));
    fSuccess &= (hFile->read(&pLight->colour, sizeof(pLight->colour)).bytes == static_cast<std::size_t>(sizeof(pLight->colour)));
    fSuccess &= (hFile->read(&pLight->intensity, 4).bytes == static_cast<std::size_t>(4));
    fSuccess &= (hFile->read(&pLight->range, 4).bytes == static_cast<std::size_t>(4));
    if (fSuccess == 0) {
        return FALSE;
    }
    if (pLight->version >= 2) {
        fSuccess = (hFile->read(pLight->name, 0x14).bytes == static_cast<std::size_t>(0x14)) != 0;
        if ((pLight->flags & W8_LEVEL_LIGHT_HAS_DEFINITION) != 0) {
            pLight->create = 1;
            pLight->pExtra =
                static_cast<W8LevelFileLightExtra*>(malloc(sizeof(W8LevelFileLightExtra)));
            if (pLight->pExtra == 0) {
                return FALSE;
            }
            fSuccess &= (hFile->read(pLight->pExtra, sizeof(W8LevelFileLightExtra)).bytes == static_cast<std::size_t>(sizeof(W8LevelFileLightExtra)));
            if (fSuccess == 0) {
                return FALSE;
            }
            if ((pLight->pExtra->flags & W8_PARAM_LIGHT_HAS_PATH) != 0) {
                pLight->pPathAI =
                    static_cast<W8LevelFilePathAI*>(malloc(sizeof(W8LevelFilePathAI)));
                if (pLight->pPathAI == 0) {
                    return FALSE;
                }
                fSuccess = ReadPathAIFile(hFile, pLight->pPathAI);
            }
        }
    }
    if (fSuccess == 0) {
        srAssertFail("fSuccess", LEVELFILE_CPP, 0x35f, "Couldn't read light.");
    }
    return fSuccess;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004D1960
BOOLEAN WriteLightFile(wiz8::File* hFile, W8LevelFileLight* pLight)
{
    BOOLEAN fSuccess = TRUE;
    fSuccess &= (hFile->write(&pLight->version, 2), true);
    fSuccess &= (hFile->write(&pLight->create, 4), true);
    fSuccess &= (hFile->write(pLight->unknown_06, 2), true);
    fSuccess &= (hFile->write(&pLight->position, sizeof(pLight->position)), true);
    fSuccess &= (hFile->write(&pLight->colour, sizeof(pLight->colour)), true);
    fSuccess &= (hFile->write(&pLight->intensity, 4), true);
    fSuccess &= (hFile->write(&pLight->range, 4), true);
    if (fSuccess == 0) {
        return FALSE;
    }
    if (pLight->version >= 2) {
        fSuccess = (hFile->write(pLight->name, 0x14), true) != 0;
        if (((pLight->flags & W8_LEVEL_LIGHT_HAS_DEFINITION) != 0) && (pLight->pExtra != 0)) {
            fSuccess &= (hFile->write(pLight->pExtra, sizeof(W8LevelFileLightExtra)), true);
            if (fSuccess == 0) {
                return FALSE;
            }
            if (((pLight->pExtra->flags & W8_PARAM_LIGHT_HAS_PATH) != 0) &&
                (pLight->pPathAI != 0)) {
                fSuccess = WritePathAIFile(hFile, pLight->pPathAI);
                free(pLight->pPathAI);
            }
            free(pLight->pExtra);
        }
    }
    if (fSuccess == 0) {
        srAssertFail("fSuccess", LEVELFILE_CPP, 0x38e, "Couldn't Write light.");
    }
    return fSuccess;
}

// FUNCTION: WIZ8 0x004D1A90
BOOLEAN ReadAnimLightFile(wiz8::File* hFile, W8LevelFileAnimLight* pLight)
try
{
    BOOLEAN fSuccess = TRUE;
    fSuccess &= (hFile->read(&pLight->version, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pLight->position, sizeof(pLight->position)).bytes == static_cast<std::size_t>(sizeof(pLight->position)));
    fSuccess &= (hFile->read(&pLight->color, sizeof(pLight->color)).bytes == static_cast<std::size_t>(sizeof(pLight->color)));
    fSuccess &= (hFile->read(&pLight->intensity, 4).bytes == static_cast<std::size_t>(4));
    fSuccess &= (hFile->read(&pLight->range, 4).bytes == static_cast<std::size_t>(4));
    if (fSuccess == 0) {
        return FALSE;
    }
    if (pLight->version >= 2) {
        pLight->pExtra = static_cast<W8LevelFileLightExtra*>(malloc(sizeof(W8LevelFileLightExtra)));
        if (pLight->pExtra == 0) {
            return FALSE;
        }
        fSuccess &= (hFile->read(pLight->pExtra, sizeof(W8LevelFileLightExtra)).bytes == static_cast<std::size_t>(sizeof(W8LevelFileLightExtra)));
    }
    if (fSuccess == 0) {
        srAssertFail("fSuccess", LEVELFILE_CPP, 0x3b2, "Couldn't read anim light.");
    }
    return fSuccess;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004D1B50
BOOLEAN WriteAnimLightFile(wiz8::File* hFile, W8LevelFileAnimLight* pLight)
{
    BOOLEAN fSuccess = TRUE;
    fSuccess &= (hFile->write(&pLight->version, 1), true);
    fSuccess &= (hFile->write(&pLight->position, sizeof(pLight->position)), true);
    fSuccess &= (hFile->write(&pLight->color, sizeof(pLight->color)), true);
    fSuccess &= (hFile->write(&pLight->intensity, 4), true);
    fSuccess &= (hFile->write(&pLight->range, 4), true);
    if (fSuccess == 0) {
        return FALSE;
    }
    if ((pLight->version >= 2) && (pLight->pExtra != 0)) {
        fSuccess &= (hFile->write(pLight->pExtra, sizeof(W8LevelFileLightExtra)), true);
        free(pLight->pExtra);
    }
    if (fSuccess == 0) {
        srAssertFail("fSuccess", LEVELFILE_CPP, 0x3d4, "Couldn't write anim light.");
    }
    return fSuccess;
}

// FUNCTION: WIZ8 0x004D1C10
BOOLEAN ReadTriggerFile(wiz8::File* hFile, W8LevelFileTrigger* pTrigger)
try
{
    unsigned char fSuccess =
        (hFile->read(&pTrigger->version, 1).bytes == static_cast<std::size_t>(1)) && (hFile->read(&pTrigger->type, 1).bytes == static_cast<std::size_t>(1));
    switch (pTrigger->type) {
    case 1: {
        W8LevelFileSwitch* pSwitch =
            static_cast<W8LevelFileSwitch*>(malloc(sizeof(W8LevelFileSwitch)));
        if (pSwitch == 0) {
            srAssertFail("pSwitch", LEVELFILE_CPP, 0x3f8, 0);
        }
        memset(pSwitch, 0, sizeof(W8LevelFileSwitch));
        fSuccess &= (hFile->read(&pSwitch->version, 1).bytes == static_cast<std::size_t>(1));
        fSuccess &= (hFile->read(&pSwitch->cycle_bounce, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pSwitch->state_count, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pSwitch->animate_states, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pSwitch->range, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pSwitch->action, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pSwitch->value, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pSwitch->animate_action, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pSwitch->packed_flags, 1).bytes == static_cast<std::size_t>(1));
        fSuccess &= (hFile->read(&pSwitch->enabled, 1).bytes == static_cast<std::size_t>(1));
        fSuccess &= (hFile->read(pSwitch->name, sizeof(pSwitch->name)).bytes == static_cast<std::size_t>(sizeof(pSwitch->name)));
        fSuccess &= (hFile->read(pSwitch->recipients, sizeof(pSwitch->recipients)).bytes == static_cast<std::size_t>(sizeof(pSwitch->recipients)));
        fSuccess &= (hFile->read(pSwitch->sound, sizeof(pSwitch->sound)).bytes == static_cast<std::size_t>(sizeof(pSwitch->sound)));
        if (fSuccess == 0) {
            srAssertFail("fSuccess", LEVELFILE_CPP, 0x408, 0);
        }
        ReportBuildStatus(
            5,
            FormatString(
                "Switch Trigger: %s, recipients: %s\n",
                pSwitch->name, pSwitch->recipients));
        if (pSwitch->version > 1) {
            fSuccess &= (hFile->read(&pSwitch->minimum_range, 4).bytes == static_cast<std::size_t>(4));
            fSuccess &= (hFile->read(pSwitch->surface_id, sizeof(pSwitch->surface_id)).bytes == static_cast<std::size_t>(sizeof(pSwitch->surface_id)));
            ReportBuildStatus(
                5, FormatString("Switch Trigger name: %s\n", pSwitch->surface_id));
        }
        if (pSwitch->version > 2) {
            fSuccess &= (hFile->read(&pSwitch->has_door_trigger, 1).bytes == static_cast<std::size_t>(1));
            if (pSwitch->has_door_trigger != 0) {
                fSuccess &= (hFile->read(&pSwitch->door.kind, 1).bytes == static_cast<std::size_t>(1));
                if (pSwitch->door.kind == 1) {
                    fSuccess &= ReadDoorTriggerFile(hFile, &pSwitch->door);
                    if (fSuccess == 0) {
                        ReportBuildStatus(7, "Problem reading door trigger.\n");
                        return 0;
                    }
                }
            }
        }
        if (pSwitch->version > 3) {
            fSuccess &= (hFile->read(&pSwitch->action_value, 4).bytes == static_cast<std::size_t>(4));
        }
        pTrigger->pData = pSwitch;
        g_level_file->switch_triggers[g_level_file->num_switch_triggers] = pSwitch;
        ++g_level_file->num_switch_triggers;
        return fSuccess;
    }
    case 2: {
        W8LevelFileInvisible* pInvis =
            static_cast<W8LevelFileInvisible*>(malloc(sizeof(W8LevelFileInvisible)));
        if (pInvis == 0) {
            srAssertFail("pInvis", LEVELFILE_CPP, 0x42f, 0);
        }
        memset(pInvis, 0, sizeof(W8LevelFileInvisible));
        fSuccess &= (hFile->read(&pInvis->version, 1).bytes == static_cast<std::size_t>(1));
        fSuccess &= (hFile->read(&pInvis->range, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pInvis->position, sizeof(pInvis->position)).bytes == static_cast<std::size_t>(sizeof(pInvis->position)));
        fSuccess &= (hFile->read(&pInvis->action, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pInvis->searchable, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pInvis->fire_linked, 1).bytes == static_cast<std::size_t>(1));
        fSuccess &= (hFile->read(&pInvis->enabled, 1).bytes == static_cast<std::size_t>(1));
        fSuccess &= (hFile->read(pInvis->name, sizeof(pInvis->name)).bytes == static_cast<std::size_t>(sizeof(pInvis->name)));
        fSuccess &= (hFile->read(pInvis->recipients, sizeof(pInvis->recipients)).bytes == static_cast<std::size_t>(sizeof(pInvis->recipients)));
        ReportBuildStatus(
            5,
            FormatString(
                "Invisible Trigger: %s, recipients: %s\n",
                pInvis->name, pInvis->recipients));
        if (pInvis->version > 1) {
            fSuccess &= (hFile->read(&pInvis->plane_flag, 1).bytes == static_cast<std::size_t>(1));
            pInvis->pPlane = static_cast<W8LevelFilePlane*>(malloc(sizeof(W8LevelFilePlane)));
            if (pInvis->pPlane == 0) {
                srAssertFail("pInvis->pPlane", LEVELFILE_CPP, 0x441, 0);
            }
            memset(pInvis->pPlane, 0, sizeof(W8LevelFilePlane));
            fSuccess &= (hFile->read(pInvis->pPlane, sizeof(W8LevelFilePlane)).bytes == static_cast<std::size_t>(sizeof(W8LevelFilePlane)));
        }
        if (pInvis->version > 2) {
            fSuccess &= (hFile->read(&pInvis->angle, 4).bytes == static_cast<std::size_t>(4));
            fSuccess &= (hFile->read(&pInvis->direction, sizeof(pInvis->direction)).bytes == static_cast<std::size_t>(sizeof(pInvis->direction)));
            fSuccess &= (hFile->read(&pInvis->unused, 1).bytes == static_cast<std::size_t>(1));
            fSuccess &= (hFile->read(pInvis->action_string, sizeof(pInvis->action_string)).bytes == static_cast<std::size_t>(sizeof(pInvis->action_string)));
        }
        if (pInvis->version > 3) {
            fSuccess &= (hFile->read(&pInvis->flag, 1).bytes == static_cast<std::size_t>(1));
            fSuccess &= (hFile->read(&pInvis->action_value, 4).bytes == static_cast<std::size_t>(4));
        }
        if (pInvis->version > 4) {
            fSuccess &= (hFile->read(&pInvis->has_legacy_geometry, 1).bytes == static_cast<std::size_t>(1));
            if (pInvis->has_legacy_geometry != 0) {
                fSuccess &= (hFile->read(&pInvis->geometry_kind, 1).bytes == static_cast<std::size_t>(1));
                if (pInvis->linked_record_kind_gate == 2) {
                    W8LevelFileLinkedRecord* pRecord = static_cast<W8LevelFileLinkedRecord*>(
                        malloc(sizeof(W8LevelFileLinkedRecord)));
                    unsigned char okRecord = 0;
                    if (pRecord != 0) {
                        okRecord = (hFile->read(&pRecord->kind, 1).bytes == static_cast<std::size_t>(1));
                        okRecord &=
                            (hFile->read(pRecord->vertices, sizeof(pRecord->vertices)).bytes == static_cast<std::size_t>(sizeof(pRecord->vertices)));
                        okRecord &= (hFile->read(&pRecord->linked_face, 2).bytes == static_cast<std::size_t>(2));
                        g_level_file->linked_records[g_level_file->num_linked_records] = pRecord;
                        ++g_level_file->num_linked_records;
                        pInvis->pRecord = pRecord;
                    }
                    if ((fSuccess & okRecord) != 0) {
                        pInvis->pRecord->normal_scale = pInvis->range;
                        pInvis->pRecord->forward_scale = 1.0f;
                        pTrigger->pData = pInvis;
                        return fSuccess & okRecord;
                    }
                    return 0;
                }
            }
        }
        g_level_file->invisible_planes[g_level_file->num_invisible_planes] = pInvis->pPlane;
        ++g_level_file->num_invisible_planes;
        pTrigger->pData = pInvis;
        return fSuccess;
    }
    case 3: {
        W8LevelFileSound* pSound = static_cast<W8LevelFileSound*>(malloc(sizeof(W8LevelFileSound)));
        if (pSound == 0) {
            srAssertFail("pSound", LEVELFILE_CPP, 0x46c, 0);
        }
        memset(pSound, 0, sizeof(W8LevelFileSound));
        fSuccess &= (hFile->read(&pSound->version, 1).bytes == static_cast<std::size_t>(1));
        fSuccess &= (hFile->read(&pSound->volume_min, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pSound->volume_max, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pSound->speed_min, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pSound->speed_max, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pSound->time_min, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pSound->time_max, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pSound->unbounded, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pSound->radius, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pSound->position, sizeof(pSound->position)).bytes == static_cast<std::size_t>(sizeof(pSound->position)));
        fSuccess &= (hFile->read(&pSound->region_u, sizeof(pSound->region_u)).bytes == static_cast<std::size_t>(sizeof(pSound->region_u)));
        fSuccess &= (hFile->read(&pSound->region_v, sizeof(pSound->region_v)).bytes == static_cast<std::size_t>(sizeof(pSound->region_v)));
        fSuccess &= (hFile->read(pSound->wave, sizeof(pSound->wave)).bytes == static_cast<std::size_t>(sizeof(pSound->wave)));
        if (pSound->version > 1) {
            fSuccess &= (hFile->read(&pSound->has_position, 1).bytes == static_cast<std::size_t>(1));
            fSuccess &= (hFile->read(&pSound->looping, 1).bytes == static_cast<std::size_t>(1));
        }
        if (pSound->version > 2) {
            fSuccess &= (hFile->read(&pSound->region_center, sizeof(pSound->region_center)).bytes == static_cast<std::size_t>(sizeof(pSound->region_center)));
            fSuccess &= (hFile->read(&pSound->region_angle, 4).bytes == static_cast<std::size_t>(4));
            fSuccess &= (hFile->read(&pSound->region_min, sizeof(pSound->region_min)).bytes == static_cast<std::size_t>(sizeof(pSound->region_min)));
            fSuccess &= (hFile->read(&pSound->region_max, sizeof(pSound->region_max)).bytes == static_cast<std::size_t>(sizeof(pSound->region_max)));
        }
        if (pSound->version > 3) {
            fSuccess &= (hFile->read(pSound->name, sizeof(pSound->name)).bytes == static_cast<std::size_t>(sizeof(pSound->name)));
            ReportBuildStatus(
                5, FormatString("Sound Trigger: %s\n",
                              pSound->name));
        }
        if (pSound->version > 4) {
            fSuccess &= (hFile->read(&pSound->shared, 1).bytes == static_cast<std::size_t>(1));
        }
        pTrigger->pData = pSound;
        return fSuccess;
    }
    case 4:
        return ReadSuperTriggerFile(hFile, pTrigger) & fSuccess;
    default:
        return fSuccess;
    }
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004D23F0
BOOLEAN WriteTriggerFile(wiz8::File* hFile, W8LevelFileTrigger* pTrigger)
{
    unsigned char fSuccess = (hFile->write(&pTrigger->version, 1), true) != 0;
    fSuccess &= fSuccess && (hFile->write(&pTrigger->type, 1), true);
    switch (pTrigger->type) {
    case 1: {
        W8LevelFileSwitch* pSwitch = static_cast<W8LevelFileSwitch*>(pTrigger->pData);
        if (pSwitch == 0) {
            srAssertFail("pSwitch", LEVELFILE_CPP, 0x4be, 0);
        }
        fSuccess &= (hFile->write(&pSwitch->version, 1), true);
        fSuccess &= (hFile->write(&pSwitch->cycle_bounce, 4), true);
        fSuccess &= (hFile->write(&pSwitch->state_count, 4), true);
        fSuccess &= (hFile->write(&pSwitch->animate_states, 4), true);
        fSuccess &= (hFile->write(&pSwitch->range, 4), true);
        fSuccess &= (hFile->write(&pSwitch->action, 4), true);
        fSuccess &= (hFile->write(&pSwitch->value, 4), true);
        fSuccess &= (hFile->write(&pSwitch->animate_action, 4), true);
        fSuccess &= (hFile->write(&pSwitch->packed_flags, 1), true);
        fSuccess &= (hFile->write(&pSwitch->enabled, 1), true);
        fSuccess &= (hFile->write(pSwitch->name, sizeof(pSwitch->name)), true);
        fSuccess &= (hFile->write(pSwitch->recipients, sizeof(pSwitch->recipients)), true);
        fSuccess &= (hFile->write(pSwitch->sound, sizeof(pSwitch->sound)), true);
        if (fSuccess == 0) {
            srAssertFail("fSuccess", LEVELFILE_CPP, 0x4cd, 0);
        }
        if (pSwitch->version > 1) {
            fSuccess &= (hFile->write(&pSwitch->minimum_range, 4), true);
            fSuccess &= (hFile->write(pSwitch->surface_id, sizeof(pSwitch->surface_id)), true);
        }
        if (pSwitch->version > 2) {
            fSuccess &= (hFile->write(&pSwitch->has_door_trigger, 1), true);
            if (pSwitch->has_door_trigger != 0) {
                fSuccess &= (hFile->write(&pSwitch->door.kind, 1), true);
                if (pSwitch->door.kind == 1) {
                    fSuccess &= WriteDoorTriggerFile(hFile, &pSwitch->door);
                }
            }
        }
        if (pSwitch->version > 3) {
            fSuccess &= (hFile->write(&pSwitch->action_value, 4), true);
        }
        free(pSwitch);
        return fSuccess;
    }
    case 2: {
        W8LevelFileInvisible* pInvis = static_cast<W8LevelFileInvisible*>(pTrigger->pData);
        if (pInvis == 0) {
            srAssertFail("pInvis", LEVELFILE_CPP, 0x4ea, 0);
        }
        fSuccess &= (hFile->write(&pInvis->version, 1), true);
        fSuccess &= (hFile->write(&pInvis->range, 4), true);
        fSuccess &= (hFile->write(&pInvis->position, sizeof(pInvis->position)), true);
        fSuccess &= (hFile->write(&pInvis->action, 4), true);
        fSuccess &= (hFile->write(&pInvis->searchable, 4), true);
        fSuccess &= (hFile->write(&pInvis->fire_linked, 1), true);
        fSuccess &= (hFile->write(&pInvis->enabled, 1), true);
        fSuccess &= (hFile->write(pInvis->name, sizeof(pInvis->name)), true);
        fSuccess &= (hFile->write(pInvis->recipients, sizeof(pInvis->recipients)), true);
        if (pInvis->version > 1) {
            fSuccess &= (hFile->write(&pInvis->plane_flag, 1), true);
            fSuccess &= (hFile->write(pInvis->pPlane, sizeof(W8LevelFilePlane)), true);
            free(pInvis->pPlane);
        }
        if (pInvis->version > 2) {
            fSuccess &= (hFile->write(&pInvis->angle, 4), true);
            fSuccess &= (hFile->write(&pInvis->direction, sizeof(pInvis->direction)), true);
            fSuccess &= (hFile->write(&pInvis->unused, 1), true);
            fSuccess &= (hFile->write(pInvis->action_string, sizeof(pInvis->action_string)), true);
        }
        if (pInvis->version > 3) {
            fSuccess &= (hFile->write(&pInvis->flag, 1), true);
            fSuccess &= (hFile->write(&pInvis->action_value, 4), true);
        }
        if (pInvis->version > 4) {
            fSuccess &= (hFile->write(&pInvis->has_legacy_geometry, 1), true);
            if (pInvis->has_legacy_geometry != 0) {
                fSuccess &= (hFile->write(&pInvis->geometry_kind, 1), true);
                if (pInvis->linked_record_kind_gate == 2) {
                    W8LevelFileLinkedRecord* pRecord = pInvis->pRecord;
                    unsigned char okRecord = 0;
                    if (pRecord != 0) {
                        okRecord = (hFile->write(&pRecord->kind, 1), true);
                        okRecord &=
                            (hFile->write(pRecord->vertices, sizeof(pRecord->vertices)), true);
                        okRecord &= (hFile->write(&pRecord->linked_face, 2), true);
                        free(pRecord);
                    }
                    fSuccess &= okRecord;
                    if (fSuccess == 0) {
                        return 0;
                    }
                }
            }
        }
        free(pInvis);
        return fSuccess;
    }
    case 3: {
        W8LevelFileSound* pSound = static_cast<W8LevelFileSound*>(pTrigger->pData);
        if (pSound == 0) {
            srAssertFail("pSound", LEVELFILE_CPP, 0x51e, 0);
        }
        fSuccess &= (hFile->write(&pSound->version, 1), true);
        fSuccess &= (hFile->write(&pSound->volume_min, 4), true);
        fSuccess &= (hFile->write(&pSound->volume_max, 4), true);
        fSuccess &= (hFile->write(&pSound->speed_min, 4), true);
        fSuccess &= (hFile->write(&pSound->speed_max, 4), true);
        fSuccess &= (hFile->write(&pSound->time_min, 4), true);
        fSuccess &= (hFile->write(&pSound->time_max, 4), true);
        fSuccess &= (hFile->write(&pSound->unbounded, 4), true);
        fSuccess &= (hFile->write(&pSound->radius, 4), true);
        fSuccess &= (hFile->write(&pSound->position, sizeof(pSound->position)), true);
        fSuccess &= (hFile->write(&pSound->region_u, sizeof(pSound->region_u)), true);
        fSuccess &= (hFile->write(&pSound->region_v, sizeof(pSound->region_v)), true);
        fSuccess &= (hFile->write(pSound->wave, sizeof(pSound->wave)), true);
        if (pSound->version > 1) {
            fSuccess &= (hFile->write(&pSound->has_position, 1), true);
            fSuccess &= (hFile->write(&pSound->looping, 1), true);
        }
        if (pSound->version > 2) {
            fSuccess &= (hFile->write(&pSound->region_center, sizeof(pSound->region_center)), true);
            fSuccess &= (hFile->write(&pSound->region_angle, 4), true);
            fSuccess &= (hFile->write(&pSound->region_min, sizeof(pSound->region_min)), true);
            fSuccess &= (hFile->write(&pSound->region_max, sizeof(pSound->region_max)), true);
        }
        if (pSound->version > 3) {
            fSuccess &= (hFile->write(pSound->name, sizeof(pSound->name)), true);
        }
        if (pSound->version > 4) {
            fSuccess &= (hFile->write(&pSound->shared, 1), true);
        }
        free(pSound);
        return fSuccess;
    }
    case 4:
        return WriteSuperTriggerFile(hFile, pTrigger) & fSuccess;
    default:
        return fSuccess;
    }
}

// FUNCTION: WIZ8 0x004D2A30
BOOLEAN ReadSuperTriggerFile(wiz8::File* hFile, W8LevelFileTrigger* pTrigger)
try
{
    W8LevelFileSuperTrigger* pSuper =
        static_cast<W8LevelFileSuperTrigger*>(malloc(sizeof(W8LevelFileSuperTrigger)));
    if (pSuper == 0) {
        ReportBuildStatus(7, "ReadSuperTrigger: Could not allocate SuperTrigger strucutre.\n");
        return FALSE;
    }
    unsigned char fSuccess = (hFile->read(&pSuper->version, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(pSuper->name, sizeof(pSuper->name)).bytes == static_cast<std::size_t>(sizeof(pSuper->name)));
    fSuccess &= (hFile->read(&pSuper->flags, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pSuper->active, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pSuper->kind, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pSuper->when_active, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pSuper->prop_index, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pSuper->activation_count, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pSuper->inactive_count, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pSuper->trigger, 4).bytes == static_cast<std::size_t>(4));
    fSuccess &= (hFile->read(&pSuper->trigger_on, 4).bytes == static_cast<std::size_t>(4));
    fSuccess &= (hFile->read(&pSuper->trigger_off, 4).bytes == static_cast<std::size_t>(4));
    fSuccess &= (hFile->read(pSuper->recipients, sizeof(pSuper->recipients)).bytes == static_cast<std::size_t>(sizeof(pSuper->recipients)));
    fSuccess &= (hFile->read(&pSuper->ataxia_or_cure, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(pSuper->ps_events, sizeof(pSuper->ps_events)).bytes == static_cast<std::size_t>(sizeof(pSuper->ps_events)));
    fSuccess &= (hFile->read(&pSuper->allow_save, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pSuper->price, 4).bytes == static_cast<std::size_t>(4));
    fSuccess &= (hFile->read(&pSuper->door_kind, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(pSuper->animation, sizeof(pSuper->animation)).bytes == static_cast<std::size_t>(sizeof(pSuper->animation)));
    if (!fSuccess) {
        return FALSE;
    }
    ReportBuildStatus(
        5,
        FormatString("Super Trigger: %s, recipients: %s\n", pSuper->name, pSuper->recipients));
    if (pSuper->version >= 2) {
        fSuccess &= (hFile->read(pSuper->size, sizeof(pSuper->size)).bytes == static_cast<std::size_t>(sizeof(pSuper->size)));
        fSuccess &= (hFile->read(&pSuper->direction, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pSuper->wait0, 1).bytes == static_cast<std::size_t>(1));
        fSuccess &= (hFile->read(&pSuper->wait1, 1).bytes == static_cast<std::size_t>(1));
        fSuccess &= (hFile->read(&pSuper->wait2, 1).bytes == static_cast<std::size_t>(1));
        fSuccess &= (hFile->read(&pSuper->loop, 1).bytes == static_cast<std::size_t>(1));
        fSuccess &= (hFile->read(&pSuper->speed, 0x10).bytes == static_cast<std::size_t>(0x10));
        if (!fSuccess) {
            return FALSE;
        }
    }
    fSuccess &= (hFile->read(&pSuper->ignore, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pSuper->group, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pSuper->set_group, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(pSuper->groups, sizeof(pSuper->groups)).bytes == static_cast<std::size_t>(sizeof(pSuper->groups)));
    fSuccess &= (hFile->read(pSuper->objects, sizeof(pSuper->objects)).bytes == static_cast<std::size_t>(sizeof(pSuper->objects)));
    fSuccess &= (hFile->read(&pSuper->close_door, 1).bytes == static_cast<std::size_t>(1));
    if (!fSuccess) {
        return FALSE;
    }
    fSuccess &= (hFile->read(&pSuper->wait3, 4).bytes == static_cast<std::size_t>(4));
    fSuccess &= (hFile->read(&pSuper->field, 4).bytes == static_cast<std::size_t>(4));
    fSuccess &= (hFile->read(pSuper->event, sizeof(pSuper->event)).bytes == static_cast<std::size_t>(sizeof(pSuper->event)));
    fSuccess &= (hFile->read(&pSuper->normal_scale, 4).bytes == static_cast<std::size_t>(4));
    if (!fSuccess) {
        return FALSE;
    }
    if (pSuper->version >= 3) {
        fSuccess &= (hFile->read(pSuper->particle_system, sizeof(pSuper->particle_system)).bytes == static_cast<std::size_t>(sizeof(pSuper->particle_system)));
    }
    if ((pSuper->flags & 1) == 0) {
        fSuccess &= (hFile->read(&pSuper->placement_kind, 1).bytes == static_cast<std::size_t>(1));
        if (pSuper->placement_kind == 1) {
            pSuper->pPosition = static_cast<W8LevelFileTriggerPosition*>(
                malloc(sizeof(W8LevelFileTriggerPosition)));
            if (pSuper->pPosition == 0) {
                ReportBuildStatus(
                    7, "ReadSuperTrigger: Could not allocate Trigger Position structure.\n");
                return FALSE;
            }
            fSuccess &= (hFile->read(pSuper->pPosition, sizeof(W8LevelFileTriggerPosition)).bytes == static_cast<std::size_t>(sizeof(W8LevelFileTriggerPosition)));
        } else if (pSuper->placement_kind == 2) {
            pSuper->pPlane = static_cast<W8LevelFilePlane*>(malloc(sizeof(W8LevelFilePlane)));
            if (pSuper->pPlane == 0) {
                ReportBuildStatus(
                    7, "ReadSuperTrigger: Could not allocate Trigger Plane structure.\n");
                return FALSE;
            }
            fSuccess &= (hFile->read(pSuper->pPlane, sizeof(W8LevelFilePlane)).bytes == static_cast<std::size_t>(sizeof(W8LevelFilePlane)));
            g_level_file->invisible_planes[g_level_file->num_invisible_planes] = pSuper->pPlane;
            ++g_level_file->num_invisible_planes;
        }
        if (!fSuccess) {
            return FALSE;
        }
        fSuccess &= (hFile->read(&pSuper->has_hotspot, 1).bytes == static_cast<std::size_t>(1));
        if (pSuper->has_hotspot != 0) {
            pSuper->pHotSpot =
                static_cast<W8LevelFileTriggerHotSpot*>(malloc(sizeof(W8LevelFileTriggerHotSpot)));
            if (pSuper->pHotSpot == 0) {
                ReportBuildStatus(
                    7, "ReadSuperTrigger: Could not allocate Trigger HotSpot structure.\n");
                return FALSE;
            }
            fSuccess &= (hFile->read(pSuper->pHotSpot, sizeof(W8LevelFileTriggerHotSpot)).bytes == static_cast<std::size_t>(sizeof(W8LevelFileTriggerHotSpot)));
        }
    }
    if (!fSuccess) {
        return FALSE;
    }
    fSuccess &= (hFile->read(&pSuper->has_door, 1).bytes == static_cast<std::size_t>(1));
    if (pSuper->has_door != 0) {
        fSuccess &= (hFile->read(&pSuper->door.kind, 1).bytes == static_cast<std::size_t>(1));
        if (pSuper->door.kind == 1) {
            fSuccess = ReadDoorTriggerFile(hFile, &pSuper->door);
        } else if (pSuper->door.kind == 2) {
            W8LevelFileLinkedRecord* pRecord =
                static_cast<W8LevelFileLinkedRecord*>(malloc(sizeof(W8LevelFileLinkedRecord)));
            unsigned char ok = 0;
            if (pRecord != 0) {
                ok = (hFile->read(&pRecord->kind, 1).bytes == static_cast<std::size_t>(1));
                ok &= (hFile->read(pRecord->vertices, sizeof(pRecord->vertices)).bytes == static_cast<std::size_t>(sizeof(pRecord->vertices)));
                ok &= (hFile->read(&pRecord->linked_face, 2).bytes == static_cast<std::size_t>(2));
                g_level_file->linked_records[g_level_file->num_linked_records] = pRecord;
                ++g_level_file->num_linked_records;
                pSuper->pRecord = pRecord;
            }
            fSuccess &= ok;
            /* Retail stores through pRecord even when the allocation failed. */
            pSuper->pRecord->normal_scale = pSuper->normal_scale;
            pSuper->pRecord->forward_scale = pSuper->direction;
        }
    }
    pTrigger->pData = pSuper;
    return fSuccess;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004D3000
BOOLEAN WriteSuperTriggerFile(wiz8::File* hFile, W8LevelFileTrigger* pTrigger)
{
    W8LevelFileSuperTrigger* pSuper = static_cast<W8LevelFileSuperTrigger*>(pTrigger->pData);
    if (pSuper == 0) {
        ReportBuildStatus(7, "WriteSuperTrigger: Couldn't create SuperTrigger structure.\n");
        return 0;
    }
    unsigned char fSuccess = (hFile->write(&pSuper->version, 1), true);
    fSuccess &= (hFile->write(pSuper->name, sizeof(pSuper->name)), true);
    fSuccess &= (hFile->write(&pSuper->flags, 1), true);
    fSuccess &= (hFile->write(&pSuper->active, 1), true);
    fSuccess &= (hFile->write(&pSuper->kind, 1), true);
    fSuccess &= (hFile->write(&pSuper->when_active, 1), true);
    fSuccess &= (hFile->write(&pSuper->prop_index, 1), true);
    fSuccess &= (hFile->write(&pSuper->activation_count, 1), true);
    fSuccess &= (hFile->write(&pSuper->inactive_count, 1), true);
    fSuccess &= (hFile->write(&pSuper->trigger, 4), true);
    fSuccess &= (hFile->write(&pSuper->trigger_on, 4), true);
    fSuccess &= (hFile->write(&pSuper->trigger_off, 4), true);
    fSuccess &= (hFile->write(pSuper->recipients, sizeof(pSuper->recipients)), true);
    fSuccess &= (hFile->write(&pSuper->ataxia_or_cure, 1), true);
    fSuccess &= (hFile->write(pSuper->ps_events, sizeof(pSuper->ps_events)), true);
    fSuccess &= (hFile->write(&pSuper->allow_save, 1), true);
    fSuccess &= (hFile->write(&pSuper->price, 4), true);
    fSuccess &= (hFile->write(&pSuper->door_kind, 1), true);
    fSuccess &= (hFile->write(pSuper->animation, sizeof(pSuper->animation)), true);
    if (fSuccess == 0) {
        return 0;
    }
    if (pSuper->version >= 2) {
        fSuccess &= (hFile->write(pSuper->size, sizeof(pSuper->size)), true);
        fSuccess &= (hFile->write(&pSuper->direction, 4), true);
        fSuccess &= (hFile->write(&pSuper->wait0, 1), true);
        fSuccess &= (hFile->write(&pSuper->wait1, 1), true);
        fSuccess &= (hFile->write(&pSuper->wait2, 1), true);
        fSuccess &= (hFile->write(&pSuper->loop, 1), true);
        fSuccess &= (hFile->write(&pSuper->speed, 0x10), true);
        if (fSuccess == 0) {
            return 0;
        }
    }
    fSuccess &= (hFile->write(&pSuper->ignore, 1), true);
    fSuccess &= (hFile->write(&pSuper->group, 1), true);
    fSuccess &= (hFile->write(&pSuper->set_group, 1), true);
    fSuccess &= (hFile->write(pSuper->groups, sizeof(pSuper->groups)), true);
    fSuccess &= (hFile->write(pSuper->objects, sizeof(pSuper->objects)), true);
    fSuccess &= (hFile->write(&pSuper->close_door, 1), true);
    if (fSuccess == 0) {
        return 0;
    }
    fSuccess &= (hFile->write(&pSuper->wait3, 4), true);
    fSuccess &= (hFile->write(&pSuper->field, 4), true);
    fSuccess &= (hFile->write(pSuper->event, sizeof(pSuper->event)), true);
    fSuccess &= (hFile->write(&pSuper->normal_scale, 4), true);
    if (fSuccess == 0) {
        return 0;
    }
    if (pSuper->version >= 3) {
        fSuccess &= (hFile->write(pSuper->particle_system, sizeof(pSuper->particle_system)), true);
    }
    if ((pSuper->flags & 1) == 0) {
        fSuccess &= (hFile->write(&pSuper->placement_kind, 1), true);
        if (pSuper->placement_kind == 1) {
            if (pSuper->pPosition == 0) {
                ReportBuildStatus(7, "WriteSuperTrigger: No Trigger Position structure.\n");
                return 0;
            }
            fSuccess &= (hFile->write(pSuper->pPosition, sizeof(W8LevelFileTriggerPosition)), true);
            free(pSuper->pPosition);
        } else if (pSuper->placement_kind == 2) {
            if (pSuper->pPlane == 0) {
                ReportBuildStatus(7, "WriteSuperTrigger: No FileTriggerPlane structure.\n");
                return 0;
            }
            fSuccess &= (hFile->write(pSuper->pPlane, sizeof(W8LevelFilePlane)), true);
            free(pSuper->pPlane);
        }
        if (fSuccess == 0) {
            return 0;
        }
        fSuccess &= (hFile->write(&pSuper->has_hotspot, 1), true);
        if (pSuper->has_hotspot != 0) {
            if (pSuper->pHotSpot == 0) {
                ReportBuildStatus(7, "WriteSuperTrigger: No Trigger HotSpot structure.\n");
                return 0;
            }
            fSuccess &= (hFile->write(pSuper->pHotSpot, sizeof(W8LevelFileTriggerHotSpot)), true);
            free(pSuper->pHotSpot);
        }
    }
    if (fSuccess == 0) {
        return 0;
    }
    fSuccess &= (hFile->write(&pSuper->has_door, 1), true);
    if (pSuper->has_door != 0) {
        fSuccess &= (hFile->write(&pSuper->door.kind, 1), true);
        if (pSuper->door.kind == 1) {
            fSuccess = WriteDoorTriggerFile(hFile, &pSuper->door);
        } else if (pSuper->door.kind == 2) {
            W8LevelFileLinkedRecord* pRecord = pSuper->pRecord;
            unsigned char ok;
            if (pRecord == 0) {
                ok = 0;
            } else {
                ok = (hFile->write(&pRecord->kind, 1), true);
                ok &= (hFile->write(pRecord->vertices, sizeof(pRecord->vertices)), true);
                ok &= (hFile->write(&pRecord->linked_face, 2), true);
                free(pRecord);
            }
            fSuccess &= ok;
        }
    }
    free(pSuper);
    return fSuccess;
}

// FUNCTION: WIZ8 0x004D3540
BOOLEAN ReadDoorTriggerFile(wiz8::File* hFile, W8LevelFileDoorRef* pDoor)
try
{
    W8LevelFileDoor* pDoorRec = static_cast<W8LevelFileDoor*>(malloc(sizeof(W8LevelFileDoor)));
    if (pDoorRec == 0) {
        return 0;
    }
    unsigned char fSuccess = (hFile->read(&pDoorRec->version, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pDoorRec->flags[0], 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pDoorRec->flags[1], 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pDoorRec->flags[2], 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pDoorRec->flags[3], 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pDoorRec->flags[4], 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pDoorRec->flags[5], 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pDoorRec->flags[6], 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pDoorRec->flags[7], 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pDoorRec->flags[8], 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pDoorRec->item, 2).bytes == static_cast<std::size_t>(2));
    fSuccess &= (hFile->read(&pDoorRec->has_position, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pDoorRec->position, sizeof(pDoorRec->position)).bytes == static_cast<std::size_t>(sizeof(pDoorRec->position)));
    fSuccess &= (hFile->read(pDoorRec->linked_trigger, 0x80).bytes == static_cast<std::size_t>(0x80));
    pDoor->door = pDoorRec;
    return fSuccess;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004D3660
BOOLEAN WriteDoorTriggerFile(wiz8::File* hFile, W8LevelFileDoorRef* pDoor)
{
    W8LevelFileDoor* pDoorRec = pDoor->door;
    if (pDoorRec == 0) {
        return 0;
    }
    unsigned char fSuccess = (hFile->write(&pDoorRec->version, 1), true);
    fSuccess &= (hFile->write(&pDoorRec->flags[0], 1), true);
    fSuccess &= (hFile->write(&pDoorRec->flags[1], 1), true);
    fSuccess &= (hFile->write(&pDoorRec->flags[2], 1), true);
    fSuccess &= (hFile->write(&pDoorRec->flags[3], 1), true);
    fSuccess &= (hFile->write(&pDoorRec->flags[4], 1), true);
    fSuccess &= (hFile->write(&pDoorRec->flags[5], 1), true);
    fSuccess &= (hFile->write(&pDoorRec->flags[6], 1), true);
    fSuccess &= (hFile->write(&pDoorRec->flags[7], 1), true);
    fSuccess &= (hFile->write(&pDoorRec->flags[8], 1), true);
    fSuccess &= (hFile->write(&pDoorRec->item, 2), true);
    fSuccess &= (hFile->write(&pDoorRec->has_position, 1), true);
    fSuccess &= (hFile->write(&pDoorRec->position, sizeof(pDoorRec->position)), true);
    fSuccess &= (hFile->write(pDoorRec->linked_trigger, 0x80), true);
    free(pDoorRec);
    return fSuccess;
}

// FUNCTION: WIZ8 0x004D3770
BOOLEAN ReadPathAIFile(wiz8::File* hFile, W8LevelFilePathAI* pPathAI)
try
{
    unsigned char fSuccess = (hFile->read(&pPathAI->version, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pPathAI->scaled, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pPathAI->position, 4).bytes == static_cast<std::size_t>(4));
    fSuccess &= (hFile->read(pPathAI->unknown_06, 4).bytes == static_cast<std::size_t>(4));
    fSuccess &= (hFile->read(&pPathAI->path_count, 4).bytes == static_cast<std::size_t>(4));
    if (pPathAI->scaled == 2) {
        if (pPathAI->path_count != 0) {
            pPathAI->pScaledPaths = static_cast<W8LevelFileScaledPathNode*>(
                malloc(pPathAI->path_count * sizeof(W8LevelFileScaledPathNode)));
            if (pPathAI->pScaledPaths == 0) {
                srAssertFail("pPathAI->pScaledPaths", LEVELFILE_CPP, 0x732, 0);
            }
            fSuccess &= (hFile->read(pPathAI->pScaledPaths, pPathAI->path_count * sizeof(W8LevelFileScaledPathNode)).bytes == static_cast<std::size_t>(pPathAI->path_count * sizeof(W8LevelFileScaledPathNode)));
        }
    } else if (pPathAI->path_count != 0) {
        pPathAI->pPaths = static_cast<W8LevelFilePathNode*>(
            malloc(pPathAI->path_count * sizeof(W8LevelFilePathNode)));
        if (pPathAI->pPaths == 0) {
            srAssertFail("pPathAI->pPaths", LEVELFILE_CPP, 0x73d, 0);
        }
        fSuccess &=
            (hFile->read(pPathAI->pPaths, pPathAI->path_count * sizeof(W8LevelFilePathNode)).bytes == static_cast<std::size_t>(pPathAI->path_count * sizeof(W8LevelFilePathNode)));
    }
    return fSuccess;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004D38E0
BOOLEAN WritePathAIFile(wiz8::File* hFile, W8LevelFilePathAI* pPathAI)
{
    unsigned char fSuccess = (hFile->write(&pPathAI->version, 1), true);
    fSuccess &= (hFile->write(&pPathAI->scaled, 1), true);
    fSuccess &= (hFile->write(&pPathAI->position, 4), true);
    fSuccess &= (hFile->write(pPathAI->unknown_06, 4), true);
    fSuccess &= (hFile->write(&pPathAI->path_count, 4), true);
    if (pPathAI->scaled == 2) {
        if (pPathAI->path_count != 0) {
            if (pPathAI->pScaledPaths == 0) {
                srAssertFail("pPathAI->pScaledPaths", LEVELFILE_CPP, 0x761, 0);
            }
            fSuccess &= (hFile->write(pPathAI->pScaledPaths, pPathAI->path_count * sizeof(W8LevelFileScaledPathNode)), true);
            free(pPathAI->pScaledPaths);
            pPathAI->pScaledPaths = 0;
        }
    } else if (pPathAI->path_count != 0) {
        if (pPathAI->pPaths == 0) {
            srAssertFail("pPathAI->pPaths", LEVELFILE_CPP, 0x76b, 0);
        }
        fSuccess &=
            (hFile->write(pPathAI->pPaths, pPathAI->path_count * sizeof(W8LevelFilePathNode)), true);
        free(pPathAI->pPaths);
        pPathAI->pPaths = 0;
    }
    return fSuccess;
}

// FUNCTION: WIZ8 0x004D3A10
BOOLEAN ReadAnimObjFile(wiz8::File* hFile, W8LevelFileAnimObj* pAnimObj)
try
{
    unsigned short usFrame;
    short i;
    short j;

    memset(pAnimObj, 0, sizeof(W8LevelFileAnimObj));
    BOOLEAN fSuccess = TRUE;
    fSuccess &= (hFile->read(&pAnimObj->version, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pAnimObj->num_anims, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pAnimObj->animation_playing, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pAnimObj->frame_method, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pAnimObj->behaviour, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pAnimObj->cycle, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pAnimObj->path_lists, 1).bytes == static_cast<std::size_t>(1));
    if (pAnimObj->version >= 3) {
        fSuccess = fSuccess && (hFile->read(&pAnimObj->playback_scale, 4).bytes == static_cast<std::size_t>(4));
    } else {
        pAnimObj->playback_scale = 15.0f;
    }
    if (pAnimObj->version >= 5) {
        fSuccess = fSuccess && (hFile->read(&pAnimObj->start_frame, 1).bytes == static_cast<std::size_t>(1));
    } else {
        pAnimObj->start_frame = 0;
    }
    if (pAnimObj->version >= 6) {
        fSuccess = fSuccess && (hFile->read(&pAnimObj->random_play, 1).bytes == static_cast<std::size_t>(1)) &&
                   (hFile->read(&pAnimObj->play_chance, 4).bytes == static_cast<std::size_t>(4));
    } else {
        pAnimObj->random_play = 0;
        pAnimObj->play_chance = 1.0f;
    }
    fSuccess = fSuccess && (hFile->read(pAnimObj->discarded, 0x32).bytes == static_cast<std::size_t>(0x32));
    if (!fSuccess) {
        srAssertFail("fSuccess", LEVELFILE_CPP, 0x7a5, 0);
    }
    if (pAnimObj->num_anims != 0) {
        pAnimObj->abHowMany = static_cast<char*>(malloc(pAnimObj->num_anims));
        if (pAnimObj->abHowMany == 0) {
            srAssertFail("pAnimObj->abHowMany", LEVELFILE_CPP, 0x7aa, 0);
        }
        fSuccess &= (hFile->read(pAnimObj->abHowMany, pAnimObj->num_anims).bytes == static_cast<std::size_t>(pAnimObj->num_anims));
    }
    if (pAnimObj->version >= 7) {
        fSuccess &= (hFile->read(&pAnimObj->num_bound_box, 1).bytes == static_cast<std::size_t>(1));
        if (pAnimObj->num_bound_box != 0) {
            pAnimObj->pBoundBox = static_cast<W8LevelFileBounds*>(
                malloc(pAnimObj->num_bound_box * sizeof(W8LevelFileBounds)));
            if (pAnimObj->pBoundBox == 0) {
                srAssertFail("pAnimObj->pBoundBox", LEVELFILE_CPP, 0x7b3, 0);
            }
            fSuccess &= (hFile->read(pAnimObj->pBoundBox, pAnimObj->num_bound_box * sizeof(W8LevelFileBounds)).bytes == static_cast<std::size_t>(pAnimObj->num_bound_box * sizeof(W8LevelFileBounds)));
        }
    }
    if (fSuccess == 0) {
        return FALSE;
    }
    if (pAnimObj->version >= 8) {
        fSuccess = (hFile->read(&pAnimObj->num_anim_lights, 1).bytes == static_cast<std::size_t>(1));
        if (fSuccess == 0) {
            return FALSE;
        }
        if (pAnimObj->num_anim_lights != 0) {
            pAnimObj->pAnimLights = static_cast<W8LevelFileAnimLight*>(
                malloc(pAnimObj->num_anim_lights * sizeof(W8LevelFileAnimLight)));
            if (pAnimObj->pAnimLights == 0) {
                return FALSE;
            }
            memset(pAnimObj->pAnimLights, 0,
                   pAnimObj->num_anim_lights * sizeof(W8LevelFileAnimLight));
            for (i = 0; i < pAnimObj->num_anim_lights; ++i) {
                fSuccess &= ReadAnimLightFile(hFile, pAnimObj->pAnimLights + i);
                if (fSuccess == 0) {
                    return FALSE;
                }
            }
        }
    }
    if (pAnimObj->path_lists == 0) {
        if ((pAnimObj->version >= 9) &&
            ((hFile->read(&pAnimObj->has_path_ai, 1).bytes == static_cast<std::size_t>(1)), pAnimObj->has_path_ai != 0)) {
            pAnimObj->pPathAI = static_cast<W8LevelFilePathAI*>(malloc(sizeof(W8LevelFilePathAI)));
            if (pAnimObj->pPathAI == 0) {
                return FALSE;
            }
            fSuccess = ReadPathAIFile(hFile, pAnimObj->pPathAI);
            if (fSuccess == 0) {
                return FALSE;
            }
        }
        if (pAnimObj->num_anims != 0) {
            pAnimObj->pMorphs = static_cast<W8LevelFileMorph*>(
                malloc(pAnimObj->num_anims * sizeof(W8LevelFileMorph)));
            if (pAnimObj->pMorphs == 0) {
                srAssertFail("pAnimObj->pMorphs", LEVELFILE_CPP, 0x7e1, 0);
            }
            memset(pAnimObj->pMorphs, 0, pAnimObj->num_anims * sizeof(W8LevelFileMorph));
            for (i = 0; i < pAnimObj->num_anims; ++i) {
                W8LevelFileMorph* pMorph = pAnimObj->pMorphs + i;
                fSuccess &= (hFile->read(&pMorph->channel, 1).bytes == static_cast<std::size_t>(1));
                fSuccess &= (hFile->read(&pMorph->num_frames, 1).bytes == static_cast<std::size_t>(1));
                if (pMorph->num_frames != 0) {
                    pMorph->LODMesh.pFrames = static_cast<W8LevelFileFrame*>(
                        malloc(pMorph->num_frames * sizeof(W8LevelFileFrame)));
                    if (pMorph->LODMesh.pFrames == 0) {
                        srAssertFail("pAnimObj->pMorphs[i].LODMesh.pFrames", LEVELFILE_CPP, 0x7eb,
                                     0);
                    }
                    memset(pMorph->LODMesh.pFrames, 0,
                           pMorph->num_frames * sizeof(W8LevelFileFrame));
                    if (pMorph->num_frames != 0) {
                        usFrame = 0;
                        do {
                            W8LevelFileFrame* pFrame =
                                pMorph->LODMesh.pFrames + static_cast<short>(usFrame);
                            fSuccess &= (hFile->read(&pFrame->flags, 1).bytes == static_cast<std::size_t>(1));
                            fSuccess &= ReadMeshFile(hFile, &pFrame->mesh);
                            fSuccess &= (hFile->read(&pFrame->num_textures, 2).bytes == static_cast<std::size_t>(2));
                            if (pFrame->num_textures != 0) {
                                pFrame->pTextures = static_cast<W8MaterialRecord*>(
                                    malloc(pFrame->num_textures * sizeof(W8MaterialRecord)));
                                if (pFrame->pTextures == 0) {
                                    srAssertFail(
                                        "pAnimObj->pMorphs[i].LODMesh.pFrames[i2].pTextures",
                                        LEVELFILE_CPP, 0x7f9, 0);
                                }
                                memset(pFrame->pTextures, 0,
                                       pFrame->num_textures * sizeof(W8MaterialRecord));
                                unsigned char fTextures = 1;
                                for (j = 0; j < pFrame->num_textures; ++j) {
                                    W8MaterialRecord* pTexture = pFrame->pTextures + j;
                                    fTextures = ReadMaterialRecord(hFile, pTexture);
                                    if (fTextures == 0) {
                                        return FALSE;
                                    }
                                }
                                if (fTextures == 0) {
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
        fSuccess &= (hFile->read(&pAnimObj->num_transforms, 1).bytes == static_cast<std::size_t>(1));
        if (pAnimObj->num_transforms != 0) {
            pAnimObj->pTransforms = static_cast<W8LevelFileTransform*>(
                malloc(pAnimObj->num_transforms * sizeof(W8LevelFileTransform)));
            if (pAnimObj->pTransforms == 0) {
                srAssertFail("pAnimObj->pTransforms", LEVELFILE_CPP, 0x80e, 0);
            }
            memset(pAnimObj->pTransforms, 0,
                   pAnimObj->num_transforms * sizeof(W8LevelFileTransform));
            for (i = 0; i < pAnimObj->num_transforms; ++i) {
                W8LevelFileTransform* pTransform = pAnimObj->pTransforms + i;
                fSuccess &= (hFile->read(&pTransform->channel, 1).bytes == static_cast<std::size_t>(1));
                fSuccess &= (hFile->read(&pTransform->num_frames, 1).bytes == static_cast<std::size_t>(1));
                if (pTransform->num_frames != 0) {
                    pTransform->LODMesh.pFrames = static_cast<W8LevelFileFrame*>(
                        malloc(pTransform->num_frames * sizeof(W8LevelFileFrame)));
                    if (pTransform->LODMesh.pFrames == 0) {
                        srAssertFail("pAnimObj->pTransforms[i].LODMesh.pFrames", LEVELFILE_CPP,
                                     0x818, 0);
                    }
                    memset(pTransform->LODMesh.pFrames, 0,
                           pTransform->num_frames * sizeof(W8LevelFileFrame));
                    if (pTransform->num_frames != 0) {
                        short iFrame = 0;
                        do {
                            W8LevelFileFrame* pFrame = pTransform->LODMesh.pFrames + iFrame;
                            fSuccess &= (hFile->read(&pFrame->flags, 1).bytes == static_cast<std::size_t>(1));
                            fSuccess &= ReadMeshFile(hFile, &pFrame->mesh);
                            fSuccess &= (hFile->read(&pFrame->num_textures, 2).bytes == static_cast<std::size_t>(2));
                            if ((pFrame->num_textures < 0) || (pFrame->num_textures > 500)) {
                                sprintf(g_level_file_error,
                                        "Invalid number of materials in mesh (%d materials).\n",
                                        static_cast<int>(pFrame->num_textures));
                                ReportBuildStatus(7, g_level_file_error);
                                return FALSE;
                            }
                            if (pFrame->num_textures != 0) {
                                pFrame->pTextures = static_cast<W8MaterialRecord*>(
                                    malloc(pFrame->num_textures * sizeof(W8MaterialRecord)));
                                if (pFrame->pTextures == 0) {
                                    srAssertFail(
                                        "pAnimObj->pTransforms[i].LODMesh.pFrames[i2].pTextures",
                                        LEVELFILE_CPP, 0x82e, 0);
                                }
                                memset(pFrame->pTextures, 0,
                                       pFrame->num_textures * sizeof(W8MaterialRecord));
                                unsigned char fTextures = 1;
                                for (j = 0; j < pFrame->num_textures; ++j) {
                                    W8MaterialRecord* pTexture = pFrame->pTextures + j;
                                    fTextures = ReadMaterialRecord(hFile, pTexture);
                                    if (fTextures == 0) {
                                        return FALSE;
                                    }
                                }
                                if (fTextures == 0) {
                                    return FALSE;
                                }
                            }
                            ++iFrame;
                        } while (iFrame < static_cast<short>(pTransform->num_frames));
                    }
                }
                fSuccess &= ReadPathAIFile(hFile, &pTransform->pathAI);
            }
        }
    }
    return fSuccess;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004D4480
BOOLEAN WriteAnimObjFile(wiz8::File* hFile, W8LevelFileAnimObj* pAnimObj)
{
    unsigned short usFrame;
    short i;
    short j;

    BOOLEAN fSuccess = TRUE;
    fSuccess &= (hFile->write(&pAnimObj->version, 1), true);
    fSuccess &= (hFile->write(&pAnimObj->num_anims, 1), true);
    fSuccess &= (hFile->write(&pAnimObj->animation_playing, 1), true);
    fSuccess &= (hFile->write(&pAnimObj->frame_method, 1), true);
    fSuccess &= (hFile->write(&pAnimObj->behaviour, 1), true);
    fSuccess &= (hFile->write(&pAnimObj->cycle, 1), true);
    fSuccess &= (hFile->write(&pAnimObj->path_lists, 1), true);
    if (pAnimObj->version >= 3) {
        fSuccess = fSuccess && (hFile->write(&pAnimObj->playback_scale, 4), true);
    }
    if (pAnimObj->version >= 5) {
        fSuccess = fSuccess && (hFile->write(&pAnimObj->start_frame, 1), true);
    }
    if (pAnimObj->version >= 6) {
        fSuccess = fSuccess && (hFile->write(&pAnimObj->random_play, 1), true) &&
                   (hFile->write(&pAnimObj->play_chance, 4), true);
    }
    fSuccess = fSuccess && (hFile->write(pAnimObj->discarded, 0x32), true);
    if (!fSuccess) {
        srAssertFail("fSuccess", LEVELFILE_CPP, 0x867, 0);
    }
    if (pAnimObj->num_anims != 0) {
        if (pAnimObj->abHowMany == 0) {
            srAssertFail("pAnimObj->abHowMany", LEVELFILE_CPP, 0x86b, 0);
        }
        fSuccess &= (hFile->write(pAnimObj->abHowMany, pAnimObj->num_anims), true);
        free(pAnimObj->abHowMany);
    }
    if (pAnimObj->version >= 7) {
        fSuccess &= (hFile->write(&pAnimObj->num_bound_box, 1), true);
        if (pAnimObj->num_bound_box != 0) {
            if (pAnimObj->pBoundBox == 0) {
                srAssertFail("pAnimObj->pBoundBox", LEVELFILE_CPP, 0x874, 0);
            }
            fSuccess &= (hFile->write(pAnimObj->pBoundBox, pAnimObj->num_bound_box * sizeof(W8LevelFileBounds)), true);
            free(pAnimObj->pBoundBox);
        }
    }
    if (fSuccess == 0) {
        return FALSE;
    }
    if (pAnimObj->version >= 8) {
        fSuccess = (hFile->write(&pAnimObj->num_anim_lights, 1), true);
        if ((pAnimObj->num_anim_lights != 0) && (pAnimObj->pAnimLights != 0)) {
            for (i = 0; i < pAnimObj->num_anim_lights; ++i) {
                fSuccess &= WriteAnimLightFile(hFile, pAnimObj->pAnimLights + i);
                if (fSuccess == 0) {
                    return FALSE;
                }
            }
            free(pAnimObj->pAnimLights);
        }
        if (fSuccess == 0) {
            return FALSE;
        }
    }
    if (pAnimObj->path_lists == 0) {
        if ((pAnimObj->version >= 9) &&
            ((hFile->write(&pAnimObj->has_path_ai, 1), true), pAnimObj->has_path_ai != 0) &&
            (pAnimObj->pPathAI != 0)) {
            fSuccess = WritePathAIFile(hFile, pAnimObj->pPathAI);
            free(pAnimObj->pPathAI);
            if (fSuccess == 0) {
                return FALSE;
            }
        }
        if (pAnimObj->num_anims != 0) {
            if (pAnimObj->pMorphs == 0) {
                srAssertFail("pAnimObj->pMorphs", LEVELFILE_CPP, 0x89e, 0);
            }
            for (i = 0; i < pAnimObj->num_anims; ++i) {
                W8LevelFileMorph* pMorph = pAnimObj->pMorphs + i;
                fSuccess &= (hFile->write(&pMorph->channel, 1), true);
                fSuccess &= (hFile->write(&pMorph->num_frames, 1), true);
                if (pMorph->num_frames != 0) {
                    if (pMorph->LODMesh.pFrames == 0) {
                        srAssertFail("pAnimObj->pMorphs[i].LODMesh.pFrames", LEVELFILE_CPP, 0x8a5,
                                     0);
                    }
                    usFrame = 0;
                    do {
                        W8LevelFileFrame* pFrame =
                            pMorph->LODMesh.pFrames + static_cast<short>(usFrame);
                        fSuccess &= (hFile->write(&pFrame->flags, 1), true);
                        fSuccess &= WriteMeshFile(hFile, &pFrame->mesh);
                        fSuccess &= (hFile->write(&pFrame->num_textures, 2), true);
                        if (pFrame->num_textures != 0) {
                            if (pFrame->pTextures == 0) {
                                srAssertFail("pAnimObj->pMorphs[i].LODMesh.pFrames[i2].pTextures",
                                             LEVELFILE_CPP, 0x8af, 0);
                            }
                            fSuccess = 1;
                            for (j = 0; j < pFrame->num_textures; ++j) {
                                W8MaterialRecord* pTexture = pFrame->pTextures + j;
                                fSuccess = WriteMaterialRecord(hFile, pTexture);
                                if (fSuccess == 0) {
                                    return FALSE;
                                }
                            }
                            if (fSuccess == 0) {
                                return FALSE;
                            }
                            free(pFrame->pTextures);
                            pFrame->pTextures = 0;
                        }
                        if ((usFrame == 0) && ((pMorph->LODMesh.pFrames->mesh.flags &
                                                W8_LEVEL_MESH_LOD_VERTICES) != 0)) {
                            usFrame = pMorph->num_frames;
                        }
                        ++usFrame;
                    } while (static_cast<short>(usFrame) < static_cast<short>(pMorph->num_frames));
                    free(pMorph->LODMesh.pFrames);
                    pMorph->LODMesh.pFrames = 0;
                }
            }
            free(pAnimObj->pMorphs);
            pAnimObj->pMorphs = 0;
            return fSuccess;
        }
    } else {
        fSuccess &= (hFile->write(&pAnimObj->num_transforms, 1), true);
        if (pAnimObj->num_transforms != 0) {
            if (pAnimObj->pTransforms == 0) {
                srAssertFail("pAnimObj->pTransforms", LEVELFILE_CPP, 0x8c7, 0);
            }
            for (i = 0; i < pAnimObj->num_transforms; ++i) {
                W8LevelFileTransform* pTransform = pAnimObj->pTransforms + i;
                fSuccess &= (hFile->write(&pTransform->channel, 1), true);
                fSuccess &= (hFile->write(&pTransform->num_frames, 1), true);
                if (pTransform->num_frames != 0) {
                    if (pTransform->LODMesh.pFrames == 0) {
                        srAssertFail("pAnimObj->pTransforms[i].LODMesh.pFrames", LEVELFILE_CPP,
                                     0x8ce, 0);
                    }
                    short iFrame = 0;
                    do {
                        W8LevelFileFrame* pFrame = pTransform->LODMesh.pFrames + iFrame;
                        fSuccess &= (hFile->write(&pFrame->flags, 1), true);
                        fSuccess &= WriteMeshFile(hFile, &pFrame->mesh);
                        fSuccess &= (hFile->write(&pFrame->num_textures, 2), true);
                        if (pFrame->num_textures != 0) {
                            if (pFrame->pTextures == 0) {
                                srAssertFail(
                                    "pAnimObj->pTransforms[i].LODMesh.pFrames[i2].pTextures",
                                    LEVELFILE_CPP, 0x8d8, 0);
                            }
                            fSuccess = 1;
                            for (j = 0; j < pFrame->num_textures; ++j) {
                                W8MaterialRecord* pTexture = pFrame->pTextures + j;
                                fSuccess = WriteMaterialRecord(hFile, pTexture);
                                if (fSuccess == 0) {
                                    return FALSE;
                                }
                            }
                            if (fSuccess == 0) {
                                return FALSE;
                            }
                            free(pFrame->pTextures);
                            pFrame->pTextures = 0;
                        }
                        ++iFrame;
                    } while (iFrame < static_cast<short>(pTransform->num_frames));
                    free(pTransform->LODMesh.pFrames);
                    pTransform->LODMesh.pFrames = 0;
                }
                fSuccess &= WritePathAIFile(hFile, &pTransform->pathAI);
            }
            free(pAnimObj->pTransforms);
            pAnimObj->pTransforms = 0;
        }
    }
    return fSuccess;
}

// FUNCTION: WIZ8 0x004D4CB0
W8LevelFileProp* ReadPropsFile(wiz8::File* hFile, int count)
{
    unsigned char fSuccess = 1;
    if (count == 0) {
        return 0;
    }
    W8LevelFileProp* pProps =
        static_cast<W8LevelFileProp*>(malloc(count * sizeof(W8LevelFileProp)));
    if (pProps == 0) {
        srAssertFail("pProps", LEVELFILE_CPP, 0x905, 0);
    }
    memset(pProps, 0, count * sizeof(W8LevelFileProp));
    for (int i = 0; i < count; ++i) {
        W8LevelFileProp* pProp = pProps + i;
        fSuccess &= (hFile->read(&pProp->version, 1).bytes == static_cast<std::size_t>(1));
        fSuccess &= (hFile->read(&pProp->bNumFrames, 1).bytes == static_cast<std::size_t>(1));
        if (pProp->version >= 5) {
            fSuccess &= (hFile->read(&pProp->option, 1).bytes == static_cast<std::size_t>(1));
            fSuccess &= (hFile->read(&pProp->position, sizeof(pProp->position)).bytes == static_cast<std::size_t>(sizeof(pProp->position)));
        }
        if (pProp->version >= 6) {
            fSuccess &= (hFile->read(&pProp->flags, 4).bytes == static_cast<std::size_t>(4));
        }
        if (pProp->version >= 7) {
            fSuccess &= (hFile->read(pProp->name, sizeof(pProp->name)).bytes == static_cast<std::size_t>(sizeof(pProp->name)));
            ReportBuildStatus(
                5, FormatString("Prop: %s\n",
                              pProp->name));
        }
        if (fSuccess == 0) {
            srAssertFail("fSuccess", LEVELFILE_CPP, 0x918, 0);
        }
        if (pProp->version >= 8) {
            fSuccess &= (hFile->read(&pProp->num_frame_pos, 1).bytes == static_cast<std::size_t>(1));
            if (pProp->num_frame_pos != 0) {
                pProp->usFrame_Pos = static_cast<W8LevelFileFramePosition*>(
                    malloc(pProp->num_frame_pos * sizeof(W8LevelFileFramePosition)));
                if (pProp->usFrame_Pos == 0) {
                    srAssertFail(
                        "pProps[i1].usFrame_Pos", LEVELFILE_CPP, 0x91f,
                        FormatString("Could not allocate %d segments for prop '%s'!",
                                   static_cast<int>(pProp->num_frame_pos), pProp->name));
                }
                fSuccess &= (hFile->read(pProp->usFrame_Pos, pProp->num_frame_pos << 2).bytes == static_cast<std::size_t>(pProp->num_frame_pos << 2));
            }
        }
        unsigned char okAnimObj = ReadAnimObjFile(hFile, &pProp->anim_obj);
        if ((fSuccess & okAnimObj) == 0) {
            srAssertFail("fSuccess", LEVELFILE_CPP, 0x924, 0);
        }
        fSuccess = fSuccess & okAnimObj & (hFile->read(&pProp->has_trigger, 1).bytes == static_cast<std::size_t>(1));
        if (fSuccess == 0) {
            srAssertFail("fSuccess", LEVELFILE_CPP, 0x931, 0);
        }
        if (pProp->has_trigger != 0) {
            pProp->pTrigger = static_cast<W8LevelFileTrigger*>(malloc(sizeof(*pProp->pTrigger)));
            if (pProp->pTrigger == 0) {
                srAssertFail("pProps[i1].pTrigger", LEVELFILE_CPP, 0x935, 0);
            }
            memset(pProp->pTrigger, 0, sizeof(W8LevelFileTrigger));
            fSuccess &= ReadTriggerFile(hFile, pProp->pTrigger);
            if (fSuccess == 0) {
                srAssertFail("fSuccess", LEVELFILE_CPP, 0x938, 0);
            }
        }
        if (pProp->version >= 9) {
            fSuccess &= (hFile->read(&pProp->has_footsteps, 1).bytes == static_cast<std::size_t>(1));
            if (fSuccess == 0) {
                srAssertFail("fSuccess", LEVELFILE_CPP, 0x93e, 0);
            }
            if (pProp->has_footsteps != 0) {
                fSuccess &= (hFile->read(&pProp->footstep_surface, 1).bytes == static_cast<std::size_t>(1));
                fSuccess &= (hFile->read(&pProp->footstep_material, 1).bytes == static_cast<std::size_t>(1));
            }
        }
    }
    if (fSuccess == 0) {
        return 0;
    }
    return pProps;
}

// FUNCTION: WIZ8 0x004D4FC0
BOOLEAN WritePropsFile(wiz8::File* hFile, int count, W8LevelFileProp* pProps)
{
    unsigned char fSuccess = 1;
    if (count == 0) {
        return TRUE;
    }
    if (pProps == 0) {
        srAssertFail("pProps", LEVELFILE_CPP, 0x962, 0);
    }
    for (int i = 0; i < count; ++i) {
        W8LevelFileProp* pProp = pProps + i;
        fSuccess &= (hFile->write(&pProp->version, 1), true);
        fSuccess &= (hFile->write(&pProp->bNumFrames, 1), true);
        if (pProp->version >= 5) {
            fSuccess &= (hFile->write(&pProp->option, 1), true);
            fSuccess &= (hFile->write(&pProp->position, sizeof(pProp->position)), true);
        }
        if (pProp->version >= 6) {
            fSuccess &= (hFile->write(&pProp->flags, 4), true);
        }
        if (pProp->version >= 7) {
            fSuccess &= (hFile->write(pProp->name, sizeof(pProp->name)), true);
        }
        if (fSuccess == 0) {
            srAssertFail("fSuccess", LEVELFILE_CPP, 0x970, 0);
        }
        if (pProp->version >= 8) {
            fSuccess &= (hFile->write(&pProp->num_frame_pos, 1), true);
            if (pProp->num_frame_pos != 0) {
                if (pProp->usFrame_Pos == 0) {
                    srAssertFail("pProps[i1].usFrame_Pos", LEVELFILE_CPP, 0x976, 0);
                }
                fSuccess &= (hFile->write(pProp->usFrame_Pos, pProp->num_frame_pos << 2), true);
                free(pProp->usFrame_Pos);
            }
        }
        fSuccess &= WriteAnimObjFile(hFile, &pProp->anim_obj);
        fSuccess &= (hFile->write(&pProp->has_trigger, 1), true);
        if (fSuccess == 0) {
            srAssertFail("fSuccess", LEVELFILE_CPP, 0x987, 0);
        }
        if (pProp->has_trigger != 0) {
            fSuccess &= WriteTriggerFile(hFile, pProp->pTrigger);
            if (fSuccess == 0) {
                srAssertFail("fSuccess", LEVELFILE_CPP, 0x98b, 0);
            }
            free(pProp->pTrigger);
            pProp->pTrigger = 0;
        }
        if (pProp->version >= 9) {
            fSuccess &= (hFile->write(&pProp->has_footsteps, 1), true);
            if (fSuccess == 0) {
                srAssertFail("fSuccess", LEVELFILE_CPP, 0x993, 0);
            }
            if (pProp->has_footsteps != 0) {
                fSuccess &= (hFile->write(&pProp->footstep_surface, 1), true);
                fSuccess &= (hFile->write(&pProp->footstep_material, 1), true);
            }
        }
    }
    free(pProps);
    return fSuccess;
}

// FUNCTION: WIZ8 0x004D5240
BOOLEAN ReadParticleSystemFile(wiz8::File* hFile, W8LevelFileParticleSystem* pSystem)
try
{
    BOOLEAN fSuccess = TRUE;
    fSuccess &= (hFile->read(pSystem, 0x217).bytes == static_cast<std::size_t>(0x217));
    if (pSystem->version > 1) {
        fSuccess &= (hFile->read(&pSystem->particle.attachment_key, 2).bytes == static_cast<std::size_t>(2));
    } else {
        pSystem->particle.attachment_key = 0;
    }
    if (pSystem->version > 2) {
        fSuccess &= (hFile->read(&pSystem->particle.emission_limit, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pSystem->particle.requires_sorted_renderer, 1).bytes == static_cast<std::size_t>(1));
    } else {
        pSystem->particle.emission_limit = 0;
        pSystem->particle.requires_sorted_renderer = 0;
    }
    if (pSystem->version > 3) {
        fSuccess &= (hFile->read(&pSystem->particle.start_frame, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pSystem->particle.end_frame, 4).bytes == static_cast<std::size_t>(4));
    } else {
        pSystem->particle.start_frame = 0;
    }
    if (fSuccess == 0) {
        srAssertFail("fSuccess", LEVELFILE_CPP, 0xa03, "Couldn't read particle system.");
    }
    ReportBuildStatus(
        5,
        FormatString("Particle System: %s, Position %f, %f, %f\n", pSystem->particle.name,
                   pSystem->particle.location.x * g_world_scale,
                   pSystem->particle.location.y * g_world_scale,
                   pSystem->particle.location.z * g_world_scale));
    return fSuccess;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004D5370
BOOLEAN WriteParticleSystemFile(wiz8::File* hFile, W8LevelFileParticleSystem* pSystem)
{
    BOOLEAN fSuccess = TRUE;
    fSuccess &= (hFile->write(pSystem, 0x217), true);
    if (pSystem->version > 1) {
        fSuccess &= (hFile->write(&pSystem->particle.attachment_key, 2), true);
    }
    if (pSystem->version > 2) {
        fSuccess &= (hFile->write(&pSystem->particle.emission_limit, 4), true);
        fSuccess &= (hFile->write(&pSystem->particle.requires_sorted_renderer, 1), true);
    }
    if (pSystem->version > 3) {
        fSuccess &= (hFile->write(&pSystem->particle.start_frame, 4), true);
        fSuccess &= (hFile->write(&pSystem->particle.end_frame, 4), true);
    }
    if (fSuccess == 0) {
        srAssertFail("fSuccess", LEVELFILE_CPP, 0xa2a, "Couldn't Write particle system.");
    }
    return fSuccess;
}

// FUNCTION: WIZ8 0x004D5430
BOOLEAN ReadLevelFileBlock(wiz8::File* hFile, W8LevelFileBlock* pBlock)
try
{
    BOOLEAN fSuccess = TRUE;
    fSuccess &= (hFile->read(&pBlock->fog_enabled, 1).bytes == static_cast<std::size_t>(1));
    fSuccess &= (hFile->read(&pBlock->environment_red, 4).bytes == static_cast<std::size_t>(4));
    fSuccess &= (hFile->read(&pBlock->environment_green, 4).bytes == static_cast<std::size_t>(4));
    fSuccess &= (hFile->read(&pBlock->environment_blue, 4).bytes == static_cast<std::size_t>(4));
    fSuccess &= (hFile->read(&pBlock->intensity, 4).bytes == static_cast<std::size_t>(4));
    fSuccess &= (hFile->read(&pBlock->view_distance, 4).bytes == static_cast<std::size_t>(4));
    fSuccess &= (hFile->read(&pBlock->camera_mode, 1).bytes == static_cast<std::size_t>(1));
    if (pBlock->camera_mode >= 1) {
        fSuccess &= (hFile->read(&pBlock->camera_position, sizeof(pBlock->camera_position)).bytes == static_cast<std::size_t>(sizeof(pBlock->camera_position)));
    }
    if (pBlock->camera_mode >= 2) {
        fSuccess &= (hFile->read(&pBlock->camera_angle, 4).bytes == static_cast<std::size_t>(4));
        fSuccess &= (hFile->read(&pBlock->camera_axis, sizeof(pBlock->camera_axis)).bytes == static_cast<std::size_t>(sizeof(pBlock->camera_axis)));
    }
    fSuccess = fSuccess != 0 && (hFile->read(&pBlock->has_light_colours, 1).bytes == static_cast<std::size_t>(1)) != 0;
    if (pBlock->has_light_colours != 0) {
        fSuccess &= (hFile->read(pBlock->light_colours, 0x300).bytes == static_cast<std::size_t>(0x300));
    }
    fSuccess = fSuccess != 0 && (hFile->read(&pBlock->has_environment_colours, 1).bytes == static_cast<std::size_t>(1)) != 0;
    if (pBlock->has_environment_colours != 0) {
        fSuccess &= (hFile->read(pBlock->environment_colours, 0x300).bytes == static_cast<std::size_t>(0x300));
    }
    return fSuccess;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004D5580
BOOLEAN WriteLevelFileBlock(wiz8::File* hFile, W8LevelFileBlock* pBlock)
{
    BOOLEAN fSuccess = TRUE;
    fSuccess &= (hFile->write(&pBlock->fog_enabled, 1), true);
    fSuccess &= (hFile->write(&pBlock->environment_red, 4), true);
    fSuccess &= (hFile->write(&pBlock->environment_green, 4), true);
    fSuccess &= (hFile->write(&pBlock->environment_blue, 4), true);
    fSuccess &= (hFile->write(&pBlock->intensity, 4), true);
    fSuccess &= (hFile->write(&pBlock->view_distance, 4), true);
    fSuccess &= (hFile->write(&pBlock->camera_mode, 1), true);
    if (pBlock->camera_mode >= 1) {
        fSuccess &= (hFile->write(&pBlock->camera_position, sizeof(pBlock->camera_position)), true);
    }
    if (pBlock->camera_mode >= 2) {
        fSuccess &= (hFile->write(&pBlock->camera_angle, 4), true);
        fSuccess &= (hFile->write(&pBlock->camera_axis, sizeof(pBlock->camera_axis)), true);
    }
    fSuccess = fSuccess != 0 && (hFile->write(&pBlock->has_light_colours, 1), true) != 0;
    if (pBlock->has_light_colours != 0) {
        fSuccess &= (hFile->write(pBlock->light_colours, 0x300), true);
    }
    fSuccess = fSuccess != 0 && (hFile->write(&pBlock->has_environment_colours, 1), true) != 0;
    if (pBlock->has_environment_colours != 0) {
        fSuccess &= (hFile->write(pBlock->environment_colours, 0x300), true);
    }
    return fSuccess;
}
