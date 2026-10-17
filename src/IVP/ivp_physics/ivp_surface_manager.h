// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstddef>
#include <cstdint>

// ============================================================================
// IVP_SurfaceManager -- the abstract "what surface/ledge geometry does this
// object present to the collision detector?" interface, plus this module's
// one concrete default implementation and its single static instance.
//
// D-claude-common-ivp pass (2026-09-17). Created to close class-shape defect
// (3c) reported by the vtable-identification lane: before this pass
// IVP_SurfaceManager existed ONLY as a forward declaration in
// ivp_real_object.h, so IVP_Real_Object::surface_manager (+0x80) was an
// opaque pointer to nothing and the object_type==3 constructor's
// `surface_manager = &DAT_0658afe0` write had nowhere to point.
//
// GROUND TRUTH: EXE/Disasm/Common.dll.txt only. Every claim below cites the
// raw listing address it came from.
//
// ----------------------------------------------------------------------------
// VTABLE GEOMETRY (both tables independently byte-extent-checked, per this
// project's standing "vtable byte_size is routinely overcounted by one slot"
// caution -- the count below is derived from the raw .rdata extent between
// consecutive PTR_ labels, NOT from a tool's byte_size field):
//
//   Abstract base   .rdata:063a56ac .. 063a56d4 inclusive = 0x28 bytes
//                   = 11 slots. Next label PTR_LAB_063a56d8 starts a
//                   different table, so 11 is exact (Common.dll.txt:569185-
//                   569197).
//   Default derived .rdata:063a5830 .. 063a5858 inclusive = 0x28 bytes
//                   = 11 slots. Next label PTR_FUN_063a585c starts
//                   IVP_Ball's table, so 11 is exact (Common.dll.txt:569368-
//                   569380). This matches the "11 slots" the vtable lane
//                   reported.
//
// The two tables line up slot-for-slot, which is what proves the
// base/derived relationship:
//
//   slot  base (063a56ac)              derived (063a5830)
//   ----  --------------------------   --------------------------------------
//    0    LAB_06391fb0 (_purecall)     062d1ad0 "TractorArm::GetMode" (ICF)
//    1    LAB_06391fb0 (_purecall)     063623d0 "CAsteroid::get_velocity" (ICF)
//    2    LAB_06391fb0 (_purecall)     LAB_063623f0
//    3    LAB_06391fb0 (_purecall)     LAB_0634dc00
//    4    LAB_06391fb0 (_purecall)     LAB_06362400
//    5    LAB_06391fb0 (_purecall)     FUN_06362460
//    6    LAB_06391fb0 (_purecall)     LAB_063624a0
//    7    062ed730                     062ed730   <-- SAME address in both
//    8    062ed730                     062ed730   <-- SAME address in both
//    9    LAB_06391fb0 (_purecall)     FUN_063624c0 (scalar-deleting dtor)
//   10    LAB_06391fb0 (_purecall)     063624b0 "Archetype::EqObj::get_class_type" (ICF)
//
// LAB_06391fb0 is _purecall (this project's standing invariant -- it is NOT
// a no-op stub), so 9 of the base's 11 slots are genuinely pure virtual.
// Slots 7 and 8 carry a real, non-pure body (062ed730 = a bare `RET 4`,
// i.e. a one-argument do-nothing default) and the derived table repeats
// that SAME address, which is exactly what an inherited, non-overridden
// virtual looks like.
//
// THE ICF CAVEAT: slots 0, 1 and 10 of the derived table carry names
// belonging to completely unrelated Freelancer classes (TractorArm,
// CAsteroid, Archetype::EqObj). That is /OPT:ICF folding identical function
// bodies together, exactly as this project's standing caution describes --
// those names say nothing about the receiver. The bodies are what matter
// and they are trivial:
//   slot 0  `MOV EAX,[this+4]; RET`            (Common.dll.txt:209992-209993)
//   slot 1  zeroes 3 floats through the hidden
//           return pointer and returns it      (Common.dll.txt:063623d0-063623de)
//   slot 10 `MOV EAX,1; RET`                   (Common.dll.txt:063624b0-063624b5)
// Ownership of the derived table is established the way this project
// requires -- by the constructor that stores it: FUN_06362370 writes
// PTR_GetMode_063a5830 into [ESI] where ESI is &DAT_0658afe0
// (Common.dll.txt:06362360-0636239a).
//
// ----------------------------------------------------------------------------
// THE "CORE" NULL-STORE IDIOM: four of the derived slots begin with, or
// consist entirely of, the ten-byte instruction
//     c7 05 00 00 00 00 00 00 00 00   MOV dword ptr [0x00000000],0
// (Ghidra renders the target as DAT_00000000). That is a deliberate store to
// address zero -- an intentional crash marker compiled into retail, not a
// decompiler artifact and not a no-op. It appears at Common.dll.txt:
// 063623f0 (slot 2), 0634dc00 (slot 3), 06362465 (slot 5, followed by real
// work) and 063624a0 (slot 6). This port reproduces it as a real null store,
// IVP_CoreTrap (ivp_utility/ivp_core_trap.h); see ivp_real_object.cpp.
//
// ----------------------------------------------------------------------------
// SLOT SEMANTICS: NOT identified. Per this project's placeholder policy the
// slots below carry SlotN_ names with their confirmed argument count (from
// each body's own `RET n`) and confirmed return shape, and nothing more.
// TODO: name these once a caller outside this file is traced -- retail
// dispatches through them from the collision-detection side, which this
// lane does not own.
// ============================================================================

#pragma pack(push, 4)

// Growable pointer array used by slots 4 and 5 below. NOT IVP_U_Vector_Base
// (which is the 16-bit-header flavour at IVP_Environment+0x124): this one has
// 32-bit fields -- capacity at +0x00, count at +0x04, elems at +0x08 -- read
// that way by both slot bodies (Common.dll.txt:0636243f-06362458 and
// 06362476-0636248f) and grown by FUN_0635d3e0 (Common.dll.txt:448646-448697),
// which sets the new capacity to 2*old+1 and allocates newCapacity*4+4 bytes
// (`LEA ECX,[EAX*8+8]` from the OLD capacity, the same number), copying the
// live count across.
// Left opaque here: the element type is caller-defined and no caller has been
// traced from this lane.
struct IVP_SurfaceManager_OutList
{
    int32_t Capacity; // +0x00
    int32_t Count;    // +0x04
    void**  Elems;    // +0x08
};
static_assert(offsetof(IVP_SurfaceManager_OutList, Capacity) == 0x00, "IVP_SurfaceManager_OutList+0x00");
static_assert(offsetof(IVP_SurfaceManager_OutList, Count) == 0x04, "IVP_SurfaceManager_OutList+0x04");
static_assert(offsetof(IVP_SurfaceManager_OutList, Elems) == 0x08, "IVP_SurfaceManager_OutList+0x08");

// Abstract base. vtable .rdata:063a56ac, 11 slots, 9 of them _purecall.
// sizeof == 4 (vptr only): the base adds no data members -- FUN_0635d440,
// its own destructor body, writes nothing but the vptr
// (Common.dll.txt:0635d440-0635d446), and the one derived class's
// constructor writes its own first field at +0x04 with no gap.
class IVP_SurfaceManager
{
public:
    // Slot names: the Ipion IVP_SurfaceManager API, matched slot-for-slot against the bodies
    // of IVP_SurfaceManager_Polygon (vtable 0639c2dc, RTTI-proven; see ivp_surman_polygon.h).
    // Slot 0 -- `RET` (no stack arguments), returns a pointer (the single convex ledge).
    virtual void* get_single_convex() = 0;

    // Slot 1 -- `RET 4`: one stack argument, which is the hidden
    // return-by-value pointer for a 3-float vector (the ICF-folded body
    // zeroes [out+0], [out+4], [out+8] and returns `out` in EAX).
    virtual float* get_mass_center(float* OutVec3) = 0;

    // Slot 2 -- `RET 0xc`: three stack arguments. "Unimplemented" describes
    // the DEFAULT implementation only (it traps). A different, still
    // unidentified surface-manager adapter -- vtable PTR_LAB_0639c2dc, the
    // one reached on the object_type==2 path -- has a real body here, called
    // as (const IVP_U_Point* In, float* OutA, float* OutB); see
    // IVP_InitCoreRestStateAndDamping in ivp_real_object.cpp. Same three
    // pointer-sized arguments, so the slot shape agrees; the parameter types
    // here stay deliberately generic because no single signature covers both
    // implementations' proven usage.
    virtual void get_radius_and_radius_dev_to_given_center(void* Param1, void* Param2, void* Param3) = 0;

    // Slot 3 -- `RET 4`: one stack argument. Same caveat as slot 2: the
    // object_type==2 adapter has a real body here, called as
    // (IVP_U_Point* Out).
    virtual void get_rotation_inertia(void* Param1) = 0;

    // Slot 4 -- `RET 0x18`: six stack arguments. The default implementation
    // is a sphere-overlap collect (see IVP_SurfaceManager_Default).
    virtual void get_all_ledges_within_radius(const float* Center3, float Radius, void* Param3,
                                              void* Param4, void* Param5,
                                              IVP_SurfaceManager_OutList* OutList) = 0;

    // Slot 5 -- `RET 4`: one stack argument.
    virtual void get_all_terminal_ledges(IVP_SurfaceManager_OutList* OutList) = 0;

    // Slot 6 -- `RET 8`: two stack arguments.
    virtual void insert_all_ledges_hitting_ray(void* Param1, void* Param2) = 0;

    // Slots 7 and 8 -- `RET 4` each, one stack argument, body at 062ed730 is
    // a bare return. NOT pure: both tables carry this same real address, so
    // this is a base-provided default that the default implementation
    // inherits unchanged. Deliberately non-virtual-pure and left empty here.
    virtual void add_reference_to_ledge(void* Param1);
    virtual void remove_reference_to_ledge(void* Param1);

    // Slot 9 -- the destructor. Pure in the base (its slot is _purecall) yet
    // it still has a real out-of-line body, FUN_0635d440, which resets the
    // vptr and returns. That combination is precisely what MSVC emits for a
    // pure virtual destructor with a definition, so it is modeled as one.
    virtual ~IVP_SurfaceManager() = 0;

    // Slot 10 -- `RET` (no stack arguments), returns int. The default
    // implementation returns the constant 1.
    virtual int32_t get_type() = 0;
};

// The module's one concrete implementation. vtable .rdata:063a5830.
// Constructor FUN_06362370 (Common.dll.txt:456769-456808), destructor
// FUN_063624e0 (Common.dll.txt:456984-457010), scalar-deleting wrapper
// FUN_063624c0 (Common.dll.txt:456962-456973).
//
// NAMING: "_Default" is descriptive of the one provable fact -- this is the
// implementation behind the module's single static instance, which the
// object_type==3 constructor installs unconditionally. No retail symbol name
// survives for it (slots 0/1/10 carry ICF-borrowed names from unrelated
// classes; see the ICF caveat above). TODO: rename if a real symbol turns up.
class IVP_SurfaceManager_Default : public IVP_SurfaceManager
{
public:
    IVP_SurfaceManager_Default();
    ~IVP_SurfaceManager_Default() override;

    void*   get_single_convex() override;
    float*  get_mass_center(float* OutVec3) override;
    void    get_radius_and_radius_dev_to_given_center(void* Param1, void* Param2, void* Param3) override;
    void    get_rotation_inertia(void* Param1) override;
    void    get_all_ledges_within_radius(const float* Center3, float Radius, void* Param3,
                                         void* Param4, void* Param5,
                                         IVP_SurfaceManager_OutList* OutList) override;
    void    get_all_terminal_ledges(IVP_SurfaceManager_OutList* OutList) override;
    void    insert_all_ledges_hitting_ray(void* Param1, void* Param2) override;
    int32_t get_type() override;

    // +0x04. A 0x20-byte, 16-byte-aligned block allocated by the
    // constructor through FUN_06343fc0(0x20, 0x10) -- the module's aligned
    // allocator, which over-allocates by size+align+8, stores the magic
    // 0x65981234 at the raw base and the raw base pointer at
    // [aligned-4] (Common.dll.txt:404944-404970) -- and then zero-filled
    // with `XOR EAX,EAX; MOV ECX,8; REP STOSD`
    // (Common.dll.txt:063623ad-063623b4). Freed by the destructor through
    // FUN_06343ff0 (Common.dll.txt:06362513). Its 32 bytes are never
    // written again anywhere in this module; slot 0 hands the raw pointer
    // out and slots 4/5 append it to a caller-supplied list. Contents
    // therefore stay opaque -- modeled as a plain owned byte block, per
    // this project's placeholder policy.
    void* GeometryBlock;
};
static_assert(sizeof(IVP_SurfaceManager) == 0x04, "IVP_SurfaceManager size mismatch (vptr only)");
static_assert(sizeof(IVP_SurfaceManager_Default) == 0x08, "IVP_SurfaceManager_Default size mismatch");

// Size of the block IVP_SurfaceManager_Default::GeometryBlock points at, and
// the alignment it is allocated with -- the literal `PUSH 0x10; PUSH 0x20`
// pair at Common.dll.txt:0636238e-06362390.
constexpr std::size_t kIVPSurfaceManagerGeometryBlockSize  = 0x20;
constexpr std::size_t kIVPSurfaceManagerGeometryBlockAlign = 0x10;

// DAT_0658afe0 (Common.dll.txt:2283120, .data, 3 XREFs: 06362360, 06362560,
// 063625c4). The module's single static IVP_SurfaceManager_Default.
//
// CORRECTION to the vtable-identification lane's report, which described it
// as "returned by IVP_GetDefaultSurfaceManager (FUN_06362370)": FUN_06362370
// is not a getter and returns nothing meaningful to a caller -- it is the
// CONSTRUCTOR. It is reached only from FUN_06362360, a two-instruction thunk
// that loads ECX = &DAT_0658afe0 and tail-jumps into it
// (Common.dll.txt:06362360-06362365), which in turn is reached only from
// LAB_06362350, a CRT static-initializer entry (referenced from the .rdata
// initializer table at 063ea610) that calls the constructor and then
// tail-jumps to FUN_06362550 to register the matching atexit teardown:
// FUN_06362550 pushes LAB_06362560 (ECX = &DAT_0658afe0; jump
// FUN_063624e0 = the destructor) into the module's atexit chain
// (Common.dll.txt:06362550-06362565). So this is an ordinary file-scope
// static object with static-init/atexit-teardown, and there is no getter
// function at all -- retail's one consumer takes its address directly
// (`MOV dword ptr [ESI+0x80], DAT_0658afe0` at Common.dll.txt:063625c4,
// inside IVP_Ball's constructor).
extern IVP_SurfaceManager_Default g_IVP_DefaultSurfaceManager_0658afe0;

#pragma pack(pop)
