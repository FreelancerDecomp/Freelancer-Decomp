// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail NN_ArrivalText (RTTI .?AVNN_ArrivalText@@ : WinControl) -- the "<BASE>, <SYSTEM>" /
// "<SYSTEM> <region>" caption that types itself out on arrival (the Ship's jump state 5, the
// space init, the base entry) and fades away after a hold time. One instance at a time: the ctor
// stores itself in DAT_0066d398 (G_NnArrivalText, nn_modal.h) and the dtor clears it.
//
// WINFACTORY<NN_ArrivalText> DAT_0066d35c "NN_ArrivalText": static init LAB_0045ab60
// (FUN_0045b4c0 + atexit LAB_0045ab80 -> FUN_00579380), Create FUN_0045b530 (operator_new(0x33c),
// FUN_0059f860, the vptrs, DAT_0066d398 = this, +0x338 = 0).
//
// vftable 0x005ce85c (45 slots) -- WinControl's 0x005e5e44 with three overrides:
//   0 FUN_0045ab90 (scalar deleting dtor around FUN_0045abb0), 9 FUN_0045abd0 Create,
//   11 LAB_0045acc0 Update. IDataViewOwner face at +0x7c: WinControl's (LAB_0053aaa0 / 0053e4a0).
//
// Layout:
//   +0x000 WinControl (0x32c)
//   +0x32c "ArrivalText" TextControl (Create)
//   +0x330 float hold time once the caption is fully typed (Create: 2.0f)
//   +0x334 float delay before the caption shows (Create: 4.0f; the Show* methods reset it)
//   +0x338 byte: the caption waits for the letterbox to go away (ctor 0; FUN_0045aff0 sets it)
//   size 0x33c

#include "wincontrol_base.h"

#include <stddef.h>

class NnTextControl;

class NnArrivalText : public WinControl
{
public:
    // Inlined into FUN_0045b530 (0x0045b569..0x0045b583): WinControl ctor, the vptrs,
    // DAT_0066d398 = this, +0x338 = 0. +0x32c..+0x334 are left to Create.
    NnArrivalText();
    // FUN_0045abb0: the vptrs, DAT_0066d398 = 0, then FUN_0059fa50 (the WinControl dtor).
    ~NnArrivalText() override;

    // slot 9 FUN_0045abd0 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 LAB_0045acc0
    void Update() override;

    // FUN_0045ae90 (thiscall, RET 4) -- docked only: "<base>, <system>" (ids 0x55c) with the
    // delay set so the caption finishes typing HoldTime before ArrivalTime runs out.
    void ShowBaseArrival(float ArrivalTime);
    // FUN_0045aff0 (thiscall, RET 4) -- the system caption: ids 0x55e with the region the
    // system's strid_name falls in, or ids 0x55d alone; waits for the letterbox.
    void ShowSystemArrival(bool bResetDelay);
    // FUN_0045b1c0 (thiscall, RET 4) -- "<NameIds>, <system>" (ids 0x55c), delay 2.0f.
    void ShowNamedArrival(unsigned NameIds);
    // FUN_0045b2c0 (thiscall) -- bit 1 of the caption's Win3D flags (it is showing).
    bool IsCaptionVisible() const;

    NnTextControl* Caption;      // +0x32c
    float HoldTime;              // +0x330
    float Delay;                 // +0x334
    unsigned char bWaitForLetterbox; // +0x338
    unsigned char Pad339[3];     // +0x339

    // The teardown written out at 0x0045ace0, 0x0045ad90, 0x0045ae63 and in FUN_0045b380 /
    // FUN_0045b3c0: FUN_0043d2a0 on every child (the next sibling read first), slot 10 Destroy,
    // then slot 0 with 1 (delete). `this` is gone afterwards.
    void DestroySelf();

private:
    NnArrivalText(NnArrivalText const&) = delete;
    NnArrivalText& operator=(NnArrivalText const&) = delete;
};

static_assert(sizeof(NnArrivalText) == 0x33c, "retail NN_ArrivalText is 0x33c bytes (0x0045b548)");
static_assert(offsetof(NnArrivalText, Caption) == 0x32c, "NN_ArrivalText +0x32c caption");
static_assert(offsetof(NnArrivalText, HoldTime) == 0x330, "NN_ArrivalText +0x330 hold");
static_assert(offsetof(NnArrivalText, Delay) == 0x334, "NN_ArrivalText +0x334 delay");
static_assert(offsetof(NnArrivalText, bWaitForLetterbox) == 0x338, "NN_ArrivalText +0x338");

// The cdecl entry points. Each "ensure" makes the window through the WinFactory
// (FUN_0059db20("NN_ArrivalText", "NN_ArrivalText", 0, 0)) when DAT_0066d398 is null and gives up
// when it still is.
// FUN_0045b2d0 -- ensure, then ShowBaseArrival(ArrivalTime). Caller 0x00558a04.
void NnArrivalTextShowBase(float ArrivalTime);
// FUN_0045b340 -- ensure, then ShowNamedArrival(NameIds). Caller 0x00514cc3 (FUN_00514bf0).
void NnArrivalTextShowNamed(unsigned NameIds);
// FUN_0045b380 -- tear the window down when there is one. Callers 0x0043b40d, 0x0043b862,
// 0x0043b978, 0x0045a184.
void NnArrivalTextDestroy();
// FUN_0045b3c0 -- a pending caption (delay above 0) has its delay cut to at most 1.0f; one whose
// delay has run out is torn down. Caller 0x00456883.
void NnArrivalTextHurry();
// FUN_0045b430 -- ensure, then Delay = DelayBits (copied as a dword), no letterbox wait, and the
// caption's property 0x1e with Arg. Caller 0x0044eedd.
void NnArrivalTextShowRaw(float Delay, uintptr_t Arg);
// FUN_0045b490 -- ensure, then ShowSystemArrival(true). Caller 0x0054adec (the space init).
void NnArrivalTextShowSystemNow();
