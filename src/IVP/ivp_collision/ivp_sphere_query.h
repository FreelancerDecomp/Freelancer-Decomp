// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The sphere-overlap query PhysicalSystem::FindSphereCollisions (0628d450) runs against an
// IVP environment: Common.dll .text:06350630-06350a57 (four functions). Class and member names
// are port-local (no retail symbol survives); the layout is read off the listing.
//
// 0628d450 builds the query object on its stack (local_230, 0x224 bytes): an IVP_U_Vector whose
// elems point at the 0x80-entry inline buffer right after the header (0628d4be-0628d4c9), filled
// by collect_objects (06350740) and freed on exit by the vector's own inline-buffer test
// (0628d525-0628d542, also the EH unwind state 0 at 0628d4dd).

#include <cstdint>

#include "../ivp_utility/ivp_math.h"
#include "../ivp_utility/ivp_u_vector.h"

class IVP_Real_Object;
class IVP_Environment;
class IVP_OV_Node;
class IVP_OV_Tree_Manager;

// The 6-dword record 0628d450 builds at local_248 and hands to collect_objects.
struct FIVP_Sphere_Query_Record
{
    float Center[3];            // +0x00 (0628d47d..0628d4a8)
    float Unwritten_0x0c;       // +0x0c: 0628d499/0628d4a8 copy an uninitialised stack dword
                                //        (local_24c) here; 06350740 never reads it
    float Radius;               // +0x10 (0628d4b2)
    int32_t MaxLedgeTreeDepth;  // +0x14 (0628d4b6: 4)
};
static_assert(sizeof(FIVP_Sphere_Query_Record) == 0x18, "the record is six dwords");

class IVP_Sphere_Object_Query
{
public:
    IVP_U_Vector_Fixed<IVP_Real_Object, 0x80> Found; // +0x000: header + 0x80 inline slots
    IVP_U_Point Center;                    // +0x208 (hesse +0x214 is never written)
    float Radius;                          // +0x218
    int32_t MaxLedgeTreeDepth;             // +0x21c
    IVP_OV_Tree_Manager* TreeManager;      // +0x220

    // FUN_06350740 (.text:06350740-063507d9, thiscall RET 8).
    void collect_objects(const FIVP_Sphere_Query_Record* Record, IVP_Environment* Environment);

    // FUN_063507e0 (.text:063507e0-06350a57, thiscall RET 0xc), recursive over the OV tree.
    void collect_objects_in_node(IVP_OV_Node* Node, const IVP_U_Point* HalfSize, const IVP_U_Point* QueryCenter);
};
static_assert(sizeof(IVP_Sphere_Object_Query) == 0x224, "0628d450 reserves 0x224 bytes at local_230");
