// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail WinControl (RTTI .?AUWinControl@@ : Win3D, IDataViewOwner) -- the mesh-backed Win3D
// every NeuroNet control, UIScroll, BaseRichTextWin / RichTextWin and most HUD panels derive
// from. Constructed by FUN_0059f860, destroyed by FUN_0059fa50 (scalar deleting FUN_0059fa30),
// built by WINFACTORY<WinControl>::Create FUN_005a1a20 (operator_new(0x32c)).
//
// vftable 0x005e5e44 (45 slots): slots 0..27 are Win3D's (win3d.h) with these overrides --
//   0 FUN_0059fa30 dtor, 2 FUN_0043d950 (returns this), 4 FUN_005a0340 HitTest,
//   5 FUN_005a0880 SetPosition, 7 FUN_005a0950 SetOrientation, 10 FUN_0059fb60 Destroy,
//   13 FUN_005a0760 Draw, 19 FUN_005a0650 MouseEvent, 23 FUN_005a0840 Slot23
// -- and slots 28..44 are WinControl's own (declared below).
// The IDataViewOwner face sits at +0x7c (vftable 0x005e5e38).
//
// Layout (FUN_0059f860):
//   +0x000 Win3D (0x7c)
//   +0x07c IDataViewOwner vptr
//   +0x080 FClientMeshHolder (0x28): holder ctor FUN_00420d40, +0x90 anim = -1,
//          +0x94..+0xa4 = 1.0f; the holder's own vftable 0x005d2efc is not modelled
//   +0x0a8 offset[3]            ("offset" in the BaseFrame ini; added to +0x60 for the mesh)
//   +0x0b4 half extents[3]      ("mouse_size"; the box FUN_005a1130 tests the ray against)
//   +0x0c0 hit offset[3]        (added to the hit box position by FUN_005a0340)
//   +0x0cc tooltip id           ("tooltip"; slot 34 / FUN_005a0340)
//   +0x0d0 char[0x100] mesh path (slot 38)
//   +0x1d0 char[0x100] hardpoint name (slot 35 attaches to the parent's hardpoint of this name)
//   +0x2d0 bCaptured, +0x2d1 no_z_enable, +0x2d2 (property 9), +0x2d3 identity hit orientation
//   +0x2e0 {0, 0, 10000.0f}, +0x2ec 0, +0x2f0 0 -- written by the ctor, read by subclasses
//   +0x2f4 flag byte: bit0 = add +0xa8 to the instance position (ctor sets it)
//   +0x2f8 attach orientation[9], +0x31c attach position[3], +0x328 bAttached
//   size 0x32c

#include "client_mesh_load.h"
#include "win3d.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

class RichTextWin;
class RichTextEditWin;
class UiScroll;

// RTTI .?AUIDataViewOwner@@. The base vftable 0x005e5efc holds two _purecall entries; every
// derived face seen so far (WinControl 0x005e5e38) fills them with LAB_0053aaa0 (RET 8) and
// FUN_0053e4a0 (RET 4). Names unknown.
struct IDataViewOwner
{
    virtual void DataViewSlot0(unsigned A, unsigned B) = 0; // +0x00
    virtual void DataViewSlot1(unsigned A) = 0;             // +0x04
};

class WinControl : public Win3D, public IDataViewOwner
{
public:
    // FUN_0059f860
    WinControl();
    // FUN_0059fa50
    ~WinControl() override;

    // --- Win3D overrides ---
    // slot 2 FUN_0043d950 -- MOV EAX,ECX / RET
    WinControl* AsWinControl() override;
    // slot 4 FUN_005a0340
    bool HitTest(float const* Ray) override;
    // slot 5 FUN_005a0880
    void SetPosition(float const* NewPosition) override;
    // slot 7 FUN_005a0950
    void SetOrientation(float const* NewOrientation) override;
    // slot 10 FUN_0059fb60
    void Destroy() override;
    // slot 13 FUN_005a0760
    void Draw() override;
    // slot 19 FUN_005a0650
    bool MouseEvent(FWin3DMouseEvent const* Event) override;
    // slot 23 FUN_005a0840 -- 0xef01 show / 0xef02 hide
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;

    // --- IDataViewOwner (vftable 0x005e5e38) ---
    // LAB_0053aaa0 -- RET 8
    void DataViewSlot0(unsigned A, unsigned B) override;
    // FUN_0053e4a0 -- RET 4
    void DataViewSlot1(unsigned A) override;

    // --- WinControl slots 28..44 ---
    // slot 28 (+0x70) LAB_0059fd10 -- read the [BaseFrame] ini at Path, then build every other
    // section's class_name through the factory with this as the parent
    virtual void LoadIni(char const* Path);
    // slots 29 / 30 (+0x74 / +0x78) FUN_0053e4a0 -- RET 4. Slot 29's argument is a FILE*: the
    // one override seen, NavBar's FUN_0043efd0, fprintf's its [NavBar] section into it (the
    // counterpart of slot 28 LoadIni). Name not recovered.
    virtual void Slot29(FILE* File);
    virtual void Slot30(unsigned A);
    // slot 31 (+0x7c) FUN_005a0250
    virtual void SetOffset(float X, float Y, float Z);
    // slot 32 (+0x80) FUN_005a0300
    virtual void CaptureMouse();
    // slot 33 (+0x84) FUN_005a0320
    virtual void ReleaseMouse();
    // slot 34 (+0x88) FUN_005a0530 -- feed the ToolTip with this control's tooltip id
    virtual void ShowToolTip();
    // slot 35 (+0x8c) FUN_005a09d0
    virtual void RefreshAttachment();
    // slot 36 (+0x90) FUN_005a0a70
    virtual bool GetHardpointTransform(char const* Name, float* OutTransform12);
    // slot 37 (+0x94) FUN_005a0bc0
    virtual WinControl* FindChild(char const* Name);
    // slot 38 (+0x98) FUN_0059fbd0 -- MaterialPath is FUN_0059ec80's second argument.
    virtual void SetMesh(char const* Path, char const* MaterialPath);
    // slot 39 (+0x9c) FUN_00401e90 -- RET; Draw's tail call
    virtual void Slot39();
    // slot 40 (+0xa0) FUN_005a1670
    virtual void GetAttachOrientation(float* OutOrientation9);
    // slot 41 (+0xa4) FUN_005a1750
    virtual void GetAttachPosition(float* OutPosition3);
    // slot 42 (+0xa8) FUN_005a13c0 -- property switch 1..11 (see wincontrol_base.cpp). A and B
    // are dwords that are an int, a flag or a pointer depending on Msg.
    virtual intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B);
    // slot 43 (+0xac) FUN_005a1380
    virtual void AddAttachOffset(float const* Delta);
    // slot 44 (+0xb0) FUN_00585090
    virtual bool NameEquals(char const* OtherName);

    // FUN_005a06a0 (thiscall) -- with "DRAW STATIC CONTROLS" on and an instance bound, submit
    // the +0x80 instance with depth test off, tinted by DAT_00679bb4. The head of Draw
    // (FUN_005a0760 inlines it); BaseRichTextWin::Draw (0x0057ae1f) and two HUD draws
    // (0x00492a2c, 0x004d573e) call it directly.
    void DrawStaticMesh();

    // FUN_005a0c30 (thiscall, RET 4) -- every child that answers AsWinControl (slot 2) gets
    // SetProperty(1, bHide, 0); then this control's own Flags: bHide clears bits 0..1, otherwise
    // sets them. Callers: HUD_Target 0x004e2062 / 0x004e3621.
    void SetHiddenWithChildren(bool bHide);

    // --- Child construction (wincontrol_children.cpp) ---
    // Each is thiscall on the parent: FUN_0059db20(Name, <class>, this, 0), then -- when the
    // factory returned an object -- that control's Init with the remaining arguments in order.
    // The return value is FUN_0059db20's result (null when the factory failed).
    //
    // FUN_005a0c90 (RET 0x3c) -- "ButtonControl" + ButtonControl::Init FUN_0055ac30. The Init
    // arguments land at +0xd0 (mesh), +0x350 (string), +0x360 (sound, slot at +0x374), the
    // FUN_0055a490 key lookup (+0x34c / +0xcc), +0xa8 (position), +0x1d0 (hardpoint), +0xb4
    // (half extents), +0x331 bit 0, +0x330 bits 3 and 5, +0x2d1, +0xcc, +0x32c and +0x34c.
    WinControl* CreateButtonControl(char const* Name, char const* MeshPath, char const* String350,
                                    char const* SoundName, char const* KeyCommandName,
                                    float const* Position, char const* Hardpoint,
                                    float const* HalfExtents, unsigned char bFlag331,
                                    unsigned char bFlag330Bit3, unsigned char bFlag330Bit5,
                                    unsigned char bNoZEnable, int ToolTipIds, int Dword32c,
                                    int Dword34c);
    // FUN_005a0d00 (RET 0x24) -- "WireButtonControl" + WireButtonControl::Init FUN_005a3ee0.
    WinControl* CreateWireButtonControl(char const* Name, char const* SoundName,
                                        float const* Position, float const* HalfExtents,
                                        unsigned char bFlag3, unsigned char bNoZEnable,
                                        int ToolTipIds, int CommandId, int WireFlags);
    // FUN_005a0d60 (RET 0x1c) -- "WireStaticControl" + WireStaticControl::Init FUN_005a3360.
    WinControl* CreateWireStaticControl(char const* Name, float const* Position,
                                        float const* HalfExtents, unsigned char bFlag3,
                                        unsigned char bNoZEnable, int ToolTipIds, int WireFlags);
    // FUN_005a10e0 (RET 0x14) -- "RichTextWin", then SetNormSize (FUN_00579cc0), SetAttachOffset
    // (FUN_00579c40) and SetAutoScrollbar (FUN_005799f0). Unlike the others it does not test
    // the factory's result before configuring it.
    // FUN_005a17e0 (RET 0x20) -- "UIScroll" with the FUiPayloadScrollCycle {Width, Height,
    // float(Min), float(Max), float(Page), 1.0f, bHorizontal}; on a non-null result
    // SetScrollOffset(Position) FUN_005972d0, SetValue(0) FUN_00595db0 and SetHorizontal
    // FUN_00597550. Caller: HUD_Status::Create 0x004daf10 ("WeaponListScroll").
    UiScroll* CreateUIScroll(char const* Name, float const* Position, float Width, float Height,
                             int Min, int Max, int Page, unsigned char bHorizontal);
    // FUN_005a1870 (RET 0x20) -- byte for byte FUN_005a17e0 with the class name "UICycle"
    // (0x005e53f4). Caller: NN_Preferences::Create 0x004aa7ab ("video_mode_cycle").
    UiScroll* CreateUICycle(char const* Name, float const* Position, float Width, float Height,
                            int Min, int Max, int Page, unsigned char bHorizontal);
    RichTextWin* CreateRichTextWin(char const* Name, float Width, float Height,
                                   float const* AttachOffset, bool bAutoScrollbar);
    // FUN_005a1900 (RET 0x14) -- "UITextEdit" with the FUiPayloadText {Width, Height, Caption};
    // on a non-null result SetAttachOffset (FUN_00579c40). Caller: NN_LoadSave::Create
    // 0x00486739 ("LoadSave_Edit2").
    RichTextEditWin* CreateUiTextEdit(char const* Name, float const* AttachOffset, float Width,
                                      float Height, wchar_t* Caption);
    // FUN_005a1950 (RET 0x28) -- "RichTextWireButtonControl" + FUN_005a4810.
    WinControl* CreateRichTextWireButtonControl(char const* Name, char const* SoundName,
                                                float const* Position, float const* HalfExtents,
                                                unsigned char bFlag3, unsigned char bNoZEnable,
                                                int ToolTipIds, unsigned CaptionIds,
                                                int CommandId, int WireFlags);
    // FUN_005a0db0 (RET 0x40) -- "StaticControl" + StaticControl::Init FUN_00588540.
    WinControl* CreateStaticControl(char const* Name, char const* MeshPath, float const* Position,
                                    char const* Hardpoint, float const* HalfExtents,
                                    unsigned char bNoZEnable, unsigned char Byte34c,
                                    unsigned char Byte344, unsigned char Byte345,
                                    unsigned char Byte346, unsigned char Byte34b,
                                    unsigned char Byte34a, int Int418, int Int41c,
                                    unsigned char Byte349, float const* Vector338);
    // FUN_005a0e30 (RET 0x2c) -- "TextControl" + TextControl::Init FUN_0058a110.
    WinControl* CreateTextControl(char const* Name, char const* StyleName, float const* Position,
                                  char const* Hardpoint, float Width, float Height, float Arg6,
                                  unsigned Ids, int Dword358, int Dword344,
                                  unsigned char Byte378);
    // FUN_005a1080 (RET 0x24) -- "CargoBarControl" + CargoBarControl::Init FUN_0055ee80
    // (cargo_bar_control.cpp). Color / HighlightColor are RGBA byte quads in a dword.
    WinControl* CreateCargoBarControl(char const* Name, char const* MeshPath,
                                      float const* NewOffset, char const* Hardpoint,
                                      float const* NewHalfExtents, unsigned char bHorizontal,
                                      int ToolTip2Ids, unsigned Color, unsigned HighlightColor);
    // FUN_005a1020 (RET 0x28) -- "ShipViewControl" + ShipViewControl::Init FUN_005842a0
    // (ship_view_control.cpp). RectColor / WireColor point at RGBA byte quads.
    WinControl* CreateShipViewControl(char const* Name, float const* RotSpeed,
                                      float const* NewOffset, float const* Ypr,
                                      float const* NewViewSize, char const* Hardpoint,
                                      bool bShowRect, bool bShowWire, unsigned const* RectColor,
                                      unsigned const* WireColor);
    // FUN_005a0e90 (RET 0x28) -- "EditControl" + FUN_0058a110 (TextControl::Init) with the
    // helper's arguments 2..10 and a trailing 0 for Init's last (byte +0x378) argument.
    WinControl* CreateEditControl(char const* Name, char const* StyleName, float const* Position,
                                  char const* Hardpoint, float Width, float Height, float Arg6,
                                  unsigned Ids, int Dword358, int Dword344);
    // FUN_005a0fb0 (RET 0x38) -- "ToggleButtonControl" + ToggleButtonControl::Init FUN_0055c1c0
    // with arguments 2..14. That Init sets byte +0x335 = 1, runs ButtonControl::Init
    // FUN_0055ac30(MeshPath, AltMeshPath, SoundName, KeyCommandName, Position, Hardpoint,
    // HalfExtents, bFlag331, bFlag330Bit3, bFlag330Bit5, bNoZEnable, ToolTipIds, Dword32c, 0)
    // and loads AltMeshPath into a second mesh holder at +0x378.
    WinControl* CreateToggleButtonControl(char const* Name, char const* MeshPath,
                                          char const* AltMeshPath, char const* SoundName,
                                          char const* KeyCommandName, float const* Position,
                                          char const* Hardpoint, float const* HalfExtents,
                                          unsigned char bFlag331, unsigned char bFlag330Bit3,
                                          unsigned char bFlag330Bit5, unsigned char bNoZEnable,
                                          int ToolTipIds, int Dword32c);
    // FUN_005a0ef0 (RET 0x70) -- "ScrollControl" + ScrollControl::Init FUN_00582120
    // (scroll_control.cpp) with the other 27 arguments. Callers: 0x004bf4ec, 0x004c1093.
    WinControl* CreateScrollControl(
        char const* Name, char const* BackMesh, float const* BackOffset, char const* BackHp,
        float const* BackMouse, bool bBackNoMeshRender, char const* NextMesh,
        float const* NextOffset, char const* NextHp, float const* NextMouse,
        bool bNextNoMeshRender, char const* PrevMesh, float const* PrevOffset,
        char const* PrevHp, float const* PrevMouse, bool bPrevNoMeshRender,
        char const* ThumbMesh, float const* ThumbOffset0, float const* ThumbOffset1,
        char const* ThumbHp0, char const* ThumbHp1, float const* ThumbMouse,
        bool bThumbNoMeshRender, int MinRange, int MaxRange, int PageSize,
        bool bSliderBehavior, bool bNoZEnableArg);

    FClientMeshHolder MeshHolder;   // +0x080
    float Offset[3];                // +0x0a8
    float HalfExtents[3];           // +0x0b4
    float HitOffset[3];             // +0x0c0
    int ToolTipIds;                 // +0x0cc
    char MeshPath[0x100];           // +0x0d0
    char HardpointName[0x100];      // +0x1d0
    unsigned char bCaptured;        // +0x2d0
    unsigned char bNoZEnable;       // +0x2d1
    unsigned char Field2d2;         // +0x2d2 -- property 9
    unsigned char bIdentityHitOrientation; // +0x2d3
    // +0x2d4 -- not written by the ctor and never read by WinControl itself: the HUD panels park
    // a child's rest Offset here (HUD_Target 0x004e0ee0.., HUD_ContactList 0x004d38e7) and hand
    // it to ButtonMover as the slide start (0x0045ce55, 0x004e4188).
    float HomeOffset[3];
    float Field2e0[3];              // +0x2e0 -- ctor {0, 0, 10000.0f}
    int Field2ec;                   // +0x2ec -- ctor 0
    int Field2f0;                   // +0x2f0 -- ctor 0
    unsigned char PositionFlags;    // +0x2f4 -- bit0: instance position includes +0xa8
    unsigned char Pad2f5[3];        // +0x2f5
    float AttachOrientation[9];     // +0x2f8
    float AttachPosition[3];        // +0x31c
    unsigned char bAttached;        // +0x328
    unsigned char Pad329[3];        // +0x329

private:
    WinControl(WinControl const&) = delete;
    WinControl& operator=(WinControl const&) = delete;
};

static_assert(sizeof(WinControl) == 0x32c, "retail WinControl is 0x32c bytes");
static_assert(offsetof(WinControl, MeshHolder) == 0x80, "WinControl +0x80 mesh holder");
static_assert(offsetof(WinControl, Offset) == 0xa8, "WinControl +0xa8 offset");
static_assert(offsetof(WinControl, HalfExtents) == 0xb4, "WinControl +0xb4 half extents");
static_assert(offsetof(WinControl, HitOffset) == 0xc0, "WinControl +0xc0 hit offset");
static_assert(offsetof(WinControl, ToolTipIds) == 0xcc, "WinControl +0xcc tooltip");
static_assert(offsetof(WinControl, MeshPath) == 0xd0, "WinControl +0xd0 mesh path");
static_assert(offsetof(WinControl, HardpointName) == 0x1d0, "WinControl +0x1d0 hardpoint");
static_assert(offsetof(WinControl, bCaptured) == 0x2d0, "WinControl +0x2d0 captured");
static_assert(offsetof(WinControl, Field2e0) == 0x2e0, "WinControl +0x2e0");
static_assert(offsetof(WinControl, PositionFlags) == 0x2f4, "WinControl +0x2f4 flags");
static_assert(offsetof(WinControl, AttachOrientation) == 0x2f8, "WinControl +0x2f8 attach");
static_assert(offsetof(WinControl, AttachPosition) == 0x31c, "WinControl +0x31c attach pos");
static_assert(offsetof(WinControl, bAttached) == 0x328, "WinControl +0x328 attached");

// FUN_005a1340 (thiscall on HalfExtents, RET 4; wincontrol_children.cpp) -- the mesh's box via
// FUN_0059f0d0; on success HalfExtents = {box[0], box[2], 0}. Callers pass a control's +0xb4
// and its +0x80 holder (NN_Inventory 0x004782b1 and nine other windows).
bool WinControlHalfExtentsFromMesh(float* HalfExtents, FClientMeshHolder* Holder);
