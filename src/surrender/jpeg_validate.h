#pragma once

#include <cstdint>
#include <optional>
#include <span>

struct srJpegInfo {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t components;
};

/* Run libjpeg over every scan without decoding pixels. Corrupt or truncated
   data, which libjpeg reports as warnings, rejects the image. */
std::optional<srJpegInfo> srValidateJpeg(std::span<const std::uint8_t> data);
