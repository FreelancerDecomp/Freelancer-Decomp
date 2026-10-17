// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Common.dll-internal helpers shared by common_systemmanager.cpp and common_sysmgr_gungroups.cpp.
// CMN-SPLIT stage 1 (2026-10-14): these were anonymous-namespace helpers of the one
// common_systemmanager.cpp; the split made them shared, so they live here, inline, in a named
// namespace (common_aisteer.cpp has its own unrelated anonymous WatchedObject).

#include "common_systemmanager.h" // BaseWatcher, Watchable, IObjRW, ObjRwFromWatchable

#include <cstdint>
#include <cstring>

namespace CmnSysMgr
{
    inline void WriteFloatAt(unsigned char* Dst, float Value)
    {
        std::memcpy(Dst, &Value, sizeof(float));
    }

    // The module-wide "watcher holds a live object" test: retail reads the watched pointer,
    // tests it for 0, subtracts 8 (the Watchable -> IObjRW downcast) and tests again, so the
    // literal 8 counts as empty as well.
    inline bool IsWatcherLive(BaseWatcher const& Watcher)
    {
        Watchable const* const Target = Watcher.target();
        return Target != nullptr && reinterpret_cast<std::uintptr_t>(Target) != 8;
    }

    // `EAX = [W]; ECX = EAX ? EAX - 8 : 0` -- the object behind a watcher, or null. Callers
    // that dereference it without a test do so exactly where retail does.
    inline IObjRW* WatchedObject(BaseWatcher const& Watcher)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    }
}
