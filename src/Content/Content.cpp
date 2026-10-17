// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ContentInternal.h"
#include "ContentStaticIds.h" // TU26 static IDs 06eeea30..06eeec10
#include "ContentDefaultId.h" // TU26 06eeec30

#include <cstdio>
#include <cstring>
#include <string.h>

HMODULE G_ContentModule = nullptr;

// Retail DAT_06fce76c.
unsigned int GContentNextUniqueId = 1;

namespace
{
    // Retail DAT_06fce368 starts at 01h and stores the inverse of GCS clamp.
    unsigned char G_bContentGcsClampDisabled = 1;
}

void __cdecl ContentSetGcsClampEnabled(unsigned char EnabledByte)
{
    // FUN_06eeba10: TEST AL,AL / SETZ AL / MOV DAT_06fce368,AL.
    G_bContentGcsClampDisabled = EnabledByte == 0 ? 1 : 0;
}

bool __cdecl ContentIsGcsClampEnabled()
{
    // FUN_06eeba20: TEST CL,CL / SETZ AL.
    return G_bContentGcsClampDisabled == 0;
}

void __cdecl SpacePopSetGcsClampEnabled(unsigned char EnabledByte)
{
    // FUN_06f5cf60 is a tail jump to FUN_06eeba10.
    ContentSetGcsClampEnabled(EnabledByte);
}

I3DMathEngine* __cdecl ContentAcquire3DMathEngine()
{
    // FUN_06fa8fa0: 0xC descriptor, DACOM CreateInstance slot +0x0C.
    FMathDesc Desc = { sizeof(FMathDesc), "3DMathEngine", nullptr };
    void* Instance = nullptr;
    ICOManager* Manager = DACOM_Acquire();
    Manager->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), &Instance);
    return static_cast<I3DMathEngine*>(Instance);
}

IDLL::IDLL()
{
}

IDLL::IDLL(IDLL const& /*Other*/)
{
}

IDLL& IDLL::operator=(IDLL const& /*Other*/)
{
    return *this;
}

IDLL::~IDLL()
{
}

namespace
{
    typedef IDLL* (*PCreateFactory)(char const* Name, unsigned int ControllerId,
                                    void const* Parms);

    struct FFactoryEntry
    {
        char const* Name;
        PCreateFactory Create;
    };

    // FUN_06eeec50: stricmp chain in this order; each name has its own operator new
    // size (GameSupport 8, SpaceSupport 4, BaseSupport 8, DestroyMission 0x29c,
    // SpacePop 8, BasePop 0x1c, StoryLauncher 8, Story 0x3c, Mission_01a 0x440,
    // Mission_01b 0x330, Mission_02..13 and GenericMission 0x32c) and its own ctor.
    // A failed new or an unknown name returns 0; a match returns right after its ctor
    // (e.g. 06eeec8c-06eeec91), so first-match-wins is the retail order.
    FFactoryEntry const G_Factories[] = {
        { "GameSupport", &CreateGameSupport },
        { "SpaceSupport", &CreateSpaceSupport },
        { "BaseSupport", &CreateBaseSupport },
        { "DestroyMission", &CreateDestroyMission },
        { "SpacePop", &CreateSpacePop },
        { "BasePop", &CreateBasePop },
        { "StoryLauncher", &CreateStoryLauncher },
        { "Story", &CreateStory },
        { "Mission_01a", &CreateMission01a },
        { "Mission_01b", &CreateMission01b },
        { "Mission_02", &CreateMission02 },
        { "Mission_03", &CreateMission03 },
        { "Mission_04", &CreateMission04 },
        { "Mission_05", &CreateMission05 },
        { "Mission_06", &CreateMission06 },
        { "Mission_07", &CreateMission07 },
        { "Mission_08", &CreateMission08 },
        { "Mission_09", &CreateMission09 },
        { "Mission_10", &CreateMission10 },
        { "Mission_11", &CreateMission11 },
        { "Mission_12", &CreateMission12 },
        { "Mission_13", &CreateMission13 },
        { "GenericMission", &CreateGenericMission },
    };

    int const G_FactoryCount = sizeof(G_Factories) / sizeof(G_Factories[0]);
    static_assert(G_FactoryCount == 23, "retail CreateInstance has 23 names");
}

IDLL* __cdecl CreateInstance(unsigned int ControllerId, char const* Name, void const* Parms)
{
    for (int Index = 0; Index < G_FactoryCount; ++Index)
    {
        if (_stricmp(Name, G_Factories[Index].Name) == 0)
        {
            return G_Factories[Index].Create(G_Factories[Index].Name, ControllerId, Parms);
        }
    }

    return nullptr;
}

void __cdecl DestroyInstance(IDLL* Instance)
{
    if (Instance != nullptr)
        delete Instance;
}

BOOL WINAPI DllMain(HINSTANCE Instance, DWORD Reason, LPVOID /*Reserved*/)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Instance);
        G_ContentModule = Instance;
        return TRUE;
    }

    if (Reason == DLL_PROCESS_DETACH)
    {
        G_ContentModule = nullptr;
        return TRUE;
    }

    return TRUE;
}
