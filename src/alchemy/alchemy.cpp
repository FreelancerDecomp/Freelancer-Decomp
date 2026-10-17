// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "dacom.h"

#include "alchemy_types.h"
#include "alchemy_common.h"
#include "alchemy_alchemy.h"
#include "alchemy_fxruntime.h"
#include "alchemy_effectarchetype.h"
#include "alchemy_effectobject.h"
#include "alchemy_effectlibrary.h"
#include "alchemy_genericfx.h"
#include "alchemy_ramp.h"
#include "alchemy_factory.h"

// FUN_06203b60: CPU selection precedes ordered factory/INI/pool startup.

BOOL WINAPI DllMain(HINSTANCE Instance, DWORD Reason, LPVOID /*Reserved*/)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Instance);
        AlchemyImpl::SelectAlchemyMathImplementation();
        AlchemyImpl::InitializeAlchemyModule();
    }
    else if (Reason == DLL_PROCESS_DETACH)
    {
        AlchemyImpl::FreeAllAppearancePools();
    }
    return TRUE;
}
