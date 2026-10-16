#include "wiz8/engine_code/Levels.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ReadMesh.h"
#include "asset_paths.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "layouts/levels.h"
#include "layouts/world.h"
#include "surrender/srCamera.h" // IWYU pragma: keep - complete camera type for conversion to srNode.
#include "surrender/srDebug.h"
#include "surrender/srMath.h"
#include "surrender/srMeshModel.h"
#include "surrender/srModelInstance.h"
#include "surrender/srNode.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/3dapi.h"
#include "wiz8/engine_code/AmbientSound.h"
#include "wiz8/engine_code/Environment.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Level.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/fact_state.h"
#include "wiz8/filesystem.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/Search.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/local_screens/AutomapScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/monster_generators.h"
#include "wiz8/music_playlist.h"
#include "wiz8/startup_world.h"
#include "wiz8/world_cursor.h"
#include "wiz8/xstatus.h"

#define LEVELS_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\Levels.cpp"

// GLOBAL: WIZ8 0x00604478
W8LevelFolderRecord g_level_folders[W8_LEVEL_COUNT] = {
    {"Arnika", "Arnika2", "ARN", 1, 2},
    {"Ascension", "Ascension", "ASC", 0, 12},
    {"Bayjin", "Bayjin1", "BA1", -1, 8},
    {"Bayjin", "Bayjin2", "BA2", -1, 8},
    {"Circle", "Circle", "CIR", 10, 14},
    {"Marten", "Marten1", "MR1", -1, 5},
    {"Marten", "Marten2", "MR2", 0, 5},
    {"MountainPass", "MountainPass", "MNT", 2, 6},
    {"Monastery", "Monastery1", "MO1", 0, 1},
    {"Monastery", "Monastery2", "MO2", 0, 1},
    {"MtGigas", "MtGigasWaterCaves", "MGW", -1, 15},
    {"MtGigas", "MtGigasBelowCaves", "MGB", -1, 11},
    {"MtGigas", "MtGigas1", "MG1", -1, 11},
    {"MtGigas", "MtGigas2", "MG2", -1, 11},
    {"MtGigas", "MtGigasOuter", "MGO", 0, 11},
    {"MtGigas", "MtGigasTop", "MGT", 0, 11},
    {"Camp", "Camp", "CMP", 7, 13},
    {"Rapax", "RapaxCellar", "RAC", 1, 9},
    {"Rapax", "RapaxMainFloor", "RAM", 1, 9},
    {"Rapax", "RapaxUpperFloor", "RAU", 1, 9},
    {"Rapax", "RapaxExterior", "RAE", 1, 9},
    {"Rift", "Rift1", "RIF", 3, 10},
    {"SeaCaves", "SeaCave1", "SC1", 0, 7},
    {"SeaCaves", "SeaCave2", "SC2", 0, 7},
    {"Swamp", "Swamp", "SWM", -1, 4},
    {"Trynnie", "Trynnie1", "TY1", -1, 3},
    {"Trynnie", "Trynnie2", "TY2", -1, 3},
    {"ConnectiveTissue", "Arnika_Trynton", "CT1", 0, 16},
    {"ConnectiveTissue", "Trynton_Swamp", "CT2", 0, 0},
    {"ConnectiveTissue2", "SouthEastWilderness", "CT3", 0, 17},
    {"ConnectiveTissue", "Rift_Peak", "CT4", 0, 0},
    {"ConnectiveTissue2", "NorthEastWilderness", "CT5", 0, 0},
    {"ConnectiveTissue", "NorthWilderness", "CT6", 0, 0},
    {"ConnectiveTissue", "Arnika_StarterDungeon", "CT7", 0, 0},
    {"ConnectiveTissue", "Peak_RapaxCastle", "CT8", 0, 0},
    {"Footsteps", "Footsteps", "FS1", 0, 0},
    {"SavantTower", "SavantTower", "SAV", -1, 2},
    {"Trynnie", "Ratkin", "RTK", -1, 3},
    {"Camp", "CampNoRapax", "CNR", 7, 0},
    {"Dungeon", "Dungeon", "DUN", -1, 0},
    {"Spare14", "Spare14", "SPE", 0, 0},
    {"Spare15", "Spare15", "SPF", 0, 0},
    {"Spare16", "Spare16", "SPG", 0, 0},
    {"Spare17", "Spare17", "SPH", 0, 0},
    {"Spare18", "Spare18", "SPI", 0, 0},
    {"Spare19", "Spare19", "SPJ", 0, 0},
    {"Spare20", "Spare20", "SPK", 0, 0},
};

/* The sky index of the world currently held in g_secondary_world, or -1 when no
   sky is loaded. Every retail access is a byte access. */
// GLOBAL: WIZ8 0x00604470
static signed char g_loaded_sky_index = -1;
// GLOBAL: WIZ8 0x00659738
W8MaterialMapper g_material_mapper;

// FUNCTION: WIZ8 0x0042b740
char GetLevelBand(int saved_level)
{
    int level = NormalizeMasterFunctionValue(saved_level);
    /* `<= 0x2f` is the retail bound: it admits the first test-level slot, one
       past the W8_LEVEL_COUNT-entry table. */
    if (level >= 0 && level <= 0x2f) {
        return g_level_folders[level].level_band;
    }
    return 0;
}

/* Start the saved level's music playlist. Formats a path from the level's
   name, falls back to Adventure.MPL when no level-specific playlist exists. */
// FUNCTION: WIZ8 0x0042b770
void StartLevelMusic(int fade, int replace_current)
{
    const auto level = g_status.current_level;
    const std::string playlist = level >= 0 && level < W8_LEVEL_COUNT
                                     ? std::string(g_level_folders[level].level_name) + ".MPL"
                                     : "Adventure.MPL";
    const auto path = std::filesystem::path("Data") / "Music" / wiz8::path_from_utf8(playlist);
    const auto status = wiz8::file_status(wiz8::asset_path(wiz8::path_to_utf8(path)));
    StartMusicResource(status && status->info.type == SDL_PATHTYPE_FILE ? playlist.c_str()
                                                                        : "Adventure.MPL",
                       fade, replace_current);
    ServiceMusicPlaylist();
}

// FUNCTION: WIZ8 0x0042b3e0
void UnloadSkyWorld(void)
{
    W8World* world = GetSecondaryWorld();
    if (world != 0) {
        ForwardDestroyWorld(world);
        SetSecondaryWorld(0);
        g_loaded_sky_index = 0xff;
        ResetEnvironment();
    }
}

// The original bounds-checks only the upper end, so a negative level_id reads
// before the table. Reproduced as-is; GetLevelFolderName checks both ends.
// FUNCTION: WIZ8 0x0042b500
unsigned char GetLevelLocationCode(int level_id, char* location_code)
{
    if (level_id >= W8_LEVEL_COUNT) {
        return 0;
    }
    if (!location_code) {
        return 0;
    }
    strcpy(location_code, g_level_folders[level_id].location_code);
    return 1;
}

static int LevelFindIDByLocationCode(const char* location_code)
{
    int level_id;

    for (level_id = 0; level_id < W8_LEVEL_COUNT; level_id++) {
        if (SDL_strcasecmp(g_level_folders[level_id].location_code, location_code) == 0) {
            return level_id;
        }
    }
    return -1;
}

// FUNCTION: WIZ8 0x0042b410
int FindLevelIdByLocationCode(const char* location_code)
{
    W8LevelInfo info;
    int level_id;

    if (!location_code) {
        return -1;
    }
    level_id = LevelFindIDByLocationCode(location_code);
    if (level_id == -1) {
        return level_id;
    }
    if (level_id >= 57) {
        return -1;
    }
    if (level_id < W8_LEVEL_COUNT) {
        if (strlen(g_level_folders[level_id].folder_name) == 0 ||
            strlen(g_level_folders[level_id].level_name) == 0) {
            return -1;
        }
    }
    if (!LevelBuildInfoByID(level_id, &info)) {
        return -1;
    }
    return level_id;
}

// FUNCTION: WIZ8 0x0042b550
const char* GetLevelFolderName(int level_id)
{
    if (level_id >= W8_LEVEL_COUNT || level_id < 0) {
        return 0;
    }
    return g_level_folders[level_id].folder_name;
}

// FUNCTION: WIZ8 0x0042b580
int GetLoadedLevelID(void)
{
    return g_status.current_level;
}

/* Load or replace the sky world for a level. A level whose sky index already
   matches the world held in g_secondary_world reuses it; a level with no sky
   (-1) only tears the previous one down. The replacement sky receives the
   animated-cloud material: its cloud meshes are marked for the renderer's
   control bits and every CloudsN prop's mesh chain is rebound to it. */
// FUNCTION: WIZ8 0x0042B020
unsigned char LoadSkyWorld(int level, W8LevelInfo* info)
{
    signed char sky_index;
    W8LevelInfo local_info;
    W8World* sky_world;

    if (level < W8_LEVEL_COUNT) {
        sky_index = g_level_folders[level].sky_index;
    } else {
        sky_index = 0;
    }
    if (sky_index == g_loaded_sky_index) {
        return 1;
    }
    if (info == 0) {
        if (!LevelBuildInfoByID(level, &local_info)) {
            return 0;
        }
        info = &local_info;
    }
    if (GetSecondaryWorld() != 0) {
        ForwardDestroyWorld(GetSecondaryWorld());
        SetSecondaryWorld(0);
        g_loaded_sky_index = 0xff;
        ResetEnvironment();
    }
    if (sky_index == -1 || info->sky_path.empty()) {
        return 1;
    }
    sky_world = CreateWorld();
    g_secondary_world = sky_world;
    if (sky_world == 0) {
        return 0;
    }
    if (!LoadWorld(sky_world, info->sky_path, false)) {
        return 0;
    }

    const glm::dvec3 position(0);
    static_cast<srNode*>(sky_world->camera)->setLocation(position);

    auto material = std::make_shared<stMaterial>();
    material->setName("AnimatedCloudMaterial");
    sky_world->cloud_material = material;
    glm::vec4 colour;
    colour = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    material->setAmbient(colour);
    colour = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    material->setDiffuse(colour);
    material->setSpecular(colour);
    material->parms.shininess = 1.0f;
    material->parms.diffuse.w = 1.0f;
    material->parms.emissive = glm::vec4(0.0f);
    material->dirty = 1;
    material->m_surface_flags = 0;
    material->setMapper(&g_material_mapper);

    for (srNode* node = sky_world->level->getChild(); node != 0; node = node->getNext()) {
        if (auto* instance = dynamic_cast<stModelInstance*>(node)) {
            srMeshModel* mesh = static_cast<srMeshModel*>(instance->getModel());

            mesh->enable(srMeshModel::CONTROL_SKIP_AUTO_SPHERE); /* CONTROL_SKIP_AUTO_SPHERE */
            mesh->enable(srMeshModel::CONTROL_SKIP_AUTO_BOX); /* CONTROL_SKIP_AUTO_BOX */
        }
    }

    for (int index = 1; index < 9; ++index) {
        char prop_name[28];

        sprintf(prop_name, "Clouds0%d", index);
        W8Prop* prop = FindPropByName(sky_world, prop_name);
        if (prop != 0) {
            srModelInstance* instance = prop->ToggleRepAnimation(0);

            if (instance != 0) {
                for (stMeshModel* mesh :
                     stMeshModel::Meshes(static_cast<stMeshModel*>(instance->getModel()))) {
                    mesh->setMaterial(material, 0, srMeshModel::SIDE_FRONT);
                }
            }
        }
    }

    SetWorldSceneMeshShaderLowBits(GetSecondaryWorld(), 0);
    SetWorldSceneMeshShaderBit3(GetSecondaryWorld(), 1);
    WorldRemoveLight(sky_world, sky_world->camera_light);
    sky_world->camera_light = 0;
    g_loaded_sky_index = sky_index;
    return 1;
}

// GLOBAL: WIZ8 0x00605820
unsigned short g_level_name_indices[W8_LEVEL_COUNT] = {
    0x6f7, 0x6f8, 0x6f9, 0x6fa, 0x6fb, 0x6fd, 0x6fc, 0x6fe, 0x6ff, 0x700, 0x701, 0x719,
    0x702, 0x703, 0x704, 0x705, 0x706, 0x707, 0x708, 0x709, 0x70a, 0x70b, 0x70c, 0x719,
    0x70d, 0x70e, 0x70f, 0x710, 0x719, 0x711, 0x719, 0x712, 0x713, 0x714, 0x719, 0x715,
    0x716, 0x717, 0x706, 0x718, 0x719, 0x719, 0x719, 0x719, 0x719, 0x719, 0x719,
};

// GLOBAL: WIZ8 0x00659756
bool g_level_status_loading;

// GLOBAL: WIZ8 0x00603ac8
float g_default_world_height = 1000.0f;

// GLOBAL: WIZ8 0x005ebfdc
const float g_position_height_epsilon = 2500.0f;

// GLOBAL: WIZ8 0x00603ad0
unsigned char g_environment_load_flag = 1;

// GLOBAL: WIZ8 0x0065ba70
bool g_camera_path_active;

// GLOBAL: WIZ8 0x0068f0fd

// GLOBAL: WIZ8 0x00605880
const char* g_sky_names[] = {
    "DefaultSky", "RapaxSky", "MountainPassSky", "RiftSky1",  "TrynnieSky1", "TrynnieSky2",
    "RatkinSky1", "CampSky",  "BluffSky1",       "BluffSky2", "CircleSky",
};

/* Resolve a database level number into all level and sky resource paths. The
   regular forty-seven levels use the database row directly; the ten test
   slots synthesize level1..level9 and DefaultLevel. A missing LVL file is
   valid only when both its OCT and PVL replacements exist. */
// FUNCTION: WIZ8 0x0042A370
bool LevelBuildInfoByID(int level, W8LevelInfo* info)
{
    if (level < 0 || level >= 57 || info == nullptr) return false;

    std::filesystem::path folder = "Levels";
    std::string level_name;
    std::string sky_name;
    if (level < W8_LEVEL_COUNT) {
        const auto& row = g_level_folders[level];
        folder /= wiz8::path_from_utf8(row.folder_name);
        level_name = row.level_name;
        if (row.sky_index != -1) sky_name = g_sky_names[row.sky_index];
    } else {
        folder /= "Test";
        if (level == 56) {
            level_name = "DefaultLevel";
            sky_name = g_sky_names[0];
        } else {
            const auto number = std::to_string(level - W8_LEVEL_COUNT + 1);
            level_name = "level" + number;
            sky_name = "sky" + number;
        }
    }

    const auto is_file = [](const std::filesystem::path& path) {
        const auto status = wiz8::file_status(wiz8::asset_path(wiz8::path_to_utf8(path)));
        return status && status->info.type == SDL_PATHTYPE_FILE;
    };
    auto level_path = folder / wiz8::path_from_utf8(level_name + ".LVL");
    auto oct_path = level_path;
    oct_path.replace_extension(".oct");
    auto pvl_path = level_path;
    pvl_path.replace_extension(".pvl");
    if (!is_file(level_path) && (!is_file(oct_path) || !is_file(pvl_path))) return false;

    std::filesystem::path sky_path;
    if (!sky_name.empty()) {
        sky_path = folder / wiz8::path_from_utf8(sky_name + ".LVL");
        if (!is_file(sky_path)) {
            if (level < W8_LEVEL_COUNT) {
                sky_path = std::filesystem::path("Levels") / "Test" / "DefaultSky.LVL";
                if (!is_file(sky_path)) return false;
            } else {
                sky_path.clear();
            }
        }
    }
    *info = {wiz8::path_to_utf8(level_path), wiz8::path_to_utf8(sky_path)};
    return true;
}

/* Build the complete live level around the current world. The subordinate
   loaders remain in their original units; this body owns their ordering,
   rollback boundary, entry positioning, first-visit work and final renderer
   publication. */
// FUNCTION: WIZ8 0x0042A6F0
unsigned char LoadLevel(int requested_level, int entrance, bool restoring_game)
{
    int level = NormalizeMasterFunctionValue(requested_level);
    W8LevelInfo level_info;
    int previous_level;
    bool first_visit = false;

    if (level >= 57) {
        srAssertFail("iLevel < TEST_LEVEL_COUNT",
                     "C:\\Projects\\Wizardry 8\\Engine Code\\Levels.cpp", 385, 0);
    }
    if (!LevelBuildInfoByID(level, &level_info)) {
        return 0;
    }

    DisableSky();
    if (GetWorld() != 0) {
        ForwardDestroyWorld(GetWorld());
        SetCurrentWorld(0);
    }
    ReleaseReadMeshScratch();
    InvalidateRendererTextureCache();
    SetCurrentWorld(CreateWorld());

    previous_level = g_status.current_level;
    g_status.current_level = level;
    StartLevelMusic(1, 1);

    if (!LoadSkyWorld(level, &level_info)) {
        return 0;
    }
    InitializeMonsterManagerState();
    InitializeItemManagerState();
    ResetNextTriggerId();
    if (!LoadWorld(GetWorld(), level_info.level_path, true)) {
        /* This is the complete canonical rollback here: restore the level ID.
           The already-installed replacement world is not destroyed. */
        g_status.current_level = previous_level;
        return 0;
    }

    SetSkyNodeVisible(false);
    ResetAutomapView();
    if (!LoadLevelStatus(wiz8::user_path("saves/CurrentGame.SAV"), level)) {
        const auto status_path = BuildLevelStatusPath(level);
        g_level_status_loading = true;
        LoadLevelStatus(wiz8::asset_path(status_path), level);
        g_level_status_loading = false;
    }

    if (!restoring_game && entrance != -1) {
        Trigger* trigger = 0;

        if (level < W8_LEVEL_COUNT) {
            char trigger_name[8];
            sprintf(trigger_name, "%3s%02d", g_level_folders[level].location_code, entrance);
            trigger = FindTriggerByName(trigger_name);
        }
        if (trigger != 0 && ((trigger->flags & W8_TRIGGER_POSITIONED) != 0)) {
            glm::vec3 trigger_position;
            glm::vec3 position;

            trigger->GetPosition(&trigger_position);
            position = trigger_position;
            position.y = probeGround(trigger_position).position.y + g_default_world_height;
            if (fabs(position.y - trigger_position.y) > g_position_height_epsilon) {
                position.y = trigger_position.y;
            }
            SetWorldScenePosition(GetWorld(), &position);

            if (trigger->trigger_kind == 2) {
                glm::vec3 axis;
                glm::mat3 rotation;

                axis = trigger->direction;
                rotation = glm::mat3(1);
                if (trigger->angle != 0.0f) {
                    rotation *= glm::mat3_cast(
                        glm::angleAxis(float(std::atan2(sin(trigger->angle), cos(trigger->angle))),
                                       normalizedOrZero(glm::vec3(axis))));
                }
                ApplyCameraRotation(&rotation);
            }
        } else {
            glm::vec3 position;

            position = glm::vec3(0.0f, g_default_world_height, 0.0f);
            SetWorldScenePosition(GetWorld(), &position);
        }
    } else {
        RestoreWorldCameraState(GetWorld(), GetSecondaryWorld(), &g_status.pending_move_location);
    }

    if (level < W8_LEVEL_COUNT && !g_status.level_progress[level].visited) {
        ResetMonsterGroupTurnState();
        RebindMonsterGroupScripts();
        g_status.level_progress[level].visited = true;
        first_visit = true;
    }

    auto ambient_path = wiz8::path_from_utf8(level_info.level_path);
    ambient_path.replace_extension(".SCF");
    ReadAmbientSoundListFile(wiz8::path_to_utf8(ambient_path).c_str());
    if (!g_environment_load_flag) {
        ResetCurrentEnvironment();
    }
    g_camera_path_active = false;
    InitializeLevelEnvironment();
    InitializeLevelMasterFunctions(level);
    RebindNpcLevelTriggers();
    SetWorldCursorNodesVisible(false);
    RebuildPartyEffectBlock();

    if (!restoring_game) {
        if (level < W8_LEVEL_COUNT) {
            ResetNpcBindingsForParty();
            ClearPendingNpcLevelFlags();
            ReleaseNpcMonsterBindings();
            RecordLevelEntryDialogueState();
            gXStatus.combat_countdown = 0;
        }
        if (g_status.greeting_pending &&
            (GetFact(W8_FACT_IMPORT_UMPANI) || GetFact(W8_FACT_IMPORT_TRANG))) {
            DespawnAllActiveMonsterGroups();
        } else {
            UpdateRandomEncounterBudget(first_visit);
        }
        if (!first_visit) {
            ResetAndRefreshAllSight();
            AgeAllMonsterSight();
        }
        g_spell_effects.clear();
    } else {
        W8SpellEffectEntry* effect = FindMonsterControlSpellEffect();

        if (effect != 0) {
            SpawnLureEffects(effect, effect->definition.duration_scale, &effect->target);
        }
    }

    if (first_visit) {
        InitializeMonsterRuntimeStats();
        RebuildAllWorldItemInstances();
    }
    MarkRendererReady();
    UpdateWorldMeshAfterLoad();
    ReleaseReadMeshScratch();
    return 1;
}

// FUNCTION: WIZ8 0x0042ACE0
unsigned char UnloadLevel(const char* save_directory)
{
    if (gXStatus.fCombatMode) {
        EndCombat(true);
    }

    if (g_status.current_level >= 0 && g_status.current_level < W8_LEVEL_COUNT) {
        g_status.level_progress[g_status.current_level].sight_clock = g_status.world_clock;
    }

    if (g_world_cleanup_flag) {
        RenderFrame();
    }

    ReleaseMarkedNpcBindings();
    if (strcmp(save_directory, "") != 0) {
        SaveLevelStatus("saves\\CurrentGame.SAV");
    }

    if (g_world_cleanup_flag) {
        RenderFrame();
    }

    if (ReleaseItemLists() == 0) {
        return 0;
    }
    if (g_world_cleanup_flag) {
        RenderFrame();
    }
    if (ShutdownMonsterManager() == 0) {
        return 0;
    }
    if (g_world_cleanup_flag) {
        RenderFrame();
    }
    if (ScreenLifecycleSuccess() == 0) {
        return 0;
    }
    if (g_world_cleanup_flag) {
        RenderFrame();
    }

    ReleaseWorldCursorNodes();
    ClearSearchables();
    if (g_world_cleanup_flag) {
        RenderFrame();
    }

    ReleaseWorldCursor();
    DisableSky();
    W8World* world = GetWorld();
    if (world != 0) {
        ForwardDestroyWorld(world);
        SetCurrentWorld(0);
    }
    ReleaseReadMeshScratch();

    if (g_world_cleanup_flag) {
        RenderFrame();
    }

    DisableSky();
    g_status.current_level = -1;
    ReleaseEnvironmentObjects();
    g_runtime_world_scale = 500.0f;
    ClearActiveWorldCursorNode();
    return 1;
}

/* Unload the current level and load `level` at `entrance`. When the request
   re-enters the current level through the -1 entrance sentinel, the camera's
   saved yaw/pitch records and position are put back after the load; every
   other request is a plain unload+load. */
// FUNCTION: WIZ8 0x0042AF60
unsigned char ReloadLevelPreservingCamera(int level, int entrance)
{
    W8CameraAngleRecord saved_angle;
    W8CameraAngleRecord saved_pitch;
    glm::vec3 saved_position;
    bool restore = false;

    if (entrance == -1 && level == g_status.current_level) {
        GetCameraOrientation(saved_angle, saved_pitch);
        saved_position = cameraLocationOrZero(*GetWorld());
        restore = true;
    }
    if (g_status.current_level != -1) {
        if (UnloadLevel("saves") == 0) {
            return 0;
        }
    }
    if (LoadLevel(level, entrance, false) == 0) {
        return 0;
    }
    if (restore) {
        RestoreWorldCameraOrientation(saved_angle, saved_pitch, GetWorld());
        SetWorldScenePosition(GetWorld(), &saved_position);
    }
    return 1;
}
