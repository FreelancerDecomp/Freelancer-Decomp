// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "globals.h"
#include "common_misc.h"   // SinglePlayer
#include "common_timing.h" // Timing::Delta
#include "dalib_net.h"
#include "menu_sound_arch.h" // PauseAllClientSounds / ResumePausedClientSounds (FUN_00428550 / 80)

#include <windows.h>

namespace
{
    // Retail freelancer.exe frame-timer BSS. FUN_0042d770 is the tick, 0042d650..0042d6e0 the
    // accessor family, FUN_005b2360 the main-loop wrapper.
    Timing::Delta G_FrameDelta;   // DAT_00667cf8

    double G_ElapsedTime = 0.0;   // _DAT_00667d38 — sum of capped dt, never pause-gated
    double G_FrameDtCapped = 0.0; // _DAT_00667d40 — dt clamped to kMaxFrameSeconds
    double G_FrameDtRaw = 0.0;    // _DAT_00667d48 — dt as measured
    double G_AvgFrameTime = 0.0;  // _DAT_00667d60 — seconds/frame over the last window
    // _DAT_00610a00 — seconds accumulated in the window. .data ships the double 10.0 (dwords
    // 0x00000000 / 0x40240000 at 0x00610a00 / 0x00610a04), so the very first tick already
    // closes a window; 0x0042d859 / 0x0042d863 zero both halves on every close.
    double G_FpsWindow = 10.0;
    unsigned G_FrameCount = 0;    // DAT_00667d50
    unsigned G_FpsWindowFrames = 0; // DAT_00667d6c
    // DAT_00667d20 — timeGetTime() of the current frame; seeded at load by the XC entry
    // LAB_0042d650 (0x0060f188: CALL timeGetTime / MOV [0x667d20],EAX).
    unsigned long G_LastTickMs = timeGetTime();
    bool G_bNetworkStalled = false; // DAT_00667d58
    int G_PauseDepth = 0;         // DAT_00667d68 — frame-time getters (0042d680/6a0)
    int G_ServerTickPauseDepth = 0; // DAT_00667d54 — FUN_0042d740 skips IServer::Update
    double G_AccumulatedClientTime = 0.0; // _DAT_0067ecd8 — FUN_005b2360's own clock

    // DAT_00610a0c — busy-wait fps cap; .data ships 200.0f and both writers (00558a14 /
    // 00558a57) take it from COMMON.DLL::RtcSlider.
    float G_FpsCap = 200.0f;
    // DAT_00610a08 — dt clamp, .data ships 1.0f and nothing in the PE ever writes it, so the
    // capped and raw dt only diverge on frames longer than a whole second.
    const float kMaxFps = 1.0f;
    // DAT_005ca228 — the fps window length.
    const double kFpsWindowSeconds = 2.0;

    void TickFrameTimer()
    {
        // FUN_0042d770.
        const double MinFrameSeconds = 1.0 / static_cast<double>(G_FpsCap);
        double Dt = 0.0;
        while (true)
        {
            G_FrameDelta.stop();
            Dt = G_FrameDelta.calc_dt();
            if (Dt >= MinFrameSeconds)
                break;
            Sleep(1);
        }
        G_FrameDelta.swap();

        G_FrameDtRaw = Dt;

        const double MaxFrameSeconds = 1.0 / static_cast<double>(kMaxFps);
        // 0042d7e3: FCOM + TEST AH,5 + JNP — keep Dt only when Dt < Max (C0 alone), take Max
        // otherwise, including the unordered case (C0|C2|C3 -> even parity -> no jump).
        G_FrameDtCapped = (Dt < MaxFrameSeconds) ? Dt : MaxFrameSeconds;

        G_ElapsedTime += G_FrameDtCapped;
        G_FrameCount += 1;
        G_FpsWindowFrames += 1;
        G_FpsWindow += G_FrameDtCapped;
        if (G_FpsWindow > kFpsWindowSeconds)
        {
            // 0042d843: FILD frames, FDIVR window — the result is seconds per frame.
            G_AvgFrameTime = G_FpsWindow / static_cast<double>(G_FpsWindowFrames);
            G_FpsWindowFrames = 0;
            G_FpsWindow = 0.0;
        }

        G_LastTickMs = timeGetTime();
        G_bNetworkStalled = false;
        // 0042d885..0042d8a4: in multiplayer the flag is set when no DirectPlay message has
        // arrived for more than a second -- (DAT_00667d20 - CDPClient::GetLastMsgTimestamp())
        // > 0x3e8, unsigned (JBE). GetLastMsgTimestamp is the DALib import (DALib .text:065c2bc0).
        if (!SinglePlayer())
        {
            if (G_LastTickMs - CDPClient::GetLastMsgTimestamp() > 1000)
                G_bNetworkStalled = true;
        }
    }
}

void TickClientFrameTimer()
{
    // FUN_0042d770 on its own — the pre-init call at 005b1fb8 primes the delta without touching
    // the client clock.
    TickFrameTimer();
}

void UpdateClientTiming()
{
    // FUN_005b2360 (.text:005b2360-005b2392) — tick, then accumulate the client clock: single
    // player uses the capped dt (FUN_0042d680), multiplayer the raw one (FUN_0042d6a0). Both
    // getters return 0 while paused, so _DAT_0067ecd8 is a separate clock from G_ElapsedTime.
    // Timing::UpdateGlobalTime is not here: FUN_005b2890 calls it (client_frame_tick.cpp).
    TickFrameTimer();
    if (SinglePlayer())
        G_AccumulatedClientTime += GetClientFrameTimeCapped();
    else
        G_AccumulatedClientTime += GetClientFrameTimeRaw();
}

double GetClientAccumulatedTime()
{
    return G_AccumulatedClientTime;
}

void SeedClientAccumulatedTime(double Seconds)
{
    // FUN_005ab380 .text:005ab3b1..005ab3bf (multiplayer) / 005ab3c9..005ab3d5 (single player):
    // the character-select response's +8 / +0xc dwords go into DAT_0067ecd8 / DAT_0067ecdc,
    // the two halves of the double FUN_005b2360 advances (FADD / FSTP double ptr [0x67ecd8]).
    G_AccumulatedClientTime = Seconds;
}

float GetClientFrameDeltaSeconds()
{
    return static_cast<float>(SinglePlayer() ? GetClientFrameTimeCapped()
                                             : GetClientFrameTimeRaw());
}

double GetClientFrameTimeCapped()
{
    // FUN_0042d680 — 21 call sites; 0.0 while paused.
    if (G_PauseDepth != 0)
        return 0.0;
    return G_FrameDtCapped;
}

double GetClientFrameTimeRaw()
{
    // FUN_0042d6a0 — one call site (FUN_005b2360); 0.0 while paused.
    if (G_PauseDepth != 0)
        return 0.0;
    return G_FrameDtRaw;
}

double GetClientElapsedTime()
{
    // FUN_0042d660 — 13 call sites. Not pause-gated.
    return G_ElapsedTime;
}

unsigned GetClientFrameCount()
{
    // FUN_0042d6e0.
    return G_FrameCount;
}

unsigned long GetClientFrameTimestampMs()
{
    // FUN_0042d670.
    return G_LastTickMs;
}

void PushClientTimingPause()
{
    // FUN_0042d6c0.
    G_PauseDepth += 1;
}

void PopClientTimingPause()
{
    // FUN_0042d6d0.
    G_PauseDepth -= 1;
}

void SetClientServerTickPause(bool bPause)
{
    // FUN_0042d6f0(param1). Counter is DAT_00667d54. 0x0042d6f0..0x0042d716: pausing from 0
    // in single player (Common SinglePlayer) pauses every playing sound (FUN_00428550) before
    // the INC. 0x0042d717..0x0042d729: the DEC; reaching 0 in single player resumes the paused
    // ones (FUN_00428580, JMP).
    if (bPause)
    {
        if (G_ServerTickPauseDepth == 0 && SinglePlayer())
            PauseAllClientSounds();
        G_ServerTickPauseDepth += 1;
        return;
    }

    G_ServerTickPauseDepth -= 1;
    if (G_ServerTickPauseDepth != 0)
        return;
    if (SinglePlayer())
        ResumePausedClientSounds();
}

bool IsClientServerTickPaused()
{
    // FUN_0042d730: XOR EAX,EAX / TEST [DAT_00667d54] / SETG AL.
    return G_ServerTickPauseDepth > 0;
}

bool ShouldSkipLocalServerTick()
{
    // FUN_0042d740 — retail frame loop at .text:005b2949 gates Update+ElapseTime.
    if (!SinglePlayer())
        return false;
    return G_ServerTickPauseDepth > 0;
}

void SetClientFpsCap(float Fps)
{
    // DAT_00610a0c — retail stores RtcSlider::get_fps_cap (00558a14, entering a base) and
    // RtcSlider::get_default_fps_cap (00558a57, leaving it) with a plain FSTP; no test.
    G_FpsCap = Fps;
}
