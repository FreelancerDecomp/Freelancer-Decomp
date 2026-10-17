// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The client state classes -- retail Freelancer.exe's IInputEvent / IEnvironment hierarchy.
//
// Every client state object derives from IEnvironment, which derives from IInputEvent; the
// RTTI Class Hierarchy Descriptors at each vftable's [-4] COL list exactly these two bases:
//   GF               vftable 0x005cae24, COL 0x005e8734 (.?AUGF@@)              gameflow.cpp
//   SpaceApp         vftable 0x005e0554, COL 0x005ef678 (.?AVSpaceApp@@)        space_app.h
//                    (plus Event::Handler at +4, vftable 0x005e0548, COL 0x005ef6e0)
//   IntroFocus       vftable 0x005e3034, COL 0x005f04a0 (.?AUIntroFocus@@)      client_state.cpp
//   MissionBriefing  vftable 0x005cb094, COL 0x005e88d4 (.?AUMissionBriefing@@) mission_briefing.cpp
// A scan of every COL in the image finds no other class with either base, and neither base has
// a vftable of its own in the exe (their constructors are elided: IntroFocus's ctor FUN_00573840
// stamps only 0x005e3034). Each derived vftable has the same 15 slots, all thiscall; the
// arities below are the RET n of every body in the four tables. No slot is a destructor.
//
// Where IInputEvent ends and IEnvironment begins is not in the binary. The port puts the five
// window-message handlers (slots 0..4: the message-shaped arguments, RET 0x10 / 0xc, a bool
// answer) in IInputEvent and the rest in IEnvironment; the vtable order -- the only thing a
// caller sees -- is the retail order either way.
//
// Who calls what (raw listing, every holder of a state pointer read: DAT_0067ecac XREF[14] and
// the four singleton getters FUN_00437420 / FUN_0054bd30 / FUN_00575430 / FUN_0043d560):
//   +0x1c slot 7   FUN_005b0880 (IsClientIntroEnvironment), FUN_005b2a30 case 3 (0x005b2b93)
//   +0x20 slot 8   InitializeEngine 0x005b1e0d / 0x005b1ea5 / 0x005b1f50 / 0x005b1f5c
//   +0x24 slot 9   the atexit teardown LAB_005b2190 (0x005b21db..0x005b21ff)
//   +0x28 slot 10  FUN_005b2a30 cases 4 / 5 (0x005b2c05 / 0x005b2bf2)
//   +0x2c slot 11  the render surface reset 0x00424c23 (Width, Height)
//   +0x30 slot 12  FUN_005b2a30 (0x005b2af4, 0x005b2b50, 0x005b2bb7, 0x005b2bdf)
//   +0x34 slot 13  FUN_005b2a30 (every case), LAB_005b2190 (0x005b21ac), SpaceApp slot 9
//   +0x38 slot 14  the frame FUN_005b2890 (0x005b2a0b), NN_Prompt's modal loops (0x004740b6,
//                  0x00474220, 0x0047439b), ServerFilterDialog's loop (0x00563872)
// Slots 0..6 are called by nothing in the exe (tracker LAB_0054a0e0 has the full survey); the
// window procedure feeds input through the Win3D manager and Event::Handler, not these slots.
//
// Names: slots 7..9 and 11..14 are named after what every one of the four bodies and every
// caller does; the retail names are not recovered. Slots with no caller and no shared meaning
// keep SlotNN.

struct IInputEvent
{
    // +0x00, RET 0x10 -- a key message (A, Message, B, C). GF LAB_00437150, SpaceApp
    // LAB_0054a0e0, IntroFocus 0x0043d8f0, MissionBriefing LAB_0043d130.
    virtual bool Slot00(unsigned A, unsigned Message, unsigned B, unsigned C) = 0;
    // +0x04, RET 0xc. GF LAB_004371e0, SpaceApp / IntroFocus 0x004b50e0, MissionBriefing
    // LAB_0043d160.
    virtual bool Slot01(unsigned A, unsigned B, unsigned C) = 0;
    // +0x08, RET 0xc -- a mouse move (A, B, packed lParam). GF LAB_00437200, SpaceApp
    // LAB_0054a210, IntroFocus 0x004b50e0, MissionBriefing LAB_0043d160.
    virtual bool Slot02(unsigned A, unsigned B, unsigned long Packed) = 0;
    // +0x0c, RET 0x10 -- a mouse button (A, Message, B, packed lParam). GF LAB_00437230,
    // SpaceApp LAB_0054a2e0, IntroFocus 0x0043d8f0, MissionBriefing LAB_0043d150.
    virtual bool Slot03(unsigned A, unsigned Message, unsigned B, unsigned long Packed) = 0;
    // +0x10, RET 0xc. GF / MissionBriefing LAB_0043d160, SpaceApp LAB_0054a450, IntroFocus
    // 0x004b50e0.
    virtual bool Slot04(unsigned A, unsigned B, unsigned C) = 0;
};

struct IEnvironment : public IInputEvent
{
    // +0x14, RET 0. GF LAB_00437260 (JMP FUN_00558e90); the others 0x00401e90 (a bare RET).
    virtual void Slot05() = 0;
    // +0x18, RET 4. 0x0053e4a0 (a bare RET 4) in all four tables.
    virtual void Slot06(unsigned A) = 0;
    // +0x1c -- the environment type: SpaceApp 0 (0x004b5010), GF 1 (LAB_0051ca40), IntroFocus 2
    // (LAB_00517320), MissionBriefing 3 (LAB_0043d2e0).
    virtual int GetEnvironmentType() = 0;
    // +0x20 -- once, from InitializeEngine. GF LAB_00437270, MissionBriefing LAB_0043d170;
    // SpaceApp / IntroFocus 0x00401e90 (a bare RET).
    virtual void PreInit() = 0;
    // +0x24 -- once, from the atexit teardown. GF LAB_00437290, SpaceApp FUN_0054ab20,
    // MissionBriefing LAB_0043d260; IntroFocus 0x00401e90.
    virtual void Shutdown() = 0;
    // +0x28, RET 0. 0x00401e90 (a bare RET) in all four tables.
    virtual void Slot10() = 0;
    // +0x2c, RET 8 -- the render surface size. GF / IntroFocus LAB_00437400, SpaceApp
    // LAB_0054a540, MissionBriefing 0x0053aaa0 (a bare RET 8).
    virtual void Resize(int Width, int Height) = 0;
    // +0x30, RET 4 -- enter the state; false refuses it (FUN_005b2a30 then returns null).
    virtual bool Init(int Param) = 0;
    // +0x34 -- leave the state.
    virtual void Destroy() = 0;
    // +0x38, RET 4 -- one frame; the answer is the next state id (0 = stay).
    virtual int Update(float DeltaSeconds) = 0;
};
