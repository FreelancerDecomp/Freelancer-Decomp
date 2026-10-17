// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdint>

#include "ivp_compact_ledge.h"
#include "../ivp_collision/ivp_compact_surface.h" // IVP_Compact_Ledgetree_Node
#include "../ivp_utility/ivp_u_vector.h"
#include "../ivp_utility/ivp_vhash.h"

struct IVP_SurfaceManager_OutList; // ivp_surface_manager.h (retail IVP_U_BigVector)
struct IVP_U_Matrix;               // ivp_math.h

// Retail's IVP_SurfaceBuilder_Ledge_Soup.
//
// Class name confirmed from a literal string found in the shipped Common.dll
// binary itself -- NOT present in the Ghidra .txt/.c export (it must live
// outside the exported range, e.g. a leftover RTTI/debug string), so it has
// to be recovered by scanning the raw DLL bytes:
//   grep -a -o "IVP_[A-Za-z_]*" EXE/Common.dll | sort -u | grep -i surface
// -> IVP_SurfaceBuilder_Ledge_Soup, IVP_SurfaceBuilder_Pointsoup,
//    IVP_SurfaceManager, IVP_SurfaceManager_Polygon
//
// Purpose (confirmed via both real Common.dll call sites -- PhyArch::GetDefaultPart
// @0x6280450-0x6280480 and one other OBB-corner builder @0x628ae87-0x628aead):
// collects one or more already-built IVP_Compact_Ledge pieces (e.g. the output of
// IVP_SurfaceBuilder_Pointsoup::BuildConvexHull, see ivp_surface_builder_pointsoup.h)
// via InsertLedge(), then Compile()s them into a single combined compact-surface
// result. Both call sites are stack-local instances (not heap/member objects):
//   FUN_0636c9a0(&pointVector)          // build one hull -> IVP_Compact_Ledge*
//   FUN_0636a050(&soup, hull)           // == InsertLedge()
//   FUN_0636a0b0(&soup, ...)            // == Compile() (NOT YET PORTED)
//
// Object layout below is confirmed byte-exact from the constructor
// (Common.dll+0x6369e70) and destructor (Common.dll+0x6369f20) disassembly:
// the ctor zeroes exactly 0x94 bytes (`MOV ECX,0x25; REP STOSD`) and the dtor
// walks 6 embedded 8-byte "growable array" headers (word capacity, word count,
// dword element-pointer), freeing each one's data pointer unless it still
// equals exactly (header_start + 8) -- the empty/never-grown sentinel. That is
// byte-for-byte the same shape as this codebase's existing IVP_U_Vector<T>
// (src/IVP/ivp_utility/ivp_u_vector.h: memsize@+0, n_elems@+2, elems@+4,
// "empty" elems == (T**)(this+1), grown via the same FUN_063441a0 Realloc
// confirmed elsewhere in this port) -- so all 6 are modeled directly as
// IVP_U_Vector<T> members here rather than hand-rolled.
//
// Vector A's element type is confirmed: IVP_Compact_Ledge* (see
// InsertLedge, matches decomp-tracker item 3616).
//
// [2026-09-09 D81] Vectors B, C and F are now ALSO confirmed (not guessed --
// structurally derived from reading the full raw disassembly of all 8
// Compile()-callee functions, decomp-tracker items 3619/3620/3621/3622/3625/
// 3629/3630/3631, all moved unprocessed->partial with the evidence recorded
// in each item's notes): all three are IVP_U_Vector<BuildNode*>, where
// BuildNode is a newly-identified 0x28-byte heap struct (operator_new'd once
// per inserted ledge inside FUN_0636a350/item 3621) holding a copied
// center/position, a radius-ish float, 3 per-axis octant-classification
// bytes, a payload/ledge-pointer field, and two child-link fields that
// FUN_0636b5e0 (item 3629) walks recursively to compile a position-
// independent, self-relative-offset-encoded node tree directly into the
// final IVP_Compact_Surface blob (allocated by FUN_0636b190/item 3625).
// BuildNode itself is NOT yet defined as a real C++ struct here -- its field
// semantics (esp. the exact meaning of the 3 octant bytes and the +0x1c
// value) are understood from disassembly but not cross-checked against any
// IVP reference source (none exists in this port), so it is deliberately
// left undeclared rather than guessed.
//
// [2026-09-09 D91] Follow-up pass read the ~14-callee TODO list's smallest
// items plus 2 more newly-discovered ones (decomp-tracker items 3618/3623/
// 3624/3627/3628, all moved unprocessed->partial). Corrections/additions to
// the above:
//  - BuildNode+0x1c is NOT a single fixed-meaning field: it holds the source
//    IVP_Compact_Ledge* until FUN_0636b520 (item 3628, called from
//    FUN_0636b190/item 3625's step 6 -- NOT one of Compile()'s 8 direct
//    calls) runs, at which point it gets overwritten IN PLACE with the
//    node's destination byte offset in the merged blob. FUN_0636b5e0/item
//    3629 (which reads +0x1c as an already-a-destination-offset payload)
//    necessarily runs after this two-phase reuse.
//  - m_VectorB IS populated by genuine (non-dead) retail code somewhere --
//    FUN_0636b520 walks it unconditionally (not just m_VectorC), proving it
//    participates in every real Compile() call, not merely via item 3619's
//    dead branch. WHERE it's populated is still not found (ruled out:
//    FUN_0636a350/a640/a720/a860/a190, items 3621/3622/3623/3624/3618).
//  - m_Ledges is freed and reset to empty by FUN_0636b520 once every ledge
//    has been merged into the compiled blob (matches InsertLedge's "ownership
//    consumed" framing above).
//  - FUN_0636a860 (item 3624, 689 insns, the largest function in the whole
//    chain) is the actual recursive spatial mean/median-split BuildNode tree
//    builder: given a sub-list of BuildNode*, it either returns the sole
//    element (list size 1, leaf terminal case) or computes the sub-list's
//    bbox (via the newly-found helper FUN_0636a720/item 3623), allocates a
//    new internal BuildNode, partitions the list axis-by-axis into temp
//    buckets via mean+-epsilon classification, and recurses into itself
//    twice to build the two children -- this is what actually uses the 3
//    octant-classification bytes to shape the tree, confirming they are
//    genuine per-axis split-side decisions, not arbitrary flags.
//  - this[+0x7c] (see below, the object FUN_0636b190/item 3625 step 4
//    constructs) is confirmed to be a VERTEX-DEDUPLICATION HASH INDEX: when
//    merging multiple ledges (this[+0x7c]!=0), FUN_0636b3a0 (item 3627)
//    hash-looks-up/inserts each vertex via FUN_0636cfa0/FUN_06352580 so
//    shared vertices across ledges get welded into one shared entry rather
//    than duplicated; for a single-ledge Compile() (this[+0x7c]==0) it's a
//    plain flat memcpy of the whole ledge blob instead.
//
// [2026-09-09 D94] Both of D91's open questions below are now RESOLVED (not
// merely narrowed) -- read every remaining candidate function's full raw
// disassembly (FUN_0636ca00/FUN_0636cf70/FUN_063449d0/FUN_0636d650/
// FUN_0635ada0/FUN_06391d5a/FUN_063556b0/FUN_06343f80/FUN_0635aa60/
// FUN_06347e00/FUN_06347e60/FUN_063522a0, decomp-tracker items 3643/3648/
// 2767/3659/3312/4052/3223/2748/3308/2878/2879/3170) plus, for m_VectorD/E,
// an exhaustive raw-disasm grep across the WHOLE Common.dll.txt for every
// register-relative (i.e. not ESP-local) reference to offsets +0x5c/+0x5e/
// +0x60 and +0x64/+0x66/+0x68, filtered to this class's own address range
// (0x06369e70 ctor .. 0x0636e0c0):
//
//  - m_VectorB: has exactly ONE write site in the entire class -- the
//    push_back inside FUN_0636a1e0 (item 3619), and that function is PROVEN
//    unreachable in retail (both real Common.dll call sites of Compile()
//    pass options=nullptr, which defaults to {0,1,0,1}, so the
//    options[0]!=0 gate that reaches FUN_0636a1e0 never holds). Its two
//    "consumers" -- FUN_0636b190/item 3625's step-1 size-accumulation loop
//    and FUN_0636b520/item 3628's merge loop -- both walk it unconditionally
//    but over zero elements in practice, making them harmless no-ops. All 7
//    originally-flagged candidate populator functions were confirmed
//    unrelated: FUN_0636ca00/FUN_0636cf70 belong to the OLD, superseded
//    IVP_SurfaceBuilder_Pointsoup degenerate-case hull path (different
//    class); FUN_063449d0/FUN_06391d5a/FUN_063556b0 are generic CRT/
//    IVP_U_Vector<T> template boilerplate (Clear/free/Add); FUN_0636d650/
//    FUN_0635ada0 belong to the unrelated mass-property/PhySys::BuildIVP_Real
//    pipeline. CONCLUSION: m_VectorB is provably ALWAYS EMPTY when Compile()
//    executes in real retail -- there is no live populator to keep looking
//    for. Element type (IVP_U_Vector<BuildNode*>) stays as confirmed by D81.
//  - m_VectorD and m_VectorE: provably NEVER touched by any live code in this
//    class at all -- not even a read. The exhaustive offset grep's only hits
//    inside this class's address range are the constructor (zero-init) and
//    destructor (empty-check-and-maybe-free, which never actually frees
//    since the vectors are always empty); zero hits anywhere in Compile()'s
//    entire call graph (all 8 direct callees plus every second-level callee
//    read across this and prior passes, ~20 functions / ~1900 instructions
//    total). Their element type is therefore unknowable AND irrelevant --
//    no code ever writes or reads a payload through them. They are dead/
//    vestigial members, left as IVP_U_Vector<void> (never guess a layout for
//    a member with zero observed usage).
//
// See decomp-tracker item 3617's notes for the full evidence trail. This
// closes the vector-identification phase of this class's investigation --
// the only remaining blocker for porting Compile() itself is defining
// BuildNode/IVP_Compact_Surface as real structs and transcribing the ~18
// already-decoded-but-unported functions in the dependency chain (dense
// per-axis FP math, deliberately left for a build-capable lane).
//
// [2026-09-09 D96] BuildNode is now defined below as a real C++ struct
// (0x28 bytes, matching every field prior lanes confirmed: item 3621's
// leaf-creation writes, item 3629's compiled-node copy, item 3624's
// internal-node allocation). m_VectorB/C/F are re-typed from
// IVP_U_Vector<void> to IVP_U_Vector<BuildNode> accordingly (m_VectorD/E
// stay IVP_U_Vector<void> -- D94 proved them permanently dead/never
// populated, so no element type is knowable or needed for them).
//
// Also ported this pass: FUN_0636a720 (decomp-tracker item 3623, the
// smallest/most-confirmed of the ~18 remaining callees, a per-BuildNode-list
// bounding-box accumulator used only by FUN_0636a860/item 3624). Re-derived
// instruction-by-instruction from Common.dll.txt:470911-471025 (not
// transcribed from item 3623's prior English summary), including manually
// tracking x87 FPU register-stack depth across all 106 instructions and
// resolving the DAT_063ee784 float constant's raw bits (0x3B831206) from the
// .data section. See ComputeNodeListBounds() below and its .cpp
// implementation for the full evidence trail.
//
// FUN_0636a860 (item 3624, 689 insns, the recursive tree-splitter that is
// this helper's only caller) remains NOT ported -- per this class's
// precision warning, its size and self-recursive complexity make it the
// highest-risk piece in the chain; left for a follow-up build-capable lane
// together with Compile() itself and the other ~16 still-unported callees.
//
// [2026-09-09 D99] BuildLeafNodes() (retail FUN_0636a350, item 3621, the
// primary leaf-node/bbox/octant constructor -- exactly the kind of dense
// per-axis-FP-math function the precision warning is about) and its
// dependency ComputeLedgeBounds() (retail FUN_0635aa60, item 3308) are now
// BOTH fully ported below, instruction-by-instruction, after achieving very
// high confidence in every float operation (see each function's own .cpp
// comment for the complete evidence trail, including a transcription-bug
// fix found in ComputeNodeListBounds' own extent-scale constant while
// cross-checking it). Also discovered: since BOTH of this class's real
// retail call sites only ever insert exactly ONE ledge, FUN_0636a860/item
// 3624's entire dense recursive-split body never actually executes in
// practice (only its one-line "list size == 1" base case does), and
// FUN_0636b190/item 3625's N-ledge vertex-dedup-hash-index path is likewise
// provably dead code -- see items 3617/3622/3624/3625/3627/3628's tracker
// notes for the full reasoning. This significantly de-risks a follow-up
// pass on the remaining callees (3622/3625/3630, plus 3624's own trivial
// base case) by letting it target the ALWAYS-TRUE single-ledge path rather
// than the fully general N-ledge merge logic.
//
// [2026-09-09 D101] Completed the class: ported the remaining single-ledge-
// path pieces of the Compile() chain (FinalizeBuildTree/item 3622,
// SplitBuildNodeList's base case/item 3624, CopyLedgeIntoSurface's plain-copy
// branch/item 3627, MergeLedgesIntoSurface/item 3628, BuildCompactSurface's
// non-dedup path/item 3625) and Compile() itself (item 3617), closing out
// this whole multi-lane investigation (D78/D81/D91/D94/D96/D98/D99/D101).
// Re-traced every one of the above instruction-by-instruction rather than
// reusing prior lanes' English summaries, which surfaced two corrections:
//  - Item 3622 (FinalizeBuildTree): D81's "for entries where an
//    ancestor-chain condition holds" push_back gating does NOT exist --
//    re-verified the raw disasm (Common.dll.txt:470834-470853) shows an
//    UNCONDITIONAL walk-and-collect of every m_BuildTreeScratch chain entry.
//    Also confirmed the function's float parameter is genuinely unused (no
//    x87 instruction anywhere in its 66-instruction body), contradicting
//    D81's guess that it "flows into FUN_0636a860".
//  - Item 3622 ALSO unconditionally seeds this->m_SplitNodeCount (+0x8) =
//    this->m_LeafCount (+0x4) right before calling the splitter
//    (Common.dll.txt:470855-470860, `MOV EDX,[EDI+4]; MOV [EDI+8],EDX`) --
//    this CORRECTS D99's assumption (recorded in item 3625's own notes) that
//    m_SplitNodeCount is "provably 0" for the always-true single-ledge case.
//    It is actually provably m_LeafCount (==1), since SplitBuildNodeList's
//    base case never increments it further. Re-tracing BuildCompactSurface()
//    (item 3625) with this correction in hand resolved what its own notes
//    called "not fully re-derived" blob-header math: m_SplitNodeCount turns
//    out to mean "total CompactTreeNode count to reserve room for" (leaves +
//    internal splits), which for a single leaf is exactly the 1 CompactTreeNode
//    CompileNode() writes for it -- a clean, self-consistent explanation the
//    "assume 0" reading did not provide.
// m_VertexWriteCursor/m_VertexDedupBase/m_VertexDedupCount/m_VertexDedupIndex
// (+0x70/+0x74/+0x78/+0x7c, formerly "m_Unknown70[0x10]") are now fully
// identified -- see their own field comments below. The N>1 recursive-split
// (item 3624's general case) and vertex-dedup-hash-merge (item 3627's branch
// B, item 3625's hash-index-construction block, item 3630's mass-property
// post-process) subsystems remain intentionally unported: all three are
// provably unreachable for both of this class's real retail call sites
// (single-ledge inserts only), documented with asserts rather than silently
// risked, per this class's precision warning and the marathon's "port what's
// provable, document the rest" convention.
//
// [2026-09-09 D91] Separately: the small pocket of functions immediately
// BEFORE this class in the address space (0x06369200-0x06369960, decomp-
// tracker items 3594-3598) turned out, after verifying their actual callers,
// to be entirely unrelated to IVP_SurfaceBuilder_Ledge_Soup -- they are
// private support machinery (a byte-hash function, a 4-dword equality
// comparator, and a 3-function recursive octant/candidate-search chain) for
// the Simplex/LCP contact-solver-adjacent class a prior lane (D85) already
// mapped but declined to port (decomp-tracker items 3540-3593). Likewise two
// functions physically inside the 0x0636a190-0x0636be65 scan window (items
// 3635/3636, FUN_0636bce0/FUN_0636bdc0) turned out to belong to the
// already-skipped IVP_SurfaceBuilder_Pointsoup qhull-rewrite cluster (items
// 3632-3634/3637), not this class either.
//
// [2026-09-10 D103] Item 3630 (FUN_0636b6a0, the mass-property header
// post-process D101 left entirely unported) re-examined instruction-by-
// instruction: its header-write/bit-pack SHELL is now ported as
// ApplyMassPropertyHeader() below (also newly resolves the IVP_Compact_Surface
// blob header's first 0x1c bytes: mass_center-shaped centerOfMass[3] at
// +0x0..+0x8, rotation_inertia-shaped rotationInertia[3] at +0xc..+0x14,
// upper_limit_radius-shaped scalar at +0x18, and a 1-byte bucket-
// classification field packed into the low byte of the +0x1c size/flags
// dword BuildCompactSurface() already writes). Still NOT wired into
// Compile() -- its 4 inputs are themselves produced by FUN_0636d650/
// FUN_0635ada0 (items 3659/3312), neither of which is portable yet (one is
// a well-identified-but-out-of-scope Mirtich's-algorithm mass-property
// computation belonging to common_physys.cpp's territory; the other bottoms
// out in a genuinely unidentified tree-node layout, items 3310/3311). See
// ApplyMassPropertyHeader()'s own .cpp comment and Compile()'s declaration
// comment below for the complete evidence trail and current blocker.
class IVP_SurfaceBuilder_Ledge_Soup
{
public:
    IVP_SurfaceBuilder_Ledge_Soup();

    // Appends one ledge to the soup (retail: FUN_0636a050, Common.dll+0x6369e70..a0af).
    // A null ledge is silently dropped -- retail conditionally logs a warning
    // through a shared CRT-internal log-level-gated printf helper first (see
    // ivp_surface_builder_ledge_soup.cpp for why an earlier investigation pass's
    // read of this function was wrong: it does NOT substitute a placeholder
    // ledge on null input, it just returns).
    void InsertLedge(IVP_Compact_Ledge* ledge);

    // Compile() itself is declared further below (after the private
    // implementation details it depends on) -- see that declaration for the
    // full status. [2026-09-09 D101] All 8 originally-scoped direct callees
    // are now ported (FUN_0636a350/a640/b190/b740/b5e0/b6a0/a2b0, decomp-
    // tracker items 3621/3622/3625/3631/3629/3630/3620) except item 3630
    // (deliberately skipped, see Compile()'s own comment) and item 3619
    // (confirmed dead code, correctly left unported). Compile() itself is
    // now ported too.

private:
    // Un-aligned 16-byte point (X,Y,Z,pad) matching retail's un-padded stack
    // layout. Deliberately NOT the alignas(16) IVP_U_Float_Point from
    // ivp_compact_ledge.h: that alignment would force the compiler to pad
    // this whole class's size up to a multiple of 16 (0xa0), breaking the
    // confirmed exact retail size of 0x94 bytes.
    struct RawPoint3f
    {
        float X, Y, Z, Pad;
    };

    // BuildNode: the 0x28-byte heap struct operator_new'd once per inserted
    // ledge (leaf case, FUN_0636a350/item 3621) or once per internal tree
    // split (FUN_0636a860/item 3624), tracked in m_VectorC/m_VectorF and
    // walked recursively by FUN_0636b5e0/item 3629 to compile the final
    // position-independent IVP_Compact_Surface blob. NOT a retail type name
    // -- "BuildNode" is this investigation's own label (see decomp-tracker
    // items 3617/3619-3631 for the full evidence trail); no IVP reference
    // source exists in this port to check against.
    //
    // Field layout confirmed byte-exact by cross-referencing 3 independent
    // consumers:
    //  - FUN_0636a350/item 3621: leaf creation, writes +4/+8/+0xc (center,
    //    via FUN_06347e00), +0x14 (radius, via FUN_06347e60), +0x18/+0x19/
    //    +0x1a (per-axis octant bytes, FLD/FDIV/ftol/INC AL idiom), zeroes
    //    +0x1c/+0x20/+0x24 (leaf: no children yet).
    //  - FUN_0636a720/item 3623 (ported below as ComputeNodeListBounds):
    //    reads +4/+8/+0xc as float center X/Y/Z directly in FP arithmetic
    //    (not just raw dword copies) and +0x14/+0x18/+0x19/+0x1a exactly as
    //    above -- confirms the float typing, not just the dword-copy shape.
    //  - FUN_0636b5e0/item 3629: copies +4/+8/+0xc/+0x14/+0x18..+0x1a
    //    straight into the compiled blob; reads +0x1c as a dual-phase field
    //    (source IVP_Compact_Ledge* until FUN_0636b520/item 3628 overwrites
    //    it in place with a destination byte offset) and +0x20/+0x24 as the
    //    two child-node links (+0x20==0 => leaf).
    //  - FUN_0636a860/item 3624: internal-node allocation confirms the same
    //    0x28-byte operator_new + REP STOSD zero pattern, the same +4/+8/+0xc
    //    center / +0x14 radius / +0x18-1a octant fills, and that +0x20/+0x24
    //    are where the two recursive child subtree results get stored.
    //
    // +0x00 and +0x10 are never read or written by ANY of the ~20 functions
    // read across this class's whole investigation (items 3617-3631) --
    // left as unidentified 4-byte gaps rather than guessed, same convention
    // as the outer class's own m_UnknownXX members below.
    struct BuildNode
    {
        // +0x00: RESOLVED [2026-09-09 D99]. Full raw-disasm re-trace of
        // FUN_0636a350/item 3621 (Common.dll.txt:470690, `MOV [ESI],EBP`)
        // shows this leaf constructor writes its own 1-based loop index
        // (EBP, matching the SAME index used as this node's slot in
        // m_BuildTreeScratch) into BuildNode+0x00 right after zeroing the
        // child links. Not otherwise read by any currently-ported function
        // (ComputeNodeListBounds/CompileNode/CompileTree/FreeBuildState all
        // skip it), so its consumer (if any) is presumably inside the still-
        // unported FUN_0636a860/item 3624. Kept as a plain field rather than
        // folded into m_BuildTreeScratch since BOTH structures redundantly
        // carry it in retail.
        uint32_t ScratchIndex;
        float CenterX;          // +0x04: confirmed float (FUN_0636a720 uses it directly in FADD/FSUB).
        float CenterY;          // +0x08
        float CenterZ;          // +0x0c
        uint32_t m_Unknown10;   // +0x10: never read. SplitBuildNodeList writes it with the 4th (unwritten) dword of its center IVP_U_Point (retail: stack garbage, port: 0).
        float Radius;           // +0x14: confirmed float (FUN_0636a720's FMUL float ptr [EAX+0x14]).
        uint8_t Octant[3];      // +0x18..+0x1a: per-axis octant/bucket-split classification bytes (0..2).
        uint8_t m_Pad1b;        // +0x1b: unused pad byte (FUN_0636b5e0 forces the compiled copy's +0x1b to 0).
        IVP_Compact_Ledge* Ledge; // +0x1c (formerly void* m_LedgeOrDestOffset): the node's ledge -- the source ledge until MergeLedgesIntoSurface (0636b520) repoints it at its copy inside the compiled blob; null on split nodes without a hull.
        BuildNode* Child0;      // +0x20: first child (0 => this is a leaf node).
        BuildNode* Child1;      // +0x24: second child.
    };
    static_assert(sizeof(BuildNode) == 0x28,
                  "must match retail size -- FUN_0636a350/FUN_0636a860 both operator_new(0x28) + REP STOSD (ECX=0xa dwords) this struct");

    // CompileNode() (FUN_0636b5e0) writes one IVP_Compact_Ledgetree_Node
    // (ivp_collision/ivp_compact_surface.h; formerly the local struct
    // CompactTreeNode with the same 0x1c-byte layout) per compiled BuildNode,
    // bump-allocated from m_CompactNodeCursor (+0x80). The tree's readers
    // (0634ac70.., 0634a3f0.., 0634a9b0) use exactly that layout.

    // The leaf ledges of the compiled surface are collected by
    // IVP_Compact_Ledge_Solver::get_all_ledges (FUN_0635abf0 / FUN_0635aba0,
    // ivp_compact_ledge_solver.cpp) into an IVP_SurfaceManager_OutList.

    // FUN_0636d020 builds it (thiscall): zeroes +0x00/+0x04/+0x08/+0x10/+0x14.
    // The volume / first-moment accumulator of FUN_0636d550 / d510 / d360.
    struct FVolumeIntegralAccumulator
    {
        float Moment[3];     // +0x00: sum of 0.25*D*p over every triangle vertex
        float Field_0x0C;    // +0x0c: never written nor read by retail (the port zeroes it)
        float NormalQuadSum; // +0x10: sum of |n|^2
        float Volume6;       // +0x14: sum of D = n . P0 (six times the volume)

        FVolumeIntegralAccumulator()
            : Moment{ 0.0f, 0.0f, 0.0f }
            , Field_0x0C(0.0f)
            , NormalQuadSum(0.0f)
            , Volume6(0.0f)
        {
        }
    };
    static_assert(sizeof(FVolumeIntegralAccumulator) == 0x18, "0x18-byte stack object of FUN_0636d6f0 (local_68)");

    // FUN_0636d000(csys) builds it (thiscall, RET 4): {csys, 0, 0, 0}. The
    // accumulator of FUN_0636d580 / FUN_0636d040.
    struct FAxisIntegralAccumulator
    {
        const IVP_U_Matrix* Csys; // +0x00
        float Mass;               // +0x04
        float SumA;               // +0x08
        float SumB;               // +0x0c

        explicit FAxisIntegralAccumulator(const IVP_U_Matrix* InCsys)
            : Csys(InCsys)
            , Mass(0.0f)
            , SumA(0.0f)
            , SumB(0.0f)
        {
        }
    };

    // BuildTreeScratchEntry: the 8-byte manually-managed doubly-linked-list
    // entry making up m_BuildTreeScratch's raw array (calloc'd, NOT an
    // IVP_U_Vector -- see that field's own comment). NOT a retail type name.
    // Layout confirmed byte-exact by BuildLeafNodes() (FUN_0636a350/item
    // 3621, Common.dll.txt:470690-470698, the entry-linking code) and
    // cross-checked against CompileTree()'s (item 3631) existing raw-pointer
    // read of the same shape. Entry [0] is a header slot: its
    // NextIndexOrCount field holds the CURRENT ROOT entry's index (read by
    // CompileTree()); entries [1..n] are the actual build-tree nodes.
    struct BuildTreeScratchEntry
    {
        int16_t PrevIndex;         // +0x00: previous entry's index in the doubly-linked chain (or the root-slot's own bookkeeping for entry 0).
        int16_t NextIndexOrCount;  // +0x02: next entry's index (0 = end of chain), OR for entry [0] specifically, the current root entry's index.
        BuildNode* Payload;        // +0x04: the BuildNode this entry represents (entries [1..n] only; entry [0]'s payload is never written/read).
    };
    static_assert(sizeof(BuildTreeScratchEntry) == 0x8,
                  "must match retail size -- BuildLeafNodes()/CompileTree() both index this array with a fixed *8 stride");

    void* m_CompactSurface;                    // +0x00: pointer to the built surface. Set somewhere inside the unported Compile() callee chain; read and cleared (ownership transferred to the caller) at the end of Compile(). Not yet typed as IVP_Compact_Surface* -- that class isn't defined anywhere in this port yet.

    // +0x04..+0x13: RESOLVED [2026-09-09 D99] -- all 4 dwords of the former
    // "m_Unknown04[0x10]" gap identified by re-tracing BuildLeafNodes()
    // (FUN_0636a350/item 3621) instruction-by-instruction:
    int32_t m_LeafCount;      // +0x04: m_Ledges.Len() AT BuildLeafNodes() TIME (Common.dll.txt:470527, `MOV [EBX+4],ESI`). This IS the field the still-unported Compile()/item 3617 reads as `*(int*)(this+4) > 1` to decide whether to do a final aligned shrink-copy of m_CompactSurface.
    int32_t m_SplitNodeCount;  // +0x08: internal-split-node counter. Zeroed by the ctor; incremented once per internal tree split inside SplitBuildNodeList() (FUN_0636a860/item 3624, now fully ported -- confirmed both the ==2 and >2 branches each increment this exactly once per new internal BuildNode, Common.dll.txt:471240-471243 and 471832). Read by FUN_0636b190/item 3625 (`MOV EAX,[ESI+8]`) to size the CompactTreeNode array portion of the merged blob's allocation.
    float m_MinRadius;          // +0x0c: running MINIMUM BuildNode::Radius seen across all leaves built so far (Common.dll.txt:470699-470714 -- CORRECTS D81's original item 3621 guess of "largest node radius": the FCOMP/JZ-skip-on-"not less" idiom keeps the SMALLER of the two, not the larger). Sentinel 0.0f (confirmed via DAT_063a53d4's raw bytes: 00 00 00 00) means "not yet initialized"; ctor's whole-object zero-init already produces that sentinel for free.
    int32_t m_LeafByteTotal;     // +0x10: running total of BuildNode allocations, incremented by exactly sizeof(BuildNode)==0x28 per leaf built (Common.dll.txt:470685, `ADD [EBX+0x10],0x28`). Consumer not yet found in any ported function; presumably read inside the still-unported FUN_0636a860/item 3624 or FUN_0636b190/item 3625.

    void* m_BuildTreeScratch;                   // +0x14: raw scratch build-tree array allocated by BuildLeafNodes() (FUN_0636a350/item 3621) via calloc(n+1, sizeof(BuildTreeScratchEntry)) -- forming a manually-managed doubly-linked index over [1..n]. Walked by CompileTree() (item 3631) to find the root entry and freed by FreeBuildState() (item 3620). Confirmed NOT one of the 6 IVP_U_Vector members -- see item 3620's notes.
    IVP_U_Vector<IVP_Compact_Ledge> m_Ledges;   // +0x18: CONFIRMED -- the ledge soup itself, see InsertLedge / decomp-tracker item 3616.
    IVP_U_Vector<BuildNode> m_VectorB;          // +0x20: IVP_U_Vector<BuildNode*> (confirmed). Provably ALWAYS EMPTY in retail -- its only writer (FUN_0636a1e0) is dead code; see header comment above.
    IVP_U_Vector<BuildNode> m_VectorC;          // +0x28: IVP_U_Vector<BuildNode*> (confirmed, see header comment above).
    RawPoint3f m_BBoxMin;                       // +0x30: bounding-box min corner. Ctor-inits to a large positive sentinel (raw bits 0x49742400 per axis). Also reused as a SCRATCH min-corner by FUN_0636a860/item 3624 while splitting sub-lists (NOT the finalized class-wide bbox at that point -- see ComputeNodeListBounds's call sites).
    RawPoint3f m_BBoxMax;                       // +0x40: bounding-box max corner. Ctor-inits to the negated sentinel (raw bits 0xc9742400 per axis) -- classic "inverted bbox" pattern so the first real point encountered replaces both min and max. Same scratch-reuse caveat as m_BBoxMin.
    int32_t m_SplitAxis;                        // +0x50: RESOLVED [2026-09-09 D99] -- the tree-split axis selector: 0=X, 1=Y, 2=Z, chosen as the axis with the LARGEST extent of the finished class-wide bbox (m_BBoxMin/m_BBoxMax), per BuildLeafNodes()'s tail (Common.dll.txt:470742-470789, a 3-way FCOM cascade comparing (max-min) per axis). Only written when m_LeafCount > 1 (leaves this at the ctor's zero-init otherwise); consumer not yet found in any ported function, presumably the still-unported FUN_0636a860/item 3624 (the recursive splitter, which needs to know which axis to partition on).
    uint8_t m_Unknown54[0x4];                   // +0x54..+0x57: unidentified (remainder of the former "m_Unknown50[0x8]" gap, unaffected by this pass's findings).
    uint32_t m_Unknown58;                       // +0x58: unidentified.
    IVP_U_Vector<void> m_VectorD;                // +0x5c: dead/vestigial -- provably never written OR read anywhere in this class (see header comment above); element type unknowable.
    IVP_U_Vector<void> m_VectorE;                // +0x64: dead/vestigial -- same as m_VectorD.
    void* m_CompileOptions;                     // +0x6c: pointer to a caller-supplied 4-dword "compile options" block, read by Compile(). Common.dll's own callers always pass null for it, at which point Compile() defaults it to a local {0,1,0,1} temporary (see FUN_0636a0b0 disasm) -- meaning neither real call site actually exercises a caller-supplied options block.

    // +0x70..+0x7f: RESOLVED [2026-09-09 D101]. Former "m_Unknown70[0x10]"
    // gap, fully identified by tracing BuildCompactSurface() (FUN_0636b190,
    // item 3625) and MergeLedgesIntoSurface() (FUN_0636b520, item 3628)
    // instruction-by-instruction:
    void* m_VertexWriteCursor;   // +0x70: running output-byte cursor into the freshly allocated m_CompactSurface blob's vertex-data region. Seeded by BuildCompactSurface() to (blob+0x30) right before calling MergeLedgesIntoSurface(); read ONCE by that function into a local and never written back (Common.dll.txt:472250 `MOV EDI,[ESI+0x70]` -- no matching store afterwards), so it is stale/unused after that single call in retail.
    void* m_VertexDedupBase;     // +0x74: ONLY meaningful on the N>1 vertex-dedup-hash-index path (this[+0x7c]!=nullptr, provably unreachable in real retail -- see m_VertexDedupIndex below). Zeroed by BuildCompactSurface() whenever the dedup index is NOT constructed (the always-true single-ledge case, Common.dll.txt:471991 `MOV [ESI+0x74],EDX` with EDX=0).
    uint32_t m_VertexDedupCount;  // +0x78: same as m_VertexDedupBase -- dedup-path-only scratch, zeroed alongside it in the always-true case (Common.dll.txt:471998 `MOV [ESI+0x78],EDX`).
    IVP_VHash* m_VertexDedupIndex; // +0x7c: point hash (vtable 063a58f8, 0x10 bytes) built by BuildCompactSurface() when more than one ledge is merged with options[2]==0 and options[3] in {1,2}; deleted (slot 1, flag 1) after MergeLedgesIntoSurface().

    IVP_Compact_Ledgetree_Node* m_CompactNodeCursor; // +0x80: bump-allocator cursor into the IVP_Compact_Surface blob's ledgetree node array. Allocated/initialized by BuildCompactSurface() (item 3625); advanced by one 0x1c-byte node per call inside CompileNode() (item 3629, `MOV EAX,[ESI+0x80]; LEA ... [ESI+0x1c]; MOV [EBX+0x80],EAX`, Common.dll.txt:472347-472349).
    uint8_t m_Unknown84[0x8];                   // +0x84..+0x8b: unidentified.
    IVP_U_Vector<BuildNode> m_VectorF;          // +0x8c: IVP_U_Vector<BuildNode*> (confirmed, see header comment above).

    // Computes the axis-aligned bounding box of a BuildNode* list, writing
    // into CALLER-SUPPLIED outMin/outMax (NOT this object's own m_BBoxMin/
    // m_BBoxMax members, even though FUN_0636a860's own call sites happen to
    // pass those two fields' addresses as scratch -- see the field comments
    // above). Retail: FUN_0636a720 (Common.dll+0x636a720..0x636a85d,
    // decomp-tracker item 3623). [2026-09-11] Only caller in retail is
    // SplitBuildNodeList() (FUN_0636a860/item 3624), now fully ported below.
    static void ComputeNodeListBounds(const IVP_U_Vector<BuildNode>& nodes, float outMin[3], float outMax[3]);

    // `[node + 4 + axis*4]`, read inline by SplitBuildNodeList in retail
    // (not a retail function). CenterX/Y/Z are contiguous at +0x4/+0x8/+0xc.
    static float GetCenterAxis(const BuildNode* node, int axis) { return (&node->CenterX)[axis]; }

    // Computes one IVP_Compact_Ledge's own axis-aligned bounding box by
    // walking every triangle edge's start point (same GetFirstTriangle()/
    // GetFirstEdge()/GetNextEdge()/GetPoint() machinery ivp_compact_ledge.h
    // already exposes). Retail: FUN_0635aa60 (Common.dll+0x635aa60..
    // 0x635ab9e, decomp-tracker item 3308, 110 insns, Common.dll.txt:
    // 444050-444182). Only caller in the currently-ported chain is
    // BuildLeafNodes() below.
    static void ComputeLedgeBounds(const IVP_Compact_Ledge* ledge, float outMin[3], float outMax[3]);

    // Builds one leaf BuildNode per ledge in m_Ledges (center = ledge bbox
    // midpoint, radius = distance from center to the max corner, 3 octant
    // classification bytes), links them all into a fresh m_BuildTreeScratch
    // array, expands the class-wide m_BBoxMin/m_BBoxMax to cover every leaf's
    // bounding sphere, and (when more than one ledge was inserted) picks the
    // longest-extent axis into m_SplitAxis. Retail: FUN_0636a350 (Common.dll+
    // 0x636a350..0x636a638, decomp-tracker item 3621, 259 insns). Compile()'s
    // (item 3617) first callee, called unconditionally once m_Ledges is
    // known non-empty. See the .cpp implementation for the full evidence
    // trail.
    void BuildLeafNodes();

    // Recursively compiles one BuildNode (and, for internal nodes, both its
    // children) into a bump-allocated IVP_Compact_Ledgetree_Node at
    // m_CompactNodeCursor. Retail: FUN_0636b5e0. Returns the node written (the
    // cursor value read on entry, before the bump).
    IVP_Compact_Ledgetree_Node* CompileNode(BuildNode* source);

    // Thin wrapper: looks up the root entry of m_BuildTreeScratch (the entry
    // at the array's own "current count" index) and compiles the whole build
    // tree from it via CompileNode(), then resets the scratch array's
    // count/root-index field to mark the tree consumed. Retail: FUN_0636b740
    // (Common.dll+0x636b740..0x636b767, decomp-tracker item 3631, 14 insns).
    void CompileTree();

    // Final teardown step of Compile(): frees m_VectorC's backing array
    // (its BuildNode elements are owned/freed via m_VectorF, not here --
    // m_VectorC and m_VectorF both hold pointers to the SAME BuildNode
    // objects, see item 3620/3621 notes), free()s every BuildNode referenced
    // by m_VectorF then frees m_VectorF's own backing array, and frees the
    // raw m_BuildTreeScratch array. Retail: FUN_0636a2b0
    // (Common.dll+0x636a2b0..0x636a34b, decomp-tracker item 3620, 53 insns).
    void FreeBuildState();

    // [2026-09-09 D101] Newly ported this pass -- see the .cpp for each
    // function's full instruction-level evidence trail.

    // Collapses the current build tree's flat root-level chain in
    // m_BuildTreeScratch down to a single root BuildNode via
    // SplitBuildNodeList() below, then writes that result back into the
    // scratch array's root slot. Retail: FUN_0636a640 (Common.dll+
    // 0x636a640..0x636a710, decomp-tracker item 3622, 66 insns). Compile()'s
    // SECOND callee (called right after BuildLeafNodes()). The float
    // parameter is confirmed UNUSED by this function's own body (no x87
    // instruction anywhere in it) -- kept only for call-site fidelity with
    // Compile()'s `FUN_0636a640(this, 1.0f)`.
    void FinalizeBuildTree(float unusedThreshold);

    // Reduces a list of BuildNode* down to a single BuildNode*. Retail:
    // FUN_0636a860 (Common.dll+0x636a860..0x636b189, decomp-tracker item
    // 3624, 689 insns -- the largest function in Compile()'s whole
    // dependency chain, and the class's own flagged "highest-risk dense-FP-
    // math function"). [2026-09-11] FULLY ported now -- list.Len()==1 (base
    // case, returns list[0] directly), ==2 (a dedicated trivial split), and
    // the general >2 recursive spatial-median-split (per-axis mean+-epsilon
    // bucket partition with a neighbor-based tie-break cascade, a bbox-
    // volume axis-selection heuristic, and 2 recursive self-calls) are all
    // implemented -- see the .cpp for the complete instruction-level
    // derivation. Still PROVABLY UNREACHABLE in real retail (both of this
    // class's call sites insert exactly one ledge, so this function is only
    // ever invoked with a 1-element list in practice) -- but per the
    // marathon's "no permanent simplification" policy this general path is
    // now a real, verified port rather than a permanent assert.
    BuildNode* SplitBuildNodeList(IVP_U_Vector<BuildNode>& list);

    // Copies one source IVP_Compact_Ledge into the m_CompactSurface blob at
    // destCursor and returns the number of bytes written. Retail FUN_0636b3a0:
    // without a vertex hash it copies the whole ledge ((hdr[+8]>>8)<<4 bytes);
    // with one it copies header+triangles only and welds the points into the
    // shared pool at m_VertexDedupBase.
    uint32_t CopyLedgeIntoSurface(const void* sourceLedge, void* destCursor);

    // Retail FUN_0636a190: depth-first collection of the source ledges below
    // a build node (a node with a ledge is a leaf; otherwise Child0 is
    // recursed and the walk continues with Child1).
    static void CollectNodeLedges(IVP_U_Vector<IVP_Compact_Ledge>& OutLedges, const BuildNode* Node);

    // Retail FUN_0636a1e0: Compile()'s options[0] step - convex hull of every
    // ledge below the root build node, added to m_Ledges, stored as the
    // root's ledge and queued in m_VectorB.
    void BuildRootConvexHull();

    // Walks m_VectorC and m_VectorB, copying/merging each BuildNode's source
    // ledge into the m_CompactSurface blob via CopyLedgeIntoSurface() above
    // (repointing each BuildNode's Ledge at its copy in the blob in the
    // process), optionally freeing each source ledge afterward
    // (gated on m_CompileOptions[1]==1, the real retail default), then clears
    // m_Ledges (ownership of the original inserted-ledge list is fully
    // consumed by this point). Retail: FUN_0636b520 (Common.dll+
    // 0x636b520..0x636b5d2, decomp-tracker item 3628, 75 insns). Both vector
    // walks are ported in full (m_VectorB's loop body is mechanically
    // identical to m_VectorC's, so porting it carries no more risk despite
    // m_VectorB being provably always empty in retail -- see that field's
    // header comment).
    void MergeLedgesIntoSurface();

    // Allocates the combined m_CompactSurface blob (sized from
    // m_SplitNodeCount*sizeof(IVP_Compact_Ledgetree_Node) + the summed byte size of
    // every BuildNode's source ledge, 16-byte aligned), initializes its
    // header fields, calls MergeLedgesIntoSurface() above to copy every
    // ledge's data into it, then sets up m_CompactNodeCursor for
    // CompileTree()/CompileNode() to bump-allocate ledgetree node entries
    // from. Retail: FUN_0636b190 (Common.dll+0x636b190..0x636b378, decomp-
    // tracker item 3625, 173 insns). Compile()'s FOURTH callee. Only the
    // this->m_VertexDedupIndex==nullptr (total node count <= 1) path is
    // ported past the allocation -- the vertex-dedup-hash-index construction
    // block (Common.dll.txt:471942-471989, allocates and vtable-patches a
    // FUN_063522a0/item 3170 hash-map object) is provably unreachable in real
    // retail (see m_VertexDedupIndex's field comment) and is left as a
    // documented, asserted gap rather than porting an otherwise-unneeded
    // hash-table class. Returns false if the blob allocation itself fails
    // (matches retail's early-return-0 path, Common.dll.txt:471929-471938).
    bool BuildCompactSurface();

    // Componentwise min / max, FUN_06348040 / FUN_06348070 (components z, y,
    // x): min copies on less or unordered (TEST AH,0x1), max only on greater
    // (TEST AH,0x41). Only caller: the fallback of FUN_0636d6f0.
    static void ComponentwiseMin3(float inout[3], const float other[3]);
    static void ComponentwiseMax3(float inout[3], const float other[3]);

    // LAB_0636d866, the fallback tail of FUN_0636d6f0 (.text:0636d866-
    // 0636d93d): the combined AABB of every leaf, its midpoint as center of
    // mass and an isotropic inertia from its diagonal. See the .cpp.
    static void ComputeBoundingSphereMassPropertiesFallback(const IVP_SurfaceManager_OutList& leaves,
                                                              float outCenterOfMass[3], float outRotationInertia[3]);

    // FUN_0636d360: one triangle's volume / first-moment contribution.
    static void AccumulateTriangleVolume(FVolumeIntegralAccumulator& Acc, const IVP_Compact_Ledge* Ledge,
                                         const IVP_Compact_Triangle* Triangle);
    // FUN_0636d510: every triangle of one ledge, ascending.
    static void AccumulateLedgeVolume(FVolumeIntegralAccumulator& Acc, const IVP_Compact_Ledge* Ledge);
    // FUN_0636d550: every leaf ledge, descending.
    static void AccumulateVolumeIntegral(FVolumeIntegralAccumulator& Acc, const IVP_SurfaceManager_OutList& Leaves);

    // FUN_0636d040: one triangle's axis-integral contribution (A, B, C an
    // axis permutation). Points go through Acc.Csys->vimult4 (FUN_06348470).
    static void AccumulateTriangleAxisIntegral(FAxisIntegralAccumulator& Acc, const IVP_Compact_Ledge* Ledge,
                                               const IVP_Compact_Triangle* Triangle, int32_t AxisA, int32_t AxisB,
                                               int32_t AxisC);

    // FUN_0636d580 (cdecl): the axis integral of every leaf (descending)
    // in the Csys frame; FUN_0636d6f0 calls it with (0,1,2), (1,2,0), (2,0,1).
    // Mass below 1e-10 (or unordered) gives (0, 0, 1.0f).
    static void AccumulateAxisIntegral(const IVP_SurfaceManager_OutList& Leaves, int32_t AxisA, int32_t AxisB, int32_t AxisC,
                                       const IVP_U_Matrix* Csys, float* OutNormalized1, float* OutRawMass,
                                       float* OutNormalized2);

    // FUN_0636d6f0 (cdecl, leaves, &com, &inertia) -- Mirtich's polyhedral
    // mass properties over every leaf ledge:
    //  1. local_50 IVP_U_Matrix = identity (FUN_063481b0); accumulator
    //     (FUN_0636d020); FUN_0636d550.
    //  2. Mirtich path only when Volume6 > sqrt(NormalQuadSum)^3 * DAT_063a5914;
    //     otherwise the fallback (returns 0).
    //  3. Identity again; com = acc.Moment * float(1 / Volume6) (FUN_0628be10)
    //     -> *outCenterOfMass and Csys.vv.
    //  4. FUN_0636d580 with (0,1,2) / (1,2,0) / (2,0,1); inertia =
    //     (sqrt(B3^2+B2^2), sqrt(B3^2+B1^2), sqrt(B1^2+B2^2)); returns 1.
    static bool ComputeMassProperties(const IVP_SurfaceManager_OutList& leaves,
                                       float outCenterOfMass[3], float outRotationInertia[3]);

    // Retail FUN_0636d650: leaf collection (FUN_0635abf0) + FUN_0636d6f0.
    static bool ComputeSurfaceMassProperties(const void* CompiledSurface,
                                              float outCenterOfMass[3], float outRotationInertia[3]);

    // FUN_0635ac10 (cdecl, leaf, &com, &upper, &aux): folds the farthest
    // vertex distance from com into *upper and, when the aux pointer is
    // non-null, the largest distance of a vertex from the line com + t*N of
    // its triangle (N = FUN_0635b940) into *aux.
    static void AccumulateLeafLedgeRadii(const IVP_Compact_Ledge* leaf, const float centerOfMass[3],
                                          float* upperLimitRadiusAccum, float* auxRadiusMetricAccum);

    // FUN_0635ada0 (cdecl, surface, &com, &upper, &aux): zeroes both
    // outputs, collects the leaves (get_all_ledges into a {0x400} big
    // vector) and runs FUN_0635ac10 on each, descending. com is input only.
    static void ComputeSurfaceRadii(const void* CompiledSurface, const float centerOfMass[3],
                                     float* outUpperLimitRadius, float* outAuxRadiusMetric);

    // FUN_0636b6a0: FUN_0636d650 + FUN_0635ada0, then com / inertia / upper
    // into the compiled surface header and a classification byte into the
    // low byte of +0x1c. See the .cpp.
    void ApplyMassPropertyHeader();

public:
    // Builds and returns the combined compact surface from every ledge
    // inserted so far (nullptr if InsertLedge() was never called with a
    // non-null ledge). Retail: FUN_0636a0b0 (Common.dll+0x6636a0b0..
    // 0x6636a189, decomp-tracker item 3617). Ownership of the returned
    // pointer transfers to the caller (this->m_CompactSurface is cleared to
    // nullptr before returning).
    //
    // Order (.text:0636a0b0-0636a189): BuildLeafNodes, FinalizeBuildTree,
    // [BuildRootConvexHull], BuildCompactSurface, CompileTree,
    // ApplyMassPropertyHeader, FreeBuildState, optional shrink-copy.
    void* Compile(const void* options = nullptr);
};
static_assert(sizeof(IVP_SurfaceBuilder_Ledge_Soup) == 0x94,
              "must match retail object size -- Common.dll+0x6369e70's ctor zeroes exactly 0x94 bytes (REP STOSD, ECX=0x25 dwords)");
