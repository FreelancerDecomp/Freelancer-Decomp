// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail Client\Gf\RoomControlHotspot.cpp -- the room buttons. See room_control_hotspot.h.

#include "room_control_hotspot.h"

#include "fade_to_black.h"      // SetFadeTarget / SetFadeImmediate / GetFadeDuration
#include "gf_object_ship.h"     // GfObjectShip (FUN_00446be0), PlayLaunchScript (FUN_00447cd0)
#include "input_manager.h"      // SetClientMouseCursorDrawFlag (FUN_0041dda0)
#include "menu_universe_base.h" // ApplyClientLocationAudio / QueueClientLocationChange
#include "nav_bar.h"            // NavBarCloseService (FUN_004424f0)
#include "nn_mission_vendor.h"  // NnMissionVendor::CloseWindow (FUN_0048c720)
#include "win3d.h"              // Win3DFindByName (FUN_0059da10), Win3DDeleteTree (FUN_0043d2a0)
#include "win_factory.h"        // WinFactoryCreate (FUN_0059db20)

RoomButton::RoomButton()
    : Location(0)
    , Behavior(0)
{
    // FUN_0044d2c0: vftable 0x005cc084, +0x04 = +0x08 = 0.
}

RoomButton::~RoomButton()
{
    // FUN_0044d300: MOV [ECX],0x005cc084 -- nothing else.
}

void RoomButton::Activate()
{
    // 0x00401e90: RET.
}

void RoomButton::Update(float /*Delta*/)
{
    // 0x0053e4a0: RET 4.
}

void RoomButton::Set(unsigned InLocation, int InBehavior)
{
    // FUN_0044d310: +0x04 = param_1, +0x08 = param_2.
    Location = InLocation;
    Behavior = InBehavior;
}

ExitDoorButton::ExitDoorButton()
    : State(0)
    , Mode(0)
{
    // 0x0044cf69..0x0044cf74: vftable 0x005cc068, +0x10 = 0, +0x0c = 0. +0x14 is left alone
    // (Activate zeroes it before Update reads it).
}

ExitDoorButton::~ExitDoorButton()
{
    // FUN_0044d1a0: thunk_FUN_0044d300 (the RoomButton vftable back) -- nothing of its own.
}

void ExitDoorButton::Activate()
{
    // FUN_0044d330. 0x0044d333..0x0044d338: only from idle.
    if (State != 0)
        return;
    // 0x0044d33a..0x0044d341: +0x0c = 1, +0x14 = 0.
    State = 1;
    Elapsed = 0.0f;
    // 0x0044d348..0x0044d35d: FUN_00412050(1.0f, (float)(1.0 / DAT_0061650c)) -- FLD the fade
    // duration, FDIVR the double 1.0 at 0x005c89b8, FSTP float.
    SetFadeTarget(1.0f, static_cast<float>(1.0 / static_cast<double>(GetFadeDuration())));
    // 0x0044d362..0x0044d364: FUN_0041dda0(0) -- the software cursor off.
    SetClientMouseCursorDrawFlag(0);
    // 0x0044d369..0x0044d375: FUN_0043bab0(+0x04) on DAT_00668708 -- the location's audio.
    ApplyClientLocationAudio(Location);
}

void ExitDoorButton::Update(float Delta)
{
    // FUN_0044d380 (RET 4).
    if (State == 0)
        return;
    if (State == 1)
    {
        // 0x0044d38f..0x0044d3a4: FLD Delta, FADD float [+0x14], FST float [+0x14], FCOMP
        // float [DAT_0061650c], TEST AH,0x1 (C0) + JNZ -> return. C0 is set when the sum is
        // below the duration or unordered; the compare is on the unrounded sum still on the
        // FPU stack, not on the float just stored.
        double const Sum = static_cast<double>(Delta) + static_cast<double>(Elapsed);
        Elapsed = static_cast<float>(Sum);
        if (!(Sum >= static_cast<double>(GetFadeDuration())))
            return;
        // 0x0044d3a6..0x0044d3b9: +0x0c = 2, FUN_0041dda0(1), FUN_00412070(1.0f).
        State = 2;
        SetClientMouseCursorDrawFlag(1);
        SetFadeImmediate(1.0f);
        return;
    }
    if (State != 2)
        return;
    // 0x0044d3ca..0x0044d3cf: by +0x10.
    if (Mode == 0)
    {
        // 0x0044d3d1..0x0044d3e1: FUN_0043ba90(+0x04, 0, 0) on DAT_00668708 -- the location
        // change queued with no delay and no audio; +0x0c = 0.
        QueueClientLocationChange(Location, 0.0f, false);
        State = 0;
        return;
    }
    if (Mode == 1)
    {
        // 0x0044d3f1..0x0044d3f8: FUN_00447cd0 on FUN_00446be0()'s result, not null-tested.
        GfObjectShip()->PlayLaunchScript();
    }
    // 0x0044d3fd.
    State = 0;
}

LaunchButton::LaunchButton()
{
    // 0x0044d030..0x0044d03d: +0x0c = 0, vftable 0x005cc058, +0x10 = 1.
    Mode = 1;
}

NewsVendorButton::NewsVendorButton()
{
    // FUN_0044d410: +0x04 = +0x08 = 0, vftable 0x005cc094.
}

NewsVendorButton::~NewsVendorButton()
{
    // FUN_0044d450. 0x0044d474..0x0044d48d: FUN_0059da10("NN_News"); none -> nothing.
    Win3D* const Window = Win3DFindByName("NN_News");
    if (Window != nullptr)
    {
        // 0x0044d48f..0x0044d491: FUN_004424f0(0) -- the NavBar's open service tab closed.
        NavBarCloseService(false);
        // 0x0044d496..0x0044d4c1: FUN_0043d2a0 on every child (the next sibling read first),
        // slot 10 (+0x28) and slot 0 with 1 on the window -- FUN_0043d2a0's body inlined.
        Win3DDeleteTree(Window);
    }
    // 0x0044d4c7: vftable 0x005cc084 (~RoomButton).
}

void NewsVendorButton::Activate()
{
    // LAB_0044d4e0. 0x0044d4e1..0x0044d526: the destructor's close, inlined.
    Win3D* const Window = Win3DFindByName("NN_News");
    if (Window != nullptr)
    {
        NavBarCloseService(false);
        Win3DDeleteTree(Window);
    }
    // 0x0044d528..0x0044d536: FUN_0059db20("NN_News", "NN_News", 0, 0); the result is dropped.
    WinFactoryCreate("NN_News", "NN_News", nullptr, nullptr);
}

MissionVendorButton::MissionVendorButton()
{
    // FUN_0044d540: +0x04 = +0x08 = 0, vftable 0x005cc0a4.
}

MissionVendorButton::~MissionVendorButton()
{
    // FUN_0044d580. 0x0044d5a3..0x0044d5be: FUN_0059da10("NN_MissionVendor") -- the string is
    // 0x005cb69f + 1 -- and FUN_0048c720 on the window found.
    Win3D* const Window = Win3DFindByName("NN_MissionVendor");
    if (Window != nullptr)
        static_cast<NnMissionVendor*>(Window)->CloseWindow();
    // 0x0044d5c7: vftable 0x005cc084 (~RoomButton).
}

void MissionVendorButton::Activate()
{
    // LAB_0044d5e0: FUN_0059db20("NN_MissionVendor", "NN_MissionVendor", 0, 0) (the result is
    // dropped), then FUN_0041dda0(1) -- the software cursor on.
    WinFactoryCreate("NN_MissionVendor", "NN_MissionVendor", nullptr, nullptr);
    SetClientMouseCursorDrawFlag(1);
}
