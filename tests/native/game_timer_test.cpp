#include <array>
#include <cstdint>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "save_payload.h"
#include "temporary_directory.h"
#include "wiz8/filesystem.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/platform/GameTimeAccumulator.h"
#include "wiz8/platform/IntervalGate.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/gpu_renderer.h"
#include "wiz8/render/texture.h"
#include "wiz8/runtime_test_hooks.h"

W8RuntimeTestHooks g_runtime_test_hooks;

namespace {
struct TimerFixture {
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
        g_status.game_time_days = 0;
        g_status.game_time_ms = 0;
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
    REQUIRE(GameClockMilliseconds() == 2500);
    W8GameTimer later;
    REQUIRE(later.ReadClock() == 15000);
}

TEST_CASE_METHOD(TimerFixture, "Game-day time and accelerated frame deltas keep separate clocks",
                 "[timers]")
{
    W8GameTimer game(2.0f, false);
    game.SetMode(W8_TIMER_CLOCK_GAME);
    advance(250'000);
    REQUIRE(game.GetProgress() == 0);
    g_status.game_time_ms = 250;
    REQUIRE(game.GetProgress() == Catch::Approx(0.125f));

    ResetGameTimeAccumulator();
    auto& accumulator = GameTimeAccumulator();
    accumulator.SetTimeScale(0.5f);
    advance(25'000);
    REQUIRE(accumulator.Update() == Catch::Approx(0.5f));
    PauseSharedGameTimers();
    advance(1'000'000);
    REQUIRE(accumulator.Update() == 0);
    ResumeSharedGameTimers();
    advance(25'000);
    REQUIRE(accumulator.Update() == Catch::Approx(0.5f));
    accumulator.ResetTimeScale();
    advance(25'000);
    REQUIRE(accumulator.Update() == Catch::Approx(0.25f));
    REQUIRE(accumulator.GetElapsed() == Catch::Approx(1.25f));
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

TEST_CASE_METHOD(TimerFixture, "Renderer statistics measure elapsed seconds without a clock owner",
                 "[timers][renderer]")
{
    GpuRenderer renderer;
    renderer.resetStatistics();
    advance(250'000);
    REQUIRE(renderer.getStatistics().elapsed == Catch::Approx(0.25));
    renderer.resetStatistics();
    advance(125'000);
    REQUIRE(renderer.getStatistics().elapsed == Catch::Approx(0.125));
}

TEST_CASE_METHOD(TimerFixture, "Texture animations retain their clock after 32-bit tick rollover",
                 "[timers]")
{
    g_runtime_test_hooks.clock_us = ((std::uint64_t{1} << 32) + 250) * 1000;
    stTextureAnim animation;
    animation.textures.resize(3);
    animation.frame_rate = 1.0f;
    advance(1'000'000);
    animation.UpdateFrame();
    REQUIRE(animation.frame == 1);
    animation.SetFrame(0);
    advance(1'000'000);
    animation.UpdateFrame();
    REQUIRE(animation.frame == 1);
}
