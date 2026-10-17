// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Common.dll-internal helpers shared by the BaseIdea family TUs that were split out of
// common_idea.cpp (CMN-SPLIT stage 1, 2026-10-14): common_idea.cpp, common_moveidea.cpp,
// common_faceidea.cpp, common_splinemoveidea.cpp, common_dodgeidea.cpp,
// common_breakawayidea.cpp, common_dodgemissileidea.cpp. None of these is a retail
// function of its own; until the split they lived in common_idea.cpp's anonymous
// namespace. Consumers write `using namespace CmnIdea;` after their includes.

#include "common_ini.h"     // Vector
#include "common_goods.h"   // BaseWatcher
#include "common_iface.h"   // IObjInspect, IObjRW, ObjRwFromWatchable
#include "common_aisteer.h" // GetInterceptClearanceOfIface

#include <cstring>

namespace CmnIdea
{
    // Local helper: reinterpret a raw IEEE-754 bit pattern as float, for the
    // handful of constants in this cluster that don't round-trip cleanly
    // through a decimal literal (e.g. 0x3dc90fdb). Matches this marathon's
    // established BitsToFloat convention used elsewhere in the port (see
    // src/IVP/ivp_compact_builder/ivp_surface_builder_ledge_soup.cpp).
    inline float BitsToFloat(unsigned int Bits)
    {
        float Result;
        std::memcpy(&Result, &Bits, sizeof(Result));
        return Result;
    }

    // Shared by MoveIdea::SetPosition/SetWatchObject/Commit and (independently)
    // by SplineMoveIdea's own SetContext/Commit (common_splinemoveidea.cpp): retail reaches an
    // external "owning slot array" through `*(BaseIdea::Context + 0xc)` and then
    // writes fixed offsets inside it (+0x64 for an object id, +0x200..+0x208 for
    // a Vector). This is the same array common_behavior.h models as
    // FormationMoveFx::SlotOwner -- which independently confirms that class's
    // "SlotOwner" at +0x0c is nothing but BaseIdea::Context under another name.
    //
    // Context is the IBehaviorManager and its +0x0c the owned PathfindManager; retail
    // dereferences both levels unguarded (e.g. 062d584e/062d5865), and so does this.
    inline unsigned char* SlotArrayOf(void* Context)
    {
        return *reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(Context) + 0x0c);
    }

    // BaseIdea::Handle is the owning SystemManager: retail reaches its SelfTarget as
    // `[this+8] + 0x14fc` and its ThrustManager as `[this+8] + 0x18` with plain ADDs and
    // never tests Handle. (The former NavBlockOf / NavOwnerIfaceOf / ComponentThisOf
    // helpers are gone: callers write Handle->SelfTarget / Handle->Thrust.)

    // FUN_062e4bb0 (decomp-tracker item 1749). MoveIdea::Update's
    // TargetKind48 == 1 arm calls it at .text:062d50a2 as
    // FUN_062e4bb0(NavBlock, WatchedOuter) and squares the result to get its
    // distance gate.
    //
    // CLOSED 2026-09-17 (claude:common-steering-close). Item 1749 is now ported
    // for real as GetInterceptClearanceOfIface (common_aisteer.h, common_aiintercept.cpp), next to
    // the GetInterceptClearance / IsWithinInterceptRangeOfIface family it belongs
    // to, and this helper is a thin forwarder rather than the 0x7effffff sentinel
    // stand-in it used to be. The DEVIATION note that lived here is gone with it:
    // the real clearance is computed now.
    //
    // Both retail sites (062d50a2, 062d92ea) push `[this+8]+0x14fc` and the
    // `raw ? raw-8 : 0` watched object straight into FUN_062e4bb0 with no test,
    // and both keep the ST0 return unrounded (062d50a7 squares it, 062d92f9
    // FCOMPs it), so this forwards unguarded and returns double.
    inline double NavInterceptClearanceOfIface(FAiTarget* NavBlock, IObjInspect const* TargetIface)
    {
        return GetInterceptClearanceOfIface(NavBlock, TargetIface);
    }

    // BreakAwayIdea::Update and its EvaluateBreakAwayClearance helper both reach
    // the watched object the same way: `MOV EAX,[this+0x28]` (the
    // FDirectionPicker's BaseWatcher), then the family's `EAX ? EAX - 8 : 0`
    // downcast from the Watchable sub-object to the whole IObjRW.
    inline IObjRW* WatchedObjectOf(BaseWatcher const& Watcher)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    }
}
