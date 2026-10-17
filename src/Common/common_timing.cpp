// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Timing -- retail CTiming.cpp (0x06307f20-0x063089d0). Declared in common_timing.h.
#include "common_timing.h"
#include "dacom.h" // FDUMP

#include <cmath>
#include <cstddef>
#include <intrin.h>
#include <windows.h>

namespace
{
    const char kTimingSource[] = "E:\\FL\\Scratch\\Source\\Common\\CTiming.cpp";
    double g_timing_frequency = 0.0;
    double g_timing_period = 0.0;
    double g_timing_global = 0.0;

    // DAT_063ed210 — cached Timing::GetMachineSpeed() result in whole MHz, negative
    // sentinel meaning "not yet measured" (.data initial value -1).
    int g_machine_speed_mhz = -1;

    long long ReadPerformanceCounterOrZero()
    {
        LARGE_INTEGER Counter;
        if (!QueryPerformanceCounter(&Counter))
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kTimingSource, 0x56,
                  "QueryPerformanceCounter returned false");
            return 0;
        }
        return Counter.QuadPart;
    }

    void CaptureStart(Timing::Delta* Self)
    {
        Timing::init();
        Self->StartTicks = ReadPerformanceCounterOrZero();
        Self->StartTickCount = GetTickCount();
        Self->bStarted = true;
        Self->bStopped = false;
    }

    void CaptureStop(Timing::Delta* Self)
    {
        Timing::init();
        Self->StopTicks = ReadPerformanceCounterOrZero();
        Self->StopTickCount = GetTickCount();
        Self->bStopped = true;
    }

    double CalculateDeltaSeconds(Timing::Delta* Self)
    {
        if (!Self->bStarted || !Self->bStopped)
        {
            Self->Dt = 0.0;
            return Self->Dt;
        }

        const long long QpcDelta = Self->StopTicks - Self->StartTicks;
        Self->Dt = (double)QpcDelta * g_timing_period;

        // _ftol(Dt * -1000.0) keeps the low 32 bits of the 64-bit truncation; the
        // difference -ftol - tick_delta and its CDQ/XOR/SUB abs wrap in 32 bits, and
        // the `CMP EAX,0x32; JLE` test is signed. More than 50 ms of disagreement
        // between QPC and GetTickCount falls back to the tick count (unsigned) * 0.001.
        const unsigned long FtolNegMs = (unsigned long)(long long)(Self->Dt * -1000.0);
        const unsigned long TickDelta = Self->StopTickCount - Self->StartTickCount;
        int AbsDiff = (int)(0UL - FtolNegMs - TickDelta);
        if (AbsDiff < 0)
            AbsDiff = (int)(0UL - (unsigned long)AbsDiff);
        if (AbsDiff > 50)
            Self->Dt = (double)TickDelta * 0.001;

        return Self->Dt;
    }

    // Common.dll.c FUN_06307ff0 @ 0xa7ff0 -- "TestClockSpeed". Each round takes six
    // RDTSC-delta samples across a 20ms Sleep (retrying an individual sample if the
    // 64-bit RDTSC readback ever appears to go backward), discards the single
    // smallest and single largest sample, then checks whether the remaining four
    // samples' max deviation from their mean is under 0.1% of the mean. Converged
    // rounds return Hz (mean 20ms tick-delta * 50); after 25 failed rounds it gives
    // up and returns a fixed 750 MHz guess, matching retail's FDUMP + fallback.
    // The RDTSC readings are signed 64-bit (`CMP hi; JL` / `JG` / `CMP lo; JC`), and
    // the delta reaches the x87 stack through FILD qword (signed).
    double TestClockSpeed()
    {
        for (int Round = 0; Round <= 0x18; ++Round)
        {
            double Samples[6];
            for (int i = 0; i < 6; ++i)
            {
                long long Start, End;
                do
                {
                    Start = (long long)__rdtsc();
                    Sleep(0x14);
                    End = (long long)__rdtsc();
                } while (End < Start);
                Samples[i] = (double)(End - Start);
            }

            int Count = 6;
            {
                int MinIdx = 0;
                for (int i = 1; i < Count; ++i)
                    if (Samples[i] < Samples[MinIdx])
                        MinIdx = i;
                for (int i = MinIdx; i < Count - 1; ++i)
                    Samples[i] = Samples[i + 1];
                --Count;
            }
            {
                int MaxIdx = 0;
                for (int i = 1; i < Count; ++i)
                    if (Samples[i] > Samples[MaxIdx])
                        MaxIdx = i;
                for (int i = MaxIdx; i < Count - 1; ++i)
                    Samples[i] = Samples[i + 1];
                --Count;
            }

            double Mean = 0.0;
            for (int i = 0; i < Count; ++i)
                Mean += Samples[i];
            Mean /= Count;

            double MaxDev = 0.0;
            for (int i = 0; i < Count; ++i)
            {
                const double Dev = std::fabs(Samples[i] - Mean);
                if (Dev > MaxDev)
                    MaxDev = Dev;
            }

            if (MaxDev / Mean < 0.001)
                return Mean * 50.0;
        }

        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kTimingSource, 0x11e,
              "TestClockSpeed - failed!");
        return 1000000.0 * 750.0;
    }
}

namespace Timing
{
    void __cdecl init()
    {
        // Common.dll.c Timing::init @ 0xa8270
        if (g_timing_frequency != 0.0)
            return;

        LARGE_INTEGER Freq;
        if (!QueryPerformanceFrequency(&Freq))
        {
            FDUMP(0x100001,
                  "%s(%d) : *** ERROR: QueryPerformanceFrequence failed. GetLastError returned %u",
                  kTimingSource, 0x137, GetLastError());
            g_timing_frequency = 0.0;
            g_timing_period = 1.0 / g_timing_frequency;
            return;
        }

        // FILD dword HighPart (signed) * 2^32 + FILD qword {LowPart, 0}: the signed
        // 64-bit count.
        g_timing_frequency = (double)Freq.QuadPart;
        g_timing_period = 1.0 / g_timing_frequency;
    }

    long long __cdecl read_ticks()
    {
        // Common.dll.c Timing::read_ticks @ 0xa8300 — auto-init frequency + FDUMP on QPC fail
        init();
        return ReadPerformanceCounterOrZero();
    }

    double __cdecl seconds(const long long& Ticks)
    {
        // Common.dll.c Timing::seconds @ 0xa83e0
        return (double)Ticks * g_timing_period;
    }

    double __cdecl read()
    {
        // Common.dll.c Timing::read @ 0xa83f0
        init();
        const long long Ticks = ReadPerformanceCounterOrZero();
        return (double)Ticks * g_timing_period;
    }

    void __cdecl UpdateGlobalTime(double Dt)
    {
        // Common.dll.c Timing::UpdateGlobalTime @ 0xa7f60
        g_timing_global += Dt;
    }

    double __cdecl GetGlobalTime()
    {
        // Common.dll.c Timing::GetGlobalTime @ 0xa7f80
        return g_timing_global;
    }

    int __cdecl GetMachineSpeed()
    {
        // Common.dll.txt 06307f20: `TEST EAX,EAX; JGE` -- the cache DAT_063ed210 is used
        // once it is >= 0. Otherwise TestClockSpeed (FUN_06307ff0, Hz on the x87 stack)
        // FDIV 1000000.0 (063a30f0), FMUL 10.0 (0639c478), _ftol, FILD, FMUL 0.1
        // (063a0460), _ftol, store: whole MHz. Both _ftol calls keep the low 32 bits of
        // the 64-bit truncation.
        if (g_machine_speed_mhz < 0)
        {
            int const TenthsMhz = (int)(long long)(TestClockSpeed() / 1000000.0 * 10.0);
            g_machine_speed_mhz = (int)(long long)((double)TenthsMhz * 0.1);
        }
        return g_machine_speed_mhz;
    }

    Delta::Delta()
    {
        // Common.dll.c Timing::Delta::Delta @ 0xa84f0 — no vptr
        Dt = 0.0;
        StartTicks = 0;
        StopTicks = 0;
        StartTickCount = 0;
        StopTickCount = 0;
        bStarted = false;
        bStopped = false;
    }

    void Delta::init()
    {
        // Common.dll.c Timing::Delta::init @ 0xa8520
        if (bStarted)
            return;
        CaptureStart(this);
    }

    void Delta::start()
    {
        // Common.dll.c Timing::Delta::start @ 0xa8620
        CaptureStart(this);
    }

    void Delta::stop()
    {
        // Common.dll.c Timing::Delta::stop @ 0xa8720
        if (!bStarted)
            CaptureStart(this);
        CaptureStop(this);
    }

    void Delta::swap()
    {
        // Common.dll.c Timing::Delta::swap @ 0xa8900
        if (!bStopped)
            return;
        StartTicks = StopTicks;
        StartTickCount = StopTickCount;
        bStarted = true;
        bStopped = false;
    }

    double Delta::calc_dt()
    {
        // Common.dll.c Timing::Delta::calc_dt @ 0xa8930
        return CalculateDeltaSeconds(this);
    }

    double Delta::read_dt()
    {
        // Common.dll.c Timing::Delta::read_dt @ 0xa89d0: an inlined start() under its own
        // bStarted test, then the inlined stop() (whose second bStarted test is now always
        // true), calc_dt and swap. The FDUMP masks there come from an out-of-line
        // (0x10000 << 4 | 1) builder (FUN_062b7750) and equal the 0x100001 used here.
        if (!bStarted)
            start();
        stop();
        const double Elapsed = calc_dt();
        swap();
        return Elapsed;
    }
}
