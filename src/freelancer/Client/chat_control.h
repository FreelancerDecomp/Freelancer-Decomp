// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// ChatControl -- the NeuroNet chat panel (RTTI .?AVChatControl@@ : WinControl). Retail
// `NN_Chat.cpp`. NN_MainButton opens it by class name for panel 0x68 (0x0045cfda /
// 0x0045d37b -> WinFactoryCreate(Name, Name, ...)).
//
// Retail:
//   WINFACTORY<ChatControl> DAT_0066d450, static init LAB_00464d90 (registrar FUN_0046c4e0,
//   atexit LAB_00464db0); create FUN_0046c550: operator_new(0x3d0), ctor FUN_00464dc0.
//   vftable 0x005cfac4 (WinControl's except):
//      0 FUN_00464f20 (scalar deleting around the dtor FUN_00464f40)
//      9 LAB_004670c0 Create          10 FUN_00467030 Destroy
//     11 FUN_00468260 Update          12 LAB_004682f0 UpdateInactive (Win3D's, shared)
//     23 LAB_00467f10 Slot23          42 LAB_00468220 SetProperty
//   The IDataViewOwner face at +0x7c keeps WinControl's entries.
//
// Layout past WinControl (0x32c):
//   +0x32c dword, ctor 0, no other access      +0x330 table generation last shown
//   +0x334 / +0x340 / +0x34c / +0x358 RenderDisplayLists: the History / Broadcast / Private
//          titles and the "None" text (IDS 0x60032 / 0x6002c / 0x60033 / 0x60054)
//   +0x364 RenderDisplayList[4] -- the private list's column headers
//   +0x394 GroupColor, +0x398 SystemColor, +0x39c InactiveColor, +0x3a0 uppip, +0x3a4 DownPip
//          (TextRenderContext::Image*, created and destroyed through the private list's TRC)
//   +0x3a8 sort column (ctor 1), +0x3ac 4 direction bytes (ctor 0)
//   +0x3b0 FChatChannelTable*, +0x3b4 MsgWin "HistoryDisplay", +0x3b8 "HistoryTitle",
//   +0x3bc "BroadcastTitle", +0x3c0 "PrivateTitle", +0x3c4 UIScrollTable "BroadcastList",
//   +0x3c8 UIScrollTable "PrivateList", +0x3cc "BtnCCClose". Size 0x3d0.

#include "chat_init.h"
#include "ui_fast_table.h"
#include "wincontrol_base.h"

#include "common_text.h"

class MsgWin;
class RichTextWin;

// One list entry FUN_00465110 / FUN_004669c0 build on the stack (alloca(Names.size() * 12)).
struct FChatControlEntry
{
    unsigned Key;               // +0x0 CHAT_ID::Id
    RenderDisplayList* Name;    // +0x4 the name-map value
    FChatPlayerInfo* Info;      // +0x8 the player-info value, or the shared zero record
};

class ChatControl : public WinControl
{
public:
    // Retail ChatControl::RowData (RTTI .?AVRowData@ChatControl@@ : UIFastTable::RefCounter),
    // vftable 0x005cfc98 = {FUN_0056ae70} (scalar deleting around the RefCounter dtor
    // FUN_00467ee0). Construction is inlined in Create (0x00467907..0x00467924): RefCount 1,
    // +0x8 zeroed, then +0x8 = DAT_0066da40 (G_ChatIdChannel1). 0xc bytes.
    class RowData : public UiFastTableRefCounter
    {
    public:
        RowData()
            : ChatId(G_ChatIdChannel1)
        {
        }

        CHAT_ID ChatId;     // +0x08 -- the row's chat target
    };

    // FUN_00464dc0
    ChatControl();
    // FUN_00464f40
    ~ChatControl() override;

    // slot 9 LAB_004670c0 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 10 FUN_00467030
    void Destroy() override;
    // slot 11 FUN_00468260
    void Update() override;
    // slot 23 LAB_00467f10 (RET 0xc) -- 0xef01 / 0xef02 / 0x300d / 0xf001 / 0xf002
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;
    // slot 42 LAB_00468220 (RET 0xc) -- 0x26 on BtnCCClose
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // FUN_00468310 -- the two lists' item counts from the name table.
    void RefreshCounts();
    // FUN_00465110 (RET 8) -- the broadcast channel rows from FirstItem.
    void FillBroadcast(int FirstItem, unsigned VisibleRows);
    // FUN_004669c0 (RET 8) -- the private list: header row, then two rows per player.
    void FillPrivate(int FirstItem, unsigned VisibleRows);
    // FUN_00465ab0 (RET 0xc) -- one player's two rows of the private list.
    void RenderPrivateRow(unsigned Row, FChatControlEntry const* Entry, bool bEnabled);

    int Field_0x32c;                        // +0x32c ctor 0; TODO: meaning (no other access)
    int TableGeneration;                    // +0x330
    RenderDisplayList HistoryTitleText;     // +0x334
    RenderDisplayList BroadcastTitleText;   // +0x340
    RenderDisplayList PrivateTitleText;     // +0x34c
    RenderDisplayList NoneText;             // +0x358
    RenderDisplayList Headers[4];           // +0x364
    TextRenderContext::Image* GroupColorImage;      // +0x394
    TextRenderContext::Image* SystemColorImage;     // +0x398
    TextRenderContext::Image* InactiveColorImage;   // +0x39c
    TextRenderContext::Image* UpPipImage;           // +0x3a0
    TextRenderContext::Image* DownPipImage;         // +0x3a4
    unsigned SortColumn;                    // +0x3a8
    unsigned char SortAscending[4];         // +0x3ac -- non-zero: the ascending comparators
    FChatChannelTable* Table;               // +0x3b0
    MsgWin* HistoryDisplay;                 // +0x3b4
    RichTextWin* HistoryTitle;              // +0x3b8
    RichTextWin* BroadcastTitle;            // +0x3bc
    RichTextWin* PrivateTitle;              // +0x3c0
    UiScrollTable* BroadcastList;           // +0x3c4
    UiScrollTable* PrivateList;             // +0x3c8
    WinControl* BtnCCClose;                 // +0x3cc

private:
    ChatControl(ChatControl const&) = delete;
    ChatControl& operator=(ChatControl const&) = delete;
};
