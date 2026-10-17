// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Intro.cpp (E:\FL\Scratch\Source\Client\Interface\Intro.cpp) -- the main-menu column.
//
// TypeSelect (RTTI .?AUTypeSelect@@ : Win3D, Event::Handler at +0x7c) is the five-button
// column IntroFocus::EnterState state 1 (0x005743f8) builds: New Game / Load Game /
// Multiplayer / Options / Exit. One static instance, DAT_0067b780 (static init LAB_00566180 ->
// ctor FUN_005662a0, atexit LAB_005661a0 -> dtor FUN_005661b0). Earlier port notes called it
// "CIntroScreen"; the RTTI name is TypeSelect.
//
// TextMsgButton (RTTI .?AUTextMsgButton@@ : TextWin, vftable 0x005e2844) is its caption
// button: a TextWin whose click sends parent slot 23 (0x3004, command, 0).
//
// Call path: IntroFocus::Update (0x00573eb0) polls TypeSelect+0x80 (DAT_0067b800) in state 1;
// TypeSelect::Slot23 starts the slide-out animation for commands 3 / 0xa / 0x10, and
// TypeSelect::Update copies the command into +0x80 once the buttons are gone.

#include "event_handler.h"
#include "text_win.h"
#include "win3d.h"

#include <stddef.h>
#include <windows.h>

class TextMsgButton : public TextWin
{
public:
    // FUN_00566380 -- the inlined Win3D + TextWin parts, +0xec = 0, vftable 0x005e2844,
    // +0xe6 = 1.
    TextMsgButton();
    // slot 0 FUN_0059cda0 -> FUN_0059ce40 = JMP FUN_005a1af0 (the TextWin dtor, nothing more)
    ~TextMsgButton() override;

    // slot 15 FUN_005664e0 (RET 4) -- if (+0xe6) FUN_00564700 (the rollover sound).
    void MouseEnter(float const* Ray) override;
    // slot 19 FUN_005664a0 (RET 4) -- Field1 == 2 && +0xe6 && Field0 == -1 && parent:
    // return parent->slot23(0x3004, +0xec, 0); else false.
    bool MouseEvent(FWin3DMouseEvent const* Event) override;

    // FUN_005753d0 (thiscall, RET 0x14) -- +0xec = Command, +0xe6 = 1, slot 9 Create(Parent, 0),
    // +0xb4 = BoxWidth, +0xb8 = BoxHeight, slot 28 SetText(Caption),
    // FUN_005a1cc0("ButtonText", 1.0f).
    void Init(Win3D* Parent, wchar_t const* Caption, float BoxWidth, float BoxHeight,
              int Command);
    // FUN_005753b0 (thiscall, RET 8) -- +0xe3 = 1, +0xe4 = A, +0xe5 = B.
    void SetFlagsE4E5(unsigned char A, unsigned char B);

    int Command; // +0xec -- the slot 23 argument sent on a click
};

static_assert(sizeof(TextMsgButton) == 0xf0, "retail TextMsgButton is 0xf0 bytes");
static_assert(offsetof(TextMsgButton, Command) == 0xec, "TextMsgButton +0xec command");

class TypeSelect : public Win3D, public EventHandler
{
public:
    // FUN_005662a0
    TypeSelect();
    // FUN_005661b0
    ~TypeSelect() override;

    // slot 9 FUN_005668f0 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 10 FUN_00566ab0
    void Destroy() override;
    // slot 11 FUN_00566510
    void Update() override;
    // slot 13 FUN_00566500 -- JMP FUN_0059e5c0
    void Draw() override;
    // slot 23 FUN_00566bd0 (RET 0xc)
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;

    // Event::Handler slot 0 at +0x7c, FUN_00566fc0 (RET 4) -- 0xf -> FUN_00471f30(0), true.
    bool OnEvent(unsigned MsgId) override;

    typedef BOOL(WINAPI* FIsNetworkAliveFn)(LPDWORD Flags);

    int NextState;                    // +0x080 -- DAT_0067b800, polled by IntroFocus state 1
    TextMsgButton Buttons[5];         // +0x084 -- DAT_0067b804 + i * 0xf0
    Win3D* Statics[5];                // +0x534 -- DAT_0067bcb4: the "bImg" StaticControls
    Win3D* Version;                   // +0x548 -- DAT_0067bcc8: the "Version" TextControl
    HMODULE SensApi;                  // +0x54c -- sensapi.dll
    FIsNetworkAliveFn IsNetworkAlive; // +0x550
    unsigned PollIntervalMs;          // +0x554 -- 1000 (Create)
    unsigned LastPollMs;              // +0x558 -- timeGetTime of the last probe
    unsigned Pad55c;                  // +0x55c -- never written
    double AnimTime;                  // +0x560 -- DAT_0067bce0
    int PendingCommand;               // +0x568 -- DAT_0067bce8
    unsigned char bAnimating;         // +0x56c -- DAT_0067bcec
    unsigned char Pad56d[3];
    int Phase;                        // +0x570 -- frames spent hidden after the slide-out

private:
    // The "is the network there" probe shared by Create (0x00566a4f) and Update (0x00566564).
    unsigned char ProbeMultiplayerAvailable();
};

static_assert(offsetof(TypeSelect, NextState) == 0x80, "TypeSelect +0x80 next state");
static_assert(offsetof(TypeSelect, Buttons) == 0x84, "TypeSelect +0x84 buttons");
static_assert(offsetof(TypeSelect, Statics) == 0x534, "TypeSelect +0x534 statics");
static_assert(offsetof(TypeSelect, Version) == 0x548, "TypeSelect +0x548 version");
static_assert(offsetof(TypeSelect, SensApi) == 0x54c, "TypeSelect +0x54c sensapi");
static_assert(offsetof(TypeSelect, AnimTime) == 0x560, "TypeSelect +0x560 anim time");
static_assert(offsetof(TypeSelect, PendingCommand) == 0x568, "TypeSelect +0x568 command");
static_assert(offsetof(TypeSelect, bAnimating) == 0x56c, "TypeSelect +0x56c animating");
static_assert(offsetof(TypeSelect, Phase) == 0x570, "TypeSelect +0x570 phase");
static_assert(sizeof(TypeSelect) == 0x578, "retail TypeSelect is 0x578 bytes");

// DAT_0067b780.
TypeSelect& GetTypeSelect();
