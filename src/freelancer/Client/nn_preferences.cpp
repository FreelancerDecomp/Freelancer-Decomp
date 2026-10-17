// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail NN_Preferences (E:\FL\Scratch\Source\Client\nn\NN_Preferences.cpp -- FDUMP at
// 0x004af4b5 names the file). See nn_preferences.h.
//
// The class replaces the flat NnPreferencesScreen / FOverlayNav shell (menu_ui_widgets.cpp,
// nn_screen.cpp) and the old options_screen_data.* / menu_options.* ports; "NN_Preferences" is
// registered only by the WINFACTORY global below.

#include "nn_preferences.h"
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates

#include "gameflow.h"              // ResetGameflowRoomLetterbox (FUN_00558d60)
#include "globals.h"               // GetClientFrameTimeCapped, g_display, g_client, SetRenderModeWithFallback
#include "ids_rdl.h"               // LoadIdsIntoRdl (FUN_0057da40)
#include "ids_strings.h"           // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "login_mgr.h"             // G_Server (DAT_0067ecd0)
#include "menu_keymap_config.h"
#include "menu_sound_arch.h"       // BindClientBackgroundMusic (FUN_00428ba0)
#include "menu_ui_init.h"          // PlayUiSoundSlot / PlayUiSoundSlotFlag1 / StopUiSoundSlot
#include "nn_button_control.h"
#include "nn_modal.h"              // G_bNnScreenUp (DAT_0067dcc8), NnModalResumeGame, NnFloatBits
#include "cargo_bar_control.h"
#include "font_string.h"           // FontStringConstruct (FUN_004144b0)
#include "nn_rollover.h"           // NnRolloverShowIdsAtGlobal / NnRolloverShowHoverAtGlobal
#include "tooltip_hover.h"         // FToolTipHoverInfo, ToolTipHoverReset (FUN_004963a0)
#include "nn_text_control.h"
#include "nn_ui_pulse.h"           // G_NnPaletteArgb_679bb0 / 679ba4 / 679b94
#include "perf_options.h"          // SavePerfOptions (FUN_005b3ec0), FindPerfOption*, ProbeMachineGraphicsLevel
#include "pixel_format.h"          // PixelFormatFromD3D / PixelFormatGetDesc (FUN_005b7010 / 005b7030)
#include "player_spawn.h"          // GetPlayerConnectionId (DAT_00673344)
#include "rich_text_win.h"
#include "ui_scroll.h"
#include "widescreen.h"            // port-only (FREELANCER_WIDESCREEN_SUPPORT)
#include "win3d.h"
#include "win3d_renderables.h"     // Win3DFlushBeforeOverlayDraw (FUN_0045a9f0)
#include "win_factory.h"
#include "wire_controls.h"

#include "common_csys.h"           // RotateMatrix
#include "common_ini.h"            // INI_Reader
#include "common_server.h"         // IServer::ReqDifficultyScale (slot 87)
#include "common_misc.h"           // SinglePlayer
#include "dacom.h"                 // FDUMP
#include "DALib.h"
#include "engine/engbase.h"
#include "soundmanager.h"          // ISoundSource

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

// Retail DAT_006721d0 -- WINFACTORY<NN_Preferences> (LAB_004a9270 -> FUN_004b28e0; Create
// FUN_004b2950; atexit LAB_004a9290 -> FUN_00579380).
WINFACTORY<NnPreferences> G_NnPreferencesFactory("NN_Preferences");

namespace
{
    // Retail DAT_006721c0 -- the empty key-row vector category 3 answers with (static init
    // FUN_004addb0, atexit LAB_004adde0).
    std::vector<FKeyRow> const G_NnPreferencesEmptyRows;

    // DAT_00611e28 -- keys TryBindKey refuses (Shift, Ctrl, Alt, Esc, F1, -1, LWin, RWin).
    int const kForbiddenKeys[8] = {0x10, 0x11, 0x12, 0x1b, 0x70, -1, 0x5b, 0x5c};

    char const kKeyListPath[] = "../Data/interface/keylist.ini"; // 0x005d4cbc
    char const kPerfOptionsFile[] = "PerfOptions.ini";           // 0x005d5088

    void ShowControl(Win3D* Control)
    {
        Control->Flags = static_cast<unsigned char>(Control->Flags | 3);
    }

    void HideControl(Win3D* Control)
    {
        Control->Flags = static_cast<unsigned char>(Control->Flags & 0xfc);
    }

    // FUN_004347e0(DAT_0067eca8, Ids, Buffer, 0x40) then the control's SetProperty(0x1d, Buffer).
    void SetTextIds(WinControl* Text, unsigned Ids)
    {
        wchar_t Buffer[0x40];
        GetIdsStringWWithRawFallback(Ids, Buffer, 0x40);
        Text->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Buffer), 0);
    }

    void SetPosition3(Win3D* Control, float X, float Y, float Z)
    {
        float Position[3] = {X, Y, Z};
        Control->SetPosition(Position);
    }

    // Inlined at 0x004ac3d0 / 0x004ac4c7: the eight dialog widgets hidden, rows back, every
    // option caption re-enabled.
    void HideVideoConfirmDialog(NnPreferences* Self);
}

NnPreferences::NnPreferences()
{
    // FUN_004b2950: FUN_0059f860, the vptrs, the nine vectors emptied. Every other member is
    // written by Create before it is read; the port zeroes the block here.
    bClosed = 0; bInGame = 0; bSeedMode = 0; Pad32f = 0; SelWidth = 0; bSel32 = 0;
    BigBox = nullptr; Pad33c[0] = nullptr; Pad33c[1] = nullptr; Title = nullptr; CloseButton = nullptr;
    for (int Index = 0; Index < 6; ++Index) { PageButtons[Index] = nullptr; PageTexts[Index] = nullptr; }
    for (int Index = 0; Index < 13; ++Index)
    {
        CommandRows[Index] = nullptr; KeyRows[Index] = nullptr; KeyAltRows[Index] = nullptr;
        KeyBtnRows[Index] = nullptr; KeyAltBtnRows[Index] = nullptr;
    }
    for (int Index = 0; Index < 14; ++Index)
    {
        StaticRows[Index] = nullptr; OptTitleText[Index] = nullptr; OptText[Index] = nullptr;
        OptToggle[Index] = nullptr; OptToggleText[Index] = nullptr; OptScroll[Index] = nullptr;
        ScrollTextLeft[Index] = nullptr; ScrollTextRight[Index] = nullptr; SmallText[Index] = nullptr;
        SmallButton[Index] = nullptr; SmallButtonText[Index] = nullptr; Snapshot[Index] = 0.0f;
    }
    KeyScroll = nullptr; KeyColumn1 = nullptr; KeyColumn2 = nullptr;
    for (int Index = 0; Index < 3; ++Index) { GroupButtons[Index] = nullptr; GroupTexts[Index] = nullptr; GroupNameIds[Index] = 0; }
    Pad4d0 = 0; Pad4e0 = 0; SetKeyFrame = nullptr;
    DialogText[0] = DialogText[1] = DialogText[2] = nullptr;
    OverwriteButton = nullptr; DialogCancelButton = nullptr;
    DialogButtonText[0] = DialogButtonText[1] = nullptr;
    VideoModeCycle = nullptr; VideoBox = nullptr; VideoBox2 = nullptr; VideoModeText = nullptr;
    StaticBar = nullptr; CancelButton = nullptr; CancelText = nullptr; DefaultsButton = nullptr; DefaultsText = nullptr;
    CreditsText = nullptr; CurPage = 0; ReqPage = 0; CurGroup = 0; Anim = 0.0;
    bDirty = 0; bCreating = 0; bConfirmMode = 0; bPausedGame = 0; bKeyCapture = 0; bVideoConfirm = 0;
    CaptureRow = FKeyRow();
    bCaptureAlt = 0; bConflictAlt = 0; bConflict = 0; ConflictRow = 0; ConflictCategory = 0; ConflictCommand = 0;
    FlipAngle = 0.0; FlipSpeed = 0.0; bFlipping = 0; bFlipHalf = 0; bClosingAnim = 0;
    CreditsScroll = 0.0; CreditsSpeed = 0.0; CurWidth = 0; bCur32 = 0; ConfirmCountdown = 0.0; bApplyingPreset = 0;
    for (int Index = 0; Index < kPrefVideoModeCount; ++Index)
    {
        Modes[Index].Width = 0; Modes[Index].Height = 0; Modes[Index].Bpp = 0;
        bModeAvail[Index] = 0; CycleIndex[Index] = 0;
    }
    ModeCount = 0; bBigVram = 0;
}

NnPreferences::~NnPreferences()
{
    // FUN_004a9600: the nine vectors (81c, 80c, 7fc, 554 .. 504) are freed, then the WinControl
    // dtor -- the std::vector members do it here.
}

bool NnPreferences::Slot23(unsigned, unsigned, unsigned)
{
    // FUN_004ada30
    OutputDebugStringA("OnMessage\n");
    return false;
}

void NnPreferences::Draw()
{
    // FUN_004abdb0: flush, then every visible child (Flags bit 1) draws; the set-key frame
    // flushes the overlay batch again before it draws.
    Win3DFlushBeforeOverlayDraw();
    Win3D* Child = ChildHead;
    for (; Child != nullptr; Child = Child->NextSibling)
    {
        if ((Child->Flags & 2) == 0)
            continue;
        if (Child->AsWinControl() == SetKeyFrame)
            Win3DFlushBeforeOverlayDraw();
        Child->Draw();
    }
}

void NnPreferences::Update()
{
    // FUN_004ab6e0
    WinControl::Update();
    double Dt = GetClientFrameTimeCapped();

    // The column buttons and their texts in retail's local order: interface, keymap, video,
    // audio, credits (main menu only -- in game the slot stays unwritten and is never read),
    // main menu.
    Win3D* Buttons[6] = {PageButtons[1], PageButtons[0], PageButtons[2], PageButtons[3], nullptr, PageButtons[5]};
    Win3D* Texts[6] = {PageTexts[1], PageTexts[0], PageTexts[2], PageTexts[3], nullptr, PageTexts[5]};
    if (bInGame == 0)
    {
        Buttons[4] = PageButtons[4];
        Texts[4] = PageTexts[4];
    }

    if (Dt > 0.2)
        Dt = 0.2;

    bool bDoClosing = false;
    if (Anim < 1.0 && bInGame == 0)
    {
        if (bClosingAnim == 0)
        {
            // 0x004ab7f6: the opening slide.
            Anim += Dt * 1.5;
            for (int Step = 6, Index = 0; Step > 0; --Step, ++Index)
            {
                double const Offset = Step * 0.1;
                double const X = Offset + Anim;
                if (X > 0.0)
                {
                    SetPosition3(Buttons[Index], 0.0f, 0.0f, 0.0f);
                    SetPosition3(Texts[Index], 0.0f, 0.0f, 0.0f);
                }
                else
                {
                    SetPosition3(Buttons[Index], static_cast<float>(X), 0.0f, 0.0f);
                    SetPosition3(Texts[Index], static_cast<float>(Offset + Anim), 0.0f, 0.0f);
                }
            }
            if (Anim > 0.0)
            {
                EnterInterfacePage();
                EnterInterfacePage();
                SetPosition3(BigBox, 0.0f, 0.0f, 0.0f);
                bFlipping = 0;
                Anim = 1.0;
            }
            else
            {
                SetPosition3(BigBox, static_cast<float>(Anim * -1.2), 0.0f, 0.0f);
            }
            if (Anim > -0.8 && Anim < -0.1)
                PlayUiSoundSlot(0x1b);
        }
        else
        {
            bDoClosing = true;
        }
    }
    else if (bClosingAnim != 0 && bInGame == 0)
    {
        bDoClosing = true;
    }

    if (bDoClosing)
    {
        // 0x004ab9b1: the closing slide.
        if (Dt > 0.03)
            Dt = 0.03;
        Anim -= Dt * 1.5;
        for (int Step = 6, Index = 0; Step > 0; --Step, ++Index)
        {
            double const Offset = Step * 0.1;
            double const X = Offset + Anim;
            if (X > 0.0)
            {
                SetPosition3(Buttons[Index], 0.0f, 0.0f, 0.0f);
                SetPosition3(Texts[Index], 0.0f, 0.0f, 0.0f);
            }
            else
            {
                SetPosition3(Buttons[Index], static_cast<float>(X), 0.0f, 0.0f);
                SetPosition3(Texts[Index], static_cast<float>(Offset + Anim), 0.0f, 0.0f);
            }
        }
        if (Anim + 0.6 > 0.0)
            PlayUiSoundSlot(0x1b);
        if (Anim < -1.0)
        {
            for (int Index = 0; Index < 6; ++Index)
            {
                HideControl(Buttons[Index]);
                HideControl(Texts[Index]);
            }
            bClosed = 1;
            G_bNnScreenUp = false;
            return;
        }
        SetPosition3(BigBox, static_cast<float>(Anim * -1.2), 0.0f, 0.0f);
    }

    // 0x004abb5b
    if (bInGame == 0)
    {
        if (bFlipping != 0)
        {
            if (Dt > 0.03)
                Dt = 0.03;
            FlipAngle = Dt * FlipSpeed + FlipAngle;
            if (FlipAngle >= 180.0)
            {
                int const Page = CurPage;
                FlipAngle = 0.0;
                switch (Page)
                {
                case 0: EnterKeyMapPage(); break;
                case 1: EnterInterfacePage(); break;
                case 2: EnterVideoPage(); break;
                case 3: EnterAudioPage(); break;
                case 4: EnterCreditsPage(); break;
                default: break;
                }
                bFlipHalf = static_cast<unsigned char>(bFlipHalf == 0);
                bFlipping = 0;
            }
            double Angle = FlipAngle;
            if (bFlipHalf != 0)
                Angle += 180.0;
            Vector Angles;
            Angles.x = static_cast<float>(Angle);
            Angles.y = 0.0f;
            Angles.z = 0.0f;
            Matrix Rotation = RotateMatrix(Angles);
            DALib::Engine->SetInstanceOrientation(BigBox->MeshHolder.EngineInstance,
                                                  reinterpret_cast<float*>(&Rotation));
            DALib::Engine->UpdateInstance(BigBox->MeshHolder.EngineInstance, 0, nullptr);
        }
    }
    else
    {
        switch (CurPage)
        {
        case 0: EnterKeyMapPage(); break;
        case 1: EnterInterfacePage(); break;
        case 2: EnterVideoPage(); break;
        case 3: EnterAudioPage(); break;
        case 4: EnterCreditsPage(); break;
        default: break;
        }
        bFlipping = 0;
    }

    // 0x004abcd7: the credits roll.
    if (CurPage == 4 && bFlipping == 0)
    {
        CreditsScroll = Dt * CreditsSpeed + CreditsScroll;
        CreditsText->SetScrollOrigin(0, static_cast<int>(CreditsScroll));
    }

    // 0x004abd10: the video-mode confirm countdown.
    if (bVideoConfirm != 0)
    {
        ConfirmCountdown -= Dt;
        Dt = ConfirmCountdown;
        // TEST AH,0x41 + JP 0x004abd48: less or equal cancels; greater (and unordered) formats.
        if (ConfirmCountdown <= 0.0)
        {
            CancelChanges();
            return;
        }
        wchar_t Buffer[0x40];
        swprintf(Buffer, 0x40, L"%1.0f", Dt); // 0x005d5048
        DialogText[2]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Buffer), 0);
    }
}

void NnPreferences::SetRowsHidden(bool bHide)
{
    // FUN_004abdf0
    if (CurPage == 0)
    {
        for (int Index = 0; Index < 13; ++Index)
        {
            if (bHide)
            {
                HideControl(KeyBtnRows[Index]);
                HideControl(KeyAltBtnRows[Index]);
                KeyRows[Index]->SetProperty(0x24, 0, 0);
                KeyAltRows[Index]->SetProperty(0x24, 0, 0);
                HideControl(CommandRows[Index]);
                HideControl(KeyRows[Index]);
                HideControl(KeyAltRows[Index]);
                HideControl(StaticRows[Index]);
            }
            else
            {
                ShowControl(KeyBtnRows[Index]);
                ShowControl(KeyAltBtnRows[Index]);
                ShowControl(CommandRows[Index]);
                ShowControl(KeyRows[Index]);
                ShowControl(KeyAltRows[Index]);
                ShowControl(StaticRows[Index]);
                KeyRows[Index]->SetProperty(0x24, 1, 0);
                KeyAltRows[Index]->SetProperty(0x24, 1, 0);
            }
        }
        if (!bHide)
        {
            ShowControl(KeyScroll);
            ShowControl(StaticRows[13]);
            return;
        }
        HideControl(KeyScroll);
        HideControl(StaticRows[13]);
    }
    else if (CurPage == 1 || CurPage == 2 || CurPage == 3)
    {
        for (int Index = 0; Index < 14; ++Index)
        {
            if (bHide)
            {
                HideControl(OptToggle[Index]);
                HideControl(OptScroll[Index]);
                HideControl(SmallButton[Index]);
                HideControl(SmallButtonText[Index]);
                OptText[Index]->SetProperty(0x24, 0, 0);
            }
            else
            {
                OptText[Index]->SetProperty(0x24, 1, 0);
            }
        }
        if (!bHide)
        {
            RefreshRows(-1);
            return;
        }
    }
    if (bHide)
        HideControl(CreditsText);
}

void NnPreferences::ShowKeyDialog()
{
    // FUN_004abfb0
    SetTextIds(DialogButtonText[0], 0x5e8);
    SetTextIds(DialogButtonText[1], 0x2148);
    ShowControl(SetKeyFrame);
    SetRowsHidden(true);
    ShowControl(OverwriteButton);
    ShowControl(DialogCancelButton);
    ShowControl(DialogButtonText[0]);
    ShowControl(DialogButtonText[1]);
    ShowControl(DialogText[0]);
    if (bVideoConfirm != 0 || bKeyCapture != 0)
    {
        ShowControl(DialogText[1]);
        ShowControl(DialogText[2]);
    }
}

void NnPreferences::CloseKeyDialogRestoreRows()
{
    // Inlined at 0x004ac3d0 / 0x004ac649 / 0x004ac85e / 0x004ac90b / 0x004ad834 / 0x004adb40
    // (the callers clear bKeyCapture and release the capture first where retail does).
    HideControl(DialogText[0]);
    HideControl(DialogText[1]);
    HideControl(DialogText[2]);
    HideControl(DialogButtonText[0]);
    HideControl(DialogButtonText[1]);
    HideControl(OverwriteButton);
    HideControl(DialogCancelButton);
    HideControl(SetKeyFrame);
    SetRowsHidden(false);
    for (int Index = 0; Index < 13; ++Index)
    {
        ShowControl(KeyBtnRows[Index]);
        ShowControl(KeyAltBtnRows[Index]);
        ShowControl(StaticRows[Index]);
    }
    ShowControl(KeyBtnRows[0]);
}

void NnPreferences::MarkKeyMapDirtyAndReload()
{
    // Inlined at 0x004ac6e7 / 0x004ac801 / 0x004acc1d / 0x004ad7ac.
    bDirty = 1;
    CancelText->TextColour = G_NnPaletteArgb_679bb0;
    CancelButton->bUseColour = 0;
    CancelButton->ButtonState = static_cast<unsigned char>(CancelButton->ButtonState | 4);
    LoadKeyList(kKeyListPath);
    FillKeyRows(static_cast<int>(KeyScroll->SetProperty(0x3b, 0, 0)));
}

intptr_t NnPreferences::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // FUN_004ac0a0
    Win3D* const Source = reinterpret_cast<Win3D*>(A);

    if (Msg == 0x46 || Msg == 0x25)
    {
        // 0x004ac0df: row tooltips.
        if (CurPage == 0)
        {
            for (int Index = 0; Index < 13; ++Index)
            {
                if (Source == KeyRows[Index] || Source == KeyAltRows[Index])
                    NnRolloverShowIdsAtGlobal(0x68c, 0.0f, 0.2f); // FUN_004b6440 @ 0x004ac1b4
                else if (Source == CommandRows[Index])
                    ShowRowTip(Index);
            }
        }
        else if (CurPage == 1 && (Source == VideoModeCycle || Source == VideoModeText))
        {
            // LoadOptList pads every section to at least 15 rows, so 14 rows are always there.
            for (int Index = 0; Index < 14; ++Index)
            {
                if (OptionsA[Index].Id == 0x536)
                    ShowRowTip(Index);
            }
        }
        else
        {
            for (int Index = 0; Index < 14; ++Index)
            {
                if (Source == OptText[Index] || Source == OptToggle[Index] ||
                    Source == OptToggleText[Index] || Source == OptScroll[Index] ||
                    Source == SmallText[Index] || Source == SmallButton[Index] ||
                    Source == SmallButtonText[Index])
                {
                    ShowRowTip(Index);
                }
            }
        }
    }

    if (Msg == 0x4f && Anim < 0.0)
        return 1;
    if (bFlipping != 0)
        return 0;

    if (bConfirmMode != 0)
    {
        // 0x004ac205: the "apply changes?" dialog.
        if (Msg != 0x26)
            return 0;
        if (Source == OverwriteButton)
        {
            if (CurPage == 0)
            {
                char const* const UserPath = EnsureUserKeyMapPath();
                GetKeyMapIniRelPath();
                SaveUserKeyMapIni(UserPath);
            }
            else
            {
                if (bVideoConfirm != 0)
                {
                    bCur32 = bSel32;
                    bVideoConfirm = 0;
                    CurWidth = SelWidth;
                    RefreshRows(0x536);
                }
                TakeSnapshot();
            }
            bDirty = 0;
            CancelText->TextColour = G_NnPaletteArgb_679b94;
            CancelButton->bUseColour = 1;
            CancelButton->ButtonState = static_cast<unsigned char>(CancelButton->ButtonState & 0xfb);
            ShowConfirmDialog(false);
            LeaveCurrentPage();
            if (bDirty == 0)
                GotoRequestedPage();
            SavePerfOptions(kPerfOptionsFile);
            return 1;
        }
        if (Source == DialogCancelButton)
        {
            CancelChanges();
            return 1;
        }
        return 0;
    }

    if (bVideoConfirm != 0)
    {
        // 0x004ac3b8: "keep this video mode?".
        if (Msg != 0x26)
            return 0;
        if (Source == OverwriteButton)
        {
            bVideoConfirm = 0;
            HideVideoConfirmDialog(this);
            CurWidth = SelWidth;
            bCur32 = bSel32;
        }
        else if (Source == DialogCancelButton)
        {
            TryApplyVideoMode(CurWidth, bCur32);
            bVideoConfirm = 0;
            HideVideoConfirmDialog(this);
            SelWidth = CurWidth;
            bSel32 = bCur32;
        }
        else
        {
            return 0;
        }
        // 0x004ac46e
        RefreshRows(0x536);
        UpdateDirty(-1, -1.0f);
        return 1;
    }

    if (bKeyCapture != 0)
    {
        // 0x004ac553: the key conflict / capture dialog.
        if (Msg != 0x26)
            return 0;
        if (Source == OverwriteButton)
        {
            FKeyRowKey const& CapKey = bCaptureAlt != 0 ? CaptureRow.Alt : CaptureRow.Primary;
            if (bConflict != 0)
            {
                std::vector<FKeyRow> const Group = GetCurGroupRows();
                std::vector<FKeyRow> const Category = GetCategoryRows(ConflictCategory);
                FKeyRow const& Row = Category[ConflictRow];
                FKeyRowKey const& RowKey = bConflictAlt != 0 ? Row.Alt : Row.Primary;
                UpdateKeyCmdBinding(CaptureRow.CommandId, RowKey.VirtualKey, RowKey.Modifiers,
                                    CapKey.VirtualKey, CapKey.Modifiers);
                UpdateKeyCmdBinding(ConflictCommand, 0, 0, RowKey.VirtualKey, RowKey.Modifiers);
                bKeyCapture = 0;
                Win3DRemoveCapture(this);
                CloseKeyDialogRestoreRows();
                MarkKeyMapDirtyAndReload();
                bConflict = 0;
                return 1;
            }
            UpdateKeyCmdBinding(CaptureRow.CommandId, 0, 0, CapKey.VirtualKey, CapKey.Modifiers);
            MarkKeyMapDirtyAndReload();
            bKeyCapture = 0;
            Win3DRemoveCapture(this);
            CloseKeyDialogRestoreRows();
            return 1;
        }
        if (Source == DialogCancelButton)
        {
            bKeyCapture = 0;
            Win3DRemoveCapture(this);
            CloseKeyDialogRestoreRows();
            return 1;
        }
        return 0;
    }

    // 0x004ac9bb
    if (Msg == 0x3e)
    {
        std::vector<FPrefOption>* Options = nullptr;
        switch (CurPage)
        {
        case 0:
            if (Source == KeyScroll)
                FillKeyRows(static_cast<int>(B));
            return 0;
        case 1: Options = &OptionsA; break;
        case 2: Options = &OptionsC; break;
        case 3: Options = &OptionsB; break;
        default: return 0;
        }
        // Every section holds at least 15 rows (LoadOptList pads).
        for (int Index = 0; Index < 14; ++Index)
        {
            if (Source == OptScroll[Index])
                SetSliderOption((*Options)[Index].Id, static_cast<int>(B), Index);
        }
        if (CurPage == 1 && Source == VideoModeCycle)
        {
            int Row = -1;
            for (int Index = 0; Index < 14; ++Index)
            {
                if (Row == -1 && OptionsA[Index].Id == 0x536)
                    Row = Index;
            }
            SetSliderOption(0x536, static_cast<int>(B), Row);
        }
        return 0;
    }

    if (Msg == 0x29)
    {
        // 0x004acb2c: a slider released on the audio page stops its preview sound.
        if (CurPage != 3)
            return 0;
        for (int Index = 0; Index < 14; ++Index)
        {
            if (Source != OptScroll[Index])
                continue;
            if (OptionsB[Index].Id == 0x581)
                StopUiSoundSlot(0x1e);
            if (OptionsB[Index].Id == 0x538)
                StopUiSoundSlot(0x1f);
            if (OptionsB[Index].Id == 0x539)
                StopUiSoundSlot(0x20);
        }
        return 0;
    }

    if (Msg != 0x26)
        return 0;

    // 0x004acbb8: clicks, per page.
    switch (CurPage)
    {
    case 0:
    {
        for (int Index = 0; Index < 3; ++Index)
        {
            if (Source == GroupButtons[Index])
                SelectGroup(Index);
        }
        char const* const UserPath = EnsureUserKeyMapPath();
        char const* const DefaultPath = GetKeyMapIniRelPath();
        if (Source == DefaultsButton)
        {
            LoadUserKeyMapIni(DefaultPath);
            MarkKeyMapDirtyAndReload();
        }
        else if (Source == CancelButton)
        {
            if (LoadUserKeyMapIni(UserPath) < 1)
                LoadUserKeyMapIni(DefaultPath);
            bDirty = 0;
            CancelText->TextColour = G_NnPaletteArgb_679b94;
            CancelButton->bUseColour = 1;
            CancelButton->ButtonState = static_cast<unsigned char>(CancelButton->ButtonState & 0xfb);
            LoadKeyList(kKeyListPath);
            FillKeyRows(static_cast<int>(KeyScroll->SetProperty(0x3b, 0, 0)));
        }
        else
        {
            for (int Index = 0; Index < 13; ++Index)
            {
                if (Source == KeyBtnRows[Index])
                    BeginKeyCapture(static_cast<int>(KeyScroll->SetProperty(0x3b, 0, 0)) + Index, false);
                else if (Source == KeyAltBtnRows[Index])
                    BeginKeyCapture(static_cast<int>(KeyScroll->SetProperty(0x3b, 0, 0)) + Index, true);
            }
        }
        break;
    }
    case 1:
    case 2:
    case 3:
    {
        std::vector<FPrefOption>& Options = CurPage == 1 ? OptionsA : (CurPage == 2 ? OptionsC : OptionsB);
        if (Source == CancelButton)
        {
            Revert(true);
            bDirty = 0;
            CancelText->TextColour = G_NnPaletteArgb_679b94;
            CancelButton->bUseColour = 1;
            CancelButton->ButtonState = static_cast<unsigned char>(CancelButton->ButtonState & 0xfb);
        }
        else if (Source == DefaultsButton)
        {
            for (unsigned Index = 0; Index < Options.size(); ++Index)
            {
                FPrefOption const Entry = Options[Index];
                if (Entry.bHeader != 0 || Entry.Default == -1)
                    continue;
                if (Entry.bSlider != 0)
                    SetSliderOption(Entry.Id, Entry.Default, -1);
                else
                    SetToggleOption(Entry.Id, Entry.Default, -1);
            }
            // 0x004ace35 / 0x004ad1eb: FLD float +0x14, FMUL float 100.0f on the x87 stack, ftol --
            // an exact product (24 x 7 bits), so double, not a rounded float, reaches ftol.
            FPerfOption* const Graphics = FindPerfOption("default_graphics");
            SetSliderOption(0x767, static_cast<int>(static_cast<double>(Graphics->Applied) * 100.0), -1);
            if (CurPage == 1)
            {
                // 0x004ace5c: the default video mode from the device caps.
                unsigned long Ability4 = 0;
                DALib::Pipeline->QueryDeviceAbility(4, &Ability4);
                unsigned long VideoMemory = 0;
                DALib::Pipeline->QueryDeviceAbility(3, &VideoMemory);
                VideoMemory >>= 20;
                RefreshRows(-1);
                int Width = 0x320;
                int Bpp = 0x10;
                if (Ability4 != 0)
                {
                    Width = 0x400;
                    Bpp = 0x20;
                }
                else if (VideoMemory > 0x10)
                {
                    Bpp = 0x20;
                }
                SelWidth = Width;
                int CycleValue = 0;
                for (int Mode = 0; Mode < kPrefVideoModeCount; ++Mode)
                {
                    if (bModeAvail[Mode] != 0 && Modes[Mode].Width == Width && Modes[Mode].Bpp == Bpp)
                    {
                        CycleValue = CycleIndex[Mode] == -1 ? 0 : CycleIndex[Mode];
                        break;
                    }
                }
                int Row = -1;
                for (int Index = 0; Index < 14; ++Index)
                {
                    if (Row == -1 && OptionsA[Index].Id == 0x536)
                        Row = Index;
                }
                if (Row >= 0)
                {
                    VideoModeCycle->SetProperty(0x37, 0, static_cast<uintptr_t>(ModeCount - 1));
                    VideoModeCycle->SetProperty(0x38, static_cast<uintptr_t>(CycleValue), 0);
                    SetSliderOption(0x536, CycleValue, Row);
                }
            }
            else
            {
                RefreshRows(-1);
            }
            UpdateDirty(-1, -1.0f);
        }
        else
        {
            for (int Index = 0; Index < 14; ++Index)
            {
                if (Source == OptToggle[Index])
                {
                    int Value = 0;
                    OptToggle[Index]->SetProperty(0x2f, reinterpret_cast<uintptr_t>(&Value), 0);
                    SetToggleOption(Options[Index].Id, Value, Index);
                }
                else if (CurPage == 1 && Source == SmallButton[Index])
                {
                    SetToggleOption(Options[Index].Id, 0, Index);
                }
            }
        }
        break;
    }
    case 4:
        if (Source == DefaultsButton)
            CreditsScroll = 0.0;
        break;
    default:
        break;
    }

    // 0x004ad2a1: the column buttons.
    int const PageOfButton[5] = {0, 1, 2, 3, 4};
    for (int Button = 0; Button < 5; ++Button)
    {
        if (Source != PageButtons[Button])
            continue;
        int const Page = PageOfButton[Button];
        if (CurPage != Page)
        {
            ReqPage = Page;
            LeaveCurrentPage();
            if (bDirty == 0)
                GotoRequestedPage();
        }
        break;
    }

    // 0x004ad438: close / main menu.
    if (Source == CloseButton || Source == PageButtons[5])
    {
        if (CurPage == 5)
            return 0;
        ReqPage = 5;
        LeaveCurrentPage();
        if (bDirty == 0)
            GotoRequestedPage();
    }
    return 0;
}

bool NnPreferences::TryBindKey(int Vk, unsigned Mods)
{
    // FUN_004ad600
    for (int Index = 0; Index < 8; ++Index)
    {
        if (Vk == kForbiddenKeys[Index])
            return false;
    }
    ConflictRow = -1;
    bConflictAlt = 0;
    for (int Category = 0; Category < 4; ++Category)
    {
        std::vector<FKeyRow> const Rows = GetCategoryRows(Category);
        for (unsigned Index = 0; Index < Rows.size(); ++Index)
        {
            if (ConflictRow != -1)
                continue;
            FKeyRow const& Row = Rows[Index];
            if (Vk == Row.Primary.VirtualKey && Row.Primary.Modifiers == Mods)
            {
                ConflictRow = static_cast<int>(Index);
                ConflictCategory = Category;
                ConflictCommand = Row.CommandId;
                bConflictAlt = 0;
                bConflict = 1;
            }
            else if (Vk == Row.Alt.VirtualKey && Row.Alt.Modifiers == Mods)
            {
                ConflictRow = static_cast<int>(Index);
                ConflictCategory = Category;
                ConflictCommand = Row.CommandId;
                bConflictAlt = 1;
                bConflict = 1;
            }
        }
    }

    if (ConflictRow < 0)
    {
        FKeyRowKey const& CapKey = bCaptureAlt != 0 ? CaptureRow.Alt : CaptureRow.Primary;
        UpdateKeyCmdBinding(CaptureRow.CommandId, Vk, Mods, CapKey.VirtualKey, CapKey.Modifiers);
        MarkKeyMapDirtyAndReload();
        bKeyCapture = 0;
        Win3DRemoveCapture(this);
        CloseKeyDialogRestoreRows();
        return true;
    }

    // 0x004ad8c4: conflict -- hide the rows and ask.
    for (int Index = 0; Index < 13; ++Index)
    {
        HideControl(KeyBtnRows[Index]);
        HideControl(KeyAltBtnRows[Index]);
        HideControl(StaticRows[Index]);
    }
    HideControl(KeyBtnRows[0]);
    FKeyRow Row;
    {
        std::vector<FKeyRow> const Rows = GetCategoryRows(ConflictCategory);
        Row = Rows[ConflictRow];
    }
    wchar_t Name[0x100];
    Name[0] = 0;
    FKeyBindingLookup Lookup;
    FKeyRowKey const& Key = bConflictAlt != 0 ? Row.Alt : Row.Primary;
    Lookup.NicknameId = Key.NicknameId;
    Lookup.VirtualKey = Key.VirtualKey;
    Lookup.Modifiers = Key.Modifiers;
    KeyMapFormatBindingName(&Lookup, Name, 0x100);
    DialogText[0]->SetText(Name);
    SetTextIds(DialogText[1], 0x5eb);
    SetTextIds(DialogText[2], static_cast<unsigned>(Row.NameIds));
    SetTextIds(DialogButtonText[0], 0x5e8);
    ShowKeyDialog();
    return true;
}

bool NnPreferences::MouseEvent(FWin3DMouseEvent const* Event)
{
    // FUN_004ada40
    char Text[0x100];
    sprintf(Text, "OnButton = %d\n", Event->Field0); // 0x005d50a4
    OutputDebugStringA(Text);
    if (bKeyCapture == 0)
        return false;
    if (Event->Field0 == -1)
        return true;
    if (Event->Field1 != 2)
        return true;
    return TryBindKey(Event->Field0, 0);
}

bool NnPreferences::KeyEvent(unsigned Key, unsigned char* Modifiers)
{
    // FUN_004adac0
    char Text[0x100];
    sprintf(Text, "OnKey = %d\n", Key); // 0x005d50b4
    OutputDebugStringA(Text);

    if (bKeyCapture != 0)
    {
        if ((Modifiers[0] & 2) == 0)
            return false;
        unsigned const State = KeyMapGetModifierState();
        if (Key == 0x73)
        {
            if (State == 0x10)
                return false; // Alt+F4 passes through
        }
        else
        {
            if (Key == 0x2a)
                return false;
            if (Key == 0x1b)
            {
                bKeyCapture = 0;
                Win3DRemoveCapture(this);
                CloseKeyDialogRestoreRows();
                return true;
            }
        }
        unsigned Mods = 0;
        if (GetKeyState(VK_SHIFT) < 0)
            Mods = 1;
        else if (GetKeyState(VK_CONTROL) < 0)
            Mods = 4;
        else if (GetKeyState(VK_MENU) < 0)
            Mods = 0x10;
        TryBindKey(static_cast<int>(Key), Mods);
        return true;
    }

    if ((Modifiers[0] & 2) == 0 || Key != 0x1b)
        return false;
    if (bConfirmMode != 0)
    {
        CancelChanges();
        return true;
    }
    if (CurPage != 6)
    {
        ReqPage = 6;
        LeaveCurrentPage();
        if (bDirty == 0)
            GotoRequestedPage();
    }
    StopUiSoundSlot(0x1e);
    StopUiSoundSlot(0x1f);
    StopUiSoundSlot(0x20);
    return true;
}

std::vector<FKeyRow> NnPreferences::GetCategoryRows(int Category) const
{
    // FUN_004ade00: a copy of the category's rows; anything but 0..2 -> DAT_006721c0 (empty).
    switch (Category)
    {
    case 0: return KeyGroups[0];
    case 1: return KeyGroups[1];
    case 2: return KeyGroups[2];
    default: return G_NnPreferencesEmptyRows;
    }
}

std::vector<FKeyRow> NnPreferences::GetCurGroupRows() const
{
    // FUN_004ae030: the same switch on +0x838.
    return GetCategoryRows(CurGroup);
}

void NnPreferences::SelectGroup(int Group)
{
    // FUN_004ae260
    CurGroup = Group;
    KeyScroll->SetProperty(0x38, 0, 0);
    int Count = static_cast<int>(GetCurGroupRows().size());
    KeyScroll->SetProperty(0x37, 0, static_cast<uintptr_t>(Count - 1));
    for (int Index = 0; Index < 13; ++Index)
    {
        Count = static_cast<int>(GetCurGroupRows().size());
        if (Index < Count)
        {
            KeyBtnRows[Index]->ButtonState = static_cast<unsigned char>(KeyBtnRows[Index]->ButtonState | 4);
            KeyAltBtnRows[Index]->ButtonState = static_cast<unsigned char>(KeyAltBtnRows[Index]->ButtonState | 4);
            ShowControl(CommandRows[Index]);
            ShowControl(KeyRows[Index]);
            ShowControl(KeyAltRows[Index]);
        }
        else
        {
            KeyBtnRows[Index]->ButtonState = static_cast<unsigned char>(KeyBtnRows[Index]->ButtonState & 0xfb);
            KeyAltBtnRows[Index]->ButtonState = static_cast<unsigned char>(KeyAltBtnRows[Index]->ButtonState & 0xfb);
            HideControl(CommandRows[Index]);
            HideControl(KeyRows[Index]);
            HideControl(KeyAltRows[Index]);
        }
    }
    FillKeyRows(0);
    unsigned const Rows = static_cast<unsigned>(GetCurGroupRows().size());
    if (Rows > 13)
        ShowControl(KeyScroll);
    else
        HideControl(KeyScroll);
    for (int Index = 0; Index < 3; ++Index)
    {
        if (Index != Group)
            GroupTexts[Index]->TextColour = G_NnPaletteArgb_679bb0;
    }
    GroupTexts[Group]->TextColour = G_NnPaletteArgb_679ba4;
}

void NnPreferences::EnterKeyMapPage()
{
    // FUN_004ae4b0
    if (CurPage == 0 && bFlipping == 0)
        return;
    if (bFlipping == 0)
    {
        CurPage = 0;
        bFlipping = 1;
        wchar_t Unused[0x40];
        GetIdsStringWWithRawFallback(0x52f, Unused, 0x40);
        PageTexts[0]->TextColour = G_NnPaletteArgb_679ba4;
        return;
    }
    SelectGroup(0);
    for (int Index = 0; Index < 13; ++Index)
    {
        ShowControl(CommandRows[Index]);
        ShowControl(KeyRows[Index]);
        ShowControl(KeyBtnRows[Index]);
        ShowControl(KeyAltRows[Index]);
        ShowControl(KeyAltBtnRows[Index]);
    }
    for (int Index = 0; Index < 14; ++Index)
        ShowControl(StaticRows[Index]);
    for (int Index = 0; Index < 3; ++Index)
    {
        ShowControl(GroupTexts[Index]);
        ShowControl(GroupButtons[Index]);
    }
    ShowControl(KeyColumn1);
    ShowControl(KeyColumn2);
    ShowControl(CancelButton);
    ShowControl(CancelText);
    ShowControl(DefaultsButton);
    ShowControl(DefaultsText);
    ShowControl(Title);
    SetTextIds(Title, 0x691);
    SetRowsHidden(false);
    HideControl(CreditsText);
}

namespace
{
    // The "enter an option page" tail shared by FUN_004ae650 / 4ae770 / 4ae890.
    void ResetCancelGrey(NnTextControl* CancelText, WireButtonControl* CancelButton)
    {
        CancelText->TextColour = G_NnPaletteArgb_679b94;
        CancelButton->bUseColour = 1;
        CancelButton->ButtonState = static_cast<unsigned char>(CancelButton->ButtonState & 0xfb);
    }
}

void NnPreferences::EnterInterfacePage()
{
    // FUN_004ae650
    if (CurPage == 1 && bFlipping == 0)
        return;
    if (bFlipping == 0)
    {
        CurPage = 1;
        bFlipping = 1;
        PageTexts[1]->TextColour = G_NnPaletteArgb_679ba4;
        return;
    }
    RefreshRows(-1);
    TakeSnapshot();
    bDirty = 0;
    ResetCancelGrey(CancelText, CancelButton);
    ShowControl(CancelButton);
    ShowControl(CancelText);
    ShowControl(DefaultsButton);
    ShowControl(DefaultsText);
    ShowControl(Title);
    SetTextIds(Title, 0x68e);
    SetRowsHidden(false);
    ProbeModes(-1);
}

void NnPreferences::EnterVideoPage()
{
    // FUN_004ae770
    if (CurPage == 2 && bFlipping == 0)
        return;
    if (bFlipping == 0)
    {
        CurPage = 2;
        bFlipping = 1;
        PageTexts[2]->TextColour = G_NnPaletteArgb_679ba4;
        return;
    }
    RefreshRows(-1);
    TakeSnapshot();
    bDirty = 0;
    ResetCancelGrey(CancelText, CancelButton);
    ShowControl(CancelButton);
    ShowControl(CancelText);
    ShowControl(DefaultsButton);
    ShowControl(DefaultsText);
    ShowControl(Title);
    ShowControl(StaticBar);
    SetTextIds(Title, 0x68d);
    SetRowsHidden(false);
}

void NnPreferences::EnterAudioPage()
{
    // FUN_004ae890
    if (CurPage == 3 && bFlipping == 0)
        return;
    if (bFlipping == 0)
    {
        CurPage = 3;
        bFlipping = 1;
        PageTexts[3]->TextColour = G_NnPaletteArgb_679ba4;
        return;
    }
    RefreshRows(-1);
    TakeSnapshot();
    bDirty = 0;
    ResetCancelGrey(CancelText, CancelButton);
    ShowControl(CancelButton);
    ShowControl(CancelText);
    ShowControl(DefaultsButton);
    ShowControl(DefaultsText);
    ShowControl(Title);
    SetTextIds(Title, 0x68f);
    SetRowsHidden(false);
}

void NnPreferences::EnterCreditsPage()
{
    // FUN_004ae9a0
    if (CurPage == 4 && bFlipping == 0)
        return;
    if (bFlipping == 0)
    {
        CurPage = 4;
        bFlipping = 1;
        PageTexts[4]->TextColour = G_NnPaletteArgb_679ba4;
        return;
    }
    SetTextIds(DefaultsText, 0x765);
    CreditsScroll = 0.0;
    ShowControl(CreditsText);
    ShowControl(DefaultsButton);
    ShowControl(DefaultsText);
    ShowControl(Title);
    SetTextIds(Title, 0x532);
    SetRowsHidden(false);
}

void NnPreferences::LeaveKeyMapPage()
{
    // FUN_004aeab0
    if (bDirty != 0)
    {
        ShowConfirmDialog(true);
        return;
    }
    for (int Index = 0; Index < 13; ++Index)
    {
        HideControl(CommandRows[Index]);
        HideControl(KeyRows[Index]);
        HideControl(KeyAltRows[Index]);
        HideControl(KeyBtnRows[Index]);
        HideControl(KeyAltBtnRows[Index]);
    }
    for (int Index = 0; Index < 14; ++Index)
        HideControl(StaticRows[Index]);
    for (int Index = 0; Index < 3; ++Index)
    {
        HideControl(GroupTexts[Index]);
        HideControl(GroupButtons[Index]);
    }
    HideControl(KeyColumn1);
    HideControl(KeyColumn2);
    HideControl(KeyScroll);
    HideControl(CancelButton);
    HideControl(CancelText);
    HideControl(DefaultsButton);
    HideControl(DefaultsText);
    HideControl(Title);
    PageTexts[0]->TextColour = G_NnPaletteArgb_679bb0;
}

namespace
{
    // The row hides shared by FUN_004aebb0 / 4aeca0 / 4aed70.
    void HideOptionRow(NnPreferences* Self, int Index)
    {
        HideControl(Self->OptTitleText[Index]);
        HideControl(Self->OptText[Index]);
        HideControl(Self->OptToggle[Index]);
        HideControl(Self->OptScroll[Index]);
        HideControl(Self->SmallText[Index]);
        HideControl(Self->ScrollTextLeft[Index]);
        HideControl(Self->ScrollTextRight[Index]);
        HideControl(Self->SmallButton[Index]);
        HideControl(Self->SmallButtonText[Index]);
        HideControl(Self->OptToggleText[Index]);
    }
}

void NnPreferences::LeaveInterfacePage()
{
    // FUN_004aebb0
    if (bDirty != 0)
    {
        ShowConfirmDialog(true);
        return;
    }
    for (int Index = 0; Index < 14; ++Index)
    {
        HideOptionRow(this, Index);
        OptToggle[Index]->ButtonBits2 = static_cast<unsigned char>(OptToggle[Index]->ButtonBits2 | 2);
    }
    HideControl(VideoModeCycle);
    HideControl(VideoBox);
    HideControl(VideoBox2);
    HideControl(VideoModeText);
    HideControl(Title);
    HideControl(CancelButton);
    HideControl(CancelText);
    HideControl(DefaultsButton);
    HideControl(DefaultsText);
    PageTexts[1]->TextColour = G_NnPaletteArgb_679bb0;
}

void NnPreferences::LeaveVideoPage()
{
    // FUN_004aeca0
    if (bDirty != 0)
    {
        ShowConfirmDialog(true);
        return;
    }
    for (int Index = 0; Index < 14; ++Index)
        HideOptionRow(this, Index);
    HideControl(Title);
    HideControl(CancelButton);
    HideControl(CancelText);
    HideControl(DefaultsButton);
    HideControl(DefaultsText);
    HideControl(StaticBar);
    PageTexts[2]->TextColour = G_NnPaletteArgb_679bb0;
}

void NnPreferences::LeaveAudioPage()
{
    // FUN_004aed70
    if (bDirty != 0)
    {
        ShowConfirmDialog(true);
        return;
    }
    for (int Index = 0; Index < 14; ++Index)
        HideOptionRow(this, Index);
    HideControl(Title);
    HideControl(CancelButton);
    HideControl(CancelText);
    HideControl(DefaultsButton);
    HideControl(DefaultsText);
    PageTexts[3]->TextColour = G_NnPaletteArgb_679bb0;
}

void NnPreferences::LeaveCreditsPage()
{
    // Inlined at 0x004ac2f9 / 0x004ad3bf / 0x004ad4a6 / 0x004adcf1 / 0x004aefb9.
    HideControl(CreditsText);
    PageTexts[4]->TextColour = G_NnPaletteArgb_679bb0;
    SetTextIds(DefaultsText, 0x5e4);
    HideControl(DefaultsButton);
    HideControl(DefaultsText);
    HideControl(Title);
}

void NnPreferences::LeaveCurrentPage()
{
    // The five-entry jump tables 0x004ad544 / 57c / 590 / 5a4 / 5b8 / 5cc / 5e0 / 0x004add98 /
    // 0x004af108; pages 5 and 6 leave nothing.
    switch (CurPage)
    {
    case 0: LeaveKeyMapPage(); break;
    case 1: LeaveInterfacePage(); break;
    case 2: LeaveVideoPage(); break;
    case 3: LeaveAudioPage(); break;
    case 4: LeaveCreditsPage(); break;
    default: break;
    }
}

void NnPreferences::GotoRequestedPage()
{
    // FUN_004aee30
    if (ReqPage != 5 && ReqPage != 6)
    {
        PlayOrRewindUiSoundSlot(2);
        PlayOrRewindUiSoundSlot(5);
    }
    switch (ReqPage)
    {
    case 0: EnterKeyMapPage(); return;
    case 1: EnterInterfacePage(); return;
    case 2: EnterVideoPage(); return;
    case 3: EnterAudioPage(); return;
    case 4: EnterCreditsPage(); return;
    case 5:
        if (bInGame == 0)
        {
            // 0x004aeec9: the main-menu closing slide.
            bClosingAnim = 1;
            Anim = 0.0;
            int const Slide[5] = {0, 1, 2, 3, 5};
            for (int Index = 0; Index < 5; ++Index)
            {
                NnButtonControl* const Button = PageButtons[Slide[Index]];
                Button->ButtonBits2 = static_cast<unsigned char>(Button->ButtonBits2 & 0xfd);
            }
            LeaveCurrentPage();
            SavePerfOptions(kPerfOptionsFile);
            return;
        }
        // 0x004af03d
        bClosed = 1;
        NnModalResumeGame(bPausedGame);
        G_bNnScreenUp = false;
        SavePerfOptions(kPerfOptionsFile);
        break;
    case 6:
        // 0x004af084
        bClosed = 1;
        G_bNnScreenUp = false;
        if (bInGame == 0)
            return;
        NnModalResumeGame(bPausedGame);
        SavePerfOptions(kPerfOptionsFile);
        break;
    default:
        return;
    }
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

namespace
{
    void HideVideoConfirmDialog(NnPreferences* Self)
    {
        HideControl(Self->DialogText[0]);
        HideControl(Self->DialogText[1]);
        HideControl(Self->DialogText[2]);
        HideControl(Self->DialogButtonText[0]);
        HideControl(Self->DialogButtonText[1]);
        HideControl(Self->OverwriteButton);
        HideControl(Self->DialogCancelButton);
        HideControl(Self->SetKeyFrame);
        Self->SetRowsHiddenPublic(false);
        for (int Index = 0; Index < 14; ++Index)
            Self->OptText[Index]->SetProperty(0x24, 1, 0);
    }

    // The 0x18-byte record IRenderPipeline slot 10 fills (rp8 FUN_06d084f2 writes +0x00..+0x10:
    // mode index, width, height, refresh rate, rp pixel format). FUN_004af120 reads +0x04
    // ([ESP+0x10]), +0x08 ([ESP+0x14]) and +0x10 ([ESP+0x1c], CMP 1 and FUN_005b7030).
    struct FPipelineDisplayMode
    {
        int Index;       // +0x00
        int Width;       // +0x04
        int Height;      // +0x08
        int RefreshRate; // +0x0c
        int Format;      // +0x10 FUN_005b7010(0) before the call
        int Field14;     // +0x14 FUN_005b7010(0) before the call
    };

#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only replacement for the ctor's fixed five resolutions (0x004a982f..0x004a98bc): every
    // device resolution of at least 800x600 that is at least as wide as 4:3, ascending by width,
    // at most kPrefVideoResolutionCount of them (the largest kept), then the same list at 32 bpp.
    // The page keys its selection by width alone (SelWidth / CurWidth), so one height is kept
    // per width: the one whose aspect is closest to the desktop's (1280x720 over 1280x960 on a
    // 16:9 monitor). Unused entries get width -1, which FindVideoMode never matches.
    void BuildWidescreenModeTable(FPrefVideoMode* Modes)
    {
        int DesktopWidth = 4;
        int DesktopHeight = 3;
        WidescreenGetDesktopSize(&DesktopWidth, &DesktopHeight);
        double const DesktopAspect =
            static_cast<double>(DesktopWidth) / static_cast<double>(DesktopHeight);

        std::vector<FPrefVideoMode> Picked;
        unsigned int Count = 0;
        DALib::Pipeline->GetDisplayModeCount(&Count);
        for (unsigned int Index = 0; Index < Count; ++Index)
        {
            FPipelineDisplayMode Mode;
            Mode.Field14 = PixelFormatFromD3D(0);
            Mode.Format = PixelFormatFromD3D(0);
            if (DALib::Pipeline->GetDisplayModeEnum(Index, &Mode) != 0)
                continue;
            if (Mode.Format == 1)
                continue;
            if (Mode.Width < 0x320 || Mode.Height < 0x258)
                continue;
            if (Mode.Width * 3 < Mode.Height * 4)
                continue;

            double const Aspect = static_cast<double>(Mode.Width) / static_cast<double>(Mode.Height);
            bool bSameWidth = false;
            for (size_t Slot = 0; Slot < Picked.size(); ++Slot)
            {
                if (Picked[Slot].Width != Mode.Width)
                    continue;
                bSameWidth = true;
                double const Held =
                    static_cast<double>(Picked[Slot].Width) / static_cast<double>(Picked[Slot].Height);
                if (fabs(Aspect - DesktopAspect) < fabs(Held - DesktopAspect))
                    Picked[Slot].Height = Mode.Height;
                break;
            }
            if (!bSameWidth)
            {
                FPrefVideoMode Entry;
                Entry.Width = Mode.Width;
                Entry.Height = Mode.Height;
                Entry.Bpp = 0x10;
                Picked.push_back(Entry);
            }
        }

        std::sort(Picked.begin(), Picked.end(),
                  [](FPrefVideoMode const& A, FPrefVideoMode const& B) { return A.Width < B.Width; });
        size_t First = 0;
        if (Picked.size() > static_cast<size_t>(kPrefVideoResolutionCount))
            First = Picked.size() - static_cast<size_t>(kPrefVideoResolutionCount);

        for (int Index = 0; Index < kPrefVideoResolutionCount; ++Index)
        {
            size_t const Source = First + static_cast<size_t>(Index);
            if (Source < Picked.size())
            {
                Modes[Index].Width = Picked[Source].Width;
                Modes[Index].Height = Picked[Source].Height;
            }
            else
            {
                Modes[Index].Width = -1;
                Modes[Index].Height = -1;
            }
            Modes[Index + kPrefVideoResolutionCount].Width = Modes[Index].Width;
            Modes[Index + kPrefVideoResolutionCount].Height = Modes[Index].Height;
            Modes[Index].Bpp = 0x10;
            Modes[Index + kPrefVideoResolutionCount].Bpp = 0x20;
        }
    }
#endif
}

bool NnPreferences::FindVideoMode(DisplaySettings* Out, int Width, int Height, int Bpp) const
{
    // FUN_004af120 (RET 0x10; this unused): the first 4:3 device mode matching the non-zero
    // Width / Height and Bpp (Bpp <= 0: Out->color_bpp) lands in Out->width / height.
    Out->width = 0;
    Out->height = 0;
    unsigned int Count = 0;
    DALib::Pipeline->GetDisplayModeCount(&Count);
    for (unsigned int Index = 0; Index < Count; ++Index)
    {
        // 0x004af164..0x004af179: +0x14 then +0x10 seeded with FUN_005b7010(0).
        FPipelineDisplayMode Mode;
        Mode.Field14 = PixelFormatFromD3D(0);
        Mode.Format = PixelFormatFromD3D(0);
        if (DALib::Pipeline->GetDisplayModeEnum(Index, &Mode) != 0)
            continue;
        if (Mode.Format == 1)
            continue;
        if (Width != 0 && Width != Mode.Width)
            continue;
        if (Height != 0 && Height != Mode.Height)
            continue;
        // 0x004af1b9..0x004af1d1: FUN_005b7030(format)+4 against Bpp (Bpp > 0, JLE signed) or
        // against Out+0x10.
        int const ModeBpp = static_cast<int>(PixelFormatGetDesc(Mode.Format)->Bpp);
        if (Bpp > 0 ? Bpp != ModeBpp : Out->color_bpp != ModeBpp)
            continue;
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        // Port-only: any mode at least as wide as 4:3 (16:10, 16:9, ...); the cameras go Hor+.
        if (Mode.Width * 3 >= Mode.Height * 4)
#else
        if (Mode.Width * 3 == Mode.Height * 4)
#endif
        {
            Out->width = Mode.Width;
            Out->height = Mode.Height;
            break;
        }
    }
    // 0x004af202..0x004af20e: TEST / JBE on each (unsigned: non-zero).
    return Out->width != 0 && Out->height != 0;
}

void NnPreferences::LoadKeyList(char const* Path)
{
    // FUN_004af230
    KeyGroups[0].clear();
    KeyGroups[1].clear();
    KeyGroups[2].clear();
    INI_Reader Reader;
    if (!Reader.open(Path, false))
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Could not open %s",
              "E:\\FL\\Scratch\\Source\\Client\\nn\\NN_Preferences.cpp", 0x910, Path);
        return;
    }
    int Group = 0; // retail: uninitialised until the first [Group]
    while (Reader.read_header())
    {
        if (Reader.is_header("Group"))
        {
            Group = -1;
            while (Reader.read_value())
            {
                if (Reader.is_value("group_num"))
                    Group = Reader.get_value_int(0);
                else if (Reader.is_value("name"))
                    GroupNameIds[Group] = Reader.get_value_int(0);
            }
            if (Group == 1)
            {
                // 0x004af3a3: group 1 opens with a caption row.
                FKeyRow Caption = FKeyRow();
                Caption.Primary.NicknameId = 0;
                Caption.Primary.VirtualKey = -1;
                Caption.Primary.Modifiers = 0;
                Caption.NameIds = 0xc2e;
                Caption.InfoIds = 0xcf7;
                Caption.CommandId = 0;
                Caption.BindingKind = Group;
                Caption.bEditable = 0;
                KeyGroups[1].push_back(Caption);
            }
        }
        if (Reader.is_header("Key"))
        {
            int CommandId = -1;
            while (Reader.read_value())
            {
                if (Reader.is_value("id"))
                {
                    char const* const Nickname = Reader.get_value_string();
                    if (MsvcrtAtoi(Nickname) == -1)
                    {
                        CommandId = -1;
                    }
                    else
                    {
                        FKeyCmd* const Cmd = FindKeyCmdByNickname(Nickname);
                        if (Cmd != nullptr)
                            CommandId = static_cast<int>(Cmd->NicknameId);
                    }
                }
                else if (Reader.is_value("name"))
                {
                    Reader.get_value_int(0);
                }
            }
            AddKeyRow(CommandId, Group);
        }
    }
    Reader.close();
}

void NnPreferences::AddKeyRow(int CommandId, int Category)
{
    // FUN_004af5c0 (RET 8)
    if (Category < 0 || Category > 3)
        return;
    FKeyRow Row = FKeyRow();
    Row.bEditable = 1;
    int NameIds = -1;
    int InfoIds = -1;
    if (CommandId >= 0)
    {
        unsigned const Id = static_cast<unsigned>(CommandId);
        FKeyBindingLookup Lookup;
        Lookup.NicknameId = 0;
        Lookup.VirtualKey = 0;
        Lookup.Modifiers = 0;
        if (KeyMapFindPrimaryBinding(Id, &Lookup))
        {
            Row.Primary.NicknameId = Lookup.NicknameId;
            Row.Primary.VirtualKey = Lookup.VirtualKey;
            Row.Primary.Modifiers = Lookup.Modifiers;
            FKeyBindingLookup AltLookup;
            AltLookup.NicknameId = 0;
            AltLookup.VirtualKey = 0;
            AltLookup.Modifiers = 0;
            bool const bAlt = KeyMapFindAltBinding(Id, &AltLookup);
            Row.Alt.NicknameId = AltLookup.NicknameId;
            Row.Alt.VirtualKey = AltLookup.VirtualKey;
            Row.Alt.Modifiers = AltLookup.Modifiers;
            Row.BindingKind = bAlt ? 2 : 1;
        }
        else
        {
            Row.Primary.NicknameId = Lookup.NicknameId;
            Row.BindingKind = 3;
        }
        if (FindKeyCmdById(Id) != nullptr)
        {
            if (FindKeyCmdById(Id)->IdsName != 0)
            {
                NameIds = static_cast<int>(FindKeyCmdById(Id)->IdsName);
                if (Id == 0x14)
                    Row.bEditable = 0;
            }
            else
            {
                NameIds = -1;
            }
            if (FindKeyCmdById(Id)->IdsInfo != 0)
                InfoIds = static_cast<int>(FindKeyCmdById(Id)->IdsInfo);
            else
                InfoIds = -1;
        }
        Row.CommandId = Id;
    }
    else
    {
        Row.BindingKind = 0;
        Row.CommandId = 0;
    }
    Row.NameIds = NameIds;
    Row.InfoIds = InfoIds;
    if (Category < 3)
        KeyGroups[Category].push_back(Row);
}

void NnPreferences::FillKeyRows(int First)
{
    // FUN_004af900 (RET 4)
    std::vector<FKeyRow> const Rows = GetCurGroupRows();
    if (static_cast<unsigned>(First) > static_cast<unsigned>(static_cast<int>(Rows.size()) - 13))
        return;
    wchar_t Name[0x100];
    Name[0] = 0;
    for (int Index = 0; Index < 13; ++Index)
    {
        bool const bInRange =
            static_cast<unsigned>(First + Index) < static_cast<unsigned>(GetCurGroupRows().size());
        if (!bInRange)
            continue;
        FKeyRow const& Row = Rows[First + Index];
        NnTextControl* const KeyText = KeyRows[Index];
        NnTextControl* const AltText = KeyAltRows[Index];
        CommandRows[Index]->TextColour = G_NnPaletteArgb_679bb0;
        KeyBtnRows[Index]->EdgeMask = 0xa;
        if (Row.bEditable != 0)
        {
            KeyText->TextColour = G_NnPaletteArgb_679bb0;
            AltText->TextColour = G_NnPaletteArgb_679bb0;
        }
        else
        {
            KeyText->TextColour = G_NnPaletteArgb_679b94;
            AltText->TextColour = G_NnPaletteArgb_679b94;
        }
        FKeyBindingLookup Lookup;
        if (Row.BindingKind == 1)
        {
            Lookup.NicknameId = Row.Primary.NicknameId;
            Lookup.VirtualKey = Row.Primary.VirtualKey;
            Lookup.Modifiers = Row.Primary.Modifiers;
            KeyMapFormatBindingName(&Lookup, Name, 0x100);
            KeyText->SetText(Name);
            AltText->SetText(Row.bEditable != 0 ? L"" : Name); // 0x005c8ce0 is L""
        }
        else if (Row.BindingKind == 2)
        {
            Lookup.NicknameId = Row.Primary.NicknameId;
            Lookup.VirtualKey = Row.Primary.VirtualKey;
            Lookup.Modifiers = Row.Primary.Modifiers;
            KeyMapFormatBindingName(&Lookup, Name, 0x100);
            KeyText->SetText(Name);
            Lookup.NicknameId = Row.Alt.NicknameId;
            Lookup.VirtualKey = Row.Alt.VirtualKey;
            Lookup.Modifiers = Row.Alt.Modifiers;
            KeyMapFormatBindingName(&Lookup, Name, 0x100);
            AltText->SetText(Name);
        }
        else
        {
            KeyText->SetText(L"");
            AltText->SetText(L"");
        }
        if (Row.NameIds > 0)
            SetTextIds(CommandRows[Index], static_cast<unsigned>(Row.NameIds));
        else
            CommandRows[Index]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(L""), 0);
    }
}

void NnPreferences::BeginKeyCapture(int Row, bool bAlt)
{
    // FUN_004afb80 (RET 8)
    std::vector<FKeyRow> const Rows = GetCurGroupRows();
    FKeyRow const& Entry = Rows[Row];
    if (Entry.BindingKind == 0 || Entry.bEditable == 0)
        return;
    bKeyCapture = 1;
    Win3DPushCapture(this);
    CaptureRow = Entry;
    SetTextIds(DialogText[0], 0x5e6);
    SetTextIds(DialogText[1], static_cast<unsigned>(Entry.NameIds));
    SetTextIds(DialogButtonText[0], 0x698);
    SetTextIds(DialogButtonText[1], 0x2148);
    SetRowsHidden(true);
    ShowControl(SetKeyFrame);
    ShowControl(DialogText[0]);
    ShowControl(DialogText[1]);
    ShowControl(DialogCancelButton);
    ShowControl(DialogButtonText[1]);
    bool bOverwrite = false;
    if (bAlt)
    {
        bCaptureAlt = 1;
        bOverwrite = Entry.BindingKind == 2;
    }
    else
    {
        bCaptureAlt = 0;
        bOverwrite = Entry.BindingKind != 3;
    }
    if (bOverwrite)
    {
        ShowControl(OverwriteButton);
        ShowControl(DialogButtonText[0]);
    }
}

namespace
{
    // FUN_004afd80 (thiscall on the row, RET 8) -- one "option = id[, info, default, slider,
    // byte15[, min, max]]" line; a lone id is a header. With bSeed the matching perf option
    // (FUN_005b3770 by ids) takes float(Default * 0.01) as its value (its vt[0], a no-op in
    // every retail FPerfOption, is not modelled).
    void ParseOptLine(FPrefOption* Option, INI_Reader& Reader, bool bSeed)
    {
        unsigned const Count = Reader.get_num_parameters();
        Option->Id = Reader.get_value_int(0);
        if (Count > 1)
        {
            Option->InfoIds = Reader.get_value_int(1);
            Option->Default = Reader.get_value_int(2);
            Option->bSlider = static_cast<unsigned char>(Reader.get_value_bool(3));
            Option->Byte15 = static_cast<unsigned char>(Reader.get_value_bool(4));
            if (Count > 5)
            {
                Option->Min = Reader.get_value_int(5);
                Option->Max = Reader.get_value_int(6);
            }
        }
        else
        {
            Option->bHeader = 1;
        }
        if (!bSeed)
            return;
        FPerfOption* const Perf = FindPerfOptionByIds(Option->Id);
        if (Perf == nullptr)
            return;
        Perf->Value = static_cast<float>(Option->Default * 0.01);
    }

    FPrefOption BlankOption()
    {
        FPrefOption Option;
        Option.Id = -1;
        Option.InfoIds = -1;
        Option.Default = -1;
        Option.Min = -1;
        Option.Max = -1;
        Option.bSlider = 0;
        Option.Byte15 = 0;
        Option.bHeader = 0;
        Option.Pad17 = 0;
        return Option;
    }

    // One [Interface] / [Audio] / [Video] section: its options, then blank rows up to 15 when
    // it had 14 or fewer.
    void ReadOptSection(INI_Reader& Reader, std::vector<FPrefOption>& Options, bool bSeed)
    {
        int Count = 0;
        while (Reader.read_value())
        {
            if (!Reader.is_value("option"))
                continue;
            FPrefOption Option = BlankOption();
            ParseOptLine(&Option, Reader, bSeed);
            ++Count;
            Options.push_back(Option);
        }
        if (Count > 14)
            return;
        for (int Pad = 15 - Count; Pad != 0; --Pad)
            Options.push_back(BlankOption());
    }
}

bool NnPreferences::LoadOptList(char const* Path)
{
    // FUN_004afe30 (RET 4). The vectors are appended to, never cleared.
    INI_Reader Reader;
    bool const bOpened = Reader.open(Path, false);
    if (!bOpened)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Could not open %s",
              "E:\\FL\\Scratch\\Source\\Client\\nn\\NN_Preferences.cpp", 0x9f5, Path);
        return bOpened;
    }
    while (Reader.read_header())
    {
        if (Reader.is_header("Interface"))
            ReadOptSection(Reader, OptionsA, bSeedMode != 0);
        if (Reader.is_header("Audio"))
            ReadOptSection(Reader, OptionsB, bSeedMode != 0);
        if (Reader.is_header("Video"))
            ReadOptSection(Reader, OptionsC, bSeedMode != 0);
    }
    Reader.close();
    if (bSeedMode != 0)
        ApplyChangedPerfOptions();
    return bOpened;
}

void NnPreferences::TakeSnapshot()
{
    // FUN_004b0530: the current value of each of the 14 rows (headers -1, sliders their scroll
    // value, toggles 0/1), then the three preview sounds stop. The row copy is a local that is
    // only overwritten on pages 1..3 -- retail seeds it once (Id -1, bytes 0).
    if (bSeedMode != 0)
        return;
    FPrefOption Row = BlankOption();
    for (int Index = 0; Index < 14; ++Index)
    {
        if (CurPage == 1)
            Row = OptionsA[Index];
        else if (CurPage == 2)
            Row = OptionsC[Index];
        else if (CurPage == 3)
            Row = OptionsB[Index];
        if (Row.bHeader != 0)
        {
            Snapshot[Index] = -1.0f;
        }
        else if (Row.bSlider != 0)
        {
            UiScroll* const Scroll = Row.Id == 0x536 ? VideoModeCycle : OptScroll[Index];
            unsigned const Value = static_cast<unsigned>(Scroll->SetProperty(0x3b, 0, 0));
            Snapshot[Index] = static_cast<float>(static_cast<long long>(Value));
        }
        else
        {
            unsigned Value = 0;
            OptToggle[Index]->SetProperty(0x2f, reinterpret_cast<uintptr_t>(&Value), 0);
            int bOn = 1;
            if (static_cast<double>(static_cast<long long>(Value)) < 0.999)
                bOn = 0;
            Snapshot[Index] = static_cast<float>(bOn);
        }
    }
    StopUiSoundSlot(0x1e);
    StopUiSoundSlot(0x1f);
    StopUiSoundSlot(0x20);
}

void NnPreferences::ShowConfirmDialog(bool bShow)
{
    // FUN_004b2540 (RET 4)
    if (bCreating != 0)
        return;
    bConfirmMode = static_cast<unsigned char>(bShow);
    if (!bShow)
    {
        HideControl(DialogText[0]);
        HideControl(DialogText[1]);
        HideControl(DialogText[2]);
        HideControl(DialogButtonText[0]);
        HideControl(DialogButtonText[1]);
        HideControl(OverwriteButton);
        HideControl(DialogCancelButton);
        HideControl(SetKeyFrame);
        SetRowsHidden(false);
        return;
    }
    if (CurWidth != SelWidth || bCur32 != bSel32)
    {
        if (TryApplyVideoMode(SelWidth, bSel32))
        {
            bVideoConfirm = 1;
            wchar_t Text[0x40];
            GetIdsStringWWithRawFallback(0x779, Text, 0x40);
            DialogText[1]->SetText(Text);
            HideControl(CreditsText);
        }
        else
        {
            RefreshRows(0x536);
        }
    }
    wchar_t Question[0x40];
    GetIdsStringWWithRawFallback(0x778, Question, 0x40);
    DialogText[0]->SetText(Question);
    ShowKeyDialog();
    SetTextIds(DialogButtonText[0], 0x4dc);
    SetTextIds(DialogButtonText[1], 0x4dd);
}

void NnPreferences::CancelChanges()
{
    // FUN_004b2700
    if (CurPage == 0)
    {
        char const* const UserPath = EnsureUserKeyMapPath();
        char const* const DefaultPath = GetKeyMapIniRelPath();
        if (LoadUserKeyMapIni(UserPath) < 1)
            LoadUserKeyMapIni(DefaultPath);
        LoadKeyList(kKeyListPath);
        FillKeyRows(static_cast<int>(KeyScroll->SetProperty(0x3b, 0, 0)));
    }
    else
    {
        if (bVideoConfirm != 0)
        {
            TryApplyVideoMode(CurWidth, bCur32);
            bSel32 = bCur32;
            bVideoConfirm = 0;
            SelWidth = CurWidth;
            RefreshRows(0x536);
        }
        Revert(true);
    }
    bDirty = 0;
    CancelText->TextColour = G_NnPaletteArgb_679b94;
    CancelButton->bUseColour = 1;
    CancelButton->ButtonState = static_cast<unsigned char>(CancelButton->ButtonState & 0xfb);
    ShowConfirmDialog(false);
    LeaveCurrentPage(); // table 0x004b28c8
    if (bDirty == 0)
        GotoRequestedPage();
}

namespace
{
    // The perf-option write every setter here inlines: +0x10 = Value, vt[0](Value) (a no-op for
    // every retail option, not modelled -- see FPerfOption), then when +0x14 differs from +0x10
    // (FCOMP / TEST AH,0x44 / JNP: equal skips, unordered applies) +0x14 = +0x10 and vt[1].
    void SetPerfOptionValue(FPerfOption* Option, float Value)
    {
        Option->Value = Value;
        if (Option->Applied == Option->Value)
            return;
        Option->Applied = Option->Value;
        if (Option->Apply != nullptr)
            Option->Apply(Option->Value);
    }

    char const kOptListPath[] = "../Data/interface/optlist.ini"; // 0x005d4a28

    // The widget the "OptToggleText[Row]" slot resolves to: retail indexes +0x61c + Row * 4
    // without a range check, so the Row -1 its seed / defaults / preset callers pass lands on
    // +0x618, the last toggle button (FUN_004b1d20 0x004b1df5).
    WinControl* ToggleTextSlot(NnPreferences* Self, int Row)
    {
        if (Row >= 0)
            return Self->OptToggleText[Row];
        return Self->OptToggle[14 + Row];
    }
}

void NnPreferences::ApplyPercent(int Percent)
{
    // FUN_004b0370 (RET 4): every row with a default (+0x08 != -1) of the three sections, in
    // the order +0x504, +0x524, +0x544, gets it -- sliders through FUN_004b1450, toggles through
    // FUN_004b1d20, both with row -1. The size is re-read every pass.
    for (unsigned Index = 0; Index < OptionsA.size(); ++Index)
    {
        FPrefOption const Entry = OptionsA[Index];
        if (Entry.Default == -1)
            continue;
        if (Entry.bSlider != 0)
            SetSliderOption(Entry.Id, Entry.Default, -1);
        else
            SetToggleOption(Entry.Id, Entry.Default, -1);
    }
    for (unsigned Index = 0; Index < OptionsB.size(); ++Index)
    {
        FPrefOption const Entry = OptionsB[Index];
        if (Entry.Default == -1)
            continue;
        if (Entry.bSlider != 0)
            SetSliderOption(Entry.Id, Entry.Default, -1);
        else
            SetToggleOption(Entry.Id, Entry.Default, -1);
    }
    for (unsigned Index = 0; Index < OptionsC.size(); ++Index)
    {
        FPrefOption const Entry = OptionsC[Index];
        if (Entry.Default == -1)
            continue;
        if (Entry.bSlider != 0)
            SetSliderOption(Entry.Id, Entry.Default, -1);
        else
            SetToggleOption(Entry.Id, Entry.Default, -1);
    }

    // 0x004b04b0: -1 -> ftol(default_graphics +0x14 * 100.0f) (no null test).
    if (Percent == -1)
        Percent = static_cast<int>(static_cast<double>(FindPerfOption("default_graphics")->Applied) * 100.0);
    SetSliderOption(0x767, Percent, -1);

    // 0x004b04e3: advanced_graphics = 0 (no null test).
    SetPerfOptionValue(FindPerfOption("advanced_graphics"), 0.0f);
}

bool NnPreferences::SeedGraphicsDefaults()
{
    // FUN_004a9380 (cdecl): a bare NN_Preferences (operator_new(0x980), FUN_0059f860, the nine
    // vectors, the vptrs -- no Create) in seed mode (+0x32e = 1) reads optlist.ini, which stores
    // every option's default; then the machine score (FUN_004a92a0).
    NnPreferences* const Dialog = new NnPreferences();
    Dialog->bSeedMode = 1;
    Dialog->LoadOptList(kOptListPath);
    float const Score = ProbeMachineGraphicsLevel();

    // 0x004a94b7: FCOMP 0.0f / TEST AH,5 / JP -- only "less" returns (unordered goes on). The
    // dialog is not freed on this path.
    if (Score < 0.0f)
        return false;

    // 0x004a94dd..0x004a95a4: each option null-tested.
    FPerfOption* const Default = FindPerfOption("default_graphics");
    if (Default != nullptr)
        SetPerfOptionValue(Default, Score);
    FPerfOption* const General = FindPerfOption("general_graphics");
    if (General != nullptr)
        SetPerfOptionValue(General, Score);
    FPerfOption* const Advanced = FindPerfOption("advanced_graphics");
    if (Advanced != nullptr)
        SetPerfOptionValue(Advanced, 0.0f);

    // 0x004a95a7: FUN_004b0370(ftol(score * 100.0f)), then vt[0](1) -- the scalar deleting dtor.
    Dialog->ApplyPercent(static_cast<int>(static_cast<double>(Score) * 100.0));
    delete Dialog;
    return true;
}

void NnPreferences::SetSliderOption(int Id, int Value, int Row)
{
    // FUN_004b1450 (RET 0xc)
    if (Id == 0x534 || Id == 0x657)
    {
        // 0x004b146b: gamma (ability 1) and environment maps (ability 8) need the device; without
        // it the call ends here -- no dirty update.
        unsigned long Ability = 0;
        DALib::Pipeline->QueryDeviceAbility(Id == 0x534 ? 1u : 8u, &Ability);
        if (Ability == 0)
            return;
    }

    FPerfOption* const Option = FindPerfOptionByIds(Id);
    if (Option != nullptr && Id != 0x57f)
    {
        // 0x004b14bf: FILD / FMUL double 0.01 / FST float.
        float const Scaled = static_cast<float>(static_cast<double>(Value) * 0.01);
        SetPerfOptionValue(Option, Scaled);
        // 0x004b1501: the paired options follow (no null tests): sfx -> 0x583, music -> 0x584,
        // asteroids -> 0x57d and 0x5ca.
        if (Id == 0x538)
        {
            SetPerfOptionValue(FindPerfOptionByIds(0x583), Scaled);
        }
        else if (Id == 0x539)
        {
            SetPerfOptionValue(FindPerfOptionByIds(0x584), Scaled);
        }
        else if (Id == 0x57c)
        {
            SetPerfOptionValue(FindPerfOptionByIds(0x57d), Scaled);
            SetPerfOptionValue(FindPerfOptionByIds(0x5ca), Scaled);
        }
    }

    wchar_t Text[0x40];
    if (Id == 0x581)
    {
        // 0x004b18fd
        if (Row >= 0)
            PlayUiSoundSlotFlag1(0x1e);
    }
    else if (Id > 0x581)
    {
        if (Id == 0x687)
        {
            // 0x004b1974: max_submitted_sounds shows its count.
            swprintf(Text, 0x40, L"%d", Value); // 0x005d0ec4
            if (Row >= 0)
                SmallText[Row]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Text), 0);
        }
        else if (Id == 0x767)
        {
            // 0x004b19cd: the graphics preset drives every video row that has a range.
            for (unsigned Index = 0; Index < OptionsC.size(); ++Index)
            {
                FPrefOption const Entry = OptionsC[Index];
                if (Entry.Min == -1 || Entry.Id == 0x767)
                    continue;
                if (Entry.bSlider != 0)
                {
                    if (Value <= Entry.Min)
                    {
                        SetSliderOption(Entry.Id, 0, -1);
                    }
                    else if (Value >= Entry.Max)
                    {
                        SetSliderOption(Entry.Id, 100, -1);
                    }
                    else
                    {
                        // 0x004b1a7d: FILD (Max - Min) / FDIVR double 100.0 / FIMUL (Value - Min).
                        double const Scale = 100.0 / static_cast<double>(Entry.Max - Entry.Min);
                        SetSliderOption(Entry.Id,
                                        static_cast<int>(Scale * static_cast<double>(Value - Entry.Min)),
                                        -1);
                    }
                }
                else
                {
                    SetToggleOption(Entry.Id, (Value >= Entry.Min && Value <= Entry.Max) ? 1 : 0, -1);
                }
            }
            // 0x004b1ae4: general_graphics follows the preset (no null test).
            SetPerfOptionValue(FindPerfOption("general_graphics"),
                               static_cast<float>(static_cast<double>(Value) * 0.01));
            if (bSeedMode != 0)
            {
                RefreshRows(0x767);
            }
            else
            {
                bApplyingPreset = 1;
                RefreshRows(-1);
                bApplyingPreset = 0;
            }
        }
        else if (Id == 0x583)
        {
            // 0x004b1956
            if (Row >= 0)
                PlayUiSoundSlotFlag1(0x21);
        }
        else if (Id == 0x584)
        {
            // 0x004b1938
            if (Row >= 0)
                PlayUiSoundSlotFlag1(0x22);
        }
    }
    else
    {
        // 0x004b1643: the jump table 0x004b1b80 / 0x004b1b98 over 0x534..0x57f.
        switch (Id)
        {
        case 0x534:
        {
            // 0x004b18a5: gamma as "%3.2f" of Value * 0.01f.
            swprintf(Text, 0x40, L"%3.2f", static_cast<double>(Value) * static_cast<double>(0.01f));
            if (Row >= 0)
                SmallText[Row]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Text), 0);
            break;
        }
        case 0x536:
        {
            // 0x004b17cb: the video mode cycle; a negative value ends the call (no dirty update).
            if (Value < 0)
                return;
            int Match = 0;
            for (int Mode = 0; Mode < kPrefVideoModeCount; ++Mode)
            {
                if (CycleIndex[Mode] == Value)
                    Match = Mode;
            }
            FPrefVideoMode const Found = Modes[Match];
            SelWidth = Found.Width;
            swprintf(Text, 0x40, L"%d x %d", Found.Width, Found.Height); // 0x005d5190
            if (bBigVram != 0)
            {
                bSel32 = static_cast<unsigned char>(Found.Bpp == 0x20);
                // Retail formats the buffer into itself (0x004b1851 / 0x004b185e).
                wchar_t Size[0x40];
                wcscpy(Size, Text);
                swprintf(Text, 0x40, bSel32 != 0 ? L"%ls x 32" : L"%ls x 16", Size); // 0x005d5180 / 5170
            }
            if (Row >= 0)
            {
                VideoModeText->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Text), 0);
                RefreshRows(0x57e);
            }
            break;
        }
        case 0x538:
            // 0x004b1767
            if (Row >= 0)
                PlayUiSoundSlotFlag1(0x1f);
            break;
        case 0x539:
        {
            // 0x004b1785: the music preview plays only when no background music is bound.
            if (Row >= 0)
            {
                void* Source = nullptr;
                if (!BindClientBackgroundMusic(&Source)) // FUN_00428ba0
                    PlayUiSoundSlotFlag1(0x20);
                else
                    static_cast<ISoundSource*>(Source)->Release(); // vt+8
            }
            break;
        }
        case 0x57f:
        {
            // 0x004b1660: DIFFICULTY_SCALE = Value * 0.5, only when it differs from the shared
            // slot 6 (FUN_005b3510); then the server hears of it. The option is not null-tested.
            float const Scale = static_cast<float>(static_cast<double>(Value) * 0.5);
            if (GetPerfOptionSlot(6) != Scale)
            {
                SetPerfOptionValue(Option, Scale);
                unsigned const Connection = GetPlayerConnectionId();
                if (G_Server != nullptr && Connection != 0)
                    G_Server->ReqDifficultyScale(Scale, Connection); // slot 87 (+0x15c)
            }
            if (Row >= 0)
            {
                // 0x004b16e5: FUN_004347e0(0x702 + Value) for 0..3.
                // PORT: retail leaves the buffer as it was for any other value; the port blanks it.
                Text[0] = 0;
                if (static_cast<unsigned>(Value) <= 3)
                    GetIdsStringWWithRawFallback(0x702u + static_cast<unsigned>(Value), Text, 0x40);
                SmallText[Row]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Text), 0);
            }
            break;
        }
        default:
            break;
        }
    }

    // 0x004b1b62: FUN_004b1f30(-1, -1.0f).
    UpdateDirty(-1, -1.0f);
}

bool NnPreferences::TryApplyVideoMode(int Width, unsigned char b32)
{
    // FUN_004b1c00 (RET 8): the first 0x28 bytes of DAT_00679bc0 on the stack.
    DisplaySettings Mode;
    memcpy(&Mode, &g_display, 0x28);
    CurWidth = Mode.width;
    bCur32 = static_cast<unsigned char>(Mode.color_bpp == 0x20);
    int const Bpp = b32 != 0 ? 0x20 : 0x10;
    Mode.width = 0;
    Mode.height = 0;
    Mode.color_bpp = Bpp;
    for (int Index = 0; Index < kPrefVideoModeCount; ++Index)
    {
        if (bModeAvail[Index] == 0)
            continue;
        if (Modes[Index].Width != Width || Modes[Index].Bpp != Bpp)
            continue;
        FindVideoMode(&Mode, Modes[Index].Width, Modes[Index].Height, -1);
        if (bInGame == 0)
            ResetGameflowRoomLetterbox(); // FUN_00558d60(Width, Height) reads neither
    }
    if (Mode.width == 0 || Mode.height == 0)
        return false;
    SetRenderModeWithFallback(g_client.window, &Mode); // FUN_00424ce0(DAT_0067eca0, &Mode)
    ConfirmCountdown = 10.0;
    return true;
}

void NnPreferences::SetToggleOption(int Id, int Value, int Row)
{
    // FUN_004b1d20 (RET 0xc)
    if (Id == 0x534 || Id == 0x657)
    {
        unsigned long Ability = 0;
        DALib::Pipeline->QueryDeviceAbility(Id == 0x534 ? 1u : 8u, &Ability);
        if (Ability == 0)
            return;
    }

    // 0x004b1d72: FILD Value / FCOMP double 0.999 / TEST AH,0x41 -- only "greater" is on.
    int const bOn = static_cast<double>(Value) > 0.999 ? 1 : 0;
    FPerfOption* const Option = FindPerfOptionByIds(Id);
    if (Option != nullptr)
    {
        SetPerfOptionValue(Option, static_cast<float>(bOn));
        if (bSeedMode == 0)
        {
            wchar_t Text[0x40];
            GetIdsStringWWithRawFallback(bOn != 0 ? 0x65fu : 0x67au, Text, 0x40);
            ToggleTextSlot(this, Row)->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Text), 0);
        }
    }

    if (Id == 0x57e)
    {
        // 0x004b1e53: "apply video mode" -- ask to keep it, or redraw the rows.
        if (TryApplyVideoMode(SelWidth, bSel32))
        {
            bVideoConfirm = 1;
            wchar_t Text[0x40];
            GetIdsStringWWithRawFallback(0x778, Text, 0x40);
            DialogText[0]->SetText(Text);
            GetIdsStringWWithRawFallback(0x779, Text, 0x40);
            DialogText[1]->SetText(Text);
            ShowKeyDialog();
            for (int Index = 0; Index < 14; ++Index)
                OptText[Index]->SetProperty(0x24, 0, 0);
        }
        else
        {
            RefreshRows(-1);
        }
    }

    // 0x004b1f02
    UpdateDirty(Id, static_cast<float>(bOn));
}

void NnPreferences::UpdateDirty(int Id, float Value)
{
    // FUN_004b1f30 (RET 8): a pending video mode, or any row that no longer matches its
    // snapshot, makes the page dirty.
    if (bSeedMode != 0)
        return;
    bool bFound = CurWidth != SelWidth || bCur32 != bSel32;
    // The row copy: Id -1 and the three bytes 0 before the loop, overwritten only on pages 1..3.
    FPrefOption Row = BlankOption();
    for (int Index = 0; Index < 14 && !bFound; ++Index)
    {
        if (CurPage == 1)
            Row = OptionsA[Index];
        else if (CurPage == 2)
            Row = OptionsC[Index];
        else if (CurPage == 3)
            Row = OptionsB[Index];
        if (Row.bHeader != 0)
            continue;
        if (Row.bSlider != 0)
        {
            if (Row.Id == 0x536)
                continue;
            // 0x004b1ff2: ftol(snapshot) against ftol(the scroll value as an unsigned qword).
            unsigned const ScrollValue = static_cast<unsigned>(OptScroll[Index]->SetProperty(0x3b, 0, 0));
            if (static_cast<int>(Snapshot[Index]) != static_cast<int>(static_cast<long long>(ScrollValue)))
                bFound = true;
            continue;
        }
        // 0x004b2037: toggles count only while visible.
        if ((OptToggle[Index]->Flags & 2) == 0)
            continue;
        if (Row.Id == Id)
        {
            // 0x004b204d: FLD snapshot / FCOMP Value / TEST AH,0x44 / JP -- equal marks it.
            if (Snapshot[Index] == Value)
                bFound = true;
            continue;
        }
        unsigned State = 0;
        OptToggle[Index]->SetProperty(0x2f, reinterpret_cast<uintptr_t>(&State), 0);
        int const bOn = static_cast<double>(static_cast<long long>(State)) > 0.999 ? 1 : 0;
        if (static_cast<float>(bOn) != Snapshot[Index])
            bFound = true;
    }

    // 0x004b20d7
    bDirty = static_cast<unsigned char>(bFound);
    CancelText->TextColour = bFound ? G_NnPaletteArgb_679bb0 : G_NnPaletteArgb_679b94;
    CancelButton->bUseColour = static_cast<unsigned char>(!bFound);
    CancelButton->ButtonState =
        static_cast<unsigned char>((CancelButton->ButtonState & 0xfb) | (bFound ? 4 : 0));
}

namespace
{
    // FUN_004b2130's two inlined record blocks (0x004b21d7..0x004b2259, 0x004b2338..0x004b239b):
    // caption null, the Font::String constructed (FUN_004144b0), FUN_004963a0's stores, Ids =
    // IdsAlt = the row's info ids, FUN_004b6420(&record, 0, 0.2f), then the record's dtor.
    void ShowRowTipRecord(int InfoIds)
    {
        FToolTipHoverInfo Record; // caption null + FUN_004144b0; dtor FUN_00444ee0
        ToolTipHoverReset(&Record);
        Record.Ids = InfoIds;
        Record.IdsAlt = InfoIds;
        NnRolloverShowHoverAtGlobal(&Record, 0.0f, 0.2f); // 0x3e4ccccd
    }
}

void NnPreferences::ShowRowTip(int Row)
{
    // FUN_004b2130 (RET 4): the row's info ids in the rollover (FUN_004b6420 with a record
    // {Ids, Ids, empty caption}, 0, 0.2f).
    if (CurPage == 0)
    {
        int const First = static_cast<int>(KeyScroll->SetProperty(0x3b, 0, 0));
        // PORT: retail reads an uninitialised local for a group outside 0..2 (0x004b21cb);
        // SelectGroup only ever stores 0..2.
        int InfoIds = 0;
        switch (CurGroup)
        {
        case 0: InfoIds = KeyGroups[0][First + Row].InfoIds; break;
        case 1: InfoIds = KeyGroups[1][First + Row].InfoIds; break;
        case 2: InfoIds = KeyGroups[2][First + Row].InfoIds; break;
        default: break;
        }
        if (InfoIds > 0)
            ShowRowTipRecord(InfoIds);
        return;
    }
    FPrefOption Entry;
    if (CurPage == 1)
        Entry = OptionsA[Row];
    else if (CurPage == 2)
        Entry = OptionsC[Row];
    else if (CurPage == 3)
        Entry = OptionsB[Row];
    else
        return;
    // 0x004b2314: nothing while a key is captured or a video mode waits for confirmation.
    if (Entry.InfoIds <= 0 || bKeyCapture != 0 || bVideoConfirm != 0)
        return;
    ShowRowTipRecord(Entry.InfoIds);
}

void NnPreferences::ProbeModes(int)
{
    // FUN_004b2440 (RET 4; the argument is not read).
    DisplaySettings Mode;
    memcpy(&Mode, &g_display, 0x28);
    Mode.width = 0;
    Mode.height = 0;
    unsigned long VideoMemory = 0;
    DALib::Pipeline->QueryDeviceAbility(3, &VideoMemory);
    VideoMemory >>= 20;
    bBigVram = static_cast<unsigned char>(VideoMemory >= 0x11);
    ModeCount = 0;
    for (int Index = 0; Index < kPrefVideoModeCount; ++Index)
    {
        CycleIndex[Index] = -1;
        bModeAvail[Index] = static_cast<unsigned char>(
            FindVideoMode(&Mode, Modes[Index].Width, Modes[Index].Height, Modes[Index].Bpp));
        if (bModeAvail[Index] == 0)
            continue;
        // 0x004b24e8: 32-bit modes need more than 16 MB of video memory.
        if (bBigVram == 0 && Modes[Index].Bpp != 0x10)
        {
            bModeAvail[Index] = 0;
            continue;
        }
        CycleIndex[Index] = ModeCount;
        ++ModeCount;
    }
}

void NnPreferences::Revert(bool bApply)
{
    // FUN_004b06a0 (RET 4): every row that moved away from its snapshot goes back; with bApply
    // the widgets follow and the row index is passed on.
    if (bSeedMode != 0)
        return;
    FPrefOption Row = BlankOption();
    for (int Index = 0; Index < 14; ++Index)
    {
        if (CurPage == 1)
            Row = OptionsA[Index];
        else if (CurPage == 2)
            Row = OptionsC[Index];
        else if (CurPage == 3)
            Row = OptionsB[Index];
        if (Row.bHeader != 0)
            continue;
        if (Row.bSlider != 0)
        {
            if (Row.Id == 0x536)
            {
                // 0x004b0740: back to the current video mode, every matching entry.
                if (CurWidth == SelWidth && bCur32 == bSel32)
                    continue;
                for (int Mode = 0; Mode < kPrefVideoModeCount; ++Mode)
                {
                    if (bModeAvail[Mode] == 0 || Modes[Mode].Width != CurWidth)
                        continue;
                    if (static_cast<int>(bCur32) != (Modes[Mode].Bpp == 0x20 ? 1 : 0))
                        continue;
                    if (bApply)
                    {
                        VideoModeCycle->SetProperty(0x38, static_cast<uintptr_t>(CycleIndex[Mode]), 0);
                        SetSliderOption(0x536, CycleIndex[Mode], Index);
                    }
                    else
                    {
                        SetSliderOption(0x536, CycleIndex[Mode], -1);
                    }
                }
                continue;
            }
            // 0x004b07e6: FILD qword (the scroll value) / FCOMP snapshot -- equal skips.
            unsigned const ScrollValue = static_cast<unsigned>(OptScroll[Index]->SetProperty(0x3b, 0, 0));
            if (static_cast<double>(static_cast<long long>(ScrollValue)) == static_cast<double>(Snapshot[Index]))
                continue;
            int const Restored = static_cast<int>(Snapshot[Index]);
            if (bApply)
            {
                OptScroll[Index]->SetProperty(0x38, static_cast<uintptr_t>(Restored), 0);
                SetSliderOption(Row.Id, Restored, Index);
            }
            else
            {
                SetSliderOption(Row.Id, Restored, -1);
            }
            continue;
        }
        // 0x004b0868: toggles only while their text is visible.
        if ((OptToggleText[Index]->Flags & 2) == 0)
            continue;
        // FLD snapshot / FCOMP double 0.999 / TEST AH,1: "less" (and unordered) is off; the
        // button and the option get the opposite (SETZ).
        int const bOn = static_cast<double>(Snapshot[Index]) >= 0.999 ? 1 : 0;
        int const State = bOn == 0 ? 1 : 0;
        OptToggle[Index]->SetProperty(0x30, static_cast<uintptr_t>(State), 0);
        SetToggleOption(Row.Id, State, bApply ? Index : -1);
    }
    StopUiSoundSlot(0x1e);
    StopUiSoundSlot(0x1f);
    StopUiSoundSlot(0x20);
}

namespace
{
    // The colour / state block RefreshRows writes per row (0x004b0bae greyed, 0x004b0c58 live).
    void SetOptionRowGreyed(NnPreferences* Self, int Index, bool bGreyed)
    {
        unsigned long const Colour = bGreyed ? G_NnPaletteArgb_679b94 : G_NnPaletteArgb_679bb0;
        Self->OptText[Index]->TextColour = Colour;
        Self->SmallButtonText[Index]->TextColour = Colour;
        WireButtonControl* const Small = Self->SmallButton[Index];
        if (bGreyed)
        {
            Small->bUseColour = 1;
            Small->ButtonState = static_cast<unsigned char>(Small->ButtonState & 0xfb);
        }
        else
        {
            Small->bUseColour = 0;
            Small->ButtonState = static_cast<unsigned char>(Small->ButtonState | 4);
        }
        Self->OptToggleText[Index]->TextColour = Colour;
        NnButtonControl* const Toggle = Self->OptToggle[Index];
        if (bGreyed)
        {
            Toggle->bUseColour = 1;
            Toggle->ButtonBits2 = static_cast<unsigned char>(Toggle->ButtonBits2 & 0xfd);
        }
        else
        {
            Toggle->bUseColour = 0;
            Toggle->ButtonBits2 = static_cast<unsigned char>(Toggle->ButtonBits2 | 2);
        }
        Self->ScrollTextLeft[Index]->TextColour = Colour;
        Self->ScrollTextRight[Index]->TextColour = Colour;
        Self->SmallText[Index]->TextColour = Colour;
        Self->OptScroll[Index]->SetCustomColorField(bGreyed); // +0x32d
        Self->OptScroll[Index]->SetActiveField(!bGreyed);     // +0x32c
    }
}

void NnPreferences::RefreshRows(int Id)
{
    // FUN_004b0920 (RET 4): lay the 14 rows of the current page out from its section; Id -1 does
    // every row, anything else only the row(s) with that option id.
    if (bSeedMode == 0)
    {
        // 0x004b0936: row 0's slider and caption back to their defaults (the preset row
        // changes them below).
        OptScroll[0]->SetSize(NnFloatBits(0x3e75c28f), NnFloatBits(0x3c656042));
        SetPosition3(OptScroll[0], 0.0f, 0.0f, 0.0f);
        OptText[0]->SetFontByName("SubHeader");
        OptText[0]->SetWidth(NnFloatBits(0x3e5c28f6));
    }

    FPrefOption Row = BlankOption();
    for (int Index = 0; Index < 14; ++Index)
    {
        bool bDisabled = false;
        if (bSeedMode == 0)
            OptToggle[Index]->ButtonBits2 = static_cast<unsigned char>(OptToggle[Index]->ButtonBits2 | 2);
        // 0x004b09c9: only the id and the three bytes are reset; pages 1..3 copy the row.
        Row.Id = -1;
        Row.bSlider = 0;
        Row.Byte15 = 0;
        Row.bHeader = 0;
        if (CurPage == 1 || CurPage == 2 || CurPage == 3)
        {
            if (CurPage == 1)
                Row = OptionsA[Index];
            else if (CurPage == 2)
                Row = OptionsC[Index];
            else
                Row = OptionsB[Index];
            if (Row.Id == 0x534 || Row.Id == 0x657)
            {
                unsigned long Ability = 0;
                DALib::Pipeline->QueryDeviceAbility(Row.Id == 0x534 ? 1u : 8u, &Ability);
                bDisabled = Ability == 0;
            }
            else if (Row.Id == 0x67f)
            {
                // 0x004b0a7e: dithering is pointless in 32-bit colour (a copy of the first 0x28
                // bytes of DAT_00679bc0, its +0x10).
                bDisabled = g_display.color_bpp == 0x20;
            }
            else if (Row.Id == 0x57f)
            {
                bDisabled = !SinglePlayer();
            }
        }
        int const RowId = Row.Id;
        if (Id != -1 && Id != RowId)
            continue;

        // 0x004b0ac9
        OptScroll[Index]->SetProperty(0x37, 0, 100);
        HideControl(OptToggle[Index]);
        HideControl(OptTitleText[Index]);
        HideControl(OptScroll[Index]);
        HideControl(SmallText[Index]);
        HideControl(ScrollTextLeft[Index]);
        HideControl(ScrollTextRight[Index]);
        HideControl(SmallButton[Index]);
        HideControl(SmallButtonText[Index]);
        HideControl(OptToggleText[Index]);
        if (RowId == -1)
        {
            HideControl(OptText[Index]);
            continue;
        }

        wchar_t Text[0x40];
        if (Row.bHeader != 0)
        {
            // 0x004b0b48: a header row is its title only.
            HideControl(OptText[Index]);
            GetIdsStringWWithRawFallback(static_cast<unsigned>(RowId), Text, 0x40);
            OptTitleText[Index]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Text), 0);
            ShowControl(OptTitleText[Index]);
            continue;
        }
        if (bDisabled)
            continue;

        // 0x004b0b94: "not in game" rows are greyed while the window is open in game.
        SetOptionRowGreyed(this, Index, Row.Byte15 != 0 && bInGame != 0);
        ShowControl(OptText[Index]);
        GetIdsStringWWithRawFallback(static_cast<unsigned>(RowId), Text, 0x40);
        OptText[Index]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Text), 0);

        FPerfOption* const Option = FindPerfOptionByIds(RowId);
        if (Option != nullptr)
        {
            if (Row.bSlider != 0)
            {
                // 0x004b0d58: ftol(+0x14 * 100.0f).
                OptScroll[Index]->SetProperty(
                    0x38, static_cast<uintptr_t>(static_cast<int>(static_cast<double>(Option->Applied) * 100.0)), 0);
                ShowControl(OptScroll[Index]);
            }
            else
            {
                // 0x004b0d82: FCOMP double 0.999 / TEST AH,0x41 -- only "greater" is on.
                int const bOn = static_cast<double>(Option->Applied) > 0.999 ? 1 : 0;
                OptToggle[Index]->SetProperty(0x30, static_cast<uintptr_t>(bOn), 0);
                ShowControl(OptToggle[Index]);
                GetIdsStringWWithRawFallback(bOn != 0 ? 0x65fu : 0x67au, Text, 0x40);
                OptToggleText[Index]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Text), 0);
                ShowControl(OptToggleText[Index]);
            }
        }

        // 0x004b0e14
        switch (RowId)
        {
        case 0x534:
        {
            // 0x004b1179: gamma's value as "%3.2f".
            if (Option == nullptr)
                break;
            wchar_t Value[0x40];
            swprintf(Value, 0x40, L"%3.2f", static_cast<double>(Option->Applied)); // 0x005d5164
            SmallText[Index]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Value), 0);
            ShowControl(SmallText[Index]);
            break;
        }
        case 0x536:
        {
            // 0x004b0f0d: the video mode cycle moves up to this row: y = -Index * 0.035f.
            float const Y = static_cast<float>(static_cast<double>(-Index) * static_cast<double>(NnFloatBits(0x3d0f5c29)));
            SetPosition3(VideoModeCycle, 0.0f, Y, 0.0f);
            float const Y2 = Y + Y;
            SetPosition3(VideoBox, 0.0f, Y2, 0.0f);
            SetPosition3(VideoBox2, 0.0f, Y2, 0.0f);
            SetPosition3(VideoModeText, 0.0f, Y, 0.0f);
            VideoModeCycle->SetProperty(0x37, 0, static_cast<uintptr_t>(ModeCount - 1));

            // 0x004b0fd7: the current display mode's entry (the last match wins).
            wchar_t ModeText[0x40];
            bool bFound = false;
            for (int Mode = 0; Mode < kPrefVideoModeCount; ++Mode)
            {
                if (bModeAvail[Mode] == 0)
                    continue;
                if (g_display.width != Modes[Mode].Width || g_display.color_bpp != Modes[Mode].Bpp)
                    continue;
                CurWidth = Modes[Mode].Width;
                SelWidth = Modes[Mode].Width;
                bSel32 = static_cast<unsigned char>(Modes[Mode].Bpp == 0x20);
                swprintf(ModeText, 0x40, L"%d x %d", Modes[Mode].Width, Modes[Mode].Height); // 0x005d5190
                VideoModeCycle->SetProperty(0x38, static_cast<uintptr_t>(CycleIndex[Mode]), 0);
                bFound = true;
            }
            if (!bFound)
            {
                // 0x004b10a3: none -- the display's own size, cycle at 0.
                CurWidth = g_display.width;
                SelWidth = g_display.width;
                bSel32 = static_cast<unsigned char>(g_display.color_bpp == 0x20);
                swprintf(ModeText, 0x40, L"%d x %d", g_display.width, g_display.height);
                VideoModeCycle->SetProperty(0x38, 0, 0);
            }
            if (bBigVram != 0)
            {
                // Retail formats the buffer into itself (0x004b1113 / 0x004b1123).
                wchar_t Size[0x40];
                wcscpy(Size, ModeText);
                swprintf(ModeText, 0x40, bSel32 != 0 ? L"%ls x 32" : L"%ls x 16", Size);
            }
            VideoModeText->SetProperty(0x1d, reinterpret_cast<uintptr_t>(ModeText), 0);
            ShowControl(VideoModeCycle);
            ShowControl(VideoBox);
            ShowControl(VideoBox2);
            ShowControl(VideoModeText);
            break;
        }
        case 0x57e:
        {
            // 0x004b0e45: the "apply" button, live while a different mode is selected.
            ShowControl(SmallButton[Index]);
            ShowControl(SmallButtonText[Index]);
            GetIdsStringWWithRawFallback(0x60b, Text, 0x40);
            SmallButtonText[Index]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Text), 0);
            if (SelWidth == CurWidth && bSel32 == bCur32)
            {
                SmallButtonText[Index]->TextColour = G_NnPaletteArgb_679b94;
                SmallButton[Index]->bUseColour = 1;
                SmallButton[Index]->ButtonState = static_cast<unsigned char>(SmallButton[Index]->ButtonState & 0xfb);
            }
            else
            {
                SmallButtonText[Index]->TextColour = G_NnPaletteArgb_679bb0;
                SmallButton[Index]->bUseColour = 0;
                SmallButton[Index]->ButtonState = static_cast<unsigned char>(SmallButton[Index]->ButtonState | 4);
            }
            break;
        }
        case 0x57f:
        {
            // 0x004b11ac: difficulty 0..3 from DIFFICULTY_SCALE * 2 (the option is not
            // null-tested here).
            float const Applied = Option->Applied;
            OptScroll[Index]->SetProperty(0x37, 0, 3);
            int const Level = static_cast<int>(static_cast<double>(Applied) + static_cast<double>(Applied));
            OptScroll[Index]->SetProperty(0x38, static_cast<uintptr_t>(Level), 0);
            ShowControl(OptScroll[Index]);
            // Jump table 0x004b143c: ids 0x702..0x705.
            // PORT: retail leaves the buffer as it was for any other level; the port blanks it.
            wchar_t LevelText[0x40];
            LevelText[0] = 0;
            if (static_cast<unsigned>(Level) <= 3)
                GetIdsStringWWithRawFallback(0x702u + static_cast<unsigned>(Level), LevelText, 0x40);
            SmallText[Index]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(LevelText), 0);
            ShowControl(SmallText[Index]);
            break;
        }
        case 0x687:
        {
            // 0x004b1367: max_submitted_sounds, 8..32.
            if (Option == nullptr)
                break;
            OptScroll[Index]->SetProperty(0x37, 8, 0x20);
            float const Count = static_cast<float>(GetMaxSubmittedSounds()); // FUN_005b3860
            OptScroll[Index]->SetProperty(0x38, static_cast<uintptr_t>(static_cast<int>(Count)), 0);
            ShowControl(OptScroll[Index]);
            wchar_t CountText[0x40];
            swprintf(CountText, 0x40, L"%1.0f", static_cast<double>(Count)); // 0x005d5048
            SmallText[Index]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(CountText), 0);
            ShowControl(SmallText[Index]);
            break;
        }
        case 0x767:
        {
            // 0x004b12d3: the graphics preset row gets a longer slider and a larger caption.
            OptScroll[Index]->SetSize(NnFloatBits(0x3eb4bc6a), NnFloatBits(0x3c9374bc));
            SetPosition3(OptScroll[Index], NnFloatBits(0xbde147ae), 0.0f, 0.0f);
            if (bApplyingPreset == 0)
            {
                FPerfOption* const General = FindPerfOption("general_graphics");
                OptScroll[Index]->SetProperty(
                    0x38, static_cast<uintptr_t>(static_cast<int>(static_cast<double>(General->Applied) * 100.0)), 0);
            }
            ShowControl(OptScroll[Index]);
            OptText[Index]->SetWidth(NnFloatBits(0x3e3d70a4));
            OptText[Index]->SetFontByName("PageHeader");
            break;
        }
        default:
        {
            // 0x004b128c: a row without a perf option shows its caption only.
            if (Option != nullptr)
                break;
            HideControl(OptText[Index]);
            GetIdsStringWWithRawFallback(static_cast<unsigned>(RowId), Text, 0x40);
            OptText[Index]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Text), 0);
            ShowControl(OptText[Index]);
            break;
        }
        }
    }
}

bool NnPreferences::Create(Win3D* NewParent, void* Data)
{
    // FUN_004a9790 (RET 8).
    //
    // 0x004a97b3..0x004a9829: the state bytes and doubles.
    bCreating = 1;
    bSeedMode = 0;
    FlipAngle = 0.0;
    FlipSpeed = 360.0;              // 0x40768000:00000000
    bFlipping = 0;
    bFlipHalf = 0;
    bClosingAnim = 0;
    Anim = -2.0;                    // 0xc0000000:00000000
    bBigVram = 0;
    bKeyCapture = 0;
    bVideoConfirm = 0;
    bClosed = 0;
    bApplyingPreset = 0;
    bDirty = 0;
    bConflict = 0;
    bConfirmMode = 0;

#ifdef FREELANCER_WIDESCREEN_SUPPORT
    BuildWidescreenModeTable(Modes);
#else
    // 0x004a982f..0x004a98bc: the five resolutions at 16 bpp, copied to 5..9 at 32 bpp.
    int const kModeWidths[5] = {0x320, 0x400, 0x480, 0x500, 0x640};
    int const kModeHeights[5] = {0x258, 0x300, 0x360, 0x3c0, 0x4b0};
    for (int Index = 0; Index < 5; ++Index)
    {
        Modes[Index].Width = kModeWidths[Index];
        Modes[Index].Height = kModeHeights[Index];
    }
    for (int Index = 0; Index < 5; ++Index)
    {
        Modes[Index + 5].Width = Modes[Index].Width;
        Modes[Index + 5].Height = Modes[Index].Height;
        Modes[Index].Bpp = 0x10;
        Modes[Index + 5].Bpp = 0x20;
    }
#endif
    // 0x004a98be..0x004a98db
    for (int Index = 0; Index < kPrefVideoModeCount; ++Index)
    {
        bModeAvail[Index] = 0;
        CycleIndex[Index] = -1;
    }
    ModeCount = 0;
    ProbeModes(-1);

    // 0x004a98ec..0x004a9920: payload null -> opened in game (the game is paused, FUN_004a7670).
    bInGame = static_cast<unsigned char>(Data == nullptr);
    CurGroup = 0;
    G_bNnScreenUp = true;
    if (bInGame != 0)
        bPausedGame = NnModalPauseGame();
    CurPage = 0;

    float const Zero[3] = {0.0f, 0.0f, 0.0f};
    if (bInGame != 0)
    {
        // 0x004a9958..0x004a9a31: FUN_005a0db0 with NoMeshRender 0, NoWireRender 1,
        // NoColoring 1, RenderToWindow 0, NoClearRender 1 -- neither result is kept.
        float const PlanePosition[3] = {0.0f, 0.0f, -1.0f};
        CreateStaticControl("Options_BlackPlane", "Interface\\NeuroNet\\MISC_bigblackplane.3db",
                            PlanePosition, nullptr, Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);
        float const BoxPosition[3] = {0.0f, NnFloatBits(0xbd99999a), -1.0f};
        CreateStaticControl("OptionsBox", "Interface\\NeuroNet\\Inventory\\FRONT_options_box2.cmp",
                            BoxPosition, nullptr, Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);
    }
    else
    {
        // 0x004a9a38..0x004a9a8e -> +0x338.
        float const BoxPosition[3] = {NnFloatBits(0x3e19999a), NnFloatBits(0xbd50e560), -1.0f};
        BigBox = CreateStaticControl("big box",
                                     "Interface\\NeuroNet\\Inventory\\FRONT_shortoptions_box2.cmp",
                                     BoxPosition, nullptr, Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);
    }

    // 0x004a9a94..0x004a9aee: a copy of the display mode (DAT_00679bc0, 10 dwords); 32 bpp
    // without the big-VRAM flag counts as 16.
    SelWidth = 0;
    CurWidth = 0;
    bCur32 = static_cast<unsigned char>(g_display.color_bpp == 0x20 ? 1 : 0);
    bSel32 = bCur32;
    if (bBigVram == 0 && bCur32 == 1)
    {
        bCur32 = 0;
        bSel32 = 0;
    }

    // 0x004a9af5..0x004a9b7b: the in-game close box and the title height.
    float TitlePosition[3] = {NnFloatBits(0xbdb851ec), 0.0f, 0.0f};
    if (bInGame != 0)
    {
        float const ClosePosition[3] = {NnFloatBits(0x3edc28f6), NnFloatBits(0x3e6c8b44), 0.0f};
        float const CloseExtents[3] = {NnFloatBits(0x3ca3d70a), NnFloatBits(0x3ca3d70a), 0.0f};
        CloseButton = CreateButtonControl("PreferencesCloseButton",
                                          "Interface\\NeuroNet\\Inventory\\X.3db", nullptr,
                                          "ui_select_item", nullptr, ClosePosition, nullptr,
                                          CloseExtents, 0, 0, 0, 0, 0x409, 0, 0);
        TitlePosition[1] = NnFloatBits(0x3e7ced91);
    }
    else
    {
        TitlePosition[1] = NnFloatBits(0x3e8ed917);
    }

    // 0x004a9b83..0x004a9bc7: FUN_005a0e30("Screen Title", "PageHeader", ..., 0, 0.481, 0.06,
    // 1.75, 0x4f8, 1, 0, 1) -> +0x344, colour DAT_00679bb0.
    Title = static_cast<NnTextControl*>(
        CreateTextControl("Screen Title", "PageHeader", TitlePosition, nullptr,
                          NnFloatBits(0x3ef645a2), NnFloatBits(0x3d75c28f), NnFloatBits(0x3fe00000),
                          0x4f8, 1, 0, 1));
    Title->TextColour = G_NnPaletteArgb_679bb0;

    // 0x004a9bcd..0x004a9c07: the page column's origin and step.
    float ColumnX = NnFloatBits(0xbe99999a);
    float ColumnY = NnFloatBits(0x3e51eb85);
    float Step = NnFloatBits(0x3d9db22d);
    if (bInGame == 0)
    {
        ColumnX = NnFloatBits(0xbea8f5c3);
        ColumnY = NnFloatBits(0x3e570a3d);
        Step = NnFloatBits(0x3dae147b);
    }

    // 0x004a9c07..0x004a9ed7: the page buttons, FUN_005a0c90(name, FRONT_button.cmp, 0,
    // "ui_select_item", 0, {X, Y, 0}, 0, {0.13, 0.026, 0}, 0, 0, 0, 0, 0, 0, 0); all but the
    // credits button get FUN_0055bfc0(1). The step multiples are FSTP'd floats (Step + Step,
    // Step * 3.0f, Step * 6.0f); the credits row keeps Step * 4.0f in the x87 register.
    char const kButtonMesh[] = "Interface\\NeuroNet\\Inventory\\FRONT_button.cmp";
    float const ButtonExtents[3] = {NnFloatBits(0x3e051eb8), NnFloatBits(0x3cd4fdf4), 0.0f};
    float const KeymapExtents[3] = {NnFloatBits(0x3e051eb8), NnFloatBits(0x3ccccccd), 0.0f};
    float const Step2 = static_cast<float>(static_cast<double>(Step) + static_cast<double>(Step));
    float const Step3 = static_cast<float>(static_cast<double>(Step) * 3.0);
    float const Step6 = static_cast<float>(static_cast<double>(Step) * 6.0);
    {
        float Position[3] = {ColumnX, ColumnY, 0.0f};
        PageButtons[1] = static_cast<NnButtonControl*>(
            CreateButtonControl("interfaceButton", kButtonMesh, nullptr, "ui_select_item", nullptr,
                                Position, nullptr, ButtonExtents, 0, 0, 0, 0, 0, 0, 0));
        PageButtons[1]->SetMontageMode(1);

        Position[1] = static_cast<float>(static_cast<double>(ColumnY) - static_cast<double>(Step));
        PageButtons[0] = static_cast<NnButtonControl*>(
            CreateButtonControl("keymapButton", kButtonMesh, nullptr, "ui_select_item", nullptr,
                                Position, nullptr, KeymapExtents, 0, 0, 0, 0, 0, 0, 0));
        PageButtons[0]->SetMontageMode(1);

        Position[1] = static_cast<float>(static_cast<double>(ColumnY) - static_cast<double>(Step2));
        PageButtons[2] = static_cast<NnButtonControl*>(
            CreateButtonControl("VideoButton", kButtonMesh, nullptr, "ui_select_item", nullptr,
                                Position, nullptr, ButtonExtents, 0, 0, 0, 0, 0, 0, 0));
        PageButtons[2]->SetMontageMode(1);

        Position[1] = static_cast<float>(static_cast<double>(ColumnY) - static_cast<double>(Step3));
        PageButtons[3] = static_cast<NnButtonControl*>(
            CreateButtonControl("AudioButton", kButtonMesh, nullptr, "ui_select_item", nullptr,
                                Position, nullptr, ButtonExtents, 0, 0, 0, 0, 0, 0, 0));
        PageButtons[3]->SetMontageMode(1);

        if (bInGame == 0)
        {
            // 0x004a9e31..0x004a9e6b: no FUN_0055bfc0 for this one.
            Position[1] = static_cast<float>(static_cast<double>(ColumnY) -
                                             static_cast<double>(Step) * 4.0);
            PageButtons[4] = static_cast<NnButtonControl*>(
                CreateButtonControl("CreditsButton", kButtonMesh, nullptr, "ui_select_item",
                                    nullptr, Position, nullptr, ButtonExtents, 0, 0, 0, 0, 0, 0, 0));
        }

        Position[1] = static_cast<float>(static_cast<double>(ColumnY) - static_cast<double>(Step6));
        PageButtons[5] = static_cast<NnButtonControl*>(
            CreateButtonControl("MainMenuButton", kButtonMesh, nullptr, "ui_select_item", nullptr,
                                Position, nullptr, ButtonExtents, 0, 0, 0, 0, 0, 0, 0));
        PageButtons[5]->SetMontageMode(1);
    }

    // 0x004a9edc..0x004aa176: the captions, FUN_005a0e30(name, "ButtonText", {X - 0.13, Y', 0},
    // 0, 0.2580, 0.03, 1.25, ids, 1, 0, 1) and SetProperty(0x24, 1, 0) each. Y' is the double
    // Y + 0.015 minus the same step multiples.
    {
        double const TextBase = static_cast<double>(ColumnY) + 0.015;
        float Position[3] = {static_cast<float>(static_cast<double>(ColumnX) - 0.13),
                             static_cast<float>(TextBase), 0.0f};
        float const TextWidth = NnFloatBits(0x3e841893);
        float const TextHeight = NnFloatBits(0x3cf5c28f);
        float const TextScale = NnFloatBits(0x3fa00000);

        PageTexts[1] = static_cast<NnTextControl*>(
            CreateTextControl("InterfaceText", "ButtonText", Position, nullptr, TextWidth,
                              TextHeight, TextScale, 0x52e, 1, 0, 1));
        PageTexts[1]->SetProperty(0x24, 1, 0);

        Position[1] = static_cast<float>(TextBase - static_cast<double>(Step));
        PageTexts[0] = static_cast<NnTextControl*>(
            CreateTextControl("keymapText", "ButtonText", Position, nullptr, TextWidth, TextHeight,
                              TextScale, 0x52f, 1, 0, 1));
        PageTexts[0]->SetProperty(0x24, 1, 0);

        Position[1] = static_cast<float>(TextBase - static_cast<double>(Step2));
        PageTexts[2] = static_cast<NnTextControl*>(
            CreateTextControl("VideoText", "ButtonText", Position, nullptr, TextWidth, TextHeight,
                              TextScale, 0x5e2, 1, 0, 1));
        PageTexts[2]->SetProperty(0x24, 1, 0);

        Position[1] = static_cast<float>(TextBase - static_cast<double>(Step3));
        PageTexts[3] = static_cast<NnTextControl*>(
            CreateTextControl("AudioText", "ButtonText", Position, nullptr, TextWidth, TextHeight,
                              TextScale, 0x5e3, 1, 0, 1));
        PageTexts[3]->SetProperty(0x24, 1, 0);

        unsigned MainMenuIds = 0x58b;
        if (bInGame == 0)
        {
            Position[1] = static_cast<float>(TextBase - static_cast<double>(Step) * 4.0);
            PageTexts[4] = static_cast<NnTextControl*>(
                CreateTextControl("CreditsText", "ButtonText", Position, nullptr, TextWidth,
                                  TextHeight, TextScale, 0x532, 1, 0, 1));
            PageTexts[4]->SetProperty(0x24, 1, 0);
            MainMenuIds = 0x533;
        }

        Position[1] = static_cast<float>(TextBase - static_cast<double>(Step6));
        PageTexts[5] = static_cast<NnTextControl*>(
            CreateTextControl("mainMenuText", "ButtonText", Position, nullptr, TextWidth,
                              TextHeight, TextScale, MainMenuIds, 1, 0, 1));
        PageTexts[5]->SetProperty(0x24, 1, 0);
        PageTexts[5]->TextColour = G_NnPaletteArgb_679bb0;
    }

    // 0x004aa17c..0x004aa1bc: the option rows' origin, optlist.ini, and the scroll origin
    // (RowY + 0.007f, FADD dword, FSTP float).
    float RowY = NnFloatBits(0x3e47ae14);
    if (bInGame == 0)
        RowY = NnFloatBits(0x3e51eb85);
    LoadOptList(kOptListPath);
    float const ScrollRowY = static_cast<float>(static_cast<double>(RowY) +
                                                static_cast<double>(NnFloatBits(0x3be56042)));

    // 0x004aa1c0..0x004aa6c4: fourteen option rows. F = float(i * 0.035f), TY = float(RowY - F).
    char Name[0x100];
    for (int Index = 0; Index < 14; ++Index)
    {
        float const Offset = static_cast<float>(static_cast<double>(Index) *
                                                static_cast<double>(NnFloatBits(0x3d0f5c29)));
        float const RowTextY = static_cast<float>(static_cast<double>(RowY) -
                                                  static_cast<double>(Offset));

        // FUN_005a0e30(name, "PageHeader", {-0.03, TY, 0}, 0, 0.215, 0.03, 1.43, 0, 0, 0, 0).
        sprintf(Name, "opt_titleText %d", Index);
        float const TitleRow[3] = {NnFloatBits(0xbcf5c28f), RowTextY, 0.0f};
        OptTitleText[Index] = static_cast<NnTextControl*>(
            CreateTextControl(Name, "PageHeader", TitleRow, nullptr, NnFloatBits(0x3e5c28f6),
                              NnFloatBits(0x3cf5c28f), NnFloatBits(0x3fb70a3d), 0, 0, 0, 0));

        // "SubHeader" at {0, TY, 0}, scale 1.0; SetProperty(0x24, 1, 0), byte +0x380 = 0.
        sprintf(Name, "opt_text %d", Index);
        float const TextRow[3] = {0.0f, RowTextY, 0.0f};
        OptText[Index] = static_cast<NnTextControl*>(
            CreateTextControl(Name, "SubHeader", TextRow, nullptr, NnFloatBits(0x3e5c28f6),
                              NnFloatBits(0x3cf5c28f), 1.0f, 0, 0, 0, 0));
        OptText[Index]->SetProperty(0x24, 1, 0);
        OptText[Index]->Byte380 = 0;

        // FUN_005a0fb0(name, check.3db, repairblackbox.3db, "ui_shipview_select", 0,
        // {0.415, TY, 0}, 0, {0.015, 0.015, 0}, 0, 0, 0, 0, 0x65f, 0x67a); SetProperty(0x35, 1, 0);
        // +0x370 = DAT_00679b94.
        sprintf(Name, "opt_toggle %d", Index);
        float const TogglePosition[3] = {NnFloatBits(0x3ed47ae1), RowTextY, 0.0f};
        float const ToggleExtents[3] = {NnFloatBits(0x3c75c28f), NnFloatBits(0x3c75c28f), 0.0f};
        OptToggle[Index] = static_cast<NnButtonControl*>(CreateToggleButtonControl(
            Name, "Interface\\NeuroNet\\Inventory\\check.3db",
            "Interface\\NeuroNet\\Inventory\\repairblackbox.3db", "ui_shipview_select", nullptr,
            TogglePosition, nullptr, ToggleExtents, 0, 0, 0, 0, 0x65f, 0x67a));
        OptToggle[Index]->SetProperty(0x35, 1, 0);
        memcpy(OptToggle[Index]->Colour, &G_NnPaletteArgb_679b94, 4);

        // "SubHeader" at {0.28, TY - 0.003f, 0}, 0.22 x 0.03, scale 0.9, 0, 2, 0, 0.
        sprintf(Name, "opt_toggleText %d", Index);
        float const ToggleTextY = static_cast<float>(static_cast<double>(RowTextY) -
                                                     static_cast<double>(NnFloatBits(0x3b449ba6)));
        float const ToggleTextRow[3] = {NnFloatBits(0x3e8f5c29), ToggleTextY, 0.0f};
        OptToggleText[Index] = static_cast<NnTextControl*>(
            CreateTextControl(Name, "SubHeader", ToggleTextRow, nullptr, NnFloatBits(0x3e6147ae),
                              NnFloatBits(0x3cf5c28f), NnFloatBits(0x3f666666), 0, 2, 0, 0));

        // FUN_005a17e0(name, {0.19, float(ScrollRowY - F), -1}, 0.24, 0.014, 0, 100, 1, 1);
        // SetProperty(0x37, 0, 100); +0x3b4 = 1; +0x3cc = 1.0f.
        sprintf(Name, "opt_scroll %d", Index);
        float const ScrollPosition[3] = {
            NnFloatBits(0x3e428f5c),
            static_cast<float>(static_cast<double>(ScrollRowY) - static_cast<double>(Offset)),
            -1.0f};
        OptScroll[Index] = CreateUIScroll(Name, ScrollPosition, NnFloatBits(0x3e75c28f),
                                          NnFloatBits(0x3c656042), 0, 100, 1, 1);
        OptScroll[Index]->SetProperty(0x37, 0, 100);
        OptScroll[Index]->SetStepMode(true);
        OptScroll[Index]->SetPageField(1.0f);

        // "HudSmall" at {0.133, TY, 0}, 0.11 x 0.02, scale 0.8, 0, 2, 0, 0.
        sprintf(Name, "opt_smallText %d", Index);
        float const SmallRow[3] = {NnFloatBits(0x3e083127), RowTextY, 0.0f};
        SmallText[Index] = static_cast<NnTextControl*>(
            CreateTextControl(Name, "HudSmall", SmallRow, nullptr, NnFloatBits(0x3de147ae),
                              NnFloatBits(0x3ca3d70a), NnFloatBits(0x3f4ccccd), 0, 2, 0, 0));

        // "ToolTip" at {0.26, TY + 0.017f, 0} (left, 0, 0, 0, 0) and {0.359, same, 0} (right,
        // 0, 2, 0, 0).
        sprintf(Name, "opt_scrollTextLeft %d", Index);
        float const ScrollTextY = static_cast<float>(static_cast<double>(RowTextY) +
                                                     static_cast<double>(NnFloatBits(0x3c8b4396)));
        float const LeftRow[3] = {NnFloatBits(0x3e851eb8), ScrollTextY, 0.0f};
        ScrollTextLeft[Index] = static_cast<NnTextControl*>(
            CreateTextControl(Name, "ToolTip", LeftRow, nullptr, NnFloatBits(0x3de147ae),
                              NnFloatBits(0x3ca3d70a), NnFloatBits(0x3f4ccccd), 0, 0, 0, 0));

        sprintf(Name, "opt_scrollTextRight %d", Index);
        float const RightRow[3] = {NnFloatBits(0x3eb7ced9), ScrollTextY, 0.0f};
        ScrollTextRight[Index] = static_cast<NnTextControl*>(
            CreateTextControl(Name, "ToolTip", RightRow, nullptr, NnFloatBits(0x3de147ae),
                              NnFloatBits(0x3ca3d70a), NnFloatBits(0x3f4ccccd), 0, 2, 0, 0));

        // FUN_005a0d00(name, "ui_select_item", {0.32, TY, 0}, {0.09, 0.011, 0}, 0, 0, 0, 0, 0);
        // +0x378 = DAT_00679b94, +0x35d |= 1.
        sprintf(Name, "opt_smallBtn %d", Index);
        float const SmallButtonRow[3] = {NnFloatBits(0x3ea3d70a), RowTextY, 0.0f};
        float const SmallButtonExtents[3] = {NnFloatBits(0x3db851ec), NnFloatBits(0x3c343958), 0.0f};
        SmallButton[Index] = static_cast<WireButtonControl*>(CreateWireButtonControl(
            Name, "ui_select_item", SmallButtonRow, SmallButtonExtents, 0, 0, 0, 0, 0));
        memcpy(SmallButton[Index]->Colour, &G_NnPaletteArgb_679b94, 4);
        SmallButton[Index]->WireState2 = static_cast<unsigned char>(SmallButton[Index]->WireState2 | 1);

        // "ItemName" at {0.32, TY - 0.003f, 0}, 0.18 x 0.03, scale 0.9, 0, 1, 0, 0.
        sprintf(Name, "opt_smallBtnText %d", Index);
        float const SmallButtonTextRow[3] = {NnFloatBits(0x3ea3d70a), ToggleTextY, 0.0f};
        SmallButtonText[Index] = static_cast<NnTextControl*>(
            CreateTextControl(Name, "ItemName", SmallButtonTextRow, nullptr,
                              NnFloatBits(0x3e3851ec), NnFloatBits(0x3cf5c28f),
                              NnFloatBits(0x3f666666), 0, 1, 0, 0));
    }

    // 0x004aa6ca..0x004aa713: FUN_005a0d60("opt_static_bar", {0.31, 0.31, -1},
    // {0.53, 0.0295, 0}, 0, 0, 0, 1) -> +0x7a4.
    {
        float const BarPosition[3] = {NnFloatBits(0x3e9eb852), NnFloatBits(0x3e9eb852), -1.0f};
        float const BarExtents[3] = {NnFloatBits(0x3f07ae14), NnFloatBits(0x3cf1a9fc), 0.0f};
        StaticBar = static_cast<WireStaticControl*>(
            CreateWireStaticControl("opt_static_bar", BarPosition, BarExtents, 0, 0, 0, 1));
    }

    // 0x004aa718..0x004aa88c: cancel / set defaults boxes and captions.
    {
        float const WireExtents[3] = {NnFloatBits(0x3d8f5c29), NnFloatBits(0x3c75c28f), 0.0f};
        float const CancelPosition[3] = {NnFloatBits(0x3e9a1cac), NnFloatBits(0xbe970a3d), 0.0f};
        CancelButton = static_cast<WireButtonControl*>(CreateWireButtonControl(
            "opt_cancel_btn", "ui_select_item", CancelPosition, WireExtents, 0, 0, 0, 0, 0));
        float const DefaultsPosition[3] = {NnFloatBits(0xbc75c28f), NnFloatBits(0xbe970a3d), 0.0f};
        DefaultsButton = static_cast<WireButtonControl*>(CreateWireButtonControl(
            "opt_set_defaults_btn", "ui_select_item", DefaultsPosition, WireExtents, 0, 0, 0, 0, 0));
        memcpy(CancelButton->Colour, &G_NnPaletteArgb_679b94, 4);
        memcpy(DefaultsButton->Colour, &G_NnPaletteArgb_679b94, 4);

        float const CancelTextPosition[3] = {NnFloatBits(0x3e9a1cac), NnFloatBits(0xbe981062), 0.0f};
        CancelText = static_cast<NnTextControl*>(
            CreateTextControl("opt_cancel_txt", "ItemName", CancelTextPosition, nullptr,
                              NnFloatBits(0x3e8a3d71), NnFloatBits(0x3cf5c28f), 1.0f, 0x2148, 1, 0,
                              0));
        float const DefaultsTextPosition[3] = {NnFloatBits(0xbc75c28f), NnFloatBits(0xbe981062),
                                               0.0f};
        DefaultsText = static_cast<NnTextControl*>(
            CreateTextControl("opt_set_defaults_txt", "ItemName", DefaultsTextPosition, nullptr,
                              NnFloatBits(0x3e8a3d71), NnFloatBits(0x3cf5c28f), 1.0f, 0x5e4, 1, 0,
                              0));
    }

    // 0x004aa891..0x004aa9f2: the video mode cycle, its two frames and caption.
    {
        float const CyclePosition[3] = {NnFloatBits(0x3ed47ae1), NnFloatBits(0x3e604189), -1.0f};
        VideoModeCycle = CreateUICycle("video_mode_cycle", CyclePosition, NnFloatBits(0x3c449ba6),
                                       NnFloatBits(0x3ccccccd), 0, 0, 0, 0);
        float const BoxPosition[3] = {NnFloatBits(0x3f1a9fbe), NnFloatBits(0x3ed2f1aa), -1.0f};
        float const BoxExtents[3] = {NnFloatBits(0x3e6147ae), NnFloatBits(0x3ccccccd), 0.0f};
        VideoBox = static_cast<WireStaticControl*>(
            CreateWireStaticControl("video_mode_box", BoxPosition, BoxExtents, 0, 0, 0, 0xf));
        float const Box2Position[3] = {NnFloatBits(0x3f578d50), NnFloatBits(0x3ed2f1aa), -1.0f};
        float const Box2Extents[3] = {NnFloatBits(0x3c54fdf4), NnFloatBits(0x3ccccccd), 0.0f};
        VideoBox2 = static_cast<WireStaticControl*>(
            CreateWireStaticControl("video_mode_box2", Box2Position, Box2Extents, 0, 0, 0, 0xf));
        float const TextPosition[3] = {NnFloatBits(0x3e9a1cac), NnFloatBits(0x3e4ed917), 0.0f};
        VideoModeText = static_cast<NnTextControl*>(
            CreateTextControl("video_mode_text", "ItemName", TextPosition, nullptr,
                              NnFloatBits(0x3e604189), NnFloatBits(0x3cf5c28f),
                              NnFloatBits(0x3f666666), 0, 1, 0, 0));
        VideoModeCycle->SetStepMode(true);
        VideoModeCycle->SetPageField(1.0f);
        VideoModeText->SetProperty(0x24, 1, 0);
    }

    // 0x004aa9f8..0x004aaa2e: clean, cancel greyed and enabled.
    bDirty = 0;
    CancelText->TextColour = G_NnPaletteArgb_679b94;
    CancelButton->bUseColour = 1;
    CancelButton->ButtonState = static_cast<unsigned char>(CancelButton->ButtonState & 0xfb);

    // 0x004aaa34..0x004aaa47: FUN_004aed70, FUN_004aeca0, FUN_004af230(keylist.ini).
    LeaveAudioPage();
    LeaveVideoPage();
    LoadKeyList(kKeyListPath);

    // 0x004aaa4c..0x004aaaa8: FUN_005a17e0("keymap_scroll", {0.415, 0.135, -1}, 0.015, 0.39, 0,
    // KeyGroups[0].size(), 13, 0) -> +0x4b8.
    {
        float const ScrollPosition[3] = {NnFloatBits(0x3ed47ae1), NnFloatBits(0x3e0a3d71), -1.0f};
        KeyScroll = CreateUIScroll("keymap_scroll", ScrollPosition, NnFloatBits(0x3c75c28f),
                                   NnFloatBits(0x3ec7ae14), 0,
                                   static_cast<int>(KeyGroups[0].size()), 0xd, 0);
    }

    // 0x004aaaad..0x004aab53: the three group tabs, x = float(j * 0.175 - 0.03) in doubles;
    // +0x35c &= 0xbf.
    for (int Group = 0; Group < 3; ++Group)
    {
        sprintf(Name, "IGroup_Btn_Row%d", Group);
        float const TabPosition[3] = {
            static_cast<float>(static_cast<double>(Group) * 0.175 - 0.03), NnFloatBits(0x3e428f5c),
            0.0f};
        float const TabExtents[3] = {NnFloatBits(0x3d8f5c29), NnFloatBits(0x3c75c28f), 0.0f};
        GroupButtons[Group] = static_cast<WireButtonControl*>(CreateWireButtonControl(
            Name, "ui_select_item", TabPosition, TabExtents, 0, 0, 0, 0, 0xf));
        GroupButtons[Group]->WireState =
            static_cast<unsigned char>(GroupButtons[Group]->WireState & 0xbf);
    }

    // 0x004aab59..0x004aac64: the tab captions, "ItemName" 0.125 x 0.0295, scale 0.9, ids
    // 0x76e..0x770, 1, 0, 0; then SetProperty(0x24, 1, 0) on all three.
    {
        float const TabTextWidth = NnFloatBits(0x3e000000);
        float const TabTextHeight = NnFloatBits(0x3cf1a9fc);
        float const TabTextScale = NnFloatBits(0x3f666666);
        float Position[3] = {NnFloatBits(0xbce56042), NnFloatBits(0x3e400000), 0.0f};
        GroupTexts[0] = static_cast<NnTextControl*>(
            CreateTextControl("IGroup_Txt_Row1", "ItemName", Position, nullptr, TabTextWidth,
                              TabTextHeight, TabTextScale, 0x76e, 1, 0, 0));
        Position[0] = NnFloatBits(0x3e178d50);
        GroupTexts[1] = static_cast<NnTextControl*>(
            CreateTextControl("IGroup_Txt_Row2", "ItemName", Position, nullptr, TabTextWidth,
                              TabTextHeight, TabTextScale, 0x76f, 1, 0, 0));
        Position[0] = NnFloatBits(0x3ea5e354);
        GroupTexts[2] = static_cast<NnTextControl*>(
            CreateTextControl("IGroup_Txt_Row3", "ItemName", Position, nullptr, TabTextWidth,
                              TabTextHeight, TabTextScale, 0x770, 1, 0, 0));
        for (int Group = 0; Group < 3; ++Group)
            GroupTexts[Group]->SetProperty(0x24, 1, 0);
    }

    // 0x004aac66..0x004aaf49: thirteen key rows. BY = float(0.1745 - (k - 1) * 0.059); the
    // captions sit at (k + 0.5) * double(0.0295f) below double(0.128f) / double(0.133f).
    for (int Row = 0; Row < 13; ++Row)
    {
        float const ButtonY = static_cast<float>(0.1745 - static_cast<double>(Row - 1) * 0.059);
        float const RowExtents[3] = {NnFloatBits(0x3e19999a), NnFloatBits(0x3cf1a9fc), 0.0f};

        // FUN_005a0d00(name, "ui_select_item", {0.34, BY, -1}, {0.15, 0.0295, 0}, 0, 0, 0, 0,
        // 0xa); +0x35c &= 0xbf.
        sprintf(Name, "IBtn_Row%d", Row);
        float const ButtonPosition[3] = {NnFloatBits(0x3eae147b), ButtonY, -1.0f};
        KeyBtnRows[Row] = static_cast<WireButtonControl*>(CreateWireButtonControl(
            Name, "ui_select_item", ButtonPosition, RowExtents, 0, 0, 0, 0, 0xa));
        KeyBtnRows[Row]->WireState = static_cast<unsigned char>(KeyBtnRows[Row]->WireState & 0xbf);

        // {0.64, BY, -1}, flags 0; +0x384 = 0.
        sprintf(Name, "IBtn_alt_Row%d", Row);
        float const AltPosition[3] = {NnFloatBits(0x3f23d70a), ButtonY, -1.0f};
        KeyAltBtnRows[Row] = static_cast<WireButtonControl*>(CreateWireButtonControl(
            Name, "ui_select_item", AltPosition, RowExtents, 0, 0, 0, 0, 0));
        KeyAltBtnRows[Row]->EdgeMask = 0;

        double const CaptionOffset = (static_cast<double>(Row) + 0.5) *
                                     static_cast<double>(NnFloatBits(0x3cf1a9fc));

        // "ItemName" {-0.014, float(double(0.128f) - offset), 0}, 0.196 x 0.03, scale 1.1,
        // 0, 0, 0, 0; SetProperty(0x24, 1, 0); byte +0x380 = 0.
        sprintf(Name, "ICommand_Row%d", Row);
        float const CommandPosition[3] = {
            NnFloatBits(0xbc656042),
            static_cast<float>(static_cast<double>(NnFloatBits(0x3e03126f)) - CaptionOffset),
            0.0f};
        CommandRows[Row] = static_cast<NnTextControl*>(
            CreateTextControl(Name, "ItemName", CommandPosition, nullptr, NnFloatBits(0x3e48b439),
                              NnFloatBits(0x3cf5c28f), NnFloatBits(0x3f8ccccd), 0, 0, 0, 0));
        CommandRows[Row]->SetProperty(0x24, 1, 0);
        CommandRows[Row]->Byte380 = 0;

        // "ItemName" {0.17 / 0.316, float(double(0.133f) - offset), 0}, 0.145 x 0.017, scale
        // 1.1, 0, 1, 0, 0.
        float const KeyY = static_cast<float>(static_cast<double>(NnFloatBits(0x3e083127)) -
                                              CaptionOffset);
        sprintf(Name, "IKey_Row%d", Row);
        float const KeyPosition[3] = {NnFloatBits(0x3e2e147b), KeyY, 0.0f};
        KeyRows[Row] = static_cast<NnTextControl*>(
            CreateTextControl(Name, "ItemName", KeyPosition, nullptr, NnFloatBits(0x3e147ae1),
                              NnFloatBits(0x3c8b4396), NnFloatBits(0x3f8ccccd), 0, 1, 0, 0));
        sprintf(Name, "IKey_alt_Row%d", Row);
        float const KeyAltPosition[3] = {NnFloatBits(0x3ea1cac1), KeyY, 0.0f};
        KeyAltRows[Row] = static_cast<NnTextControl*>(
            CreateTextControl(Name, "ItemName", KeyAltPosition, nullptr, NnFloatBits(0x3e147ae1),
                              NnFloatBits(0x3c8b4396), NnFloatBits(0x3f8ccccd), 0, 1, 0, 0));
        KeyRows[Row]->SetProperty(0x24, 1, 0);
        KeyAltRows[Row]->SetProperty(0x24, 1, 0);
    }

    // 0x004aaf4f..0x004aafe9: the two column headers, ids 0x763 / 0x764.
    {
        float Position[3] = {NnFloatBits(0x3e2e147b), NnFloatBits(0x3e1cac08), 0.0f};
        KeyColumn1 = static_cast<NnTextControl*>(
            CreateTextControl("keymap_col1", "ItemName", Position, nullptr, NnFloatBits(0x3e147ae1),
                              NnFloatBits(0x3c8b4396), NnFloatBits(0x3f8ccccd), 0x763, 1, 0, 0));
        Position[0] = NnFloatBits(0x3ea1cac1);
        KeyColumn2 = static_cast<NnTextControl*>(
            CreateTextControl("keymap_col2", "ItemName", Position, nullptr, NnFloatBits(0x3e147ae1),
                              NnFloatBits(0x3c8b4396), NnFloatBits(0x3f8ccccd), 0x764, 1, 0, 0));
    }

    // 0x004aafef..0x004ab077: fourteen row separators, FUN_005a0d60(name, {0.287, BY, -1},
    // {0.53, 0.0295, 0}, 0, 0, 0, 1).
    for (int Row = 0; Row < 14; ++Row)
    {
        sprintf(Name, "Istatics_Row%d", Row);
        float const StaticPosition[3] = {
            NnFloatBits(0x3e92f1aa),
            static_cast<float>(0.1745 - static_cast<double>(Row - 1) * 0.059), -1.0f};
        float const StaticExtents[3] = {NnFloatBits(0x3f07ae14), NnFloatBits(0x3cf1a9fc), 0.0f};
        StaticRows[Row] = static_cast<WireStaticControl*>(
            CreateWireStaticControl(Name, StaticPosition, StaticExtents, 0, 0, 0, 1));
    }

    // 0x004ab07d..0x004ab09e: the last separator's +0x35d |= 1, FUN_004af900(0), FUN_004aeab0.
    StaticRows[13]->WireState2 = static_cast<unsigned char>(StaticRows[13]->WireState2 | 1);
    FillKeyRows(0);
    LeaveKeyMapPage();

    // 0x004ab0a3..0x004ab1f9: the credits. FUN_005a10e0("CreditsMessageText", 0.5, 0.44,
    // {-0.113, 0.208, -1}, 0) -> +0x7f0 (no null test), FUN_0057b370, +0x8b0 = 20.0, twenty
    // ParagraphNodes through FUN_0057b470, then ids 0x6003f..0x60048 loaded into one local
    // list (FUN_0057da40, the last paragraph is kept) and appended with FUN_0057b3e0.
    RenderDisplayList Credits;
    {
        float const CreditsOffset[3] = {NnFloatBits(0xbde76c8b), NnFloatBits(0x3e54fdf4), -1.0f};
        CreditsText = CreateRichTextWin("CreditsMessageText", 0.5f, NnFloatBits(0x3ee147ae),
                                        CreditsOffset, false);
        CreditsText->ClearContent();
        CreditsSpeed = 20.0;
        for (int Index = 0; Index < 0x14; ++Index)
        {
            ParagraphNode Para;
            CreditsText->AppendNode(Para);
        }
        for (unsigned Ids = 0x6003f; Ids <= 0x60048; ++Ids)
            LoadIdsIntoRdl(Ids, Credits);
        CreditsText->AppendContent(Credits);
    }

    // 0x004ab1fe..0x004ab273: from the main menu -- credits hidden, the credits caption idle,
    // the defaults caption re-read (ids 0x5e4) and the defaults box, its caption and the title
    // hidden.
    if (bInGame == 0)
    {
        HideControl(CreditsText);
        PageTexts[4]->TextColour = G_NnPaletteArgb_679bb0;
        SetTextIds(DefaultsText, 0x5e4);
        HideControl(DefaultsButton);
        HideControl(DefaultsText);
        HideControl(Title);
    }

    // 0x004ab276..0x004ab311: FUN_005a0db0("SetKeyFrame", alt-f4window.cmp, {0, 0, -1}, 0,
    // {0, 0, 0}, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, {0, 0, 0}) -> +0x4e4.
    {
        float const FramePosition[3] = {0.0f, 0.0f, -1.0f};
        SetKeyFrame = CreateStaticControl("SetKeyFrame",
                                          "Interface\\NeuroNet\\Options\\alt-f4window.cmp",
                                          FramePosition, nullptr, Zero, 0, 0, 0, 1, 1, 0, 1, 0, 0,
                                          0, Zero);
    }

    // 0x004ab2f8..0x004ab3dc: the dialog's three lines, "ConfirmText" 0.27 x 0.03, scale 1.5,
    // 1, 0, 0 (only the first has an ids, 0x5e6).
    {
        float const DialogWidth = NnFloatBits(0x3e8a3d71);
        float const DialogHeight = NnFloatBits(0x3cf5c28f);
        float const DialogScale = NnFloatBits(0x3fc00000);
        float Position[3] = {NnFloatBits(0xbc343958), NnFloatBits(0x3da3d70a), 0.0f};
        DialogText[0] = static_cast<NnTextControl*>(
            CreateTextControl("SetKeyFrameText1", "ConfirmText", Position, nullptr, DialogWidth,
                              DialogHeight, DialogScale, 0x5e6, 1, 0, 0));
        Position[1] = NnFloatBits(0x3d23d70a);
        DialogText[1] = static_cast<NnTextControl*>(
            CreateTextControl("SetKeyFrameText2", "ConfirmText", Position, nullptr, DialogWidth,
                              DialogHeight, DialogScale, 0, 1, 0, 0));
        Position[1] = NnFloatBits(0xbbc49ba6);
        DialogText[2] = static_cast<NnTextControl*>(
            CreateTextControl("SetKeyFrameText3", "ConfirmText", Position, nullptr, DialogWidth,
                              DialogHeight, DialogScale, 0, 1, 0, 0));
    }

    // 0x004ab3d7..0x004ab52c: overwrite / cancel boxes (+0x35d |= 1 on cancel) and captions
    // (ids 0x5e8 / 0x2148).
    {
        float const DialogExtents[3] = {NnFloatBits(0x3dcccccd), NnFloatBits(0x3cac0831), 0.0f};
        float const LeftPosition[3] = {NnFloatBits(0xbde56042), NnFloatBits(0xbd8f5c29), 0.0f};
        float const RightPosition[3] = {NnFloatBits(0x3de56042), NnFloatBits(0xbd8f5c29), 0.0f};
        OverwriteButton = static_cast<WireButtonControl*>(CreateWireButtonControl(
            "SetKeyOverwriteBtn", "ui_select_item", LeftPosition, DialogExtents, 0, 0, 0, 0, 0));
        DialogCancelButton = static_cast<WireButtonControl*>(CreateWireButtonControl(
            "SetKeyCancelBtn", "ui_select_item", RightPosition, DialogExtents, 0, 0, 0, 0, 0));
        DialogCancelButton->WireState2 =
            static_cast<unsigned char>(DialogCancelButton->WireState2 | 1);
        DialogButtonText[0] = static_cast<NnTextControl*>(
            CreateTextControl("keymap_keyBtnText1", "ButtonText", LeftPosition, nullptr,
                              NnFloatBits(0x3e8a3d71), NnFloatBits(0x3cf5c28f),
                              NnFloatBits(0x3fc00000), 0x5e8, 1, 0, 0));
        DialogButtonText[1] = static_cast<NnTextControl*>(
            CreateTextControl("keymap_keyBtnText2", "ButtonText", RightPosition, nullptr,
                              NnFloatBits(0x3e8a3d71), NnFloatBits(0x3cf5c28f),
                              NnFloatBits(0x3fc00000), 0x2148, 1, 0, 0));
    }

    // 0x004ab532..0x004ab577: the dialog starts hidden.
    HideControl(DialogText[0]);
    HideControl(DialogText[1]);
    HideControl(DialogText[2]);
    HideControl(DialogButtonText[0]);
    HideControl(DialogButtonText[1]);
    HideControl(OverwriteButton);
    HideControl(DialogCancelButton);
    HideControl(SetKeyFrame);

    // 0x004ab57a..0x004ab59e: FUN_004abdf0(0), FUN_004abdf0(1), FUN_004aebb0, and in game
    // FUN_004ae650.
    SetRowsHidden(false);
    SetRowsHidden(true);
    LeaveInterfacePage();
    if (bInGame != 0)
        EnterInterfacePage();

    // 0x004ab5a3..0x004ab60f: slot 31 SetOffset(0, 0, -1), slot 5 SetPosition(+0x60), layer
    // 100 / 6, then FUN_004b5060. On failure +0x32c = 1. The local list is freed on both paths.
    SetOffset(0.0f, 0.0f, -1.0f);
    SetPosition(Position);
    DrawLayer = 0x64;
    DrawSubLayer = 6;
    bCreating = 0;
    bool const bOk = Win3D::Create(NewParent, Data);
    if (!bOk)
        bClosed = 1;
    return bOk;
}
