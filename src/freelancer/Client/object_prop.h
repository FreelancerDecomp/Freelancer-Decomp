// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Gf::ObjectProp -- E:\FL\Scratch\Source\Client\Gf\ObjectProp.cpp (freelancer.exe
// 0x00445e20..0x00446b9f). A room prop: one engine instance loaded from a PetalDB record or an
// archetype, optionally with the ship loadout's equipment attached and its lights blinking.
// RoomControl builds one per RoomData+0x158 record (FUN_0044a030); the Thorn spawn FUN_0044e430
// builds one per COMPOUND entity. Vtable 0x005cbc0c, sizeof 0x80 (operator new(0x80) at
// 0x0044a19f and 0x0044e7ab).

#include "anim_script_set.h"
#include "common_csys.h"
#include "flash_light.h"
#include "room_control.h"

#include <list>
#include <string>

class HardpointSummary;

// The prop description FUN_0044a030 / FUN_0044e430 fill on the stack and FUN_004464f0 reads
// (sizeof 0x64; three VC6 strings at +0x00 / +0x10 / +0x54).
struct FPropData
{
    // FUN_00445e20 -- empty strings, Type -1, Pos 0, Orient identity.
    FPropData();
    // FUN_00445ea0 -- the three strings, last to first.

    std::string Template;   // +0x00 the archetype / PetalDB nickname (or a "$..." indirection)
    std::string Name;       // +0x10 the entity name (FUN_0044a2f0 finds props by it)
    int Type;               // +0x20 0/5/6 PetalDB category, 1 ship, 2 solar, 3 asteroid, 4 equip
    Vector Pos;             // +0x24
    Matrix Orient;          // +0x30
    std::string Loadout;    // +0x54 only read for Type 1
};

class FObjectProp : public FRoomObject
{
public:
    // FUN_00445f40 (thiscall, RET 8).
    FObjectProp(FRoomRuntime* InOwner, FPropData const& Data);
    // FUN_00446020 (FUN_00446000 is the scalar deleting destructor).
    ~FObjectProp() override;

    void Update(double DeltaSeconds) override;      // 5  FUN_00446a60
    void* GetEngineInstance() override;             // 6  LAB_00443870: +0x30
    void DrawOpaque(ICamera* Camera) override;      // 7  FUN_00446ac0
    void DrawTranslucent(ICamera* Camera) override; // 8  FUN_00446b40

    // RoomControl's reads (FUN_0044a030 0x0044a2a3 / FUN_0044e430 0x0044e506 read +0x58, the
    // script table's +0x0c; FUN_00449420 0x00449566 tests +0x49).
    unsigned long GetMainScript() const { return Scripts.Script; }
    char const* GetName() const { return Name.c_str(); }

    void* Instance;                                // +0x30 -1 until a mesh loaded
    int Type;                                      // +0x34 FPropData::Type
    std::string Name;                              // +0x38 FPropData::Name
    unsigned char bDetached;                       // +0x48 0; gates the draws and the release
    unsigned char bRoomMain;                       // +0x49 RoomPropInfo+0x3f (FUN_0044a030)
    FAnimScriptSet Scripts;                        // +0x4c FUN_00555a40 / FUN_00555a70
    HardpointSummary* Hardpoints;                  // +0x64 FUN_004467e0, when a loadout loaded
    std::list<void*> Attached;                     // +0x68 loadout equipment instances
    FFlashLightSet Flashlights;                    // +0x74 FUN_00558180

private:
    // FUN_004464f0 (thiscall, RET 4) -- load the mesh the description names, place it.
    void Load(FPropData const& Data);
    // FUN_00446160 (thiscall, RET 8) -- load a PetalDB record's mesh with its extra materials.
    void LoadPetal(void const* Record, char const* Template);
    // FUN_00446280 (thiscall, RET 4) -- the nickname id of Template; "$..." names one of the
    // current base's ship / terrain nicknames.
    unsigned ResolveTemplate(char const* Template);
    // FUN_004467e0 (thiscall, RET 4) -- attach a ship loadout's equipment.
    void LoadLoadout(char const* LoadoutName);
    // FUN_004468f0 (thiscall, RET 8) -- attach one loadout item.
    void AttachEquipment(void const* Item, HardpointSummary* Summary);
};
