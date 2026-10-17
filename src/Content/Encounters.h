// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_id.h"
#include "x86math.h"

#include <map>
#include <vector>

class INI_Reader;

// Content.dll lane CNT-POP (docs/re/content.md, TU map): encounters and the static game tables.
//   TU31 tail 06efc9b0-06efe100  missions\formations.ini database (DAT_06fcf38c)
//   TU32      06efe100-06efe8e0  LoadContentGameTables / UnloadContentGameTables

// ---------------------------------------------------------------------------------------
// TU32 -- the static game tables, loaded by the GameSupport ctor FUN_06efeb10 (06efeb73)
// and released by its dtor FUN_06efebb0 (06efebdb).
// ---------------------------------------------------------------------------------------
void __cdecl LoadContentGameTables();      // FUN_06efe300
void __cdecl UnloadContentGameTables();    // FUN_06efe870

// ---------------------------------------------------------------------------------------
// TU31 tail -- formations (missions\formations.ini, [Formation] blocks).
// ---------------------------------------------------------------------------------------

// FUN_06efc7a0 (thiscall on the matrix, no return): the 3x3 identity.
void SetMatrixIdentity(FMatrix* Matrix);

struct FFormationSlot           // retail 0x34 bytes
{
    FVector Position;           // +0x00 "pos" / "pl_pos" x, y, z
    FMatrix Orientation;        // +0x0c the loader always stores the identity
    bool bPlayerSlot;           // +0x30 1 for "pl_pos", 0 for "pos"
};

struct FFormation               // map value, retail 0x44 bytes
{
    ID_String Nickname;         // +0x00 CreateID("nickname")
    FVector Position;           // +0x04 the loader leaves it zero
    FMatrix Orientation;        // +0x10 the loader leaves it identity
    std::vector<FFormationSlot> Slots;   // +0x34 (alloc byte) / +0x38..+0x40

    int CountPlayerSlots() const;                               // FUN_06efcac0
    int CountShipSlots() const;                                 // FUN_06efcaf0
    // Slot position in world space: Orientation * slot + Position; zero vector when
    // the slot vector is unallocated or Index is out of range.
    FVector GetSlotPosition(unsigned int Index) const;          // FUN_06efcb20
    // Position as above plus Orientation * slot orientation; false (outputs untouched)
    // when the slot vector is unallocated or Index is out of range.
    bool GetSlotTransform(unsigned int Index, FVector* OutPosition,
                          FMatrix* OutOrientation) const;       // FUN_06efcbe0
    // No range check, exactly as retail.
    bool IsPlayerSlot(int Index) const;                         // FUN_06efccd0
};

// DAT_06fcf38c: map<formation nickname id, formation>; static init LAB_06efc9b0, atexit
// FUN_06efc9e0.
extern std::map<unsigned int, FFormation> GFormations;

void __cdecl LoadFormation(INI_Reader* Reader);                          // FUN_06efccf0
void __cdecl ClearFormations();                                          // FUN_06efd070
FFormation* __cdecl FindFormation(unsigned int const* FormationId);      // FUN_06efd1a0
