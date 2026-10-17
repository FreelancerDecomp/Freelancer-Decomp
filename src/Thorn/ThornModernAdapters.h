// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

struct IDAComponent;

// Modern owners for a handful of retail Thorn helpers whose original
// implementation was coupled to raw VC6 register/stack conventions.  The
// public API preserves the observed operations while keeping ownership in
// ordinary C++.
//
// NOTE (2026-09-10 cleanup): this class used to declare many more methods
// (SwapRecordValues, FinalizeParameterStream, PrependRecordBlock,
// EnsureRecordCapacity, CloseCapture, MatchCapture, ReturnDispatchValue,
// VisitDescending[WithCleanup], VisitStepped, RepeatWithCleanup,
// CleanupRepeated, plus their supporting FThornModernRecordStorage /
// FThornModernParameterStream / FThornModernCaptureState /
// FThornModernRepeatState structs).  Two independent audit passes (SONNET-L3
// and SONNET-L4, 2026-09-10) found these were fabricated "modern owner"
// explanations for FUN_ addresses that are actually plain vendored Lua 3.2.2
// internals (luaM_growaux, luaV_closure, lbuiltin.c's swap/auxsort,
// lstrlib.c's end_capt/match_capture/match) or compiler-generated
// EH-vector construct/destruct thunks used only by RB-tree helper arrays
// that the port already replaces with contiguous/value-owned containers
// (FParamCurve::Slot13Rebuild, FThornScene::RebuildPending).  None of those
// methods had a single real caller anywhere in src/Thorn, so they were
// deleted after the decomp-tracker items pointing at them were re-attributed
// to their real owners. See docs/re/thorn.md, SONNET-CLEANUP (2026-09-10).
class FThornModernAdapters final
{
public:
    static char* FormatDiagnostic(char* Destination, const char* Format, ...);

    // Modern owner for the retail 06f4a910/06f4a930/06f4a950 cleanup
    // thunks: release a non-null interface and always clear the owning slot.
    // The interface pointer is borrowed only for the call; no retail wrapper
    // or VC6 smart-pointer layout crosses this boundary.
    static void ReleaseOwnedComponent(IDAComponent*& Component);

    // Modern source-string owner for retail FUN_06f4ccc0/lua_dostring:
    // measure a NUL-terminated chunk without allowing size_t-to-int wrap.
    static bool MeasureSourceLength(const char* Source, int& Length);

    // Modern value-tag owner for retail FUN_06f4b270/lua_rawgettable.
    static bool IsLuaTableTag(int TypeTag);

    // Modern count owner for retail FUN_06f4d4d0/luaV_pack: negative
    // vararg counts are observable as an empty sequence.
    static int NormalizeVarargCount(int RequestedCount);

    // Modern diagnostic owner for retail FUN_06f4dfc0/call_arith.
    static const char* GetArithmeticDiagnostic();

};
