// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// ============================================================================
// IVP_U_BigVector growth helpers (retail 0x0635d3e0 / 0x06350590), defined in
// ivp_u_bigvector.cpp (moved from src/Common/common_stubs.cpp, 2026-10-14).
//
// The vector is the 32-bit-header growable pointer array
// {capacity@+0, count@+4, elems@+8[, inline buffer @+0xc]} -- in this port
// IVP_SurfaceManager_OutList (ivp_physics/ivp_surface_manager.h). Not
// IVP_U_Vector, whose header fields are 16-bit.
// ============================================================================

struct IVP_SurfaceManager_OutList;

// 0x0635d3e0 (formerly extern "C" FUN_0635d3e0): grow to 2*capacity+1,
// copying the live elements; the old buffer is freed unless it is the inline
// buffer right after the header (or null). Callers: the compact ledge
// solver, the polygon surface manager, IVP_Create_Exact_Mindists'
// ledge lists.
void __fastcall IVP_U_BigVector_Grow(IVP_SurfaceManager_OutList* Vector);

// 0x06350590 (formerly extern "C" FUN_06350590): grow only when the vector is
// full (count >= capacity). Retail's sole caller (0634e223,
// IVP_Create_Exact_Mindists) is ported with this test inlined
// (ivp_mindist_create.cpp, FLedgeList::Add), so nothing calls it here.
void __fastcall IVP_U_BigVector_EnsureRoom(IVP_SurfaceManager_OutList* Vector);
