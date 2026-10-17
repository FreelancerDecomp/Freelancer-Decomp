// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The pause/resume pair every NeuroNet modal dialog (NN_Exit, NN_NetworkOperation, NN_Error,
// BlockingDialog) brackets itself with. NN_Prompt does not: neither its ctor (FUN_00474a30), dtor
// (FUN_00472a20), Create (FUN_00472ab0) nor any close path (LAB_00473070 / LAB_00473240 /
// LAB_00473420) calls them. Retail keeps them next to NN_Pause (0x004a7670 / 0x004a7690); both
// are cdecl.

// FUN_004a7670 -- FUN_0042d6f0(1) (SetClientServerTickPause), then remember the software-cursor
// draw flag (FUN_0041dd90), force it on (FUN_0041dda0(1)) and return the remembered value. The
// dialogs keep the result in a byte of their own and hand it back to NnModalResumeGame.
unsigned char NnModalPauseGame();

// FUN_004a7690 -- FUN_0042d6f0(0), then FUN_0041dda0(bCursorDrawFlag) (a tail jump).
void NnModalResumeGame(unsigned char bCursorDrawFlag);

// DAT_0067dcc8 -- a byte the NeuroNet windows raise while one of them holds the screen (written
// 1 at 0x0045c921, 0x0048a537, 0x004a6a31, 0x004a98fe; 0 at 0x0045c914, 0x0048a13b,
// 0x0048b005.., 0x004af057, and the space teardown 0x0054af3c = DestroySpaceState) and readers test (0x0043e748.., 0x0045a297 in FUN_0045a270,
// 0x0045a740, 0x0046fcf3, NN_Pause 0x004a75c0). The Ship's jump states also write it (1 at
// 0x005438ce entering state 1, 0 at 0x00543851 leaving state 5 -- FClientShipGameobj::
// SetJumpState); of the writers listed above only the space teardown is ported so far.
extern bool G_bNnScreenUp;

// The NN_MainButton singleton, DAT_0066d3f0 (RTTI .?AVNN_MainButton@@; class in
// nn_main_button.h). Written by its ctor FUN_0045b700 and cleared by its dtor FUN_0045b820;
// NnSystemInit makes it, NnSystemShutdown deletes it.
class NN_MainButton;
class WinControl;
extern NN_MainButton* G_NnMainButton;

// FUN_0045a100 (cdecl) -- the NeuroNet system: when NN_MainButton does not exist yet,
// DAT_0066d35a = 0, the KnowledgeMap (FUN_00459830) and InfocardMap (FUN_00458790) singletons,
// NN_MainButton through the WinFactory when bCreateMainButton, and the NN voice init (tail JMP
// FUN_00489de0). Callers: GF's first frame 0x004372fe, the MissionBriefing state 0x0043d434,
// the space init 0x0054ab7c.
void NnSystemInit(bool bCreateMainButton);

// FUN_0045a140 (cdecl) -- NN_MainButton Destroy + delete, the trade window's 0xef02, the
// KnowledgeMap / InfocardMap frees, NN_ArrivalText teardown, the story star reset
// (FUN_004b9bb0) and the NeuroNetMissionMessage teardown (tail JMP FUN_0048a130). Callers: GF
// Destroy 0x004372c8, the MissionBriefing state 0x0043d337 / 0x0043d52d, the space teardown
// 0x0054aff1, the client teardown 0x005b2232.
void NnSystemShutdown();

// DAT_00671ff4 -- the NeuroNetMissionMessage window (ctor 0x0048b20d sets it, dtor 0x0048a46c
// clears it). The class is NeuroNetMissionMessage (nn_mission_message.h).
extern WinControl* G_NnMissionMessage;

// FUN_0048a130 (cdecl) -- with a NeuroNetMissionMessage window: DAT_0067dcc8 = 0 and the
// window's subtree deleted (FUN_0043d2a0 per child, slot 10, deleting dtor).
void NnMissionMessageDestroy();

// DAT_0066d35a -- zeroed by FUN_0045a100, set at 0x004bc1ff (the story star's button
// dispatcher); while set FUN_004b9bb0 keeps the story star's pending list. Read at 0x004b9bb0,
// 0x004b9d90, 0x004b9dd7, 0x004ba46f, 0x004bcc4e -- none ported yet.
extern unsigned char G_bNnStoryStarKeepPending;

// FUN_0045a7a0 (cdecl) -- null when there is no NN_MainButton, else FUN_0045c820(PanelId): the
// icon button of that panel. Caller: NN_Inventory 0x00477ae9.
WinControl* NnMainButtonGetPanelButton(int PanelId);

// The NN_ArrivalText singleton, DAT_0066d398 (written by its ctor, 0x0045b57d, and zeroed by its
// dtor at 0x0045abbd). Class and WINFACTORY in nn_arrival_text.h / nn_modal.cpp.
class NnArrivalText;
extern NnArrivalText* G_NnArrivalText;

// FUN_0045b310 (cdecl) -- make the NN_ArrivalText window through the WinFactory when there is
// none, then FUN_0045aff0(0) on it (the system arrival caption). Caller: the Ship's jump state
// 5 for the player's character (0x00543ad4). The other entry points are in nn_arrival_text.h.
void NnArrivalTextShow();

// FUN_0045a460 -- `if (DAT_0066d3f0) FUN_0045cf60(DAT_0066d3f0)` (NN_MainButton::ClosePanel). Called by the NeuroNet windows'
// Create bodies (NN_Exit 0x00471825, 0x004a6bac, ...) before they build their controls.
void NnMainButtonOnModalOpen();

// FUN_0045a780 (cdecl) -- `if (DAT_0066d3f0) FUN_0045c7a0(DAT_0066d3f0, PanelId, Origin)`:
// NN_MainButton stores the three floats as the origin of panel PanelId (0x63..0x6a map to
// slots 0..6 at +0x444 + 12 * slot). Called by NN_Inventory's dtor (0x00477e4d) and
// 0x00477b33.
void NnMainButtonSetPanelOrigin(int PanelId, float const* Origin);

// FUN_0045a470 -- `DAT_0066d3f0 != 0 && FUN_0045da00(DAT_0066d3f0)`; FUN_0045da00 is
// `[this + 0x3a8] != 0`, the panel the button has open (FUN_0045cf60 switches +0x3ac over the
// NeuroNet panel names).
bool NnMainButtonHasActivePanel();

// FUN_0045a270(Msg, WParam, LParam) -- the mouse message nobody else claimed (FUN_0041ef20 hands
// it WM_LBUTTONDOWN for an unclaimed left press). With the cursor drawn and no NeuroNet screen up:
// in space it goes to the mouse-flight hook FUN_004c80f0; docked, NN_MainButton handles it.
void NnMainButtonOnUnclaimedMouse(unsigned Msg, unsigned WParam, unsigned LParam);

// The dialogs' Create bodies push their layout floats as raw immediates (e.g. PUSH 0x3cf5c28f);
// this turns such an immediate back into the float it encodes, bit for bit.
inline float NnFloatBits(unsigned Bits)
{
    union
    {
        unsigned U;
        float F;
    } Value;
    Value.U = Bits;
    return Value.F;
}
