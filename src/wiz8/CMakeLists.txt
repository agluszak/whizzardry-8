target_sources(wiz8_engine PRIVATE
    ../canvas/image.cpp
    ../canvas/sprite.cpp
    ../canvas/Font.cpp
    ../canvas/himage.cpp
    ../canvas/image_assets.cpp
    ../canvas/shading.cpp
    ../platform/input.cpp
    ../platform/window.cpp
    ../platform/game_timer.cpp
    ../platform/GameTimeAccumulator.cpp
    ../platform/IntervalGate.cpp
    ../audio/soundman.cpp
    ../audio/native_audio.cpp
    ../audio/sound3d.cpp
    ../audio/SoundEvent.cpp
    ../audio/AmbientSound.cpp
    ../audio/mouth_gap.cpp
    dialog_code/ButtonAppearance.cpp
    "local_code/PC Item.cpp"
    local_code/FormationAndFacing.cpp
    local_code/ThingEditorShared.cpp
    local_code/TextBuffer.cpp
    local_code/RangeControl.cpp
    local_code/UiLayer.cpp
    local_code/Factions.cpp
    local_code/LoadSaveGame.cpp
    save_codec.cpp
    local_code/Magic.cpp
    "local_code/Health Stamina Mana.cpp"
    local_code/QuoteManager.cpp
    local_code/ConditionsAndEnchantments.cpp
    local_code/GameplayTime.cpp
    local_code/Sight.cpp
    combat/party_movement.cpp
    combat/range.cpp
    "local_code/Gameplay Mods.cpp"
    local_code/GameplayCode.cpp
    combat/fight.cpp
    combat/turn.cpp
    combat/actions.cpp
    combat/sounds.cpp
    local_code/MonsterAI.cpp
    "local_code/NPC Manager.cpp"
    combat/attack.cpp
    combat/hostility.cpp
    "local_code/Magic Effects.cpp"
    local_code/CatalogArtwork.cpp
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
    dialog_code/stMessageDialog.cpp
    dialog_code/DialogBase.cpp
    dialog_code/DialogFactoryDialogs.cpp
    dialog_code/DialogTextEntry.cpp
    dialog_code/StatInfoDialogs.cpp
    dialog_code/CharacterSummaryDialog.cpp
    dialog_code/ProfRaceInfoDialog.cpp
    ../spatial/collision.cpp
    ../spatial/level_spatial_index.cpp
    ../spatial/navigation.cpp
    ../world/automap.cpp
    ../world/level_session.cpp
    ../world/level_loader.cpp
    ../render/resources/mesh_loader.cpp
    ../world/world.cpp
    ../render/effects/particle_system.cpp
    ../render/resources/compiled_mesh.cpp
    ../entities/trigger_actions.cpp
    ../entities/trigger_io.cpp
    ../entities/locks.cpp
    ../spatial/surface_index.cpp
    ../spatial/surface_trace.cpp
    ../world/environment.cpp
    ../entities/cycle_resources.cpp
    ../entities/animation.cpp
    ../entities/animation_mesh.cpp
    ../entities/world_item.cpp
    ../entities/native_object.cpp
    ../entities/missile.cpp
    ../entities/keyframe_track.cpp
    local_code/spell_database.cpp
    ../entities/spell_visual.cpp
    ../entities/prop_collision.cpp
    ../entities/prop.cpp
    ../spatial/navigator.cpp
    "local_code/Party Import.cpp"
    learned_spells.cpp
    packed_bits.cpp
    local_code/Noise.cpp
    local_code/chunk.cpp
    local_code/DifficultyScaling.cpp
    ../entities/cycle_player.cpp
    ../render/effects/ground_shadow.cpp
    "local_code/Gameplay Init.cpp"
    dialog_code/DialogInterface.cpp
    dialog_code/PortraitQuote.cpp
    local_code/ItemManager.cpp
    ../render/resources/material_loader.cpp
    ../render/picking.cpp
    ../entities/monster.cpp
    ../entities/monster_script.cpp
    ../entities/monster_loader.cpp
    ../entities/script.cpp
    local_code/Targeting.cpp
    ../world/update.cpp
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
    ../render/quality.cpp
    startup_render_state.cpp
    startup_subsystems.cpp
    startup_ui_state.cpp
    ../render/frame.cpp
    character_skills.cpp
    loaded_game_data.cpp
    game.cpp
    game_advance.cpp
    combat/entity_signals.cpp
    combat/script_commands.cpp
    fact_state.cpp
    ../world/party_camera.cpp
    ../world/party_motion.cpp
    ../world/selection.cpp
    ../entities/cursor_nodes.cpp
    ../entities/cursor.cpp
    ../render/effects/light_animation.cpp
    version.cpp
    inventory.cpp
    live_party.cpp
    local_code/Search.cpp
    local_code/npc_interaction.cpp
    local_code/npc_dialogue.cpp
    local_code/npc_dialogue_view.cpp
    local_code/npc_speech.cpp
    local_code/npc_story_events.cpp
    "local_code/NPC Scripting.cpp"
    "local_code/NPC Scripting Facts.cpp"
    local_code/formation_state.cpp
    combat/group_attacks.cpp
    local_code/CharGeneration.cpp
    local_screens/AutomapScreen.cpp
    local_screens/ExitScreen.cpp
    local_screens/MGSPortraitCombat.cpp
    local_screens/MGSButtons.cpp
    local_screens/MGSSpellCasting.cpp
    local_screens/PartySelectionScreen.cpp
    local_code/Text_Input.cpp
    local_code/Traps.cpp
    screen_router.cpp
    game_init.cpp
    dialog_code/MonsterInfoDialog.cpp
    dialog_code/AssayDialog.cpp
    dialog_code/DialogTextArea.cpp
    dialog_code/NpcDialog.cpp
    local_code/ButtonWidget.cpp
    dialog_code/SpellInfoDialog.cpp
    dialog_code/SplitItemDialog.cpp
    dialog_code/DialogScrollBar.cpp
    dialog_code/DialogNumericInput.cpp
    ../world/encounters.cpp
    music_playlist.cpp
    pointer.cpp
    targeting_presentation.cpp
    npc_items.cpp
    npc_script_file.cpp
    ../native/application.cpp
    ../native/key_bindings.cpp
    ../native/game_events.cpp
    ../native/movie.cpp
)
target_link_libraries(wiz8_engine PRIVATE PkgConfig::FFMPEG)
target_link_libraries(wiz8_engine PUBLIC wiz8_native_settings
    ZLIB::ZLIB SDL3_image::SDL3_image SDL3_mixer::SDL3_mixer)

# Keep focused integration tests able to discard unused recovered game functions.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(wiz8_engine PRIVATE /Gy /Gw)
else()
    target_compile_options(wiz8_engine PRIVATE -ffunction-sections -fdata-sections)
endif()

if(WIZ8_PRECOMPILE_HEADERS)
    target_precompile_headers(wiz8_engine PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${PROJECT_SOURCE_DIR}/src/wiz8/pch.h>")
endif()
if(WIZ8_UNITY_BUILD)
    set_target_properties(wiz8_engine PROPERTIES
        UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE ${WIZ8_UNITY_BATCH_SIZE})
endif()

# The renderer's optional unity batches share one engine archive with the game.
# Keep unrelated game files independent unless whole-engine unity was requested.
if(WIZ8_RENDER_UNITY_BUILD AND NOT WIZ8_UNITY_BUILD)
    get_target_property(engine_sources wiz8_engine SOURCES)
    foreach(source IN LISTS engine_sources)
        if(NOT source MATCHES "/src/render/")
            set_source_files_properties("${source}" TARGET_DIRECTORY wiz8_engine
                PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
        endif()
    endforeach()
endif()
