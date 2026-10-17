// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP_U_BigVector growth helpers. Moved from src/Common/common_stubs.cpp (via
// common_stubs_a3.cpp) on 2026-10-14 (claude-code:cmn-a3); renamed from the
// raw extern "C" FUN_0635d3e0 / FUN_06350590 and typed on the vector header
// (the former int* view: [0] capacity, [1] count, [2] elems). Same
// statements, same order.

#include "ivp_u_bigvector.h"

#include "../ivp_physics/ivp_surface_manager.h"

#include <cstdlib>

static_assert(sizeof(IVP_SurfaceManager_OutList) == 0xc, "the inline buffer starts right after the 12-byte header");

// Common.dll.txt 0635d3e0-0635d43d (XREF 0634aadb, 0634ab2e, 0634e357,
// 06350597, 0635abd2, 06362445, 0636247c), thiscall on the vector. Mallocs a
// new buffer sized OldCapacity*8+8 bytes (`LEA ECX,[EAX*8+8]`), stores the
// new capacity OldCap*2+1, copies the Count live elements across, frees the
// old buffer unless it is the inline buffer immediately following the header
// (`LEA ECX,[ESI+0xc]; CMP`) or null -- nulling the field on that path --
// then installs the new buffer.
void __fastcall IVP_U_BigVector_Grow(IVP_SurfaceManager_OutList* Vector) // formerly extern "C" FUN_0635d3e0
{
    void* newBuffer = malloc(static_cast<size_t>(Vector->Capacity) * 8 + 8); // FUN_06343f70
    Vector->Capacity = Vector->Capacity * 2 + 1;
    int count = Vector->Count;
    void** newElems = (void**)newBuffer;
    void** oldElems = Vector->Elems;
    for (int i = 0; i < count; ++i)
    {
        newElems[i] = oldElems[i];
    }
    void** oldBuffer = Vector->Elems;
    if (oldBuffer != (void**)(Vector + 1) && oldBuffer != nullptr)
    {
        free(oldBuffer); // FUN_06343fb0
        Vector->Elems = nullptr;
    }
    Vector->Elems = newElems;
}

// Common.dll.txt 06350590-0635059c (XREF 0634e223): `MOV EAX,[ECX+4];
// CMP EAX,[ECX]; JL ret; JMP FUN_0635d3e0` -- grow only when Count >=
// Capacity, otherwise there is room and this is a no-op.
void __fastcall IVP_U_BigVector_EnsureRoom(IVP_SurfaceManager_OutList* Vector) // formerly extern "C" FUN_06350590
{
    if (Vector->Count >= Vector->Capacity)
    {
        IVP_U_BigVector_Grow(Vector);
    }
}
