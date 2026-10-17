// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstddef>
#include <cstdint>

#include "../ivp_utility/ivp_u_vector.h"
#include "../ivp_utility/ivp_vhash.h"   // IVP_OV_Tree_Hash's base
#include "../ivp_collision/ivp_collision.h" // IVP_Collision (collision_fvector element type)
#include "ivp_listener.h"                   // IVP_Listener_Hull (IVP_OV_Element's base)

// ============================================================================
// IVP_OV_Element / IVP_OV_Node / IVP_OV_Tree_Manager / IVP_Mindist_Manager
// ----------------------------------------------------------------------------
// Lane claude:w18-ivp, 2026-09-17. This file exists because decomp-tracker item
// 3111 (FUN_0634ebd0, 411 insns, the single largest `blocked` function left in
// Common.dll outside the two excluded solvers) turned out NOT to be blocked at
// all. Its inherited blocker_reason claimed that "what FUN_0635d030 /
// FUN_0635d2e0 actually WRITE onto EDI (the field_90 pointer)" was unknowable
// without re-deriving a whole octree subsystem. That is disproved by reading
// those two functions' own bytes:
//
//   * FUN_0635d2e0 (Common.dll.txt:0635d2e0-0635d358, 30 insns) writes exactly
//     ONE field of its argument: `MOV dword ptr [EDI+0x8],0x0` at :0635d2f0.
//     Everything else it touches belongs to the NODE it unlinks the element
//     from, not to the element.
//   * FUN_0635d030 (:0635d030-0635d25e) writes exactly TWO: `FSTP [EBP+0x20]`
//     at :0635d074 (the extrapolated radius its FUN_0635c620 sub-call returns)
//     and `MOV [EBP+8],<node>` at :0635d0c4 / :0635d144 / :0635d1d6 (the node
//     it was inserted into). It additionally APPENDS to the caller's out-vector
//     (manager+0x16c, :0635d230) via FUN_0635ce90.
//
// So the element side of the contract is fully determined, and the type of the
// pointer itself is pinned independently by its construction site:
// FUN_0634f180 (item 3113) does `operator new(0x30)` + FUN_0635c020(new, obj)
// and stores the result into IVP_Real_Object+0x90, while FUN_0635c020 is a
// plain constructor that stores the owning object back at +0x24.
//
// Retail names are not recovered for any of these classes; the IVP_OV_* names
// are this port's own, chosen because the structure (an element record with a
// center/radius that lives in one node of a level-indexed spatial subdivision,
// plus a per-element vector of pairwise collision records) is the classic
// "object volume tree" shape.
// ============================================================================

class IVP_Real_Object;
class IVP_Environment;
class IVP_Hull_Manager;
class IVP_OV_Element;
class IVP_Ledge_Mindist; // src/IVP/ivp_physics/ivp_mindist.h

#pragma pack(push, 4)

// ----------------------------------------------------------------------------
// IVP_Collision -- the polymorphic per-object-pair record stored in
// IVP_OV_Element::collision_fvector -- is declared in
// src/IVP/ivp_collision/ivp_collision.h (2026-10-08, cmn-phy5: it used to be
// a raw `void* vtable` struct here with slot-1/slot-4 dispatch helpers).
//
// Evidence for the slots this file's callers use, all Common.dll.txt:
//   * vtable slot 1 fills a 2-pointer out-array with the pair's two objects:
//     `MOV EAX,[ECX]; LEA EDX,[ESP+0x38]; PUSH EDX; CALL [EAX+4]` then the
//     caller reads [ESP+0x38] and [ESP+0x3c] (:0634edd1-:0634eddf), and the
//     same shape again at :0634ef11-:0634ef23 and inside FUN_0634f140.
//   * vtable slot 4 is the "drop this collision" entry point, called with the
//     literal 1: `PUSH 0x1; CALL [EDX+0x10]` (:0634f00c).
//   * +0xc / +0x10 are this record's own index inside each of the two
//     elements' collision_fvectors -- FUN_063505a0 (:063505bd-:063505dd)
//     fixes exactly one of the two up after swapping two vector slots, picking
//     whichever already held the old index.
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// IVP_OV_Node -- one cell of the spatial subdivision. operator new(0x28) at
// Common.dll.txt:0635d0cc pins the size; FUN_0635c2c0 is its constructor (the
// SAME constructor IVP_OV_Tree_Manager runs on its own embedded query cell at
// manager+0x144, which is how the two are known to be the same type).
//
// GEOMETRY, settled 2026-09-17 by lane claude:w19-ovtree against
// FUN_0635c620 / FUN_0635c590 / FUN_0635c8d0 / FUN_0635cbd0 / FUN_0635c9d0 /
// FUN_0635d360 (all read end to end, see ivp_ov_element.cpp's range table):
//
//   * CellX/Y/Z are INTEGER grid coordinates at this node's own level, and a
//     node covers TWO cells per axis, i.e. the world box
//     [C * S, (C + 2) * S] with S == IVP_OV_Tree_Manager::CellSizeTable[Level
//     + 40] == 2^Level. Proof: FUN_0635d360 emits `CellSizeTable[Level+40] *
//     (float)CellX` as the box origin and `CellSizeTable[LevelPlus1+40]` as
//     the single shared extent (:0635d364-:0635d3a6); FUN_0635c8d0's overlap
//     test brackets the fine node by [b, b+2) against [a<<d, (a+2)<<d)
//     (:0635c8f0/:0635c93c); and FUN_0635c9d0 splits a parent into the THREE
//     overlapping children 2C, 2C+1, 2C+2 (:0635ca2b-:0635ca69), which only
//     makes sense for a 2-cell-wide node.
//   * BIGGER Level == BIGGER cell. FUN_0635d030 only runs the containment test
//     when root->Level >= new->Level and otherwise grows the root coarser via
//     FUN_0635cbd0 (:0635d179-:0635d196), and FUN_0635cbd0 builds that coarser
//     root with Level + 1 and halved coordinates (:0635cc8c-:0635cc97).
//   * LevelPlus1 is always exactly Level + 1 -- FUN_0635c620 writes the pair as
//     (EDI-1, EDI) (:0635c75c-:0635c776) and FUN_0635cbd0 / FUN_0635c9d0
//     increment/decrement both together. It exists so FUN_0635d360 can index
//     the same table twice without an add, and so the level deltas in
//     FUN_0635c9d0 come out already off-by-one relative to FUN_0635c980's.
// ----------------------------------------------------------------------------
class IVP_OV_Node
{
public:
    // +0x00..+0x10: the five dwords FUN_0635d030 copies verbatim out of the
    // manager's own embedded query cell at manager+0x144..+0x154
    // (Common.dll.txt:0635d0ed-:0635d115) -- which is simply the first 20 bytes
    // of one IVP_OV_Node copied into another. Those same 20 bytes are what
    // FUN_06369200 hashes and what the node ValueSet's comparator (LAB_06369240,
    // materialized in ivp_ov_element.cpp) compares, except that the
    // comparator skips LevelPlus1 because it is redundant with Level.
    int32_t CellX;      // +0x00 (was `CellKey0`)
    int32_t CellY;      // +0x04 (was `CellKey1`)
    int32_t CellZ;      // +0x08 (was `CellKey2`)
    int32_t Level;      // +0x0c
    int32_t LevelPlus1; // +0x10 (was `CellKey4`)
    IVP_OV_Node* Parent; // +0x14: FUN_0635d270 returns it after deleting an empty node (:0635d28f/:0635d2ba).
    // +0x18..+0x1f: the child vector. Retail grows it with the same
    // FUN_063441a0 realloc every other IVP_U_Vector uses (:0635cd7a/:0635cb0e)
    // and appends with the same `elems[n_elems++] = x` (:0635cd7f-:0635cd90).
    // Capacity is soft-capped at 27: both FUN_0635cbd0 (:0635cd56) and
    // FUN_0635c9d0 (:0635cb42/:0635cb9d) only *print* a German "Mehr als 27
    // Kinder" / "Excessive amount of..." warning, they never refuse the append.
    IVP_U_Vector<IVP_OV_Node> Children;    // +0x18
    IVP_U_Vector<IVP_OV_Element> Elements; // +0x20: memsize/n_elems/elems read at :0635d0a0/:0635d0bd (insert) and :0635d2f7/:0635d305 (remove).

    // FUN_0635c2c0 (decomp-tracker item 3337, Common.dll.txt:0635c2c0-0635c300).
    // Zeroes Parent and both vector headers and NOTHING else -- CellX/Y/Z,
    // Level and LevelPlus1 stay raw `operator new` scratch until the caller
    // fills them in.
    IVP_OV_Node();

    // FUN_0635c310 (item 3338, :0635c310-0635c40e).
    ~IVP_OV_Node();
};
static_assert(sizeof(IVP_OV_Node) == 0x28, "IVP_OV_Node size must be 0x28 (operator new(0x28), Common.dll.txt:0635d0cc)");
static_assert(offsetof(IVP_OV_Node, Level) == 0x0c, "IVP_OV_Node+0x0c");
static_assert(offsetof(IVP_OV_Node, LevelPlus1) == 0x10, "IVP_OV_Node+0x10");
static_assert(offsetof(IVP_OV_Node, Parent) == 0x14, "IVP_OV_Node+0x14");
static_assert(offsetof(IVP_OV_Node, Children) == 0x18, "IVP_OV_Node+0x18");
static_assert(offsetof(IVP_OV_Node, Elements) == 0x20, "IVP_OV_Node+0x20");

// ----------------------------------------------------------------------------
// IVP_OV_Element -- 0x30 bytes (operator new(0x30), Common.dll.txt:0634f1ae).
// One per IVP_Real_Object with collision geometry enabled; reached as
// IVP_Real_Object::ov_element (+0x90).
// ----------------------------------------------------------------------------
//
// cmn-phy5 2026-10-08: a real IVP_Listener_Hull now (it is a payload of its
// object's hull manager pool, and the hull manager dispatches slots 1/2/3 on
// it). Vtable .rdata:063a5680, 5 slots:
//   +0x00 0626c9a0 `MOV EAX,3; RET`        get_type
//   +0x04 0635c1e0                          hull_limit_exceeded_event
//   +0x08 0634dc10 `if (this) slot4(1)`    hull_manager_is_going_to_be_deleted_event
//   +0x0c 0628f050 `RET 8`                  hull_manager_is_reset (base default)
//   +0x10 0635c070                          scalar deleting destructor (FUN_0635c090)
// The ctor stamps the base table 063a5694 (:0635c024) and then this one
// (:0635c047). +0x04 (IVP_Listener_Hull::HullPoolSlot) is the slot index
// IVP_Hull_Manager::PoolInsert returned, stored and read as a full dword
// (`MOV [ESI+4],EAX` at :0635c194/:0635c1ca, read at :0635c0c1).
class IVP_OV_Element : public IVP_Listener_Hull
{
public:
    IVP_OV_Node* node;        // +0x08: the cell this element currently sits in; cleared to 0 by FUN_0635d2e0 (:0635d2f0), set by FUN_0635d030.
    IVP_Hull_Manager* hull_manager; // +0x0c: the owner object's OWN embedded hull manager (&real_object->hull_manager); null until the first insert. Zeroed by the ctor (:0635c059) and by the dtor after unlinking (:0635c0cd).
    float    CenterX;         // +0x10..+0x18: refreshed from the core's world matrix translation
    float    CenterY;         //   (IVP_Core::MWorldFCore.vv, core+0x124..+0x12c) at
    float    CenterZ;         //   Common.dll.txt:0634ec53-:0634ec7a.
    float    Pad1C;           // +0x1c: never written; the ctor zeroes only +0x10/+0x14/+0x18 (:0635c04d-:0635c053).
    float    radius;          // +0x20: initialised to -1.0f by the ctor (:0635c060, literal 0xbf800000) and overwritten by every insert (:0635d074).
    IVP_Real_Object* real_object; // +0x24: the owner (:0635c05c).
    IVP_U_Vector<IVP_Collision> collision_fvector; // +0x28: every live pairwise collision this object takes part in. The ctor gives it memsize 0x10 and a 0x40-byte HEAP buffer (:0635c02e-:0635c03d) -- note it does NOT use an inline buffer, but the destructor still runs the standard `elems != this+0x30` inline-buffer guard (:0635c0f0).

    // FUN_0635c020 (Common.dll.txt:0635c020-0635c06a, decomp-tracker item 3330).
    explicit IVP_OV_Element(IVP_Real_Object* Owner);

    // FUN_0635c090 (item 3332, :0635c090-0635c127). The real destructor;
    // slot 4 (0635c070) is its scalar deleting wrapper.
    virtual ~IVP_OV_Element();

    // 0626c9a0 (ICF-shared `MOV EAX,3; RET`).
    int32_t get_type() override { return 3; }

    // LAB_0635c1e0 (RET 8): real_object->environment->mindist_manager->
    // RecheckOvElement(real_object); the two arguments are not read.
    void hull_limit_exceeded_event(IVP_Hull_Manager* HullManager, float HullIntrusionValue) override;

    // LAB_0634dc10 (RET 4, the ICF-shared body IVP_Collision's slot 3 also
    // uses): `if (this) this->slot4(1)` -- delete this.
    void hull_manager_is_going_to_be_deleted_event(IVP_Hull_Manager* HullManager) override;

    // FUN_0635c130 (item 3333, :0635c130-0635c1d1). Re-key this element inside
    // its owner's hull manager pool: drop the old pool slot if there is one,
    // evaluate the hull value for "now", and insert a fresh slot. `NewHullMgr`
    // is only consulted when this element has no hull manager yet -- retail
    // ignores the argument entirely on the re-insert path (:0635c158), which
    // is correct because it is always the same object's own manager.
    void InsertIntoHullManager(IVP_Hull_Manager* NewHullMgr, float KeyBias);

    // FUN_0635c200 (.text:0635c200-0635c248, RET 4). Append Collision to
    // collision_fvector and record its slot in whichever of the collision's
    // two back-indices is still -1 (+0xc first, else +0x10).
    void add_oo_collision(IVP_Collision* Collision);

    // FUN_0635c250 (.text:0635c250-0635c2bb, RET 4). Swap-with-last removal
    // using the collision's own back-indices, then reset the one that pointed
    // here to -1.
    void remove_oo_collision(IVP_Collision* Collision);
};
static_assert(sizeof(IVP_OV_Element) == 0x30, "IVP_OV_Element size must be 0x30 (operator new(0x30), Common.dll.txt:0634f1ae)");
static_assert(offsetof(IVP_OV_Element, HullPoolSlot) == 0x04, "IVP_OV_Element+0x04");
static_assert(offsetof(IVP_OV_Element, node) == 0x08, "IVP_OV_Element+0x08");
static_assert(offsetof(IVP_OV_Element, hull_manager) == 0x0c, "IVP_OV_Element+0x0c");
static_assert(offsetof(IVP_OV_Element, CenterX) == 0x10, "IVP_OV_Element+0x10");
static_assert(offsetof(IVP_OV_Element, radius) == 0x20, "IVP_OV_Element+0x20");
static_assert(offsetof(IVP_OV_Element, real_object) == 0x24, "IVP_OV_Element+0x24");
static_assert(offsetof(IVP_OV_Element, collision_fvector) == 0x28, "IVP_OV_Element+0x28");

// ----------------------------------------------------------------------------
// IVP_OV_Tree_Manager -- the 0x17c-byte object at IVP_Environment+0x14
// (declared there as `void* field_14_manager`, whose own comment still says
// "What THIS 0x17c-byte object is has never been proven ... TODO: identify
// it." -- THIS IS THAT IDENTIFICATION, see the note in ivp_environment.h).
//
// Proof that env+0x14 is the spatial-subdivision manager:
//   * IVP_OV_Element's destructor reaches it as
//     `real_object->environment->+0x14` and hands itself to FUN_0635d2e0,
//     which unlinks the element from a node (Common.dll.txt:0635c0dc-:0635c0e8).
//   * IVP_Mindist_Manager::RecheckOvElement uses the same chain for both the
//     remove (:0634ec37-:0634ec44) and the insert (:0634ecd5-:0634ece6).
//   * The "81-entry power-of-two float table" its own constructor builds and
//     that ivp_environment.h already documents at +0x00..+0x140 is indexed by
//     IVP_OV_Node::Level in FUN_0635d360 (`FLD [ECX + level*4 + 0xa0]`) --
//     i.e. it is the per-level cell-size table of the subdivision, which also
//     explains why the earlier "statistics histogram" hypothesis felt close
//     (powers of two) but never fit.
//
// This port does NOT construct this class; instances are always the existing
// retail-shaped allocation reached through IVP_Environment. Only the fields
// with a proven access site are named.
// ----------------------------------------------------------------------------
// The node hash of IVP_OV_Tree_Manager (+0x170): IVP_VHash(0x100) with vtable
// 063a56a4 {LAB_06369240 compare, 0635c4e0 deleting dtor -> 063691f0}.
// compare: the stored node's +0x0c, +0x00, +0x04, +0x08 against the probe's
// (Level, CellX, CellY, CellZ; LevelPlus1 is not compared), early-out on the
// first mismatch, 1 when all four match. The dtor (063691f0) only re-stamps
// the vtable and runs ~IVP_VHash (063522f0). cmn-phy7 2026-10-08.
class IVP_OV_Tree_Hash : public IVP_VHash
{
public:
    explicit IVP_OV_Tree_Hash(int Size) : IVP_VHash(Size) {}
    ~IVP_OV_Tree_Hash() override {}
    int Compare(void* Elem0, void* Elem1) override;
};

class IVP_OV_Tree_Manager
{
public:
    // FUN_0635c410 (.text:0635c410-0635c4d8): QueryCell ctor (0635c2c0), +0x174
    // and +0x178 = 0, NodeHash = new IVP_OV_Tree_Hash(0x100), then the 81-entry
    // power-of-two table. +0x16c is left untouched. Built by the environment
    // ctor (operator new(0x17c), .text:0634c364-0634c3a1).
    IVP_OV_Tree_Manager();

    // FUN_0635c500 (.text:0635c500-0635c562): NodeHash slot 1 (1) when set,
    // +0x170 = 0, then ~QueryCell (0635c310). The nodes of the tree itself are
    // not touched.
    ~IVP_OV_Tree_Manager();

    // +0x000..+0x143. CellSizeTable[i] == 2.0f^(i - 40) EXACTLY (the
    // constructor FUN_0635c410 builds it with two iterative accumulators
    // doubling and halving outward from index 40 == 1.0f -- it is already
    // ported, as the IVP_OV_Tree_Manager constructor in ivp_ov_element.cpp, which
    // replaced the earlier raw AnomalyManager0635c410::powerOfTwoTable model that
    // sat in the (since dissolved) src/Common/common_stubs.cpp).
    //
    // Indexed two different ways, which is what pinned the exponent sign:
    //   * `CellSizeTable[Level + 40]` == 2^Level == the world SIZE of one cell
    //     at that level (FUN_0635d360 `FLD [ECX+EDX*4+0xa0]; FIMUL [EAX]`,
    //     :0635d367-:0635d372, and FUN_0635cbd0 :0635cca1/:0635ccb3).
    //   * `CellSizeTable[0x29 - LevelPlus1]` == 2^-Level == its RECIPROCAL,
    //     which is what FUN_0635c620 multiplies world coordinates by to get
    //     grid coordinates (:0635c64c-:0635c653, :0635c78e-:0635c795).
    float CellSizeTable[81];

    // +0x144..+0x16b. An EMBEDDED IVP_OV_Node, not a bare 5-dword key: the
    // manager's constructor runs the node constructor FUN_0635c2c0 on it
    // (Common.dll.txt:446920-446922, `LEA ECX,[ESI+0x144]; CALL FUN_0635c2c0`)
    // and 0x144 + sizeof(IVP_OV_Node) == 0x16c lands exactly on CollectVector.
    // FUN_0635c620 fills its first five dwords, FUN_0635d030 hashes it
    // (:0635d083) and looks it up (:0635d08f), and on a miss copies those same
    // five dwords into the freshly allocated real node (:0635d0ed-:0635d115).
    // Its own Parent/Children/Elements are never used.
    IVP_OV_Node QueryCell;

    // +0x16c: where insert_ov_element parks the caller's out-vector so
    // FUN_0635cdd0/FUN_0635ce90 can append to it (:0635d230, :0635ce31,
    // :0635ceee). Retail passes it implicitly through the manager rather than
    // threading it down the recursion.
    IVP_U_Vector<IVP_OV_Element>* CollectVector;

    // +0x170: the cell-key -> node lookup. It is the generic "Robin Hood
    // value-set" container IVP_VHash (src/IVP/ivp_utility/ivp_vhash.h/.cpp:
    // FUN_063522a0 ctor with capacity 0x100, FUN_063523d0 AddElem,
    // FUN_06352480 RemoveElem, FUN_06352580 FindElem), with the
    // comparator LAB_06369240 installed directly by the manager's own
    // constructor. Stored VALUES are IVP_OV_Node*; the tag is
    // IVP_OV_HashNodeKey's CRC32 of the node's first 20 bytes.
    IVP_OV_Tree_Hash* NodeHash;

    void* Unmodeled_0x174;    // +0x174: zeroed by the ctor, never read by anything traced.
    IVP_OV_Node* CurrentRoot; // +0x178: the tree's current root cell (:0635d147/:0635d151).

    // FUN_0635d2e0 (decomp-tracker item 3353, Common.dll.txt:0635d2e0-0635d358,
    // 30 insns). Detach `Element` from whatever node it currently sits in.
    void remove_ov_element(IVP_OV_Element* Element);

    // FUN_0635d030 (item 3351, :0635d030-0635d25e). Returns the element's new
    // extrapolated radius (retail returns it on the x87 stack) and, when
    // `TouchingOut` is non-null, appends every element whose volume overlaps
    // the freshly inserted one.
    float insert_ov_element(IVP_OV_Element* Element,
                            float MinRadius,
                            float MaxRadius,
                            IVP_U_Vector<IVP_OV_Element>* TouchingOut);

    // FUN_0635d270 (item 3352, :0635d270-0635d2d3). Deletes `Node` if it has
    // become empty and returns its parent so the caller can keep collapsing
    // upward; returns null when nothing was collapsed.
    IVP_OV_Node* collapse_empty_node(IVP_OV_Node* Node);

    // FUN_0635d360 (item 3354, :0635d360-0635d3a8). Decode a node back into a
    // world-space box: `OriginOut` gets C * 2^Level per axis and `*ExtentOut`
    // the single shared edge length 2^(Level+1) (the node is two cells wide).
    void node_cell_bounds(const IVP_OV_Node* Node, float* OriginOut3, float* ExtentOut) const;

    // ---- everything below is the tree machinery insert_ov_element drives ----

    // FUN_0635c620 (item 3344, :0635c620-0635c8c1). Choose the subdivision
    // level for `Element` at the given radius and write the resulting cell key
    // into QueryCell. Returns whichever of the two radii it managed to fit.
    float compute_query_cell(IVP_OV_Element* Element, float MinRadius, float MaxRadius);

    // FUN_0635cbd0 (item 3348, :0635cbd0-0635cdc9). Replace CurrentRoot with a
    // fresh cell one level COARSER that still contains the old root, biased
    // toward `Target`.
    void grow_root(const IVP_OV_Node* Target);

    // FUN_0635c980 (item 3346, :0635c980-0635c9cd). Walk down from `Start` to
    // the deepest already-existing node that still contains `Target`.
    IVP_OV_Node* find_deepest_container(IVP_OV_Node* Start, const IVP_OV_Node* Target);

    // FUN_0635c9d0 (item 3347, :0635c9d0-0635cbc9). Create the chain of
    // intermediate cells between `Ancestor` and `Node` and link `Node` in.
    void link_node_under(IVP_OV_Node* Ancestor, IVP_OV_Node* Node);

    // FUN_0635cdd0 (item 3349, :0635cdd0-0635ce80). Append every element in
    // `Node`'s whole subtree whose sphere touches `Element`'s to CollectVector.
    void collect_subtree(const IVP_OV_Element* Element, IVP_OV_Node* Node);

    // FUN_0635ce90 (item 3350, :0635ce90-0635d021). The same collect, but
    // descending only into the children that can actually reach `Target`.
    void collect_touching(const IVP_OV_Element* Element, IVP_OV_Node* Node, const IVP_OV_Node* Target);

    // FUN_0635c590 (item 3343, :0635c590-0635c615). Does `Coarse`, scaled down
    // by `LevelDelta` levels, fully contain `Fine`?
    static bool cell_contains(const IVP_OV_Node* Coarse, const IVP_OV_Node* Fine, int32_t LevelDelta);

    // FUN_0635c8d0 (item 3345, :0635c8d0-0635c972). Same scaling, but a plain
    // AABB overlap rather than containment.
    static bool cell_overlaps(const IVP_OV_Node* Coarse, const IVP_OV_Node* Fine, int32_t LevelDelta);
};
static_assert(offsetof(IVP_OV_Tree_Manager, QueryCell) == 0x144, "IVP_OV_Tree_Manager+0x144");
static_assert(offsetof(IVP_OV_Tree_Manager, CollectVector) == 0x16c, "IVP_OV_Tree_Manager+0x16c");
static_assert(offsetof(IVP_OV_Tree_Manager, NodeHash) == 0x170, "IVP_OV_Tree_Manager+0x170");
static_assert(offsetof(IVP_OV_Tree_Manager, CurrentRoot) == 0x178, "IVP_OV_Tree_Manager+0x178");
static_assert(sizeof(IVP_OV_Tree_Manager) == 0x17c, "IVP_OV_Tree_Manager size must be 0x17c (IVP_Environment+0x14's documented allocation size)");

// FUN_06369200 (decomp-tracker item 3594, Common.dll.txt:06369200-0636922f).
// The tag IVP_OV_Tree_Manager::NodeHash is keyed by: a table-driven CRC32 over
// the node's first 20 bytes (CellX/CellY/CellZ/Level/LevelPlus1), finished with
// `| 0x80000000` so a live tag can never collide with the container's own
// "slot empty" zero.
uint32_t IVP_OV_HashNodeKey(const IVP_OV_Node* Node);

// ----------------------------------------------------------------------------
// IVP_Mindist_Manager -- the 0x18-byte object at IVP_Environment+0x10 (its
// constructor, FUN_06350390, is already ported as the IVP_Mindist_Manager
// constructor in ivp_ov_element.cpp, decomp-tracker item 3134; only the two fields
// this file's two methods actually touch are named here).
// ----------------------------------------------------------------------------
class IVP_Mindist_Manager
{
public:
    // FUN_06350390 (.text:06350390-063503b3, RET 4): the inlined IVP_U_Vector
    // ctor of +0x0c, REP STOSD over all 6 dwords, then +0x04 = Environment.
    // Built by the environment ctor (operator new(0x18), .text:0634c333-0634c371).
    explicit IVP_Mindist_Manager(IVP_Environment* Environment);

    // FUN_063503c0 (.text:063503c0-06350456): deletes every mindist on the
    // +0x08 list, then every one on the +0x14 list (slot 4 with 1, the next
    // link +0x74 read before each delete), then the +0x0c vector's Clear.
    ~IVP_Mindist_Manager();

    // +0x00: a re-entrancy guard, NOT a generic flag. RecheckOvElement sets it
    // to 1 immediately before dispatching the environment's +0x2c listener and
    // back to 0 immediately after (Common.dll.txt:0634ecbe / :0634ecca), and
    // tests it at the top (:0634ec81) to take a stripped-down path when it is
    // already set -- i.e. when the listener itself caused the re-entry.
    int32_t InListenerCallback;
    IVP_Environment* environment;   // +0x04: every manager lookup in RecheckOvElement goes through it.

    // +0x08/+0x0c/+0x14: RESOLVED 2026-09-17 (lane claude:w26-simunit) by
    // reading FUN_0634fec0 (item 3125) end to end, Common.dll.txt:0634fec0-
    // 0634ff8f. That function is this class's own "detach this mindist from me"
    // method and it touches three of the four remaining dwords.
    //
    // +0x08 -- head of an intrusive doubly-linked list of the mindists this
    // manager owns, threaded through IVP_Ledge_Mindist::ManagerNext (+0x74) and
    // ManagerPrev (+0x78). The head write is the tell:
    //     :0634fee4  EAX=[ESI+0x74]; if (EAX) [EAX+0x78]=[ESI+0x78]
    //     :0634fef1  EAX=[ESI+0x78]; if (EAX) [EAX+0x74]=[ESI+0x74]
    //     :0634ff00  else            [EDI+0x8]=[ESI+0x74]
    // Only a node with a NULL +0x78 rewrites the manager's +0x08, so +0x78 is
    // prev, +0x74 is next, and +0x08 is the head.
    IVP_Ledge_Mindist* ActiveMindistHead; // +0x08

    // +0x0c: an IVP_U_Vector header, exactly the {uint16 memsize; uint16
    // n_elems; T** elems;} shape this port already models -- NOT a new
    // container type. Two independent confirmations:
    //   * this class's own constructor, FUN_06350390 (Common.dll.txt:06350395-
    //     0635039d), zeroes `word[EDX+0xc]`, `word[EDX+0xe]` and `dword[EDX+0x10]`
    //     individually BEFORE the REP STOSD that would have covered them anyway
    //     -- an inlined IVP_U_Vector constructor, not a memset.
    //   * FUN_0634fec0's tail (:0634ff50-:0634ff8a) does the vector's own
    //     remove-and-allow-resort: `AX=word[EDI+0xe]; DEC EAX; JS return;
    //     ECX=[EDI+0x10]+EAX*4` then a backward scan for the mindist, and on a
    //     hit `DEC word[EDI+0xe]; CX=word[EDI+0xe]; [EDX+EAX*4]=[EDX+ECX*4]`.
    //     That is IVP_U_Vector::RemoveAtAndAllowSort against memsize@+0x0c,
    //     n_elems@+0x0e, elems@+0x10.
    IVP_U_Vector<IVP_Ledge_Mindist> ActiveMindists; // +0x0c..+0x13

    // +0x14: head of the "invalid" mindist list, threaded through the same
    // ManagerNext(+0x74)/ManagerPrev(+0x78) links as ActiveMindistHead.
    // FUN_0634e960 pushes onto it: `EDX=[ECX+0x14]; [EAX+0x74]=EDX;
    // [EAX+0x78]=0; if (EDX) [EDX+0x78]=EAX; [ECX+0x14]=EAX`
    // (.text:0634e976-0634e98a). Was `uint32_t Field_0x14` (cmn-phy5, 2026-10-08).
    IVP_Ledge_Mindist* InvalidMindistHead; // +0x14

    // FUN_0634ebd0 (decomp-tracker item 3111, Common.dll.txt:0634ebd0-0634f13a,
    // 411 insns).
    void RecheckOvElement(IVP_Real_Object* Object);

    // FUN_0634f180 (item 3113, :0634f180-0634f204, 39 insns).
    void RebuildOvElement(IVP_Real_Object* Object);

    // FUN_0634fec0 (decomp-tracker item 3125, Common.dll.txt:0634fec0-0634ff8f,
    // 79 insns). Defined in src/IVP/ivp_collision/ivp_mindist_limiter.cpp, next
    // to IVP_RescheduleCollisionEvent, its most-studied caller. Fully detaches
    // a mindist: cancels its queued collision event, unlinks it from this
    // manager's list, unlinks both of its synapse slots from their objects'
    // synapse lists, and removes it from ActiveMindists.
    void DetachMindist(IVP_Ledge_Mindist* Mindist);

    // FUN_0634e9f0 (.text:0634e9f0-0634eaef, RET 4). Called by
    // IVP_Ledge_Mindist::init_mindist when neither object has a phantom
    // (+0x1c): link the mindist into this manager's list and both objects'
    // synapse lists, run the first narrow-phase pass, then either queue its
    // collision event or hand it to vtable slot 7. Defined in
    // ivp_mindist_limiter.cpp.
    void insert_exact_mindist(IVP_Ledge_Mindist* Mindist);

    // FUN_0634eb00 (.text:0634eb00-0634ebc3, RET 4). The phantom variant.
    // NOT PORTED YET (cmn-phy5, 2026-10-08): see the definition.
    void insert_lazy_mindist(IVP_Ledge_Mindist* Mindist);

    // FUN_0634e960 (.text:0634e960-0634e9cd, RET 4). Push the mindist onto
    // InvalidMindistHead and both of its synapses onto their objects'
    // invalid_synapse_list_head. Defined in ivp_mindist_limiter.cpp.
    void insert_invalid_mindist(IVP_Ledge_Mindist* Mindist);

    // FUN_0634e8a0 (.text:0634e8a0-0634e954, RET 4): insert_exact_mindist's
    // linking half (state bits, exact list, synapse lists, ActiveMindists)
    // without the narrow-phase pass or event. Port-local name (the library
    // name was not recovered). Defined in ivp_mindist_limiter.cpp.
    void insert_exact_mindist_no_recalc(IVP_Ledge_Mindist* Mindist);

    // FUN_0634ffa0 (.text:0634ffa0-06350013, RET 4): unlink from the invalid
    // list (+0x14) and both objects' invalid_synapse_list_head (+0x24).
    // Defined in ivp_mindist_limiter.cpp.
    void remove_invalid_mindist(IVP_Ledge_Mindist* Mindist);

    // FUN_06350020 (.text:06350020-06350044, RET 4), the library's
    // remove_hull_mindist: take both synapses out of their objects' hull
    // manager pools (object +0x64, FUN_06363280) by the slot each synapse
    // holds at its +0x04. `this` is not read. Callers: 0634ddd3 (the mindist
    // dtor 0634dd60), 0634fcd6, 06362d75 (IVP_Complex_Mindist::
    // DeactivateToSimple). Defined in ivp_mindist_limiter.cpp.
    void remove_hull_mindist(IVP_Ledge_Mindist* Mindist);

    // FUN_06350460 (.text:06350460-0635053b, RET 4), the library's
    // recalc_exact_mindist: re-solve one mindist and re-file it (invalid via
    // slot 7, or into the phantoms' lazy/hull scheme). Callers 063454cb,
    // 0634f259, 0634f311. Defined in ivp_mindist_limiter.cpp.
    void recalc_exact_mindist(IVP_Ledge_Mindist* Mindist);

    // FUN_0634f300 (.text:0634f300-0634f31e): recalc_exact_mindist on every
    // mindist of the exact list (+0x08), next (+0x74) read before each call.
    // Called by Simulate_Psi (:0634d17c). Defined in ivp_mindist_limiter.cpp.
    void recalc_all_exact_mindists();

    // FUN_0634f230 (.text:0634f230-0634f2fa): the library's
    // recalc_all_exact_wheel_mindist over ActiveMindists (+0x0c), backwards.
    // Called by Simulate_Psi (:0634d0eb). Defined in ivp_mindist_limiter.cpp.
    void recalc_all_exact_wheel_mindist();

    // FUN_0634f210 (.text:0634f210-0634f22b, plain RET) -- the library's
    // recalc_all_exact_mindists_events: for every mindist of the
    // ActiveMindistHead list (next = +0x74, read before the call),
    // FUN_0634f320(mindist, 1, 1) = IVP_RescheduleCollisionEvent. Called by
    // IVP_Environment_Simulate_Psi (0634d18e). Defined in
    // ivp_mindist_limiter.cpp. cmn-phy5 2026-10-08.
    void recalc_all_exact_mindists_events();
};
static_assert(offsetof(IVP_Mindist_Manager, environment) == 0x04, "IVP_Mindist_Manager+0x04");
static_assert(offsetof(IVP_Mindist_Manager, ActiveMindistHead) == 0x08, "IVP_Mindist_Manager+0x08");
static_assert(offsetof(IVP_Mindist_Manager, ActiveMindists) == 0x0c, "IVP_Mindist_Manager+0x0c");
static_assert(offsetof(IVP_Mindist_Manager, InvalidMindistHead) == 0x14, "IVP_Mindist_Manager+0x14");
static_assert(sizeof(IVP_Mindist_Manager) == 0x18, "IVP_Mindist_Manager size must be 0x18");

// FUN_0634f140 (decomp-tracker item 3112, Common.dll.txt:0634f140-0634f172,
// 19 insns). Spread hash of a collision's PARTNER object: the record's own two
// objects are XOR-ed together with `Object`, which cancels whichever of the two
// IS `Object` and leaves the other one, then the result is run through the
// multiplicative spread `(h >> 8) * 0x3ff + h`. Returned UNMASKED -- every
// caller applies the table mask itself.
uint32_t IVP_HashCollisionPartner(IVP_Collision* Collision, IVP_Real_Object* Object);

#pragma pack(pop)
