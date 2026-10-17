// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "page_server_select.h"

#include "chat_init.h"         // ChatAppendRdlCopy (the FUN_0046c800 / FUN_005754b0 clone loop)
#include "connect_flow.h"      // ConnectToServer (FUN_005a8770)
#include "dacom.h"             // FDUMP
#include "font_imp.h"          // FindMenuFontImp (FUN_004177b0)
#include "globals.h"           // g_client (DAT_0067ecb8), g_display, GetClientFrameTimeCapped
#include "ids_rdl.h"           // LoadIdsIntoRdl, RdlAppendNode, RdlClear, RdlRemoveLastParagraph
#include "ids_strings.h"       // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "input_manager.h"     // MouseSetCursor (FUN_0041dde0)
#include "intro_page_util.h"   // the inline RDL / Win3D idioms shared with PageCharacterSelect
#include "loading_screen.h"    // LoadingScreenStart (FUN_00579290)
#include "login_mgr.h"         // G_LoginMgr (DAT_0067e7b8), G_bInternetServerList
#include "menu_ui_init.h"      // PlayUiSoundSlot (FUN_00564570), PlayOrRewindUiSoundSlot
#include "nn_prompt.h"         // NnPromptRunTextEntry (FUN_00473f70)
#include "nn_text_control.h"   // NnTextControl (TextControl, FUN_0058a110)
#include "nn_ui_pulse.h"       // G_HudChromeColorRgba (DAT_00679bb4)
#include "reg_key_stack.h"     // FRegKeyStack
#include "server_filter_dialog.h" // ServerFilterDialogRun (FUN_005637b0)
#include "tooltip_hover.h"     // FToolTipHoverInfo
#include "ui_fast_table.h"
#include "ui_rich_text_win.h"
#include "ui_scroll.h"
#include "ui_text_msg_button.h"
#include "win3d_immediate.h"   // Win3DVec3Add (FUN_00423b30)
#include "win_factory.h"       // WinFactoryCreate (FUN_0059db20)

#include "common_text.h"

#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    // DAT_0067ab20.
    PageServerSelect G_PageServerSelect;

    char const kIntroCpp[] = "E:\\FL\\Scratch\\Source\\Client\\Interface\\Intro.cpp";

    char const kServerTableKey[] = "ServerTable"; // s_ 0x005e2c4c (also the widget name)

    // .rdata:005e2604 -- the column captions, loaded by Create (0x0056e853).
    unsigned const kColumnHeaderIds[8] = {0x747, 0x745, 0x746, 0x748,
                                          0x749, 0x74a, 0x74b, 0x74c};
    // .rdata:005e2624 -- the header cells' tool tips (0x0056e876).
    unsigned const kColumnHeaderTipIds[8] = {0xac0, 0xabe, 0xabf, 0xac1,
                                             0xac2, 0xac3, 0xac4, 0xac5};
    // .rdata:005e2644 -- the column edges Create writes when the registry has none (0x0056eaa0).
    float const kDefaultColumnEdges[8] = {0.0f,         0.371596009f, 0.553894997f,
                                          0.664525986f, 0.756824017f, 0.875788987f,
                                          0.992121994f, 1.06441998f};

    // -----------------------------------------------------------------------------------------
    // The sixteen column comparators (cdecl, "A before B"). CLoginMgr::GetFilteredServers sorts
    // with one of them. Unsigned compares throughout (SBB/NEG, SETA, SETC).

    // LAB_00565e50 -- column 0: _wcsicoll(A name, B name) < 0.
    bool CompareNameAsc(FServerInfo const* A, FServerInfo const* B)
    {
        return _wcsicoll(A->Name.c_str(), B->Name.c_str()) < 0;
    }

    // FUN_00565db0 -- column 1: A +0x10 < B +0x10.
    bool CompareField10Asc(FServerInfo const* A, FServerInfo const* B)
    {
        return static_cast<unsigned>(A->IpAddress) < static_cast<unsigned>(B->IpAddress);
    }

    // FUN_00565dd0 -- column 2: known logins first (FUN_005a9950); two known -> the later login
    // time (FUN_005a9970, SETA) first; two unknown -> std::operator< on the names.
    bool CompareKnownAsc(FServerInfo const* A, FServerInfo const* B)
    {
        bool const bKnownA = G_LoginMgr.IsKnownLogin(*A);
        bool const bKnownB = G_LoginMgr.IsKnownLogin(*B);
        if (bKnownA)
        {
            if (!bKnownB)
                return true;
            unsigned const TimeA = G_LoginMgr.GetLoginTime(*A);
            unsigned const TimeB = G_LoginMgr.GetLoginTime(*B);
            return TimeA > TimeB;
        }
        if (bKnownB)
            return false;
        return A->Name < B->Name;
    }

    // FUN_00565e90 -- column 3: A ping < B ping (so -1 / -2 sort last).
    bool ComparePingAsc(FServerInfo const* A, FServerInfo const* B)
    {
        return static_cast<unsigned>(A->Ping) < static_cast<unsigned>(B->Ping);
    }

    // FUN_00565eb0 -- column 4: players (+0x84), then the maximum (+0x88).
    bool ComparePlayersAsc(FServerInfo const* A, FServerInfo const* B)
    {
        if (A->PlayerCount != B->PlayerCount)
            return static_cast<unsigned>(A->PlayerCount) < static_cast<unsigned>(B->PlayerCount);
        return static_cast<unsigned>(A->MaxPlayers) < static_cast<unsigned>(B->MaxPlayers);
    }

    // FUN_00565ee0 -- column 5: A version < B version.
    bool CompareVersionAsc(FServerInfo const* A, FServerInfo const* B)
    {
        return A->Version < B->Version;
    }

    // FUN_00565f00 -- column 6: +0x3e set first; otherwise the pointers (A < B).
    bool CompareField3eAsc(FServerInfo const* A, FServerInfo const* B)
    {
        if (A->Field_0x3e != 0)
        {
            if (B->Field_0x3e == 0)
                return true;
        }
        else if (B->Field_0x3e != 0)
        {
            return false;
        }
        return std::less<FServerInfo const*>()(A, B);
    }

    // FUN_00565f30 -- column 7: password (+0x94) first, then +0x96 first, then the pointers.
    bool CompareLockAsc(FServerInfo const* A, FServerInfo const* B)
    {
        if (A->bPassword != 0)
        {
            if (B->bPassword == 0)
                return true;
        }
        else if (B->bPassword != 0)
        {
            return false;
        }
        if (A->Field_0x96 != 0)
        {
            if (B->Field_0x96 == 0)
                return true;
        }
        else if (B->Field_0x96 != 0)
        {
            return false;
        }
        return std::less<FServerInfo const*>()(A, B);
    }

    // LAB_00566030 -- column 0 descending: _wcsicoll(A name, B name) > 0.
    bool CompareNameDesc(FServerInfo const* A, FServerInfo const* B)
    {
        return _wcsicoll(A->Name.c_str(), B->Name.c_str()) > 0;
    }

    // FUN_00565fa0 -- B +0x10 < A +0x10.
    bool CompareField10Desc(FServerInfo const* A, FServerInfo const* B)
    {
        return static_cast<unsigned>(B->IpAddress) < static_cast<unsigned>(A->IpAddress);
    }

    // FUN_00565fc0 -- unknown logins first; two known -> the earlier time first (SETC); two
    // unknown -> std::operator> on the names.
    bool CompareKnownDesc(FServerInfo const* A, FServerInfo const* B)
    {
        bool const bKnownA = G_LoginMgr.IsKnownLogin(*A);
        bool const bKnownB = G_LoginMgr.IsKnownLogin(*B);
        if (bKnownA)
        {
            if (!bKnownB)
                return false;
            unsigned const TimeA = G_LoginMgr.GetLoginTime(*A);
            unsigned const TimeB = G_LoginMgr.GetLoginTime(*B);
            return TimeA < TimeB;
        }
        if (bKnownB)
            return true;
        return A->Name > B->Name;
    }

    // FUN_00566070 -- B ping < A ping.
    bool ComparePingDesc(FServerInfo const* A, FServerInfo const* B)
    {
        return static_cast<unsigned>(B->Ping) < static_cast<unsigned>(A->Ping);
    }

    // FUN_00566090 -- B players < A players, then the maxima the same way.
    bool ComparePlayersDesc(FServerInfo const* A, FServerInfo const* B)
    {
        if (B->PlayerCount != A->PlayerCount)
            return static_cast<unsigned>(B->PlayerCount) < static_cast<unsigned>(A->PlayerCount);
        return static_cast<unsigned>(B->MaxPlayers) < static_cast<unsigned>(A->MaxPlayers);
    }

    // FUN_005660c0 -- B version < A version.
    bool CompareVersionDesc(FServerInfo const* A, FServerInfo const* B)
    {
        return B->Version < A->Version;
    }

    // FUN_005660e0 -- +0x3e clear first; otherwise the pointers (B < A).
    bool CompareField3eDesc(FServerInfo const* A, FServerInfo const* B)
    {
        if (A->Field_0x3e != 0)
        {
            if (B->Field_0x3e == 0)
                return false;
        }
        else if (B->Field_0x3e != 0)
        {
            return true;
        }
        return std::less<FServerInfo const*>()(B, A);
    }

    // FUN_00566110 -- no password first, then +0x96 clear first, then the pointers (B < A).
    bool CompareLockDesc(FServerInfo const* A, FServerInfo const* B)
    {
        if (A->bPassword != 0)
        {
            if (B->bPassword == 0)
                return false;
        }
        else if (B->bPassword != 0)
        {
            return true;
        }
        if (A->Field_0x96 != 0)
        {
            if (B->Field_0x96 == 0)
                return false;
        }
        else if (B->Field_0x96 != 0)
        {
            return true;
        }
        return std::less<FServerInfo const*>()(B, A);
    }

    // .data:00614aa0 / .data:00614ac0.
    FServerCompare const kAscendingCompare[8] = {
        CompareNameAsc,    CompareField10Asc, CompareKnownAsc,  ComparePingAsc,
        ComparePlayersAsc, CompareVersionAsc, CompareField3eAsc, CompareLockAsc};
    FServerCompare const kDescendingCompare[8] = {
        CompareNameDesc,    CompareField10Desc, CompareKnownDesc,  ComparePingDesc,
        ComparePlayersDesc, CompareVersionDesc, CompareField3eDesc, CompareLockDesc};

}

// AppendNodeClone, LoadIds, LoadIdsCut, LoadStyledIds, PackTextAttributes, HideWin3D,
// ShowWin3D, ChromeColor, EdgeCount -- shared with PageCharacterSelect.
using namespace IntroPageUtil;

// ---------------------------------------------------------------------------------------------
// ServerRowData

PageServerSelect::ServerRowData::ServerRowData(FServerInfo const& Source)
{
    // FUN_00571280: FServerInfo's defaults (the port's FServerInfo() writes the same values),
    // then the members assigned one by one from Source (FUN_005595c0 for the port vector).
    Info = Source;
}

PageServerSelect::ServerRowData::~ServerRowData()
{
    // FUN_005725e0: the strings at +0x7c / +0x6c / +0x5c / +0x4c, the vector at +0x20, the
    // address at +0x08, then the RefCounter vftable -- Info's own destructor.
}

// ---------------------------------------------------------------------------------------------

PageServerSelect::PageServerSelect()
    : UIPage::MasterWin(Desc.GetPageData())
    , Result(0)
    , Connect(nullptr)
    , MainMenu(nullptr)
    , SetFilter(nullptr)
    , ServerTable(nullptr)
    , ServerScroll(nullptr)
    , ListServerStatus(nullptr)
    , ServerDetails(nullptr)
    , Refresh(nullptr)
    , Title(nullptr)
    , SubTitle(nullptr)
    , ServerInfoBox(nullptr)
    , ServerSelectBox(nullptr)
    , Legend(nullptr)
    , Version(nullptr)
    , InfoBoxHome{0.0f, 0.0f, 0.0f}
    , SelectBoxHome{0.0f, 0.0f, 0.0f}
    , SlideT(0.0)
    , bLeaving(0)
    , LeaveDirection(0)
    , FirstVisible(0)
    , UpPip(nullptr)
    , DownPip(nullptr)
    , Locked(nullptr)
    , Skull(nullptr)
    , GrayLock(nullptr)
    , GraySkull(nullptr)
    , BlueLock(nullptr)
    , BlueSkull(nullptr)
    , ServerConnectImage(nullptr)
    , ServerNoConnectImage(nullptr)
    , bListRequested(0)
    , SortColumn(2)
    , bAscending{1, 1, 1, 1, 1, 1, 1, 1}
    , SelectedServerId(0xffffffffu)
    , ListStatus(3)
{
    // FUN_0056d510: FUN_00591ae0(FUN_00594580(+0x8c)), FUN_00591e30 on +0x8c, +0xb8..+0xe8 = 0,
    // the eight header lists (FUN_005b7fba, 8 x 0xc), the RenderDisplayLists +0x17c..+0x260,
    // ServerFilter (FUN_005628e0), the two edge vectors (FUN_005a6900), +0xb4 = +0x118 = 0,
    // +0x294 = 0, the vftables, +0x2cc = 3, +0x298 = 2, +0x2c8 = -1, +0x29c..+0x2a3 = 1,
    // +0xec = 0. +0xf0..+0x114 and the images are not written; the static object starts zeroed,
    // which is what the initialisers above reproduce.
}

PageServerSelect::~PageServerSelect()
{
    // FUN_005723b0: vftables back, the edge vectors (FUN_004de730), the lists +0x260..+0x17c,
    // the header array (FUN_005b7ec6), +0xb8..+0xe8 = 0, FUN_00591ef0 on +0x8c, FUN_00591ba0 --
    // the member destructors in reverse declaration order, then the base. The ServerFilter at
    // +0x2a4 has no destructor.
}

bool PageServerSelect::SaveEdges()
{
    // FUN_0056daa0: HKCU (the stack's root), "...\\Freelancer\\1.0" and "ServerTable" created
    // with KEY_ALL_ACCESS (0x2001f), the edges written under the top key, the top popped and
    // closed, then every key left on the stack closed (HKCU included, as retail).
    bool bSaved = false;
    FRegKeyStack Keys;
    if (Keys.Create(G_FreelancerRegKey, 0x2001f) == nullptr)
        return false;
    if (Keys.Create(kServerTableKey, 0x2001f) == nullptr)
        return false;
    if (Edges.SaveToRegistry(&Keys))
        bSaved = true;
    Keys.PopClose();
    return bSaved;
}

void PageServerSelect::Destroy()
{
    // FUN_0056d8f0: FUN_005760f0(0xf) on the Event::Handler face.
    UnregisterEvent(0xf);
    if (ServerTable != nullptr)
    {
        // 0x0056d911..0x0056da1b: the table's edges saved, then the images destroyed on the
        // TRC they were made on (DeviceTRC slot 11, the TRC fetched each time).
        ServerTable->GetEdges(&Edges); // FUN_0058d0f0
        SaveEdges();
        ServerTable->GetDeviceTRC()->destroy_image(UpPip);
        ServerTable->GetDeviceTRC()->destroy_image(DownPip);
        ServerTable->GetDeviceTRC()->destroy_image(Locked);
        ServerTable->GetDeviceTRC()->destroy_image(Skull);
        ServerTable->GetDeviceTRC()->destroy_image(GrayLock);
        ServerTable->GetDeviceTRC()->destroy_image(GraySkull);
        ServerTable->GetDeviceTRC()->destroy_image(BlueLock);
        ServerTable->GetDeviceTRC()->destroy_image(BlueSkull);
        Refresh->GetDeviceTRC()->destroy_image(ServerConnectImage);
        Refresh->GetDeviceTRC()->destroy_image(ServerNoConnectImage);
    }
    UIPage::MasterWin::Destroy(); // FUN_00591de0

    // 0x0056da25..0x0056da90: the Version control deleted (slot 0 with 1), the widget
    // pointers cleared, the "Arrow" cursor.
    if (Version != nullptr)
    {
        delete Version;
        Version = nullptr;
    }
    Connect = nullptr;
    MainMenu = nullptr;
    SetFilter = nullptr;
    ServerTable = nullptr;
    ServerScroll = nullptr;
    ServerDetails = nullptr;
    ListServerStatus = nullptr;
    Refresh = nullptr;
    Title = nullptr;
    ServerInfoBox = nullptr;
    ServerSelectBox = nullptr;
    SubTitle = nullptr;
    Legend = nullptr;
    MouseSetCursor("Arrow", true);
}

bool PageServerSelect::Create(Win3D* NewParent, void* /*Payload*/)
{
    // FUN_0056dbf0. MasterWin::Create (FUN_00591cd0) with no payload; false goes straight back.
    if (!UIPage::MasterWin::Create(NewParent, nullptr))
        return false;

    // 0x0056dc24..0x0056dc5b: the list not yet requested, status 3, the filter's version range
    // the client's own version, no server remembered, no result.
    bListRequested = 0;
    ListStatus = 3;
    Filter.VersionMin = static_cast<unsigned>(g_client.packed_version); // +0x2bc
    Filter.VersionMax = static_cast<unsigned>(g_client.packed_version); // +0x2c0
    SelectedServerId = 0xffffffffu;
    Result = 0;

    // 0x0056dc61..0x0056dd4c: the widgets through the IUIPage face (slot 4).
    Connect = static_cast<UiTextMsgButton*>(FindWidget("Connect"));
    MainMenu = static_cast<UiTextMsgButton*>(FindWidget("MainMenu"));
    SetFilter = static_cast<UiTextMsgButton*>(FindWidget("SetFilter"));
    ServerTable = static_cast<UiFastTable*>(FindWidget(kServerTableKey));
    ServerScroll = static_cast<UiScroll*>(FindWidget("ServerScroll"));
    ServerDetails = static_cast<UiRichTextWin*>(FindWidget("ServerDetails"));
    ListServerStatus = static_cast<UiRichTextWin*>(FindWidget("ListServerStatus"));
    Refresh = static_cast<UiTextMsgButton*>(FindWidget("Refresh"));
    Title = static_cast<UiRichTextWin*>(FindWidget("Title"));
    ServerInfoBox = FindWidget("ServerInfoBox");
    ServerSelectBox = FindWidget("ServerSelectBox");
    SubTitle = static_cast<UiRichTextWin*>(FindWidget("SubTitle"));
    Legend = static_cast<UiRichTextWin*>(FindWidget("Legend"));

    // 0x0056dd45..0x0056de44: the "Version" TextControl -- the "Normal" style at
    // (0.32, -0.35, -1), 0.25 wide, the font's line height (slot 6, unsigned) * 0.002 high;
    // the IDS 0x4f7 format with the version word's high then low byte (DAT_0067ecba) as its
    // text (slot 42, property 0x1d); hidden.
    Version = WinFactoryCreate("Version", "TextControl", this, nullptr);
    if (Version != nullptr)
    {
        NnTextControl* const VersionText = static_cast<NnTextControl*>(Version);
        IFont* const NormalFont = FindMenuFontImp("Normal");
        float const Height =
            static_cast<float>(static_cast<unsigned>(NormalFont->GetLineHeight())) * 0.002f;
        float const Position[3] = {0.319999993f, -0.349999994f, -1.0f};
        VersionText->Init("Normal", Position, nullptr, 0.25f, Height, 1.0f, 0u, 1, 0, 0);
        wchar_t Format[0x100];
        GetIdsStringWWithRawFallback(0x4f7u, Format, 0x100);
        unsigned const VersionWord =
            (static_cast<unsigned>(g_client.packed_version) >> 16) & 0xffffu;
        wchar_t Text[0x100];
        swprintf(Text, 0x100, Format, (VersionWord >> 8) & 0xffu, VersionWord & 0xffu);
        VersionText->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Text), 0);
        HideWin3D(Version);
    }

    // 0x0056de48..0x0056de50: FUN_005760d0(0xf) on the Event::Handler face.
    RegisterEvent(0xf);

    // 0x0056de55..0x0056df77: the images, on the table's TRC and (the last two) on Refresh's.
    UpPip = ServerTable->GetDeviceTRC()->create_image("interface\\intro\\images\\uppip.bmp");
    DownPip = ServerTable->GetDeviceTRC()->create_image("interface\\intro\\images\\DownPip.bmp");
    Locked = ServerTable->GetDeviceTRC()->create_image("interface\\intro\\images\\Locked.bmp");
    Skull = ServerTable->GetDeviceTRC()->create_image("interface\\intro\\images\\Skull.bmp");
    GrayLock =
        ServerTable->GetDeviceTRC()->create_image("interface\\intro\\images\\GrayLocked.bmp");
    GraySkull =
        ServerTable->GetDeviceTRC()->create_image("interface\\intro\\images\\GraySkull.bmp");
    BlueLock =
        ServerTable->GetDeviceTRC()->create_image("interface\\intro\\images\\BlueLocked.bmp");
    BlueSkull =
        ServerTable->GetDeviceTRC()->create_image("interface\\intro\\images\\BlueSkull.bmp");
    ServerConnectImage =
        Refresh->GetDeviceTRC()->create_image("interface\\intro\\images\\MULTI_ServerConnect.bmp");
    ServerNoConnectImage = Refresh->GetDeviceTRC()->create_image(
        "interface\\intro\\images\\MULTI_ServerNoConnect.bmp");

    // 0x0056df71..0x0056dfee: slot 46 (+0xb8) on each, +4 of the record it returns.
    Connect->GetToolTipHoverInfo()->IdsAlt = 0xac7;
    MainMenu->GetToolTipHoverInfo()->IdsAlt = 0xac6;
    SetFilter->GetToolTipHoverInfo()->IdsAlt = 0xaf0;
    ServerDetails->GetToolTipHoverInfo()->IdsAlt = 0xac8;
    ListServerStatus->GetToolTipHoverInfo()->IdsAlt = 0xade;
    Refresh->GetToolTipHoverInfo()->IdsAlt = 0xac9;

    ServerTable->SetTipMode(3); // FUN_0058d5e0(3): one tip per cell

    // 0x0056e002..0x0056e831: the lists, in retail's order.
    LoadIds(TextYes, 0x727);
    LoadIds(TextNo, 0x728);
    RdlClear(PingUnknown);
    {
        TextNode const Node(L"????", -1); // u_ 0x005e2c84
        AppendNodeClone(PingUnknown, Node);
    }
    RdlClear(PingNone);
    {
        TextNode const Node(L"-", -1); // DAT_005e2c7c
        AppendNodeClone(PingNone, Node);
    }
    LoadIds(Text729, 0x729);
    LoadIds(DetailsHeader, 0x744);
    LoadIds(DetailsPassword, 0x750);
    LoadIds(DetailsField95, 0x74f);
    LoadIdsCut(DetailsRestricted, 0x6007c);
    LoadStyledIds(SubTitleText, 0x43e0, 0x6007d);
    LoadStyledIds(TitleText, 0x1df5, 0x60072);
    LoadStyledIds(StatusText60058, 0xbb80, 0x60058);
    LoadStyledIds(StatusText60057, 0xbb80, 0x60057);
    LoadStyledIds(StatusText60055, 0x9ca4, 0x60055);
    LoadStyledIds(StatusText60056, 0xbb80, 0x60056);
    LoadIdsCut(LegendSkullText, 0x6007b);
    LoadIdsCut(LegendLockText, 0x6007a);
    LoadStyledIds(Text601eb, 0x5724, 0x601eb);
    LoadStyledIds(SubTitleLan, 0xc9b7, 0x601ef);
    LoadStyledIds(SubTitleInternet, 0xc9b7, 0x601ee);

    // 0x0056e834..0x0056e887: the column captions and the header cells' tool tips.
    for (unsigned Column = 0; Column < 8; ++Column)
    {
        LoadIds(ColumnHeaders[Column], kColumnHeaderIds[Column]);
        FToolTipHoverInfo* const Tip = ServerTable->GetCellTip(0, Column); // FUN_0058d960
        if (Tip != nullptr)
            Tip->IdsAlt = kColumnHeaderTipIds[Column];
    }

    // 0x0056e889..0x0056e8f9: the table's look.
    ServerTable->SetFrameColor(ChromeColor());            // FUN_0058d560(&DAT_00679bb4)
    ServerTable->SetGrabDistance(0.00400000019f);         // FUN_0058d570(0x3b83126f)
    ServerTable->SetResizable(false, true);               // FUN_0058d590(0, 1)
    ServerTable->SetLayoutFlag(true);                     // FUN_0058d5d0(1)
    ServerTable->SetHeaderRows(1);                        // FUN_0058cbb0(1)
    ServerTable->SetBorders(false, false, false, true);   // FUN_0058cb80(0, 0, 0, 1)
    ServerTable->SetRowHighlights(false);                 // FUN_0058cbe0(0)
    ServerTable->SetCellMargins(2, 2);                    // FUN_0058d5b0(2, 2)

    // 0x0056e8fe..0x0056e9c1: the scroll bar -- chrome colours, range 0..rows - 1, page
    // rows - 1 (both through FILD of a zero-extended dword), step 1, value 0, hidden.
    ServerScroll->SetArrowColors(ChromeColor(), ChromeColor()); // FUN_00595f30
    ServerScroll->SetBarColors(ChromeColor(), ChromeColor());   // FUN_00595f50
    ServerScroll->SetRange(
        0.0f, static_cast<float>(static_cast<unsigned>(EdgeCount(ServerTable->RowEdges) - 1)));
    ServerScroll->SetPage(
        static_cast<float>(static_cast<unsigned>(EdgeCount(ServerTable->RowEdges) - 1)));
    ServerScroll->SetStep(1.0f);
    ServerScroll->SetValue(0.0f);
    HideWin3D(ServerScroll);
    ServerDetails->SetAutoScrollbar(true); // FUN_005799f0(1)

    // 0x0056e9d2..0x0056ea7d: the saved edges from HKCU\...\ServerTable (KEY_READ); on success
    // two keys popped and closed, on failure one, then the rest closed.
    bool bLoaded = false;
    {
        FRegKeyStack Keys;
        if (Keys.Open(G_FreelancerRegKey, 0x20019) != nullptr &&
            Keys.Open(kServerTableKey, 0x20019) != nullptr)
        {
            if (Edges.LoadFromRegistry(&Keys))
                bLoaded = true;
            Keys.PopClose();
        }
        Keys.PopClose();
    }
    // 0x0056ea82..0x0056eab5: nothing saved -> the table's own edges with the default column
    // edges written over the first eight (no size check, as retail).
    if (!bLoaded)
    {
        ServerTable->GetEdges(&Edges);
        float* const Cols = Edges.Cols.data();
        for (int Index = 0; Index < 8; ++Index)
            Cols[Index] = kDefaultColumnEdges[Index];
    }
    ServerTable->SetEdges(&Edges); // FUN_0058d250

    // 0x0056eac9..0x0056eb15: with eight column edges the header row gets its captions and
    // stops hovering.
    if (ServerTable->ColEdges.size() == 8)
    {
        for (unsigned Column = 0; Column < 8; ++Column)
            SetCell(0, Column, &ColumnHeaders[Column], false, false);
        ServerTable->GetRow(0)->bHoverable = 0;
    }

    // 0x0056eb18..0x0056eb99: the title.
    Title->ClearContent();
    {
        JustifyNode const Justify(TextJustifyCenter);
        Title->AppendNode(Justify);
    }
    {
        RDLRefNode const Ref(TitleText);
        Title->AppendNode(Ref);
    }
    // 0x0056eb9b..0x0056ec58: the subtitle -- the LAN or internet line after the caption.
    SubTitle->ClearContent();
    {
        JustifyNode const Justify(TextJustifyLeft);
        SubTitle->AppendNode(Justify);
    }
    {
        RDLRefNode const Ref(SubTitleText);
        SubTitle->AppendNode(Ref);
    }
    {
        ParagraphNode const Paragraph;
        SubTitle->AppendNode(Paragraph);
    }
    SubTitle->AppendContent(G_bInternetServerList != 0 ? SubTitleInternet : SubTitleLan);
    Refresh->bInteractive = 1; // 0x0056ec63

    // 0x0056ec6a..0x0056ed21: every row below the header emptied, its payload dropped, neither
    // selectable nor hoverable.
    int const RowCount = EdgeCount(ServerTable->RowEdges);
    for (int Row = 1; Row < RowCount; ++Row)
    {
        for (unsigned Column = 0; Column < ServerTable->ColEdges.size(); ++Column)
            ServerTable->ClearCell(static_cast<unsigned>(Row), Column);
        FUiFastTableRow* const RowRecord = ServerTable->GetRow(static_cast<unsigned>(Row));
        if (RowRecord != nullptr)
            RowRecord->Payload.Assign(nullptr); // FUN_00575520(0)
        ServerTable->GetRow(static_cast<unsigned>(Row))->bSelectable = 0;
        ServerTable->GetRow(static_cast<unsigned>(Row))->bHoverable = 0;
    }

    RefreshDetailsText(); // 0x0056ed29

    // 0x0056ed2e..0x0056ee50: the status line, right aligned; FUN_0056fc90's body inline.
    ListServerStatus->SetAlignMode(2); // FUN_00579a60(2)
    RefreshListStatus();

    // 0x0056ee52..0x0056efe2: the legend -- blue lock, its caption, a space, blue skull, its
    // caption.
    Legend->ClearContent();
    {
        StyleNode const Style(0x76ec);
        Legend->AppendNode(Style);
    }
    {
        JustifyNode const Justify(TextJustifyCenter);
        Legend->AppendNode(Justify);
    }
    {
        ImageNode const Image(BlueLock);
        Legend->AppendNode(Image);
    }
    {
        RDLRefNode const Ref(LegendLockText);
        Legend->AppendNode(Ref);
    }
    {
        TextNode const Space(L" ", -1); // u_ 0x005e2c58
        Legend->AppendNode(Space);
    }
    {
        ImageNode const Image(BlueSkull);
        Legend->AppendNode(Image);
    }
    {
        RDLRefNode const Ref(LegendSkullText);
        Legend->AppendNode(Ref);
    }

    // 0x0056efe4..0x0056f0b7: the slide starts at -1.5; the table, scroll bar, legend, details,
    // title, subtitle and status hidden; the buttons' surfaces frozen; the boxes' homes kept.
    SlideT = -1.5;
    bLeaving = 0;
    LeaveDirection = 0;
    HideWin3D(ServerTable);
    HideWin3D(ServerScroll);
    HideWin3D(Legend);
    HideWin3D(ServerDetails);
    HideWin3D(Title);
    HideWin3D(SubTitle);
    HideWin3D(ListServerStatus);
    Connect->SetSkipUpload(true); // FUN_00579a90(1)
    MainMenu->SetSkipUpload(true);
    SetFilter->SetSkipUpload(true);
    Refresh->SetSkipUpload(true);
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        InfoBoxHome[Axis] = ServerInfoBox->Position[Axis];
        SelectBoxHome[Axis] = ServerSelectBox->Position[Axis];
    }

    // 0x0056f0ba..0x0056f157: FUN_0056f610's body inline once more.
    RefreshDetailsText();
    return true;
}

void PageServerSelect::SetCell(unsigned Row, unsigned Column, RenderDisplayList const* Content,
                               bool bVersionMatch, bool bSelected)
{
    // FUN_0056f180. FUN_0058cd00; false -> nothing.
    FUiFastTableCell* Cell = nullptr;
    if (!ServerTable->GetCell(Row, Column, &Cell))
        return;
    RdlClear(Cell->Source); // FUN_0046c860 over the cell's list

    bool bSortArrow = false;
    if (Row == 0)
    {
        // 0x0056f1ec..0x0056f2f4: the header -- the sort column's style (and its arrow below),
        // centred.
        if (Column == SortColumn)
        {
            StyleNode const Style(0xd190);
            AppendNodeClone(Cell->Source, Style);
            bSortArrow = true;
        }
        else
        {
            StyleNode const Style(0xd824);
            AppendNodeClone(Cell->Source, Style);
        }
        JustifyNode const Justify(TextJustifyCenter);
        AppendNodeClone(Cell->Source, Justify);
    }
    else
    {
        // 0x0056f2f9..0x0056f46d: grey for another version, else selected / normal; column 0
        // left, the rest centred.
        unsigned short const StyleId =
            !bVersionMatch ? static_cast<unsigned short>(0xbb80)
                           : (bSelected ? static_cast<unsigned short>(0x3343)
                                        : static_cast<unsigned short>(0x69f5));
        {
            StyleNode const Style(StyleId);
            AppendNodeClone(Cell->Source, Style);
        }
        JustifyNode const Justify(Column == 0 ? TextJustifyLeft : TextJustifyCenter);
        AppendNodeClone(Cell->Source, Justify);
    }

    // 0x0056f483..0x0056f4ac: a clone of every Content node (FUN_0046c800 per node).
    ChatAppendRdlCopy(Cell->Source, *Content);

    // 0x0056f4ae..0x0056f5e0: the arrow -- DownPip ascending, UpPip descending.
    if (bSortArrow)
    {
        ImageNode const Arrow(bAscending[Column] != 0 ? DownPip : UpPip);
        AppendNodeClone(Cell->Source, Arrow);
    }
    ServerTable->InvalidateCell(Row, Column); // FUN_0058cdb0
}

void PageServerSelect::RefreshDetailsText()
{
    // FUN_0056f610.
    ServerDetails->ClearContent();
    {
        StyleNode const Style(0x4433);
        ServerDetails->AppendNode(Style);
    }
    {
        RDLRefNode const Ref(DetailsHeader);
        ServerDetails->AppendNode(Ref);
    }
    ParagraphNode const Paragraph;
    ServerDetails->AppendNode(Paragraph);
}

void PageServerSelect::RefreshListStatus()
{
    // FUN_0056fc90: Refresh stays interactive; the status text right-justified.
    Refresh->bInteractive = 1;
    ListServerStatus->ClearContent();
    {
        JustifyNode const Justify(TextJustifyRight);
        ListServerStatus->AppendNode(Justify);
    }
    if (ListStatus == 1 || ListStatus == 4)
    {
        RDLRefNode const Ref(StatusText60057);
        ListServerStatus->AppendNode(Ref);
    }
    else if (ListStatus == 2)
    {
        RDLRefNode const Ref(StatusText60055);
        ListServerStatus->AppendNode(Ref);
        Refresh->bInteractive = 1; // 0x0056fd84
    }
    else
    {
        RDLRefNode const Ref(StatusText60058);
        ListServerStatus->AppendNode(Ref);
    }
}

void PageServerSelect::RebuildTable()
{
    // FUN_0056fdf0. The comparator for the sort column and direction (none past column 7).
    FServerCompare Compare = nullptr;
    if (SortColumn < 8)
        Compare = bAscending[SortColumn] != 0 ? kAscendingCompare[SortColumn]
                                              : kDescendingCompare[SortColumn];
    std::vector<FServerInfo*> Servers;
    G_LoginMgr.GetFilteredServers(Servers, Compare, &Filter); // FUN_005aca40
    ServerTable->ClearSelection();                            // FUN_0058d4a0

    // 0x0056fe72..0x0056ff37: the scroll range over the server count, a page of rows - 1;
    // FirstVisible from the scroll value; the bar shown (unless leaving) only when the list
    // does not fit.
    int const ServerCount = static_cast<int>(Servers.size());
    int const RowCount = EdgeCount(ServerTable->RowEdges);
    ServerScroll->SetRange(0.0f, static_cast<float>(ServerCount));
    int const VisibleRows = RowCount - 1;
    ServerScroll->SetPage(static_cast<float>(VisibleRows));
    ServerScroll->SetStep(1.0f);
    FirstVisible = static_cast<int>(ServerScroll->GetValue()); // ftol
    if (ServerCount > VisibleRows && bLeaving == 0)
        ShowWin3D(ServerScroll);
    else
        HideWin3D(ServerScroll);

    // 0x0056ff3a..0x0056ff83: the visible servers from row 1; the remembered one selected.
    int Row = 1;
    for (int Index = FirstVisible; Index < ServerCount; ++Index, ++Row)
    {
        if (Row >= RowCount)
            return;
        FillRow(static_cast<unsigned>(Row), Servers[static_cast<size_t>(Index)]);
        if (Servers[static_cast<size_t>(Index)]->Id == SelectedServerId)
            ServerTable->SelectRow(static_cast<unsigned>(Row)); // FUN_0058d4b0
    }

    // 0x0056ff85..0x00570012: the rows left over emptied (their payload released inline).
    for (; Row < RowCount; ++Row)
    {
        for (unsigned Column = 0; Column < ServerTable->ColEdges.size(); ++Column)
            ServerTable->ClearCell(static_cast<unsigned>(Row), Column);
        FUiFastTableRow* const RowRecord = ServerTable->GetRow(static_cast<unsigned>(Row));
        if (RowRecord != nullptr && RowRecord->Payload.Object != nullptr)
            RowRecord->Payload.Reset(); // FUN_00599980, then +8 = 0
        ServerTable->GetRow(static_cast<unsigned>(Row))->bSelectable = 0;
        ServerTable->GetRow(static_cast<unsigned>(Row))->bHoverable = 0;
    }
}

void PageServerSelect::FillRow(unsigned Row, FServerInfo const* Server)
{
    // FUN_00570040. A server of another version is greyed and inert.
    bool const bVersionMatch =
        Server->Version == static_cast<unsigned>(g_client.packed_version);
    bool const bSelected = Server->Id == SelectedServerId;

    // 0x005700a1..0x0057013f: column 1, the address.
    {
        RenderDisplayList Text;
        TextNode const Node(Server->Address.c_str(), -1);
        AppendNodeClone(Text, Node);
        SetCell(Row, 1, &Text, bVersionMatch, bSelected);
    }

    // 0x00570144..0x00570a7e: column 3, the ping -- "????" for -2, "-" for -1 (both SetCell
    // inlined, FUN_005754b0 inserting the clones), else the number through a wide
    // stringstream (operator<<(unsigned), ends).
    {
        int const Ping = Server->Ping;
        if (Ping == -2 || Ping == -1)
        {
            RenderDisplayList Text;
            RDLRefNode const Ref(Ping == -2 ? PingUnknown : PingNone);
            AppendNodeClone(Text, Ref);
            SetCell(Row, 3, &Text, bVersionMatch, bSelected);
        }
        else
        {
            std::wstringstream Stream;
            Stream << static_cast<unsigned>(Ping) << std::ends;
            std::wstring const PingText = Stream.str();
            RenderDisplayList Text;
            TextNode const Node(PingText.c_str(), -1);
            AppendNodeClone(Text, Node);
            SetCell(Row, 3, &Text, bVersionMatch, bSelected);
        }
    }

    // 0x00570a83..0x00570aae: column 2, a known login (FUN_005a9950).
    SetCell(Row, 2, G_LoginMgr.IsKnownLogin(*Server) ? &TextYes : &TextNo, bVersionMatch,
            bSelected);

    // 0x00570ab3..0x00570b58: column 0, the name.
    {
        RenderDisplayList Text;
        TextNode const Node(Server->Name.c_str(), -1);
        AppendNodeClone(Text, Node);
        SetCell(Row, 0, &Text, bVersionMatch, bSelected);
    }

    // 0x00570b5d..0x00570cf9: column 4, "players/max".
    {
        std::wstringstream Stream;
        Stream << static_cast<unsigned>(Server->PlayerCount) << L"/"
               << static_cast<unsigned>(Server->MaxPlayers) << std::ends;
        std::wstring const PlayersText = Stream.str();
        RenderDisplayList Text;
        TextNode const Node(PlayersText.c_str(), -1);
        AppendNodeClone(Text, Node);
        SetCell(Row, 4, &Text, bVersionMatch, bSelected);
    }

    // 0x00570cff..0x00570f3c: column 5, "major.minor" (bytes +0x8f, +0x8e as unsigned long),
    // behind a TRANode (colour 0x1d1dbf, mask 0xffffff00) when it is not the client's.
    {
        std::wstringstream Stream;
        Stream << static_cast<unsigned long>((Server->Version >> 24) & 0xffu) << L"."
               << static_cast<unsigned long>((Server->Version >> 16) & 0xffu) << std::ends;
        RenderDisplayList Text;
        if (Server->Version != static_cast<unsigned>(g_client.packed_version))
        {
            TextRenderAttributes const Attrs = PackTextAttributes(0, 0, 0, 0, 0x1d1dbf);
            TRANode const Tra(Attrs, 0xffffff00u, 0);
            AppendNodeClone(Text, Tra);
        }
        std::wstring const VersionText = Stream.str();
        TextNode const Node(VersionText.c_str(), -1);
        AppendNodeClone(Text, Node);
        SetCell(Row, 5, &Text, bVersionMatch, bSelected);
    }

    // 0x00570f42..0x00570f61: column 6, +0x3e.
    SetCell(Row, 6, Server->Field_0x3e != 0 ? &TextYes : &TextNo, bVersionMatch, bSelected);

    // 0x00570f66..0x00571180: column 7, the lock (+0x94) and the skull (+0x96) -- plain when
    // selected, blue for this version, grey otherwise.
    {
        RenderDisplayList Icons;
        if (Server->bPassword != 0)
        {
            ImageNode const Image(bSelected ? Locked : (bVersionMatch ? BlueLock : GrayLock));
            AppendNodeClone(Icons, Image);
        }
        if (Server->Field_0x96 != 0)
        {
            ImageNode const Image(bSelected ? Skull : (bVersionMatch ? BlueSkull : GraySkull));
            AppendNodeClone(Icons, Image);
        }
        SetCell(Row, 7, &Icons, bVersionMatch, bSelected);
    }

    // 0x00571191..0x005711e8: the row selectable and hoverable for this version only; its
    // payload a fresh ServerRowData (FUN_00571280) taken by FUN_00575520, the creation
    // reference dropped (FUN_00599980). No null test on the row, as retail.
    FUiFastTableRow* const RowRecord = ServerTable->GetRow(Row);
    RowRecord->bSelectable = bVersionMatch ? 1 : 0;
    RowRecord->bHoverable = bVersionMatch ? 1 : 0;
    ServerRowData* const Data = new ServerRowData(*Server);
    RowRecord->Payload.Assign(Data);
    Data->Release();
}

bool PageServerSelect::Slot23(unsigned Msg, unsigned A, unsigned B)
{
    // FUN_005714d0.
    unsigned const TableId = static_cast<unsigned>(reinterpret_cast<uintptr_t>(ServerTable));
    if (Msg == 0x3004)
    {
        PlayOrRewindUiSoundSlot(0); // FUN_005645c0(0)
        if (A == 0x3e9)
        {
            // 0x00571609..0x005716f1: "MainMenu" -- leave back.
            BeginLeave();
            LeaveDirection = -1;
            return false;
        }
        if (A == 0x3ea)
        {
            // 0x00571517..0x00571606: "SetFilter" -- the tool tips off while the filter dialog
            // runs (FUN_005637b0 on +0x2a4); a WM_QUIT there quits the process (exit(0)).
            SetToolTipsEnabled(false);
            if (!ServerFilterDialogRun(this, &Filter))
                exit(0);
            SetToolTipsEnabled(true);
            RebuildTable();
            return false;
        }
        if (A == 0x3eb)
        {
            // 0x005716f4..0x00571770: "Refresh" -- only before the first list request
            // (CLoginMgr +0x23c, DAT_0067e9f4); the rows emptied, the details reset, the lists
            // reset and requested again.
            if (G_LoginMgr.Field_0x23c != 0)
                return false;
            int const RowCount = EdgeCount(ServerTable->RowEdges);
            for (int Row = 1; Row < RowCount; ++Row)
            {
                ClearRow(static_cast<unsigned>(Row));
                ServerTable->GetRow(static_cast<unsigned>(Row))->bSelectable = 0;
                ServerTable->GetRow(static_cast<unsigned>(Row))->bHoverable = 0;
            }
            RefreshDetailsText();
            ServerDetails->SetScrollOrigin(0, 0); // FUN_005798b0(0, 0)
            G_LoginMgr.ResetServerLists();        // FUN_005abc80
            G_LoginMgr.RequestServerList();       // FUN_005ac240
            return false;
        }
        if (A != 0x3003)
            return false;

        // 0x00571783..0x005718d1: "Connect".
        FServerInfo const* const Server = GetSelectedServer();
        if (Server == nullptr)
            return true;
        if (!ConnectToServer(Server))
            return true;
        BeginLeave();
        LeaveDirection = 1;
        return true;
    }

    if (Msg == 0xf001)
    {
        // 0x005718fb..0x00571988: a header click on the table -- the same column flips its
        // direction, another becomes the sort column; the captions redrawn, the list rebuilt.
        if (A != TableId || (B & 0xffff0000u) != 0)
            return false;
        unsigned const Column = B & 0xffffu;
        if (Column >= 8)
            return false;
        if (Column == SortColumn)
            bAscending[SortColumn] = bAscending[SortColumn] == 0 ? 1 : 0;
        else
            SortColumn = Column;
        for (unsigned Index = 0; Index < 8; ++Index)
            SetCell(0, Index, &ColumnHeaders[Index], false, false);
        ServerTable->GetRow(0)->bHoverable = 0;
        RebuildTable();
        return true;
    }

    if (Msg == 0x300d)
    {
        // 0x0057198b..0x00571a90: a row selected -- its details (the vertical scroll reset
        // for another server), the new row redrawn as selected, the row of the server selected
        // before redrawn as not.
        if (A != TableId)
            return false;
        RefreshDetailsText();
        unsigned const PreviousId = SelectedServerId;
        SelectedServerId = 0xffffffffu;
        unsigned SelectedRow = 0;
        if (!ServerTable->GetSelectedRow(&SelectedRow))
            return true;
        ServerRowData* const Data =
            static_cast<ServerRowData*>(ServerTable->GetRow(SelectedRow)->Payload.Object);
        if (Data == nullptr)
            return true;
        ShowServerDetails(&Data->Info, Data->Info.Id != PreviousId);
        SelectedServerId = Data->Info.Id;
        FillRow(SelectedRow, &Data->Info);
        for (unsigned Row = 1; Row < ServerTable->RowEdges.size(); ++Row)
        {
            ServerRowData* const Other =
                static_cast<ServerRowData*>(ServerTable->GetRow(Row)->Payload.Object);
            if (Other != nullptr && Other->Info.Id == PreviousId)
            {
                FillRow(Row, &Other->Info);
                return true;
            }
        }
        return true;
    }

    if (Msg == 0xf002)
    {
        // 0x00571a93..0x00571b0a: a double click on the table -- connect and leave.
        if (A != TableId)
            return false;
        FServerInfo const* const Server = GetSelectedServer();
        if (Server == nullptr)
            return true;
        if (!ConnectToServer(Server))
            return true;
        BeginLeave(); // FUN_00571fc0
        LeaveDirection = 1;
        return true;
    }

    if (Msg == 0xf003)
    {
        // 0x00571b26..0x00571b3c: back to the main menu at once.
        Result = 1;
        MouseSetCursor("Arrow", true);
        return true;
    }

    if (Msg == 0xf005)
    {
        // 0x0057197b: the scroll bar moved.
        RebuildTable();
        return true;
    }
    return false;
}

FServerInfo const* PageServerSelect::GetSelectedServer()
{
    // 0x00571783..0x005717d3 / 0x00571a9f..0x00571ae5: the selected row's payload (a row with
    // none gives nothing), else the remembered id through FUN_005abdb0.
    unsigned SelectedRow = 0;
    if (ServerTable->GetSelectedRow(&SelectedRow))
    {
        ServerRowData* const Data =
            static_cast<ServerRowData*>(ServerTable->GetRow(SelectedRow)->Payload.Object);
        if (Data == nullptr)
            return nullptr;
        return &Data->Info;
    }
    if (SelectedServerId == 0xffffffffu)
        return nullptr;
    return G_LoginMgr.FindServer(SelectedServerId);
}

void PageServerSelect::ClearRow(unsigned Row)
{
    // FUN_00571b50: every cell (the column count read again each pass), then the payload
    // released inline (FUN_00599980's body) and cleared.
    for (unsigned Column = 0; Column < ServerTable->ColEdges.size(); ++Column)
        ServerTable->ClearCell(Row, Column);
    FUiFastTableRow* const RowRecord = ServerTable->GetRow(Row);
    if (RowRecord != nullptr && RowRecord->Payload.Object != nullptr)
        RowRecord->Payload.Reset();
}

void PageServerSelect::ShowServerDetails(FServerInfo const* Server, bool bNewServer)
{
    // FUN_00571bd0.
    ServerDetails->ClearContent();
    if (bNewServer)
    {
        // 0x00571c01..0x00571c23: back to the top, the horizontal scroll kept.
        int ScrollX = 0;
        int ScrollY = 0;
        ServerDetails->GetScrollOrigin(&ScrollX, &ScrollY); // FUN_00579970
        ServerDetails->SetScrollOrigin(ScrollX, 0);         // FUN_005798b0
    }

    // 0x00571c28..0x00571d4d: the caption, then the server's description (+0x54) as text
    // (FUN_0057db50, TextNode form).
    {
        StyleNode const Style(0x4433);
        ServerDetails->AppendNode(Style);
    }
    {
        RDLRefNode const Ref(DetailsHeader);
        ServerDetails->AppendNode(Ref);
    }
    {
        ParagraphNode const Paragraph;
        ServerDetails->AppendNode(Paragraph);
    }
    {
        StyleNode const Style(0x76ec);
        ServerDetails->AppendNode(Style);
    }
    RenderDisplayList Description;
    AppendTextAsRdl(Server->Field_0x54.c_str(), static_cast<unsigned>(Server->Field_0x54.size()),
                    Description, false);
    ServerDetails->AppendContent(Description);
    {
        ParagraphNode const Paragraph;
        ServerDetails->AppendNode(Paragraph);
    }

    // 0x00571d52..0x00571ed5: the restrictions -- shown unless +0x95 is set and there is no
    // password: +0x95 clear adds 0x74f, a password adds 0x750.
    if (Server->Field_0x95 != 0 && Server->bPassword == 0)
        return;
    {
        StyleNode const Style(0x4433);
        ServerDetails->AppendNode(Style);
    }
    {
        RDLRefNode const Ref(DetailsRestricted);
        ServerDetails->AppendNode(Ref);
    }
    {
        ParagraphNode const Paragraph;
        ServerDetails->AppendNode(Paragraph);
    }
    {
        StyleNode const Style(0x76ec);
        ServerDetails->AppendNode(Style);
    }
    if (Server->Field_0x95 == 0)
    {
        {
            RDLRefNode const Ref(DetailsField95);
            ServerDetails->AppendNode(Ref);
        }
        ParagraphNode const Paragraph;
        ServerDetails->AppendNode(Paragraph);
    }
    if (Server->bPassword != 0)
    {
        {
            RDLRefNode const Ref(DetailsPassword);
            ServerDetails->AppendNode(Ref);
        }
        ParagraphNode const Paragraph;
        ServerDetails->AppendNode(Paragraph);
    }
}

void PageServerSelect::SetToolTipsEnabled(bool bEnabled)
{
    // FUN_00571f50: the Legend's tip is always switched off.
    ServerTable->SetToolTipEnabled(bEnabled); // FUN_00579890
    Connect->SetToolTipEnabled(bEnabled);
    MainMenu->SetToolTipEnabled(bEnabled);
    SetFilter->SetToolTipEnabled(bEnabled);
    ServerDetails->SetToolTipEnabled(bEnabled);
    Legend->SetToolTipEnabled(false);
    Refresh->SetToolTipEnabled(bEnabled);
    ListServerStatus->SetToolTipEnabled(bEnabled);
}

void PageServerSelect::BeginLeave()
{
    // FUN_00571fc0 (and its inline copies at 0x00571609 / 0x005717e9): leaving from SlideT 0,
    // the table side hidden, the four buttons inert and frozen, the slide sound, the tool tips
    // off (FUN_00571f50(0)'s body inline).
    bLeaving = 1;
    SlideT = 0.0;
    HideWin3D(ServerTable);
    HideWin3D(ServerScroll);
    HideWin3D(Legend);
    HideWin3D(ServerDetails);
    HideWin3D(Title);
    HideWin3D(SubTitle);
    HideWin3D(ListServerStatus);
    HideWin3D(Version);
    Connect->bInteractive = 0; // +0x498
    MainMenu->bInteractive = 0;
    SetFilter->bInteractive = 0;
    Refresh->bInteractive = 0;
    Connect->SetSkipUpload(true); // FUN_00579a90(1)
    MainMenu->SetSkipUpload(true);
    SetFilter->SetSkipUpload(true);
    Refresh->SetSkipUpload(true);
    PlayUiSoundSlot(0x1b); // FUN_00564570(0x1b)
    SetToolTipsEnabled(false);
}

bool PageServerSelect::ConnectToServer(FServerInfo const* Server)
{
    // FUN_00572100.
    if (!G_LoginMgr.SetCurrentServer(*Server)) // FUN_005a9c30
    {
        FDUMP(0x100001, "%s(%d) : ERROR:General:%s", kIntroCpp, 0x14cb,
              "Internal error: invalid server data.\n");
        return false;
    }

    // 0x00572130..0x005721fd: an empty password (DAT_005c8ce0); a locked server asks for one
    // (IDS 0x60071, OK tool tip 0xd48); cancelling ends the connect.
    std::wstring Password(L"");
    if (Server->bPassword != 0 && !NnPromptRunTextEntry(0x60071, &Password, false, 0xd48))
    {
        FDUMP(0x100003, "%s(%d) : NOTICE:General:%s", kIntroCpp, 0x14b4,
              "Password cancelled.\n");
        return false;
    }

    // 0x00572200..0x005722a3: CLoginMgr +0x130 = the password, then ConnectToServer on the ini
    // path (DAT_00679d04); a failure recreates the Gun.
    G_LoginMgr.ServerPassword = Password;
    if (::ConnectToServer(g_display.ini_path.c_str()))
        return true;
    G_LoginMgr.CreateGun(); // FUN_005ab4c0
    FDUMP(0x100003, "%s(%d) : NOTICE:General:%s", kIntroCpp, 0x14c5,
          "Failed to connect to the server.\n");
    return false;
}

void PageServerSelect::Update()
{
    // FUN_0056f6e0. 0x0056f6e6..0x0056f6fa: the list asked for until CLoginMgr takes it.
    if (bListRequested == 0)
        bListRequested = G_LoginMgr.RequestServerList() ? 1 : 0;

    // 0x0056f700..0x0056f7b3: once in (FCOMP 1.0, TEST AH,0x41 / JP -- past 1.0 or unordered)
    // or leaving, and not while a leave with a direction runs: the list pumped (a change
    // rebuilds the table), the status line kept current, Connect live only with a server.
    if ((!(SlideT <= 1.0) || bLeaving != 0) && (bLeaving == 0 || LeaveDirection == 0))
    {
        if (G_LoginMgr.PumpServerList()) // FUN_005ac9d0
            RebuildTable();
        int const Status = G_LoginMgr.GetGunStatus(); // FUN_005abc40
        if (Status != ListStatus)
        {
            ListStatus = Status;
            RefreshListStatus();
        }
        unsigned SelectedRow = 0;
        if (!ServerTable->GetSelectedRow(&SelectedRow) && SelectedServerId == 0xffffffffu)
        {
            Connect->Flags = static_cast<unsigned char>(Connect->Flags & 0xfe);
            Connect->bInteractive = 0;
        }
        else
        {
            Connect->Flags = static_cast<unsigned char>(Connect->Flags | 1);
            Connect->bInteractive = 1;
        }
    }

    // 0x0056f7ba..0x0056faa0: the slide in, unless past 1.0 (or unordered) or leaving.
    if (SlideT <= 1.0 && bLeaving == 0)
    {
        // 0x0056f7df..0x0056f823: the frame time, capped at 0.5 (FCOM 0.5, TEST AH,0x41 / JNZ
        // keeps it when not greater), 1.5 per second.
        double Dt = GetClientFrameTimeCapped();
        if (Dt > 0.5)
            Dt = 0.5;
        SlideT = Dt * 1.5 + SlideT;

        // 0x0056f81d..0x0056f82e: FCOM 0.0, TEST AH,0x41 / JP -- in place once past 0.
        if (SlideT <= 0.0)
        {
            // 0x0056f834..0x0056f8f6: MainMenu / SetFilter offset (t, 0, 0), Connect /
            // Refresh (-t, 0, 0) through slot 42 property 6; the info box at home - t, the
            // select box at home + t (FUN_00423b30, slot 5).
            float Right[3] = {static_cast<float>(SlideT), 0.0f, 0.0f};
            float Left[3] = {static_cast<float>(-SlideT), 0.0f, 0.0f};
            MainMenu->SetProperty(6, reinterpret_cast<uintptr_t>(Right), 0);
            SetFilter->SetProperty(6, reinterpret_cast<uintptr_t>(Right), 0);
            Connect->SetProperty(6, reinterpret_cast<uintptr_t>(Left), 0);
            Refresh->SetProperty(6, reinterpret_cast<uintptr_t>(Left), 0);
            float BoxPosition[3];
            Win3DVec3Add(BoxPosition, InfoBoxHome, Left);
            ServerInfoBox->SetPosition(BoxPosition);
            Win3DVec3Add(BoxPosition, SelectBoxHome, Right);
            ServerSelectBox->SetPosition(BoxPosition);
        }
        else
        {
            // 0x0056f8fb..0x0056fa69: everything in place, shown and live; SlideT parked at
            // 2.0.
            float ZeroOffset[3] = {0.0f, 0.0f, 0.0f};
            MainMenu->SetProperty(6, reinterpret_cast<uintptr_t>(ZeroOffset), 0);
            SetFilter->SetProperty(6, reinterpret_cast<uintptr_t>(ZeroOffset), 0);
            Connect->SetProperty(6, reinterpret_cast<uintptr_t>(ZeroOffset), 0);
            Refresh->SetProperty(6, reinterpret_cast<uintptr_t>(ZeroOffset), 0);
            ServerInfoBox->SetPosition(InfoBoxHome);
            ServerSelectBox->SetPosition(SelectBoxHome);
            ShowWin3D(ServerTable);
            ShowWin3D(Legend);
            ShowWin3D(ServerDetails);
            ShowWin3D(Title);
            ShowWin3D(SubTitle);
            ShowWin3D(ListServerStatus);
            ShowWin3D(Version);
            Connect->SetSkipUpload(false); // FUN_00579a90(0)
            MainMenu->SetSkipUpload(false);
            SetFilter->SetSkipUpload(false);
            Refresh->SetSkipUpload(false);
            ServerTable->SetToolTipEnabled(true); // FUN_00579890
            Connect->SetToolTipEnabled(true);
            MainMenu->SetToolTipEnabled(true);
            SetFilter->SetToolTipEnabled(true);
            ServerDetails->SetToolTipEnabled(true);
            Legend->SetToolTipEnabled(false);
            Refresh->SetToolTipEnabled(true);
            ListServerStatus->SetToolTipEnabled(true);
            SlideT = 2.0;
        }

        // 0x0056fa73..0x0056faa0: the slide sound while -0.8 < SlideT < -0.1 (FCOMP -0.8,
        // TEST AH,0x41 / JNZ; FCOMP -0.1, TEST AH,5 / JP).
        if (SlideT > -0.8 && SlideT < -0.1)
            PlayUiSoundSlot(0x1b); // FUN_00564570(0x1b)
    }

    // 0x0056faa3..0x0056fc56: leaving with a direction -- SlideT runs down at 1.5 per second
    // (no cap), the buttons and boxes slide out the other way.
    if (bLeaving != 0 && LeaveDirection != 0)
    {
        SlideT = SlideT - GetClientFrameTimeCapped() * 1.5;
        float Right[3] = {static_cast<float>(SlideT), 0.0f, 0.0f};
        float Left[3] = {static_cast<float>(-SlideT), 0.0f, 0.0f};
        MainMenu->SetProperty(6, reinterpret_cast<uintptr_t>(Right), 0);
        SetFilter->SetProperty(6, reinterpret_cast<uintptr_t>(Right), 0);
        Connect->SetProperty(6, reinterpret_cast<uintptr_t>(Left), 0);
        Refresh->SetProperty(6, reinterpret_cast<uintptr_t>(Left), 0);
        float BoxPosition[3];
        Win3DVec3Add(BoxPosition, InfoBoxHome, Left);
        ServerInfoBox->SetPosition(BoxPosition);
        Win3DVec3Add(BoxPosition, SelectBoxHome, Right);
        ServerSelectBox->SetPosition(BoxPosition);

        // 0x0056fbd2..0x0056fbe3: FCOMP -1.5 (DAT_005e2bb0), TEST AH,0x41 / JP -- done once
        // SlideT <= -1.5.
        if (SlideT <= -1.5)
        {
            HideWin3D(MainMenu);
            HideWin3D(SetFilter);
            HideWin3D(Connect);
            HideWin3D(Refresh);
            HideWin3D(ServerInfoBox);
            HideWin3D(ServerSelectBox);
            HideWin3D(Version);
            if (LeaveDirection == 1)
            {
                Result = 2;
                LoadingScreenStart(); // FUN_00579290
            }
            else if (LeaveDirection == -1)
            {
                Result = 1;
            }
            bLeaving = 0;
        }
    }

    // 0x0056fc5d..0x0056fc7d: Win3D's child walk (FUN_005a17b0 inlined).
    Win3D::Update();
}

bool PageServerSelect::OnEvent(unsigned MsgId)
{
    // LAB_00572330 (this = the +0x80 face): event 0xf goes back to the main menu unless a leave
    // runs; anything else is FUN_0053f330 (false).
    if (MsgId != 0xf)
        return UIPage::MasterWin::OnEvent(MsgId);
    if (bLeaving == 0)
    {
        Result = 1;
        MouseSetCursor("Arrow", true);
    }
    return true;
}

PageServerSelect& GetPageServerSelect()
{
    return G_PageServerSelect;
}
