// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_cobject.h"
#include "common_cobject_internal.h" // CmnCobject helpers defined below
#include "x86math.h"

#include "common_cobjdb.h"   // IObjDB::GetZoneDb (CmnCobject::GetObjectDbCollisionGroup)
#include "common_goods.h"
#include "common_physys.h"
#include "common_iface.h"
#include "common_universe.h" // Universe::ISystem (CmnCobject::GetObjectDbCollisionGroup)
#include "common_misc.h"     // Heap (CObject::operator new/delete pools)

#include "DALib.h"
#include "dacom.h"

#include <cstdint>
#include <list>
#include <new>
#include <stdlib.h>

namespace
{
    const char kCObjectSource[] = "E:\\FL\\Scratch\\Source\\Common\\CObject.cpp";

    // Per-class instance lists (FUN_062ae690 / FUN_062af9b0).
    // Not the Vibe map DAT_064018c8 (CleanUpDatabases / FreeDynamic).
    enum EClassList
    {
        ListObject = 0,
        ListMine,
        ListCounterMeasure,
        ListBeam,
        ListGuided,
        ListSolar,
        ListAsteroid,
        ListShip,
        ListLoot,
        ListEquipment,
        ListDynamicAsteroid,
        ListCount
    };

    std::list<CObject*> G_ClassLists[ListCount];
    std::list<CObject*>* G_FindList = nullptr;
    std::list<CObject*>::iterator G_FindNextIt;
    bool G_bFindActive = false;

    // The class -> list switch inlined at the head of Count / Find / FindFirst (e.g.
    // .text:062ae7e0..062ae892, jump table 062ae894 + byte map 062ae8ac): exactly the eleven
    // concrete classes own a list, every other value (0, ClassSimple, ClassEqObj, ...) has
    // none.
    std::list<CObject*>* ListForClass(CObject::Class ObjectClass)
    {
        switch (ObjectClass)
        {
        case CObject::ClassObject:          return &G_ClassLists[ListObject];          // 063fc32c
        case CObject::ClassMine:            return &G_ClassLists[ListMine];            // 063fc374
        case CObject::ClassCounterMeasure:  return &G_ClassLists[ListCounterMeasure];  // 063fc368
        case CObject::ClassBeam:            return &G_ClassLists[ListBeam];            // 063fc398
        case CObject::ClassGuided:          return &G_ClassLists[ListGuided];          // 063fc38c
        case CObject::ClassSolar:           return &G_ClassLists[ListSolar];           // 063fc35c
        case CObject::ClassShip:            return &G_ClassLists[ListShip];            // 063fc3a4
        case CObject::ClassLoot:            return &G_ClassLists[ListLoot];            // 063fc380
        case CObject::ClassAsteroid:        return &G_ClassLists[ListAsteroid];        // 063fc350
        case CObject::ClassEquipmentObj:    return &G_ClassLists[ListEquipment];       // 063fc338
        case CObject::ClassDynamicAsteroid: return &G_ClassLists[ListDynamicAsteroid]; // 063fc344
        default:                            return nullptr;
        }
    }

    bool ClassHasSimpleId(CObject::Class ObjectClass)
    {
        return ObjectClass != CObject::ClassObject
            && ObjectClass != CObject::ClassEquipmentObj
            && ObjectClass != CObject::ClassDynamicAsteroid;
    }

    void RemoveFromClassList(CObject* Object)
    {
        if (Object == nullptr)
            return;
        std::list<CObject*>* List = ListForClass(Object->ObjectClass);
        if (List == nullptr)
            return;
        std::list<CObject*>::iterator It = List->begin();
        for (; It != List->end(); ++It)
        {
            if (*It == Object)
            {
                if (G_bFindActive && G_FindList == List && G_FindNextIt == It)
                    ++G_FindNextIt;
                List->erase(It);
                return;
            }
        }
    }
}

// Helpers shared by several CObject-family TUs; declared in common_cobject_internal.h.
namespace CmnCobject
{

    // Common.dll.c 0x52850 (CShip::init_physics) established this transform: retail
    // reads Orientation transposed into a scratch buffer, then feeds it through the
    // lazily-created IVP math-helper singleton (vtable slot +0x2c off DAT_063fcXXX)
    // to rotate a world-space AngularVelocity into object space before handing it to
    // PhySys::AddToAngularVelocityOS, which stores an object-space delta (see
    // AddToAngularVelocityOS/GetAngularVelocityOS in common_physys.cpp). Same
    // transpose-multiply pattern as CShip::check_formation_follower_status, which
    // uses it (in the opposite, local-to-world direction) to place formation
    // followers. Identical singleton-call pattern in CGuided, CMine,
    // CCounterMeasure, CLoot, CShip init_physics.
    // Retail (e.g. CGuided 062acfc9..062ad033) copies Orientation transposed into a local
    // matrix -- rows {E00,E10,E20}, {E01,E11,E21}, {E02,E12,E22} -- and calls the cached
    // 3DMathEngine's TransformMatrix (slot +0x2c) on it; the arithmetic is the engine's.
    Vector RotateWorldToObjectSpace(Matrix const& Orient, Vector const& World)
    {
        Matrix Transposed;
        Transposed.E[0][0] = Orient.E[0][0];
        Transposed.E[0][1] = Orient.E[1][0];
        Transposed.E[0][2] = Orient.E[2][0];
        Transposed.E[1][0] = Orient.E[0][1];
        Transposed.E[1][1] = Orient.E[1][1];
        Transposed.E[1][2] = Orient.E[2][1];
        Transposed.E[2][0] = Orient.E[0][2];
        Transposed.E[2][1] = Orient.E[1][2];
        Transposed.E[2][2] = Orient.E[2][2];
        Vector Out;
        Acquire3DMathEngine()->TransformMatrix((FVector&)Out, (FMatrix const&)Transposed,
                                               (FVector const&)World);
        return Out;
    }

    // IObjDB vtable[2]()->vtable[0]() -> CollisionGroupId (system ID).
    // Identical in CGuided, CMine, CCounterMeasure, CLoot, CShip and CSolar init_physics
    // (.text:062a8631, 062ac52b, 062acac1, 062acf88, 062b28d8, 062b7879): none of the six tests
    // the database or the system for null, so neither does this.
    unsigned GetObjectDbCollisionGroup(IObjDB* Db)
    {
        // CALLING CONVENTION (fixed 2026-09-17). Both slots below were DACOM_API
        // (== __stdcall) with the receiver as an explicit stack argument. Both are
        // __thiscall, and both take NO arguments at all. Retail, in CGuided::init_physics:
        //     .text:062b28d8  MOV  ECX,dword ptr [ESI + 0xac]   ; ObjectDb -> ECX
        //     .text:062b28e6  MOV  EDX,dword ptr [ECX]          ; vptr
        //     .text:062b28eb  CALL dword ptr [EDX + 0x8]        ; slot 2, NOTHING pushed
        //     .text:062b28ee  MOV  EDX,dword ptr [EAX]          ; result's vptr
        //     .text:062b28f0  MOV  ECX,EAX                      ; result -> ECX
        //     .text:062b28f2  CALL dword ptr [EDX]              ; slot 0, NOTHING pushed
        // (The `ADD ESP,0xc` at .text:062b28e8 cleans up the preceding cdecl _itoa call, not
        // either of these.) Declared __stdcall with an explicit receiver, each call pushed a
        // dword the callee never pops -- four bytes of caller stack leaked per call, and the
        // callee reading its receiver as garbage out of ECX. Same bug class already fixed for
        // the IObjDB slot-0 enumerator in common_cequip.cpp and common_casteroid.cpp.
        //
        // IObjDB slot 2 (+0x08, GetZoneDb) -> the system's Universe::ISystem; its slot 0
        // (SYSTEM 062969c0, [+0x4c]) is the system id.
        Universe::ISystem const* const System = Db->GetZoneDb();
        return System->GetId();
    }

    void CopyMatrix9(Matrix& Dst, Matrix const& Src)
    {
        unsigned Row = 0;
        for (; Row < 3; ++Row)
        {
            unsigned Col = 0;
            for (; Col < 3; ++Col)
                Dst.E[Row][Col] = Src.E[Row][Col];
        }
    }
} // namespace CmnCobject

void DACOM_API CObject::destroy_instance(long Inst)
{
    // Listing 0x4f610 stdcall. this+4 = -1; if RefCount==1, RefCount=0 then
    // vcall +0x38(1) (scalar deleting dtor). Not FindChild / IVP.
    (void)Inst;
    EngineInstanceId = -1;
    if (RefCount == 1)
    {
        RefCount = 0;
        delete this;
    }
}

CObject::CObject(Class ObjectClass)
    : EngineObject()
{
    // .text:062af2e0..062af3fc. After EngineObject(): +0x4c = Class, +0x50..+0x60 = 0, the
    // PartMap vector empty (062af324..062af32d), +0x74 = 0, the Phantoms list head
    // (operator_new(0xc), self-linked, size 0: 062af337..062af34e), +0x84 = 0, +0x88 = 0, the
    // Advisors list head from the shared node pool (062af363..062af3d7), +0x98 = 0, then the
    // CObject vftable. The std containers' default constructors stand for the list/vector
    // construction; the node-pool refcount (DAT_063fc438) is VC6 allocator internals.
    this->ObjectClass = ObjectClass;
    PhysicsSystemId = 0;
    BodyPtr = nullptr;
    PhyPart = nullptr;
    SurfaceManager = nullptr;
    GolemController = nullptr;
    PhysicsControlMode = 0;
    CollisionStateMgr = nullptr;
    Archetype = nullptr;
    RefCount = 0;
}

// Common.dll.c 40405-40505 (0x211b0), tracker id 10557 (CSimple::CSimple(CSimple const&),
// which flattens/inlines exactly this CObject-owned field-copying prologue in retail -- there
// is no standalone CObject copy ctor symbol in the binary, see the header comment). Field order
// matches the disasm's copy order.
CObject::CObject(CObject const& Other)
    : EngineObject(Other)
    , ObjectClass(Other.ObjectClass)
    , PhysicsSystemId(Other.PhysicsSystemId)
    , BodyPtr(Other.BodyPtr)
    , PhyPart(Other.PhyPart)
    , SurfaceManager(Other.SurfaceManager)
    , GolemController(Other.GolemController)
    , PartMap(Other.PartMap)
    , PhysicsControlMode(Other.PhysicsControlMode)
    , Phantoms(Other.Phantoms)
    , CollisionStateMgr(Other.CollisionStateMgr)
    , Archetype(Other.Archetype)
    , Advisors(Other.Advisors)
    , RefCount(Other.RefCount)
{
    // The CObject half of CSimple's copy ctor, .text:06281235..062812ed, member by member:
    // +0x4c..+0x60 dword copies; +0x64 vector deep copy, exact size (FUN_06282da0 count,
    // operator_new, FUN_06282dc0, _Last = _End = new end); +0x74 dword; +0x78 list deep copy
    // (FUN_062819b0); +0x84 / +0x88 dwords (no AddRef on the archetype); +0x8c list deep copy
    // (FUN_06281a30); +0x98 dword (the source's live refcount).
}

CObject::~CObject()
{
    // .text:062af440..062af5f5.
    // 062af467..062af4b9: unlink this from its class list (FUN_062ae690 + FUN_062af830).
    RemoveFromClassList(this);
    // 062af4be..062af4d1: IEngine+0x60 DestroyInstance if the instance is set; the field is
    // not reset.
    if (EngineInstanceId != -1)
        DALib::Engine->DestroyInstance((void*)EngineInstanceId);
    // 062af4d4..062af4f1: every advisor gets CBase::notify_of_destruction(this) (the ICF body
    // at .text:062ed730, `RET 4`).
    for (std::list<CBase*>::iterator It = Advisors.begin(); It != Advisors.end(); ++It)
        (*It)->notify_of_destruction(this);
    // 062af4f3..062af513: PhySys::DetachPhantom(front) until the phantom list is empty --
    // DetachPhantom unlinks the node itself.
    while (!Phantoms.empty())
        PhySys::DetachPhantom(Phantoms.front());
    // 062af515..062af51b.
    PhySys::DeactivatePhysics(this);
    // 062af51b..062af52d.
    if (Archetype != nullptr)
    {
        Archetype->Release();
        Archetype = nullptr;
    }
    // 062af533..062af5df: Advisors, Phantoms and PartMap are destroyed in that order -- the
    // reverse of their declaration order, which the member destructors reproduce.
}

// The six per-size pools behind CObject::operator new/delete. Retail builds them in static
// initializers .text:062adaa0..062adb92 as {ElementSize, FreeList=0, MaxFreeCount=-1,
// CurrentFreeCount=0} -- Heap(Size, -1) -- at .data 0x063fc400 (0x9c), 0x063fc3f0 (0x120),
// 0x063fc3e0 (0x104), 0x063fc3d0 (0xf0), 0x063fc3c0 (0x1ec), 0x063fc3b0 (0x2dc).
static Heap GObjectHeap9c(0x9c, -1);
static Heap GObjectHeap120(0x120, -1);
static Heap GObjectHeap104(0x104, -1);
static Heap GObjectHeapF0(0xf0, -1);
static Heap GObjectHeap1ec(0x1ec, -1);
static Heap GObjectHeap2dc(0x2dc, -1);

void* CObject::operator new(unsigned int Size) noexcept
{
    // .text:062adbc0. Size switch: 0x120, 0x9c, 0xf0, 0x104 below the JA, 0x1ec and 0x2dc
    // above it; anything else goes to the CRT malloc.
    switch (Size)
    {
    case 0x9c:
        return GObjectHeap9c.Malloc();
    case 0xf0:
        return GObjectHeapF0.Malloc();
    case 0x104:
        return GObjectHeap104.Malloc();
    case 0x120:
        return GObjectHeap120.Malloc();
    case 0x1ec:
        return GObjectHeap1ec.Malloc();
    case 0x2dc:
        return GObjectHeap2dc.Malloc();
    default:
        return malloc(Size);
    }
}

void CObject::operator delete(void* Ptr, unsigned int Size)
{
    // .text:062adc40. The same size switch, returning the block to its pool or to free().
    switch (Size)
    {
    case 0x9c:
        GObjectHeap9c.Free(Ptr);
        return;
    case 0xf0:
        GObjectHeapF0.Free(Ptr);
        return;
    case 0x104:
        GObjectHeap104.Free(Ptr);
        return;
    case 0x120:
        GObjectHeap120.Free(Ptr);
        return;
    case 0x1ec:
        GObjectHeap1ec.Free(Ptr);
        return;
    case 0x2dc:
        GObjectHeap2dc.Free(Ptr);
        return;
    default:
        free(Ptr);
        return;
    }
}

CObject* __cdecl CObject::Alloc(Class ObjectClass)
{
    // Retail takes each object either straight from its size pool (Heap::Malloc) or through
    // CObject::operator new, which picks the same pool by size -- so `new X()` below, which
    // resolves to CObject::operator new(sizeof(X)), lands in the pool retail uses. The
    // new-expression null-checks the (noexcept) result before the ctor, as retail does.
    // .text:062aee50..062af19e. Every branch: allocate, construct with the class value, then
    // `INC [obj+0x98]` (AddRef, no null test on the allocation -- a failed pool allocation
    // crashes in retail on the increment, and does so here too), then push_back onto that
    // class's own list (FUN_062af9b0 / FUN_062af7d0, both a VC6 list::insert at the head,
    // i.e. push_back; they differ only in which STL node cache they draw from). Any other
    // class value returns 0 without a message.
    CObject* Result = nullptr;
    switch (ObjectClass)
    {
    case ClassObject:                                   // Heap 063fc400 (0x9c), list 063fc32c
        Result = new CObject(ClassObject);
        Result->AddRef();
        G_ClassLists[ListObject].push_back(Result);
        break;
    case ClassBeam:                                     // Heap 063fc3f0 (0x120), list 063fc398
        Result = new CBeam(ClassBeam);
        Result->AddRef();
        G_ClassLists[ListBeam].push_back(Result);
        break;
    case ClassGuided:                                   // Heap 063fc3e0 (0x104), list 063fc38c
        Result = new CGuided(ClassGuided);
        Result->AddRef();
        G_ClassLists[ListGuided].push_back(Result);
        break;
    case ClassMine:                                     // Heap 063fc3d0 (0xf0), list 063fc374
        Result = new CMine(ClassMine);
        Result->AddRef();
        G_ClassLists[ListMine].push_back(Result);
        break;
    case ClassCounterMeasure:                           // Heap 063fc3d0 (0xf0), list 063fc368
        Result = new CCounterMeasure(ClassCounterMeasure);
        Result->AddRef();
        G_ClassLists[ListCounterMeasure].push_back(Result);
        break;
    case ClassSolar:                                    // Heap 063fc3c0 (0x1ec), list 063fc35c
        Result = new CSolar(ClassSolar);
        Result->AddRef();
        G_ClassLists[ListSolar].push_back(Result);
        break;
    case ClassShip:                                     // Heap 063fc3b0 (0x2dc), list 063fc3a4
        Result = new CShip();
        Result->AddRef();
        G_ClassLists[ListShip].push_back(Result);
        break;
    case ClassLoot:                                     // operator new(0x104), list 063fc380
        Result = new CLoot(ClassLoot);
        Result->AddRef();
        G_ClassLists[ListLoot].push_back(Result);
        break;
    case ClassAsteroid:                                 // operator new(0xfc), list 063fc350
        Result = new CAsteroid();
        Result->AddRef();
        G_ClassLists[ListAsteroid].push_back(Result);
        break;
    case ClassEquipmentObj:                             // operator new(0x9c), list 063fc338
        Result = new CEquipmentObj(ClassEquipmentObj);
        Result->AddRef();
        G_ClassLists[ListEquipment].push_back(Result);
        break;
    case ClassDynamicAsteroid:                          // operator new(0x9c), list 063fc344
        Result = new CDynamicAsteroid();
        Result->AddRef();
        G_ClassLists[ListDynamicAsteroid].push_back(Result);
        break;
    default:
        break;
    }
    return Result;
}

int __cdecl CObject::Count(Class ObjectClass)
{
    // .text:062ae7e0..062ae892: the class's list size (+0x8), or -1 for a class without one.
    std::list<CObject*>* const List = ListForClass(ObjectClass);
    if (List == nullptr)
        return -1;
    return (int)List->size();
}

CObject* __cdecl CObject::Find(long EngineInstanceId, Class ObjectClass)
{
    // .text:062ae960..062aea30: -1 or a class without a list -> null; the first entry whose
    // instance (+0x4) matches gets RefCount (+0x98) + 1 and is returned.
    if (EngineInstanceId == -1)
        return nullptr;
    std::list<CObject*>* const List = ListForClass(ObjectClass);
    if (List == nullptr)
        return nullptr;
    for (std::list<CObject*>::iterator It = List->begin(); It != List->end(); ++It)
    {
        CObject* const Obj = *It;
        if (Obj->EngineInstanceId == EngineInstanceId)
        {
            Obj->AddRef();
            return Obj;
        }
    }
    return nullptr;
}

CObject* __cdecl CObject::Find(unsigned const& Id, Class ObjectClass)
{
    // .text:062aeb00..062aebca: like Find(long), matching [entry + 0xb0] (CSimple::Id)
    // against *Id for whatever list the class selects. Retail reads +0xb0 even in the
    // ClassObject / EquipmentObj / DynamicAsteroid lists, whose 0x9c-byte objects end
    // before it (reads past the object -- very strange); the port skips those entries
    // instead of reading outside them.
    std::list<CObject*>* List = ListForClass(ObjectClass);
    if (List == nullptr)
        return nullptr;
    for (std::list<CObject*>::iterator It = List->begin(); It != List->end(); ++It)
    {
        CObject* const Obj = *It;
        if (!ClassHasSimpleId(Obj->ObjectClass))
            continue;
        CSimple* const Simple = (CSimple*)Obj;
        if (Simple->Id == Id)
        {
            Obj->AddRef();
            return Obj;
        }
    }
    return nullptr;
}

CObject* __cdecl CObject::FindFirst(Class ObjectClass)
{
    // .text:062aec90..062aed52: the class's list (null for a class without one, which also
    // returns null) is remembered in DAT_063fc430 and its cursor DAT_063fc328 set to begin;
    // an empty list returns null, otherwise the first entry is returned and the cursor
    // advanced. G_bFindActive mirrors "list non-null" for RemoveFromClassList.
    G_FindList = ListForClass(ObjectClass);
    if (G_FindList == nullptr)
    {
        G_bFindActive = false;
        return nullptr;
    }
    G_FindNextIt = G_FindList->begin();
    G_bFindActive = true;
    if (G_FindNextIt == G_FindList->end())
        return nullptr;
    CObject* Obj = *G_FindNextIt;
    ++G_FindNextIt;
    return Obj;
}

CObject* __cdecl CObject::FindNext()
{
    // .text:062aee20..062aee42: no list or cursor at end -> null; else return the entry and
    // advance the cursor.
    if (!G_bFindActive || G_FindList == nullptr)
        return nullptr;
    if (G_FindNextIt == G_FindList->end())
        return nullptr;
    CObject* Obj = *G_FindNextIt;
    ++G_FindNextIt;
    return Obj;
}

void __cdecl CObject::ReleaseAll()
{
    // .text:062af260. Step (1) below is VC6 STL-node-cache bookkeeping with no counterpart
    // in the modern std::list this port uses; step (2) is reproduced here, in retail's order.
    GObjectHeap9c.Shrink();
    GObjectHeap120.Shrink();
    GObjectHeap104.Shrink();
    GObjectHeap1ec.Shrink();
    GObjectHeap2dc.Shrink();
    GObjectHeapF0.Shrink();
    // Common.dll.c 82113-82136 (0x4ef00). Despite the name, retail's ReleaseAll does NOT
    // walk the per-class object lists (DAT_063fc32c etc.) and does NOT release/delete any
    // live CObject at all. It only: (1) walks a private STL-list-node free-list
    // (DAT_063fc420/DAT_063fc42c) freeing each cached node, and (2) calls Heap::Shrink on
    // six per-class custom allocator pools (DAT_063fc3b0.. DAT_063fc400, one per CObject
    // subclass size bucket -- the same pools CObject::operator_new/operator_delete target,
    // see common_cobject.h CObject::Alloc comment "Port: CRT new + class list. Does not
    // Vibe::Alloc"). This port uses plain new/delete for every CObject subclass (see
    // CObject::Alloc) -- the six pools now exist (GObjectHeap*, see CObject::operator new)
    // and are shrunk above. Retail's version never released any live object either.
}

void CObject::open(Archetype::Root* Arch)
{
    // .text:062af400-062af417: Root vtable+0x0c load(), then this+0x88 = Arch. No null test.
    Arch->load();
    Archetype = Arch;
}

int CObject::update(float Dt, unsigned Flags)
{
    // Common.dll.c 82227-82236 (0x4f420): if a physics body exists AND this object's
    // PhyCollisionStateManager (raw offset +0x84, same field PhySys:: uses throughout
    // common_physys.cpp -- see e.g. enable_collisions/get_collision_state at Object+0x84)
    // is non-null, forward the tick to PhyCollisionStateManager::update(Dt).
    (void)Flags;
    if (BodyPtr != nullptr)
    {
        PhySys::PhyCollisionStateManager* Mgr = CollisionStateMgr;
        if (Mgr != nullptr)
            Mgr->update(Dt);
    }
    return 0;
}

float CObject::get_physical_radius_r(Vector& OutCenter) const
{
    // .text:062ad980..062ada0e. With a body (+0x54): PhySys::GetRadiusR(this, false, Out).
    if (BodyPtr != nullptr)
        return PhySys::GetRadiusR(this, false, OutCenter);

    // Otherwise: Radius (pre-zeroed local) and the local centre straight into OutCenter from
    // IEngine +0x88 GetInstanceExtent(EngineInstanceId, 0, &Radius, &OutCenter); then the
    // math singleton (DAT_063fc41c, created on first use) slot +0x28 TransformXform(&tmp,
    // this+0x08 (Orientation + WorldPosition, one Transform), OutCenter); Out = tmp; return
    // Radius. No null tests.
    float Radius = 0.0f;
    DALib::Engine->GetInstanceExtent((void*)EngineInstanceId, 0, &Radius, (float*)&OutCenter);
    I3DMathEngine* const Math = Acquire3DMathEngine();
    Vector World;
    // this+0x08: Orientation followed by WorldPosition, one Transform.
    Math->TransformXform((FVector&)World, *(FTransform const*)&Orientation,
                         (FVector const&)OutCenter);
    OutCenter = World;
    return Radius;
}

float CObject::hierarchy_radius() const
{
    // Common.dll.c 0x1cdd0 -- calls the vtable slot +0x64 (get_physical_radius_r) with a
    // throwaway local Vector, discarding the computed center.
    Vector Discard;
    return get_physical_radius_r(Discard);
}

namespace
{
    // FUN_062ad6f0 (__cdecl, 3 args). Walks Inst's children (IEngine +0xd8
    // GetRelatedInstance(Inst, 1, cursor), first cursor -1, until -1). A child whose user data
    // (IEngine +0x80) is Parent is descended with the same Parent; any other user data is
    // inserted into Set (find, absent -> insert at the end, then found again; the result is
    // not used) and descended from ITS OWN engine instance (+0x04). No null test on the user
    // data.
    void CollectHierarchyObjects(CObject* Parent, long Inst, CheapSet<CObject*, std::less<CObject*>>& Set)
    {
        // IEngine passes instance handles as pointer-sized values.
        void* const InstHandle = (void*)Inst;
        long Child = (long)DALib::Engine->GetRelatedInstance(InstHandle, 1, (void*)(intptr_t)-1);
        while (Child != -1)
        {
            CObject* const Obj = (CObject*)DALib::Engine->GetInstanceUserData((void*)Child);
            if (Obj == Parent)
            {
                CollectHierarchyObjects(Parent, Child, Set);              // 062ad785..062ad788
            }
            else
            {
                Set.Insert(Obj);                                           // 062ad73a..062ad77b
                CollectHierarchyObjects(Obj, Obj->EngineInstanceId, Set); // 062ad77d..062ad788
            }
            Child = (long)DALib::Engine->GetRelatedInstance(InstHandle, 1, (void*)Child);
        }
    }
}

void CObject::get_intruder_set_r(CheapSet<CObject*, std::less<CObject*>>& Out)
{
    // .text:062ad7c0..062ad979. A local set seeded with this (062ad7f4..062ad834); then the
    // walk of FUN_062ad6f0 inlined over this->EngineInstanceId with this as the parent
    // (062ad839..062ad915 -- the same steps, so the helper is called with (this, Inst));
    // finally every member whose collision state is on (PhySys::GetCollisionState) adds its
    // phantom intruders' roots: CollisionStateMgr (+0x84)->get_intruding_cobjs(true, Out)
    // (062ad91b..062ad958). The local vector is freed at the end (062ad95f).
    CheapSet<CObject*, std::less<CObject*>> Hierarchy;
    Hierarchy.Insert(this);
    CollectHierarchyObjects(this, EngineInstanceId, Hierarchy);
    for (std::vector<CObject*>::iterator It = Hierarchy.Items.begin(); It != Hierarchy.Items.end(); ++It)
    {
        CObject* const Obj = *It;
        if (PhySys::GetCollisionState(Obj))
            Obj->CollisionStateMgr->get_intruding_cobjs(true, Out);
    }
}

float CObject::hierarchy_radius(Vector& OutCenter) const
{
    // Common.dll.c 0x1cdf0 -- non-virtual public alias for get_physical_radius_r via the
    // same vtable slot +0x64.
    return get_physical_radius_r(OutCenter);
}

Vector CObject::get_velocity() const
{
    // .text:0627d120: with a physics body (+0x54) -> PhySys::GetVelocity, else zero.
    if (BodyPtr != nullptr)
    {
        return PhySys::GetVelocity(this);
    }
    Vector Out;
    Out.x = 0.0f;
    Out.y = 0.0f;
    Out.z = 0.0f;
    return Out;
}

float CObject::get_mass() const
{
    // .text:062ada60..062ada7a: body (+0x54) -> PhySys::GetMass(this); else FLD float
    // [Archetype(+0x88)+0x20]. No null test on the archetype.
    if (BodyPtr != nullptr)
        return PhySys::GetMass(this);
    return Archetype->Mass;
}

Vector CObject::get_angular_velocity() const
{
    // .text:0627d150-0627d1cf. With a physics body (+0x54), the object-space angular velocity
    // (PhySys::GetAngularVelocityOS, 0627d164) is rotated to world by the 3DMathEngine
    // singleton (DAT_063fc018 / FUN_06391cf0) slot +0x2c TransformMatrix(Out, Orientation, w)
    // = Orientation * w (0627d181-0627d18e); without one the result is three zero dwords.
    if (BodyPtr != nullptr)
    {
        Vector const ObjSpace = PhySys::GetAngularVelocityOS(this);
        Vector World;
        Acquire3DMathEngine()->TransformMatrix((FVector&)World, (FMatrix const&)Orientation,
                                               (FVector const&)ObjSpace);
        return World;
    }
    return Vector{0.0f, 0.0f, 0.0f};
}

void CObject::beam_object(Vector const& Pos, Matrix const& Orient, bool bAttach)
{
    // Common.dll.c 0x4da80. Delegates to PhySys::BeamR.
    PhySys::BeamR(this, Pos, Orient, bAttach);
}

void CObject::disable_controllers()
{
    // .text:062a8920: `RET` (shared with enable_controllers). Retail vtable+0x5c.
}

void CObject::enable_controllers()
{
    // .text:062a8920: `RET` (shared with disable_controllers). Retail vtable+0x60.
}

void CObject::unmake_physical()
{
    // Common.dll.c 0x4d520. vtable+0x5c (disable_controllers) then unmake.
    disable_controllers();
    PhySys::UnMakePhysicalR(this);
}

void CObject::remake_physical(PhySys::CreateParms const& Parms, float Radius)
{
    // Common.dll.c 0x4d4f0 (80743-80750): ReMakePhysicalR, then `CALL [EDX+0x60]`
    // (.text:062ad50a) -- enable_controllers.
    PhySys::ReMakePhysicalR(this, Parms, Radius);
    enable_controllers();
}

long CObject::part_to_inst(unsigned PartId) const
{
    // .text:0627d1e0..0627d214: SurfaceManager (+0x5c) == 0 -> EngineInstanceId; else the
    // first PartMap entry with PartId -> its instance; none -> -1.
    if (SurfaceManager == nullptr)
        return EngineInstanceId;
    for (std::vector<FPartMapEntry>::const_iterator It = PartMap.begin(); It != PartMap.end(); ++It)
    {
        if (It->PartId == PartId)
            return It->EngineInstanceId;
    }
    return -1;
}

Vector CObject::get_moment_of_inertia() const
{
    // Common.dll.c 0x1d0b0: IVP moment-of-inertia if this+0x54 (BodyPtr) is set, else zero.
    if (BodyPtr != nullptr)
    {
        return PhySys::GetMomentOfInertia(this);
    }
    Vector Out;
    Out.x = 0.0f;
    Out.y = 0.0f;
    Out.z = 0.0f;
    return Out;
}

void CObject::add_impulse(Vector const& Impulse)
{
    // Common.dll.c 0x1d0e0: no-op without a physics body.
    if (BodyPtr != nullptr)
    {
        PhySys::LinearImpulse(this, Impulse, 1.0f);
    }
}

void CObject::add_impulse(Vector const& Impulse, Vector const& Point)
{
    // Common.dll.c 0x1d100: no-op without a physics body.
    if (BodyPtr != nullptr)
    {
        PhySys::AddImpulseAtPoint(this, Impulse, Point);
    }
}

unsigned CObject::inst_to_part(long EngineInst) const
{
    // .text:0627d220..0627d243: the first PartMap entry whose instance matches -> its PartId;
    // none -> 0. No +0x5c test.
    for (std::vector<FPartMapEntry>::const_iterator It = PartMap.begin(); It != PartMap.end(); ++It)
    {
        if (It->EngineInstanceId == EngineInst)
            return It->PartId;
    }
    return 0;
}

bool CObject::is_shield_part(unsigned PartId) const
{
    // .text:0627d250..0627d273: the first entry with PartId -> its byte +8; none -> 0.
    for (std::vector<FPartMapEntry>::const_iterator It = PartMap.begin(); It != PartMap.end(); ++It)
    {
        if (It->PartId == PartId)
            return It->bShieldPart != 0;
    }
    return false;
}

bool CObject::flag_part_as_shield(unsigned PartId)
{
    // .text:062ad4c0..062ad4e6: the first entry with PartId gets byte +8 = 1 -> true;
    // none -> false.
    for (std::vector<FPartMapEntry>::iterator It = PartMap.begin(); It != PartMap.end(); ++It)
    {
        if (It->PartId == PartId)
        {
            It->bShieldPart = 1;
            return true;
        }
    }
    return false;
}

bool CObject::get_surface_extents(Vector& OutMin, Vector& OutMax) const
{
    // Common.dll.c 80694-80714 (0x4d470). If the physics part (+0x58) is set, copies its
    // ExtentsMin (+0x08) and ExtentsMax (+0x14) into the two out-params and returns true;
    // else returns false.
    if (PhyPart == nullptr)
        return false;
    OutMin = PhyPart->ExtentsMin;
    OutMax = PhyPart->ExtentsMax;
    return true;
}

Archetype::Root* CObject::get_archetype() const
{
    return Archetype;
}

unsigned CObject::AddRef()
{
    RefCount += 1;
    return RefCount;
}

unsigned CObject::Release()
{
    unsigned Next = RefCount - 1;
    RefCount = Next;
    if (Next == 0)
        delete this;
    return Next;
}

void CObject::advise(CBase* Advisor, bool bOn)
{
    // .text:062af660..062af6f9.
    if (bOn)
    {
        // 062af66c..062af683: FUN_062af8a0(end, 1, &Advisor) -- insert one copy at the end.
        Advisors.push_back(Advisor);
        return;
    }
    // 062af68f..062af6a9: find the first node holding Advisor; none -> return. 062af6b2..
    // 062af6f3: then erase every node whose value equals that node's value (the VC6
    // list::remove(*found) shape; the value is Advisor itself either way).
    Advisors.remove(Advisor);
}

void CObject::enable_collisions_r(bool bEnable)
{
    // .text:062ad540..062ad6ec. A local CheapSet seeded with this (062ad567..062ad5b3); the
    // FUN_062ad6f0 walk inlined over this->EngineInstanceId with this as the parent
    // (062ad5b8..062ad696 -- the same steps, so the helper is called with (this, Inst)); then
    // every member with a physics body (+0x54) gets PhySys::SetCollisionState(obj, bEnable)
    // (062ad69c..062ad6cc); the local vector is freed (062ad6d3). No null tests.
    CheapSet<CObject*, std::less<CObject*>> Hierarchy;
    Hierarchy.Insert(this);
    CollectHierarchyObjects(this, EngineInstanceId, Hierarchy);
    for (std::vector<CObject*>::iterator It = Hierarchy.Items.begin(); It != Hierarchy.Items.end(); ++It)
    {
        CObject* const Obj = *It;
        if (Obj->BodyPtr != nullptr)
            PhySys::SetCollisionState(Obj, bEnable);
    }
}

COMMON_API IBehaviorManager* __cdecl GetBehaviorManager(IObjRW* Obj)
{
    // .text:062d4ba0..062d4beb. Null Obj -> null. Its object (IObjInspect slot 84, +0x150
    // cobject) must exist and be a CEqObj ((ObjectClass +0x4c & 0x103) == 0x103), else
    // null. Then get_behavior_interface(); when that is null the manager is created on the
    // spot: create_behavior_interface(Obj, 0).
    if (Obj == nullptr)
        return nullptr;
    CEqObj* const EqObj = (CEqObj*)const_cast<CObject*>(Obj->cobject());
    if (EqObj == nullptr || (EqObj->ObjectClass & CObject::ClassEqObj) != CObject::ClassEqObj)
        return nullptr;
    IBehaviorManager* const Manager = EqObj->get_behavior_interface();
    if (Manager != nullptr)
        return Manager;
    return EqObj->create_behavior_interface(Obj, 0);
}

// CObject update-flag constants. Retail emits one exported `unsigned int const` per name in
// .rdata at 0x0639e524..0x0639e53c; values are the literal dwords there. See the declarations
// in common_cobject.h for the per-flag read sites. UPDATE_DUMB is the composite
// NO_DYNAMICS|NO_SCANNER|NO_BEHAVIOR|NO_ACTIONS|NO_COUNTERMEASURE_AI (0x01|0x04|0x08|0x10|0x20).
unsigned int const CObject::UPDATE_DEFAULT = 0x00u;
unsigned int const CObject::NO_DYNAMICS = 0x01u;
unsigned int const CObject::NO_SCANNER = 0x04u;
unsigned int const CObject::NO_BEHAVIOR = 0x08u;
unsigned int const CObject::NO_ACTIONS = 0x10u;
unsigned int const CObject::NO_COUNTERMEASURE_AI = 0x20u;
unsigned int const CObject::UPDATE_DUMB = 0x3Du;





Vector CObject::get_center_of_mass() const
{
    // Common.dll.c 81019-81035 (0x4da20). If a physics body exists (BodyPtr, +0x54),
    // delegates to PhySys::GetCenterOfMass(this) [already ported in common_physys.cpp;
    // FULLPORT-DEFERRED there -- returns (0,0,0) since our fake IVP objects carry no
    // hull geometry]. Otherwise returns WorldPosition (+0x2c/+0x30/+0x34) verbatim --
    // NOT a local->world transform through Orientation like get_physical_radius_r's
    // fallback path; retail reads the three WorldPosition floats directly here.
    if (BodyPtr != nullptr)
        return PhySys::GetCenterOfMass(this);
    return WorldPosition;
}


// .text:062a8920 (?ToggleIceCap@@YAX_N@Z): a bare RET in retail -- the empty body is the port.
void __cdecl ToggleIceCap(bool bOn)
{
    (void)bOn;
}
