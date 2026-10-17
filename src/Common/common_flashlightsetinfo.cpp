// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// FlashLightSetInfo -- retail RoomData.cpp TU (0x06273bb0-0x06274120). Declared in
// common_goods.h, before RoomData.
#include "common_goods.h"
#include "common_csys.h"  // Csys, Csys_interpolate, Acquire3DMathEngine
#include "common_misc.h"  // malloc_strfree
#include "x86math.h"      // I3DMathEngine::MulTransform
#include "DALib.h"        // DALib::Engine
#include "dacom.h"        // FDUMP

#include <cstring>

int CallDacomStricmp(const char* Left, const char* Right); // common_dacom_stricmp.cpp

FlashLightSetInfo::FlashLightSetInfo()
{
    // Common.dll.c 0x13bb0: body is just clear(this).
    clear();
}

FlashLightSetInfo::~FlashLightSetInfo()
{
    // .text:06273bf0 is shared with destroy() (ICF).
    destroy();
}

void FlashLightSetInfo::destroy()
{
    // .text:06273bf0: free both malloc'd-string banks (64 pairs, +0x20 and +0x120), then
    // JMP clear().
    for (int I = 0; I < 64; ++I)
    {
        malloc_strfree(const_cast<char**>(&HardpointNames[I]));
        malloc_strfree(const_cast<char**>(&SetpointNames[I]));
    }
    clear();
}

void FlashLightSetInfo::clear()
{
    // Common.dll.c 0x13c30.
    Param0 = 1.0f;
    Param1 = 1.0f;
    Param2 = 1.0f;
    Param3 = 2.0f;
    Param4 = 0.1f;
    Param5 = 0.2f;
    Param6 = 1.0f;
    Mode = 0;

    for (int I = 0; I < 64; ++I)
    {
        HardpointNames[I] = nullptr;
        SetpointNames[I] = nullptr;
        LightFrames[I].set_identity();
    }

    NumLights = 0;
    NumLightsAlt = 0;
    Cursor = 0;

    for (int I = 0; I < 64; ++I)
        FinalFrames[I].set_identity();
}

void FlashLightSetInfo::add_light_csys(Csys const& LightFrame)
{
    // Common.dll.c 0x13d70: appends at FinalFrames[Cursor], then advances Cursor.
    // Retail does not bounds-check Cursor against the 64-slot bank.
    FinalFrames[Cursor] = LightFrame;
    ++Cursor;
}

void FlashLightSetInfo::unfixup()
{
    // Common.dll.c 0x14120: just resets the FinalFrames write cursor.
    Cursor = 0;
}

void FlashLightSetInfo::fixup(long Instance, HardpointSummary const* HpSum, RoomData const* Room)
{
    // 0x06273db0. Instance (param_1) is never read.
    (void)Instance;
    static char const kRoomDataFile[] = "E:\\FL\\Scratch\\Source\\Common\\RoomData.cpp";

    // .text:06273dc6: Cursor = 0; .text:06273dd0: nothing to resolve when NumLights <= 0.
    Cursor = 0;
    for (long Index = 0; Index < NumLights; ++Index)
    {
        // .text:06273df0: an empty hardpoint slot is skipped.
        char const* const HardpointName = HardpointNames[Index];
        if (HardpointName != nullptr)
        {
            // .text:06273e04 get_info_by_name; a miss logs and keeps the frame
            // (.text:06273ec0, line 0x187).
            HardpointInfo const* const Info = HpSum->get_info_by_name(HardpointName);
            if (Info != nullptr)
            {
                // .text:06273e18: the instance the hardpoint sits on.
                long const HpInstance = HpSum->find_name_instance(HardpointName);
                // .text:06273e1d..06273e45: the hardpoint's local transform -- the 3x3 from
                // Info+0x10, the position from Info+0x04.
                Transform Local;
                Local.Rotation = Info->Orient;
                Local.Translation = Info->Position;
                // .text:06273e4f: DALib::Engine slot 40 (+0xa0), the instance's world
                // transform.
                Transform const* const InstanceXform =
                    (Transform const*)DALib::Engine->GetInstanceTransform((void*)HpInstance);
                // .text:06273e57..06273e7b: the 3DMathEngine singleton's MulTransform
                // (+0x1c), World = InstanceXform * Local.
                I3DMathEngine* const Math = Acquire3DMathEngine();
                Transform World;
                Math->MulTransform((FTransform&)World, (FTransform const&)*InstanceXform,
                                   (FTransform const&)Local);
                // .text:06273e8a..06273eb5: the Csys takes the position from +0x24 and the
                // 3x3 from +0x00.
                LightFrames[Index].set_transform(World);
            }
            else
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: Cannot find hardpoint %s in the ONE room object\n",
                      kRoomDataFile, 0x187, HardpointName);
            }
        }

        // .text:06273ee0: the setpoint override.
        char const* const SetpointName = SetpointNames[Index];
        if (SetpointName != nullptr)
        {
            // .text:06273ef4..06273f40: every setpoint of the room is compared; the LAST
            // case-insensitive name match wins.
            SetpointInfo const* Found = nullptr;
            for (unsigned SetpointIndex = 0; SetpointIndex < Room->Setpoints.size(); ++SetpointIndex)
            {
                // .text:06273f25: the DACOM.DLL stricmp import (IAT 0639902c), not the CRT.
                if (CallDacomStricmp(SetpointName, Room->Setpoints[SetpointIndex]->Name) == 0)
                    Found = Room->Setpoints[SetpointIndex];
            }

            if (Found != nullptr)
            {
                // .text:06274034: the frame is replaced by the setpoint's Csys (+0x04).
                LightFrames[Index] = Found->Frame;
            }
            else
            {
                // .text:06273f4e..06273f9d: two errors, the first with an empty script name
                // (.rdata:063997b0) and the room's ini file (+0x10).
                FDUMP(0x100001,
                      "%s(%d) : *** ERROR: script %s calls for setpoint %s which does not exist in room .ini = %s\n",
                      kRoomDataFile, 0x5a8, "", SetpointName, Room->File);
                FDUMP(0x100001, "%s(%d) : *** ERROR: Cannot find setpoint %s\n",
                      kRoomDataFile, 0x194, SetpointName);
            }
        }
    }

    if (Mode == 1)
    {
        // .text:06273fd7..06274025: every resolved frame appended in order.
        for (long Index = 0; Index < NumLights; ++Index)
        {
            FinalFrames[Cursor] = LightFrames[Index];
            ++Cursor;
        }
    }
    else if (Mode == 2)
    {
        // .text:06274050..062740e4: NumLightsAlt frames interpolated from LightFrames[0] to
        // LightFrames[1], T = Step / (NumLightsAlt - 1) (FILD / FIDIV on the FPU stack, one
        // FSTP dword at .text:06274083 -- so the quotient is formed in double, rounded once).
        for (long Step = 0; Step < NumLightsAlt; ++Step)
        {
            float const T = (float)((double)Step / (NumLightsAlt - 1));
            FinalFrames[Cursor] = Csys_interpolate(LightFrames[0], LightFrames[1], T);
            ++Cursor;
        }
    }
}
