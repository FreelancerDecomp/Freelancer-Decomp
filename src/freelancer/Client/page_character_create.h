// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail CharCreate.cpp PageCharacterCreate (`E:\FL\Scratch\Source\Client\Interface\
// CharCreate.cpp`, .rdata 0x005e1b1c) : UIPage::MasterWin -- the multiplayer "new character"
// page (PAGE_CharacterCreate.page: SelectShip, Back, Title, DataFrame, CharacterName,
// BaseCostume, StartingDesc, NameLabel). One static instance, DAT_0067a890 (static init
// LAB_00573580 -> ctor FUN_0055fea0(&DAT_0067bcf8), atexit LAB_005735a0 -> dtor FUN_005600c0).
// vftables: main 0x005e1a4c (0 FUN_005600a0 scalar deleting dtor, 9 FUN_00560310 Create,
// 10 FUN_005602f0 Destroy, 11 LAB_00561790 Update, 13 FUN_00566500 Draw, 19 LAB_00561730
// MouseEvent, 23 LAB_00561630 Slot23), IUIPage face (+0x7c) 0x005e1a34 (slot 0 LAB_00561bd0 =
// the scalar deleting dtor through `this - 0x7c`), Event::Handler face (+0x80) 0x005e1a2c
// (slot 0 LAB_00561700).
//
// Call path: IntroFocus EnterState case 14 (0x005752a0) releases the active page, loads the
// descriptor through FUN_005602b0 (which calls slot 9 Create(0, 0)) and makes this the active
// page, or moves to state 9 when the file does not load. IntroFocus::Update poll index 6
// (0x0057415d) moves to the state FUN_005602a0 reports once FUN_005602e0 says there is one.
// IntroFocus Exit (0x00573e6d) calls slot 10.
//
// The page shows the pick PageCharacterSelect wrote into DAT_0067bcf8 (G_CharacterCreateSelection):
// the home base, the package's money, ship and description, and the pilot's body on a
// UIWinDeform posed with its body.anim scripts. The typed name goes into the pick's Name and
// CLoginMgr::CreateNewCharacter (FUN_005aa750) sends it.
//
// Retail layout past MasterWin (+0x00..+0x8b) is listed member by member below. The port's
// MasterWin has no retail byte layout (see page_options.h), so the members keep only the order.
// +0xe8..+0xf3 are neither read nor written by CharCreate.cpp and have no absolute reference
// (DAT_0067a978..DAT_0067a983); the port does not hold them.

#include "common_text.h" // RenderDisplayList
#include "ui_page.h"
#include "ui_page_desc.h"

struct SCreateCharacterInfo;
class UiRichTextWin;
class UiTextEdit;
class UiTextMsgButton;
class UiWinDeform;
class UiWinObject;

class PageCharacterCreate : public UIPage::MasterWin
{
public:
    // FUN_0055fea0 (RET 4) -- Selection = &DAT_0067bcf8 (SCreateCharacterInfo, then its faction
    // index at +0x40).
    explicit PageCharacterCreate(SCreateCharacterInfo* NewSelection);
    // FUN_005600c0.
    ~PageCharacterCreate() override;

    // slot 9 FUN_00560310 (RET 8).
    bool Create(Win3D* NewParent, void* Payload) override;
    // slot 10 FUN_005602f0.
    void Destroy() override;
    // slot 11 LAB_00561790.
    void Update() override;
    // slot 13 FUN_00566500 -- JMP FUN_0059e5c0 (Win3D's sorted children).
    void Draw() override;
    // slot 19 LAB_00561730 (RET 4).
    bool MouseEvent(FWin3DMouseEvent const* Event) override;
    // slot 23 LAB_00561630 (RET 0xc).
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;
    // Event::Handler face (+0x80) slot 0 LAB_00561700 (RET 4).
    bool OnEvent(unsigned MsgId) override;

    // FUN_005602a0 -- MOV EAX,[ECX+0xb4].
    int GetResult() const;
    // FUN_005602b0 (RET 4) -- the descriptor loaded from FullPath (FUN_00592230 on +0x8c); on
    // success slot 9 Create(0, 0), whose result is not tested, and true.
    bool Load(char const* FullPath);
    // FUN_005602e0 -- SETNZ on +0xb4.
    bool HasResult() const;

    FUiPageDesc Desc;                    // +0x8c
    int Result;                          // +0xb4 -- the next IntroFocus state (5 / 8)
    SCreateCharacterInfo* Selection;     // +0xb8 -- &DAT_0067bcf8
    int* SelectionFactionIndex;          // +0xbc -- &DAT_0067bd38; no reader in CharCreate.cpp
    RenderDisplayList BackText;          // +0xc0 -- IDS 0x60029, Back's text
    RenderDisplayList Text60028;         // +0xcc -- IDS 0x60028; loaded, never referenced
    double SlideT;                       // +0xd8 -- Create -1.0, 0..2 in; Leave 0, down to -0.9
    unsigned char bLeaving;              // +0xe0
    int LeaveDirection;                  // +0xe4 -- -1 back, 1 created, 2 failed (0 = none)
    float DataFrameHome[3];              // +0xf4 -- DataFrame's position at Create
    UiWinObject* DataFrame;              // +0x100 -- "DataFrame"
    UiWinDeform* BaseCostume;            // +0x104 -- "BaseCostume"
    UiTextMsgButton* Back;               // +0x108 -- "Back" (command 0x300c)
    UiTextMsgButton* SelectShip;         // +0x10c -- "SelectShip" (command 0x300d)
    UiTextEdit* CharacterName;           // +0x110 -- "CharacterName"
    UiRichTextWin* StartingDesc;         // +0x114 -- "StartingDesc"
    UiRichTextWin* Title;                // +0x118 -- "Title"
    UiRichTextWin* NameLabel;            // +0x11c -- "NameLabel"
    // +0x120 -- constructed (0x0055ff5d) and destroyed (0x0056016c) only: no other access to
    // DAT_0067a9b0 or to +0x120 of the page anywhere in the exe (checked 2026-09-30, W4-N3; the
    // page is reached only through DAT_0067a890 and its own methods).
    RenderDisplayList Unused120;
    RenderDisplayList NameLabelText;     // +0x12c -- IDS 0x6006c
    RenderDisplayList BaseLabelText;     // +0x138 -- IDS 0x6006b
    RenderDisplayList ShipLabelText;     // +0x144 -- IDS 0x6006e
    RenderDisplayList Text6006a;         // +0x150 -- IDS 0x6006a; loaded, never referenced
    RenderDisplayList MoneyLabelText;    // +0x15c -- IDS 0x60063
    RenderDisplayList PackageDescText;   // +0x168 -- the package's strid_desc
    RenderDisplayList TitleText;         // +0x174 -- IDS 0x60034

private:
    // FUN_00560c00 (RET 4) -- the name box and StartingDesc from the pick; with bWithCostume the
    // pilot's body on BaseCostume, settled for 0.1 s and posed.
    void ShowSelection(bool bWithCostume);
    // FUN_00561560 -- the typed name into the pick ("higeneko" becomes its hiragana), then
    // CLoginMgr::CreateNewCharacter and the leave.
    void Confirm();
    // FUN_00561b20 -- start the leave slide.
    void BeginLeave();
    // The name read-back Slot23 and Confirm spell inline (0x00561649..0x005616ad,
    // 0x00561563..0x005615c4): GetText into the pick's Name (0x18 chars), and the easter egg.
    void ReadNameIntoSelection();

    PageCharacterCreate(PageCharacterCreate const&) = delete;
    PageCharacterCreate& operator=(PageCharacterCreate const&) = delete;
};

// DAT_0067a890.
PageCharacterCreate& GetPageCharacterCreate();
