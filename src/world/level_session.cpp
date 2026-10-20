#include "wiz8/world/level_session.h"

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
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "asset_paths.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "layouts/levels.h"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/fact_state.h"
#include "wiz8/filesystem.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/level_script.h"
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
#include "wiz8/location_variables.h"
#include "wiz8/math.h"
#include "wiz8/monster_generators.h"
#include "wiz8/music_playlist.h"
#include "wiz8/platform/GameTimeAccumulator.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/material.h"
#include "wiz8/render/material_loader.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/mesh_loader.h"
#include "wiz8/script_context.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/unicode.h"
#include "wiz8/world/camera_control.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"
#include "wiz8/world_cursor.h"

namespace {
Level& CurrentLevel()
{
    return CurrentGame().level;
}
}

World* GetWorld()
{
    return GameInProgress() ? CurrentGame().level.primary.get() : nullptr;
}
World* GetSecondaryWorld()
{
    return GameInProgress() ? CurrentGame().level.sky.get() : nullptr;
}
int GetLoadedLevelID()
{
    const World* world = GetWorld();
    return world ? world->level_id : -1;
}

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

signed char GetLevelBand(int saved_level)
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

void UnloadSkyWorld(void)
{
    CurrentLevel().replaceSky(nullptr);
    ResetEnvironment();
}

// The original bounds-checks only the upper end, so a negative level_id reads
// before the table. Reproduced as-is; GetLevelFolderName checks both ends.
std::optional<std::string> GetLevelLocationCode(int level_id)
{
    if (level_id >= W8_LEVEL_COUNT || level_id < 0) {
        return std::nullopt;
    }
    return std::string(g_level_folders[level_id].location_code);
}

static int LevelFindIDByLocationCode(std::string_view location_code)
{
    for (int level_id = 0; level_id < W8_LEVEL_COUNT; level_id++) {
        if (CompareTextIgnoreAsciiCase(g_level_folders[level_id].location_code, location_code) ==
            0) {
            return level_id;
        }
    }
    return -1;
}

int FindLevelIdByLocationCode(std::string_view location_code)
{
    W8LevelInfo info;
    int level_id;

    if (location_code.empty()) {
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

std::string_view GetLevelFolderName(int level_id)
{
    if (level_id >= W8_LEVEL_COUNT || level_id < 0) {
        return {};
    }
    return g_level_folders[level_id].folder_name;
}

/* Reuse the sky with the same canonical resource path. An empty path removes
   the previous sky. A replacement receives the animated-cloud material:
   its cloud meshes are marked for the renderer's
   control bits and every CloudsN prop's mesh chain is rebound to it. */
static std::unique_ptr<World> PrepareSkyWorld(std::string_view sky_path)
{
    if (sky_path.empty()) return nullptr;
    auto replacement = LoadWorld(-1, std::string(sky_path), false);
    if (!replacement) return nullptr;
    auto* sky = replacement.get();

    const glm::dvec3 position(0);
    sky->camera->transform.setLocation(position);

    auto material = std::make_shared<Material>();
    material->setName("AnimatedCloudMaterial");
    sky->environment.cloud_material = material;
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
    material->uv_mapping = Material::UvMapping::Scroll;
    material->uv_velocity = glm::vec2(.002f, 0);

    for (const auto& instance : sky->scene->static_meshes) {
        auto* mesh = instance->getModel();
        mesh->enable(Mesh::CONTROL_SKIP_AUTO_SPHERE);
        mesh->enable(Mesh::CONTROL_SKIP_AUTO_BOX);
    }

    for (int index = 1; index < 9; ++index) {
        const auto prop_name = "Clouds0" + std::to_string(index);
        Prop* prop = FindPropByName(sky, prop_name.c_str());
        if (prop == nullptr) continue;
        if (MeshInstance* instance = prop->ToggleRepAnimation(0)) {
            for (Mesh* mesh : meshParts(static_cast<Mesh*>(instance->getModel()))) {
                mesh->setMaterial(material, 0, Mesh::SIDE_FRONT);
            }
        }
    }

    SetWorldSceneMeshShaderLowBits(sky, 0);
    SetWorldSceneMeshShaderBit3(sky, 1);
    WorldRemoveLight(sky, sky->camera_light);
    sky->camera_light = 0;
    return replacement;
}

bool LoadSkyWorld(std::string_view sky_path)
{
    Level& level = CurrentLevel();
    if (level.sky &&
        level.sky->source_path == wiz8::normalize_asset_path(sky_path).generic_string())
        return true;
    auto replacement = PrepareSkyWorld(sky_path);
    if (!sky_path.empty() && !replacement) return false;
    level.replaceSky(std::move(replacement));
    if (!level.sky) ResetEnvironment();
    return true;
}

unsigned short g_level_name_indices[W8_LEVEL_COUNT] = {
    0x6f7, 0x6f8, 0x6f9, 0x6fa, 0x6fb, 0x6fd, 0x6fc, 0x6fe, 0x6ff, 0x700, 0x701, 0x719,
    0x702, 0x703, 0x704, 0x705, 0x706, 0x707, 0x708, 0x709, 0x70a, 0x70b, 0x70c, 0x719,
    0x70d, 0x70e, 0x70f, 0x710, 0x719, 0x711, 0x719, 0x712, 0x713, 0x714, 0x719, 0x715,
    0x716, 0x717, 0x706, 0x718, 0x719, 0x719, 0x719, 0x719, 0x719, 0x719, 0x719,
};

float g_default_world_height = 1000.0f;

const float g_position_height_epsilon = 2500.0f;

unsigned char g_environment_load_flag = 1;

bool g_camera_path_active;

const char* g_sky_names[] = {
    "DefaultSky", "RapaxSky", "MountainPassSky", "RiftSky1",  "TrynnieSky1", "TrynnieSky2",
    "RatkinSky1", "CampSky",  "BluffSky1",       "BluffSky2", "CircleSky",
};

/* Resolve a database level number into all level and sky resource paths. The
   regular forty-seven levels use the database row directly; the ten test
   slots synthesize level1..level9 and DefaultLevel. A missing LVL file is
   valid only when both its OCT and PVL replacements exist. */
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
    CurrentGame().status.next_trigger_id = GetWorld()->next_trigger_id;
    UpdateGameDataRuntime();
    if (GetWorld()->environment.fog_enabled) UpdateEnvironmentLight();
    ApplyCameraRotation(&GetWorld()->initial_camera_rotation);
    GetWorld()->party_camera.applySway(*GetWorld()->camera, GetWorld()->initial_camera_sway
                                                                ? CameraSwayMode::Start
                                                                : CameraSwayMode::Stop);
    SetSkyNodeVisible(false);
    ResetAutomapView();
    BeginRestoredLevelActivity(*GetWorld(), status);

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

            if (trigger->trigger_kind == W8TriggerKind::prop) {
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
        RestoreWorldCameraState(&CurrentGame().status.pending_move_location);
    }

    if (level < W8_LEVEL_COUNT && !CurrentGame().status.level_progress[level].visited) {
        ResetMonsterGroupTurnState();
        RebindMonsterGroupScripts();
        CurrentGame().status.level_progress[level].visited = true;
        first_visit = true;
    }

    StartAmbientSoundList(*GetWorld());
    if (!g_environment_load_flag) {
        ResetCurrentEnvironment();
    }
    g_camera_path_active = false;
    InitializeLevelEnvironment();
    World& world = *GetWorld();
    world.script = MakeLevelScript(level, MakeScriptContext(CurrentGame()));
    world.script->enter();
    RebindNpcLevelTriggers();
    SetWorldCursorNodesVisible(false);
    RebuildPartyEffectBlock();

    if (!restoring_game) {
        if (level < W8_LEVEL_COUNT) {
            ResetNpcBindingsForParty();
            ClearPendingNpcLevelFlags();
            ReleaseNpcMonsterBindings();
            RecordLevelEntryDialogueState();
            ForgetRecentCombat();
        }
        if (CurrentGame().status.greeting_pending &&
            (GetFact(W8_FACT_IMPORT_UMPANI) || GetFact(W8_FACT_IMPORT_TRANG))) {
            DespawnAllActiveMonsterGroups();
        } else {
            UpdateRandomEncounterBudget(first_visit);
        }
        if (!first_visit) {
            ResetAndRefreshAllSight();
            AgeAllMonsterSight();
        }
        CurrentGame().spell_effects.clear();
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
static void FinishLevelDeparture()
{
    // Combat state is transient on restore. End it after saving the level;
    // sight age is session history recorded at the actual departure boundary.
    if (CurrentGame().combat.has_value()) {
        CurrentGame().endCombat(true);
    }

    // These values become durable session history at departure. Saving only
    // observes them; releasing the old world's resources never captures gameplay.
    const World& world = *GetWorld();
    for (const auto& [name, value] :
         world.script ? world.script->captureState() : W8SavedLocationState{}) {
        CreateLocationVar(name.c_str(), value);
        SetTriggerVariableByName(name.c_str(), value);
    }
    if (GetLoadedLevelID() >= 0 && GetLoadedLevelID() < W8_LEVEL_COUNT) {
        CurrentGame().status.level_progress[GetLoadedLevelID()].sight_clock =
            CurrentGame().status.world_clock;
    }
}

bool LoadLevel(int requested_level, int entrance, bool restoring_game)
{
    int level = NormalizeMasterFunctionValue(requested_level);
    W8LevelInfo level_info;

    if (level >= 57) {
        Fail("iLevel < TEST_LEVEL_COUNT");
    }
    if (!LevelBuildInfoByID(level, &level_info)) {
        return false;
    }

    auto replacement = LoadWorld(level, level_info.level_path);
    if (!replacement) return false;
    auto ambient_path = wiz8::path_from_utf8(level_info.level_path);
    ambient_path.replace_extension(".SCF");
    ReadAmbientSoundListFile(*replacement, wiz8::path_to_utf8(ambient_path).c_str());
    Level& current = CurrentLevel();
    const bool reuse_sky =
        current.sky && current.sky->source_path ==
                           wiz8::normalize_asset_path(level_info.sky_path).generic_string();
    auto replacement_sky = reuse_sky ? nullptr : PrepareSkyWorld(level_info.sky_path);
    if (!reuse_sky && !level_info.sky_path.empty() && !replacement_sky) return false;
    if (GetWorld() && !SaveLevelStatus("saves\\CurrentGame.SAV")) return false;
    auto status = ReadLevelStatus(*replacement, wiz8::user_path("saves/CurrentGame.SAV"), level);
    if (!status)
        status = ReadLevelStatus(*replacement, wiz8::asset_path(BuildLevelStatusPath(level)), level,
                                 true);
    if (GetWorld()) LeaveLevel();
    InvalidateRendererTextureCache();
    current.replacePrimary(std::move(replacement));
    if (!reuse_sky) current.replaceSky(std::move(replacement_sky));
    InstallLevelSessionState(*GetWorld(), status.get());
    EnterLevel(level, entrance, restoring_game, status.get());
    return true;
}

void LeaveLevel()
{
    if (!GetWorld()) return;
    FinishLevelDeparture();
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

    ReleaseWorldCursor();
    DisableSky();
    EndCameraSway();
    ResetGameTimeAccumulator();
    CurrentLevel().replacePrimary(nullptr);

    if (g_world_cleanup_flag) {
        RenderFrame();
    }
}

/* Unload the current level and load `level` at `entrance`. When the request
   re-enters the current level through the -1 entrance sentinel, the camera's
   saved yaw/pitch records and position are put back after the load; every
   other request is a plain unload+load. */
bool ReloadLevelPreservingCamera(int level, int entrance)
{
    CameraOrientation saved_orientation;
    glm::vec3 saved_position;
    bool restore = false;

    if (entrance == -1 && level == GetLoadedLevelID()) {
        saved_orientation = cameraOrientation();
        saved_position = cameraLocationOrZero(*GetWorld());
        restore = true;
    }
    if (LoadLevel(level, entrance, false) == 0) {
        return false;
    }
    if (restore) {
        setCameraOrientation(saved_orientation);
        SetWorldScenePosition(GetWorld(), &saved_position);
    }
    return true;
}
