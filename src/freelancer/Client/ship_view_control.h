// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail ShipViewControl (RTTI .?AUShipViewControl@@ : WinControl) -- the rotating 3D ship
// preview of the NeuroNet inventory / dealer / ship-dealer windows: the ship mesh in its own
// camera and light rig, drawn into a screen rectangle, with the mounted equipment shown on the
// hardpoints (FEquipVisualSet) and a "mountpoint" marker mesh per hardpoint.
//
// WINFACTORY<ShipViewControl> "ShipViewControl": ctor FUN_005870f0 (vftable 0x005e4d60), Create
// FUN_00587160 (operator_new(0x570), FUN_005836a0, slot 9). Built directly by
// WinControl::CreateShipViewControl (FUN_005a1020 -> Init FUN_005842a0).
//
// vftable 0x005e4bcc (45 slots) -- WinControl's with these overrides:
//    0 FUN_00583b70 scalar deleting dtor (FUN_00583c30)
//    4 LAB_00584bd0 HitTest           5 LAB_004b5020 (Win3D's SetPosition, not WinControl's)
//    6 LAB_005846b0 Slot6             9 FUN_00583e30 Create        11 LAB_00584740 Update
//   13 LAB_00584470 Draw             19 LAB_005846a0 (JMP FUN_005a0650, WinControl's MouseEvent)
//   42 LAB_005864c0 SetProperty
// UIShipViewControl (vftable 0x005e59a4) derives from this class and replaces slots 0 / 8 / 9
// (ui_ship_view_control.h).
//
// Layout (ctor FUN_005836a0, size 0x570):
//   +0x32c ID_String ship nickname          +0x330 FClientMeshHolder (the ship mesh)
//   +0x358 object orientation[9]            +0x37c object_ypr[3] (degrees)
//   +0x388 FSpaceCamera (0xc0)              +0x448 ambient RGBA bytes {0,0,0,0xff}
//   +0x44c FEngineLight*[8] (three used)    +0x46c light count (3)
//   +0x470 viewSize[3]                      +0x47c attach position[3] (hardpoint)
//   +0x488 offset[3]                        +0x494 screen rect (left, top, right, bottom)
//   +0x4a4 camera distance                  +0x4a8 bounds centre[3]
//   +0x4b4 vertical FOV (25.0)              +0x4b8 framed engine instance (-1)
//   +0x4bc wire_color, +0x4c0 rect_color, +0x4c4 {0xc3,0xb8,0x20,0xff} (RGBA bytes)
//   +0x4c8 EquipDescList (retail VC6 std::list<EquipDesc>, 0xc bytes)
//   +0x4d4 FEquipVisualSet*                 +0x4d8 accumulated rotation[3]
//   +0x4e4 rot_speed[3]                     +0x4f0 -1, +0x4f4 0, +0x4f8 0 (Reset)
//   +0x4fc selected hp class, +0x500 selected weapon, +0x504 selected shield generator
//   +0x508 selected instance (-1), +0x50c selected radius, +0x510 selected extent radius
//   +0x518 flags: bit2 show_rect, bit3 show_wireframe (also property 0x19), bit4 (FUN_00585bd0),
//          bit5 (FUN_00585cd0 / 00585d20 / 00585d90)
//   +0x51c mode (property 0x4c; ctor 2)     +0x520 drag ypr[3], +0x52c last drag angles[3]
//   +0x538 std::vector<FClientMeshHolder*>  mountpoint marker per hardpoint
//   +0x54c std::vector<int>                 hp types of one hardpoint (FUN_00586560)
//   +0x55c byte (ctor 0)                    +0x560 std::vector<FShipViewHpEntry> (0x20 each)
//
// The port's EquipDescList and std::vector members are Common's / the current CRT's, so the
// offsets from +0x4c8 on are retail's for reference only (no retail ABI, AGENTS.md).

#include "wincontrol_base.h"

#include "space_camera.h"

#include "common_goods.h"
#include "common_id.h"

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

struct FEngineLight;
struct FEquipVisualSet;

namespace Archetype
{
    struct Equipment;
}

// One row of the hardpoint list FUN_00586b70 builds (retail element 0x20 bytes; element dtor
// FUN_00455e90 / FUN_00587dc0 frees the VC6 string).
struct FShipViewHpEntry
{
    int Ids;          // +0x00 DAT_00615244[k] -- the hp-type caption IDS
    std::string Name; // +0x04 the hardpoint name (VC6 std::string, 0x10 bytes)
    int SortKey;      // +0x14 the DAT_00615240 row while sorting; 0 in the stored copy
    int Group;        // +0x18 FUN_00586800's grouping of the hp type (0 / 1 / 2)
    int Class;        // +0x1c FUN_00585b10's class of the hp type (0..9)
};

// FUN_005835f0 (cdecl) -- the hp class of an equipment archetype for the ship view: 0 when the
// archetype is unknown, 1 for an item without a hp type, 4 (and *OutWeapon) for hp types 4, 0xa
// and 0xd, otherwise the hp type itself -- and *OutShield when it is 5..8 on a ShieldGenerator.
int ShipViewEquipmentHpClass(unsigned ArchId, Archetype::Equipment const** OutWeapon,
                             Archetype::Equipment const** OutShield);
// FUN_00583690 -- the same for an EquipDesc (its ArchId at +4), tail-jumping into the above.
int ShipViewEquipDescHpClass(EquipDesc const* Desc, Archetype::Equipment const** OutWeapon,
                             Archetype::Equipment const** OutShield);
// FUN_00585b10 (cdecl) -- the hardpoint class shown for an hp type (switch table 0x00585b90).
int ShipViewHpTypeClass(int HpType);
// FUN_00584710 -- the row count of the hp-type caption table DAT_00615240 (0x3c).
int ShipViewHpTypeTableCount();
// FUN_00584720 -- DAT_00615240[Index].HpType, 0 outside 0..0x3b.
int ShipViewHpTypeTableType(int Index);

class ShipViewControl : public WinControl
{
public:
    // FUN_005836a0
    ShipViewControl();
    // FUN_00583c30
    ~ShipViewControl() override;

    // FUN_005842a0 (RET 0x24) -- the argument list of FUN_005a1020 after the name. RectColor /
    // WireColor point at RGBA byte quads.
    void Init(float const* NewRotSpeed, float const* NewOffset, float const* NewYpr,
              float const* NewViewSize, char const* Hardpoint, bool bShowRect, bool bShowWire,
              unsigned const* RectColor, unsigned const* WireColor);

    // --- Win3D / WinControl overrides ---
    bool HitTest(float const* Ray) override;                       // slot 4  LAB_00584bd0
    void SetPosition(float const* NewPosition) override;           // slot 5  LAB_004b5020
    bool Slot6(unsigned A) override;                               // slot 6  LAB_005846b0
    bool Create(Win3D* NewParent, void* Data) override;            // slot 9  FUN_00583e30
    void Update() override;                                        // slot 11 LAB_00584740
    void Draw() override;                                          // slot 13 LAB_00584470
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override; // slot 42 LAB_005864c0

    // FUN_00583b90 -- clear the selection and the drag state; frame nothing.
    void Reset();
    // FUN_00584c80 (RET 4) -- mouse drag rotation: the mouse ray projected into the rect gives
    // two angles; bStart records them, otherwise the difference turns +0x520.
    void DragRotate(bool bStart);
    // FUN_00585070 (RET 4) -- +0x520 = Ypr.
    void SetDragYpr(float const* Ypr);
    // FUN_005850f0 (RET 4) -- replace the shown equipment (null: none) and the marker meshes.
    void LoadEquipment(EquipDescList const* List);
    // FUN_00585380 (RET 8) -- frame Instance: bounds, camera distance, camera placement.
    void SetInstance(long NewInstance, bool bFixedDistance);
    // FUN_00585630 (RET 4) -- place the camera on the framed instance (mode 1 from the side,
    // mode 0 along its own z axis); other modes do nothing.
    void PlaceCamera(int PlaceMode);
    // FUN_00585890 (RET 0x10) -- load Path into the ship holder, frame it and centre it.
    // MaterialPath is handed to FUN_0059ec80 as its material-library path (may be null).
    bool LoadMesh(char const* Path, char const* MaterialPath, bool bFixedDistance,
                  bool bDpConnect);
    // FUN_00585a90 (RET 0xc) -- LoadEquipment(null), then the ship archetype's mesh.
    bool LoadShip(ID_String const* ShipId, bool bFixedDistance, bool bDpConnect);
    // FUN_00585bd0 (RET 0xc) -- select Instance (-1: no change); Unused is never read.
    void SelectInstance(long NewSelection, bool bFlag, int Unused);
    // FUN_00585cd0 (RET 0x10) -- select a hp class / weapon / shield directly.
    void SelectHpClass(int HpClass, Archetype::Equipment const* Weapon,
                       Archetype::Equipment const* Shield, bool bFlag);
    // FUN_00585d20 (RET 8) -- select by an EquipDesc's archetype.
    void SelectEquipDesc(EquipDesc const* Desc, bool bFlag);
    // FUN_00585d90 (RET 8) -- select by a good id.
    void SelectGood(unsigned GoodId, bool bFlag);
    // FUN_00585e20 (RET 4) -- the hardpoint name of the attached item instance, or null.
    char const* GetHardpointOfInstance(long ItemInstance) const;
    // FUN_00585e80 (RET 4) -- the item instance on the hardpoint named Name, or -1.
    long GetInstanceOnHardpoint(char const* Name) const;
    // FUN_00585ef0 (RET 8) -- FindHardpointForGood(Arch2Good(Desc->ArchId)), null for no Desc.
    char const* FindHardpointForEquipDesc(EquipDesc const* Desc, bool bFirstMatch) const;
    // FUN_00585f20 (RET 8) -- a hardpoint of the ship that can take GoodId.
    char const* FindHardpointForGood(unsigned GoodId, bool bFirstMatch) const;
    // FUN_00586310 (RET 8) -- the screen rect of the view box around Position.
    void ComputeScreenRect(float const* Position, bool bSetCamera);
    // FUN_00586510 (RET 4) -- is HpType among the +0x54c hp types.
    bool HasHpType(int HpType) const;
    // FUN_00586560 (RET 4) -- +0x54c = the hp types whose legal hardpoints include HpName.
    void CollectHpTypes(char const* HpName);
    // FUN_00586800 (RET 4) -- does any +0x54c hp type fall in Group.
    bool HasHpTypeGroup(int Group) const;
    // FUN_005868c0 / 005868e0 / 00586900 / 00586a10 (RET 4) -- the Init fields one at a time.
    void SetViewSize(float const* NewViewSize);
    void SetRotSpeed(float const* NewRotSpeed);
    void SetObjectYpr(float const* NewYpr);
    void SetAttachPosition(float const* NewPosition);
    // FUN_00586a30 / 00586a50 (RET 4) -- flag bits 2 / 3.
    void SetShowRect(bool bShow);
    void SetShowWireframe(bool bShow);
    // FUN_00586a70 / 00586af0 (RET 4) -- RGBA floats 0..1 into the byte colours.
    void SetRectColor(float const* Rgba);
    void SetWireColor(float const* Rgba);
    // FUN_00586b70 -- rebuild +0x560 from the hardpoints of the shown equipment.
    void BuildHardpointList();
    // FUN_00587050 -- +0x560's size.
    int GetHardpointListCount() const;
    // FUN_00587070 (RET 4) -- &+0x560[Index], null out of range.
    FShipViewHpEntry const* GetHardpointListEntry(int Index) const;

    ID_String ShipId;                          // +0x32c
    FClientMeshHolder ShipHolder;              // +0x330
    float ObjectOrientation[9];                // +0x358
    float ObjectYpr[3];                        // +0x37c
    FSpaceCamera Camera;                       // +0x388
    unsigned char Ambient[4];                  // +0x448
    FEngineLight* Lights[8];                   // +0x44c
    int LightCount;                            // +0x46c
    float ViewSize[3];                         // +0x470
    float AttachPos[3];                        // +0x47c
    float ViewOffset[3];                       // +0x488
    int ScreenRect[4];                         // +0x494
    float Distance;                            // +0x4a4
    float Center[3];                           // +0x4a8
    float FovY;                                // +0x4b4
    long Instance;                             // +0x4b8
    unsigned char WireColor[4];                // +0x4bc
    unsigned char RectColor[4];                // +0x4c0
    unsigned char Color4c4[4];                 // +0x4c4 -- no reader in this class
    EquipDescList Equipment;                   // +0x4c8
    FEquipVisualSet* Visuals;                  // +0x4d4
    float RotAccum[3];                         // +0x4d8
    float RotSpeed[3];                         // +0x4e4
    int Field4f0;                              // +0x4f0 -- Reset -1, no reader in this class
    int Field4f4;                              // +0x4f4 -- Reset 0
    int Field4f8;                              // +0x4f8 -- Reset 0
    int SelHpClass;                            // +0x4fc
    Archetype::Equipment const* SelWeapon;     // +0x500
    Archetype::Equipment const* SelShield;     // +0x504
    long SelInstance;                          // +0x508
    float SelRadius;                           // +0x50c
    float SelExtent;                           // +0x510
    int Field514;                              // +0x514 -- never written here
    unsigned char ViewFlags;                   // +0x518 (Win3D::Flags is +0x6c)
    unsigned char Pad519[3];                   // +0x519
    int Mode;                                  // +0x51c
    float DragYpr[3];                          // +0x520
    float DragLast[3];                         // +0x52c
    std::vector<FClientMeshHolder*> MountHolders; // +0x538
    int Field548;                              // +0x548 -- never written here
    std::vector<int> HpTypes;                  // +0x54c
    unsigned char Byte55c;                     // +0x55c -- ctor 0
    std::vector<FShipViewHpEntry> HpList;      // +0x560

private:
    ShipViewControl(ShipViewControl const&) = delete;
    ShipViewControl& operator=(ShipViewControl const&) = delete;
};

static_assert(offsetof(ShipViewControl, ShipId) == 0x32c, "ShipViewControl +0x32c");
static_assert(offsetof(ShipViewControl, ShipHolder) == 0x330, "ShipViewControl +0x330");
static_assert(offsetof(ShipViewControl, Camera) == 0x388, "ShipViewControl +0x388");
static_assert(offsetof(ShipViewControl, Lights) == 0x44c, "ShipViewControl +0x44c");
static_assert(offsetof(ShipViewControl, Instance) == 0x4b8, "ShipViewControl +0x4b8");
static_assert(offsetof(ShipViewControl, Equipment) == 0x4c8, "ShipViewControl +0x4c8");
