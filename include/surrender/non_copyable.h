#ifndef SURRENDER_NON_COPYABLE_H
#define SURRENDER_NON_COPYABLE_H

/* Base for resource owners whose identity must stay stable. Inherit privately
   so the class is not usable as a NonCopyable and gains no public interface.
   Moves are not declared: with the copy operations deleted here, derived classes
   cannot be moved implicitly either. */
class NonCopyable {
protected:
    NonCopyable() = default;
    ~NonCopyable() = default;
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};

#endif
