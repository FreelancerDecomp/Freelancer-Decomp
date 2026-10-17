// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Client\Gf\RoomControlHotspot.cpp (the FDUMP path at .rdata 0x005cc01c) -- the room
// buttons RoomControl builds from its RoomData hotspots (FUN_0044ced0, room_control.cpp) and
// keeps at RoomControl +0x1134 (count +0x1334). NavBar tells them a tab was pressed
// (FUN_0044d240: slot 1 on every button whose +0x04 or +0x08 equals the tab's type); the room
// update steps them every frame (FUN_0044d200: slot 2 with the frame delta); the RoomControl
// destructor deletes them (FUN_0044d1c0).
//
// RTTI (COL at vftable[-1]), every vftable three slots
//   0 scalar deleting dtor, 1 Activate (thiscall, no arguments), 2 Update (thiscall, float, RET 4):
//   RoomButton          vftable 0x005cc084 (COL 0x005e8df8): LAB_0044d2e0, 0x00401e90, 0x0053e4a0
//   ExitDoorButton      vftable 0x005cc068 (COL 0x005e8d50) : RoomButton
//                       FUN_0044d1a0, FUN_0044d330, FUN_0044d380
//   LaunchButton        vftable 0x005cc058 (COL 0x005e8dcc) : ExitDoorButton -- the same three
//                       functions; only its constructor differs (+0x10 = 1)
//   NewsVendorButton    vftable 0x005cc094 (COL 0x005e8e40) : RoomButton
//                       FUN_0044d430, LAB_0044d4e0, 0x0053e4a0
//   MissionVendorButton vftable 0x005cc0a4 (COL 0x005e8e88) : RoomButton
//                       FUN_0044d560, LAB_0044d5e0, 0x0053e4a0
// 0x00401e90 (a bare RET) and 0x0053e4a0 (RET 4) are ICF-folded empty bodies shared with
// IDPMsgHandler; here they are RoomButton's own empty Activate / Update.
//
// The RoomButtonInfo "behavior=" value (Common.dll RoomData::read_Hotspot_block option table)
// picks the class in FUN_0044ced0: 1 ExitDoor -> ExitDoorButton, 2 NewsVendor ->
// NewsVendorButton, 3 MissionVendor -> MissionVendorButton, 4 Launch -> LaunchButton,
// 5 StartDealer / 6 FrontDesk -> a plain RoomButton, 7..12 -> no button, anything else ->
// FDUMP "Unknown button behavior type".

class RoomButton
{
public:
    // FUN_0044d2c0 -- vftable 0x005cc084, +0x04 = +0x08 = 0.
    RoomButton();
    // FUN_0044d300 (the vftable put back; thunk 0x0044d190), scalar deleting LAB_0044d2e0.
    virtual ~RoomButton();
    // Slot 1 -- 0x00401e90: nothing.
    virtual void Activate();
    // Slot 2 -- 0x0053e4a0 (RET 4): nothing.
    virtual void Update(float Delta);

    // FUN_0044d310 (thiscall, RET 8) -- +0x04 = InLocation, +0x08 = InBehavior.
    void Set(unsigned InLocation, int InBehavior);

    // +0x04 -- MakeLocationID(RoomControl +0x34 base, hotspot room_switch) through FUN_0043a4f0,
    // 0 when the hotspot has no room_switch.
    unsigned Location;
    // +0x08 -- the hotspot's behavior value.
    int Behavior;

private:
    RoomButton(RoomButton const&) = delete;
    RoomButton& operator=(RoomButton const&) = delete;
};

// operator_new(0x18). An exit door: Activate fades to black and starts the location's audio;
// once the fade has run its duration Update snaps to black and changes location (Mode 0) or
// plays the player ship's launch script (Mode 1, LaunchButton).
class ExitDoorButton : public RoomButton
{
public:
    // Inlined at 0x0044cf62..0x0044cf74: FUN_0044d2c0, vftable 0x005cc068, +0x10 = +0x0c = 0.
    ExitDoorButton();
    // FUN_0044d1a0 -- thunk_FUN_0044d300, then the conditional delete.
    ~ExitDoorButton() override;
    // FUN_0044d330.
    void Activate() override;
    // FUN_0044d380 (RET 4).
    void Update(float Delta) override;

    int State;     // +0x0c 0 idle, 1 fading, 2 faded
    int Mode;      // +0x10 0 exit door (location change), 1 launch
    float Elapsed; // +0x14 the fade time run so far (not written by the ctor)
};

class LaunchButton : public ExitDoorButton
{
public:
    // Inlined at 0x0044d029..0x0044d03d: FUN_0044d2c0, +0x0c = 0, vftable 0x005cc058, +0x10 = 1.
    LaunchButton();
};

// operator_new(0xc). Opens NN_News (a fresh one each time); the destructor closes it.
class NewsVendorButton : public RoomButton
{
public:
    // FUN_0044d410 -- +0x04 = +0x08 = 0, vftable 0x005cc094.
    NewsVendorButton();
    // FUN_0044d450 (scalar deleting FUN_0044d430).
    ~NewsVendorButton() override;
    // LAB_0044d4e0.
    void Activate() override;
};

// operator_new(0xc). Opens NN_MissionVendor; the destructor closes it.
class MissionVendorButton : public RoomButton
{
public:
    // FUN_0044d540 -- +0x04 = +0x08 = 0, vftable 0x005cc0a4.
    MissionVendorButton();
    // FUN_0044d580 (scalar deleting FUN_0044d560).
    ~MissionVendorButton() override;
    // LAB_0044d5e0.
    void Activate() override;
};
