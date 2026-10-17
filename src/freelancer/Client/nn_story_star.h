// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail NN_StoryStar.cpp ("E:\FL\Scratch\Source\Client\nn\NN_StoryStar.cpp", 0x004b9890..
// 0x004bd5d0): the mission log and the mission-objective banner.
//
// The data model is two VC6 std::list<FmtStr> (0x20-byte nodes {next, prev, FmtStr}):
//   the HISTORY  (alloc byte DAT_00672330, head DAT_00672334, size DAT_00672338) -- the log the
//                NeuroNetStoryStar window lists; entries whose text starts with '*' are log
//                lines, the others mission objectives (static init LAB_004b9950, atexit
//                LAB_004b9990);
//   the PENDING  (DAT_00672324 / DAT_00672328 / DAT_0067232c) -- objectives waiting to be shown
//                by the MissionObjectiveWindow banner (LAB_004b9a00 / LAB_004b9a40).
// Server messages reach it through IClient slot 70 (FUN_004b9c60) and slot 97 (FUN_004ba430);
// NN_MainButton's update ticks it (FUN_004bc990).
//
// Four WinFactory classes live here:
//   NeuroNetStoryStar          WINFACTORY DAT_006723c0 (LAB_004b9890 -> FUN_004bcfc0), Create
//                              FUN_004bd030: operator_new(0x390); vftable 0x005d5b74; instance
//                              DAT_006723fc. NN_MainButton's mission-log panel (0x67) makes it.
//   MissionObjectiveWindow     WINFACTORY DAT_00672394 (LAB_004b98c0 -> FUN_004bd100), Create
//                              FUN_004bd170: operator_new(0x340); vftable 0x005d5a8c; instance
//                              DAT_00672400. The fading objective banner.
//   MissionLogMoveToNNWindow   WINFACTORY DAT_00672368 (LAB_004b98f0 -> FUN_004bd250), Create
//                              FUN_004bd2c0: operator_new(0x350); vftable 0x005d5dd4,
//                              ButtonMover face 0x005d5e8c. The log icon flying to the button.
//   MissionLogMoveFromNNWindow WINFACTORY DAT_0067233c (LAB_004b9920 -> FUN_004bd380), Create
//                              FUN_004bd3f0: operator_new(0x350); vftable 0x005d5eac,
//                              ButtonMover face 0x005d5f64. RTTI: derives from the ToNN class.

#include "button_mover.h"
#include "wincontrol_base.h"

#include "common_text.h"

#include <list>
#include <stddef.h>

class NnTextControl;
class RichTextWin;

// ---------------------------------------------------------------------------------------------
// MissionObjectiveWindow (RTTI .?AVMissionObjectiveWindow@@ : WinControl), 0x340 bytes.
//
// vftable 0x005d5a8c -- WinControl's 0x005e5e44 with four overrides:
//   0 FUN_004bb1c0 (scalar deleting dtor around FUN_004bb1e0), 9 FUN_004bb270 Create,
//   11 LAB_004bb390 Update, 13 FUN_004bb680 Draw.
class MissionObjectiveWindow : public WinControl
{
public:
    // Inlined into FUN_004bd170 (0x004bd1a8..0x004bd1e6): WinControl ctor, the vptrs,
    // DAT_00672400 = this, +0x32c = 0, +0x331 = 1, +0x332 = 0xff, +0x334 = 2.5f, +0x330 = 0,
    // +0x338 = 0.
    MissionObjectiveWindow();
    // FUN_004bb1e0
    ~MissionObjectiveWindow() override;

    // slot 9 FUN_004bb270 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 LAB_004bb390
    void Update() override;
    // slot 13 FUN_004bb680
    void Draw() override;

    // FUN_004bb6b0 (thiscall) -- the first pending objective into the text (property 0x1d),
    // new lines dropped; one other than ids 0x762 also becomes the "last objective".
    void ShowFrontObjective();

    float Elapsed;                  // +0x32c -- time in the current phase
    unsigned char bHolding;         // +0x330 -- fully shown, counting HoldTime down
    unsigned char bFadingIn;        // +0x331 -- ctor 1
    unsigned char Alpha;            // +0x332 -- ctor 0xff; the last alpha sent to the text
    unsigned char Pad333;
    float HoldTime;                 // +0x334 -- ctor 2.5f
    // +0x338 -- set when another NeuroNet window cut the banner short; the pending objective
    // then stays queued instead of being popped when the banner goes.
    unsigned char bInterrupted;
    unsigned char Pad339[3];
    NnTextControl* Text;            // +0x33c -- "MissionObjectiveText"

private:
    MissionObjectiveWindow(MissionObjectiveWindow const&) = delete;
    MissionObjectiveWindow& operator=(MissionObjectiveWindow const&) = delete;
};

static_assert(sizeof(void*) != 4 || sizeof(MissionObjectiveWindow) == 0x340,
              "retail MissionObjectiveWindow is 0x340 bytes (0x004bd189)");
static_assert(sizeof(void*) != 4 || offsetof(MissionObjectiveWindow, Elapsed) == 0x32c,
              "MissionObjectiveWindow +0x32c");
static_assert(sizeof(void*) != 4 || offsetof(MissionObjectiveWindow, HoldTime) == 0x334,
              "MissionObjectiveWindow +0x334");
static_assert(sizeof(void*) != 4 || offsetof(MissionObjectiveWindow, Text) == 0x33c,
              "MissionObjectiveWindow +0x33c");

// ---------------------------------------------------------------------------------------------
// NeuroNetStoryStar (RTTI .?AVNeuroNetStoryStar@@ : WinControl), 0x390 bytes.
//
// vftable 0x005d5b74 -- WinControl's with six overrides:
//   0 FUN_004bb760 (scalar deleting dtor around FUN_004bb780), 9 FUN_004bb810 Create,
//   11 FUN_004bc970 Update, 13 FUN_004bc940 Draw, 23 LAB_004bc090 Slot23,
//   42 LAB_004bc110 SetProperty.
//
// Two layouts, chosen in Create by NN_MainButton's force-mission-log flag (+0x52f): normally a
// close button and, in single player, the Mission / Log toggles over the history text; when
// the flag is up (the player died) the load-game / main-menu buttons (single player) or respawn
// / new player / new server (multiplayer) instead, with the game paused.
class NeuroNetStoryStar : public WinControl
{
public:
    // Inlined into FUN_004bd030 (0x004bd068..0x004bd09b): WinControl ctor, the vptrs,
    // DAT_006723fc = this, +0x338 = 0, +0x37c = 1, +0x36c = 0, +0x374 = 0, +0x38c = 0.
    NeuroNetStoryStar();
    // FUN_004bb780
    ~NeuroNetStoryStar() override;

    // slot 9 FUN_004bb810 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 FUN_004bc970
    void Update() override;
    // slot 13 FUN_004bc940
    void Draw() override;
    // slot 23 LAB_004bc090 (RET 0xc) -- 0xef01 show / 0xef02 tear down
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;
    // slot 42 LAB_004bc110 (RET 0xc) -- 0x26, a button was clicked
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // FUN_004bbfb0 (RET 4) -- switch the history text between the objectives (true) and the log
    // (false), keeping each one's scroll position.
    void SetMissionMode(bool bMission);
    // FUN_004bc2a0 -- rebuild the history text from the history list.
    void RebuildHistoryText();
    // FUN_004bc830 -- "Load Autosave": reload the auto-save character.
    void LoadAutosave();

    int Field_0x32c;                // +0x32c -- TODO: never written or read by this class
    WinControl* CloseButton;        // +0x330 "StoryStarCloseButton"
    RichTextWin* HistoryText;       // +0x334 "StoryStarHistoryText"
    int HistoryShownCount;          // +0x338 -- DAT_00672338 when the text was built
    WinControl* LoadAutosaveButton; // +0x33c
    WinControl* LoadSavegameButton; // +0x340
    WinControl* MainMenuButton;     // +0x344
    WinControl* LoadAutosaveText;   // +0x348
    WinControl* LoadSavegameText;   // +0x34c
    WinControl* MainMenuText;       // +0x350
    WinControl* RespawnButton;      // +0x354
    WinControl* NewPlayerButton;    // +0x358
    WinControl* NewServerButton;    // +0x35c
    WinControl* RespawnText;        // +0x360
    WinControl* NewPlayerText;      // +0x364
    WinControl* NewServerText;      // +0x368
    WinControl* MissionButton;      // +0x36c
    WinControl* MissionText;        // +0x370
    WinControl* LogButton;          // +0x374
    WinControl* LogText;            // +0x378
    unsigned char bMissionMode;     // +0x37c -- ctor 1
    unsigned char SavedCursorDrawFlag; // +0x37d -- FUN_004a7670's result
    unsigned char Pad37e[2];
    int MissionScrollY;             // +0x380
    int LogScrollY;                 // +0x384
    int PendingScrollY;             // +0x388 -- applied by the next Draw, then -1
    unsigned char bPausedGame;      // +0x38c -- Create paused the game (FUN_004a7670)
    unsigned char Pad38d[3];

private:
    NeuroNetStoryStar(NeuroNetStoryStar const&) = delete;
    NeuroNetStoryStar& operator=(NeuroNetStoryStar const&) = delete;
};

static_assert(sizeof(void*) != 4 || sizeof(NeuroNetStoryStar) == 0x390,
              "retail NeuroNetStoryStar is 0x390 bytes (0x004bd049)");
static_assert(sizeof(void*) != 4 || offsetof(NeuroNetStoryStar, HistoryText) == 0x334,
              "NeuroNetStoryStar +0x334");
static_assert(sizeof(void*) != 4 || offsetof(NeuroNetStoryStar, MissionButton) == 0x36c,
              "NeuroNetStoryStar +0x36c");
static_assert(sizeof(void*) != 4 || offsetof(NeuroNetStoryStar, bMissionMode) == 0x37c,
              "NeuroNetStoryStar +0x37c");
static_assert(sizeof(void*) != 4 || offsetof(NeuroNetStoryStar, PendingScrollY) == 0x388,
              "NeuroNetStoryStar +0x388");
static_assert(sizeof(void*) != 4 || offsetof(NeuroNetStoryStar, bPausedGame) == 0x38c,
              "NeuroNetStoryStar +0x38c");

// ---------------------------------------------------------------------------------------------
// MissionLogMoveToNNWindow (RTTI .?AVMissionLogMoveToNNWindow@@ : WinControl, ButtonMover at
// +0x32c), 0x350 bytes. The story-star icon ("DummyMissionLog") sliding from the middle of the
// screen to the mission-log button; it deletes itself once the slide ends.
//
// vftable 0x005d5dd4: 0 FUN_004bd4f0 (scalar deleting dtor around FUN_004bd510),
// 9 FUN_004bcd30 Create, 11 FUN_004bce50 Update. The ButtonMover face 0x005d5e8c keeps
// ButtonMover's two slots (LAB_00401e90 / LAB_0043d8a0).
class MissionLogMoveToNNWindow : public WinControl, public ButtonMover
{
public:
    // Inlined into FUN_004bd2c0 (0x004bd2f9..0x004bd31f): WinControl ctor, ButtonMover ctor
    // FUN_0055dec0 at +0x32c, the vptrs.
    MissionLogMoveToNNWindow();
    // FUN_004bd510 -- the mover's list freed, then the WinControl dtor.
    ~MissionLogMoveToNNWindow() override;

    // slot 9 FUN_004bcd30 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 FUN_004bce50 -- shared with MissionLogMoveFromNNWindow
    void Update() override;

    WinControl* Icon;               // +0x34c "DummyMissionLog"

private:
    MissionLogMoveToNNWindow(MissionLogMoveToNNWindow const&) = delete;
    MissionLogMoveToNNWindow& operator=(MissionLogMoveToNNWindow const&) = delete;
};

// MissionLogMoveFromNNWindow (RTTI .?AVMissionLogMoveFromNNWindow@@ :
// MissionLogMoveToNNWindow), 0x350 bytes. The same icon sliding back out of the button.
// vftable 0x005d5eac: 0 FUN_004bd4f0, 9 FUN_004bceb0 Create, 11 FUN_004bce50 Update.
class MissionLogMoveFromNNWindow : public MissionLogMoveToNNWindow
{
public:
    // Inlined into FUN_004bd3f0 (0x004bd429..0x004bd44f).
    MissionLogMoveFromNNWindow();

    // slot 9 FUN_004bceb0 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
};

// ---------------------------------------------------------------------------------------------
// The data model.

// DAT_00672330..0x00672338 -- the history (front = newest).
extern std::list<FmtStr> G_StoryStarHistory;
// DAT_00672324..0x0067232c -- the objectives waiting for the banner (front = shown next).
extern std::list<FmtStr> G_StoryStarPending;
// DAT_0067230c -- the last objective the banner showed (or the deferred 0x549c one); static
// init LAB_004b9ab0, atexit LAB_004b9b10 (~FmtStr).
extern FmtStr G_StoryStarLastObjective;
// DAT_00672404 -- G_StoryStarLastObjective holds something the button can re-show.
extern unsigned char G_bStoryStarHasLastObjective;
// DAT_00672400 -- the MissionObjectiveWindow (ctor sets, dtor FUN_004bb1e0 clears).
extern MissionObjectiveWindow* G_MissionObjectiveWindow;
// DAT_006723fc -- the NeuroNetStoryStar (ctor sets, dtor FUN_004bb780 clears).
extern NeuroNetStoryStar* G_NeuroNetStoryStar;
// DAT_00672408 -- seconds with nothing to show (FUN_004bc990); the banner's Create compares it
// with 0.5 to decide whether the log icon flies back out.
extern float G_StoryStarIdleTime;
// DAT_0067240c -- a rank reached in single player (IClient slot 117) not yet announced.
extern int G_StoryStarRankUp;

// FUN_004b9b20 (cdecl) -- G_StoryStarRankUp = Rank. Callers: IClient slot 117 in single player
// (0x0053e23f), and 0 from the connect-list resets (0x004c4baa, 0x004c4cab).
void StoryStarSetRankUp(int Rank);
// FUN_004b9b30 (cdecl) -- G_bStoryStarHasLastObjective = 0. Caller: the base load 0x00439b11.
void StoryStarForgetLastObjective();
// FUN_004b9b40 (cdecl) -- with a last objective, nothing pending and no banner: queue it again.
// Caller: NN_MainButton's event 0x6e (0x0045cb39).
void StoryStarRequeueLastObjective();
// FUN_004b9bb0 (cdecl) -- unless DAT_0066d35a, drop the pending objectives and the last
// objective flag; then tear the banner down. Callers: 0x0043d33c, 0x0043d3ee, 0x0043d40a,
// 0x0043d532 (MissionBriefing), 0x0045a189 (NN shutdown), 0x0049f71b (nav map abort),
// 0x004c4ba3, 0x004c4ca4 (connect-list resets).
void StoryStarReset();
// FUN_004b9c60 (cdecl) -- a mission message of kind Type (IClient slot 70, 0x005a5ba9; the
// lost-ship path 0x00471efe).
void StoryStarOnMissionMessage(FmtStr const* Text, int Type);
// FUN_004b9f90 (cdecl) -- queue an objective for the banner.
void StoryStarPushPending(FmtStr const* Text);
// FUN_004ba0f0 (cdecl) -- keep only the log lines of the history, re-send it to the server in
// single player, and drop the pending objectives.
void StoryStarPruneHistory();
// FUN_004ba2a0 (cdecl) -- add an entry to the front of the history (single player: send it).
void StoryStarAddHistory(FmtStr const* Text);
// FUN_004ba430 (cdecl) -- replace the history with the server's packed log (IClient slot 97,
// 0x0053c43a): +0x04 the record count, the FmtStr records from +0x08, Size bytes in all.
void StoryStarLoadHistory(unsigned char* Data, int Size);
// FUN_004ba5a0 (cdecl) -- turn the game ids in Text's values into ids-string ids in place.
void StoryStarResolveIds(FmtStr* Text);
// FUN_004bc990 (cdecl) -- the per-frame driver: show the banner for the next pending objective
// (or the rank-up) when no other NeuroNet window is in the way. Caller: NN_MainButton::Update
// (0x0045c949).
void StoryStarTick();
