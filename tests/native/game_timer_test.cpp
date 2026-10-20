#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "save_payload.h"
#include "scoped_game.h"
#include "temporary_directory.h"
#include "wiz8/chunk.h"
#include "wiz8/filesystem.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/MonsterGenerator.h"
#include "wiz8/platform/GameTimeAccumulator.h"
#include "wiz8/platform/IntervalGate.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/world_scene.h"
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
        g_shared_timer_pause_base = 0;
        g_shared_timer_pause_time = 0;
        g_shared_timer_paused = false;
        g_shared_timer_flag0 = false;
        g_level_motion_resume_pending = false;
        CurrentGame().status.game_time_days = 0;
        CurrentGame().status.game_time_ms = 0;
    }
    ~TimerFixture() { ResetGameTimeAccumulator(); }
    void advance(unsigned long long micros) { g_runtime_test_hooks.clock_us += micros; }
};
}

TEST_CASE_METHOD(TimerFixture, "Clock lifetime is independent of timers and gameplay pause",
                 "[timers]")
{
    REQUIRE(GameClockTicks() == 10000);
    PauseSharedGameTimers();
    advance(500'000);
    {
        W8GameTimer gameplay(1.0f, false), real(1.0f, true);
        REQUIRE(g_shared_timer_paused);
        REQUIRE(gameplay.ReadClock() == 10000);
        REQUIRE(real.ReadClock() == 15000);
        ResumeSharedGameTimers();
        advance(250'000);
        REQUIRE(gameplay.GetElapsedSeconds() == Catch::Approx(0.25f));
        REQUIRE(real.GetElapsedSeconds() == Catch::Approx(0.25f));
        PauseSharedGameTimers();
        advance(500'000);
        REQUIRE(gameplay.GetElapsedSeconds() == Catch::Approx(0.25f));
        REQUIRE(real.GetElapsedSeconds() == Catch::Approx(0.75f));
        ResumeSharedGameTimers();
        advance(250'000);
        REQUIRE(gameplay.GetElapsedSeconds() == Catch::Approx(0.5f));
        REQUIRE(real.GetElapsedSeconds() == Catch::Approx(1.0f));
    }
    // The world's animation clock is the game's step time, not the monotonic clock.
    REQUIRE(GameClockMilliseconds() == 0);
    W8GameTimer later;
    REQUIRE(later.ReadClock() == 15000);
}

TEST_CASE_METHOD(TimerFixture, "Game-day time and accelerated frame deltas keep separate clocks",
                 "[timers]")
{
    // Thirty game days put the tick clock past 2^32, so it has wrapped.
    CurrentGame().status.game_time_days = 30;
    W8GameTimer game(2.0f, false);
    game.SetMode(W8_TIMER_CLOCK_GAME);
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
    W8IntervalGate repeating(0.1f, false, false), once(0.1f, false, true);
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
    W8IntervalGate saved(2.0f, false, false);
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
    W8IntervalGate loaded(2.0f, false, false);
    {
        auto file = wiz8::open_file(path);
        REQUIRE(ReadSavePayload(*file, [&](W8Chunk& chunk) { return loaded.LoadState(chunk); }));
    }
    REQUIRE(loaded.GetProgress() == Catch::Approx(0.25f));
    advance(250'000);
    REQUIRE(loaded.GetProgress() == Catch::Approx(0.5f));
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
    REQUIRE(GameClockMilliseconds() == 433);
    REQUIRE(game.autosave_due == Game::kAutosaveInterval - milliseconds{433});

    // The monotonic clock moving between steps changes nothing.
    advance(10'000'000);
    REQUIRE(game.time == milliseconds{433});

    // Held shared timers stop the countdowns that stand in for them, not the step time.
    PauseSharedGameTimers();
    step(1000);
    ResumeSharedGameTimers();
    REQUIRE(game.time == milliseconds{1433});
    REQUIRE(game.autosave_due == Game::kAutosaveInterval - milliseconds{433});

    g_settings.auto_save = auto_save;
}

TEST_CASE_METHOD(TimerFixture, "Texture animations retain their clock after 32-bit tick rollover",
                 "[timers]")
{
    g_runtime_test_hooks.clock_us = ((std::uint64_t{1} << 32) + 250) * 1000;
    TexturePlayback playback;
    playback.textures.resize(3);
    playback.frame_rate = 1.0f;
    Texture animation(std::move(playback));
    advance(1'000'000);
    animation.UpdateFrame();
    REQUIRE(animation.playback->frame == 1);
    animation.SetFrame(0);
    advance(1'000'000);
    animation.UpdateFrame();
    REQUIRE(animation.playback->frame == 1);
}
