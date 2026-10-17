// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <stdint.h>
#include "common_tick_profile.h"

// On-screen "Beta 2" version label (CLIENT-BETA-LABEL). Off by default; set through the
// FreelancerBetaLabel property (src/Freelancer.Diagnostics.props).
#ifndef FREELANCER_BETA_LABEL
#define FREELANCER_BETA_LABEL 0
#endif

// Port-only diagnostics requested by Giperion (CLIENT-BETA-LABEL).
// Inclusive wall-clock times on the client thread; children are part of their parents.
enum class EClientTickSection
{
    Tick,
    Ui,
    Server,
    Game,
    Objects,
    Physics,
    PhysicsStep,
    PhysicsSimulate,
    PhysicsPsiPrepare,
    PhysicsSimUnits,
    PhysicsCommitHulls,
    PhysicsContacts,
    PhysicsSync,
    MoveBeams,
    BeamHits,
    HitDispatch,
    WorldFx,
    Sound,
    Render,
    Present,
    Count
};

#if FREELANCER_ENABLE_TICK_PROFILE
class FClientTickProfileFrame
{
public:
    FClientTickProfileFrame();
    ~FClientTickProfileFrame();
    FClientTickProfileFrame(FClientTickProfileFrame const&) = delete;
    FClientTickProfileFrame& operator=(FClientTickProfileFrame const&) = delete;

private:
    int64_t Start;
    bool bOwnsFrame;
};

class FClientTickScope
{
public:
    explicit FClientTickScope(EClientTickSection InSection);
    ~FClientTickScope();
    void Stop();
    FClientTickScope(FClientTickScope const&) = delete;
    FClientTickScope& operator=(FClientTickScope const&) = delete;

private:
    EClientTickSection Section;
    int64_t Start;
    bool bRunning;
};

void BeginClientProfileRender();
void EndClientProfileRender();
#else
class FClientTickProfileFrame
{
public:
    FClientTickProfileFrame() = default;
    FClientTickProfileFrame(FClientTickProfileFrame const&) = delete;
    FClientTickProfileFrame& operator=(FClientTickProfileFrame const&) = delete;
};

class FClientTickScope
{
public:
    explicit FClientTickScope(EClientTickSection) {}
    void Stop() {}
    FClientTickScope(FClientTickScope const&) = delete;
    FClientTickScope& operator=(FClientTickScope const&) = delete;
};

inline void BeginClientProfileRender() {}
inline void EndClientProfileRender() {}
#endif

void DrawClientTickProfile();
// Must run before FontShutdown: cached strings own atlas pages and refer to fonts.
void ShutdownClientTickProfile();

// Arms Common's per-thread sink only for the client Physics / hits block.
#if FREELANCER_ENABLE_TICK_PROFILE
class FClientPhysicsProfileScope
{
public:
    FClientPhysicsProfileScope();
    ~FClientPhysicsProfileScope();
    void Stop();
    FClientPhysicsProfileScope(FClientPhysicsProfileScope const&) = delete;
    FClientPhysicsProfileScope& operator=(FClientPhysicsProfileScope const&) = delete;

private:
    FPhysicsTickProfile Counters;
    FPhysicsTickProfile* Previous;
    bool bRunning;
};

#else
class FClientPhysicsProfileScope
{
public:
    FClientPhysicsProfileScope() = default;
    void Stop() {}
    FClientPhysicsProfileScope(FClientPhysicsProfileScope const&) = delete;
    FClientPhysicsProfileScope& operator=(FClientPhysicsProfileScope const&) = delete;
};
#endif
