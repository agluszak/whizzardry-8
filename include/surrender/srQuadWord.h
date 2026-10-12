#pragma once


#include <cstdint>
/* SurRender's 64-bit value type: two 32-bit words. */
class srQuadWord {
public:
    std::uint32_t lo; /* 0x00 */
    std::uint32_t hi; /* 0x04 */

    /* The timer bodies store ftol results straight into a quad word; this is
       the low/high word store that lowers to. */
    srQuadWord& operator=(std::uint64_t value)
    {
        lo = static_cast<std::uint32_t>(value);
        hi = static_cast<std::uint32_t>(value >> 0x20);
        return *this;
    }

    operator double() const
    {
        /* The word pair is only four-byte aligned, including in retail
           srTimer. */
        const std::uint64_t bits = (static_cast<std::uint64_t>(hi) << 32) | lo;
        return static_cast<std::uint32_t>(bits >> 32) * 4294967296.0 +
               static_cast<std::uint32_t>(bits);
    }

    /* The retail srTimer bodies subtracted quad words with an explicit borrow
       out of the low half; that is what this lowers to. */
    srQuadWord operator-(const srQuadWord& other) const
    {
        srQuadWord result;
        result.lo = lo - other.lo;
        result.hi = hi - other.hi - (lo < other.lo);
        return result;
    }

    srQuadWord& operator+=(const srQuadWord& other)
    {
        std::uint32_t carry = lo;
        lo += other.lo;
        hi += other.hi + (lo < carry);
        return *this;
    }

    /* Retail scales each word separately: hi * (2^32 * factor) + lo * factor
       (0x10060F90 multiplies by 4294.967296 and 1e-6; calibrate by 42949672.96
       and 0.01), which rounds differently from converting first. */
    double operator*(double factor) const
    {
        return hi * (4294967296.0 * factor) + lo * factor;
    }

    srQuadWord operator*(std::uint32_t factor) const
    {
        std::uint64_t product = (((std::uint64_t)hi << 32) | lo) * factor;
        srQuadWord result;
        result.lo = static_cast<std::uint32_t>(product);
        result.hi = static_cast<std::uint32_t>(product >> 32);
        return result;
    }

    srQuadWord operator/(const srQuadWord& divisor) const
    {
        std::uint64_t dividend = ((std::uint64_t)hi << 32) | lo;
        std::uint64_t divisor64 = ((std::uint64_t)divisor.hi << 32) | divisor.lo;
        std::uint64_t quotient = dividend / divisor64;
        srQuadWord result;
        result.lo = static_cast<std::uint32_t>(quotient);
        result.hi = static_cast<std::uint32_t>(quotient >> 32);
        return result;
    }
};

