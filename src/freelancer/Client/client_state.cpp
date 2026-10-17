// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "client_state.h"

#include "common_ini.h"
#include "loading_screen.h"
#include "dataconfig_progress.h"
#include "globals.h"
#include "input_manager.h"
#include "login_mgr.h"
#include "menu_enter_state.h"
#include "page_account_select.h"
#include "page_character_create.h"        // PageCharacterCreate (DAT_0067a890)
#include "page_character_select.h"        // PageCharacterSelect (DAT_0067b180)
#include "page_character_select_single.h" // PageCharacterSelectSingle (DAT_0067ae10)
#include "page_server_select.h"
#include "page_options.h"
#include "server_connect.h"
#include "type_select.h"
#include "mission_briefing.h"
#include "connect_flow.h"
#include "gameflow.h"
#include "space_state.h"
#include "character_select_equip.h" // GetPlayerBaseIdRetail (FUN_004c4810's DAT_00673358)
#include "main_command_handler.h"   // SetClientQuitRequested (DAT_0067ecbd)
#include "nn_modal.h"               // G_bNnScreenUp (DAT_0067dcc8)
#include "player_spawn.h"           // ResetMissionOfferAnswerRetail (FUN_004c55b0)
#include "fade_to_black.h"          // SetFadeImmediate / SetFadeTarget (FUN_00412070 / 00412050)
#include "gf_script_object.h"       // G_GfScriptManager (DAT_0066d2d0)
#include "menu_universe_base.h"     // ResetClientLocationController / EnterClientLocationBase
#include "nav_bar.h"                // NavBarDestroy (FUN_00442130)
#include "sound_config.h"           // SetClientAudioMode (FUN_00428030)
#include "win3d.h"                  // Win3DFindByName (FUN_0059da10)

#include "common_universe.h"        // Universe::get_base_id

#include "common_server.h"          // IServer (CharacterSkipAutosave)
#include "common_server_types.h"    // CHARACTER_ID
#include "dacom.h"                  // FDUMP

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace
{
    // Retail IntroFocus (DAT_0067a598), RTTI .?AUIntroFocus@@ (COL 0x005f04a0): the vftable
    // pointer, then FMenuNavState (menu_enter_state.h). Vftable 0x005e3034, fifteen slots (raw
    // PE dwords; the next dword is string data):
    //   0 0x0043d8f0 (XOR AL,AL / RET 0x10)  1 0x004b50e0 (XOR AL,AL / RET 0xc)  2 0x004b50e0
    //   3 0x0043d8f0  4 0x004b50e0  5 0x00401e90 (RET)  6 0x0053e4a0 (RET 4)
    //   7 LAB_00517320 (MOV EAX,2)  8 / 9 / 10 0x00401e90  11 LAB_00437400 (RET 8)
    //   12 FUN_00573860  13 LAB_00573c00  14 FUN_00573eb0
    // Static init LAB_00573790: ctor FUN_00573840 (+0x0c byte 0 -- the CHARACTER_ID ctor,
    // +0x224 = 0 and +0x210 vptr 0x005e2718 -- the Image ctor, then the IntroFocus vptr), atexit
    // LAB_005737b0 -> dtor FUN_005737c0 (the Image dtor on +0x210 only). Both are the
    // compiler-generated special members of this struct.
    struct IntroFocus : public IEnvironment
    {
        FMenuNavState Nav;

        // Slots 0 and 3 (0x0043d8f0): XOR AL,AL / RET 0x10.
        bool Slot00(unsigned A, unsigned Message, unsigned B, unsigned C) override
        {
            (void)A;
            (void)Message;
            (void)B;
            (void)C;
            return false;
        }

        // Slots 1, 2 and 4 (0x004b50e0): XOR AL,AL / RET 0xc.
        bool Slot01(unsigned A, unsigned B, unsigned C) override
        {
            (void)A;
            (void)B;
            (void)C;
            return false;
        }

        bool Slot02(unsigned A, unsigned B, unsigned long Packed) override
        {
            (void)A;
            (void)B;
            (void)Packed;
            return false;
        }

        bool Slot03(unsigned A, unsigned Message, unsigned B, unsigned long Packed) override
        {
            (void)A;
            (void)Message;
            (void)B;
            (void)Packed;
            return false;
        }

        bool Slot04(unsigned A, unsigned B, unsigned C) override
        {
            (void)A;
            (void)B;
            (void)C;
            return false;
        }

        // Slot 5 (0x00401e90): a bare RET.
        void Slot05() override
        {
        }

        // Slot 6 (0x0053e4a0): a bare RET 4.
        void Slot06(unsigned A) override
        {
            (void)A;
        }

        // Slot 7 (LAB_00517320): MOV EAX,2 / RET -- the environment type FUN_005b0880 and
        // FUN_005b2a30 case 3 test for.
        int GetEnvironmentType() override
        {
            return 2;
        }

        // Slot 8 (0x00401e90): a bare RET (InitializeEngine 0x005b1e04..0x005b1e0d calls it).
        void PreInit() override
        {
        }

        // Slot 9 (0x00401e90): a bare RET (the atexit teardown 0x005b21d2..0x005b21db).
        void Shutdown() override
        {
        }

        // Slot 10 (0x00401e90): a bare RET.
        void Slot10() override
        {
        }

        // Slot 11 (+0x2c) is LAB_00437400 (.rdata 0x005e3060), the same body as the GF
        // state's: FUN_00558d60(Width, Height), which reads neither argument. RET 8.
        void Resize(int Width, int Height) override
        {
            (void)Width;
            (void)Height;
            ResetGameflowRoomLetterbox();
        }

        bool Init(int Param) override
        {
            // FUN_00573860 (RET 4). Param is not read.
            (void)Param;
            ShutdownServerConnection();      // FUN_005a8260
            ResetMissionOfferAnswerRetail(); // FUN_004c55b0 (0x00573869)
            G_LoginMgr.Reset();              // FUN_005a9300
            Nav.Flag20c = 0;
            Nav.PrevState = 0;

            // 0x00573883..0x005738d8: latch DAT_0067c1fc = request DAT_0067c200, request = 0.
            MenuNavConsumeIntroModeRequest();
            int const Latch = MenuNavGetIntroModeLatch();
            if (Latch == 2 || Latch == 4)
            {
                Nav.CurrentState = 0x13; // back from the game: EnterState case 19
            }
            else if (Latch == 3)
            {
                Nav.CurrentState = 5; // PageServerSelect
            }
            else
            {
                // 0 / 1 start at the column; any other latch keeps CurrentState. Both start
                // the single-player session (FUN_005739c0); failure -> state 13 (quit).
                if (Latch == 0 || Latch == 1)
                    Nav.CurrentState = 1;
                if (!MenuNavStartSinglePlayer(&Nav))
                    Nav.CurrentState = 0xd;
            }

            // 0x005738df..0x005738f6: +0x228 = 0, Image(+0x210)->slot 1(&DAT_0067bf4c); the
            // result is not read.
            Nav.ActivePage = nullptr;
            Nav.LogoImage.Load(GetIntroConfigState()->Logo.Text);

            // 0x005738f9..0x005739b0: with bDrawScripts (DAT_00614a9c) and latch != 4, the intro
            // base is entered. 0x00573919 sets LocationController+0x5a before any base work;
            // LoadLocationBase reads it through `this+0x5a` at 0x004398a4 and returns before
            // server synchronization or looking up the player's as-yet-unset system id.
            if (GetIntroConfigState()->bDrawScripts && Latch != 4)
            {
                // 0x00573912..0x00573938: DAT_0067bf40 (intro.ini's pick) 1 -> Intro2_Base,
                // 2 -> Intro3_Base, anything else -> Intro1_Base; Universe::get_base_id.
                int const Pick = GetIntroConfigState()->IntroBasePick;
                SetClientLocationNoServerNotify(true); // DAT_00668762 = 1 (0x00573919)
                char const* BaseNickname = "Intro1_Base";
                if (Pick == 1)
                    BaseNickname = "Intro2_Base";
                else if (Pick == 2)
                    BaseNickname = "Intro3_Base";
                unsigned const BaseId = Universe::get_base_id(BaseNickname);

                MouseSetCursor("Arrow", true);   // FUN_0041dde0("Arrow", 1) (0x00573947)
                SetClientMouseCursorDrawFlag(1); // FUN_0041dda0(1) (0x0057394e)
                Nav.Flag22c = 0;
                Nav.Field230 = 0.0f;
                SetClientAudioMode(3);           // FUN_00428030(3) (0x00573961)
                SetFadeImmediate(1.0f);          // FUN_00412070(1.0) (0x0057396b)
                // 0x00573970..0x00573983: FUN_00412050(0, (float)(1.0 / DAT_0061650c)).
                SetFadeTarget(0.0f,
                              static_cast<float>(1.0 / static_cast<double>(GetFadeDuration())));
                // 0x00573990: FUN_004564d0 on DAT_0066d2d0 -- the Gf script manager takes id 0xf.
                G_GfScriptManager.RegisterEvents();
                Nav.Field230 = 0.0f;
                ResetClientLocationController(); // FUN_00439200 on DAT_00668708 (0x005739a0)
                EnterClientLocationBase(BaseId); // FUN_0043b290(BaseId) (0x005739ab)
                NavBarDestroy();                 // FUN_00442130 (0x005739b0)
            }
            return true;
        }

        void Destroy() override
        {
            // FUN_00573c00.
            MenuEnterStateReleaseActivePage(&Nav); // +0x228 slot 10, +0x228 = 0
            // 0x00573c22..0x00573ce3: TypeSelect::Destroy (FUN_00566ab0) inlined.
            GetTypeSelect().Destroy();
            // 0x00573ceb..0x00573d7e: PageCharacterSelect's slot 10 (FUN_00567290) inlined on
            // DAT_0067b180.
            GetPageCharacterSelect().Destroy();
            // 0x00573d51..0x00573e67: PageCharacterSelectSingle's slot 10 (LAB_0056b240)
            // inlined on DAT_0067ae10 (its child walk copies the class name with FUN_00575450,
            // the constructor form of LAB_0056b240's FUN_00420750).
            GetPageCharacterSelectSingle().Destroy();
            GetPageCharacterCreate().Destroy(); // FUN_005602f0 on DAT_0067a890 (0x00573e6d)
            GetPageServerSelect().Destroy();  // FUN_0056d8f0 on DAT_0067ab20 (0x00573e77)
            GetPageAccountSelect().Destroy(); // FUN_005727b0 on DAT_0067aa10 (0x00573e81)
            GetPageOptions().Destroy(); // FUN_00591de0 on DAT_0067a7d0 (0x00573e8b)
            // 0x00573e90..0x00573ea2: FUN_005650d0 inlined on the logo Image -- its BigImage
            // (+0x224) released (slot 2) and cleared.
            Nav.LogoImage.Release();
        }

        int Update(float DeltaSeconds) override
        {
            // FUN_00573eb0 -- the whole IntroFocus frame, the scene step and the draw included.
            // The column's mouse input arrives through the Win3D manager (FUN_0059d140 /
            // FUN_0059e080), not through this slot.
            return MenuStateDriverUpdate(&Nav, DeltaSeconds);
        }
    };

    // DAT_0067a598. The retail IntroFocus offsets hold on the 32-bit build.
    IntroFocus G_IntroFocus;
    static_assert(sizeof(void*) != 4 || offsetof(IntroFocus, Nav.CurrentState) == 0x04,
                  "IntroFocus +0x04 current state");
    static_assert(sizeof(void*) != 4 || offsetof(IntroFocus, Nav.PrevState) == 0x08,
                  "IntroFocus +0x08 previous state");
    static_assert(sizeof(void*) != 4 || offsetof(IntroFocus, Nav.CharacterId) == 0x0c,
                  "IntroFocus +0x0c CHARACTER_ID");
    static_assert(sizeof(void*) != 4 || offsetof(IntroFocus, Nav.Flag20c) == 0x20c,
                  "IntroFocus +0x20c");
    static_assert(sizeof(void*) != 4 || offsetof(IntroFocus, Nav.LogoImage) == 0x210,
                  "IntroFocus +0x210 logo Image");
    static_assert(sizeof(void*) != 4 || offsetof(IntroFocus, Nav.ActivePage) == 0x228,
                  "IntroFocus +0x228 active page");
    static_assert(sizeof(void*) != 4 || offsetof(IntroFocus, Nav.Flag22c) == 0x22c,
                  "IntroFocus +0x22c");
    static_assert(sizeof(void*) != 4 || offsetof(IntroFocus, Nav.Flag22d) == 0x22d,
                  "IntroFocus +0x22d");
    static_assert(sizeof(void*) != 4 || offsetof(IntroFocus, Nav.Field230) == 0x230,
                  "IntroFocus +0x230");

    // The previous state's slot 13 as every case of FUN_005b2a30 sends it: TEST ESI,ESI / JZ,
    // then CALL [vftable + 0x34] (e.g. 0x005b2b38..0x005b2b40).
    void DestroyPreviousState(IEnvironment* PreviousState)
    {
        if (PreviousState != nullptr)
            PreviousState->Destroy();
    }

    // LAB_005b2b43..0x005b2b63: FUN_0054bd30 (the space state)->slot 12 Init(0); true -> it,
    // false -> 0. Case 2 comes here after the loading screen and the previous state's slot 13;
    // case 7 jumps straight here (0x005b2ae5) -- no second loading screen, the previous state
    // already destroyed.
    IEnvironment* InitSpaceStateTail()
    {
        IEnvironment* const Space = GetSpaceState();
        if (!Space->Init(0))
            return nullptr;
        return Space;
    }

    IEnvironment* EnterInGameState(IEnvironment* PreviousState)
    {
        // 0x005b2b29..0x005b2b33: FUN_00579280(1), FUN_00579290 -- the loading screen; then
        // the previous state's slot 13 (0x005b2b38..0x005b2b40).
        LoadingScreenSetClear(1);
        LoadingScreenStart();
        DestroyPreviousState(PreviousState);
        return InitSpaceStateTail();
    }

    IEnvironment* EnterMenuState(IEnvironment* PreviousState)
    {
        // FUN_005b2a30 case 6 (0x005b2bc7..0x005b2be8): the previous state's slot 13, then
        // FUN_00575430 (IntroFocus)->slot 12 Init(0) -- its answer is not tested; IntroFocus is
        // returned either way.
        DestroyPreviousState(PreviousState);
        IEnvironment* const Intro = GetIntroFocusState();
        Intro->Init(0);
        return Intro;
    }

    // FUN_005b2a30 case 3 (0x005b2b64..0x005b2bc6) -- into GF, the base: FUN_00437420 returns
    // DAT_006686a0, whose vftable 0x005cae24 has the COL 0x005e8734 -> .?AUGF@@ (environment
    // type 1). Code 3 is what FUN_005b20a0 and the menu driver (0x00573fef) return when the
    // player's base is set; it is not a preferences screen.
    IEnvironment* EnterBaseState(IEnvironment* PreviousState)
    {
        // 0x005b2b64..0x005b2b6b: FUN_00579280(1), FUN_00579290 -- the loading screen
        // (DAT_0067c3b8).
        LoadingScreenSetClear(1);
        LoadingScreenStart();

        // 0x005b2b70..0x005b2b86: FUN_0059da10("NN_Preferences"); none -> DAT_0067dcc8 = 0.
        if (Win3DFindByName("NN_Preferences") == nullptr)
            G_bNnScreenUp = false;

        // 0x005b2b75, 0x005b2b8b..0x005b2ba4: the flag starts at 1 and drops to 0 unless the
        // previous state's slot 7 (+0x1c) answers 2 (IntroFocus); then the previous state's slot
        // 13. Retail keeps the flag in the low byte of its own PreviousState argument slot and
        // pushes that whole dword (0x005b2bae); GF's Init reads only the low byte (MOV DL,
        // [ESP+4] at 0x004372a0), so the port passes 0 / 1.
        int bFromIntro = 1;
        if (PreviousState != nullptr)
        {
            if (PreviousState->GetEnvironmentType() != 2)
                bFromIntro = 0;
            PreviousState->Destroy();
        }
        // 0x005b2ba7..0x005b2bc6: GF->slot 12 (+0x30) Init(bFromIntro); true -> GF is the new
        // state, false -> 0 (GF's Init, LAB_004372a0, always answers true).
        IEnvironment* const Gameflow = GetGameflowState();
        if (!Gameflow->Init(bFromIntro))
            return nullptr;
        return Gameflow;
    }

    // FUN_005b2a30 case 8 -- into MissionBriefing (FUN_0043d560 = DAT_00668774, COL 0x005e88d4).
    // Code 8 is what the GF base frame answers after FUN_00558a30 (RequestGameflowMissionBriefing).
    // The state is mission_briefing.cpp (vftable 0x005cb094).
    IEnvironment* EnterMissionBriefingState(IEnvironment* PreviousState)
    {
        // 0x005b2b08..0x005b2b12: FUN_00579280(1), FUN_00579290 -- the loading screen.
        LoadingScreenSetClear(1);
        LoadingScreenStart();
        // 0x005b2b17..0x005b2b1f: the previous state's slot 13.
        DestroyPreviousState(PreviousState);
        // The briefing's update (LAB_0043d360) draws and presents its own frame, as GF's does.

        // 0x005b2b22..0x005b2b55: FUN_0043d560()->slot 12 Init(0); false -> 0.
        IEnvironment* const Briefing = GetMissionBriefingState();
        if (!Briefing->Init(0))
            return nullptr;
        return Briefing;
    }

    // FUN_005b2a30 case 7 (0x005b2a53..0x005b2b07) -- restart the single-player session on the
    // character in DAT_0067ea78. FUN_005b2c70 stores that character and queues state 7
    // (DAT_0067ecc4 = 7); its callers are 0x00488144 and 0x004bc8d5, the load-game paths.
    IEnvironment* EnterLoadGameState(IEnvironment* PreviousState)
    {
        // 0x005b2a53..0x005b2a5d: FUN_00579280(1), FUN_00579290 -- the loading screen.
        LoadingScreenSetClear(1);
        LoadingScreenStart();
        // 0x005b2a62..0x005b2a6a: the previous state's slot 13. On the failure exits below
        // retail returns this same, already destroyed, state (ESI) -- reproduced.
        DestroyPreviousState(PreviousState);

        // 0x005b2a6d..0x005b2a93: FUN_005a8260, FUN_005a8960(&DAT_00679d04), FUN_005aa680. A
        // failure leaves through caseD_8, which returns the previous state (ESI).
        ShutdownServerConnection();
        if (!ConnectAndLogin(g_display.ini_path.Text))
            return PreviousState;
        if (G_LoginMgr.RequestCharacterInfo() == 0)
            return PreviousState;

        // 0x005b2a99..0x005b2add: FUN_005ab1f0(&DAT_0067ea78); false -> the WARNING FDUMP (main.cpp
        // line 0x6fd), DAT_0067ecbd = 1, return the previous state.
        if (!G_LoginMgr.SelectCharacterById(G_LoadGameCharacterId))
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: Selection of character id %s failed\n",
                  "E:\\FL\\Scratch\\Source\\Client\\main.cpp", 0x6fd,
                  G_LoadGameCharacterId.Buffer);
            SetClientQuitRequested(true);
            return PreviousState;
        }

        // 0x005b2ade..0x005b2b07: FUN_004c4810 (the player's base is set) -> GF->slot 12 Init(1)
        // (true -> GF, false -> 0).
        if ((GetPlayerBaseIdRetail() & 0x3fffffffu) != 0)
        {
            IEnvironment* const Gameflow = GetGameflowState();
            if (!Gameflow->Init(1))
                return nullptr;
            return Gameflow;
        }

        // 0x005b2ae5 -> LAB_005b2b43: in space, the tail case 2 shares -- FUN_0054bd30 Init(0).
        // The previous state is already gone and the loading screen already up.
        return InitSpaceStateTail();
    }
}

// DAT_0067ea78 -- the character state 7 selects. Static init LAB_005b0810 zeroes its first byte
// (the CHARACTER_ID ctor); FUN_005b2c70 (QueueLoadGameCharacter) copies a character in.
CHARACTER_ID G_LoadGameCharacterId;

// DAT_0067ecc4 -- written 7 by FUN_005b2c70 only; the frame (0x005b29b3..0x005b29be) takes it
// over the state's own answer and clears it.
int G_PendingClientStateId = 0;

void QueueLoadGameCharacter(CHARACTER_ID const* CharId, bool bSkipAutosave)
{
    // FUN_005b2c70 (cdecl): an empty id does nothing; otherwise queue state 7, copy the id
    // (CHARACTER_ID::operator=, COMMON.DLL) and, for the autosave, IServer::CharacterSkipAutosave
    // (+0x60) -- no server test.
    if (CharId->Buffer[0] == 0)
        return;
    G_PendingClientStateId = 7;
    G_LoadGameCharacterId = *CharId;
    if (bSkipAutosave)
        G_Server->CharacterSkipAutosave(GetPlayerConnectionId());
}

int ConsumePendingClientState(int StateId)
{
    // 0x005b29b3..0x005b29be
    if (G_PendingClientStateId != 0)
    {
        StateId = G_PendingClientStateId;
        G_PendingClientStateId = 0;
    }
    return StateId;
}

IEnvironment* EnterClientState(int StateId, IEnvironment* PreviousState)
{
    // FUN_005b2a30 (cdecl). 0x005b2a3c: RefreshLoadingProgress on entry, and again on every
    // way out (each case's tail and LAB_005b2c23).
    RefreshLoadingProgress();

    IEnvironment* Result = PreviousState;
    switch (StateId)
    {
    case 1:
        // caseD_1 (0x005b2c0f..0x005b2c1d): the previous state's slot 13, PostQuitMessage(0),
        // 0 returned.
        DestroyPreviousState(PreviousState);
        PostQuitMessage(0);
        Result = nullptr;
        break;
    case 2:
        Result = EnterInGameState(PreviousState);
        break;
    case 3:
        Result = EnterBaseState(PreviousState);
        break;
    case 4:
        // caseD_4 (0x005b2bfc..0x005b2c0e): GF (FUN_00437420)->slot 10 (+0x28), the previous
        // state returned as is. GF's slot 10 is 0x00401e90, a bare RET.
        GetGameflowState()->Slot10();
        break;
    case 5:
        // caseD_5 (0x005b2be9..0x005b2bfb): the space state (FUN_0054bd30)->slot 10 (+0x28), the
        // previous state returned as is. SpaceApp's slot 10 (vftable 0x005e0554 + 0x28 =
        // 0x00401e90) is a bare RET too.
        GetSpaceState()->Slot10();
        break;
    case 6:
        Result = EnterMenuState(PreviousState);
        break;
    case 7:
        Result = EnterLoadGameState(PreviousState);
        break;
    case 8:
        Result = EnterMissionBriefingState(PreviousState);
        break;
    default:
        // 0x005b2a42..0x005b2a46: DEC / CMP 7 / JA -- any other id returns the previous state.
        break;
    }
    RefreshLoadingProgress();
    return Result;
}

bool IsClientIntroEnvironment()
{
    // FUN_005b0880: DAT_0067ecac (current state) non-null && current->slot 7 (+0x1c) == 2 (MOV
    // AL,1); otherwise XOR AL,AL. Only IntroFocus (slot 7 = LAB_00517320) answers 2.
    IEnvironment* const Current = g_client.current_state;
    if (Current == nullptr)
        return false;
    return Current->GetEnvironmentType() == 2;
}

IEnvironment* GetIntroFocusState()
{
    // FUN_00575430: MOV EAX,0x0067a598 (the IntroFocus singleton); RET.
    return &G_IntroFocus;
}

void CallClientStateUpdateSlot(float DeltaSeconds)
{
    // 0047409f..004740b6 (and 00474209 / 00474384, 0056385b..00563872): ECX = DAT_0067ecac;
    // non-null -> CALL [vftable + 0x38] with the frame time as a float, the result ignored.
    IEnvironment* const Current = g_client.current_state;
    if (Current != nullptr)
        Current->Update(DeltaSeconds);
}
