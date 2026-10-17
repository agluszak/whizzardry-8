#include "wiz8/engine_code/Levels.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

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
#include "surrender/srMaterial.h"
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
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/local_screens/AutomapScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/monster_generators.h"
#include "wiz8/music_playlist.h"
#include "wiz8/startup_world.h"
#include "wiz8/world_cursor.h"
#include "wiz8/xstatus.h"

// GLOBAL: WIZ8 0x00659a80
namespace {
std::unique_ptr<W8World> primary_world;
std::unique_ptr<W8World> sky_world;
}

W8World* GetWorld()
{
    return primary_world.get();
}
W8World* GetSecondaryWorld()
{
    return sky_world.get();
}
int GetLoadedLevelID()
{
    return primary_world ? primary_world->level_id : -1;
}

std::unique_ptr<W8World> ReplaceWorld(std::unique_ptr<W8World> world)
{
    return std::exchange(primary_world, std::move(world));
}

std::unique_ptr<W8World> ReplaceSkyWorld(std::unique_ptr<W8World> world)
{
    return std::exchange(sky_world, std::move(world));
}

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
    const auto level = GetLoadedLevelID();
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
    ReplaceSkyWorld(nullptr);
    ResetEnvironment();
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

/* Reuse the sky with the same canonical resource path. An empty path removes
   the previous sky. A replacement receives the animated-cloud material:
   its cloud meshes are marked for the renderer's
   control bits and every CloudsN prop's mesh chain is rebound to it. */
// FUNCTION: WIZ8 0x0042B020
static std::unique_ptr<W8World> PrepareSkyWorld(std::string_view sky_path)
{
    if (sky_path.empty()) return nullptr;
    auto replacement = LoadWorld(-1, std::string(sky_path), false);
    if (!replacement) return nullptr;
    auto* sky = replacement.get();

    const glm::dvec3 position(0);
    static_cast<srNode*>(sky->camera)->setLocation(position);

    auto material = std::make_shared<srMaterial>();
    material->setName("AnimatedCloudMaterial");
    sky->cloud_material = material;
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
    material->setMapper(&g_material_mapper);

    for (srNode* node = sky->level->getChild(); node != 0; node = node->getNext()) {
        if (auto* instance = dynamic_cast<stModelInstance*>(node)) {
            srMeshModel* mesh = static_cast<srMeshModel*>(instance->getModel());
            mesh->enable(srMeshModel::CONTROL_SKIP_AUTO_SPHERE);
            mesh->enable(srMeshModel::CONTROL_SKIP_AUTO_BOX);
        }
    }

    for (int index = 1; index < 9; ++index) {
        const auto prop_name = "Clouds0" + std::to_string(index);
        W8Prop* prop = FindPropByName(sky, prop_name.c_str());
        if (prop == nullptr) continue;
        if (srModelInstance* instance = prop->ToggleRepAnimation(0)) {
            for (stMeshModel* mesh :
                 stMeshModel::Meshes(static_cast<stMeshModel*>(instance->getModel()))) {
                mesh->setMaterial(material, 0, srMeshModel::SIDE_FRONT);
            }
        }
    }

    SetWorldSceneMeshShaderLowBits(sky, 0);
    SetWorldSceneMeshShaderBit3(sky, 1);
    WorldRemoveLight(sky, sky->camera_light);
    sky->camera_light = 0;
    return replacement;
}

unsigned char LoadSkyWorld(std::string_view sky_path)
{
    if (sky_world &&
        sky_world->source_path == wiz8::normalize_asset_path(sky_path).generic_string())
        return 1;
    auto replacement = PrepareSkyWorld(sky_path);
    if (!sky_path.empty() && !replacement) return 0;
    ReplaceSkyWorld(std::move(replacement));
    if (!sky_world) ResetEnvironment();
    return 1;
}

// GLOBAL: WIZ8 0x00605820
unsigned short g_level_name_indices[W8_LEVEL_COUNT] = {
    0x6f7, 0x6f8, 0x6f9, 0x6fa, 0x6fb, 0x6fd, 0x6fc, 0x6fe, 0x6ff, 0x700, 0x701, 0x719,
    0x702, 0x703, 0x704, 0x705, 0x706, 0x707, 0x708, 0x709, 0x70a, 0x70b, 0x70c, 0x719,
    0x70d, 0x70e, 0x70f, 0x710, 0x719, 0x711, 0x719, 0x712, 0x713, 0x714, 0x719, 0x715,
    0x716, 0x717, 0x706, 0x718, 0x719, 0x719, 0x719, 0x719, 0x719, 0x719, 0x719,
};

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

static void EnterLevel(int level, int entrance, bool restoring_game, W8LoadedLevelStatus* status)
{
    bool first_visit = false;
    StartLevelMusic(1, 1);
    InitializeMonsterManagerState();
    InitializeItemManagerState();
    g_status.next_trigger_id = GetWorld()->next_trigger_id;
    UpdateGameDataRuntime();
    if (GetWorld()->game_data) g_environ = GetWorld()->game_data->m_ppEnvirons.front().get();
    if (GetWorld()->fog_enabled) UpdateEnvironmentLight();
    ApplyCameraRotation(&GetWorld()->initial_camera_rotation);
    SetCameraSwayMode(GetWorld()->camera, GetWorld()->initial_camera_sway ? 1 : -1);
    SetSkyNodeVisible(false);
    ResetAutomapView();
    if (status) ActivateLevelStatus(*GetWorld(), *status);

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
        RestoreWorldCameraState(GetSecondaryWorld(), &g_status.pending_move_location);
    }

    if (level < W8_LEVEL_COUNT && !g_status.level_progress[level].visited) {
        ResetMonsterGroupTurnState();
        RebindMonsterGroupScripts();
        g_status.level_progress[level].visited = true;
        first_visit = true;
    }

    StartAmbientSoundList(*GetWorld());
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
    RefreshWorldView();
}

/* Construct level and sky resources before leaving the current level. The
   remaining work activates gameplay against the completed scenes. */
// FUNCTION: WIZ8 0x0042A6F0
unsigned char LoadLevel(int requested_level, int entrance, bool restoring_game)
{
    int level = NormalizeMasterFunctionValue(requested_level);
    W8LevelInfo level_info;

    if (level >= 57) {
        srAssertFail("iLevel < TEST_LEVEL_COUNT",
                     "C:\\Projects\\Wizardry 8\\Engine Code\\Levels.cpp", 385, 0);
    }
    if (!LevelBuildInfoByID(level, &level_info)) {
        return 0;
    }

    auto replacement = LoadWorld(level, level_info.level_path);
    if (!replacement) return 0;
    auto ambient_path = wiz8::path_from_utf8(level_info.level_path);
    ambient_path.replace_extension(".SCF");
    ReadAmbientSoundListFile(*replacement, wiz8::path_to_utf8(ambient_path).c_str());
    const bool reuse_sky =
        sky_world &&
        sky_world->source_path == wiz8::normalize_asset_path(level_info.sky_path).generic_string();
    auto replacement_sky = reuse_sky ? nullptr : PrepareSkyWorld(level_info.sky_path);
    if (!reuse_sky && !level_info.sky_path.empty() && !replacement_sky) return 0;
    if (GetWorld() && !SaveLevelStatus("saves\\CurrentGame.SAV")) return 0;
    auto status = ReadLevelStatus(*replacement, wiz8::user_path("saves/CurrentGame.SAV"), level);
    if (!status)
        status = ReadLevelStatus(*replacement, wiz8::asset_path(BuildLevelStatusPath(level)), level,
                                 true);
    if (GetWorld()) UnloadLevel();
    InvalidateRendererTextureCache();
    ReplaceWorld(std::move(replacement));
    if (!reuse_sky) ReplaceSkyWorld(std::move(replacement_sky));
    EnterLevel(level, entrance, restoring_game, status.get());
    return 1;
}

// FUNCTION: WIZ8 0x0042ACE0
void UnloadLevel()
{
    if (!primary_world) return;
    if (gXStatus.fCombatMode) {
        EndCombat(true);
    }

    if (GetLoadedLevelID() >= 0 && GetLoadedLevelID() < W8_LEVEL_COUNT) {
        g_status.level_progress[GetLoadedLevelID()].sight_clock = g_status.world_clock;
    }

    if (g_world_cleanup_flag) {
        RenderFrame();
    }

    ReleaseMarkedNpcBindings();

    if (g_world_cleanup_flag) {
        RenderFrame();
    }

    ReleaseItemLists();
    if (g_world_cleanup_flag) {
        RenderFrame();
    }
    ShutdownMonsterManager();
    if (g_world_cleanup_flag) {
        RenderFrame();
    }

    ReleaseWorldCursorNodes();
    if (g_world_cleanup_flag) {
        RenderFrame();
    }

    ReleaseWorldCursor();
    DisableSky();
    EndCameraSway();
    if (primary_world->game_data) primary_world->game_data->ReleaseLevelData();
    ReplaceWorld(nullptr);

    if (g_world_cleanup_flag) {
        RenderFrame();
    }

    DisableSky();
    g_runtime_world_scale = 500.0f;
    ClearActiveWorldCursorNode();
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

    if (entrance == -1 && level == GetLoadedLevelID()) {
        GetCameraOrientation(saved_angle, saved_pitch);
        saved_position = cameraLocationOrZero(*GetWorld());
        restore = true;
    }
    if (LoadLevel(level, entrance, false) == 0) {
        return 0;
    }
    if (restore) {
        RestoreWorldCameraOrientation(saved_angle, saved_pitch);
        SetWorldScenePosition(GetWorld(), &saved_position);
    }
    return 1;
}
