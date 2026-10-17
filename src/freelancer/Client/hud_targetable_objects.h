// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Client\Space\HUD_Targetable_Objects.cpp -- HUD_Targetable_Objects, the in-space overlay
// that brackets every object the player's scanner sees (plus the nav waypoint and the best-path
// waypoint), tracks the contact nearest the mouse, draws the lead-fire markers, the reticle and
// the four alert icons, and resolves the object under the cursor for the "select target" command.
//
// RTTI .?AUHUD_Targetable_Objects@@ : HUDItem @0, WinControl @0x38 (Win3D @0x38, IDataViewOwner
// @0xb4), Event::Handler @0x364. Size 0x93c (HUDFACTORY Create FUN_004f28a0: operator new(0x93c)).
//   ctor FUN_004e8fe0 (thiscall, INI_Reader& -- never read), dtor FUN_004ea7b0 (scalar deleting
//   FUN_004ea6c0; the WinControl face's slot 0 LAB_004f3a60 is `SUB ECX,0x38 / JMP 004ea6c0`).
//   vftables: HUDItem face 0x005d950c (11 slots), WinControl face 0x005d9454 (45 slots),
//             IDataViewOwner face 0x005d9448 (WinControl's defaults), Handler 0x005d9440.
//
// HUDItem-face overrides: 0 FUN_004ea6c0 dtor, 5 LAB_004ead20 SetTarget, 6 LAB_004ec450 Render.
// WinControl-face overrides: 0 LAB_004f3a60 (thunk to the dtor), 9 FUN_004f17c0 Create.
// Handler: 0 LAB_004ed910 OnEvent.
//
// DAT_00674b70 holds the live instance (set by the ctor at 0x004e9747, cleared by the dtor at
// 0x004ea950); every retail reader calls through it without a null test.

#include "client_mesh_load.h"
#include "event_handler.h"
#include "font_string.h"
#include "hud_element_list.h"
#include "wincontrol_base.h"

#include "common_cequip.h"   // CEScanner
#include "common_cobject.h"  // CObject, CSimple, CShip, Vector
#include "common_goods.h"    // BaseWatcher

#include <list>
#include <map>
#include <vector>

class INI_Reader;
class IFont;
struct IObjRW;
struct ICamera;
struct FSpaceCamera;
struct FButtonMontageTile;
class FClientGameobj;

// -------------------------------------------------------------------------------------------
// The per-contact record, 0x34 bytes. HUD_Targetable_Objects keeps a std::vector of them at
// +0x370 (one per scanner entry, rebuilt by FUN_004ed180) and two standalone ones: the nav
// waypoint at +0x380 and the best-path waypoint at +0x3b4. Offsets from FUN_004ed180's stores.
// -------------------------------------------------------------------------------------------
struct FHudContactRecord
{
    unsigned TypeFlags;         // +0x00 the object's type mask (IObjInspect slot 0x88)
    Vector Screen;              // +0x04 projected position (FUN_004ecfd0)
    Vector World;               // +0x10 world position (IObjInspect slot 0)
    unsigned ObjectId;          // +0x1c IObjInspect slot 0x20
    float ScreenRadius;         // +0x20 the bracket's half size in pixels (FUN_004ecfd0)
    unsigned char bHot;         // +0x24 the contact nearest the mouse (FUN_004ed180 tail)
    unsigned char Pad25[3];
    float Distance;             // +0x28 distance to the player (FUN_004ecfd0)
    unsigned char bWatchListed; // +0x2c the client gameobj's +0xa8 byte (FUN_005416c0 lookup)
    unsigned char bOnScreen;    // +0x2d FUN_004ecfd0 succeeded
    unsigned char bPlayerShip;  // +0x2e a CShip with +0x2bd (bIsPlayer) set
    unsigned char bValid;       // +0x2f the record holds a waypoint (standalone records only)
    unsigned char bIsBestPath;  // +0x30 the object is the best-path waypoint's
    unsigned char bIsWaypoint;  // +0x31 the object is the nav waypoint's
    unsigned char Pad32[2];
};

static_assert(sizeof(FHudContactRecord) == 0x34, "retail contact record stride 0x34");

// The lead-fire marker FUN_004ebf70 (0x004ec2f5..0x004ec39a) and FUN_004f1dd0 (0x004f1fdc..
// 0x004f202d) append to the list at +0x910: the node value is 0x20 bytes -- a watcher on the
// target, the lead position and its projection (camera slot 0x44). Render empties the list
// first (FUN_004f2d50, 0x004ec536).
struct FHudLeadMarker
{
    BaseWatcher Target;         // +0x00
    Vector LeadWorld;           // +0x08 CShip::get_tgt_lead_fire_pos
    Vector LeadScreen;          // +0x14 ICamera::ProjectVec of LeadWorld
};

// The Font::String value of the name-label map at +0x8fc: retail's map node carries a bare
// Font::String (ctor FUN_004144b0, dtor FUN_004144f0 + FUN_00445dc0 + free + FUN_00444e20);
// this wrapper runs the same construction and teardown (FFontString's own ctor / dtor).
struct FHudNameLabel
{
    FHudNameLabel() = default;
    ~FHudNameLabel() = default;

    FFontString Text;

private:
    FHudNameLabel(FHudNameLabel const&) = delete;
    FHudNameLabel& operator=(FHudNameLabel const&) = delete;
};

// FUN_0051ddd0's 0x18-byte result (0x0051ddf5..0x0051de0d start it as {0, -1, 0, 0, 0, 0};
// 0x0051df10..0x0051df61 fill it from the first accepted PhySys hit): the object under a camera
// ray. HUD_Targetable_Objects keeps the last one at +0x3f0 (the ctor writes the same start
// values), and FUN_004ed9f0 builds one on its stack.
struct FCameraRayHit
{
    CObject* Object;            // +0x00 GetRoot of the hit part's object; null for no hit
    long Instance;              // +0x04 the hit part's engine instance (-1 for no hit)
    Vector Point;               // +0x08 the hit point
    float Distance;             // +0x14 |Point - camera position|
};

static_assert(sizeof(FCameraRayHit) == 0x18, "FUN_0051dd20 result is 0x18 bytes");

// FUN_0051ddd0 (cdecl, returns Out) -- cast a ray from the camera's position to *To through the
// active system (PhySys::FindRayCollisions, at most 8 hits) and keep the first hit whose object
// and root are not Ignore and whose part (the hit's +0x1c word) is not a shield part. With
// bSkipFireBlocked, a hit whose root is a ship with CEqObj+0x140 >= 1.0 is passed over too.
// Retail keeps it with the space cameras (0x0051ddd0); its only caller is FUN_0051dd20.
FCameraRayHit* CameraRayPickTo(FCameraRayHit* Out, ICamera* Camera, Vector const* To,
                               CObject const* Ignore, bool bSkipFireBlocked);
// FUN_0051dd20 (cdecl, returns Out) -- unproject the screen point through Camera (slot 14),
// stretch the direction to 1e7 and pick along it with FUN_0051ddd0. Callers: Render 0x004ec50a,
// PickUnderCursor 0x004edd2f.
FCameraRayHit* CameraRayPick(FCameraRayHit* Out, ICamera* Camera, float ScreenX, float ScreenY,
                             CObject const* Ignore, bool bSkipFireBlocked);

// The four float rectangles the ctor measures from the bar meshes (FUN_0059f0d0 extents:
// maxX, minX, maxY, minY, maxZ, minZ).
struct FHudMeshRect
{
    float Left;                 // +0x0 minX
    float Bottom;               // +0x4 minY (health / shield bars: minZ)
    float Width;                // +0x8 maxX - minX
    float Top;                  // +0xc maxY (bars: maxZ); the reticle keeps its height here
};

class HudTargetableObjects : public HUDItem, public WinControl, public EventHandler
{
public:
    // FUN_004e8fe0 -- built by HUDFACTORY<HudTargetableObjects>::Create (FUN_004f28a0), which
    // passes the reader of the (empty) [Targetable_Objects] section; the ctor never reads it.
    explicit HudTargetableObjects(INI_Reader& Reader);
    // FUN_004ea7b0
    ~HudTargetableObjects() override;

    // --- HUDItem face ---
    // slot 5 LAB_004ead20 -- FUN_004c81a0, then the watched ship's scanner (FindFirst(0x80000)
    // + CEScanner::cast) into +0x368.
    void SetTarget(IObjRW* Target) override;
    // slot 6 LAB_004ec450 (RET 8)
    void Render(FSpaceCamera* Camera, float DeltaSeconds) override;

    // --- WinControl face ---
    // slot 9 FUN_004f17c0 (RET 8) -- SetPosition(own position), Win3D::Create, and on success
    // RegisterEvent(6) on the handler face.
    bool Create(Win3D* NewParent, void* Data) override;

    // --- Event::Handler face ---
    // slot 0 LAB_004ed910 (RET 4)
    bool OnEvent(unsigned MsgId) override;

    // FUN_004f1da0 (thiscall, RET 4) -- copy the aim point at +0x91c into Out; returns Out.
    Vector* GetAimPoint(Vector* Out) const;

    // FUN_004ec400's body on this instance: clear the low two bits of +0x6c on the four alerts.
    void HideAlerts();

private:
    // FUN_004ecfd0 (RET 0x14; the callers load ECX with this, the body never reads it) --
    // project World through the listener camera (FUN_0040f130); with the point beyond the near
    // plane and on the surface, fill Screen / ScreenRadius. Distance to the player is always
    // written. True when on screen.
    static bool ProjectContact(Vector const* World, float Radius, Vector* OutScreen,
                               float* OutScreenRadius, float* OutDistance);
    // FUN_004ed180
    void RebuildContacts();
    // FUN_004ed9f0 (thiscall, RET 4) -- what a click would select: fills PickId / PickSubObjId
    // (+0x448 / +0x446). A click on the nav or best-path waypoint's bracket selects that waypoint
    // unless bQuiet (then the result is false); otherwise the bracketed contact, else the camera
    // ray's object, else the pickable client object nearest the cursor, with a hostile contact
    // under the cursor taking over when it is nearer ("hostile pick assistance").
    // Callers: OnEvent 0x004ed955 (bQuiet 1), 0x004ed991 (bQuiet 0).
    bool PickUnderCursor(bool bQuiet);
    // FUN_004edeb0 (thiscall, RET 0xc) -- the id and sub-object a camera ray hit selects: a
    // simple within the scanner's radar range of the player's ship (an object with none of the
    // type bits 7 always, others within 45 radii), a solar's parent for a solar with one; the
    // sub-object from CEqObj::inst_to_subobj_id, SUBOBJ_ID_ROOT reported as SUBOBJ_ID_NONE.
    void PickRayTarget(FCameraRayHit const* Hit, unsigned* OutId, unsigned short* OutSubObjId);
    // FUN_004ee100 (thiscall) -- the client object of ObjDB bucket 2 that is a simple whose
    // CSimple+0xe0 low byte has bit 7,
    // whose doubled bracket holds the cursor and which is nearest the player; null unless that
    // distance is under 11000.
    FClientGameobj* FindPickableNearCursor();
    // FUN_004f1c20 (thiscall) -- the nearest on-screen contact of type 0x10000 that is hostile
    // to the player (FUN_0045a490 == -1) and whose bracket circle holds the cursor.
    IObjRW* FindHostileContactNearCursor();

    // --- the Render tree (LAB_004ec450) ---
    // FUN_004ebf70 -- the contact brackets (FUN_004eada0) in each contact's colour, and a lead
    // marker for each contact the player's ship can lead-fire at.
    void DrawContactMarkers();
    // FUN_004eada0 (thiscall, RET 0x10; the body never reads ECX) -- one contact's bracket as
    // 2-wide lines (FUN_005a2940) around Record's screen square: corner cuts for a hostile,
    // corner angles otherwise, the side bars between the cuts for a watch-listed hostile, and
    // side marks for a player ship (blue when watch-listed) or a watch-listed object. Color
    // points at RGBA bytes. Nothing for a zero bracket radius. Sole caller: 0x004ec21d.
    static void DrawContactBracket(FHudContactRecord const* Record, unsigned long const* Color,
                                   bool bHostile, bool bWatchListed);
    // FUN_004ee260 -- the waypoint and best-path markers with their labels, every scanned
    // contact's reticle / off-screen arrow and player-ship name label, then the ship's target
    // when the scan did not already draw it.
    void DrawTargetReticle();
    // FUN_004ebde0 (thiscall, RET 0xc; ECX and the third argument are never read) -- a diamond
    // of 2-wide lines through the four edge midpoints of Record's screen square, in the colour of
    // the RGBA bytes at Color. Callers: 0x004ee40a (nav waypoint), 0x004ee52d (best path).
    static void DrawWaypointMarker(FHudContactRecord const* Record, unsigned long const* Color,
                                   int Unused);
    // FUN_004f0620 (thiscall, RET 4) -- the edge arrow of an off-screen waypoint record, and the
    // pick of that record when the cursor is over the arrow (DAT_00674b78 / DAT_00674b74).
    void DrawWaypointArrow(FHudContactRecord const* Record);
    // FUN_004ef030 (thiscall, RET 8) -- the reticle of an on-screen target (Object / Record):
    // corner tiles (or the waypoint tile), the hat, the health / shield bars, the caption, the
    // range and toughness read-outs, and the lead-fire marker.
    void DrawOnScreenTarget(IObjRW* Object, FHudContactRecord const* Record);
    // FUN_004eef50 (thiscall, RET 0x14) -- the range read-out at Point: Distance less Radius
    // (when Radius > 0, not below 0) as "%d M" up to 2000, else "%d.%d K"; the text control
    // (+0x40c) placed (SetProperty 5) and drawn. Unused1 (always &DAT_00674b54) and Unused3
    // (the record) are not read.
    void DrawRangeLabel(void const* Unused1, float Radius, FHudContactRecord const* Unused3,
                        int Distance, Vector const* Point);
    // FUN_004f0900 (thiscall, RET 0xc) -- the edge arrow of an off-screen contact; bIsTarget for
    // the player's target.
    void DrawOffScreenContact(IObjRW* Object, FHudContactRecord const* Record, bool bIsTarget);
    // FUN_004f1dd0 -- a lead marker for every fully on-screen object of ObjDB bucket 9 with a
    // collision state that the player's ship can lead-fire at.
    void CollectLeadMarkers();
    // FUN_004f2070 -- a small cross on each lead marker (FUN_005a2940).
    void DrawLeadMarkers();
    // FUN_004f2220 -- the aim point at +0x91c.
    void UpdateAimPoint();
    // FUN_004f18d0 -- the missile alert: blinks while a missile targets the player's ship, faster
    // the nearer it is; the first sighting queues "incoming_missile" / "incoming_torpedo".
    void UpdateMissileAlert();
    // FUN_004f1800 -- the trail alert: blinks while a ship trails the player's ship within 500.
    void UpdateTrailAlert();
    // FUN_004f1b40 -- the lag alert (multiplayer only): lit above 600 ms ping, blinking red
    // above 1200 ms.
    void UpdateLagAlert();

    // Instance state (offsets from the ctor and the functions that read them).
    CEScanner* Scanner;                          // +0x368 LAB_004ead20
    ScanList* ScanEntries;                       // +0x36c Scanner + 0x24 (FUN_004ed180)
    std::vector<FHudContactRecord> Contacts;     // +0x370
    FHudContactRecord WaypointRecord;            // +0x380 nav waypoint (FUN_004c46a0)
    FHudContactRecord BestPathRecord;            // +0x3b4 best-path waypoint (FUN_004c4ff0)
    int HotIndex;                                // +0x3e8 the contact under the mouse, -1 none
    float HotScreenRadius;                       // +0x3ec its bracket radius (1e8 when none)
    FCameraRayHit RayHit;                        // +0x3f0 FUN_0051dd20 under the mouse
    CSimple* RayObject;                          // +0x408 unscanned object under the mouse
    WinControl* ReticleRangeText;                // +0x40c TextControl
    WinControl* MissileAlert;                    // +0x410 StaticControl
    WinControl* TrailAlert;                      // +0x414
    WinControl* RadiationAlert;                  // +0x418
    WinControl* LagAlert;                        // +0x41c
    float MissileBlinkTimer;                     // +0x420 0.1f (FUN_004f18d0)
    float MissileBlinkPeriod;                    // +0x424 0.1f; sqrt(nearest) * 0.001, >= 0.1
    unsigned char bMissileBlinkOn;               // +0x428 1
    float TrailBlinkTimer;                       // +0x42c 0.5f (FUN_004f1800)
    float TrailBlinkPeriod;                      // +0x430 0.5f
    unsigned char bTrailBlinkOn;                 // +0x434 1
    float RadiationBlinkTimer;                   // +0x438 0.7f (Render)
    unsigned char bRadiationBlinkOn;             // +0x43c 1
    float LagTimer;                              // +0x440 1.0f (FUN_004f1b40)
    unsigned char bLagBlinkOn;                   // +0x444 0
    unsigned short PickSubObjId;                 // +0x446 SUBOBJ_ID_NONE
    unsigned PickId;                             // +0x448 0
    unsigned char bMissileWarned;                // +0x44c 0; the missile voice line was queued
    FClientMeshHolder TargetArrow;               // +0x450 hud_targetarrow.3db
    FClientMeshHolder TargetArrowHat;            // +0x478 hud_targetarrowhat.3db
    FClientMeshHolder TargetArrowPlayer;         // +0x4a0 hud_targetarrowplayer.3db
    FClientMeshHolder TargetHealth;              // +0x4c8 hud_targethealth.3db
    FClientMeshHolder TargetShields;             // +0x4f0 hud_targetshields.3db
    FClientMeshHolder TargetArrowQuotes;         // +0x518 hud_targetarrowquotes.3db
    FClientMeshHolder NonselectedArrow;          // +0x540 hud_nonselectedarrow.3db
    FClientMeshHolder NonselectedArrowHat;       // +0x568 hud_nonselectedarrowhat.3db
    FClientMeshHolder NonselectedArrowQuotes;    // +0x590 hud_nonselectedarrowquotes.3db
    FClientMeshHolder NonselectedPlayerArrow;    // +0x5b8 hud_nonselectedplayerarrow.3db
    FClientMeshHolder LootArrow;                 // +0x5e0 hud_lootarrow.3db
    FHudMeshRect HealthBarRect;                  // +0x608 hud_targethealth extents
    FHudMeshRect ShieldBarRect;                  // +0x618 hud_targetshields extents
    FClientMeshHolder Reticle;                   // +0x628 hud_reticle.3db
    FClientMeshHolder ReticleQuotes;             // +0x650 hud_reticle_quotes.3db
    FClientMeshHolder ReticleArrows;             // +0x678 hud_reticle_arrows.3db
    FClientMeshHolder ReticleHealth;             // +0x6a0 hud_reticle_health.3db
    FClientMeshHolder ReticleShields;            // +0x6c8 hud_reticle_shields.3db
    FClientMeshHolder WaypointReticle;           // +0x6f0 hud_waypointreticle.3db
    FClientMeshHolder SelectedWaypoint;          // +0x718 hud_selectedwaypoint.3db
    FClientMeshHolder NonselectedWaypoint;       // +0x740 hud_nonselectedwaypoint.3db
    float WaypointReticleWidth;                  // +0x768 maxX - minX
    float WaypointReticleHeight;                 // +0x76c maxY - minY
    FHudMeshRect ReticleRect;                    // +0x770 minX, minY, width, height
    FHudMeshRect ReticleHealthRect;              // +0x780 minX, minY, width, maxY
    FHudMeshRect ReticleShieldsRect;             // +0x790
    FButtonMontageTile* CurrentTargetCorner;     // +0x7a0 FUN_0055a360 tiles
    FButtonMontageTile* CurrentTargetArrow;      // +0x7a4
    FButtonMontageTile* CurrentTargetArrowOutline; // +0x7a8
    FButtonMontageTile* CurrentTargetArrowHat;   // +0x7ac
    FButtonMontageTile* CurrentTargetHat;        // +0x7b0
    FButtonMontageTile* WaypointTarget;          // +0x7b4
    FButtonMontageTile* WaypointArrow;           // +0x7b8
    FButtonMontageTile* LootArrowTile;           // +0x7bc
    FButtonMontageTile* NSTargetArrow;           // +0x7c0
    FButtonMontageTile* NSTargetHat;             // +0x7c4
    FButtonMontageTile* PlayerArrow;             // +0x7c8
    FButtonMontageTile* NSArrowBackground;       // +0x7cc
    FButtonMontageTile* LootArrowBackground;     // +0x7d0
    FButtonMontageTile* WaypointArrowBackground; // +0x7d4
    FButtonMontageTile* PlayerArrowBackground;   // +0x7d8
    FFontString WaypointLabel;                   // +0x7dc IDS 0x442
    FFontString BestPathLabel;                   // +0x810 IDS 0x443
    // The target caption cache FUN_004ef030 keeps (0x004efe40..0x004f0194): the CSimple id
    // (+0xb0) the caption was built for, its loot units (1 for non-loot) and its visited flag;
    // a caption that fails to build resets them to 0 / -1 / -1. The timer (float) runs only for
    // TypeBits 0x80 objects and drops the id after 10 s, forcing a rebuild.
    unsigned NameCacheSimpleId;                  // +0x844 0
    int NameCacheKey848;                         // +0x848 -1 loot units
    int NameCacheKey84c;                         // +0x84c -1 visited (1) / unvisited (0)
    float NameCacheTimer;                        // +0x850 0
    FFontString TargetNameText;                  // +0x854 caption, HudSmall
    FFontString TargetInfoText;                  // +0x888 caption of a player-owned ship, PlayerName
    int TargetInfoKey;                           // +0x8bc -1 the toughness RangeText shows
    FFontString RangeText;                       // +0x8c0 the target's toughness ("%d")
    IFont* SmallFont;                            // +0x8f4 "HudSmall"
    IFont* PlayerNameFont;                       // +0x8f8 "PlayerName"
    std::map<unsigned, FHudNameLabel> NameLabels; // +0x8fc keyed by CShip +0xb0
    std::list<FHudLeadMarker> LeadMarkers;       // +0x910 FUN_004ebf70 / FUN_004f1dd0
    Vector AimPoint;                             // +0x91c written by FUN_004f2220
    unsigned char bFireAttitude;                 // +0x928
    int FireAttitude;                            // +0x92c -1 hostile / 0 neutral / 1 friendly
    unsigned LastTargetId;                       // +0x930
    float SelectPulse;                           // +0x934 seconds since the target changed
    unsigned char bPickPending;                  // +0x938 OnEvent's deferred pick
};

// DAT_00674b70 -- the live element (see the header comment).
extern HudTargetableObjects* G_HudTargetableObjects;

// FUN_004ec400 (cdecl) -- with DAT_00674b70 set and the letterbox up (FUN_0041a3e0), clear the
// low two bits of Win3D +0x6c on the four alert controls. Caller: the space frame 0x0054b0ee.
void HudTargetableObjectsHideAlertsInLetterbox();

// FUN_004eeea0 (cdecl) -- the colour of an off-screen / on-screen target marker into *OutArgb,
// returned: the idle cyan (DAT_00679bb0) when Object's get_type (IObjInspect slot 34) fails;
// type bit 0x200 violet (DAT_00679b80), 0x10000000 white, 0x5000000 red (DAT_00679ba8);
// otherwise, with the player in ObjDB bucket 1, its reputation colour (FUN_0045a4e0), else the
// idle cyan. Callers: FUN_004ef030 (0x004ef3aa), FUN_004f0900 (0x004f0b78).
unsigned long* HudTargetMarkerColor(unsigned long* OutArgb, IObjRW* Object);

// FUN_004d4220 (cdecl) -- ObjectId is the object the HUD waypoint points at: with player
// waypoints, the first one's position is the live waypoint (FUN_004f4b90 on DAT_00674bc8,
// not null-tested) or its non-zero +0x10 id is ObjectId; failing that, with a best path, its
// first point's resolved position (FUN_004c52d0) is the live waypoint, or its resolved object
// (FUN_004c5110) has ObjectId. Callers: FUN_004ef030 (0x004ef0c1), FUN_004f0900 (0x004f0a80).
bool HudIsWaypointObjectId(unsigned ObjectId);

// The factory is HUDFACTORY<HudTargetableObjects> (hud_element_list.h): Create FUN_004f28a0 is
// operator new(0x93c) + FUN_004e8fe0. Its static instance ("Targetable_Objects", built through
// FUN_004f2830 from the XC entry at 0x004f27aa) lives in hud_targetable_objects.cpp.

// DAT_00674bcc -- the player's hazard-warning state (static initialiser LAB_004f5310:
// {0, 0, 2.0f}). FUN_004f4f50 runs on it once per HUD-camera frame through FUN_004f5330.
struct FHudHazardWarning
{
    unsigned MineId;    // +0x0 the id of the last scanned mine within 2000 m, 0 when none
    unsigned ZoneFlags; // +0x4 the zone mask of the last zone sample
    float Timer;        // +0x8 seconds since the last zone sample

    // FUN_004f4f50 (thiscall, RET 4).
    void Update(float DeltaSeconds);
};

// FUN_004f5330 (cdecl) -- DAT_00674bcc.Update(DeltaSeconds). Caller: HUDCamera 0x0051d2fe.
void UpdateHudHazardWarning(float DeltaSeconds);

// FUN_004f5340 -- DAT_00674c08: set by FUN_004f4f50 while the ship (with CShip +0x218 clear)
// sits in a zone with mask bit 0x40000 (the "radiation_damage_detected" zone). Reader:
// HUD_Targetable_Objects 0x004ec63c.
bool IsHudRadiationZoneWarningOn();
