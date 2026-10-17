// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The equipment-visual set: the engine instances that show the equipment mounted on a ship's
// hardpoints (guns, thrusters, lights), plus the blinking lights of the light items. Retail
// freelancer.exe 0x00457d60..0x004582c0, no vtable and no RTTI, so the retail class name is
// unknown; the name here is descriptive. operator new(0x2c) at 0x00585173 (ShipViewControl)
// and inside the other two owners (callers 0x004438xx / 0x004470xx .. 0x004478xx).
//
// Layout (ctor FUN_00457d60):
//   +0x00 HardpointSummary* (operator new(0x20) in FUN_00457f10; the exe's own copy of the
//         HardpointSummary vftable 0x005cbd68, whose slot 0 FUN_004468d0 is the plain
//         ~HardpointSummary + operator delete)
//   +0x04 hardpoint count
//   +0x08 long[count]      engine instance of the item on each hardpoint (-1 when empty)
//   +0x0c unsigned[count]  archetype id of that item (0 when empty)
//   +0x10 VC6 std::vector<FCliMaterialEntry*> (allocator byte, begin +0x14, end +0x18, cap +0x1c)
//         -- the material-library entries FUN_0041c540 hands over for the attached meshes
//   +0x20 FFlashLightSet (FUN_00558180)

#include "flash_light.h"
#include "client_cliobj.h"

#include <vector>

class EquipDescList;
class HardpointSummary;
struct ICamera;

namespace Archetype
{
    struct Equipment;
}

struct FEquipVisualSet
{
    // FUN_00457d60 -- the four head fields zeroed, the vector and the flashlight set empty.
    FEquipVisualSet();
    // FUN_00457dc0 -- Clear(), then the flashlight set and the vector buffer.
    ~FEquipVisualSet();

    // FUN_00457e20 -- zero the four head fields and nothing else (no release).
    void ResetFields();
    // FUN_00457e30 -- release every material entry and empty the vector, destroy every item
    // instance, empty the flashlight set, free both arrays and delete the summary.
    void Clear();
    // FUN_00457f00 (RET 4) -- Instances[Index], no range check.
    long GetInstance(int Index) const;
    // FUN_00457f10 (RET 4) -- a new HardpointSummary analysed on RootInstance, and both
    // per-hardpoint arrays sized to its count (-1 / 0).
    void Populate(long RootInstance);
    // FUN_00457fe0 (RET 8) -- put Equip's visual attachment on hardpoint HpIndex: either
    // connect the attachment's HpChild hardpoint to it (DALib::Hardpoint slot 6) or join it to
    // the hardpoint's frame (IEngine::CreateJoint). False when the hardpoint is taken, the good
    // is unknown or has no visual attachment.
    bool Attach(int HpIndex, Archetype::Equipment const* Equip);
    // FUN_00458160 (RET 4) -- destroy the instance on HpIndex (when there is one) and clear
    // its archetype id.
    void Detach(int HpIndex);
    // FUN_004581b0 (RET 4) -- Attach every non-internal item of List whose hardpoint the
    // summary knows.
    void AttachAll(EquipDescList const* List);
    // FUN_00458220 (RET 4) -- a flashlight for every non-internal always-on light item of List
    // whose hardpoint the summary knows.
    void AddFlashLights(EquipDescList const* List);
    // FUN_004582b0 -- JMP FUN_00558580 on +0x20.
    void DrawFlashLights(ICamera* Camera);
    // FUN_004582c0 (RET 4) -- FUN_00558470 on +0x20 with the summary.
    void UpdateFlashLights(float Delta);

    HardpointSummary* Summary;                                  // +0x00
    int Count;                                                  // +0x04
    long* Instances;                                            // +0x08
    unsigned* ArchIds;                                          // +0x0c
    std::vector<Archetype::FCliMaterialEntry*> MaterialEntries; // +0x10
    FFlashLightSet FlashLights;                                 // +0x20

private:
    FEquipVisualSet(FEquipVisualSet const&) = delete;
    FEquipVisualSet& operator=(FEquipVisualSet const&) = delete;
};
