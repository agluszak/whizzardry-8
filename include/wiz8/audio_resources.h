#pragma once

#include <memory>

struct AudioClip;
struct AudioPlayback;
using SoundClip = std::shared_ptr<AudioClip>;
// Owns one playback instance. Completion/stop releases its SDL resources even
// when callers retain the object. The mixer service retains fire-and-forget audio.
using SoundPlayback = std::shared_ptr<AudioPlayback>;
