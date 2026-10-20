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

    inline void ConstructHermiteRecord(FHermiteKnot* destination, const FHermiteKnot* source)
    {
        if (!destination) return;
        auto* dst = reinterpret_cast<unsigned char*>(destination);
        const auto* src = reinterpret_cast<const unsigned char*>(source);
        for (unsigned offset = 0; offset < 8; offset += 4)
        {
            std::uint32_t word;
            std::memcpy(&word, src + offset, sizeof(word));
            std::memcpy(dst + offset, &word, sizeof(word));
        }
        // Snapshot both numerical inputs before either output can overwrite one.
        const volatile double out = static_cast<double>(source->MOut);
        const volatile double in = static_cast<double>(source->MIn);
        destination->MIn = static_cast<float>(in);
        destination->MOut = static_cast<float>(out);
    }

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
