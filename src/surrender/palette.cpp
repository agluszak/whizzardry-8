#include <algorithm>
#include <memory>
#include <ostream>
#include <vector>

#include "compat/compiler.h"
#include "surrender/srARGB.h"
#include "surrender/srMath.h"
#include "surrender/srPalette.h"
#include "surrender/srTypeRegistry.h"
#include "surrender/stream_scope.h"

/* Squared channel distance scaled by the luma weights 299/587/114; the biased
   pointers index by (channel - reference) so negative differences work. */
// GLOBAL: SURRENDER 0x100A02A8
static w8_ulong dist_green_table[0x200];
// GLOBAL: SURRENDER 0x100A0AA8
static w8_ulong dist_blue_table[0x200];
// GLOBAL: SURRENDER 0x100A12A8
static w8_ulong dist_red_table[0x200];
// GLOBAL: SURRENDER 0x10098320
static w8_ulong* dist_red = dist_red_table + 0x100;
// GLOBAL: SURRENDER 0x10098324
static w8_ulong* dist_green = dist_green_table + 0x100;
// GLOBAL: SURRENDER 0x10098328
static w8_ulong* dist_blue = dist_blue_table + 0x100;
// GLOBAL: SURRENDER 0x100A1AA8
int srPalette::Quantizer::initialized = 0;

// FUNCTION: SURRENDER 0x10006940
void srPalette::Quantizer::checkLuts()
{
    if (initialized == 0) {
        for (w8_long index = 0; index < 0x200; ++index) {
            float delta = index - 256.0f;
            w8_long squared = (w8_long)(delta * delta);
            dist_red_table[index] = squared * 299;
            dist_green_table[index] = squared * 587;
            dist_blue_table[index] = squared * 114;
        }
        initialized = 1;
    }
}

// FUNCTION: SURRENDER 0x100069C0
void srPalette::Quantizer::initRG(w8_long red_lo, w8_long red_hi, w8_long green_lo,
                                  w8_long green_hi)
{
    w8_long best = 0x40000000;
    w8_long index;
    for (index = 0; index < color_count; ++index) {
        w8_long red_bound = red_hi;
        if ((red_lo + red_hi) / 2 <= palette[index].red) {
            red_bound = red_lo;
        }
        w8_long green_bound = green_hi;
        if ((green_lo + green_hi) / 2 <= palette[index].green) {
            green_bound = green_lo;
        }
        w8_long dist = dist_red[palette[index].red - red_bound] +
                       dist_green[palette[index].green - green_bound];
        if (dist < best) {
            best = dist;
        }
    }
    w8_long count = 0;
    for (index = 0; index < color_count; ++index) {
        w8_ulong red = palette[index].red;
        w8_ulong green = palette[index].green;
        w8_long dist = 0;
        w8_long bound = red_lo;
        if ((w8_long)red < red_lo || (bound = red_hi, red_hi < (w8_long)red)) {
            dist = dist_red[red - bound];
        }
        bound = green_lo;
        if ((w8_long)green < green_lo || (bound = green_hi, green_hi < (w8_long)green)) {
            dist += dist_green[green - bound];
        }
        if (dist < best) {
            entries[count].red = red;
            entries[count].green = green;
            entries[count].blue = palette[index].blue;
            entries[count].index = index;
            ++count;
        }
    }
    entry_count = count;
}

// FUNCTION: SURRENDER 0x10006B20
unsigned char srPalette::Quantizer::findRG(w8_long red, w8_long green)
{
    w8_long best = 0x40000000;
    w8_long selected = 0;
    for (w8_long index = 0; index < entry_count; ++index) {
        w8_long dist =
            dist_red[entries[index].red - red] + dist_green[entries[index].green - green];
        if (dist < best) {
            best = dist;
            selected = index;
        }
    }
    return (unsigned char)entries[selected].index;
}

// FUNCTION: SURRENDER 0x10006BB0
void srPalette::Quantizer::createRGTable()
{
    w8_long red_steps = 1 << (red_bits & 0x1f);
    w8_long green_steps = 1 << (green_bits & 0x1f);
    w8_long red_blocks = (red_steps + ((red_steps >> 0x1f) & 0xf)) >> 4;
    w8_long green_blocks = (green_steps + ((green_steps >> 0x1f) & 0xf)) >> 4;
    w8_long red_size = 0x10;
    if (red_blocks == 0) {
        red_blocks = 1;
        red_size = red_steps;
    }
    w8_long green_size = 0x10;
    if (green_blocks == 0) {
        green_blocks = 1;
        green_size = green_steps;
    }
    w8_long green_base = 0;
    for (w8_long green_block = 0; green_block < green_blocks; ++green_block) {
        w8_long red_base = 0;
        for (w8_long red_block = 0; red_block < red_blocks; ++red_block) {
            initRG(red_base, red_base + red_size - 1, green_base, green_base + green_size - 1);
            for (w8_long red = 0; red < red_size; ++red) {
                for (w8_long green = 0; green < green_size; ++green) {
                    lut_rg[red_base + red + (green_base + green) * 0x100] =
                        findRG(red_base + red, green_base + green);
                }
            }
            red_base += red_size;
        }
        green_base += green_size;
    }
}

// FUNCTION: SURRENDER 0x10006D30
unsigned char srPalette::Quantizer::findRGB(w8_long blue)
{
    w8_long best = 0x40000000;
    w8_long selected = 0;
    for (w8_long index = 0; index < entry_count; ++index) {
        w8_long dist = dist_blue[entries[index].blue - blue] + rg_dist[index];
        if (dist < best) {
            best = dist;
            selected = index;
        }
    }
    return (unsigned char)entries[selected].index;
}

// FUNCTION: SURRENDER 0x10006DB0
void srPalette::Quantizer::initRGB(w8_long red, w8_long green, w8_long blue_lo, w8_long blue_hi)
{
    w8_long best = 0x40000000;
    w8_long index;
    for (index = 0; index < color_count; ++index) {
        if (duplicate[index] == 0) {
            w8_long blue_bound = blue_hi;
            if ((blue_hi + blue_lo) / 2 <= palette[index].blue) {
                blue_bound = blue_lo;
            }
            w8_long dist = dist_red[palette[index].red - red] +
                           dist_green[palette[index].green - green] +
                           dist_blue[palette[index].blue - blue_bound];
            if (dist < best) {
                best = dist;
            }
        }
    }
    w8_long count = 0;
    for (index = 0; index < color_count; ++index) {
        if (duplicate[index] == 0) {
            w8_long rg =
                dist_red[palette[index].red - red] + dist_green[palette[index].green - green];
            w8_ulong blue = palette[index].blue;
            w8_long dist = rg;
            w8_long bound = blue_lo;
            if ((w8_long)blue < blue_lo || (bound = blue_hi, blue_hi < (w8_long)blue)) {
                dist = rg + dist_blue[blue - bound];
            }
            if (dist < best) {
                rg_dist[count] = rg;
                entries[count].red = palette[index].red;
                entries[count].green = palette[index].green;
                entries[count].blue = blue;
                entries[count].index = index;
                ++count;
            }
        }
    }
    entry_count = count;
}

// FUNCTION: SURRENDER 0x10006F60
void srPalette::Quantizer::partitionRGB(w8_long blue_lo, w8_long blue_hi)
{
    lut_row[blue_lo] = findRGB(blue_lo);
    lut_row[blue_hi] = findRGB(blue_hi);
    w8_long next = blue_lo + 1;
    if (next < blue_hi) {
        while (lut_row[blue_lo] != lut_row[blue_hi]) {
            blue_lo = (blue_lo + blue_hi) / 2;
            partitionRGB(next, blue_lo - 1);
            blue_hi = blue_hi - 1;
            lut_row[blue_lo] = findRGB(blue_lo);
            lut_row[blue_hi] = findRGB(blue_hi);
            next = blue_lo + 1;
            if (blue_hi <= next) {
                return;
            }
        }
        std::fill_n(lut_row + blue_lo + 1, blue_hi - blue_lo - 1, lut_row[blue_lo]);
    }
}

// FUNCTION: SURRENDER 0x10007080
void srPalette::Quantizer::createRGBTable()
{
    w8_long blue_steps = 1 << (blue_bits & 0x1f);
    w8_long blue_blocks = (blue_steps + ((blue_steps >> 0x1f) & 0x7f)) >> 7;
    w8_long blue_size = 0x80;
    if (blue_blocks == 0) {
        blue_blocks = 1;
        blue_size = blue_steps;
    }
    unsigned char* row = lut_rgb;
    for (w8_long index = 0; index < 0x100; ++index) {
        lut_row = row;
        unsigned char red = palette[index].red;
        unsigned char green = palette[index].green;
        for (w8_long block = 0; block < blue_blocks; ++block) {
            w8_long blue_lo = block * blue_size;
            initRGB(red, green, blue_lo, blue_lo + blue_size - 1);
            partitionRGB(blue_lo, blue_lo + blue_size - 1);
        }
        row += 0x100;
    }
}

// FUNCTION: SURRENDER 0x10007160
void srPalette::Quantizer::setPalette(srARGB* colors, w8_long color_count_,
                                      unsigned char* duplicates, unsigned char red_bits_,
                                      unsigned char green_bits_, unsigned char blue_bits_)
{
    checkLuts();
    color_count = color_count_;
    blue_bits = blue_bits_;
    entry_count = 0;
    lut_row = 0;
    red_bits = red_bits_;
    green_bits = green_bits_;
    srARGB empty_color;
    empty_color.blue = empty_color.green = empty_color.red = empty_color.alpha = 0;
    std::fill_n(palette, 0x100, empty_color);
    std::fill_n(duplicate, 0x100, 0);
    std::fill_n(entries, 0x100, Entry{});
    std::fill_n(rg_dist, 0x100, 0);
    std::fill_n(lut_rg, 0x10000, 0);
    std::fill_n(lut_rgb, 0x10000, 0);
    if (duplicates != 0) {
        std::copy_n(duplicates, 0x100, duplicate);
    }
    if (color_count > 0) {
        std::copy_n(colors, color_count, palette);
    }
    w8_long index;
    for (index = 0; index < color_count; ++index) {
        palette[index].alpha = 0xff;
    }
    for (index = 0; index < color_count; ++index) {
        for (w8_long prior = 0; prior < index && duplicate[index] == 0; ++prior) {
            if (palette[prior].red == palette[index].red &&
                palette[prior].green == palette[index].green &&
                palette[prior].blue == palette[index].blue) {
                duplicate[index] = 1;
            }
        }
    }
    createRGTable();
    createRGBTable();
    if (duplicates != 0) {
        std::copy_n(duplicates, 0x100, duplicate);
    } else {
        std::fill_n(duplicate, 0x100, 0);
    }
}

// FUNCTION: SURRENDER 0x10007510
unsigned char srPalette::Quantizer::quantize(const srARGB& color)
{
    return lut_rgb[(lut_rg[(color.green << 8) | color.red] << 8) | color.blue];
}

// FUNCTION: SURRENDER 0x10007550
unsigned char srPalette::Quantizer::quantize(unsigned char red, unsigned char green,
                                             unsigned char blue)
{
    return lut_rgb[(lut_rg[(green << 8) | red] << 8) | blue];
}

// FUNCTION: SURRENDER 0x10007590
void srPalette::Quantizer::quantize(unsigned char* indices, const srARGB* colors,
                                    w8_long color_count)
{
    for (w8_long index = 0; index < color_count; ++index) {
        indices[index] = quantize(colors[index].red, colors[index].green, colors[index].blue);
    }
}

// FUNCTION: SURRENDER 0x10004B70
srPalette::Quantizer::Quantizer()
{
    color_count = 0;
}

// FUNCTION: SURRENDER 0x10004160
int srPalette::matchPalette(const srARGB* const colors, w8_long color_count) const
{
    if (color_count != this->color_count) {
        return 0;
    }
    for (w8_long index = 0; index < color_count; ++index) {
        // reinterpret-ok: retail compares the packed color dwords
        if (reinterpret_cast<const w8_ulong&>(this->colors[index]) !=
            reinterpret_cast<const w8_ulong&>(colors[index])) {
            return 0;
        }
    }
    return 1;
}

// FUNCTION: SURRENDER 0x10004210
void srPalette::releaseQuantizer()
{
    quantizer.reset();
    flags = flags | 1;
}

// FUNCTION: SURRENDER 0x10004240
void srPalette::updateQuantizer()
{
    quantizer = std::make_unique<Quantizer>(colors.data(), color_count, nullptr, '\b', '\b', '\b');
}

// FUNCTION: SURRENDER 0x10004300
void srPalette::update()
{
    updateQuantizer();
    flags = flags & ~1;
}

/* A null color table builds the default palette: the 6x6x6 color cube with its gray diagonal
   skipped (216 - 6 = 210 entries), then a 46-entry gray ramp for the rest of the 256. A non-cube
   count gets a linear ramp. */
// FUNCTION: SURRENDER 0x10004310
srPalette::srPalette(srARGB* colors, w8_long color_count)
    : srClassSupport<srPalette, srClass, 0x2900>(), flags(0), colors(color_count),
      color_count(color_count)
{
    if (colors == 0) {
        if (color_count == 0x100) {
            w8_long index = 0;
            for (w8_long red = 0; red < 6; ++red) {
                for (w8_long green = 0; green < 6; ++green) {
                    for (w8_long blue = 0; blue < 6; ++blue) {
                        if (red != green || red != blue) {
                            srARGB& color = this->colors[index++];
                            color.alpha = static_cast<unsigned char>(srFloatToInt(255.0));
                            color.red =
                                static_cast<unsigned char>(srFloatToInt(red * 0.2f * 255.0));
                            color.green =
                                static_cast<unsigned char>(srFloatToInt(green * 0.2f * 255.0));
                            color.blue =
                                static_cast<unsigned char>(srFloatToInt(blue * 0.2f * 255.0));
                        }
                    }
                }
            }
            for (w8_long gray = 0; gray < 0x2e; ++gray) {
                srARGB& color = this->colors[0xd2 + gray];
                color.alpha = static_cast<unsigned char>(srFloatToInt(255.0));
                double value = gray * 0.022222223f * 255.0;
                color.red = static_cast<unsigned char>(srFloatToInt(value));
                color.green = static_cast<unsigned char>(srFloatToInt(value));
                color.blue = static_cast<unsigned char>(srFloatToInt(value));
            }
        } else {
            double step = 0.0;
            if (1 < color_count) {
                step = 1.0 / (color_count - 1);
            }
            for (w8_long index = 0; index < color_count; ++index) {
                srARGB& color = this->colors[index];
                double value = index * step * 255.0;
                color.alpha = static_cast<unsigned char>(srFloatToInt(255.0));
                color.red = static_cast<unsigned char>(srFloatToInt(value));
                color.green = static_cast<unsigned char>(srFloatToInt(value));
                color.blue = static_cast<unsigned char>(srFloatToInt(value));
            }
        }
    } else if (color_count > 0) {
        std::copy_n(colors, color_count, this->colors.begin());
    }
    flags = flags | 1;
}

// FUNCTION: SURRENDER 0x100046E0
srPalette& srPalette::operator=(const srPalette& other)
{
    if (this == &other) {
        return *this;
    }
    srClass::operator=(other);
    flags = other.flags;
    color_count = other.color_count;
    colors = other.colors;
    quantizer.reset();
    flags = flags | 1;
    return *this;
}

// FUNCTION: SURRENDER 0x100047A0
void srPalette::dump(std::ostream& stream)
{
    srClass::dump(stream);
    const srDumpStreamScope formatting(stream);
    stream.width(0x20);
    stream << "  Colors: " << color_count << '\n';
    stream.width(0x20);
    stream << "  Dataptr: " << static_cast<void*>(colors.data()) << '\n';
}

// FUNCTION: SURRENDER 0x10004850
void srPalette::setColors(w8_long destination_index, const srARGB* const colors,
                          w8_long color_count)
{
    if (0 <= destination_index && destination_index + color_count <= this->color_count) {
        for (w8_long index = 0; index < color_count; ++index) {
            this->colors[destination_index + index] = colors[index];
        }
        flags = flags | 1;
    }
}

// FUNCTION: SURRENDER 0x100048A0
srARGB srPalette::getColor(w8_long index) const
{
    return colors[index];
}

// FUNCTION: SURRENDER 0x100048C0
w8_long srPalette::getPaletteSize() const
{
    return color_count;
}

// FUNCTION: SURRENDER 0x100048D0
void srPalette::setColor(w8_long index, const srARGB& color)
{
    if (0 <= index && index < color_count) {
        colors[index] = color;
        flags = flags | 1;
    }
}

// FUNCTION: SURRENDER 0x10004900
unsigned char srPalette::quantize(const srARGB& color)
{
    if ((flags & 1) != 0) {
        update();
    }
    return quantizer->quantize(color);
}

// FUNCTION: SURRENDER 0x10004920
void srPalette::quantize(unsigned char* const indices, const srARGB* const colors,
                         w8_long color_count)
{
    if ((flags & 1) != 0) {
        update();
    }
    quantizer->quantize(indices, colors, color_count);
}

// FUNCTION: SURRENDER 0x10004950
const srARGB* srPalette::getPaletteDataPtr()
{
    return colors.data();
}

// FUNCTION: SURRENDER 0x10004960
const char* srPalette::sGetClassName()
{
    return "srPalette";
}
