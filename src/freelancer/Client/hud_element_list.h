// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Client\space\HUD.cpp (FDUMP path at .rdata 0x005d6dcc) -- the HUD element registry, the
// HUDItem base class every HUD panel derives from, the HUDFACTORY<T> self-registering factories,
// and HUD_Steer (the mouse-flight steering arrow).
//
// Globals (.data):
//   DAT_00673510 / DAT_00673514 -- factory chain head / tail   (LAB_004c72a0 zeroes them)
//   DAT_00673518 / DAT_0067351c -- active HUDItem list head / tail (LAB_004c7260)
//   DAT_00673408 / DAT_0067340c -- the stored hud.ini path, length + char[0x104] (LAB_004c7360)
//   DAT_00673520 -- the HUD ShapeDB (G_HudShapeDB, texture_shapes.h)
//   DAT_00673570 -- the [HUD] static_txm texture reference (static ctor LAB_004cc950)
//   DAT_00612374 -- the [HUD] static_blend int (initial value 2)
//   DAT_00673548 -- HUD_Steer::Render's lazily acquired 3DMathEngine (FUN_005b73e0)

#include "client_mesh_load.h"
#include "common_goods.h"  // BaseWatcher
#include "common_tstring.h"

class INI_Reader;
struct IObjRW;
struct FSpaceCamera;
struct FTextureRef;

// -------------------------------------------------------------------------------------------
// HUDItem (RTTI .?AVHUDItem@@, vftable 0x005d6eac, 11 slots, 0x38 bytes).
//
// No out-of-line constructor exists: every derived constructor inlines it (e.g. 0x004c84d8 in
// HUD_Steer's factory, 0x004c8de3 in HUD_Button's). The inlined sequence is: +0x04 name length
// = 0, +0x08 name[0] = 0, the watcher at +0x28 built (BaseWatcher() + set(0)), the vptr, +0x34
// = 0, then an inlined SetTarget(0) (`if (watched) set_pointer(0)`), then +0x30 = 0.
// Destructor FUN_004c8630 (vptr back, watcher cleared); scalar deleting FUN_004c8650.
//
// Slots (retail byte offset; implementation in the HUDItem vftable):
//   0 +0x00 FUN_004c8650  scalar deleting dtor
//   1 +0x04 FUN_0044fd00  XOR AL,AL / RET -- HUD_Steer answers its +0x38 byte (LAB_004c8670)
//   2 +0x08 FUN_0053e4a0  RET 4 -- broadcast by FUN_004c78a0 from the HUD camera's slot 0x5c
//                          (0x0051cb9c), whose argument is the ViewRect it just applied
//   3 +0x0c FUN_004b6c50  MOV AL,1 / RET -- FUN_004c73d0 keeps the element only when true
//   4 +0x10 FUN_00401e90  RET -- FUN_004c77a0's first pass
//   5 +0x14 FUN_004c81a0  watch an IObjRW (broadcast by FUN_004c7330 from 0x0051cbea)
//   6 +0x18 FUN_0053aaa0  RET 8 -- FUN_004c8090 (camera, frame time) from 0x0051dae6
//   7 +0x1c FUN_0053f330  XOR AL,AL / RET 4 -- polled by FUN_004c7820 (camera slot 0x70)
//   8 +0x20 FUN_0053f330  -- polled by FUN_004c7860 (camera slot 0x74)
//   9 +0x24 FUN_0053f330
//  10 +0x28 FUN_00401e90  RET
// Slots 7 and 8 receive the command record FUN_0054bcb0 hands the active camera (a 0x28-byte
// block whose first dword is the command id -- HUD_Button::MouseEvent 0x004c9430 builds one).
// -------------------------------------------------------------------------------------------

class HUDItem
{
public:
    HUDItem();
    // FUN_004c8630
    virtual ~HUDItem();

    // slot 1 (+0x04) FUN_0044fd00
    virtual bool Slot1();
    // slot 2 (+0x08) FUN_0053e4a0
    virtual void SetViewport(int const* ViewRect4);
    // slot 3 (+0x0c) FUN_004b6c50
    virtual bool Init();
    // slot 4 (+0x10) FUN_00401e90
    virtual void Shutdown();
    // slot 5 (+0x14) FUN_004c81a0 -- watch Target's Watchable (IObjRW + 8), or nothing
    virtual void SetTarget(IObjRW* Target);
    // slot 6 (+0x18) FUN_0053aaa0
    virtual void Render(FSpaceCamera* Camera, float DeltaSeconds);
    // slots 7 / 8 / 9 (+0x1c / +0x20 / +0x24) FUN_0053f330
    virtual bool Slot7(void* Command);
    virtual bool Slot8(void* Command);
    virtual bool Slot9(void* Command);
    // slot 10 (+0x28) FUN_00401e90
    virtual void Slot10();

    // FUN_004c81d0 (thiscall) -- with a tool-tip id at +0x30, feed the ToolTip window
    // (FUN_004be370) with a rich-text copy of that string. HUD_Button::Update 0x004c941c calls it
    // while the button is hovered.
    void ShowToolTip();

    TString<32> Name;          // +0x04
    BaseWatcher Target;        // +0x28
    int ToolTipIds;            // +0x30 ("tool_tip_id" in the HUD_Button ini)
    HUDItem* Next;             // +0x34 active-list link

private:
    HUDItem(HUDItem const&) = delete;
    HUDItem& operator=(HUDItem const&) = delete;
};

static_assert(sizeof(HUDItem) == 0x38, "retail HUDItem is 0x38 bytes");

// Transitional name kept while the HUD-A lane (hud_targetable_objects / hud_target) moves over.
using IHudElement = HUDItem;

// -------------------------------------------------------------------------------------------
// HUDItem_Factory (RTTI .?AVHUDItem_Factory@@, vftable 0x005d6e30: two slots, both
// FUN_0053e180 `XOR EAX,EAX / RET 4`) and HUDFACTORY<T>.
//
// Layout: +0x00 vptr, +0x04 next in the factory chain, +0x08 TString<32> section name. The
// constructor is inlined into every HUDFACTORY<T> constructor (0x004cd270, 0x004d9990, ...):
// base vptr, +0x08 = 0, +0x0c = 0, +0x04 = 0, the name copied (<= 31 chars), FUN_004c72b0 to
// append to the chain, then the derived vptr. The destructor is not virtual: the atexit thunks
// (LAB_004c8310, LAB_004c8340) call LAB_004c8350 directly, which puts the base vptr back and
// unlinks the factory (same body as FUN_004c72e0, inlined).
// -------------------------------------------------------------------------------------------

class HUDItem_Factory
{
public:
    explicit HUDItem_Factory(char const* SectionName);
    // LAB_004c8350
    ~HUDItem_Factory();

    // slot 0 (+0x00) -- build the element for the [SectionName] section the reader sits on.
    // FUN_004c73d0 passes its INI_Reader by address.
    virtual HUDItem* Create(INI_Reader& Reader);
    // slot 1 (+0x04) FUN_0053e180 -- never overridden, never called through a traced site
    virtual HUDItem* Slot1(void* Arg);

    HUDItem_Factory* Next;     // +0x04
    TString<32> SectionName;   // +0x08

private:
    HUDItem_Factory(HUDItem_Factory const&) = delete;
    HUDItem_Factory& operator=(HUDItem_Factory const&) = delete;
};

// Retail HUDFACTORY<T>::Create (e.g. FUN_004d9a00 for HUD_Maneuvers, FUN_004cd2e0 for
// HUD_Receiver): operator new(sizeof(T)), T(Reader), and the HUDItem face of the result -- which
// is not the object's address when HUDItem is not T's first base (HUD_Receiver: +0x7c).
template <class T>
class HUDFACTORY : public HUDItem_Factory
{
public:
    explicit HUDFACTORY(char const* SectionName) : HUDItem_Factory(SectionName) {}

    HUDItem* Create(INI_Reader& Reader) override
    {
        T* const Element = new T(Reader);
        return Element;
    }
};

// Transitional name kept while the HUD-A lane moves over (see IHudElement above).
using HudElementFactoryBase = HUDItem_Factory;

// -------------------------------------------------------------------------------------------
// HUD_Steer (RTTI .?AUHUD_Steer@@ : HUDItem, vftable 0x005d6e7c, 0x74 bytes).
// Built by UHUD_Steer::HUDFACTORY::Create FUN_004c84a0 (the constructor is inlined there),
// destroyed by FUN_004c8c20 (scalar deleting FUN_004c8c00). Its factory is the static object
// DAT_006733dc (ctor FUN_004c8400 from the XC entry LAB_004c82f0, "Steer").
// Overrides: 0 FUN_004c8c00, 1 LAB_004c8670 (the +0x38 byte), 6 LAB_004c8680 Render.
// -------------------------------------------------------------------------------------------

struct HUD_Steer : public HUDItem
{
    // FUN_004c84a0's inlined constructor
    explicit HUD_Steer(INI_Reader& Reader);
    // FUN_004c8c20
    ~HUD_Steer() override;

    // LAB_004c8670 -- MOV AL,[ECX+0x38]
    bool Slot1() override;
    // LAB_004c8680
    void Render(FSpaceCamera* Camera, float DeltaSeconds) override;

    unsigned char Byte38;          // +0x38 -- the ctor clears it; nothing in the PE sets it
    unsigned char Pad39[3];
    float Radius;                  // +0x3c "radius", 40.0f
    float Range;                   // +0x40 "range", 200.0f
    float Field44;                 // +0x44 0.2f, never read
    unsigned char Byte48;          // +0x48 cleared, never read
    unsigned char Pad49[3];
    FClientMeshHolder MeshHolder;  // +0x4c interface\hud\HUD_steeringarrow.3db
};

static_assert(sizeof(HUD_Steer) == 0x74, "retail HUD_Steer is 0x74 bytes");

// FUN_004c8400 -- UHUD_Steer::HUDFACTORY: the factory whose Create builds HUD_Steer. Retail
// keeps a separate out-of-line copy of the base constructor for it (with the chain append
// inlined) instead of the shared inlined form; the effect is the same.
class HUD_SteerFactory : public HUDItem_Factory
{
public:
    explicit HUD_SteerFactory(char const* SectionName) : HUDItem_Factory(SectionName) {}
    // FUN_004c84a0
    HUDItem* Create(INI_Reader& Reader) override;
};

// -------------------------------------------------------------------------------------------
// Registry functions.
// -------------------------------------------------------------------------------------------

// LAB_004c72a0 -- zero the factory-chain head/tail.
void HudElementFactoryListInit();
// FUN_004c72b0 -- append to the factory chain's tail.
void HudElementFactoryListAdd(HUDItem_Factory* Factory);
// FUN_004c72e0 -- unlink a factory from the chain (no-op if not present).
void HudElementFactoryListRemove(HUDItem_Factory* Factory);

// LAB_004c7260 -- zero the active-element list head/tail.
void HudElementListInit();
// FUN_004c7330 (cdecl) -- HUDItem::SetTarget(Target) on every active element.
void HudElementListSetTarget(IObjRW* Target);
// FUN_004c78a0 (cdecl) -- HUDItem::SetViewport(ViewRect4) on every active element.
void HudElementListSetViewport(int const* ViewRect4);
// FUN_004c8090 (cdecl) -- IMaterialBatcher slot 8 (bin 1, -1), HUDItem::Render(Camera,
// DeltaSeconds) on every active element, slot 8 (1, 1). Its one caller, 0x0051dae6 in
// FUN_0051daa0 (the HUD camera's draw), pushes the frame time (FUN_0042d680) then the camera.
void HudElementListRender(FSpaceCamera* Camera, float DeltaSeconds);
// FUN_004c7820 (cdecl) -- true at the first active element whose Slot7(Command) is true.
bool HudElementListAnySlot7(void* Command);
// FUN_004c7860 (cdecl) -- the same over Slot8.
bool HudElementListAnySlot8(void* Command);
// FUN_004c77a0 -- release the static_txm texture (DAT_00673570, TextureLib slot +0x38),
// Shutdown() on every element, delete every element, empty the list, then tail-jump into
// FUN_00425a20 on the HUD ShapeDB. Caller: the space teardown 0x0054af1d.
void HudElementListShutdown();

// FUN_004c7370 -- ProgressCB's `HUD = ...` key stores the resolved path (cap 0x103).
void SetHudIniPath(char const* Path);
// The path last stored by SetHudIniPath (DAT_0067340c).
char const* GetHudIniPath();

// FUN_004c73d0 -- open the stored hud.ini; [HUD] loads shapes / static_txm / static_blend,
// every other section is offered to each factory in chain order until one builds an element;
// an element whose Init() is false is deleted, a section nobody builds is FDUMPed. Then the
// mouse-flight state is reset (0x004c75c5 .. 0x004c75ee). Caller: the space init 0x0054ad62.
void LoadHudElementsFromIni();

// DAT_00673570 / DAT_00612374 -- read by the comm window's static-noise draw (FUN_004cc9a0).
FTextureRef* GetHudStaticTexture();
int GetHudStaticBlend();

// FUN_004c8b10 (cdecl Out, In, Length) -- Out = In * (1 / Length); callers besides HUD_Steer at
// 0x005809a0, 0x005809cc, 0x00580c7b.
void HudVec3ScaleByInverse(float* Out, float const* In, float Length);

// The three rotation helpers on a row-major 3x3 matrix (retail thiscall on the matrix, RET 4);
// callers besides HUD_Steer: 0x00583fa7 .. 0x005848xx, 0x0059b196, 0x004f4249.
// FUN_004c8b40 -- rotation about Y: [ cos 0 sin ; 0 1 0 ; -sin 0 cos ].
void HudMatrixRotationY(float* M, float Angle);
// FUN_004c8b80 -- rows 1 and 2 turned column by column:
//   row1 = row1 * cos - sin * row2;  row2 = row1 * sin + cos * row2.
void HudMatrixRotateRows12(float* M, float Angle);
// FUN_004c8bc0 -- rows 0 and 1 turned the same way.
void HudMatrixRotateRows01(float* M, float Angle);

// FUN_0053eb10 (cdecl X, Y) -- the angle of (X, Y) measured from the +Y axis: 0 for (0, 0);
// with |X| > |Y| it is atan(Y / |X|) -/+ pi/2, otherwise atan(-X / |Y|), shifted by +/- pi
// when Y < 0 (sign chosen by the sign of that arctangent).
float HudVectorAngle(float X, float Y);
