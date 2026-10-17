// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// DataConfig -- defined in common_dataconfig.cpp.
namespace Archetype
{
    struct ICliObjFactory;
}

// Common.dll.c DataConfig::startup @ 0xa8e60 / shutdown @ 0xa9850
// ?startup@DataConfig@@YAHPBDPAUICliObjFactory@Archetype@@PAUProgressCB@1@@Z
// ?shutdown@DataConfig@@YAXXZ
// ProgressCB is nested in DataConfig (mangle @1). ICliObjFactory / ICliObj nested in Archetype.
namespace DataConfig
{
    // RTTI .?AUProgressCB@DataConfig@@. startup (Common.dll 0x06308f38..0x06308f52) calls
    // slot 0, thiscall, with each [Data] value string when the pointer is non-null; a nonzero
    // EAX aborts the scan and startup returns 1. The only retail implementation is
    // Freelancer.exe's DataConfigPump (vftable 0x005e6ab8 = {LAB_005b1fe0}, one slot).
    struct ProgressCB
    {
        virtual int Slot00(char const* ValueString) = 0; // +0x00
    };

    COMMON_API int __cdecl startup(char const* IniPath, Archetype::ICliObjFactory* Factory,
                        ProgressCB* Progress);
    COMMON_API void __cdecl shutdown();
}
