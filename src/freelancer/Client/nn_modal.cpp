// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "nn_modal.h"

#include "character_select_equip.h" // GetPlayerBaseIdRetail (DAT_00673358), GetPlayerStarSystemIdRetail
#include "client_base_market.h" // GetClientCurrentLocationBaseData (FUN_0043b630)
#include "globals.h"       // SetClientServerTickPause (FUN_0042d6f0), GetClientFrameTimeCapped
#include "hud_comm.h"      // HudCommIsWindowOpen (FUN_004cc8d0)
#include "ids_strings.h"   // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "input_manager.h" // Get/SetClientMouseCursorDrawFlag (FUN_0041dd90 / FUN_0041dda0)
#include "menu_universe_base.h" // GetClientCurrentBaseStridName (FUN_0043bbe0)
#include "infocard_map.h"  // InfocardMapCreate / InfocardMapDestroy (FUN_00458790 / FUN_00458860)
#include "knowledge_map.h" // KnowledgeMapCreate / KnowledgeMapDestroy (FUN_00459830 / FUN_00459900)
#include "cargo_bar_control.h"
#include "font_string.h"   // FontStringConstruct (FUN_004144b0)
#include "menu_ui_widgets.h"
#include "nn_rollover.h"   // NnRolloverShowHoverGlobal (FUN_004b62d0)
#include "tooltip_hover.h" // FToolTipHoverInfo, ToolTipHoverReset (FUN_004963a0)
#include "nav_bar.h"       // NavBarHasServiceWindow (FUN_00442720)
#include "nn_arrival_text.h"
#include "nn_inventory.h"  // IsPlayerDockedRetail (FUN_004c4810)
#include "nn_main_button.h"
#include "nn_messaging.h"  // NnVoiceInit (FUN_00489de0)
#include "nn_trade.h"      // G_NnTrade (DAT_006724b4)
#include "nn_story_star.h" // G_MissionObjectiveWindow (DAT_00672400), StoryStarReset
#include "nn_text_control.h"
#include "nn_text_scratch.h" // G_NnTextScratchA/B, G_NnTextScratchCapacity
#include "nn_ui_pulse.h"   // G_NnPaletteArgb_679b90 (DAT_00679b90)
#include "player_behavior.h" // OnMouseFlightRawMessage (FUN_004c80f0)
#include "room_control.h"  // FRoomRuntime, GetRoomRuntimeRoomId (runtime +0x38)
#include "space_letterbox.h" // IsLetterboxVisible (FUN_0041a3e0)
#include "win3d.h"         // Win3DDeleteTree (FUN_0043d2a0)
#include "win_factory.h"   // WinFactoryCreate (FUN_0059db20)

#include "common_goods.h"  // BaseData::get_base_id
#include "common_id.h"     // CreateID, MakeLocationID
#include "common_universe.h"

#include <stdio.h>
#include <wchar.h>
#include <windows.h>

bool G_bNnScreenUp = false;              // DAT_0067dcc8
NN_MainButton* G_NnMainButton = nullptr; // DAT_0066d3f0
NnArrivalText* G_NnArrivalText = nullptr; // DAT_0066d398

// Retail DAT_0066d35c -- WINFACTORY<NN_ArrivalText> static (LAB_0045ab60 -> FUN_0045b4c0; Create
// FUN_0045b530; atexit LAB_0045ab80 -> FUN_00579380).
WINFACTORY<NnArrivalText> G_NnArrivalTextFactory("NN_ArrivalText");

namespace
{
    // DAT_0066d344 -- set by the static initializer LAB_0045a0e0 to
    // CreateID("click_on_characters_and_buttons"); only reader FUN_0045a270 (0x0045a37e).
    unsigned const G_NnVoiceClickOnCharactersId = CreateID("click_on_characters_and_buttons");

    // FUN_0059db20("NN_ArrivalText", "NN_ArrivalText", 0, 0) when DAT_0066d398 is null, then the
    // pointer re-read -- the head every cdecl entry point spells out (0x0045b2d0.., 0x0045b310..,
    // 0x0045b340.., 0x0045b430.., 0x0045b490..).
    NnArrivalText* EnsureNnArrivalText()
    {
        if (G_NnArrivalText == nullptr)
            WinFactoryCreate("NN_ArrivalText", "NN_ArrivalText", nullptr, nullptr);
        return G_NnArrivalText;
    }

    // The strid_name of the player's system: Universe::get_system(FUN_004c4830()) and its slot 3
    // (+0x0c), or 0 when there is no such system -- 0x0045aebd..0x0045aed7, 0x0045b004..
    // 0x0045b01e, 0x0045b1d4..0x0045b1ee.
    int PlayerSystemStridName()
    {
        Universe::ISystem const* const System =
            Universe::get_system(GetPlayerStarSystemIdRetail());
        if (System == nullptr)
            return 0;
        return System->GetStridName();
    }

    // .rdata 0x005ce928: the typing time per character ShowBaseArrival reserves.
    constexpr float kArrivalSecondsPerChar = 0.06f;
    // Each Show* reads its format (ids 0x55c / 0x55d / 0x55e) with a count of 0x80 into a stack
    // buffer of 0x80 BYTES (0x40 wchars) -- a retail overrun for a format of 64 characters or
    // more. The port's buffer holds the 0x80 wchars the count allows; the formats are short.
    constexpr int kArrivalFormatChars = 0x80;
}

NnArrivalText::NnArrivalText()
{
    // FUN_0045b530 0x0045b569..0x0045b583: FUN_0059f860, vptrs, DAT_0066d398 = this, +0x338 = 0.
    G_NnArrivalText = this;
    bWaitForLetterbox = 0;
}

NnArrivalText::~NnArrivalText()
{
    // FUN_0045abb0: the vptrs, DAT_0066d398 = 0, JMP FUN_0059fa50 (the WinControl dtor).
    G_NnArrivalText = nullptr;
}

bool NnArrivalText::Create(Win3D* NewParent, void* Data)
{
    // FUN_0045abd0 (RET 8).
    (void)NewParent;

    // 0x0045abd4..0x0045ac22: FUN_005a0e30 "ArrivalText" / "PageHeader" at (-0.45f, 0.24f, 0),
    // no hardpoint, 0.95f x 0.06f, 1.4f, ids 0, 0, 0, last byte 1 -> +0x32c.
    float const CaptionPosition[3] = {NnFloatBits(0xbee66666), NnFloatBits(0x3e75c28f), 0.0f};
    Caption = static_cast<NnTextControl*>(CreateTextControl(
        "ArrivalText", "PageHeader", CaptionPosition, nullptr, NnFloatBits(0x3f733333),
        NnFloatBits(0x3d75c28f), NnFloatBits(0x3fb33333), 0, 0, 0, 1));

    // 0x0045ac1b..0x0045ac2e: the caption's slot 42 (0x33, &DAT_00679b90, 1) -- the colour
    // override. No null test on the control.
    Caption->SetProperty(0x33, reinterpret_cast<uintptr_t>(&G_NnPaletteArgb_679b90), 1);
    // 0x0045ac34..0x0045ac3a: the caption hidden (flags &= 0xfc).
    Caption->Flags = static_cast<unsigned char>(Caption->Flags & 0xfc);

    // 0x0045ac3e..0x0045ac73: slot 31 SetOffset(0, 0, -1.0f), the vector passed by value.
    SetOffset(0.0f, 0.0f, -1.0f);
    // 0x0045ac76..0x0045ac7e: slot 5 SetPosition(this + 0x60).
    SetPosition(Position);

    // 0x0045ac8c / 0x0045ac96
    HoldTime = 2.0f;
    Delay = 4.0f;

    // 0x0045ac81..0x0045aca7: FUN_004b5060 with [this + 4] (the window's current parent, not
    // NewParent) and Data, SETNZ.
    return Win3D::Create(GetParent(), Data);
}

void NnArrivalText::DestroySelf()
{
    Win3D* Child = GetChildHead();
    while (Child != nullptr)
    {
        Win3D* const Next = Child->GetNextSibling();
        Win3DDeleteTree(Child);
        Child = Next;
    }
    Destroy();
    delete this;
}

void NnArrivalText::Update()
{
    // LAB_0045acc0. 0x0045acc8..0x0045ad07: FUN_0045a470 (NN_MainButton has a panel open) tears
    // the caption down.
    if (NnMainButtonHasActivePanel())
    {
        DestroySelf();
        return;
    }

    // 0x0045ad08..0x0045ad49: waiting for the letterbox. While it is up (FUN_0041a3e0) the
    // caption stays hidden and nothing else runs; once it is down a delay at or below zero (FCOMP
    // 0.0f, TEST AH,0x41 / JP skips -- NaN skips) shows it.
    if (bWaitForLetterbox != 0)
    {
        if (IsLetterboxVisible())
        {
            Caption->Flags = static_cast<unsigned char>(Caption->Flags & 0xfc);
            return;
        }
        if (Delay <= 0.0f)
            Caption->Flags = static_cast<unsigned char>(Caption->Flags | 3);
    }

    // 0x0045ad4d..0x0045adb7: the caption's visible bit is sampled here. A caption that is
    // showing with its delay spent goes away as soon as the mission-objective banner
    // (DAT_00672400, MissionObjectiveWindow) or HUD_Comm's window (FUN_004cc8d0) is up.
    bool const bWasVisible = ((Caption->Flags >> 1) & 1) != 0;
    if (Delay <= 0.0f && bWasVisible)
    {
        if (G_MissionObjectiveWindow != nullptr || HudCommIsWindowOpen())
        {
            DestroySelf();
            return;
        }
    }

    // 0x0045adb8: FUN_005a17b0, the base Update.
    Win3D::Update();

    if ((Caption->Flags & 2) == 0)
    {
        // 0x0045adcb..0x0045ae03: hidden -- unless one of those windows is up, the delay runs
        // down by the capped frame time (in double, stored as float) and a negative delay
        // (TEST AH,5 / JP skips) shows the caption.
        if (G_MissionObjectiveWindow == nullptr && !HudCommIsWindowOpen())
        {
            Delay = static_cast<float>(static_cast<double>(Delay) - GetClientFrameTimeCapped());
            if (Delay < 0.0f)
                Caption->Flags = static_cast<unsigned char>(Caption->Flags | 3);
        }
    }
    else if (Caption->bTypewriter == 0)
    {
        // 0x0045ae2c..0x0045ae8a: showing and fully typed (+0x362 clear) -- the hold time runs
        // down the same way; a negative one tears the caption down.
        HoldTime = static_cast<float>(static_cast<double>(HoldTime) - GetClientFrameTimeCapped());
        if (HoldTime < 0.0f)
        {
            DestroySelf();
            return;
        }
    }

    // 0x0045ae07..0x0045ae1f: a caption that became visible this frame starts typing
    // (property 0x50 with 1 and 60 ms per character).
    if (!bWasVisible && (Caption->Flags & 2) != 0)
        Caption->SetProperty(0x50, 1, 0x3c);
}

void NnArrivalText::ShowBaseArrival(float ArrivalTime)
{
    // FUN_0045ae90 (RET 4). 0x0045ae9b..0x0045aea2: FUN_004c4810 -- only docked.
    if (!IsPlayerDockedRetail())
        return;
    // 0x0045aead
    bWaitForLetterbox = 0;

    // 0x0045aea8..0x0045aee3: FUN_0043bbe0 on DAT_00668708 (the base's strid_name), then the
    // system's; either one zero returns.
    int const BaseIds = GetClientCurrentBaseStridName();
    int const SystemIds = PlayerSystemStridName();
    if (BaseIds == 0 || SystemIds == 0)
        return;

    // 0x0045aee9..0x0045af5f: ids 0x55c is the format; base and system names into the two halves
    // of scratch A; swprintf into scratch B; CharUpperW.
    wchar_t Format[kArrivalFormatChars];
    GetIdsStringWWithRawFallback(0x55c, Format, kArrivalFormatChars);
    int const Half = G_NnTextScratchCapacity / 2;
    wchar_t* const SecondHalf = G_NnTextScratchA + Half;
    GetIdsStringWWithRawFallback(static_cast<unsigned>(BaseIds), G_NnTextScratchA, Half);
    GetIdsStringWWithRawFallback(static_cast<unsigned>(SystemIds), SecondHalf, Half);
    _swprintf(G_NnTextScratchB, Format, G_NnTextScratchA, SecondHalf);
    CharUpperW(G_NnTextScratchB);

    // 0x0045af65..0x0045af76: the caption's property 0x1d (the text).
    Caption->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
    int const Length = static_cast<int>(wcslen(G_NnTextScratchB));

    // 0x0045af87..0x0045afcd: only a positive ArrivalTime (TEST AH,0x41 / JNZ skips) sets the
    // delay so typing (0.06f a character) plus the hold ends with it; a negative result (TEST
    // AH,5 / JP skips) is clamped to 0.
    if (!(ArrivalTime > 0.0f))
        return;
    Delay = ArrivalTime - (static_cast<float>(Length) * kArrivalSecondsPerChar + HoldTime);
    if (Delay < 0.0f)
        Delay = 0.0f;
}

void NnArrivalText::ShowSystemArrival(bool bResetDelay)
{
    // FUN_0045aff0 (RET 4). 0x0045affb
    bWaitForLetterbox = 1;

    int const SystemIds = PlayerSystemStridName();

    // 0x0045b020..0x0045b055: the region caption by the strid_name's place in 0x30001..0x30018
    // (byte table 0x0045b1a0 over the jump table 0x0045b190).
    unsigned const Index = static_cast<unsigned>(SystemIds) - 0x30001u;
    unsigned RegionIds = 0;
    if (Index <= 4)
        RegionIds = 0x4e1;
    else if (Index <= 10)
        RegionIds = 0x4e2;
    else if (Index <= 16)
        RegionIds = 0x4e4;
    else if (Index <= 0x17)
        RegionIds = 0x4e3;

    // 0x0045b057
    if (SystemIds == 0)
        return;

    wchar_t Format[kArrivalFormatChars];
    if (RegionIds != 0)
    {
        // 0x0045b071..0x0045b0fb: ids 0x55e, the system and the region into the two halves.
        GetIdsStringWWithRawFallback(0x55e, Format, kArrivalFormatChars);
        int const Half = G_NnTextScratchCapacity / 2;
        wchar_t* const SecondHalf = G_NnTextScratchA + Half;
        GetIdsStringWWithRawFallback(static_cast<unsigned>(SystemIds), G_NnTextScratchA, Half);
        GetIdsStringWWithRawFallback(RegionIds, SecondHalf, Half);
        _swprintf(G_NnTextScratchB, Format, G_NnTextScratchA, SecondHalf);
        CharUpperW(G_NnTextScratchB);
        Caption->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
    }
    else
    {
        // 0x0045b0fd..0x0045b164: ids 0x55d with the system alone; the delay becomes 2.0f.
        GetIdsStringWWithRawFallback(0x55d, Format, kArrivalFormatChars);
        GetIdsStringWWithRawFallback(static_cast<unsigned>(SystemIds), G_NnTextScratchA,
                                     G_NnTextScratchCapacity / 2);
        _swprintf(G_NnTextScratchB, Format, G_NnTextScratchA);
        CharUpperW(G_NnTextScratchB);
        Caption->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
        Delay = 2.0f;
    }

    // 0x0045b16e..0x0045b179
    if (bResetDelay)
        Delay = 2.0f;
}

void NnArrivalText::ShowNamedArrival(unsigned NameIds)
{
    // FUN_0045b1c0 (RET 4). 0x0045b1cb
    bWaitForLetterbox = 0;

    int const SystemIds = PlayerSystemStridName();
    // 0x0045b1f0..0x0045b201
    if (NameIds == 0 || SystemIds == 0)
        return;

    // 0x0045b207..0x0045b29a: ids 0x55c, NameIds and the system into the two halves, the text,
    // delay 2.0f.
    wchar_t Format[kArrivalFormatChars];
    GetIdsStringWWithRawFallback(0x55c, Format, kArrivalFormatChars);
    int const Half = G_NnTextScratchCapacity / 2;
    wchar_t* const SecondHalf = G_NnTextScratchA + Half;
    GetIdsStringWWithRawFallback(NameIds, G_NnTextScratchA, Half);
    GetIdsStringWWithRawFallback(static_cast<unsigned>(SystemIds), SecondHalf, Half);
    _swprintf(G_NnTextScratchB, Format, G_NnTextScratchA, SecondHalf);
    CharUpperW(G_NnTextScratchB);
    Caption->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
    Delay = 2.0f;
}

bool NnArrivalText::IsCaptionVisible() const
{
    // FUN_0045b2c0: MOV EAX,[ECX+0x32c] / MOV AL,[EAX+0x6c] / SHR AL,1 / AND AL,1.
    return ((Caption->Flags >> 1) & 1) != 0;
}

void NnArrivalTextShow()
{
    // FUN_0045b310 (cdecl): ensure, then FUN_0045aff0(0) (0x0045b338..0x0045b33a).
    NnArrivalText* const Self = EnsureNnArrivalText();
    if (Self == nullptr)
        return;
    Self->ShowSystemArrival(false);
}

void NnArrivalTextShowBase(float ArrivalTime)
{
    // FUN_0045b2d0 (cdecl): ensure, then FUN_0045ae90(ArrivalTime).
    NnArrivalText* const Self = EnsureNnArrivalText();
    if (Self == nullptr)
        return;
    Self->ShowBaseArrival(ArrivalTime);
}

void NnArrivalTextShowNamed(unsigned NameIds)
{
    // FUN_0045b340 (cdecl): ensure, then FUN_0045b1c0(NameIds).
    NnArrivalText* const Self = EnsureNnArrivalText();
    if (Self == nullptr)
        return;
    Self->ShowNamedArrival(NameIds);
}

void NnArrivalTextDestroy()
{
    // FUN_0045b380 (cdecl): no window -> nothing; else the teardown.
    NnArrivalText* const Self = G_NnArrivalText;
    if (Self == nullptr)
        return;
    Self->DestroySelf();
}

void NnArrivalTextHurry()
{
    // FUN_0045b3c0 (cdecl). 0x0045b3cb..0x0045b3fc: a delay above 0 (TEST AH,0x41 / JNZ goes to
    // the teardown -- NaN too) is cut to 1.0f when above it; otherwise the window goes.
    NnArrivalText* const Self = G_NnArrivalText;
    if (Self == nullptr)
        return;
    if (Self->Delay > 0.0f)
    {
        if (Self->Delay > 1.0f)
            Self->Delay = 1.0f;
        return;
    }
    Self->DestroySelf();
}

void NnArrivalTextShowRaw(float Delay, uintptr_t Arg)
{
    // FUN_0045b430 (cdecl): ensure; +0x334 = Delay (dword copy), +0x338 = 0, then the caption's
    // slot 42 (0x1e, Arg, 0).
    NnArrivalText* const Self = EnsureNnArrivalText();
    if (Self == nullptr)
        return;
    Self->Delay = Delay;
    Self->bWaitForLetterbox = 0;
    Self->Caption->SetProperty(0x1e, Arg, 0);
}

void NnArrivalTextShowSystemNow()
{
    // FUN_0045b490 (cdecl): ensure, then FUN_0045aff0(1).
    NnArrivalText* const Self = EnsureNnArrivalText();
    if (Self == nullptr)
        return;
    Self->ShowSystemArrival(true);
}

void NnMainButtonOnModalOpen()
{
    // FUN_0045a460: MOV ECX,[DAT_0066d3f0] / TEST / JZ RET / JMP FUN_0045cf60.
    if (G_NnMainButton == nullptr)
        return;
    G_NnMainButton->ClosePanel();
}

void NnMainButtonSetPanelOrigin(int PanelId, float const* Origin)
{
    // FUN_0045a780: MOV ECX,[DAT_0066d3f0] / TEST / JZ RET / FUN_0045c7a0(PanelId, Origin).
    if (G_NnMainButton == nullptr)
        return;
    G_NnMainButton->SetPanelOrigin(PanelId, Origin);
}

WinControl* NnMainButtonGetPanelButton(int PanelId)
{
    // FUN_0045a7a0: null -> XOR EAX,EAX; else FUN_0045c820(PanelId).
    if (G_NnMainButton == nullptr)
        return nullptr;
    return G_NnMainButton->GetPanelButton(PanelId);
}

bool NnMainButtonHasActivePanel()
{
    // FUN_0045a470: null -> XOR AL,AL; else JMP FUN_0045da00 (`[ECX + 0x3a8] != 0`).
    if (G_NnMainButton == nullptr)
        return false;
    return G_NnMainButton->HasActivePanel();
}

WinControl* G_NnMissionMessage = nullptr; // DAT_00671ff4

void NnMissionMessageDestroy()
{
    // FUN_0048a130: with a NeuroNetMissionMessage window, DAT_0067dcc8 = 0, FUN_0043d2a0 on every
    // child (next sibling read first), slot 10 Destroy, slot 0 with 1. The pointer is not
    // cleared here; the window's dtor (0x0048a46c) does that.
    WinControl* const Window = G_NnMissionMessage;
    if (Window == nullptr)
        return;
    G_bNnScreenUp = false;
    Win3DDeleteTree(Window);
}

unsigned char G_bNnStoryStarKeepPending = 0; // DAT_0066d35a

void NnSystemInit(bool bCreateMainButton)
{
    // FUN_0045a100 (cdecl). Nothing when NN_MainButton already exists.
    if (G_NnMainButton != nullptr)
        return;
    G_bNnStoryStarKeepPending = 0;
    // 0x0045a110: FUN_00459830 -- the KnowledgeMap singleton DAT_0066d334
    // (interface\KnowledgeMap.ini).
    KnowledgeMapCreate();
    // 0x0045a115: FUN_00458790 -- the InfocardMap singleton DAT_0066d320
    // (interface\InfocardMap.ini).
    InfocardMapCreate();
    // 0x0045a11a..0x0045a130: FUN_0059db20("NN_MainButton", "NN_MainButton", 0, 0).
    if (bCreateMainButton)
        WinFactoryCreate("NN_MainButton", "NN_MainButton", nullptr, nullptr);
    // 0x0045a138: JMP FUN_00489de0.
    NnVoiceInit();
}

void NnSystemShutdown()
{
    // FUN_0045a140 (cdecl).
    // 0x0045a140..0x0045a15d: NN_MainButton slot 10 Destroy, then (re-read) slot 0 with 1.
    if (G_NnMainButton != nullptr)
    {
        G_NnMainButton->Destroy();
        if (G_NnMainButton != nullptr)
            delete G_NnMainButton;
    }
    // 0x0045a15f..0x0045a174: the trade window's slot 42 (0xef02, 0, 0).
    if (G_NnTrade != nullptr)
        G_NnTrade->SetProperty(0xef02, 0, 0);
    // 0x0045a17a: FUN_00459900 -- free the KnowledgeMap.
    KnowledgeMapDestroy();
    // 0x0045a17f: FUN_00458860 -- free the InfocardMap.
    InfocardMapDestroy();
    // 0x0045a184: FUN_0045b380.
    NnArrivalTextDestroy();
    // 0x0045a189: FUN_004b9bb0 -- the story star reset (the DAT_00672328 pending FmtStr list
    // unless DAT_0066d35a, then the DAT_00672400 banner).
    StoryStarReset();
    // 0x0045a18e: JMP FUN_0048a130.
    NnMissionMessageDestroy();
}

void NnMainButtonOnUnclaimedMouse(unsigned Msg, unsigned WParam, unsigned LParam)
{
    // FUN_0045a270 (cdecl, 3 args).
    (void)WParam;
    (void)LParam;

    // 0x0045a28a..0x0045a2a0: FUN_0041dd90() and DAT_0067dcc8 == 0.
    if (GetClientMouseCursorDrawFlag() == 0)
        return;
    if (G_bNnScreenUp)
        return;

    // 0x0045a2a6: FUN_004c4810 -- `(DAT_00673358 & 0x3fffffff) != 0`, the player is docked.
    // In space the message goes to FUN_004c80f0(Msg, WParam, LParam) (0x0045a424).
    if ((GetPlayerBaseIdRetail() & 0x3fffffffu) == 0)
    {
        OnMouseFlightRawMessage(Msg);
        return;
    }

    // 0x0045a2b3: docked, and no NN_MainButton -> nothing.
    if (G_NnMainButton == nullptr)
        return;
    // 0x0045a2bf..0x0045a2c6: FUN_00442720 -- a NavBar with one of the service windows up.
    if (NavBarHasServiceWindow())
        return;
    // 0x0045a2cc..0x0045a2d9: FUN_0045da00 -- the NN_MainButton has a panel open.
    if (G_NnMainButton->HasActivePanel())
        return;

    // 0x0045a2df..0x0045a32c: a hover record -- caption null, the Font::String constructed
    // (FUN_004144b0), IdsAlt 0, Ids -1, +0x44 0, the caption cleared (FUN_00414280), +0x60 0 --
    // then Ids = IdsAlt = 0x45e.
    FToolTipHoverInfo Record; // caption null + FUN_004144b0: the record's constructor
    ToolTipHoverReset(&Record);
    Record.Ids = 0x45e;
    Record.IdsAlt = 0x45e;
    // 0x0045a330..0x0045a36a: in the base's "Cityscape" room (the current room runtime's id
    // +0x38 against MakeLocationID(BaseData's id, "Cityscape"); FUN_0043a460 / FUN_0043b630,
    // both null-tested) the ids are 0x3a0 instead.
    FRoomRuntime* const Runtime = static_cast<FRoomRuntime*>(GetClientCurrentLocationRuntime());
    BaseData* const Base = GetClientCurrentLocationBaseData();
    if (Runtime != nullptr && Base != nullptr &&
        GetRoomRuntimeRoomId(Runtime) == MakeLocationID(Base->get_base_id(), "Cityscape"))
    {
        Record.Ids = 0x3a0;
        Record.IdsAlt = 0x3a0;
    }

    // 0x0045a36e..0x0045a379: FUN_004b62d0(&record, 8.0f) -- the rollover shows the record for
    // eight seconds.
    NnRolloverShowHoverGlobal(&Record, NnFloatBits(0x41000000));
    // 0x0045a37e..0x0045a386: FUN_00489e80(DAT_0066d344, 0) -- the voice line.
    NnVoiceEnqueue(G_NnVoiceClickOnCharactersId, 0);
    // 0x0045a38e..0x0045a40e: the record's dtor inlined (FUN_00444ee0) -- ~FToolTipHoverInfo.
}

unsigned char NnModalPauseGame()
{
    // FUN_004a7670: 004a7671 PUSH 1 / CALL FUN_0042d6f0; 004a7678 CALL FUN_0041dd90, MOV BL,AL;
    // 004a767d PUSH 1 / CALL FUN_0041dda0; 004a7689 MOV AL,BL.
    SetClientServerTickPause(true);
    unsigned char const bPrevious = GetClientMouseCursorDrawFlag();
    SetClientMouseCursorDrawFlag(1);
    return bPrevious;
}

void NnModalResumeGame(unsigned char bCursorDrawFlag)
{
    // FUN_004a7690: 004a7690 PUSH 0 / CALL FUN_0042d6f0; 004a76a2 JMP FUN_0041dda0 with the
    // argument slot rewritten to param_1.
    SetClientServerTickPause(false);
    SetClientMouseCursorDrawFlag(bCursorDrawFlag);
}
