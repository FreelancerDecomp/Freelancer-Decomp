// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_tick_profile.h"

// Private to Common.dll (including its IVP translation units).
#if FREELANCER_ENABLE_TICK_PROFILE
class FPhysicsTickScope
{
public:
    explicit FPhysicsTickScope(EPhysicsTickSection InSection);
    ~FPhysicsTickScope();
    void Stop();
    FPhysicsTickScope(FPhysicsTickScope const&) = delete;
    FPhysicsTickScope& operator=(FPhysicsTickScope const&) = delete;

private:
    FPhysicsTickProfile* Profile;
    EPhysicsTickSection Section;
    int64_t Start;
};

#else
class FPhysicsTickScope
{
public:
    explicit FPhysicsTickScope(EPhysicsTickSection) {}
    void Stop() {}
    FPhysicsTickScope(FPhysicsTickScope const&) = delete;
    FPhysicsTickScope& operator=(FPhysicsTickScope const&) = delete;
};
#endif
