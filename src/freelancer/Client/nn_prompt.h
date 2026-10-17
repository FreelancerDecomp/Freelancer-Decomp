// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail NN_Prompt (RTTI .?AVNN_Prompt@@ : WinControl, Event::Handler) -- the NeuroNet modal
// question box. Three modes, chosen by the Create payload's third dword (+0x33c):
//   0 -- a text-entry prompt: "PromptText", the "PromptEdit" RichTextEditWin and one "PromptOk";
//   1 -- a yes/no prompt with the ids-captioned "PromptYes" (0x4dc) / "PromptNo" (0x4dd);
//   2 -- a two-choice prompt whose button captions the caller supplies as RDLs.
// Every mode also gets the "PromptFrame" chrome and the "PromptExit" X box.
// Raised and pumped by FUN_00473f70 (mode 0), FUN_004740f0 (mode 1) and FUN_00474260 (mode 2).
//
// WINFACTORY<NN_Prompt> DAT_0066db44 "NN_Prompt": static init LAB_004729d0 (FUN_004749c0 +
// atexit LAB_004729f0 -> FUN_00579380), Create FUN_00474a30 (operator_new(0x354)).
//
// vftable 0x005d07bc -- WinControl's with these overrides:
//   0 FUN_00472a00 (scalar deleting dtor around FUN_00472a20), 9 FUN_00472ab0 Create,
//   13 FUN_00488960 Draw, 23 LAB_00473240 Slot23, 42 LAB_00473420 SetProperty.
// Event::Handler face +0x32c vftable 0x005d07a4 (LAB_00473070).
//
// Layout:
//   +0x000 WinControl (0x32c), +0x32c Event::Handler vptr
//   +0x330 byte, never written; +0x331 byte, ctor 0, never read
//   +0x334 int* result slot, +0x338 std::wstring* edit-text destination, +0x33c mode
//   +0x340 "PromptText" (RichTextWin), +0x344 "PromptEdit" (RichTextEditWin, mode 0 only)
//   +0x348 "PromptExit" (ButtonControl), +0x34c "PromptNo", +0x350 "PromptOk" / "PromptYes"
//   (RichTextWireButtonControl)
//   size 0x354

#include "event_handler.h"
#include "wincontrol_base.h"

#include <stddef.h>
#include <string>

class RenderDisplayList;
class RichTextEditWin;
class RichTextWin;
class RichTextWireButtonControl;

// The Create payload (three dwords on the caller's stack: local_14 / local_10 / local_c).
struct FNnPromptPayload
{
    int YesToolTipIds; // [0] -- the OK / Yes box's tool tip; 0 keeps the default (see Create)
    int NoToolTipIds;  // [1] -- the No box's tool tip
    int Mode;          // [2] -- +0x33c
};

class NnPrompt : public WinControl, public EventHandler
{
public:
    // Inlined into FUN_00474a30: WinControl ctor, the vptrs, the nine fields zeroed,
    // ++DAT_0067dccc. Unlike NN_Error there is no FUN_0042d6c0.
    NnPrompt();
    // FUN_00472a20
    ~NnPrompt() override;

    // slot 9 FUN_00472ab0
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 13 FUN_00488960
    void Draw() override;
    // slot 23 LAB_00473240
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;
    // slot 42 LAB_00473420
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // Event::Handler face, LAB_00473070
    bool OnEvent(unsigned MsgId) override;

    // FUN_00473770 (RET 8) -- a list {StyleNode(StyleId)}, then the ids text (FUN_0057da40);
    // only when that loads is PromptText cleared and given the list.
    void SetPromptIds(unsigned Ids, unsigned short StyleId);
    // FUN_004738e0 (RET 8) -- PromptText cleared, StyleNode(StyleId), then every node of Text.
    void SetPromptRdl(RenderDisplayList const* Text, unsigned short StyleId);
    // FUN_00473970 (RET 8) -- Which 0 is the Yes box, 1 the No box: its caption cleared,
    // StyleNode(0x828b), JustifyNode(1), then every node of Caption.
    void SetButtonRdl(int Which, RenderDisplayList const* Caption);

    unsigned char Byte330;                // +0x330 -- no instruction writes it
    unsigned char Byte331;                // +0x331 -- ctor 0
    unsigned char Pad332[2];              // +0x332
    int* Result;                          // +0x334
    std::wstring* EditText;               // +0x338
    int Mode;                             // +0x33c
    RichTextWin* PromptText;              // +0x340
    RichTextEditWin* PromptEdit;          // +0x344
    WinControl* PromptExit;               // +0x348
    RichTextWireButtonControl* PromptNo;  // +0x34c
    RichTextWireButtonControl* PromptYes; // +0x350

private:
    // Inlined at every close path: with PromptEdit and EditText, FUN_0057c2c0 into a 0x400
    // wchar buffer and the buffer assigned to *EditText.
    void CopyEditText();
    // Inlined at 0047312b / 004731f3 / 0047330b / 004733d0 / 004734ff / 004736b4: slot 33
    // ReleaseMouse, FUN_0043d2a0 on every child (next read first), slot 10 Destroy, delete.
    void Close();
};

static_assert(sizeof(NnPrompt) == 0x354, "retail NN_Prompt is 0x354 bytes (operator_new 00474a49)");
static_assert(offsetof(NnPrompt, Byte331) == 0x331, "NN_Prompt +0x331");
static_assert(offsetof(NnPrompt, Result) == 0x334, "NN_Prompt +0x334 result slot");
static_assert(offsetof(NnPrompt, EditText) == 0x338, "NN_Prompt +0x338 edit text");
static_assert(offsetof(NnPrompt, Mode) == 0x33c, "NN_Prompt +0x33c mode");
static_assert(offsetof(NnPrompt, PromptText) == 0x340, "NN_Prompt +0x340 PromptText");
static_assert(offsetof(NnPrompt, PromptEdit) == 0x344, "NN_Prompt +0x344 PromptEdit");
static_assert(offsetof(NnPrompt, PromptExit) == 0x348, "NN_Prompt +0x348 PromptExit");
static_assert(offsetof(NnPrompt, PromptNo) == 0x34c, "NN_Prompt +0x34c PromptNo");
static_assert(offsetof(NnPrompt, PromptYes) == 0x350, "NN_Prompt +0x350 PromptOk / PromptYes");

// FUN_00473f70 (cdecl) -- mode 0. Raise (or reuse) the NN_Prompt, centre it, show Ids in style
// 0x5c12, seed the edit with *Text, point the dialog at *Text and at a local result, capture the
// mouse, then pump frames until it is gone. False when the message pump sees WM_QUIT; otherwise
// result == 1 (OK or event 0x13). Text is not null-tested.
bool NnPromptRunTextEntry(unsigned Ids, std::wstring* Text, bool bUpdateServer,
                          int OkToolTipIds);

// FUN_004740f0 (cdecl) -- mode 1, the same loop. True only when the answer was Yes (result 1).
bool NnPromptRunYesNo(unsigned Ids, unsigned short StyleId, bool bUpdateServer,
                      int YesToolTipIds, int NoToolTipIds);

// FUN_00474260 (cdecl) -- mode 2, the same loop with RDL text and captions. False on WM_QUIT
// or when the dialog was closed without a choice (result 0: the X box or events 0xf / 0x12);
// Yes (result 2) -> *OutChoice = 0, No (result 3) -> *OutChoice = 1; true. Any other result
// (1, from Slot23 0xf004) returns true and leaves *OutChoice alone.
bool NnPromptRunChoice(RenderDisplayList const* Text, unsigned short StyleId,
                       RenderDisplayList const* YesCaption, RenderDisplayList const* NoCaption,
                       int* OutChoice, bool bUpdateServer);
