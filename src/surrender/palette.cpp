#include "surrender/srMath.h"

#include "surrender/srPalette.h"
#include "surrender/srStreamFlags.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <ostream>
#include <vector>

#include "surrender/srColorSurfaceIFace.h"
#include "surrender/srCore.h"
#include "surrender/srImporter.h"

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
void srPalette::Quantizer::initRG(w8_long red_lo, w8_long red_hi, w8_long green_lo, w8_long green_hi)
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
        w8_long dist = dist_red[entries[index].red - red] + dist_green[entries[index].green - green];
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
            w8_long rg = dist_red[palette[index].red - red] + dist_green[palette[index].green - green];
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
void srPalette::Quantizer::setPalette(srARGB* colors, w8_long color_count_, unsigned char* duplicates,
                                      unsigned char red_bits_, unsigned char green_bits_,
                                      unsigned char blue_bits_)
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
void srPalette::Quantizer::quantize(unsigned char* indices, const srARGB* colors, w8_long color_count)
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

// FUNCTION: SURRENDER 0x100041A0
srPalette* srPalette::findMatchingPalette(const srARGB* const colors, w8_long color_count)
{
    srPalette* palette = 0;
    while (true) {
        srRegistry* registry = srCore.getRegistry();
        srRegistry::ClassNode* node = registry->getClassNode(0x2900);
        if (node == 0) {
            node = registry->registerClass(sGetClassName(), srClass::sGetClassNode(), 0x2900, 1);
        }
        palette = static_cast<srPalette*>(registry->findExact(node, palette));
        if (palette == 0) {
            break;
        }
        if (palette->matchPalette(colors, color_count) != 0) {
            return palette;
        }
    }
    return 0;
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
    : srClassSupport<srPalette, srClass, 1, 0x2900>(), flags(0), colors(color_count),
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
    std::ios::fmtflags flags = stream.flags();
    stream.setf(std::ios::left, std::ios::adjustfield);
    stream.width(0x20);
    stream << "  Colors: " << color_count << '\n';
    stream.width(0x20);
    stream << "  Dataptr: " << static_cast<void*>(colors.data()) << '\n';
    stream.flags(static_cast<std::ios::fmtflags>(flags & 0x7fff));
}

// FUNCTION: SURRENDER 0x10004850
void srPalette::setColors(w8_long destination_index, const srARGB* const colors, w8_long color_count)
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
void srPalette::quantize(unsigned char* const indices, const srARGB* const colors, w8_long color_count)
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

// FUNCTION: SURRENDER 0x10004970
srClass* srPalette::vInstance()
{
    return new srPalette(0, 1);
}

// FUNCTION: SURRENDER 0x100067D0
srPalette::Sampler::Sampler(w8_long sample_limit)
{
    this->sample_limit = sample_limit;
    if (sample_limit < 0) {
        this->sample_limit = 0;
    }
    setOutputPaletteSize(0x100);
    setSampleFactor(0.05);
    setSampleBits(6);
    discard();
    std::fill_n(mask_flags, 0x100, 0);
    srARGB empty_color;
    empty_color.blue = empty_color.green = empty_color.red = empty_color.alpha = 0;
    std::fill_n(mask_colors, 0x100, empty_color);
}

// FUNCTION: SURRENDER 0x10004AF0
w8_long srPalette::Sampler::getColorCount()
{
    return color_count;
}

// FUNCTION: SURRENDER 0x10004B00
w8_long srPalette::Sampler::getOutputPaletteSize()
{
    return output_palette_size;
}

// FUNCTION: SURRENDER 0x10004B10
w8_long srPalette::Sampler::getSampleCount()
{
    return sample_count;
}

// FUNCTION: SURRENDER 0x10004B20
double srPalette::Sampler::getSampleFactor()
{
    return sample_factor;
}

// FUNCTION: SURRENDER 0x10004B30
w8_long srPalette::Sampler::getSampleBits()
{
    return sample_bits;
}

// FUNCTION: SURRENDER 0x10006710
void srPalette::Sampler::setOutputPaletteSize(w8_long size)
{
    if (size < 1) {
        size = 1;
    }
    if (0x100 < size) {
        size = 0x100;
    }
    output_palette_size = size;
}

// FUNCTION: SURRENDER 0x10006740
void srPalette::Sampler::setSampleBits(w8_long bits)
{
    if (bits < 1) {
        bits = 1;
    }
    if (8 < bits) {
        bits = 8;
    }
    sample_bits = bits;
}

// FUNCTION: SURRENDER 0x10006770
void srPalette::Sampler::setSampleFactor(double factor)
{
    if (factor <= 0.0) {
        sample_factor = 0.0;
    } else if (factor >= 1.0) {
        sample_factor = 1.0;
    } else {
        sample_factor = factor;
    }
}

// FUNCTION: SURRENDER 0x100068D0
void srPalette::Sampler::discard()
{
    std::fill_n(buckets, 0x8000, -1);
    colors.clear();
    links.clear();
    color_count = 0;
    sample_count = 0;
    capacity = 0;
}

// FUNCTION: SURRENDER 0x10006630
void srPalette::Sampler::reallocColors(w8_long new_capacity)
{
    srARGB empty_color;
    empty_color.blue = empty_color.green = empty_color.red = empty_color.alpha = 0;
    colors.resize(new_capacity, ColorEntry{empty_color, 0});
    links.resize(new_capacity, -1);
    capacity = new_capacity;
}

// FUNCTION: SURRENDER 0x10006300
void srPalette::Sampler::removeMaskColor(w8_long index)
{
    if ((0 <= index) && (index <= 0xff)) {
        mask_flags[index] = 0;
        mask_colors[index].alpha = 0;
        mask_colors[index].red = 0;
        mask_colors[index].green = 0;
        mask_colors[index].blue = 0;
    }
}

// FUNCTION: SURRENDER 0x10006320
void srPalette::Sampler::setMaskColor(w8_long index, const srARGB& color)
{
    if ((0 <= index) && (index <= 0xff)) {
        mask_flags[index] = 1;
        mask_colors[index] = color;
    }
}

// FUNCTION: SURRENDER 0x100062A0
void srPalette::Sampler::shiftDown(srARGB& color)
{
    if (sample_bits != 8) {
        w8_long shift = 8 - sample_bits;
        color.blue >>= shift;
        color.green >>= shift;
        color.red >>= shift;
        color.alpha >>= shift;
    }
}

// FUNCTION: SURRENDER 0x100062D0
void srPalette::Sampler::shiftUp(srARGB& color)
{
    if (sample_bits != 8) {
        w8_long shift = 8 - sample_bits;
        color.blue <<= shift;
        color.green <<= shift;
        color.red <<= shift;
        color.alpha <<= shift;
    }
}

// FUNCTION: SURRENDER 0x10006530
void srPalette::Sampler::addColor(const srARGB& source, w8_long weight)
{
    if (weight > 0) {
        srARGB color = source;
        sample_count += weight;
        color.alpha = 0xff;
        shiftDown(color);
        w8_ulong packed =
            reinterpret_cast<w8_ulong&>(color); /* reinterpret-ok: packed color dword */
        w8_long bucket = ((packed >> 6) & 0x7c00) + ((packed >> 3) & 0x3e0) + (packed & 0x1f);
        for (w8_long index = buckets[bucket]; index != -1; index = links[index]) {
            if (reinterpret_cast<w8_ulong&>(
                    colors[index].color) == /* reinterpret-ok: packed color dword */
                packed) {
                colors[index].count += weight;
                return;
            }
        }
        if (color_count >= capacity) {
            w8_long new_capacity = capacity * 2;
            if (new_capacity < 0x200) {
                new_capacity = 0x200;
            }
            if ((0 < sample_limit) && (sample_limit < new_capacity)) {
                new_capacity = sample_limit;
            }
            reallocColors(new_capacity);
        }
        if (color_count < capacity) {
            colors[color_count].color = color;
            colors[color_count].count = weight;
            links[color_count] = buckets[bucket];
            buckets[bucket] = color_count;
            ++color_count;
        }
    }
}

// FUNCTION: SURRENDER 0x10006500
void srPalette::Sampler::addColors(const srARGB* colors, w8_long color_count, w8_long weight)
{
    if ((colors != 0) && (color_count > 0)) {
        do {
            addColor(*colors, weight);
            ++colors;
            --color_count;
        } while (color_count != 0);
    }
}

// FUNCTION: SURRENDER 0x10006350
void srPalette::Sampler::addSurfaces(srColorSurfaceIFace** surfaces, w8_long surface_count,
                                     w8_long weight)
{
    if ((surfaces != 0) && (surface_count > 0)) {
        do {
            if (*surfaces != 0) {
                addSurface(**surfaces, weight);
            }
            ++surfaces;
            --surface_count;
        } while (surface_count != 0);
    }
}

// FUNCTION: SURRENDER 0x10006390
void srPalette::Sampler::addSurface(const char* name, w8_long weight)
{
    if ((name != 0) && (*name != '\0')) {
        srSurfaceIOManager::ImportInfo options;
        options.unknown_00 = 0;
        srColorSurfaceIFace* surface = srCore.getSurfaceIOManager()->importSurface(name, options);
        if (surface != 0) {
            addSurface(*surface, weight);
            surface->release();
        }
    }
}

// FUNCTION: SURRENDER 0x100063E0
void srPalette::Sampler::addSurface(srColorSurfaceIFace& surface, w8_long weight)
{
    w8_long width = surface.getWidth();
    w8_long height = surface.getHeight();
    w8_long samples = 1;
    if ((sample_factor > 0.0) &&
        ((samples = (w8_long)(width * height * sample_factor)), samples < 1)) {
        samples = 1;
    }
    if (sample_factor == 1.0) {
        std::vector<srARGB> pixels(width);
        for (w8_long y = 0; y < height; ++y) {
            /* reinterpret-ok: the surface API exchanges packed srARGB rows as dwords */
            surface.getPixelRow(reinterpret_cast<w8_ulong*>(pixels.data()), y, 0, width);
            addColors(pixels.data(), width, weight);
        }
        return;
    }
    for (; samples > 0; --samples) {
        w8_ulong pixel = surface.getPixel(rand() % width, rand() % height);
        addColor(
            reinterpret_cast<srARGB&>(pixel), /* reinterpret-ok: packed pixel value as srARGB */
            weight);
    }
}

// FUNCTION: SURRENDER 0x10006010
void srPalette::Sampler::dump(std::ostream& stream)
{
    w8_long flags = srGetStreamFlags(stream);
    srSetStreamFlags(stream, (flags & ~0x180L) | 0x40);
    stream << "Sampling frequency:       " << sample_factor << '\n';
    stream << "Sampling bit depth:       " << sample_bits << '\n';
    stream << "Distinct colors:          " << color_count << '\n';
    stream << "Samples:                  " << sample_count << '\n';
    stream << "Output palette size:      " << output_palette_size << '\n';
    if (output_palette_size != 0) {
        stream << "Color reduction:          " << (double)color_count / output_palette_size << ":1"
               << '\n';
    }
    stream << "Sampler memory usage:     " << (capacity * 0xc + 0x20530) * 0.0009765625 << " kB"
           << '\n';
    srSetStreamFlags(stream, flags & 0x7fff);
}

// FUNCTION: SURRENDER 0x10006190
srPalette* srPalette::Sampler::createOptimalPalette()
{
    Optimizer::PaletteInfo info;
    info.color_count = color_count;
    if ((color_count <= 0) || (info.palette_size = output_palette_size, output_palette_size <= 0)) {
        return 0;
    }
    info.colors = colors.data();
    info.mask_colors = 0;
    info.mask_flags = 0;
    info.mask_count = 0;
    srARGB shifted[0x100];
    w8_long index;
    for (index = 0; index < 0x100; ++index) {
        shifted[index] = mask_colors[index];
        shiftDown(shifted[index]);
    }
    for (index = 0; index < 0x100; ++index) {
        if (mask_flags[index] != 0) {
            info.mask_colors = shifted;
            info.mask_count = output_palette_size;
            info.mask_flags = mask_flags;
            break;
        }
    }
    srPalette* palette = Optimizer::createOptimalPalette(info);
    if ((sample_bits != 8) && (output_palette_size > 0)) {
        for (index = 0; index < output_palette_size; ++index) {
            const srARGB* color = mask_colors + index;
            srARGB widened;
            if (mask_flags[index] == 0) {
                widened = palette->getColor(index);
                shiftUp(widened);
                color = &widened;
            }
            palette->setColor(index, *color);
        }
    }
    return palette;
}

// FUNCTION: SURRENDER 0x10005630
void srPalette::Optimizer::setupLUT(LUT& lut, const srARGB& color)
{
    lut.color = color;
    lut.r = lut.dist_r + (0x100 - color.red);
    lut.g = lut.dist_g + (0x100 - color.green);
    lut.b = lut.dist_b + (0x100 - color.blue);
}

// FUNCTION: SURRENDER 0x10005420
void srPalette::Optimizer::findOptimalColor(Node* node, const LUT& lut)
{
    double distance = 0.0;
    if (lut.color.red < node->bound_lo.red) {
        distance = lut.r[node->bound_lo.red];
    }
    if (node->bound_hi.red < lut.color.red) {
        distance = distance + lut.r[node->bound_hi.red];
    }
    if (lut.color.green < node->bound_lo.green) {
        distance = distance + lut.g[node->bound_lo.green];
    }
    if (node->bound_hi.green < lut.color.green) {
        distance = distance + lut.g[node->bound_hi.green];
    }
    if (lut.color.blue < node->bound_lo.blue) {
        distance = distance + lut.b[node->bound_lo.blue];
    }
    if (node->bound_hi.blue < lut.color.blue) {
        distance = distance + lut.b[node->bound_hi.blue];
    }
    if (distance < node->err_min) {
        node->err_total = 0.0;
        node->err_min = 0.0;
        if (node->leaves == 0) {
            for (w8_long index = 0; index < 8; ++index) {
                if (node->children[index] != 0) {
                    findOptimalColor(node->children[index], lut);
                }
            }
        } else {
            node->color = node->leaves[0].color;
            double best = 0.0;
            for (w8_long index = 0; index < node->leaf_count; ++index) {
                Leaf* leaf = node->leaves + index;
                unsigned char* bytes =
                    (unsigned char*)&leaf->color; /* reinterpret-ok: packed leaf color bytes */
                float error = lut.r[bytes[2]] + lut.g[bytes[1]] + lut.b[bytes[0]];
                if (error < (float)leaf->error) {
                    leaf->error = (double)error;
                }
                if (node->err_min < leaf->error) {
                    node->err_min = leaf->error;
                }
                double contribution = leaf->weight * leaf->error;
                node->err_total = contribution + node->err_total;
                if (best < contribution) {
                    node->color = leaf->color;
                    best = contribution;
                }
            }
        }
    }
    Node* parent = node->parent;
    if (parent != 0) {
        if (parent->err_min < node->err_min) {
            parent->err_min = node->err_min;
        }
        if (parent->err_total < node->err_total) {
            parent->err_total = node->err_total;
            parent->color = node->color;
        }
    }
}

/* Hash entries link 5-5-5 quantized colors inside the two 0x8000-bucket
   tables; leaf bucket nodes then anchor the octree's deepest level. */
// FUNCTION: SURRENDER 0x10005690
srPalette* srPalette::Optimizer::createOptimalPalette(const PaletteInfo& info)
{
    if ((info.colors == 0) || (info.color_count < 1) || (info.palette_size < 1) ||
        ((info.mask_count != 0) && ((info.mask_flags == 0) || (info.mask_colors == 0)))) {
        return 0;
    }

    std::vector<HashEntry*> buckets(0x8000, nullptr);
    std::vector<HashEntry> entries(info.color_count);
    std::vector<HashEntry*> rehash(0x8000, nullptr);
    srARGB empty_color;
    empty_color.blue = empty_color.green = empty_color.red = empty_color.alpha = 0;
    std::vector<srARGB> palette_colors(info.palette_size, empty_color);
    auto lut = std::make_unique<LUT>();

    w8_long distinct = 0;
    HashEntry* entry = entries.data();
    w8_long index;
    for (index = 0; index < info.color_count; ++index) {
        w8_long weight = info.colors[index].count;
        if (weight > 0) {
            srARGB opaque = info.colors[index].color;
            opaque.alpha = 0xff;
            w8_ulong color =
                reinterpret_cast<w8_ulong&>(opaque); /* reinterpret-ok: packed dword */
            w8_ulong bucket =
                ((color & 0x1f0000) >> 6) + ((color & 0x1f00) >> 3) + (color & 0x1f);
            HashEntry* link;
            for (link = buckets[bucket]; link != 0; link = link->next) {
                if (link->color == color) {
                    link->count += weight;
                    goto next_color;
                }
            }
            ++distinct;
            entry->color = color;
            entry->count = info.colors[index].count;
            entry->next = buckets[bucket];
            buckets[bucket] = entry;
            ++entry;
        }
    next_color:;
    }

    w8_long leaf_nodes = 0;
    for (index = 0; index < 0x8000; ++index) {
        HashEntry* link = buckets[index];
        while (link != 0) {
            w8_ulong color = link->color;
            HashEntry* next = link->next;
            w8_ulong bucket = ((color >> 0x13 & 0x1f) * 0x20 + (color >> 0xb & 0x1f)) * 0x20 +
                                   (color >> 3 & 0x1f);
            if (rehash[bucket] == 0) {
                ++leaf_nodes;
            }
            link->next = rehash[bucket];
            rehash[bucket] = link;
            link = next;
        }
    }

    auto leaf_pool = std::make_unique<Node[]>(leaf_nodes);
    std::vector<Leaf> leaves(distinct);
    std::vector<Node*> leaf_map(0x8000, nullptr);

    w8_ulong dominant_color = 0;
    w8_long dominant_weight = 0;
    w8_long used_leaves = 0;
    w8_long used_nodes = 0;
    for (index = 0; index < 0x8000; ++index) {
        HashEntry* link = rehash[index];
        if (link != 0) {
            Node* node = leaf_pool.get() + used_nodes;
            ++used_nodes;
            leaf_map[index] = node;
            node->leaves = leaves.data() + used_leaves;
            w8_ulong bounds_lo = 0xffffffff;
            w8_ulong bounds_hi = 0;
            w8_long count = 0;
            do {
                Leaf* leaf = node->leaves + count;
                leaf->color = link->color;
                leaf->weight = link->count;
                leaf->error = 1e9;
                if (dominant_weight < link->count) {
                    dominant_color = link->color;
                    dominant_weight = link->count;
                }
                for (w8_long byte = 0; byte < 4; ++byte) {
                    unsigned char value =
                        ((const unsigned char*)&link->color)[byte]; /* reinterpret-ok: packed
                                                                       color bytes */
                    if (value < ((unsigned char*)&bounds_lo)[byte]) {
                        ((unsigned char*)&bounds_lo)[byte] =
                            value; /* reinterpret-ok: byte-wise bounds */
                    }
                    if (((unsigned char*)&bounds_hi)[byte] < value) {
                        ((unsigned char*)&bounds_hi)[byte] =
                            value; /* reinterpret-ok: byte-wise bounds */
                    }
                }
                link = link->next;
                ++count;
            } while (link != 0);
            node->err_min = 1e10;
            node->err_total = 1e10;
            node->color = 0;
            node->bound_lo =
                reinterpret_cast<srARGB&>(bounds_lo); /* reinterpret-ok: packed bounds dword */
            node->bound_hi =
                reinterpret_cast<srARGB&>(bounds_hi); /* reinterpret-ok: packed bounds dword */
            node->leaf_count = count;
            used_leaves += count;
        }
    }

    static const w8_ulong level_node_counts[] = {1, 8, 0x40, 0x200, 0x1000, 0x8000};
    std::array<std::unique_ptr<Node[]>, 5> levels;
    for (w8_long level = 4; level >= 0; --level) {
        w8_long dim = 1 << level;
        levels[level] = std::make_unique<Node[]>(level_node_counts[level]);
        Node* nodes = levels[level].get();
        for (w8_long z = 0; z < dim; ++z) {
            for (w8_long y = 0; y < dim; ++y) {
                Node* node = nodes + (z * dim + y) * dim;
                w8_ulong child_base = (z * 2 * dim + y) * dim * 4;
                for (w8_long x = 0; x < dim; ++x, ++node, child_base += 2) {
                    node->bound_lo.alpha = 0xff;
                    node->bound_lo.red = 0xff;
                    node->bound_lo.green = 0xff;
                    node->bound_lo.blue = 0xff;
                    node->bound_hi.alpha = 0;
                    node->bound_hi.red = 0;
                    node->bound_hi.green = 0;
                    node->bound_hi.blue = 0;
                    node->err_min = 1e9;
                    node->err_total = 1e9;
                    node->leaf_count = 0;
                    for (w8_long child = 0; child < 8; ++child) {
                        w8_ulong child_index = child_base;
                        if ((child & 1) != 0) {
                            ++child_index;
                        }
                        if ((child & 2) != 0) {
                            child_index += dim * 2;
                        }
                        if ((child & 4) != 0) {
                            child_index += dim * dim * 4;
                        }
                        Node* link;
                        if (level == 4) {
                            link = leaf_map[child_index];
                        } else {
                            link = levels[level + 1].get() + child_index;
                        }
                        if ((link != 0) && (link->leaf_count == 0)) {
                            link = 0;
                        }
                        node->children[child] = link;
                        if (link != 0) {
                            link->parent = node;
                            for (w8_long byte = 0; byte < 4; ++byte) {
                                unsigned char* child_bytes =
                                    (unsigned char*)&link
                                        ->bound_lo; /* reinterpret-ok: byte bounds merge */
                                unsigned char* node_lo =
                                    (unsigned char*)&node
                                        ->bound_lo; /* reinterpret-ok: byte bounds merge */
                                unsigned char* node_hi =
                                    (unsigned char*)&node
                                        ->bound_hi; /* reinterpret-ok: byte bounds merge */
                                if (child_bytes[byte] < node_lo[byte]) {
                                    node_lo[byte] = child_bytes[byte];
                                }
                                if (child_bytes[byte + 4] > node_hi[byte]) {
                                    node_hi[byte] = child_bytes[byte + 4];
                                }
                            }
                            node->leaf_count += link->leaf_count;
                        }
                    }
                }
            }
        }
    }

    for (index = 0; index < 0x200; ++index) {
        float delta = index - 256.0f;
        delta = delta * delta;
        lut->dist_r[index] = delta * 0.299f;
        lut->dist_g[index] = delta * 0.587f;
        lut->dist_b[index] = delta * 0.114f;
    }

    if (info.mask_count < 1) {
        levels[0][0].color = dominant_color;
    } else {
        w8_long limit = info.palette_size;
        if (info.mask_count < limit) {
            limit = info.mask_count;
        }
        for (index = 0; index < limit; ++index) {
            if (info.mask_flags[index] != 0) {
                palette_colors[index] = info.mask_colors[index];
                setupLUT(*lut, palette_colors[index]);
                findOptimalColor(levels[0].get(), *lut);
            }
        }
    }
    for (index = 0; index < info.palette_size; ++index) {
        if ((info.mask_flags == 0) || (info.mask_count <= index) || (info.mask_flags[index] == 0)) {
            palette_colors[index] = reinterpret_cast<srARGB&>(
                levels[0][0].color); /* reinterpret-ok: packed color dword */
            setupLUT(*lut, palette_colors[index]);
            findOptimalColor(levels[0].get(), *lut);
        }
    }

    return new srPalette(palette_colors.data(), info.palette_size);
}
