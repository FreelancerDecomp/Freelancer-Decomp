// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "physics_tick_profile.h"
#if FREELANCER_ENABLE_TICK_PROFILE
#include <windows.h>

namespace
{
    thread_local FPhysicsTickProfile* G_PhysicsTickProfile = nullptr;

    int64_t ReadPhysicsProfileTicks()
    {
        LARGE_INTEGER Ticks;
        QueryPerformanceCounter(&Ticks);
        return Ticks.QuadPart;
    }
}

FPhysicsTickProfile* __cdecl PhySys::SetTickProfile(FPhysicsTickProfile* Profile)
{
    FPhysicsTickProfile* const Previous = G_PhysicsTickProfile;
    G_PhysicsTickProfile = Profile;
    return Previous;
}

FPhysicsTickScope::FPhysicsTickScope(EPhysicsTickSection InSection)
    : Profile(G_PhysicsTickProfile), Section(InSection), Start(0)
{
    if (Profile != nullptr)
        Start = ReadPhysicsProfileTicks();
}

FPhysicsTickScope::~FPhysicsTickScope()
{
    Stop();
}

void FPhysicsTickScope::Stop()
{
    if (Profile == nullptr)
        return;
    Profile->Ticks[static_cast<int>(Section)] += ReadPhysicsProfileTicks() - Start;
    Profile = nullptr;
}

#else
FPhysicsTickProfile* __cdecl PhySys::SetTickProfile(FPhysicsTickProfile*)
{
    return nullptr;
}
#endif
