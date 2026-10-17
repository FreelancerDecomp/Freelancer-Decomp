// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ThornModernAdapters.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "dacom.h"

#include <cstring>
#include <limits>
#include <cstdarg>
#include <cstdio>

char* FThornModernAdapters::FormatDiagnostic(char* Destination, const char* Format, ...)
{
    va_list Arguments;
    va_start(Arguments, Format);
    _vsnprintf(Destination, 8192, Format, Arguments);
    va_end(Arguments);
    return Destination;
}

void FThornModernAdapters::ReleaseOwnedComponent(IDAComponent*& Component)
{
    // FUN_06f4a910: TEST owned slot, dispatch vtable slot +8 (Release), then
    // store null.  Clearing after Release preserves the retail observable
    // state even when the interface implementation observes its final call.
    if (Component != nullptr)
    {
        Component->Release();
        Component = nullptr;
    }
}

bool FThornModernAdapters::MeasureSourceLength(const char* Source, int& Length)
{
    Length = 0;
    if (Source == nullptr)
        return false;

    const std::size_t Size = std::strlen(Source);
    if (Size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return false;
    Length = static_cast<int>(Size);
    return true;
}

extern "C" int ThornModernMeasureSourceLength(const char* Source, int* Length)
{
    if (Length == nullptr)
        return 0;
    return FThornModernAdapters::MeasureSourceLength(Source, *Length) ? 1 : 0;
}

bool FThornModernAdapters::IsLuaTableTag(int TypeTag)
{
    return TypeTag == -3;
}

extern "C" int ThornModernIsLuaTableTag(int TypeTag)
{
    return FThornModernAdapters::IsLuaTableTag(TypeTag) ? 1 : 0;
}

int FThornModernAdapters::NormalizeVarargCount(int RequestedCount)
{
    return RequestedCount < 0 ? 0 : RequestedCount;
}

extern "C" int ThornModernNormalizeVarargCount(int RequestedCount)
{
    return FThornModernAdapters::NormalizeVarargCount(RequestedCount);
}

const char* FThornModernAdapters::GetArithmeticDiagnostic()
{
    return "unexpected type in arithmetic operation";
}

extern "C" char* ThornModernGetArithmeticDiagnostic()
{
    return const_cast<char*>(FThornModernAdapters::GetArithmeticDiagnostic());
}
