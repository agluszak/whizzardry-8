#pragma once

#include <cassert>

/* A world record's link to its scene entity. The world sets it when the record becomes
   active and clears it before the entity is destroyed, so only an active record is sure to
   have one. A caller says which it needs: `find()` looks the entity up and may find none,
   and `->` and `*` require it. Anything kept past the current call keeps the record's id,
   never the entity. */
template<class Entity> class EntityLink {
public:
    Entity* find() const { return entity_; }
    Entity& operator*() const
    {
        assert(entity_ && "the record has no active entity");
        return *entity_;
    }
    Entity* operator->() const { return &**this; }
    explicit operator bool() const { return entity_ != nullptr; }

    void attach(Entity& entity) { entity_ = &entity; }
    void detach() { entity_ = nullptr; }

private:
    Entity* entity_ = nullptr;
};
