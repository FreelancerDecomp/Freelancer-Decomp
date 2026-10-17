// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail NN_LoadSave (RTTI .?AVNN_LoadSave@@ : WinControl, Event::Handler;
// E:\FL\Scratch\Source\Client\nn\NN_LoadSave.cpp) -- the NeuroNet load / save game window:
// a list of the saved characters (name and date columns), the info panel of the selected one,
// the description edit box (save mode) and the Save / Load / Delete / Main Menu / Resume buttons.
//
// WINFACTORY<NN_LoadSave> DAT_00671f14 "NN_LoadSave": static init LAB_00485930 (ctor
// FUN_00488ca0, atexit LAB_00485950 -> FUN_00579380), Create FUN_00488d10 (operator_new(0x178c),
// ctor FUN_00485960, slot 9; on failure the scalar deleting dtor).
//
// vftable 0x005d1d5c (45 slots, COL 0x005eb2ac) -- WinControl's 0x005e5e44 with these overrides:
//   0 FUN_00485a40 (scalar deleting dtor around FUN_00485a60), 9 LAB_00485be0 Create,
//   11 FUN_004881f0 Update, 13 FUN_00486c10 Draw, 23 LAB_00486c50, 42 LAB_00486ca0 SetProperty.
// IDataViewOwner face +0x7c vftable 0x005d1d50; Event::Handler face +0x32c vftable 0x005d1d48
// (LAB_00486b80).
//
// Modes (+0x334, the int the factory payload points at): 1 load, 2 save, 3 load with no way
// back (NN_StoryStar 0x004bc1b3 / 0x004bc908 -- Delete, Resume and the close box hidden, and
// USER_CANCEL ignored).
//
// Layout:
//   +0x000 WinControl (0x32c)
//   +0x32c Event::Handler vptr
//   +0x330 software-cursor draw flag saved when the window opened (FUN_004a7670)
//   +0x334 mode
//   +0x338 what the open NN_Confirm asks: 1 delete, 2 main menu, 3 the NN_Error raised for
//          failed saves, 4 overwrite; 0 none
//   +0x33c main menu confirmed -- Update closes the window and posts USER_EXIT_GAME (0xc5)
//   +0x33d save allowed (USER_ENTER 0xf004 saves only when set; Draw sets it once +0x33e is)
//   +0x33e the list has been refilled since the last save (FUN_004877f0's callers set it)
//   +0x340 std::vector<FNnLoadSaveEntry> (VC6: allocator byte, begin +0x344, end +0x348, cap +0x34c)
//   +0x350 std::wstring -- ids 0x65d, the prefix of generated save names (VC6: +0x354 ptr,
//          +0x358 length, +0x35c capacity)
//   +0x360 that prefix's length, +0x364 the number the next generated name gets
//   +0x368 CHARACTER_ID the save overwrites (first byte 0 = a new save)
//   +0x568 wchar_t[0x20] the name the save is made under
//   +0x5a8 LoadSave_Title (TextControl)
//   +0x5ac SaveGame / LoadGame / Delete / MainMenu / ResumeGame (ButtonControl), +0x5c0 their
//          captions (TextControl), +0x5d4 whether each is enabled (SelectSave writes them)
//   +0x5dc ListBox of names, +0xe8c its offset from the window; +0xe98 ListBox of dates,
//          +0x1748 its offset
//   +0x1754 visible rows (0x10 in save mode, else 0x14), +0x1758 LoadSave_Scroll (UIScroll)
//   +0x175c LoadSave_DescripText, +0x1760 LoadSave_DescripButton, +0x1764 LoadSave_Edit2
//          (UITextEdit) -- save mode only, else all three 0
//   +0x1768 / +0x176c / +0x1770 / +0x1774 location / credits / rank / ship titles,
//   +0x1778 / +0x177c / +0x1780 / +0x1784 their values (TextControl)
//   +0x1788 LoadSave_Close (ButtonControl)
//   size 0x178c
// The port keeps retail's member order; the two std containers are the current STL, so offsets
// past +0x340 are retail's in the comments only.

#include "event_handler.h"
#include "list_box.h"
#include "wincontrol_base.h"

#include "common_server_types.h" // CHARACTER_ID
#include "server_characterset.h" // CCharacterSet::CharacterInfo

#include <stddef.h>

#include <string>
#include <vector>

class RichTextEditWin;
class UiScroll;

// One row of the list (0x24 bytes in retail; element dtor FUN_00487c30, which destroys the two
// strings -- the same body folded for every {dword, wstring, wstring} record).
struct FNnLoadSaveEntry
{
    CCharacterSet::CharacterInfo* Info; // +0x00 -- the G_LoginMgr record (FUN_005aaca0)
    std::wstring Time;                  // +0x04 -- filetime_to_wstring(Info +0x228)
    std::wstring Name;                  // +0x14 -- NnLoadSaveEntryText(Info) (FUN_00488b70)
};

enum
{
    kNnLoadSaveModeLoad = 1,
    kNnLoadSaveModeSave = 2,
    kNnLoadSaveModeStoryStar = 3,
};

class NnLoadSave : public WinControl, public EventHandler
{
public:
    // FUN_00485960 (inlined nowhere: FUN_00488d10 calls it).
    NnLoadSave();
    // FUN_00485a60 -- vptrs back; the members and the bases go in reverse order.
    ~NnLoadSave() override;

    // slot 9 LAB_00485be0 (RET 8); Data points at the mode.
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 FUN_004881f0
    void Update() override;
    // slot 13 FUN_00486c10
    void Draw() override;
    // slot 23 LAB_00486c50 (RET 0xc)
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;
    // slot 42 LAB_00486ca0 (RET 0xc)
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // Event::Handler face, LAB_00486b80 (RET 4)
    bool OnEvent(unsigned MsgId) override;

    // FUN_00487550 -- save under the edit box's name (or a generated one); an existing save of
    // that name asks first (NN_Confirm, +0x338 = 4).
    void SaveGame();
    // FUN_004877f0 -- rebuild the list from a fresh character list (FUN_005aa680).
    void RefreshSaveList();
    // FUN_00487cb0 (RET 4) -- show row Index in the info panel and set the buttons; -1 clears.
    void SelectSave(int Index);
    // FUN_004880f0 (RET 4) -- load row Index: queue the character (FUN_005b2c70), close, fade in.
    void LoadSave(int Index);

    unsigned char bCursorDrawFlag;             // +0x330
    unsigned char Pad331[3];                   // +0x331
    int Mode;                                  // +0x334
    int ConfirmMode;                           // +0x338
    unsigned char bExitToMainMenu;             // +0x33c
    unsigned char bSaveAllowed;                // +0x33d
    unsigned char bListRefreshed;              // +0x33e
    unsigned char Pad33f;                      // +0x33f
    unsigned char Byte340;                     // +0x340 -- VC6 vector allocator byte
    std::vector<FNnLoadSaveEntry> Entries;     // retail +0x344..+0x34c
    std::wstring SaveNamePrefix;               // retail +0x350 (allocator byte), +0x354..+0x35c
    int SaveNamePrefixLength;                  // retail +0x360
    int NextSaveNumber;                        // retail +0x364
    CHARACTER_ID OverwriteId;                  // retail +0x368
    wchar_t SaveName[0x20];                    // retail +0x568
    WinControl* Title;                         // retail +0x5a8
    WinControl* Buttons[5];                    // retail +0x5ac
    WinControl* ButtonTexts[5];                // retail +0x5c0
    unsigned char bButtonEnabled[5];           // retail +0x5d4
    unsigned char Pad5d9[3];                   // retail +0x5d9
    ListBox NameList;                          // retail +0x5dc
    float NameListOffset[3];                   // retail +0xe8c
    ListBox TimeList;                          // retail +0xe98
    float TimeListOffset[3];                   // retail +0x1748
    int VisibleRows;                           // retail +0x1754
    UiScroll* Scroll;                          // retail +0x1758
    WinControl* DescripText;                   // retail +0x175c
    WinControl* DescripButton;                 // retail +0x1760
    RichTextEditWin* DescripEdit;              // retail +0x1764
    WinControl* InfoTitles[4];                 // retail +0x1768 location, credits, rank, ship
    WinControl* InfoValues[4];                 // retail +0x1778 location, credits, rank, ship
    WinControl* CloseButton;                   // retail +0x1788

private:
    // The close every path shares (OnEvent 0x00486b93..0x00486c00, Update 0x00488261..
    // 0x004882c2, LoadSave 0x00488149..0x004881b0): FUN_004a7690(+0x330), slot 10 on both lists,
    // slot 33 ReleaseMouse, FUN_0043d2a0 on every child, slot 10, the scalar deleting dtor.
    void ResumeAndDelete();
    // Both lists emptied and unselected (+0xe80 / +0x173c = 0, +0xe88 / +0x1744 = -1), then
    // SelectSave(-1) -- inline at 0x00487211, 0x004872d2 and 0x0048774e.
    void ClearListsAndSelection();
    // 0x2c / 0x2d: the caption of the button A, or null for another sender.
    WinControl* ButtonTextFor(uintptr_t Button) const;
};

static_assert(sizeof(void*) != 4 || offsetof(NnLoadSave, bCursorDrawFlag) == 0x330,
              "NN_LoadSave +0x330 cursor flag");
static_assert(sizeof(void*) != 4 || offsetof(NnLoadSave, Mode) == 0x334, "NN_LoadSave +0x334");
static_assert(sizeof(void*) != 4 || offsetof(NnLoadSave, ConfirmMode) == 0x338,
              "NN_LoadSave +0x338");
static_assert(sizeof(void*) != 4 || offsetof(NnLoadSave, bExitToMainMenu) == 0x33c,
              "NN_LoadSave +0x33c");
static_assert(sizeof(void*) != 4 || offsetof(NnLoadSave, Byte340) == 0x340,
              "NN_LoadSave +0x340");

// FUN_00488970 (cdecl) -- unless an NN_LoadSave exists (FUN_0059d9c0), build one with Mode as
// its payload, SetProperty(0x42, &{0, 0, 0}, 0) and capture the mouse. Callers: NN_Pause
// 0x004a733b (2) / 0x004a73b3 (1), NN_StoryStar 0x004bc1b3 / 0x004bc908 (3).
void OpenNnLoadSave(int Mode);

// FUN_00488b70 (cdecl) -- the text a save is listed under: DescripStrId (+0x224) through
// FUN_004347e0 into G_NnLoadSaveText (FDUMP on a miss, the buffer either way), else the
// Description string (+0x214). Callers: FUN_004877f0 0x00487967, 0x0056b5ee, 0x0056c708,
// 0x0056ccaa.
wchar_t const* NnLoadSaveEntryText(CCharacterSet::CharacterInfo const* Info);

// DAT_00671ce8 -- 0x100 wide characters (up to DAT_00671ee8); the string-resource buffer of
// FUN_00488b70 and SelectSave.
extern wchar_t G_NnLoadSaveText[0x100];
