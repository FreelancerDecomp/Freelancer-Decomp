// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "nn_mission_vendor.h"

#include "character_select_equip.h" // GetPlayerShipArchetypeIdRetail (FUN_004c3e10)
#include "globals.h"                // GetClientFrameTimeCapped (FUN_0042d680)
#include "hud_command_dispatch.h"   // HudPushCancelHandler (FUN_00576360)
#include "ids_rdl.h"                // AppendTextMaybeXmlRdl (FUN_0057e2c0), RdlAppendNode
#include "knowledge_map.h"          // KnowledgeMapRevealFmtStr (FUN_00459950)
#include "login_mgr.h"              // G_Server (DAT_0067ecd0)
#include "menu_universe_base.h"     // GetClientLocationBaseMissionOffers (FUN_0043a8d0),
                                    // GetClientCurrentLocationRuntime (FUN_0043a460),
                                    // GetClientLocationCurrentBase (DAT_0066873c)
#include "mirror_property_bucket.h"
#include "nav_bar.h"                // NavBarCloseService (FUN_004424f0)
#include "misc_util.h"              // FmtStrSprintf (FUN_0041c970),
                                    // MiscUtilLoadMaterialLibrary (FUN_0041c850)
#include "player_spawn.h"           // GetPlayerConnectionId (DAT_00673344)
#include "rich_text_win.h"
#include "room_control.h"           // GetRoomRuntimeBaseId (+0x34)
#include "ui_scroll.h"
#include "win3d.h"                  // Win3DFindByName, Win3DFindByClassName, Win3DDeleteTree
#include "win_factory.h"
#include "wire_controls.h"          // WireControlsJoinColumn (FUN_005a4b50)

#include "DALib.h"
#include "common_goods.h"           // MissionVendorProperties, MissionVendorAcceptance
#include "common_server.h"          // IServer::MissionResponse
#include "common_text.h"            // FmtStr, RenderDisplayList, StyleNode
#include "dacom.h"                  // FDUMP
#include "render/shading.h"         // IMaterialLibrary::DeleteGroup

#include <new>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

// Retail DAT_00671ffc -- WINFACTORY<NN_MissionVendor> static (LAB_0048b2b0 -> FUN_0048d5b0;
// Create FUN_0048d620; atexit LAB_0048b2d0 -> FUN_00579380).
WINFACTORY<NnMissionVendor> G_NnMissionVendorFactory("NN_MissionVendor");

int G_MissionVendorNoMissionsReason = 0; // DAT_00672038 (.data, zero)

namespace
{
    char const kMissionVendorCpp[] = "E:\\FL\\Scratch\\Source\\Client\\nn\\NN_MissionVendor.cpp";
    char const kNoMaterialInVendor[] = "%s(%d) : *** ERROR: Cannot find material %s in vendor\n";
    char const kVideoMaterialName[] = "MV_vidmaterial";
    char const kVideoTextureLibrary[] = "Interface\\NeuroNet\\MissionVendor\\MissionVendor.txm";
    char const kVideoScreenMesh[] = "Interface\\NeuroNet\\MissionVendor\\MV_videoscreen.3db";
    char const kHeadlineIconMesh[] = "Interface\\NeuroNet\\NewsVendor\\NEWS_headlineicon.3db";

    // DAT_00611bf0 -- the tooltip ids of the seven icons (MissionVendorProperties +0x10 indexes
    // it; read only at 0x0048c9cb and 0x0048d2f7).
    int const G_MissionVendorIconIds[7] = {0, 0x32fa, 0x32fb, 0x32fc, 0x32fd, 0x32fe, 0x32ff};

    // The seven paths Create stores into IconPaths (0x0048c36b..0x0048c3b8), in that order.
    char const* const kMissionIconMeshes[7] = {
        kHeadlineIconMesh,
        "Interface\\NeuroNet\\MissionVendor\\MV_missiondestroy.3db",
        "Interface\\NeuroNet\\MissionVendor\\MV_missiondestroyinstall.3db",
        "Interface\\NeuroNet\\MissionVendor\\MV_missionassassinate.3db",
        "Interface\\NeuroNet\\MissionVendor\\MV_missiondestroycontra.3db",
        "Interface\\NeuroNet\\MissionVendor\\MV_missionbounty.3db",
        "Interface\\NeuroNet\\MissionVendor\\MV_missionretrieve.3db",
    };

    // LAB_0048b280 / LAB_0048b290 / LAB_0048b2a0 -- three float statics of this translation unit
    // (DAT_00672030 = DAT_0067202c = -0.6, DAT_00672028 = 0.6). Each has one XREF, its
    // initializer's write; nothing reads them.
    float G_MissionVendorStatic672030 = -0.6f;
    float G_MissionVendorStatic67202c = -0.6f;
    float G_MissionVendorStatic672028 = 0.6f;

    uintptr_t PointerArg(void const* Pointer)
    {
        return reinterpret_cast<uintptr_t>(Pointer);
    }

    // The reward line of RefreshRows (0x0048ca54..0x0048cae2) and ShowSelected
    // (0x0048cf03..0x0048cf95): mad-lib 0x732 with the credits, or 0x741 without any.
    void FormatReward(MissionVendorProperties const& Offer, wchar_t* Buffer)
    {
        FmtStr Reward;
        if (Offer.IconTextureId != 0)
        {
            Reward.begin_mad_lib(0x732);
            Reward.append_int(Offer.IconTextureId);
        }
        else
        {
            Reward.begin_mad_lib(0x741);
        }
        Reward.end_mad_lib();
        FmtStrSprintf(&Reward, Buffer, 0x400);
    }

    // The full-text idiom of ShowSelected (0x0048cd79..0x0048ce6b / 0x0048cfd4..0x0048d0c6): a
    // style node 0x9ca4, the text (XML when it parses), then the pane cleared, refilled and
    // scrolled to the top.
    void SetFullText(RichTextWin* Pane, wchar_t const* Buffer)
    {
        RenderDisplayList Text;
        {
            StyleNode Style(0x9ca4);
            RdlAppendNode(Text, Style.Clone());
        }
        AppendTextMaybeXmlRdl(Buffer, static_cast<unsigned>(wcslen(Buffer)), Text, false);
        Pane->ClearContent();          // FUN_0057b370
        Pane->AppendContent(Text);     // FUN_0057b3e0
        Pane->SetScrollOrigin(0, 0);   // FUN_005798b0
    }
}

NnMissionVendor::NnMissionVendor()
    : ResultCallback(nullptr)
    , ResultContext(nullptr)
    , bCloseRequested(0)
    , ScrollOffset(0)
    , Field_0x374(0)
    , Selected(0)
    , Offers(nullptr)
    , OfferCount(0)
    , IconPaths(nullptr)
    , IconPathCount(0)
{
    // FUN_0048b2e0. The members above are 0x0048b390..0x0048b3ae / 0x0048b3c3..0x0048b3d5.
    // 0x0048b3b4..0x0048b3c1: FmtStr::destroy, then its six dwords zeroed.
    StatusText.destroy();
    memset(static_cast<void*>(&StatusText), 0, sizeof(StatusText));
    // 0x0048b3db..0x0048b4ad: the string emptied (erase(0, npos) inlined), FUN_0042cc30.
    VideoTexture.clear();
    VideoMaterial.Reset();
    // 0x0048b4bd..0x0048b4ca. MaterialGroup (+0x348) is not written.
    Clicked = 0;
    bNoMissions = 0;
    Hovered = -1;
}

NnMissionVendor::~NnMissionVendor()
{
    // FUN_0048b510. 0x0048b54a..0x0048b593: the status text emptied, the offer array
    // (vector delete, MissionVendorProperties' dtor) and the path array freed.
    StatusText.destroy();
    delete[] Offers;
    ::operator delete(static_cast<void*>(IconPaths));
    // 0x0048b593..0x0048b63f
    ResetVideo();
    // 0x0048b644..0x0048b65f: MaterialLib slot 10.
    DALib::MaterialLib->DeleteGroup(MaterialGroup);
    MaterialGroup = -1;
    // Then ~FmtStr, FUN_0042cc50 (~FSwappedMaterial), the string, FUN_00576010 and FUN_0059fa50
    // -- the members' and bases' destructors in that order.
}

uintptr_t NnMissionVendor::RowDword(size_t ArrayOffset, int Index) const
{
    uintptr_t Value = 0;
    unsigned char const* const Bytes = reinterpret_cast<unsigned char const*>(this);
    memcpy(&Value, Bytes + ArrayOffset + static_cast<ptrdiff_t>(Index) * 4, sizeof(Value));
    return Value;
}

void NnMissionVendor::DeleteSelf()
{
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        Win3DDeleteTree(Child); // FUN_0043d2a0
        Child = Next;
    }
    Destroy();   // slot 10
    delete this; // slot 0 with 1
}

void NnMissionVendor::ResetVideo()
{
    VideoTexture.clear();
    VideoMaterial.Release(); // FUN_0042cc50
    VideoTexture.clear();    // ?erase@ (0, npos)
    VideoMaterial.Reset();   // FUN_0042cc30
}

void NnMissionVendor::RevealSelectedOffer()
{
    // e.g. 0x0048c3e2..0x0048c40b: title, description, briefing.
    MissionVendorProperties const& Offer = Offers[Selected];
    // FUN_00459950 on each (the KnowledgeMap reveal).
    KnowledgeMapRevealFmtStr(&Offer.Title);
    KnowledgeMapRevealFmtStr(&Offer.Description);
    KnowledgeMapRevealFmtStr(&Offer.Briefing);
}

bool NnMissionVendor::Create(Win3D* NewParent, void* Data)
{
    // LAB_0048b6e0 (RET 8). 0x0048b6eb..0x0048b781: extents, hit offset and the base position.
    HalfExtents[0] = 0.43f; // +0xb4
    HalfExtents[1] = 0.3f;
    HalfExtents[2] = 0.0f;
    HitOffset[0] = 0.0f;    // +0xc0
    HitOffset[1] = 0.033f;
    HitOffset[2] = -1.0f;
    BasePosition[0] = 0.0f;
    BasePosition[1] = -0.1f;
    BasePosition[2] = -1.498f;
    float const X = BasePosition[0];
    float const Y = BasePosition[1];
    float const Z = BasePosition[2];
    float const Zero[3] = {0.0f, 0.0f, 0.0f};

    // 0x0048b784..0x0048b7f8
    {
        float const Position[3] = {X, Y, Z};
        Background = CreateStaticControl("MissionVendorBackground",
                                         "Interface\\NeuroNet\\NewsVendor\\NEWS_background.cmp",
                                         Position, nullptr, Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);
    }
    // 0x0048b7fe..0x0048b868
    {
        float const Position[3] = {X + 0.584f, Y + 0.455f, Z};
        float const Half[3] = {0.025f, 0.025f, 0.0f};
        CloseButton = CreateButtonControl("MissionVendorCloseButton",
                                          "Interface\\NeuroNet\\Inventory\\x.3db", nullptr,
                                          "ui_select_item", nullptr, Position, nullptr, Half, 0, 0,
                                          0, 0, 0x72e, 0, 0);
    }
    // 0x0048b86e..0x0048b8cb
    {
        float const Position[3] = {X - 0.6f, Y + 0.486f, Z};
        Title = CreateTextControl("MissionVendorTitle", "PageHeader", Position, nullptr, 1.2f, 0.03f,
                                  2.0f, 0x72b, 1, 0, 1);
    }

    // 0x0048b8d1..0x0048b91c: the offers of the base of the current room (FUN_0043a460's +0x34),
    // counted with FUN_0043ce90.
    TMirrorPropertyBucket<MissionVendorProperties>* const Bucket = GetClientLocationBaseMissionOffers(
        GetRoomRuntimeBaseId(static_cast<FRoomRuntime*>(GetClientCurrentLocationRuntime())));
    int Count = 0;
    MetaNode<MissionVendorProperties>* Node = nullptr;
    while (Bucket->List.traverse(Node, true))
        ++Count;

    if (Count == 0)
    {
        // 0x0048b922..0x0048b9cb: why there is nothing to offer.
        bNoMissions = 1;
        unsigned Ids = 0;
        if (GetPlayerShipArchetypeIdRetail() == 0)
        {
            Ids = 0x3b0;
        }
        else
        {
            // Jump table 0x0048c444; above 4 keeps 0.
            switch (G_MissionVendorNoMissionsReason)
            {
            case 0: Ids = 0x3d0; break;
            case 1: Ids = 0x384; break;
            case 2: Ids = 0x385; break;
            case 3: Ids = 0x386; break;
            case 4: Ids = 0x3d0; break;
            default: break;
            }
        }
        float const Position[3] = {-0.54f, -0.01f, BasePosition[2]};
        CreateTextControl("MissionVendorNoMissions", "SubHeader", Position, nullptr, 1.055f, 0.03f,
                          2.0f, Ids, 1, 0, 1);
        HudPushCancelHandler(AsHudCmdHandler(), 0); // FUN_00576360
        return Win3D::Create(NewParent, Data);      // FUN_004b5060
    }

    // 0x0048b9d0..0x0048bb75: the three frames.
    {
        float const Position[3] = {X - 0.215f, Y + 0.217f, Z};
        CreateStaticControl("MissionVendorHeadlinesFrame",
                            "Interface\\NeuroNet\\NewsVendor\\NEWS_headlineframe.3db", Position,
                            nullptr, Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);
    }
    {
        float const Position[3] = {X - 0.006f, Y - 0.149f, Z};
        CreateStaticControl("MissionVendorStoryFrame",
                            "Interface\\NeuroNet\\NewsVendor\\NEWS_textframe.3db", Position, nullptr,
                            Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);
    }
    {
        float const Position[3] = {X + 0.364f, Y + 0.288f, Z};
        CreateStaticControl("MissionVendorVideoFrame",
                            "Interface\\NeuroNet\\NewsVendor\\NEWS_videowindow.3db", Position,
                            nullptr, Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);
    }
    // 0x0048bb7a..0x0048bc16: the video screen's material group, then the screen itself.
    MaterialGroup = MiscUtilLoadMaterialLibrary(kVideoScreenMesh);
    {
        float const Position[3] = {X + 0.364f, Y + 0.288f, Z};
        CreateStaticControl("MissionVendorVideoScreen", kVideoScreenMesh, Position, nullptr, Zero, 0,
                            0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);
    }
    // 0x0048bc1b..0x0048bcd8: the accept caption and button.
    {
        float const Position[3] = {X + 0.237f, Y + 0.105f, Z};
        CreateTextControl("MissionVendorAcceptText", "ButtonText", Position, nullptr, 0.277f, 0.03f,
                          2.0f, 0x3e9, 1, 0, 1);
    }
    {
        float const Position[3] = {X + 0.37f, Y + 0.078f, Z};
        float const Half[3] = {0.133f, 0.026f, 0.0f};
        AcceptButton = CreateWireButtonControl("MissionVendorAcceptButton", "ui_execute_transaction",
                                               Position, Half, 0, 0, 0x331, 0, 0);
    }

    // 0x0048bce3..0x0048c07b: five rows, 0.0615 apart from {x - 0.39, y + 0.36}.
    float const Top[3] = {X - 0.39f, Y + 0.36f, Z};
    char Name[0x100];
    for (int Row = 0; Row < 5; ++Row)
    {
        // 0x0048bd21..0x0048bd45: FILD Row, FMUL 0.0615f, subtracted at extended precision and
        // stored once as float.
        float const RowPos[3] = {
            Top[0],
            static_cast<float>(static_cast<double>(Top[1]) -
                               static_cast<double>(Row) * static_cast<double>(0.0615f)),
            Top[2]};

        sprintf(Name, "MissionVendorSummaryIcon%d", Row);
        {
            float const Position[3] = {RowPos[0] - 0.125f, RowPos[1], RowPos[2]};
            SummaryIcons[Row] = CreateStaticControl(Name, kHeadlineIconMesh, Position, nullptr, Zero, 0,
                                                    0, 0, 1, 0, 0, 1, 0, 0, 0, Zero);
        }
        sprintf(Name, "MissionVendorIconFrame%d", Row);
        {
            float const Position[3] = {RowPos[0] - 0.125f, RowPos[1], RowPos[2]};
            float const Half[3] = {0.03f, 0.03f, 0.0f};
            IconFrames[Row] = CreateWireButtonControl(Name, "ui_select_item", Position, Half, 0, 0,
                                                      0x72c, 0, 0);
        }
        sprintf(Name, "MissionVendorSummaryBox%d", Row);
        {
            float const Position[3] = {RowPos[0] + 0.185f, RowPos[1], RowPos[2]};
            float const Half[3] = {0.245f, 0.031f, 0.0f};
            SummaryBoxes[Row] = CreateWireButtonControl(Name, nullptr, Position, Half, 0, 0, 0x72c, 0,
                                                        0);
        }
        sprintf(Name, "MissionVendorSummaryDest%d", Row);
        {
            float const Position[3] = {RowPos[0] + 0.195f, RowPos[1] + 0.017f, RowPos[2]};
            SummaryDests[Row] = CreateTextControl(Name, "NewsMission", Position, nullptr, 0.49f, 0.03f,
                                                  1.5f, 0, 0, 0, 0);
        }
        sprintf(Name, "MissionVendorSummaryTitle%d", Row);
        {
            float const Position[3] = {RowPos[0] + 0.195f, RowPos[1] - 0.009f, RowPos[2]};
            SummaryTitles[Row] = CreateTextControl(Name, "NewsMission", Position, nullptr, 0.49f,
                                                   0.03f, 1.5f, 0, 0, 0, 0);
        }
        sprintf(Name, "MissionVendorSummaryReward%d", Row);
        {
            float const Position[3] = {RowPos[0] + 0.293f, RowPos[1] + 0.004f, RowPos[2]};
            SummaryRewards[Row] = CreateTextControl(Name, "NewsMission", Position, nullptr, 0.27f,
                                                    0.03f, 1.5f, 0, 2, 0, 0);
        }
    }

    // 0x0048c081..0x0048c0d1: the scroll bar (0..5, page 5).
    {
        float const Position[3] = {X + 0.09f, Y + 0.397f, Z};
        SummaryScroll = CreateUIScroll("MissionVendorSummaryScroll", Position, 0.019f, 0.317f, 0, 5,
                                       5, 0);
    }
    // 0x0048c0c4..0x0048c26f: the selected offer's lines (each name goes through sprintf into
    // the name buffer first; none has a conversion).
    {
        float const Position[3] = {X - 0.493f, Y + 0.0085f, Z};
        MissionReject = CreateTextControl("MissionVendorMissionReject", "Normal", Position, nullptr,
                                          0.7f, 0.03f, 1.8f, 0x72f, 0, 0, 1);
    }
    {
        float const Position[3] = {X - 0.494f, Y + 0.022f, Z};
        MissionDest = CreateTextControl("MissionVendorMissionDest", "NewsMission", Position, nullptr,
                                        0.7f, 0.03f, 1.5f, 0, 0, 0, 1);
    }
    {
        float const Position[3] = {X - 0.494f, Y - 0.006f, Z};
        MissionTitle = CreateTextControl("MissionVendorMissionTitle", "NewsMission", Position,
                                         nullptr, 0.7f, 0.03f, 1.5f, 0, 0, 0, 1);
    }
    {
        float const Position[3] = {X + 0.348f, Y + 0.009f, Z};
        MissionReward = CreateTextControl("MissionVendorMissionReward", "NewsMission", Position,
                                          nullptr, 0.2f, 0.03f, 1.5f, 0, 2, 0, 1);
    }
    // 0x0048c275..0x0048c2ad
    {
        float const Position[3] = {X - 0.556f, Y - 0.051f, Z};
        FullText = CreateRichTextWin("MissionVendorMissionFullText", 1.08f, 0.27f, Position, true);
    }
    // 0x0048c2b2..0x0048c342: the selected offer's icon (fixed x / y, the base's z).
    {
        float const Position[3] = {-0.535f, -0.111f, BasePosition[2]};
        float const Half[3] = {0.03f, 0.03f, 0.0f};
        MissionIcon = CreateStaticControl("MissionVendorIcon", kHeadlineIconMesh, Position, nullptr,
                                          Half, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, Zero);
    }

    // 0x0048c347..0x0048c3b8
    DrawLayer = 0x64;
    IconPathCount = 7;
    IconPaths = static_cast<char const**>(::operator new(0x1c));
    for (int Index = 0; Index < 7; ++Index)
        IconPaths[Index] = kMissionIconMeshes[Index];

    // 0x0048c3c2..0x0048c40b
    LoadOffers();
    RefreshRows();
    ShowSelected();
    if (Selected != -1)
        RevealSelectedOffer();

    // 0x0048c40e..0x0048c437
    HudPushCancelHandler(AsHudCmdHandler(), 0); // FUN_00576360
    return Win3D::Create(NewParent, Data);      // FUN_004b5060
}

void NnMissionVendor::LoadOffers()
{
    // FUN_0048c770. 0x0048c788..0x0048c7c0: the offers of the base the player is in
    // (DAT_0066873c), counted by walking Head / Next.
    TMirrorPropertyBucket<MissionVendorProperties>* const Bucket =
        GetClientLocationBaseMissionOffers(GetClientLocationCurrentBase());
    int Count = 0;
    MetaNode<MissionVendorProperties>* Node = nullptr;
    while (Bucket->List.traverse(Node, true))
        ++Count;

    // 0x0048c7c2..0x0048c812: new[] (count prefix, MissionVendorProperties' ctor).
    Offers = new MissionVendorProperties[Count];
    OfferCount = Count;

    // 0x0048c820..0x0048c858: element i is assigned from the list's i-th node (retail walks
    // from the head for each i; one walk visits the same nodes in the same order).
    Node = nullptr;
    for (int Index = 0; Index < Count; ++Index)
    {
        Bucket->List.traverse(Node, true);
        Offers[Index] = *Node->Data; // ??4MissionVendorProperties@@
    }

    // 0x0048c85c..0x0048c881: the rows from the top, offer 0 selected, the scroll range.
    ScrollOffset = 0;
    Field_0x374 = 0;
    Selected = 0;
    SummaryScroll->SetProperty(0x37, 0, static_cast<uintptr_t>(OfferCount - 1));
}

void NnMissionVendor::RefreshRows()
{
    // FUN_0048c8a0.
    wchar_t Buffer[0x400];
    int Row = 0;
    for (int Index = ScrollOffset; Index < OfferCount; ++Index)
    {
        MissionVendorProperties const& Offer = Offers[Index];
        WinControl* const Controls[6] = {SummaryIcons[Row], IconFrames[Row], SummaryBoxes[Row],
                                         SummaryDests[Row], SummaryTitles[Row], SummaryRewards[Row]};

        // 0x0048c8f7..0x0048c9ab: the row's six controls, property 2 then property 1, with 0.
        for (int Slot = 0; Slot < 6; ++Slot)
            Controls[Slot]->SetProperty(2, 0, 0);
        for (int Slot = 0; Slot < 6; ++Slot)
            Controls[Slot]->SetProperty(1, 0, 0);

        // 0x0048c9b1..0x0048ca05: the icon mesh and its tooltip; an unknown class keeps path 0
        // and ids 0 (unsigned compare, JAE).
        char const* Path = IconPaths[0];
        int Ids = 0;
        if (Offer.MissionClass < IconPathCount)
        {
            Path = IconPaths[Offer.MissionClass];
            Ids = G_MissionVendorIconIds[Offer.MissionClass];
        }
        SummaryIcons[Row]->SetProperty(4, PointerArg(Path), 0);
        IconFrames[Row]->SetProperty(3, static_cast<uintptr_t>(Ids), 0);
        SummaryBoxes[Row]->SetProperty(3, static_cast<uintptr_t>(Ids), 0);

        // 0x0048ca0b..0x0048ca4e: the offer's +0x14 and +0x2c texts.
        FmtStrSprintf(&Offer.Title, Buffer, 0x400);
        SummaryDests[Row]->SetProperty(0x1d, PointerArg(Buffer), 0);
        FmtStrSprintf(&Offer.Description, Buffer, 0x400);
        SummaryTitles[Row]->SetProperty(0x1d, PointerArg(Buffer), 0);

        // 0x0048ca54..0x0048caf4
        FormatReward(Offer, Buffer);
        SummaryRewards[Row]->SetProperty(0x1d, PointerArg(Buffer), 0);

        // 0x0048cafa..0x0048cb2f: the selected offer's frame and box lit (0xc with 1).
        uintptr_t const bSelected = (Index == Selected) ? 1u : 0u;
        IconFrames[Row]->SetProperty(0xc, bSelected, 0);
        SummaryBoxes[Row]->SetProperty(0xc, bSelected, 0);

        // 0x0048cb35..0x0048cb53
        ++Row;
        if (Row >= 5)
            break;
    }

    // 0x0048cb81..0x0048ccb4: every unused row, property 2 then property 1 with 1, the default
    // icon mesh, the four text-bearing controls cleared (0x1e with 0).
    for (; Row < 5; ++Row)
    {
        WinControl* const Controls[6] = {SummaryIcons[Row], IconFrames[Row], SummaryBoxes[Row],
                                         SummaryDests[Row], SummaryTitles[Row], SummaryRewards[Row]};
        for (int Slot = 0; Slot < 6; ++Slot)
            Controls[Slot]->SetProperty(2, 1, 0);
        for (int Slot = 0; Slot < 6; ++Slot)
            Controls[Slot]->SetProperty(1, 1, 0);
        SummaryIcons[Row]->SetProperty(4, PointerArg(IconPaths[0]), 0);
        SummaryBoxes[Row]->SetProperty(0x1e, 0, 0);
        SummaryDests[Row]->SetProperty(0x1e, 0, 0);
        SummaryTitles[Row]->SetProperty(0x1e, 0, 0);
        SummaryRewards[Row]->SetProperty(0x1e, 0, 0);
    }
}

void NnMissionVendor::ShowSelected()
{
    // FUN_0048cce0.
    wchar_t Buffer[0x400];
    if (Selected == -1)
    {
        // 0x0048cd13..0x0048ce9e: the reject line's ids, the three lines cleared, the status
        // text in the full-text pane. The video is left as it is.
        MissionReject->SetProperty(0x1e, 0x72f, 0);
        MissionDest->SetProperty(0x1e, 0, 0);
        MissionTitle->SetProperty(0x1e, 0, 0);
        MissionReward->SetProperty(0x1e, 0, 0);
        FmtStrSprintf(&StatusText, Buffer, 0x400);
        SetFullText(FullText, Buffer);
        return;
    }

    // 0x0048cea3..0x0048d0cd. Selected is not range-checked (a click below the last offer
    // selects past the array, as in retail).
    MissionVendorProperties const& Offer = Offers[Selected];
    FmtStrSprintf(&Offer.Title, Buffer, 0x400);
    MissionDest->SetProperty(0x1d, PointerArg(Buffer), 0);
    FmtStrSprintf(&Offer.Description, Buffer, 0x400);
    MissionTitle->SetProperty(0x1d, PointerArg(Buffer), 0);
    FormatReward(Offer, Buffer);
    MissionReward->SetProperty(0x1d, PointerArg(Buffer), 0);
    MissionReject->SetProperty(0x1e, 0, 0);
    FmtStrSprintf(&Offer.Briefing, Buffer, 0x400);
    SetFullText(FullText, Buffer);
    UpdateVideo(); // FUN_0048d140
}

void NnMissionVendor::UpdateVideo()
{
    // FUN_0048d140. 0x0048d343: without a selection the icon is hidden.
    if (Selected < 0)
    {
        MissionIcon->SetProperty(1, 1, 0);
        return;
    }

    // 0x0048d155..0x0048d285: the offer's texture name (Common's get_icon_texture), the previous
    // swap undone, the name kept in VideoTexture. Retail runs strlen on the result unchecked.
    MissionVendorProperties const& Offer = Offers[Selected];
    char const* const Texture = Offer.get_icon_texture();
    ResetVideo();
    VideoTexture.assign(Texture, strlen(Texture));

    // 0x0048d285..0x0048d2d8
    if (!VideoMaterial.Init(kVideoMaterialName))
        FDUMP(0x100001, kNoMaterialInVendor, kMissionVendorCpp, 0x3af, kVideoMaterialName);
    VideoMaterial.Swap(VideoTexture.c_str(), kVideoTextureLibrary);

    // 0x0048d2dd..0x0048d335: the icon mesh, its tooltip, shown.
    char const* Path = IconPaths[0];
    int Ids = 0;
    if (Offer.MissionClass < IconPathCount)
    {
        Path = IconPaths[Offer.MissionClass];
        Ids = G_MissionVendorIconIds[Offer.MissionClass];
    }
    MissionIcon->SetProperty(4, PointerArg(Path), 0);
    MissionIcon->SetProperty(3, static_cast<uintptr_t>(Ids), 0);
    MissionIcon->SetProperty(1, 0, 0);
}

void NnMissionVendor::Draw()
{
    // FUN_0048d360.
    if (bNoMissions == 0)
    {
        // 0x0048d371..0x0048d3f1: every child at 0x64; the hovered row's frame or box 0x65, the
        // selected row's 0x66 (gated on Clicked, indexed by Selected; the second test wins). The
        // rows are Hovered / Selected minus the scroll offset and are not range-checked.
        int const SelectedRow = Selected - ScrollOffset;
        int const HoveredRow = Hovered - ScrollOffset;
        size_t const FramesOffset = offsetof(NnMissionVendor, IconFrames);
        size_t const BoxesOffset = offsetof(NnMissionVendor, SummaryBoxes);
        for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
        {
            Child->DrawLayer = 0x64;
            if (Hovered >= 0)
            {
                uintptr_t const Control = PointerArg(Child->AsWinControl()); // slot 2
                if (Control == RowDword(FramesOffset, HoveredRow) ||
                    Control == RowDword(BoxesOffset, HoveredRow))
                    Child->DrawLayer = 0x65;
            }
            if (Clicked >= 0)
            {
                uintptr_t const Control = PointerArg(Child->AsWinControl());
                if (Control == RowDword(FramesOffset, SelectedRow) ||
                    Control == RowDword(BoxesOffset, SelectedRow))
                    Child->DrawLayer = 0x66;
            }
        }

        // 0x0048d3f3..0x0048d427
        WireControlsJoinColumn(IconFrames, 5);   // FUN_005a4b50
        WireControlsJoinColumn(SummaryBoxes, 5);
        Background->DrawLayer = 0x63;
        FullText->DrawLayer = 0x67;
    }
    WinControl::Draw(); // FUN_005a0760 (tail jump)
}

void NnMissionVendor::Update()
{
    // LAB_0048d440: the frame time is fetched and dropped (FSTP ST0), Win3D's Update, then a
    // close OnAcceptResult asked for.
    (void)GetClientFrameTimeCapped(); // FUN_0042d680
    Win3D::Update();                  // FUN_005a17b0
    if (bCloseRequested != 0)
    {
        bCloseRequested = 0;
        NavBarCloseService(false); // FUN_004424f0(0)
        DeleteSelf();
    }
}

intptr_t NnMissionVendor::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_0048c460 (RET 0xc): byte table 0x0048c6f0 over Msg - 0x26 (0..0x28) into the five
    // cases at 0x0048c6dc. Every path returns 0.
    switch (Msg)
    {
    case 0x4e:
    {
        // 0x0048c484..0x0048c4ba: closed from outside -- no callback, no NavBar. (Retail tests
        // `this` first.)
        DeleteSelf();
        return 0;
    }
    case 0x3e:
    {
        // 0x0048c4bd..0x0048c4f0: the scroll bar moved.
        if (A == PointerArg(SummaryScroll) && static_cast<int>(B) != ScrollOffset)
        {
            ScrollOffset = static_cast<int>(B);
            RefreshRows();
        }
        return 0;
    }
    case 0x2c:
    case 0x2d:
    {
        // 0x0048c4f3..0x0048c580: the hovered row, written (0x2c) or cleared (0x2d) on every
        // pass until the control is found; 0x2d compares against Row - ScrollOffset as retail
        // does (0x0048c515..0x0048c51f). Then the row's other wire button gets the same message.
        int Row = 0;
        for (; Row < 5; ++Row)
        {
            if (Msg == 0x2c)
                Hovered = ScrollOffset + Row;
            else if (Hovered == Row - ScrollOffset)
                Hovered = -1;
            if (A == PointerArg(IconFrames[Row]) || A == PointerArg(SummaryBoxes[Row]))
                break;
        }
        if (Row == 5)
            return 0;
        if (A != PointerArg(IconFrames[Row]))
            IconFrames[Row]->SetProperty(Msg, 0, 0);
        if (A != PointerArg(SummaryBoxes[Row]))
            SummaryBoxes[Row]->SetProperty(Msg, 0, 0);
        return 0;
    }
    case 0x26:
    {
        if (A == PointerArg(CloseButton))
        {
            // 0x0048c58f..0x0048c5df
            if (ResultCallback != nullptr)
                ResultCallback(ResultContext, 0);
            NavBarCloseService(false);
            DeleteSelf();
            return 0;
        }
        if (A == PointerArg(AcceptButton))
        {
            // 0x0048c5ea..0x0048c64a: accept the selected offer -- the callback with 1, then
            // IServer slot 43 (+0xac) with the offer's +0x08 and +0x0c. The window stays open
            // until OnAcceptResult.
            if (Selected < 0)
                return 0;
            if (ResultCallback != nullptr)
                ResultCallback(ResultContext, 1);
            MissionVendorProperties const& Offer = Offers[Selected];
            G_Server->MissionResponse(Offer.TargetId, Offer.SystemId, true, GetPlayerConnectionId());
            return 0;
        }

        // 0x0048c64d..0x0048c6ce: a row clicked. No match leaves Clicked at -1 and selects
        // ScrollOffset - 1, as retail does.
        Clicked = -1;
        for (int Row = 0; Row < 5; ++Row)
        {
            if (A == PointerArg(IconFrames[Row]) || A == PointerArg(SummaryBoxes[Row]))
            {
                Clicked = Row;
                break;
            }
        }
        Selected = ScrollOffset + Clicked;
        RefreshRows();
        ShowSelected();
        if (Selected != -1)
            RevealSelectedOffer();
        return 0;
    }
    default:
        return 0;
    }
}

bool NnMissionVendor::OnEvent(unsigned MsgId)
{
    // LAB_0048d540 (RET 4), on the +0x32c face: USER_CANCEL (0x10) closes the window like the
    // close button. (Retail tests the adjusted `this` for null.)
    if (MsgId != 0x10)
        return false;
    if (ResultCallback != nullptr)
        ResultCallback(ResultContext, 0);
    NavBarCloseService(false);
    DeleteSelf();
    return true;
}

void NnMissionVendor::CloseWindow()
{
    // FUN_0048c720. The NavBar goes first; retail then tests `this` (its caller already has).
    NavBarCloseService(false);
    DeleteSelf();
}

void NnMissionVendor::OnAcceptResult(MissionVendorAcceptance const& Result)
{
    // FUN_0048d4a0: the open NN_MissionVendor (FUN_0059d9c0 by class name).
    NnMissionVendor* const Window =
        static_cast<NnMissionVendor*>(Win3DFindByClassName("NN_MissionVendor"));
    if (Window == nullptr)
        return;

    // 0x0048d4ba..0x0048d4d0: accepted (byte +0x08 == 1) -- close at the next Update.
    if (Result.bAccepted)
    {
        Window->bCloseRequested = 1;
        return;
    }

    // 0x0048d4d1..0x0048d537: refused -- no selection, the reason (+0x0c) as the status text.
    Window->Selected = -1;
    Window->StatusText.destroy();
    Window->StatusText = Result.Reason;
    Window->RefreshRows();
    Window->ShowSelected();
    if (Window->Selected != -1)
        Window->RevealSelectedOffer();
}
