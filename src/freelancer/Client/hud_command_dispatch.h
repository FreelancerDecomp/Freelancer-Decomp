// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Client/Interface/KeyMap.cpp — HUD / KeyCmd message post + handler tree.

// Event::Handler is the single canonical command receiver.
class EventHandler;
using FHudCmdHandler = EventHandler;

// DAT_0067c278 -- the record of the event being dispatched (seven dwords). FUN_00576410 writes
// the id at +0; the poster fills the rest before posting. Readers reach it through
// FUN_00576240 (HudCommandRecord). Field meanings as the Win3D input handler FUN_0059d140 and
// the key dispatcher FUN_0059dfd0 read them:
struct FHudCmdRecord
{
    unsigned MsgId;          // +0x00
    unsigned Key;            // +0x04 -- key code (event 1)
    unsigned Modifiers;      // +0x08 -- DAT_0067c280: Shift 1 / Ctrl 4 / Alt 0x10 (FUN_005763b0)
    unsigned Field0C;        // +0x0c -- DAT_0067c284: raw window message (FUN_00577850)
    unsigned Arg10;          // +0x10 -- DAT_0067c288: wParam (event 2 first argument)
    unsigned Arg14;          // +0x14 -- DAT_0067c28c: lParam (event 2 second argument)
    unsigned char Byte18;    // +0x18 -- DAT_0067c290: key down (FUN_00577850) / set by FUN_005763b0
    unsigned char Byte19;    // +0x19 -- DAT_0067c291: auto-repeat key event (FUN_00577850)
    unsigned char Byte1A;    // +0x1a -- mouse events: second flag (FUN_0059d140 0059d184)
    unsigned char Byte1B;    // +0x1b
};

// FUN_00576240 -- &DAT_0067c278.
FHudCmdRecord* HudCommandRecord();

// FUN_005763b0(msgId, bSkipMods): if bSkipMods==0 capture Shift/Ctrl/Alt into
// DAT_0067c280 and set DAT_0067c290=1; then FUN_00576410(msgId, 0). The tail is a JMP, so the
// result is FUN_00576410's (HUD_Target's SetProperty 0x004e1b04 / 0x004e1b8e returns its low byte).
int HudPostCommand(unsigned MsgId, int bSkipMods);

// FUN_00576410(msgId, mode): walk DAT_0067c3a4 handler tree; call handler vt[0].
int HudDispatchCommand(unsigned MsgId, int Mode);

// FUN_005760d0 — register Handler for MsgId into DAT_0067c3a4 multimap (after existing entries
// with the same id; duplicates allowed).
void HudRegisterCommandHandler(FHudCmdHandler* Handler, unsigned MsgId);

// FUN_005760f0 — erase Handler's entries for one MsgId (the tree is kept even if it empties).
void HudUnregisterCommand(FHudCmdHandler* Handler, unsigned MsgId);

// FUN_00576190 — erase every entry whose handler == Handler; free the tree once it is empty.
void HudUnregisterCommandHandler(FHudCmdHandler* Handler);

// FUN_005762f0 — cancel stack DAT_0067c270: skip the first Skip entries holding Handler and
// unlink every later one.
void HudRemoveCancelHandler(FHudCmdHandler* Handler, int Skip);

// FUN_00576360 — HudRemoveCancelHandler(Handler, Skip), then push Handler on the cancel stack;
// USER_CANCEL (0xf) in mode 1 pops it and calls its slot 0 with 0x10 (FUN_00576410 tail).
void HudPushCancelHandler(FHudCmdHandler* Handler, int Skip);

// FUN_00576010 — Event::Handler base destructor body: HudPopInputFocus, erase every
// registration (freeing an emptied tree), HudRemoveCancelHandler(Handler, 0).
void HudDestroyEventHandler(FHudCmdHandler* Handler);

// FUN_00577b40 — make Handler the Mode-1 filter (DAT_0067c3ac); a filter already in place is
// pushed onto DAT_0067c3a8. Modal NeuroNet dialogs call it when they open.
void HudPushInputFocus(FHudCmdHandler* Handler);

// FUN_00577c30 — drop Handler: if it is the active filter the last pushed one comes back,
// otherwise it is unlinked from the stack. An emptied stack is freed.
void HudPopInputFocus(FHudCmdHandler* Handler);

// FUN_00577d50 — the active Mode-1 filter (DAT_0067c3ac).
FHudCmdHandler* HudGetInputFocus();

// The 0x28-byte command record FUN_0054bcb0 routes. HUD_Button's click (LAB_004c9430) zeroes
// ten dwords, sets +0x26 = 1 and the id at +0; the other posters (0x0049407d, 0x004cef01 ...)
// fill the target fields as well.
struct FSpaceCommand
{
    int Id;                        // +0x00 -- 0 ends routing after the camera's first look
    unsigned char Pad04[0x0c];
    unsigned ObjectId;             // +0x10
    unsigned SubObjectId;          // +0x14
    unsigned char Pad18[0x0e];
    unsigned char Byte26;          // +0x26
    unsigned char Pad27;
};
static_assert(sizeof(FSpaceCommand) == 0x28, "retail command record is 0x28 bytes");

// FUN_0054bcb0 (cdecl, one argument) -- route a command through the in-flight camera
// controller. False when no controller exists (DAT_00678ba0); otherwise the active camera
// (DAT_00679738) sees it through its slot 0x70, a record whose id is then 0 is consumed, slot
// 0x74 may consume it, and ids above 0xe go on to the watched client ship's slot 0x1cc. True
// whenever the controller exists and the id is above 0xe or a camera consumed it.
bool SpaceSendCommand(FSpaceCommand* Command);
