// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Intro.cpp PageAccountSelect (RTTI .?AVPageAccountSelect@@ : UIPage::MasterWin) -- the
// multiplayer "enter your CD key" page (PAGE_AccountSelect.page: AccountBoxShort, SelectServer,
// Back, AccountID, Explain). One static instance, DAT_0067aa10 (static init LAB_005726a0 -> ctor
// FUN_005726c0, atexit LAB_00573570 -> dtor FUN_00573470). vftables: main 0x005e2ef4, IUIPage
// face (+0x7c) 0x005e2ed8, Event::Handler face (+0x80) 0x005e2ed0 (slot 0 LAB_005733b0).
//
// Call path: IntroFocus EnterState case 6 (0x00574c1a) loads the page descriptor (+0x8c),
// calls slot 9 Create(0, 0) and makes this the active page (+0x228). IntroFocus::Update poll
// index 2 (0x00574021) moves to the state in +0xb4 (DAT_0067aac4) and clears it. IntroFocus
// Exit calls slot 10 (0x00573e81).
//
// Retail layout past MasterWin (+0x00..+0x8b): +0x8c FUiPageDesc, +0xb4 Result, +0xb8 slide
// parameter (double), +0xc0 leaving, +0xc4 leave direction, +0xd4 the box's home position,
// +0xe0..+0xf0 the five widgets, +0xf4 / +0x100 two RenderDisplayLists. The port's MasterWin
// has no retail byte layout (see page_options.h), so the members below keep only the order.

#include "common_text.h" // RenderDisplayList
#include "ui_page.h"
#include "ui_page_desc.h"

class UiRichTextWin;
class UiTextEdit;
class UiTextMsgButton;

class PageAccountSelect : public UIPage::MasterWin
{
public:
    // FUN_005726c0.
    PageAccountSelect();
    // FUN_00573470.
    ~PageAccountSelect() override;

    // slot 9 FUN_005727f0 (RET 8).
    bool Create(Win3D* NewParent, void* Payload) override;
    // slot 10 FUN_005727b0.
    void Destroy() override;
    // slot 11 LAB_00572cc0 -- the slide in / out, then Win3D's child walk.
    void Update() override;
    // slot 23 FUN_00573010 (RET 0xc).
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;
    // Event::Handler face (+0x80) slot 0 LAB_005733b0 (RET 4).
    bool OnEvent(unsigned MsgId) override;

    // FUN_00573370 (RET 4) -- FUN_00579890(bEnabled) on AccountID, SelectServer and Back.
    void SetToolTipsEnabled(bool bEnabled);

    FUiPageDesc Desc;              // +0x8c -- EnterState case 6 loads it (DAT_0067aa9c)
    int Result;                    // +0xb4 -- DAT_0067aac4: the next IntroFocus state
    double SlideT;                 // +0xb8 -- Create -1.5; 0..2 while in, 0..-0.9 while leaving
    unsigned char bLeaving;        // +0xc0
    int LeaveDirection;            // +0xc4 -- 1 on to ServerSelect (Result 5), -1 back (1)
    float BoxHome[3];              // +0xd4 -- AccountBoxShort's position after Create
    UiTextEdit* AccountId;         // +0xe0 -- "AccountID"
    UiTextMsgButton* SelectServer; // +0xe4 -- "SelectServer"
    UiTextMsgButton* Back;         // +0xe8 -- "Back"
    UiRichTextWin* Explain;        // +0xec -- "Explain"
    Win3D* AccountBox;             // +0xf0 -- "AccountBoxShort" (UIWinObject)
    RenderDisplayList ExplainText; // +0xf4 -- StyleNode 0x9ca4 + IDS 0x60005
    RenderDisplayList Text601eb;   // +0x100 -- StyleNode 0x9ca4 + IDS 0x601eb; no reader here

private:
    // The leave sequence Slot23 and OnEvent spell out inline, identical at all four sites
    // (0x005730fd.., 0x0057322d.., 0x005732dd.., 0x005733bc..): leaving from SlideT 0, both
    // buttons inert and their surfaces frozen, the edit field and the explanation hidden, the
    // slide sound, the three ToolTips off, then the direction.
    void BeginLeave(int Direction);
    // The key check both "SelectServer" (0x3004 / 0x3ea) and 0xf004 spell out: the typed key
    // lower-cased and narrowed into CPlayerAccount::SetTextKey; refused -> the 0x76a error and
    // no direction, accepted -> leave towards ServerSelect. Always true.
    bool SubmitTypedKey();

    PageAccountSelect(PageAccountSelect const&) = delete;
    PageAccountSelect& operator=(PageAccountSelect const&) = delete;
};

// DAT_0067aa10.
PageAccountSelect& GetPageAccountSelect();
