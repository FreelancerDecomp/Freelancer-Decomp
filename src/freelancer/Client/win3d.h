// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Win3D (`E:\FL\Scratch\Source\Client\Interface\Win3D.cpp`, RTTI .?AUWin3D@@) -- the
// root of every client GUI object: WinControl, TextWin, WinObject, ListBox, UIPage::MasterWin,
// TypeSelect, UITable, UIWinDeform, HUD_Receiver.
//
// Layout (ctor FUN_004b6790, inlined into every derived ctor, e.g. 0059f87f..0059f8c4):
//   +0x00 vftable 0x005d5544 (28 slots)
//   +0x04 LinkTree<Win3D>: parent +0x04, next sibling +0x08, first child +0x0c
//   +0x10 class name -- &WINFACTORY<T>::ClassName of the factory that built it (005a1a7d)
//   +0x14 name length, +0x18 char name[32] (FUN_0059db20 copies <= 31 chars)
//   +0x38 next link of the mouse-capture stack (DAT_0067dce0, FUN_0059d8d0 / FUN_0059d960)
//   +0x3c 3x3 orientation (identity), +0x60 position[3]
//   +0x6c flags byte: bit0 active (Update vs UpdateInactive), bit1 visible, bit2 unregistered
//         latch (FUN_0059dc00), bit3 pickable (FUN_0059dd80 / FUN_0059de50)
//   +0x70 draw layer (0 = draw at once; FUN_0059e300 / FUN_0059e5c0 sort key)
//   +0x74 hit priority (slot 27), +0x78 draw sub-layer 0..7 (FUN_0059e300)
//   size 0x7c
//
// DAT_0067dcd4 counts live Win3D objects (++ in the ctor, -- in the dtor FUN_004b4fd0).

#include <stddef.h>

struct Win3D;
class WinControl;
struct FSpaceCamera;

// Retail LinkTree<Win3D> (RTTI .?AU?$LinkTree@UWin3D@@@@), non-polymorphic, at Win3D+4.
template <class T>
struct LinkTree
{
    T* Parent;      // +0x04 of the Win3D
    T* NextSibling; // +0x08
    T* ChildHead;   // +0x0c
};

// The two dwords slot 19 receives (FUN_0059d140 cases 6..13 build it on the stack):
//   Field0 -- -1 - (event - 6) for mouse button events 6..13, else 0
//   Field1 -- 1, or 2 / 3 when the input record's +0x18 byte is set (3 when +0x1a is set too)
// UITextMsgButton LAB_0059a3d0 treats Field0 == -1 && Field1 == 2 as a click.
struct FWin3DMouseEvent
{
    int Field0;
    int Field1;
};

struct Win3D : public LinkTree<Win3D>
{
    // FUN_004b6790
    Win3D();
    // slot 0 -- FUN_004b50f0 scalar deleting dtor around FUN_004b4fd0
    virtual ~Win3D();

    // slot 1 (+0x04) LAB_0043d8a0 -- XOR AL,AL / RET 8
    virtual bool Slot1(unsigned A, unsigned B);
    // slot 2 (+0x08) FUN_004b5010 -- returns 0; WinControl FUN_0043d950 returns this
    virtual WinControl* AsWinControl();
    // slot 3 (+0x0c) LAB_0043d8b0 -- parent == Ancestor, else ask the parent (tail jump)
    virtual bool IsDescendantOf(Win3D* Ancestor);
    // slot 4 (+0x10) FUN_0053f330 -- ray pick; false
    virtual bool HitTest(float const* Ray);
    // slot 5 (+0x14) LAB_004b5020 -- copy three dwords to +0x60
    virtual void SetPosition(float const* NewPosition);
    // slot 6 (+0x18) LAB_0043d8e0 -- MOV AL,1 / RET 4
    virtual bool Slot6(unsigned A);
    // slot 7 (+0x1c) FUN_004b5040 -- copy nine dwords to +0x3c
    virtual void SetOrientation(float const* NewOrientation);
    // slot 8 (+0x20) LAB_00595f70 -- slot 7 (Transform), then slot 5 (Transform + 9)
    virtual void SetTransform(float const* Transform);
    // slot 9 (+0x24) FUN_004b5060 -- reparent, register, flags = (flags & 0xfb) | 3, true
    virtual bool Create(Win3D* NewParent, void* Data);
    // slot 10 (+0x28) FUN_004b5090 -- flags &= 0xfc, children slot 10, FUN_0059dc00(this)
    virtual void Destroy();
    // slot 11 (+0x2c) FUN_005a17b0 -- per child: bit0 ? slot 11 : slot 12
    virtual void Update();
    // slot 12 (+0x30) LAB_004682f0 -- slot 12 on every child whose bit0 is clear
    virtual void UpdateInactive();
    // slot 13 (+0x34) FUN_0059e5c0 -- draw the visible children, sorted
    virtual void Draw();
    // slots 14 / 15 / 16 (+0x38 / +0x3c / +0x40) FUN_0053e4a0 -- RET 4. FUN_0059df80 calls
    // 16 on the old hover object, 15 then 14 on the new one, each with DAT_0067dbcc.
    virtual void MouseOver(float const* Ray);
    virtual void MouseEnter(float const* Ray);
    virtual void MouseLeave(float const* Ray);
    // slot 17 (+0x44) FUN_00401e90 -- RET
    virtual void Slot17();
    // slot 18 (+0x48) FUN_0053f330 -- false. FUN_0059d140 case 4 passes DAT_0061684c.
    virtual bool Slot18(int Value);
    // slot 19 (+0x4c) LAB_004b50c0 -- bubble to the parent's slot 19, false at the root
    virtual bool MouseEvent(FWin3DMouseEvent const* Event);
    // slot 20 (+0x50) LAB_0043d8a0 -- false. FUN_0059dfd0 passes (key, &modifier byte).
    virtual bool KeyEvent(unsigned Key, unsigned char* Modifiers);
    // slot 21 (+0x54) LAB_0043d8f0 -- XOR AL,AL / RET 0x10
    virtual bool Slot21(unsigned A, unsigned B, unsigned C, unsigned D);
    // slot 22 (+0x58) LAB_0043d8a0 -- false. FUN_0059d140 event 2 passes record +0x10/+0x14.
    virtual bool Slot22(unsigned A, unsigned B);
    // slot 23 (+0x5c) LAB_004b50e0 -- XOR AL,AL / RET 0xc. Broadcast by FUN_0059df40.
    virtual bool Slot23(unsigned Msg, unsigned A, unsigned B);
    // slot 24 (+0x60) LAB_0043d900 -- slot 24 on every child
    virtual void Slot24(unsigned A);
    // slots 25 / 26 (+0x64 / +0x68) LAB_0043d930 -- FLD 1.0f (DAT_005c75dc)
    virtual float Slot25();
    virtual float Slot26();
    // slot 27 (+0x6c) FUN_0043d940 -- MOV EAX,[ECX+0x74]
    virtual int GetHitPriority();

    // FUN_0059db20's inline name copy (0059dba5..0059dbe1): <= 31 chars + length, or empty.
    void SetName(char const* NewName);
    char const* GetName() const { return Name; }
    Win3D* GetParent() const { return Parent; }
    Win3D* GetChildHead() const { return ChildHead; }
    Win3D* GetNextSibling() const { return NextSibling; }

    char const* ClassName;      // +0x10
    unsigned NameLength;        // +0x14
    char Name[32];              // +0x18
    Win3D* CaptureNext;         // +0x38
    float Orientation[9];       // +0x3c
    float Position[3];          // +0x60
    unsigned char Flags;        // +0x6c
    unsigned char Pad6d[3];     // +0x6d (never written by the ctor)
    unsigned DrawLayer;         // +0x70
    int HitPriority;            // +0x74
    int DrawSubLayer;           // +0x78

private:
    Win3D(Win3D const&) = delete;
    Win3D& operator=(Win3D const&) = delete;
};

static_assert(sizeof(Win3D) == 0x7c, "retail Win3D is 0x7c bytes");
static_assert(offsetof(Win3D, Parent) == 0x04, "Win3D +0x04 parent");
static_assert(offsetof(Win3D, NextSibling) == 0x08, "Win3D +0x08 next sibling");
static_assert(offsetof(Win3D, ChildHead) == 0x0c, "Win3D +0x0c first child");
static_assert(offsetof(Win3D, ClassName) == 0x10, "Win3D +0x10 class name");
static_assert(offsetof(Win3D, Name) == 0x18, "Win3D +0x18 name");
static_assert(offsetof(Win3D, CaptureNext) == 0x38, "Win3D +0x38 capture link");
static_assert(offsetof(Win3D, Orientation) == 0x3c, "Win3D +0x3c orientation");
static_assert(offsetof(Win3D, Position) == 0x60, "Win3D +0x60 position");
static_assert(offsetof(Win3D, Flags) == 0x6c, "Win3D +0x6c flags");
static_assert(offsetof(Win3D, DrawLayer) == 0x70, "Win3D +0x70 draw layer");
static_assert(offsetof(Win3D, HitPriority) == 0x74, "Win3D +0x74 hit priority");
static_assert(offsetof(Win3D, DrawSubLayer) == 0x78, "Win3D +0x78 sub-layer");

// Flag bits of Win3D::Flags (+0x6c).
enum EWin3DFlags : unsigned char
{
    kWin3DActive = 0x01,
    kWin3DVisible = 0x02,
    kWin3DUnregistered = 0x04,
    kWin3DPickable = 0x08
};

// ---------------------------------------------------------------------------------------
// Win3D manager (the rest of Win3D.cpp, 0x0059d5a0..0x0059e8d0).
// ---------------------------------------------------------------------------------------

// DAT_0067dcd4 -- live Win3D count.
extern int G_Win3DLiveCount;

// FUN_0059d8c0 -- DAT_0067dcdc = Focus (keyboard focus; FUN_0059dfd0 sends keys to it first).
void Win3DSetKeyFocus(Win3D* Focus);
// FUN_0059d8d0 -- put Object at the head of the capture stack (moving it there if present).
void Win3DPushCapture(Win3D* Object);
// FUN_0059d960 -- unlink Object from the capture stack.
void Win3DRemoveCapture(Win3D* Object);
// FUN_0059d9b0 -- head of the capture stack (DAT_0067dce0).
Win3D* Win3DGetCapture();
// FUN_0059d9c0 -- first registered object whose +0x10 class name stricmp-equals ClassName.
Win3D* Win3DFindByClassName(char const* ClassName);
// FUN_0059da10 -- first registered object whose +0x18 name stricmp-equals Name.
Win3D* Win3DFindByName(char const* Name);
// FUN_0059da50 -- first registered object, or 0.
Win3D* Win3DGetFirstRegistered();
// FUN_0059da70 -- copy the cached mouse ray DAT_0067dbcc/d0/d4 into Out; returns Out.
float* Win3DGetMouseRay(float* Out);
// DAT_0067dbcc -- the ray itself (FUN_0059df80 / FUN_0059e080 push its address).
float* Win3DMouseRayStorage();
// FUN_0059da60 -- &DAT_0067dbf8, the Win3D scene camera.
FSpaceCamera* Win3DGetCamera();
// FUN_0059dcc0 -- that camera's viewport = {0, 0, Width - 1, Height - 1} (FUN_0040fb80).
void Win3DSetViewportSize(int Width, int Height);
// FUN_0059daa0 -- the object under the mouse (DAT_0067dcd8).
Win3D* Win3DGetHover();
// FUN_0059dbf0 -- append Object to the registry (DAT_0067dbe4, FUN_0059e810).
void Win3DRegister(Win3D* Object);
// FUN_0059dc00 -- latch bit2, drop focus/capture/hover references, unlink from the parent and
// move the registry node to the pending-remove list. False when it was not registered.
bool Win3DUnregister(Win3D* Object);
// FUN_0059dd00 -- unlink Child from its parent and append it as NewParent's last child.
void Win3DReparent(Win3D* Child, Win3D* NewParent);
// FUN_0059dfd0 -- key to the focus object (slot 20), else to every registered object until one
// returns true. Record is the event record DAT_0067c278 (hud_command_dispatch.h).
struct FHudCmdRecord;
bool Win3DDispatchKey(FHudCmdRecord const* Record);
// FUN_0059dd80 -- the topmost pickable root-visible registered object Ray hits.
Win3D* Win3DPick(float const* Ray);
// FUN_0059de50 -- the same pick restricted to Capture and its descendants.
Win3D* Win3DPickWithin(Win3D* Capture, float const* Ray);
// FUN_0059df40 -- slot 23 on every registered object until one returns true.
bool Win3DBroadcast(unsigned Msg, unsigned A, unsigned B);
// FUN_0059df80 -- change the hover object (slot 16 on the old one, 15 and 14 on the new one).
void Win3DSetHover(Win3D* Object);
// FUN_0059e080 -- per-frame update of every registered root (see win3d.cpp).
void Win3DUpdateAll(double DeltaSeconds);
// FUN_0059e300 -- draw every registered visible root, sorted by layer / depth / sub-layer.
void Win3DDrawAll();
// FUN_0059e500 -- the render-state preamble of FUN_0059e300 on its own.
void Win3DBeginDraw();
// FUN_0059e5a0 -- MaterialBatcher slot 8 (1, 1), the epilogue of FUN_0059e300.
void Win3DEndDraw();
// FUN_0059e5c0 (thiscall on Parent) -- draw Parent's visible children sorted by +0x70/+0x68.
void Win3DDrawChildren(Win3D* Parent);
// FUN_0059e6e0 (cdecl) -- the same body for callers that pass the parent on the stack.
void Win3DDrawChildrenOf(Win3D* Parent);
// FUN_0043d2a0 (cdecl) -- post-order delete of Root's subtree: every child subtree, then slot 10
// Destroy and the scalar deleting dtor on Root. Null does nothing.
void Win3DDeleteTree(Win3D* Root);
// DAT_0067dccc -- raised by the modal NN dialogs' ctors, lowered by their dtors; while it is
// non-zero FUN_0059e080 updates only the WinControl holding the mouse.
extern int G_ModalCaptureCount;
// FUN_0059e6a0 -- true while any registered WinControl holds the mouse (+0x2d0).
bool Win3DAnyCaptured();
// FUN_0059d5a0 -- the Win3D camera's viewport / near-far / field of view, the cursors, both
// line batchers, and interface\interface.generic.vms through the material cache
// (FUN_0041ade0 -> DAT_0067dbc8). Caller: InitializeEngine 0x005b1d53.
void Win3DStartupManager(int Width, int Height);
// FUN_0059d6d0 -- drop focus / capture, unregister everything, free both lists, give back the
// interface material, release the cursors and the line batchers.
void Win3DShutdownManager();

struct FVector;
struct FMatrix;
// FUN_00423b90 (cdecl Out, Matrix, Source) -- I3DMathEngine::TransformMatrix (+0x2c) through
// the engine it caches in DAT_006679d8, into a local that is then copied to Out.
void Win3DTransformByMatrix(FVector& Out, FMatrix const& Matrix, FVector const& Source);
// DAT_006679d8 itself, acquired (FUN_005b73e0) when still null -- the same test-and-fill every
// OpenGL.cpp site repeats inline before calling the engine directly (the fuzzy arc FUN_004231f0
// at 0x004234ff and 0x00423a06 / 0x00423a40).
struct I3DMathEngine;
I3DMathEngine* Win3DTransformMathEngine();
// FUN_004bdd40 (thiscall on the camera) -- the camera-space point on the near plane under a
// screen pixel: ((x - cx) * (znear / xscale), (y - cy) * (znear / yscale), -znear).
void Win3DScreenToNearPlane(FSpaceCamera const* Cam, FVector& Out, float ScreenX, float ScreenY);
