#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_rect.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "harness.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/skills.h"
#include "wiz8/dialog_code/PortraitQuote.h"
#include "wiz8/entities/monster.h"
#include "wiz8/fact_state.h"
#include "wiz8/game.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/npc_dialogue_view.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/local_screens/npc_dialogue_panel.h"
#include "wiz8/message_box.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/npc_speech.h"
#include "wiz8/platform/timer.h"
#include "wiz8/rules/events.h"
#include "wiz8/screen.h"
#include "wiz8/script_context.h"
#include "wiz8/text_input.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"

namespace {

/* --- NPC dialogue ---------------------------------------------------------- */

/* The conversation waits for the player: nothing spoken, queued or asked. */
bool DialogueAwaitingPlayer()
{
    return CurrentGame().activity.conversation &&
           !CurrentGame().npc_dialogue.has_active_utterance() &&
           !CurrentGame().npc_dialogue.has_queued_actions() &&
           CurrentGame().npc_dialogue.reply() == W8NpcReplyState::none && g_modal_owner == nullptr;
}

/* Skip voiced lines with Escape, as the player can, while waiting for input. */
void WaitDialogue(GameHarness& game, const char* step, const std::function<bool()>& ready)
{
    game.wait(
        step,
        [&] {
            if (ready()) return true;
            if (CurrentGame().npc_dialogue.voice_pending() && g_modal_owner == nullptr) {
                game.key(SDLK_ESCAPE);
            }
            return false;
        },
        90);
}

void StartConversation(GameHarness& game, const char* step, W8NpcState* npc)
{
    REQUIRE(!CurrentGame().activity.conversation);
    ForwardNpcScriptNotice(npc, nullptr, -1, false);
    WaitDialogue(game, step, [&] {
        return CurrentGame().npc_interaction.dialogue_npc.get() == npc && DialogueAwaitingPlayer();
    });
}

/* Types a keyword into the transcript's input field and submits it. */
void Ask(GameHarness& game, std::string_view keyword)
{
    REQUIRE(DialogueAwaitingPlayer());
    game.key(SDLK_RETURN);
    REQUIRE((TextFieldGroup::Focused() && TextFieldGroup::Focused()->Editing()));
    game.enter_text(keyword);
    REQUIRE(TextFieldGroup::Focused()->Text(0) == keyword);
    game.key(SDLK_RETURN);
}

/* Reply buttons sit at y=214 (price) or y=219 (options), above the modal body. */
void ChooseFirstReply(GameHarness& game, const char* step)
{
    WaitDialogue(game, step, [] { return g_modal_owner != nullptr; });
    HitTarget* first = nullptr;
    for (const int y : {214, 219}) {
        for (int x = 0; x < 640; ++x) {
            auto* target = UiTargetAt({x, y});
            if (target && target->bounds().h <= 24 &&
                (!first || target->bounds().x < first->bounds().x))
                first = target;
        }
    }
    REQUIRE(first != nullptr);
    game.click(step, *first);
    game.wait(step, [] { return g_modal_owner == nullptr; });
}

/* Buys one paid answer: the price prompt, "yes", the gold and the fact. */
void BuyAnswer(GameHarness& game, const char* step, W8FactId fact)
{
    WaitDialogue(game, step, [] {
        return g_modal_owner != nullptr && CurrentGame().npc_dialogue.price_check().has_value();
    });
    const unsigned gold = CurrentGame().party.gold;
    const int price = CurrentGame().npc_dialogue.price_check().value().price;
    CAPTURE(step, gold, price);
    REQUIRE((price > 0 && unsigned(price) <= gold));
    REQUIRE(GetFact(fact) == 0);
    ChooseFirstReply(game, step);
    WaitDialogue(game, step, DialogueAwaitingPlayer);
    REQUIRE(CurrentGame().party.gold == gold - unsigned(price));
    REQUIRE(GetFact(fact) != 0);
}

/* Escape asks for the farewell; once it has been said, Escape dismisses it. */
void SayFarewell(GameHarness& game, const char* step)
{
    REQUIRE(DialogueAwaitingPlayer());
    game.key(SDLK_ESCAPE);
    game.wait(step, [&] {
        if (!CurrentGame().activity.conversation) return true;
        if (CurrentGame().npc_dialogue.has_active_utterance()) game.key(SDLK_ESCAPE);
        return false;
    });
    WaitMainGame(game, step);
    REQUIRE(!CurrentGame().npc_dialogue.has_queued_actions());
}

/* A conversation with Burz, the merchant beside the starting area: the
   messenger keyword defers a yes/no option list behind its question, both
   paid answers charge the party, and the farewell ends the session so a
   second visit starts afresh. */
void NpcDialogueScenario()
{
    GameHarness game(false);
    StartPremadeGame(game);
    Settle(game, "settle");
    W8NpcState* burz = FindNpcStateByName("Burz");
    REQUIRE(burz);
    CurrentGame().party.gold = 200;

    StartConversation(game, "greeting", burz);
    {
        Game detached(CurrentGame().data);
        const auto playing = MainGame()->dialogue.utterance_identity;
        DisplayNpcQuote(detached, "Not this game's quote", false);
        ShowNpcPortraitMessage(detached, "Not this game's notice");
        W8NpcScriptQuote quote;
        quote.subquotes.emplace_back("Detached");
        REQUIRE(SpeakNpcSubquote(MakeScriptContext(detached), &quote, 0, false, false) ==
                W8NpcSubquoteOutcome::presented);
        REQUIRE(MainGame()->dialogue.utterance_identity == playing);
        REQUIRE_FALSE(detached.npc_dialogue.has_active_utterance());
    }
    {
        auto& panel = MainGame()->dialogue;
        auto& sequence = CurrentGame().npc_dialogue;
        const auto notices = std::make_shared<unsigned>(0);
        CurrentGame().events.observe([notices](const Event& event) {
            if (std::holds_alternative<CharacterNotice>(event)) ++*notices;
            PresentGameEvent(event);
        });
        panel.showPortraitMessage(
            "Old notice",
            W8SkillNoticePayload{{{static_cast<unsigned>(PremadeSlot()), W8_SKILL_COMMUNICATION}}});
        const auto identity = sequence.BeginVoiceQuote();
        panel.utterance_identity = identity;
        const auto completions = std::make_shared<unsigned>(0);
        REQUIRE(PlayNpcVoice(panel.speech, "Data\\Sound\\misc\\startgame.wav",
                             [completions] { ++*completions; }));
        sequence.VoiceStarted(identity, MonotonicMilliseconds(), true, 2000);
        const auto old_voice = panel.speech.voice_handle;
        auto* body = GetNpcMonster(burz);
        REQUIRE(body);
        body->talking = true;
        sequence.ReplyDialogOpened(std::nullopt); // Ordinary completion retains its question.
        panel.showPortraitMessage("Replacement notice");
        REQUIRE(*notices == 1);
        REQUIRE(panel.active_quote);
        REQUIRE(panel.active_quote->bubble);
        REQUIRE(panel.active_quote->bubble->text() == "Replacement notice");
        REQUIRE_FALSE(wiz8::sound::is_playing(old_voice));
        REQUIRE_FALSE(body->talking);
        REQUIRE(panel.utterance_identity);
        const auto replacement = *panel.utterance_identity;
        sequence.VoiceFinished(identity, MonotonicMilliseconds());
        REQUIRE(sequence.utterance(replacement));
        REQUIRE(sequence.utterance(replacement)->phase ==
                W8NpcDialogue::Utterance::Phase::portrait_message);
        panel.tryFinishVoice(true);
        wiz8::sound::service();
        REQUIRE_FALSE(sequence.has_active_utterance());
        REQUIRE(*completions == 0);
        REQUIRE(*notices == 1);
        sequence.ReplyDialogClosed();
        CurrentGame().events.observe(PresentGameEvent);
    }
    // Filtering/sorting changes the view, not notebook identity. Removing the
    // first displayed topic must remove that topic from the actual notebook.
    auto& controller = *MainGame()->dialogue.ui->transcript;
    const bool sorted = CurrentGame().story.keywords_sorted;
    LearnDialogueKeyword("000NotebookProbe", W8_DIALOGUE_CATEGORY_PEOPLE, false);
    CurrentGame().story.keywords_sorted = true;
    CurrentGame().npc_interaction.dialogue_category_filter = W8_DIALOGUE_CATEGORY_PEOPLE;
    controller.RefreshNotebook();
    controller.SelectTranscriptKeywordAtPoint(0x1e3, 0x12d - controller.scroll_height + 1);
    const int selected = controller.GetSelectedTranscriptEntryIndex();
    REQUIRE(selected >= 0);
    REQUIRE(CurrentGame().story.keywords[selected].text == "000NotebookProbe");
    controller.RemoveSelectedTranscriptEntry();
    REQUIRE(std::ranges::none_of(CurrentGame().story.keywords, [](const auto& entry) {
        return entry.text == "000NotebookProbe";
    }));
    CurrentGame().story.keywords_sorted = sorted;
    CurrentGame().npc_interaction.dialogue_category_filter = W8_DIALOGUE_CATEGORY_ALL;
    controller.RefreshNotebook();

    Ask(game, "messenger");
    ChooseFirstReply(game, "messenger-options");
    BuyAnswer(game, "messenger-price", W8_FACT_QUEST_BURZ_MARTEN_TRYNTON);
    Ask(game, "ship");
    BuyAnswer(game, "ship-price", W8_FACT_QUEST_BURZ_REPORT_SHIP);
    SayFarewell(game, "farewell");
    Walk(game, "walk-after-farewell", 2.0f);

    StartConversation(game, "second-greeting", burz);
    // Keyword popups own their editor and restore the transcript's focus.
    auto* transcript_fields = TextFieldGroup::Focused();
    REQUIRE(transcript_fields);
    W8NpcQuoteEntry keyword_request{};
    keyword_request.kind = W8_NPC_ENTRY_KEYWORD_INPUT;
    OpenNpcDialog(nullptr, &keyword_request, 0);
    game.frame();
    REQUIRE(TextFieldGroup::Focused() != transcript_fields);
    game.enter_text("cancelled");
    game.key(SDLK_ESCAPE);
    REQUIRE_FALSE(g_modal_owner);
    REQUIRE(TextFieldGroup::Focused() == transcript_fields);
    OpenNpcDialog(nullptr, &keyword_request, 0);
    game.frame();
    game.enter_text("hello");
    REQUIRE(TextFieldGroup::Focused()->Text(0) == "hello");
    game.key(SDLK_RETURN);
    REQUIRE_FALSE(g_modal_owner);
    REQUIRE(TextFieldGroup::Focused() == transcript_fields);
    WaitDialogue(game, "keyword-reply", DialogueAwaitingPlayer);

    // A portrait opens the camp over the conversation; leaving the camp takes it up again.
    OpenCharacterScreenForPartySlot(PremadeSlot(), false);
    game.wait_screen("camp-from-dialogue", W8_SCREEN_CAMP);
    REQUIRE((CurrentGame().activity.conversation_aside && !CurrentGame().activity.conversation));
    game.key(SDLK_ESCAPE);
    WaitMainGame(game, "camp-left");
    WaitDialogue(game, "dialogue-resumed", [&] {
        return CurrentGame().npc_interaction.dialogue_npc.get() == burz && DialogueAwaitingPlayer();
    });
    REQUIRE_FALSE(CurrentGame().activity.conversation_aside);

    // A skill increase that waited out the conversation is shown once the game is idle.
    auto& deferred = CurrentGame().monster_manager_entries[PremadeSlot()].skill_notice_pending;
    deferred[W8_SKILL_COMMUNICATION] = true;
    game.frame();
    REQUIRE(deferred[W8_SKILL_COMMUNICATION]);
    SayFarewell(game, "second-farewell");
    game.wait("deferred-skill-notice", [&] { return !deferred[W8_SKILL_COMMUNICATION]; });
    std::printf("npc-dialogue: two visits after %lu frames\n", game.frames());
}

} // namespace

TEST_CASE("npc-dialogue", "[runtime]")
{
    RunScenario(NpcDialogueScenario);
}
