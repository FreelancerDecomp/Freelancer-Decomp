// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CAttachmentObj, CNonPhysAttachment, CPhysAttachment -- split out of common_cequip.cpp (CMN-B2).
// Declared in common_cequip.h; helpers shared with the sibling TUs are in
// common_cequip_internal.h (namespace CmnCequip). Engine instances are the IEngine's `void*`
// instance handles; the attachments keep them as `long` ids, as retail does.
#include "common_cequip.h"
#include "x86math.h"

#include "common_cobject.h"
#include "common_csys.h"     // Acquire3DMathEngine
#include "common_ini.h"
#include "common_physys.h"
#include "DALib.h"
#include "dacom.h"

#include <stdlib.h>
#include "common_cequip_internal.h"

using namespace CmnCequip;

namespace
{
    // FUN_0628f700 (VC6 vector<FPartMapEntry>::_Insert_n on CObject+0x64). Every call site
    // (CNonPhysAttachment::Connect 062939a2, EnableCollisions 0629382e) inserts one element at
    // _Last, i.e. a push_back onto CObject::PartMap.
    void AppendPartMapEntry(CObject* Owner, unsigned PartId, long InstanceId, bool bShieldPart)
    {
        FPartMapEntry Entry;
        Entry.PartId = PartId;
        Entry.EngineInstanceId = InstanceId;
        Entry.bShieldPart = bShieldPart ? 1 : 0;
        Owner->PartMap.push_back(Entry);
    }

    // 0629386c (EnableCollisions(false)): the first PartMap entry for InstanceId is erased (tail
    // shifted down one slot) and its shield byte handed back; false when there is none.
    bool RemovePartMapEntryByInstance(CObject* Owner, long InstanceId, unsigned char& OutFlag)
    {
        // No null check: retail 0629383f reads Owner+0x68 directly.
        for (std::vector<FPartMapEntry>::iterator It = Owner->PartMap.begin(); It != Owner->PartMap.end(); ++It)
        {
            if (It->EngineInstanceId == InstanceId)
            {
                OutFlag = It->bShieldPart;
                Owner->PartMap.erase(It);   // shift the tail down one slot, _Last -= 0xc
                return true;
            }
        }
        return false;
    }

    // The 0x48-byte hardpoint record IHardpoint::GetHardpoint copies out, as FUN_062940c0
    // reads it: +0x00 joint type (2 = revolute), +0x04 position, +0x10 orientation,
    // +0x34 rotation axis, +0x40/+0x44 angle limits.
    struct FHardpointData
    {
        unsigned Type;      // +0x00
        Vector Position;    // +0x04
        Matrix Orient;      // +0x10
        Vector Axis;        // +0x34
        float Min;          // +0x40
        float Max;          // +0x44
    };
    static_assert(sizeof(FHardpointData) == 0x48, "FHardpointData 0x48");

    // .text:062942b0..062942f1 (and the copy at 06294326): wrap a limit range into [0, 360) by
    // whole steps of the double 6.2831854820251465 (DAT_0639c9e0); bounds DAT_0639c430 0.0 and
    // DAT_0639c9d8 360.0. Kept on the x87 stack in retail, so double here.
    double WrapLimitRange(double Range)
    {
        // TEST AH,0x5 + JP: falls into the loop only while C0 is set (Range < 0).
        while (Range < 0.0)
            Range += 6.2831854820251465;
        // TEST AH,0x1 + JNZ skips when Range < 360; JZ loops while Range >= 360.
        while (Range >= 360.0)
            Range -= 6.2831854820251465;
        return Range;
    }

    // Retail FUN_062940c0 (.text:062940c0..06294459, cdecl). Connects ChildInst's ChildHp to
    // ParentInst's ParentHp. When both hardpoints are revolute (type 2) it builds a revolute
    // joint (narrower of the two limit ranges, orientation = parent * child, axis = child axis in
    // that frame) through IEngine::CreateJoint; otherwise IHardpoint::ConnectHardpoints. Returns
    // the child's IEngine::GetInstanceRelative block, or null when either archetype or
    // hardpoint lookup fails. Both archetype handles are released, also when they are -1.
    unsigned long* ConnectAttachedHardpoints(long ParentInst, char const* ParentHp, long ChildInst,
                                             char const* ChildHp)
    {
        unsigned long* Result = nullptr;
        void* ParentHandle = DALib::Engine->GetInstanceArchetype((void*)ParentInst); // +0x64
        if (ParentHandle != (void*)-1)
        {
            FHardpointKey ParentKey;
            ParentKey.Name = ParentHp;
            ParentKey.Crc = DACOM_CRC::GetCRC32(ParentHp);
            FHardpointData ParentData;
            // IHardpoint+0xc; retail tests only AL.
            if ((unsigned char)DALib::Hardpoint->GetHardpoint(ParentHandle, &ParentKey, &ParentData) != 0)
            {
                void* ChildHandle = DALib::Engine->GetInstanceArchetype((void*)ChildInst);
                if (ChildHandle != (void*)-1)
                {
                    FHardpointKey ChildKey;
                    ChildKey.Name = ChildHp;
                    ChildKey.Crc = DACOM_CRC::GetCRC32(ChildHp);
                    FHardpointData ChildData;
                    if ((unsigned char)DALib::Hardpoint->GetHardpoint(ChildHandle, &ChildKey, &ChildData) != 0)
                    {
                        if (ChildData.Type == 2 && ParentData.Type == 2)
                        {
                            FJointRelative Joint;   // retail ESP+0x30 (after PUSH EBP)
                            Joint.Type = 2;
                            Joint.Field_0x54 = 0;  // retail: unwritten stack (see FJointRelative)
                            Joint.Field_0x58 = 0;
                            Joint.ParentPos = ParentData.Position;
                            Joint.ChildPos = ChildData.Position;
                            I3DMathEngine* Math = Acquire3DMathEngine();  // DAT_063fc114
                            Matrix Combined;
                            Math->MulMatrix((FMatrix&)Combined, (FMatrix const&)ParentData.Orient,
                                            (FMatrix const&)ChildData.Orient); // +0x20
                            Joint.Orient = Combined;
                            Vector Axis;
                            Math->TransformMatrix((FVector&)Axis, (FMatrix const&)Joint.Orient,
                                                  (FVector const&)ChildData.Axis); // +0x2c
                            Joint.Axis = Axis;

                            // 0x06294265: parent range; FCOM float 2pi + TEST AH,0x1 + JNZ:
                            // range >= 2pi falls through and opens the limits to [-pi, pi]
                            // (unordered sets C0 too, so a NaN range takes the wrap path).
                            double ParentRange = (double)ParentData.Max - ParentData.Min;
                            if (ParentRange >= 6.2831855f)
                            {
                                ParentData.Min = -3.1415927f;  // 0xc0490fdb
                                ParentData.Max = 3.1415927f;   // 0x40490fdb
                            }
                            else
                            {
                                ParentRange = WrapLimitRange(ParentRange);
                            }
                            double ChildRange = (double)ChildData.Max - ChildData.Min;
                            if (ChildRange >= 6.2831855f)
                            {
                                ChildData.Min = -3.1415927f;
                                ChildData.Max = 3.1415927f;
                            }
                            else
                            {
                                ChildRange = WrapLimitRange(ChildRange);
                            }
                            // 0x06294366: FCOMP parent vs child, TEST AH,0x5 + JP: parent < child
                            // falls through and keeps the parent limits, else the child's.
                            if (ParentRange < ChildRange)
                            {
                                Joint.Min = ParentData.Min;
                                Joint.Max = ParentData.Max;
                            }
                            else
                            {
                                Joint.Min = ChildData.Min;
                                Joint.Max = ChildData.Max;
                            }
                            DALib::Engine->CreateJoint((void*)ParentInst, (void*)ChildInst,
                                                       (unsigned long const*)&Joint); // +0xbc
                        }
                        else
                        {
                            FHardpointKey ChildConnectKey;
                            ChildConnectKey.Name = ChildHp;
                            ChildConnectKey.Crc = DACOM_CRC::GetCRC32(ChildHp);
                            FHardpointKey ParentConnectKey;
                            ParentConnectKey.Name = ParentHp;
                            ParentConnectKey.Crc = DACOM_CRC::GetCRC32(ParentHp);
                            DALib::Hardpoint->ConnectHardpoints((void*)ParentInst, &ParentConnectKey,
                                                                (void*)ChildInst, &ChildConnectKey); // +0x18
                        }
                        Result = DALib::Engine->GetInstanceRelative((void*)ChildInst); // +0xc4
                    }
                }
                DALib::Engine->ReleaseHandle(ChildHandle);   // +0x3c
            }
        }
        DALib::Engine->ReleaseHandle(ParentHandle);          // +0x3c
        return Result;
    }
}

CAttachmentObj::~CAttachmentObj()
{
}

// GetRootIndex / GetPhysicsOwner / EnableCollisions / Connect / Disconnect are pure in retail
// (_purecall slots 1-5 of .rdata:0639c6b0) and have no CAttachmentObj body or export.

CNonPhysAttachment::CNonPhysAttachment()
{
    // 062937a0: +4 = -1, +8 = 0, byte +0xc = 0. Retail leaves CachedShieldFlag (+0xd)
    // unwritten; the port zeroes it.
    EngineInstanceId = -1;
    HpCrc = 0;
    bCollisions = 0;
    CachedShieldFlag = 0;
}

CNonPhysAttachment::CNonPhysAttachment(const CNonPhysAttachment& Other)
{
    // 06292c00: +4, +8, byte +0xc, byte +0xd.
    EngineInstanceId = Other.EngineInstanceId;
    HpCrc = Other.HpCrc;
    bCollisions = Other.bCollisions;
    CachedShieldFlag = Other.CachedShieldFlag;
}

CNonPhysAttachment& CNonPhysAttachment::operator=(const CNonPhysAttachment& Other)
{
    // 06292c30: +4, +8, byte +0xc, byte +0xd; no self test.
    EngineInstanceId = Other.EngineInstanceId;
    HpCrc = Other.HpCrc;
    bCollisions = Other.bCollisions;
    CachedShieldFlag = Other.CachedShieldFlag;
    return *this;
}

CNonPhysAttachment::~CNonPhysAttachment()
{
    // Listing 0x06292bf0: ICF-folded with ~CAttachmentObj -- the vtable store and RET, nothing
    // else (the scalar deleting destructor LAB_06292c60 at vftable 0639c6cc slot 0 calls it).
    // The engine instance is NOT destroyed here: on a FATE_STAY disconnect it stays a child of
    // the owner's instance and keeps its PartMap entry (Connect appended one); it goes away
    // with the owner's instance tree.
}

long CNonPhysAttachment::GetRootIndex() const
{
    return EngineInstanceId;
}

CObject* CNonPhysAttachment::GetPhysicsOwner() const
{
    // .text:062937c0..062937dc: only -1 means "no instance"; otherwise IEngine+0x80.
    if (EngineInstanceId == -1)
        return nullptr;
    return (CObject*)DALib::Engine->GetInstanceUserData((void*)EngineInstanceId);
}

void CNonPhysAttachment::EnableCollisions(bool bEnable)
{
    // Listing 0x337e0: nothing unless the state changes and there is an instance
    // (0x062937eb / 0x062937f4).
    if ((unsigned char)bEnable == bCollisions)   // 062937eb
        return;
    if (EngineInstanceId == -1)                  // 062937f4
        return;

    CObject* Owner = GetPhysicsOwner();
    if (bEnable)
    {
        // 0x06293815/0x06293820: the entry comes back with the cached shield byte.
        AppendPartMapEntry(Owner, HpCrc, EngineInstanceId, CachedShieldFlag != 0);
    }
    else
    {
        // 0x0629386c: the entry is removed and its shield byte cached; with no entry the cache
        // keeps its value.
        unsigned char RemovedFlag = 0;
        if (RemovePartMapEntryByInstance(Owner, EngineInstanceId, RemovedFlag))
            CachedShieldFlag = RemovedFlag;
    }
    bCollisions = bEnable ? 1 : 0;
}

bool CNonPhysAttachment::Connect(CAttachedEquip* Equip)
{
    // Listing 0x062938c0..0x06293a51.
    Archetype::AttachedEquipment* Arch = (Archetype::AttachedEquipment*)Equip->Arch;
    EngineInstanceId = Arch->create_instance(nullptr);
    DALib::Engine->UpdateInstance((void*)EngineInstanceId, 0, nullptr); // +0x6c
    if (EngineInstanceId == -1)
        return false;

    char const* ChildHp = Arch->HpChild.Value;                       // Arch+0x80
    if (ConnectAttachedHardpoints(Equip->GetParentConnector(false),  // vt+0x64
                                  Equip->GetParentHPName().Value,    // vt+0x60
                                  EngineInstanceId, ChildHp) == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: CAttachedEquip: Failed to connect %s on %s to %s (Ignorable)",
              kCAttachedEquipSource, 0x1d0, Arch->HpChild.Value, Arch->DaArchetype.Value,
              Equip->GetParentHPName().Value);
        return false;
    }

    HpCrc = DACOM_CRC::GetCRC32(Equip->GetParentHPName().Value);
    void* ParentUser = DALib::Engine->GetInstanceUserData((void*)Equip->GetParentConnector(false)); // +0x80
    DALib::Engine->SetInstanceUserData((void*)EngineInstanceId, ParentUser); // +0x7c

    // 0x062939a2: FUN_0628f700 = insert one {HpCrc, instance, flag byte 0} at the end of the
    // parent's PartMap vector (+0x64 object, end = +0x6c).
    AppendPartMapEntry((CObject*)ParentUser, HpCrc, EngineInstanceId, false);

    void* Child = DALib::Engine->GetRelatedInstance((void*)EngineInstanceId, 0, (void*)-1);   // +0xd8
    while (Child != (void*)-1)
    {
        DALib::Engine->SetInstanceUserData(Child, ParentUser);
        Child = DALib::Engine->GetRelatedInstance((void*)EngineInstanceId, 0, Child);
    }
    bCollisions = 1;
    return true;
}

CObject* CNonPhysAttachment::Disconnect(CAttachedEquip* Equip, bool bMakeDebris)
{
    // Listing 0x06293a60..0x06293cec.
    CObject* Debris = nullptr;
    if (bMakeDebris)
    {
        void* Inst = (void*)EngineInstanceId;
        CObject* ParentObj = (CObject*)DALib::Engine->GetInstanceUserData(Inst); // +0x80
        if (ParentObj != nullptr)
        {
            // 0x06293a91..0x06293af8: erase the first PartMap entry whose PartId (hardpoint
            // CRC) is HpCrc (tail shifted down one slot, _Last -= 0xc).
            for (std::vector<FPartMapEntry>::iterator It = ParentObj->PartMap.begin();
                 It != ParentObj->PartMap.end(); ++It)
            {
                if (It->PartId == HpCrc)
                {
                    ParentObj->PartMap.erase(It);
                    break;
                }
            }
        }

        void* ParentInst = DALib::Engine->GetInstanceParent(Inst);                 // +0xd4
        DALib::Engine->DestroyJoint(ParentInst, Inst);                             // +0xc0
        Debris = CObject::Alloc(CObject::ClassEquipmentObj);
        Debris->open(Equip->Arch);
        IEngineInstance* const DebrisInstance = Debris;
        DALib::Engine->SetInstanceTransform(Inst, DebrisInstance);                 // +0x70

        // 0x06293b53..0x06293c56. Retail first stores the CreateParms defaults (mass 1.0, default
        // linear/angular damping) and then overwrites them; the values that reach BuildIVP_Real
        // are: archetype mass, ZERO linear and angular damping (a zero Vector temporary is copied
        // over the angular damping, re-checked in the raw bytes at 0x06293c18), the parent's
        // system id, type 2, flag 0, collisions on, itoa(owner id, radix 0x18) and the
        // archetype's rotation inertia when it has one. Pad27 is not written by retail.
        PhySys::CreateParms Parms;
        Parms.Mass = Equip->Arch->Mass;                        // Arch+0x20
        Parms.LinearDamping = 0.0f;
        Parms.AngularDamping[0] = 0.0f;
        Parms.AngularDamping[1] = 0.0f;
        Parms.AngularDamping[2] = 0.0f;
        Parms.CollisionGroupId = ParentObj->PhysicsSystemId;   // parent +0x50
        Parms.Type = 2;
        Parms.bFlag1c = 0;
        Parms.bEnableCollisions = 1;
        Parms.Name[0] = '\0';
        _itoa((int)Equip->Owner->Id, Parms.Name, 0x18);
        Parms.bHasInertia = 0;
        Parms.Pad27 = 0;
        Parms.Inertia.x = 0.0f;
        Parms.Inertia.y = 0.0f;
        Parms.Inertia.z = 0.0f;
        if (Equip->Arch->bHasRotationInertia)                  // Arch+0x38
        {
            Parms.bHasInertia = 1;
            Parms.Inertia.x = Equip->Arch->RotationInertiaX;   // Arch+0x2c..+0x34
            Parms.Inertia.y = Equip->Arch->RotationInertiaY;
            Parms.Inertia.z = Equip->Arch->RotationInertiaZ;
        }
        PhySys::BuildIVP_Real(Debris, Parms);

        if (Debris->BodyPtr != nullptr)
        {
            Vector ParentVel = PhySys::GetVelocity(ParentObj);
            PhySys::AddToVelocity(Debris, ParentVel);
        }
        else
        {
            Archetype::Equipment* Arch = Equip->Arch;
            FDUMP(0x100001,
                  "%s(%d) : *** ERROR: CAttachedEquip: Failed to create physics for [%s]: %s (Ignorable)",
                  kCAttachedEquipSource, 0x224, Arch->Nickname.get_string(), Arch->DaArchetype.Value);
            Debris->Release();
            Debris = nullptr;
        }
    }
    else
    {
        // 0x06293cc0: FUN_0628a410(GetRootIndex()) (vtable+4).
        DetachInstanceInPlace(GetRootIndex());
    }

    EngineInstanceId = -1;
    HpCrc = 0;
    bCollisions = 0;
    return Debris;
}

CPhysAttachment::CPhysAttachment()
{
    EquipObject = nullptr;
}

CPhysAttachment::CPhysAttachment(const CPhysAttachment& Other)
{
    // Common.dll.c 0x32cc0: a shallow pointer copy of EquipObject, no AddRef.
    EquipObject = Other.EquipObject;
}

CPhysAttachment::~CPhysAttachment()
{
    if (EquipObject != nullptr)
    {
        EquipObject->Release();
        EquipObject = nullptr;
    }
}

long CPhysAttachment::GetRootIndex() const
{
    if (EquipObject == nullptr)
        return -1;
    return EquipObject->EngineInstanceId;
}

CObject* CPhysAttachment::GetPhysicsOwner() const
{
    return EquipObject;
}

void CPhysAttachment::EnableCollisions(bool bEnable)
{
    // Common.dll.c 56086-56099 (0x33d60): only on a change of the physics collision state.
    if (EquipObject == nullptr)
        return;
    if (PhySys::GetCollisionState(EquipObject) != bEnable)
        PhySys::SetCollisionState(EquipObject, bEnable);
}

bool CPhysAttachment::Connect(CAttachedEquip* Equip)
{
    // 06293d90..06293fc2 (RET 4).
    CEqObj* ParentObj = Equip->Owner;
    Archetype::AttachedEquipment* Arch = (Archetype::AttachedEquipment*)Equip->Arch;

    // 06293d9e-06293ddc: a new equipment object, opened on the archetype (CObject slot
    // +0x4c); create_instance writes its instance through IEngineInstance (the returned id is
    // not stored); UpdateInstance(instance, 0, 0).
    EquipObject = CObject::Alloc(CObject::ClassEquipmentObj);
    EquipObject->open(Arch);
    Arch->create_instance(EquipObject);
    DALib::Engine->UpdateInstance((void*)EquipObject->EngineInstanceId, 0, nullptr); // +0x6c

    if (EquipObject == nullptr)
    {
        // 06293f90-06293fc2, then 06293f33: unreachable (EquipObject was dereferenced above).
        // Retail reports the owner archetype's nickname and then calls CObject::Release on the
        // null pointer.
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: CAttachedEquip: Object (%s) failed to create attachment instance (Ignorable)\n",
              kCAttachedEquipSource, 0x2ab, ParentObj->Archetype->Nickname.get_string());
        EquipObject->Release();
        EquipObject = nullptr;
        return false;
    }

    // 06293dea-06293e19: the child hardpoint (Arch+0x80) and the instance id are read first,
    // then GetParentHPName (vt+0x60), then GetParentConnector(false) (vt+0x64).
    char const* ChildHp = Arch->HpChild.Value;
    long const Inst = EquipObject->EngineInstanceId;
    char const* ParentHp = Equip->GetParentHPName().Value;
    long const ParentInst = Equip->GetParentConnector(false);
    if (ConnectAttachedHardpoints(ParentInst, ParentHp, Inst, ChildHp) == nullptr)
    {
        // 06293f50-06293f8c.
        FDUMP(0x100001, "%s(%d) : *** ERROR: CAttachedEquip: Failed to connect %s on %s to %s (Ignorable)",
              kCAttachedEquipSource, 0x2a5, Arch->HpChild.Value, Arch->DaArchetype.Value,
              Equip->GetParentHPName().Value);
        EquipObject->Release();
        EquipObject = nullptr;
        return false;
    }

    // 0x06293e29..0x06293ec2: mass 1.0, default damping, the owner's system id, type 2,
    // flag 0, collisions OFF (+0x1d = 0, written last), itoa(owner id, radix 0x18), no inertia.
    PhySys::CreateParms Parms;
    Parms.Mass = 1.0f;
    Parms.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
    Parms.AngularDamping[0] = PhySys::DEFAULT_ANGULAR_DAMPING.x;
    Parms.AngularDamping[1] = PhySys::DEFAULT_ANGULAR_DAMPING.y;
    Parms.AngularDamping[2] = PhySys::DEFAULT_ANGULAR_DAMPING.z;
    Parms.CollisionGroupId = ParentObj->PhysicsSystemId;   // Owner+0x50
    Parms.Type = 2;
    Parms.bFlag1c = 0;
    Parms.bEnableCollisions = 0;
    Parms.Name[0] = '\0';
    _itoa((int)ParentObj->Id, Parms.Name, 0x18);
    Parms.bHasInertia = 0;
    Parms.Pad27 = 0;
    Parms.Inertia.x = 0.0f;
    Parms.Inertia.y = 0.0f;
    Parms.Inertia.z = 0.0f;
    PhySys::BuildIVP_Real(EquipObject, Parms);

    if (EquipObject->BodyPtr != nullptr)
    {
        PhySys::Attach(ParentObj, EquipObject);
        EquipObject->enable_collisions_r(true);
        EquipObject->AddRef();
        return true;
    }

    FDUMP(0x100001, "%s(%d) : *** ERROR: CAttachedEquip: Failed to create physics for [%s]: %s (Ignorable)",
          kCAttachedEquipSource, 0x29d, Arch->Nickname.get_string(), Arch->DaArchetype.Value);
    EquipObject->Release();
    EquipObject = nullptr;
    return false;
}

CObject* CPhysAttachment::Disconnect(CAttachedEquip* Equip, bool bMakeDebris)
{
    // Listing 0x06293fd0..0x06294099.
    CObject* Debris = nullptr;
    if (bMakeDebris)
    {
        Archetype::Equipment* Arch = Equip->Arch;
        PhySys::DetachParms Parms;
        Parms.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
        Parms.AngularDamping = PhySys::DEFAULT_ANGULAR_DAMPING;
        Parms.Name[0] = '\0';
        Parms.Mass = Arch->Mass;                         // 1.0f, then Arch+0x20
        Parms.bHasInertia = 0;
        Parms.Inertia.x = 0.0f;
        Parms.Inertia.y = 0.0f;
        Parms.Inertia.z = 0.0f;
        if (Arch->bHasRotationInertia)                   // Arch+0x38
        {
            Parms.bHasInertia = 1;
            Parms.Inertia.x = Arch->RotationInertiaX;    // Arch+0x2c..+0x34
            Parms.Inertia.y = Arch->RotationInertiaY;
            Parms.Inertia.z = Arch->RotationInertiaZ;
        }
        Debris = DetachInstance(GetRootIndex(), Parms);   // FUN_0628a050
    }
    else
    {
        DetachInstanceInPlace(GetRootIndex());           // FUN_0628a410
    }

    EquipObject->Release();
    EquipObject = nullptr;
    return Debris;
}
