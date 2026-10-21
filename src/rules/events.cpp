#include "wiz8/rules/events.h"

#include <utility>

#include <fmt/format.h>

#include "wiz8/rules/party.h"

void Events::push(Event event)
{
    if (observer_)
        observer_(event);
    else
        pending_.push_back(std::move(event));
}

void Events::observe(Observer observer)
{
    observer_ = std::move(observer);
    if (!observer_) return;
    for (const auto& event : std::exchange(pending_, {})) observer_(event);
}

TextNotice FormatCharacterNotice(const W8Party& party, const CharacterNotice& notice)
{
    const auto& name = party.characters[notice.party_slot].name;
    const auto& text = notice.text;
    const auto separator = text.starts_with("'") || text.starts_with(":") ? "" : " ";
    const auto stop = name.size() + (text.starts_with("'s") ? 2 : text.starts_with("'") ? 1 : 0);
    return {
        .palette = W8_FONT_PALETTE_WHITE,
        .text = fmt::format("{}{}{}", name, separator, text),
        .text_box = notice.context,
        .highlight = {static_cast<unsigned char>(party.orderIndexOf(notice.party_slot)), 0, stop}};
}
