// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail NN_Preferences (RTTI .?AVNN_Preferences@@ : WinControl) -- the options window: key map,
// the three option pages built from interface/optlist.ini, credits, and the in-game close box.
//
// WINFACTORY<NN_Preferences> DAT_006721d0 "NN_Preferences": static init LAB_004a9270 (factory
// ctor FUN_004b28e0, atexit LAB_004a9290 -> FUN_00579380), Create FUN_004b2950
// (operator_new(0x980), WinControl ctor FUN_0059f860, the two vptrs, the nine vectors empty,
// slot 9 with the payload; on failure the scalar deleting dtor).
//
// vftable 0x005d4a5c (one table; the old docs/re/options_screen.md claim of a second one at
// 0x5d4a80 was wrong) -- WinControl's with these overrides:
//   0 FUN_004a95e0 (scalar deleting dtor around FUN_004a9600), 9 FUN_004a9790 Create,
//   11 FUN_004ab6e0 Update, 13 FUN_004abdb0 Draw, 19 FUN_004ada40 MouseEvent,
//   20 FUN_004adac0 KeyEvent, 23 FUN_004ada30 (OutputDebugString "OnMessage\n", false),
//   42 FUN_004ac0a0 SetProperty.
// IDataViewOwner face +0x7c vftable 0x005d4a4c. No Event::Handler face.
//
// Openers: NN_Pause::DoAction action 4 (payload 0 -> in-game) and PageOptions::Create
// (payload 1 -> main menu); PageOptions polls bClosed (+0x32c).
//
// Pages (+0x7f4 CurPage / +0x7f8 ReqPage): 0 key map, 1 "interface" (OptionsA, the video mode
// cycle lives here), 2 "video" (OptionsC), 3 "audio" (OptionsB), 4 credits, 5 close, 6 escape.
//
// PORT: retail std::vector is 16 bytes (allocator byte + three pointers); the port's is not, so
// the layout is asserted only up to +0x504. Retail offsets past it are in the comments.

#include "wincontrol_base.h"

#include <stddef.h>
#include <vector>

class NnButtonControl;
class NnTextControl;
class RichTextWin;
class UiScroll;
class WireButtonControl;
class WireStaticControl;

// One optlist.ini row (0x18 bytes); FUN_004afe30 initialises every int to -1 and every byte to 0.
struct FPrefOption
{
    int Id;                 // +0x00 ids of the caption, also the option's key
    int InfoIds;            // +0x04 the rollover text
    int Default;            // +0x08 -1: none
    int Min;                // +0x0c
    int Max;                // +0x10
    unsigned char bSlider;  // +0x14 slider (else toggle)
    unsigned char Byte15;   // +0x15
    unsigned char bHeader;  // +0x16 caption only
    unsigned char Pad17;
};
static_assert(sizeof(FPrefOption) == 0x18, "retail optlist row is 0x18 bytes");

// One key map row (0x2c bytes) -- two FKeyBindingLookup-shaped triples and the command data.
struct FKeyRowKey
{
    unsigned NicknameId; // +0x00
    int VirtualKey;      // +0x04
    unsigned Modifiers;  // +0x08
};
struct FKeyRow
{
    FKeyRowKey Primary;   // +0x00
    FKeyRowKey Alt;       // +0x0c
    int NameIds;          // +0x18
    int InfoIds;          // +0x1c
    unsigned CommandId;   // +0x20
    int BindingKind;      // +0x24 0 no command, 1 primary only, 2 has an alt, 3 no primary
    unsigned char bEditable; // +0x28
    unsigned char Pad29[3];
};
static_assert(sizeof(FKeyRow) == 0x2c, "retail key row is 0x2c bytes");

// Retail +0x8cc: ten {Width, Height, Bpp} modes (five resolutions at 16 bpp, then at 32).
struct FPrefVideoMode
{
    int Width;
    int Height;
    int Bpp;
};

#ifdef FREELANCER_WIDESCREEN_SUPPORT
// Port-only: the resolutions come from the device instead of retail's fixed five, so the table
// grows. The layout past +0x8cc no longer matches retail under the define.
constexpr int kPrefVideoResolutionCount = 24;
#else
constexpr int kPrefVideoResolutionCount = 5;
#endif
constexpr int kPrefVideoModeCount = kPrefVideoResolutionCount * 2;

class NnPreferences : public WinControl
{
public:
    // Inlined into FUN_004b2950.
    NnPreferences();
    // FUN_004a9600
    ~NnPreferences() override;

    // slot 9 FUN_004a9790 (RET 8); Data == null -> opened in game.
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 FUN_004ab6e0
    void Update() override;
    // slot 13 FUN_004abdb0
    void Draw() override;
    // slot 19 FUN_004ada40 (RET 4)
    bool MouseEvent(FWin3DMouseEvent const* Event) override;
    // slot 20 FUN_004adac0 (RET 8)
    bool KeyEvent(unsigned Key, unsigned char* Modifiers) override;
    // slot 23 FUN_004ada30 (RET 0xc)
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;
    // slot 42 FUN_004ac0a0 (RET 0xc)
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // FUN_004a9380 (cdecl) -- the machine-score seeding of the graphics defaults.
    static bool SeedGraphicsDefaults();

    // FUN_004abdf0 for the file-local helpers that inline its callers' sequences.
    void SetRowsHiddenPublic(bool bHide) { SetRowsHidden(bHide); }

    unsigned char bClosed;          // +0x32c
    unsigned char bInGame;          // +0x32d
    unsigned char bSeedMode;        // +0x32e
    unsigned char Pad32f;
    int SelWidth;                   // +0x330
    unsigned char bSel32;           // +0x334
    unsigned char Pad335[3];
    WinControl* BigBox;             // +0x338 "big box" (main menu only)
    WinControl* Pad33c[2];          // +0x33c
    NnTextControl* Title;           // +0x344 "Screen Title"
    WinControl* CloseButton;        // +0x348 "PreferencesCloseButton" (in game only)
    NnButtonControl* PageButtons[6]; // +0x34c keymap, interface, Video, Audio, Credits, MainMenu
    NnTextControl* PageTexts[6];    // +0x364 the same order
    NnTextControl* CommandRows[13]; // +0x37c "ICommand_Row%d"
    NnTextControl* KeyRows[13];     // +0x3b0 "IKey_Row%d"
    NnTextControl* KeyAltRows[13];  // +0x3e4 "IKey_alt_Row%d"
    WireStaticControl* StaticRows[14]; // +0x418 "Istatics_Row%d"
    WireButtonControl* KeyBtnRows[13];    // +0x450 "IBtn_Row%d"
    WireButtonControl* KeyAltBtnRows[13]; // +0x484 "IBtn_alt_Row%d"
    UiScroll* KeyScroll;            // +0x4b8 "keymap_scroll"
    NnTextControl* KeyColumn1;      // +0x4bc
    NnTextControl* KeyColumn2;      // +0x4c0
    WireButtonControl* GroupButtons[3]; // +0x4c4 "IGroup_Btn_Row%d"
    int Pad4d0;
    NnTextControl* GroupTexts[3];   // +0x4d4 "IGroup_Txt_Row%d"
    int Pad4e0;
    WinControl* SetKeyFrame;        // +0x4e4 "SetKeyFrame"
    NnTextControl* DialogText[3];   // +0x4e8 Text1..3
    WireButtonControl* OverwriteButton; // +0x4f4
    WireButtonControl* DialogCancelButton; // +0x4f8
    NnTextControl* DialogButtonText[2]; // +0x4fc
    std::vector<FPrefOption> OptionsA;  // +0x504 page 1
    std::vector<FPrefOption> UnusedA;   // +0x514
    std::vector<FPrefOption> OptionsB;  // +0x524 page 3
    std::vector<FPrefOption> UnusedB;   // +0x534
    std::vector<FPrefOption> OptionsC;  // +0x544 page 2
    std::vector<FPrefOption> UnusedC;   // +0x554
    UiScroll* VideoModeCycle;       // +0x564 "video_mode_cycle"
    WireStaticControl* VideoBox;    // +0x568
    WireStaticControl* VideoBox2;   // +0x56c
    NnTextControl* VideoModeText;   // +0x570
    NnTextControl* OptTitleText[14];   // +0x574
    NnTextControl* OptText[14];        // +0x5ac
    NnButtonControl* OptToggle[14];    // +0x5e4
    NnTextControl* OptToggleText[14];  // +0x61c
    UiScroll* OptScroll[14];           // +0x654
    NnTextControl* ScrollTextLeft[14]; // +0x68c
    NnTextControl* ScrollTextRight[14]; // +0x6c4
    NnTextControl* SmallText[14];      // +0x6fc
    WireButtonControl* SmallButton[14]; // +0x734
    NnTextControl* SmallButtonText[14]; // +0x76c
    WireStaticControl* StaticBar;   // +0x7a4
    WireButtonControl* CancelButton; // +0x7a8
    NnTextControl* CancelText;      // +0x7ac
    WireButtonControl* DefaultsButton; // +0x7b0
    NnTextControl* DefaultsText;    // +0x7b4
    float Snapshot[14];             // +0x7b8
    RichTextWin* CreditsText;       // +0x7f0
    int CurPage;                    // +0x7f4
    int ReqPage;                    // +0x7f8
    std::vector<FKeyRow> KeyGroups[3]; // +0x7fc / +0x80c / +0x81c
    int GroupNameIds[3];            // +0x82c
    int CurGroup;                   // +0x838
    double Anim;                    // +0x840 (-2.0 from Create)
    unsigned char bDirty;           // +0x848
    unsigned char bCreating;        // +0x849
    unsigned char bConfirmMode;     // +0x84a the "apply changes?" dialog
    unsigned char bPausedGame;      // +0x84b
    unsigned char bKeyCapture;      // +0x84c
    unsigned char bVideoConfirm;    // +0x84d
    FKeyRow CaptureRow;             // +0x850
    unsigned char bCaptureAlt;      // +0x87c
    unsigned char bConflictAlt;     // +0x87d
    unsigned char bConflict;        // +0x87e
    int ConflictRow;                // +0x880
    int ConflictCategory;           // +0x884
    unsigned ConflictCommand;       // +0x888
    double FlipAngle;               // +0x890
    double FlipSpeed;               // +0x898 (360.0)
    unsigned char bFlipping;        // +0x8a0
    unsigned char bFlipHalf;        // +0x8a1
    unsigned char bClosingAnim;     // +0x8a2
    double CreditsScroll;           // +0x8a8
    double CreditsSpeed;            // +0x8b0 (20.0)
    int CurWidth;                   // +0x8b8
    unsigned char bCur32;           // +0x8bc
    double ConfirmCountdown;        // +0x8c0
    unsigned char bApplyingPreset;  // +0x8c8
    FPrefVideoMode Modes[kPrefVideoModeCount];      // +0x8cc
    unsigned char bModeAvail[kPrefVideoModeCount];  // +0x944
    int ModeCount;                                  // +0x950
    int CycleIndex[kPrefVideoModeCount];            // +0x954
    unsigned char bBigVram;         // +0x97c

private:
    // FUN_004abdf0 (RET 4) -- hide / show the current page's row widgets.
    void SetRowsHidden(bool bHide);
    // FUN_004abfb0 -- show the set-key / confirm dialog.
    void ShowKeyDialog();
    // FUN_004ad600 (RET 8) -- try to bind the captured command to (Vk, Mods).
    bool TryBindKey(int Vk, unsigned Mods);
    // FUN_004ade00 (RET 8) -- a copy of KeyGroups[Category] (3: the empty static).
    std::vector<FKeyRow> GetCategoryRows(int Category) const;
    // FUN_004ae030 (RET 4) -- the same for CurGroup.
    std::vector<FKeyRow> GetCurGroupRows() const;
    // FUN_004ae260 (RET 4)
    void SelectGroup(int Group);
    // FUN_004ae4b0 / 4ae650 / 4ae770 / 4ae890 / 4ae9a0
    void EnterKeyMapPage();
    void EnterInterfacePage();
    void EnterVideoPage();
    void EnterAudioPage();
    void EnterCreditsPage();
    // FUN_004aeab0 / 4aebb0 / 4aeca0 / 4aed70 and the inlined credits block.
    void LeaveKeyMapPage();
    void LeaveInterfacePage();
    void LeaveVideoPage();
    void LeaveAudioPage();
    void LeaveCreditsPage();
    // The five-way switch every page change inlines.
    void LeaveCurrentPage();
    // FUN_004aee30
    void GotoRequestedPage();
    // FUN_004af120 (RET 0x10; this unused)
    bool FindVideoMode(struct DisplaySettings* Out, int Width, int Height, int Bpp) const;
    // FUN_004af230 (RET 4)
    void LoadKeyList(char const* Path);
    // FUN_004af5c0 (RET 8)
    void AddKeyRow(int CommandId, int Category);
    // FUN_004af900 (RET 4)
    void FillKeyRows(int First);
    // FUN_004afb80 (RET 8)
    void BeginKeyCapture(int Row, bool bAlt);
    // FUN_004afe30 (RET 4) -- FUN_004afd80 (one option line) is a file-local helper.
    bool LoadOptList(char const* Path);
    // FUN_004b0370 (RET 4)
    void ApplyPercent(int Percent);
    // FUN_004b0530
    void TakeSnapshot();
    // FUN_004b06a0 (RET 4)
    void Revert(bool bApply);
    // FUN_004b0920 (RET 4)
    void RefreshRows(int Id);
    // FUN_004b1450 (RET 0xc)
    void SetSliderOption(int Id, int Value, int Row);
    // FUN_004b1c00 (RET 8)
    bool TryApplyVideoMode(int Width, unsigned char b32);
    // FUN_004b1d20 (RET 0xc)
    void SetToggleOption(int Id, int Value, int Row);
    // FUN_004b1f30 (RET 8)
    void UpdateDirty(int Id, float Value);
    // FUN_004b2130 (RET 4)
    void ShowRowTip(int Row);
    // FUN_004b2440 (RET 4)
    void ProbeModes(int Arg);
    // FUN_004b2540 (RET 4)
    void ShowConfirmDialog(bool bShow);
    // FUN_004b2700
    void CancelChanges();
    // The eight-widget hide + row restore sequence inlined at 0x004ac3d0 / 0x004ac649 /
    // 0x004ac85e / 0x004ac90b / 0x004ad834 / 0x004adb40.
    void CloseKeyDialogRestoreRows();
    // The "+0x848 = 1, cancel enabled, reload keylist, refill" block inlined at 0x004ac6e7 /
    // 0x004ac801 / 0x004acc1d / 0x004ad7ac.
    void MarkKeyMapDirtyAndReload();
};

static_assert(sizeof(void*) != 4 || offsetof(NnPreferences, bClosed) == 0x32c, "NN_Preferences +0x32c");
static_assert(sizeof(void*) != 4 || offsetof(NnPreferences, BigBox) == 0x338, "NN_Preferences +0x338");
static_assert(sizeof(void*) != 4 || offsetof(NnPreferences, PageButtons) == 0x34c, "NN_Preferences +0x34c");
static_assert(sizeof(void*) != 4 || offsetof(NnPreferences, CommandRows) == 0x37c, "NN_Preferences +0x37c");
static_assert(sizeof(void*) != 4 || offsetof(NnPreferences, StaticRows) == 0x418, "NN_Preferences +0x418");
static_assert(sizeof(void*) != 4 || offsetof(NnPreferences, KeyScroll) == 0x4b8, "NN_Preferences +0x4b8");
static_assert(sizeof(void*) != 4 || offsetof(NnPreferences, GroupTexts) == 0x4d4, "NN_Preferences +0x4d4");
static_assert(sizeof(void*) != 4 || offsetof(NnPreferences, DialogButtonText) == 0x4fc, "NN_Preferences +0x4fc");
static_assert(sizeof(void*) != 4 || offsetof(NnPreferences, OptionsA) == 0x504, "NN_Preferences +0x504");
