#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
#include <string>
#include <memory>
#include <string_view>
#include <vector>

#include "wiz8/asset_paths.h"
#include "wiz8/filesystem.h"
#include "wiz8/engine_code/stScript.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/sr_api.h"
#include "wiz8/local_code/CombatSound.h"
#include "temporary_directory.h"

static void write_text(std::string_view path, std::string_view text)
{
    auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
    file->write(text.data(), text.size());
}

TEST_CASE("resource_text_test")
{
    const std::filesystem::path root = make_temporary_directory("wiz8-resource-text");
    std::filesystem::create_directories(root / "assets");
    std::filesystem::create_directories(root / "user");
    w8_native::configure_paths({(root / "assets").string(), (root / "user").string(), {}});
    wiz8::create_directory("Data/Databases");
    const std::string long_line = "SOUND " + std::string(600, 'x') + "é";
    write_text("Data/script.txt", "* comment\r\n\r\nStart:\r\n  " + long_line + "\r\nSTART:\r\nend");
    stScript script;
    REQUIRE((script.Load("Data/script.txt")));
    REQUIRE((script.lines.size() == 2 && script.labels.size() == 1));
    REQUIRE((script.lines[0].text == "  " + long_line));
    REQUIRE((script.lines[1].text == "end"));
    REQUIRE((script.GetSourceLine(0) == 4 && script.GetSourceLine(1) == 6));
    REQUIRE((script.GetSourceLine(-1) == -1 && script.GetSourceLine(2) == -1));
    REQUIRE((script.FindLabelLine("start") == 0 && script.FindLabelLine("missing") == -1));
    script.Clear();
    REQUIRE((script.lines.empty() && script.labels.empty()));
    // Labels sharing a long prefix remain distinct; they have no disk width.
    const std::string prefix(31, 'a');
    write_text("Data/script.txt", prefix + "tail:\nend\n" + prefix + "different:\nend");
    REQUIRE((script.Load("Data/script.txt")));
    REQUIRE((script.FindLabelLine(prefix + "tail") == 0));
    REQUIRE((script.FindLabelLine(prefix + "different") == 1));
    REQUIRE((!script.Load("Data/missing.txt")));

    REQUIRE((srInit()));
    {
        W8Monster monster;
        REQUIRE((monster.EvaluateScriptCondition("random(100)")));
        REQUIRE((!monster.EvaluateScriptCondition("RANDOM(0)")));
        REQUIRE((!monster.EvaluateScriptCondition("RANDOM(0," + std::string(1000, 'x') + ")")));
        REQUIRE((!monster.EvaluateScriptCondition(std::string(1000, 'x'))));
    }
    REQUIRE((srExit()));

    std::string attacks;
    for (int i = 0; i < 38; ++i) {
        attacks += "attack * comment\r\n";
    }
    const std::string impact = "impact_" + std::string(300, 'x') + "é";
    write_text("Data/Databases/HitSounds.txt",
               attacks + "# metal\r\n" + impact + " * comment\r\nsecond\r\n# wood\r\nwood");
    REQUIRE((LoadHitSoundDatabase()));
    REQUIRE((GetMaterialImpactSound(0, 0) == impact));
    REQUIRE((GetMaterialImpactSound(1, 0) == "second"));
    REQUIRE((GetMaterialImpactSound(27, 0) == impact));
    REQUIRE((GetMaterialImpactSound(0, 1) == "wood"));
    REQUIRE((GetMaterialImpactSound(-1, 0) == "HIT"));
    REQUIRE((GetMaterialImpactSound(0, 12) == "HIT"));
    write_text("Data/Databases/HitSounds.txt", "truncated\n");
    REQUIRE((!LoadHitSoundDatabase()));
    REQUIRE((GetMaterialImpactSound(0, 0) == impact)); // Publish only complete tables.
    write_text("Data/Databases/HitSounds.txt", attacks + "# metal\nreplacement\n");
    REQUIRE((LoadHitSoundDatabase()));
    REQUIRE((GetMaterialImpactSound(1, 0) == "replacement"));
    REQUIRE((GetMaterialImpactSound(0, 1).empty()));
    ReleaseHitSoundDatabase();
    ReleaseHitSoundDatabase();
    REQUIRE((GetMaterialImpactSound(0, 0).empty()));

    write_text("Data/bytes.txt", std::string("é\r\n\0tail", 9));
    auto file = wiz8::open_file("Data/bytes.txt");
    file->seek(2, wiz8::SeekOrigin::begin);
    REQUIRE((file->read_text() == std::string("\r\n\0tail", 7)));
    REQUIRE((file->read_text().empty()));
    file.reset();
    std::filesystem::remove_all(root);
    std::puts("ok: owned script and sound text, long lines, CRLF and EOF");
}
