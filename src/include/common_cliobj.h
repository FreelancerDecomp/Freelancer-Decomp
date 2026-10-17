// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// ICliObjFactory — the cross-DLL contract between freelancer.exe and Common.dll.
// Common.dll's DataConfig::startup / Archetype::Load* receive a factory pointer and call
// vtable[0](TypeId, ICliObj** Out) for every archetype that gets a client companion; the
// server passes null. The factory itself, its ICliObj companions and the MaterialLib cache
// are freelancer.exe code in retail (DAT_006164a0 / FUN_0040ca50, Client\cmn\MaterialLib.cpp)
// and live in src/freelancer/Client/client_cliobj.{h,cpp}; Common.dll exports none of them.

#include "common_archetype.h"

namespace Archetype
{
    // Retail: thiscall vtable[0](TypeId, ICliObj** Out). Server passes null.
    struct ICliObjFactory
    {
        virtual void Create(int TypeId, ICliObj** Out) = 0;
    };
}
