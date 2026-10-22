#pragma once

#include <string_view>

class GameSession;

enum class PartyImportResult { Success, Unreadable, UnsupportedEnding };

// Decode one file, begin its game, then convert the decoded records.
// The opening sequence keeps only the imported story choices, not parser state.
PartyImportResult ImportWizardry7Party(GameSession& games, std::string_view path);
