#include "wiz8/asset_paths.h"
#include "wiz8/filesystem.h"
#include "wiz8/engine_code/stScript.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/sr_api.h"
#include "wiz8/local_code/CombatSound.h"
#include "temporary_directory.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#define CHECK(expression) do { if (!(expression)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression); std::exit(1); } } while (false)

static void write_text(std::string_view path, std::string_view text)
{
    auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
    file->write(text.data(), text.size());
}

int main()
{
    const std::filesystem::path root = make_temporary_directory("wiz8-resource-text");
    std::filesystem::create_directories(root / "assets");
    std::filesystem::create_directories(root / "user");
    w8_native::configure_paths({(root / "assets").string(), (root / "user").string(), {}});
    wiz8::create_directory("Data/Databases");
    const std::string long_line = "SOUND " + std::string(600, 'x') + "é";
    write_text("Data/script.txt", "* comment\r\n\r\nStart:\r\n  " + long_line +
               "\r\nSTART:\r\nend");
    stScript script;
    CHECK(script.Load("Data/script.txt"));
    CHECK(script.lines.size() == 2 && script.labels.size() == 1);
    CHECK(script.lines[0].text == "  " + long_line);
    CHECK(script.lines[1].text == "end");
    CHECK(script.GetSourceLine(0) == 4 && script.GetSourceLine(1) == 6);
    CHECK(script.GetSourceLine(-1) == -1 && script.GetSourceLine(2) == -1);
    CHECK(script.FindLabelLine("start") == 0 && script.FindLabelLine("missing") == -1);
    script.Clear();
    CHECK(script.lines.empty() && script.labels.empty());
    // Labels sharing a long prefix remain distinct; they have no disk width.
    const std::string prefix(31, 'a');
    write_text("Data/script.txt", prefix + "tail:\nend\n" + prefix + "different:\nend");
    CHECK(script.Load("Data/script.txt"));
    CHECK(script.FindLabelLine(prefix + "tail") == 0);
    CHECK(script.FindLabelLine(prefix + "different") == 1);
    CHECK(!script.Load("Data/missing.txt"));

    CHECK(srInit());
    {
        W8Monster monster;
        CHECK(monster.EvaluateScriptCondition("random(100)"));
        CHECK(!monster.EvaluateScriptCondition("RANDOM(0)"));
        CHECK(!monster.EvaluateScriptCondition("RANDOM(0," + std::string(1000, 'x') + ")"));
        CHECK(!monster.EvaluateScriptCondition(std::string(1000, 'x')));
    }
    CHECK(srExit());

    std::string attacks;
    for (int i = 0; i < 38; ++i) attacks += "attack * comment\r\n";
    const std::string impact = "impact_" + std::string(300, 'x') + "é";
    write_text("Data/Databases/HitSounds.txt", attacks + "# metal\r\n" + impact +
               " * comment\r\nsecond\r\n# wood\r\nwood");
    CHECK(LoadHitSoundDatabase());
    CHECK(GetMaterialImpactSound(0, 0) == impact);
    CHECK(GetMaterialImpactSound(1, 0) == "second");
    CHECK(GetMaterialImpactSound(27, 0) == impact);
    CHECK(GetMaterialImpactSound(0, 1) == "wood");
    CHECK(GetMaterialImpactSound(-1, 0) == "HIT");
    CHECK(GetMaterialImpactSound(0, 12) == "HIT");
    write_text("Data/Databases/HitSounds.txt", "truncated\n");
    CHECK(!LoadHitSoundDatabase());
    CHECK(GetMaterialImpactSound(0, 0) == impact); // Publish only complete tables.
    write_text("Data/Databases/HitSounds.txt", attacks + "# metal\nreplacement\n");
    CHECK(LoadHitSoundDatabase());
    CHECK(GetMaterialImpactSound(1, 0) == "replacement");
    CHECK(GetMaterialImpactSound(0, 1).empty());
    ReleaseHitSoundDatabase();
    ReleaseHitSoundDatabase();
    CHECK(GetMaterialImpactSound(0, 0).empty());

    write_text("Data/bytes.txt", std::string("é\r\n\0tail", 9));
    auto file = wiz8::open_file("Data/bytes.txt");
    file->seek(2, wiz8::SeekOrigin::begin);
    CHECK(file->read_text() == std::string("\r\n\0tail", 7));
    CHECK(file->read_text().empty());
    file.reset();
    std::filesystem::remove_all(root);
    std::puts("ok: owned script and sound text, long lines, CRLF and EOF");
}
