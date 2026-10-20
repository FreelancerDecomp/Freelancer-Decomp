#pragma once

#include "portable_integer_conversion.h"
#include <cmath>
#if defined(_WIN32) && defined(_M_IX86)
#include <float.h>
#endif

namespace PortableRpcServerQuantization
{
    static_assert(sizeof(int) == 4 && sizeof(float) == 4 &&
                  std::numeric_limits<float>::is_iec559, "32-bit RPC integers and IEEE floats required");
    inline bool ExtendedPrecision()
    {
#if defined(_WIN32) && defined(_M_IX86)
        unsigned int control;
        _controlfp_s(&control, 0, 0);
        return (control & _MCW_PC) == _PC_64;
#else
        return false; // Other targets use their ordinary binary64 arithmetic.
#endif
    }

    inline int Rounding()
    {
#if defined(_WIN32) && defined(_M_IX86)
        // Read x87 specifically: the caller may have left SSE rounding unchanged.
        unsigned int x87, sse;
        __control87_2(0, 0, &x87, &sse);
        switch (x87 & _MCW_RC)
        {
        case _RC_DOWN: return FE_DOWNWARD;
        case _RC_UP: return FE_UPWARD;
        case _RC_CHOP: return FE_TOWARDZERO;
        default: return FE_TONEAREST;
        }
#else
        return std::fegetround();
#endif
    }

    inline std::uint64_t Chop(std::uint64_t significand, int power, bool negative)
    {
        if (!significand) return 0;
        unsigned bits = 0;
        for (auto remaining = significand; remaining; remaining >>= 1) ++bits;
        if (static_cast<int>(bits) + power > 64 ||
            (static_cast<int>(bits) + power == 64 && (!negative ||
                significand != (UINT64_C(1) << (bits - 1)))))
        {
            std::feraiseexcept(FE_INVALID);
            return UINT64_C(0x8000000000000000);
        }
        std::uint64_t magnitude;
        if (power >= 0) magnitude = significand << power;
        else
        {
            const unsigned shift = static_cast<unsigned>(-power);
            magnitude = shift >= 64 ? 0 : significand >> shift;
            if (shift >= 64 || (significand & ((UINT64_C(1) << shift) - 1)))
                std::feraiseexcept(FE_INEXACT);
        }
        return negative ? UINT64_C(0) - magnitude : magnitude;
    }

    inline std::uint64_t QuantizeComm(const float* input)
    {
        const volatile double loaded = static_cast<double>(*input);
        if (!ExtendedPrecision())
        {
            const volatile double tenth = loaded * static_cast<double>(0.1f);
            const double scaled = tenth * 255.0;
            return PortableIntegerConversion::ToInt64Bits(&scaled);
        }
        std::uint32_t bits;
        std::memcpy(&bits, input, sizeof(bits));
        const unsigned exponent = (bits >> 23) & 0xff;
        if (exponent == 0xff)
        {
            std::feraiseexcept(FE_INVALID);
            return UINT64_C(0x8000000000000000);
        }
        const auto mantissa = (bits & 0x7fffff) | (exponent ? 0x800000u : 0);
        // Both original products are exact at PC64: 24 + 24 + 8 bits.
        const std::uint64_t product = static_cast<std::uint64_t>(mantissa) * 0xcccccd * 255;
        return Chop(product, static_cast<int>(exponent ? exponent : 1) - 177, (bits >> 31) != 0);
    }

    inline int ExpandHull(unsigned char encoded, int maximum)
    {
        if (ExtendedPrecision())
        {
            // The binary64 1/255 constant lies just below the exact reciprocal.
            // At PC64 its first product fits exactly. For int32 maxima, final
            // rounding cannot cross a noninteger boundary; exact multiples
            // remain just below their mathematical integer before FISTP.
            const std::int64_t product = static_cast<std::int64_t>(maximum) * encoded;
            if (!product) return 0;
            std::feraiseexcept(FE_INEXACT);
            auto result = product / 255;
            if (product % 255 == 0) result += product < 0 ? 1 : -1;
            return static_cast<int>(result);
        }
        const volatile double fraction = static_cast<double>(encoded) * (1.0 / 255.0);
        const double expanded = fraction * static_cast<double>(maximum);
        const auto word = static_cast<std::uint32_t>(PortableIntegerConversion::ToInt64Bits(&expanded));
        int result;
        static_assert(sizeof(result) == sizeof(word), "32-bit hull value required");
        std::memcpy(&result, &word, sizeof(result));
        return result;
    }

    inline float PrepareHull(int hitPoints)
    {
        const volatile float rounded = static_cast<float>(hitPoints);
        return rounded;
    }

    inline bool RoundUp(std::uint64_t remainder, std::uint64_t divisor,
                        std::uint64_t mantissa, bool negative, int rounding)
    {
        if (!remainder) return false;
        if (rounding == FE_UPWARD) return !negative;
        if (rounding == FE_DOWNWARD) return negative;
        if (rounding == FE_TOWARDZERO) return false;
        return remainder > divisor / 2 ||
            (!(divisor & 1) && remainder == divisor / 2 && (mantissa & 1));
    }

    inline unsigned char QuantizeHull(float points, int maximum)
    {
        // points must be the integer-valued float returned by PrepareHull.
        if (!ExtendedPrecision())
        {
            const volatile double fraction = static_cast<double>(points) / static_cast<double>(maximum);
            const double scaled = fraction * 255.0;
            return static_cast<unsigned char>(PortableIntegerConversion::ToInt64Bits(&scaled));
        }
        if (!maximum)
        {
            std::feraiseexcept(points == 0 ? FE_INVALID : FE_DIVBYZERO | FE_INVALID);
            return 0;
        }
        if (points == 0) return 0;
        const bool negative = std::signbit(points) != (maximum < 0);
        const auto numerator = static_cast<std::uint64_t>(std::fabs(static_cast<double>(points)));
        const auto denominator = static_cast<std::uint64_t>(maximum < 0 ? -static_cast<std::int64_t>(maximum) : maximum);
        int exponent = 0;
        if (numerator >= denominator)
            while ((denominator << (exponent + 1)) <= numerator) ++exponent;
        else
            while ((numerator << -exponent) < denominator) --exponent;
        std::uint64_t mantissa = numerator / denominator, remainder = numerator % denominator;
        for (int bit = 0; bit < 63 - exponent; ++bit)
        {
            remainder *= 2;
            mantissa = (mantissa << 1) | (remainder >= denominator);
            if (remainder >= denominator) remainder -= denominator;
        }
        const int rounding = Rounding();
        if (remainder) std::feraiseexcept(FE_INEXACT);
        if (RoundUp(remainder, denominator, mantissa, negative, rounding) && ++mantissa == 0)
        {
            mantissa = UINT64_C(1) << 63;
            ++exponent;
        }
        // Exact 64-by-8 multiplication, split into two portable 32-bit limbs.
        const auto lowProduct = (mantissa & 0xffffffffu) * 255;
        const auto highProduct = (mantissa >> 32) * 255;
        const auto low = lowProduct + (highProduct << 32);
        const auto high = (highProduct >> 32) + (low < lowProduct);
        const unsigned shift = high >= 128 ? 8 : 7;
        mantissa = (high << (64 - shift)) | (low >> shift);
        const auto discarded = low & ((UINT64_C(1) << shift) - 1);
        if (discarded) std::feraiseexcept(FE_INEXACT);
        exponent += shift;
        if (RoundUp(discarded, UINT64_C(1) << shift, mantissa, negative, rounding) && ++mantissa == 0)
        {
            mantissa = UINT64_C(1) << 63;
            ++exponent;
        }
        return static_cast<unsigned char>(Chop(mantissa, exponent - 63, negative));
    }
}
