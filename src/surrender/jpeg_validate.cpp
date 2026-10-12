#include "jpeg_validate.h"

#include <csetjmp>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include <cstdio> // IWYU pragma: keep - jpeglib.h needs FILE declared first.
#include <jpeglib.h>

namespace {

/* libjpeg reports errors by calling back into the application, which must
   not return; the only way out is longjmp. Every object alive across that
   jump is trivially destructible, so the jump skips no destructor. */
struct Validation {
    jpeg_decompress_struct image;
    jpeg_error_mgr errors;
    std::jmp_buf failure;
};

[[noreturn]] void Reject(j_common_ptr image)
{
    std::longjmp(static_cast<Validation*>(image->client_data)->failure, 1);
}

void Warn(j_common_ptr image, int level)
{
    if (level < 0) Reject(image);
}

/* Returns whether every scan decoded; fills `info` from the header. The
   caller destroys the decompressor, which is safe even if creation failed
   because the state starts zeroed. */
bool ValidateScans(Validation& state, std::span<const std::uint8_t> data, srJpegInfo& info)
{
    if (setjmp(state.failure)) return false;
    jpeg_create_decompress(&state.image);
    jpeg_mem_src(&state.image, data.data(), static_cast<unsigned long>(data.size()));
    const bool supported = jpeg_read_header(&state.image, TRUE) == JPEG_HEADER_OK &&
        state.image.data_precision == 8 && state.image.image_width != 0 &&
        state.image.image_height != 0 &&
        (state.image.num_components == 1 || state.image.num_components == 3 ||
         state.image.num_components == 4) &&
        std::uint64_t{state.image.image_width} * state.image.image_height <= 64 * 1024 * 1024;
    if (!supported) return false;
    // Coefficient decoding covers baseline/progressive scans and restart
    // markers without duplicating libjpeg syntax or performing pixel IDCT.
    if (jpeg_read_coefficients(&state.image) == nullptr || !jpeg_finish_decompress(&state.image))
        return false;
    info = {state.image.image_width, state.image.image_height,
            static_cast<std::uint32_t>(state.image.num_components)};
    return true;
}

} // namespace

std::optional<srJpegInfo> srValidateJpeg(std::span<const std::uint8_t> data)
{
    // Heap-allocated so the codec state is not an automatic object of the
    // frame setjmp returns into.
    auto state = std::make_unique<Validation>();
    state->image.err = jpeg_std_error(&state->errors);
    state->errors.error_exit = Reject;
    state->errors.emit_message = Warn;
    state->image.client_data = state.get();
    srJpegInfo info{};
    const bool valid = ValidateScans(*state, data, info);
    jpeg_destroy_decompress(&state->image);
    if (!valid) return std::nullopt;
    return info;
}
