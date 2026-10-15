#pragma once

#include <memory>

namespace wiz8::sound {
struct AudioClip;
struct AudioPlayback;
using Clip = std::shared_ptr<AudioClip>;
// Owns one playback instance. Completion/stop releases its SDL resources even
// when callers retain the object. The mixer service retains fire-and-forget audio.
using Playback = std::shared_ptr<AudioPlayback>;
} // namespace wiz8::sound
