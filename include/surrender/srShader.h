#pragma once

#include <cstdint>

/* A shader is one 32-bit packed word. The mesh default 0x0100241b is PASS_LEQUAL, depth and
   color write, DSTBLEND_ZERO, FOG_DISABLE, GRADIENT_MODULATE, SRCBLEND_ONE, TEXTURING_DISABLE,
   DITHER_ENABLE. */
class srShader {
public:
    // NAME: srShader::srShader
    // RECOMP: ??0srShader@@QAE@XZ
    srShader() : value(0x0100241b) {}

    static constexpr std::uint32_t PASS_NEVER = 0;
    static constexpr std::uint32_t PASS_LESS = 1;
    static constexpr std::uint32_t PASS_EQUAL = 2;
    static constexpr std::uint32_t PASS_LEQUAL = 3;
    static constexpr std::uint32_t PASS_GREATER = 4;
    static constexpr std::uint32_t PASS_NOTEQUAL = 5;
    static constexpr std::uint32_t PASS_GEQUAL = 6;
    static constexpr std::uint32_t PASS_ALWAYS = 7;
    static constexpr std::uint32_t DSTBLEND_ZERO = 0;
    static constexpr std::uint32_t DSTBLEND_ONE = 1;
    static constexpr std::uint32_t DSTBLEND_SRC_COLOR = 2;
    static constexpr std::uint32_t DSTBLEND_ONE_MINUS_SRC_COLOR = 3;
    static constexpr std::uint32_t DSTBLEND_SRC_ALPHA = 4;
    static constexpr std::uint32_t DSTBLEND_ONE_MINUS_SRC_ALPHA = 5;
    static constexpr std::uint32_t FOG_DISABLE = 0;
    static constexpr std::uint32_t FOG_ENABLE = 1;
    static constexpr std::uint32_t FOG_SCALE_FRAGMENT = 2;
    static constexpr std::uint32_t FOG_WHITE = 3;
    static constexpr std::uint32_t GRADIENT_DISABLE = 0;
    static constexpr std::uint32_t GRADIENT_MODULATE = 1;
    static constexpr std::uint32_t GRADIENT_ADD = 2;
    static constexpr std::uint32_t SRCBLEND_ZERO = 0;
    static constexpr std::uint32_t SRCBLEND_ONE = 1;
    static constexpr std::uint32_t SRCBLEND_SRC_ALPHA = 2;
    static constexpr std::uint32_t SRCBLEND_ONE_MINUS_SRC_ALPHA = 3;
    static constexpr std::uint32_t DETAILCOLOR_DISABLE = 0;
    static constexpr std::uint32_t DETAILCOLOR_DETAIL = 1;
    static constexpr std::uint32_t DETAILCOLOR_SCALE = 2;
    static constexpr std::uint32_t DETAILCOLOR_INVSCALE = 3;
    static constexpr std::uint32_t DETAILCOLOR_ADD = 4;
    static constexpr std::uint32_t DETAILCOLOR_SUB = 5;
    static constexpr std::uint32_t DETAILCOLOR_SUBR = 6;
    static constexpr std::uint32_t DETAILCOLOR_BLEND = 7;
    static constexpr std::uint32_t DETAILCOLOR_DETAILBLEND = 8;
    static constexpr std::uint32_t DETAILALPHA_DISABLE = 0;
    static constexpr std::uint32_t DETAILALPHA_DETAIL = 1;
    static constexpr std::uint32_t DETAILALPHA_SCALE = 2;
    static constexpr std::uint32_t DETAILALPHA_INVSCALE = 3;
    static constexpr std::uint32_t DEPTH_WRITE_DISABLE = 0;
    static constexpr std::uint32_t DEPTH_WRITE_ENABLE = 1;
    static constexpr std::uint32_t COLOR_WRITE_DISABLE = 0;
    static constexpr std::uint32_t COLOR_WRITE_ENABLE = 1;
    static constexpr std::uint32_t ALPHATEST_DISABLE = 0;
    static constexpr std::uint32_t ALPHATEST_ENABLE = 1;
    static constexpr std::uint32_t DITHER_DISABLE = 0;
    static constexpr std::uint32_t DITHER_ENABLE = 1;
    static constexpr std::uint32_t SECONDARY_GRADIENT_DISABLE = 0;
    static constexpr std::uint32_t SECONDARY_GRADIENT_ENABLE = 1;
    static constexpr std::uint32_t TEXTURING_DISABLE = 0;
    static constexpr std::uint32_t TEXTURING_ENABLE = 1;

    // These constants encode fields in the packed shader word. They are
    // unsigned values and masks, not separate enumeration domains.
    static constexpr std::uint32_t PASS_MASK = 7;
    static constexpr std::uint32_t DEPTH_WRITE_SHIFT = 3;
    static constexpr std::uint32_t MASK_DEPTH_WRITE = 0x8;
    static constexpr std::uint32_t COLOR_WRITE_SHIFT = 4;
    static constexpr std::uint32_t MASK_COLOR_WRITE = 0x10;
    static constexpr std::uint32_t DSTBLEND_SHIFT = 5;
    static constexpr std::uint32_t FOG_SHIFT = 8;
    static constexpr std::uint32_t MASK_FOG = 0x300;
    static constexpr std::uint32_t GRADIENT_SHIFT = 10;
    static constexpr std::uint32_t MASK_GRADIENT = 0xc00;
    static constexpr std::uint32_t MASK_GRADIENT_MODULATE = 0x400;
    static constexpr std::uint32_t MASK_GRADIENT_ADD = 0x800;
    static constexpr std::uint32_t SECONDARY_GRADIENT_SHIFT = 12;
    static constexpr std::uint32_t MASK_SECONDARY_GRADIENT = 0x1000;
    static constexpr std::uint32_t SRCBLEND_SHIFT = 13;
    static constexpr std::uint32_t TEXTURING_SHIFT = 15;
    static constexpr std::uint32_t MASK_TEXTURING = 0x8000;
    static constexpr std::uint32_t DETAILCOLOR0_SHIFT = 16;
    static constexpr std::uint32_t MASK_DETAILCOLOR0 = 0x000f0000;
    static constexpr std::uint32_t DETAILALPHA0_SHIFT = 20;
    static constexpr std::uint32_t MASK_DETAILALPHA0 = 0x00700000;
    static constexpr std::uint32_t ALPHATEST_SHIFT = 23;
    static constexpr std::uint32_t MASK_ALPHATEST = 0x800000;
    static constexpr std::uint32_t DITHER_SHIFT = 24;
    static constexpr std::uint32_t MASK_DITHER = 0x1000000;
    static constexpr std::uint32_t DETAILCOLOR1_SHIFT = 25;
    static constexpr std::uint32_t MASK_DETAILCOLOR1 = 0x1e000000;
    static constexpr std::uint32_t DETAILALPHA1_SHIFT = 29;
    static constexpr std::uint32_t MASK_DETAILALPHA1 = 0xe0000000;

    std::uint32_t value;
};
