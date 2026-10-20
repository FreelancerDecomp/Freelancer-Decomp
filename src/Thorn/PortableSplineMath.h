#pragma once

#include <cfenv>
#include <cstdint>
#include <cstring>

namespace ThornPortableSplineMath
{
    inline void ScaleStored(float* dst, const float* src, const float* factorInput)
    {
        std::uint32_t factorBits;
        std::memcpy(&factorBits, factorInput, sizeof(factorBits));
        const bool factorNaN = (factorBits & 0x7fffffffu) > 0x7f800000u;
        const bool factorSignaling = factorNaN && !(factorBits & 0x00400000u);
        for (int index = 2; index >= 0; --index)
        {
            std::uint32_t bits;
            std::memcpy(&bits, src + index, sizeof(bits));
            const volatile double loaded = static_cast<double>(src[index]);
            const bool sourceNaN = (bits & 0x7fffffffu) > 0x7f800000u;
            if (sourceNaN || factorNaN)
            {
                if (factorSignaling || (sourceNaN && !(bits & 0x00400000u)))
                    std::feraiseexcept(FE_INVALID);
                std::uint32_t chosen = factorBits | 0x00400000u;
                if (sourceNaN)
                {
                    chosen = bits | 0x00400000u;
                    // FLD has quieted the source before FMUL sees the factor.
                    // A signaling memory NaN loses to that quiet source NaN;
                    // two quiet NaNs select larger payload, positive on a tie.
                    if (factorNaN && !factorSignaling)
                    {
                        const auto sourceMagnitude = chosen & 0x7fffffffu;
                        const auto factorMagnitude = factorBits & 0x7fffffffu;
                        if (sourceMagnitude < factorMagnitude ||
                            (sourceMagnitude == factorMagnitude && factorBits < chosen))
                            chosen = factorBits;
                    }
                }
                std::memcpy(dst + index, &chosen, sizeof(chosen));
            }
            else if (((bits & 0x7fffffffu) == 0x7f800000u && (factorBits & 0x7fffffffu) == 0) ||
                     ((factorBits & 0x7fffffffu) == 0x7f800000u && (bits & 0x7fffffffu) == 0))
            {
                std::feraiseexcept(FE_INVALID);
                const std::uint32_t indefinite = 0xffc00000u;
                std::memcpy(dst + index, &indefinite, sizeof(indefinite));
            }
            else
                // Keep this read inside the finite/ordinary branch. A speculative
                // factor load would add a denormal flag when a source NaN wins.
                dst[index] = static_cast<float>(loaded) *
                    *static_cast<const volatile float*>(factorInput);
        }
    }

    inline void NegateVector(float* dst, const float* src)
    {
        // Read Z/Y/X before publishing X/Y/Z, including for overlapping ranges.
        const volatile double z = static_cast<double>(src[2]);
        const volatile double y = static_cast<double>(src[1]);
        const volatile double x = static_cast<double>(src[0]);
        dst[0] = static_cast<float>(-x);
        dst[1] = static_cast<float>(-y);
        dst[2] = static_cast<float>(-z);
    }

    inline void DoubleStored(float* dst, const float* src)
    {
        // Keep float additions and the original Z/Y/X publication order.
        // Widening and narrowing through a temporary can add an x87 waiting
        // load, altering C1 and when an unmasked overflow is delivered.
        dst[2] = src[2] + src[2];
        dst[1] = src[1] + src[1];
        dst[0] = src[0] + src[0];
    }

    inline void NegateStored(float* dst, const float* src)
    {
        // Widen before negating: a raw sign-bit XOR would fail to quiet an
        // input signaling NaN and would skip the original floating-point load.
        // Materialize each load in Z/Y/X order, including overlapping outputs.
        volatile double z = static_cast<double>(src[2]);
        dst[2] = static_cast<float>(-z);
        volatile double y = static_cast<double>(src[1]);
        dst[1] = static_cast<float>(-y);
        volatile double x = static_cast<double>(src[0]);
        dst[0] = static_cast<float>(-x);
    }
}
