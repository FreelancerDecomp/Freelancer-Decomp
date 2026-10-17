// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail NN_Error (RTTI .?AVNN_Error@@ : WinControl, Event::Handler) -- the modal error box
// ("Freelancer Encountered an Error" + explanation + OK). Raised by FUN_00473c00 (non-blocking),
// FUN_00473cb0 (blocking on an ids string: the connect / login failures) and FUN_00473e00
// (blocking on a narrow message: the fatal-error path of WinMain).
//
// WINFACTORY<NN_Error> DAT_0066db70 "NN_Error": static init LAB_004724d0 (FUN_00474880 +
// atexit LAB_004724f0 -> FUN_00579380), Create FUN_004748f0 (operator_new(0x344)).
//
// vftable 0x005d0634 (45 slots) -- WinControl's with these overrides:
//   0 FUN_00472500 (scalar deleting dtor around FUN_00472520), 9 FUN_004725b0 Create,
//   13 FUN_00488960 Draw, 20 LAB_00472940 KeyEvent, 42 LAB_00472880 SetProperty.
// Event::Handler face +0x32c vftable 0x005d0620 (LAB_004727f0).
//
// Layout:
//   +0x000 WinControl (0x32c), +0x32c Event::Handler vptr
//   +0x330 software-cursor draw flag saved by FUN_004a7670
//   +0x334 "Text" (TextControl, the explanation), +0x338 "ErrorOkayButton" (WireButtonControl),
//   +0x33c "ErrorOkayText" (TextControl), +0x340 "ErrorDialogTitle" (TextControl)
//   size 0x344

#include "event_handler.h"
#include "wincontrol_base.h"

#include <stddef.h>

class NnError : public WinControl, public EventHandler
{
public:
    // Inlined into FUN_004748f0: WinControl ctor, FUN_0042d6c0, ++DAT_0067dccc. No field of its
    // own is written before Create.
    NnError();
    // FUN_00472520
    ~NnError() override;

    // slot 9 FUN_004725b0
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 13 FUN_00488960
    void Draw() override;
    // slot 20 LAB_00472940
    bool KeyEvent(unsigned Key, unsigned char* Modifiers) override;
    // slot 42 LAB_00472880
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // Event::Handler face, LAB_004727f0
    bool OnEvent(unsigned MsgId) override;

    unsigned char bCursorDrawFlag; // +0x330
    unsigned char Pad331[3];       // +0x331
    WinControl* Text;              // +0x334
    WinControl* OkayButton;        // +0x338
    WinControl* OkayText;          // +0x33c
    WinControl* Title;             // +0x340

private:
    // The dismiss sequence, inlined at 00472803, 004728d4 and 0047295a: DAT_0066dc04 = 0, the
    // parent's slot 23 with (0x3006, 0, 0), FUN_004a7690(+0x330), slot 33 ReleaseMouse,
    // FUN_0043d2a0 on every child, slot 10 Destroy, delete.
    void Dismiss();
};

static_assert(sizeof(NnError) == 0x344, "retail NN_Error is 0x344 bytes (operator_new 00474908)");
static_assert(offsetof(NnError, bCursorDrawFlag) == 0x330, "NN_Error +0x330");
static_assert(offsetof(NnError, Text) == 0x334, "NN_Error +0x334 Text");
static_assert(offsetof(NnError, OkayButton) == 0x338, "NN_Error +0x338 okay button");
static_assert(offsetof(NnError, Title) == 0x340, "NN_Error +0x340 title");

// FUN_00473c00 (cdecl) -- unless one is registered, build an NN_Error under Parent, centre it,
// hide the title (flags &= 0xfc), set the explanation to Ids and capture the mouse. Returns at
// once.
void NnErrorOpen(unsigned Ids, Win3D* Parent);
// FUN_00473ca0 (cdecl) -- FUN_0059d9c0("NN_Error").
Win3D* NnErrorFind();
// FUN_00473cb0 (cdecl) -- NnErrorOpen's body, then pump frames (Sleep 10, FUN_005b0b60, server
// step, FUN_0059e080, draw) until the dialog is gone or the message queue sees WM_QUIT.
// bUpdateServer picks IServer::Update over the client/server run-permission handshake.
void NnErrorRunModal(unsigned Ids, bool bUpdateServer, Win3D* Parent);
// FUN_00473e00 (cdecl) -- true at once when an NN_Error is already up; otherwise raise one with
// the title shown and Message (narrow, converted with mbstowcs) as the explanation, pump frames
// until it goes away, and return DAT_0066dc04 == 0.
bool NnErrorRunModalMessage(char const* Message);
