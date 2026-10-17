// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// NN_Trade -- see nn_trade.h. Retail freelancer.exe 0x004bfb60..0x004c3908 plus the static
// initializers LAB_004be670 / LAB_004be690 / LAB_004be6a0.

#include "nn_trade.h"

#include "cargo_bar_control.h"      // CTradeEquipListLine::HealthBar
#include "chat_init.h"              // ChatPlaceMsgWinForTrade (FUN_0046b450)
#include "character_select_equip.h" // GetPlayerEquipDescListRetail (FUN_004c4060),
                                    // FindPlayerNonMissionEquipRetail (FUN_004c45d0),
                                    // GetPlayerCharacterNameRetail (DAT_006732f0)
#include "client_gameobj.h"         // FClientGameobj -> IObjRW
#include "client_mesh_load.h"       // ClientMeshHolderHasInstance (FUN_00420d60)
#include "client_objdb.h"           // ClientObjDb_FindObject (FUN_005416c0)
#include "globals.h"                // GetClientFrameTimeCapped (FUN_0042d680)
#include "hud_status_core.h"        // GetHudStatus (DAT_00674aa0), G_HudTargetPanel (DAT_00674b04)
#include "hud_target.h"             // HudShipOfObjRw (FUN_00543320), HudTargetBase
#include "ids_rdl.h"                // LoadIdsIntoRdl (FUN_0057da40), RdlAppendNode
#include "login_mgr.h"              // G_Server (DAT_0067ecd0)
#include "nn_button_control.h"
#include "nn_inventory.h"           // G_NanobotArchId (DAT_00674a8c), G_ShieldBatteryArchId (DAT_00674a88)
#include "nn_static_control.h"      // StaticControl +0x348 / +0x410 / +0x414
#include "nn_text_control.h"
#include "player_behavior.h"        // GetPlayerObjRW (FUN_0054baf0)
#include "player_spawn.h"           // GetPlayerConnectionId (DAT_00673344),
                                    // GetPlayerShipHoldSizeRetail (FUN_004c4610)
#include "rich_text_win.h"
#include "scroll_control.h"
#include "ship_view_control.h"
#include "star_sphere.h"            // Win3DIdentityRotation3x3, Win3DBindModelview (FUN_00422690)
#include "win3d.h"
#include "win3d_immediate.h"        // Win3DBegin / Win3DVertex3f / Win3DEnd / ... (OpenGL.cpp)
#include "win3d_renderables.h"      // Win3DFlushBeforeOverlayDraw (FUN_0045a9f0)
#include "win_factory.h"

#include "DALib.h"
#include "dacom.h"
#include "render/IRenderPipeline.h"
#include "render/rendcomp.h"

#include "common_archetype.h"
#include "common_cobject.h"         // CShip
#include "common_goods.h"
#include "common_id.h"              // CreateID
#include "common_server.h"          // IServer
#include "common_text.h"            // RenderDisplayList, StyleNode

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <string>

NnTrade* G_NnTrade = nullptr; // DAT_006724b4

// Retail DAT_0067245c -- WINFACTORY<NN_Trade> static (LAB_004be670 -> FUN_004c37a0; Create
// FUN_004c3810; atexit LAB_004be690 -> FUN_00579380).
WINFACTORY<NnTrade> G_NnTradeFactory("NN_Trade");

namespace
{
    // DAT_00672458 -- LAB_004be6a0: CreateID("insufficient_cargo_space"), read by SetMode
    // (0x004c1e1c, 0x004c1e90).
    unsigned const G_InsufficientCargoSpaceVoiceId = CreateID("insufficient_cargo_space");

    // DAT_006728bc (.bss float, 0.0) -- the x of the window's offset; only Create reads it.
    float G_NnTradeOffsetX = 0.0f;
    // DAT_00612188 (.data float) -- the y of the window's offset.
    float G_NnTradeOffsetY = -0.257f;
    // DAT_006728c0 (.bss float) -- the quantity caret's blink timer.
    float G_NnTradeCaretTimer = 0.0f;
    // DAT_0061218c (.data byte, 1) -- the caret is shown.
    unsigned char G_bNnTradeCaretShown = 1;

    // The window's meshes (.rdata strings 0x005d6c68..0x005d64d8).
    char const kBackgroundMesh[] = "interface\\multiplayer\\trade\\TRADE_background.cmp";
    char const kTitleBoxMesh[] = "Interface\\multiplayer\\trade\\TRADE_tradetitlebox.3db";
    char const kTitleArrowMesh[] = "Interface\\multiplayer\\trade\\TRADE_transactiondirection.3db";
    char const kCargoFullMesh[] = "Interface\\multiplayer\\trade\\TRADE_cargofull.3db";
    char const kCargoEmptyMesh[] = "Interface\\multiplayer\\trade\\TRADE_cargoempty.3db";
    char const kQuantityBoxMesh[] = "Interface\\multiplayer\\trade\\TRADE_quantitybox.3db";
    char const kExecuteMesh[] = "interface\\multiplayer\\trade\\TRADE_executetransaction.3db";
    char const kCloseMesh[] = "interface\\multiplayer\\trade\\TRADE_closeinterfacex.3db";
    char const kQuantityBarMesh[] = "interface\\multiplayer\\trade\\TRADE_quantityscrollbar.3db";
    char const kRightArrowMesh[] = "interface\\multiplayer\\trade\\TRADE_rightarrow.3db";
    char const kLeftArrowMesh[] = "interface\\multiplayer\\trade\\TRADE_leftarrow.3db";
    char const kQuantityHandleMesh[] = "interface\\multiplayer\\trade\\TRADE_quantityscrollhandle.3db";
    char const kAcceptTradeMesh[] = "interface\\multiplayer\\trade\\TRADE_accepttradebox.3db";
    char const kInfoFrameMesh[] = "Interface\\multiplayer\\trade\\TRADE_infoframe.3db";

    // PTR_DAT_005d0ec4 -- L"%d"; DAT_005d6b44 -- L"Target Name".
    wchar_t const kQuantityFormat[] = L"%d";
    wchar_t const kTargetNamePlaceholder[] = L"Target Name";

    // 50000000 (0x2faf080): what an unlimited (-1) nanobot / battery limit becomes.
    int const kUnlimitedAmmo = 50000000;

    uintptr_t PointerArg(void const* Pointer)
    {
        return reinterpret_cast<uintptr_t>(Pointer);
    }

    // FUN_004a1ad0 (thiscall, RET 0x10) -- four bytes in argument order; the trade window stores
    // the result as the TextControl's +0x348 dword.
    unsigned long TradeColour(unsigned char B0, unsigned char B1, unsigned char B2,
                              unsigned char B3)
    {
        return static_cast<unsigned long>(B0) | (static_cast<unsigned long>(B1) << 8) |
               (static_cast<unsigned long>(B2) << 16) | (static_cast<unsigned long>(B3) << 24);
    }

    NnTextControl* AsText(WinControl* Control)
    {
        return static_cast<NnTextControl*>(Control);
    }

    // The cargo bars come from WinControl::CreateStaticControl (FUN_005a0db0), which returns the
    // StaticControl it made.
    NnStaticControl* AsStatic(WinControl* Control)
    {
        return static_cast<NnStaticControl*>(Control);
    }

    // 0x004c0a15..0x004c0a4d / 0x004c0ba7..0x004c0be0: bytes {ff,00,00,c0} built on the stack and
    // stored as the dword at +0x410, then again at +0x414.
    void SetCargoBarColours(NnStaticControl* Bar)
    {
        Bar->MeshColour[0] = 0xff;
        Bar->MeshColour[1] = 0;
        Bar->MeshColour[2] = 0;
        Bar->MeshColour[3] = 0xc0;
        Bar->WireColour[0] = 0xff;
        Bar->WireColour[1] = 0;
        Bar->WireColour[2] = 0;
        Bar->WireColour[3] = 0xc0;
    }

    // Byte +0 of a list entry is the trade selection mark (retail writes it as a byte; the port's
    // EquipDesc keeps the word +0 as Cached).
    void SetTradeMark(EquipDesc* Item, unsigned char bMarked)
    {
        Item->Cached = static_cast<unsigned short>((Item->Cached & 0xff00) | bMarked);
    }

    // The inlined "scroll range [0, max(0, size - rows)], then Refresh(-1)" of FUN_004c2220 /
    // FUN_004c2380 / FUN_004c2d50 (e.g. 0x004c22a4..0x004c22c2).
    void RefreshTradeListRange(CTradeEquipList* List)
    {
        int Range = static_cast<int>(List->Items.Items.size()) - List->VisibleRows;
        if (Range < 0)
            Range = 0;
        List->ScrollBar->SetProperty(0x37, 0, static_cast<uintptr_t>(Range));
        List->Refresh(-1);
    }

    // FUN_00489e80(Id, 0) -- the NN voice queue (DAT_00671f50, FUN_00489b50) is not in the port.
    // HOLE: each call is reported instead of queueing the line.
    void QueueTradeVoice(unsigned VoiceId)
    {
        FDUMP(0x100002, "NN_Trade: HOLE FUN_00489e80 (voice %u) -- voice queue not ported",
              VoiceId);
    }

    // FUN_004bff30 (cdecl) -- blink a '|' caret at the end of Text every half second.
    void BlinkQuantityCaret(NnTextControl* Text)
    {
        // 0x004bff48..0x004bff7a: the sum is compared before it is rounded to the float.
        double const Timer = GetClientFrameTimeCapped() + static_cast<double>(G_NnTradeCaretTimer);
        G_NnTradeCaretTimer = static_cast<float>(Timer);
        if (Timer > 0.5) // DAT_005c75e8; TEST AH,0x41 / JNZ skips on not-greater
        {
            G_bNnTradeCaretShown = (G_bNnTradeCaretShown == 0) ? 1 : 0;
            G_NnTradeCaretTimer = 0.0f;
        }

        // 0x004bff7f..0x004bffaf: nothing to do when the text is empty or already agrees.
        wchar_t const* const Current = Text->GetFontString()->Text; // FUN_0058a520
        int const Last = static_cast<int>(wcslen(Current)) - 1;
        if (Last < 0)
            return;
        unsigned char const bHasCaret = (Current[Last] == L'|') ? 1 : 0;
        if (bHasCaret == G_bNnTradeCaretShown)
            return;

        // 0x004bffb5..0x004c00f1: a heap copy (FUN_004142a0 + wcsncpy) with the '|' appended, or
        // with its last character cut, handed to FUN_0058a530 and freed.
        std::wstring Copy(Current);
        if (G_bNnTradeCaretShown != 0)
            Copy.append(L"|");
        else
            Copy.resize(Copy.size() - 1);
        Text->SetText(Copy.c_str());
    }

    // 0x004c01a9..0x004c0312 (and 0x004c0362..0x004c04cb): the dark quad over the part of a
    // cargo bar right of X in the bar's frame, then the bar's own mesh.
    void DrawCargoFill(WinControl* Bar, float X)
    {
        IRenderPipeline* const Pipe = DALib::Pipeline;
        Pipe->SetRenderState(D3DRS_ZENABLE, 1);
        Pipe->SetRenderState(D3DRS_ZFUNC, 8);
        Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 1);
        float Transform[12];
        memcpy(Transform, DALib::Engine->GetInstanceTransform(Bar->MeshHolder.EngineInstance),
               sizeof(Transform)); // IEngine +0xa0, REP MOVSD 12 dwords
        Win3DBindModelview(nullptr, Transform); // FUN_00422690(0, &Transform)
        Win3DDisable(kWin3DTexture2D);          // FUN_00421ed0(0xde1)
        Win3DEnable(kWin3DBlend);               // FUN_00421dd0(0xbe2)
        Win3DSetBlendFunc(2, 4);                // FUN_004220c0
        Win3DColor4ub(0, 0, 0, 0x0a);           // FUN_00421b70
        Win3DBegin(kWin3DQuads);                // FUN_00421670(8)
        float const HalfX = Bar->HalfExtents[0];
        float const HalfY = Bar->HalfExtents[1];
        Win3DVertex3f(HalfX, HalfY, 0.0f);      // FUN_00421cc0
        Win3DVertex3f(HalfX, -HalfY, 0.0f);
        Win3DVertex3f(X, -HalfY, 0.0f);
        Win3DVertex3f(X, HalfY, 0.0f);
        Win3DEnd();                             // FUN_00421690
        Pipe->SetRenderState(D3DRS_ZFUNC, 4);
        DALib::Renderer->RenderInstance(Win3DGetCamera(), Bar->MeshHolder.EngineInstance, 1.0f, 0, nullptr,
                               nullptr); // IRenderer2 +0x1c, &DAT_0067dbf8
        Win3DDisable(kWin3DBlend);               // FUN_00421ed0(0xbe2)
    }

    // X = 2 * hx * Ratio - hx, kept as a double on the FPU and rounded to a float at the vertex.
    float CargoFillX(WinControl const* Bar, double Ratio)
    {
        double X = static_cast<double>(Bar->HalfExtents[0]) * Ratio;
        X = X + X;
        X = X - static_cast<double>(Bar->HalfExtents[0]);
        return static_cast<float>(X);
    }

    // The CShip of the other player's object, or of none (FUN_005416c0(Id, 1) + FUN_00543320).
    FClientGameobj* FindTradeObject(unsigned ObjectId)
    {
        return ClientObjDb_FindObject(ObjectId, 1);
    }
}

bool NnTradeIsOpen()
{
    return G_NnTrade != nullptr;
}

// ---------------------------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------------------------

NnTrade::NnTrade()
    : TargetList(nullptr)
    , CenterList(nullptr)
    , PlayerList(nullptr)
    , QuantityLine(nullptr)
    , TradeClose(nullptr)
    , Field344(0)
    , QuantityTitle(nullptr)
    , QuantityBar(nullptr)
    , QuantityFrame(nullptr)
    , QuantityAccept(nullptr)
    , QuantityReject(nullptr)
    , QuantityText(nullptr)
    , bQuantityTyped(0)
    , Quantity(0)
    , PlayerCardFrame(nullptr)
    , PlayerCardClose(nullptr)
    , PlayerCardModel(nullptr)
    , PlayerCardTitle(nullptr)
    , PlayerCardInfo(nullptr)
    , TargetCardFrame(nullptr)
    , TargetCardClose(nullptr)
    , TargetCardModel(nullptr)
    , TargetCardTitle(nullptr)
    , TargetCardInfo(nullptr)
    , Mode(0)
    , PlayerName(nullptr)
    , TargetName(nullptr)
    , TitleFrame(nullptr)
    , TitleArrow(nullptr)
    , TitleText(nullptr)
    , Field3ac(0)
    , Field3b0(0)
    , PlayerCargoText(nullptr)
    , PlayerCargoFull(nullptr)
    , PlayerCargoEmpty(nullptr)
    , PlayerCargoUsed(0.0)
    , PlayerNanobots(0)
    , PlayerNanobotLimit(0)
    , PlayerBatteries(0)
    , PlayerBatteryLimit(0)
    , TargetCargoText(nullptr)
    , TargetCargoFull(nullptr)
    , TargetCargoEmpty(nullptr)
    , TargetCargoUsed(0.0)
    , TargetHoldSize(0.0)
    , TargetNanobots(0)
    , TargetNanobotLimit(0)
    , TargetBatteries(0)
    , TargetBatteryLimit(0)
    , NanobotsOffered(0)
    , BatteriesOffered(0)
    , PlayerAcceptedText(nullptr)
    , TargetAcceptedText(nullptr)
    , Byte418(0)
    , Byte419(0)
    , AcceptTradeFrame(nullptr)
    , AcceptTradeText(nullptr)
    , SelectedLine(nullptr)
    , SelectedItem(nullptr)
    , TargetObjectId(0xffffffffu)
    , TargetOwnerPlayer(0)
    , bHudTargetWasExpanded(0)
    , bHudStatusWasExpanded(0)
{
    // FUN_004bfb60. +0x394, +0x3e4, +0x438 and +0x43c are left as allocated.
    G_NnTrade = this;
}

NnTrade::~NnTrade()
{
    // FUN_004bfd40 (scalar deleting FUN_004bfd20).
    if (TargetList != nullptr)
    {
        delete TargetList;
        TargetList = nullptr;
    }
    if (CenterList != nullptr)
    {
        delete CenterList;
        CenterList = nullptr;
    }
    if (PlayerList != nullptr)
    {
        delete PlayerList;
        PlayerList = nullptr;
    }
    G_NnTrade = nullptr;
}

bool NnTrade::OnEvent(unsigned MsgId)
{
    // LAB_0043d8e0 -- MOV AL,1 / RET 4.
    (void)MsgId;
    return true;
}

bool NnTrade::GetHardpointTransform(char const* Name, float* OutTransform12)
{
    // LAB_004bfe00 (RET 8).
    if (ClientMeshHolderHasInstance(&MeshHolder)) // FUN_00420d60
    {
        void* const Archetype = DALib::Engine->GetInstanceArchetype(MeshHolder.EngineInstance);
        DALib::Engine->ReleaseHandle(Archetype);
        FHardpointKey Key;
        Key.Name = Name;
        Key.Crc = DACOM_CRC::GetCRC32(Name);
        unsigned char Blob[0x48];
        if (static_cast<unsigned char>(DALib::Hardpoint->GetHardpoint(Archetype, &Key, Blob)) != 0)
        {
            // Blob +0x10 orientation, +0x04 position; this window's +0xa8 is added, then the z
            // translation is overwritten with 0 (0x004bfeda).
            float Local[12];
            memcpy(Local, Blob + 0x10, 9 * sizeof(float));
            memcpy(Local + 9, Blob + 0x04, 3 * sizeof(float));
            memcpy(OutTransform12, Local, sizeof(Local));
            OutTransform12[9] = Offset[0] + OutTransform12[9];
            OutTransform12[10] = Offset[1] + OutTransform12[10];
            OutTransform12[11] = 0.0f;
            return true;
        }
    }

    // 0x004bfeec..0x004bff27: identity, no translation, no offset.
    OutTransform12[8] = 1.0f;
    OutTransform12[4] = 1.0f;
    OutTransform12[0] = 1.0f;
    OutTransform12[7] = 0.0f;
    OutTransform12[6] = 0.0f;
    OutTransform12[5] = 0.0f;
    OutTransform12[3] = 0.0f;
    OutTransform12[2] = 0.0f;
    OutTransform12[1] = 0.0f;
    OutTransform12[11] = 0.0f;
    OutTransform12[10] = 0.0f;
    OutTransform12[9] = 0.0f;
    return false;
}

bool NnTrade::Create(Win3D* NewParent, void* Data)
{
    // LAB_004c04f0 (RET 8).
    if (!Win3D::Create(NewParent, Data)) // FUN_004b5060
        return false;

    // 0x004c052a..0x004c055e: what the player carries of nanobots and shield batteries.
    EquipDesc* const Nanobots = FindPlayerNonMissionEquipRetail(G_NanobotArchId);    // FUN_004c45d0
    EquipDesc* const Batteries = FindPlayerNonMissionEquipRetail(G_ShieldBatteryArchId);
    if (Nanobots != nullptr)
        PlayerNanobots = Nanobots->Count;
    if (Batteries != nullptr)
        PlayerBatteries = Batteries->Count;

    // 0x004c0564..0x004c05df: the player's ship (FUN_00543320 inlined) and its limits.
    CShip* const Ship = HudShipOfObjRw(static_cast<IObjRW*>(GetPlayerObjRW())); // FUN_0054baf0
    if (Ship != nullptr)
    {
        Archetype::Ship const* const ShipArch = Ship->shiparch(); // CObject +0x88
        PlayerNanobotLimit = ShipArch->NanobotLimit;
        PlayerBatteryLimit = ShipArch->ShieldBatteryLimit;
        if (PlayerBatteryLimit == -1)
            PlayerBatteryLimit = kUnlimitedAmmo;
        if (PlayerNanobotLimit == -1)
            PlayerNanobotLimit = kUnlimitedAmmo;
    }
    PlayerShip = Ship;
    PlayerCargoUsed = GetPlayerEquipDescListRetail().get_cargo_space_occupied(); // FUN_004c4640

    // 0x004c05e5..0x004c0650: the offset, the background and its size.
    Offset[0] = G_NnTradeOffsetX;
    Offset[1] = G_NnTradeOffsetY;
    Offset[2] = 0.0f;
    float const Zero[3] = {0.0f, 0.0f, 0.0f};
    SetMesh(kBackgroundMesh, 0); // slot 38
    WinControlHalfExtentsFromMesh(HalfExtents, &MeshHolder);

    // 0x004c0655..0x004c06c9: the title box, hidden.
    FormatTradeWidgetName("TitleFrame");
    TitleFrame = CreateStaticControl(G_TradeWidgetName, kTitleBoxMesh, Zero, "hp_title_frame", Zero,
                                     1, 1, 1, 0, 0, 0, 1, 0, 0, 0, Zero);
    TitleFrame->SetProperty(1, 1, 0);

    // 0x004c06cb..0x004c0724: the transfer arrow.
    FormatTradeWidgetName("TitleArrow");
    TitleArrow = CreateStaticControl(G_TradeWidgetName, kTitleArrowMesh, Zero, "hp_title_arrow",
                                     Zero, 1, 1, 0, 1, 0, 0, 1, 0, 0, 0, Zero);

    // 0x004c0729..0x004c0785: the page title (ids 0x754).
    FormatTradeWidgetName("TitleText");
    TitleText = CreateTextControl(G_TradeWidgetName, "PageHeader", Zero, "hp_title_text", 0.172f,
                                  0.051f, 2.0f, 0x754, 1, 0, 0);

    // 0x004c078a..0x004c0867: the two players' names.
    FormatTradeWidgetName("PlayerName");
    PlayerName = CreateTextControl(G_TradeWidgetName, "TradePlayerName", Zero, "hp_player_name",
                                   0.415f, 0.039f, 2.0f, 0, 1, 0, 0);
    FormatTradeWidgetName("TargetName");
    TargetName = CreateTextControl(G_TradeWidgetName, "TradePlayerName", Zero, "hp_target_name",
                                   0.415f, 0.039f, 2.0f, 0, 1, 0, 0);
    PlayerName->SetProperty(0x1d, PointerArg(GetPlayerCharacterNameRetail()), 0); // DAT_006732f0
    TargetName->SetProperty(0x1d, PointerArg(kTargetNamePlaceholder), 0);

    // 0x004c086d..0x004c099c: the two "accepted" texts, red "not accepted" (ids 0x75a).
    FormatTradeWidgetName("PlayerAcceptedText");
    PlayerAcceptedText = AsText(CreateTextControl(G_TradeWidgetName, "SubHeader", Zero,
                                                  "hp_player_accepted", 0.415f, 0.029f, 1.5f, 0,
                                                  1, 0, 0));
    FormatTradeWidgetName("TargetAcceptedText");
    TargetAcceptedText = AsText(CreateTextControl(G_TradeWidgetName, "SubHeader", Zero,
                                                  "hp_target_accepted", 0.415f, 0.029f, 1.5f, 0,
                                                  1, 0, 0));
    PlayerAcceptedText->SetProperty(0x1e, 0x75a, 0);
    PlayerAcceptedText->TextColour = TradeColour(0xff, 0, 0, 0xc0);
    TargetAcceptedText->SetProperty(0x1e, 0x75a, 0);
    TargetAcceptedText->TextColour = TradeColour(0xff, 0, 0, 0xc0);

    // 0x004c099c..0x004c0a4d: the player's full cargo bar, its half extents from the mesh, and
    // the StaticControl colours +0x410 / +0x414 = {ff,00,00,c0} (0x004c0a2b, 0x004c0a4d).
    FormatTradeWidgetName("PlayerCargoBarFull");
    PlayerCargoFull = CreateStaticControl(G_TradeWidgetName, kCargoFullMesh, Zero,
                                          "hp_player_cargo_capacity", Zero, 1, 1, 0, 1, 0, 0, 1,
                                          0, 0, 0, Zero);
    WinControlHalfExtentsFromMesh(PlayerCargoFull->HalfExtents, &PlayerCargoFull->MeshHolder);
    SetCargoBarColours(AsStatic(PlayerCargoFull));

    // 0x004c0a53..0x004c0aba: its caption (ids 0x755), as wide as the bar.
    FormatTradeWidgetName("PlayerCargoText");
    PlayerCargoText = CreateTextControl(G_TradeWidgetName, "SubHeader", Zero, "hp_player_cargo_text",
                                        PlayerCargoFull->HalfExtents[0] + PlayerCargoFull->HalfExtents[0],
                                        0.051f, 1.25f, 0x755, 1, 0, 0);

    // 0x004c0ac0..0x004c0b2b: the empty bar under the same name and hardpoint; the full one is
    // hidden (Draw shades the empty one instead).
    FormatTradeWidgetName("PlayerCargoBarFull");
    PlayerCargoEmpty = CreateStaticControl(G_TradeWidgetName, kCargoEmptyMesh, Zero,
                                           "hp_player_cargo_capacity", Zero, 1, 1, 0, 1, 0, 0, 1,
                                           0, 0, 0, Zero);
    PlayerCargoFull->SetProperty(1, 1, 0);

    // 0x004c0b31..0x004c0bf2: the other player's full bar with the same colours (0x004c0bbd,
    // 0x004c0be0), hidden.
    FormatTradeWidgetName("TargetCargoBarFull");
    TargetCargoFull = CreateStaticControl(G_TradeWidgetName, kCargoFullMesh, Zero,
                                          "hp_target_cargo_capacity", Zero, 1, 1, 0, 1, 0, 0, 1,
                                          0, 0, 0, Zero);
    WinControlHalfExtentsFromMesh(TargetCargoFull->HalfExtents, &TargetCargoFull->MeshHolder);
    SetCargoBarColours(AsStatic(TargetCargoFull));
    TargetCargoFull->SetProperty(1, 1, 0);

    // 0x004c0bf8..0x004c0c60: its caption (retail's hardpoint name is spelt "traget").
    FormatTradeWidgetName("TargetCargoText");
    TargetCargoText = CreateTextControl(G_TradeWidgetName, "SubHeader", Zero, "hp_traget_cargo_text",
                                        TargetCargoFull->HalfExtents[0] + TargetCargoFull->HalfExtents[0],
                                        0.051f, 1.25f, 0x755, 1, 0, 0);

    // 0x004c0c66..0x004c0cc3: the other player's empty bar.
    FormatTradeWidgetName("TargetCargoBarFull");
    TargetCargoEmpty = CreateStaticControl(G_TradeWidgetName, kCargoEmptyMesh, Zero,
                                           "hp_target_cargo_capacity", Zero, 1, 1, 0, 1, 0, 0, 1,
                                           0, 0, 0, Zero);

    // 0x004c0cc9..0x004c0db5: the quantity panel's item row, 0.015 right and 0.032 down
    // (DAT_005d1f04, DAT_005d69c0) of the centre list's hardpoint (this window's slot 36).
    float Centre[12];
    Win3DIdentityRotation3x3(Centre);
    Centre[9] = 0.0f;
    Centre[10] = 0.0f;
    Centre[11] = 0.0f;
    GetHardpointTransform("hp_center_inventory_list", Centre);
    float RowAt[3];
    RowAt[0] = Centre[9] + 0.015f;
    RowAt[1] = Centre[10] - 0.032f;
    RowAt[2] = Centre[11];
    QuantityLine = new CTradeEquipListLine(nullptr); // operator_new(0x2c), ctor inlined
    QuantityLine->Init(this, RowAt);

    // 0x004c0dbb..0x004c0e17: the panel's title.
    FormatTradeWidgetName("QuantTitTxt");
    QuantityTitle = CreateTextControl(G_TradeWidgetName, "ItemName", Zero, "hp_quantity_title",
                                      0.415f, 0.051f, 1.5f, 0, 1, 0, 0);

    // 0x004c0e1d..0x004c0e90: the number box.
    FormatTradeWidgetName("QuantFrame");
    QuantityFrame = CreateStaticControl(G_TradeWidgetName, kQuantityBoxMesh, Zero,
                                        "hp_quantity_number_frame", Zero, 1, 1, 1, 0, 0, 0, 1, 0,
                                        0, 0, Zero);
    WinControlHalfExtentsFromMesh(QuantityFrame->HalfExtents, &QuantityFrame->MeshHolder);

    // 0x004c0e95..0x004c0f25: the number, 0.01 up (DAT_005c99c8), as wide as the box and as
    // high as its half height; then 0.
    float const NumberAt[3] = {Zero[0], Zero[1] + 0.01f, Zero[2]};
    FormatTradeWidgetName("QuantText");
    QuantityText = AsText(CreateTextControl(
        G_TradeWidgetName, "ItemName", NumberAt, "hp_quantity_number_frame",
        QuantityFrame->HalfExtents[0] + QuantityFrame->HalfExtents[0], QuantityFrame->HalfExtents[1],
        1.5f, 0, 1, 0, 0));
    SetQuantity(0);

    // 0x004c0f25..0x004c0ffa: accept and cancel of the quantity panel.
    FormatTradeWidgetName("QuantAccept");
    QuantityAccept = CreateButtonControl(G_TradeWidgetName, kExecuteMesh, kExecuteMesh, nullptr,
                                         nullptr, Zero, "hp_center_add_accept", Zero, 0, 0, 0, 1,
                                         0, 0, 0);
    WinControlHalfExtentsFromMesh(QuantityAccept->HalfExtents, &QuantityAccept->MeshHolder);
    FormatTradeWidgetName("QuantReject");
    QuantityReject = CreateButtonControl(G_TradeWidgetName, kCloseMesh, kCloseMesh, nullptr, nullptr,
                                         Zero, "hp_center_add_cancel", Zero, 0, 0, 0, 1, 0, 0, 0);
    WinControlHalfExtentsFromMesh(QuantityReject->HalfExtents, &QuantityReject->MeshHolder);

    // 0x004c0fff..0x004c10f3: the quantity bar [0, 100], page 1, and the half extents of its
    // four parts from their meshes.
    FormatTradeWidgetName("QuantScrollBar");
    QuantityBar = static_cast<ScrollControl*>(CreateScrollControl(
        G_TradeWidgetName, kQuantityBarMesh, Zero, "hp_center_quantity_bar", Zero, true,
        kRightArrowMesh, Zero, "hp_center_quantity_right", Zero, false, kLeftArrowMesh, Zero,
        "hp_center_quantity_left", Zero, false, kQuantityHandleMesh, Zero, Zero,
        "hp_center_quantity_thumb_left", "hp_center_quantity_thumb_right", Zero, false, 0, 100, 1,
        false, false));
    WinControlHalfExtentsFromMesh(&QuantityBar->Back.MouseSize.X, &QuantityBar->Back.Holder);
    WinControlHalfExtentsFromMesh(&QuantityBar->Next.MouseSize.X, &QuantityBar->Next.Holder);
    WinControlHalfExtentsFromMesh(&QuantityBar->Prev.MouseSize.X, &QuantityBar->Prev.Holder);
    WinControlHalfExtentsFromMesh(&QuantityBar->ThumbMouse.X, &QuantityBar->ThumbHolder);

    // 0x004c10f8..0x004c11d7: the "accept trade" box (+0x331 bit 0 set) and its caption
    // (ids 0x756) filling it.
    FormatTradeWidgetName("AcceptTradeFrame");
    AcceptTradeFrame = CreateButtonControl(G_TradeWidgetName, kAcceptTradeMesh, kAcceptTradeMesh,
                                           nullptr, nullptr, Zero, "hp_center_trade_accept", Zero,
                                           1, 0, 0, 1, 0, 0, 0);
    WinControlHalfExtentsFromMesh(AcceptTradeFrame->HalfExtents, &AcceptTradeFrame->MeshHolder);
    FormatTradeWidgetName("AcceptTradeText");
    AcceptTradeText = CreateTextControl(
        G_TradeWidgetName, "ButtonText", Zero, "hp_center_trade_accept",
        AcceptTradeFrame->HalfExtents[0] + AcceptTradeFrame->HalfExtents[0],
        AcceptTradeFrame->HalfExtents[1] + AcceptTradeFrame->HalfExtents[1], 2.0f, 0x756, 1, 0, 0);

    // 0x004c11dd..0x004c14e7: the player's info card, hidden -- frame, close button, a turning
    // ship view (+0x518 bit 7 cleared), the item's name and a RichTextWin for the info text.
    float const CardFrameAt[3] = {0.002f, -0.007f, 0.0f};
    FormatTradeWidgetName("PICFrame");
    PlayerCardFrame = CreateStaticControl(G_TradeWidgetName, kInfoFrameMesh, CardFrameAt,
                                          "hp_player_infocard_center", Zero, 0, 1, 1, 0, 0, 0, 1,
                                          0, 0, 0, Zero);
    PlayerCardFrame->SetProperty(1, 1, 0);

    FormatTradeWidgetName("PICClose");
    PlayerCardClose = CreateButtonControl(G_TradeWidgetName, kCloseMesh, kCloseMesh, nullptr,
                                          nullptr, Zero, "hp_player_infocard_close", Zero, 0, 0, 0,
                                          1, 0, 0, 0);
    WinControlHalfExtentsFromMesh(PlayerCardClose->HalfExtents, &PlayerCardClose->MeshHolder);
    PlayerCardClose->SetProperty(1, 1, 0);

    unsigned const NoColour = 0;
    float const ModelViewSize[3] = {0.13f, -0.047f, 0.0f};
    float const ModelYpr[3] = {0.0f, 270.0f, 0.0f};
    float const ModelRotSpeed[3] = {0.0f, 0.0f, 30.0f};
    FormatTradeWidgetName("PICModel");
    PlayerCardModel = static_cast<ShipViewControl*>(CreateShipViewControl(
        G_TradeWidgetName, ModelRotSpeed, Zero, ModelYpr, ModelViewSize, "hp_player_infocard_model",
        false, false, &NoColour, &NoColour));
    PlayerCardModel->SetProperty(1, 1, 0);
    PlayerCardModel->ViewFlags &= 0x7f;

    float const PlayerTitleAt[3] = {0.095f, 0.0125f, 0.0f};
    FormatTradeWidgetName("PICTitle");
    PlayerCardTitle = CreateTextControl(G_TradeWidgetName, "SubHeader", PlayerTitleAt,
                                        "hp_player_infocard_itemname", 0.255f, 0.01f, 1.5f, 0, 0,
                                        0, 0);
    PlayerCardTitle->SetProperty(1, 1, 0);
    PlayerCardTitle->SetProperty(0x22, 0, 1);

    FormatTradeWidgetName("PICInfo");
    PlayerCardInfo = static_cast<RichTextWin*>(
        WinFactoryCreate(G_TradeWidgetName, "RichTextWin", PlayerCardFrame, nullptr));
    PlayerCardInfo->SetNormSize(0.23f, 0.205f);             // FUN_00579cc0
    float const PlayerInfoAt[3] = {-0.124f, 0.0325f, 0.0f};
    PlayerCardInfo->SetAttachOffset(PlayerInfoAt);          // FUN_00579c40
    PlayerCardInfo->Flags &= 0xfc;

    // 0x004c14e7..0x004c1804: the other player's info card, the same, a little further right.
    float const TargetFrameAt[3] = {0.01f, -0.007f, 0.0f};
    FormatTradeWidgetName("TICFrame");
    TargetCardFrame = CreateStaticControl(G_TradeWidgetName, kInfoFrameMesh, TargetFrameAt,
                                          "hp_target_infocard_center", Zero, 0, 1, 1, 0, 0, 0, 1,
                                          0, 0, 0, Zero);
    TargetCardFrame->SetProperty(1, 1, 0);

    float const TargetCloseAt[3] = {0.01f, 0.0f, 0.0f};
    FormatTradeWidgetName("TICClose");
    TargetCardClose = CreateButtonControl(G_TradeWidgetName, kCloseMesh, kCloseMesh, nullptr,
                                          nullptr, TargetCloseAt, "hp_target_infocard_close", Zero,
                                          0, 0, 0, 1, 0, 0, 0);
    WinControlHalfExtentsFromMesh(TargetCardClose->HalfExtents, &TargetCardClose->MeshHolder);
    TargetCardClose->SetProperty(1, 1, 0);

    float const TargetModelAt[3] = {0.01f, 0.0f, 0.0f};
    FormatTradeWidgetName("TICModel");
    TargetCardModel = static_cast<ShipViewControl*>(CreateShipViewControl(
        G_TradeWidgetName, ModelRotSpeed, TargetModelAt, ModelYpr, ModelViewSize,
        "hp_target_infocard_model", false, false, &NoColour, &NoColour));
    TargetCardModel->SetProperty(1, 1, 0);
    TargetCardModel->ViewFlags &= 0x7f;

    float const TargetTitleAt[3] = {0.105f, 0.0125f, 0.0f};
    FormatTradeWidgetName("TICTitle");
    TargetCardTitle = CreateTextControl(G_TradeWidgetName, "SubHeader", TargetTitleAt,
                                        "hp_target_infocard_itemname", 0.255f, 0.01f, 1.5f, 0, 0,
                                        0, 0);
    TargetCardTitle->SetProperty(1, 1, 0);
    TargetCardTitle->SetProperty(0x22, 0, 1);

    FormatTradeWidgetName("TICInfo");
    TargetCardInfo = static_cast<RichTextWin*>(
        WinFactoryCreate(G_TradeWidgetName, "RichTextWin", TargetCardFrame, nullptr));
    TargetCardInfo->SetNormSize(0.23f, 0.205f);
    float const TargetInfoAt[3] = {-0.117f, 0.0325f, 0.0f};
    TargetCardInfo->SetAttachOffset(TargetInfoAt);
    TargetCardInfo->Flags &= 0xfc;

    // 0x004c1809..0x004c1876: the window's close button.
    FormatTradeWidgetName("TradeClose");
    TradeClose = CreateButtonControl(G_TradeWidgetName, kCloseMesh, kCloseMesh, nullptr, nullptr,
                                     Zero, "hp_tradecancel", Zero, 0, 0, 0, 1, 0, 0, 0);
    WinControlHalfExtentsFromMesh(TradeClose->HalfExtents, &TradeClose->MeshHolder);

    // 0x004c187b..0x004c1983: the three lists; the player's is filled at once.
    TargetList = new CTradeEquipList(this, "hp_target_inventory_list", "hp_target_scroll_bar",
                                     "hp_target_scroll_up", "hp_target_scroll_down",
                                     "hp_target_thumb_top", "hp_target_thumb_bottom", 5, 1, 0);
    PlayerList = new CTradeEquipList(this, "hp_player_inventory_list", "hp_player_scroll_bar",
                                     "hp_player_scroll_up", "hp_player_scroll_down",
                                     "hp_player_scroll_thumb_top", "hp_player_scroll_thumb_bottom",
                                     5, 1, 1);
    PlayerList->Rebuild(); // FUN_004bf720
    CenterList = new CTradeEquipList(this, "hp_center_inventory_list", "hp_center_scroll_bar",
                                     "hp_center_scroll_up", "hp_center_scroll_down",
                                     "hp_center_thumb_top", "hp_center_thumb_down", 5, 0, 1);

    // 0x004c1989..0x004c1997: closed, then browsing; drawn above the other windows.
    SetMode(1);
    SetMode(2);
    DrawLayer = 1;

    // 0x004c199a..0x004c19e3: the status and target panels are collapsed while trading; what
    // they were is kept for the close (0xef02).
    HUD_Status* const Status = GetHudStatus(); // DAT_00674aa0
    if (Status != nullptr)
    {
        bHudStatusWasExpanded = Status->bExpanded;
        if (bHudStatusWasExpanded != 0)
            Status->Slot12(); // HUDItem vt+0x30
    }
    HudTargetBase* const Target = G_HudTargetPanel; // DAT_00674b04
    if (Target != nullptr)
    {
        bHudTargetWasExpanded = Target->bExpanded;
        if (bHudTargetWasExpanded != 0)
            Target->Slot12();
    }

    ChatPlaceMsgWinForTrade(1); // FUN_0046b450(1), 0x004c19e4
    return true;
}

// ---------------------------------------------------------------------------------------------
// Per frame
// ---------------------------------------------------------------------------------------------

void NnTrade::Draw()
{
    // LAB_004c0120.
    Win3DFlushBeforeOverlayDraw(); // FUN_0045a9f0
    BlinkQuantityCaret(QuantityText); // FUN_004bff30
    WinControl::Draw();               // FUN_005a0760

    // 0x004c0144..0x004c0312: the player's bar, shaded right of the used share of the hold.
    // Every branch then sets the full bar's bUseColours (+0x348) = 1 (0x004c018b, 0x004c01a2).
    if (PlayerCargoFull != nullptr)
    {
        double const Hold = GetPlayerShipHoldSizeRetail(); // FUN_004c4610
        double Ratio;
        if (PlayerCargoUsed > 1e-5) // DAT_005cbfb0; TEST AH,0x41 / JNZ -> not greater
        {
            Ratio = PlayerCargoUsed / Hold; // FDIVR
            if (Ratio > 1.0)                // DAT_005c89b8
                Ratio = 1.0;
        }
        else
        {
            Ratio = 0.0; // DAT_005c8ba8
        }
        AsStatic(PlayerCargoFull)->bUseColours = 1;
        // The quad and the mesh are both the (hidden) full bar's (0x004c020e, 0x004c02e5).
        DrawCargoFill(PlayerCargoFull, CargoFillX(PlayerCargoFull, Ratio));
    }

    // 0x004c0315..0x004c04cb: the other player's bar; both paths join at 0x004c035b, which sets
    // its bUseColours (+0x348) = 1.
    if (TargetCargoFull != nullptr)
    {
        double Ratio;
        if (TargetCargoUsed > 1e-5)
        {
            Ratio = TargetCargoUsed / TargetHoldSize;
            if (Ratio > 1.0)
                Ratio = 1.0;
        }
        else
        {
            Ratio = 0.0;
        }
        AsStatic(TargetCargoFull)->bUseColours = 1;
        DrawCargoFill(TargetCargoFull, CargoFillX(TargetCargoFull, Ratio));
    }
}

bool NnTrade::KeyEvent(unsigned Key, unsigned char* Modifiers)
{
    // FUN_004c2c70 (RET 8). Only the quantity panel takes keys, and only with modifier bit 0
    // (key down) or bit 1.
    bool bHandled = false;
    if (Mode != 3)
        return false;
    unsigned char const Mods = *Modifiers;
    unsigned char const bDown = static_cast<unsigned char>(Mods & 1);
    if (bDown == 0 && (Mods & 2) == 0)
        return false;

    int const Code = static_cast<int>(Key);
    if (Code == 8)
    {
        // 0x004c2ca3..0x004c2cc6: backspace drops the last digit.
        if (bDown != 0)
            Quantity = Quantity / 10;
        bHandled = true;
    }
    else if (Code >= '0' && Code <= '9')
    {
        // 0x004c2cd2..0x004c2d0f: the first digit typed replaces the value; a value outside the
        // bar's range is ignored.
        bHandled = true;
        if (bDown != 0)
        {
            if (bQuantityTyped == 0)
                Quantity = 0;
            int const Typed = Code + Quantity * 10 - '0';
            if (Typed >= QuantityBar->MinRange && Typed <= QuantityBar->MaxRange)
                Quantity = Typed;
        }
    }

    // 0x004c2d15..0x004c2d42.
    bQuantityTyped = 1;
    if (bHandled)
    {
        QuantityBar->SetProperty(0x38, static_cast<uintptr_t>(Quantity), 0);
        SetQuantity(Quantity);
    }
    return bHandled;
}

// ---------------------------------------------------------------------------------------------
// Quantity panel and modes
// ---------------------------------------------------------------------------------------------

void NnTrade::SetQuantity(int NewQuantity)
{
    // FUN_004c1a10 (RET 4).
    if (QuantityBar != nullptr)
    {
        if (NewQuantity > QuantityBar->MaxRange)
            NewQuantity = QuantityBar->MaxRange;
        else if (NewQuantity < QuantityBar->MinRange)
            NewQuantity = QuantityBar->MinRange;
    }

    // 0x004c1a47..0x004c1ab3: "%d" into the first 32 characters of the 0x80-byte buffer, then
    // copied backwards into the second half with a ',' after every third digit from the right.
    wchar_t Buffer[64];
    swprintf(Buffer, 32, kQuantityFormat, NewQuantity);
    int Remaining = static_cast<int>(wcslen(Buffer));
    wchar_t* Out = &Buffer[32 + Remaining + Remaining / 3];
    *Out = L'\0';
    wchar_t const* In = &Buffer[Remaining - 1];
    int Group = 0;
    while (Remaining != 0)
    {
        wchar_t const Digit = *In;
        --Out;
        --Remaining;
        ++Group;
        --In;
        *Out = Digit;
        if (Group == 3)
        {
            if (Remaining == 0)
                break;
            --Out;
            *Out = L',';
            Group = 0;
        }
    }

    // 0x004c1ab5..0x004c1ac8.
    QuantityText->SetProperty(0x1d, PointerArg(Out), 0);
    Quantity = NewQuantity;
}

void NnTrade::SetMode(int NewMode)
{
    // FUN_004c1ae0 (RET 4).
    int Previous = Mode;
    if (NewMode == 4)
    {
        Previous = 4;
        NewMode = 3;
    }
    Mode = NewMode;

    if (NewMode == 1)
    {
        // 0x004c207d..0x004c2205: everything but the lists hidden.
        TitleFrame->SetProperty(1, 1, 0);
        TitleArrow->SetProperty(1, 1, 0);
        TitleText->SetProperty(1, 1, 0);
        QuantityLine->Hide(); // FUN_004bed40 inlined
        QuantityTitle->SetProperty(1, 1, 0);
        QuantityFrame->SetProperty(1, 1, 0);
        QuantityAccept->SetProperty(1, 1, 0);
        QuantityReject->SetProperty(1, 1, 0);
        QuantityText->SetProperty(1, 1, 0);
        QuantityBar->SetProperty(1, 1, 0);
        for (size_t Index = 0; Index < CenterList->Lines.size(); ++Index)
            CenterList->Lines[Index]->Hide();
        CenterList->ScrollBar->SetProperty(1, 1, 0);
        AcceptTradeFrame->SetProperty(1, 1, 0);
        AcceptTradeText->SetProperty(1, 1, 0);
        return;
    }

    if (NewMode == 2)
    {
        // 0x004c1ed6..0x004c2074: the arrow, the title, the offer list and "accept trade".
        TitleFrame->SetProperty(1, 1, 0);
        TitleArrow->SetProperty(1, 0, 0);
        TitleText->SetProperty(1, 0, 0);
        QuantityLine->Hide(); // FUN_004bed40 inlined
        QuantityTitle->SetProperty(1, 1, 0);
        QuantityFrame->SetProperty(1, 1, 0);
        QuantityAccept->SetProperty(1, 1, 0);
        QuantityReject->SetProperty(1, 1, 0);
        QuantityText->SetProperty(1, 1, 0);
        QuantityBar->SetProperty(1, 1, 0);
        for (size_t Index = 0; Index < CenterList->Lines.size(); ++Index)
            CenterList->Lines[Index]->Show(); // FUN_004bedc0
        CenterList->ScrollBar->SetProperty(1, 0, 0);
        CenterList->Refresh(-1);
        AcceptTradeFrame->SetProperty(1, 0, 0);
        AcceptTradeText->SetProperty(1, 0, 0);
        return;
    }

    if (NewMode != 3)
        return;

    // 0x004c1b1f..0x004c1c33: the quantity panel replaces the offer list.
    for (size_t Index = 0; Index < CenterList->Lines.size(); ++Index)
        CenterList->Lines[Index]->Hide();
    CenterList->ScrollBar->SetProperty(1, 1, 0);
    AcceptTradeFrame->SetProperty(1, 1, 0);
    AcceptTradeText->SetProperty(1, 1, 0);
    TitleFrame->SetProperty(1, 1, 0);
    TitleArrow->SetProperty(1, 0, 0);
    TitleText->SetProperty(1, 0, 0);
    QuantityLine->SetItem(SelectedItem); // FUN_004bef00
    QuantityLine->Show();                // FUN_004bedc0
    QuantityTitle->SetProperty(1, 0, 0);
    QuantityFrame->SetProperty(1, 0, 0);
    QuantityAccept->SetProperty(1, 0, 0);
    QuantityReject->SetProperty(1, 0, 0);
    QuantityText->SetProperty(1, 0, 0);
    QuantityBar->SetProperty(1, 0, 0);

    // 0x004c1c39..0x004c1ce0: one unit of the item, which side it comes from, the other hold's
    // free space (1000 when the other ship's hold size is unknown).
    EquipDesc Unit = *SelectedItem;
    Unit.set_count(1);
    bool const bFromPlayer = SelectedLine->Owner == PlayerList;
    double const UnitVolume = Unit.get_cargo_space_occupied();
    double Free;
    if (TargetHoldSize != 0.0) // TEST AH,0x44 / JNP -> equal
        Free = TargetHoldSize - TargetCargoUsed;
    else
        Free = 1000.0;

    // 0x004c1ce0..0x004c1dad: the most the other side can take. (EBP -- Held -- is 0 again by
    // the clamp at 0x004c1da9, so the clamp and the bar's minimum are 0.)
    int Held = 0;
    int Most;
    if (Unit.ArchId == G_ShieldBatteryArchId) // DAT_00674a88
    {
        Most = TargetBatteryLimit - TargetBatteries - (bFromPlayer ? BatteriesOffered : 0);
    }
    else if (Unit.ArchId == G_NanobotArchId) // DAT_00674a8c
    {
        Most = TargetNanobotLimit - TargetNanobots - (bFromPlayer ? NanobotsOffered : 0);
    }
    else if (PlayerShip->get_ammo_capacity_remaining(Unit.ArchId) > -1)
    {
        // 0x004c1d43..0x004c1d7c: ammo -- the other ship's room less what is already offered.
        if (bFromPlayer)
        {
            EquipDesc const* const Offered = CenterList->Items.find_matching_cargo(Unit.ArchId, 1, 1.0f);
            if (Offered != nullptr)
                Held = Offered->Count;
        }
        Most = TargetShip->get_ammo_capacity_remaining(Unit.ArchId) - Held;
        Held = 0;
    }
    else if (UnitVolume != 0.0)
    {
        Most = static_cast<int>(Free / UnitVolume); // ftol
    }
    else
    {
        Most = SelectedItem->Count;
    }
    if (Most < Held)
        Most = 0;
    if (!bFromPlayer)
        Most = SelectedItem->Count;

    // 0x004c1dbc..0x004c1e07: the bar's range; a single unit of the player's has no bar.
    QuantityBar->MinRange = Held;
    QuantityBar->MaxRange = (SelectedItem->Count < Most) ? SelectedItem->Count : Most;
    if (bFromPlayer && SelectedItem->Count == 1)
        QuantityBar->SetProperty(1, 1, 0);

    // 0x004c1e07..0x004c1e35: re-entered (4) with a value that no longer fits -- the voice line;
    // a fresh panel forgets the typed digits.
    if (Previous == 4)
    {
        if (QuantityBar->Value > Most)
            QueueTradeVoice(G_InsufficientCargoSpaceVoiceId); // FUN_00489e80(DAT_00672458, 0)
    }
    else
    {
        bQuantityTyped = 0;
    }

    // 0x004c1e35..0x004c1e87: start at the most; the title says which way it goes.
    QuantityBar->SetProperty(0x38, static_cast<uintptr_t>(Most), 0);
    SetQuantity(Most);
    QuantityTitle->SetProperty(0x1e, (SelectedLine->Owner == PlayerList) ? 0x757 : 0x758, 0);

    // 0x004c1e87..0x004c1ec8: nothing fits -- the voice line and back to browsing.
    if (Most >= 1)
        return;
    QueueTradeVoice(G_InsufficientCargoSpaceVoiceId);
    CTradeEquipList* const Owner = SelectedLine->Owner;
    SetTradeMark(SelectedItem, 0);
    SelectedLine = nullptr;
    SelectedItem = nullptr;
    Owner->Refresh(-1);
    SetMode(2);
}

// ---------------------------------------------------------------------------------------------
// The other player's side (IClient slots)
// ---------------------------------------------------------------------------------------------

void NnTrade::AddTargetItem(EquipDesc const* Item)
{
    // FUN_004c2220 (RET 4).
    if (TargetList != nullptr)
    {
        // 0x004c223c..0x004c22c2: a copy without the selection mark, merged when combinable.
        EquipDesc Copy = *Item;
        SetTradeMark(&Copy, 0);
        TargetList->Items.add_equipment_item(Copy, arch_is_combinable(Copy.ArchId));
        TargetList->CargoVolume = TargetList->Items.get_cargo_space_occupied();
        RefreshTradeListRange(TargetList);

        // 0x004c22c7..0x004c2321: the other hold as it will be after the trade.
        FClientGameobj* const Object = FindTradeObject(TargetObjectId);
        if (Object != nullptr)
        {
            CShip* const Ship = HudShipOfObjRw(Object); // FUN_00543320, not null-tested after
            double const TargetOffer = TargetList->CargoVolume;
            double const PlayerOffer = CenterList->CargoVolume;
            TargetCargoUsed = (TargetHoldSize - Ship->get_cargo_hold_remaining()) - TargetOffer +
                              PlayerOffer;
        }

        // 0x004c2327..0x004c235c: and the player's.
        double const PlayerOffer = CenterList->CargoVolume;
        double const TargetOffer = TargetList->CargoVolume;
        PlayerCargoUsed = GetPlayerEquipDescListRetail().get_cargo_space_occupied() - PlayerOffer +
                          TargetOffer; // FUN_004c4640
    }

    // 0x004c2362..0x004c236f: an open quantity panel re-checks its limits.
    if (Mode == 3)
        SetMode(4);
}

void NnTrade::RemoveTargetItem(EquipDesc const* Item)
{
    // FUN_004c2380 (RET 4).
    if (TargetList != nullptr)
    {
        // 0x004c239c..0x004c2406: the entry before and after the removal.
        EquipDescList& Items = TargetList->Items;
        EquipDesc* const Before = Items.find_equipment_item(Item->Id);
        Items.remove_equipment_item(Item->Id, Item->Count);
        TargetList->CargoVolume = Items.get_cargo_space_occupied();
        EquipDesc* const After = Items.find_equipment_item(Item->Id);
        RefreshTradeListRange(TargetList);
        EquipDesc* const Kept = (After != nullptr) ? Before : nullptr; // NEG / SBB / AND

        // 0x004c240b..0x004c24b0: the two holds (see AddTargetItem).
        FClientGameobj* const Object = FindTradeObject(TargetObjectId);
        if (Object != nullptr)
        {
            CShip* const Ship = HudShipOfObjRw(Object);
            double const TargetOffer = TargetList->CargoVolume;
            double const PlayerOffer = CenterList->CargoVolume;
            TargetCargoUsed = (TargetHoldSize - Ship->get_cargo_hold_remaining()) - TargetOffer +
                              PlayerOffer;
        }
        double const PlayerOffer = CenterList->CargoVolume;
        double const TargetOffer = TargetList->CargoVolume;
        PlayerCargoUsed = GetPlayerEquipDescListRetail().get_cargo_space_occupied() - PlayerOffer +
                          TargetOffer;

        // 0x004c24b6..0x004c24e6: the entry shown on the other player's card changed -- close
        // the card and drop the selection. (A removed entry leaves Kept null, which matches a
        // list with nothing selected.)
        if (Kept == TargetList->SelectedItem)
        {
            ShowInfoCard(TargetList, 0);
            if (TargetList->SelectedItem != nullptr)
            {
                SetTradeMark(TargetList->SelectedItem, 0);
                TargetList->SelectedItem = nullptr;
            }
            TargetList->Refresh(-1);
        }
    }

    if (Mode == 3)
        SetMode(4);
}

void NnTrade::SetTarget(unsigned ObjectId)
{
    // FUN_004c2510 (RET 4).
    FClientGameobj* const Object = FindTradeObject(ObjectId);
    TargetObjectId = ObjectId;
    if (Object == nullptr)
        return;

    // 0x004c2531..0x004c2608: the other ship, its hold, name, owner and ammo room.
    CShip* const Ship = HudShipOfObjRw(Object); // FUN_00543320, not null-tested after
    TargetShip = Ship;
    Archetype::Ship const* const ShipArch = Ship->shiparch(); // CObject +0x88
    TargetHoldSize = ShipArch->HoldSize;
    TargetCargoUsed = TargetHoldSize - Ship->get_cargo_hold_remaining();
    TargetName->SetProperty(0x1d, PointerArg(Ship->get_pilot_name()), 0);
    TargetOwnerPlayer = Ship->OwnerPlayer; // CSimple +0xb4
    TargetNanobotLimit = ShipArch->NanobotLimit;
    if (TargetNanobotLimit == -1)
        TargetNanobotLimit = kUnlimitedAmmo;
    TargetNanobots = ShipArch->NanobotLimit - Ship->get_repair_kit_capacity_remaining();
    TargetBatteryLimit = ShipArch->ShieldBatteryLimit;
    if (TargetBatteryLimit == -1)
        TargetBatteryLimit = kUnlimitedAmmo;
    TargetBatteries = ShipArch->ShieldBatteryLimit - Ship->get_shield_battery_capacity_remaining();
}

void NnTrade::SetTargetAccepted(unsigned char bAccepted)
{
    // FUN_004c2620 (RET 4): ids 0x759 on green, else 0x75a on red; alpha 0xc0.
    if (bAccepted == 1)
    {
        TargetAcceptedText->SetProperty(0x1e, 0x759, 0);
        TargetAcceptedText->TextColour = TradeColour(0, 0xff, 0, 0xc0);
    }
    else
    {
        TargetAcceptedText->SetProperty(0x1e, 0x75a, 0);
        TargetAcceptedText->TextColour = TradeColour(0xff, 0, 0, 0xc0);
    }
}

void NnTrade::SetPlayerAccepted(unsigned char bAccepted)
{
    // FUN_004c2690 (RET 4) -- FUN_004c2620 on +0x410.
    if (bAccepted == 1)
    {
        PlayerAcceptedText->SetProperty(0x1e, 0x759, 0);
        PlayerAcceptedText->TextColour = TradeColour(0, 0xff, 0, 0xc0);
    }
    else
    {
        PlayerAcceptedText->SetProperty(0x1e, 0x75a, 0);
        PlayerAcceptedText->TextColour = TradeColour(0xff, 0, 0, 0xc0);
    }
}

void NnTrade::SetTargetMoney(int Money)
{
    // FUN_004c2700 (RET 4) -- the body of FUN_004bf620 on +0x330, instruction for instruction
    // (credits entry found: count, or erase every entry with its id; none: insert the template
    // at the end and count it; then the template's count and Refresh(-1)).
    TargetList->SetMoney(Money);
}

void NnTrade::ShowInfoCard(CTradeEquipList* List, unsigned char bShow)
{
    // FUN_004c2800 (RET 8).
    CacheString Mesh;
    CacheString Material;
    Mesh.clear();
    Material.clear();
    // Retail lends the bShow argument slot to FUN_004be6c0 as the name id and a stack local as
    // the info id; FUN_004be6c0 leaves both untouched only for an entry with no archetype at all.
    int IdsName = static_cast<int>(bShow);
    int IdsInfo = 0;
    if (bShow != 0)
        GetTradeItemVisuals(List->SelectedItem, &Mesh, &Material, &IdsName, &IdsInfo, true);

    if (List == PlayerList)
    {
        // 0x004c286a..0x004c2a26.
        List->bSelectMode = bShow;
        List->Refresh(-1);
        uintptr_t const bHide = (bShow == 0) ? 1 : 0;
        PlayerCardFrame->SetProperty(1, bHide, 0);
        PlayerCardClose->SetProperty(1, bHide, 0);
        PlayerCardModel->SetProperty(1, bHide, 0);
        PlayerCardTitle->SetProperty(1, bHide, 0);
        if (bShow != 0)
        {
            // 0x004c28d3: FUN_0053e4a0(1) on the ship view -- RET 4, nothing.
            if (Mesh.Value != nullptr)
                PlayerCardModel->LoadMesh(Mesh.Value, Material.Value, false,
                                          false); // FUN_00585890
            PlayerCardTitle->SetProperty(0x1e, static_cast<uintptr_t>(IdsName), 0);
            RenderDisplayList Card;
            {
                StyleNode Style(0xd801);
                RdlAppendNode(Card, Style.Clone());
            }
            LoadIdsIntoRdl(static_cast<unsigned>(IdsInfo), Card); // FUN_0057da40
            PlayerCardInfo->ClearContent();                      // FUN_0057b370
            PlayerCardInfo->AppendContent(Card);                 // FUN_0057b3e0
            PlayerCardInfo->Flags |= 3;
            PlayerCardInfo->ShowScrollBarV(true);                // FUN_00579bd0
        }
        else
        {
            PlayerCardInfo->Flags &= 0xfc;
            PlayerCardInfo->ShowScrollBarV(false);
        }
    }
    else if (List == TargetList)
    {
        // 0x004c2a37..0x004c2c32: the same for the other player's card, except that the ship
        // view is loaded without testing the mesh (0x004c2aad..0x004c2ac1).
        List->bSelectMode = bShow;
        List->Refresh(-1);
        uintptr_t const bHide = (bShow == 0) ? 1 : 0;
        TargetCardFrame->SetProperty(1, bHide, 0);
        TargetCardClose->SetProperty(1, bHide, 0);
        TargetCardModel->SetProperty(1, bHide, 0);
        TargetCardTitle->SetProperty(1, bHide, 0);
        if (bShow != 0)
        {
            TargetCardModel->LoadMesh(Mesh.Value, Material.Value, false, false);
            TargetCardTitle->SetProperty(0x1e, static_cast<uintptr_t>(IdsName), 0);
            RenderDisplayList Card;
            {
                StyleNode Style(0xd801);
                RdlAppendNode(Card, Style.Clone());
            }
            LoadIdsIntoRdl(static_cast<unsigned>(IdsInfo), Card);
            TargetCardInfo->ClearContent();
            TargetCardInfo->AppendContent(Card);
            TargetCardInfo->Flags |= 3;
            TargetCardInfo->ShowScrollBarV(true);
        }
        else
        {
            TargetCardInfo->Flags &= 0xfc;
            TargetCardInfo->ShowScrollBarV(false);
        }
    }

    // 0x004c2c37..0x004c2c4c: the quantity panel's item follows the card of its list.
    if (SelectedLine != nullptr && SelectedLine->Owner == List)
        SetTradeMark(SelectedItem, bShow);
}

// ---------------------------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------------------------

intptr_t NnTrade::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // FUN_004c2d50 (RET 0xc).
    WinControl* const Control = reinterpret_cast<WinControl*>(A);

    if (Msg == 0x2c || Msg == 0x2d)
    {
        // 0x004c2d82..0x004c2e31 (0x2c, mouse over) / 0x004c35bb..0x004c369d (0x2d, mouse out):
        // a row's frames follow the hovered button's bit 2, get bit 0 (pressed-look) set or
        // cleared and the row is lifted: layer 2 when selected (bit 6), else 1 while hovered.
        // Only ButtonControls send these (0x0055b8cb, 0x0055bbf9); the rows' buttons carry the
        // row in +0x2f0.
        CTradeEquipListLine* const Line = reinterpret_cast<CTradeEquipListLine*>(Control->Field2f0);
        if (Line == nullptr)
            return 0;
        if (Control == Line->InfoCard)
            return 0;
        unsigned char const Bit2 =
            static_cast<unsigned char>(static_cast<NnButtonControl*>(Control)->GetFlags330() & 0x04);
        unsigned char Flags = Line->IconFrame->GetFlags330();
        Line->IconFrame->SetFlags330(static_cast<unsigned char>(((Flags ^ Bit2) & 0x04) ^ Flags));
        Flags = Line->InfoFrame->GetFlags330();
        Line->InfoFrame->SetFlags330(static_cast<unsigned char>(((Flags ^ Bit2) & 0x04) ^ Flags));
        unsigned Layer;
        if (Msg == 0x2c)
        {
            Line->IconFrame->SetFlags330(static_cast<unsigned char>(Line->IconFrame->GetFlags330() | 1));
            Line->InfoFrame->SetFlags330(static_cast<unsigned char>(Line->InfoFrame->GetFlags330() | 1));
            Layer = 1;
        }
        else
        {
            Line->IconFrame->SetFlags330(static_cast<unsigned char>(Line->IconFrame->GetFlags330() & 0xfe));
            Line->InfoFrame->SetFlags330(static_cast<unsigned char>(Line->InfoFrame->GetFlags330() & 0xfe));
            Layer = 0;
        }
        if ((Line->IconFrame->GetFlags330() & 0x40) != 0)
            Layer = 2;
        Line->IconFrame->DrawLayer = Layer;
        Line->IconMesh->DrawLayer = Layer;
        Line->InfoFrame->DrawLayer = Layer;
        Line->HealthBar->DrawLayer = Layer;
        Line->ItemCount->DrawLayer = Layer;
        if (Line->InfoCard != nullptr)
            Line->InfoCard->DrawLayer = Layer;
        return 0;
    }

    if (Msg == 0x3e)
    {
        // 0x004c374c..0x004c378c: the quantity bar moved, or a list's bar (its +0x2f0 is the
        // list) scrolled.
        if (Control == QuantityBar)
        {
            if (Quantity != static_cast<int>(B))
                SetQuantity(static_cast<int>(B));
            return 0;
        }
        CTradeEquipList* const List = reinterpret_cast<CTradeEquipList*>(Control->Field2f0);
        if (List != nullptr && List->TopIndex != static_cast<int>(B))
        {
            List->TopIndex = static_cast<int>(B);
            List->Refresh(-1);
        }
        return 0;
    }

    if (Msg == 0xef02)
    {
        // 0x004c36bf..0x004c3741: close -- the HUD panels the window collapsed come back, then
        // the window takes itself apart.
        if (bHudStatusWasExpanded != 0)
        {
            HUD_Status* const Status = GetHudStatus();
            if (Status != nullptr && Status->bExpanded == 0)
                Status->Slot12();
        }
        if (bHudTargetWasExpanded != 0)
        {
            HudTargetBase* const Target = G_HudTargetPanel;
            if (Target != nullptr && Target->bExpanded == 0)
                Target->Slot12();
        }
        ChatPlaceMsgWinForTrade(0); // FUN_0046b450(0), 0x004c3707
        Win3DReparent(this, nullptr); // FUN_0059dd00
        Win3D* Child = ChildHead;
        while (Child != nullptr)
        {
            Win3D* const Next = Child->NextSibling;
            Win3DDeleteTree(Child); // FUN_0043d2a0
            Child = Next;
        }
        Destroy(); // slot 10
        delete this; // slot 0 (1)
        return 0;
    }

    if (Msg != 0x26)
        return 0;

    // --- 0x26: a click ---
    if (Control == TradeClose)
    {
        // 0x004c2e4a..0x004c2e70: IServer +0x140, then the button is disabled.
        G_Server->TerminateTrade(GetPlayerConnectionId(), 0);
        TradeClose->SetProperty(2, 1, 0);
        return 0;
    }

    if (Control == PlayerCardClose || Control == TargetCardClose)
    {
        // 0x004c2e89..0x004c2eb8 / 0x004c2ed0..0x004c2eff.
        CTradeEquipList* const List = (Control == PlayerCardClose) ? PlayerList : TargetList;
        ShowInfoCard(List, 0);
        if (List->SelectedItem != nullptr)
        {
            SetTradeMark(List->SelectedItem, 0);
            List->SelectedItem = nullptr;
        }
        List->Refresh(-1);
        return 0;
    }

    if (Control == QuantityAccept)
    {
        // 0x004c2f1b..0x004c3443: move the chosen quantity. From the offer (+0x334) the value is
        // what stays offered; from the player's cargo (+0x338) it is what is added to the offer.
        EquipDesc* Unmark = SelectedItem; // EDI -- cleared when its entry is gone
        EquipDesc* const Item = SelectedItem;
        unsigned const Session = GetPlayerConnectionId(); // DAT_00673344

        if (Item->ArchId == 0)
        {
            // Credits.
            if (SelectedLine->Owner == CenterList)
            {
                // 0x004c2f4b..0x004c2f95.
                int const Value = QuantityBar->Value;
                if (Value < Item->Count)
                {
                    int const Back = Item->Count - Value;
                    if (CenterList->SetMoney(CenterList->CreditsTemplate.Count - Back) == 0)
                        Unmark = nullptr;
                    PlayerList->SetMoney(PlayerList->CreditsTemplate.Count + Back);
                    G_Server->SetTradeMoney(
                        Session, static_cast<unsigned long>(CenterList->CreditsTemplate.Count));
                }
            }
            else if (QuantityBar->Value != 0)
            {
                // 0x004c2fa8..0x004c3006.
                G_Server->SetTradeMoney(Session, static_cast<unsigned long>(
                                                     CenterList->CreditsTemplate.Count +
                                                     QuantityBar->Value));
                CenterList->SetMoney(CenterList->CreditsTemplate.Count + QuantityBar->Value);
                if (PlayerList->SetMoney(PlayerList->CreditsTemplate.Count - QuantityBar->Value) == 0)
                    Unmark = nullptr;
            }
        }
        else if (SelectedLine->Owner == CenterList)
        {
            // 0x004c3022..0x004c320c: back from the offer to the player.
            int const Value = QuantityBar->Value;
            if (Value < Item->Count)
            {
                EquipDesc Moved = *Item;
                int const Back = Moved.Count - Value;
                if (Moved.ArchId == G_ShieldBatteryArchId)
                    BatteriesOffered = Value;
                else if (Moved.ArchId == G_NanobotArchId)
                    NanobotsOffered = Value;
                if (Back > 0)
                {
                    Moved.set_count(Back);
                    G_Server->DelTradeEquip(Session, Moved); // IServer +0x150

                    EquipDescList& Offer = CenterList->Items;
                    EquipDesc* const Before = Offer.find_equipment_item(Moved.Id);
                    Offer.remove_equipment_item(Moved.Id, Moved.Count);
                    CenterList->CargoVolume = Offer.get_cargo_space_occupied();
                    EquipDesc* const After = Offer.find_equipment_item(Moved.Id);
                    RefreshTradeListRange(CenterList);
                    Unmark = (After != nullptr) ? Before : nullptr;

                    EquipDesc Copy(Moved);
                    SetTradeMark(&Copy, 0);
                    PlayerList->Items.add_equipment_item(Copy, arch_is_combinable(Copy.ArchId));
                    PlayerList->CargoVolume = PlayerList->Items.get_cargo_space_occupied();
                    RefreshTradeListRange(PlayerList);

                    FClientGameobj* const Object = FindTradeObject(TargetObjectId);
                    if (Object != nullptr)
                    {
                        CShip* const Ship = HudShipOfObjRw(Object);
                        double const TargetOffer = TargetList->CargoVolume;
                        double const PlayerOffer = CenterList->CargoVolume;
                        TargetCargoUsed = (TargetHoldSize - Ship->get_cargo_hold_remaining()) -
                                          TargetOffer + PlayerOffer;
                    }
                    double const PlayerOffer = CenterList->CargoVolume;
                    double const TargetOffer = TargetList->CargoVolume;
                    PlayerCargoUsed = GetPlayerEquipDescListRetail().get_cargo_space_occupied() -
                                      PlayerOffer + TargetOffer;
                }
            }
        }
        else
        {
            // 0x004c3211..0x004c343d: from the player's cargo into the offer.
            if (QuantityBar->Value != 0)
            {
                EquipDesc Moved = *Item;
                Moved.set_count(QuantityBar->Value);
                G_Server->AddTradeEquip(Session, Moved); // IServer +0x14c
                if (Moved.ArchId == G_ShieldBatteryArchId)
                    BatteriesOffered += QuantityBar->Value;
                else if (Moved.ArchId == G_NanobotArchId)
                    NanobotsOffered += QuantityBar->Value;

                EquipDesc Copy(Moved);
                SetTradeMark(&Copy, 0);
                CenterList->Items.add_equipment_item(Copy, arch_is_combinable(Copy.ArchId));
                CenterList->CargoVolume = CenterList->Items.get_cargo_space_occupied();
                RefreshTradeListRange(CenterList);

                EquipDescList& Cargo = PlayerList->Items;
                EquipDesc* const Before = Cargo.find_equipment_item(Moved.Id);
                Cargo.remove_equipment_item(Moved.Id, Moved.Count);
                PlayerList->CargoVolume = Cargo.get_cargo_space_occupied();
                EquipDesc* const After = Cargo.find_equipment_item(Moved.Id);
                RefreshTradeListRange(PlayerList);
                Unmark = (After != nullptr) ? Before : nullptr;

                FClientGameobj* const Object = FindTradeObject(TargetObjectId);
                if (Object != nullptr)
                {
                    CShip* const Ship = HudShipOfObjRw(Object);
                    double const TargetOffer = TargetList->CargoVolume;
                    double const PlayerOffer = CenterList->CargoVolume;
                    TargetCargoUsed = (TargetHoldSize - Ship->get_cargo_hold_remaining()) -
                                      TargetOffer + PlayerOffer;
                }
                double const PlayerOffer = CenterList->CargoVolume;
                double const TargetOffer = TargetList->CargoVolume;
                PlayerCargoUsed = GetPlayerEquipDescListRetail().get_cargo_space_occupied() -
                                  PlayerOffer + TargetOffer;
            }
        }

        // 0x004c3443..0x004c3488: the panel closes.
        CTradeEquipList* const Owner = SelectedLine->Owner;
        if (Unmark != nullptr)
            SetTradeMark(Unmark, 0);
        SelectedLine = nullptr;
        SelectedItem = nullptr;
        Owner->Refresh(-1);
        SetMode(2);
        return 0;
    }

    if (Control == QuantityReject)
    {
        // 0x004c345e..0x004c3488.
        CTradeEquipList* const Owner = SelectedLine->Owner;
        SetTradeMark(SelectedItem, 0);
        SelectedLine = nullptr;
        SelectedItem = nullptr;
        Owner->Refresh(-1);
        SetMode(2);
        return 0;
    }

    if (Control == AcceptTradeFrame)
    {
        // 0x004c34a0..0x004c34ef: the player accepts -- green text, IServer +0x144.
        PlayerAcceptedText->SetProperty(0x1e, 0x759, 0);
        PlayerAcceptedText->TextColour = TradeColour(0, 0xff, 0, 0xc0);
        G_Server->AcceptTrade(GetPlayerConnectionId(), true);
        return 0;
    }

    // 0x004c3500..0x004c35b0: a row's button.
    CTradeEquipListLine* const Line = reinterpret_cast<CTradeEquipListLine*>(Control->Field2f0);
    if (Line == nullptr || Line->bInUse == 0)
        return 0;
    CTradeEquipList* const List = Line->Owner;
    if (Control != Line->InfoCard)
    {
        if (List == nullptr)
            return 0;
        if (List->bSelectMode == 0)
        {
            if (List == TargetList)
                return 0;

            // 0x004c3577..0x004c35ab: open the quantity panel on the row's item.
            if (Line->Item == SelectedItem)
                return 0;
            if (SelectedItem != nullptr)
                SetTradeMark(SelectedItem, 0);
            SelectedLine = Line;
            SelectedItem = Line->Item;
            SetTradeMark(SelectedItem, 1);
            List->Refresh(-1);
            SetMode(3);
            return 0;
        }
    }

    // 0x004c3540..0x004c3567: the "?" button, or any button of a list in select mode -- the row's
    // item goes onto the list's info card.
    if (List->SelectedItem != nullptr)
    {
        SetTradeMark(List->SelectedItem, 0);
        List->SelectedItem = nullptr;
    }
    EquipDesc* const Item = Line->Item;
    if (Item != nullptr)
    {
        List->SelectedItem = Item;
        SetTradeMark(Item, 1);
    }
    ShowInfoCard(List, 1);
    List->Refresh(-1);
    return 0;
}
