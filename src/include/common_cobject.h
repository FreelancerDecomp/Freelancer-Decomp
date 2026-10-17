// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// Common.dll CObject / CSimple / CEqObj / CShip — layouts from Common.dll.c ctors.
// NOT placeholder sizeof in Common.dll.h. Linked as Common.lib.
// See docs/re/common_physys.md.

#include "common_archetype.h"
#include "common_cequip.h"
#include "common_csys.h"
#include "common_goods.h"   // BaseWatcher (CGuided::TargetWatch)
#include "common_misc.h"    // DamageEntry::SubObjFate (CArchGroup::Kill)
#include "common_physys.h"
#include "common_reputation.h" // Costume (CEqObj::SpaceCostume)
#include "common_cheapset.h" // CheapSet<CObject*> (CObject::get_intruder_set_r)
#include "dacom.h"

// Forward declarations for CShip's watcher members and its atmosphere pointer
struct IObjRW;
struct CSolar;
class IBehaviorManager;
class IVP_SurfaceManager;           // CObject::SurfaceManager (+0x5c)
class IVP_Real_Object;              // CObject::BodyPtr (+0x54)
class IVP_Controller;               // CObject::GolemController (+0x60)

#include <cmath>
#include <cstddef>
#include <list>
#include <map>
#include <set>
#include <vector>

struct IObjDB;

// One CObject::PartMap element (0xc bytes, stride read off part_to_inst .text:0627d1e0 and
// FUN_0628f700): the part id (hardpoint CRC), its engine instance, and the is_shield_part
// flag in the low byte of the third word (is_shield_part reads, flag_part_as_shield writes
// only that byte).
struct FPartMapEntry
{
    unsigned PartId;                    // +0x00
    long EngineInstanceId;              // +0x04
    unsigned char bShieldPart;          // +0x08 (+0x09..+0x0b: alignment, no reader)
};
static_assert(sizeof(FPartMapEntry) == 0xc, "FPartMapEntry is 0xc bytes");

namespace Archetype
{
    struct Root;
    struct Ship;
}

// Retail EngineObject vtable @ .rdata 0002:000025BC. Slots 0–13 are IEngineInstance
// stdcall (same layout as EngBase FInstanceTransform). IEngine+0xa0 = slot 8
// get_transform → this+8 (3×3 then pos). CObject thiscall open starts at slot 19.
struct IEngineInstance
{
    virtual void DACOM_API initialize_instance(long Inst) = 0;                         // +0x00
    virtual void DACOM_API create_instance(long Inst) = 0;                             // +0x04 RET
    virtual void DACOM_API destroy_instance(long Inst) = 0;                            // +0x08
    virtual void DACOM_API set_position(long Inst, Vector const& Pos) = 0;             // +0x0C
    virtual Vector const& DACOM_API get_position(long Inst) const = 0;                 // +0x10
    virtual void DACOM_API set_orientation(long Inst, Matrix const& Orient) = 0;       // +0x14
    virtual Matrix const& DACOM_API get_orientation(long Inst) const = 0;              // +0x18
    virtual void DACOM_API set_transform(long Inst, Transform const& Xform) = 0;       // +0x1C
    virtual Transform const& DACOM_API get_transform(long Inst) const = 0;             // +0x20 IEngine+0xa0
    virtual void DACOM_API get_centered_radius(long Inst, float* OutRadius,
                                               Vector* OutCenter) const = 0;           // +0x24
    virtual void DACOM_API set_centered_radius(long Inst, float Radius,
                                               Vector const& Center) = 0;              // +0x28
    virtual void DACOM_API set_instance_flags(long Inst, unsigned long Flags) = 0;     // +0x2C
    virtual unsigned long DACOM_API get_instance_flags(long Inst) const = 0;           // +0x30
    virtual bool DACOM_API joint_changed(long Inst) = 0;                               // +0x34 false
};

// EngineObject ctor 0xa98a0. Prefix of every CObject. sizeof 0x4c. Retail spells it a struct
// (??4EngineObject@@QAEAAU0@ABU0@@Z) and keeps every IEngineInstance override private
// (?initialize_instance@EngineObject@@EAGXJ@Z -- E = private virtual): the engine reaches them
// through IEngineInstance only.
struct COMMON_API EngineObject : public IEngineInstance
{
public:
    EngineObject();
    virtual ~EngineObject();

private:
    virtual void DACOM_API initialize_instance(long Inst);
    virtual void DACOM_API create_instance(long Inst);
    virtual void DACOM_API destroy_instance(long Inst);
    virtual void DACOM_API set_position(long Inst, Vector const& Pos);
    virtual Vector const& DACOM_API get_position(long Inst) const;
    virtual void DACOM_API set_orientation(long Inst, Matrix const& Orient);
    virtual Matrix const& DACOM_API get_orientation(long Inst) const;
    virtual void DACOM_API set_transform(long Inst, Transform const& Xform);
    virtual Transform const& DACOM_API get_transform(long Inst) const;
    virtual void DACOM_API get_centered_radius(long Inst, float* OutRadius,
                                               Vector* OutCenter) const;
    virtual void DACOM_API set_centered_radius(long Inst, float NewRadius,
                                               Vector const& Center);
    virtual void DACOM_API set_instance_flags(long Inst, unsigned long Flags);
    virtual unsigned long DACOM_API get_instance_flags(long Inst) const;
    virtual bool DACOM_API joint_changed(long Inst);
    // Listing 0xa99b0 / 0xa99f0. Base-level defaults: always the zero vector (retail backs
    // these with a lazily-constructed function-local static; a plain static works here since
    // Vector is POD). Distinct overload from CObject::get_velocity()/get_angular_velocity()
    // below (no Inst arg, real IVP-backed) -- that pair hides these two by name in CObject's
    // scope, which matches how each is actually called (engine-style Inst dispatch vs. game
    // logic's no-arg convenience call), so no functional collision.
    // Retail vftable 0x0639b5bc: +0x38 deleting dtor, +0x3c get_velocity, +0x40 set_velocity,
    // +0x44 get_angular_velocity, +0x48 set_angular_velocity -- 19 slots, so CObject's own
    // first new virtual (open) lands at +0x4c. Both setters are the shared empty body
    // .text:062a8910 (`RET 0xc`).
    virtual Vector const& DACOM_API get_velocity(long Inst) const;
    virtual void DACOM_API set_velocity(long Inst, Vector const& Vel);
    virtual Vector const& DACOM_API get_angular_velocity(long Inst) const;
    virtual void DACOM_API set_angular_velocity(long Inst, Vector const& AngVel);

public:
    // Thiscall listing 0x1cb10 / 0x1cb30 / 0x1cb50 / 0x1caf0 / 0x1cb70.
    void set_position(Vector const& Pos);
    void set_orientation(Matrix const& Orient);
    void set_transform(Transform const& Xform);
    Transform const& get_transform() const;
    // .text:06263b80 (ICF with get_transform): LEA EAX,[this+0x8].
    Matrix const& get_orientation() const;
    // .text:06356500: LEA EAX,[this+0x2c].
    Vector const& get_position() const;
    // .text:062d1ad0: MOV EAX,[this+0x4].
    long get_index() const;
    // Retail returns a const-qualified float: ?get_radius@EngineObject@@QBE?BMXZ.
    float const get_radius() const;
    void update_tree() const;

    int EngineInstanceId; // +0x04  FEngineInstance* from initialize_instance
    Matrix Orientation;   // +0x08  identity — IEngine+0xa0 / +0x98
    Vector WorldPosition; // +0x2c
    float Radius;         // +0x38
    Vector RadiusCenter;  // +0x3c  get/set_centered_radius (formerly ExtentsMin)
    unsigned long InstanceFlags; // +0x48  get/set_instance_flags 06309a90/06309aa0 (formerly Pad48)

private:
    // Listing 0x1cb80. Retail dispatches through this object's own vtable slot +0x28
    // (this->set_centered_radius(EngineInstanceId, NewRadius, Center)) rather than
    // writing Radius/RadiusCenter directly -- reproduced here as a direct virtual call,
    // which is exactly what that vtable-slot indirection resolves to at this level.
    void set_centered_radius(float NewRadius, Vector const& Center);
};

static_assert(sizeof(EngineObject) == 0x4c, "EngineObject sizeof 0x4c");
static_assert(offsetof(EngineObject, EngineInstanceId) == 0x04, "EngineObject+0x04 instance");
static_assert(offsetof(EngineObject, Orientation) == 0x08, "EngineObject+0x08 matrix");
static_assert(offsetof(EngineObject, WorldPosition) == 0x2c, "EngineObject+0x2c position");
static_assert(offsetof(EngineObject, Radius) == 0x38, "EngineObject+0x38 radius");
static_assert(offsetof(EngineObject, Orientation) + sizeof(Matrix) ==
                  offsetof(EngineObject, WorldPosition),
              "Orientation+WorldPosition overlay Transform");

struct COMMON_API CObject : public EngineObject
{
public:
    // Alloc switch (Common.dll.c 0x4ee50). All 11 classes constructed.
    enum Class
    {
        ClassObject = 1,
        // 0x03 / 0x103: the default arguments retail's `default constructor closure's push
        // for CSimple (.text:06281440 PUSH 0x3) and CEqObj (.text:06287600 PUSH 0x103). No
        // retail object is allocated with either value (both classes are abstract).
        ClassSimple = 0x03,
        ClassEqObj = 0x103,
        ClassMine = 0x0f,
        ClassCounterMeasure = 0x17,
        ClassBeam = 0x67,
        ClassGuided = 0xa7,
        ClassSolar = 0x303,
        ClassShip = 0x503,
        ClassLoot = 0x803,
        ClassAsteroid = 0x1003,
        ClassEquipmentObj = 0x2001,
        ClassDynamicAsteroid = 0x4001
    };

    // Flag bits for update(float, unsigned) below. Retail exports each one as its own
    // `public: static unsigned int const CObject::<NAME>` in .rdata at 0x0639e524..0x0639e53c
    // (ordinals 2068, 1853, 1854, 1851, 1850, 1852, 2069), so they are part of Common.dll's
    // public surface and other modules link against them by name -- they are definitions,
    // not just an enum. Values read straight off those .rdata dwords; UPDATE_DUMB == 0x3d is
    // exactly NO_DYNAMICS|NO_SCANNER|NO_BEHAVIOR|NO_ACTIONS|NO_COUNTERMEASURE_AI, which is
    // the arithmetic check that the individual bit values are right. Bit 0x02 has no named
    // constant anywhere in this module (open TODO: find who owns it).
    static unsigned int const UPDATE_DEFAULT;        // 0x0639e524 = 0x00
    static unsigned int const NO_DYNAMICS;           // 0x0639e528 = 0x01, read at 062960aa/062b0e75
    static unsigned int const NO_SCANNER;            // 0x0639e52c = 0x04, read at 0629f719
    static unsigned int const NO_BEHAVIOR;           // 0x0639e530 = 0x08, read at 062aa5b0
    static unsigned int const NO_ACTIONS;            // 0x0639e534 = 0x10, read at 062b0e3a
    static unsigned int const NO_COUNTERMEASURE_AI;  // 0x0639e538 = 0x20, read at 0629a33a
    static unsigned int const UPDATE_DUMB;           // 0x0639e53c = 0x3d

protected:
    // Default argument: retail's CObject::`default constructor closure' (.text:0627d040) is
    // `PUSH 0x1 / CALL CObject::CObject / RET`. The ctor itself is protected in retail
    // (??0CObject@@IAE@W4Class@0@@Z); CObject::Alloc, a static member, builds the bare
    // ClassObject case.
    explicit CObject(Class ObjectClass = ClassObject);

public:
    // Common.dll.c 40405-40505 (0x211b0): retail never emits a standalone CObject copy
    // ctor symbol -- this field-copying prologue is inlined at the top of every non-trivial
    // CSimple-family copy ctor (CSimple::CSimple(CSimple const&) itself, tracker id 10557,
    // is the cleanest exemplar; CProjectile/CMunition/CBeam/CEqObj/CLoot/CCounterMeasure/
    // CMine/CAsteroid's own copy ctors repeat the identical byte pattern). Given as a real
    // out-of-line ctor here so CSimple(Other) below can delegate to it via the member-init
    // list, matching the CArchGroupManager(CArchGroupManager const&) precedent (this file,
    // "Groups(new std::map<...>(*Other.Groups))").
    CObject(CObject const& Other);

protected:
    // Retail: ??1CObject@@MAE@XZ (M = protected virtual). Objects die through Release().
    virtual ~CObject();

public:
    // Listing 0x4f610 stdcall. Clears this+4; refcount==1 → dtor slot +0x38 (delete this).
    // Not FindChild / IVP. EngBase FreeOwnedTransform CallRelease still 1-arg.
    virtual void DACOM_API destroy_instance(long Inst);

    static CObject* __cdecl Alloc(Class ObjectClass);
    static int __cdecl Count(Class ObjectClass);
    static CObject* __cdecl Find(long EngineInstanceId, Class ObjectClass);
    static CObject* __cdecl Find(unsigned const& Id, Class ObjectClass);
    static CObject* __cdecl FindFirst(Class ObjectClass);
    static CObject* __cdecl FindNext();
    // Listing 0x4ef00 (Common.dll.c 82115-82136). Retail never actually releases/deletes
    // any live CObject here -- see the .cpp definition for why this is a documented no-op.
    static void __cdecl ReleaseAll();

    virtual void open(Archetype::Root* Arch);
    void enable_collisions_r(bool bEnable);
    virtual int update(float Dt, unsigned Flags);
    // Listing 0x1d120. Vcall +0x54 from CELauncher::ComputeLaunchInfo. IVP body @ +0x54.
    virtual Vector get_velocity() const;
    // Listing 0x1d150. Returns angular velocity from IVP body or zero. Retail vtable+0x58,
    // immediately after get_velocity (+0x54) -- confirmed via this port's own named vtable
    // dump (??_7CObject@@6B@, Common.dll.txt 557774+) and by common_iobjinspectimpl.cpp's
    // IObjInspectImpl::get_angular_velocity comment ("vcall delegate vtable+0x58"). Previously
    // mis-declared after get_mass/get_surface_extents below; moved here to match retail.
    virtual Vector get_angular_velocity() const;
    // Retail vtable+0x5c / +0x60 (??_7CObject@@6B@ 0x0639b6c8 / 0x0639b6cc). At this level
    // both slots are the single empty body at .text:062a8920 (`RET`) exported under both
    // names; CEqObj's vftable (0x0639beb8 / 0x0639bebc) names them, disable first.
    // unmake_physical() calls +0x5c before PhySys::UnMakePhysicalR, remake_physical() calls
    // +0x60 after PhySys::ReMakePhysicalR (.text:062ad50a CALL [EDX+0x60]).
    virtual void disable_controllers();
    virtual void enable_controllers();
    virtual float get_physical_radius_r(Vector& OutCenter) const;
    // Listing 0x1cdd0 / 0x1cdf0. Non-virtual public aliases that dispatch through the same
    // vtable slot (+0x64) as get_physical_radius_r -- i.e. just call it. The no-arg overload
    // discards the computed center into a throwaway local.
    float hierarchy_radius() const;
    float hierarchy_radius(Vector& OutCenter) const;
    // .text:062ad7c0 ?get_intruder_set_r@CObject@@QAEXAAU?$CheapSet@PAUCObject@@U?$less@PAUCObject@@@std@@@@@Z.
    // Collects this object and every CObject in its engine hierarchy, then adds the phantom
    // intruders' roots of each one whose collision state is on.
    void get_intruder_set_r(CheapSet<CObject*, std::less<CObject*>>& Out);
    virtual Vector get_center_of_mass() const;
    // Listing 0x4da60. Returns mass from IVP body or archetype.
    virtual float get_mass() const;
    // Common.dll.c 80694-80714 (0x4d470). Retail vtable+0x70, immediately after get_mass
    // (+0x6c) and before unmake_physical (+0x74) -- confirmed via this port's own named
    // vtable dump (??_7CObject@@6B@, Common.dll.txt 557774+). Reads the two Vectors at
    // PhyPart+8/+0x14 into OutMin/OutMax; false if PhyPart (+0x58) is null.
    virtual bool get_surface_extents(Vector& OutMin, Vector& OutMax) const;
    virtual void unmake_physical();
    // Listing 0x4d4f0 (Common.dll.c 80743-80750): PhySys::ReMakePhysicalR(this, Parms,
    // Radius) then enable_controllers() (vtable +0x60).
    virtual void remake_physical(PhySys::CreateParms const& Parms, float Radius);
    // Listing 0x1cb70. Beam/attach object at position/orientation.
    virtual void beam_object(Vector const& Pos, Matrix const& Orient, bool bAttach);
    // Listing 0x1d1e0. Map part index to engine instance. Retail mangled name
    // ?part_to_inst@CObject@@QBEJI@Z uses access code 'Q' (public, non-virtual, thiscall) --
    // NOT virtual in retail, matching the is_shield_part/inst_to_part/flag_part_as_shield
    // style further below. It has no slot in CObject's own named vtable (??_7CObject@@6B@,
    // Common.dll.txt 557774+), which ends at +0x7c (beam_object).
    long part_to_inst(unsigned PartId) const;
    // Listing 0x1d0b0. IVP moment-of-inertia, or zero vector without a physics body.
    Vector get_moment_of_inertia() const;
    // Listing 0x1d0e0. No-op without a physics body.
    void add_impulse(Vector const& Impulse);
    // Listing 0x1d100. No-op without a physics body.
    void add_impulse(Vector const& Impulse, Vector const& Point);
    // Listing 0x1d220. Reverse lookup of part_to_inst: engine instance -> part index, or 0.
    unsigned inst_to_part(long EngineInst) const;
    // Listing 0x1d250. True if the PartMap entry for PartId has its shield-part byte set.
    bool is_shield_part(unsigned PartId) const;
    // Common.dll.c 80718-80736 (0x4d4c0). Same PartMap scan as is_shield_part, but sets the
    // shield-part byte to 1 on a match instead of reading it. False if PartId isn't found.
    bool flag_part_as_shield(unsigned PartId);

    Archetype::Root* get_archetype() const;
    unsigned AddRef();
    unsigned Release();
    // .text:062af660 ?advise@CObject@@QAEXPAUCBase@@_N@Z: bOn -> push_back onto Advisors;
    // otherwise remove every element equal to the first match.
    void advise(CBase* Advisor, bool bOn);

    // .text:062adbc0 / .text:062adc40. Per-size Heap pools (0x9c, 0xf0, 0x104, 0x120, 0x1ec,
    // 0x2dc; every other size goes to malloc/free). noexcept so a new-expression checks the
    // result for null before running the constructor, as retail's CObject::Alloc does.
    static void* operator new(unsigned int Size) noexcept;
    static void operator delete(void* Ptr, unsigned int Size);

    // Retail exports no CObject::operator= (VC6 only emitted the implicit one on use, and
    // nothing assigns a CObject).
    CObject& operator=(CObject const&) = delete;

    Class ObjectClass;                  // +0x4c
    // +0x50: the PhySys system (star-system) id this object's physics body lives in. NOT
    // padding and NOT a "material id" (PhySys::BuildIVP_Sphere's tracker note said material;
    // the listing disproves it). Proof, all from Common.dll.txt:
    //   * Written ONLY next to BodyPtr, from the PhySys system record's +0x14:
    //       .text:06289471 (PhySys::BuildIVP_Sphere)  MOV EDX,[EBP+0x14] / MOV [EDI+0x50],EDX
    //                                                 right after MOV [EDI+0x54],EAX
    //       .text:0628a681 (PhySys::ReMakePhysicalR)  MOV ECX,[ESI+0x14] / MOV [EDI+0x50],ECX
    //     EBP/ESI there is `mapnode+0x10`, i.e. the mapped value of PhySys's system map
    //     DAT_063fc098 (VC6 _Rb_tree node: _Left/_Parent/_Right then _Value at +0xc, so the
    //     unsigned key sits at node+0xc and the record at node+0x10).
    //   * Cleared to 0 together with BodyPtr on teardown:
    //       .text:0628a7e0 (PhySys::UnMakePhysicalR) and .text:0628a9a0 (FUN_0628a990).
    //   * Consumed as the `unsigned` argument of PhySys::GetCurrentTime at .text:0627e261
    //     (AnimDB::Update): MOV EAX,[ESI+0x50] / PUSH EAX / PUSH &sret / CALL
    //     PhySys::GetCurrentTime / ADD ESP,8 -- retail's own mangled name there is
    //     ?GetCurrentTime@PhySys@@YA?AVIVP_Time@@I@Z, i.e. (unsigned int), and that unsigned
    //     is used as the DAT_063fc098 map key (.text:06289a8e `CMP EDX,[EAX+0xc]`, unsigned
    //     JC), the same key PhySys::Startup(unsigned)/Update(unsigned,float) take.
    //   * The record's own +0x14 is that key: PhySys::Startup pushes its `unsigned` parameter
    //     into FUN_0628c1f0 (.text:062895ca), which stores it at this+0x14 (.text:0628c242).
    // Fed in from PhySys::CreateParms+0x14 -- which common_physys.h still calls
    // `CollisionGroupId`; that name is a misnomer for the same star-system id (common_cobject
    // .cpp already fills it from IObjDB's get-system accessor and says so). Renaming the
    // CreateParms member touches src/Common/common_casteroid.* which this lane does not own,
    // so it is reported rather than changed here. Open TODO.
    unsigned PhysicsSystemId;           // +0x50
    IVP_Real_Object* BodyPtr;           // +0x54  IVP physics body, null if not physical
    // +0x58: the object's physics part (Archetype-owned, not freed by ~CObject), or null.
    // Written by PhySys::BuildPartIVP_Real (.text:06289138) and the part-instance builder
    // (.text:0628a14d, Root::get_phy_part or PhyArch::GetDefaultPart); zeroed by the ctor
    // (.text:062af317). get_surface_extents reads its ExtentsMin/ExtentsMax (+0x08/+0x14),
    // CAttachedEquip::Connect its hardpoint id list.
    PhyArch::Part const* PhyPart;       // +0x58 (formerly SurfaceExtentsPtr)
    // +0x5c: the IVP surface manager of this object's compound (part-mapped) body, or null.
    // PhySys::BuildPartIVP_Real (FUN_06289100) stores it, FUN_0628c7b0 hands it to the IVP
    // polygon creation, and part_to_inst only tests it for 0 (.text:0627d1e0: no surface
    // manager -> EngineInstanceId).
    IVP_SurfaceManager* SurfaceManager;
    // +0x60: the PhySys "golem" force controller for this object, or null. A heap object of
    // 0xd0 bytes with vtable .rdata:0x0639c1c8, allocated lazily by FUN_06288e60 -- the tail
    // of PhySys::BuildIVP_Real (called at .text:06288bee) -- and ONLY when the requested
    // control mode is 1:
    //   .text:06288edd  MOV dword ptr [ESI+0x74],0x1        ; mode 1 = golem
    //   .text:06288f67  PUSH 0xd0 / CALL operator_new       ; EDI = the golem
    //   .text:06288fa9  MOV ECX,EDI / CALL FUN_06343050     ; init from CObject::BodyPtr(+0x54)
    //   .text:06288fae  MOV [EDI],.rdata:PTR_LAB_0639c1c8   ; its vftable
    //   .text:06288fbd  MOV dword ptr [ESI+0x60],EDI        ; <- stored here (null on OOM)
    // The surrounding code reads PhySys::GOLEM_FORCE_FACTOR / GOLEM_DAMP_FACTOR /
    // GOLEM_TORQUE_FACTOR / GOLEM_ANGULAR_DAMP_FACTOR / GOLEM_MAX_TRANSLATION_FORCE into its
    // init template (.text:06288ee9-06288f7a), which is where the "golem" name comes from.
    // Consumed as a __thiscall receiver: AnimDB::Update null-tests it at .text:0627e1f9
    // (`CMP dword ptr [ESI+0x60],EBX` with EBX==0, skip on zero), loads it at .text:0628e25a
    // 0627e25a (`MOV EDI,[ESI+0x60]`) and drives it with MOV ECX,EDI / CALL FUN_06343580 (.text:
    // 0627e2c3) then MOV ECX,EDI / CALL FUN_06343670 (.text:0627e2d9) -- the same two
    // thiscalls FUN_06288e60 itself issues at .text:0628909c / .text:062890b1.
    // Declared IVP_Controller* because that is the static type retail's own
    // __RTDynamicCast calls name as the source (PhySys::GetVelocity .text:0628580f,
    // CSimple::update_zones .text:062b5d38). It holds either a FLControllerGolem
    // (AttachGolemController) or a CRemotePhysicsController (PhySys::RCBegin); both have
    // IVP_Controller at offset 0 through IVP_Controller_Golem.
    IVP_Controller* GolemController;    // +0x60
    // +0x64: std::vector<FPartMapEntry> (VC6 layout: allocator byte +0x64, _First +0x68,
    // _Last +0x6c, _End +0x70). The ctor zeroes the three pointers (.text:062af327..062af32d;
    // the byte at +0x64 is the allocator, copied from an uninitialised temporary); the copy in
    // CSimple's copy ctor (.text:06281259..0628129e) deep-copies it exact-size (FUN_06282da0
    // count, operator_new(n*0xc), FUN_06282dc0 element copy); ~CObject frees _First
    // (.text:062af5c9..062af5df). FUN_0628f700 (vector::_Insert_n) appends at _Last from the
    // non-physical attachment paths.
    std::vector<FPartMapEntry> PartMap; // +0x64 .. +0x73
    // +0x74: which physics control mode this object's body was built with. A plain dword taking
    // exactly the values 0 (never built / not physical), 1 (golem force controller) or 2 (plain
    // IVP body). Mirrored verbatim out of PhySys::CreateParms+0x18 (`Type` in common_physys.h)
    // by FUN_06288e60, the tail of PhySys::BuildIVP_Real:
    //   .text:06288e83  MOV EAX,[EBX+0x18]                 ; EBX = the CreateParms
    //   .text:06288e86  DEC EAX / JZ  06288ed1             ; ==1 -> golem branch
    //   .text:06288e8a  DEC EAX / JNZ 062890b7             ; !=2 -> do nothing at all
    //   .text:06288e99  MOV dword ptr [ESI+0x74],0x2       ; mode 2, the fallthrough
    //   .text:06288edd  MOV dword ptr [ESI+0x74],0x1       ; mode 1, the golem branch
    // BuildIVP_Real itself switches its own damping template on the same CreateParms+0x18 at
    // .text:06288a08-06288a4a (==1 pulls PhySys::GOLEM_CHILD_MASS/GOLEM_CHILD_*, ==2 pulls
    // PhySys::DEFAULT_*_DAMPING), which is the independent confirmation that 1 and 2 are the
    // only defined modes.
    // Read back as the dispatch for AnimDB::Update's transform push-down, at .text:0627e12a:
    //   MOV EAX,[ESI+0x74] / DEC EAX / JZ 0627e1f9 / DEC EAX / JNZ 0627e2de
    // mode 1 drives GolemController (+0x60) via FUN_06343580/FUN_06343670 using
    // PhySys::GetCurrentTime(PhysicsSystemId); mode 2 drives BodyPtr (+0x54) via FUN_06343cc0.
    // Nothing in this port writes it yet (PhySys::BuildIVP_Real here is `simplified`), so it
    // stays 0 and both AnimDB::Update branches are inert. Open TODO, not an accepted gap.
    int PhysicsControlMode;             // +0x74
    // +0x78: std::list<void*> of the PhySys phantoms attached to this object (VC6 layout:
    // allocator byte +0x78, _Myhead +0x7c, _Mysize +0x80). The ctor builds the empty head
    // (.text:062af337..062af351); PhySys::AttachPhantom push_backs the phantom and sets
    // phantom+0x14 = this (.text:0628b5cc..0628b5ff); ~CObject calls PhySys::DetachPhantom on
    // the front element until the list is empty (.text:062af4f3..062af513), then frees the
    // head (.text:062af59f..062af5c6). Deep-copied by CSimple's copy ctor (FUN_062819b0).
    std::list<void*> Phantoms;          // +0x78 .. +0x83
    // +0x84: the object's PhySys collision-state manager (operator_new(0x14) in the PhySys
    // build paths, freed by PhySys::UnMakePhysical); CObject::update forwards to it. Zeroed by
    // the ctor (.text:062af357), copied as a dword.
    PhySys::PhyCollisionStateManager* CollisionStateMgr; // +0x84
    Archetype::Root* Archetype;         // +0x88  copied as a raw pointer in the copy ctor (no
                                         // AddRef observed at the copy site).
    // +0x8c: std::list<CBase*> of advisors (VC6 layout: allocator byte +0x8c, _Myhead +0x90,
    // _Mysize +0x94), whose nodes come from the shared list-node pool DAT_063fc434/438/440
    // (VC6 allocator internals). CObject::advise(CBase*, bool) adds / removes; ~CObject calls
    // CBase::notify_of_destruction(this) on every element (.text:062af4d4..062af4f1). Retail
    // Common never calls advise itself (only the export entry references it).
    std::list<CBase*> Advisors;         // +0x8c .. +0x97
    unsigned RefCount;                  // +0x98  copied verbatim (the source's live refcount
                                         // value, not reset to 1) -- matches retail exactly,
                                         // confirmed via Common.dll.c 40405-40505.
};

static_assert(sizeof(CObject) == 0x9c, "CObject sizeof 0x9c");
static_assert(offsetof(CObject, ObjectClass) == 0x4c, "CObject+0x4c Class");
static_assert(offsetof(CObject, PhysicsSystemId) == 0x50, "CObject+0x50 PhySys system id");
static_assert(offsetof(CObject, BodyPtr) == 0x54, "CObject+0x54 IVP body");
static_assert(offsetof(CObject, SurfaceManager) == 0x5c, "CObject+0x5c surface manager");
static_assert(offsetof(CObject, GolemController) == 0x60, "CObject+0x60 golem force controller");
static_assert(offsetof(CObject, PartMap) == 0x64, "CObject+0x64 part map vector");
static_assert(offsetof(CObject, PhysicsControlMode) == 0x74, "CObject+0x74 physics control mode");
static_assert(offsetof(CObject, Phantoms) == 0x78, "CObject+0x78 phantom list");
static_assert(offsetof(CObject, CollisionStateMgr) == 0x84, "CObject+0x84 collision state manager");
static_assert(offsetof(CObject, Archetype) == 0x88, "CObject+0x88 archetype");
static_assert(offsetof(CObject, Advisors) == 0x8c, "CObject+0x8c advisor list");
static_assert(offsetof(CObject, RefCount) == 0x98, "CObject+0x98 refcount");

// Listing operator_new(0x9c) Alloc(0x2001). CPhysAttachment::Connect gun mesh CObject.
class COMMON_API CEquipmentObj : public CObject
{
public:
    // .text:062940a0 ??0CEquipmentObj@@QAE@W4Class@CObject@@@Z; the default argument is what
    // CEquipmentObj::`default constructor closure' (.text:06292f40) pushes: 0x2001.
    explicit CEquipmentObj(Class ObjectClass = ClassEquipmentObj);
    // ??0CEquipmentObj@@QAE@ABV0@@Z (.text:06292d40): CObject(Other) + the vptr.
    CEquipmentObj(CEquipmentObj const& Other);
    virtual ~CEquipmentObj();

    CEquipmentObj& operator=(CEquipmentObj const&) = delete;
};

static_assert(sizeof(CEquipmentObj) == 0x9c, "CEquipmentObj sizeof 0x9c");

struct COMMON_API CSimple : public CObject
{
public:
    // CSimple::CreateParms — CSimple::init 0x55b60.
    // Default member initializers reproduce retail's default ctor (Common.dll.c 0x265e0,
    // flattened into CGuided::CreateParms::CreateParms — CSimple/CProjectile never got
    // their own separate compiled ctor, but this is exactly the base-class portion of it):
    // Id/ObjDb/OwnerPlayer=0, HitPts=-1.0f, Pos=(0,0,0), Orient=identity, Velocity=(0,0,0),
    // AngularVelocity=(0,0,0).
    struct CreateParms
    {
        unsigned Id = 0;                  // +0x00 → this+0xb0
        IObjDB* ObjDb = nullptr;          // +0x04 connect() → this+0xac
        unsigned OwnerPlayer = 0;         // +0x08 → this+0xb4
        float HitPts = -1.0f;             // +0x0c  -1.0f = arch max
        Vector Pos{0.0f, 0.0f, 0.0f};     // +0x10 → EngineObject+0x2c
        Matrix Orient{1.0f, 0.0f, 0.0f,
                       0.0f, 1.0f, 0.0f,
                       0.0f, 0.0f, 1.0f}; // +0x1c → EngineObject+0x08
        Vector Velocity{0.0f, 0.0f, 0.0f};        // +0x40 init_physics
        Vector AngularVelocity{0.0f, 0.0f, 0.0f}; // +0x4c init_physics
    };

    static_assert(offsetof(CreateParms, Pos) == 0x10, "CSimple::CreateParms+0x10 pos");
    static_assert(offsetof(CreateParms, Orient) == 0x1c, "CSimple::CreateParms+0x1c matrix");
    static_assert(offsetof(CreateParms, Velocity) == 0x40, "CSimple::CreateParms+0x40 vel");
    static_assert(sizeof(CreateParms) == 0x58, "CSimple::CreateParms through +0x4c Vector");

    // Default argument: CSimple::`default constructor closure' (.text:06281440) pushes 0x3.
    explicit CSimple(Class ObjectClass = ClassSimple);
    // Common.dll.c 40405-40505 (0x211b0), tracker id 10557. Delegates the CObject-owned
    // portion to CObject::CObject(CObject const&) then copies CSimple's own fields below,
    // including the map/tree cluster at OpaqueBc -- see common_csimple.cpp for field-by-field
    // detail and the parts left as an intentional documented gap (map key/value types).
    CSimple(CSimple const& Other);
    virtual ~CSimple();

    CSimple& operator=(CSimple const&) = delete;

    // New virtuals in retail slot order (??_7CSimple@@6B@ 0x0639b884): +0x80 init,
    // +0x84 cache_physical_props, +0x88 get_name, +0x8c is_targetable, +0x90 connect,
    // +0x94 disconnect, +0x98 set_hit_pts, +0x9c init_physics (_purecall, .text:06391fb0).
    virtual void init(CreateParms const& Parms);
    virtual void cache_physical_props();
    // Listing 0x55f70. Returns archetype nickname ID.
    virtual unsigned get_name() const;
    // .text:06281190: `MOV AL,[this+0xa0] / AND AL,1 / RET`.
    virtual bool is_targetable() const;
    virtual void connect(IObjDB* ObjDb);
    virtual void disconnect(IObjDB* ObjDb);
    virtual void open(Archetype::Root* Arch);
    virtual float get_physical_radius_r(Vector& OutCenter) const;
    // Listing 0x55fb0. Returns archetype HitPts field. NOT virtual in retail
    // (?get_max_hit_pts@CSimple@@QBEMXZ is Q, not U) -- nothing derives an override.
    float get_max_hit_pts() const;
    // Listing 0x55fc0. Returns current hit points (clamped to max).
    float get_hit_pts() const;
    // Listing 0x55fd0. Stores hit points verbatim (no clamping in retail).
    virtual void set_hit_pts(float NewHitPts);
    // Pure in retail: CSimple's and CEqObj's vftable slot +0x9c is _purecall (.text:06391fb0),
    // and no CSimple::init_physics symbol exists.
    virtual void init_physics(Vector const& Velocity, Vector const& AngularVelocity) = 0;
    // .text:062b5fe0: HitPts / archetype HitPts, left unrounded in ST0; retail consumers compare
    // it there (Common 062aa548 FCOMP double, 062bf3a2 FCOMP; freelancer.exe 004dc791, 005355b3,
    // 00545ff0; Server.dll 06ce73d7, 06ce7ee6, 06ceb3be, 06ceee3a), so it is double here
    // (export ?get_relative_health@CSimple@@QBENXZ; retail QBEMXZ).
    double get_relative_health() const;
    // Listing 0x55e00. NOT virtual in retail (?get_scanner_interference@CSimple@@QBEMXZ is Q).
    // Re-samples the current zone's interference modifier through ITS OWN vtable slot +0x60
    // (a Zone-side virtual, unrelated to CObject's own +0x60 enable_controllers slot) and
    // scales the cached ScannerInterference value (set by update_zones), clamped to [0,1].
    float get_scanner_interference() const;
    // Listing 0x1cb70 override / 0x55ea0. CObject::beam_object then a refresh of the cached
    // center-of-mass/radius (vtable+0x84 -- see cpp for the ICF identity with
    // cache_physical_props).
    virtual void beam_object(Vector const& Pos, Matrix const& Orient, bool bAttach) override;
    virtual void unmake_physical();
    unsigned int const& get_id() const;
    unsigned int get_type() const;
    IObjDB* get_object_database() const;
    unsigned int GetOwnerPlayer() const;
    void SetOwnerPlayer(unsigned int PlayerId);

protected:
    // Retail: ?update_zones@CSimple@@IAEXMI@Z (I = protected); called from the derived
    // classes' update().
    void update_zones(float Dt, unsigned int Flags);

public:

    // +0x9c. Confirmed by TractorArm::GetTarget (Common.dll.c 65123-65131, `this+8 - 0x9c`)
    // and TractorArm::SetTarget (65510-65532, `target + 0x9c`): every CSimple-derived object
    // (CLoot in particular) can be watched through this embedded Watchable, the same
    // "BaseWatcher stores a Watchable*, owner recovered by subtracting the sub-object offset"
    // idiom as CGuided::TargetWatch above (IObjRW+8 there, CSimple+0x9c here).
    Watchable SelfWatchable;    // +0x9c
    unsigned char FlagsA0;      // +0xa0  bit0 targetable (+0xa1..+0xa3: alignment, never written)
    Universe::IZone* ZoneAtA4;  // +0xa4  Common ZONE (vftable 063a5094), set by update_zones
    float ScannerInterference;  // +0xa8
    IObjDB* ObjectDb;           // +0xac
    unsigned Id;                // +0xb0
    unsigned OwnerPlayer;       // +0xb4
    float HitPts;               // +0xb8
    // +0xbc (0x14 bytes in retail): the set of damaged-object hardpoint names already attached
    // to this object -- a VC6 std::set<CacheString> (allocator byte +0xbc, _Head +0xc0, _Nil
    // +0xc4, _Multi byte +0xc8, _Size +0xcc). Identified from its two users:
    //   CEqObj::is_damaged_obj_attached (.text:062a9590) = find(Name) != end(): lower_bound
    //     via FUN_062fcfb0 on this+0xbc, then `CMP ECX,[EAX+0xc] / JC` against the node key;
    //   CEqObj::attaching_damaged_obj (.text:062a95f0) = insert(Name): descend from
    //     [_Head+4] comparing `CMP EBP,[ESI+0xc] / SETC` (unsigned), then FUN_0628fd80 (_Insert).
    // The key is compared as the raw 32-bit CacheString::Value pointer (interned strings), so
    // the ordering is pointer order, not string order -- FCacheStringLess below. The CSimple
    // copy ctor (Common.dll.c 40405-40505) deep-copies it node by node (items 673/2108), the
    // dtor frees it -- both what std::set's own copy ctor/dtor do.
    struct FCacheStringLess
    {
        bool operator()(CacheString const& Left, CacheString const& Right) const
        {
            // A pointer compare is the same unsigned 32-bit compare (SETC).
            return Left.Value < Right.Value;
        }
    };
    std::set<CacheString, FCacheStringLess> AttachedDamageObjs; // +0xbc
    unsigned char PadBcSet[0x14 - sizeof(std::set<CacheString, FCacheStringLess>)]; // keeps +0xd0
    // +0xd0 / +0xdc: already identified and consumed elsewhere in this port (predates this
    // pass) as the lazily-cached center-of-mass/radius pair -- CSimple::cache_physical_props
    // (common_csimple.cpp, Common.dll.c 0x55ed0) writes both via raw `this+0xd0`/`this+0xdc`
    // pointer arithmetic, CSimple::get_physical_radius_r (Common.dll.c 87507) reads them back
    // and lazily recomputes when CachedRadius is still the ctor's -1.0f "uncached" sentinel.
    // This pass's copy-ctor archaeology independently confirms the SAME 12-byte Vector shape
    // at +0xd0 (copied via the identical LEA+3xMOV dword-triplet pattern used for
    // WorldPosition/RadiusCenter above) and the same 4-byte scalar at +0xdc -- now given real
    // names/types instead of raw byte blobs + reinterpret_cast (see common_csimple.cpp for the
    // two call sites updated to use these fields directly instead of pointer arithmetic).
    Vector CachedCenterOfMass;          // +0xd0 .. +0xdb
    float CachedRadius;                 // +0xdc .. +0xdf  -1.0f = not yet cached
    unsigned TypeBits;          // +0xe0  arch ObjectType after open
};

static_assert(sizeof(CSimple) == 0xe4, "CSimple sizeof 0xe4");
static_assert(offsetof(CSimple, SelfWatchable) == 0x9c, "CSimple+0x9c self-watchable");
static_assert(offsetof(CSimple, ObjectDb) == 0xac, "CSimple+0xac IObjDB");
static_assert(offsetof(CSimple, Id) == 0xb0, "CSimple+0xb0 id");
static_assert(offsetof(CSimple, OwnerPlayer) == 0xb4, "CSimple+0xb4 owner");
static_assert(offsetof(CSimple, HitPts) == 0xb8, "CSimple+0xb8 hit_pts");
static_assert(offsetof(CSimple, AttachedDamageObjs) == 0xbc, "CSimple+0xbc attached damage-object set");
static_assert(offsetof(CSimple, CachedCenterOfMass) == 0xd0, "CSimple+0xd0 cached center of mass");
static_assert(offsetof(CSimple, CachedRadius) == 0xdc, "CSimple+0xdc cached radius");
static_assert(offsetof(CSimple, TypeBits) == 0xe0, "CSimple+0xe0 type bits");

// Listing Heap DAT_063fc3e0 size 0x104 / DAT_063fc3f0 size 0x120.
// CProjectile 0xf0: CSimple + owner +0xe4 / lifetime +0xe8 / owner_safe +0xec.
struct COMMON_API CProjectile : public CSimple
{
public:
    struct CreateParms : CSimple::CreateParms
    {
        unsigned Pad58 = 0;   // +0x58
        unsigned OwnerId = 0; // +0x5c → this+0xe4
    };

    static_assert(offsetof(CreateParms, OwnerId) == 0x5c, "CProjectile::CreateParms+0x5c owner");
    static_assert(sizeof(CreateParms) == 0x60, "CProjectile::CreateParms sizeof 0x60");

    explicit CProjectile(Class ObjectClass);
    virtual ~CProjectile();

    CProjectile& operator=(CProjectile const&) = delete;

    // ??_7CProjectile@@6B@ 0x0639bb0c: +0xa0 init, +0xa4 set_dead, +0xa8 expire_safe_time
    // (_purecall .text:06391fb0 -- no CProjectile::expire_safe_time symbol exists; CBeam,
    // CGuided, CMine and CCounterMeasure each supply one).
    virtual void init(CreateParms const& Parms);
    virtual int update(float Dt, unsigned Flags);
    virtual void set_dead();
    virtual void expire_safe_time() = 0;

    // 0x06262d50, ICF-folded `MOV EAX,[this+0x88]; RET` (see CMunition::munitionarch).
    Archetype::Projectile const* projarch() const;
    unsigned int const& get_owner() const;
    bool is_alive() const;
    bool is_owner_safe() const;

    unsigned OwnerId;        // +0xe4
    float Lifetime;          // +0xe8  arch Projectile+0x70
    float OwnerSafeTime;     // +0xec  arch Projectile+0x74
};

static_assert(sizeof(CProjectile) == 0xf0, "CProjectile sizeof 0xf0");
static_assert(offsetof(CProjectile, OwnerId) == 0xe4, "CProjectile+0xe4 owner");
static_assert(offsetof(CProjectile, Lifetime) == 0xe8, "CProjectile+0xe8 lifetime");
static_assert(offsetof(CProjectile, OwnerSafeTime) == 0xec, "CProjectile+0xec owner_safe");

// CMunition: the interposing layer between CProjectile and the guided/beam weapons.
// Retail exports exactly five CMunition symbols and nothing else:
//   ??0CMunition@@QAE@W4Class@CObject@@@Z   0x06286380  ctor(Class)
//   ??0CMunition@@QAE@ABU0@@Z               0x062863a0  copy ctor
//   ??1CMunition@@UAE@XZ                    0x06286c80  dtor (ICF-folded, see below)
//   ?munitionarch@CMunition@@QBEPBUMunition@Archetype@@XZ 0x06262d50 (ICF-folded)
//   ??_7CMunition@@6B@                      0x0639bbbc  vftable
//
// Layout: CMunition::CMunition(Class) at 0x06286380 is nine instructions --
// CALL CProjectile::CProjectile / MOV [ESI],&CMunition::`vftable' / MOV EAX,ESI / RET 4.
// It initialises no field of its own. The copy ctor at 0x062863a0 confirms this from the
// other side: it copies +0x04..+0xec and stops (0xec is CProjectile::OwnerSafeTime, the
// last CProjectile field), then re-stamps the vptr. So sizeof(CMunition)==sizeof(CProjectile).
//
// Vtable: 0x0639bbbc holds 43 slots (0x0639bbbc..0x0639bc64 = 172 bytes), NOT the 44/176
// the tracker recorded -- the dword at 0x0639bc68 is CGuided's RTTICompleteObjectLocator
// sitting at [CGuided_vftable-4], the same neighbour-COL overcount that bit 48 other items
// in the 2026-09-17 vftable sweep. All 43 slots are byte-for-byte the same target addresses
// as CProjectile::`vftable' (0x0639bb0c..0x0639bbb4): CMunition overrides NOTHING. It is a
// pure taxonomy layer, which is why it has no method of its own beyond munitionarch().
//
// Hierarchy (RTTI BaseClassArrays, the authority -- ctor CALLs are useless here because
// MSVC elides the redundant intermediate vptr store):
//   CMunition COL 0x063a6ca8 -> hierarchy desc 0x063a6c98, 7 bases from array 0x063a6c78
//   CGuided   COL 0x063a6d08 -> 8 bases from array 0x063a6cd4: CGuided, CMunition, CProjectile, ...
//   CBeam     COL ...        -> 8 bases from array 0x063a6d34: CBeam,   CMunition, CProjectile, ...
//   CMine                    -> 7 bases from array 0x063a87f4: CMine,   CProjectile, ...  (NO CMunition)
// So CGuided and CBeam derive from CMunition; CMine and CCounterMeasure do not, they stay
// on CProjectile directly. (The 2026-09-06 tracker note claiming CGuided/CBeam derive
// straight from CProjectile in retail was wrong on this point; RTTI settles it.)
struct COMMON_API CMunition : public CProjectile
{
public:
    explicit CMunition(Class ObjectClass);
    virtual ~CMunition();

    CMunition& operator=(CMunition const&) = delete;

    // 0x06262d50, ICF-folded with CObject::get_archetype, CAsteroid::asteroidarch,
    // CMine::minearch, CShip::shiparch and ten more: `MOV EAX,[this+0x88]; RET`.
    // No null check in retail -- the fold body is two instructions.
    Archetype::Munition const* munitionarch() const;
};

static_assert(sizeof(CMunition) == 0xf0, "CMunition sizeof 0xf0 (adds no field to CProjectile)");

// Listing Alloc(0xa7) Heap 0x104. CMunition vtable then CGuided fields.
struct COMMON_API CGuided : public CMunition
{
public:
    // operator= copies 0x1a dwords. Target is IObjRW* (Watchable at +8).
    struct COMMON_API CreateParms : CProjectile::CreateParms
    {
        // User-declared so that no implicit move members are generated and exported:
        // VC6 had none, and retail exports exactly the default ctor and copy assignment.
        CreateParms& operator=(CreateParms const&) = default;

        IObjRW* Target = nullptr;           // +0x60
        unsigned short SubTarget = 0;       // +0x64 (+0x66: padding; the ctor 062865e0 leaves it)
    };

    static_assert(offsetof(CreateParms, Target) == 0x60, "CGuided::CreateParms+0x60 target");
    static_assert(offsetof(CreateParms, SubTarget) == 0x64, "CGuided::CreateParms+0x64 sub");
    static_assert(sizeof(CreateParms) == 0x68, "CGuided::CreateParms sizeof 0x68");

    // Default argument: CGuided::`default constructor closure' (.text:06286820) pushes 0xa7.
    explicit CGuided(Class ObjectClass = ClassGuided);
    virtual ~CGuided();

    CGuided& operator=(CGuided const&) = delete;

    void init(CreateParms const& Parms);
    virtual int update(float Dt, unsigned Flags);

protected:
    // Retail: ?init_physics@CGuided@@MAEXABVVector@@0@Z / ?expire_safe_time@CGuided@@MAEXXZ
    // (M = protected virtual).
    virtual void init_physics(Vector const& Velocity, Vector const& AngularVelocity);
    virtual void expire_safe_time();

public:
    IObjRW* get_target() const;
    void set_target(IObjRW* Target);
    unsigned short get_sub_target() const;
    void set_sub_target(unsigned short SubTargetId);
    bool motor_on() const;
    Archetype::MotorData const* get_motor_arch() const;
    bool seeker_can_see(Vector const& Pos) const;
    // .text:062acd60. The same range+cone test for an arbitrary seeker position/forward and
    // munition archetype: true when |Target-From| < SeekerRange and the unit direction's dot
    // with Forward is >= SeekerFovCos.
    static bool __cdecl seeker_can_see(Vector const& From, Vector const& Target, Vector const& Forward,
                                       Archetype::Munition const* Arch);

    BaseWatcher TargetWatch;                  // +0xf0 watches Target's Watchable (IObjRW+8)
    unsigned short SubTarget;                 // +0xf8 (+0xfa: alignment, never written)
    Archetype::MotorData const* MotorArch;    // +0xfc
    float SeekerElapsed;                      // +0x100
};

static_assert(sizeof(CGuided) == 0x104, "CGuided sizeof 0x104");
static_assert(offsetof(CGuided, TargetWatch) == 0xf0, "CGuided+0xf0 watcher");
static_assert(offsetof(CGuided, SubTarget) == 0xf8, "CGuided+0xf8 sub_target");
static_assert(offsetof(CGuided, MotorArch) == 0xfc, "CGuided+0xfc motor");
static_assert(offsetof(CGuided, SeekerElapsed) == 0x100, "CGuided+0x100 elapsed");

// Listing Alloc(0x67) Heap 0x120. CMunition then colgroup / vel / trail.
// CBeam::CBeam(CBeam const&) at 0x06286b?? stamps CSimple's vptr, copies +0xe4/+0xe8/+0xec,
// then stamps CMunition::`vftable' at 0x06286bd8 before copying its own +0xf0.. fields --
// direct evidence of the CMunition subobject, matching CBeam's 8-entry RTTI base array.
struct COMMON_API CBeam : public CMunition
{
public:
    // Default argument: CBeam::`default constructor closure' (.text:06286c90) pushes 0x67.
    explicit CBeam(Class ObjectClass = ClassBeam);
    virtual ~CBeam();

    CBeam& operator=(CBeam const&) = delete;

    // 0x06262d50 (ICF fold, `MOV EAX,[this+0x88]; RET`). Retail CBeam redeclares it, hiding
    // CMunition::munitionarch with the identical body.
    Archetype::Munition const* munitionarch() const;

protected:
    // Retail: protected virtual (?init_physics@CBeam@@MAE..., ?expire_safe_time@CBeam@@MAEXXZ).
    virtual void init_physics(Vector const& Velocity, Vector const& AngularVelocity);
    virtual void expire_safe_time();

public:
    virtual Vector get_velocity() const;
    virtual float get_mass() const;

    void move(float Dt);
    void set_velocity(Vector const& Vel);
    Vector const& get_last_pos() const;
    char const* get_collision_group() const;

    char CollisionGroup[8];  // +0xf0  itoa(owner, radix 0x18)
    Vector Velocity;         // +0xf8
    Vector OriginPos;        // +0x104 first-move snapshot
    Vector LastPos;          // +0x110 get_last_pos when bMoved
    unsigned char bMoved;    // +0x11c (+0x11d..+0x11f: alignment)
};

static_assert(sizeof(CBeam) == 0x120, "CBeam sizeof 0x120");
static_assert(offsetof(CBeam, CollisionGroup) == 0xf0, "CBeam+0xf0 colgroup");
static_assert(offsetof(CBeam, Velocity) == 0xf8, "CBeam+0xf8 velocity");
static_assert(offsetof(CBeam, OriginPos) == 0x104, "CBeam+0x104 origin");
static_assert(offsetof(CBeam, LastPos) == 0x110, "CBeam+0x110 last pos");
static_assert(offsetof(CBeam, bMoved) == 0x11c, "CBeam+0x11c moved");

// CMine: projectile subclass for space mines. Heap size 0xf0 (same as CProjectile).
// Derives from CProjectile directly, NOT from CMunition: CMine's RTTI base array at
// 0x063a87f4 has 7 entries (CMine, CProjectile, CSimple, ...) against CGuided's/CBeam's 8.
struct COMMON_API CMine : public CProjectile
{
public:
    struct CreateParms : CProjectile::CreateParms
    {
        // Extends CProjectile::CreateParms with mine-specific fields if any
    };

    // Default argument: CMine::`default constructor closure' (.text:062ac9d0) pushes 0xf.
    explicit CMine(Class ObjectClass = ClassMine);
    virtual ~CMine();

    CMine& operator=(CMine const&) = delete;

    virtual int update(float Dt, unsigned Flags);

    // ?minearch@CMine@@QAEPBUMine@Archetype@@XZ -- not const. 0x06262d50 ICF fold.
    Archetype::Mine const* minearch();

protected:
    // Retail: protected virtual (?init_physics@CMine@@MAE..., ?expire_safe_time@CMine@@MAEXXZ).
    virtual void init_physics(Vector const& Velocity, Vector const& AngularVelocity);
    virtual void expire_safe_time();
};

static_assert(sizeof(CMine) == 0xf0, "CMine sizeof 0xf0 (inherits CProjectile)");

// CCounterMeasure: projectile subclass for countermeasures (chaff/flares).
// Like CMine, derives from CProjectile directly: RTTI base array 0x063a8124 (COL 0x063a8154,
// hierarchy descriptor 0x063a8144, numBaseClasses 7) reads CCounterMeasure, CProjectile,
// CSimple, ... -- no CMunition entry.
struct COMMON_API CCounterMeasure : public CProjectile
{
public:
    struct CreateParms : CProjectile::CreateParms
    {
    };

    // Default argument: CCounterMeasure::`default constructor closure' (.text:062a8570)
    // pushes 0x17.
    explicit CCounterMeasure(Class ObjectClass = ClassCounterMeasure);
    virtual ~CCounterMeasure();

    CCounterMeasure& operator=(CCounterMeasure const&) = delete;

    virtual int update(float Dt, unsigned Flags);

    // ?countermeasure_arch@CCounterMeasure@@QAEPBUCounterMeasure@Archetype@@XZ -- not const.
    Archetype::CounterMeasure const* countermeasure_arch();

protected:
    // Retail: protected virtual (?init_physics@CCounterMeasure@@MAE...,
    // ?expire_safe_time@CCounterMeasure@@MAEXXZ).
    virtual void init_physics(Vector const& Velocity, Vector const& AngularVelocity);
    virtual void expire_safe_time();
};

static_assert(sizeof(CCounterMeasure) == 0xf0, "CCounterMeasure sizeof 0xf0");

// CLoot: cargo container dropped by destroyed ships/solars.
// CLoot::init 0x4c460 / CLoot::CLoot 0x4c400 / CLoot::update 0x4c680.
// Real object size confirmed by the retail allocation site (operator_new(0x104)
// before CLoot::CLoot at Common.dll.c ~82073), not the accessor offsets alone.
struct COMMON_API CLoot : public CSimple
{
public:
    struct CreateParms : CSimple::CreateParms
    {
        unsigned Pad58;      // +0x58 unused (same unused slot as CProjectile::CreateParms+0x58)
        unsigned OwnerId;    // +0x5c → this+0xe4
        int NameId;          // +0x60 → this+0xe8  (IDS override; 0 → falls back to ContentsArch->IdsName)
        Archetype::Equipment const* ContentsArch; // +0x64 → this+0xec (pre-resolved pointer, not an id)
        unsigned Units;      // +0x68 → this+0xf0
        float ContentsHitPts; // +0x6c → this+0xf4
        bool bCanAiTractor;  // +0x70 → this+0xfc
        bool bTemporary;     // +0x71 → this+0xfd (+0x72/+0x73: alignment)
    };

    static_assert(offsetof(CreateParms, OwnerId) == 0x5c, "CLoot::CreateParms+0x5c owner");
    static_assert(offsetof(CreateParms, NameId) == 0x60, "CLoot::CreateParms+0x60 name id");
    static_assert(offsetof(CreateParms, ContentsArch) == 0x64, "CLoot::CreateParms+0x64 contents arch ptr");
    static_assert(offsetof(CreateParms, Units) == 0x68, "CLoot::CreateParms+0x68 units");
    static_assert(offsetof(CreateParms, ContentsHitPts) == 0x6c, "CLoot::CreateParms+0x6c hp");
    static_assert(offsetof(CreateParms, bCanAiTractor) == 0x70, "CLoot::CreateParms+0x70 can_ai_tractor");
    static_assert(offsetof(CreateParms, bTemporary) == 0x71, "CLoot::CreateParms+0x71 temp flag");
    static_assert(sizeof(CreateParms) == 0x74, "CLoot::CreateParms sizeof 0x74");

    // Default argument: CLoot::`default constructor closure' (.text:0629d7f0) pushes 0x803.
    explicit CLoot(Class ObjectClass = ClassLoot);
    // ??0CLoot@@QAE@ABU0@@Z (.text:0629d530): CSimple(Other) inlined, then CLoot's own fields.
    CLoot(CLoot const& Other);
    virtual ~CLoot();

    CLoot& operator=(CLoot const&) = delete;

    virtual void init(CreateParms const& Parms);
    virtual int update(float Dt, unsigned Flags);
    virtual void set_hit_pts(float NewHitPts);
    virtual unsigned get_name() const;

protected:
    // Retail: ?init_physics@CLoot@@MAEXABVVector@@0@Z (M = protected virtual).
    virtual void init_physics(Vector const& Velocity, Vector const& AngularVelocity);

public:
    unsigned get_owner() const;
    unsigned get_units() const;
    float get_volume() const;
    float get_contents_hit_pts() const;
    void set_units(unsigned Count);
    void set_contents_hit_pts(float Pts);
    bool is_loot_temporary() const;
    bool can_ai_tractor() const;
    // Retail returns a non-const pointer from a const method:
    // ?contents_arch@CLoot@@QBEPAUEquipment@Archetype@@XZ (and container_arch alike).
    Archetype::Equipment* contents_arch() const;
    Archetype::Equipment* container_arch() const;

    unsigned OwnerId;             // +0xe4
    int NameId;                   // +0xe8  IDS override; 0 → falls back to ContentsArch->IdsName
    Archetype::Equipment const* ContentsArch; // +0xec  cached resolved pointer (not an id)
    unsigned Units;               // +0xf0
    float ContentsHitPts;         // +0xf4
    float OwnerSafeTime;          // +0xf8  counts down from LOOT_OWNER_SAFE_TIME while OwnerId != 0
    bool bCanAiTractor;           // +0xfc
    bool bTemporary;              // +0xfd (+0xfe/+0xff: alignment, never written)
    unsigned Reserved100;         // +0x100  zeroed/copied by retail ctor/copy-ctor; not read anywhere in this module
};

static_assert(sizeof(CLoot) == 0x104, "CLoot sizeof 0x104 (retail operator_new(0x104) before CLoot::CLoot)");
static_assert(offsetof(CLoot, OwnerId) == 0xe4, "CLoot+0xe4 owner");
static_assert(offsetof(CLoot, NameId) == 0xe8, "CLoot+0xe8 name id");
static_assert(offsetof(CLoot, ContentsArch) == 0xec, "CLoot+0xec contents arch ptr");
static_assert(offsetof(CLoot, Units) == 0xf0, "CLoot+0xf0 units");
static_assert(offsetof(CLoot, ContentsHitPts) == 0xf4, "CLoot+0xf4 hp");
static_assert(offsetof(CLoot, OwnerSafeTime) == 0xf8, "CLoot+0xf8 owner safe time");
static_assert(offsetof(CLoot, bCanAiTractor) == 0xfc, "CLoot+0xfc can_ai_tractor");
static_assert(offsetof(CLoot, bTemporary) == 0xfd, "CLoot+0xfd temp flag");
static_assert(offsetof(CLoot, Reserved100) == 0x100, "CLoot+0x100 reserved");

// CmnAsteroid::CAsteroidField is a distinct, non-CObject subsystem (common_casteroid.h).
// CAsteroid only ever stores a back-pointer to it, so a forward declaration is enough here.
namespace CmnAsteroid
{
    class CAsteroidField;
}

// CAsteroid: static asteroid rock spawned by CmnAsteroid::CAsteroidCube. A CSimple
// subclass (ObjectClass 0x1003) -- NOT related to CLoot despite CLoot::set_hit_pts /
// CLoot::set_contents_hit_pts appearing to call "CAsteroid::asteroidarch(this)" in the
// decomp (Common.dll.c 0x4c700 / 0x3d4d0): that is retail identical-code-folding, not
// inheritance. CAsteroid::asteroidarch, CMine::minearch, CShip::shiparch, CObject::get_archetype
// and a dozen other unrelated classes' arch() accessors all compile to the exact same
// "return *(T**)(this+0x88)" body and got linked into ONE physical function (Common.dll.c
// 9341-9363); the caller's static type just picks which alias name Ghidra shows, so
// CLoot::set_hit_pts landing on the "CAsteroid" alias says nothing about CLoot's hierarchy.
// Proof CAsteroid : CSimple (not CLoot): CAsteroid::CAsteroid(void) calls
// CSimple::CSimple(this, 0x1003) directly (Common.dll.c 0x42840), operator_new(0xfc) is used
// for ObjectClass 0x1003 in the CObject factory (Common.dll.c ~82049), and CSimple's own
// sizeof is 0xe4 -- exactly where CAsteroid's own fields (0xe4..0xfc) begin.
//
// CDynamicAsteroid (declared further below) looks like a sibling by name but is a totally
// separate class: its ctor calls CObject::CObject(this, 0x4001) directly (skipping CSimple
// entirely) and operator_new(0x9c) == sizeof(CObject) exactly, i.e. it adds no data members
// of its own at all.
struct COMMON_API CAsteroid : public CSimple
{
public:
    // CAsteroid::init 0x428f0. init() extends CSimple::CreateParms with fields copied
    // straight into the matching CAsteroid+0xe8.. members below.
    struct CreateParms : CSimple::CreateParms
    {
        unsigned Pad58;              // +0x58 (matches CProjectile::CreateParms's own pad)
        unsigned long AsteroidId;    // +0x5c -> this+0xe8
        // The id of the owning asteroid cube: the cube's populate fills it (.text:062a6331)
        // and init() copies it to this+0xec (.text:062a290a).
        unsigned CubeId;             // +0x60 -> this+0xec  formerly FieldE4Value
        bool bMine;                  // +0x64 -> this+0xf0 (CAsteroid::is_mine)
        bool bInstantKill;           // +0x65 -> this+0xf1 (CAsteroid::is_instant_kill) (+0x66/+0x67: alignment)
        CmnAsteroid::CAsteroidField const* OwnerField; // +0x68 -> this+0xf8 (get_owner_field)
    };

    static_assert(offsetof(CreateParms, AsteroidId) == 0x5c, "CAsteroid::CreateParms+0x5c id");
    static_assert(offsetof(CreateParms, CubeId) == 0x60, "CAsteroid::CreateParms+0x60 cube id");
    static_assert(offsetof(CreateParms, bMine) == 0x64, "CAsteroid::CreateParms+0x64 mine");
    static_assert(offsetof(CreateParms, bInstantKill) == 0x65, "CAsteroid::CreateParms+0x65");
    static_assert(offsetof(CreateParms, OwnerField) == 0x68, "CAsteroid::CreateParms+0x68 field");
    static_assert(sizeof(CreateParms) == 0x6c, "CAsteroid::CreateParms sizeof 0x6c");

    CAsteroid();
    CAsteroid(CAsteroid const& Other);
    virtual ~CAsteroid();

    CAsteroid& operator=(CAsteroid const&) = delete;

    // Common.dll.c 0x428f0. Non-virtual: hides (does not override) CSimple::init -- the
    // retail vtable slot at +0x80 still resolves to CSimple::init (Common.dll.txt
    // 0639dc14), and every call site invokes CAsteroid::init explicitly by name
    // (e.g. Common.dll.c ~73766, the cube's per-asteroid spawn loop).
    void init(CreateParms const& Parms);

protected:
    // Common.dll.c 0x42930. Builds a near-immovable IVP body (Mass 1000, LinearDamping
    // and AngularDamping all 1e6): retail never applies Velocity/AngularVelocity to it,
    // so this port doesn't either -- asteroids are always physically motionless.
    // Retail: ?init_physics@CAsteroid@@MAEXABVVector@@0@Z (M = protected virtual).
    virtual void init_physics(Vector const& Velocity, Vector const& AngularVelocity);

public:
    // Common.dll.c 0x428e0. Trivial passthrough (COMDAT-folded with CDynamicAsteroid::update).
    virtual int update(float Dt, unsigned Flags);
    // Common.dll.c 0x1023d0. COMDAT-folded with CSolar::get_velocity: always the zero vector.
    virtual Vector get_velocity() const;

    Archetype::Asteroid const* asteroidarch() const;
    // Retail's four accessors are non-const (?get_asteroid_id@CAsteroid@@QAEKXZ etc.):
    // .text:062a22b0 / 062a22c0 / 062a22d0 / 062a22e0.
    unsigned long get_asteroid_id();
    bool is_mine();
    bool is_instant_kill();
    CmnAsteroid::CAsteroidField const* get_owner_field();
    // .text:0629d4d0 (ICF-folded with CLoot::set_contents_hit_pts): `MOV [this+0xf4],EAX`.
    // Stores the star-system id the owning cube passes in (CollisionGroupId below).
    void set_system(unsigned int SystemId);

    unsigned char OpaqueE4[4];      // +0xe4 unidentified; only ever bulk-copied (copy ctor)
    unsigned long AsteroidId;       // +0xe8
    unsigned CubeId;                // +0xec  owning cube's id (CreateParms::CubeId); formerly FieldECValue
    bool bMine;                     // +0xf0
    bool bInstantKill;              // +0xf1
    // Set directly by the owning CAsteroidCube right before init() is called (Common.dll.c
    // ~73763: this_01+0xf4 = Cube+0x50, i.e. the cube's own CmnAsteroid::CAsteroidCube::SystemId),
    // NOT populated through CreateParms. Fed into PhySys::CreateParms::CollisionGroupId by
    // init_physics.
    unsigned CollisionGroupId;      // +0xf4
    CmnAsteroid::CAsteroidField const* OwnerField;  // +0xf8
};

static_assert(sizeof(CAsteroid) == 0xfc, "CAsteroid sizeof 0xfc (operator_new(0xfc), Alloc case 0x1003)");
static_assert(offsetof(CAsteroid, AsteroidId) == 0xe8, "CAsteroid+0xe8 id");
static_assert(offsetof(CAsteroid, CubeId) == 0xec, "CAsteroid+0xec cube id");
static_assert(offsetof(CAsteroid, bMine) == 0xf0, "CAsteroid+0xf0 mine");
static_assert(offsetof(CAsteroid, bInstantKill) == 0xf1, "CAsteroid+0xf1 instant_kill");
static_assert(offsetof(CAsteroid, CollisionGroupId) == 0xf4, "CAsteroid+0xf4 collision group");
static_assert(offsetof(CAsteroid, OwnerField) == 0xf8, "CAsteroid+0xf8 owner field");

// CDynamicAsteroid: animated/scripted asteroid-like scenery (ObjectClass 0x4001). Despite
// the name, this is a direct CObject subclass -- see the block comment on CAsteroid above
// for the ctor/sizeof evidence. It adds no data members of its own (sizeof == sizeof(CObject)),
// only new/overridden virtuals.
struct COMMON_API CDynamicAsteroid : public CObject
{
public:
    // CDynamicAsteroid::init (0x42a70) forwards straight into its own virtual init_physics,
    // passing pieces of this struct -- there's no CSimple-style CreateParms chain since
    // CDynamicAsteroid isn't a CSimple.
    struct CreateParms
    {
        unsigned CollisionGroupId;  // +0x00
        Vector Velocity;            // +0x04
        Vector AngularVelocity;     // +0x10
    };

    static_assert(offsetof(CreateParms, Velocity) == 0x04, "CDynamicAsteroid::CreateParms+0x4");
    static_assert(offsetof(CreateParms, AngularVelocity) == 0x10, "CDynamicAsteroid::CreateParms+0x10");
    static_assert(sizeof(CreateParms) == 0x1c, "CDynamicAsteroid::CreateParms sizeof 0x1c");

    CDynamicAsteroid();
    CDynamicAsteroid(CDynamicAsteroid const& Other);
    virtual ~CDynamicAsteroid();

    CDynamicAsteroid& operator=(CDynamicAsteroid const&) = delete;

    // 0x06262d50 (ICF fold, `MOV EAX,[this+0x88]; RET`).
    Archetype::DynamicAsteroid const* dynamicAsteroidArch() const;

    // Common.dll.c 0x42a70. Non-virtual; just forwards to the virtual init_physics below.
    void init(CreateParms const& Parms);
    // Common.dll.c 0x42a90. Mass 1.0, LinearDamping 1e-5, AngularDamping (0,0,0);
    // CollisionGroupId comes from CreateParms rather than a cube (unlike CAsteroid).
    // Retail never applies Velocity/AngularVelocity here either.
    // Retail: ?init_physics@CDynamicAsteroid@@MAEXABVVector@@0I@Z (M = protected virtual).
protected:
    virtual void init_physics(Vector const& Velocity, Vector const& AngularVelocity, unsigned CollisionGroupId);

public:
    // Common.dll.txt 0639dc8c: vtable slot is COMDAT-folded with CAsteroid::update, i.e.
    // this is the same trivial CObject::update passthrough.
    virtual int update(float Dt, unsigned Flags);
};

static_assert(sizeof(CDynamicAsteroid) == 0x9c, "CDynamicAsteroid sizeof 0x9c (== sizeof(CObject), no added fields)");

// CDeadReckonedVector: network dead reckoning for position interpolation.
//
// Retail (Common.dll) fits a quadratic P(t) = Base + Velocity*t + 0.5*Accel*t^2
// through its last 3 position samples (unevenly spaced in time), then, so a
// freshly-arrived sample doesn't make the visual position pop, blends from the
// previously-rendered position/velocity into that new quadratic over a short
// "converge" window via a second quadratic (Velocity2/Acceleration2) anchored
// at BlendBasePosition. See Common.dll.c/.txt: AddSample @0x30af0,
// ProjectVelocity @0x316e0, GetProjectedPosition @0x317b0, Doit @0x317c0.
//
// EXPORTS. Retail's header made this class dllexport in every module that
// compiled it, so the members it defined INLINE were emitted into, and
// exported from, each of those modules -- Common.dll, Server.dll (and the two
// executables), all at their own addresses:
//
//   member                        Common.dll            Server.dll
//   CalcError                     .text:062830c0 ord 1252  .text:06ce1f40 ord 69
//   GetSample                     .text:062830f0 ord 1650  .text:06ce1f70 ord 257
//   NormalizeProjectedPosition    .text:06283100 ord 1859  .text:06ce1f80 ord 347
//   NormalizeTrackedPosition      .text:06283150 ord 1860  .text:06ce1fd0 ord 348
//   copy ctor (implicit)          .text:062831a0 ord 93    .text:06ce2020 ord 5
//   operator= (implicit)          .text:06283300 ord 731   .text:06ce2180 ord 25
//   ??_7 (1 slot, deleting dtor)  .rdata:0639b98c ord 981  .rdata:06d64f10 ord 36
//
// The two instruction streams of each inline member are identical, byte for
// byte apart from relocations. The copy constructor and operator= are the
// compiler's own memberwise ones (arrays copied with REP MOVSD in the ctor,
// with element loops in operator=, the vptr stored last), so they are left
// implicit here too. Everything else -- default ctor, dtor, AddSample,
// ConvergeTime, Doit, GetProjectedPosition, ProjectVelocity, Reset,
// SetMaxCorrectiveConvergeTime -- has its only body in Common.dll; Server.dll
// reaches the one it uses (the destructor, from its own deleting destructor)
// through Common.lib's import thunk, exactly as retail's Server.dll imports
// ??1CDeadReckonedVector@@UAE@XZ (ordinal 0x211) from COMMON.DLL.
//
// SHARED_INLINE_API at class level is what reproduces this: dllexport in
// Common.dll and in Server.dll, dllimport everywhere else. Server.dll's own
// ??_7 is emitted with its copy constructor and slot 0 is a locally generated
// deleting destructor, the shape of retail's .text:06ce2320.
//
// Retail mangles every accessor QAE (non-const): ?CalcError@...@@QAENABVVector@@@Z,
// ?GetSample@...@@QAEABVVector@@H@Z, ?ConvergeTime@...@@QAENXZ,
// ?GetProjectedPosition@...@@QAEABVVector@@XZ, ?ProjectVelocity@...@@QAE?AVVector@@N@Z.
class SHARED_INLINE_API CDeadReckonedVector
{
public:
    CDeadReckonedVector();
    virtual ~CDeadReckonedVector();

    void AddSample(Vector const& Pos, double Timestamp);

    // .text:062830c0 (Common) / .text:06ce1f40 (Server), RET 0x4. Everything
    // stays on the x87 stack: each FLD float/FSUB float difference is unrounded,
    // the squares are summed as (Dx^2 + Dz^2) + Dy^2 (FMULP ST3, then FMUL ST1 /
    // FADDP ST3 for Dz, then FMUL ST2 / FADDP ST3 for Dy), and the FSQRT result is
    // returned in ST0 without ever being stored.
    double CalcError(Vector const& Pos)
    {
        double const Dx = (double)Pos.x - TrackedPosition.x;
        double const Dy = (double)Pos.y - TrackedPosition.y;
        double const Dz = (double)Pos.z - TrackedPosition.z;
        return sqrt((Dx * Dx + Dz * Dz) + Dy * Dy);
    }

    double ConvergeTime();
    void Doit(double Dt);
    Vector const& GetProjectedPosition();

    // .text:062830f0 / .text:06ce1f70: LEA EAX,[this + Index*0xc + 0x8] -- no
    // bounds check.
    Vector const& GetSample(int Index)
    {
        return Samples[Index];
    }

    // .text:06283100 / .text:06ce1f80 (+0x60/+0x64/+0x68) and .text:06283150 /
    // .text:06ce1fd0 (+0x6c/+0x70/+0x74): the same 31 instructions on different
    // fields. FLD z, FLD y, FLD x; x*x + y*y, + z*z (that association); FSQRT --
    // Len stays on the x87 stack. FCOM float 1e-5f, TEST AH,0x41, JNZ past the
    // divide: the divide runs only when Len is STRICTLY greater (not <=, not
    // unordered). FDIVR double 1.0 keeps the reciprocal on the stack too; each
    // component is FLD inv, FMUL float [c], FSTP float [c].
    void NormalizeProjectedPosition()
    {
        double const X = ProjectedPosition.x;
        double const Y = ProjectedPosition.y;
        double const Z = ProjectedPosition.z;
        double const Len = sqrt((X * X + Y * Y) + Z * Z);
        if (Len > 1e-05f)
        {
            double const InvLen = 1.0 / Len;
            ProjectedPosition.x = (float)(InvLen * X);
            ProjectedPosition.y = (float)(InvLen * Y);
            ProjectedPosition.z = (float)(InvLen * Z);
        }
    }

    void NormalizeTrackedPosition()
    {
        double const X = TrackedPosition.x;
        double const Y = TrackedPosition.y;
        double const Z = TrackedPosition.z;
        double const Len = sqrt((X * X + Y * Y) + Z * Z);
        if (Len > 1e-05f)
        {
            double const InvLen = 1.0 / Len;
            TrackedPosition.x = (float)(InvLen * X);
            TrackedPosition.y = (float)(InvLen * Y);
            TrackedPosition.z = (float)(InvLen * Z);
        }
    }

    Vector ProjectVelocity(double Dt);
    void Reset();
    void SetMaxCorrectiveConvergeTime(double Time);

    // Offsets are past the vptr the virtual destructor introduces.
    Vector Samples[3];            // +0x08 raw samples: [0]=newest, [1]=middle, [2]=oldest
    double Timestamps[3];         // +0x30 (compiler-padded to 8-byte align) timestamps, same order
    Vector BasePosition;          // +0x48 Samples[0] as of the last AddSample; target quadratic's t=0 anchor
    Vector BlendBasePosition;     // +0x54 ProjectedPosition as of the last AddSample; corrective quadratic's t=0 anchor
    Vector ProjectedPosition;     // +0x60 rendered/output position (what GetProjectedPosition returns)
    Vector TrackedPosition;       // +0x6c raw target-quadratic position, kept current every Doit
    Vector Velocity;              // +0x78 target quadratic's linear coefficient (evaluated at BasePosition)
    Vector Velocity2;             // +0x84 corrective quadratic's linear coefficient (evaluated at BlendBasePosition)
    Vector ConvergedVelocity;     // +0x90 last "0.5*Accel*t^2 + Velocity" reading; feeds the SampleCount==0 fallback
    Vector Acceleration;          // +0x9c target quadratic's quadratic coefficient
    Vector Acceleration2;         // +0xa8 corrective quadratic's quadratic coefficient
    int SampleCount;              // +0xb4 total samples ever added (unbounded; gates AddSample's 0/1/2+ cases)
    double ConvergeWindow;        // +0xb8 current blend duration (ConvergeTime() getter); dt between the 2 newest samples, sometimes capped by MaxCorrectiveTime
    double ElapsedTime;           // +0xc0 time accumulated via Doit since the last AddSample
    double MaxCorrectiveTime;     // +0xc8 cap on ConvergeWindow in the degenerate/reversal fallback path; default 0.25
};

static_assert(sizeof(CDeadReckonedVector) == 0xd0, "CDeadReckonedVector sizeof 0xd0");
static_assert(offsetof(CDeadReckonedVector, Samples) == 0x08, "CDeadReckonedVector+0x08 samples");
static_assert(offsetof(CDeadReckonedVector, Timestamps) == 0x30, "CDeadReckonedVector+0x30 timestamps");
static_assert(offsetof(CDeadReckonedVector, BasePosition) == 0x48, "CDeadReckonedVector+0x48 base pos");
static_assert(offsetof(CDeadReckonedVector, BlendBasePosition) == 0x54, "CDeadReckonedVector+0x54 blend base pos");
static_assert(offsetof(CDeadReckonedVector, ProjectedPosition) == 0x60, "CDeadReckonedVector+0x60 projected pos");
static_assert(offsetof(CDeadReckonedVector, TrackedPosition) == 0x6c, "CDeadReckonedVector+0x6c tracked pos");
static_assert(offsetof(CDeadReckonedVector, Velocity) == 0x78, "CDeadReckonedVector+0x78 velocity");
static_assert(offsetof(CDeadReckonedVector, Velocity2) == 0x84, "CDeadReckonedVector+0x84 velocity2");
static_assert(offsetof(CDeadReckonedVector, ConvergedVelocity) == 0x90, "CDeadReckonedVector+0x90 converged velocity");
static_assert(offsetof(CDeadReckonedVector, Acceleration) == 0x9c, "CDeadReckonedVector+0x9c accel");
static_assert(offsetof(CDeadReckonedVector, Acceleration2) == 0xa8, "CDeadReckonedVector+0xa8 accel2");
static_assert(offsetof(CDeadReckonedVector, SampleCount) == 0xb4, "CDeadReckonedVector+0xb4 sample count");
static_assert(offsetof(CDeadReckonedVector, ConvergeWindow) == 0xb8, "CDeadReckonedVector+0xb8 converge window");
static_assert(offsetof(CDeadReckonedVector, ElapsedTime) == 0xc0, "CDeadReckonedVector+0xc0 elapsed time");
static_assert(offsetof(CDeadReckonedVector, MaxCorrectiveTime) == 0xc8, "CDeadReckonedVector+0xc8 max corrective time");

// SubObjectID (the three id makers and the id classifiers) MOVED 2026-09-24 to
// common_archetype.h, next to CollisionGroupDesc and for the same reason: PlayerData
// holds a SubObjectID::EquipIdMaker by value at retail slot+0x3C8 (Server.dll imports
// ?Reset@EquipIdMaker@SubObjectID@@QAEXXZ for it), and common_server_playerdb.h must
// not include this whole header. This header still sees the classes through its own
// #include "common_archetype.h".

// CollisionGroupDesc itself now lives in common_archetype.h (included above). It was moved
// there 2026-09-17 so PlayerData -- whose retail slot+0x26c is a std::list<CollisionGroupDesc>
// -- can hold one by value without common_server_playerdb.h having to drag the whole object
// layer into fourteen translation units. Nothing else changed: this header still sees the
// complete type, because common_archetype.h is one of its own includes.

class COMMON_API CArchGroup
{
    // Destroy is protected in retail (?Destroy@CArchGroup@@IAEXXZ), yet its only four callers
    // are CArchGroupManager::DestroyGroups / DestroyDeadGroups / DestroyLowerGroups /
    // DestroyDeadGroup (.text:062a1a25, 062a1a95, 062a1b3a, 062a1c1d) -- the manager is a friend.
    friend class CArchGroupManager;

public:
    // Retail ??0CArchGroup@@QAE@ABUCollisionGroup@Archetype@@QAUCEqObj@@@Z: the owner pointer
    // parameter itself is const (Q, not P).
    CArchGroup(Archetype::CollisionGroup const& Arch, CEqObj* const OwnerObj);
    ~CArchGroup();
    // Retail exports no copy assignment for CArchGroup.
    CArchGroup& operator=(CArchGroup const&) = delete;

    void SetRelativeHealth(float Frac);
    // .text:062a0260: the quotient stays unrounded in ST0 and retail consumers use it there
    // (freelancer.exe 004e6d37 FSUBR, 00545a1d FCOMP double; Server.dll 06ceb2fd FCOMP), so
    // it is double here (export ?GetRelativeHealth@CArchGroup@@QBENXZ; retail QBEMXZ).
    double GetRelativeHealth() const;
    void Kill(DamageEntry::SubObjFate FateValue);
    DamageEntry::SubObjFate GetFate() const;
    // Common.dll.c 0x40130. True once State is 2 (killed) or 3 (destroyed).
    bool IsDead() const;
    // .text:062a0150. True once State is 3 (destroyed).
    bool IsDestroyed() const;
    // Common.dll.c 0x40280. Fills Out.Id from Arch->Id and Out.Health from
    // HitPts / Arch->HitPts (same ratio as GetRelativeHealth).
    void GetCollisionGroupDesc(CollisionGroupDesc& Out) const;
    CacheString const& Name() const;
    // .text:062a0120: Arch->Id (word at CollisionGroup+0x04), no null check.
    unsigned short GetID() const;
    // Common.dll.c 0x402a0. -1 once State==3 (destroyed), else RootIndex.
    long GetRootIndex() const;
    // Common.dll.c 0x402b0. False once State==3; else IsDescendant(RootIndex, InstIndex).
    bool IsInstInGroup(long InstIndex) const;
    // Common.dll.c 0x40300. Out=(0,0,0); false once State==3 or RootIndex==-1; else
    // Out=PhySys::GetVelocity(Owner), true.
    bool GetVelocity(Vector& Out) const;
    // Listing 0x40360 (?GetCenterOfMass@CArchGroup@@QBE_NAAVVector@@@Z, export 1511).
    // Out=(0,0,0); false once State==3 or RootIndex==-1. Otherwise populates the
    // bBoundsValid/BoundRadius/BoundCenter cache once via GetBoundingSphere, then returns
    // the cached (root-local) center re-based into world space through
    // IEngine::GetInstanceTransform(RootIndex) [+0xa0] + I3DMathEngine::TransformXform
    // [+0x28] -- the same pair CExternalEquip::GetConnectionPosition uses.
    bool GetCenterOfMass(Vector& Out) const;
    // Listing 0x40400 (?GetRadius@CArchGroup@@QBE_NAAM@Z, export 1636). Same guards and the
    // same one-shot cache fill, but hands back BoundRadius untransformed (retail never
    // re-scales it).
    bool GetRadius(float& Out) const;
    // Common.dll.c 0x40690. -1 once State==3; else Hierarchy::GetDepth(RootIndex, InstIndex).
    int InstDepthInGroup(long InstIndex) const;
    float GetHitPoints() const;
    void SetHitPoints(float NewHitPts);
    // .text:062a0200: the exact FILD value stays in ST0 and retail consumers use it there
    // (Server.dll 06cebb73 FDIV ST0,ST1, 06d01408 FMUL; Common 062ab5f4 FADD), so it is double
    // here (export ?GetMaxHitPoints@CArchGroup@@QBENXZ; retail QBEMXZ).
    double GetMaxHitPoints() const;
    unsigned GetCollisionGroupType() const;
    CObject* RetreiveDestroyedObj();
    bool Separable() const;
    bool DestroysParent() const;
    bool IsRootHealthProxy() const;
    ID_String const& ExplosionTypeName() const;
    ID_String const& DebrisTypeName() const;
    float ExplosionParentImpulse() const;
    float ExplosionGroupImpulse() const;
    unsigned LinkedEquipmentType() const;
    float LinkedEquipmentDamage() const;
    float ExplosionResistance() const;
    // Common.dll.c 0x40740/0x40770: reinterpret Arch+0x40 / Arch+0x4c directly as
    // Archetype::DamageObjInfoList (no null-check on Arch in retail -- ports that as-is).
    Archetype::DamageObjInfoList const& ParentDamageObjs() const;
    Archetype::DamageObjInfoList const& GroupDamageObjs() const;
    // Common.dll.c 0x40760: reinterpret Arch+0x58 as Archetype::FuseIgnitionList (no null-check).
    Archetype::FuseIgnitionList const& FuseIgnitions() const;

protected:
    // Listing 0x40450 (?Destroy@CArchGroup@@IAEXXZ): protected, called by CArchGroupManager.
    void Destroy();
    // .text:062a0e00 (?LoadDamageObjs@CArchGroup@@IAEXXZ), called at the end of the ctor
    // (062a01cf): for every entry of the parent then the group damage-object list, look the
    // archetype up with Archetype::GetSimple and, when found, call its vtable +0x0c (load).
    void LoadDamageObjs();
    // .text:062a0e70 (?FreeDamageObjs@CArchGroup@@IAEXXZ), tail-jumped from the dtor
    // (062a01f2): the same two walks, calling Archetype::Root::Release instead.
    void FreeDamageObjs();
    // .text:062a0c10 (?AttachDamageObjs@CArchGroup@@IBEXPAUCObject@@ABVDamageObjInfoList@
    // Archetype@@@Z, ordinal 1217, protected const). For every {hardpoint, archetype} entry
    // whose hardpoint exists on Target and is not yet recorded on the owner: records it, spawns
    // a CObject of that archetype, connects its "DpConnect" to the hardpoint and, when that
    // succeeds, builds its physics body and attaches it to Target. Called by Destroy
    // (062a0477, 062a05f1).
    void AttachDamageObjs(CObject* Target, Archetype::DamageObjInfoList const& List) const;
    // Listing 0x407a0 (?GetBoundingSphere@CArchGroup@@IBEXAAMAAVVector@@@Z, export 1508).
    // Builds a world-space AABB over the group's root instance extent plus every descendant
    // instance whose Owner->inst_to_subobj_id() equals this group's Arch->Id, reduces it to
    // a centre/radius, refines the radius against every contributing child sphere, and
    // finally maps the centre back into the root instance's local frame. Only ever called
    // through the GetCenterOfMass/GetRadius cache fill above.
    void GetBoundingSphere(float& OutRadius, Vector& OutCenter) const;

public:
    CEqObj* Owner;                              // +0x00
    Archetype::CollisionGroup const* Arch;      // +0x04
    int RootIndex;                              // +0x08 FindChild, -1
    float HitPts;                               // +0x0c
    int Field10;                                // +0x10 TODO: the ctor writes 1 (062a019d); no reader found in Common
    int State;                                  // +0x14 1 alive / 2 killed / 3 destroyed
    int Fate;                                   // +0x18
    CObject* DestroyedObj;                      // +0x1c
    // +0x20..+0x33 was `unsigned char Opaque20[0x14]`. The ctor (listing 062a0192-062a01ab)
    // zeroes exactly the byte at +0x20 and the four dwords at +0x24/+0x28/+0x2c/+0x30 and
    // leaves the padding +0x21..+0x23 untouched, which pins the split below; GetCenterOfMass
    // (0x40360) and GetRadius (0x40400) then test the byte at +0x20 and call GetBoundingSphere
    // with &(+0x24) and &(+0x28) as its two out-params. All three are declared const in
    // retail, so the cache is mutable.
    mutable bool bBoundsValid;                  // +0x20
    mutable float BoundRadius;                  // +0x24
    mutable Vector BoundCenter;                 // +0x28 .. +0x33
};

static_assert(sizeof(CArchGroup) == 0x34, "CArchGroup sizeof 0x34");
static_assert(offsetof(CArchGroup, Arch) == 0x04, "CArchGroup+0x04 CollisionGroup*");
static_assert(offsetof(CArchGroup, HitPts) == 0x0c, "CArchGroup+0x0c hit_pts");
static_assert(offsetof(CArchGroup, State) == 0x14, "CArchGroup+0x14 state");
static_assert(offsetof(CArchGroup, bBoundsValid) == 0x20, "CArchGroup+0x20 bounds-cache valid flag");
static_assert(offsetof(CArchGroup, BoundRadius) == 0x24, "CArchGroup+0x24 cached bound radius");
static_assert(offsetof(CArchGroup, BoundCenter) == 0x28, "CArchGroup+0x28 cached bound center");

// Stateful cursor for CArchGroupManager::Traverse(): an "at start" flag (+0x00) plus the
// current position (+0x04, retail a VC6 _Tree node pointer advanced by _Tree::_Inc
// FUN_063429f0). The port keeps the current group's map key instead of a node pointer:
// a std::map iterator is not trivially copyable in Debug builds, which made the compiler
// export a copy ctor, a dtor and move members that retail does not have. Retail exports
// exactly ??0 (default), ??4 (copy assignment) and Restart; the defaulted copy assignment
// below keeps the implicit move members from being declared.
class COMMON_API CArchGrpTraverser
{
public:
    CArchGrpTraverser();  // .text:06280ec0
    CArchGrpTraverser& operator=(CArchGrpTraverser const&) = default;
    void Restart();       // .text:06280ed0

private:
    friend class CArchGroupManager;
    bool AtStart;               // +0x00
    unsigned short CurrentKey;  // retail +0x04: node pointer; here the node's key
};

// sizeof 0x18 at CEqObj+0x144. Retail embeds a VC6 std::map at +0x04 (allocator and
// key_compare bytes, head node at +0x08, the tree's own nil node at +0x0c, _Multi at +0x10,
// size at +0x14; ctor .text:06280b60). The port keeps a heap std::map* in the head slot.
class COMMON_API CArchGroupManager
{
public:
    CArchGroupManager();
    ~CArchGroupManager();

    // Common.dll.c 0x20bd0 / 0x20d30. Retail's copy ctor/operator= both deep-copy the
    // Groups tree structure but only shallow-copy each entry's CArchGroup* value (same
    // semantics std::map<unsigned short, CArchGroup*>'s own copy ctor/operator= already
    // give us) -- two CArchGroupManagers can end up pointing at the same CArchGroup
    // instances after a copy, same as retail. No caller of CEqObj::CEqObj(CEqObj const&)
    // (the only place that copy-constructs a CArchGroupManager) exists anywhere in
    // Common.dll, freelancer.exe, or Server.dll -- this is exported (dllexport pulls in
    // all special members) but dead in practice.
    CArchGroupManager(CArchGroupManager const& Other);
    CArchGroupManager& operator=(CArchGroupManager const& Other);

    bool Init(CEqObj* OwnerObj);
    bool AddNewArchGroup(CArchGroup* Group);
    int Clear();
    // Common.dll.c 69248-69266 (0x417a0). Walks every group via Traverse() (a fresh
    // traverser), counting each whose State isn't Killed(2) or Destroyed(3).
    int NumLiving() const;
    bool DestroyDeadGroups();
    // Common.dll.c 69316-69349 (0x418b0). Unlike DestroyDeadGroups/DestroyGroups (which
    // Destroy() a group in place but leave its map entry around), this actually erases the
    // entry: finds it by GroupId, explicitly destructs+frees the CArchGroup object (matching
    // retail's explicit ~CArchGroup + free-list call), then removes the map entry entirely.
    bool DeleteArchGroup(unsigned short GroupId);
    // Common.dll.c 0x62a1960. Kills groups named by List's ArchGroupID entries, then
    // sweeps Groups for anything left in the Killed state and destroys it (same tail as
    // DestroyDeadGroups, but this also runs DestroyLowerGroups first, see .cpp).
    bool DestroyGroups(DamageList const& List, bool Param2);
    CArchGroup* FindByID(unsigned short GroupId);
    CArchGroup const* FindByID(unsigned short GroupId) const;  // retail QBE overload, same body .text:062a1540
    // .text:06263bb0 (ICF fold): Count at +0x14.
    int Size() const;
    // Common.dll.c 0x62a1460 (69026-69053... actually 68995-69020). Looks a group up via the
    // same tree-find as FindByID/FindByName; if not found returns false, else returns
    // (Group->State == 3) when CheckDestroyed, else true.
    bool IsOnMap(unsigned short GroupId, bool CheckDestroyed) const;
    // Common.dll.c 0x62a14e0. Linear scan (unlike FindByID, Groups isn't name-indexed);
    // matches by CacheString pointer identity (interned strings, same as retail's raw
    // pointer compare) and skips groups already Killed/Destroyed (State 2 or 3).
    CArchGroup* FindByName(CacheString const& Name);
    CArchGroup const* FindByName(CacheString const& Name) const;  // retail QBE overload identical/ICF-folded into 0x62a14e0
    CArchGroup* Traverse(CArchGrpTraverser& Traverser);              // Common.dll.c 0x62a15a0
    CArchGroup const* Traverse(CArchGrpTraverser& Traverser) const;  // retail QBE overload identical/ICF-folded into 0x62a15a0
    // Common.dll.c 0x62a1600. Finds the group whose InstDepthInGroup(InstIndex) is smallest
    // (nearest ancestor) among all groups where it isn't -1, returns that group's GetID(), or 0
    // if InstIndex==-1 or no group contains it.
    unsigned short InstToSubObjID(long InstIndex) const;

protected:
    // Common.dll.c 0x62a1c10. Retail's DestroyDeadGroup(CArchGroup&) thunk: DestroyLowerGroups then Destroy.
    void DestroyDeadGroup(CArchGroup& Group);
    // Common.dll.c 0x62a1ac0. Force-destroys any OTHER group in Groups that both (a) sits
    // under DeadGroup's instance (IsInstInGroup on DeadGroup, indexed by the entry's own
    // GetRootIndex()) and (b) isn't itself already Killed/Destroyed -- an inconsistent
    // "UnDead lower group" retail logs and force-deletes. An entry already Killed/Destroyed
    // recurses (its own lower groups first) then gets Destroy()'d normally instead.
    void DestroyLowerGroups(CArchGroup const& DeadGroup);

    // Retail `protected: static int const CArchGroupManager::MAX_GROUP_DEPTH`, export ordinal
    // 1835, .rdata 0x0639d9dc = 0x7fff. No reader exists anywhere in Common.dll -- it is
    // exported but dead in retail, ported here for symbol parity. Note the neighbouring
    // .rdata string is "E:\FL\Scratch\Source\Common\CArchGroup...", which is what ties this
    // otherwise anonymous dword to the DestroyLowerGroups recursion this class performs.
    static int const MAX_GROUP_DEPTH;

public:
    CEqObj* Owner;                                      // +0x00
    unsigned char TreeAllocator;                        // +0x04 never read
    unsigned char TreeCompare;                          // +0x05 never read
    std::map<unsigned short, CArchGroup*>* Groups;      // +0x08 retail: tree head node
    unsigned TreeNilSlot;                               // +0x0c retail: nil node*; unused here
    bool bTreeMulti;                                    // +0x10 _Multi, false for a map
    unsigned Count;                                     // +0x14 the map's size
};

static_assert(sizeof(CArchGroupManager) == 0x18, "CArchGroupManager sizeof 0x18");
static_assert(offsetof(CArchGroupManager, Groups) == 0x08, "CArchGroupManager+0x08 map");
static_assert(offsetof(CArchGroupManager, bTreeMulti) == 0x10, "CArchGroupManager+0x10 _Multi");
static_assert(offsetof(CArchGroupManager, Count) == 0x14, "CArchGroupManager+0x14 size");

// Return type of CEqObj::activate (Common.dll.c 78735-78751, ?activate@CEqObj@@UAE?AW4...).
// Retail never names these values; the names are ours. The numbers come from the success path
// of CEqObj::activate (.text:062ab16e..062ab17d) and CSolar::activate (.text:062b7d0c..
// 062b7d1f): `CMP new,old; SETNZ; TEST; SETZ AL; INC EAX`, i.e. 1 when the stored byte changed
// and 2 when it already held the requested value. Every failure path returns 0; CSolar's
// lane-ring branch (.text:062b7d2a) returns 1.
enum ObjActivateResult
{
    ObjActivateFailed = 0,
    ObjActivateChanged = 1,
    ObjActivateNoChange = 2,
};

struct COMMON_API CEqObj : public CSimple
{
public:
    // CEqObj::CreateParms extends CSimple::CreateParms (init 0x492e0).
    struct CreateParms : CSimple::CreateParms
    {
        unsigned Pad58;           // +0x58
        unsigned DockingCount;    // +0x5c → CShip +0x160
        unsigned VibeId;          // +0x60 → this+0x104 (stored as int)
        Costume SpaceCostume;     // +0x64 → this+0x108 (13 dwords)
        unsigned VoiceId;         // +0x98 → this+0x13c  // formerly VibeFields[5]/RankSrc/RankCount/Field98
        EquipDescVector Equipment; // +0x9c load_equip_and_cargo
        // Retail +0xac: a std::list<CollisionGroupDesc> (VC6: allocator, head, size). The
        // export ?load_arch_groups@CEqObj@@QAEXABV?$list@UCollisionGroupDesc@@...@std@@@Z takes
        // it, CEqObj::init passes Parms+0xac, and CSolar::ReadObj hands the same address to
        // Archetype::EqObj::get_undamaged_collision_group_list (.text:062b808e-062b8097).
        // A modern std::list is smaller than VC6's, so every offset from here on is retail's,
        // given in the comments only; the fields are reached by name.
        std::list<CollisionGroupDesc> ArchGroupDescs; // retail +0xac load_arch_groups
        // Retail +0xb8, an ID_String. Read by CSolar::init (0x57560, Common.dll.c 89077/89113)
        // to seed CSolar+0x1a4 and to name the object in its "dockable but no destination"
        // warning. CEqObj/CShip themselves never read it -- only CSolar does.
        ID_String NicknameSrc;
        // Retail +0xbc..+0xe7: written by CSolar::ReadObj (0x57ff0) and read back by
        // CSolar::init (0x57560). Named after the CSolar fields they seed.
        unsigned char VisitValue;        // retail +0xbc  "visit" -> CSolar::VisitValue
                                         // retail +0xbd..+0xbf: alignment
        unsigned ParentNicknameId;       // retail +0xc0  "parent" -> CSolar::ParentNickname
        unsigned char bDestructible;     // retail +0xc4  "destructible" -> CSolar::bDestructible
        unsigned char bDynamic;          // retail +0xc5  -> CSolar::bDynamic
                                         // retail +0xc6/+0xc7: alignment
        unsigned DestSystem;             // retail +0xc8  "goto" system -> CSolar::DestSystem
        unsigned DestGateId;             // retail +0xcc  "goto" gate -> CSolar::DestGate
        unsigned PrevRing;               // retail +0xd0  "prev_ring"
        unsigned NextRing;               // retail +0xd4  "next_ring"
        unsigned TradelaneSpaceNameId;   // retail +0xd8  "tradelane_space_name" (an int in the ini)
        float AtmosphereRange;           // retail +0xdc  "atmosphere_range"
        unsigned DockWithBaseId;         // retail +0xe0  "dock_with" -> CSolar::init_docking_points
        // Retail's CEqObj::CreateParms ends here, at +0xe4. Both extensions start at that
        // boundary with unrelated content: CSolar::CreateParms puts its anim-name std::list
        // there (+0xe4..+0xef), CShip::CreateParms its own fields (+0xe8..+0xf4). Server's
        // ~CSolar::CreateParms FUN_06d0a120 destroys the list at +0xe4 and only then the
        // base's +0xac list and +0x9c vector (FUN_06d0a080 is the base part alone).
    };

    static_assert(offsetof(CreateParms, DockingCount) == 0x5c, "CEqObj::CreateParms+0x5c");
    static_assert(offsetof(CreateParms, VibeId) == 0x60, "CEqObj::CreateParms+0x60");
    static_assert(offsetof(CreateParms, SpaceCostume) == 0x64, "CEqObj::CreateParms+0x64 costume");
    static_assert(offsetof(CreateParms, VoiceId) == 0x98, "CEqObj::CreateParms+0x98 voice");
    static_assert(offsetof(CreateParms, Equipment) == 0x9c, "CEqObj::CreateParms+0x9c EquipDescVector");
    static_assert(offsetof(CreateParms, ArchGroupDescs) == 0xac, "CEqObj::CreateParms+0xac CollisionGroupDesc");

protected:
    // Retail: ??0CEqObj@@IAE@W4Class@CObject@@@Z (I = protected).
    // Default argument: CEqObj::`default constructor closure' (.text:06287600) pushes 0x103.
    explicit CEqObj(Class ObjectClass = ClassEqObj);

public:
    // Common.dll.c 0x272c0. Retail's version is a real, non-trivial member-wise copy (it
    // even re-copy-constructs ArchGroups' embedded map), but it has zero callers anywhere
    // in Common.dll, freelancer.exe, or Server.dll -- CEqObj is a dllexport class, so MSVC
    // emits and exports every special member whether or not the game ever uses it. Ported
    // as a straightforward member-wise copy (base + every field) rather than reverse
    // engineering retail's byte-exact shuffling, since that would require porting several
    // more never-called helper functions purely to replicate dead code.
    CEqObj(CEqObj const& Other);

protected:
    // Retail: ??1CEqObj@@MAE@XZ and ?init@CEqObj@@MAEXABUCreateParms@1@@Z (M = protected
    // virtual). CShip::init and CSolar::init reach init through the qualified base call.
    virtual ~CEqObj();

    CEqObj& operator=(CEqObj const&) = delete;

    virtual void init(CreateParms const& Parms);

public:
    virtual void load_equip_and_cargo(EquipDescVector const& Items, bool bClear);
    virtual void clear_equip_and_cargo();
    virtual bool add_item(EquipDesc const& Desc);
    // Common.dll.c 77809-77925 (0x49f00, ?get_equip_desc_list@CEqObj@@UBEXAAUEquipDescVector@@@Z).
    // Reserves OutList for (ExternalCount + InternalCount) more elements, then appends one
    // EquipDesc per equip whose GetEquipDesc (vtable+0x28) returns true -- the inlined VC6
    // reserve and insert(end(), 1, x) with their own growth.
    virtual void get_equip_desc_list(EquipDescVector& OutList) const;

protected:
    // Retail: ?alloc_equip@CEqObj@@MAEPAVCEquip@@GPAUEquipment@Archetype@@_N@Z (protected).
    virtual CEquip* alloc_equip(unsigned short EquipId, Archetype::Equipment* Arch,
                                bool bMissionFlag);

public:
    // Listing 0x4a420. Returns archetype nickname ID (overrides CSimple).
    virtual unsigned get_name() const;
    // .text:06287240: `(FlagsA0 & 1) && !(CloakPercent >= 1.0f)` (FCOMP vs DAT_0639a1d0 = 1.0f,
    // TEST AH,1 -> true when CloakPercent is below it or NaN).
    virtual bool is_targetable() const override;
    // Overrides of CObject's +0x5c / +0x60 (CEqObj vftable 0x0639beb8 / 0x0639bebc).
    virtual void disable_controllers() override;
    virtual void enable_controllers() override;
    virtual void flush_animations();
    // Overrides CSimple::connect/disconnect(IObjDB*) -- a separate overload from the
    // INotify* connect/disconnect pair below (different retail addresses/vtable slots).
    virtual void connect(IObjDB* ObjDb);
    virtual void disconnect(IObjDB* ObjDb);
    virtual void connect(INotify* NotifyParam);
    virtual void disconnect(INotify* NotifyParam);
    virtual void notify(INotify::Event EventParam, void* Context);
    // Common.dll.c 0x4a3d0 (140474-140510). A THIRD, distinct disconnect overload (different
    // retail vtable slot from both the IObjDB* and INotify* pairs above). When the object
    // BehaviorMgr's +0xF0 watcher holds is Watcher, the manager is destroyed (FUN_062d2530,
    // ~IBehaviorManager) and freed, and BehaviorMgr is cleared.
    virtual void disconnect(IObjRW* Watcher);
    // .text:062aa2f0: CEShield::DestroyIntruderChecker on every shield equip, then
    // CSimple::unmake_physical.
    virtual void unmake_physical() override;
    // .text:062aa360: CObject::remake_physical, then CEShield::AttachIntruderChecker on every
    // shield equip.
    virtual void remake_physical(PhySys::CreateParms const& Parms, float Radius) override;
    // .text:062aa520: base update, per-equip Update, the behavior manager's tick
    // (IBehaviorManager::UpdateBehaviorTick, FUN_062d2b10) when allowed, docking animations.
    virtual int update(float Dt, unsigned Flags) override;
    // Common.dll.c 0x4b130 (78735-78751). Toggles DockingPoints[DockIndex]'s activation flag;
    // DockingPoints is populated 1:1 with Archetype::EqObj::DockingSpheres by
    // init_docking_points, so its own size() faithfully replaces retail's raw archetype
    // begin/end pointer-count check.
    virtual ObjActivateResult activate(bool bOn, unsigned DockIndex);
    // Common.dll.c 0x4b190 (78762-78843). Appends one bool per DockingPoints entry
    // (bActive, the byte CEqObj::activate writes) and returns true
    // if any docking point is active. Retail's own hand-inlined vector<bool> push_back
    // growth machinery collapses to std::vector<bool>::push_back here.
    // ?get_activate_state@CEqObj@@UAE_NAAV?$vector@_N...@std@@@Z -- not const in retail.
    virtual bool get_activate_state(std::vector<bool>& OutStates);
    // .text:062ab330: Out = get_velocity() first; then CExternalEquip::GetVelocity (+0x6c) or
    // CArchGroup::GetVelocity into a temporary that overwrites Out on success.
    virtual bool get_sub_obj_velocity(unsigned short SubObjId, Vector& Out) const;
    // Listing 0x4b3f0 (062ab3f0..062ab4b8). Now fully wired, both branches. The two reasons
    // this was previously left as a bare `return false` were both stale by the time they were
    // re-checked: the equip branch's +0x70 slot is CExternalEquip::GetCenterOfMass (declared
    // in common_cequip.h, overridden by CAttachedEquip), and CArchGroup::GetCenterOfMass now
    // exists (common_carchgroup.cpp, listing 0x40360). Retail leaves Out untouched on failure.
    virtual bool get_sub_obj_center_of_mass(unsigned short SubObjId, Vector& Out) const;
    // Common.dll.c 0x4b820 (79147-79179). Fully faithful: both GetMaxHitPoints (CEquip base
    // virtual) and CArchGroup::GetMaxHitPoints are real, unambiguous calls.
    virtual bool get_sub_obj_max_hit_pts(unsigned short SubObjId, float& Out) const;
    // Common.dll.c 0x4b8a0 (79185-79223). Fully faithful: GetRelativeHealth (CEquip base
    // virtual), CArchGroup::GetRelativeHealth, and the SubObjId==1 CSimple::get_relative_health
    // fallback are all real, unambiguous calls.
    virtual bool get_sub_obj_relative_health(unsigned short SubObjId, float& Out) const;
    // Common.dll.c 0x4b990 (79252-79291). Equip branch uses CAttachedEquip::cast then calls
    // vtable+0x84 -- the same slot already identified as GetRootIndex()-equivalent by
    // CAttachedEquip::IsInstOnEquip's own class comment (common_cequip.h), reused here rather
    // than re-deriving it. ArchGroup and SubObjId==1 (EngineInstanceId) branches are direct.
    virtual long sub_obj_id_to_inst(unsigned short SubObjId) const;
    // Common.dll.c 0x4c240 (79688-79721). Retail allocates a 0x1cc-byte IBehaviorManager via
    // FUN_062d2220(Owner), which binds Owner into the manager's +0xf0 watcher (the same
    // sub-object disconnect(IObjRW*) above refers to) and builds its PathfindManager; the
    // port's IBehaviorManager(IObjRW*) does both. Then, whether the manager was just built or
    // already existed, ConnectBehaviorManagerObjectDb(ObjectDb) (FUN_062d2830, .text:062ac2ab)
    // and set_state_graph(GraphId, false).
    IBehaviorManager* create_behavior_interface(IObjRW* Owner, int GraphId);
    // Common.dll.c 0x49920 (139295-139435, ?sync_cargo@CEqObj@@QAE_NABVEquipDescList@@@Z).
    // First pass: walk existing CECargo equips (CEquipTraverser filtered to EquipTypeCargo),
    // zeroing Count for anything missing from List, else syncing Count/HitPoints from it.
    // Second pass: for every non-equipped List entry with no existing equip of that Id, spawn
    // a new CECargo (mirrors add_cargo_item). Server.dll (src/Server/IServerImpl.cpp) has
    // call-site comments naming this exact method; not yet wired up there.
    bool sync_cargo(EquipDescList const& List);

    // 0x06262d50 (ICF fold, `MOV EAX,[this+0x88]; RET`): ?eqobjarch@CEqObj@@QBEPAUEqObj@Archetype@@XZ
    // -- a const method returning a non-const pointer.
    Archetype::EqObj* eqobjarch() const;
    // .text:062a9590: AttachedDamageObjs.find(Name) != end().
    bool is_damaged_obj_attached(CacheString const& Name) const;
    // .text:062a95f0: AttachedDamageObjs.insert(Name).
    void attaching_damaged_obj(CacheString const& Name);

protected:
    // Retail: ?add_equipped_item@CEqObj@@IAE_NABUEquipDesc@@@Z / ?add_cargo_item@... (I =
    // protected). link_shields is declared with the protected virtuals at the end of the class.
    bool add_equipped_item(EquipDesc const& Desc);
    bool add_cargo_item(EquipDesc const& Desc);

public:
    void load_arch_groups(std::list<CollisionGroupDesc> const& Descs);
    // Common.dll.c 0x494f0. Thin wrapper: ArchGroups.Clear().
    void clear_arch_groups();
    // Common.dll.c 0x49500 (?get_collision_group_description@CEqObj@@QBEXAAV?$list@...).
    // Appends one CollisionGroupDesc per live arch group (via ArchGroups.Traverse +
    // CArchGroup::GetCollisionGroupDesc) to the caller-supplied list. Retail returns a
    // real std::list<CollisionGroupDesc>&, ported as-is (see common_casteroid.h scope
    // note on using real std::list/std::map where retail hand-rolled its own).
    void get_collision_group_description(std::list<CollisionGroupDesc>& OutDescs) const;
    // Listing 0x4a640. Reputation-based attitude towards another CEqObj.
    float attitude_towards(CEqObj const* Other) const;
    // Listing 0x4a600. Bidirectional reputation-based attitude.
    void attitude_towards_symmetrical(float& OutAtt, CEqObj const* Other,
                                      float& OutReverse) const;
    // Listing 0x4a980. Resolves a docking/launch hardpoint by index to a world position and
    // orientation (spread-adjusted); false + this object's own transform when HardpointIndex
    // is out of range. See common_ceqobj.cpp for the retail asm trace.
    bool launch_pos(Vector& OutPos, Matrix& OutOrient, int HardpointIndex) const;
    // Listing 0x4bd50. Lazily-cached bounding sphere used by the explosion-damage-impulse
    // system; see compute_explosion_dmg_bounding_sphere for the one-time computation.
    bool get_explosion_dmg_bounding_sphere(float& OutRadius, Vector& OutCenter) const;
    // ?get_behavior_interface@CEqObj@@QAEPAVIBehaviorManager@@XZ -- not const in retail.
    IBehaviorManager* get_behavior_interface();

    void set_control_exclusion(unsigned int Flags);
    bool is_control_excluded(unsigned int Flags) const;
    float get_cloak_percentage() const;
    // Common.dll.c 0x27210. CloakPercent >= 1.0f (fully cloaked). Retail returns int 1/0
    // (?is_cloaked@CEqObj@@QBEHXZ; .text:06287223 MOV EAX,1 / .text:06287229 XOR EAX,EAX).
    int is_cloaked() const;
    unsigned get_base() const;
    bool is_base() const;
    unsigned int const& get_dock_target() const;
    bool is_dock() const;
    float get_power() const;
    void set_power(float NewPower);  // Common.dll.c 0x276c0
    float get_max_power() const;
    // Common.dll.c 0x276e0. Power / MaxPower, or 0 if MaxPower <= 0.
    float get_power_ratio() const;
    int get_vibe() const;
    // Common.dll.c 0x4ba50 (?destroy_sub_objs@CEqObj@@UAEXABUDamageList@@_N@Z, 143037-143062
    // in the raw listing -- thin, not separately decompiled). Forwards to the private
    // destroy_equipment() helper below, then CArchGroupManager::DestroyGroups(List, Param2).
    // The only caller found in this port's own tracker graph is destroy_equipment's raw-asm
    // XREF back to this function; no other in-repo caller yet (likely invoked by an unported
    // damage/death path elsewhere in Common.dll or by another module -- see this project's
    // "no dead code by absence" rule).
    virtual void destroy_sub_objs(DamageList const& List, bool Param2);
    // Common.dll.c 0x4ba80 (?enumerate_sub_objs@CEqObj@@UBEHXZ). Sum of external+internal
    // attached-equipment counts plus the live arch-group count -- the total number of
    // destructible sub-objects this CEqObj currently exposes.
    virtual int enumerate_sub_objs() const;
    // Common.dll.c 0x4b100 (?get_base_name@CEqObj@@QBEIXZ). DockingBaseId's nickname ID
    // via Universe::get_base(), or 0 when not docked/DockingBaseId's low 30 bits are unset.
    unsigned get_base_name() const;
    // Common.dll.c 0x4b4c0/0x4b570/0x4b620. Sum this object's own hit points/max hit points
    // (or ratio) with every attached CAttachedEquip-mask (0x1ee0) equip's own
    // GetHitPoints()/GetMaxHitPoints(), plus every non-Killed/Destroyed arch group's
    // GetHitPoints()/GetMaxHitPoints().
    virtual float get_total_hit_pts() const;
    virtual float get_total_max_hit_pts() const;
    virtual float get_total_relative_health() const;
    // Common.dll.c 0x4b720/0x4b7a0. SubObjId routes to either an equip (CEquipManager::
    // FindByID + GetHitPoints/SetHitPoints) or an arch group (CArchGroupManager::FindByID +
    // CArchGroup::GetHitPoints/SetHitPoints); false if SubObjId matches neither category or
    // isn't found.
    virtual bool get_sub_obj_hit_pts(unsigned short SubObjId, float& OutHitPts) const;
    virtual bool set_sub_obj_hit_pts(unsigned short SubObjId, float HitPts);
    // Common.dll.c 0x4b940 (?inst_to_subobj_id@CEqObj@@UBEGJ@Z). Engine instance handle ->
    // SubObjId: the root instance (EngineInstanceId) always yields 1; otherwise tries
    // EquipManager.InstToSubObjID() then ArchGroups.InstToSubObjID(), falling back to 1.
    virtual unsigned short inst_to_subobj_id(long Instance) const;

protected:
    // Common.dll.c 0x4bbe0, ?link_shields@CEqObj@@MAEXXZ: protected VIRTUAL in retail, the
    // last slot of ??_7CEqObj@@6B@ (.rdata:0639bf64, right after alloc_equip). CShip's and
    // CSolar's vtables keep CEqObj's entry (.rdata:0639c134 / 0639d4cc) -- no override.
    virtual void link_shields();

    void init_docking_points(unsigned int BaseId);
    // Listing 0x4bda0. One-time computation behind get_explosion_dmg_bounding_sphere's cache.
    void compute_explosion_dmg_bounding_sphere(float& OutRadius, Vector& OutCenter) const;

private:
    // Common.dll.c 78494-78550 (raw asm 0x062aab50-0x062aad29, private in retail). Called
    // from update() every tick; see common_ceqobj.cpp for the full trace.
    void update_docking_animations(float Dt);
    // Common.dll.c 79321-79380 (raw asm 0x062abaa0-0x062abbc7, private in retail). Called by
    // the public destroy_sub_objs() virtual above; see common_ceqobj.cpp for the full trace.
    void destroy_equipment(DamageList const& Damage, bool bParam2);

public:

    CEquipManager EquipManager;     // +0xe4
    int VibeId;                   // +0x104  Reputation group id (get_vibe returns int; -1 = none)
    // +0x108..+0x13b: the object's space costume (13 dwords: four part ids, eight accessory
    // ids, the accessory count). pub::SpaceObj::SendComm (Server.dll 06cf931a) takes
    // `LEA ESI,[EqObj+0x108]` as the default Costume const* and copies 0xd dwords; CEqObj::init
    // stores the count unclamped.  // formerly Vibe108/Vibe10c/Vibe110/Vibe114/RankCopy/RankCount
    Costume SpaceCostume;
    unsigned VoiceId;             // +0x13c  SendComm's default voice (06cf930c)  // formerly Field13c
    // +0x140: the cloak fraction 0..1 (get_cloak_percentage 06287230 returns it; CECloakingDevice
    // update 06294738 stores cloak_percent() here; >= 1.0 = fully cloaked).  // formerly Field140
    float CloakPercent;
    CArchGroupManager ArchGroups; // +0x144
    unsigned char bWeaponsOffline; // +0x15c  CELauncher::IsDisabled gate (+0x15d..: padding)
    unsigned DockingBaseId;       // +0x160
    // Retail name CEqObj::DockAnimInfo: ctor .text:06287270 (byte +0 = 0, +4 = -1, +8 = 0,
    // +0xc = 0), operator= .text:06287290 (four-dword copy, ICF-folded with
    // CEquipTraverser::operator=). CEqObj::activate (.text:062ab130) reads and writes +0 as a
    // byte (`MOV DL,[EAX+ESI]` / `MOV [EAX],CL`), so it is a bool, not a dword of flags.
    struct COMMON_API DockAnimInfo
    {
        DockAnimInfo();
        DockAnimInfo& operator=(DockAnimInfo const&) = default;

        bool bActive;                                      // +0x00 (+0x01..+0x03: alignment, the ctor leaves them)
        int AnimChannel;                                   // +0x04 (-1 if none/failed)
        // +0x08: NOT padding -- CEqObj::update_docking_animations (Common.dll.c 78494-78550)
        // reads and rewrites this every call: it caches the previous frame's Anim
        // GetStoredDuration() (vtable slot 16) result for its threshold comparisons.
        float LastDuration;                                // +0x08
        Archetype::EqObj::DockingSphere const* ArchPoint;  // +0x0C (pointer into Archetype)
    };

    unsigned DockingPointBaseId;  // +0x164
    struct INotify* Notify;       // +0x168
    // get_explosion_dmg_bounding_sphere's lazy cache (Common.dll.c 0x4bd50/0x4bda0). Both
    // accessors are const in retail (QBE/IBE...const), so the cache fields are mutable.
    // +0x16c is a byte: ctor 062a90fa `MOV byte [ESI+0x16c],BL`, getter 062abd56 `MOV AL,byte`
    // / 062abd70 `MOV byte ..,1`, copy ctor 0628750f byte copy. The 3 bytes after it are
    // never written by any of them.
    mutable bool bExplosionDmgCached;        // +0x16c  false = not yet computed (+0x16d..: padding)
    mutable float ExplosionDmgRadius;        // +0x170  local-space (instance-relative) radius
    mutable Vector ExplosionDmgCenter;       // +0x174  local-space (instance-relative) center
    std::vector<DockAnimInfo> DockingPoints; // +0x180
    unsigned int ControlExclusion; // +0x190
    IBehaviorManager* BehaviorMgr; // +0x194
    float Power;                  // +0x198  set to MaxPower at init
    float MaxPower;               // +0x19c  CEPower arch+0x70 sum
    int BehaviorIndex;            // +0x1a0  -1; the behaviour index set by the behaviour manager (062d35a9), read when no manager is attached  // formerly Field1a0
};

static_assert(sizeof(CEqObj) == 0x1a4, "CEqObj sizeof 0x1a4: CShip's Controller starts there");
static_assert(offsetof(CEqObj, EquipManager) == 0xe4, "CEqObj+0xe4 CEquipManager");
static_assert(offsetof(CEqObj, VibeId) == 0x104, "CEqObj+0x104 vibe");
static_assert(offsetof(CEqObj, SpaceCostume) == 0x108, "CEqObj+0x108 space costume");
static_assert(offsetof(CEqObj, VoiceId) == 0x13c, "CEqObj+0x13c voice id");
static_assert(offsetof(CEqObj, CloakPercent) == 0x140, "CEqObj+0x140 cloak percent");
static_assert(offsetof(CEqObj, ArchGroups) == 0x144, "CEqObj+0x144 CArchGroupManager");
static_assert(offsetof(CEqObj, bWeaponsOffline) == 0x15c, "CEqObj+0x15c IsDisabled gate");
static_assert(offsetof(CEqObj, DockingBaseId) == 0x160, "CEqObj+0x160");
static_assert(offsetof(CEqObj, bExplosionDmgCached) == 0x16c, "CEqObj+0x16c explosion dmg cache flag");
static_assert(offsetof(CEqObj, ExplosionDmgRadius) == 0x170, "CEqObj+0x170 explosion dmg radius");
static_assert(offsetof(CEqObj, ExplosionDmgCenter) == 0x174, "CEqObj+0x174 explosion dmg center");
static_assert(offsetof(CEqObj, ControlExclusion) == 0x190, "CEqObj+0x190 control exclusion flags");
static_assert(offsetof(CEqObj, BehaviorMgr) == 0x194, "CEqObj+0x194 BehaviorMgr");
static_assert(offsetof(CEqObj, Power) == 0x198, "CEqObj+0x198 power");
static_assert(offsetof(CEqObj, MaxPower) == 0x19c, "CEqObj+0x19c max power");
static_assert(offsetof(CEqObj, BehaviorIndex) == 0x1a0, "CEqObj+0x1a0 behaviour index");

// CShip::get_gun_stats return structure. From disasm at 0x514f0/0x514b0/0x51290.
// Retail builds this by traversing EquipTypeGun equips and, per gun, reading its
// Archetype::Gun (== Archetype::Launcher base) fields through whichever of
// CEArmor::ArmorArch/CECounterMeasureDropper::CounterMeasureArch got /OPT:ICF-folded onto
// the call site -- both are byte-identical "return this->Arch" accessors, so either name
// resolves to the same Gun archetype pointer regardless of which one the disassembly names.
// Guns whose archetype RefireDelay == 0 accumulate into ActiveGunCount/AvgMuzzleVelocity/
// MaxMunitionRange below; guns with RefireDelay != 0 accumulate into CmDropperCount/
// CmMaxMunitionRange instead.
struct COMMON_API ShipGunStats
{
    // .text:06287ae0 (ordinal 439): stores 0 into all five dwords +0x00..+0x10. Retail
    // inlines it into CShip's ctor as the GunStats member construction (.text:062b07c9-062b07d8,
    // EH state 0xf).
    ShipGunStats()
        : ActiveGunCount(0)
        , AvgMuzzleVelocity(0.0f)
        , MaxMunitionRange(0.0f)
        , CmDropperCount(0)
        , CmMaxMunitionRange(0.0f)
    {
    }

    // User-declared so that no implicit move members are generated and exported:
    // VC6 had none, and retail exports exactly the copy assignment.
    ShipGunStats& operator=(ShipGunStats const&) = default;

    int ActiveGunCount;        // +0x00
    float AvgMuzzleVelocity;   // +0x04  sum of Launcher::MuzzleVelocity over active guns, averaged
    float MaxMunitionRange;    // +0x08  farthest active gun's CEGun::GetMunitionRange()
    int CmDropperCount;        // +0x0c
    float CmMaxMunitionRange;  // +0x10  farthest CM-type gun's CEGun::GetMunitionRange()

    // Listing 0x27b00: zeroes all five fields (identical body to the retail default ctor
    // at 0x27ae0). CShip::get_gun_stats() ports this inline by assigning every field
    // unconditionally instead of calling clear() first; kept here too for callers that
    // want a bare zeroed instance.
    void clear()
    {
        ActiveGunCount = 0;
        AvgMuzzleVelocity = 0.0f;
        MaxMunitionRange = 0.0f;
        CmDropperCount = 0;
        CmMaxMunitionRange = 0.0f;
    }
};

static_assert(sizeof(ShipGunStats) == 0x14, "ShipGunStats sizeof 0x14");

// Retail's typed watcher: a BaseWatcher with no data and no vtable of its own whose Watched
// points at the Watchable that sits WatchableOffset bytes into a T. Its copy constructor
// re-derives the owner from the source and watches it again, which keeps the intrusive chain
// right when a std::vector of them reallocates (copy the element, destroy the old one):
//   FUN_062904d0 (T = IObjRW, Watchable at +8):     zero both words; W = Src.Watched;
//       W != 0 -> Owner = W - 8; Owner != 0 -> set_pointer(Owner + 8)        (.text:062904ee..0629051b)
//   FUN_06290530 (T = CSimple, Watchable at +0x9c): the same with 0x9c          (.text:0629054e..0629057f)
// Its destructor is BaseWatcher's (FUN_062b57e0 = ~BaseWatcher, then an optional free).
// Retail only ever appends to the vectors of these (FUN_062b5200 / FUN_062b54f0 insert at
// _Last) and never erases or assigns an element, so assignment is deleted rather than
// inherited from BaseWatcher's raw two-dword operator=.
template <class T, unsigned WatchableOffset>
struct TWatcher : public BaseWatcher
{
    TWatcher()
    {
    }

    // `Object != 0 -> set_pointer(Object + WatchableOffset)`: the temporary retail builds
    // before an append (.text:062b48b4..062b48d2, 062b2541..062b2568).
    explicit TWatcher(T* Object)
    {
        if (Object != nullptr)
            set_pointer(WatchableOf(Object));
    }

    TWatcher(TWatcher const& Other)
        : TWatcher(Other.get())
    {
    }

    TWatcher& operator=(TWatcher const&) = delete;

    // `W = Watched; W != 0 -> W - WatchableOffset`, the owner every reader tests for null.
    T* get() const
    {
        Watchable const* const Watched = target();
        if (Watched == nullptr)
            return nullptr;
        return reinterpret_cast<T*>(
            reinterpret_cast<char*>(const_cast<Watchable*>(Watched)) - WatchableOffset);
    }

    // `EAX = Object ? Object + WatchableOffset : 0; CMP EAX,[this]; JZ; set_pointer(EAX)`.
    void watch(T* Object)
    {
        set(Object != nullptr ? WatchableOf(Object) : nullptr);
    }

    static Watchable const* WatchableOf(T* Object)
    {
        return reinterpret_cast<Watchable const*>(reinterpret_cast<char*>(Object) + WatchableOffset);
    }
};

using FObjRWWatcher = TWatcher<IObjRW, 0x08>;
using FSimpleWatcher = TWatcher<CSimple, 0x9c>;

static_assert(sizeof(FObjRWWatcher) == 0x08, "TWatcher adds no data to BaseWatcher");

// FAnimScriptRef (CShip +0x22c BayAnim, CSolar +0x1c0 / +0x1cc) is declared in common_cequip.h,
// next to its third owner CEInternalFX.

struct COMMON_API CShip : public CEqObj, public PhySys::Controller
{
public:
    // CShip::CreateParms extends CEqObj (init 0x52690).
    struct CreateParms : CEqObj::CreateParms
    {
        // Retail offsets; the base's std::list makes the port's own offsets differ.
        // Retail +0xe4 is neither written by Server's create_ship nor read by CShip::init.
        unsigned FieldE8;        // retail +0xe8  zeroed by create_ship (.text:06d0aa50), read by nothing known
        unsigned GroupNameId;    // retail +0xec → this+0x21c  // formerly FieldEc
        unsigned Rank;           // retail +0xf0  Vibe::SetRank
        unsigned char bUsingTradelane; // retail +0xf4 → this+0x2bc  // formerly FieldF4
    };

    CShip();
    // Common.dll.c 46575 (retail 0x27bb0). Zero real callers anywhere in Common.dll/
    // Server.dll/freelancer.exe (only exported because CShip is a dllexport class) --
    // see common_cship.cpp for the port rationale (delegates to CEqObj/PhySys::Controller's
    // own already-ported copy ctors rather than re-inlining their bodies).
    CShip(CShip const& Other);
    virtual ~CShip();

    CShip& operator=(CShip const&) = delete;

    virtual void init(CreateParms const& Parms);

protected:
    // Retail: ?init_physics@CShip@@MAE... and ?alloc_equip@CShip@@MAE... (protected virtual).
    virtual void init_physics(Vector const& Velocity, Vector const& AngularVelocity);
    virtual CEquip* alloc_equip(unsigned short EquipId, Archetype::Equipment* Arch,
                                bool bMissionFlag);

public:
    // Listing 0x55f70 (shared with CSimple). Returns archetype nickname ID.
    virtual unsigned get_name() const;
    // Listing 0x50e30. Main game loop update for ships.
    virtual int update(float Dt, unsigned Flags) override;
    // .text:062b2aa0. Opens ship bay doors. Retail ?open_bay@CShip@@QAEMXZ returns float, but
    // its counter path leaves Script - Stored unrounded in ST0 and freelancer.exe 005468b4
    // adds it on the FPU, so the port returns double (Server drops the result).
    double open_bay();
    // Listing 0x524d0. Notifies ship about target change.
    void notify_set_target(CShip* TargetShip, bool bIsTarget);
    // Listing 0x52a20. Launches ship from dock/base.
    void launch(IObjRW* DockObj, CEqObj* BaseObj, int LaunchType);
    int get_status(char* p1, int p2) const;
    // Checks if this ship is player-controlled.
    bool is_player() const;

    // Physics setters
    void set_throttle(float ThrottleValue);
    void set_axis_throttle(Vector const& AxisThrottleValue);
    void set_nudge_vec(Vector const& NudgeValue);
    void set_strafe_dir(StrafeDir Dir);
    void set_angular_drag_factor(float Factor);
    void set_thrust_power(float Power);
    void set_sub_target(unsigned short SubTargetId);
    void set_gun_stats_dirty();

    // Tradelane methods
    void use_tradelane(bool bUseTradelane);
    void stop_tradelane();
    void request_exit_tradelane();
    void set_tradelane_speed(float Speed);
    // .text:062b4f00: `LEA EAX,[this+0x1e8]; RET` -- a reference to FollowOffset, and not
    // const (?get_follow_offset@CShip@@QAEABVVector@@XZ).
    Vector const& get_follow_offset();
    void set_follow_offset(Vector const& Offset);

    // Formation methods
    void set_follow_leader(IObjRW* Leader);
    FORMATION_RTYPE remove_follow_follower(IObjRW* Follower);
    FORMATION_RTYPE remove_formation_follower(IObjRW* Follower);

private:
    // Retail: ?recalculate_formation_speed@CShip@@AAEXXZ (A = private).
    void recalculate_formation_speed();

public:
    IObjRW* next_enemy();
    IObjRW* previous_enemy();
    IObjRW* next_target(bool bReverse);
    bool leaving_system_update(unsigned& SystemId);
    Archetype::Ship const* shiparch() const;

    static float const JETTISON_CLEARANCE_TIME;
    // Listing 0x52310. Returns renderable subtarget index.
    long get_renderable_subtarget() const;

    bool is_cruise_active() const;
    bool is_using_tradelane() const;
    // Listing 0x51660. Checks if any thrusters are active.
    bool any_thrusters_on() const;
    // Listing 0x51480. Computes lead fire position for current target.
    bool get_tgt_lead_fire_pos(Vector& OutPos) const;
    // Listing 0x52300. Returns sub-target index.
    unsigned short get_sub_target() const;
    // Listing 0x523c0. Returns current target IObjRW pointer.
    IObjRW* get_target() const;
    // Listing 0x523e0. Sets current target.
    void set_target(IObjRW* NewTarget);
    // Listing 0x54a90. Returns formation follower count.
    int get_formation_follower_count();
    // Listing 0x54ac0. Fills array with formation followers.
    int get_formation_followers(IObjRW** const OutFollowers, unsigned int MaxCount);
    // Listing 0x54ee0. Returns follow leader IObjRW pointer.
    IObjRW* get_follow_leader();
    // Listing 0x51700. Returns nudge direction vector.
    Vector get_nudge_vec() const;
    // Listing 0x51740. Returns strafe direction enum.
    StrafeDir get_strafe_dir() const;
    // Listing 0x514b0. Returns farthest active gun range.
    bool get_farthest_active_gun_range(float& OutRange) const;
    // Listing 0x514f0. Returns the cached gun statistics by reference
    // (?get_gun_stats@CShip@@QBEABUShipGunStats@@XZ).
    ShipGunStats const& get_gun_stats() const;
    // Listing 0x53040. Returns remaining cargo hold capacity.
    // .text:062b3040. Retail ?get_cargo_hold_remaining@CShip@@QBEMXZ returns float, but leaves
    // Hold - Used unrounded in ST0 and its consumers (Common 0629e59d / 062ce8e8 / 062cebe2,
    // freelancer.exe 004c230d.., Server 06cf9728) keep it on the FPU, so the port returns double.
    double get_cargo_hold_remaining() const;
    // Listing 0x530f0. Returns remaining repair kit capacity.
    int get_repair_kit_capacity_remaining() const;
    // Listing 0x53180. Returns remaining shield battery capacity.
    int get_shield_battery_capacity_remaining() const;
    // Listing 0x27b50. Returns bay door state enum.
    enum BayState get_bay_state() const;
    // Listing 0x53220. Returns ammo capacity for given equipment type.
    int get_ammo_capacity_remaining(unsigned EquipClassType) const;
    // Common.dll.c 0x53210: thin overload forwarding Arch->get_id() to the unsigned overload.
    int get_ammo_capacity_remaining(Archetype::Equipment const* Arch) const;
    // Listing 0x532e0. Returns space available for cargo type.
    int get_space_for_cargo_type(Archetype::Equipment const* CargoArch) const;
    // Listing 0x53630. Returns pilot name string (retail ?get_pilot_name@CShip@@QBEPBGXZ,
    // G = VC6 wchar_t).
    wchar_t const* get_pilot_name() const;
    // Listing 0x51a00. Returns true if ship is launching.
    bool is_launching() const;
    // Listing 0x51a20. Returns true if ship is jumping.
    bool is_jumping() const;
    // Listing 0x51b70. Checks if other object is enemy based on reputation. Retail's is NOT
    // const (?is_enemy@CShip@@QAE_NPAUIObjInspect@@@Z).
    bool is_enemy(IObjInspect* OtherInspect);
    // Listing 0x52640. Returns group name hash.
    unsigned get_group_name() const;
    // Listing 0x524a0. Returns count of enemies targeting this ship.
    int get_targeted_enemy_count();
    // Listing 0x54810. Adds object to formation.
    FORMATION_RTYPE add_formation_follower(IObjRW* Follower);
    // Listing 0x54d10. Adds object as follow follower.
    FORMATION_RTYPE add_follow_follower(IObjRW* Follower);
    // Listing 0x54f10. Generates offset for follow formation.
    bool generate_follow_offset(IObjInspect const* Inspect, Transform& OutOffset) const;
    // Listing 0x51810. Returns tradelane entrance ring.
    unsigned get_tradelane_entrance_ring();
    // Listing 0x51840. Returns tradelane exit ring.
    unsigned get_tradelane_exit_ring();
    // Listing 0x517c0. Returns tradelane speed.
    float get_tradelane_speed();
    // Listing 0x51960. Initiates tradelane jump.
    int go_tradelane(IObjInspect const* EntranceRing, IObjInspect const* ExitRing,
                     IObjRW* TradelaneObj, bool bFastExit, float Speed);
    // Listing 0x52ff0. Airlocks ship into base at position/orientation.
    void airlock_in(Vector const& Pos, Matrix const& Orient);
    // Listing 0x52c10. Closes bay doors.
    float close_bay();
    // Listing 0x52d40. Jumps ship in from hyperspace.
    void jump_in(IObjRW* JumpObj, Vector const& Pos, Matrix const& Orient, float Speed);

private:
    // Listing 0x54540. Collects force components for thrust calculation. Retail:
    // ?collect_force_components@CShip@@ABEXMW4ThrustEquipType@IObject@@AAM1@Z (private).
    void collect_force_components(float Throttle, ThrustEquipType ThrustType,
                                  float& OutForceX, float& OutForceY) const;
    // Listing 0x54700. Collects angular force components for rotation (private in retail).
    void collect_angular_force_components(unsigned Axis, float Throttle,
                                          float& OutTorqueX, float& OutTorqueY) const;

public:
    // Listing 0x53730. Returns linear drag coefficient. Retail ?get_linear_drag@CShip@@QBEMXZ
    // returns float, but the zone branch leaves the product unrounded in ST0 and every caller
    // (CEEngine 062962fd / 0629640c / 06296505, all in Common) keeps it on the FPU, so the
    // port returns double. No caller outside Common imports it.
    double get_linear_drag() const;
    // Listing 0x537b0. Returns angular drag vector.
    Vector get_angular_drag() const;
    // .text:062b37f0. Time to accelerate from FromSpeed to ToSpeed at the given throttle.
    float get_time_to_accelerate(float Throttle, float FromSpeed, float ToSpeed,
                                 ThrustEquipType ThrustType) const;
    // .text:062b3db0. Roll-axis acceleration time (3 params).
    float get_time_to_accelerate_angularly(float Throttle, float FromAngVel,
                                           float ToAngVel) const;
    // .text:062b3ce0. Pitch/yaw acceleration time (4 params).
    float get_time_to_accelerate_angularly(float PitchThrottle, float YawThrottle,
                                           float FromAngVel, float ToAngVel) const;
    // .text:062b3900. Distance travelled in Time starting at FromSpeed.
    float get_distance_travelled(float Throttle, float FromSpeed, float Time,
                                 ThrustEquipType ThrustType) const;
    // Listing 0x53aa0. Coasting distance from initial speed.
    float get_initial_speed_to_coast_distance(float Speed, bool bStop) const;
    // Listing 0x53c30. Speed at given throttle for thrust type.
    float get_speed(float Throttle, ThrustEquipType ThrustType) const;
    // Listing 0x53b70. Throttle needed for desired speed.
    float get_projected_throttle(float Speed, ThrustEquipType ThrustType) const;
    // Listing 0x540e0. Projected throttle in XY plane.
    float get_projected_axis_throttle_XY(float Speed) const;
    // Listing 0x541b0. Projected throttle in Z axis.
    float get_projected_axis_throttle_Z(float Speed) const;
    // Listing 0x54250. Angular speed in XY plane.
    float get_angular_speed_XY(float MaxRollRate, float MaxPitchRate) const;
    // Listing 0x542c0. Angular speed around Z axis.
    float get_angular_speed_Z(float MaxYawRate) const;
    // .text:062b3fb0. Roll-axis angular distance travelled (3 params).
    float get_angular_distance_travelled(float Throttle, float FromAngVel,
                                         float Time) const;
    // .text:062b3e40. Pitch/yaw angular distance travelled (4 params).
    float get_angular_distance_travelled(float PitchThrottle, float YawThrottle,
                                         float FromAngVel, float Time) const;
    // Listing 0x27b60. Returns current throttle value.
    float get_throttle() const;
    // Listing 0x27b70. Returns axis throttle vector.
    Vector const& get_axis_throttle() const;
    // Listing 0x27ba0. Returns maximum bank angle.
    float get_max_bank_angle() const;
    // Listing 0x28240. Returns current thrust power.
    float get_thrust_power() const;
    // Listing 0x28260. Returns maximum thrust power.
    float get_max_thrust_power() const;
    // Listing 0x28270. Returns thrust power ratio.
    float get_thrust_power_ratio() const;
    // Listing 0x51190. Verifies current target is valid.
    void VerifyTarget();
    // Listing 0x51290. Lead fire position for specific target.
    bool get_tgt_lead_fire_pos(IObjInspect const* TargetInspect, Vector& OutPos) const;
    // Listing 0x54bf0. Checks formation follower status over time.
    bool check_formation_follower_status(float Dt);
    // Listing 0x50dd0. Flushes all animations.
    virtual void flush_animations();
    // Listing 0x50d30. Disables steering controllers.
    virtual void disable_controllers();
    // Listing 0x50d80. Enables steering controllers.
    virtual void enable_controllers();

private:
    // .text:062b42f0. Internal acceleration time helper.
    float get_time_to_accelerate(float ForceOverMass, float Mass, float Drag,
                                 float FromSpeed, float ToSpeed) const;
    // .text:062b4440. Internal distance travelled helper.
    float get_distance_travelled(float ForceOverMass, float Mass, float Drag,
                                 float FromSpeed, float Time) const;
    // .text:062b44f0. Internal coast distance helper: (A / B) * C with A > 0, B >= 0,
    // C >= 0.01f (meaning of B/C not established -- no retail caller besides the export).
    float get_initial_speed_to_coast_distance(float A, float B, float C) const;
    // .text:0628f050 (ordinal 2445, ?do_simulation_controller@CShip@@EAEXPAVIVP_Event_Sim@@
    // PAV?$IVP_U_Vector@VIVP_Core@@@@@Z -- E = private virtual): slot 4 of CShip's
    // PhySys::Controller table (.rdata:0639c01c). The body is a bare `RET 0x8` (ICF-folded
    // with JustifyNode::Execute and many other two-argument no-ops): CShip's own controller
    // exerts nothing; its equipment controllers (CEEngine, CEThruster, CSteering, ...) do.
    virtual void do_simulation_controller(IVP_Event_Sim* EventSim,
                                          IVP_U_Vector<IVP_Core>* CoreList) override;

public:
    // +0x1b4: the multiplayer group id. Not part of PhySys::Controller (that ctor runs on +0x1a4
    // at 0x062b06a8; +0x1b4 is zeroed separately at 0x062b06ad, copied by the copy ctor at
    // 0x06287d4b). Writers: Server CPlayerGroup::AddMember / DelMember (0x06d5b8a5 /
    // 0x06d5bb1b), the server's ship creation (0x06d0acc3), the client's IClient slot 113
    // (MISCOBJUPDATE 0x0009, 0x0053e2b0). Readers compare two player ships' values: Common
    // 0x062cb7a2, freelancer.exe 0x0045a4e0 / 0x004d0250 / 0x004d0470 / 0x004d3fd0 (equal and
    // non-zero -> same group).
    unsigned GroupId;
    // +0x1b8 .. +0x1bf: a BaseWatcher on the leader's Watchable (IObjRW+8) -- Watched at +0x1b8,
    // Next at +0x1bc. ctor 0x062b06b3/b9 zeroes both, the copy ctor builds it from the other
    // ship's leader (0x06287d57..86 set_pointer), the dtor releases it (0x062b0be0..fe),
    // get_formation_leader returns Watched - 8 (0x062c030d). Writers outside Common go through
    // BaseWatcher::set (Server 0x06d029e5, freelancer.exe 0x00547182 -- the inline compare +
    // set_pointer).
    FObjRWWatcher FormationLeader;    // +0x1b8
    Vector FormationOffset;          // +0x1c0  get_formation_offset (IObjInspectImpl @ 0x602B0)
    // +0x1cc: the slowest formation follower's speed at throttle 1 (1.0e8f, 0x4cbebc20, when
    // there is none), written only by recalculate_formation_speed and the add / remove follower paths.
    // The ctor stores 0 (.text:062b06c9).
    float FormationSpeed;
    // +0x1d0 / +0x1f4: VC6 std::vector<FObjRWWatcher> (allocator byte, _First, _Last, _End), the
    // ctor reserves 16 in each (.text:062b0839..062b0906, 062b0909..062b0989). Formation
    // followers come and go through add_ / remove_formation_follower, follow followers through
    // add_ / remove_follow_follower. Removing never erases: the slot's watcher is cleared, and
    // the next add reuses the first cleared slot. Readers skip slots whose owner is null.
    std::vector<FObjRWWatcher> FormationFollowers;   // +0x1d0 .. +0x1df
    FObjRWWatcher FollowLeader;                       // +0x1e0  set_ / get_follow_leader
    Vector FollowOffset;              // +0x1e8  set_follow_offset / get_follow_offset (3 floats)
    std::vector<FObjRWWatcher> FollowFollowers;      // +0x1f4 .. +0x203
    // +0x204: VC6 std::vector<FSimpleWatcher> of the ships that currently target this one,
    // maintained by notify_set_target from the targeting ship's set_target / update; reserved
    // to 16 by the ctor (FUN_062b5100, .text:062b0994).
    std::vector<FSimpleWatcher> Targeters;            // +0x204 .. +0x213
    // +0x214: set when a non-player ship has finished jumping out in single player.
    // Common only zeroes (.text:062b0718) and copies it. freelancer.exe sets it to 1 when the
    // ship's "JumpOut" action completes (.text:00543978); Server.dll clears it on the single-
    // player jump path (.text:06d0cb27), and its death arm (.text:06d014d8) removes a ship with
    // the flag set at once instead of running the death timer. (formerly Field_0x214)
    unsigned char bJumpedOut;         // (+0x215 .. +0x217: padding)
    // +0x218: the CSolar whose atmosphere this ship is inside, or null; CShip::update rewrites
    // it every frame (.text:062b1005 / 062b10d1).
    CSolar* AtmosphereSolar;
    // +0x21c: the name id get_group_name returns when non-zero (else the vibe's affiliation
    // name); set from CreateParms retail +0xec by init (.text:062b26a3). (formerly Field21c)
    unsigned GroupNameId;
    FObjRWWatcher Target;             // +0x220  get_target / set_target
    unsigned short SubTargetIndex;    // +0x228  set_sub_target/get_renderable_subtarget (+0x22a: padding)
    FAnimScriptRef BayAnim;           // +0x22c  bay door animation script, -1 = none
    CSteering Steering;               // +0x230 (0x28 bytes, 0x230..0x257)
    CNudgeEngine NudgeEngine;         // +0x258 (0x28 bytes, 0x258..0x27f)
    CStrafeEngine StrafeEngine;       // +0x280 (0x1c bytes, 0x280..0x29b)
    float Throttle;                   // +0x29c  get_throttle / airlock_in (=1.0f)
    float ThrustPower;                // +0x2a0  get/set_thrust_power; drained by thrusters and strafe (formerly Field2a0)
    float MaxThrustPower;             // +0x2a4  get_max_thrust_power: sum of power plants' ThrustCapacity (formerly Field2a4)
    int BayDoorCounter;               // +0x2a8  close_bay (DEC)
    int BayState;                     // +0x2ac  close_bay (CMP 2,3)
    ActionDB Actions;                 // +0x2b0  (0x08 bytes, 0x2b0..0x2b7)
    float TradelaneSpeed;             // +0x2b8  get_tradelane_speed (when is_using_tradelane)
    unsigned char bUsingTradelane;    // +0x2bc  is_using_tradelane flag
    unsigned char bIsPlayer;          // +0x2bd  (OwnerPlayer != 0) (+0x2be: padding)
    float AngularDragFactor;          // +0x2c0  1.0f (angular drag factor)
    mutable unsigned char GunStatsDirty; // +0x2c4  get_gun_stats flag (mutable for const method) (+0x2c5: padding)
    // ShipGunStats cached at +0x2c8..+0x2db; get_gun_stats returns a reference to it
    // (?get_gun_stats@CShip@@QBEABUShipGunStats@@XZ).
    mutable ShipGunStats GunStats;    // +0x2c8
};

static_assert(offsetof(CShip, GroupId) == 0x1b4, "CShip GroupId +0x1b4");
static_assert(offsetof(CShip, FormationLeader) == 0x1b8, "CShip+0x1b8 formation leader");
static_assert(offsetof(CShip, FormationOffset) == 0x1c0, "CShip+0x1c0 formation offset");
static_assert(offsetof(CShip, FormationSpeed) == 0x1cc, "CShip+0x1cc formation speed");
static_assert(offsetof(CShip, FormationFollowers) == 0x1d0, "CShip+0x1d0 formation followers");
static_assert(offsetof(CShip, FollowLeader) == 0x1e0, "CShip+0x1e0 follow leader");
static_assert(offsetof(CShip, FollowFollowers) == 0x1f4, "CShip+0x1f4 follow followers");
static_assert(offsetof(CShip, Targeters) == 0x204, "CShip+0x204 targeters");
static_assert(offsetof(CShip, bJumpedOut) == 0x214, "CShip+0x214 jumped-out flag");
static_assert(offsetof(CShip, AtmosphereSolar) == 0x218, "CShip+0x218 atmosphere solar");
static_assert(offsetof(CShip, GroupNameId) == 0x21c, "CShip+0x21c");
static_assert(offsetof(CShip, Target) == 0x220, "CShip+0x220 target");
static_assert(offsetof(CShip, SubTargetIndex) == 0x228, "CShip+0x228 sub target");
static_assert(offsetof(CShip, BayAnim) == 0x22c, "CShip+0x22c anim");
static_assert(offsetof(CShip, Steering) == 0x230, "CShip+0x230 Steering");
static_assert(offsetof(CShip, NudgeEngine) == 0x258, "CShip+0x258 NudgeEngine");
static_assert(offsetof(CShip, StrafeEngine) == 0x280, "CShip+0x280 StrafeEngine");
static_assert(offsetof(CShip, Throttle) == 0x29c, "CShip+0x29c Throttle");
static_assert(offsetof(CShip, ThrustPower) == 0x2a0, "CShip+0x2a0");
static_assert(offsetof(CShip, MaxThrustPower) == 0x2a4, "CShip+0x2a4");
static_assert(offsetof(CShip, Actions) == 0x2b0, "CShip+0x2b0 Actions");
static_assert(offsetof(CShip, TradelaneSpeed) == 0x2b8, "CShip+0x2b8 TradelaneSpeed");
static_assert(offsetof(CShip, bUsingTradelane) == 0x2bc, "CShip+0x2bc");
static_assert(offsetof(CShip, bIsPlayer) == 0x2bd, "CShip+0x2bd player flag");
static_assert(offsetof(CShip, AngularDragFactor) == 0x2c0, "CShip+0x2c0 angular drag factor");
static_assert(offsetof(CShip, GunStatsDirty) == 0x2c4, "CShip+0x2c4 GunStatsDirty");
static_assert(offsetof(CShip, GunStats) == 0x2c8, "CShip+0x2c8 GunStats");
static_assert(sizeof(CShip) == 0x2dc, "CShip sizeof 0x2dc");

struct COMMON_API CSolar : public CEqObj
{
public:
    explicit CSolar(Class ObjectClass = ClassSolar);
    // Common.dll.c 61988-62093 (0x3a860). Confirmed via list_references to have zero callers
    // anywhere in Common.dll, Server.dll, or freelancer.exe (dllexport-only), same situation as
    // CShip::CShip(CShip const&). Delegates to CEqObj(CEqObj const&) for the shared base portion
    // (matching retail's inlined-but-equivalent field-by-field base copy) then copies every
    // CSolar-own field below in declaration order.
    CSolar(CSolar const& Other);
    virtual ~CSolar();

    CSolar& operator=(CSolar const&) = delete;

    // 0x06262d50 (ICF fold, `MOV EAX,[this+0x88]; RET`).
    Archetype::Solar const* solararch() const;

    // Retail's parameter type for CSolar::ReadObj and CSolar::init
    // (?init@CSolar@@QAEXABUCreateParms@1@@Z). Its one member past the CEqObj base is a
    // std::list<char const*> of continual-animation names at +0xe4 (VC6: allocator +0xe4,
    // head +0xe8, size +0xec). ReadObj appends one StringAlloc'd name per "anim" key
    // (.text:062b83a7 StringAlloc, 062b83ac..062b83e8 push_back through the head at +0xe8,
    // ++size at +0xec); CSolar::init walks it (.text:062b771a..062b7739). Server builds it
    // with the parms (FUN_06d1ad30 / FUN_06cfc340: a self-linked head) and destroys it in
    // ~CreateParms FUN_06d0a120 -- the destructor both load_object and create_solar run,
    // and the one their unwind funclets (Unwind@06d613f4, Unwind@06d614a9) jump to.
    struct CreateParms : public CEqObj::CreateParms
    {
        std::list<char const*> ContinualAnims; // retail +0xe4 anim-name list
    };

    // Common.dll.c 89065-89132 (0x57560), ?init@CSolar@@QAEXABUCreateParms@1@@Z: public and
    // NOT virtual, on CSolar's own CreateParms. It hides CEqObj::init rather than overriding
    // it -- CSolar's vtable keeps CEqObj::init in that slot (.rdata:0639d464) -- and calls it
    // directly (.text:062b756b `CALL CEqObj::init`).
    void init(CreateParms const& Parms);

    virtual void cache_physical_props() override;
    virtual float get_mass() const override;  // Common.dll.c 0x3ad00
    // Common.dll.c 0x3ac70 (decompiled .c is corrupted here; ported from raw .txt
    // 110205-110261 instead). Same object-space-to-world rotation as the base
    // CObject::get_angular_velocity, additionally gated on TypeBits & 7.
    virtual Vector get_angular_velocity() const override;
    // .text:063623d0 (ICF-folded with CAsteroid::get_velocity): always the zero vector.
    virtual Vector get_velocity() const override;
    virtual void flush_animations() override;  // Common.dll.c 0x57fa0

    // Common.dll.c 89296-89305 (0x57a70). Delegates to CEqObj::update, then drives the
    // system-gate open/close state machine when this solar is a gate (is_system_gate()).
    virtual int update(float Dt, unsigned Flags) override;
    // Common.dll.c 89397-89421 (0x57cc0). System gates repurpose activate() as the
    // open/close command (bOn selects GateAnimDirection, DockIndex must be 0); lane rings
    // (is_lane_ring()) reject it outright; everything else falls back to CEqObj::activate.
    virtual ObjActivateResult activate(bool bOn, unsigned DockIndex) override;
    // Common.dll.c 89428-89446 (0x57d50). System-gate counterpart of activate() above:
    // reports GateAnimDirection as the single activation bit; lane rings report false;
    // everything else falls back to CEqObj::get_activate_state.
    virtual bool get_activate_state(std::vector<bool>& OutStates) override;

    // Common.dll.c 89580-90093 (0x57ff0). Static per-object-class INI loader: parses one
    // Solar-section's worth of keys (archetype/nickname/pos/rotate/spin/visit/parent/goto/
    // base/dock_with/loadout/atmosphere_range/anim/prev_ring/next_ring/
    // tradelane_space_name/destructible/space_costume/voice/comm_appr) into OutParms and
    // resolves OutArch from the "archetype" key, then validates+finalizes (nickname
    // required, reputation verified, loadout equipment copied in, destructible defaulted
    // from the archetype when not explicitly overridden).
    static bool __cdecl ReadObj(INI_Reader& Reader, Archetype::Solar*& OutArch, CreateParms& OutParms);

    float get_atmosphere_immersion(Vector const& Pos) const;

    bool is_planetary() const;
    bool is_system_gate() const;
    bool is_waypoint() const;
    bool is_lane_ring() const;
    bool is_destructible() const;
    bool is_dynamic() const;

    float get_atmosphere_range() const;
    ID_String const& get_loadout() const;
    unsigned get_prev_trade_ring() const;
    unsigned get_next_trade_ring() const;
    ID_String const& GetParentNickname() const;
    unsigned get_dest_system() const;
    ID_String const& get_dest_gate() const;
    unsigned char GetVisitValue() const;
    void SetVisitValue(unsigned char NewValue);
    unsigned int get_tradelane_space_name() const;

    // CSolar::init (Common.dll.c 89077, 0x57560) copies CreateParms::NicknameSrc (+0xb8)
    // straight into here, unconditionally, right after CEqObj::init returns. The ctor
    // ID_String::clear()'s it (Common.dll.c 88970) and CSolar::CSolar(CSolar const&) copies
    // it verbatim (Common.dll.c 62014). Consumed again by CSolar::init itself, via
    // ID_String::get_string(), to name the object in the "dockable but no destination"
    // diagnostic -- consistent with an object-instance nickname rather than an
    // archetype-level name (CObject::get_name() already covers the archetype nickname).
    ID_String Nickname;             // +0x1a4
    bool bDestructible;             // +0x1a8  is_destructible() reads this[0x1a8] directly (Common.dll.c 0x3a840)
    bool bDynamic;                  // +0x1a9
                                    // +0x1aa .. +0x1ab: alignment
    float AtmosphereRange;          // +0x1ac
    // is_planetary/is_system_gate/is_waypoint/is_lane_ring (Common.dll.c 0x3a7f0-0x3a830)
    // read a bitflags dword at retail this+0xe0 -- that IS CSimple::TypeBits (see
    // offsetof(CSimple, TypeBits) == 0xe0 static_assert above), inherited by CSolar through
    // CEqObj, not a CSolar-owned field. A prior port revision fabricated a local `SolarFlags`
    // field here that aliased this ID_String Loadout's own storage at +0x1b0 with wrong bit
    // values besides; removed in favor of reading the inherited TypeBits directly.
    ID_String Loadout;              // +0x1b0  (retail get_loadout() returns this+0x1b0 directly)
    unsigned DestSystem;            // +0x1b4
    ID_String DestGate;             // +0x1b8

    // System-gate open/close animation state machine (configure_system_gate/update_system_gate,
    // Common.dll.c 0x57a00/0x57ab0). GateAnimState cycles 0->2->3->4 as the gate opens then
    // closes again; the constructor defaults it to 4 ("closed"). This replaces a raw
    // PadSolar[0x1c] placeholder -- those 28 bytes are these real fields, not padding; see
    // ~CSolar/init_continual_anim for the ContinualAnim list and update_system_gate for the
    // anim fields.
    unsigned char GateAnimDirection; // +0x1bc  update_system_gate's this[0x1bc]==0 branch selector
                                     // +0x1bd .. +0x1bf: alignment
    // +0x1c0: the gate open/close animation script (-1 = none), a member with a destructor --
    // Unwind@063947d8 of the ctor frames jumps to FUN_0629abd0 with this+0x1c0.
    FAnimScriptRef GateAnim;         // +0x1c0
    int GateAnimState;                // +0x1c4  0/1/2/3 mid-cycle, 4 = closed (ctor default)
    float GateAnimTimer;             // +0x1c8  seconds accumulator for the current GateAnimState
    // +0x1cc: std::list<FAnimScriptRef> of continual animations (VC6: allocator byte +0x1cc,
    // head +0x1d0, size +0x1d4). The ctor's byte store at +0x1cc is that allocator copy, not a
    // class id; Unwind@063947e6 destroys the list (FUN_062b8e30), ~CSolar erases node by node
    // (FUN_062b94c0), init_continual_anim inserts at begin() (FUN_0627f280).
    std::list<FAnimScriptRef> ContinualAnims; // +0x1cc .. +0x1d7

    unsigned PrevRing;              // +0x1d8
    unsigned NextRing;              // +0x1dc
    // get_tradelane_space_name() (Common.dll.c 0x58a70) returns this field directly -- it is
    // NOT ParentNickname.Id despite the two looking related; CSolar::init (0x57560) loads them
    // from separate CreateParms offsets (+0xd8 vs +0xc0) into this+0x1e0 and this+0x1e8.
    unsigned TradelaneSpaceNameId;  // +0x1e0
    unsigned char VisitValue;       // +0x1e4
                                    // +0x1e5 .. +0x1e7: alignment
    ID_String ParentNickname;       // +0x1e8  GetParentNickname() (Common.dll.c 0x54f00)

protected:
    // Common.dll.c 89149-89222 (0x57780). Builds a PhySys::CreateParms (mass from the
    // archetype, damping mode selected by whether AngularVelocity is ~zero) and calls
    // BuildIVP_Sphere for a ".sph" mesh or BuildIVP_Real otherwise, then adds
    // AngularVelocity via PhySys::AddToAngularVelocityOS when it's non-zero. Velocity is
    // read by retail as a parameter but never actually used anywhere in the function body
    // (confirmed against the raw disassembly, not just the decompiled .c) -- a genuine
    // retail dead parameter, not a porting gap.
    virtual void init_physics(Vector const& Velocity, Vector const& AngularVelocity) override;
    // Listing 0x57a00. Starts AnimName as the gate animation (PlayScript into GateAnim, the
    // same sequence CSolar::init inlines for the archetype's open_anim); a no-op when
    // AnimName is empty.
    void configure_system_gate(char const* AnimName);
    // Common.dll.c 89311-89391 (0x57ab0). System-gate open/close animation state machine,
    // driven each update() tick while is_system_gate() is true. See the .cpp for the full
    // GateAnimState/GateAnimDirection transition table.
    void update_system_gate(float Dt);
    // Common.dll.c 89513-89549 (0x57f00). Special-cases AClassTradeLane archetypes (jump
    // gates/holes) into a CETradeLaneEquip; everything else falls back to CEqObj::alloc_equip.
    virtual CEquip* alloc_equip(unsigned short EquipId, Archetype::Equipment* Arch,
                                bool bMissionFlag) override;

    // Listing 0x57d90. Inserts a fresh FAnimScriptRef at the front of ContinualAnims, starts
    // AnimName into it (EvalPlayback + AnimDB::Add on success) or pops it again and logs the
    // miss. Called once per CreateParms anim name from CSolar::init.
    void init_continual_anim(char const* AnimName);
};

static_assert(offsetof(CSolar, Nickname) == 0x1a4, "CSolar+0x1a4 Nickname");
static_assert(offsetof(CSolar, bDestructible) == 0x1a8, "CSolar+0x1a8 bDestructible");
static_assert(offsetof(CSolar, bDynamic) == 0x1a9, "CSolar+0x1a9 bDynamic");
static_assert(offsetof(CSolar, AtmosphereRange) == 0x1ac, "CSolar+0x1ac AtmosphereRange");
static_assert(offsetof(CSolar, Loadout) == 0x1b0, "CSolar+0x1b0 Loadout");
static_assert(offsetof(CSolar, DestSystem) == 0x1b4, "CSolar+0x1b4 DestSystem");
static_assert(offsetof(CSolar, DestGate) == 0x1b8, "CSolar+0x1b8 DestGate");
static_assert(offsetof(CSolar, GateAnim) == 0x1c0, "CSolar+0x1c0 GateAnim");
static_assert(offsetof(CSolar, GateAnimState) == 0x1c4, "CSolar+0x1c4 GateAnimState");
static_assert(offsetof(CSolar, GateAnimTimer) == 0x1c8, "CSolar+0x1c8 GateAnimTimer");
static_assert(offsetof(CSolar, ContinualAnims) == 0x1cc, "CSolar+0x1cc ContinualAnims list");
static_assert(offsetof(CSolar, PrevRing) == 0x1d8, "CSolar+0x1d8 PrevRing");
static_assert(offsetof(CSolar, NextRing) == 0x1dc, "CSolar+0x1dc NextRing");
static_assert(offsetof(CSolar, TradelaneSpaceNameId) == 0x1e0, "CSolar+0x1e0 TradelaneSpaceNameId");
static_assert(offsetof(CSolar, VisitValue) == 0x1e4, "CSolar+0x1e4 VisitValue");
static_assert(offsetof(CSolar, ParentNickname) == 0x1e8, "CSolar+0x1e8 ParentNickname");
static_assert(sizeof(CSolar) == 0x1ec, "CSolar sizeof 0x1ec (CObject::Alloc takes it from Heap 063fc3c0)");

// Common.dll.txt:0630e660, ?GetRoot@@YAPAUCObject@@PBU1@@Z. Walks the engine parent chain
// from the object's own instance up to the top, then hands back the CObject registered
// against that root instance. An object with no instance (-1) is its own root.
COMMON_API CObject* __cdecl GetRoot(CObject const* Obj);

COMMON_API bool __cdecl IsDescendant(long ParentInst, long ChildInst);

namespace Hierarchy
{
    // Common.dll.c 0xae6d0. Walks ChildInst up the Engine parent chain (vtable+0xd4) counting
    // hops until it reaches ParentInst (returns the hop count, 0 if already equal) or falls off
    // the chain at -1 (returns -1). Same parent-walk as IsDescendant, but counting instead of
    // just testing membership.
    COMMON_API int __cdecl GetDepth(long ParentInst, long ChildInst);
}
COMMON_API bool __cdecl HardpointPosition(long Instance, char const* HpName, Vector* OutPos, Matrix* OutOrient);
COMMON_API bool __cdecl HardpointPositionA(long Instance, char const* HpName, Vector* OutPos, Matrix* OutOrient);
COMMON_API void __cdecl CompoundInstanceAnalyze(long Instance, void (__cdecl* Callback)(long, void*), void* Context);
COMMON_API int __cdecl CompoundInstanceCount(long Instance);

// Common.dll.txt:063035c0, ?CompoundInstanceList@@YAXJPAHPAPAJ@Z. Writes the instance and
// every descendant into a freshly allocated array (raw operator new, Count * sizeof(long)),
// with the count in OutCount. The caller owns the array.
COMMON_API void __cdecl CompoundInstanceList(long Instance, int* OutCount, long** OutArray);

// Common.dll.txt:06303720, ?CompoundInstanceFindFirstName@@YAJJPBD@Z. First instance in the
// compound (the root included) whose engine name matches Name case-insensitively, or -1.
COMMON_API long __cdecl CompoundInstanceFindFirstName(long Instance, char const* Name);

// Common.dll.txt:0630e750, ?FindChild@@YAJJPBD@Z. Depth-first search of Instance's engine
// children for the first one whose name matches Name (_stricmp, a null name reads as "");
// -1 if none. Called by CArchGroup::CArchGroup for its RootIndex.
COMMON_API long __cdecl FindChild(long Instance, char const* Name);

// Common.dll.txt:062a8920, ?ToggleIceCap@@YAX_N@Z (ordinal 2058). The retail body is a bare
// RET: the export survives, the feature it once toggled does not. Nothing in Common.dll,
// Server.dll or freelancer.exe imports or calls it.
COMMON_API void __cdecl ToggleIceCap(bool bOn);
