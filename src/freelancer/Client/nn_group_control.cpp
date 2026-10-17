// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// NN_GroupControl -- the group / trade request window (see nn_group_control.h for the object).
//
//   FUN_0045fd70 ctor            FUN_00460170 dtor (FUN_00460090 scalar deleting)
//   FUN_00460520 SetChannelTable FUN_00460550 Create
//   FUN_00462a30 ShowControl     FUN_00462a40 Slot23
//   LAB_00462ab0 UpdateInactive  FUN_00462ba0 Update
//   LAB_00462bd0 SetProperty     FUN_00462e30 RefreshRequestDisplay
//   FUN_004632f0 AddTradeRequest FUN_00463630 HandleInviteMessage
//   DAT_0066d8f4 WINFACTORY<NN_GroupControl> (registrar FUN_0046bf90, create FUN_0046c000)

#include "nn_group_control.h"

#include "client_gameobj.h"
#include "client_objdb.h"      // ClientObjDb_FindObject (FUN_005416c0)
#include "hud_maneuvers.h"     // GetHudManeuvers (DAT_00674a58), RequestManeuver (FUN_004d8e20)
#include "ids_rdl.h"
#include "login_mgr.h"         // G_Server (DAT_0067ecd0), G_ChatServer (DAT_0067ecd4)
#include "nn_main_button.h"    // NN_MainButton::SetGroupButtonAlert / SetGroupButtonMesh
#include "nn_modal.h"          // G_NnMainButton (DAT_0066d3f0), NnMainButtonOnModalOpen
#include "nn_ui_pulse.h"       // G_HudChromeColorRgba (DAT_00679bb4)
#include "player_behavior.h"   // GetPlayerObjRW (FUN_0054baf0)
#include "player_spawn.h"      // GetPlayerConnectionId / GetPlayerCharacterId
#include "rich_text_win.h"
#include "win_factory.h"
#include "wire_controls.h"     // RichTextWireButtonControl

#include "common_cobject.h"
#include "common_iface.h"
#include "common_server.h"
#include "dacom.h"

#include <ctime>

// DAT_0066d8f4 -- the static initializer runs FUN_0046bf90(&DAT_0066d8f4, "NN_GroupControl").
WINFACTORY<NnGroupControl> G_NnGroupControlFactory("NN_GroupControl");

namespace
{
    const char kNnChatCpp[] = "E:\\FL\\Scratch\\Source\\Client\\nn\\NN_Chat.cpp";

    // The group channel's 8-byte record (FUN_00463630 reads [+0] and [+4]; LAB_00462bd0 builds
    // one on the stack for SubmitChat).
    struct FNnGroupWireMessage
    {
        unsigned Opcode;    // +0x0
        unsigned Payload;   // +0x4 a player id (low word) or a connection id
    };

    // DAT_0066da5c -- BtnLeave's X. A .bss float nothing in the listing writes, so it is 0.0f;
    // retail reads it from memory where the other button coordinates are .data constants.
    float G_GroupBtnLeaveX = 0.0f;

    // The clock()/1000*1000 stamp (0x10624dd3 multiply, SAR 6, sign fix, IMUL 0x3e8).
    int GroupNowMsRounded()
    {
        int const Ticks = static_cast<int>(clock());
        return (Ticks / 1000) * 1000;
    }

    void GroupNotice(int Line, char const* Message)
    {
        FDUMP(0x100003, "%s(%d) : NOTICE:General:%s", kNnChatCpp, Line, Message);
    }

    // One text of FUN_00460550's table: an optional StyleNode clone pushed first, then the
    // FUN_0057da40 / FUN_0057eb40 / JustifyNode(1) + TextNode(debug) idiom.
    void LoadGroupText(RenderDisplayList& Text, unsigned short Style, unsigned Ids,
                       wchar_t const* DebugText)
    {
        if (Style != 0)
        {
            StyleNode StyleCell(Style);
            RdlAppendNode(Text, StyleCell.Clone());
        }
        LoadIdsTextIntoRdl(Ids, Text, DebugText);
    }

    // The discard every FUN_00463630 exit that built nothing takes (0x00463706..0x00463736,
    // 0x00464c83..0x00464cd2) and FUN_004632f0's no-name exit (0x00463590..0x0046360c).
    void DiscardEntry(FChatMessageEntry* Entry)
    {
        if (G_ChatMessageStore != nullptr)
            G_ChatMessageStore->Discard(Entry);
    }

    // RDLRef(Prefix) clone, then a clone of every node of Name (the FUN_0046c800 loop).
    void AppendPrefixAndName(FChatMessageEntry* Entry, RenderDisplayList& Prefix,
                             RenderDisplayList const* Name)
    {
        RDLRefNode Ref(Prefix);
        RdlAppendNode(Entry->Body, Ref.Clone());
        if (Name != nullptr)
            ChatAppendRdlCopy(Entry->Body, *Name);
    }

    // The FUN_0046cd50 lookup on DAT_0066da7c used by every "name" case: key {Id & 0xffff, 0, 0},
    // hit only when the node exists and its RDL pointer is non-null. Retail reads DAT_0066da7c
    // without a null test at each of these sites.
    RenderDisplayList* FindChatNameForWord(unsigned Word)
    {
        return G_ChatChannelTable->FindName(ChatIdForPlayer(Word & 0xffffu));
    }
}

void NnGroupSendWireMessage(unsigned Opcode, CHAT_ID Player)
{
    // FUN_0045e360 (cdecl, 0x0045e360..0x0045e3b0): DAT_0067ecd4 null -> nothing. Sender =
    // {DAT_00673344 word, 0, 0}; record = {Opcode, MOVZX word of Player}; IChatServer slot 0
    // (SubmitChat)(sender, 8, &record, DAT_0066da34, -1).
    IChatServer* const ChatServer = static_cast<IChatServer*>(G_ChatServer);
    if (ChatServer == nullptr)
        return;
    FNnGroupWireMessage Message;
    Message.Opcode = Opcode;
    Message.Payload = Player.Id & 0xffffu;
    ChatServer->SubmitChat(CHAT_ID(GetPlayerConnectionId() & 0xffffu), sizeof(Message), &Message,
                           G_ChatIdGroupInvite, -1);
}

// ---------------------------------------------------------------------------------------------

NnGroupControl::NnGroupControl()
    : ChannelTable(nullptr)
    , ChannelTableGeneration(0)
    , ShownFrom(G_ChatIdNone)
    , ShownShipId(0)
    , Field_0x33c(0)
    , BtnClose(nullptr)
    , BtnLeave(nullptr)
    , BtnAccept(nullptr)
    , BtnDecline(nullptr)
    , RequestDisplay(nullptr)
    , AttentionTitle(nullptr)
{
    // FUN_0045fd70: WinControl, +0x334 = {0}, the set (FUN_0046d100), the list (FUN_00489f40),
    // 25 empty RDLs, the six control pointers 0, then +0x338/+0x33c/+0x330/+0x32c = 0 and
    // +0x334 = DAT_0066da48.
}

NnGroupControl::~NnGroupControl()
{
    // FUN_00460170: DAT_0066da84 cleared when it is this; then the RDLs 0x480 .. 0x360, the list
    // and the set go (member destruction order), then WinControl's dtor.
    if (G_ChatGroupControl == this)
        G_ChatGroupControl = nullptr;
}

void NnGroupControl::SetChannelTable(FChatChannelTable* Table)
{
    // FUN_00460520
    ChannelTable = Table;
    RefreshRequestDisplay();
}

void NnGroupControl::ShowControl(Win3D* Control)
{
    // FUN_00462a30: OR byte [ECX+0x6c],3.
    Control->Flags = static_cast<unsigned char>(Control->Flags | 0x03);
}

bool NnGroupControl::Create(Win3D* NewParent, void* Data)
{
    // FUN_00460550 -- Win3D::Create FUN_004b5060(NewParent, 0); Data is not passed on.
    (void)Data;
    if (!Win3D::Create(NewParent, nullptr))
        return false;

    // 0x0046058f..0x004605c3
    Offset[0] = 0.0f;
    Offset[1] = 0.052f;     // 0x3d54fdf4
    Offset[2] = 0.0f;
    // 0x004605c6..0x004605df: slot 38 (+0x98), then +0x2d2 = 1.
    SetMesh("Interface\\intro\\objects\\MULTI_attentionreq'd.cmp", 0);
    Field2d2 = 1;

    // 0x004605d3..0x004625cb: the 25 texts, in this order.
    LoadGroupText(Texts[kNnGroupTextInviteFrom], 0, 0x60064u,
                  L"DEBUG - You have received a group invitation from:");
    LoadGroupText(Texts[kNnGroupTextTradeFrom], 0, 0x6006fu,
                  L"DEBUG - You have received a trade request from:");
    LoadGroupText(Texts[kNnGroupTextLeaveGroup], 0x828b, 0x60020u, L"DEBUG - Leave Group");
    LoadGroupText(Texts[kNnGroupTextAccept], 0x828b, 0x60013u, L"DEBUG - Accept");
    LoadGroupText(Texts[kNnGroupTextDecline], 0x828b, 0x6001au, L"DEBUG - Decline");
    LoadGroupText(Texts[kNnGroupTextAcceptTrade], 0x828b, 0x60014u, L"DEBUG - Accept Trade");
    LoadGroupText(Texts[kNnGroupTextDeclineTrade], 0x828b, 0x6001bu, L"DEBUG - Decline Trade");
    LoadGroupText(Texts[kNnGroupTextNoInvitations], 0x9ca4, 0x6006du,
                  L"DEBUG - There are no outstanding group invitations.");
    LoadGroupText(Texts[kNnGroupTextInviteSent], 0x3ea8, 0x60065u,
                  L"DEBUG - Group invitation sent to:");
    LoadGroupText(Texts[kNnGroupTextJoinedGroup], 0x3ea8, 0x60067u,
                  L"DEBUG - You have joined into a group with:");
    LoadGroupText(Texts[kNnGroupTextNewMember], 0x3ea8, 0x60066u, L"DEBUG - New group member:");
    LoadGroupText(Texts[kNnGroupTextLeftGroup], 0x3ea8, 0x60069u,
                  L"DEBUG - You have left your group.");
    LoadGroupText(Texts[kNnGroupTextMemberLeft], 0x3ea8, 0x60068u,
                  L"DEBUG - A group member has left:");
    LoadGroupText(Texts[kNnGroupTextAbortMission], 0x3ea8, 0x60060u,
                  L"DEBUG - You must abort or complete your mission.");
    LoadGroupText(Texts[kNnGroupTextJoinFailedMission], 0x3ea8, 0x60061u,
                  L"DEBUG - A player failed to join your group due to already being involved "
                  L"in a mission: ");
    LoadGroupText(Texts[kNnGroupTextCannotInviteSelf], 0x3ea8, 0x6005bu,
                  L"DEBUG - You cannot invite yourself to join a group.");
    LoadGroupText(Texts[kNnGroupTextGroupFull], 0x3ea8, 0x60059u, L"DEBUG - Your group is full.");
    LoadGroupText(Texts[kNnGroupTextAlreadyInYourGroup], 0x3ea8, 0x6005au,
                  L"DEBUG - You cannot invite a player already in your group.");
    LoadGroupText(Texts[kNnGroupTextCannotJoinOwnGroup], 0x3ea8, 0x6005eu,
                  L"DEBUG - You cannot join your own group.");
    LoadGroupText(Texts[kNnGroupTextNotInvited], 0x3ea8, 0x60062u,
                  L"DEBUG - You have not received an invitation to this group:");
    LoadGroupText(Texts[kNnGroupTextAlreadyMember], 0x3ea8, 0x6005du,
                  L"DEBUG - You are already a member of this group:");
    LoadGroupText(Texts[kNnGroupTextGroupNowFull], 0x3ea8, 0x6005cu,
                  L"DEBUG - Sorry, but this group is now full:");
    LoadGroupText(Texts[kNnGroupTextNotInGroup], 0x3ea8, 0x6005fu,
                  L"DEBUG - You cannot leave your group because you are not in a group.");
    LoadGroupText(Texts[kNnGroupTextPlayerDied], 0x3ea8, 0x60051u, L"DEBUG - A player has died:");
    LoadGroupText(Texts[kNnGroupTextAttentionRequired], 0, 0x60012u,
                  L"DEBUG - ATTENTION REQUIRED");

    // 0x004625d1..0x0046267b
    RequestDisplay = static_cast<RichTextWin*>(
        WinFactoryCreate("RequestDisplay", "RichTextWin", this, nullptr));
    if (RequestDisplay != nullptr)
    {
        float const AttachOffset[3] = {-0.23f, 0.2f, 0.0f};    // DAT_005cef30 / DAT_005cef34
        RequestDisplay->SetAttachOffset(AttachOffset);
        RequestDisplay->SetNormSize(0.46f, 0.15f);             // DAT_005cef28 / DAT_005cef2c
        RequestDisplay->SetAutoScrollbar(true);
        RequestDisplay->SetFrameColor(
            reinterpret_cast<unsigned long const*>(G_HudChromeColorRgba));
        RequestDisplay->SetAlignMode(2);
        RequestDisplay->GetToolTipHoverInfo()->IdsAlt = 0x1b83;
        RequestDisplay->SetToolTipEnabled(true);
    }

    // 0x00462680..0x00462724: BtnAccept, DAT_0061157c.. = {0.089, 0.021} / {-0.11, -0.03}.
    {
        float const Position[3] = {-0.11f, -0.03f, 0.0f};
        float const Half[3] = {0.089f, 0.021f, 0.0f};
        BtnAccept = static_cast<RichTextWireButtonControl*>(CreateRichTextWireButtonControl(
            "BtnAccept", "ui_select_item", Position, Half, 0, 0, 0x1b80, 0, 0, 0));
        RDLRefNode Ref(Texts[kNnGroupTextAccept]);
        (*BtnAccept->TextWinRef)->AppendNode(Ref);
    }
    // 0x00462726..0x004627c4: BtnDecline, DAT_0061158c.. = {0.089, 0.021} / {0.12, -0.03}.
    {
        float const Position[3] = {0.12f, -0.03f, 0.0f};
        float const Half[3] = {0.089f, 0.021f, 0.0f};
        BtnDecline = static_cast<RichTextWireButtonControl*>(CreateRichTextWireButtonControl(
            "BtnDecline", "ui_select_item", Position, Half, 0, 0, 0x1b81, 0, 0, 0));
        RDLRefNode Ref(Texts[kNnGroupTextDecline]);
        (*BtnDecline->TextWinRef)->AppendNode(Ref);
    }
    // 0x004627c6..0x00462864: BtnLeave, DAT_0061159c.. = {0.089, 0.021} / {DAT_0066da5c, -0.1}.
    {
        float const Position[3] = {G_GroupBtnLeaveX, -0.1f, 0.0f};
        float const Half[3] = {0.089f, 0.021f, 0.0f};
        RichTextWireButtonControl* const Leave =
            static_cast<RichTextWireButtonControl*>(CreateRichTextWireButtonControl(
                "BtnLeave", "ui_select_item", Position, Half, 0, 0, 0x1b82, 0, 0, 0));
        BtnLeave = Leave;
        RDLRefNode Ref(Texts[kNnGroupTextLeaveGroup]);
        (*Leave->TextWinRef)->AppendNode(Ref);
    }
    // 0x00462866..0x004628cd: BtnClose.
    {
        float const Position[3] = {0.23f, 0.23f, 0.0f};    // 0x3e6b851f
        float const Half[3] = {0.02f, 0.02f, 0.0f};        // 0x3ca3d70a
        BtnClose = CreateButtonControl("BtnClose", "Interface\\NeuroNet\\Inventory\\X.3db",
                                       nullptr, "ui_select_item", nullptr, Position, nullptr,
                                       Half, 0, 0, 0, 0, 0x409, 0, 0);
    }
    // 0x004628d3..0x004629ab: AttentionTitle, 0.46 x 0.035 at {-0.23, 0.035 + 0.2, 0}.
    {
        float const AttachOffset[3] = {-0.23f, 0.035f + 0.2f, 0.0f};
        AttentionTitle = CreateRichTextWin("AttentionTitle", 0.46f, 0.035f, AttachOffset, false);
        StyleNode Style1(0x9ca4);
        AttentionTitle->AppendNode(Style1);
        StyleNode Style2(0x1df5);
        AttentionTitle->AppendNode(Style2);
        RDLRefNode Ref(Texts[kNnGroupTextAttentionRequired]);
        AttentionTitle->AppendNode(Ref);
    }

    // 0x004629ad..0x004629c7
    BtnLeave->Flags = static_cast<unsigned char>(BtnLeave->Flags | 0x03);
    BtnAccept->Flags = static_cast<unsigned char>(BtnAccept->Flags | 0x03);
    ShowControl(BtnDecline);

    // 0x004629cc..0x004629db
    if (G_ChatChannelTable != nullptr)
        SetChannelTable(G_ChatChannelTable);

    // 0x004629e0..0x00462a0a
    RefreshRequestDisplay();
    ShownFrom = G_ChatIdNone;
    ShownShipId = 0;
    WinControlHalfExtentsFromMesh(HalfExtents, &MeshHolder);
    return true;
}

bool NnGroupControl::Slot23(unsigned Msg, unsigned A, unsigned B)
{
    // FUN_00462a40
    if (Msg == 0xef01)
    {
        // 0x00462a90: slot 5 with the parent's +0x60.
        if (Parent != nullptr)
            SetPosition(Parent->Position);
        RefreshRequestDisplay();
        return true;
    }
    if (Msg == 0xef02)
    {
        // 0x00462a69: FUN_0059dd00(this, 0), then Flags &= 0xfc.
        if (Parent != nullptr)
            Win3DReparent(this, nullptr);
        Flags = static_cast<unsigned char>(Flags & 0xfc);
        RefreshRequestDisplay();
        return true;
    }
    return WinControl::Slot23(Msg, A, B);
}

void NnGroupControl::UpdateInactive()
{
    // LAB_00462ab0
    if (ChannelTable != nullptr && (Flags & 0x02) == 0)
    {
        bool bChanged = false;
        int const NowMs = GroupNowMsRounded();
        std::list<FNnGroupRequest>::iterator It = Requests.begin();
        while (It != Requests.end())
        {
            // 0x00462b01..0x00462b0e: unsigned age above 20000 ms expires the request.
            if (static_cast<unsigned>(NowMs - It->TimestampMs) <= 0x4e20u)
                break;
            if (It->From.Id != G_ChatIdNone.Id && It->ShipId != 0)
                G_Server->StopTradeRequest(It->From.Id & 0xffffu);    // IServer +0x158
            It = Requests.erase(It);
            bChanged = true;
        }
        // 0x00462b68 (list ran out) / 0x00462b6a (stopped with a change): refresh.
        if (bChanged)
            RefreshRequestDisplay();
    }
    // 0x00462b79..0x00462b93: Win3D's child walk, inlined.
    Win3D::UpdateInactive();
}

void NnGroupControl::Update()
{
    // FUN_00462ba0
    if (ChannelTable != nullptr && ChannelTableGeneration != ChannelTable->Generation)
    {
        ChannelTableGeneration = ChannelTable->Generation;
        RefreshRequestDisplay();
    }
    Win3D::Update();    // tail jump FUN_005a17b0
}

intptr_t NnGroupControl::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_00462bd0
    if (Msg != 0x26)
        return WinControl::SetProperty(Msg, A, B);

    WinControl* const Clicked = reinterpret_cast<WinControl*>(A);
    if (Clicked == BtnAccept)
    {
        if (ShownFrom.Id == G_ChatIdNone.Id)
            return 0;
        if (ShownShipId == 0)
        {
            // 0x00462c11..0x00462c5a: accept the group invitation (opcode 2).
            IChatServer* const ChatServer = static_cast<IChatServer*>(G_ChatServer);
            if (ChatServer != nullptr)
            {
                FNnGroupWireMessage Message;
                Message.Opcode = 2;
                Message.Payload = ShownFrom.Id & 0xffffu;
                ChatServer->SubmitChat(CHAT_ID(GetPlayerConnectionId() & 0xffffu),
                                       sizeof(Message), &Message, G_ChatIdGroupInvite, -1);
            }
        }
        else
        {
            // 0x00462c5e..0x00462cd5: accept the trade -- target the trader, then RequestTrade.
            FClientGameobj* const Self = ClientObjDb_FindObject(GetPlayerCharacterId(), 1);
            FClientGameobj* const Trader = ClientObjDb_FindObject(ShownShipId, 1);
            if (Self != nullptr && Trader != nullptr)
            {
                // 0x00462c89..0x00462c99: the ship bucket's gameobj (null otherwise, used
                // anyway), its object via IObjInspect slot 84 (+0x150), narrowed by 0x503.
                FClientGameobj* const ShipGo = (Self->ManagerKind == 1) ? Self : nullptr;
                CObject* const Obj = const_cast<CObject*>(ShipGo->cobject());
                CShip* const Ship =
                    (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x503u) == 0x503u)
                        ? static_cast<CShip*>(Obj)
                        : nullptr;
                Ship->set_target(static_cast<IObjRW*>(Trader));
                G_Server->RequestTrade(GetPlayerConnectionId(), ShownFrom.Id & 0xffffu);
            }
        }
        // 0x00462cdb..0x00462d05
        Requests.erase(Requests.begin());
        RefreshRequestDisplay();
        if (Requests.empty())
            NnMainButtonOnModalOpen();
        return 0;
    }

    if (Clicked == BtnDecline)
    {
        if (ShownFrom.Id == G_ChatIdNone.Id)
            return WinControl::SetProperty(Msg, A, B);
        // 0x00462d33..0x00462d9d
        if (ShownShipId != 0)
            G_Server->StopTradeRequest(ShownFrom.Id & 0xffffu);
        Requests.erase(Requests.begin());
        RefreshRequestDisplay();
        if (Requests.empty())
            NnMainButtonOnModalOpen();
        return WinControl::SetProperty(Msg, A, B);
    }

    if (Clicked == BtnLeave)
    {
        // 0x00462daf..0x00462df2: leave the group (opcode 1, own connection id).
        IChatServer* const ChatServer = static_cast<IChatServer*>(G_ChatServer);
        if (ChatServer != nullptr)
        {
            unsigned const ConnWord = GetPlayerConnectionId() & 0xffffu;
            FNnGroupWireMessage Message;
            Message.Opcode = 1;
            Message.Payload = ConnWord;
            ChatServer->SubmitChat(CHAT_ID(ConnWord), sizeof(Message), &Message,
                                   G_ChatIdGroupInvite, -1);
        }
        return 0;
    }

    if (Clicked == BtnClose)
        NnMainButtonOnModalOpen();    // 0x00462e07
    return WinControl::SetProperty(Msg, A, B);
}

void NnGroupControl::RefreshRequestDisplay()
{
    // FUN_00462e30
    ShownFrom = G_ChatIdNone;
    ShownShipId = 0;
    if (!Requests.empty())
    {
        ShownFrom = Requests.front().From;
        ShownShipId = Requests.front().ShipId;
    }

    if (ShownFrom.Id == G_ChatIdNone.Id)
    {
        // 0x00462e9f..0x00462f4b
        BtnAccept->Flags = static_cast<unsigned char>(BtnAccept->Flags & 0xfe);
        BtnDecline->Flags = static_cast<unsigned char>(BtnDecline->Flags & 0xfe);
        RequestDisplay->ClearContent();
        JustifyNode Justify(TextJustifyCenter);    // JustifyNode(1)
        RequestDisplay->AppendNode(Justify);
        StyleNode Style(0x9ca4);
        RequestDisplay->AppendNode(Style);
        RDLRefNode Ref(Texts[kNnGroupTextNoInvitations]);
        RequestDisplay->AppendNode(Ref);

        // 0x00462f51..0x00462f6f: G_NnMainButton && !(Flags & 2) -> FUN_0045da10(0) and
        // FUN_0045da50(0) on it (the plain request icon, no alert colour).
        if (G_NnMainButton != nullptr && (Flags & 0x02) == 0)
        {
            G_NnMainButton->SetGroupButtonAlert(false);
            G_NnMainButton->SetGroupButtonMesh(0);
        }
    }
    else
    {
        // 0x00462f74..0x00462feb
        BtnAccept->Flags = static_cast<unsigned char>(BtnAccept->Flags | 0x01);
        BtnDecline->Flags = static_cast<unsigned char>(BtnDecline->Flags | 0x01);
        RequestDisplay->ClearContent();
        {
            JustifyNode Justify(TextJustifyCenter);    // JustifyNode(1)
            RequestDisplay->AppendNode(Justify);
            StyleNode Style(0x9ca4);
            RequestDisplay->AppendNode(Style);
        }

        RichTextWin* const AcceptCaption = *BtnAccept->TextWinRef;
        RichTextWin* const DeclineCaption = *BtnDecline->TextWinRef;
        if (ShownShipId == 0)
        {
            // 0x00462ffd..0x004630c3: a group invitation.
            RDLRefNode What(Texts[kNnGroupTextInviteFrom]);
            RequestDisplay->AppendNode(What);
            AcceptCaption->ClearContent();
            RDLRefNode AcceptRef(Texts[kNnGroupTextAccept]);
            AcceptCaption->AppendNode(AcceptRef);
            DeclineCaption->ClearContent();
            RDLRefNode DeclineRef(Texts[kNnGroupTextDecline]);
            DeclineCaption->AppendNode(DeclineRef);
        }
        else
        {
            // 0x004630c8..0x004631b0: a trade request.
            RDLRefNode What(Texts[kNnGroupTextTradeFrom]);
            RequestDisplay->AppendNode(What);
            AcceptCaption->ClearContent();
            RDLRefNode AcceptRef(Texts[kNnGroupTextAcceptTrade]);
            AcceptCaption->AppendNode(AcceptRef);
            DeclineCaption->ClearContent();
            RDLRefNode DeclineRef(Texts[kNnGroupTextDeclineTrade]);
            DeclineCaption->AppendNode(DeclineRef);
        }

        // 0x004631b2..0x0046322c
        {
            ParagraphNode Para;
            RequestDisplay->AppendNode(Para);
            JustifyNode Justify(TextJustifyCenter);    // JustifyNode(1)
            RequestDisplay->AppendNode(Justify);
            StyleNode Style(0xd5ec);
            RequestDisplay->AppendNode(Style);
        }

        // 0x00463232..0x00463283: FUN_0046d920 lower_bound + key test on the channel table; a
        // non-null name list goes in through FUN_0057b3e0.
        if (ChannelTable != nullptr)
        {
            RenderDisplayList* const Name = ChannelTable->FindName(ShownFrom);
            if (Name != nullptr)
                RequestDisplay->AppendContent(*Name);
        }

        // 0x00463288..0x004632ba: G_NnMainButton && !(Flags & 2) -> FUN_0045da10(1) and
        // FUN_0045da50(ShipId == 0 ? 2 : 1) -- the group or the trade request icon.
        if (G_NnMainButton != nullptr && (Flags & 0x02) == 0)
        {
            G_NnMainButton->SetGroupButtonAlert(true);
            G_NnMainButton->SetGroupButtonMesh(ShownShipId == 0 ? 2 : 1);
        }
    }

    // 0x004632bf..0x004632dd: BtnLeave shows while the group has members.
    if (!GroupMembers.empty())
        BtnLeave->Flags = static_cast<unsigned char>(BtnLeave->Flags | 0x01);
    else
        BtnLeave->Flags = static_cast<unsigned char>(BtnLeave->Flags & 0xfe);
}

void NnGroupControl::AddTradeRequest(unsigned ShipId)
{
    // FUN_004632f0 -- 0x00463311..0x00463342: a pending request from this ship already exists.
    for (std::list<FNnGroupRequest>::iterator It = Requests.begin(); It != Requests.end(); ++It)
    {
        if (It->ShipId != 0 && It->ShipId == ShipId)
            return;
    }

    // 0x00463348..0x004633a2: the sender is the ship's owning player when its object is a
    // CSimple (class bits 3), else FUN_0054bb00 (DAT_00673344); no object -> {0}.
    FNnGroupRequest Request;
    Request.TimestampMs = 0;
    Request.From = G_ChatIdNone;
    Request.ShipId = ShipId;
    FClientGameobj* const Go = ClientObjDb_FindObject(ShipId, 1);
    if (Go != nullptr)
    {
        CObject* const Obj = Go->GetObject();
        unsigned Owner;
        if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x3u) == 0x3u)
            Owner = static_cast<CSimple*>(Obj)->OwnerPlayer;
        else
            Owner = GetPlayerConnectionId();
        Request.From = ChatIdForPlayer(Owner & 0xffffu);
    }

    // 0x004633a6..0x004633e7
    Request.TimestampMs = GroupNowMsRounded();
    Requests.push_back(Request);
    RefreshRequestDisplay();

    // 0x004633ec..0x0046358b: "trade request from" + the sender's name, or the entry discarded.
    FChatMessageEntry* const Entry = ChatAcquireEntry();
    RenderDisplayList* const Name = G_ChatChannelTable->FindName(Request.From);
    if (Name == nullptr)
    {
        DiscardEntry(Entry);
        return;
    }
    StyleNode Style(0x3ea8);
    RdlAppendNode(Entry->Body, Style.Clone());
    AppendPrefixAndName(Entry, Texts[kNnGroupTextTradeFrom], Name);
    ChatCommitEntries();
}

void NnGroupControl::HandleInviteMessage(void const* Buffer, unsigned SizeBytes)
{
    // FUN_00463630
    if (SizeBytes != 8)
    {
        GroupNotice(0x8df, "Received mis-sized invite message.\n");
        return;
    }

    FChatMessageEntry* const Entry = ChatAcquireEntry();
    FNnGroupWireMessage const* const Message = static_cast<FNnGroupWireMessage const*>(Buffer);
    unsigned const Payload = Message->Payload;
    CHAT_ID const Key = ChatIdForPlayer(Payload & 0xffffu);

    // Each case either appends to Entry and commits (LAB_00464b5b: FUN_004686e0 inlined, then
    // FUN_004684e0) or discards it (caseD_11 / caseD_13).
    RenderDisplayList* Name = nullptr;
    switch (Message->Opcode)
    {
    case 0:
    case 1:
    case 2:
        // 0x004636e1: server-side opcodes.
        GroupNotice(0x8ef, "Received a group command on a client. This is illegal.\n");
        DiscardEntry(Entry);
        return;

    case 3:
        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        AppendPrefixAndName(Entry, Texts[kNnGroupTextInviteSent], Name);
        ChatCommitEntries();
        return;

    case 4:
    {
        // 0x0046386d..0x004638ea: an identical pending invitation -> nothing.
        for (std::list<FNnGroupRequest>::iterator It = Requests.begin(); It != Requests.end();
             ++It)
        {
            if (It->From.Id == Key.Id && It->ShipId == 0)
            {
                DiscardEntry(Entry);
                return;
            }
        }
        FNnGroupRequest Request;
        Request.TimestampMs = GroupNowMsRounded();
        Request.From = Key;
        Request.ShipId = 0;
        Requests.push_back(Request);
        RefreshRequestDisplay();

        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        StyleNode Style(0x3ea8);
        RdlAppendNode(Entry->Body, Style.Clone());
        AppendPrefixAndName(Entry, Texts[kNnGroupTextInviteFrom], Name);
        ChatCommitEntries();
        return;
    }

    case 5:
    {
        // 0x00463aae..0x00463b0f: every pending invitation (ShipId 0, From set) is dropped.
        bool bErased = false;
        std::list<FNnGroupRequest>::iterator It = Requests.begin();
        while (It != Requests.end())
        {
            if (It->ShipId == 0 && It->From.Id != G_ChatIdNone.Id)
            {
                It = Requests.erase(It);
                bErased = true;
            }
            else
            {
                ++It;
            }
        }
        if (bErased)
            RefreshRequestDisplay();
        // 0x00463b14..0x00463b53
        GroupMembers.insert(Key.Id);
        RefreshRequestDisplay();

        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        AppendPrefixAndName(Entry, Texts[kNnGroupTextJoinedGroup], Name);
        ChatCommitEntries();
        return;
    }

    case 6:
        // 0x00463c98..0x00463d08: the invitation from this player, if pending, is dropped.
        if (Key.Id != G_ChatIdNone.Id)
        {
            for (std::list<FNnGroupRequest>::iterator It = Requests.begin();
                 It != Requests.end(); ++It)
            {
                if (It->ShipId == 0 && It->From.Id == Key.Id)
                {
                    Requests.erase(It);
                    RefreshRequestDisplay();
                    break;
                }
            }
        }
        // 0x00463d0d..0x00463d4c
        GroupMembers.insert(Key.Id);
        RefreshRequestDisplay();

        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        AppendPrefixAndName(Entry, Texts[kNnGroupTextNewMember], Name);
        ChatCommitEntries();
        return;

    case 7:
    {
        // 0x00463e8b..0x00463e98
        GroupMembers.clear();
        RefreshRequestDisplay();

        // 0x00463e9d..0x00463eef: a player ship flying behaviour 10 (formation) is switched to
        // maneuver 0x13.
        HUD_Maneuvers* const Hud = GetHudManeuvers();
        if (Hud != nullptr)
        {
            void* const PlayerRw = GetPlayerObjRW();
            if (PlayerRw != nullptr)
            {
                FClientGameobj* const PlayerGo =
                    static_cast<FClientGameobj*>(static_cast<IObjRW*>(PlayerRw));
                CObject* const Obj = PlayerGo->GetObject();
                if (Obj != nullptr &&
                    (static_cast<unsigned>(Obj->ObjectClass) & 0x503u) == 0x503u)
                {
                    IBehaviorManager* const Behavior =
                        static_cast<CEqObj*>(Obj)->get_behavior_interface();
                    if (Behavior != nullptr && Behavior->CurrentBehaviorIndex == 10)
                        Hud->RequestManeuver(0x13, false);
                }
            }
        }

        // 0x00463ef4..0x00463f71
        AppendPrefixAndName(Entry, Texts[kNnGroupTextLeftGroup], nullptr);
        ChatCommitEntries();
        return;
    }

    case 8:
        // 0x00463f76..0x00463f9a
        GroupMembers.erase(Key.Id);
        RefreshRequestDisplay();

        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        AppendPrefixAndName(Entry, Texts[kNnGroupTextMemberLeft], Name);
        ChatCommitEntries();
        return;

    case 9:
        AppendPrefixAndName(Entry, Texts[kNnGroupTextCannotInviteSelf], nullptr);
        ChatCommitEntries();
        return;

    case 0xa:
        AppendPrefixAndName(Entry, Texts[kNnGroupTextGroupFull], nullptr);
        ChatCommitEntries();
        return;

    case 0xb:
        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        AppendPrefixAndName(Entry, Texts[kNnGroupTextAlreadyInYourGroup], Name);
        ChatCommitEntries();
        return;

    case 0xc:
        AppendPrefixAndName(Entry, Texts[kNnGroupTextCannotJoinOwnGroup], nullptr);
        ChatCommitEntries();
        return;

    case 0xd:
        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        AppendPrefixAndName(Entry, Texts[kNnGroupTextNotInvited], Name);
        ChatCommitEntries();
        return;

    case 0xe:
        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        AppendPrefixAndName(Entry, Texts[kNnGroupTextAlreadyMember], Name);
        ChatCommitEntries();
        return;

    case 0xf:
        // 0x004640d9: the full payload dword against DAT_00673344.
        if (Payload == GetPlayerConnectionId())
        {
            AppendPrefixAndName(Entry, Texts[kNnGroupTextAbortMission], nullptr);
            ChatCommitEntries();
            return;
        }
        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        AppendPrefixAndName(Entry, Texts[kNnGroupTextJoinFailedMission], Name);
        ChatCommitEntries();
        return;

    case 0x10:
        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        AppendPrefixAndName(Entry, Texts[kNnGroupTextGroupNowFull], Name);
        ChatCommitEntries();
        return;

    case 0x12:
        AppendPrefixAndName(Entry, Texts[kNnGroupTextNotInGroup], nullptr);
        ChatCommitEntries();
        return;

    case 0x14:
        Name = FindChatNameForWord(Payload);
        if (Name == nullptr)
            break;
        AppendPrefixAndName(Entry, Texts[kNnGroupTextPlayerDied], Name);
        ChatCommitEntries();
        return;

    default:
        // 0x11, 0x13 and anything above 0x14 (the JA at 0x004636d8).
        break;
    }

    // caseD_11 / caseD_13 (0x00463706): no name, or an opcode with nothing to say.
    DiscardEntry(Entry);
}
