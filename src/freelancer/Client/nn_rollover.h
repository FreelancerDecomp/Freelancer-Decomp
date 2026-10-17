// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Rollover (RTTI .?AURollover@@ : TextWin, vftable 0x005d5454, 29 slots, size 0x18c).
// Built by WINFACTORY<Rollover>::Create FUN_004b6630 (the factory is DAT_0067224c, made by the
// static initializer LAB_004b6260 -> FUN_004b65c0) and kept in the singleton DAT_00672288.
// Pre-init FUN_005b1300 creates it through FUN_0059db20("Rollover", "Rollover", 0, 0).
//
// vftable 0x005d5454 against TextWin (0x005d54cc): 0 FUN_004b5260 (dtor FUN_004b5340),
// 4 FUN_0053f330 (Win3D's, not TextWin's HitTest), 9 FUN_004b54d0 Create, 13 LAB_004b5720 Draw,
// 23 LAB_004b5700. Everything else is TextWin's / Win3D's.
//
// Layout past TextWin (0xec):
//   +0x0ec the shown hover record (FToolTipHoverInfo, 0x64; its dtor is FUN_00444ee0)
//   +0x150 the RolloverTable map (VC6 std::map<int, int>, ctor FUN_004b6800, filled by
//          FUN_004b59b0 from interface\Rollover.ini, key = a hover's IdsAlt, value = the ids shown)
//   +0x164 hold time left, +0x16c anchor (3 floats, the SetPosition argument),
//   +0x178 "was visible" latch, +0x17c hidden grace time, +0x180 cursor-still time,
//   +0x184 anchor X outside the intro menu (-0.51f), +0x188 anchor X in the intro menu (0.2f).
// The port holds the map as std::map<int, int>, so the offsets past +0x150 are not retail's.

#include "text_win.h"
#include "tooltip_hover.h"
#include "win_factory.h"

#include <map>

class NnRolloverControl : public TextWin
{
public:
    // FUN_004b6630's inlined part: the TextWin part (TextWin()), the +0xec record
    // (+0xf4 / +0xf8 zeroed, FUN_004144b0 on +0xfc, FUN_004963a0), the map (FUN_004b6800),
    // then FUN_004b4f10.
    NnRolloverControl();
    // FUN_004b5340.
    ~NnRolloverControl() override;

    // slot 4 FUN_0053f330 -- XOR AL,AL / RET 4.
    bool HitTest(float const* Ray) override;
    // slot 9 FUN_004b54d0.
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 13 LAB_004b5720.
    void Draw() override;
    // slot 23 LAB_004b5700 -- message 0xef02 hides the rollover.
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;

    // FUN_004b4f10 -- the post-vtable part of the factory constructor.
    void InitRolloverDefaults();

    // FUN_004b59b0 -- interface\Rollover.ini [RolloverTable] map = key, value into +0x150.
    void LoadRolloverTable();

    // FUN_004b5600 (thiscall, double dt).
    void TickRollover(double DeltaSeconds);

    // FUN_004b5ca0 (RET 4) -- the ToolTip's hover mapped through the table.
    void FeedHover(FToolTipHoverInfo const* Info);

    // FUN_004b5fd0 (RET 8) -- show a record for HoldSeconds at the environment's anchor.
    void ShowHover(FToolTipHoverInfo const* Info, float HoldSeconds);

    // FUN_004b6130 (RET 0xc) -- the same with an explicit anchor X.
    void ShowHoverAt(FToolTipHoverInfo const* Info, float HoldSeconds, float AnchorX);

    // FUN_004b65a0's body on the singleton: +0x164 = 0, +0x6c &= 0xfd (hidden).
    void HideRollover();

private:
    FToolTipHoverInfo Entry;      // +0x0ec
    std::map<int, int> Table;     // +0x150
    float HoldSeconds;            // +0x164
    float Anchor[3];              // +0x16c
    unsigned char bWasVisible;    // +0x178
    float HiddenGrace;            // +0x17c
    float CursorStillSeconds;     // +0x180
    float AnchorXSpace;           // +0x184 = -0.51f
    float AnchorXIntro;           // +0x188 = 0.2f
};

// WINFACTORY<Rollover> (DAT_0067224c). The ctor is FUN_004b65c0 (the WinFactory head, then
// vftable 0x005d55f4); its one slot is FUN_004b6630.
class RolloverFactory : public WinFactory
{
public:
    explicit RolloverFactory(char const* Name)
        : WinFactory(Name)
    {
    }

    // FUN_004b6630.
    Win3D* Create(Win3D* Parent, void* Payload) override;
};

NnRolloverControl* GetNnRolloverSingleton();

// FUN_004b6290 (cdecl, double) -- tick the singleton when there is one.
void NnRolloverTickGlobal(double DeltaSeconds);
// FUN_004b62b0 (cdecl) -- FeedHover on the singleton when there is one. Caller: the ToolTip's
// FUN_004bdea0 (0x004bdecd).
void NnRolloverFeedHoverGlobal(FToolTipHoverInfo const* Info);
// FUN_004b62d0 (cdecl) -- ShowHover on the singleton when there is one. Caller 0x0045a379.
void NnRolloverShowHoverGlobal(FToolTipHoverInfo const* Info, float HoldSeconds);
// FUN_004b62f0 (cdecl) -- with the singleton: a record {Ids, Ids, empty caption} through
// ShowHover. Callers: the NavBar 0x0044235d / 0x004423c4 / 0x0044242f.
void NnRolloverShowIdsGlobal(int Ids, float HoldSeconds);
// FUN_004b6420 (cdecl) -- ShowHoverAt on the singleton when there is one. Callers: NN_Preferences
// FUN_004b2130 (0x004b2254, 0x004b2396).
void NnRolloverShowHoverAtGlobal(FToolTipHoverInfo const* Info, float HoldSeconds,
                                 float AnchorX);
// FUN_004b6440 (cdecl) -- with the singleton: a record {Ids, Ids, empty caption} through
// ShowHoverAt. Caller: NN_Preferences::SetProperty 0x004ac1b4 (0x68c, 0, 0.2f).
void NnRolloverShowIdsAtGlobal(int Ids, float HoldSeconds, float AnchorX);
// FUN_004b65a0 (cdecl) -- with the singleton up, its hold time (+0x164) = 0 and its visible bit
// cleared (+0x6c &= 0xfd). Caller: NN_Pause::Create 0x004a6bb8.
void NnRolloverHideGlobal();
// FUN_004b6580 (cdecl) -- the singleton's slot 10 (Destroy), then, re-read, its deleting dtor.
// Caller: the client shutdown 0x005b2228.
void NnRolloverDestroyGlobal();
