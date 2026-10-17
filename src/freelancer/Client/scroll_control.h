// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail ScrollControl (RTTI .?AUScrollControl@@ : WinControl) -- the NeuroNet mesh scroll bar:
// a back plate, a "next" and a "prev" button and a thumb that slides between two hardpoints,
// each a mesh of its own, over the integer range [MinRange, MaxRange].
//
// WINFACTORY<ScrollControl> DAT_0067c488 "ScrollControl": static init LAB_0057fd20 (factory ctor
// FUN_00583480 + atexit LAB_0057fd40 -> FUN_00579380), Create FUN_005834f0 (operator_new(0x67c),
// ctor FUN_0057fd50, slot 9). Built directly by WinControl::CreateScrollControl (FUN_005a0ef0 ->
// Init FUN_00582120); the trade lists (0x004bf4ec, 0x004c1093) use that.
//
// vftable 0x005e4984 (45 slots) -- WinControl's with these overrides:
//    0 FUN_00580170 scalar deleting dtor (FUN_00580190)
//    4 LAB_00583130 HitTest            5 LAB_00582aa0 SetPosition     7 FUN_00582ac0 SetOrientation
//    9 LAB_005815a0 Create            11 FUN_00582ae0 Update          13 LAB_005826e0 Draw
//   14 LAB_00583120 MouseOver (-> slot 15)                            15 LAB_005830a0 MouseEnter
//   16 LAB_00583120 MouseLeave (-> slot 15)                           19 LAB_00582c50 MouseEvent
//   35 FUN_00401e90 RefreshAttachment (RET: does nothing)
//   42 FUN_005831b0 SetProperty       43 LAB_00583390 AddAttachOffset
// The IDataViewOwner face (+0x7c, vftable 0x005e4978) keeps WinControl's two entries.
//
// Each of the three buttons is one FScrollControlPart (0xa4 bytes); the thumb has two
// hardpoint transforms (its ends of travel) and a current transform interpolated between them
// by the value (FUN_00580290).
//
// The value is reported to the parent WinControl as SetProperty(0x3e, this, Value) whenever the
// user changes it (FUN_00581560 and its inlined copies).

#include "wincontrol_base.h"

#include "client_mesh_load.h"
#include "x86math.h"

#include <stddef.h>
#include <stdint.h>

// One button of the bar (back plate +0x32c, next +0x3d0, prev +0x474).
struct FScrollControlPart
{
    FClientMeshHolder Holder;    // +0x00 "<part>_mesh"
    FVector Offset;              // +0x28 "<part>_offset"
    FTransform Xform;            // +0x34 the instance transform: Hp moved by Offset + position
    FVector MouseSize;           // +0x64 "<part>_mouse" -- the half extents the ray is tested on
    unsigned char bNoMeshRender; // +0x70 "<part>_no_mesh_render" -- skip the untinted pass
    FTransform Hp;               // +0x74 "<part>_hp" -- the parent's hardpoint (identity if none)
};

static_assert(sizeof(FScrollControlPart) == 0xa4, "ScrollControl part stride 0xa4");
static_assert(offsetof(FScrollControlPart, Xform) == 0x34, "part +0x34 transform");
static_assert(offsetof(FScrollControlPart, Hp) == 0x74, "part +0x74 hardpoint transform");

class ScrollControl : public WinControl
{
public:
    // FUN_0057fd50
    ScrollControl();
    // FUN_00580190
    ~ScrollControl() override;

    // FUN_00582120 (RET 0x6c) -- the argument list of FUN_005a0ef0 after the name. A null mesh
    // leaves that holder alone; a null hardpoint name leaves that transform alone. A null
    // PrevMesh skips the whole prev block (offset, hardpoint, mouse, no-mesh-render) -- the
    // retail branch at 0x00582390 jumps past all of it.
    void Init(char const* BackMesh, float const* BackOffset, char const* BackHp,
              float const* BackMouse, bool bBackNoMeshRender, char const* NextMesh,
              float const* NextOffset, char const* NextHp, float const* NextMouse,
              bool bNextNoMeshRender, char const* PrevMesh, float const* PrevOffset,
              char const* PrevHp, float const* PrevMouse, bool bPrevNoMeshRender,
              char const* ThumbMesh, float const* NewThumbOffset0, float const* NewThumbOffset1,
              char const* NewThumbHp0, char const* NewThumbHp1, float const* NewThumbMouse,
              bool bNewThumbNoMeshRender, int NewMinRange, int NewMaxRange, int NewPageSize,
              bool bNewSliderBehavior, bool bNewNoZEnable);

    // --- Win3D / WinControl overrides ---
    bool HitTest(float const* Ray) override;                          // slot 4  LAB_00583130
    void SetPosition(float const* NewPosition) override;              // slot 5  LAB_00582aa0
    void SetOrientation(float const* NewOrientation) override;        // slot 7  FUN_00582ac0
    bool Create(Win3D* NewParent, void* Data) override;               // slot 9  LAB_005815a0
    void Update() override;                                           // slot 11 FUN_00582ae0
    void Draw() override;                                             // slot 13 LAB_005826e0
    void MouseOver(float const* Ray) override;                        // slot 14 LAB_00583120
    void MouseEnter(float const* Ray) override;                       // slot 15 LAB_005830a0
    void MouseLeave(float const* Ray) override;                       // slot 16 LAB_00583120
    bool MouseEvent(FWin3DMouseEvent const* Event) override;          // slot 19 LAB_00582c50
    void RefreshAttachment() override;                                // slot 35 FUN_00401e90
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override; // slot 42 FUN_005831b0
    void AddAttachOffset(float const* Delta) override;                // slot 43 LAB_00583390

    // FUN_00580290 -- the thumb transform for the current value (valid only for
    // Min < Max and Min <= Value <= Max; otherwise bThumbValid is cleared and nothing moves).
    void UpdateThumb();
    // FUN_005803d0 -- rebuild every part transform from its hardpoint, offset and the control's
    // position, push them to the engine, then UpdateThumb.
    void UpdateLayout();
    // FUN_00580750 (RET 8) -- which way a click on the back plate pages: bPageTowardMin.
    void ChoosePageDirection(unsigned ScreenX, unsigned ScreenY);
    // FUN_00580d00 -- one page towards the cursor, while the thumb has not reached it.
    void PageStep();
    // FUN_005811b0 (RET 8) -- the value under the cursor projected on the thumb's track.
    void DragThumb(unsigned ScreenX, unsigned ScreenY);
    // FUN_00581560 -- parent->SetProperty(0x3e, this, Value).
    void NotifyValue();

    FScrollControlPart Back;         // +0x32c "back_*"
    FScrollControlPart Next;         // +0x3d0 "nextb_*"
    FScrollControlPart Prev;         // +0x474 "prevb_*"
    FClientMeshHolder ThumbHolder;   // +0x518 "thumb_mesh"
    FTransform ThumbStart;           // +0x540 ThumbHp0 moved by ThumbOffset0 + position
    FTransform ThumbEnd;             // +0x570 ThumbHp1 moved by ThumbOffset1 + position
    FVector ThumbOffset0;            // +0x5a0 "thumb_offset0"
    FVector ThumbOffset1;            // +0x5ac "thumb_offset1"
    FTransform ThumbXform;           // +0x5b8 the thumb now (FUN_00580290)
    FVector ThumbMouse;              // +0x5e8 "thumb_mouse"
    unsigned char bThumbNoMeshRender; // +0x5f4 "thumb_no_mesh_render"
    FTransform ThumbHp0;             // +0x5f8 "thumb_hp0"
    FTransform ThumbHp1;             // +0x628 "thumb_hp1"
    int MinRange;                    // +0x658 "min_range"
    int MaxRange;                    // +0x65c "max_range"
    int PageSize;                    // +0x660 "page_size" (ctor 1)
    int Value;                       // +0x664
    int Step;                        // +0x668 the button step (ctor 1; property 0x3c)
    unsigned char bSliderBehavior;   // +0x66c "slider_behavior": a back click drags the thumb
    unsigned char bThumbValid;       // +0x66d set by FUN_00580290
    float RepeatTime;                // +0x670 the last auto-repeat (FUN_0042d660); not set by the ctor
    unsigned char bDragging;         // +0x674 the thumb follows the cursor
    unsigned char bPaging;           // +0x675 the back plate is held: page every 0.3 s
    unsigned char bNextPressed;      // +0x676 the next button is held: step every 0.3 s
    unsigned char bPrevPressed;      // +0x677 the prev button is held
    unsigned char bThumbHover;       // +0x678
    unsigned char bNextHover;        // +0x679
    unsigned char bPrevHover;        // +0x67a
    unsigned char bPageTowardMin;    // +0x67b FUN_00580750; not set by the ctor

private:
    ScrollControl(ScrollControl const&) = delete;
    ScrollControl& operator=(ScrollControl const&) = delete;
};

static_assert(sizeof(ScrollControl) == 0x67c, "retail ScrollControl is 0x67c bytes (0x00583508)");
static_assert(offsetof(ScrollControl, Back) == 0x32c, "ScrollControl +0x32c back");
static_assert(offsetof(ScrollControl, Next) == 0x3d0, "ScrollControl +0x3d0 next");
static_assert(offsetof(ScrollControl, Prev) == 0x474, "ScrollControl +0x474 prev");
static_assert(offsetof(ScrollControl, ThumbHolder) == 0x518, "ScrollControl +0x518 thumb");
static_assert(offsetof(ScrollControl, ThumbXform) == 0x5b8, "ScrollControl +0x5b8 thumb xform");
static_assert(offsetof(ScrollControl, ThumbHp1) == 0x628, "ScrollControl +0x628 thumb hp1");
static_assert(offsetof(ScrollControl, MinRange) == 0x658, "ScrollControl +0x658 min");
static_assert(offsetof(ScrollControl, RepeatTime) == 0x670, "ScrollControl +0x670 repeat time");
static_assert(offsetof(ScrollControl, bPageTowardMin) == 0x67b, "ScrollControl +0x67b");

// DAT_0067c4c4 -- set while a ScrollControl drags its thumb (0x0058304a), cleared by the dtor,
// Update and MouseEvent; retail never reads it.
extern unsigned char G_ScrollControlDragging;
