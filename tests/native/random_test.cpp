#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include <glm/ext/vector_float3.hpp>

#include "scoped_game.h"
#include "scoped_game_data.h"
#include "wiz8/character_skills.h"
#include "wiz8/data/attack_modes.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/skills.h"
#include "wiz8/game.h"
#include "wiz8/local_code/CombatAttack.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/rules/random.h"
#include "wiz8/platform/random.h"
#include "wiz8/render/texture.h"

namespace {
// The drawing function's bare name: GCC spells the whole signature, Clang only the name.
std::string_view FunctionName(std::string_view signature)
{
    signature = signature.substr(0, signature.find('('));
    signature.remove_prefix(signature.rfind(' ') + 1);
    if (const auto scope = signature.rfind("::"); scope != std::string_view::npos)
        signature.remove_prefix(scope + 2);
    return signature;
}

std::vector<std::string> Describe(const std::vector<Rng::Draw>& draws)
{
    std::vector<std::string> lines;
    for (const auto& draw : draws) {
        const auto function = FunctionName(draw.where.function_name());
        lines.push_back(draw.range == 0
                            ? fmt::format("{} next() = {}", function, draw.value)
                            : fmt::format("{} below({}) = {}", function, draw.range, draw.value));
    }
    return lines;
}

// Records a stream's draws for the scope, so no stream outlives the log it records into.
struct ScopedTrace {
    explicit ScopedTrace(Rng& traced) : rng(traced) { rng.trace(&draws); }
    ScopedTrace(const ScopedTrace&) = delete;
    ScopedTrace& operator=(const ScopedTrace&) = delete;
    ~ScopedTrace() { rng.trace(nullptr); }

    Rng& rng;
    std::vector<Rng::Draw> draws;
};
} // namespace

TEST_CASE("Random covers the whole requested range", "[random]")
{
    Rng rng(1);
    for (const auto expected : {41u, 18467u, 6334u, 26500u, 19169u})
        REQUIRE(rng.next() == expected);
    rng.seed(1);
    REQUIRE(rng.below(0) == 0); // Empty ranges do not consume a draw.
    REQUIRE(rng.next() == 41);
    rng.seed(1);
    std::set<std::uint32_t> seen;
    for (int roll = 0; roll < 20000; ++roll) {
        const auto value = rng.below(10);
        REQUIRE(value < 10);
        seen.insert(value);
    }
    REQUIRE(seen.size() == 10);
    for (int roll = 0; roll < 1000; ++roll) REQUIRE(rng.below(100000) < 100000);
}

/* Party creation replaces the game it edits, and a session draws one sequence as retail
   did: each new game continues the stream of the game before it. */
TEST_CASE("A new game continues the previous game's stream", "[random]")
{
    ScopedGame scope;
    SeedGameRandom(1);
    REQUIRE(CurrentGame().rng.next() == 41);
    BeginGame();
    REQUIRE(CurrentGame().rng.next() == 18467);
    EndGame();
    BeginGame();
    REQUIRE(CurrentGame().rng.next() == 6334);
}

/* The determinism harness: party creation, loot, skill practice and combat rules draw
   from one stream in this order. A change that moves, adds or drops a draw changes
   what a seeded game plays, so it must show up here and be deliberate. */
TEST_CASE("Rules draw from the game's stream in retail order", "[random]")
{
    ScopedGame scope;
    GameData data;
    data.items.resize(1);
    data.items[0].quantity_kind = W8_ITEM_QUANTITY_STACK;
    data.items[0].initial_quantity = {.base = 0, .count = 2, .sides = 6};
    const ScopedGameData installed(std::move(data));
    auto& game = CurrentGame();
    game.rng.seed(1);
    ScopedTrace trace(game.rng);

    // Party creation: two clashes with the party's voices force two re-rolls.
    auto* created = &game.party.characters[0];
    for (int slot = 0; slot < 3; ++slot) game.party.characters[slot].fInParty = true;
    game.party.characters[2].voice = 1;
    EnsureUniquePartyVoice(created);
    REQUIRE(created->personality == 1);
    REQUIRE(created->voice == 1);

    // Loot: a new stack rolls its quantity.
    REQUIRE(MakeItem(0, false, false, false).stack_count == 7);

    // Practice rolls every usage point.
    auto& sword = created->skills[W8_SKILL_SWORD];
    sword.active = true;
    sword.base_level = 50;
    PracticeCharacterSkill(game, created, W8_SKILL_SWORD, 3, true);
    REQUIRE(sword.practice_count == 1);

    // Combat: an attack mode and a missile's scatter.
    ChooseAttackMode((1u << W8_ATTACK_MODE_SWING) | (1u << W8_ATTACK_MODE_THRUST));
    glm::vec3 aim{100.0f, 0.0f, 0.0f};
    const glm::vec3 origin{};
    ScatterMissileAimPoint(&origin, &aim, 50, false);

    REQUIRE(Describe(trace.draws) == std::vector<std::string>{
                                         "EnsureUniquePartyVoice below(9) = 0",
                                         "EnsureUniquePartyVoice below(2) = 1",
                                         "EnsureUniquePartyVoice below(9) = 1",
                                         "EnsureUniquePartyVoice below(2) = 1",
                                         "RollDice below(6) = 3",
                                         "RollDice below(6) = 2",
                                         "PracticeCharacterSkill below(100) = 35",
                                         "PracticeCharacterSkill below(100) = 89",
                                         "PracticeCharacterSkill below(100) = 82",
                                         "ChooseAttackMode below(9) = 6",
                                         "ScatterMissileAimPoint below(360) = 62",
                                         "ScatterMissileAimPoint below(100) = 85",
                                     });
}

TEST_CASE("Animated texture random triggers advance once across views", "[random][renderer]")
{
    auto& rng = PresentationRng();
    rng.seed(1);
    ScopedTrace trace(rng);
    TexturePlayback playback;
    playback.trigger_mode = W8_TEXTURE_TRIGGER_RANDOM_FRAME;
    playback.probability = 1;
    playback.textures = {std::make_shared<Texture>(), std::make_shared<Texture>(),
                         std::make_shared<Texture>()};
    Texture animation(std::move(playback));
    animation.UpdateFrame(42);
    REQUIRE(trace.draws.size() == 2);
    const auto selected = animation.currentFrame();
    animation.UpdateFrame(42);
    REQUIRE(animation.currentFrame() == selected);
    REQUIRE(trace.draws.size() == 2);
    animation.UpdateFrame(43);
    REQUIRE(trace.draws.size() == 4);
}
