#pragma once

#include <limits>
extern "C"
{
#include "lua32/include/lua.h"
}

namespace ThornLuaDetail
{
    // All unsigned 32-bit indices are exactly representable in double.
    // Retain the predicate/callback ordering and read the fallback on failure.
    inline lua_Object GetLightDoubleProbe(lua_Object table, unsigned int index, const lua_Object& fallback)
    {
        static_assert(std::numeric_limits<unsigned int>::digits == 32 &&
                      std::numeric_limits<double>::digits >= 32, "Exact Lua index conversion");
        if (lua_istable(table) == 0)
            return fallback;
        lua_pushobject(table);
        // A direct unsigned-int cast lets MSVC /O2 use a corrective FADD for
        // the high bit, which rounds at x87 PC24. Materialize a signed widened
        // operand so MSVC cannot narrow the conversion back to that sequence.
        const volatile long long integerIndex = index;
        lua_pushnumber(static_cast<double>(integerIndex));
        return lua_gettable();
    }
}
