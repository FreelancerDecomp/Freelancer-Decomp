// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "dalib_net.h"

// Retail DALib.dll.c CDPMsgList::CDPMsgList(void) @ 0x41f0: InitializeCriticalSection +
// CreateEventA(manual-reset, initially non-signaled).
CDPMsgList::CDPMsgList()
{
    InitializeCriticalSection(&Cs);
    EmptyEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
}

// Copy ctor @ 0x2c70 / operator= @ 0x2d20 are the compiler's memberwise members (list copy,
// then the CRITICAL_SECTION by MOVSD.REP x6, then the event handle) -- implicit in
// src/include/dalib_net.h, not defined here.

// Retail DALib.dll.c CDPMsgList::~CDPMsgList(void) @ 0x4240.
CDPMsgList::~CDPMsgList()
{
    DeleteCriticalSection(&Cs);
    CloseHandle(EmptyEvent);
    Messages.clear();
}

// Retail .text:065c4300: ADD ECX,0x10; PUSH ECX; CALL EnterCriticalSection.
void CDPMsgList::Lock()
{
    EnterCriticalSection(&Cs);
}

// Retail .text:065c4310: ADD ECX,0x10; PUSH ECX; CALL LeaveCriticalSection.
void CDPMsgList::Unlock()
{
    LeaveCriticalSection(&Cs);
}

// Retail .text:065c45a0: WaitForSingleObject([this+0x28], TimeoutMs); RET 4.
void CDPMsgList::WaitForMsg(unsigned long TimeoutMs)
{
    WaitForSingleObject(EmptyEvent, TimeoutMs);
}

// Retail .text:065c45c0: SetEvent([this+0x28]).
void CDPMsgList::SetEmptyEvent()
{
    SetEvent(EmptyEvent);
}

// Retail DALib.dll.c CDPMsgList::Add(CDPMessage*) @ 0x4320.
void CDPMsgList::Add(CDPMessage* Message)
{
    EnterCriticalSection(&Cs);
    if (Messages.empty())
        SetEvent(EmptyEvent);
    Messages.push_back(Message);
    LeaveCriticalSection(&Cs);
}

// Retail DALib.dll.c CDPMsgList::GetNextMsg(void) @ 0x4390.
CDPMessage* CDPMsgList::GetNextMsg()
{
    CDPMessage* Result = nullptr;
    EnterCriticalSection(&Cs);
    if (!Messages.empty())
    {
        Result = Messages.front();
        Messages.pop_front();
        if (Messages.empty())
            ResetEvent(EmptyEvent);
    }
    LeaveCriticalSection(&Cs);
    return Result;
}

// Retail DALib.dll.c CDPMsgList::PeekNextMsg(void) @ 0x43f0.
CDPMessage* CDPMsgList::PeekNextMsg()
{
    CDPMessage* Result = nullptr;
    EnterCriticalSection(&Cs);
    if (!Messages.empty())
        Result = Messages.front();
    LeaveCriticalSection(&Cs);
    return Result;
}

// Lock Dest before this; extraction does not reset the source list event.
void CDPMsgList::ExtractMsgs(unsigned long Id, CDPMsgList& Dest)
{
    EnterCriticalSection(&Dest.Cs);
    EnterCriticalSection(&Cs);
    for (std::list<CDPMessage*>::iterator It = Messages.begin(); It != Messages.end();)
    {
        if ((*It)->SenderId == Id)
        {
            if (Dest.Messages.empty())
                SetEvent(Dest.EmptyEvent);
            Dest.Messages.push_back(*It);
            It = Messages.erase(It);
        }
        else
        {
            ++It;
        }
    }
    LeaveCriticalSection(&Cs);
    LeaveCriticalSection(&Dest.Cs);
}

// Retail DALib.dll.c CDPMsgList::CopyList(CDPMsgList&) @ 0x44e0: despite the name, this moves
// every message from Other into this list (not a copy), then clears Other and resets its event.
// Retail locks Other first then this.
void CDPMsgList::CopyList(CDPMsgList& Other)
{
    EnterCriticalSection(&Other.Cs);
    EnterCriticalSection(&Cs);
    for (std::list<CDPMessage*>::iterator It = Other.Messages.begin(); It != Other.Messages.end(); ++It)
    {
        CDPMessage* Message = *It;
        if (Messages.empty())
            SetEvent(EmptyEvent);
        Messages.push_back(Message);
    }
    Other.Messages.clear();
    ResetEvent(Other.EmptyEvent);
    LeaveCriticalSection(&Cs);
    LeaveCriticalSection(&Other.Cs);
}
