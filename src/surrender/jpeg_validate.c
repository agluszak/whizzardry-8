#include "jpeg_validate.h"

#include <jpeglib.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <jmorecfg.h>

struct Validation {
    struct jpeg_decompress_struct image;
    struct jpeg_error_mgr errors;
    jmp_buf failure;
};

static void reject(j_common_ptr image)
{
    struct Validation* state = (struct Validation*)image->client_data;
    longjmp(state->failure, 1);
}
static void warning(j_common_ptr image, int level)
{
    if (level < 0) reject(image);
}

bool srValidateJpeg(const unsigned char* data, size_t size,
                    unsigned* width, unsigned* height, unsigned* components)
{
    struct Validation* state = calloc(1, sizeof(*state));
    if (!state) return false;
    state->image.err = jpeg_std_error(&state->errors);
    state->errors.error_exit = reject;
    state->errors.emit_message = warning;
    state->image.client_data = state;
    // All mutable codec state lives on the heap across longjmp.
    if (setjmp(state->failure)) {
        jpeg_destroy_decompress(&state->image);
        free(state);
        return false;
    }
    jpeg_create_decompress(&state->image);
    jpeg_mem_src(&state->image, data, size);
    const bool valid = jpeg_read_header(&state->image, TRUE) == JPEG_HEADER_OK &&
        state->image.data_precision == 8 && state->image.image_width && state->image.image_height &&
        (state->image.num_components == 1 || state->image.num_components == 3 ||
         state->image.num_components == 4) &&
        (uint64_t)state->image.image_width * state->image.image_height <= 64 * 1024 * 1024;
    bool complete = false;
    if (valid) {
        // Coefficient decoding covers baseline/progressive scans and restart
        // markers without duplicating libjpeg syntax or performing pixel IDCT.
        complete = jpeg_read_coefficients(&state->image) != NULL &&
                   jpeg_finish_decompress(&state->image);
        if (complete) {
            *width = state->image.image_width;
            *height = state->image.image_height;
            *components = (unsigned)state->image.num_components;
        }
    }
    jpeg_destroy_decompress(&state->image);
    free(state);
    return complete;
}
