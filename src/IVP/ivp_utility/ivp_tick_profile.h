// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_tick_profile.h"

// Private to Common.dll: the physics tick sections that IVP (ivp_environment.cpp) and Common
// (common_physicalsystem.cpp) time; common_tick_profile.cpp implements it. Formerly
// src/Common/physics_tick_profile.h, which IVP reached with "../../Common/".
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
