#pragma once
#include <cstddef>
namespace w8_native
{
void audio_offline_for_test(bool enabled);
bool audio_render_for_test(float* samples, size_t frames);
} // namespace w8_native
