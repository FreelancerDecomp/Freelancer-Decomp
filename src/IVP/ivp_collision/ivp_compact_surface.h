// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Compiled IVP surface blob: IVP_Compact_Surface header + its IVP_Compact_Ledgetree_Node tree
// (Ipion layout). Field offsets as read by IVP_SurfaceManager_Polygon (0634ac70 / ac90 / acb0 /
// ad40), the ray solver (0634a3f0 / 0634a4e0) and the radius query (0634a9b0); the same node
// layout is written by the ledge-soup compiler (IVP_SurfaceBuilder_Ledge_Soup::CompileNode, 0636b5e0).

#include <cstdint>
#include "../ivp_compact_builder/ivp_compact_ledge.h"

#pragma pack(push, 4)

// One node of the compiled ledge tree. The left child is the node that immediately follows
// (this + 1), the right child is at this + offset_right_node.
class IVP_Compact_Ledgetree_Node
{
public:
    int32_t offset_right_node;  // +0x00: 0 for a terminal node
    int32_t offset_compact_ledge; // +0x04: self-relative; terminal nodes always have one,
                                  //        inner nodes may carry their children's hull ledge
    float   center[3];           // +0x08
    float   radius;              // +0x14
    uint8_t box_sizes[3];        // +0x18: per-axis half extent in units of radius * 0.004
    uint8_t free_0;              // +0x1b

    const IVP_Compact_Ledgetree_Node* left_son() const { return this + 1; }
    const IVP_Compact_Ledgetree_Node* right_son() const
    {
        return reinterpret_cast<const IVP_Compact_Ledgetree_Node*>(
            reinterpret_cast<const char*>(this) + offset_right_node);
    }
    const IVP_Compact_Ledge* get_compact_ledge() const
    {
        return reinterpret_cast<const IVP_Compact_Ledge*>(
            reinterpret_cast<const char*>(this) + offset_compact_ledge);
    }
};
static_assert(sizeof(IVP_Compact_Ledgetree_Node) == 0x1c, "IVP_Compact_Ledgetree_Node size");

// `class`, not `struct`: common_phyarch.h forward-declares it as a class (Ipion's own spelling),
// and MSVC mangles the two keywords differently.
class IVP_Compact_Surface
{
public:
    float   mass_center[3];          // +0x00
    float    rotation_inertia[3];     // +0x0c
    float    upper_limit_radius;      // +0x18
    uint32_t max_deviation_and_size;  // +0x1c: low byte = max_factor_surface_deviation
    int32_t  offset_ledgetree_root;   // +0x20: self-relative

    const IVP_Compact_Ledgetree_Node* get_compact_ledge_tree_root() const
    {
        return reinterpret_cast<const IVP_Compact_Ledgetree_Node*>(
            reinterpret_cast<const char*>(this) + offset_ledgetree_root);
    }
};
static_assert(sizeof(IVP_Compact_Surface) == 0x24, "IVP_Compact_Surface header size");

#pragma pack(pop)
