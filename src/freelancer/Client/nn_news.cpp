// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "nn_news.h"

#include "client_base_market.h"     // GetClientLocationBaseMarket (FUN_0043a510),
                                    // ClientMarketCollectCategory (FUN_00437f70),
                                    // ClientMarketGetPrice (FUN_00438060)
#include "font_string.h"            // FontStringChildCount (FUN_004145a0)
#include "widescreen.h"   // port-only (FREELANCER_WIDESCREEN_SUPPORT)
#include "globals.h"                // GetClientFrameTimeCapped (FUN_0042d680),
                                    // G_RenderSurfaceWidth (DAT_00610850)
#include "hud_command_dispatch.h"   // HudPushCancelHandler (FUN_00576360)
#include "ids_rdl.h"                // AppendTextMaybeXmlRdl (FUN_0057e2c0), RdlAppendNode
#include "ids_strings.h"            // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "knowledge_map.h"          // KnowledgeMapRevealFmtStr (FUN_00459950)
#include "font_imp.h"               // FindMenuFontImp (FUN_004177b0)
#include "menu_sound_arch.h"        // LookupClientSoundArch (FUN_0042ae40),
                                    // PlayClientSoundArch (FUN_00428680),
                                    // StopClientSoundArch (FUN_004285c0)
#include "menu_universe_base.h"     // GetClientLocationBaseNews (FUN_0043a630),
                                    // GetClientLocationCurrentBase (DAT_0066873c)
#include "mirror_property_bucket.h"
#include "nav_bar.h"                // NavBarCloseService (FUN_004424f0)
#include "misc_util.h"              // FmtStrSprintf (FUN_0041c970),
                                    // MiscUtilLoadMaterialLibrary (FUN_0041c850)
#include "nn_montage_draw.h"        // NnMontageFlushButtons (FUN_00559c20)
#include "nn_text_control.h"        // NnTextControl::GetFontString (FUN_0058a5d0)
#include "rich_text_win.h"
#include "ui_scroll.h"
#include "win3d.h"                  // Win3DFindByName, Win3DDeleteTree
#include "win3d_renderables.h"      // ButtonMontageEnsure (FUN_00559c10)
#include "win_factory.h"
#include "wire_controls.h"          // WireControlsJoinColumn (FUN_005a4b50)

#include "DALib.h"
#include "common_goods.h"           // NewsBroadcastProperties, GoodList::find_by_id
#include "common_id.h"              // CreateID, ID_String
#include "common_misc.h"            // malloc_strcpy, malloc_strfree
#include "common_text.h"            // FmtStr, RenderDisplayList, StyleNode
#include "dacom.h"                  // FDUMP
#include "font.h"                   // IFont::GetCharWidths (slot 10)
#include "render/shading.h"         // IMaterialLibrary::DeleteGroup

#include <list>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <windows.h>

// Retail DAT_006720cc -- WINFACTORY<NN_News> static (LAB_004a3fc0 -> FUN_004a5fb0; Create
// FUN_004a6020; atexit LAB_004a3fe0 -> FUN_00579380).
WINFACTORY<NnNews> G_NnNewsFactory("NN_News");

namespace
{
    char const kNewsVendorCpp[] = "E:\\FL\\Scratch\\Source\\Client\\nn\\NN_NewsVendor.cpp";
    char const kNoMaterialInNewsVendor[] = "%s(%d) : *** ERROR: Cannot find material %s in News Vendor";
    char const kNoStringForGood[] = "%s(%d) : *** WARNING: Failed to find string resource %d for good %s\n";
    char const kVideoMaterialName[] = "MV_vidmaterial";
    char const kVideoTextureLibrary[] = "Interface\\NeuroNet\\NewsVendor\\NewsVendor.txm";
    char const kVideoScreenMesh[] = "Interface\\NeuroNet\\MissionVendor\\MV_videoscreen.3db";
    char const kHeadlineIconMesh[] = "Interface\\NeuroNet\\NewsVendor\\NEWS_headlineicon.3db";

    // DAT_00611d0c -- a .data pointer to "NewsTicker" (0x005d3fb8): the ticker's text style
    // (0x004a4a01) and the font whose character widths BuildTicker takes (0x004a5c12). No writer.
    char const* G_NewsTickerStyle = "NewsTicker";

    // The seven paths Create stores into IconPaths (0x004a4bd8..0x004a4c2b), in that order.
    char const* const kNewsIconMeshes[7] = {
        kHeadlineIconMesh,
        "Interface\\NeuroNet\\NewsVendor\\NEWS_missioncriticalnews.3db",
        "Interface\\NeuroNet\\NewsVendor\\NEWS_worldnews.3db",
        "Interface\\NeuroNet\\NewsVendor\\NEWS_missionlead.3db",
        "Interface\\NeuroNet\\NewsVendor\\NEWS_system.3db",
        "Interface\\NeuroNet\\NewsVendor\\NEWS_faction.3db",
        "Interface\\NeuroNet\\NewsVendor\\NEWS_universe.3db",
    };
    // The seven tooltip ids Create stores into IconIds (0x004a4c2e..0x004a4c7b).
    int const kNewsIconIds[7] = {0x4ae, 0x4af, 0x4b0, 0x4b1, 0x67c, 0x67d, 0x67e};
    // The tooltip of an unknown category (0x004a535f / 0x004a57ee).
    int const kNewsDefaultIconIds = 0x42b;

    // LAB_004a3f90 / LAB_004a3fa0 / LAB_004a3fb0 -- three float statics of this translation unit
    // (DAT_00672100 = DAT_006720fc = -0.6, DAT_006720f8 = 0.6). Each has one XREF, its
    // initializer's write; nothing reads them.
    float G_NewsStatic672100 = -0.6f;
    float G_NewsStatic6720fc = -0.6f;
    float G_NewsStatic6720f8 = 0.6f;

    uintptr_t PointerArg(void const* Pointer)
    {
        return reinterpret_cast<uintptr_t>(Pointer);
    }

    uintptr_t FloatArg(float Value)
    {
        unsigned Bits = 0;
        memcpy(&Bits, &Value, sizeof(Bits));
        return static_cast<uintptr_t>(Bits);
    }

    // The icon of a news category (RefreshRows 0x004a534e..0x004a5369, ShowSelected
    // 0x004a57dd..0x004a57f8): IconPaths[0] and 0x42b unless Category is below IconPathCount
    // (unsigned compare, JAE).
    void NewsIconFor(NnNews const& Window, unsigned Category, char const** OutPath, int* OutIds)
    {
        *OutPath = Window.IconPaths[0];
        *OutIds = kNewsDefaultIconIds;
        if (Category < Window.IconPathCount)
        {
            *OutPath = Window.IconPaths[Category];
            *OutIds = Window.IconIds[Category];
        }
    }
}

NnNews::NnNews()
    : ResultCallback(nullptr)
    , ResultContext(nullptr)
    , TickerText(nullptr)
    , TickerWidths(nullptr)
    , TickerLength(0)
    , TickerScroll(0.0f)
    , TickerIndex(0)
    , VideoTexture(nullptr)
    , ScrollOffset(0)
    , Field_0x57c(0)
    , Selected(0)
    , News(nullptr)
    , NewsCount(0)
    , IconPaths(nullptr)
    , IconPathCount(0)
    , Hovered(-1)
    , Clicked(0)
    , bOpenSoundPlaying(0)
{
    // FUN_004a3ff0. FUN_0042cc10 (the FSwappedMaterial ctor) and ID_String::clear on +0x668 run
    // before the vptrs; the members above are 0x004a4060..0x004a40c4 and 0x004a40d3 (0x344..0x354
    // are dword stores of 0, the float +0x350 included). MaterialGroup (+0x55c) is not written.
    OpenSound.clear();
    // 0x004a40ca: FUN_0042cc30.
    VideoMaterial.Reset();
}

NnNews::~NnNews()
{
    // FUN_004a4110. 0x004a4149..0x004a416f: the open sound stopped if it was started.
    if (bOpenSoundPlaying != 0)
        StopClientSoundArch(LookupClientSoundArch(&OpenSound));
    // 0x004a4172..0x004a41d3: the news array (vector delete, NewsBroadcastProperties' dtor), the
    // path array, the ticker text (CRT free) and widths, then 0x344 / 0x348 / 0x34c zeroed.
    delete[] News;
    ::operator delete(static_cast<void*>(IconPaths));
    free(TickerText);
    ::operator delete(static_cast<void*>(TickerWidths));
    TickerText = nullptr;
    TickerWidths = nullptr;
    TickerLength = 0;
    // 0x004a41d9..0x004a41f4: the texture name freed, the swap released, the name nulled, the
    // swap's fields zeroed.
    malloc_strfree(&VideoTexture);
    VideoMaterial.Release(); // FUN_0042cc50
    VideoTexture = nullptr;
    VideoMaterial.Reset();   // FUN_0042cc30
    // 0x004a41f9..0x004a4213: MaterialLib slot 10.
    DALib::MaterialLib->DeleteGroup(MaterialGroup);
    MaterialGroup = -1;
    // Then FUN_0042cc50 (~FSwappedMaterial), FUN_00576010 and FUN_0059fa50 -- the member's and
    // bases' destructors in that order.
}

uintptr_t NnNews::RowDword(size_t ArrayOffset, int Index) const
{
    uintptr_t Value = 0;
    unsigned char const* const Bytes = reinterpret_cast<unsigned char const*>(this);
    memcpy(&Value, Bytes + ArrayOffset + static_cast<ptrdiff_t>(Index) * 4, sizeof(Value));
    return Value;
}

void NnNews::DeleteSelf()
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

void NnNews::RevealSelected()
{
    // 0x004a4ca6..0x004a4cc7 / 0x004a4f54..0x004a4f75: the item's +0x28 and +0x40 texts. Retail
    // does not test Selected first (a click below the last item reveals past the array).
    NewsBroadcastProperties const& Item = News[Selected];
    KnowledgeMapRevealFmtStr(&Item.Body); // +0x28, FUN_00459950
    KnowledgeMapRevealFmtStr(&Item.Logo); // +0x40
}

bool NnNews::Create(Win3D* NewParent, void* Data)
{
    // LAB_004a4250 (RET 8). Extents, hit offset and the base position.
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

    // 0x004a4347..0x004a4385: the background at the base position (FUN_00423b30, base + 0).
    {
        float const Position[3] = {X + 0.0f, Y + 0.0f, Z + 0.0f};
        Background = CreateStaticControl("NewsBackground",
                                         "Interface\\NeuroNet\\NewsVendor\\NEWS_background.cmp",
                                         Position, nullptr, Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);
    }
    // 0x004a438b..0x004a4417: FUN_00423b30(base + {-0.215, 0.217, 0}).
    {
        float const Position[3] = {X + -0.215f, Y + 0.217f, Z + 0.0f};
        CreateStaticControl("NewsHeadlinesFrame",
                            "Interface\\NeuroNet\\NewsVendor\\NEWS_headlineframe.3db", Position,
                            nullptr, Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);
    }
    // 0x004a441c..0x004a44a5
    {
        float const Position[3] = {X - 0.006f, Y - 0.149f, Z};
        CreateStaticControl("NewsStoryFrame", "Interface\\NeuroNet\\NewsVendor\\NEWS_textframe.3db",
                            Position, nullptr, Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);
    }
    // 0x004a44aa..0x004a4533
    {
        float const Position[3] = {X + 0.364f, Y + 0.24f, Z};
        CreateStaticControl("NewsVideoFrame",
                            "Interface\\NeuroNet\\NewsVendor\\NEWS_videowindow.3db", Position,
                            nullptr, Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);
    }
    // 0x004a4538..0x004a458d
    {
        float const Position[3] = {X - 0.003f, Y + 0.471f, Z};
        CreateTextControl("NewsTitle", "PageHeader", Position, nullptr, 1.22f, 0.03f, 2.0f, 0x4ab, 1,
                          0, 0);
    }
    // 0x004a4592..0x004a462e: the video screen's material group, then the screen itself.
    MaterialGroup = MiscUtilLoadMaterialLibrary(kVideoScreenMesh);
    {
        float const Position[3] = {X + 0.364f, Y + 0.24f, Z};
        CreateStaticControl("NewsVideoScreen", kVideoScreenMesh, Position, nullptr, Zero, 0, 0, 0, 1,
                            1, 0, 1, 0, 0, 0, Zero);
    }
    // 0x004a4633..0x004a46ab
    {
        float const Position[3] = {X + 0.591f, Y + 0.461f, Z};
        float const Half[3] = {0.02f, 0.02f, 0.0f};
        CloseButton = CreateButtonControl("NewsCloseButton", "Interface\\NeuroNet\\Inventory\\x.3db",
                                          nullptr, "ui_select_item", nullptr, Position, nullptr,
                                          Half, 0, 0, 0, 0, 0x42a, 0, 0);
    }

    // 0x004a46b0..0x004a4980: five rows, 0.0615 apart from {x - 0.39, y + 0.36}.
    float const Top[3] = {X - 0.39f, Y + 0.36f, Z};
    char Name[0x100];
    for (int Row = 0; Row < 5; ++Row)
    {
        // 0x004a46f0..0x004a470c: FILD Row, FMUL 0.0615f, subtracted from the float Top.y and
        // stored once as float (the product and the difference are exact in double).
        float const RowY = static_cast<float>(static_cast<double>(Top[1]) -
                                              static_cast<double>(Row) * static_cast<double>(0.0615f));

        sprintf(Name, "NewsHeadlineIcon%d", Row);
        {
            float const Position[3] = {Top[0] - 0.125f, RowY, Top[2]};
            HeadlineIcons[Row] = CreateStaticControl(Name, kHeadlineIconMesh, Position, nullptr, Zero,
                                                     0, 0, 0, 1, 0, 0, 1, 0, 0, 0, Zero);
        }
        sprintf(Name, "NewsHeadlineIconFrame%d", Row);
        {
            float const Position[3] = {Top[0] - 0.125f, RowY, Top[2]};
            float const Half[3] = {0.03f, 0.03f, 0.0f};
            IconFrames[Row] = CreateWireButtonControl(Name, "ui_select_item", Position, Half, 0, 0,
                                                      0x42b, 0, 0);
        }
        sprintf(Name, "NewsHeadlineBox%d", Row);
        {
            float const Position[3] = {Top[0] + 0.19f, RowY, Top[2]};
            float const Half[3] = {0.245f, 0.031f, 0.0f};
            HeadlineBoxes[Row] = CreateWireButtonControl(Name, nullptr, Position, Half, 0, 0, 0x42b, 0,
                                                         0);
        }
        sprintf(Name, "NewsHeadlineHeadline%d", Row);
        {
            // 0x004a48d5..0x004a4947: the position is kept in HeadlinePositions[Row] and passed
            // from there.
            HeadlinePositions[Row][0] = Top[0] - 0.045f;
            HeadlinePositions[Row][1] = RowY + 0.036f;
            HeadlinePositions[Row][2] = Top[2];
            Headlines[Row] = CreateTextControl(Name, "NewsMission", HeadlinePositions[Row], nullptr,
                                               0.475f, 0.03f, 1.5f, 0, 0, 0, 1);
        }
        // 0x004a494c..0x004a4980: the headline's Font::String goes multi-line and drops its
        // layout, unless it already is (the factory's result is not tested).
        {
            FFontString* const Text = static_cast<NnTextControl*>(Headlines[Row])->GetFontString();
            if (Text->bMultiLine != 1)
            {
                Text->bMultiLine = 1; // +0x30
                Text->LayoutGen = 0;  // +0x28
            }
        }
    }

    // 0x004a4985..0x004a49ce: the scroll bar (0..5, page 5).
    {
        float const Position[3] = {X + 0.09f, Y + 0.397f, Z};
        Scroll = CreateUIScroll("NewsHeadlineScroll", Position, 0.019f, 0.317f, 0, 5, 5, 0);
    }
    // 0x004a49d3..0x004a4a31: the ticker (style *DAT_00611d0c, Arg6 DAT_005d3fb0 = 2.7f).
    {
        float const Position[3] = {X + 0.105f, Y - 0.342f, Z};
        Ticker = CreateTextControl("NewsTicker", G_NewsTickerStyle, Position, nullptr, 1.35f, 0.03f,
                                   2.7f, 0, 0, 0, 0);
    }
    // 0x004a4a30..0x004a4acc: the selected item's icon (fixed x / y, the base's z).
    {
        float const Position[3] = {-0.535f, -0.111f, BasePosition[2]};
        float const Half[3] = {0.03f, 0.03f, 0.0f};
        StoryIcon = CreateStaticControl("NewsStoryIcon", kHeadlineIconMesh, Position, nullptr, Half, 0,
                                        0, 0, 1, 0, 0, 1, 0, 0, 0, Zero);
    }
    // 0x004a4ad1..0x004a4b3a: the name goes through sprintf into the name buffer first (no
    // conversion).
    sprintf(Name, "NewsStoryHeadline");
    {
        float const Position[3] = {X + 0.02f, Y - 0.006f, Z};
        StoryHeadline = CreateTextControl(Name, "Normal", Position, nullptr, 1.18f, 0.03f, 1.8f, 0, 1,
                                          0, 0);
    }
    // 0x004a4b3f..0x004a4b7d
    {
        float const Position[3] = {X - 0.556f, Y - 0.051f, Z};
        StoryText = CreateRichTextWin("NewsStoryText", 1.08f, 0.27f, Position, true);
    }

    // 0x004a4b82..0x004a4bcc: layer 0x64, slot 31 with {0, 0, 0}, slot 5 with the position.
    DrawLayer = 0x64;
    SetOffset(0.0f, 0.0f, 0.0f);
    SetPosition(Position);

    // 0x004a4bcf..0x004a4c7b
    IconPathCount = 7;
    IconPaths = static_cast<char const**>(::operator new(0x1c));
    for (int Index = 0; Index < 7; ++Index)
        IconPaths[Index] = kNewsIconMeshes[Index];
    for (int Index = 0; Index < 7; ++Index)
        IconIds[Index] = kNewsIconIds[Index];

    // 0x004a4c80..0x004a4cc7
    LoadNews();
    BuildTicker();
    RefreshRows();
    Field_0x57c = 0;
    ShowSelected();
    RevealSelected();

    // 0x004a4ccc..0x004a4d31: the cancel handler; the open sound stopped if it is still going,
    // then (re)started.
    HudPushCancelHandler(AsHudCmdHandler(), 0); // FUN_00576360
    if (bOpenSoundPlaying != 0)
        StopClientSoundArch(LookupClientSoundArch(&OpenSound));
    OpenSound.Id = CreateID("news_vendor_open");
    PlayClientSoundArch(LookupClientSoundArch(&OpenSound), nullptr,
                        nullptr, 0);
    bOpenSoundPlaying = 1;

    // 0x004a4d36..0x004a4d50
    return Win3D::Create(NewParent, Data); // FUN_004b5060
}

void NnNews::LoadNews()
{
    // FUN_004a4fd0. 0x004a4fe8..0x004a5022: the news of the base the player is in
    // (DAT_0066873c), counted by walking Head / Next.
    TMirrorPropertyBucket<NewsBroadcastProperties>* const Bucket =
        GetClientLocationBaseNews(GetClientLocationCurrentBase());
    int Count = 0;
    MetaNode<NewsBroadcastProperties>* Node = nullptr;
    while (Bucket->List.traverse(Node, true))
        ++Count;

    // 0x004a5024..0x004a5071: new[] (count prefix, NewsBroadcastProperties' ctor).
    News = new NewsBroadcastProperties[Count];
    NewsCount = Count;

    // 0x004a5062..0x004a5097: one "taken" byte per node, zeroed.
    unsigned char* const Taken = static_cast<unsigned char*>(::operator new(static_cast<size_t>(Count)));
    if (Count > 0)
        memset(Taken, 0, static_cast<size_t>(Count));

    // 0x004a50a0..0x004a5220: four passes over the nodes in list order, each copying (operator=)
    // the not-yet-taken nodes that pass its test into the next slot: byte +0x09 set, then
    // Category == 1, then byte +0x08 set, then every one left. Retail finds node i by walking
    // from the head for each i; one walk per pass visits the same nodes in the same order.
    int Next = 0;
    for (int Pass = 0; Pass < 4; ++Pass)
    {
        Node = nullptr;
        for (int Index = 0; Index < Count; ++Index)
        {
            Bucket->List.traverse(Node, true);
            if (Taken[Index] != 0)
                continue;
            NewsBroadcastProperties const& Item = *Node->Data;
            bool bTake = false;
            switch (Pass)
            {
            case 0: bTake = Item.bFlag2; break;           // 0x004a50dd byte +9
            case 1: bTake = (Item.Category == 1); break;  // 0x004a513d dword +0xc
            case 2: bTake = Item.bFlag1; break;           // 0x004a519d byte +8
            default: bTake = true; break;                 // 0x004a51fd
            }
            if (!bTake)
                continue;
            News[Next] = Item; // ??4NewsBroadcastProperties@@
            ++Next;
            Taken[Index] = 1;
        }
    }

    // 0x004a5220..0x004a5257: the rows from the top, item 0 selected, the scroll range, the
    // bytes freed.
    ScrollOffset = 0;
    Field_0x57c = 0;
    Selected = 0;
    Scroll->SetProperty(0x37, 0, static_cast<uintptr_t>(NewsCount - 1));
    ::operator delete(static_cast<void*>(Taken));
}

void NnNews::RefreshRows()
{
    // FUN_004a5270.
    wchar_t Buffer[0x400];
    int Row = 0;
    for (int Index = ScrollOffset; Index < NewsCount; ++Index)
    {
        NewsBroadcastProperties const& Item = News[Index];
        WinControl* const Controls[4] = {HeadlineIcons[Row], IconFrames[Row], HeadlineBoxes[Row],
                                         Headlines[Row]};

        // 0x004a52c0..0x004a5348: the row's four controls, property 2 then property 1, with 0.
        for (int Slot = 0; Slot < 4; ++Slot)
            Controls[Slot]->SetProperty(2, 0, 0);
        for (int Slot = 0; Slot < 4; ++Slot)
            Controls[Slot]->SetProperty(1, 0, 0);

        // 0x004a534e..0x004a538a: the icon mesh and the frame's tooltip.
        char const* Path = nullptr;
        int Ids = 0;
        NewsIconFor(*this, Item.Category, &Path, &Ids);
        HeadlineIcons[Row]->SetProperty(4, PointerArg(Path), 0);
        IconFrames[Row]->SetProperty(3, static_cast<uintptr_t>(Ids), 0);

        // 0x004a5390..0x004a53af: the item's +0x28 text.
        FmtStrSprintf(&Item.Body, Buffer, 0x400);
        Headlines[Row]->SetProperty(0x1d, PointerArg(Buffer), 0);

        // 0x004a53b5..0x004a540d: a headline of fewer than two lines (unsigned JB) moves 0.016
        // down (FLD float, FSUB qword, FSTP float); property 6 with a local copy either way. The
        // five HeadlinePositions are Create's constants, and for each of them the double
        // difference rounds to the same float as the extended one (checked exactly).
        float const* const Stored = HeadlinePositions[Row];
        float Moved[3] = {Stored[0], Stored[1], Stored[2]};
        unsigned const Lines = static_cast<unsigned>(
            FontStringChildCount(static_cast<NnTextControl*>(Headlines[Row])->GetFontString()));
        if (Lines < 2)
            Moved[1] = static_cast<float>(static_cast<double>(Stored[1]) - 0.016);
        Headlines[Row]->SetProperty(6, PointerArg(Moved), 0);

        // 0x004a5413..0x004a544b: the selected item's frame and box lit (0xc with 1).
        uintptr_t const bSelected = (Index == Selected) ? 1u : 0u;
        IconFrames[Row]->SetProperty(0xc, bSelected, 0);
        HeadlineBoxes[Row]->SetProperty(0xc, bSelected, 0);

        // 0x004a5451..0x004a546b: five rows filled -- done, no tail.
        ++Row;
        if (Row >= 5)
            return;
    }

    // 0x004a5493..0x004a5564: every unused row, property 2 then property 1 with 1, the default
    // icon mesh, the box and the headline cleared (0x1e with 0).
    for (; Row < 5; ++Row)
    {
        WinControl* const Controls[4] = {HeadlineIcons[Row], IconFrames[Row], HeadlineBoxes[Row],
                                         Headlines[Row]};
        for (int Slot = 0; Slot < 4; ++Slot)
            Controls[Slot]->SetProperty(2, 1, 0);
        for (int Slot = 0; Slot < 4; ++Slot)
            Controls[Slot]->SetProperty(1, 1, 0);
        HeadlineIcons[Row]->SetProperty(4, PointerArg(IconPaths[0]), 0);
        HeadlineBoxes[Row]->SetProperty(0x1e, 0, 0);
        Headlines[Row]->SetProperty(0x1e, 0, 0);
    }
}

void NnNews::ShowSelected()
{
    // FUN_004a5580.
    if (Selected == -1)
        return;

    // 0x004a55b1..0x004a55e6: the item's +0x28 text as the story headline.
    wchar_t Buffer[0x1000];
    NewsBroadcastProperties const& Item = News[Selected];
    FmtStrSprintf(&Item.Body, Buffer, 0x1000);
    StoryHeadline->SetProperty(0x1d, PointerArg(Buffer), 0);

    // 0x004a55ec..0x004a56f9: the +0x40 text as the story -- a style node 0x9ca4, the text (XML
    // when it parses), then the pane scrolled to the top, cleared and refilled.
    FmtStrSprintf(&Item.Logo, Buffer, 0x1000);
    {
        RenderDisplayList Text;
        {
            StyleNode Style(0x9ca4);
            RdlAppendNode(Text, Style.Clone());
        }
        AppendTextMaybeXmlRdl(Buffer, static_cast<unsigned>(wcslen(Buffer)), Text, false);
        StoryText->SetScrollOrigin(0, 0); // FUN_005798b0
        StoryText->ClearContent();        // FUN_0057b370
        StoryText->AppendContent(Text);   // FUN_0057b3e0

        // 0x004a56fe..0x004a57d8: the item's logo texture; unless it is the one already swapped
        // in (strcmp), the name is freed, the swap released, the name nulled, the swap's fields
        // zeroed, the new name copied and swapped into "MV_vidmaterial".
        char const* const Texture = News[Selected].get_logo_texture();
        if (VideoTexture == nullptr || strcmp(Texture, VideoTexture) != 0)
        {
            malloc_strfree(&VideoTexture);
            VideoMaterial.Release(); // FUN_0042cc50
            VideoTexture = nullptr;
            VideoMaterial.Reset();   // FUN_0042cc30
            malloc_strcpy(&VideoTexture, Texture);
            if (!VideoMaterial.Init(kVideoMaterialName))
                FDUMP(0x100001, kNoMaterialInNewsVendor, kNewsVendorCpp, 0x426, kVideoMaterialName);
            VideoMaterial.Swap(VideoTexture, kVideoTextureLibrary);
        }

        // 0x004a57dd..0x004a581f: the icon mesh and its tooltip (not shown or hidden here).
        char const* Path = nullptr;
        int Ids = 0;
        NewsIconFor(*this, Item.Category, &Path, &Ids);
        StoryIcon->SetProperty(4, PointerArg(Path), 0);
        StoryIcon->SetProperty(3, static_cast<uintptr_t>(Ids), 0);
    } // 0x004a5825..0x004a586f: the display list destroyed.
}

void NnNews::BuildTicker()
{
    // FUN_004a5890.
    wchar_t TickerBuffer[0x400]; // [esp+0x830]
    wchar_t Part[0x100];         // [esp+0x30]
    wchar_t Space[0x100];        // [esp+0x230] (the headline separator shares it)
    wchar_t Separator[0x100];    // [esp+0x430]
    wchar_t Number[0x100];       // [esp+0x630]
    TickerBuffer[0] = L'\0';
    int Total = 0;

    // 0x004a58b6..0x004a590e: the base's market and its commodities (category 0).
    FClientMarket* const Market = GetClientLocationBaseMarket(GetClientLocationCurrentBase());
    std::list<FClientMarketGood const*> Goods;
    ClientMarketCollectCategory(Market, &Goods, 0);

    do
    {
        // 0x004a5920..0x004a59d3: every news item with byte +0x08 set: its +0x28 text and
        // " --- ", when both still fit.
        for (int Index = 0; Index < NewsCount; ++Index)
        {
            NewsBroadcastProperties const& Item = News[Index];
            if (!Item.bFlag1)
                continue;
            FmtStrSprintf(&Item.Body, Part, 0x100);
            wcscpy(Space, L" --- ");
            int const Length = static_cast<int>(wcslen(Space)) + static_cast<int>(wcslen(Part)) + Total;
            if (Length >= 0x400)
                continue;
            wcscat(TickerBuffer, Part);
            wcscat(TickerBuffer, Space);
            Total = Length;
        }

        // 0x004a59d9..0x004a5b98: every commodity the base sells or buys (TransactionType 2 or
        // 0) with a positive price and a name: "NAME price --- ", when it still fits.
        for (std::list<FClientMarketGood const*>::const_iterator It = Goods.begin();
             It != Goods.end(); ++It)
        {
            FClientMarketGood const* const Entry = *It;
            int const Type = static_cast<int>(Entry->Info.Type);
            if (Type != 2 && Type != 0)
                continue;
            unsigned const GoodId = Entry->Info.GoodId;
            int const Price = static_cast<int>(
                floor(static_cast<double>(ClientMarketGetPrice(Market, GoodId))));
            GoodInfo const* const Good = GoodList::find_by_id(GoodId);
            if (Price <= 0)
                continue;
            if (Good->IdsName == 0) // +0x88
                continue;
            if (GetIdsStringWWithRawFallback(static_cast<unsigned>(Good->IdsName), Part, 0x100) == 0)
            {
                ID_String GoodName; // the ID_String at GoodInfo +0x48
                GoodName.Id = Good->Id;
                FDUMP(0x100002, kNoStringForGood, kNewsVendorCpp, 0x35f, Good->IdsName,
                      GoodName.get_string());
            }
            // 0x004a5a97..0x004a5ac7: upper-cased in place (wcslen taken again every pass).
            for (size_t Char = 0; Char < wcslen(Part); ++Char)
                Part[Char] = static_cast<wchar_t>(towupper(Part[Char]));
            wcscpy(Space, L" ");
            _itow(Price, Number, 10);
            wcscpy(Separator, L" --- ");
            int const Length = static_cast<int>(wcslen(Separator)) + static_cast<int>(wcslen(Number)) +
                               static_cast<int>(wcslen(Space)) + static_cast<int>(wcslen(Part)) + Total;
            if (Length >= 0x400)
                continue;
            wcscat(TickerBuffer, Part);
            wcscat(TickerBuffer, Space);
            wcscat(TickerBuffer, Number);
            wcscat(TickerBuffer, Separator);
            Total = Length;
        }
    } while (Total != 0 && Total < 0x80); // 0x004a5b9e..0x004a5ba8

    // 0x004a5bae..0x004a5bdc: the goods list emptied (its storage goes when Goods leaves scope,
    // 0x004a5c43..0x004a5c7e in retail, after TickerShow).

    // 0x004a5bde..0x004a5c35: the ticker text, its length, a width per character from the
    // ticker font (slot 10 with Len -1).
    TickerText = _wcsdup(TickerBuffer);
    TickerLength = static_cast<int>(wcslen(TickerText));
    TickerWidths = static_cast<int*>(::operator new(static_cast<size_t>(TickerLength) * 4));
    FindMenuFontImp(G_NewsTickerStyle)->GetCharWidths(TickerText, -1, TickerWidths);

    // 0x004a5c38..0x004a5c3e
    TickerShow(0, 0.0f);
}

void NnNews::TickerShow(int Start, float Offset)
{
    // FUN_004a5ca0 (RET 8).
    if (TickerLength == 0)
        return;

    // 0x004a5cb2..0x004a5cc2: X = -Offset, Limit = FILD DAT_00610850 * 0.73125f. Every value here
    // is exact in double (an integer-valued float, integer widths, a 24-bit float times a small
    // integer).
    double X = -static_cast<double>(Offset);
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: the ticker is interface, as wide as on a 4:3 surface of this height.
    double const Limit = WidescreenEquivalentSurfaceWidth() * static_cast<double>(0.73125f);
#else
    double const Limit = static_cast<double>(static_cast<int>(G_RenderSurfaceWidth)) *
                         static_cast<double>(0.73125f);
#endif

    // 0x004a5cc8..0x004a5d13: characters from Start (wrapping) while fewer than 0x100 are taken
    // and X is below Limit (TEST AH,5 + JP: an unordered or not-less compare stops).
    int Count = 0;
    if (TickerLength > 0)
    {
        do
        {
            int const Index = (Count + Start) % TickerLength;
            if (Count >= 0x100)
                break;
            if (!(X < Limit))
                break;
            TickerView[Count] = TickerText[Index];
            X += static_cast<double>(TickerWidths[Index]);
            ++Count;
        } while (Count < TickerLength);
    }

    // 0x004a5d17..0x004a5d6c: the slice, then 0x22 (0, 1) and 0x23 with float bits
    // for the left trim (Offset) and right trim (Overhang). The overhang overwrites
    // Start's stack slot at 004a5d35; 004a5d65 still reads the original Offset.
    TickerView[Count] = L'\0';
    float const Overhang = static_cast<float>(X - Limit);
    Ticker->SetProperty(0x1d, PointerArg(TickerView), 0);
    Ticker->SetProperty(0x22, 0, 1);
    Ticker->SetProperty(0x23, FloatArg(Offset), FloatArg(Overhang));
}

void NnNews::Draw()
{
    // FUN_004a5d80. 0x004a5d85..0x004a5e03: every child at 0x64; the hovered row's frame or box
    // 0x65, the selected row's 0x66 (gated on Clicked, indexed by Selected; the second test
    // wins). The rows are Hovered / Selected minus the scroll offset and are not range-checked.
    int const SelectedRow = Selected - ScrollOffset;
    int const HoveredRow = Hovered - ScrollOffset;
    size_t const FramesOffset = offsetof(NnNews, IconFrames);
    size_t const BoxesOffset = offsetof(NnNews, HeadlineBoxes);
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

    // 0x004a5e05..0x004a5e2b: only the box column is joined.
    WireControlsJoinColumn(HeadlineBoxes, 5); // FUN_005a4b50
    Background->DrawLayer = 0x63;
    StoryText->DrawLayer = 0x67;

    // 0x004a5e32..0x004a5e40
    WinControl::Draw();     // FUN_005a0760
    NnMontageFlushButtons(); // FUN_00559c20
    ButtonMontageEnsure();   // FUN_00559c10 (tail jump)
}

void NnNews::Update()
{
    // LAB_004a5e50: the frame time scrolls the ticker 50 pixels a second.
    double const Dt = GetClientFrameTimeCapped(); // FUN_0042d680
    if (TickerLength != 0)
    {
        // 0x004a5e67..0x004a5e84: FMUL 50f, FADD the float scroll, stored as float.
        TickerScroll = static_cast<float>(Dt * 50.0 + static_cast<double>(TickerScroll));

        // 0x004a5e8a..0x004a5edb: whole characters scrolled past are dropped (TEST AH,1 + JNE
        // leaves when Scroll < Width; TEST AH,0x41 + JNP repeats while Width <= Scroll).
        int Width = TickerWidths[TickerIndex];
        if (static_cast<double>(TickerScroll) >= static_cast<double>(Width))
        {
            do
            {
                TickerScroll = static_cast<float>(static_cast<double>(TickerScroll) -
                                                  static_cast<double>(Width));
                TickerIndex = TickerIndex + 1;
                if (!(TickerIndex < TickerLength))
                    TickerIndex = 0;
                Width = TickerWidths[TickerIndex];
            } while (static_cast<double>(Width) <= static_cast<double>(TickerScroll));
        }

        // 0x004a5edd..0x004a5f19: the remainder rounded to whole pixels.
        float const Offset = static_cast<float>(
            static_cast<int>(floor(static_cast<double>(TickerScroll) + 0.5)));
        TickerShow(TickerIndex, Offset);
    }
    Win3D::Update(); // FUN_005a17b0 (tail jump)
}

intptr_t NnNews::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_004a4d60 (RET 0xc): byte table 0x004a4fa0 over Msg - 0x26 (0..0x28) into the cases
    // at 0x004a4f88. Every path returns 0.
    switch (Msg)
    {
    case 0x4e:
    {
        // 0x004a4d88..0x004a4dbe: closed from outside -- no callback, no NavBar. (Retail tests
        // `this` first.)
        DeleteSelf();
        return 0;
    }
    case 0x3e:
    case 0x2c:
    case 0x2d:
    {
        // 0x004a4dc1..0x004a4ddd: the scroll bar moved; then, like the hover messages, the walk
        // below (case 0x3e has no break).
        if (Msg == 0x3e && A == PointerArg(Scroll) && static_cast<int>(B) != ScrollOffset)
        {
            ScrollOffset = static_cast<int>(B);
            RefreshRows();
        }

        // 0x004a4de2..0x004a4e2b: the hovered item, written (0x2c) or cleared (anything else,
        // when it is ScrollOffset + Row) on every pass until the control is found.
        int Row = 0;
        for (; Row < 5; ++Row)
        {
            int const Item = ScrollOffset + Row;
            if (Msg == 0x2c)
                Hovered = Item;
            else if (Hovered == Item)
                Hovered = -1;
            if (A == PointerArg(IconFrames[Row]) || A == PointerArg(HeadlineBoxes[Row]))
                break;
        }
        if (Row == 5)
            return 0;

        // 0x004a4e2e..0x004a4e68: the row's other wire button gets the same message.
        if (A != PointerArg(IconFrames[Row]))
            IconFrames[Row]->SetProperty(Msg, 0, 0);
        if (A != PointerArg(HeadlineBoxes[Row]))
            HeadlineBoxes[Row]->SetProperty(Msg, 0, 0);
        return 0;
    }
    case 0x42:
    {
        // 0x004a4e6b..0x004a4e9c: moved to *A -- slot 31 with the three floats, slot 5 with the
        // position.
        float const* const Target = reinterpret_cast<float const*>(A);
        SetOffset(Target[0], Target[1], Target[2]);
        SetPosition(Position);
        return 0;
    }
    case 0x26:
    {
        if (A == PointerArg(CloseButton))
        {
            // 0x004a4ea3..0x004a4ef9
            if (ResultCallback != nullptr)
                ResultCallback(ResultContext);
            NavBarCloseService(false); // FUN_004424f0(0)
            DeleteSelf();
            return 0;
        }

        // 0x004a4efc..0x004a4f75: a row clicked. No match leaves Clicked at -1 and selects
        // ScrollOffset - 1, as retail does.
        Clicked = -1;
        for (int Row = 0; Row < 5; ++Row)
        {
            if (A == PointerArg(IconFrames[Row]) || A == PointerArg(HeadlineBoxes[Row]))
            {
                Clicked = Row;
                break;
            }
        }
        Selected = ScrollOffset + Clicked;
        RefreshRows();
        Field_0x57c = 0;
        ShowSelected();
        RevealSelected();
        return 0;
    }
    default:
        return 0;
    }
}

bool NnNews::OnEvent(unsigned MsgId)
{
    // LAB_004a5f40 (RET 4), on the +0x32c face: USER_CANCEL (0x10) closes the window like the
    // close button. (Retail tests the adjusted `this` for null.)
    if (MsgId != 0x10)
        return false;
    if (ResultCallback != nullptr)
        ResultCallback(ResultContext);
    NavBarCloseService(false); // FUN_004424f0(0)
    DeleteSelf();
    return true;
}
