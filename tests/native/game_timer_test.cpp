#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "glm/ext/matrix_float3x3.hpp"
#include "save_payload.h"
#include "scoped_game.h"
#include "scoped_game_data.h"
#include "temporary_directory.h"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/camera_motion.h"
#include "wiz8/chunk.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/filesystem.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterGenerator.h"
#include "wiz8/platform/GameTimeAccumulator.h"
#include "wiz8/platform/IntervalGate.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/runtime_test_hooks.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/world.h"

W8RuntimeTestHooks g_runtime_test_hooks;

namespace {
struct TimerFixture {
    ScopedGame game;
    TimerFixture()
    {
        g_runtime_test_hooks = {};
        g_runtime_test_hooks.virtual_clock = true;
        g_runtime_test_hooks.clock_us = 1'000'000;
        CurrentGame().timers_paused = false;
        CurrentGame().status.game_time_days = 0;
        CurrentGame().status.game_time_ms = 0;
    }
    ~TimerFixture() { ResetGameTimeAccumulator(); }
    void advance(unsigned long long micros)
    {
        g_runtime_test_hooks.clock_us += micros;
        const auto elapsed = std::chrono::milliseconds(micros / 1000);
        CurrentGame().time += elapsed;
        if (!CurrentGame().timerClocks().held()) CurrentGame().visual_time += elapsed;
    }
};
}

TEST_CASE_METHOD(TimerFixture, "Timer owners separate world, game and presentation time",
                 "[timers]")
{
    W8GameTimer world(CurrentGame().timerClocks(), 1.0f, TimerClock::World);
    W8GameTimer game(CurrentGame().timerClocks(), 1.0f, TimerClock::Game);
    W8GameTimer ui(CurrentGame().timerClocks(), 1.0f, TimerClock::Presentation);
    // Waiting and drawing alone must not advance any simulation timer.
    g_runtime_test_hooks.clock_us += 500'000;
    REQUIRE(world.GetElapsedSeconds() == 0);
    REQUIRE(game.GetElapsedSeconds() == 0);
    REQUIRE(ui.GetElapsedSeconds() == Catch::Approx(0.5f));
    advance(250'000);
    REQUIRE(world.GetElapsedSeconds() == Catch::Approx(0.25f));
    CurrentGame().timerClocks().pause();
    advance(500'000);
    REQUIRE(world.GetElapsedSeconds() == Catch::Approx(0.25f));
    REQUIRE(world.GetProgress() == 0);
    REQUIRE(game.GetElapsedSeconds() == Catch::Approx(0.75f));
    REQUIRE(ui.GetElapsedSeconds() == Catch::Approx(1.25f));
    CurrentGame().timerClocks().resume();
    advance(250'000);
    REQUIRE(world.GetElapsedSeconds() == Catch::Approx(0.5f));
    REQUIRE(game.GetElapsedSeconds() == Catch::Approx(1));
    W8GameTimer stopped(CurrentGame().timerClocks(), 1.0f, TimerClock::World);
    advance(250'000);
    stopped.Pause();
    advance(5'000'000);
    REQUIRE(stopped.GetProgress() == 0);
    REQUIRE(stopped.GetElapsedSeconds() == Catch::Approx(.25f));
    stopped.Resume();
    advance(250'000);
    REQUIRE(stopped.GetProgress() == Catch::Approx(.5f));
    // Clock stamps may wrap; interval subtraction remains valid.
    CurrentGame().visual_time = std::chrono::milliseconds((1ull << 32) / 10);
    W8GameTimer wrapped(CurrentGame().timerClocks(), 1.0f, TimerClock::World);
    advance(250'000);
    REQUIRE(wrapped.GetElapsedSeconds() == Catch::Approx(0.25f));
}

TEST_CASE_METHOD(TimerFixture, "Game-day time and accelerated frame deltas keep separate clocks",
                 "[timers]")
{
    // Thirty game days put the tick clock past 2^32, so it has wrapped.
    CurrentGame().status.game_time_days = 30;
    W8GameTimer game(CurrentGame().timerClocks(), 2.0f, TimerClock::Calendar);
    game.SetMode(TimerClock::Calendar);
    REQUIRE(game.ReadClock() == static_cast<std::uint32_t>(30ull * 864'000'000));
    advance(250'000);
    REQUIRE(game.GetProgress() == 0);
    CurrentGame().status.game_time_ms = 250;
    REQUIRE(game.GetProgress() == Catch::Approx(0.125f));

    // The frame delta follows the elapsed time the step passes, not the clock.
    ResetGameTimeAccumulator();
    auto& accumulator = GameTimeAccumulator();
    accumulator.SetTimeScale(0.5f);
    REQUIRE(accumulator.Update(std::chrono::milliseconds{25}, CurrentGame().timerClocks().held()) ==
            Catch::Approx(0.5f));
    CurrentGame().timerClocks().pause();
    REQUIRE(accumulator.Update(std::chrono::seconds{1}, CurrentGame().timerClocks().held()) == 0);
    CurrentGame().timerClocks().resume();
    REQUIRE(accumulator.Update(std::chrono::milliseconds{25}, CurrentGame().timerClocks().held()) ==
            Catch::Approx(0.5f));
    accumulator.ResetTimeScale();
    REQUIRE(accumulator.Update(std::chrono::milliseconds{25}, CurrentGame().timerClocks().held()) ==
            Catch::Approx(0.25f));
    REQUIRE(accumulator.Update(std::chrono::seconds{1}, CurrentGame().timerClocks().held()) ==
            Catch::Approx(2.0f)); // One step moves at most 200ms.
    REQUIRE(accumulator.GetElapsed() == Catch::Approx(3.25f));
}

TEST_CASE_METHOD(TimerFixture, "Interval gates retain catch-up, one-shot and saved progress",
                 "[timers][save]")
{
    W8IntervalGate repeating(CurrentGame().timerClocks(), 0.1f, TimerClock::World, false),
        once(CurrentGame().timerClocks(), 0.1f, TimerClock::World, true);
    advance(350'000);
    REQUIRE(repeating.PollElapsedIntervals() == 3);
    REQUIRE(repeating.PollElapsedIntervals() == 0);
    REQUIRE(once.PollFinished());
    once.Arm();
    advance(50'000);
    REQUIRE(repeating.PollElapsedIntervals() == 1);
    REQUIRE_FALSE(once.PollFinished());
    advance(50'000);
    REQUIRE(once.PollFinished());

    ScopedTemporaryDirectory directory{"wiz8-timers"};
    const auto path = directory.path / "progress.bin";
    W8IntervalGate saved(CurrentGame().timerClocks(), 2.0f, TimerClock::World, false);
    advance(500'000);
    saved.SetDurationScale(0.5f);
    {
        auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
        REQUIRE(saved.SaveState(*file));
    }
    {
        auto file = wiz8::open_file(path);
        REQUIRE(file->size() == sizeof(float) * 2);
        std::array<float, 2> values{};
        file->read_exact(values.data(), sizeof(values));
        REQUIRE(values[0] == Catch::Approx(0.25f));
        REQUIRE(values[1] == Catch::Approx(0.5f));
    }
    CurrentGame().visual_time = std::chrono::milliseconds::zero();
    W8IntervalGate loaded(CurrentGame().timerClocks(), 2.0f, TimerClock::World, false);
    {
        auto file = wiz8::open_file(path);
        REQUIRE(ReadSavePayload(*file, [&](W8Chunk& chunk) { return loaded.LoadState(chunk); }));
    }
    REQUIRE(loaded.GetProgress() == Catch::Approx(0.25f));
    advance(250'000);
    REQUIRE(loaded.GetProgress() == Catch::Approx(0.5f));

    // One-shot records store the time left, not the elapsed part. Completed gates
    // load as completed instead of dividing by a zero-duration interval.
    W8IntervalGate one_shot(CurrentGame().timerClocks(), 2.0f, TimerClock::World, true);
    advance(500'000);
    {
        auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
        REQUIRE(one_shot.SaveState(*file));
    }
    CurrentGame().visual_time = std::chrono::milliseconds::zero();
    W8IntervalGate restored(CurrentGame().timerClocks(), 1.0f, TimerClock::World, true);
    {
        auto file = wiz8::open_file(path);
        REQUIRE(file->size() == sizeof(float));
        REQUIRE(ReadSavePayload(*file, [&](W8Chunk& chunk) { return restored.LoadState(chunk); }));
    }
    advance(1'499'000);
    REQUIRE_FALSE(restored.PollFinished());
    advance(1'000);
    REQUIRE(restored.PollFinished());
    {
        auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
        REQUIRE(restored.SaveState(*file));
    }
    W8IntervalGate finished(CurrentGame().timerClocks(), 1.0f, TimerClock::World, true);
    {
        auto file = wiz8::open_file(path);
        REQUIRE(ReadSavePayload(*file, [&](W8Chunk& chunk) { return finished.LoadState(chunk); }));
    }
    REQUIRE(finished.PollFinished());
}

TEST_CASE_METHOD(TimerFixture, "Generator intervals catch up on step time and save their progress",
                 "[timers][save]")
{
    using std::chrono::milliseconds;
    GeneratorInterval interval;
    interval.setSeconds(0.1f);
    REQUIRE(interval.advance(milliseconds{350}) == 3);
    REQUIRE(interval.advance(milliseconds{40}) == 0);
    REQUIRE(interval.progress() == Catch::Approx(0.9f));

    // A new scale keeps the progress through the interval, as the retail timer did.
    interval.setSeconds(2.0f);
    interval.arm();
    interval.advance(milliseconds{500});
    interval.setScale(0.5f, false);
    REQUIRE(interval.progress() == Catch::Approx(0.25f));

    // A save stores the progress and the scale.
    REQUIRE(interval.scale() == 0.5f);
    GeneratorInterval loaded;
    loaded.setSeconds(2.0f);
    loaded.restore(interval.progress(), interval.scale());
    REQUIRE(loaded.progress() == Catch::Approx(0.25f));
    REQUIRE(loaded.advance(milliseconds{750}) == 1);
}

TEST_CASE_METHOD(TimerFixture, "A game steps deterministically on the times it is given",
                 "[timers][step]")
{
    using std::chrono::milliseconds;
    // An empty level in view: each step runs the whole simulation over nothing.
    auto& game = CurrentGame();
    game.level.replacePrimary(
        std::make_unique<World>(-1, "", game.timerClocks(), &game.camera_shakes));
    game.level.primary->camera = &game.level.primary->scene->camera;
    const auto auto_save = std::exchange(g_settings.auto_save, 0);
    const auto step = [&](int ms) { game.advance(milliseconds{ms}, {}); };

    step(16);
    step(17);
    step(400);
    REQUIRE(game.time == milliseconds{433});
    REQUIRE(game.visual_time == milliseconds{433});
    REQUIRE(GameClockMilliseconds() == 433);
    REQUIRE(game.autosave_due == Game::kAutosaveInterval - milliseconds{433});

    // The monotonic clock moving between steps changes nothing.
    g_runtime_test_hooks.clock_us += 10'000'000;
    REQUIRE(game.time == milliseconds{433});

    // Held shared timers stop the countdowns that stand in for them, not the step time.
    CurrentGame().timerClocks().pause();
    step(1000);
    CurrentGame().timerClocks().resume();
    REQUIRE(game.time == milliseconds{1433});
    REQUIRE(game.visual_time == milliseconds{433});
    REQUIRE(game.autosave_due == Game::kAutosaveInterval - milliseconds{433});

    g_settings.auto_save = auto_save;
}

TEST_CASE_METHOD(TimerFixture, "Texture animations retain their clock after 32-bit tick rollover",
                 "[timers]")
{
    const std::chrono::milliseconds epoch{(std::uint64_t{1} << 32) + 250};
    TexturePlayback playback;
    playback.textures.resize(3);
    playback.frame_rate = 1.0f;
    Texture animation(std::move(playback));
    animation.UpdateFrame(epoch);
    animation.UpdateFrame(epoch + std::chrono::milliseconds{1000});
    REQUIRE(animation.playback->frame == 1);
    animation.SetFrame(0);
    animation.UpdateFrame(epoch + std::chrono::milliseconds{2000});
    REQUIRE(animation.playback->frame == 1);
    // An owner cannot accidentally advance a world texture on UI time.
    animation.SetFrame(0);
    animation.UpdateFrame(epoch + std::chrono::milliseconds{3000}, TextureClock::Presentation);
    REQUIRE(animation.playback->frame == 0);
    animation.setAnimationClock(TextureClock::Presentation);
    animation.UpdateFrame(epoch + std::chrono::milliseconds{3000}, TextureClock::Presentation);
    animation.UpdateFrame(epoch + std::chrono::milliseconds{4000}, TextureClock::Presentation);
    REQUIRE(animation.playback->frame == 1);
}

TEST_CASE_METHOD(TimerFixture, "World paths, item radar and ambient replay wait for world steps",
                 "[timers][world]")
{
    KeyframeTrack path(std::vector<KeyframePoint>{{{0, 0, 0}}, {{10, 0, 0}}});
    path.speed = 1;
    PathAIEnableTimedMode(&path);
    PathAIResetTick(&path);
    WorldItemEntity item;
    item.LightRadarBlip();
    W8AmbientTrack ambient;
    ambient.time_min = ambient.time_max = 1000;
    ambient.Reschedule();
    g_runtime_test_hooks.clock_us += 20'000'000;
    PathAITick(&path, 1);
    REQUIRE(path.position == 0);
    REQUIRE(item.IsRadarBlipLit());
    REQUIRE_FALSE(ambient.Due());
    advance(500'000);
    REQUIRE(PathAITick(&path, 1));
    REQUIRE(path.position == Catch::Approx(.25f));
    CurrentGame().timerClocks().pause();
    advance(20'000'000);
    PathAITick(&path, 1);
    REQUIRE(path.position == Catch::Approx(.25f));
    REQUIRE(item.IsRadarBlipLit());
    REQUIRE_FALSE(ambient.Due());
    CurrentGame().timerClocks().resume();
    advance(500'000);
    REQUIRE(ambient.Due());
    advance(9'000'000);
    REQUIRE_FALSE(item.IsRadarBlipLit());
}

TEST_CASE("Spell practice queries are pure and attempts restart even a running cooldown",
          "[timers]")
{
    ScopedGame game;
    GameData records;
    records.spells.resize(W8_SPELL_SET_PORTAL + 1);
    ScopedGameData data{std::move(records)};
    W8CombatSlot target{};
    REQUIRE(AttemptSpellPractice(W8_SPELL_LIGHT, &target, 1, 0));
    const auto deadlines = game.game.spell_cooldowns;
    REQUIRE_FALSE(SpellAffectedTarget(W8_SPELL_LIGHT, &target, 1, 179999));
    REQUIRE(SpellAffectedTarget(W8_SPELL_LIGHT, &target, 1, 180000));
    REQUIRE(game.game.spell_cooldowns == deadlines);
    REQUIRE_FALSE(AttemptSpellPractice(W8_SPELL_LIGHT, &target, 1, 179999));
    REQUIRE_FALSE(SpellAffectedTarget(W8_SPELL_LIGHT, &target, 1, 180000));
    REQUIRE(AttemptSpellPractice(W8_SPELL_LIGHT, &target, 1, 359999));

    // An ineffective Remove Curse still consumes practice eligibility.
    target.iType = W8_TARGET_KIND_CHARACTER;
    REQUIRE_FALSE(AttemptSpellPractice(W8_SPELL_REMOVE_CURSE, &target, 1, 0));
    game.game.party.characters[0].uiCondition[W8_CONDITION_HEXED] = 1;
    REQUIRE_FALSE(SpellAffectedTarget(W8_SPELL_REMOVE_CURSE, &target, 1, 179999));
    REQUIRE(SpellAffectedTarget(W8_SPELL_REMOVE_CURSE, &target, 1, 180000));
    const auto before_combat = game.game.spell_cooldowns;
    game.game.combat.emplace();
    REQUIRE(AttemptSpellPractice(W8_SPELL_REMOVE_CURSE, &target, 1, 1));
    REQUIRE(game.game.spell_cooldowns == before_combat);
}

TEST_CASE_METHOD(TimerFixture, "Candidate timers keep their game clocks across session replacement",
                 "[timers][lifetime]")
{
    using namespace std::chrono_literals;
    auto& outgoing = game.game;
    outgoing.visual_time = 10s;
    outgoing.timerClocks().pause();
    Game candidate(outgoing.data);
    candidate.visual_time = 2s;
    candidate.time = 3s;
    World world(-1, "", candidate.timerClocks(), &candidate.camera_shakes);
    Trigger trigger(&world);
    auto& timer = world.addTimedAction(&trigger).timer;
    W8GameTimer step(candidate.timerClocks(), 1.0f, TimerClock::Game);
    W8GameTimer calendar(candidate.timerClocks(), 1.0f, TimerClock::Calendar);

    outgoing.visual_time += 1s;
    REQUIRE(timer.GetProgress() == 0);
    candidate.visual_time += 250ms;
    REQUIRE(timer.GetProgress() == Catch::Approx(.25f));
    candidate.timerClocks().pause();
    REQUIRE(timer.GetProgress() == 0); // Holds report zero, not the previous fraction.
    candidate.status.game_time_ms += 250;
    REQUIRE(calendar.GetProgress() == 0);
    candidate.time += 250ms;
    REQUIRE(step.GetProgress() == Catch::Approx(.25f));
    candidate.timerClocks().resume();
    REQUIRE(calendar.GetProgress() == Catch::Approx(.25f));

    auto& replacement = game.session.begin();
    REQUIRE_FALSE(replacement.timerClocks().held());
    replacement.visual_time = 60s;
    candidate.visual_time += 250ms;
    REQUIRE(timer.GetProgress() == Catch::Approx(.5f));
    REQUIRE(calendar.GetProgress() == Catch::Approx(.25f));
    W8GameTimer disconnected({}, 1.0f, TimerClock::Calendar);
    REQUIRE(disconnected.ReadClock() == 0);
}

TEST_CASE_METHOD(TimerFixture, "Unkeyed shakes survive level changes but not their game",
                 "[timers][lifetime][shake]")
{
    using namespace std::chrono_literals;
    auto& owner = game.game;
    const auto world = [&] {
        return std::make_unique<World>(-1, "", owner.timerClocks(), &owner.camera_shakes);
    };
    owner.level.replacePrimary(world());
    int completed = 0;
    ShakeDefinition shake(1.0f, false, 1.0f, 0, nullptr);
    shake.completion_callback = [&] { ++completed; };
    owner.camera_shakes.Start(shake);
    owner.level.replacePrimary(world());
    owner.visual_time += 1100ms;
    owner.camera_shakes.Update({0, 0, 0});
    REQUIRE(completed == 1);

    owner.camera_shakes.Start(shake);
    auto& replacement = game.session.begin();
    replacement.visual_time += 2s;
    REQUIRE(replacement.camera_shakes.Update({0, 0, 0}) == 0);
    REQUIRE(completed == 1); // A discarded game's continuation cannot run in its replacement.
}

TEST_CASE_METHOD(TimerFixture, "First camera motion resumes its clock only once",
                 "[timers][camera]")
{
    ResetGameTimeAccumulator(); // Zero movement delta: this test needs no collision geometry.
    auto clocks = game.game.timerClocks();
    PartyCamera camera(clocks);
    LevelSpatialIndex geometry;
    clocks.pause();
    REQUIRE_FALSE(camera.move(geometry, 0, glm::mat3(1)).moved);
    REQUIRE(camera.motion_started);
    REQUIRE_FALSE(clocks.held());
    clocks.pause();
    REQUIRE_FALSE(camera.move(geometry, W8_CAMERA_MOTION_FORWARD, glm::mat3(1)).moved);
    REQUIRE(clocks.held());
}
