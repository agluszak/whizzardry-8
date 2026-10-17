#pragma once

#include <memory>

#include "wiz8/layouts/world.h"

// Test builds alone allow fixture scenes without running gameplay activation.
std::unique_ptr<W8World> ExchangeCurrentWorld(std::unique_ptr<W8World> world);
std::unique_ptr<W8World> ExchangeSkyWorld(std::unique_ptr<W8World> world);
