// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Intro.cpp PageCharacterSelect (RTTI .?AVPageCharacterSelect@@ : UIPage::MasterWin) --
// the multiplayer character list (PAGE_CharacterSelect.page: CharacterTable, CharacterScroll,
// LoadCharacter, NewCharacter, DeleteCharacter, Back, MainMenu, CharacterBox, MOTDBox,
// ServerDetailsBox, Title, ServerDetails, MOTD). One static instance, DAT_0067b180 (static init
// LAB_00567000 -> ctor FUN_00567020, atexit LAB_0056b020 -> dtor FUN_0056aeb0). vftables: main
// 0x005e2934 (0 FUN_0056ae90 scalar deleting dtor, 9 LAB_005674a0 Create, 10 FUN_00567290
// Destroy, 11 LAB_0056a360 Update, 23 FUN_00569670 Slot23), IUIPage face (+0x7c) 0x00575d10,
// Event::Handler face (+0x80) 0x0056a300.
//
// Call path: IntroFocus EnterState case 9 (0x00574f50) loads the descriptor (+0x28c,
// DAT_0067b40c), calls slot 9 Create(0, 0) and makes this the active page. IntroFocus::Update
// poll index 3 (0x0057403c) watches +0x5f8 / +0x5f9 and moves to the state in +0x2b4 with the
// CHARACTER_ID at +0x399. EnterState cases 1 and 5 reset +0x8c from DAT_0067bd40.
//
// The rows are CLoginMgr's character roster (FUN_005aaca0 by index, DAT_0067e96c), sorted with
// qsort by one of ten column comparators (.data:00614b84 descending / 00614b98 ascending); each
// row keeps a CharacterRowData (the character's CHARACTER_ID) as the UIFastTable payload.
//
// Retail layout past MasterWin (+0x00..+0x8b) is listed member by member below. The port's
// MasterWin has no retail byte layout (see page_options.h), so the members keep only the order.

#include "common_text.h"   // RenderDisplayList, TextRenderContext::Image, TextRenderAttributes
#include "server_characterset.h" // CHARACTER_ID, CCharacterSet::CharacterInfo
#include "ui_fast_table.h" // UiFastTableRefCounter, FUiFastTableEdges
#include "ui_page.h"
#include "ui_page_desc.h"

class UiFastTable;
class UiRichTextWin;
class UiScroll;
class UiTextMsgButton;

// DAT_0067bd40 -- a static CHARACTER_ID only its static initializer writes (0x00564af0: the
// first byte 0): the "no character" value the page compares and resets against.
extern CHARACTER_ID G_NoCharacterId;

// DAT_0067bcf8 (0x44 bytes) -- the new character's pick: the SCreateCharacterInfo IServer's
// CreateNewCharacter takes, then the index of the faction record it came from (DAT_0067bd38).
// Writer: PageCharacterSelect's leave toward character creation (0x0056aa89..0x0056aae3).
// PageCharacterCreate (DAT_0067a890, page_character_create.h) gets its address at static init
// (LAB_00573580 -> FUN_0055fea0), shows it and writes the typed name into Info.Name.
struct FCharacterCreateSelection
{
    SCreateCharacterInfo Info; // +0x00 .. +0x3f
    int FactionIndex;          // +0x40 DAT_0067bd38
};
extern FCharacterCreateSelection G_CharacterCreateSelection;

class PageCharacterSelect : public UIPage::MasterWin
{
public:
    // RTTI row record at vftable 0x005e2a48 : UIFastTable::RefCounter (slot 0 the scalar
    // deleting destructor FUN_0056ae70 around FUN_00467ee0). 0x208 bytes: the RefCounter and the
    // character's CHARACTER_ID at +0x08. Built inline by FillRow (0x005695ea..0x0056962f).
    class CharacterRowData : public UiFastTableRefCounter
    {
    public:
        explicit CharacterRowData(CHARACTER_ID const& Source);
        ~CharacterRowData() override;

        CHARACTER_ID Id; // +0x08
    };

    // FUN_00567020.
    PageCharacterSelect();
    // FUN_0056aeb0.
    ~PageCharacterSelect() override;

    // slot 9 LAB_005674a0 (RET 8).
    bool Create(Win3D* NewParent, void* Payload) override;
    // slot 10 FUN_00567290.
    void Destroy() override;
    // slot 11 LAB_0056a360.
    void Update() override;
    // slot 23 FUN_00569670 (RET 0xc).
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;
    // Event::Handler face (+0x80) slot 0 LAB_0056a300 (RET 4).
    bool OnEvent(unsigned MsgId) override;

    CHARACTER_ID CurrentCharacter;           // +0x8c  -- DAT_0067b20c
    FUiPageDesc Desc;                        // +0x28c -- DAT_0067b40c, EnterState case 9 loads it
    int Result;                              // +0x2b4 -- DAT_0067b434: the next IntroFocus state
    UiFastTable* CharacterTable;             // +0x2b8 -- "CharacterTable"
    UiScroll* CharacterScroll;               // +0x2bc -- "CharacterScroll"
    UiTextMsgButton* LoadCharacter;          // +0x2c0 -- "LoadCharacter"
    UiTextMsgButton* NewCharacter;           // +0x2c4 -- "NewCharacter"
    UiTextMsgButton* DeleteCharacter;        // +0x2c8 -- "DeleteCharacter"
    UiTextMsgButton* Back;                   // +0x2cc -- "Back" (DAT_005e1afc)
    UiTextMsgButton* MainMenu;               // +0x2d0 -- "MainMenu"
    Win3D* CharacterBox;                     // +0x2d4 -- "CharacterBox"
    Win3D* MotdBox;                          // +0x2d8 -- "MOTDBox"
    Win3D* ServerDetailsBox;                 // +0x2dc -- "ServerDetailsBox"
    UiRichTextWin* Title;                    // +0x2e0 -- "Title"
    UiRichTextWin* ServerDetails;            // +0x2e4 -- "ServerDetails"
    UiRichTextWin* Motd;                     // +0x2e8 -- "MOTD" (DAT_005e29b4)
    int MotdVersion;                         // +0x2ec -- ChatGetServerMotdVersion at the last copy
    RenderDisplayList ColumnHeaders[5];      // +0x2f0 -- IDS table 0x005e25c8
    RenderDisplayList TitleText;             // +0x32c -- IDS 0x6004e, trailing paragraph cut
    RenderDisplayList Text729;               // +0x338 -- IDS 0x729
    RenderDisplayList DetailsHeader;         // +0x344 -- IDS 0x744
    RenderDisplayList DetailsPassword;       // +0x350 -- IDS 0x750 (server +0x94 set)
    RenderDisplayList DetailsField95;        // +0x35c -- IDS 0x74f (server +0x95 clear)
    RenderDisplayList DetailsRestricted;     // +0x368 -- IDS 0x6007c, trailing paragraph cut
    RenderDisplayList MotdHeader;            // +0x374 -- IDS 0x60007, trailing paragraph cut
    // +0x380 (5 bytes): the constructor writes flags 0 and colour 0x52eaf5 (0x005671bd..
    // 0x005671dd); nothing in the page reads it. TODO: its reader is not identified.
    TextRenderAttributes Field_0x380;
    TextRenderContext::Image* UpPip;         // +0x388 -- interface\intro\images\uppip.bmp
    TextRenderContext::Image* DownPip;       // +0x38c -- DownPip.bmp
    unsigned SortColumn;                     // +0x390 -- 0
    unsigned char bAscending[5];             // +0x394 -- constructor 0, Create 1
    CHARACTER_ID SelectedCharacter;          // +0x399 -- DAT_0067b519
    unsigned char bCharacterChosen;          // +0x599 -- set once the load sequence picked one
    int FirstVisible;                        // +0x59c -- ftol(scroll value) at the last rebuild
    FUiFastTableEdges Edges;                 // +0x5a0 -- HKCU\...\CharacterTable "Columns"/"Rows"
    float CharacterBoxHome[3];               // +0x5c0 -- CharacterBox's position after Create
    float MotdBoxHome[3];                    // +0x5cc
    float DetailsBoxHome[3];                 // +0x5d8
    double SlideT;                           // +0x5e8 -- Create -1.0; 0..2 in, 0..-0.9 leaving
    unsigned char bLeaving;                  // +0x5f0
    int LeaveDirection;                      // +0x5f4 -- 1 load, 2 new, -1 main menu, -2 back
    unsigned char bLoadRequested;            // +0x5f8 -- DAT_0067b778: IntroFocus poll 3 reads it
    unsigned char bIntroBaseLeft;            // +0x5f9 -- DAT_0067b779: IntroFocus poll 3 sets it

private:
    // FUN_00567350 -- the table's edges into HKCU\Software\Microsoft\Microsoft Games\
    // Freelancer\1.0\CharacterTable (FUN_0058c370); true when saved.
    bool SaveEdges();
    // FUN_00568880 (RET 0x14) -- Content into the cell (Row, Column) behind a style (the header
    // row: the sort column's, with its sort arrow after the content) and a justification.
    void SetCell(unsigned Row, unsigned Column, RenderDisplayList const* Content, bool bEnabled,
                 bool bSelected);
    // FUN_00568d10 -- the sorted roster into the visible rows, the rest emptied.
    void RebuildTable();
    // FUN_00569170 (RET 8) -- one character into the five cells of Row and the row payload.
    void FillRow(unsigned Row, CCharacterSet::CharacterInfo const* Character);
    // FUN_0056a150 -- the tool tips of the five buttons, ServerDetails and MOTD off.
    void DisableToolTips();
    // FUN_0056a1b0 (RET 4) -- every cell of Row emptied, its payload released.
    void ClearRow(unsigned Row);
    // FUN_0056a230 -- Load / Delete live only with a selection, New only under five characters.
    void UpdateButtons();
    // FUN_0056abb0 -- MOTD re-filled when the server's message of the day changed.
    void RefreshMotd();
    // FUN_0056ad40 -- the leave sequence (no direction); Slot23 spells it inline four times.
    void BeginLeave();

    PageCharacterSelect(PageCharacterSelect const&) = delete;
    PageCharacterSelect& operator=(PageCharacterSelect const&) = delete;
};

// DAT_0067b180.
PageCharacterSelect& GetPageCharacterSelect();
