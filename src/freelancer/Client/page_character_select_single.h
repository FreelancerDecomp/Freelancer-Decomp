// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Intro.cpp PageCharacterSelectSingle (RTTI .?AVPageCharacterSelectSingle@@ :
// UIPage::MasterWin, COL 0x005f0220) -- the single-player "Load Game" list
// (PAGE_CharSelectSingle.page: LoadCharacter, DeleteCharacter, MainMenu, the four
// "...Label" / "Character..." UIRichTextWins, CharScroll, the two UIListBoxes CharSelDesc and
// CharSelTime, ScreenTitle). One static instance, DAT_0067ae10 (static init LAB_0056b030 ->
// ctor FUN_0056b050, atexit LAB_0056d4e0 -> dtor FUN_0056d360). vftables: main 0x005e2a84
// (0 FUN_0056d340 scalar deleting dtor, 9 LAB_0056b330 Create, 10 LAB_0056b240 Destroy,
// 11 FUN_0056ce30 Update, 23 LAB_0056c360 Slot23), IUIPage face (+0x7c) 0x00575ce0,
// Event::Handler face (+0x80) 0x0056d310.
//
// CharSelDesc / CharSelTime are UIListBox (the page file's type; WINFACTORY<UIListBox>
// FUN_0059c810 builds ListBox FUN_005a2100 + vftable 0x005e5cb4, COL .?AUUIListBox@@), so the
// page's +0x88c / +0x8a0 / +0x8a1 / +0x8a2 / +0x8a4 / +0x8a8 / +0x8ac writes and FUN_005a1f50 /
// FUN_005a1f20 calls are ListBox's FirstVisible / bDrawFrame / bDrawRowRules / bSelectable /
// ItemCount / VisibleRows / SelectedIndex / AddItem / GetItemData (list_box.h).
//
// Call path: IntroFocus EnterState case 18 (0x00574fbe) builds the five StaticControls under
// the page (+0xf4..+0x104), loads the descriptor (+0x8c) and calls slot 9 Create(0, 0).
// IntroFocus::Update poll index 8 (0x005740cd) watches +0x368 / +0x369 and moves to the state in
// +0xb4 with the CHARACTER_ID at +0x118. IntroFocus Exit (0x00573d51..0x00573e67) inlines
// slot 10.
//
// Retail layout past MasterWin (+0x00..+0x8b) is listed member by member below. The port's
// MasterWin has no retail byte layout (see page_options.h), so the members keep only the order.

#include "common_text.h"         // RenderDisplayList
#include "server_characterset.h" // CHARACTER_ID, CCharacterSet::CharacterInfo
#include "ui_page.h"
#include "ui_page_desc.h"

#include <string>
#include <vector>

class UiListBox;
class UiRichTextWin;
class UiScroll;
class UiTextMsgButton;

// One row of the page's list (0x24 bytes in retail; constructor FUN_0056d4b0 -- the pointer
// and two empty strings -- destructor FUN_00487c30, the {dword, wstring, wstring} body folded for
// every such record).
struct FCharSelectSingleEntry
{
    CCharacterSet::CharacterInfo* Info; // +0x00 -- the G_LoginMgr record (FUN_005aaca0)
    std::wstring Name;                  // +0x04 -- NnLoadSaveEntryText(Info) (FUN_00488b70)
    std::wstring Time;                  // +0x14 -- filetime_to_wstring(Info +0x228)
};

class PageCharacterSelectSingle : public UIPage::MasterWin
{
public:
    // FUN_0056b050.
    PageCharacterSelectSingle();
    // FUN_0056d360.
    ~PageCharacterSelectSingle() override;

    // slot 9 LAB_0056b330 (RET 8).
    bool Create(Win3D* NewParent, void* Payload) override;
    // slot 10 LAB_0056b240.
    void Destroy() override;
    // slot 11 FUN_0056ce30.
    void Update() override;
    // slot 23 LAB_0056c360 (RET 0xc).
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;
    // Event::Handler face (+0x80) slot 0 LAB_0056d310 (RET 4).
    bool OnEvent(unsigned MsgId) override;

    FUiPageDesc Desc;                     // +0x8c  -- DAT_0067ae9c, EnterState case 18 loads it
    int Result;                           // +0xb4  -- DAT_0067aec4: the next IntroFocus state
    UiRichTextWin* CharacterMoney;        // +0xb8  -- "CharacterMoney"
    UiRichTextWin* CharacterRank;         // +0xbc  -- "CharacterRank"
    UiRichTextWin* CharacterLocation;     // +0xc0  -- "CharacterLocation"
    UiRichTextWin* CharacterShip;         // +0xc4  -- "CharacterShip"
    UiTextMsgButton* LoadCharacter;       // +0xc8  -- "LoadCharacter" (command 0x3003)
    UiTextMsgButton* DeleteCharacter;     // +0xcc  -- "DeleteCharacter" (command 0x3002)
    UiTextMsgButton* MainMenu;            // +0xd0  -- "MainMenu" (command 0x300c)
    UiScroll* CharScroll;                 // +0xd4  -- "CharScroll"
    UiListBox* CharSelDesc;               // +0xd8  -- "CharSelDesc"
    UiListBox* CharSelTime;               // +0xdc  -- "CharSelTime"
    UiRichTextWin* MoneyLabel;            // +0xe0  -- "MoneyLabel"
    UiRichTextWin* RankLabel;             // +0xe4  -- "RankLabel"
    UiRichTextWin* LocationLabel;         // +0xe8  -- "LocationLabel"
    UiRichTextWin* ShipLabel;             // +0xec  -- "ShipLabel"
    UiRichTextWin* ScreenTitle;           // +0xf0  -- "ScreenTitle"
    // +0xf4..+0x104 -- the five StaticControls EnterState case 18 builds under the page
    // (DAT_0067af04..DAT_0067af14): three "bImg" FRONT_button.cmp, "bigBox" and "smallBox".
    // The constructor does not write them (the static object starts zeroed).
    Win3D* ButtonImages[3];               // +0xf4
    Win3D* BigBox;                        // +0x100
    Win3D* SmallBox;                      // +0x104
    double SlideT;                        // +0x108 -- ctor 1.0, Create -2.0; 0..2 in, 0..-0.5 out
    unsigned char bLeaving;               // +0x110
    int LeaveDirection;                   // +0x114 -- ctor -1; 1 load, 0 main menu
    CHARACTER_ID SelectedCharacter;       // +0x118 -- DAT_0067af28: poll 8 hands it on
    unsigned char bDeletePending;         // +0x318 -- the delete confirm box is open
    unsigned char bCharacterChosen;       // +0x319 -- the load sequence picked one
    RenderDisplayList MoneyLabelText;     // +0x31c -- IDS 0x594
    RenderDisplayList RankLabelText;      // +0x328 -- IDS 0x5ec
    RenderDisplayList LocationLabelText;  // +0x334 -- IDS 0x593
    RenderDisplayList ShipLabelText;      // +0x340 -- IDS 0x591
    RenderDisplayList TitleText;          // +0x34c -- IDS 0x4f5
    std::vector<FCharSelectSingleEntry> Entries; // +0x358
    unsigned char bLoadRequested;         // +0x368 -- DAT_0067b178: IntroFocus poll 8 reads it
    unsigned char bIntroBaseLeft;         // +0x369 -- DAT_0067b179: IntroFocus poll 8 sets it

private:
    // FUN_0056bd10 -- the scroll bar only when CharSelDesc holds more than 20 rows.
    void UpdateScrollVisibility();
    // FUN_0056bd40 (RET 4) -- the four value windows from Info, or "-" with Load / Delete off.
    void ShowDetails(CCharacterSet::CharacterInfo const* Info);
    // FUN_0056cb40 -- the roster (all but the restart save) into Entries and both lists.
    void RefillLists();
    // The leave sequence Slot23 spells inline three times (0x0056c3bb..0x0056c4a7,
    // 0x0056c732..0x0056c824, 0x0056c9b6..0x0056caa2, then 0x0056caac..0x0056cac1).
    void BeginLeave(int Direction);

    PageCharacterSelectSingle(PageCharacterSelectSingle const&) = delete;
    PageCharacterSelectSingle& operator=(PageCharacterSelectSingle const&) = delete;
};

// DAT_0067ae10.
PageCharacterSelectSingle& GetPageCharacterSelectSingle();
