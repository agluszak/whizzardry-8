#include "wiz8/rules/events.h"

#include <utility>
#include <vector>

void Events::push(Event event)
{
    if (observer_ && !delivering_ && pending_.empty()) {
        observer_(event);
        return;
    }
    pending_.push_back(std::move(event));
    deliver();
}

void Events::publish(std::vector<Event> outcomes)
{
    pending_.reserve(pending_.size() + outcomes.size());
    for (auto& outcome : outcomes) pending_.push_back(std::move(outcome));
    deliver();
}

void Events::observe(Observer observer)
{
    observer_ = std::move(observer);
    deliver();
}

void Events::deliver()
{
    if (!observer_ || delivering_) return;
    delivering_ = true;
    try {
        // Follow-up outcomes join the tail, after the already completed operation.
        while (observer_ && !pending_.empty()) {
            auto event = std::move(pending_.front());
            pending_.erase(pending_.begin());
            observer_(event);
        }
    } catch (...) {
        delivering_ = false;
        throw;
    }
    delivering_ = false;
}
