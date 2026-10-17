// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail WinFactory / WINFACTORY<T> (RTTI .?AUWinFactory@@, .?AU?$WINFACTORY@U<T>@@@@).
//
// Every WINFACTORY<T> is a .data global built by a static initializer (e.g. LAB_0059c1e0:
// FUN_0059c360(&DAT_0067db70, "UIStaticText") + atexit(LAB_0059c200)). Layout:
//   +0x00 vftable (base 0x005cb554 = {_purecall}; one slot: Create)
//   +0x04 next factory                  — list head DAT_0067dcec (FUN_0059dab0 push-front)
//   +0x08 class-name length (<= 31)
//   +0x0c char[32] class name           — the PAGE_*.txt type token and the lookup key
// The ctor copies the name, registers the factory, then installs the derived vftable.
// The atexit dtor FUN_00579380 restores the base vftable and unlinks it (FUN_0059dad0).
//
// Slot 0 (e.g. FUN_0059c3d0, WinControl's FUN_005a1a20): operator_new(sizeof T), T ctor,
// widget+0x10 = &factory+0xc, Win3D slot 9 Create(Parent, Payload); false -> scalar deleting
// dtor, return 0. Everything here is typed on Win3D, the class every factory product derives
// from.
//
// (The port-only flat FMenuUiWidget registry that used to exist beside it was removed
// 2026-10-07.)

#include "win3d.h"

class WinFactory
{
public:
    // Inline head of every WINFACTORY<T> ctor (e.g. FUN_0059c360 00059c360..0059c3cd):
    // next=0, name copy clamped to 31, length, then FUN_0059dab0(this).
    explicit WinFactory(char const* Name);
    // FUN_00579380 — vftable back to WinFactory, FUN_0059dad0(this). Not virtual: retail's
    // WinFactory vftable has the single Create slot.
    ~WinFactory();

    // slot 0 (+0x00)
    virtual Win3D* Create(Win3D* Parent, void* Payload) = 0;

    WinFactory* Next;        // +0x04
    unsigned NameLength;     // +0x08
    char ClassName[32];      // +0x0c

private:
    WinFactory(WinFactory const&) = delete;
    WinFactory& operator=(WinFactory const&) = delete;
};

template <class T>
class WINFACTORY : public WinFactory
{
public:
    explicit WINFACTORY(char const* Name)
        : WinFactory(Name)
    {
    }

    Win3D* Create(Win3D* Parent, void* Payload) override
    {
        T* Widget = new T();
        Widget->ClassName = ClassName;
        if (!Widget->Create(Parent, Payload))
        {
            delete Widget;
            return nullptr;
        }
        return Widget;
    }
};

// FUN_0059dab0 — *(factory+4) = head; head = factory.
void WinFactoryRegister(WinFactory* Factory);

// FUN_0059dad0 — unlink factory from the list, factory+4 = 0.
void WinFactoryUnregister(WinFactory* Factory);

// FUN_0059db20 (cdecl) — ClassName null → 0; first factory whose name strcmp-equals ClassName;
// none → 0; W = factory->Create(Parent, Payload); W != 0 → Nickname copied into the widget
// name (clamped to 31 chars; null Nickname → empty name).
Win3D* WinFactoryCreate(char const* Nickname, char const* ClassName, Win3D* Parent,
                        void* Payload);

// (2026-10-07) The port-only bare create-function registration (FWinFactoryCreateFn /
// WinFactoryRegister(name, fn)) and WinFactoryRegisterUiPageTypes are gone with their last users
// (the "NN_Main" shell of nn_screen.cpp and the flat menu layer): every factory is a
// WINFACTORY<T> static, as in retail.
