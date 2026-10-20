#include "wiz8/rules/events.h"

#include <utility>

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
