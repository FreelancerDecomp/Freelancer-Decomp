#pragma once

#include "portable_integer_conversion.h"

namespace PortableColorQuantization
{
    inline std::uint64_t ToInt64Bits(const float* value)
    {
        // Unlike RPC encoders, this material path chops during multiplication
        // too. Restore the complete caller environment, retaining new exceptions.
        std::fenv_t saved;
        std::fegetenv(&saved);
        std::fesetround(FE_TOWARDZERO);
        const double scaled = static_cast<double>(*value) * 255.0;
        const std::uint64_t result = PortableIntegerConversion::ToInt64Bits(&scaled);
        std::feupdateenv(&saved);
        // MSVC's FE_ALL_EXCEPT omits x87's denormal-input flag. Replaying the
        // input load retains that flag after restoring the saved environment.
        const volatile double restoredInput = static_cast<double>(*value);
        (void)restoredInput;
        return result;
    }
}
