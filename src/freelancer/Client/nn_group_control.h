// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail NN_GroupControl (RTTI .?AVNN_GroupControl@@ : WinControl) -- the multiplayer group /
// trade request window. One instance: ChatInit (FUN_004690c0) builds it through
// WINFACTORY<NN_GroupControl> DAT_0066d8f4 ("NN_GroupControl", create FUN_0046c000,
// operator_new(0x4a4)) and keeps it in DAT_0066da84 (G_ChatGroupControl).
//
// vftable 0x005cf124 (45 slots, WinControl's except):
//   0 FUN_00460090 (scalar deleting dtor around FUN_00460170)
//   9 FUN_00460550 Create           10 LAB_00460540 (JMP FUN_0059fb60, WinControl::Destroy)
//  11 FUN_00462ba0 Update           12 LAB_00462ab0 UpdateInactive
//  23 FUN_00462a40 Slot23           42 LAB_00462bd0 SetProperty
// The IDataViewOwner face at +0x7c keeps WinControl's two entries.
//
// Layout (ctor FUN_0045fd70):
//   +0x000 WinControl (0x32c)
//   +0x32c FChatChannelTable* (FUN_00460520; the chat's DAT_0066da7c)
//   +0x330 the table generation last shown
//   +0x334 CHAT_ID of the request on show (G_ChatIdNone when none)
//   +0x338 its ship id (0: a group invitation, else a trade request)
//   +0x33c dword, ctor 0, no other access in the listing
//   +0x340 set<CHAT_ID> -- the local player's group (0x14 VC6 tree; ctor FUN_0046d100)
//   +0x354 list<FNnGroupRequest> -- pending requests, oldest first (FUN_00489f40 sentinel)
//   +0x360 .. +0x480  25 RenderDisplayList texts (ENnGroupText)
//   +0x48c BtnClose, +0x490 BtnLeave, +0x494 BtnAccept, +0x498 BtnDecline,
//   +0x49c RichTextWin "RequestDisplay", +0x4a0 RichTextWin "AttentionTitle"
//   size 0x4a4

#include "chat_init.h"
#include "wincontrol_base.h"

#include <list>
#include <set>

class RichTextWin;
class RichTextWireButtonControl;

// One pending request -- the VC6 list node's value (+0x08 .. +0x10).
struct FNnGroupRequest
{
    int TimestampMs;    // +0x08 clock()/1000*1000 when it arrived
    CHAT_ID From;       // +0x0c
    unsigned ShipId;    // +0x10 0 = group invitation, else the trading ship's object id
};

// The 25 texts at +0x360 + 0xc * index, in address order. FUN_00460550 loads them.
enum ENnGroupText
{
    kNnGroupTextLeaveGroup = 0,         // +0x360 style 0x828b, IDS 0x60020
    kNnGroupTextInviteFrom,             // +0x36c IDS 0x60064
    kNnGroupTextTradeFrom,              // +0x378 IDS 0x6006f
    kNnGroupTextAccept,                 // +0x384 style 0x828b, IDS 0x60013
    kNnGroupTextDecline,                // +0x390 style 0x828b, IDS 0x6001a
    kNnGroupTextAcceptTrade,            // +0x39c style 0x828b, IDS 0x60014
    kNnGroupTextDeclineTrade,           // +0x3a8 style 0x828b, IDS 0x6001b
    kNnGroupTextNoInvitations,          // +0x3b4 style 0x9ca4, IDS 0x6006d
    kNnGroupTextInviteSent,             // +0x3c0 style 0x3ea8, IDS 0x60065
    kNnGroupTextJoinedGroup,            // +0x3cc style 0x3ea8, IDS 0x60067
    kNnGroupTextNewMember,              // +0x3d8 style 0x3ea8, IDS 0x60066
    kNnGroupTextLeftGroup,              // +0x3e4 style 0x3ea8, IDS 0x60069
    kNnGroupTextMemberLeft,             // +0x3f0 style 0x3ea8, IDS 0x60068
    kNnGroupTextAbortMission,           // +0x3fc style 0x3ea8, IDS 0x60060
    kNnGroupTextJoinFailedMission,      // +0x408 style 0x3ea8, IDS 0x60061
    kNnGroupTextCannotInviteSelf,       // +0x414 style 0x3ea8, IDS 0x6005b
    kNnGroupTextGroupFull,              // +0x420 style 0x3ea8, IDS 0x60059
    kNnGroupTextAlreadyInYourGroup,     // +0x42c style 0x3ea8, IDS 0x6005a
    kNnGroupTextCannotJoinOwnGroup,     // +0x438 style 0x3ea8, IDS 0x6005e
    kNnGroupTextNotInvited,             // +0x444 style 0x3ea8, IDS 0x60062
    kNnGroupTextAlreadyMember,          // +0x450 style 0x3ea8, IDS 0x6005d
    kNnGroupTextGroupNowFull,           // +0x45c style 0x3ea8, IDS 0x6005c
    kNnGroupTextNotInGroup,             // +0x468 style 0x3ea8, IDS 0x6005f
    kNnGroupTextPlayerDied,             // +0x474 style 0x3ea8, IDS 0x60051
    kNnGroupTextAttentionRequired,      // +0x480 IDS 0x60012
    kNnGroupTextCount
};

class NnGroupControl : public WinControl
{
public:
    // FUN_0045fd70
    NnGroupControl();
    // FUN_00460170
    ~NnGroupControl() override;

    // slot 9 FUN_00460550 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 FUN_00462ba0
    void Update() override;
    // slot 12 LAB_00462ab0
    void UpdateInactive() override;
    // slot 23 FUN_00462a40 (RET 0xc) -- 0xef01 / 0xef02
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;
    // slot 42 LAB_00462bd0 (RET 0xc) -- 0x26, a child button clicked
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // FUN_00460520 (RET 4) -- +0x32c = Table, then FUN_00462e30.
    void SetChannelTable(FChatChannelTable* Table);
    // FUN_00462a30 (thiscall on a control) -- Flags |= 3. Create calls it on BtnDecline.
    static void ShowControl(Win3D* Control);
    // FUN_00462e30 -- show the oldest request (or "no invitations") and the buttons that go
    // with it.
    void RefreshRequestDisplay();
    // FUN_004632f0 (RET 4) -- a trade request from the ship ShipId (ChatGroupTradeRequestReceived).
    void AddTradeRequest(unsigned ShipId);
    // FUN_00463630 (RET 8) -- the group wire message (ChatDispatchIncoming, channel {4, 1}).
    void HandleInviteMessage(void const* Buffer, unsigned SizeBytes);

    FChatChannelTable* ChannelTable;            // +0x32c
    int ChannelTableGeneration;                 // +0x330
    CHAT_ID ShownFrom;                          // +0x334
    unsigned ShownShipId;                       // +0x338
    int Field_0x33c;                            // +0x33c ctor 0; TODO: meaning (no other access)
    std::set<unsigned> GroupMembers;            // +0x340 keyed by CHAT_ID::Id
    std::list<FNnGroupRequest> Requests;        // +0x354
    RenderDisplayList Texts[kNnGroupTextCount]; // +0x360
    WinControl* BtnClose;                       // +0x48c
    WinControl* BtnLeave;                       // +0x490
    RichTextWireButtonControl* BtnAccept;       // +0x494
    RichTextWireButtonControl* BtnDecline;      // +0x498
    RichTextWin* RequestDisplay;                // +0x49c
    RichTextWin* AttentionTitle;                // +0x4a0

private:
    NnGroupControl(NnGroupControl const&) = delete;
    NnGroupControl& operator=(NnGroupControl const&) = delete;
};

// FUN_0045e360 (cdecl) -- one 8-byte group-channel record {Opcode, Player's low word} from this
// player (G_ChatServer SubmitChat to G_ChatIdGroupInvite, system id -1); nothing without a chat
// server. Caller: HUD_Target's invite command 0xc9 (0x004e2cb7, opcode 0 with the target's
// owner). LAB_00462bd0 builds the same record inline for opcodes 1 and 2.
void NnGroupSendWireMessage(unsigned Opcode, CHAT_ID Player);
