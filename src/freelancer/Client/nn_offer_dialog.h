// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail NN_OfferDialog (RTTI .?AVNN_OfferDialog@@ : WinControl, Event::Handler) -- the mission /
// bar offer box a Gf ScriptSequence raises: a text, a title by offer type, and Accept / Decline
// or a single Close button. The answer goes to the owner's callback.
//
// WINFACTORY<NN_OfferDialog> DAT_00672108 "NN_OfferDialog": static init LAB_004a60f0 (ctor
// FUN_004a6960, atexit LAB_004a6110 -> FUN_00579380), Create FUN_004a69d0 (operator_new(0x364),
// WinControl ctor FUN_0059f860, the vptrs, DAT_0067dcc8 = 1, +0x338 = 0, +0x10 = the factory
// name, slot 9; on failure the scalar deleting dtor).
//
// vftable 0x005d443c (45 slots) -- WinControl's with these overrides:
//   0 FUN_004a6120 (scalar deleting dtor around FUN_004a6140), 9 FUN_004a61b0 Create,
//   42 LAB_004a6590 SetProperty.
// IDataViewOwner face vftable 0x005d442c; Event::Handler face +0x32c vftable 0x005d4424
// (LAB_004a6600).
//
// Layout:
//   +0x000 WinControl (0x32c)
//   +0x32c Event::Handler vptr
//   +0x330 the answer callback, +0x334 its context (FUN_004a6630)
//   +0x338 1: only the Accept button is shown and Escape does not answer
//   +0x33c OfferDialogAcceptButton (WireButtonControl), +0x340 OfferDialogAcceptText
//   +0x344 OfferDialogCloseButton (ButtonControl), +0x348 OfferDialogTitle
//   +0x34c OfferDialogOfferText (RichTextWin), +0x350 never written
//   +0x354 OfferDialogDeclineButton, +0x358 OfferDialogDeclineText
//   +0x35c OfferDialogClose2Button, +0x360 OfferDialogCloseText
//   size 0x364

#include "event_handler.h"
#include "wincontrol_base.h"

#include <stddef.h>

struct FmtStr;
class RichTextWin;

// The answer callback (cdecl): Answer 0 = Accept, 1 = Decline / Close / Escape.
typedef void(__cdecl* FNnOfferDialogCallback)(void* Context, int Answer);

class NnOfferDialog : public WinControl, public EventHandler
{
public:
    // Inlined into FUN_004a69d0: WinControl ctor, DAT_0067dcc8 = 1, +0x338 = 0.
    NnOfferDialog();
    // FUN_004a6140: DAT_0067dcc8 = 0, then the Event::Handler (FUN_00576010) and WinControl
    // (FUN_0059fa50) base dtors.
    ~NnOfferDialog() override;

    // slot 9 FUN_004a61b0 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 42 LAB_004a6590 (RET 0xc)
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // Event::Handler face, LAB_004a6600 (RET 4)
    bool OnEvent(unsigned MsgId) override;

    // FUN_004a6630 (thiscall, RET 0x14) -- show Text under the title of offer Type (0..4) with
    // the buttons of that type (Accept / Decline / Close for 0, 1, 3; Close2 for 2, 4);
    // bSingleButton leaves only Accept, centred. Callers: the Gf ScriptSequence 0x00455256 /
    // 0x00455980.
    void Open(int Type, FmtStr const* Text, bool bSingleButton, FNnOfferDialogCallback Callback,
              void* Context);

    FNnOfferDialogCallback Callback; // +0x330
    void* Context;                   // +0x334
    unsigned char bSingleButton;     // +0x338
    unsigned char Pad339[3];
    WinControl* AcceptButton;        // +0x33c
    WinControl* AcceptText;          // +0x340
    WinControl* CloseButton;         // +0x344
    WinControl* Title;               // +0x348
    RichTextWin* OfferText;          // +0x34c
    unsigned Field_0x350;            // +0x350 TODO: never written by the class; meaning unknown
    WinControl* DeclineButton;       // +0x354
    WinControl* DeclineText;         // +0x358
    WinControl* Close2Button;        // +0x35c
    WinControl* CloseText;           // +0x360
};

static_assert(sizeof(void*) != 4 || sizeof(NnOfferDialog) == 0x364,
              "retail NN_OfferDialog is 0x364 bytes (operator_new 004a69e8)");
static_assert(sizeof(void*) != 4 || offsetof(NnOfferDialog, Callback) == 0x330,
              "NN_OfferDialog +0x330 callback");
static_assert(sizeof(void*) != 4 || offsetof(NnOfferDialog, bSingleButton) == 0x338,
              "NN_OfferDialog +0x338 single-button byte");
static_assert(sizeof(void*) != 4 || offsetof(NnOfferDialog, AcceptButton) == 0x33c,
              "NN_OfferDialog +0x33c accept button");
static_assert(sizeof(void*) != 4 || offsetof(NnOfferDialog, CloseText) == 0x360,
              "NN_OfferDialog +0x360 close text");
