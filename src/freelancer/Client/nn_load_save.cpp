// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail NN_LoadSave (NN_LoadSave.cpp). See nn_load_save.h.

#include "nn_load_save.h"

#include "client_state.h"          // QueueLoadGameCharacter (FUN_005b2c70)
#include "fade_to_black.h"         // FadeIn (FUN_004120d0)
#include "hud_command_dispatch.h"  // HudPostCommand (FUN_005763b0)
#include "ids_strings.h"           // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "input_manager.h"         // Get/SetClientMouseCursorDrawFlag (FUN_0041dd90 / FUN_0041dda0)
#include "login_mgr.h"             // G_LoginMgr (DAT_0067e7b8), G_Server (DAT_0067ecd0)
#include "nn_button_control.h"     // NnButtonControl::SetMontageMode (FUN_0055bfc0)
#include "nn_confirm.h"            // NnConfirmOpen (FUN_004889e0)
#include "nn_error.h"              // NnErrorOpen (FUN_00473c00)
#include "nn_modal.h"              // NnModalPauseGame / NnModalResumeGame, NnFloatBits
#include "nn_text_control.h"       // NnTextControl (+0x381 / +0x385)
#include "nn_ui_pulse.h"           // G_HudChromeColorRgba (DAT_00679bb4), G_NnPaletteArgb_679ba4
#include "player_spawn.h"          // GetPlayerConnectionId (DAT_00673344)
#include "rich_text_win.h"         // RichTextEditWin
#include "ui_scroll.h"             // UiScroll::SetRange (FUN_00595d60)
#include "win3d.h"
#include "win3d_renderables.h"     // Win3DFlushBeforeOverlayDraw (FUN_0045a9f0)
#include "win_factory.h"

#include "common_archetype.h"      // Archetype::GetShip
#include "common_misc.h"           // filetime_to_wstring
#include "common_server.h"         // IServer::SaveGame
#include "common_universe.h"       // Universe::get_system
#include "dacom.h"                 // FDUMP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

// Retail DAT_00671f14 -- WINFACTORY<NN_LoadSave> static (LAB_00485930 -> FUN_00488ca0; Create
// FUN_00488d10; atexit LAB_00485950 -> FUN_00579380).
WINFACTORY<NnLoadSave> G_NnLoadSaveFactory("NN_LoadSave");

wchar_t G_NnLoadSaveText[0x100];

namespace
{
    char const kNnLoadSaveCpp[] = "E:\\FL\\Scratch\\Source\\Client\\nn\\NN_LoadSave.cpp"; // 005d1e40
    char const kBlackPlaneMesh[] = "Interface\\NeuroNet\\MISC_bigblackplane.3db";         // 005d21b0
    char const kFrameMesh[] = "Interface\\LoadSave\\LS_background.cmp";                   // 005d2174
    char const kButtonMesh[] = "Interface\\LoadSave\\FRONT_button.cmp";                   // 005d2130
    char const kInfoFrameMesh[] = "Interface\\LoadSave\\FRONT_infowindow.cmp";            // 005d2018
    char const kDescripButtonMesh[] = "Interface\\LoadSave\\LS_bottomframe.3db";          // 005d1ecc
    char const kCloseMesh[] = "Interface\\NeuroNet\\Inventory\\x.3db";                    // 005d01d0
    // DAT_005c8ce0 -- an empty wide string.
    wchar_t const kEmptyText[] = L"";

    // Win3D +0x6c bits 0..1 (FUN_005a0c30's pair): set = shown.
    void ShowControl(Win3D* Control)
    {
        Control->Flags = static_cast<unsigned char>(Control->Flags | 3);
    }

    void HideControl(Win3D* Control)
    {
        Control->Flags = static_cast<unsigned char>(Control->Flags & 0xfc);
    }
}

NnLoadSave::NnLoadSave()
    : bCursorDrawFlag(0)
    , Mode(0)
    , ConfirmMode(0)
    , bExitToMainMenu(0)
    , bSaveAllowed(1)
    , bListRefreshed(1)
    , Pad33f(0)
    , Byte340(0)
    , SaveNamePrefixLength(0)
    , NextSaveNumber(0)
    , Title(nullptr)
    , VisibleRows(0)
    , Scroll(nullptr)
    , DescripText(nullptr)
    , DescripButton(nullptr)
    , DescripEdit(nullptr)
    , CloseButton(nullptr)
{
    // FUN_00485960: WinControl ctor FUN_0059f860; +0x340 (allocator byte), the vector, +0x350
    // (allocator byte) and the string; +0x368 = 0 (the CHARACTER_ID ctor); ListBox ctor
    // FUN_005a2100 at +0x5dc and +0xe98; +0x33d = +0x33e = 1, +0x334 = 0, +0x338 = 0,
    // +0x33c = 0; the three vptrs. Everything else is written by Create before it is read;
    // the port zeroes it here.
    Pad331[0] = 0;
    Pad331[1] = 0;
    Pad331[2] = 0;
    SaveName[0] = 0;
    for (int Index = 0; Index < 5; ++Index)
    {
        Buttons[Index] = nullptr;
        ButtonTexts[Index] = nullptr;
        bButtonEnabled[Index] = 0;
    }
    Pad5d9[0] = 0;
    Pad5d9[1] = 0;
    Pad5d9[2] = 0;
    for (int Index = 0; Index < 3; ++Index)
    {
        NameListOffset[Index] = 0.0f;
        TimeListOffset[Index] = 0.0f;
    }
    for (int Index = 0; Index < 4; ++Index)
    {
        InfoTitles[Index] = nullptr;
        InfoValues[Index] = nullptr;
    }
}

NnLoadSave::~NnLoadSave()
{
    // FUN_00485a60: vptrs back; ListBox dtor FUN_005a1fe0 on +0xe98 then +0x5dc, the string at
    // +0x350, the vector at +0x340 (FUN_00487c30 per entry), then FUN_00576010 (Event::Handler)
    // and FUN_0059fa50 (WinControl) -- the member / base destructors, in that order.
}

bool NnLoadSave::Create(Win3D* NewParent, void* Data)
{
    // LAB_00485be0 (RET 8, 0x26c-byte frame). No null test on Data (0x00485c07).
    Mode = *static_cast<int const*>(Data);

    float const Zero[3] = {0.0f, 0.0f, 0.0f};
    float const ZFar = NnFloatBits(0xbf866666); // -1.05

    // 0x00485be9..0x00485c78: FUN_005a0db0("LoadSave_BlackPlane", mesh, {0, 0, -1}, 0, {0,0,0},
    // 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, {0,0,0}).
    float const PlanePosition[3] = {0.0f, 0.0f, -1.0f};
    CreateStaticControl("LoadSave_BlackPlane", kBlackPlaneMesh, PlanePosition, nullptr, Zero, 0, 0,
                        0, 1, 1, 0, 1, 0, 0, 0, Zero);

    // 0x00485c7d..0x00485ccf: "LoadSave_Frame" at {0.14, 0, -1.05}, the same flags.
    float const FramePosition[3] = {NnFloatBits(0x3e0f5c29), 0.0f, ZFar};
    CreateStaticControl("LoadSave_Frame", kFrameMesh, FramePosition, nullptr, Zero, 0, 0, 0, 1, 1,
                        0, 1, 0, 0, 0, Zero);

    // 0x00485cd4..0x00485d28: FUN_005a0e30("LoadSave_Title", "PageHeader", {0.145, 0.25, -1.05},
    // 0, 0.27, 0.03, 1.0, 0, 1, 0, 0) -> +0x5a8, then slot 42 (0x1e, 0x586, 0).
    float const TitlePosition[3] = {NnFloatBits(0x3e147ae1), NnFloatBits(0x3e800000), ZFar};
    Title = CreateTextControl("LoadSave_Title", "PageHeader", TitlePosition, nullptr,
                              NnFloatBits(0x3e8a3d71), NnFloatBits(0x3cf5c28f), 1.0f, 0, 1, 0, 0);
    Title->SetProperty(0x1e, 0x586, 0);

    // 0x00485d2e..0x00485ede: the five buttons. Save and Load share {-0.33, 0.27, -1.05}; Delete,
    // Main Menu and Resume step down by 0.0916 / 0.1832 / 0.2748 (FSUB float). Half extents
    // {0.089, 0.021, 0}; FUN_005a0c90(name, FRONT_button.cmp, 0, 0, 0, pos, 0, half, 0, 0, 1, 0,
    // tooltip ids 0x58c..0x590, 0, 0) -> +0x5ac..+0x5bc.
    float const ButtonX = NnFloatBits(0xbea8f5c2);
    float const ButtonY = NnFloatBits(0x3e8a3d71);
    float const ButtonHalf[3] = {NnFloatBits(0x3db645a2), NnFloatBits(0x3cac0831), 0.0f};
    float const SavePosition[3] = {ButtonX, ButtonY, ZFar};
    Buttons[0] = CreateButtonControl("LoadSave_SaveGame", kButtonMesh, nullptr, nullptr, nullptr,
                                     SavePosition, nullptr, ButtonHalf, 0, 0, 1, 0, 0x58c, 0, 0);
    Buttons[1] = CreateButtonControl("LoadSave_LoadGame", kButtonMesh, nullptr, nullptr, nullptr,
                                     SavePosition, nullptr, ButtonHalf, 0, 0, 1, 0, 0x58d, 0, 0);
    float const DeletePosition[3] = {ButtonX, ButtonY - NnFloatBits(0x3dbb98c8), ZFar};
    Buttons[2] = CreateButtonControl("LoadSave_Delete", kButtonMesh, nullptr, nullptr, nullptr,
                                     DeletePosition, nullptr, ButtonHalf, 0, 0, 1, 0, 0x58e, 0, 0);
    float const MainMenuPosition[3] = {ButtonX, ButtonY - NnFloatBits(0x3e3b98c8), ZFar};
    Buttons[3] = CreateButtonControl("LoadSave_MainMenu", kButtonMesh, nullptr, nullptr, nullptr,
                                     MainMenuPosition, nullptr, ButtonHalf, 0, 0, 1, 0, 0x58f, 0,
                                     0);
    float const ResumePosition[3] = {ButtonX, ButtonY - NnFloatBits(0x3e8cb296), ZFar};
    Buttons[4] = CreateButtonControl("LoadSave_ResumeGame", kButtonMesh, nullptr, nullptr, nullptr,
                                     ResumePosition, nullptr, ButtonHalf, 0, 0, 1, 0, 0x590, 0, 0);

    // 0x00485ee4..0x00485f14: FUN_0055bfc0(1) on each (no null test).
    for (int Index = 0; Index < 5; ++Index)
        static_cast<NnButtonControl*>(Buttons[Index])->SetMontageMode(1);

    // 0x00485f19..0x004860b4: the captions, FUN_005a0e30(name, "ButtonText", pos, 0, 0.27, 0.03,
    // 1.0, 0, 1, 0, 0) -> +0x5c0..+0x5d0. Y = the button row's Y + 0.0018 (Save, Load), - 0.0898,
    // - 0.1814, - 0.273.
    float const SaveTextPosition[3] = {ButtonX, ButtonY + NnFloatBits(0x3aebedfa), ZFar};
    ButtonTexts[0] = CreateTextControl("LoadSave_SaveGameText", "ButtonText", SaveTextPosition,
                                       nullptr, NnFloatBits(0x3e8a3d71), NnFloatBits(0x3cf5c28f),
                                       1.0f, 0, 1, 0, 0);
    ButtonTexts[1] = CreateTextControl("LoadSave_LoadGameText", "ButtonText", SaveTextPosition,
                                       nullptr, NnFloatBits(0x3e8a3d71), NnFloatBits(0x3cf5c28f),
                                       1.0f, 0, 1, 0, 0);
    float const DeleteTextPosition[3] = {ButtonX, ButtonY - NnFloatBits(0x3db7e910), ZFar};
    ButtonTexts[2] = CreateTextControl("LoadSave_DeleteText", "ButtonText", DeleteTextPosition,
                                       nullptr, NnFloatBits(0x3e8a3d71), NnFloatBits(0x3cf5c28f),
                                       1.0f, 0, 1, 0, 0);
    float const MainMenuTextPosition[3] = {ButtonX, ButtonY - NnFloatBits(0x3e39c0ec), ZFar};
    ButtonTexts[3] = CreateTextControl("LoadSave_MainMenuText", "ButtonText",
                                       MainMenuTextPosition, nullptr, NnFloatBits(0x3e8a3d71),
                                       NnFloatBits(0x3cf5c28f), 1.0f, 0, 1, 0, 0);
    float const ResumeTextPosition[3] = {ButtonX, ButtonY - NnFloatBits(0x3e8bc6a8), ZFar};
    ButtonTexts[4] = CreateTextControl("LoadSave_ResumeGameText", "ButtonText",
                                       ResumeTextPosition, nullptr, NnFloatBits(0x3e8a3d71),
                                       NnFloatBits(0x3cf5c28f), 1.0f, 0, 1, 0, 0);

    // 0x004860a8..0x0048611c: slot 42 (0x1e, 0x587..0x58b, 0) on the captions.
    for (int Index = 0; Index < 5; ++Index)
        ButtonTexts[Index]->SetProperty(0x1e, static_cast<uintptr_t>(0x587 + Index), 0);

    // 0x00486122..0x004861dd: +0x5d4..+0x5d8 = 0; each caption's +0x381 = DAT_00679bb4 and
    // +0x385 = DAT_00679ba4 (two unaligned dwords).
    for (int Index = 0; Index < 5; ++Index)
        bButtonEnabled[Index] = 0;
    for (int Index = 0; Index < 5; ++Index)
    {
        NnTextControl* const Text = static_cast<NnTextControl*>(ButtonTexts[Index]);
        memcpy(Text->BlinkColourOn, G_HudChromeColorRgba, 4);
        memcpy(Text->BlinkColourOff, &G_NnPaletteArgb_679ba4, 4);
    }

    // 0x004861be..0x00486232: "LoadSave_InfoFrame" at {-0.33, -0.2, -1.05}, the static flags.
    float const InfoFramePosition[3] = {ButtonX, NnFloatBits(0xbe4ccccd), ZFar};
    CreateStaticControl("LoadSave_InfoFrame", kInfoFrameMesh, InfoFramePosition, nullptr, Zero, 0,
                        0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);

    // 0x00486237..0x004864d4: the info texts, FUN_005a0e30(name, "Normal", pos, 0, 0.25, 0.02,
    // 1.0, 0, 0, 0, 0), from {-0.32, -0.125, -1.05} down by 0, 0.02, 0.0425, 0.0625, 0.085, 0.105,
    // 0.1275, 0.1475 (FSUB float). Created title, data, title, data...; stored at +0x1768
    // (location title), +0x1778 (location data), +0x176c, +0x177c, +0x1770, +0x1780, +0x1774,
    // +0x1784.
    float const InfoX = NnFloatBits(0xbea3d70a);
    float const InfoY = NnFloatBits(0xbe000000);
    float const InfoWidth = NnFloatBits(0x3e800000);
    float const InfoHeight = NnFloatBits(0x3ca3d70a);
    char const* const InfoTitleNames[4] = {"LoadSave_InfoLocationTitle",
                                           "LoadSave_InfoCreditsTitle", "LoadSave_InfoRankTitle",
                                           "LoadSave_InfoShipTitle"};
    char const* const InfoDataNames[4] = {"LoadSave_InfoLocationData", "LoadSave_InfoCreditsData",
                                          "LoadSave_InfoRankData", "LoadSave_InfoShipData"};
    {
        // Spelled out per text so every subtraction is the listing's single FSUB.
        float const LocationTitlePosition[3] = {InfoX, InfoY, ZFar};
        InfoTitles[0] = CreateTextControl(InfoTitleNames[0], "Normal", LocationTitlePosition,
                                          nullptr, InfoWidth, InfoHeight, 1.0f, 0, 0, 0, 0);
        float const LocationDataPosition[3] = {InfoX, InfoY - NnFloatBits(0x3ca3d70a), ZFar};
        InfoValues[0] = CreateTextControl(InfoDataNames[0], "Normal", LocationDataPosition,
                                          nullptr, InfoWidth, InfoHeight, 1.0f, 0, 0, 0, 0);
        float const CreditsTitlePosition[3] = {InfoX, InfoY - NnFloatBits(0x3d2e147b), ZFar};
        InfoTitles[1] = CreateTextControl(InfoTitleNames[1], "Normal", CreditsTitlePosition,
                                          nullptr, InfoWidth, InfoHeight, 1.0f, 0, 0, 0, 0);
        float const CreditsDataPosition[3] = {InfoX, InfoY - NnFloatBits(0x3d800000), ZFar};
        InfoValues[1] = CreateTextControl(InfoDataNames[1], "Normal", CreditsDataPosition,
                                          nullptr, InfoWidth, InfoHeight, 1.0f, 0, 0, 0, 0);
        float const RankTitlePosition[3] = {InfoX, InfoY - NnFloatBits(0x3dae147b), ZFar};
        InfoTitles[2] = CreateTextControl(InfoTitleNames[2], "Normal", RankTitlePosition, nullptr,
                                          InfoWidth, InfoHeight, 1.0f, 0, 0, 0, 0);
        float const RankDataPosition[3] = {InfoX, InfoY - NnFloatBits(0x3dd70a3e), ZFar};
        InfoValues[2] = CreateTextControl(InfoDataNames[2], "Normal", RankDataPosition, nullptr,
                                          InfoWidth, InfoHeight, 1.0f, 0, 0, 0, 0);
        float const ShipTitlePosition[3] = {InfoX, InfoY - NnFloatBits(0x3e028f5c), ZFar};
        InfoTitles[3] = CreateTextControl(InfoTitleNames[3], "Normal", ShipTitlePosition, nullptr,
                                          InfoWidth, InfoHeight, 1.0f, 0, 0, 0, 0);
        float const ShipDataPosition[3] = {InfoX, InfoY - NnFloatBits(0x3e170a3e), ZFar};
        InfoValues[3] = CreateTextControl(InfoDataNames[3], "Normal", ShipDataPosition, nullptr,
                                          InfoWidth, InfoHeight, 1.0f, 0, 0, 0, 0);
    }

    // 0x004864c8..0x00486578: titles slot 42 (0x1e, 0x593 / 0x594 / 0x5ec / 0x591, 0); values
    // (0x1d, L"", 0).
    InfoTitles[0]->SetProperty(0x1e, 0x593, 0);
    InfoTitles[1]->SetProperty(0x1e, 0x594, 0);
    InfoTitles[2]->SetProperty(0x1e, 0x5ec, 0);
    InfoTitles[3]->SetProperty(0x1e, 0x591, 0);
    for (int Index = 0; Index < 4; ++Index)
        InfoValues[Index]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(kEmptyText), 0);

    // 0x0048657e..0x004865c8: 0x10 visible rows in save mode, else 0x14. Row height
    // (double)0.032538 * rows (DAT_005d1f10) and text scale (double)0.01773 / rows
    // (DAT_005d1f08), both stored as float; the lists' Y is 0.11 in save mode, else 0.21.
    VisibleRows = Mode == kNnLoadSaveModeSave ? 0x10 : 0x14;
    float const ListHeight = static_cast<float>(0.032538 * static_cast<double>(VisibleRows));
    float const ListTextScale = static_cast<float>(0.01773 / static_cast<double>(VisibleRows));
    float ListY = NnFloatBits(0x3de147ae);
    if (Mode != kNnLoadSaveModeSave)
        ListY = NnFloatBits(0x3e570a3d);

    // 0x004865d0..0x00486654: the name list -- slot 9 Create(this, 0); height, visible rows, no
    // frame, row rules, width 0.4, text scale; its offset {-0.142, Y, -1.05} at +0xe8c.
    NameList.Create(this, nullptr);
    NameList.Height = ListHeight;
    NameList.VisibleRows = VisibleRows;
    NameList.bDrawFrame = 0;
    NameList.bDrawRowRules = 1;
    NameList.Width = NnFloatBits(0x3ecccccd);
    NameList.TextScale = ListTextScale;
    NameListOffset[0] = NnFloatBits(0xbe116873);
    NameListOffset[1] = ListY;
    NameListOffset[2] = ZFar;

    // 0x00486657..0x004866d1: the date list, the same with width 0.14 and offset
    // {0.258, Y, -1.05} at +0x1748.
    TimeList.Create(this, nullptr);
    TimeList.Height = ListHeight;
    TimeList.bDrawFrame = 0;
    TimeList.bDrawRowRules = 1;
    TimeList.Width = NnFloatBits(0x3e0f5c2a);
    TimeList.VisibleRows = VisibleRows;
    TimeList.TextScale = ListTextScale;
    TimeListOffset[0] = NnFloatBits(0x3e841894);
    TimeListOffset[1] = ListY;
    TimeListOffset[2] = ZFar;

    if (Mode == kNnLoadSaveModeSave)
    {
        // 0x004866da..0x00486753: FUN_005a1900("LoadSave_Edit2", {0.125 - 0.26, 0.158 + 0.015,
        // -1.05}, 0.52, 0.03, L"") -> +0x1764; FUN_0057bdb0(0x1e); FUN_0057c750 (no null test).
        float const DescripX = NnFloatBits(0x3e000000);
        float const DescripY = NnFloatBits(0x3e21cac1);
        float const EditPosition[3] = {DescripX - NnFloatBits(0x3e851eb8),
                                       DescripY + NnFloatBits(0x3c75c28f), ZFar};
        DescripEdit = CreateUiTextEdit("LoadSave_Edit2", EditPosition, NnFloatBits(0x3f051eb8),
                                       NnFloatBits(0x3cf5c28f), const_cast<wchar_t*>(kEmptyText));
        DescripEdit->SetMaxLength(0x1e);
        DescripEdit->Activate();

        // 0x00486758..0x004867b4: FUN_005a0c90("LoadSave_DescripButton", LS_bottomframe.3db, 0,
        // 0, 0, {0.125, 0.158, -1.05}, 0, {0.27, 0.029, 0}, 0, 0, 1, 0, 0x596, 0, 0) -> +0x1760;
        // FUN_0055bfc0(1); slot 42 (0x32, 1, 0).
        float const DescripPosition[3] = {DescripX, DescripY, ZFar};
        float const DescripHalf[3] = {NnFloatBits(0x3e8a3d71), NnFloatBits(0x3ced9168), 0.0f};
        DescripButton = CreateButtonControl("LoadSave_DescripButton", kDescripButtonMesh, nullptr,
                                            nullptr, nullptr, DescripPosition, nullptr,
                                            DescripHalf, 0, 0, 1, 0, 0x596, 0, 0);
        static_cast<NnButtonControl*>(DescripButton)->SetMontageMode(1);
        DescripButton->SetProperty(0x32, 1, 0);

        // 0x004867ba..0x0048680e: FUN_005a0e30("LoadSave_DescripText", "ListText",
        // {-0.009, 0.204, -1.05}, 0, 0.27, 0.03, 1.0, 0, 0, 0, 0) -> +0x175c; slot 42 (0x1e,
        // 0x592, 0).
        float const DescripTextPosition[3] = {NnFloatBits(0xbc1374bc), NnFloatBits(0x3e50e560),
                                              ZFar};
        DescripText = CreateTextControl("LoadSave_DescripText", "ListText", DescripTextPosition,
                                        nullptr, NnFloatBits(0x3e8a3d71), NnFloatBits(0x3cf5c28f),
                                        1.0f, 0, 0, 0, 0);
        DescripText->SetProperty(0x1e, 0x592, 0);
    }
    else
    {
        // 0x00486816..0x00486822
        DescripEdit = nullptr;
        DescripButton = nullptr;
        DescripText = nullptr;
    }

    // 0x00486828..0x004868ad: FUN_005a17e0("LoadSave_Scroll", {0.406, Y, -1.05}, 0.02, H, 0,
    // 100, rows, 0) -> +0x1758; Y / H 0.107 / 0.37 in save mode, else 0.21 / 0.465.
    float ScrollY = NnFloatBits(0x3e570a3d);
    float ScrollHeight = NnFloatBits(0x3eee147b);
    if (Mode == kNnLoadSaveModeSave)
    {
        ScrollY = NnFloatBits(0x3ddb22d1);
        ScrollHeight = NnFloatBits(0x3ebd70a4);
    }
    float const ScrollPosition[3] = {NnFloatBits(0x3ecfdf3c), ScrollY, ZFar};
    Scroll = CreateUIScroll("LoadSave_Scroll", ScrollPosition, NnFloatBits(0x3ca3d70a),
                            ScrollHeight, 0, 100, VisibleRows, 0);

    // 0x004868a2..0x00486914: FUN_005a0c90("LoadSave_Close", x.3db, 0, 0, 0,
    // {0.415, 0.26, -1.05}, 0, {0.02, 0.02, 0}, 0, 1, 0, 0, 0x409, 0, 0) -> +0x1788;
    // FUN_0055bfc0(1).
    float const ClosePosition[3] = {NnFloatBits(0x3ed47ae2), NnFloatBits(0x3e851eb8), ZFar};
    float const CloseHalf[3] = {NnFloatBits(0x3ca3d70a), NnFloatBits(0x3ca3d70a), 0.0f};
    CloseButton = CreateButtonControl("LoadSave_Close", kCloseMesh, nullptr, nullptr, nullptr,
                                      ClosePosition, nullptr, CloseHalf, 0, 1, 0, 0, 0x409, 0, 0);
    static_cast<NnButtonControl*>(CloseButton)->SetMontageMode(1);

    // 0x00486919..0x004869c2: FUN_004347e0(ids 0x65d, buffer, 0x100) -- FDUMP at line 0x18e on a
    // miss, the buffer used either way -- into the +0x350 string; +0x360 = its length.
    wchar_t PrefixText[0x100];
    if (GetIdsStringWWithRawFallback(0x65d, PrefixText, 0x100) == 0)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Failed to find string resource %d\n", kNnLoadSaveCpp,
              0x18e, 0x65d);
    }
    SaveNamePrefix.assign(PrefixText, wcslen(PrefixText));
    SaveNamePrefixLength = static_cast<int>(wcslen(SaveNamePrefix.c_str()));
    // 0x004869d3
    NextSaveNumber = 1;

    // 0x004869c8..0x00486b1e: the title and which buttons show, by mode.
    if (Mode == kNnLoadSaveModeLoad)
    {
        Title->SetProperty(0x1e, 0x588, 0);
        HideControl(Buttons[0]);
        ShowControl(Buttons[1]);
        HideControl(ButtonTexts[0]);
        ShowControl(ButtonTexts[1]);
    }
    else if (Mode == kNnLoadSaveModeSave)
    {
        Title->SetProperty(0x1e, 0x587, 0);
        ShowControl(Buttons[0]);
        HideControl(Buttons[1]);
        ShowControl(ButtonTexts[0]);
        HideControl(ButtonTexts[1]);
    }
    else if (Mode == kNnLoadSaveModeStoryStar)
    {
        Title->SetProperty(0x1e, 0x588, 0);
        HideControl(Buttons[0]);
        ShowControl(Buttons[1]);
        HideControl(ButtonTexts[0]);
        ShowControl(ButtonTexts[1]);
        HideControl(Buttons[2]);
        HideControl(Buttons[4]);
        HideControl(ButtonTexts[2]);
        HideControl(ButtonTexts[4]);
        HideControl(CloseButton);
    }
    else
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Invalid load/save mode %d\n", kNnLoadSaveCpp, 0x1bc,
              Mode);
    }

    // 0x00486b21..0x00486b28
    DrawLayer = 0x6e;
    DrawSubLayer = 6;
    // 0x00486b2f: FUN_004a7670.
    bCursorDrawFlag = NnModalPauseGame();
    // 0x00486b34..0x00486b42: FUN_005760d0(0xf) on the +0x32c face.
    RegisterEvent(0xf);
    // 0x00486b47..0x00486b52
    RefreshSaveList();
    SelectSave(-1);
    // 0x00486b69: FUN_004b5060, SETNZ.
    return Win3D::Create(NewParent, Data);
}

void NnLoadSave::ResumeAndDelete()
{
    NnModalResumeGame(bCursorDrawFlag);
    NameList.Destroy();
    TimeList.Destroy();
    ReleaseMouse();
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        Win3DDeleteTree(Child);
        Child = Next;
    }
    Destroy();
    delete this;
}

void NnLoadSave::ClearListsAndSelection()
{
    NameList.ItemCount = 0;
    NameList.SelectedIndex = -1;
    TimeList.ItemCount = 0;
    TimeList.SelectedIndex = -1;
    SelectSave(-1);
}

WinControl* NnLoadSave::ButtonTextFor(uintptr_t Button) const
{
    // 0x00487067..0x004870ba / 0x004870f0..0x00487143
    for (int Index = 0; Index < 5; ++Index)
    {
        if (Button == reinterpret_cast<uintptr_t>(Buttons[Index]))
            return ButtonTexts[Index];
    }
    return nullptr;
}

bool NnLoadSave::OnEvent(unsigned MsgId)
{
    // LAB_00486b80 (RET 4): only USER_CANCEL (0xf), which is consumed either way; mode 3 keeps
    // the window.
    if (MsgId != 0xf)
        return false;
    if (Mode != kNnLoadSaveModeStoryStar)
        ResumeAndDelete();
    return true;
}

void NnLoadSave::Draw()
{
    // FUN_00486c10: the software cursor forced on, FUN_0045a9f0, WinControl's draw, then +0x33d
    // follows +0x33e.
    if (GetClientMouseCursorDrawFlag() == 0)
        SetClientMouseCursorDrawFlag(1);
    Win3DFlushBeforeOverlayDraw();
    WinControl::Draw();
    if (bListRefreshed != 0)
        bSaveAllowed = 1;
}

bool NnLoadSave::Slot23(unsigned Msg, unsigned A, unsigned B)
{
    // LAB_00486c50 (RET 0xc): the list clicks (0xf001 / 0xf002), NN_Confirm's answer (0x3006)
    // and USER_CANCEL / USER_ENTER relays (0xf003 / 0xf004) go on to slot 42; always false.
    if (Msg == 0xf001 || Msg == 0xf002 || Msg == 0x3006 || Msg == 0xf003 || Msg == 0xf004)
        SetProperty(static_cast<int>(Msg), A, B);
    return false;
}

intptr_t NnLoadSave::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_00486ca0 (RET 0xc, 0x1218-byte frame: two 0x400-character buffers). Every path
    // returns 0; nothing reaches WinControl's switch.
    wchar_t Format[0x400];
    wchar_t Text[0x400];

    if (static_cast<unsigned>(Msg) > 0x3006)
    {
        // LAB_004873c3: 0xf001..0xf004 (jump table 00487534).
        int const Row = static_cast<int>(B);
        switch (static_cast<unsigned>(Msg))
        {
        case 0xf001:
        {
            // 0x004873d8..0x00487421: a list click -- that row selected in the list(s) it fits,
            // the date list follows the name list, then SelectSave.
            if (Row >= -1)
            {
                if (Row < NameList.ItemCount)
                    NameList.SelectedIndex = Row;
                if (Row >= -1 && Row < TimeList.ItemCount)
                    TimeList.SelectedIndex = Row;
            }
            int const Selected = NameList.SelectedIndex;
            if (Selected >= -1 && Selected < TimeList.ItemCount)
                TimeList.SelectedIndex = Selected;
            SelectSave(Selected);
            return 0;
        }
        case 0xf002:
        {
            // 0x00487435..0x0048749f: a double click loads, in modes 1 and 3.
            if (Mode != kNnLoadSaveModeLoad && Mode != kNnLoadSaveModeStoryStar)
                return 0;
            if (Row >= -1)
            {
                if (Row < NameList.ItemCount)
                    NameList.SelectedIndex = Row;
                if (Row >= -1 && Row < TimeList.ItemCount)
                    TimeList.SelectedIndex = Row;
            }
            int const Selected = NameList.SelectedIndex;
            if (Selected >= -1 && Selected < TimeList.ItemCount)
                TimeList.SelectedIndex = Selected;
            SelectSave(Selected);
            if (Selected == -1)
                return 0;
            LoadSave(Selected);
            return 0;
        }
        case 0xf003:
            // 0x004874b3..0x004874c1: the Event::Handler face's slot 0 with USER_CANCEL.
            OnEvent(0xf);
            return 0;
        case 0xf004:
            // 0x004874d2..0x004874e7: save, when allowed and in save mode.
            if (bSaveAllowed != 0 && Mode == kNnLoadSaveModeSave)
                SaveGame();
            return 0;
        default:
            return 0;
        }
    }

    if (Msg == 0x3006)
    {
        // LAB_004871b0: NN_Confirm answered (A == 1 yes) the question +0x338 names.
        switch (ConfirmMode)
        {
        case 1:
            // 0x004871c5..0x00487274: delete the selected save -- its CHARACTER_ID copied
            // (REP MOVSD 0x80), the list erased (FUN_004890c0), the lists cleared,
            // FUN_005aa8f0 on DAT_0067e7b8, the list refilled.
            if (A == 1 && NameList.SelectedIndex != -1)
            {
                CHARACTER_ID Id;
                memcpy(&Id, &Entries[NameList.SelectedIndex].Info->Id, sizeof(CHARACTER_ID));
                Entries.clear();
                ClearListsAndSelection();
                if (G_LoginMgr.DestroyCharacter(Id) != 1)
                {
                    FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kNnLoadSaveCpp, 0x3ab,
                          "Failed to delete character\n");
                }
                RefreshSaveList();
            }
            break;
        case 2:
            // 0x0048727e..0x0048728b: back to the main menu at the next Update.
            if (A == 1)
                bExitToMainMenu = 1;
            break;
        case 3:
            break;
        case 4:
            // 0x004872a9..0x00487361: overwrite -- the save FUN_00487550 would have made, with
            // +0x368 as the character.
            if (A == 1)
            {
                CHARACTER_ID Id;
                memcpy(&Id, &OverwriteId, sizeof(CHARACTER_ID));
                Entries.clear(); // FUN_00488c20
                ClearListsAndSelection();
                bSaveAllowed = 0;
                bListRefreshed = 0;
                if (G_Server != nullptr)
                {
                    G_Server->SaveGame(Id, SaveName, GetPlayerConnectionId()); // +0xe8
                }
                else
                {
                    FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kNnLoadSaveCpp, 0x366,
                          "No server, cannot save\n");
                }
                RefreshSaveList();
                bListRefreshed = 1;
            }
            break;
        default:
            FDUMP(0x100001, "%s(%d) : *** ERROR: Invalid confirmation mode %d\n", kNnLoadSaveCpp,
                  0x2dc, ConfirmMode);
            break;
        }
        // LAB_00487391: the question is over -- the edit box back, both lists selectable.
        ConfirmMode = 0;
        if (DescripEdit != nullptr)
            DescripEdit->Activate();
        NameList.bSelectable = 1;
        TimeList.bSelectable = 1;
        return 0;
    }

    switch (Msg)
    {
    case 0x42:
    {
        // caseD_42: slot 31 SetOffset(*A), slot 5 SetPosition(+0x60), then each list's slot 5
        // with the window position plus its offset (FADD float per component).
        float const* const Offset3 = reinterpret_cast<float const*>(A);
        SetOffset(Offset3[0], Offset3[1], Offset3[2]);
        SetPosition(Position);
        float const NamePosition[3] = {Position[0] + NameListOffset[0],
                                       Position[1] + NameListOffset[1],
                                       Position[2] + NameListOffset[2]};
        NameList.SetPosition(NamePosition);
        float const TimePosition[3] = {Position[0] + TimeListOffset[0],
                                       Position[1] + TimeListOffset[1],
                                       Position[2] + TimeListOffset[2]};
        TimeList.SetPosition(TimePosition);
        return 0;
    }
    case 0x26:
    {
        // caseD_26: a button was pressed (A = the sender).
        if (A == reinterpret_cast<uintptr_t>(Buttons[0]))
        {
            // 0x00486dba..0x00486dc8 -> LAB_004874e5
            if (bButtonEnabled[0] != 0)
                SaveGame();
            return 0;
        }
        if (A == reinterpret_cast<uintptr_t>(Buttons[1]))
        {
            // 0x00486dd5..0x00486df5
            if (bButtonEnabled[1] == 0 || NameList.SelectedIndex == -1)
                return 0;
            LoadSave(NameList.SelectedIndex);
            return 0;
        }
        if (A == reinterpret_cast<uintptr_t>(Buttons[2]))
        {
            // 0x00486e15..0x00486eac: "delete <name>?" -- ids 0x75c formatted with the row's
            // name (+0x14 string of the entry), NN_Confirm style "ConfirmPlayerText".
            if (bButtonEnabled[2] == 0 || NameList.SelectedIndex == -1)
                return 0;
            wchar_t const* const Name = Entries[NameList.SelectedIndex].Name.c_str();
            ConfirmMode = 1;
            if (DescripEdit != nullptr)
                DescripEdit->Deactivate(false); // FUN_0057c7d0(0)
            GetIdsStringWWithRawFallback(0x75c, Format, 0x400);
            // MSVCRT swprintf has no count in retail; the port bounds it by the buffer.
            swprintf(Text, 0x400, Format, Name);
            NnConfirmOpen(this, Text, "ConfirmPlayerText");
            return 0;
        }
        if (A == reinterpret_cast<uintptr_t>(Buttons[3]))
        {
            // 0x00486ecb..0x00486f2c: "quit to the main menu?" (ids 0x75b); both lists stop
            // taking clicks while it is up.
            if (bButtonEnabled[3] == 0)
                return 0;
            GetIdsStringWWithRawFallback(0x75b, Text, 0x400);
            ConfirmMode = 2;
            if (DescripEdit != nullptr)
                DescripEdit->Deactivate(false);
            NameList.bSelectable = 0;
            TimeList.bSelectable = 0;
            NnConfirmOpen(this, Text, nullptr);
            return 0;
        }
        if (A == reinterpret_cast<uintptr_t>(Buttons[4]))
        {
            // 0x00486f4b..0x00486f94: resume -- FUN_004a7690, slot 10 on both lists, slot 33
            // ReleaseMouse, FUN_0043d2a0(this).
            if (bButtonEnabled[4] == 0)
                return 0;
            NnModalResumeGame(bCursorDrawFlag);
            NameList.Destroy();
            TimeList.Destroy();
            ReleaseMouse();
            Win3DDeleteTree(this);
            return 0;
        }
        if (A == reinterpret_cast<uintptr_t>(DescripButton))
            return 0;
        if (A == reinterpret_cast<uintptr_t>(CloseButton))
        {
            // 0x00486fbf..0x00487023
            ResumeAndDelete();
            return 0;
        }
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kNnLoadSaveCpp, 0x260, "Unknown button\n");
        return 0;
    }
    case 0x2c:
    {
        // caseD_2c: the pointer entered a button -- its caption, when shown, gets property
        // (0x51, 1, 0).
        WinControl* const Caption = ButtonTextFor(A);
        if (Caption == nullptr || (Caption->Flags & 1) == 0)
            return 0;
        Caption->SetProperty(0x51, 1, 0);
        return 0;
    }
    case 0x2d:
    {
        // caseD_2d: it left -- (0x51, 0, 0), then the colour back to DAT_00679bb4 (0x33).
        WinControl* const Caption = ButtonTextFor(A);
        if (Caption == nullptr || (Caption->Flags & 1) == 0)
            return 0;
        Caption->SetProperty(0x51, 0, 0);
        Caption->SetProperty(0x33, reinterpret_cast<uintptr_t>(G_HudChromeColorRgba), 0);
        return 0;
    }
    case 0x3e:
        // caseD_3e: the scroll bar moved -- B is the first visible row of both lists.
        NameList.FirstVisible = static_cast<int>(B);
        TimeList.FirstVisible = static_cast<int>(B);
        return 0;
    default:
        return 0;
    }
}

void NnLoadSave::SaveGame()
{
    // FUN_00487550 (0xa14-byte frame).
    FNnLoadSaveEntry const* Selected = nullptr;
    if (NameList.SelectedIndex != -1)
    {
        // 0x00487567..0x0048757d: CHARACTER_ID::operator= (COMMON.DLL) from the row's record.
        Selected = &Entries[NameList.SelectedIndex];
        OverwriteId = Selected->Info->Id;
    }

    // 0x00487583..0x0048759d: FUN_0057c2c0(+0x568, 0x1f), or an empty name without the box.
    if (DescripEdit != nullptr)
        DescripEdit->GetText(SaveName, 0x1f);
    else
        SaveName[0] = 0;

    // 0x004875a6..0x004875cc: a different name than the selected row's makes a new save.
    if (Selected != nullptr)
    {
        if (wcscmp(Selected->Name.c_str(), SaveName) != 0)
            OverwriteId.Buffer[0] = 0;
    }

    // 0x004875d3..0x00487617: no name -- the prefix and the next number (_ltow base 10).
    if (SaveName[0] == 0)
    {
        wcscpy(SaveName, SaveNamePrefix.c_str());
        _ltow(NextSaveNumber, SaveName + SaveNamePrefixLength, 10);
    }

    if (OverwriteId.Buffer[0] != 0)
    {
        // 0x0048762e..0x004876f1: "overwrite?" (ids 0x706), +0x338 = 4, the edit box and the
        // lists put down, then FUN_004889e0's body inline with style "ConfirmText".
        wchar_t Text[0x400];
        GetIdsStringWWithRawFallback(0x706, Text, 0x400);
        ConfirmMode = 4;
        if (DescripEdit != nullptr)
            DescripEdit->Deactivate(false);
        NameList.bSelectable = 0;
        TimeList.bSelectable = 0;
        NnConfirmOpen(this, Text, "ConfirmText");
        return;
    }

    // 0x00487700..0x004877dc: the save itself -- the id copied (REP MOVSD 0x80), the list erased,
    // the lists cleared, IServer::SaveGame (+0xe8) or the FDUMP, the list refilled.
    CHARACTER_ID Id;
    memcpy(&Id, &OverwriteId, sizeof(CHARACTER_ID));
    Entries.clear();
    ClearListsAndSelection();
    bSaveAllowed = 0;
    bListRefreshed = 0;
    if (G_Server != nullptr)
    {
        G_Server->SaveGame(Id, SaveName, GetPlayerConnectionId());
    }
    else
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kNnLoadSaveCpp, 0x366,
              "No server, cannot save\n");
    }
    RefreshSaveList();
    bListRefreshed = 1;
}

void NnLoadSave::RefreshSaveList()
{
    // FUN_004877f0.
    // 0x0048780d..0x0048783e: erase(begin, end) (FUN_00489200 + FUN_00489160).
    Entries.clear();

    if (G_LoginMgr.RequestCharacterInfo() == 1)
    {
        // 0x0048784f..0x004878f5: count the character list and reserve that many rows.
        int Count = 0;
        MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;
        while (G_LoginMgr.Characters.traverse(Node, true))
            Count = Count + 1;
        if (Entries.capacity() < static_cast<size_t>(Count))
            Entries.reserve(static_cast<size_t>(Count));

        // 0x004878f8..0x004879ec: one row per record that is not the restart save (+0x200):
        // the listed text (FUN_00488b70) and the date (filetime_to_wstring(+0x228)), appended
        // (FUN_00488ee0).
        for (int Index = 0; Index < Count; ++Index)
        {
            CCharacterSet::CharacterInfo* const Info = G_LoginMgr.CharacterAt(Index);
            if (Info->bIsRestart != 0)
                continue;
            FNnLoadSaveEntry Entry;
            Entry.Info = Info;
            wchar_t const* const EntryText = NnLoadSaveEntryText(Info);
            Entry.Name.assign(EntryText, wcslen(EntryText));
            wchar_t const* const TimeText =
                filetime_to_wstring(*reinterpret_cast<_FILETIME const*>(&Info->TimestampLow));
            Entry.Time.assign(TimeText, wcslen(TimeText));
            Entries.push_back(Entry);
        }

        // 0x004879f2..0x00487a24: saves failed while away (DAT_0067e984) -- NN_Error ids 0x5cd
        // over this window, +0x338 = 3.
        if (G_LoginMgr.SaveFailureCount != 0)
        {
            ConfirmMode = 3;
            if (DescripEdit != nullptr)
                DescripEdit->Deactivate(false);
            NnErrorOpen(0x5cd, this);
        }
    }
    else
    {
        // LAB_00487a26
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kNnLoadSaveCpp, 0x446,
              "Failed to retrieve available save games\n");
    }

    // 0x00487a4a..0x00487b15: the next generated number -- the largest N among rows named
    // <prefix>N (wcsncmp, then wcstol base 10 that consumes the rest, N <= 1000000000), plus 1.
    int Largest = 0;
    for (size_t Index = 0; Index < Entries.size(); ++Index)
    {
        wchar_t const* const Name = Entries[Index].Name.c_str();
        if (wcsncmp(Name, SaveNamePrefix.c_str(), static_cast<size_t>(SaveNamePrefixLength)) != 0)
            continue;
        wchar_t* End = nullptr;
        long const Value = wcstol(Name + SaveNamePrefixLength, &End, 10);
        if (End == nullptr || *End != 0)
            continue;
        if (Value > 1000000000)
            continue;
        if (Value <= Largest)
            continue;
        Largest = static_cast<int>(Value);
    }
    NextSaveNumber = Largest + 1;

    // 0x00487b1b..0x00487bbe: at most 0x100 rows go into the lists (names, then dates).
    int Count = static_cast<int>(Entries.size());
    if (Count > 0x100)
        Count = 0x100;
    NameList.ItemCount = Count;
    TimeList.ItemCount = Count;
    for (int Index = 0; Index < Count; ++Index)
    {
        NameList.SetItemText(Index, Entries[Index].Name.c_str());
        TimeList.SetItemText(Index, Entries[Index].Time.c_str());
    }

    // 0x00487bc0..0x00487c11: the scroll bar -- range 0..1 and hidden when the rows fit
    // (count - rows + 1 <= 1), else range 0..count and shown.
    if (Count - VisibleRows + 1 <= 1)
    {
        Scroll->SetRange(0.0f, 1.0f);
        HideControl(Scroll);
    }
    else
    {
        Scroll->SetRange(0.0f, static_cast<float>(Count));
        ShowControl(Scroll);
    }
}

void NnLoadSave::SelectSave(int Index)
{
    // FUN_00487cb0 (RET 4, 0x800-byte frame).
    if (Index == -1)
    {
        // 0x00487cca..0x00487dc4: the values read ids 0x595, the description emptied; Save,
        // Main Menu and Resume on, Load and Delete greyed (caption property 2 = 1).
        for (int Value = 0; Value < 4; ++Value)
            InfoValues[Value]->SetProperty(0x1e, 0x595, 0);
        if (DescripEdit != nullptr)
            DescripEdit->ClearText();
        ButtonTexts[0]->SetProperty(2, 0, 0);
        bButtonEnabled[0] = 1;
        ButtonTexts[1]->SetProperty(2, 1, 0);
        bButtonEnabled[1] = 0;
        ButtonTexts[2]->SetProperty(2, 1, 0);
        bButtonEnabled[2] = 0;
        ButtonTexts[3]->SetProperty(2, 0, 0);
        bButtonEnabled[3] = 1;
        ButtonTexts[4]->SetProperty(2, 0, 0);
        bButtonEnabled[4] = 1;
        return;
    }

    int const Count = static_cast<int>(Entries.size());
    if (Index < 0 || Index >= Count)
    {
        // LAB_004880b6
        FDUMP(0x100001, "%s(%d) : *** ERROR: Invalid index %d for savegame\n", kNnLoadSaveCpp,
              0x4ea, Index);
        return;
    }

    CCharacterSet::CharacterInfo const* const Info = Entries[Index].Info;

    // 0x00487e0f..0x00487e4e: the system's name (ISystem slot 3, strid_name), or empty.
    Universe::ISystem const* const System = Universe::get_system(Info->SystemId);
    if (System != nullptr)
        InfoValues[0]->SetProperty(0x1e, (uintptr_t)System->GetStridName(), 0);
    else
        InfoValues[0]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(kEmptyText), 0);

    // 0x00487e54..0x00487eae: credits (+0x234) and rank (+0x270) through L"%d" (005d0ec4).
    wchar_t Number[0x400];
    swprintf(Number, 0x400, L"%d", Info->Money);
    InfoValues[1]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Number), 0);
    swprintf(Number, 0x400, L"%d", Info->Rank);
    InfoValues[2]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Number), 0);

    // 0x00487eb4..0x00487ee3: the ship's name (Archetype::GetShip(+0x230)->ids_name), or empty
    // with no ship.
    if (Info->ShipArchetype != 0)
    {
        Archetype::Ship const* const Ship = Archetype::GetShip(Info->ShipArchetype);
        InfoValues[3]->SetProperty(0x1e, static_cast<uintptr_t>(Ship->IdsName), 0);
    }
    else
    {
        InfoValues[3]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(kEmptyText), 0);
    }

    // 0x00487ee9..0x00487f6c: the description box -- DescripStrId (+0x224) through FUN_004347e0
    // into DAT_00671ce8 (FDUMP at line 0x64d on a miss, the buffer either way), else the
    // Description string; FUN_0057bff0, then FUN_0057c0e0(text, 0).
    if (DescripEdit != nullptr)
    {
        wchar_t const* Description = nullptr;
        if (Info->DescripStrId != 0)
        {
            if (GetIdsStringWWithRawFallback(Info->DescripStrId, G_NnLoadSaveText, 0x100) == 0)
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: Failed to find string resource %d\n",
                      kNnLoadSaveCpp, 0x64d, Info->DescripStrId);
            }
            Description = G_NnLoadSaveText;
        }
        else
        {
            Description = Info->Description.c_str();
        }
        DescripEdit->ClearText();
        DescripEdit->SetText(Description, nullptr);
    }

    // 0x00487f71..0x004880b3: the restart / autosave records (+0x200 / +0x201) cannot be saved
    // over or deleted.
    if (Info->bIsRestart == 0 && Info->bIsAutoSave == 0)
    {
        for (int Button = 0; Button < 5; ++Button)
        {
            ButtonTexts[Button]->SetProperty(2, 0, 0);
            bButtonEnabled[Button] = 1;
        }
        return;
    }
    ButtonTexts[0]->SetProperty(2, 1, 0);
    bButtonEnabled[0] = 0;
    ButtonTexts[1]->SetProperty(2, 0, 0);
    bButtonEnabled[1] = 1;
    ButtonTexts[2]->SetProperty(2, 1, 0);
    bButtonEnabled[2] = 0;
    ButtonTexts[3]->SetProperty(2, 0, 0);
    bButtonEnabled[3] = 1;
    ButtonTexts[4]->SetProperty(2, 0, 0);
    bButtonEnabled[4] = 1;
}

void NnLoadSave::LoadSave(int Index)
{
    // FUN_004880f0 (RET 4).
    int const Count = static_cast<int>(Entries.size());
    if (Index < 0 || Index >= Count)
    {
        // LAB_004881c0
        FDUMP(0x100001, "%s(%d) : *** ERROR: Invalid index %d for savegame\n", kNnLoadSaveCpp,
              0x502, Index);
        return;
    }
    // 0x0048812e..0x00488144: FUN_005b2c70(record, record +0x201).
    CCharacterSet::CharacterInfo const* const Info = Entries[Index].Info;
    QueueLoadGameCharacter(&Info->Id, Info->bIsAutoSave != 0);
    // 0x00488149..0x004881b0: close.
    ResumeAndDelete();
    // 0x004881b2: FUN_004120d0(1).
    FadeIn(true);
}

void NnLoadSave::Update()
{
    // FUN_004881f0: WinControl's Update (FUN_005a17b0).
    WinControl::Update();

    // 0x004881f8..0x0048823e: outside the main-menu question, the row under the pointer in
    // either list is drawn highlighted in both (ListBox +0x80).
    if (ConfirmMode != 2)
    {
        int Row = -1;
        if (NameList.HoverIndex != -1)
            Row = NameList.HoverIndex;
        if (TimeList.HoverIndex != -1)
            Row = TimeList.HoverIndex;
        if (Row >= -1)
        {
            if (Row < NameList.ItemCount)
                NameList.Field80 = Row;
            if (Row >= -1 && Row < TimeList.ItemCount)
                TimeList.Field80 = Row;
        }
    }

    // 0x00488244..0x004882c2: main menu confirmed -- FUN_005763b0(0xc5, 0) (USER_EXIT_GAME),
    // then close.
    if (bExitToMainMenu == 0)
        return;
    bExitToMainMenu = 0;
    HudPostCommand(0xc5, 0);
    ResumeAndDelete();
}

void OpenNnLoadSave(int Mode)
{
    // FUN_00488970.
    if (Win3DFindByClassName("NN_LoadSave") != nullptr)
        return;
    Win3D* const Created = WinFactoryCreate("NN_LoadSave", "NN_LoadSave", nullptr, &Mode);
    if (Created == nullptr)
        return;
    NnLoadSave* const LoadSave = static_cast<NnLoadSave*>(Created);
    float const Zero[3] = {0.0f, 0.0f, 0.0f};
    LoadSave->SetProperty(0x42, reinterpret_cast<uintptr_t>(Zero), 0);
    LoadSave->CaptureMouse();
}

wchar_t const* NnLoadSaveEntryText(CCharacterSet::CharacterInfo const* Info)
{
    // FUN_00488b70 (cdecl).
    if (Info->DescripStrId != 0)
    {
        if (GetIdsStringWWithRawFallback(Info->DescripStrId, G_NnLoadSaveText, 0x100) == 0)
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: Failed to find string resource %d\n",
                  kNnLoadSaveCpp, 0x64d, Info->DescripStrId);
        }
        return G_NnLoadSaveText;
    }
    return Info->Description.c_str();
}
