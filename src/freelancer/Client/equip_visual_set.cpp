// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The equipment-visual set -- see equip_visual_set.h. Retail freelancer.exe 0x00457d60..0x004582c0.

#include "equip_visual_set.h"

#include "misc_util.h"        // MiscUtilLoadEngineInstance (FUN_0041c540)

#include "DALib.h"
#include "dacom.h"
#include "engine/engbase.h"
#include "common_archetype.h"
#include "common_goods.h"

#include <stdint.h>
#include <string.h>

FEquipVisualSet::FEquipVisualSet()
    : Summary(nullptr), Count(0), Instances(nullptr), ArchIds(nullptr)
{
    // FUN_00457d60: the vector (+0x10..+0x1c) and FUN_00558180 on +0x20 first, then the four
    // head dwords -- the member order of the initializer list above is only notation.
}

FEquipVisualSet::~FEquipVisualSet()
{
    // FUN_00457dc0: FUN_00457e30, then FUN_005581b0 on +0x20 and operator delete on the vector
    // buffer -- the member destructors in reverse declaration order.
    Clear();
}

void FEquipVisualSet::ResetFields()
{
    // FUN_00457e20.
    Summary = nullptr;
    Count = 0;
    Instances = nullptr;
    ArchIds = nullptr;
}

void FEquipVisualSet::Clear()
{
    // FUN_00457e30. 0x00457e36..0x00457e58: FUN_0041b030 on every slot of the vector, then
    // 0x00457e5a..0x00457e74 the erase of [begin, end).
    for (size_t Index = 0; Index < MaterialEntries.size(); ++Index)
        Archetype::ReleaseMaterialEntrySlot(&MaterialEntries[Index]);
    MaterialEntries.clear();

    // 0x00457e77..0x00457eb3: IEngine slot 24 (+0x60) on every live item instance.
    for (int Index = 0; Index < Count; ++Index)
    {
        if (Instances[Index] != -1)
        {
            DALib::Engine->DestroyInstance(reinterpret_cast<void*>(Instances[Index]));
            Instances[Index] = -1;
        }
        ArchIds[Index] = 0;
    }

    // 0x00457eb5: FUN_005582a0.
    FlashLights.Clear();

    // 0x00457ebd..0x00457edc: operator delete on both arrays, then the summary's deleting
    // destructor (vftable slot 0 with 1) when there is one.
    operator delete(Instances);
    operator delete(ArchIds);
    if (Summary != nullptr)
        delete Summary;

    Summary = nullptr;
    Count = 0;
    Instances = nullptr;
    ArchIds = nullptr;
}

long FEquipVisualSet::GetInstance(int Index) const
{
    // FUN_00457f00.
    return Instances[Index];
}

void FEquipVisualSet::Populate(long RootInstance)
{
    // FUN_00457f10. operator new(0x20) + the Common ctor (0x00457f48); retail tests the
    // allocation and stores 0 on failure, which the throwing new of the port never returns.
    Summary = new HardpointSummary();
    Summary->analyze_instance(RootInstance);
    Count = Summary->get_count();
    Instances = static_cast<long*>(operator new(static_cast<size_t>(Count) * sizeof(long)));
    ArchIds = static_cast<unsigned*>(operator new(static_cast<size_t>(Count) * sizeof(unsigned)));
    for (int Index = 0; Index < Count; ++Index)
    {
        Instances[Index] = -1;
        ArchIds[Index] = 0;
    }
}

bool FEquipVisualSet::Attach(int HpIndex, Archetype::Equipment const* Equip)
{
    // FUN_00457fe0.
    if (Instances[HpIndex] != -1)
        return false;

    // 0x00457ff9..0x00458009: the archetype id is recorded before the good is even looked up.
    unsigned const ArchId = Equip->Nickname.Id;
    ArchIds[HpIndex] = ArchId;
    Archetype::ICliObj* const Companion = Equip->Companion;

    GoodInfo const* const Good = GoodList::find_by_archetype(ArchId);
    if (Good == nullptr)
        return false;
    if (!Good->has_visual_attachment())
        return false;

    // 0x00458037..0x00458061: FUN_0041c540(attachment, DALib::Data, 0, 0, companion +0x20,
    // &this+0x10, 0).
    Instances[HpIndex] = static_cast<long>(reinterpret_cast<intptr_t>(MiscUtilLoadEngineInstance(
        Good->AttachmentArchetype.Value, DALib::Data, nullptr, nullptr,
        Archetype::CliObjMaterialPaths(Companion), &MaterialEntries, nullptr)));

    char const* const HpChild = Good->HpChild.Value;
    if (HpChild != nullptr)
    {
        // 0x00458071..0x004580c8: the attachment's HpChild hardpoint is connected to the
        // ship's hardpoint -- DALib::Hardpoint slot 6 (parent instance, {name, crc},
        // attachment instance, {HpChild, crc}).
        FHardpointKey ChildKey;
        ChildKey.Name = HpChild;
        ChildKey.Crc = DACOM_CRC::GetCRC32(HpChild);
        FHardpointKey ParentKey;
        ParentKey.Name = Summary->get_name(HpIndex);
        ParentKey.Crc = DACOM_CRC::GetCRC32(ParentKey.Name);
        void* const Child = reinterpret_cast<void*>(Instances[HpIndex]);
        void* const Parent = reinterpret_cast<void*>(Summary->get_instance(HpIndex));
        DALib::Hardpoint->ConnectHardpoints(Parent, &ParentKey, Child, &ChildKey);
        return true;
    }

    // 0x004580d6..0x0045813e: a fixed joint (type 0) at the hardpoint's position and
    // orientation -- IEngine slot 47 (+0xbc) CreateJoint(parent, attachment, payload). The
    // payload is retail's full 0x5C-byte joint block (ESP+0x20..0x7c, the top of the 0x6c frame;
    // EngBase FUN_0661ec90 copies all 0x17 dwords): 0x004580e0 writes [0] = 0, then [4..6] and
    // [7..15]. Dwords 1..3 and 16..22 are never written by retail (stack garbage); the port
    // zeroes them.
    unsigned long Payload[G_EngineJointPayloadDwords];
    memset(Payload, 0, sizeof(Payload));
    HardpointInfo const& PosInfo = Summary->get_info(HpIndex);
    memcpy(&Payload[4], &PosInfo.Position, 3 * sizeof(float));
    HardpointInfo const& OrientInfo = Summary->get_info(HpIndex);
    memcpy(&Payload[7], &OrientInfo.Orient, 9 * sizeof(float));
    void* const Child = reinterpret_cast<void*>(Instances[HpIndex]);
    void* const Parent = reinterpret_cast<void*>(Summary->get_instance(HpIndex));
    DALib::Engine->CreateJoint(Parent, Child, Payload);
    return true;
}

void FEquipVisualSet::Detach(int HpIndex)
{
    // FUN_00458160.
    if (Instances[HpIndex] != -1)
    {
        DALib::Engine->DestroyInstance(reinterpret_cast<void*>(Instances[HpIndex]));
        Instances[HpIndex] = -1;
    }
    ArchIds[HpIndex] = 0;
}

void FEquipVisualSet::AttachAll(EquipDescList const* List)
{
    // FUN_004581b0: the list in its own order (node +0x08 is the EquipDesc).
    for (std::list<EquipDesc>::const_iterator It = List->Items.begin(); It != List->Items.end(); ++It)
    {
        EquipDesc const& Desc = *It;
        Archetype::Equipment* const Equip = Archetype::GetEquipment(Desc.ArchId);
        if (Desc.is_internal())
            continue;
        int const HpIndex = Summary->find_name(Desc.Hardpoint);
        if (HpIndex == -1)
            continue;
        Attach(HpIndex, Equip);
    }
}

void FEquipVisualSet::AddFlashLights(EquipDescList const* List)
{
    // FUN_00458220.
    for (std::list<EquipDesc>::const_iterator It = List->Items.begin(); It != List->Items.end(); ++It)
    {
        EquipDesc const& Desc = *It;
        Archetype::Equipment* const Equip = Archetype::GetEquipment(Desc.ArchId);
        if (Desc.is_internal())
            continue;
        int const HpIndex = Summary->find_name(Desc.Hardpoint);
        if (HpIndex == -1)
            continue;
        // 0x00458276..0x00458299: slot 4 get_class_type == 0x15 (Light) and always_on (+0x70).
        Archetype::ICliObj* const Companion = Equip->Companion;
        if (Equip->get_class_type() != Archetype::AClassLight)
            continue;
        if (!static_cast<Archetype::Light const*>(static_cast<Archetype::Root const*>(Equip))
                 ->bAlwaysOn)
            continue;
        FlashLights.Add(Companion, HpIndex);
    }
}

void FEquipVisualSet::DrawFlashLights(ICamera* Camera)
{
    // FUN_004582b0.
    FlashLights.Draw(Camera);
}

void FEquipVisualSet::UpdateFlashLights(float Delta)
{
    // FUN_004582c0.
    FlashLights.Update(Summary, Delta);
}
