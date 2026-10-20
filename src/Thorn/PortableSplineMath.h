#pragma once

#include <cfenv>
#include <cstdint>
#include <cstring>

namespace ThornPortableSplineMath
{
    // The left operand has already passed through a numerical load. Preserve
    // x87's NaN selection against the still-raw right memory operand.
    inline bool PropagateLoadedNaN(std::uint32_t left, std::uint32_t right,
                                   std::uint32_t* result)
    {
        const bool leftNaN = (left & 0x7fffffffu) > 0x7f800000u;
        const bool rightNaN = (right & 0x7fffffffu) > 0x7f800000u;
        if (!leftNaN && !rightNaN) return false;
        const bool rightSignaling = rightNaN && !(right & 0x00400000u);
        if (rightSignaling || (leftNaN && !(left & 0x00400000u)))
            std::feraiseexcept(FE_INVALID);
        *result = right | 0x00400000u;
        if (leftNaN)
        {
            *result = left | 0x00400000u;
            if (rightNaN && !rightSignaling)
            {
                const auto a = *result & 0x7fffffffu;
                const auto b = right & 0x7fffffffu;
                if (a < b || (a == b && right < *result)) *result = right;
            }
        }
        return true;
    }

    inline void ScaleStored(float* dst, const float* src, const float* factorInput)
    {
        std::uint32_t factorBits;
        std::memcpy(&factorBits, factorInput, sizeof(factorBits));
        for (int index = 2; index >= 0; --index)
        {
            std::uint32_t bits;
            std::memcpy(&bits, src + index, sizeof(bits));
            const volatile double loaded = static_cast<double>(src[index]);
            std::uint32_t chosen;
            if (PropagateLoadedNaN(bits, factorBits, &chosen))
            {
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

    template<bool Subtract> inline void BinaryStored(float* dst, const float* left, const float* right)
    {
        for (int index = 2; index >= 0; --index)
        {
            std::uint32_t a, b, result;
            std::memcpy(&a, left + index, sizeof(a));
            std::memcpy(&b, right + index, sizeof(b));
            const volatile double loaded = static_cast<double>(left[index]);
            if (PropagateLoadedNaN(a, b, &result))
                std::memcpy(dst + index, &result, sizeof(result));
            else if ((a & 0x7fffffffu) == 0x7f800000u && (b & 0x7fffffffu) == 0x7f800000u &&
                     (((a ^ b) & 0x80000000u) != 0) != Subtract)
            {
                std::feraiseexcept(FE_INVALID);
                result = 0xffc00000u;
                std::memcpy(dst + index, &result, sizeof(result));
            }
            else if constexpr (Subtract)
                dst[index] = static_cast<float>(loaded) - *static_cast<const volatile float*>(right + index);
            else
                dst[index] = static_cast<float>(loaded) + *static_cast<const volatile float*>(right + index);
        }
    }

    inline void AddStored(float* dst, const float* left, const float* right)
    {
        BinaryStored<false>(dst, left, right);
    }

    inline void SubtractStored(float* dst, const float* left, const float* right)
    {
        BinaryStored<true>(dst, left, right);
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
