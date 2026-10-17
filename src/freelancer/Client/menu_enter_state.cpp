// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "menu_enter_state.h"

#include "character_select_equip.h"
#include "client_frame_tick.h" // FUN_00424f30 / FUN_00425080 / FUN_00425140, FUN_0041b2e0
#include "chat_init.h"    // ChatShowMsgWin (FUN_0046a4a0)
#include "chat_msg_win.h" // ChatCreateMsgWin (FUN_0046b580)
#include "connect_flow.h"
#include "connect_lists.h" // PreConnectReset (FUN_004c4a80)
#include "dacom.h" // FDUMP
#include "dataconfig_progress.h" // GetIntroConfigState (bDrawScripts, DAT_00614a9c)
#include "fade_to_black.h" // SetFadeImmediate / SetFadeTarget (FUN_00412070 / FUN_00412050)
#include "font_imp.h"
#include "gf_script_object.h" // G_GfScriptManager (DAT_0066d2d0)
#include "globals.h"
#include "menu_universe_base.h" // LeaveClientLocationBase (FUN_0043b3e0)
#include "ids_strings.h"
#include "input_manager.h"
#include "loading_screen.h"
#include "login_mgr.h"
#include "menu_sound_arch.h" // TickClientSoundMixer (FUN_00428040)
#include "nn_preferences.h"  // NnPreferences::bClosed (+0x32c)
#include "nn_static_control.h"
#include "nn_text_control.h"
#include "page_account_select.h" // PageAccountSelect (DAT_0067aa10)
#include "page_character_create.h" // PageCharacterCreate (DAT_0067a890)
#include "page_character_select.h" // PageCharacterSelect (DAT_0067b180), G_NoCharacterId
#include "page_character_select_single.h" // PageCharacterSelectSingle (DAT_0067ae10)
#include "page_server_select.h"  // PageServerSelect (DAT_0067ab20)
#include "page_options.h"
#include "player_spawn.h" // ResetMissionOfferAnswerRetail (FUN_004c55b0)
#include "room_camera.h"  // FRoomCamera (FUN_0041a140's receiver)
#include "room_control.h" // GetRoomObjectList (FUN_005593c0), FRoomObject
#include "server_connect.h"
#include "sound_config.h" // SetClientAudioMode (FUN_00428030)
#include "space_letterbox.h" // UpdateRoomLetterbox (FUN_0041a140)
#include "text_win.h"
#include "type_select.h"
#include "ui_page.h"
#include "ui_page_desc.h"
#include "win3d.h"           // Win3DDrawAll (FUN_0059e300)
#include "win3d_engine_submit.h"
#include "win3d_immediate.h" // Win3DSetViewport / Win3DSetPipelineClearColor
#include "win3d_render.h"    // ClearWin3DBuffers (FUN_004215e0)
#include "win_factory.h"
#include "wincontrol_base.h"

#include "common_net.h" // CPlayerAccount (cases 2 and 4)
#include "common_misc.h" // SinglePlayer, SetMultiPlayer, GetFullFilename

#include <math.h>
#include <process.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#include <list>

namespace
{
    // .rdata:005e2724 -- the FUN_00422610 source name of the driver's frame (checked in PE bytes).
    char const kIntroCpp[] = "E:\\FL\\Scratch\\Source\\Client\\Interface\\Intro.cpp";

    // EnterState case 1 column: x = -0.33 (PUSH 0xbea8f5c3), y = DAT_005e3210 - i *
    // DAT_005e3214, z = -1 (PUSH 0xbf800000) -- 0x00574651..0x00574670.
    float const kMainColumnPosX = -0.330000013f; // 0xbea8f5c3
    float const kMainColumnPosZ = -1.0f;         // 0xbf800000
    float const kMainColumnPosY0 = 0.155000001f; // DAT_005e3210
    float const kMainColumnPosYStep = 0.081500001f; // DAT_005e3214
    float const kMainColumnBoxWidth = 0.25f;     // PUSH 0x3e800000 before each FUN_005753d0
    float const kFontLineScale = 0.002f;         // DAT_005e2554

    // The "Version" TextControl position, 0x00574857..0x00574861: PUSH 0xbf800000 / 0xbeb33333 /
    // 0x3ea3d70a into FUN_00422b80.
    float const kVersionPosX = 0.319999993f;  // 0x3ea3d70a
    float const kVersionPosY = -0.349999994f; // 0xbeb33333
    float const kVersionPosZ = -1.0f;         // 0xbf800000

    char const kFrontButtonMesh[] = "Interface\\NeuroNet\\Inventory\\FRONT_button.cmp";
    char const kShortOptionsBox[] = "Interface\\NeuroNet\\Inventory\\FRONT_shortoptions_box.cmp";
    char const kInfoWindowMesh[] = "Interface\\LoadSave\\FRONT_infowindow.cmp";

    char const kPageServerSelect[] = "Interface\\intro\\PAGE_ServerSelect.page";
    char const kPageAccountSelect[] = "Interface\\intro\\PAGE_AccountSelect.page";
    char const kPageCharacterSelect[] = "Interface\\intro\\PAGE_CharacterSelect.page";
    char const kPageCharacterCreate[] = "Interface\\intro\\PAGE_CharacterCreate.page";
    char const kPageCharSelectSingle[] = "Interface\\intro\\PAGE_CharSelectSingle.page";

    // The five TextMsgButtons: caption IDS and slot-23 command (0x005746a2..0x005747d2).
    struct FMainColumnSlot
    {
        unsigned LabelIds;
        int Command;
    };

    FMainColumnSlot const kMainColumnSlots[5] = {
        {0x4f4u, 0x10},
        {0x4f5u, 0x03},
        {0x4f6u, 0x04},
        {0x4f8u, 0x0a},
        {0x4f9u, 0x0b},
    };

    // The page fields the Update poll reads are the ported pages' own: PageCharacterSelect's
    // Result / SelectedCharacter (+0x2b4 / +0x399, DAT_0067b434 / DAT_0067b519) and
    // PageCharacterSelectSingle's (+0xb4 / +0x118, DAT_0067aec4 / DAT_0067af28).

    // DAT_0067c200 request / DAT_0067c1fc latch (menu Init / case19).
    int GIntroModeRequest = 0; // DAT_0067c200
    int GIntroModeLatch = 0;   // DAT_0067c1fc

    // DAT_0067c204 -- set by case 5, cleared when state 5 is left.
    unsigned char GServerSelectActive = 0;
    // DAT_0067c20c -- a Win3D the driver deletes when state 10 is left while it is inactive.
    // Retail has no writer of a non-null value: a raw scan of the whole PE finds exactly three
    // references, all in FUN_00573c00 (0x00573f02 / 0x00573f18 reads, 0x00573f28 = 0), and no
    // instruction takes its address. It stays null, as here.
    Win3D* GOptionsChild_67c20c = nullptr;

    // DAT_0067bd40 is G_NoCharacterId (page_character_select.h); DAT_0067b20c is
    // PageCharacterSelect::CurrentCharacter.

    // The intro base teardown the driver inlines three times -- poll index 3 0x0057404e..
    // 0x0057409b, poll index 8 0x005740df..0x0057412c, EnterState case 12 0x00574a8b..0x00574ad8:
    // FUN_0043b3e0 on DAT_00668708, FUN_004564e0 on DAT_0066d2d0, FUN_004c4910(&0),
    // FUN_004c4a10(0), FUN_00412070(1.0), FUN_00412050(0, (float)(1.0 / DAT_0061650c)),
    // then DAT_00668762 = LocationController+0x5a = 0. LoadLocationBase reads that byte
    // through its `this` pointer at 0x004398a4.
    void LeaveIntroBase()
    {
        LeaveClientLocationBase();
        G_GfScriptManager.Shutdown();
        SetPlayerBaseIdRetail(0);
        SetPlayerFactionScratchRetail(0);
        SetFadeImmediate(1.0f);
        SetFadeTarget(0.0f, static_cast<float>(1.0 / static_cast<double>(GetFadeDuration())));
        SetClientLocationNoServerNotify(false);
    }

    // Every intro PAGE screen is its own static object now (DAT_0067ab20 / 67aa10 / 67b180 /
    // 67a890 / 67ae10 / 67a7d0); the per-attach IntroPageHost the port used to allocate is gone
    // with the last of them (PageCharacterCreate, case 14).
    void ReleaseActivePageInternal(FMenuNavState* Nav)
    {
        // Retail (e.g. 0x00574fbe..0x00574fcf): if (+0x228) { vtable+0x28(); +0x228 = 0 } --
        // `this` is never tested.
        if (Nav->ActivePage != nullptr)
        {
            Nav->ActivePage->Destroy();   // slot 10 (+0x28)
            Nav->ActivePage = nullptr;
        }
    }

    // Case 18 (0x00574fbe..0x0057529b; Ghidra leaves the range undisassembled, decoded from the
    // raw bytes). The five chrome statics are members of the page object PageCharacterSelectSingle
    // (DAT_0067ae10): +0xf4..+0xfc the three "bImg", +0x100 "bigBox", +0x104 "smallBox" -- the
    // absolute stores DAT_0067af04..DAT_0067af14 (0x00574ffe, 0x0057508e, 0x00575108, 0x0057518a,
    // 0x00575200; the page's own methods read them through `this`).
    struct FCase18Static
    {
        char const* Nickname;
        char const* Mesh;
        float Offset[3];
    };

    FCase18Static const kCase18Statics[5] = {
        {"bImg", kFrontButtonMesh, {-0.34799999f, 0.19499999f, -1.0f}},          // beb22d0e/3e47ae14/bf800000
        {"bImg", kFrontButtonMesh, {-0.34900001f, 0.11050000f, -1.0f}},          // beb2b021/3de24dd3/bf800000
        {"bImg", kFrontButtonMesh, {-0.34900001f, 0.029999999f, -1.0f}},         // beb2b021/3cf5c28f/bf800000
        {"bigBox", kShortOptionsBox, {0.019999999f, -0.019999999f, -0.98000002f}}, // 3ca3d70a/bca3d70a/bf7ae148
        {"smallBox", kInfoWindowMesh, {-0.34999999f, -0.19699999f, -1.0f}},      // beb33333/be49ba5e/bf800000
    };

    // The page member each static is stored into (page +0xf4 / +0xf8 / +0xfc / +0x100 / +0x104).
    Win3D*& Case18StaticSlot(PageCharacterSelectSingle& Page, int Index)
    {
        if (Index < 3)
            return Page.ButtonImages[Index];
        if (Index == 3)
            return Page.BigBox;
        return Page.SmallBox;
    }

    // 0x00574fe0..0x0057524d, before the page is loaded: each static is FUN_0059db20(name,
    // "StaticControl", page, 0) then StaticControl::Init FUN_00588540(mesh, &Offset, 0,
    // &{0,0,0}, NoZEnable 0, ZClear 0, NoMeshRender 0, NoWireRender 1, NoColoring 1,
    // RenderToWindow 0, NoClearRender 1, ForceRadius 0, RotationSpeed 0, EnableRotation 0,
    // &{0,0,0}) -- the zero vectors are FUN_00422b80(0, 0, 0) / three MOV 0 (the first). The three
    // "bImg" skip the Init on a null create (JE 0x00575072 / 0x005750ec / 0x00575166); "bigBox"
    // and "smallBox" have no null test. Every entry into case 18 builds them again (retail keeps
    // no latch; the page's slot 10 on the way out destroys the old ones as its children).
    void EnterCase18Chrome(PageCharacterSelectSingle& Page)
    {
        float const Zero[3] = {0.0f, 0.0f, 0.0f};
        for (int Index = 0; Index < 5; ++Index)
        {
            FCase18Static const& Slot = kCase18Statics[Index];
            Win3D* const Created = WinFactoryCreate(Slot.Nickname, "StaticControl", &Page, nullptr);
            Case18StaticSlot(Page, Index) = Created;
            if (Index < 3 && Created == nullptr)
                continue;
            static_cast<NnStaticControl*>(Created)->Init(Slot.Mesh, Slot.Offset, nullptr, Zero, 0, 0,
                                                         0, 1, 1, 0, 1, 0, 0, 0, Zero);
        }
    }
}

void MenuEnterStateReleaseActivePage(FMenuNavState* Nav)
{
    ReleaseActivePageInternal(Nav);
}

bool MenuNavStartSinglePlayer(FMenuNavState* Nav)
{
    // FUN_005739c0 (thiscall on the IntroFocus).
    SetMultiPlayer(false);
    Nav->Flag22d = 0;
    // 0x005739ef: FUN_0046b650 -- the chat MsgWin tree destroyed.
    ChatDestroyMsgWin();
    // 0x005739f4..0x00573b75: the three std::wstring assigns on the login context.
    G_LoginMgr.LoginName = L"SinglePlayer";  // +0x188 (0x00573a60)
    G_LoginMgr.LoginPassword = L"";          // +0x198 (0x00573ae5)
    G_LoginMgr.ServerPassword = L"";         // +0x130 (0x00573b47)
    if (!ConnectToServer(g_display.ini_path.Text)) // FUN_005a8770(DAT_00679d04)
        return false;
    if (!LoginToServer()) // FUN_005a8840
        return false;
    G_LoginMgr.Reset(); // FUN_005a9300
    G_LoginMgr.ReloadFactions(); // FUN_005aade0 (0x00573bce): the NewChar faction records
    return G_LoginMgr.RequestCharacterInfo() != 0; // FUN_005aa680
}

namespace
{
    // EnterState case 1 (0x005743f8..0x00574908).
    void EnterCase1(FMenuNavState* Nav)
    {
        G_LoginMgr.DestroyGun(); // FUN_005abc60

        if (Nav->Flag22d != 0)
        {
            // 0x00574412..0x00574576: back from multiplayer -- restart the single-player
            // session. The body is FUN_005739c0 inlined after the Init prologue calls.
            ShutdownServerConnection();     // FUN_005a8260
            ResetMissionOfferAnswerRetail(); // FUN_004c55b0 (0x00574417)
            G_LoginMgr.Reset();              // FUN_005a9300
            if (!MenuNavStartSinglePlayer(Nav))
                Nav->CurrentState = 0xd; // 0x00574576
        }

        SetClientMouseCursorDrawFlag(1); // FUN_0041dda0(1)
        if (Nav->ActivePage != nullptr)
        {
            Nav->ActivePage->Destroy(); // slot 10
            Nav->ActivePage = nullptr;
        }

        TypeSelect& Column = GetTypeSelect(); // DAT_0067b780
        Column.NextState = 0;                 // DAT_0067b800
        Column.bAnimating = 0;                // DAT_0067bcec
        Column.PendingCommand = 0;            // DAT_0067bce8
        Column.Create(nullptr, nullptr);      // slot 9 (0, 0)

        // 0x005745bd..0x005745f4: the caption box height is the "Normal" font's line height
        // (slot 6, unsigned) times 0.002.
        IFont* const NormalFont = FindMenuFontImp("Normal"); // FUN_004177b0
        float const BoxHeight =
            static_cast<float>(static_cast<unsigned>(NormalFont->GetLineHeight())) *
            kFontLineScale;

        // 0x005745f8..0x0057469c: five "bImg" StaticControls, FRONT_button.cmp, one per row.
        for (int Index = 0; Index < 5; ++Index)
        {
            Win3D* const Created = WinFactoryCreate("bImg", "StaticControl", &Column, nullptr);
            Column.Statics[Index] = Created;
            if (Created == nullptr)
                continue;
            float const PosY =
                kMainColumnPosY0 - static_cast<float>(Index) * kMainColumnPosYStep;
            // 0x0057461c..0x00574682 (bytes Ghidra left undisassembled): FUN_00588540(
            // FRONT_button.cmp, &Vector(-0.33, y, -1) (FUN_00422b80), hardpoint 0,
            // &Vector(0,0,0) half extents, NoZEnable 0, ZClear 0, NoMeshRender 0, NoWireRender 1,
            // NoColoring 1, RenderToWindow 0, NoClearRender 1, ForceRadius 0, RotationSpeed 0,
            // EnableRotation 0, &{0,0,0} (+0x70..+0x78 zeroed at 0x00574634..0x00574644)).
            float const Offset[3] = {kMainColumnPosX, PosY, kMainColumnPosZ};
            float const ZeroVector[3] = {0.0f, 0.0f, 0.0f};
            static_cast<NnStaticControl*>(Created)->Init(kFrontButtonMesh, Offset, nullptr,
                                                         ZeroVector, 0, 0, 0, 1, 1, 0, 1, 0, 0,
                                                         0, ZeroVector);
        }

        // 0x005746a2..0x005747d2: the captions, FUN_004347e0(DAT_0067eca8, ids, buf, 0x40),
        // then FUN_005753d0(&TypeSelect, buf, 0.25, height, command).
        for (int Index = 0; Index < 5; ++Index)
        {
            wchar_t Caption[0x40];
            GetIdsStringWWithRawFallback(kMainColumnSlots[Index].LabelIds, Caption, 0x40);
            Column.Buttons[Index].Init(&Column, Caption, kMainColumnBoxWidth, BoxHeight,
                                       kMainColumnSlots[Index].Command);
        }
        // 0x005747d7..0x00574808.
        for (int Index = 0; Index < 5; ++Index)
        {
            Column.Buttons[Index].FieldE8 = 1;
            Column.Buttons[Index].SetFlagsE4E5(1, 1);
        }
        Column.AnimTime = -2.0; // DAT_0067bce0/e4 = 0 / 0xc0000000

        // 0x0057480a..0x005748dc: the "Version" TextControl.
        Win3D* const VersionWin = WinFactoryCreate("Version", "TextControl", &Column, nullptr);
        Column.Version = VersionWin;
        if (VersionWin != nullptr)
        {
            // 0x00574846..0x00574877: FUN_0058a110("Normal", &(0.32, -0.35, -1), no hardpoint,
            // width 0.25, height = the caption box height (ESI), Arg6 1.0, no ids, +0x358 = 1,
            // +0x344 = 0, +0x378 = 0) -- the text comes from slot 42 below.
            NnTextControl* const VersionText = static_cast<NnTextControl*>(VersionWin);
            float const VersionPos[3] = {kVersionPosX, kVersionPosY, kVersionPosZ};
            VersionText->Init("Normal", VersionPos, nullptr, kMainColumnBoxWidth, BoxHeight, 1.0f,
                              0u, 1, 0, 0);

            wchar_t Format[0x100];
            GetIdsStringWWithRawFallback(0x4f7u, Format, 0x100);
            // 0x0057489a..0x005748c0: the word at DAT_0067ecba (the high half of
            // DAT_0067ecb8, g_client.packed_version), its high byte then its low byte.
            unsigned const VersionWord =
                (static_cast<unsigned>(g_client.packed_version) >> 16) & 0xffffu;
            wchar_t Text[0x100];
            swprintf(Text, 0x100, Format, (VersionWord >> 8) & 0xffu, VersionWord & 0xffu);
            // slot 42 (+0xa8) with property 0x1d: set the text. TODO(UI_CTRL): the port's
            // WinControl::SetProperty handles 1..11 only, so the text is not shown yet.
            VersionText->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Text), 0);
        }

        Column.RegisterEvent(0xf); // FUN_005760d0 on +0x7c, USER_CANCEL
        Nav->ActivePage = &Column;
        // 0x005748ee..0x00574902: CHARACTER_ID::operator=(&DAT_0067b20c, &DAT_0067bd40).
        GetPageCharacterSelect().CurrentCharacter = G_NoCharacterId;
    }
}

void EnterMenuState(FMenuNavState* Nav, int State)
{
    // FUN_005743a0 (thiscall, RET 4; .text:005743a0..0x005743f7 plus case 1 up to 0x0057490c lie
    // outside the Ghidra inventory -- docs/re/freelancer.md). No test of `this`.
    // 0x005743c6..0x005743df: sprintf("EnterState %d\n") into a local nobody reads, then
    // RefreshLoadingProgress.
    RefreshLoadingProgress();

    switch (State)
    {
    case 1:
        EnterCase1(Nav);
        break;

    case 2:
        // 0x574caf: CALL Common::SinglePlayer; if true → Current=18; else LAN/account path.
        if (SinglePlayer())
        {
            Nav->CurrentState = 18;
        }
        else
        {
            // 0x00574cc5: FUN_00579290 -- the loading screen runs.
            LoadingScreenStart();
            // 0x00574cca..0x00574f42: a CPlayerAccount on the stack (EH state 4, destroyed at
            // 0x00574f3c on both arms). LoadAccount's result is not tested (0x00574ce3);
            // only VerifyAccount decides (JZ 0x00574f47 -> state 5).
            CPlayerAccount Account;
            Account.LoadAccount();
            if (!Account.VerifyAccount())
            {
                Nav->CurrentState = 5; // 0x00574f47
                break;
            }
            // 0x00574d08: CLoginMgr +0x1a8 (DAT_0067e960) = 1 -- LoginToServer sends it.
            G_LoginMgr.bCreateAccount = 1;
            // 0x00574cfb..0x00574dd3: the login name is the account's signature of the current
            // server's +0x64 string (FUN_005684a0's copy, EH state 5; null _Ptr -> the empty
            // string), rebuilt from its c-string (EH state 7) and assigned to +0x188
            // (DAT_0067e940). The three temporaries die in reverse order (0x00574dd3 temp,
            // 0x00574de7 signature, 0x00574dfe..0x00574e7b the FServerInfo copy).
            G_LoginMgr.LoginName = std::wstring(
                Account.GetServerSignature(G_LoginMgr.GetCurrentServer().Field_0x64.c_str())
                    .c_str());
            // 0x00574e81..0x00574f04: +0x198 (DAT_0067e950) = L"" (DAT_005c8ce0), through a
            // temporary (EH state 8).
            G_LoginMgr.LoginPassword = std::wstring(L"");
            // 0x00574f0a..0x00574f26: FUN_005a8840; true -> FUN_005a9300 and state 9, false ->
            // state 4.
            if (LoginToServer())
            {
                G_LoginMgr.Reset();
                Nav->CurrentState = 9;
            }
            else
            {
                Nav->CurrentState = 4;
            }
        } // 0x00574f2d..0x00574f3c: ~CPlayerAccount
        break;

    case 3:
        // 0x574962: mov [ebp+4], 2
        Nav->CurrentState = 2;
        break;

    case 4:
        // 0x0057496e..0x005749ed.
        ShutdownServerConnection();      // FUN_005a8260
        ResetMissionOfferAnswerRetail(); // FUN_004c55b0 (0x00574973)
        G_LoginMgr.Reset();              // FUN_005a9300
        SetMultiPlayer(true);
        Nav->Flag22d = 1;
        ChatCreateMsgWin();              // FUN_0046b580 (0x00574994), the chat MsgWin
        Nav->CurrentState = 5; // 0x0057499d
        {
            // 0x005749a4..0x005749e7: a CPlayerAccount on the stack (EH state 3); no account
            // (LoadAccount false, JZ 0x005749d1) or a failed check (VerifyAccount false) ->
            // state 6, the account page.
            CPlayerAccount Account;
            if (!Account.LoadAccount() || !Account.VerifyAccount())
                Nav->CurrentState = 6; // 0x005749d1
        } // 0x005749d8..0x005749e7: ~CPlayerAccount
        // 0x005749ed: FUN_00579290 -- the loading screen runs.
        LoadingScreenStart();
        break;

    case 5:
    {
        // 0x00574b4e..0x00574b58: CHARACTER_ID::operator= into DAT_0067b20c (PageCharacterSelect
        // +0x8c) from DAT_0067bd40.
        GetPageCharacterSelect().CurrentCharacter = G_NoCharacterId;
        // 0x00574b5e: FUN_00579290 -- the loading screen runs; 0x00574b63: FUN_005a8260.
        LoadingScreenStart();
        ShutdownServerConnection();
        // 0x00574b68..0x00574b99: the active page closed (slot 10), the software cursor drawn
        // (FUN_0041dda0(1)), the chat window hidden in multiplayer (FUN_0046a4a0(0)).
        ReleaseActivePageInternal(Nav);
        SetClientMouseCursorDrawFlag(1);
        if (!SinglePlayer())
            ChatShowMsgWin(false);
        // 0x00574b9c..0x00574c15: unless the server page is already the active one: the server
        // lists reset (FUN_005abc80), the Gun made (FUN_005ab4c0), the (again closed) active
        // page replaced by PageServerSelect (DAT_0067ab20) -- its descriptor (+0x8c,
        // DAT_0067abac) loaded with FUN_00592230, then slot 9 Create(0, 0) -- and
        // DAT_0067c204 = 1. A failed load goes back to state 1 (LAB_00575294).
        PageServerSelect& ServerPage = GetPageServerSelect();
        if (Nav->ActivePage == &ServerPage)
            break;
        G_LoginMgr.ResetServerLists();
        G_LoginMgr.CreateGun();
        ReleaseActivePageInternal(Nav);
        if (!ServerPage.Desc.LoadFromFile(GetFullFilename(kPageServerSelect)))
        {
            Nav->CurrentState = 1;
            break;
        }
        ServerPage.Create(nullptr, nullptr);
        Nav->ActivePage = &ServerPage;
        GServerSelectActive = 1; // DAT_0067c204 = 1 (0x00574c0e)
        break;
    }

    case 6:
    {
        // 0x00574c1a..0x00574caa: the active page closed (slot 10), the software cursor drawn
        // (FUN_0041dda0(1)), the chat window hidden in multiplayer (FUN_0046a4a0(0)); unless the
        // account page is already the active one, the (again closed) active page is replaced by
        // PageAccountSelect (DAT_0067aa10): its descriptor (+0x8c, DAT_0067aa9c) loaded with
        // FUN_00592230, then slot 9 Create(0, 0). A failed load goes back to state 1
        // (LAB_00575294).
        ReleaseActivePageInternal(Nav);
        SetClientMouseCursorDrawFlag(1);
        if (!SinglePlayer())
            ChatShowMsgWin(false);
        PageAccountSelect& AccountPage = GetPageAccountSelect();
        if (Nav->ActivePage == &AccountPage)
            break;
        ReleaseActivePageInternal(Nav);
        if (!AccountPage.Desc.LoadFromFile(GetFullFilename(kPageAccountSelect)))
        {
            Nav->CurrentState = 1;
            break;
        }
        AccountPage.Create(nullptr, nullptr);
        Nav->ActivePage = &AccountPage;
        break;
    }

    case 7:
    case 11:
    case 13:
        // 0x57533d epilogue only — no body.
        break;

    case 8:
    case 15:
        // 0x5752d4: mov [ebp+4], 9
        Nav->CurrentState = 9;
        break;

    case 9:
    {
        // 0x00574f50..0x00574fb9: the player lists reset (FUN_004c4a80), the active page closed
        // (slot 10, +0x228 = 0), the chat window hidden in multiplayer (FUN_0046a4a0(0)), then
        // PageCharacterSelect (DAT_0067b180): its descriptor (+0x28c, DAT_0067b40c) loaded with
        // FUN_00592230, slot 9 Create(0, 0), +0x228 = the page. A failed load goes back to
        // state 1 (LAB_00575294).
        PreConnectReset();
        ReleaseActivePageInternal(Nav);
        if (!SinglePlayer())
            ChatShowMsgWin(false);
        PageCharacterSelect& CharacterPage = GetPageCharacterSelect();
        if (!CharacterPage.Desc.LoadFromFile(GetFullFilename(kPageCharacterSelect)))
        {
            Nav->CurrentState = 1;
            break;
        }
        CharacterPage.Create(nullptr, nullptr);
        Nav->ActivePage = &CharacterPage;
        break;
    }

    case 10:
        // 0x00574a2f..0x00574a56: release +0x228, PageOptions (DAT_0067a7d0) slot 9 (0, 0),
        // +0x228 = &PageOptions.
        ReleaseActivePageInternal(Nav);
        GetPageOptions().Create(nullptr, nullptr);
        Nav->ActivePage = &GetPageOptions();
        break;

    case 12:
        // 0x00574a65..0x00574b49.
        {
            // 0x00574a67..0x00574a75: FUN_00579280(1), FUN_00579290 -- the loading screen.
            LoadingScreenSetClear(1);
            LoadingScreenStart();
            // 0x00574a7a..0x00574ad8: with bDrawScripts (DAT_00614a9c) and latch != 4, the intro
            // base IntroFocus::Init entered is left.
            if (GetIntroConfigState()->bDrawScripts && GIntroModeLatch != 4)
                LeaveIntroBase();
            bool bSelected = false;
            if (Nav->Flag20c != 0)
            {
                // 0x00574ade..0x00574b17: FUN_005ab1f0(&+0xc).
                bSelected = G_LoginMgr.SelectCharacterById(Nav->CharacterId);
            }
            else
            {
                // 0x00574aec..0x00574b17: FUN_005aadb0; a hit is copied (0x200 bytes) to a
                // local and handed to FUN_005ab1f0; no autosave leaves the result false.
                CCharacterSet::CharacterInfo const* const AutoSave =
                    G_LoginMgr.FindAutoSaveCharacter();
                if (AutoSave != nullptr)
                {
                    CHARACTER_ID const Local = AutoSave->Id;
                    bSelected = G_LoginMgr.SelectCharacterById(Local);
                }
            }
            RefreshLoadingProgress(); // 0x00574b20
            if (bSelected)
            {
                G_LoginMgr.DestroyGun(); // FUN_005abc60
                Nav->CurrentState = 0x14;
            }
            else
            {
                Nav->CurrentState = 5;
            }
        }
        break;

    case 14:
    {
        // 0x005752a0..0x005752e7: release +0x228 (slot 10), GetFullFilename(PAGE_CharacterCreate),
        // FUN_005602b0 on DAT_0067a890; false -> CurrentState 9, true -> +0x228 = the page.
        ReleaseActivePageInternal(Nav);
        PageCharacterCreate& CreatePage = GetPageCharacterCreate();
        if (!CreatePage.Load(GetFullFilename(kPageCharacterCreate)))
        {
            Nav->CurrentState = 9;
            break;
        }
        Nav->ActivePage = &CreatePage;
        break;
    }

    case 16:
        // 0x005752e9..0x00575336: New Game. FUN_005aad80 (the Restart.fl record the
        // single-player session listed); none -> FDUMP and back to state 1; else its id into
        // +0xc (CHARACTER_ID::operator=), Flag20c = 1, CurrentState = 0xc.
        {
            CCharacterSet::CharacterInfo const* const Restart = G_LoginMgr.FindRestartCharacter();
            if (Restart == nullptr)
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
                      "E:\\FL\\Scratch\\Source\\Client\\Interface\\Intro.cpp", 0x1d95,
                      "New game character file not found\n");
                Nav->CurrentState = 1;
            }
            else
            {
                Nav->CharacterId = Restart->Id;
                Nav->Flag20c = 1;
                Nav->CurrentState = 0xc;
            }
        }
        break;

    case 17:
        // 0x005749f7..0x00574a2a: minimise the game window (DAT_0067eca0), run Flasher.exe and
        // wait for it (_spawnl(_P_WAIT, "Flasher.exe", "Flasher.exe", 0)), maximise again.
        ShowWindow(g_client.window, SW_MINIMIZE); // 6
        _spawnl(_P_WAIT, "Flasher.exe", "Flasher.exe", nullptr);
        ShowWindow(g_client.window, SW_MAXIMIZE); // 3
        break;

    case 18:
    {
        // 0x00574fbe..0x00574fcf: the active page's slot 10, +0x228 = 0.
        ReleaseActivePageInternal(Nav);
        // 0x00574fd5..0x00574fda: the full path of PAGE_CharSelectSingle.page (kept in EDI).
        char const* const PagePath = GetFullFilename(kPageCharSelectSingle);
        // 0x00574fe0..0x0057524d: the five statics under the page (DAT_0067ae10), before it is
        // loaded.
        PageCharacterSelectSingle& SinglePage = GetPageCharacterSelectSingle();
        EnterCase18Chrome(SinglePage);
        // 0x00575252..0x0057527b: FUN_00592230(path) on the page's descriptor (+0x8c,
        // DAT_0067ae9c); true -> page slot 9 (0, 0) and +0x228 = the page.
        if (SinglePage.Desc.LoadFromFile(PagePath))
        {
            SinglePage.Create(nullptr, nullptr);
            Nav->ActivePage = &SinglePage;
        }
        else
        {
            // 0x00575280..0x0057529b: page +0x108 (DAT_0067af18 / DAT_0067af1c) = -2.0, then
            // CurrentState = 1.
            SinglePage.SlideT = -2.0;
            Nav->CurrentState = 1;
        }
        break;
    }

    case 19:
        // 0x0057490d..0x0057495d: back from the game (latch 2 / 4). FUN_005aa680 relists the
        // characters; failure -> epilogue. Latch 2 -> PageCharacterSelect (state 9);
        // otherwise select the character by the name DAT_006732f0 kept (FUN_005aad30 ->
        // FUN_005ab250) and, on success, straight back into the game (0x14).
        if (G_LoginMgr.RequestCharacterInfo() == 0)
        {
            break;
        }
        if (GIntroModeLatch == 2)
        {
            Nav->CurrentState = 9;
            break;
        }
        if (G_LoginMgr.SelectCharacter(
                G_LoginMgr.FindCharacterByName(GetPlayerCharacterNameRetail())))
        {
            Nav->CurrentState = 0x14;
        }
        break;

    default:
        break;
    }
    RefreshLoadingProgress(); // 0x0057533d
}

int MenuStateDriverUpdate(FMenuNavState* Nav, float DeltaSeconds)
{
    // FUN_00573eb0 (thiscall, RET 4).
    int TransitionCode = 0; // [esp+0x18]
    bool bDidEnter = false; // BL

    // 0x00573ed8..0x00573f53: leave the old state, enter the new one, until it settles.
    while (Nav->CurrentState != Nav->PrevState)
    {
        bDidEnter = true;
        RefreshLoadingProgress(); // EBP = RefreshLoadingProgress (0x00573ee3)
        int const Leaving = Nav->PrevState;
        // sprintf("ExitState %d\n") into a local nobody reads (0x00573ef3).

        if (Leaving == 5)
        {
            GServerSelectActive = 0; // DAT_0067c204 = 0 (0x00573f34)
        }
        else if (Leaving == 10)
        {
            // 0x00573f02..0x00573f28: DAT_0067c20c missing or inactive (flags bit0 clear)
            // -> CurrentState = 1, delete it (slot 0 with 1), DAT_0067c20c = 0.
            if (GOptionsChild_67c20c == nullptr || (GOptionsChild_67c20c->Flags & 1) == 0)
            {
                Nav->CurrentState = 1;
                if (GOptionsChild_67c20c != nullptr)
                    delete GOptionsChild_67c20c;
                GOptionsChild_67c20c = nullptr;
            }
        }

        RefreshLoadingProgress(); // 0x00573f3b
        Nav->PrevState = Nav->CurrentState;
        EnterMenuState(Nav, Nav->CurrentState);
        RefreshLoadingProgress(); // 0x00573f4b
    }

    // 0x00573f5b..0x00573f6f: per-state poll. Byte table 0x00574380 over CurrentState - 1
    // (20 entries, states 1..20); index 10 is the no-poll exit 0x00574182.
    static unsigned char const kPollIndex[20] = {0, 10, 10, 10, 1, 2, 10, 10, 3, 4,
                                                 10, 10, 5, 6, 10, 10, 7, 8, 10, 9};
    unsigned const PollSlot = static_cast<unsigned>(Nav->CurrentState - 1);
    unsigned char const Poll = PollSlot < 20u ? kPollIndex[PollSlot] : 10;
    switch (Poll)
    {
    case 0:
    {
        // 0x00573f76 (state 1): TypeSelect+0x80 (DAT_0067b800). Not cleared here -- EnterState
        // case 1 clears it.
        int const Next = GetTypeSelect().NextState;
        if (Next != 0)
        {
            Nav->CurrentState = Next;
            if (Next == 0xc)
                RefreshLoadingProgress(); // 0x00573f8f
        }
        break;
    }
    case 1:
    {
        // 0x00574006 (state 5): PageServerSelect+0xb4 (DAT_0067abd4), cleared once taken.
        PageServerSelect& ServerPage = GetPageServerSelect();
        if (ServerPage.Result != 0)
        {
            Nav->CurrentState = ServerPage.Result;
            ServerPage.Result = 0;
        }
        break;
    }
    case 2:
    {
        // 0x00574021 (state 6): PageAccountSelect+0xb4 (DAT_0067aac4), cleared once taken.
        PageAccountSelect& AccountPage = GetPageAccountSelect();
        if (AccountPage.Result != 0)
        {
            Nav->CurrentState = AccountPage.Result;
            AccountPage.Result = 0;
        }
        break;
    }
    case 3:
    {
        // 0x0057403c (state 9): PageCharacterSelect (DAT_0067b180).
        PageCharacterSelect& CharacterPage = GetPageCharacterSelect();
        // 0x0057403c..0x005740b2: its +0x5f8 (DAT_0067b778) set -> the intro base left under
        // bDrawScripts (0x0057404e..0x0057409b), DAT_00668762 = 0 again when the latch is 4,
        // then +0x5f9 (DAT_0067b779) = 1 for the page's Update.
        if (CharacterPage.bLoadRequested != 0)
        {
            if (GetIntroConfigState()->bDrawScripts)
                LeaveIntroBase();
            if (GIntroModeLatch == 4)
                SetClientLocationNoServerNotify(false); // 0x005740ab
            CharacterPage.bIntroBaseLeft = 1;
        }
        // 0x005740b9..0x0057415b: +0x2b4 (DAT_0067b434) -> CurrentState, +0x399 (DAT_0067b519)
        // -> +0xc, Flag20c = 1; the field is not cleared.
        if (CharacterPage.Result != 0)
        {
            Nav->CurrentState = CharacterPage.Result;
            Nav->CharacterId = CharacterPage.SelectedCharacter;
            Nav->Flag20c = 1;
        }
        break;
    }
    case 4:
    {
        // 0x00573f96 (state 10): PageOptions. +0xb8 (DAT_0067a888) is the NN_Preferences
        // window, +0xb4 (DAT_0067a884) its result.
        PageOptions& Options = GetPageOptions();
        WinControl* const Preferences = Options.Preferences;
        if (Preferences == nullptr)
        {
            Nav->CurrentState = 1; // 0x00573fdc
            Options.Result = 0;
            break;
        }
        // 0x00573f9f: the byte at +0x32c of the NN_Preferences object (NnPreferences::bClosed,
        // set by its close paths and by a failed Create).
        bool const bClosed = static_cast<NnPreferences*>(Preferences)->bClosed != 0;
        if (bClosed)
        {
            Win3DDeleteTree(Preferences); // FUN_0043d2a0
            Options.Preferences = nullptr;
            Nav->CurrentState = 1;
            Options.Result = 0;
            break;
        }
        if (Options.Result != 0)
        {
            Nav->CurrentState = Options.Result;
            Options.Result = 0;
        }
        break;
    }
    case 5:
        // 0x0057417a (state 13): the single-player start failed -- client state 1 (quit).
        TransitionCode = 1;
        break;
    case 6:
    {
        // 0x0057415d..0x00574178 (state 14): PageCharacterCreate (DAT_0067a890) -- FUN_005602e0
        // (a result is set) -> CurrentState = FUN_005602a0 (+0xb4). The field is not cleared.
        PageCharacterCreate& CreatePage = GetPageCharacterCreate();
        if (CreatePage.HasResult())
            Nav->CurrentState = CreatePage.GetResult();
        break;
    }
    case 7:
        // 0x00574182 (state 17): no poll.
        break;
    case 8:
    {
        // 0x005740cd (state 18): PageCharacterSelectSingle (DAT_0067ae10).
        PageCharacterSelectSingle& SinglePage = GetPageCharacterSelectSingle();
        // 0x005740cd..0x00574133: its +0x368 (DAT_0067b178) set -> the intro base left under
        // bDrawScripts (0x005740df..0x0057412c), then +0x369 (DAT_0067b179) = 1 for the page's
        // Update. Unlike index 3 there is no latch-4 write.
        if (SinglePage.bLoadRequested != 0)
        {
            if (GetIntroConfigState()->bDrawScripts)
                LeaveIntroBase();
            SinglePage.bIntroBaseLeft = 1;
        }
        // 0x0057413a..0x0057415b: +0xb4 (DAT_0067aec4) -> CurrentState, +0x118 (DAT_0067af28)
        // -> +0xc, Flag20c = 1; the field is not cleared.
        if (SinglePage.Result != 0)
        {
            Nav->CurrentState = SinglePage.Result;
            Nav->CharacterId = SinglePage.SelectedCharacter;
            Nav->Flag20c = 1;
        }
        break;
    }
    case 9:
        // 0x00573fef (state 20): into the game -- FUN_004c4810 (the player's base,
        // DAT_00673358 & 0x3fffffff, is set) ? 3 : 2.
        TransitionCode = (GetPlayerBaseIdRetail() & 0x3fffffffu) != 0 ? 3 : 2;
        break;
    default:
        break;
    }

    // 0x00574182..0x00574197: FUN_00428030(3), FUN_00428040(EDI, dt) -- the sound mixer mode
    // and tick. EDI is 0 on every path here (0x00573ec8 / 0x00573f59, no poll writes it): no
    // camera, the manager keeps its listener. dt is the uncapped argument.
    SetClientAudioMode(3);
    TickClientSoundMixer(nullptr, DeltaSeconds);

    if (bDidEnter)
    {
        return TransitionCode; // 0x0057419a: TEST BL,BL / JNZ 0x00574340
    }
    // 0x005741a2..0x005741a9: DAT_00614a9c (bDrawScripts) clear -> 0x00574340.
    if (!GetIntroConfigState()->bDrawScripts)
        return TransitionCode;

    // 0x005741af..0x005741d7: the frame time as a double, capped at 0.2 (DAT_005cf100): FCOMP /
    // TEST AH,0x41 / JNZ keeps it when not greater (or unordered); the fall-through stores 0.2.
    double Dt = static_cast<double>(DeltaSeconds);
    if (Dt > 0.2)
        Dt = 0.2;
    // 0x005741d7..0x00574219: +0x230 (float) + Dt; the sum rounded to float is kept
    // ([ESP+0x18]), floor(sum * 1000.0) (DAT_005d3f50) are the whole milliseconds, +0x230 =
    // float(sum_f - ms * 0.001) (DAT_005c75f8, FSUBP ST1,ST0), and ftol(ms) goes to FUN_00456650
    // on DAT_0066d2d0.
    double const Sum = static_cast<double>(Nav->Field230) + Dt;
    float const SumAsFloat = static_cast<float>(Sum);
    double const Milliseconds = floor(Sum * 1000.0);
    Nav->Field230 = static_cast<float>(static_cast<double>(SumAsFloat) - Milliseconds * 0.001);
    G_GfScriptManager.Update(static_cast<int>(Milliseconds));

    // 0x0057421e..0x00574230: the capped time as a float (EBX) to FUN_00439540 on DAT_00668708.
    float const DtFloat = static_cast<float>(Dt);
    TickClientLocationController(DtFloat);

    // 0x00574235..0x00574263: every RoomObject (FUN_005593c0's list) whose +0x04 is clear gets
    // slot 5 (+0x14) with the capped double.
    std::list<FRoomObject*>& Objects = GetRoomObjectList();
    for (std::list<FRoomObject*>::iterator It = Objects.begin(); It != Objects.end(); ++It)
    {
        FRoomObject* const Object = *It;
        if (Object->bNoUpdate == 0)
            Object->Update(Dt);
    }

    // 0x00574265..0x0057427f: FUN_0041a140(FUN_00438c90(), DtFloat) -- the room camera's
    // letterbox; FUN_00412360(DtFloat) -- the fade; FUN_0041b2e0 with the uncapped argument
    // ([ESP+0x434]).
    UpdateRoomLetterbox(static_cast<FRoomCamera*>(static_cast<ICamera*>(GetClientRoomCamera())),
                        DtFloat);
    UpdateFade(DtFloat);
    TickMaterialAnimationPlayback(DeltaSeconds);

    // 0x00574287..0x00574316: the frame, only when FUN_00424f30 begins one (JZ 0x0057431b).
    if (BeginClientRenderFrame())
    {
        LoadingScreenReset();                                   // FUN_00579190 (0x00574294)
        Win3DSetViewport(nullptr, kIntroCpp, 0x1ac7);           // FUN_00422610 (0x005742a5)
        Win3DSetPipelineClearColor(0.0f, 0.0f, 0.0f, 1.0f);     // FUN_004214d0 (0x005742b5)
        ClearWin3DBuffers(kWin3DClearColour | kWin3DClearDepth); // FUN_004215e0(0x4100)
        DrawClientLocation();                                   // FUN_0043bbc0 on DAT_00668708
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        // Port-only: the logo sits in the top left of its 4:3 picture and goes with the main
        // menu column to the left edge (hud_widescreen.cpp).
        ImageSetWidescreenAlign(-1);
        Nav->LogoImage.Draw();                                  // +0x210 slot 2 (0x005742dd)
        ImageSetWidescreenAlign(0);
#else
        Nav->LogoImage.Draw();                                  // +0x210 slot 2 (0x005742dd)
#endif
        ClearWin3DBuffers(kWin3DClearDepth);                    // FUN_004215e0(0x100)
        Win3DDrawAll();                                         // FUN_0059e300 (0x005742ed)
        // 0x005742f2..0x00574306: FUN_0042d730 clear -> FUN_00412430(FUN_00438c90()).
        if (!IsClientServerTickPaused())
            DrawFade(static_cast<ICamera*>(GetClientRoomCamera()));
        Sleep(0);                                               // 0x0057430b
        EndClientRenderScene();                                 // FUN_00425080
        PresentClientRenderFrame();                             // FUN_00425140
    }

    // 0x0057431b..0x00574339: once (+0x22c), the loading screen (DAT_0067c3b8) gets its
    // progress bar (FUN_00579170), drops the startup Image (FUN_00579260) and stops clearing
    // (FUN_00579280(0)).
    if (Nav->Flag22c == 0)
    {
        LoadingScreenCreateProgressBar();
        LoadingScreenReleaseImage();
        LoadingScreenSetClear(0);
        Nav->Flag22c = 1;
    }

    // 0x00574340: [ESP+0x18].
    return TransitionCode;
}

int MenuNavGetIntroModeLatch()
{
    return GIntroModeLatch;
}

void MenuNavSetIntroModeRequest(int Mode)
{
    // DAT_0067c200 — consumed into DAT_0067c1fc at menu Init / next latch read.
    GIntroModeRequest = Mode;
}

int MenuNavGetIntroModeRequest()
{
    return GIntroModeRequest;
}

bool MenuNavIsServerSelectActive()
{
    // FUN_00564b20: MOV AL,[DAT_0067c204]; RET. Read by WaitForServerResponse (005a8ba1).
    return GServerSelectActive != 0;
}

void MenuNavConsumeIntroModeRequest()
{
    GIntroModeLatch = GIntroModeRequest;
    GIntroModeRequest = 0;
}

// The flat-shell bridge that stood here (MenuEnterStateCase1HitTest / Click / Draw /
// SetHoverFromPoint and the focus stubs) is gone: the column's TextMsgButtons get the mouse the
// retail way -- MouseDispatchEvents (FUN_0041ef20) -> KeyMapRouteMouseEvent -> commands 4..13 ->
// FWin3DInputHandler::Handle (FUN_0059d140) -> Win3DPick -> slot 19, the hover from
// Win3DUpdateAll (FUN_0059e080), the draw from Win3DDrawAll (FUN_0059e300, 0x005742ed).
