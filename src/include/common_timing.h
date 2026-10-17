// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

#include <cstddef>

// Timing -- retail CTiming.cpp, defined in common_timing.cpp.
// Free functions: __cdecl (MSVC mangle YA...). Delta methods: __thiscall (QAE...).

namespace Timing
{
    COMMON_API void __cdecl init();
    COMMON_API long long __cdecl read_ticks();
    COMMON_API double __cdecl seconds(const long long& Ticks);
    COMMON_API double __cdecl read();
    COMMON_API void __cdecl UpdateGlobalTime(double Dt);
    COMMON_API double __cdecl GetGlobalTime();

    // Common.dll.c Timing::GetMachineSpeed @ 0xa7f20. Lazily measures the CPU's
    // clock speed in Hz (RDTSC-based) on first call and caches it in a signed
    // global (retail sentinel: negative == not yet measured); every later call
    // just returns the cached value truncated to int.
    COMMON_API int __cdecl GetMachineSpeed();

    // POD-like frame timer. No vtable. sizeof 0x28 (MSVC pack 8).
    // Layout from Timing::Delta::Delta @ Common.dll RVA 0xa84f0.
    struct COMMON_API Delta
    {
    public:
        // User-declared so that no implicit move members are generated and exported:
        // VC6 had none, and retail exports exactly the copy assignment.
        Delta& operator=(Delta const&) = default;

        Delta();

        void init();
        void start();
        void stop();
        void swap();
        double calc_dt();
        double read_dt();

        double Dt;                      // +0x00  last calc_dt / read_dt
        long long StartTicks;           // +0x08  QPC start
        long long StopTicks;            // +0x10  QPC stop
        unsigned long StartTickCount;   // +0x18  GetTickCount start
        unsigned long StopTickCount;    // +0x1c  GetTickCount stop
        bool bStarted;                  // +0x20
        bool bStopped;                  // +0x21
    };
}

static_assert(offsetof(Timing::Delta, Dt) == 0x00, "Timing::Delta.Dt");
static_assert(offsetof(Timing::Delta, StartTicks) == 0x08, "Timing::Delta.StartTicks");
static_assert(offsetof(Timing::Delta, StopTicks) == 0x10, "Timing::Delta.StopTicks");
static_assert(offsetof(Timing::Delta, StartTickCount) == 0x18, "Timing::Delta.StartTickCount");
static_assert(offsetof(Timing::Delta, StopTickCount) == 0x1c, "Timing::Delta.StopTickCount");
static_assert(offsetof(Timing::Delta, bStarted) == 0x20, "Timing::Delta.bStarted");
static_assert(offsetof(Timing::Delta, bStopped) == 0x21, "Timing::Delta.bStopped");
static_assert(sizeof(Timing::Delta) == 0x28, "Timing::Delta size (MSVC pack 8)");
