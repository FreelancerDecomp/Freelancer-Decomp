// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Client\Space\HUD_Target_Base.cpp -- HUD_Target_Base, the common base of the target
// panel (HUD_Target, FUN_004e08b0) and the player-status panel (HUD_Status, W3-D). It owns a
// private 3D view of one object (a GameCamera at +0x3b8 looking at a wireframe part cache of the
// object at +0x3a0), a slide-out animation (ButtonMover) and the sub-target pick / tool tip under
// the mouse.
//
// RTTI .?AUHUD_Target_Base@@ : HUDItem @0, WinControl @0x38 (Win3D @0x38, IDataViewOwner @0xb4),
// ButtonMover @0x364, Event::Handler @0x384. Size 0x4e0.
//   ctor FUN_004e6020 (thiscall, no arguments), dtor FUN_004da630 (scalar deleting FUN_004e6140)
//   vftables: HUDItem face 0x005d8e3c (17 slots), WinControl face 0x005d8d84 (45 slots),
//             IDataViewOwner face 0x005d8d74, ButtonMover face 0x005d8d68, Handler 0x005d8d60
//             (_purecall 0x005b8064 -- the class is abstract; HUD_Target / HUD_Status fill it).
//
// HUDItem-face overrides / additions:
//   0 FUN_004e6140 dtor, 11 LAB_004e6270 SetViewObject, 12 FUN_00401e90, 13 FUN_0053e4a0,
//   14 / 15 / 16 FUN_00401e90.
// WinControl-face overrides:
//   0 LAB_004e7fc0 (thunk to the dtor), 4 LAB_004e7d20 HitTest, 9 FUN_004b5060 (Win3D::Create,
//   inherited), 11 FUN_004e7980 Update, 13 FUN_004e7890 Draw, 42 FUN_004e7950 SetProperty.
// ButtonMover-face override: 0 FUN_004e79b0.

#include "button_mover.h"
#include "event_handler.h"
#include "font_string.h"   // FHudWideText
#include "hud_element_list.h"
#include "part_cache.h"
#include "space_camera_game.h"
#include "wincontrol_base.h"

#include "common_goods.h"  // BaseWatcher
#include "common_text.h"   // RenderDisplayList
#include "x86math.h"

#include <map>
#include <string>
#include <vector>

class INI_Reader;
class IFont;
struct FSpaceCamera;
struct IObjRW;
class CEquip;
struct CObject;
struct CShip;
struct CSimple;

// -------------------------------------------------------------------------------------------
// FHudInstanceBounds -- the object-space AABB of an engine instance hierarchy, each part taken
// as the sphere IEngine::GetInstanceExtent reports, moved into the frame of Xform.
// Retail: a 0x18-byte stack object {Max @0x00, Min @0x0c}; ctor FUN_004e65d0 (thiscall, RET 0xc).
// FUN_004e6880 and FUN_004e69c0 are also called from FUN_00585380 (0x00585434 / 0x005854ca).
// -------------------------------------------------------------------------------------------
struct FHudInstanceBounds
{
    FVector Max; // +0x00
    FVector Min; // +0x0c

    // FUN_004e65d0 -- root sphere (extent flag 0, centre through the instance transform, then
    // InverseTransform by Xform) gives Min = c - r, Max = c + r; every GetRelatedInstance(,1,)
    // child is folded in by ExpandChildren (the loop body is the same code as FUN_004e69c0).
    FHudInstanceBounds(void* Instance, FTransform const* Xform, bool bIncludeShieldParts);

    // No retail ctor: ShipViewControl::SetInstance (FUN_00585380) fills Max / Min on its own
    // stack (0x00585459..0x005854c6) and then calls ExpandChildren on it.
    FHudInstanceBounds() = default;

    // FUN_004e6580 (thiscall, RET 4) -- *Out = (Max + Min) * 0.5; returns Out.
    FVector* GetCenter(FVector* Out) const;

    // FUN_004e69c0 (thiscall, RET 0xc) -- for each child of Instance: unless bIncludeShieldParts,
    // skip a shield part (CObject::is_shield_part of the instance's user-data object); otherwise
    // grow Min / Max by the child's sphere and recurse into it.
    void ExpandChildren(void* Instance, FTransform const* Xform, bool bIncludeShieldParts);

    // FUN_004e68d0 (thiscall, RET 8) -- the part's extent radius; OutCenter = its extent centre
    // moved to world space through the instance transform.
    float GetPartSphere(void* Instance, FVector* OutCenter) const;

    // FUN_004e6960 (thiscall, RET 4) -- UserData(Instance)->is_shield_part(inst_to_part(Instance))
    // with both CObject lookups inlined.
    bool IsShieldPart(void* Instance) const;
};

// FUN_004e6880 (thiscall on the transform, RET 8) -- *Out = InverseTransform(Xform, In); returns Out.
FVector* HudInverseTransformPoint(FTransform const* Xform, FVector* Out, FVector const* In);

// -------------------------------------------------------------------------------------------
// FHudSpherePick -- a ray against the part spheres of an instance hierarchy, keeping the nearest
// part (the largest cosine to the ray) whose closest point lies within a slack distance of its
// sphere. Retail stack object of at least 0x74 bytes; used by HUD_Target_Base::PickSubTarget.
// -------------------------------------------------------------------------------------------
struct FHudSpherePick
{
    FVector Origin;             // +0x00
    FVector Dir;                // +0x0c  unit
    float Field18;              // +0x18  -1.0f (Setup); not read by the traced users
    unsigned char Pad1c[0x30];  // +0x1c  never written by the traced users
    unsigned char Field4c;      // +0x4c  ctor argument; not read by the traced users
    unsigned char Pad4d[3];
    int Field50;                // +0x50  -1; not read by the traced users
    int HitCount;               // +0x54
    float BestKey;              // +0x58  -cosine of the best hit
    FVector HitPoint;           // +0x5c  ray point of the best hit
    FVector HitSurface;         // +0x68  sphere point of the best hit

    // FUN_00422ba0 (thiscall, RET 4) -- +0x54 = 0, +0x58 = 0, +0x50 = -1, +0x4c = Flag.
    explicit FHudSpherePick(unsigned char Flag);

    // FUN_00422bc0 (thiscall, RET 8) -- Origin = From, Dir = normalize(To - From), +0x18 = -1.0f,
    // HitCount = 0, +0x50 = -1, BestKey = 0.
    void Setup(FVector const* From, FVector const* To);

    // FUN_00422e30 (thiscall, recursive, RET 8) -- children first (any hit makes the result
    // true), then this instance: extent flag 1 sphere, FUN_00422c70; accepted when the cosine
    // is above 0.5, the ray-to-sphere gap is <= Slack and (no hit yet or -cosine < BestKey).
    bool Test(void* Instance, float Slack);
};

// FUN_00422c70 (cdecl) -- W = TransformXform(Xform, Center). t = (W - Origin) . Dir. t < 0:
// OutRay = Origin, cosine 1; else OutRay = Origin + t * Dir, cosine = t / |W - Origin|. Q =
// OutRay - W; OutSurface = W + Q * (Radius / |Q|); when |Q| < Radius, OutRay = OutSurface.
// Returns the cosine.
float HudRaySphereClosest(FVector const* Origin, FVector const* Dir, FVector const* Center,
                          float Radius, FVector* OutRay, FVector* OutSurface,
                          FTransform const* Xform);

// FUN_0045a740 -- true while the HUD must not pick under the mouse: a NeuroNet screen is up
// (DAT_0067dcc8), the software cursor is hidden (FUN_0041dd90), the server tick is held
// (FUN_0042d730) or DAT_00668761 (LocationController +0x59) is set. Past those, false while the player is docked
// (FUN_004c4810); in space, true only when there is no player object (FUN_0054baf0).
bool HudIsMousePickBlocked();

// FUN_0045a7c0 (cdecl) -- the collision-group type -> ids_name table DAT_00611400 (0x15 pairs:
// 0 and 1 -> 0x708, then bit n (n = 1..19) -> 0x708 + n); 0 for any other value.
unsigned HudCollisionGroupTypeIds(unsigned CollisionGroupType);

// FUN_0045a1a0 (cdecl, returns Out) -- the damage colour of a part, Damage = 1 - relative
// health: < 0.25 the HUD chrome (DAT_00679bb4); < 0.5 bytes {ff, 80, 40, ff}; <= 0.75 bytes
// {ff, 40, 40, ff}; above that a red that follows the UI pulse: t = FUN_0059d570, flipped to
// 1 - t while FUN_0059d560 is set, bytes {R, G, G, ff} with R = ftol(255 * t) and
// G = ftol(R * t). Callers: 0x004e6d58 (ColourGroupParts), 0x004e6f82 (ColourEquipParts).
unsigned long* HudDamageColorArgb(unsigned long* OutArgb, float Damage);

// FUN_004c71b0 (cdecl, returns Out4) -- the screen rectangle of two points through the Win3D
// camera (DAT_0067dbf8) slot 0x34 Project: A gives left / top, B right / bottom, each
// truncated with ftol. A failed projection writes nothing, so that pair keeps whatever the
// four scratch dwords held (see .cpp). Callers: HudTargetBase::SetupViewCamera 0x004e7166 and
// 0x004cc1c9.
int* HudProjectScreenRect(int* OutRect4, FVector const* A, FVector const* B);

// FUN_004e7090 (thiscall on Xform, RET 8, returns Out) -- Out = TransformXform(Xform, In) through
// the HUD_Target_Base math engine DAT_00674b1c. 29 call sites: 0x004e7360 (SetupViewCamera),
// the beam effect 0x004fa3c0..0x004fb4ae (beam_effect.cpp, 4 + 9 sites), 0x004ff642..0x004ffc9a, 0x00513e01,
// 0x0051541a, 0x00516178, 0x005807cb..0x00580f9f, 0x00585409, 0x00596db9.
FVector* HudTransformPoint(FTransform const* Xform, FVector* Out, FVector const* In);

// FUN_00543320 (thiscall on the IObjRW, no arguments) -- the CShip behind an IObjRW: null ObjRw,
// a null IObjInspectImpl CBase (ObjRw + 0xc), a null CBase::Object or a class that is not a ship
// (& 0x503 != 0x503) gives null. Callers: HUD_Target 0x004e2aa5 / 0x004e2c6d / 0x004e3bfa.
CShip* HudShipOfObjRw(IObjRW* ObjRw);

// FUN_0045a4e0 (cdecl) -- the reputation colour of Other as the player sees it, into *OutArgb:
// white (DAT_00679b84) without both objects; purple (DAT_00679b9c) when Other's client gameobj
// has byte +0x18c bit 2; pink (DAT_00679b88) for a player ship in the player's group (CShip
// +0x1b4); else by Other's attitude towards Player: <= -0.6 red (DAT_00679ba8), >= 0.6 green
// (DAT_00679bac), otherwise -- or when the query fails -- white. Callers: HUD_Target
// 0x004e2378 / 0x004e476e, HUD_ContactList 0x004d2875 / 0x004d3a97, 0x004ec0fd, 0x004eef2d.
void HudReputationColor(unsigned long* OutArgb, IObjRW* Player, IObjRW* Other);

// FUN_0045a6e0 (cdecl) -- the band of a reputation Attitude: 0 at or below -0.6, 1 at or below
// -0.2, 4 at or above 0.6, 3 above 0.2, otherwise 2. Sole retail caller 0x004a830a
// (NN_PlayerInfo's Draw, NnPlayerInfo::Draw in nn_player_info.cpp).
int HudReputationBand(float Attitude);

// FUN_004d4c70 (cdecl) -- true for a solar the player has not visited. False when the object's
// client gameobj (FUN_005416c0 on +0xb0) is watch-listed (+0xa8) with get_type & 0x107ffdff,
// or when the object is not a solar ((class & 0x303) != 0x303); otherwise true unless bit 0 is
// set in CSolar::GetVisitValue | the visit-map flags (FUN_004c4da0). Callers: HUD_ContactList
// (0x004d11da..0x004d360a), HUD_Target 0x004e4094 / 0x004e439a, 0x004ecb73, 0x004ece71,
// 0x004efe0e.
bool HudIsUnvisitedSolar(CSimple const* Object);

// FUN_004d4d00 (cdecl) -- the visit byte of an object: 1 for null or a non-solar; for a solar,
// CSolar::GetVisitValue | the visit-map flags of its +0xb0 id (0 without an entry). Caller:
// 0x004755b8.
unsigned char HudObjectVisitFlags(CObject const* Object);

// FUN_004d4d50 (cdecl) -- the type caption ids of a CSimple from TypeBits (+0xe0), first match:
// bit 0 -> 0x4a0, bit 1 -> 0x4a1, bit 2 -> 0x4a2, 0x8010 -> 0x4a3, 0x40 -> 0x4a4, 0x80 -> 0x4a5,
// 0x100 -> 0x4a6, none -> 0x4a7. Callers: 0x004755d4, 0x004d3617, HUD_Target 0x004e40af /
// 0x004e43a7, 0x004ecb86, 0x004ece7e, 0x004efecd.
unsigned HudObjectTypeIds(CSimple const* Object);

// FHudWideText (the two-word heap wide string, FUN_004142a0 / FUN_00414280 / FUN_00444e20) is
// declared in font_string.h.

// DAT_00674b6c -- the 3DMathEngine HUD_Target_Base.cpp / HUD_Targetable_Objects.cpp share,
// FUN_005b73e0 on first use at every site (0x004e8d7b, 0x004ecfe6, 0x004ef318, 0x004ef7af,
// 0x004f063d, 0x004f06c2, 0x004f0bbc, 0x004f0c36, 0x004f1372).
extern I3DMathEngine* G_HudObjectsMath;

// FUN_005472a0 (cdecl; retail Client\space\ship.cpp) -- the ids_name of an object: 0 for null;
// a ship ((class & 0x503) == 0x503) reads Archetype (+0x88) -> ids_name (+0x14) with no null
// test; anything else tail-calls CSimple::get_name (vt+0x88). Callers: 0x00475676, 0x004cb90b,
// 0x004d310f, 0x004d36a6, 0x004d3714, HUD_Target 0x004e44b9, 0x004e8131, 0x004e8742,
// 0x004ecbf1, 0x004eced5.
unsigned GetObjectIdsName(CObject const* Object);

// FUN_005472d0 (cdecl; retail Client\space\ship.cpp) -- Out = "" (DAT_005c8ce0), then for a
// ship ((class & 0x503) == 0x503) the pilot name (CShip::get_pilot_name) when there is one, or
// Out cleared when there is none. Returns wcslen(Out->Text), 0 without a buffer. Callers:
// 0x004756f8, 0x0048ab1f, 0x004cb85f, HUD_Target 0x004e4400.
unsigned GetShipPilotName(CObject const* Object, FHudWideText* Out);

// FUN_0048dc10 (cdecl; retail NavMap) -- the nav-map sector of Pos into Out: the system's
// NavMapScale (ISystem slot 20, 1.0 without a system) scales Pos; row = bands of 33333 from
// -99999 over z * scale + 2700, column = the same over x * scale (0..8 each); ids 0x515 + row
// and 0x506 + column (0x10 chars) formatted into ids 0x54d (0x40 chars). Out has no bound.
// Callers: 0x0047522c, 0x0049f89c, 0x004e8990, 0x004e8cbf.
void NavMapSectorName(Vector const* Pos, wchar_t* Out);

// FUN_004e8060 (cdecl) -- the object-kind ids of TypeBits, first match: 1 -> 0x563, 2 -> 0x44b,
// 4 -> 0x564, 8 -> 0x565, 0x8010 -> 0x566, 0x20 -> 0x567, 0x40 -> 0x568, 0x80 -> 0x4fa,
// 0x100 -> 0x44c, 0x200 -> 0x442, 0x400 -> 0x56a, 0x800 -> 0x56b, 0x1000 -> 0x56c,
// 0x2000 -> 0x56d, none -> 0. Caller: 0x004e8685.
unsigned HudObjectKindIds(unsigned TypeBits);

// FUN_004e8d30 (cdecl) -- "A->B" for a trade-lane ring: walk PrevRing (+0x1d8) and NextRing
// (+0x1dc) through ClientObjDb_FindObject to the two ends of the lane and take their
// CSolar::get_tradelane_space_name; the order follows the player's side of the ring's plane
// (its -z axis through TransformMatrix, normalised: player . n - ring . n > 0 swaps them).
// False (Out untouched or half written, as in retail) for a non-solar, no player, a broken
// chain, a missing name or text that would not fit in Capacity. Caller: 0x004e884a.
bool HudTradelaneCaption(CSimple const* Object, wchar_t* Out, int Capacity);

// FUN_004e8100 (cdecl) -- the caption of an object into Out. Mode 0: the name line; 1: the
// location line; 2: both, separated by "\n". The name comes from the faction (Reputation::Vibe
// GetAffiliation / GetName: the two FmtStr names, a player's display name, the faction short
// name + " - " when bFactionShortName), a docking ring's base owner (cached in DAT_00674b80 /
// DAT_00674b84), the object-kind ids or the ids_name, plus " (%d)" for a loot stack; the
// location is the trade-lane "A->B", the ids_name for TypeBits & 0x8c0, or the system name,
// ids 0x561 and the nav-map sector. Built in G_NnTextScratchB (DAT_0066fc60) with
// G_NnTextScratchA (DAT_0066dc60) as scratch. Callers: 0x004d3684, HUD_Status 0x004db4b2,
// HUD_Target 0x004e42a0 / 0x004e434a, 0x004ecbd4, 0x004ecebe, 0x004efea8.
bool HudBuildObjectCaption(CSimple const* Object, FHudWideText* Out, int Mode,
                           bool bFactionShortName);

// DAT_00674b00 -- set once a trade request went to the server (0x004e2d39), cleared when the
// request is withdrawn (0x004e2901, 0x004e49b0) and by the IClient trade handlers LAB_005a6070
// (0x005a60f0, client_exports.cpp) and FUN_005a6200 (0x005a6238). Defined in
// hud_target_panel.cpp.
extern unsigned char G_bTradeRequested;

// -------------------------------------------------------------------------------------------
// HudTargetBase
// -------------------------------------------------------------------------------------------
class HudTargetBase : public HUDItem, public WinControl, public ButtonMover, public EventHandler
{
public:
    // FUN_004e6020 -- HUDItem (inlined, empty name), WinControl FUN_0059f860, ButtonMover
    // FUN_0055dec0, the handler vptr, +0x398 watcher zeroed, FPartCache FUN_00405890 (zeroed),
    // FGameCamera FUN_0051b2f0, SubTargetId = SUBOBJ_ID_NONE, bExpanded = 0, ViewPanel = 0,
    // bSubTargetPickEnabled = 0. +0x388..+0x397, +0x4b4..+0x4cb and +0x4d4.. are left to the
    // derived constructors.
    HudTargetBase();
    // FUN_004da630
    ~HudTargetBase() override;

    // --- HUDItem face, new slots 11..16 ---
    // slot 11 (+0x2c) LAB_004e6270 (RET 4) -- point the 3D view at Target's client object and
    // rebuild the wireframe part cache (see .cpp).
    virtual void SetViewObject(IObjRW* Target);
    // slot 12 (+0x30) FUN_00401e90 -- RET. SetProperty(0x26, ToggleButton) calls it; HUD_Target
    // FUN_004e2fe0 starts the slide-out there.
    virtual void Slot12();
    // slot 13 (+0x34) FUN_0053e4a0 -- RET 4. OnMoveFinished passes 0; HUD_Target FUN_004e1930
    // hands the byte to SetProperty(1, bHide, 0) of its panel controls -- WinControl property 1
    // hides on A == 1 (wincontrol_base.cpp), so the byte is "hide".
    virtual void Slot13(bool bHide);
    // slot 14 (+0x38) FUN_00401e90 -- Draw calls it right after drawing ViewPanel.
    virtual void Slot14();
    // slot 15 (+0x3c) FUN_00401e90
    virtual void Slot15();
    // slot 16 (+0x40) FUN_00401e90 -- SetViewObject calls it after re-pointing the view.
    virtual void Slot16();

    // --- WinControl face ---
    // LAB_004e7d20 (RET 4)
    bool HitTest(float const* Ray) override;
    // FUN_004e7980
    void Update() override;
    // FUN_004e7890
    void Draw() override;
    // FUN_004e7950 (RET 0xc)
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // --- ButtonMover face ---
    // FUN_004e79b0
    void OnMoveFinished() override;

    // FUN_004e7e20 (thiscall, RET 4) -- the equipment SubId of the HUDItem target (+0x28) when
    // SubId is not SUBOBJ_ID_NONE and the target's CObject is a ship (class & 0x503 == 0x503):
    // a tail jump into CEquipManager::FindByID. Otherwise null. Caller: HUD_Status 0x004db93d.
    CEquip* FindTargetEquip(unsigned short SubId);

protected:
    // FUN_004e6160 -- hide every collision group root in the part cache (MarkHidden, no
    // recursion), then every attached equipment root (mask 0x1fe0, with recursion).
    void HideGroupAndEquipParts();

    // FUN_004e70e0 (thiscall, RET 8) -- point ViewCamera (+0x3b8) at the viewed object for the
    // target view: the viewport is the hit box (+0x4b4 +- the half extents) projected through
    // the Win3D camera, shifted ScreenShift screen widths and inset by an eighth; half fov x 35;
    // the camera stands ViewRadius * 1.1 / tan(35) from the object's centre, either along the
    // player-ship -> object direction (GetDirectionMatrix; four times as far for loot) or, with
    // bFixedOrbit, along the object's own z axis turned -90 degrees about x. Then
    // Win3DSetViewport, the perspective, and bSubTargetPickEnabled = 1. Caller: HUD_Target
    // Slot14 0x004e3d0f (GetGameOption(0xc) != 0, -0.0125f).
    void SetupViewCamera(bool bFixedOrbit, float ScreenShift);

    // FUN_004e7070 -- recolour the part cache for the damage view: clear the active flag of the
    // hidden non-root parts (FUN_004059d0), then the collision groups (FUN_004e6cb0) and the
    // equipment (FUN_004e6ec0, tail jump). Caller: HUD_Target Slot14 0x004e3e73.
    void ColourViewParts();

    // FUN_004e6cb0 -- for a CEqObj view: every collision group root re-activated (no recursion)
    // in the chrome colour, or the damage colour (FUN_0045a1a0) while damaged, dimmed to 70 %
    // per byte, pulsed (FUN_00561f20) when it is the sub-target and the mouse may pick.
    void ColourGroupParts();

    // FUN_004e6ec0 -- for a CEqObj view: every attached equipment (mask 0x1fe0) with a root,
    // re-activated with recursion, in DAT_00679b98 or the damage colour; a manual gun
    // (Gun +0xba == 0) instead amber (DAT_00679ba4) while active, else the chrome colour; pulsed
    // when it is the sub-target and the mouse may pick. Not dimmed.
    void ColourEquipParts();

    // FUN_004e75b0 -- mouse-over sub-target of the viewed object -> SubTargetId and the ToolTip.
    void UpdateSubTargetToolTip();

    // FUN_004e79f0 (thiscall, RET 0x10) -- ids_name of the part under screen point (X, Y);
    // *OutSubId / *OutCount describe it (see .cpp).
    unsigned PickSubTarget(int X, int Y, unsigned short* OutSubId, int* OutCount);

    // The CObject the +0x398 watcher views: the inlined `watched - 8, IObjInspectImpl::CBase
    // +4` chain of 0x004e6166.. / 0x004e7a20.. (null anywhere along it -> null).
    CObject* GetViewCObject() const;

public:
    float ViewRadius;                  // +0x388
    FVector ViewCenter;                // +0x38c
    BaseWatcher ViewObject;            // +0x398
    FPartCache PartCache;              // +0x3a0
    unsigned short SubTargetId;        // +0x3b4
    unsigned char Pad3b6[2];
    FGameCamera ViewCamera;            // +0x3b8 .. +0x4b0
    unsigned char bSubTargetPickEnabled; // +0x4b0
    unsigned char Pad4b1[3];
    FVector HitCenter;                 // +0x4b4  derived ctors (HUD_Target {-0.387, -0.29, -1})
    float HitHalfWidth;                // +0x4c0  HUD_Target 0.11
    float HitHalfHeight;               // +0x4c4  HUD_Target 0.11
    WinControl* ToggleButton;          // +0x4c8  SetProperty(0x26, this) -> Slot12
    WinControl* ViewPanel;             // +0x4cc  Draw -> Slot14 after drawing it
    unsigned char bExpanded;           // +0x4d0  toggled by OnMoveFinished
    unsigned char Pad4d1[3];
    FVector HomePosition;              // +0x4d4  derived ctors

private:
    HudTargetBase(HudTargetBase const&) = delete;
    HudTargetBase& operator=(HudTargetBase const&) = delete;
};

// -------------------------------------------------------------------------------------------
// FHudSubTargetFlash -- HUD_Target +0x534 (0x24 bytes): the bracket drawn round the sub-target
// on the target view, flashing for Duration seconds after the sub-target changes.
// -------------------------------------------------------------------------------------------
struct FHudSubTargetFlash
{
    unsigned short SubId;     // +0x00
    unsigned char Pad02[2];
    float Timer;              // +0x04  counts down from Duration
    float Duration;           // +0x08  0.6f
    float BaseSize;           // +0x0c  16.0f
    float Field10;            // +0x10  32.0f (never read by the traced users)
    float CornerFrac;         // +0x14  0.227f
    float TickFrac;           // +0x18  0.2727f
    float TickLenFrac;        // +0x1c  0.3636f
    unsigned char Color[4];   // +0x20  {0xff, 0, 0, 0xff}

    // FUN_004dfbd0 (thiscall, no arguments).
    FHudSubTargetFlash();

    // FUN_004dfc40 (thiscall, RET 8) -- a new SubId restarts the flash (Timer = Duration) and,
    // unless it is SUBOBJ_ID_NONE, plays UI sound slot 0x18; then a positive Timer runs down by
    // DeltaSeconds, clamped at 0.
    void Update(float DeltaSeconds, unsigned short NewSubId);

    // FUN_004e04f0 (thiscall, RET 8) -- 2D overlay in Camera's viewport: ScreenPos is rounded in
    // place, then (while flashing) two wider dimmed brackets and finally the Color bracket.
    void Draw(FSpaceCamera* Camera, float* ScreenPos) const;

    // FUN_004dfca0 (thiscall, RET 8) -- the bracket: 56 GL_LINES vertices round ScreenPos with
    // half-size HalfSize (see .cpp).
    void DrawBracket(float const* ScreenPos, float HalfSize) const;
};

// -------------------------------------------------------------------------------------------
// HudTarget -- retail HUD_Target (Client\Space\HUD_Target.cpp), the [Target] section of hud.ini.
//
// RTTI .?AUHUD_Target@@ : HUD_Target_Base @0, ButtonScaler @0x4e0. Size 0x670.
//   ctor FUN_004e08b0 (INI_Reader&, RET 4; the reader is not read), dtor FUN_004e0cd0
//   (scalar deleting FUN_004e0bf0). Factory: the static HUDFACTORY<HUD_Target> DAT_00674aa8
//   ("Target"; XC LAB_004e0880 -> FUN_004e5680, atexit LAB_004e08a0), Create FUN_004e56f0.
//   vftables: HUDItem face 0x005d896c (17), WinControl 0x005d88b4 (45), IDataViewOwner
//   0x005d88a4, ButtonMover 0x005d8898, Handler 0x005d8890, ButtonScaler 0x005d89b4.
// Overrides: HUDItem 0 dtor, 12 FUN_004e2fe0, 13 FUN_004e1930, 14 FUN_004e3c90,
//   15 FUN_004e3f80, 16 FUN_004e4690; WinControl 9 FUN_004e0ee0 Create, 11 FUN_004e1c20
//   Update, 13 FUN_004e1bb0 Draw, 19 FUN_004e4af0 MouseEvent, 42 FUN_004e19e0 SetProperty;
//   mover 0 FUN_004e3320; handler FUN_004e2970; scaler 0 FUN_004e3480.
// -------------------------------------------------------------------------------------------
class HudTarget : public HudTargetBase, public ButtonScaler
{
public:
    // FUN_004e08b0
    explicit HudTarget(INI_Reader& Reader);
    // FUN_004e0cd0
    ~HudTarget() override;

    // --- HUDItem face ---
    // FUN_004e2fe0 -- start the open (mover) or the close (scaler) animation.
    void Slot12() override;
    // FUN_004e1930 -- SetProperty(1, bHide, 0) on the expanded-view controls.
    void Slot13(bool bHide) override;
    // FUN_004e3c90 -- the wireframe target view (drawn right after ViewPanel).
    void Slot14() override;
    // FUN_004e3f80 -- the target / sub-target name labels.
    void Slot15() override;
    // FUN_004e4690 -- the view object changed.
    void Slot16() override;

    // --- WinControl face ---
    // FUN_004e0ee0 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
    // FUN_004e1c20
    void Update() override;
    // FUN_004e1bb0
    void Draw() override;
    // FUN_004e4af0 (RET 4)
    bool MouseEvent(FWin3DMouseEvent const* Event) override;
    // FUN_004e19e0 (RET 0xc)
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // --- ButtonMover / ButtonScaler faces ---
    // FUN_004e3320
    void OnMoveFinished() override;
    // FUN_004e3480
    void OnScaleFinished() override;

    // --- Event::Handler ---
    // FUN_004e2970 (RET 4)
    bool OnEvent(unsigned MsgId) override;

    // FUN_004e5460 (RET 4) -- Mode (+0x648) = NewMode. Caller: HUD_ContactList 0x004cfe76.
    void SetMode(int NewMode);
    // FUN_004e5470 (RET 4) -- bRaw: Mode; else Mode while expanded and neither animation runs,
    // otherwise 3. Callers: HUD_ContactList 0x004d017c / 0x004d061e / 0x004d22fd / 0x004d247d /
    // 0x004d3ddb.
    int GetMode(bool bRaw);
    // FUN_004e54c0 -- start a cargo scan of the player's target (see .cpp).
    void OpenScanner();

private:
    // FUN_004e22b0 (RET 4)
    void UpdateButtons(bool bFull);
    // FUN_004e27e0
    void UpdateTradeButton();
    // FUN_004e3bd0 -- keep the player ship's target valid; returns CShip::get_target.
    IObjRW* SyncPlayerTarget();
    // FUN_004e4ba0 / FUN_004e4cc0
    void SelectNextSubTarget();
    void SelectPreviousSubTarget();
    // FUN_004e4dd0 (RET 4) -- refill Map with the view object's live groups and equipment;
    // returns its size. Map is always &SubTargets.
    int RebuildSubTargets(std::map<unsigned short, long>* Map);
    // FUN_004e49c0 (RET 4) -- the name of the flashing sub-target into Out (0x80 wide chars).
    void GetSubTargetName(wchar_t* Out);
    // FUN_004e4f70 (RET 4) -- the next hardpoint of HardpointNames projected to the screen.
    void NextHardpointScreenPos(float* OutScreen);
    // FUN_004e50d0 -- the scan sweep across the hardpoints while NN_Scanner is up.
    void DrawScanSweep();

public:
    WinControl* ShipName;                  // +0x500 "TargetShipName"
    WinControl* SubtargetName;             // +0x504 "SubtargetName"
    WinControl* ScanButton;                // +0x508 "TargetScanButton"
    WinControl* TractorButton;             // +0x50c "TargetTractorButton"
    WinControl* TradeButton;               // +0x510 "TargetTradeButton"
    WinControl* PreviousButton;            // +0x514 "TargetPreviousButton"
    WinControl* NextButton;                // +0x518 "TargetNextButton"
    WinControl* CommButton;                // +0x51c "TargetCommButton"
    WinControl* RequestGroupButton;        // +0x520 "TargetRequestGroupButton"
    WinControl* CloseButton;               // +0x524 "TargetCloseButton" (the ctor leaves it)
    // +0x528 one StyleNode(0x3ea8) + ids 0x60070 (Create). Retail builds and frees the list
    // inline (0x004e08ff.. / 0x004e0db5..); the port uses Common's ctor / dtor, same effect.
    RenderDisplayList StyleList;
    FHudSubTargetFlash SubTargetFlash;     // +0x534
    float LabelRefreshTimer;               // +0x558 counts to 1.0f (FUN_004e22b0)
    unsigned char bHadViewObject;          // +0x55c
    std::map<unsigned short, long> SubTargets; // +0x560 sub id -> root instance (FUN_004e4dd0)
    int SubTargetCursor;                   // +0x574
    int SubTargetCount;                    // +0x578
    std::vector<std::string> HardpointNames; // +0x57c (Slot16's hardpoint walk)
    int HardpointCount;                    // +0x58c
    int HardpointCursor;                   // +0x590
    unsigned char bScannerOpen;            // +0x594
    float SweepFrom[3];                    // +0x598
    float SweepTo[3];                      // +0x5a4
    float SweepTime;                       // +0x5b0
    unsigned char bSweepRestart;           // +0x5b4
    float SweepTrail[10][3];               // +0x5b8
    int SweepTrailCount;                   // +0x630
    WinControl* MinimizedFrame;            // +0x634 "TargetMinimizedFrame"
    WinControl* ContactListButton;         // +0x638 "SwitchToContactList"
    WinControl* TargetButton;              // +0x63c "SwitchToTarget"
    WinControl* NextEnemyButton;           // +0x640 "NextEnemyButton"
    WinControl* FormationButton;           // +0x644 "FormationList"
    int Mode;                              // +0x648 0 contact list, 1 target, 2 formation
    int ShownMode;                         // +0x64c
    unsigned char bFormationAvailable;     // +0x650
    WinControl* RankText;                  // +0x654 "TargetRankText"
    float ShownRank;                       // +0x658 -1.0f
    float Rank;                            // +0x65c
    unsigned char bClearPlayerTarget;      // +0x660 set when the new target is not attackable
    IFont* SmallFont;                      // +0x664 "HudSmall"
    IFont* PlayerNameFont;                 // +0x668 "PlayerName"
    unsigned char bFirstUpdate;            // +0x66c

private:
    HudTarget(HudTarget const&) = delete;
    HudTarget& operator=(HudTarget const&) = delete;
};
