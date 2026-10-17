// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "win_factory.h"

#include "nn_text_control.h"

#include <string.h>

namespace
{
    // Retail DAT_0067dcec — head of the factory list (next at factory+0x4). Zero-initialised
    // before any dynamic initializer runs, so WINFACTORY<T> globals may register from their
    // own static constructors in any order.
    WinFactory* G_WinFactoryHead = nullptr;
}

WinFactory::WinFactory(char const* Name)
    : Next(nullptr)
    , NameLength(0)
{
    // FUN_0059c360-shaped ctor: +0x8 = 0, +0xc = 0, +0x4 = 0; Name != 0 → strlen clamped to
    // 0x1f, REP MOVSD/MOVSB copy, NUL; +0x8 = length; FUN_0059dab0(this).
    ClassName[0] = '\0';
    unsigned Length = 0;
    if (Name != nullptr)
    {
        Length = static_cast<unsigned>(strlen(Name));
        if (Length >= 0x20)
            Length = 0x1f;
        memcpy(ClassName, Name, Length);
        ClassName[Length] = '\0';
    }
    NameLength = Length;
    WinFactoryRegister(this);
}

WinFactory::~WinFactory()
{
    // FUN_00579380
    WinFactoryUnregister(this);
}

void WinFactoryRegister(WinFactory* Factory)
{
    // FUN_0059dab0
    Factory->Next = G_WinFactoryHead;
    G_WinFactoryHead = Factory;
}

void WinFactoryUnregister(WinFactory* Factory)
{
    // FUN_0059dad0: find Factory keeping the previous node; not found → return; unlink from
    // prev->next or the head; Factory->next = 0.
    WinFactory* Prev = nullptr;
    WinFactory* Walk = G_WinFactoryHead;
    while (Walk != nullptr)
    {
        if (Walk == Factory)
            break;
        Prev = Walk;
        Walk = Walk->Next;
    }
    if (Walk == nullptr)
        return;
    if (Prev != nullptr)
        Prev->Next = Walk->Next;
    else
        G_WinFactoryHead = Walk->Next;
    Factory->Next = nullptr;
}

Win3D* WinFactoryCreate(char const* Nickname, char const* ClassName, Win3D* Parent,
                        void* Payload)
{
    // FUN_0059db20
    if (ClassName == nullptr)
        return nullptr;

    WinFactory* Factory = G_WinFactoryHead;
    while (Factory != nullptr)
    {
        // Inline strcmp 0059db42..0059db72.
        if (strcmp(Factory->ClassName, ClassName) == 0)
            break;
        Factory = Factory->Next;
    }
    if (Factory == nullptr)
        return nullptr;

    // 0059db95 factory vftable[0](Parent, Payload).
    Win3D* Widget = Factory->Create(Parent, Payload);
    if (Widget == nullptr)
        return nullptr;

    // 0059dba3..0059dbe1: widget +0x14 = length, +0x18 = name (clamped to 0x1f chars; a null
    // Nickname gives the empty name) -- Win3D::SetName is that inline copy.
    Widget->SetName(Nickname);
    return Widget;
}

