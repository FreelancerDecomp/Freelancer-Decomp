// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Async -- the synchronous work-item service (retail 0x062fd400-0x062fd5e0, plus the
// ICF-shared DumpWorkQueue / WorkQueueSize bodies). Declared in common_async.h.
#include "common_async.h"

#include <cstdlib>
#include <windows.h>

namespace
{
    int g_async_refcount = 0;
}

namespace Async
{
    void __cdecl Startup()
    {
        // Common.dll.c Async::Startup @ 0x9d5d0
        ++g_async_refcount;
    }

    void __cdecl Shutdown()
    {
        --g_async_refcount;
    }

    Action::Action(bool bInDeleteWhenDone)
    {
        // 0x062fd400: +0x04 = argument, +0x08 = 0.
        bDeleteWhenDone = bInDeleteWhenDone;
        hEvent = nullptr;
    }

    Action::Action(Action const& Other)
    {
        // 0x062fd420: copies both fields, the event handle included.
        bDeleteWhenDone = Other.bDeleteWhenDone;
        hEvent = Other.hEvent;
    }

    Action::~Action()
    {
        // 0x062fd540
        if (hEvent != nullptr)
            CloseHandle(hEvent);
    }

    void* Action::operator new(size_t Size)
    {
        // 0x062fd560: JMP msvcrt malloc
        return malloc(Size);
    }

    void Action::operator delete(void* Ptr)
    {
        // 0x062fd570: JMP msvcrt free
        free(Ptr);
    }

    bool __cdecl AddWork(Action* Work)
    {
        // 0x062fd580
        if (!Work->bDeleteWhenDone)
            Work->hEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);

        Work->Run();

        if (Work->bDeleteWhenDone)
        {
            // 062fd5aa: vtable+4 with flag 1 -- the scalar deleting destructor.
            delete Work;
            return true;
        }

        SetEvent(Work->hEvent);
        return true;
    }

    void __cdecl DumpWorkQueue()
    {
        // 0x062a8920: RET
    }

    int __cdecl WorkQueueSize()
    {
        // 0x06325390: XOR EAX,EAX; RET
        return 0;
    }
}
