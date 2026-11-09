#pragma once

#include <cfenv>
#include <cstring>
#include <limits>

namespace PortableRpcUpdateFields
{
    static_assert(sizeof(float) == 4 && sizeof(double) == 8 &&
                  std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559,
                  "RPC update fields require IEEE binary32/binary64");
    inline void NarrowTimestamp(void* destination, const void* source)
    {
        std::fenv_t saved;
        std::fegetenv(&saved);
        double input;
        std::memcpy(&input, source, sizeof(input));
        const volatile double loaded = input;
        const float result = static_cast<float>(loaded);
        std::memcpy(destination, &result, sizeof(result));
        // MSVC's standard exception merge filters the nonstandard x87 denormal
        // flag added by Clang reloading a narrowed subnormal result. Replay the
        // original input load to retain genuine denormal-input effects.
        std::feupdateenv(&saved);
        const volatile double replay = *static_cast<volatile double*>(&input);
        (void)replay;
    }

    inline void CopyReceived(void* timeDestination, void* fieldDestination,
                             const void* timeSource, const void* fieldSource)
    {
        float timeInput, fieldInput;
        std::memcpy(&timeInput, timeSource, sizeof(timeInput));
        const volatile double time = static_cast<double>(timeInput);
        std::memcpy(&fieldInput, fieldSource, sizeof(fieldInput));
        const volatile double field = static_cast<double>(fieldInput);
        const float timeResult = static_cast<float>(time);
        const float fieldResult = static_cast<float>(field);
        // Both numerical loads precede either output store, as in FLD/FST/FLD/FST.
        std::memcpy(timeDestination, &timeResult, sizeof(timeResult));
        std::memcpy(fieldDestination, &fieldResult, sizeof(fieldResult));
    }
}
