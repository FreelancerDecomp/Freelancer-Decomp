// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail MsgWin (RTTI .?AVMsgWin@@ : RichTextWin) -- the chat message window, DAT_0066da80.
// FUN_0046b580 (ChatCreateMsgWin) builds it through WINFACTORY<MsgWin> DAT_0066d47c ("MsgWin",
// ctor FUN_0046c0a0 from LAB_00464d60, create FUN_0046c110, operator_new(0x4a0)).
//
// vftable 0x005cffc4 (48 slots) is RichTextWin's except:
//    0 FUN_0046c4a0 (scalar deleting dtor around FUN_0046c4c0)
//    4 FUN_0053f330 HitTest (XOR AL,AL / RET 4) -- RichTextWin has LAB_0057aa80 here
//   11 LAB_0046c1d0 Update
// The IDataViewOwner face at +0x7c keeps WinControl's entries.
//
// Layout: +0x000 RichTextWin (0x498), +0x498 FChatDisplaySnapshot* (the snapshot it shows --
// DAT_0066da68, the store's Recent), +0x49c the snapshot generation last shown. Size 0x4a0.
// The ctor (inlined in FUN_0046c110) and the dtor FUN_0046c4c0 zero both.

#include "chat_init.h"
#include "rich_text_win.h"

class MsgWin : public RichTextWin
{
public:
    MsgWin();
    // FUN_0046c4c0
    ~MsgWin() override;

    // slot 4 FUN_0053f330 -- always false: the window is never picked.
    bool HitTest(float const* Ray) override;

    // slot 11 LAB_0046c1d0 -- post the "lost / new affiliation" notices for the local client
    // from Common's changed-affiliation map (then empty it), let the Recent snapshot age, and
    // re-show it when its generation moved on.
    void Update() override;

    // FUN_0046c410 -- ClearContent, one RDLRefNode on the snapshot's display list, remember the
    // generation.
    void RefreshFromSnapshot();

    FChatDisplaySnapshot* Snapshot;     // +0x498
    int SnapshotGeneration;             // +0x49c

private:
    MsgWin(MsgWin const&) = delete;
    MsgWin& operator=(MsgWin const&) = delete;
};

// FUN_0046b580 (cdecl) -- with no MsgWin yet and a Recent snapshot (DAT_0066da68): build
// "MsgWin", place it at {-0.51, 0.25, -1}, 1.0 x 0.39, bind it to the snapshot, show it
// (Flags |= 3), no frame, align mode 3, no auto scrollbar. Caller: the multiplayer entry of the
// menu state machine (0x00574994).
void ChatCreateMsgWin();
