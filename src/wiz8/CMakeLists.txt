set_source_files_properties(engine_code/gap.c PROPERTIES LANGUAGE CXX)

add_library(WIZ8_GAME_CORE STATIC
    ../sgp/vobject.cpp
    ../sgp/timer.cpp
    ../sgp/input.cpp
    ../sgp/native/input_events.cpp
    ../sgp/soundman.cpp
    ../sgp/native/audio.cpp
    ../sgp/native/surfaces.cpp
    ../sgp/native/video.cpp
    ../sgp/vsurface.cpp
    ../sgp/Font.cpp
    ../sgp/mousesystem.cpp
    "../sgp/Button System.cpp"
    ../sgp/line.cpp
    ../sgp/himage.cpp
    ../sgp/vobject_blitters.cpp
    ../sgp/shading.cpp
    ../sgp/Random.cpp
    ../sgp/English.cpp
    "local_code/PC Item.cpp"
    local_code/FormationAndFacing.cpp
    local_code/ThingEditorShared.cpp
    local_code/Controls.cpp
    local_code/Factions.cpp
    local_code/LoadSaveGame.cpp
    local_code/Magic.cpp
    "local_code/Health Stamina Mana.cpp"
    local_code/QuoteManager.cpp
    local_code/ConditionsAndEnchantments.cpp
    local_code/GameplayTime.cpp
    local_code/Sight.cpp
    "local_code/Combat Party Movement.cpp"
    "local_code/Combat Range.cpp"
    "local_code/Gameplay Mods.cpp"
    local_code/GameplayCode.cpp
    local_code/Combat.cpp
    "local_code/Combat Sound.cpp"
    local_code/MonsterAI.cpp
    "local_code/NPC Manager.cpp"
    "local_code/Combat Attack.cpp"
    "local_code/Combat Hostility.cpp"
    "local_code/Magic Effects.cpp"
    local_code/VideoObjectManager.cpp
    "local_screens/IntroScreen.cpp"
    "local_screens/MainMenuScreen.cpp"
    local_code/InputMapper.cpp
    local_screens/MGSKeyboard.cpp
    local_screens/OptionsScreen.cpp
    local_screens/PleaseWaitScreen.cpp
    local_screens/Screens.cpp
    local_screens/ReviewCharacterScreen.cpp
    local_screens/MGSSpellIcons.cpp
    local_screens/RCSCommon.cpp
    local_screens/MGSFormation.cpp
    local_screens/MGSRadarMap.cpp
    local_screens/RCSItemsPage.cpp
    local_screens/RCSStatsPage.cpp
    local_screens/MGSTextBox.cpp
    local_screens/MainGameScreen.cpp
    local_screens/combat_portrait_image.cpp
    local_screens/formation_panel_refresh.cpp
    local_screens/NPCInteractionSubscreen.cpp
    local_screens/CharacterScreen.cpp
    local_screens/CharacterPages.cpp
    local_screens/CGSSpellsPage.cpp
    local_screens/CGSStatsPage.cpp
    local_screens/JournalScreen.cpp
    local_screens/CreditsScreen.cpp
    local_screens/MGSPortraits.cpp
    local_screens/MGSUseItemSelect.cpp
    local_screens/MGSPartyMovement.cpp
    local_code/MonsterManager.cpp
    local_code/monster_fatigue.cpp
    local_code/MonsterGroup.cpp
    local_code/UtilityFunctions.cpp
    local_code/Strings.cpp
    local_code/Configuration.cpp
    local_code/ButtonSound.cpp
    dialog_code/NotificationDialog.cpp
    dialog_code/MessageDialogBase.cpp
    dialog_code/stMessageDialog.cpp
    dialog_code/DialogBase.cpp
    dialog_code/DialogFactoryDialogs.cpp
    dialog_code/stListBox.cpp
    dialog_code/DialogTextEntry.cpp
    dialog_code/StatInfoDialogs.cpp
    dialog_code/CharacterSummaryDialog.cpp
    dialog_code/ProfRaceInfoDialog.cpp
    engine_code/GameTimeAccumulator.cpp
    engine_code/IntervalGate.cpp
    engine_code/BitArray.cpp
    engine_code/GameData.cpp
    engine_code/GDFileIO.cpp
    engine_code/Octree.cpp
    engine_code/OctPath.cpp
    engine_code/OctPreTree.cpp
    engine_code/Levels.cpp
    engine_code/ReadLevel.cpp
    engine_code/ReadMesh.cpp
    engine_code/quad.cpp
    engine_code/3dapi.cpp
    engine_code/stMeshModel.cpp
    engine_code/stModelInstance.cpp
    engine_code/stTextureAnim.cpp
    engine_code/stTextureFile.cpp
    engine_code/stParticle.cpp
    engine_code/OctSubMesh.cpp
    engine_code/Trigger.cpp
    engine_code/OctBuildTree.cpp
    engine_code/Environment.cpp
    engine_code/AmbientSound.cpp
    engine_code/AnimObj.cpp
    engine_code/AniMesh.cpp
    engine_code/Item.cpp
    engine_code/Missile.cpp
    engine_code/PathAI.cpp
    engine_code/Spells.cpp
    engine_code/OctBuildPreTree.cpp
    engine_code/SoundEvent.cpp
    engine_code/AnimRep.cpp
    engine_code/GDProp.cpp
    engine_code/Prop.cpp
    quaternion.cpp
    engine_code/Navigator.cpp
    engine_code/GrObject.cpp
    "local_code/Party Import.cpp"
    learned_spells.cpp
    local_code/Noise.cpp
    local_code/RegionManager.cpp
    local_code/chunk.cpp
    local_code/DifficultyScaling.cpp
    "local_code/Game Difficulty.cpp"
    engine_code/GrCycle.cpp
    engine_code/UpdateMesh.cpp
    engine_code/stGroundShadow.cpp
    local_code/GameplayDatabase.cpp
    "local_code/Gameplay Init.cpp"
    dialog_code/DialogInterface.cpp
    dialog_code/PortraitQuote.cpp
    3d_code/IList.cpp
    local_code/ItemManager.cpp
    engine_code/game_timer.cpp
    engine_code/materials.cpp
    engine_code/PolyPick.cpp
    engine_code/Monster.cpp
    engine_code/OctPrePath.cpp
    engine_code/stScript.cpp
    engine_code/LevelFile.cpp
    local_code/Targeting.cpp
    3d_code/PList.cpp
    engine_code/3d.cpp
    engine_code/gap.c
    level_specific_code/MasterFunctionList.cpp
    level_specific_code/Monastery1.cpp
    level_specific_code/Monastery2.cpp
    level_specific_code/Ascension.cpp
    level_specific_code/MtGigasOuter.cpp
    level_specific_code/MtGigasTop.cpp
    level_specific_code/Trynnie1.cpp
    "level_specific_code/Cosmic Circle.cpp"
    level_specific_code/Trynnie2.cpp
    level_specific_code/Swamp.cpp
    level_specific_code/SeaCaves.cpp
    level_specific_code/Rift1.cpp
    level_specific_code/Camp.cpp
    level_specific_code/RapaxMainFloor.cpp
    level_specific_code/RapaxUpperFloor.cpp
    level_specific_code/MtGigas2.cpp
    level_specific_code/MtGigas1.cpp
    level_specific_code/MartensBluff2.cpp
    level_specific_code/MartensBluff1.cpp
    level_specific_code/SavantTower.cpp
    level_specific_code/ConnectiveTissue.cpp
    level_specific_code/Arnika.cpp
    engine_code/Quality.cpp
    startup_render_state.cpp
    startup_world.cpp
    startup_subsystems.cpp
    startup_ui_state.cpp
    sound_man.cpp
    surface2d.cpp
    engine_code/Video2.cpp
    character_skills.cpp
    fact_state.cpp
    engine_code/MonsterLight.cpp
    engine_code/GDCamera.cpp
    engine_code/camera_transition_state.cpp
    engine_code/world_selection.cpp
    engine_code/stCube.cpp
    engine_code/Camera.cpp
    engine_code/Cursor3d.cpp
    engine_code/stLight.cpp
    engine_code/OctRegionPolygon.cpp
    version.cpp
    local_code/party_encumbrance.cpp
    local_code/Search.cpp
    local_code/npc_interaction.cpp
    "local_code/NPC Scripting.cpp"
    "local_code/NPC Scripting Facts.cpp"
    local_code/formation_state.cpp
    local_code/GroupAttacks.cpp
    local_code/CharGeneration.cpp
    local_screens/mipe.cpp
    local_screens/mipeEdit.cpp
    local_screens/AutomapScreen.cpp
    local_screens/screen12.cpp
    local_screens/MGSPortraitCombat.cpp
    local_screens/MGSButtons.cpp
    local_screens/MGSSpellCasting.cpp
    local_screens/PartySelectionScreen.cpp
    local_code/Text_Input.cpp
    local_code/Traps.cpp
    local_code/Gameloop.cpp
    game_init.cpp
    integer_constants.cpp
    dialog_code/MonsterInfoDialog.cpp
    dialog_code/AssayDialog.cpp
    dialog_code/DialogTextArea.cpp
    dialog_code/NpcDialog.cpp
    dialog_code/stButton.cpp
    dialog_code/SpellInfoDialog.cpp
    dialog_code/SplitItemDialog.cpp
    dialog_code/DialogScrollBar.cpp
    dialog_code/DialogNumericInput.cpp
    engine_code/MonGen.cpp
    music_playlist.cpp
    npc_items.cpp
    npc_script_file.cpp
    engine_code/trim_string.cpp
    ../native/application.cpp
    ../native/movie.cpp
    ../native/bink_video.cpp
)
target_link_libraries(WIZ8_GAME_CORE PRIVATE PkgConfig::FFMPEG)
target_link_libraries(WIZ8_GAME_CORE PUBLIC wiz8_native_settings SURRENDER
    ZLIB::ZLIB SDL3_image::SDL3_image SDL3_mixer::SDL3_mixer)

# Keep focused integration tests able to discard unused recovered game functions.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(WIZ8_GAME_CORE PRIVATE /Gy /Gw)
else()
    target_compile_options(WIZ8_GAME_CORE PRIVATE -ffunction-sections -fdata-sections)
endif()

target_include_directories(WIZ8_GAME_CORE PUBLIC "${PROJECT_SOURCE_DIR}/src/sgp")
