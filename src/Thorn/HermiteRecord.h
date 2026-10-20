#pragma once

#include <cstdint>
#include <cstring>

namespace ThornCurveDetail
{
    struct FHermiteKnot
    {
        float Value;
        float T;
        float MIn;
        float MOut;
    };
    static_assert(sizeof(FHermiteKnot) == 16, "Four binary32 Hermite fields");

    // Four forward word reads/stores, not a whole-record snapshot or an FP
    // assignment. Preserve overlapping copies and signaling NaN payloads.
    inline void AssignHermiteRecord(FHermiteKnot* destination, const FHermiteKnot* source)
    {
        auto* dst = reinterpret_cast<unsigned char*>(destination);
        const auto* src = reinterpret_cast<const unsigned char*>(source);
        for (unsigned offset = 0; offset < sizeof(FHermiteKnot); offset += sizeof(std::uint32_t))
        {
            std::uint32_t word;
            std::memcpy(&word, src + offset, sizeof(word));
            std::memcpy(dst + offset, &word, sizeof(word));
        }
    }
}
