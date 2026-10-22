#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "save_payload.h"
#include "scoped_game.h"
#include "scoped_game_data.h"
#include "temporary_directory.h"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/chunk.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/keyframe_track.h"
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
#include "wiz8/render/texture.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/runtime_test_hooks.h"
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
        g_shared_timer_paused = false;
        g_level_motion_resume_pending = false;
        CurrentGame().status.game_time_days = 0;
        CurrentGame().status.game_time_ms = 0;
    }
    ~TimerFixture() { ResetGameTimeAccumulator(); }
    void advance(unsigned long long micros)
    {
        g_runtime_test_hooks.clock_us += micros;
        const auto elapsed = std::chrono::milliseconds(micros / 1000);
        CurrentGame().time += elapsed;
        if (!SharedGameTimersHeld()) CurrentGame().visual_time += elapsed;
    }
};
}

TEST_CASE_METHOD(TimerFixture, "Timer owners separate world, game and presentation time",
                 "[timers]")
{
    W8GameTimer world(1.0f, TimerClock::World);
    W8GameTimer game(1.0f, TimerClock::Game);
    W8GameTimer ui(1.0f, TimerClock::Presentation);
    // Waiting and drawing alone must not advance any simulation timer.
    g_runtime_test_hooks.clock_us += 500'000;
    REQUIRE(world.GetElapsedSeconds() == 0);
    REQUIRE(game.GetElapsedSeconds() == 0);
    REQUIRE(ui.GetElapsedSeconds() == Catch::Approx(0.5f));
    advance(250'000);
    REQUIRE(world.GetElapsedSeconds() == Catch::Approx(0.25f));
    PauseSharedGameTimers();
    advance(500'000);
    REQUIRE(world.GetElapsedSeconds() == Catch::Approx(0.25f));
    REQUIRE(world.GetProgress() == 0);
    REQUIRE(game.GetElapsedSeconds() == Catch::Approx(0.75f));
    REQUIRE(ui.GetElapsedSeconds() == Catch::Approx(1.25f));
    ResumeSharedGameTimers();
    advance(250'000);
    REQUIRE(world.GetElapsedSeconds() == Catch::Approx(0.5f));
    REQUIRE(game.GetElapsedSeconds() == Catch::Approx(1));
    W8GameTimer stopped(1.0f, TimerClock::World);
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
    W8GameTimer wrapped(1.0f, TimerClock::World);
    advance(250'000);
    REQUIRE(wrapped.GetElapsedSeconds() == Catch::Approx(0.25f));
}

TEST_CASE_METHOD(TimerFixture, "Game-day time and accelerated frame deltas keep separate clocks",
                 "[timers]")
{
    // Thirty game days put the tick clock past 2^32, so it has wrapped.
    CurrentGame().status.game_time_days = 30;
    W8GameTimer game(2.0f, TimerClock::Calendar);
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
    REQUIRE(accumulator.Update(std::chrono::milliseconds{25}) == Catch::Approx(0.5f));
    PauseSharedGameTimers();
    REQUIRE(accumulator.Update(std::chrono::seconds{1}) == 0);
    ResumeSharedGameTimers();
    REQUIRE(accumulator.Update(std::chrono::milliseconds{25}) == Catch::Approx(0.5f));
    accumulator.ResetTimeScale();
    REQUIRE(accumulator.Update(std::chrono::milliseconds{25}) == Catch::Approx(0.25f));
    REQUIRE(accumulator.Update(std::chrono::seconds{1}) ==
            Catch::Approx(2.0f)); // One step moves at most 200ms.
    REQUIRE(accumulator.GetElapsed() == Catch::Approx(3.25f));
}

TEST_CASE_METHOD(TimerFixture, "Interval gates retain catch-up, one-shot and saved progress",
                 "[timers][save]")
{
    W8IntervalGate repeating(0.1f, TimerClock::World, false), once(0.1f, TimerClock::World, true);
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
    W8IntervalGate saved(2.0f, TimerClock::World, false);
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
    W8IntervalGate loaded(2.0f, TimerClock::World, false);
    {
        auto file = wiz8::open_file(path);
        REQUIRE(ReadSavePayload(*file, [&](W8Chunk& chunk) { return loaded.LoadState(chunk); }));
    }
    REQUIRE(loaded.GetProgress() == Catch::Approx(0.25f));
    advance(250'000);
    REQUIRE(loaded.GetProgress() == Catch::Approx(0.5f));

    // Saved floats reach integer tick conversions: a scale that leaves no countable
    // duration is rejected, and progress that is not a number restarts the interval.
    const auto load_pair = [&](float progress, float scale) {
        {
            auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
            file->write(&progress, sizeof(progress));
            file->write(&scale, sizeof(scale));
        }
        auto file = wiz8::open_file(path);
        return ReadSavePayload(*file, [&](W8Chunk& chunk) { return loaded.LoadState(chunk); });
    };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(load_pair(0.25f, 0.0f));
    REQUIRE_FALSE(load_pair(0.25f, nan));
    REQUIRE_FALSE(load_pair(0.25f, std::numeric_limits<float>::infinity()));
    REQUIRE(load_pair(nan, 1.0f));
    REQUIRE(loaded.GetProgress() == 0.0f);

    // One-shot records store the time left, not the elapsed part. Completed gates
    // load as completed instead of dividing by a zero-duration interval.
    W8IntervalGate one_shot(2.0f, TimerClock::World, true);
    advance(500'000);
    {
        auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
        REQUIRE(one_shot.SaveState(*file));
    }
    CurrentGame().visual_time = std::chrono::milliseconds::zero();
    W8IntervalGate restored(1.0f, TimerClock::World, true);
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
    W8IntervalGate finished(1.0f, TimerClock::World, true);
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
    interval.setScale(0.5f);
    REQUIRE(interval.progress() == Catch::Approx(0.25f));

    ScopedTemporaryDirectory directory{"wiz8-generator-interval"};
    const auto path = directory.path / "progress.bin";
    {
        auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
        interval.save(*file);
    }
    {
        auto file = wiz8::open_file(path);
        REQUIRE(file->size() == sizeof(float) * 2);
        std::array<float, 2> values{};
        file->read_exact(values.data(), sizeof(values));
        REQUIRE(values[0] == Catch::Approx(0.25f));
        REQUIRE(values[1] == Catch::Approx(0.5f));
    }
    GeneratorInterval loaded;
    loaded.setSeconds(2.0f);
    {
        auto file = wiz8::open_file(path);
        REQUIRE(ReadSavePayload(*file, [&](W8Chunk& chunk) {
            loaded.load(chunk);
            return true;
        }));
    }
    REQUIRE(loaded.progress() == Catch::Approx(0.25f));
    REQUIRE(loaded.advance(milliseconds{750}) == 1);
}

TEST_CASE_METHOD(TimerFixture, "A game steps deterministically on the times it is given",
                 "[timers][step]")
{
    using std::chrono::milliseconds;
    // An empty level in view: each step runs the whole simulation over nothing.
    auto& game = CurrentGame();
    game.level.replacePrimary(std::make_unique<World>());
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
    PauseSharedGameTimers();
    step(1000);
    ResumeSharedGameTimers();
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
    KeyframeTrack path;
    PathAIAddPoint(path, {0, 0, 0});
    PathAIAddPoint(path, {10, 0, 0});
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
    PauseSharedGameTimers();
    advance(20'000'000);
    PathAITick(&path, 1);
    REQUIRE(path.position == Catch::Approx(.25f));
    REQUIRE(item.IsRadarBlipLit());
    REQUIRE_FALSE(ambient.Due());
    ResumeSharedGameTimers();
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
