#pragma once

#include <memory>

#include "wiz8/screen.h"

extern char g_default_level[];

// The fact journal, two pages of entries filtered by difficulty.
std::unique_ptr<Screen> CreateJournalScreen();
