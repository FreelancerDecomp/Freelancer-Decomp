// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// Common.dll goods / markets / bases / loadouts / constants.
// Linked into freelancer.exe. Not a Common.dll project. Not MSVCP60 STL.
// See docs/re/common.md (goods, loadouts, bases, rooms).

#include "common_archetype.h"
#include "common_csys.h"
#include "common_text.h"      // FmtStr for MissionVendorProperties/NewsBroadcastProperties
#include "common_reputation.h" // bodypart, accessory, Costume, CostumeDescriptions
#include "common_net.h"        // Watchable
#include "common_metalist.h"   // MetaList<T>, OwnerList<T> (GoodDescList)
#include "common_weighted_vector.h" // weighted_vector<T>
#include "thorn_iface.h"       // FThornEntitySnap, FThornLightProps (thorn.dll's published layouts)

#include <cstdlib>
#include <list>
#include <map>
#include <utility>
#include <vector>

class INI_Reader;
struct CObject;
struct GoodDescList;
class EquipDescList;

// ---------------------------------------------------------------------------
// The retail names Common's exports give thorn.dll's entity descriptor (IScriptEngine slot 9)
// and its two property tails, e.g. ?ThornGetRenderType@@YAKPBUThornEntity@@PBD@Z and
// ?add_set_script_light@RoomData@@AAEXKABVCsys@@PBUThornLightProperties@@E@Z. The layouts are
// the ones thorn_iface.h publishes; the names stay distinct types for the mangled signatures.
// ---------------------------------------------------------------------------
struct ThornEntity : FThornEntitySnap
{
};

// The LIGHT entity's tail at +0x44 (0x90 bytes).
struct ThornLightProperties : FThornLightProps
{
};

// The CAMERA / MONITOR entity's tail at +0x44: fovh, near, far, aspect.
// add_set_script_Camera (06275a50) reads the first three.
struct ThornCameraProperties
{
    float Fovh;      // +0x00 (entity +0x44)
    float NearPlane; // +0x04
    float FarPlane;  // +0x08
    float Aspect;    // +0x0c
};
static_assert(sizeof(ThornCameraProperties) == 0x10, "camera tail is four floats");

// ---------------------------------------------------------------------------
// HardpointInfo — sizeof 0x48. From HardpointSummary::get_info stride.
// The position/orientation order was recovered from HardpointToWorld (0xaeba0) and confirmed
// by HardpointToObject (0xaec40): both copy NINE dwords from `Hp + 0x10` as the 3x3 and read
// the vector from `Hp + 0x04`, `+0x08`, `+0x0c`. The previous comment here had the two
// swapped (orient at +0x04, pos at +0x28) — that is the layout of the 0x34-byte Barrel
// record in common_cequip.h, not of this one. 0x10 + 0x24 = 0x34 also lands exactly on the
// parent-instance field, which is the third piece of agreement.
// ---------------------------------------------------------------------------
struct HardpointInfo
{
    unsigned Crc;               // +0x00
    Vector Position;            // +0x04
    Matrix Orient;              // +0x10
    long ParentInstance;        // +0x34
    char Pad[0x10];             // +0x38..+0x47
};

static_assert(sizeof(HardpointInfo) == 0x48, "HardpointInfo sizeof 0x48");

static_assert(offsetof(HardpointInfo, Position) == 0x04, "hardpoint position at +0x04");
static_assert(offsetof(HardpointInfo, Orient) == 0x10, "hardpoint orientation at +0x10");
static_assert(offsetof(HardpointInfo, ParentInstance) == 0x34, "parent instance at +0x34");

// ---------------------------------------------------------------------------
// Hardpoint lookup -- 0xae940 / 0xae960 / 0xaeb70 / 0xaeba0.
// The shared core walks the engine instance tree: DALib::Engine slot 25 turns the handle into
// an instance, DALib::Hardpoint slot 3 answers a {name, CRC32} query against it, slot 15
// releases the instance, and slot 54 enumerates children for the recursion. On a hit the
// handle of the node it was found on comes back through OutHandle.
// ---------------------------------------------------------------------------

// 0xae940 -- the same search with the info discarded.
COMMON_API bool __cdecl FindHardpoint(long Handle, char const* Name, long& OutHandle);
// 0xaeb70 -- object space: the info comes back exactly as the hardpoint store holds it.
COMMON_API bool __cdecl FindHardpoint_OS(long Handle, char const* Name, long& OutHandle,
                                         HardpointInfo& OutInfo);
// 0xae960 -- parent space: as _OS, then walked back up to the handle the caller asked about.
COMMON_API bool __cdecl FindHardpoint_PS(long Handle, char const* Name, long& OutHandle,
                                         HardpointInfo& OutInfo);
// 0xaeba0 -- compose the hardpoint's frame with the instance's world transform, in place.
COMMON_API void __cdecl HardpointToWorld(long Handle, HardpointInfo& Info);

// ---------------------------------------------------------------------------
// SetpointProperties — sizeof 0x24. Parsed from setpoint name convention
// "zs/pc/Segment/00/Name/posture" (or "zg/..." for a zone-group setpoint).
// Retail (0x1b190/0x1b1b0 ctor/clear): +0=bValid byte(0), +1=bZoneScript byte(0),
// +4=char_type(-1), +8=segment(str*), +0xC=zone(-1), +0x10=name[16], +0x20=posture(-1).
// Confirmed by combine_to_name (0x1b660), the exact reverse of extract_from_name
// (0x1b2b0): builds "Zs|Zg / pc|npc|rpc / Segment / %02d(Zone) / NameBuffer / posture".
// ---------------------------------------------------------------------------
struct COMMON_API SetpointProperties
{
    SetpointProperties();
    ~SetpointProperties();
    SetpointProperties(SetpointProperties const& Other);
    SetpointProperties& operator=(SetpointProperties const& Other);

    void clear();
    void destroy();
    void set_segment(char const* Segment);
    void extract_from_name(char const* Name);
    char const* combine_to_name() const;

    unsigned char bValid;       // +0x00  0 or 1 -- name matched zs/zg naming convention
    unsigned char bZoneScript;  // +0x01  1="zs/" prefix (zone/solar setpoint), 0="zg/" prefix (zone group)
    unsigned char Pad02;        // +0x02  padding (retail leaves uninitialized; zeroed here)
    unsigned char Pad03;        // +0x03  padding
    int CharType;               // +0x04  -1=none, 0=npc, 1=pc, 2=rpc
    char const* Segment;        // +0x08  malloc'd string
    int Zone;                   // +0x0C  zone number (-1 default)
    char NameBuffer[16];        // +0x10  uppercase segment copy
    int Posture;                // +0x20  0=stand,1=sitlow,2=sithigh,3=special, -1 default/unset
};

static_assert(sizeof(SetpointProperties) == 0x24, "SetpointProperties sizeof 0x24");

// ---------------------------------------------------------------------------
// Room sub-structures
// ---------------------------------------------------------------------------

// RoomCameraInfo — camera snapshot from Thorn or INI [Camera].
// Clear zeros name, sets identity Csys, fov=35, near=0.25, far=1e6.
struct COMMON_API RoomCameraInfo
{
    RoomCameraInfo();
    // Retail exports operator= (0x06273370) but no copy ctor: the copy ctor is the implicit
    // trivial one (a raw member copy, Name shared), which is not exported. RoomData's own
    // copy (0x06261330) uses it for the embedded Camera.
    ~RoomCameraInfo();
    RoomCameraInfo& operator=(RoomCameraInfo const& Other);

    void clear();
    void destroy();

    char const* Name;           // +0x00
    Csys Frame;                 // +0x04  pos+orient = 0x30
    float Fov;                  // +0x34  default 35.0
    float NearPlane;            // +0x38  default 0.25
    float FarPlane;             // +0x3C  default 1000000.0
};

static_assert(sizeof(RoomCameraInfo) == 0x40, "RoomCameraInfo sizeof 0x40");

// SetpointInfo — named position in a room (from Thorn type 7 or INI).
struct COMMON_API SetpointInfo
{
    SetpointInfo();
    // 0x062612d0: Name copied as a pointer, Frame copied, SetpointProperties copy ctor.
    SetpointInfo(SetpointInfo const& Other);
    ~SetpointInfo();
    // Retail exports no operator=; RoomData keeps setpoints as SetpointInfo* (retail +0x164).
    SetpointInfo& operator=(SetpointInfo const& Other) = delete;

    void clear();
    void destroy();

    char const* Name;           // +0x00
    Csys Frame;                 // +0x04
    SetpointProperties Props;   // +0x34
};

// CharPlaceInfo — character placement slot in a room.
struct COMMON_API CharPlaceInfo
{
    CharPlaceInfo();
    // Retail exports neither a copy ctor nor operator=: placements live behind pointers
    // (RoomData::CharacterPlacements) and RoomData's own copy only copies those pointers.
    // (The implicit trivial copy ctor is not exported; operator= is deleted so that no
    // implicit copy assignment is exported either.)
    ~CharPlaceInfo();

    CharPlaceInfo& operator=(CharPlaceInfo const& Other) = delete;

    void clear();
    void destroy();
    bool is_named(char const* Name) const;

    char const* Name;           // +0x00
    // Retail also carries a Csys Frame (+0x04..+0x33, identity by default) and three
    // fields sourced from the matching Setpoint's SetpointProperties (+0x34 CharType,
    // +0x38 Posture, +0x3c bZoneScript) here, ahead of StartScript at +0x40.
    // read_CharacterPlacement_block (INI [CharacterPlacement]) only ever supplies
    // Name/StartScript, leaving these three at their defaults; RoomData::read_set_script's
    // post-loop over Setpoints (Common.dll.c 0x06278e20, called after the main Thorn
    // entity loop, retail line ~28755-28783) is what actually populates them, for every
    // Setpoint whose Props.bValid is set (i.e. its name matched the zs/zg convention).
    Csys Frame;                 // +0x04, identity by default
    int CharType;                // +0x34, default -1 -- copy of SetpointProperties::CharType
    int Posture;                 // +0x38, default -1 -- copy of SetpointProperties::Posture
    unsigned char bZoneScript;   // +0x3c, default 0 -- copy of SetpointProperties::bZoneScript
    char const* StartScript;    // +0x40
};

// ShipPlaceInfo — ship placement in a room (PlayerShipPlacement / ForSaleShipPlacement).
struct COMMON_API ShipPlaceInfo
{
    ShipPlaceInfo();
    // Retail exports neither a copy ctor nor operator= (see CharPlaceInfo).
    ~ShipPlaceInfo();

    ShipPlaceInfo& operator=(ShipPlaceInfo const& Other) = delete;

    void clear();
    void destroy();

    char const* Nickname;        // +0x00  copied from the matched Setpoint's Name on "name="
    Csys Frame;                  // +0x04  copied from the matched Setpoint's Frame on "name="
    bool bIsPlayerShip;          // +0x34  true for [PlayerShipPlacement], false for [ForSaleShipPlacement]
    bool bIsForSale;             // +0x35  false for [PlayerShipPlacement], true for [ForSaleShipPlacement]
    char const* LandingScript;   // +0x38  [PlayerShipPlacement] "landing_script" only
    char const* LaunchingScript; // +0x3c  [PlayerShipPlacement] "launching_script" only
};

static_assert(sizeof(ShipPlaceInfo) == 0x40, "ShipPlaceInfo sizeof 0x40");

// RoomButtonInfo — button/hotspot in a room.
struct COMMON_API RoomButtonInfo
{
    RoomButtonInfo();
    // Retail exports neither a copy ctor nor operator=: RoomData keeps its hotspots as
    // RoomButtonInfo* (vector at retail +0x194, filled by read_Hotspot_block through the
    // allocate-and-append helper FUN_06279400).
    ~RoomButtonInfo();

    RoomButtonInfo& operator=(RoomButtonInfo const& Other) = delete;

    void clear();
    void destroy();

    char const* Name;               // +0x00
    char const* RoomSwitch;         // +0x04  "room_switch="
    char const* VirtualRoom;        // +0x08  "virtual_room="
    char const* SetVirtualRoom;     // +0x0c  "set_virtual_room="
    int Behavior;                   // +0x10  "behavior=", exit-door option enum, -1 if unset
    unsigned int StateSend;         // +0x14  "state_send=" bitmask
    unsigned int StateRead;         // +0x18  "state_read=" bitmask
    char const* StateTransition[4]; // +0x1c/0x20/0x24/0x28  "state_transition=", up to 4 entries
};

static_assert(sizeof(RoomButtonInfo) == 0x2c, "RoomButtonInfo sizeof 0x2c");

// RoomLightInfo — light entity from Thorn type 5.
struct COMMON_API RoomLightInfo
{
    RoomLightInfo();
    ~RoomLightInfo();

    RoomLightInfo& operator=(RoomLightInfo const& Other);

    void clear();
    void destroy();

    Csys Frame;         // +0x00, 0x30 bytes
    // +0x30 -- RoomData::add_set_script_light (0x15960) copies this from
    // ThornLightProperties+0xC/+0x10/+0x14. PROVEN to be a DIRECTION, not an RGB colour
    // (this field was named Color until 2026-09-17): RoomData::read_set_script's named
    // "LtShadowSource"/"LtGlareSource" branch (0x14b40 @ 06274ec9-06274f74) reads the very
    // same ThornLightProperties+0xC triple, multiplies it by the entity's orientation matrix
    // and then normalizes it (FSQRT + FDIVR 1.0f) before storing it into RoomData's
    // Shadow/GlareLightDirection -- a colour is never rotated or normalized. Corroborated by
    // the retail ctor/clear default of (0, 0, -1.0f), a unit -Z direction rather than any
    // meaningful colour.
    float Direction[3];
    // +0x3C -- ThornLightProperties+0x28. Retail's named-light branch compares exactly this
    // dword against 3 (06274eb1 `CMP [ESP+0xd4],EBX` with EBX=3, set at 06274dd3) and takes
    // the rotate-the-direction path when it matches, versus copying the entity's own position
    // for every other value. So 3 selects directional-light semantics; the meaning of the
    // other values is not established by any call site in this module, hence the neutral name.
    int Mode;
    // +0x40 -- ThornLightProperties+0x2C/+0x30/+0x34. PROVEN to be the RGB colour: the same
    // named-light branch stores this triple verbatim into RoomData::ShadowLightColor /
    // GlareLightColor (06274f9d / 062750cb) on both the mode==3 and mode!=3 paths, i.e. it is
    // the one field that survives unchanged regardless of the geometry interpretation.
    float Color[3];
    // +0x4C, 0x58 bytes (22 dwords) -- the still-undecoded tail of ThornLightProperties
    // (+0x38..+0x90). ThornLightProperties is only forward declared in this codebase and no
    // other call site in Common.dll reads past +0x34, so these bytes are modeled opaquely
    // rather than by guessing field boundaries. Left uninitialized by the ctor/clear(),
    // matching retail exactly (Common.dll.c 0x13410 / 0x13490 jump straight from +0x38 to
    // +0xa4). TODO(full-port): decode against the Thorn-side writer that fills the struct.
    unsigned char Opaque4C[0x58];
    // +0xA4 -- the 4th (unsigned char) parameter of add_set_script_light. In the one
    // in-module producer, RoomData::read_set_script, it is the Thorn entity's own +0x40 byte
    // (0627510a `MOV BL,byte ptr [EBP+0x40]`). This is the byte the ctor/clear() zero via
    // `this[0xa4] = 0`. Its semantic meaning is not established by any call site, so the name
    // stays generic. Previously mismodeled as a Range/Type/Instance trio at +0x3C/+0x40/+0x44.
    unsigned char Type;
    // +0xA5..+0xA7: trailing compiler alignment padding, not addressed by any retail code path.
};

static_assert(sizeof(RoomLightInfo) == 0xA8, "RoomLightInfo sizeof 0xA8");
static_assert(offsetof(RoomLightInfo, Direction) == 0x30, "RoomLightInfo+0x30 direction");
static_assert(offsetof(RoomLightInfo, Mode) == 0x3C, "RoomLightInfo+0x3C mode");
static_assert(offsetof(RoomLightInfo, Color) == 0x40, "RoomLightInfo+0x40 colour");
static_assert(offsetof(RoomLightInfo, Opaque4C) == 0x4C, "RoomLightInfo+0x4C opaque tail");
static_assert(offsetof(RoomLightInfo, Type) == 0xA4, "RoomLightInfo+0xA4 type byte");

// RoomPropInfo — furniture/prop from Thorn type 1 (COMPOUND).
struct COMMON_API RoomPropInfo
{
    RoomPropInfo();
    // Retail exports operator= (0x062612b0, a 0x15-dword block copy) but no copy ctor (the
    // implicit trivial one is not exported).
    ~RoomPropInfo();

    RoomPropInfo& operator=(RoomPropInfo const& Other);

    void clear();
    void destroy();

    // Field meanings from the one producer, RoomData::read_set_script's COMPOUND branch
    // (FUN_062788b0 then .text:06274d4e..06274dcb), and the one consumer, freelancer.exe
    // FUN_0044a030, which copies each into a Gf::ObjectProp (0x0044a0b7..0x0044a27f).
    char const* Template;       // +0x00 entity +0x3c (the archetype / PetalDB nickname)
    char const* Name;           // +0x04 entity +0x38
    int Category;               // +0x08 PetalInterfaceDatabase::get_category("category")
    Csys Frame;                 // +0x0C entity +0x08 position, +0x14 orientation
    bool bVisible;              // +0x3C entity flags bit 1 -> RoomObject +0x10
    bool bLit;                  // +0x3D entity flags bit 2 -> RoomObject +0x11
    bool bFog;                  // +0x3E no "nofog" user property -> RoomObject +0x12
    bool bRoomMain;             // +0x3F "hardpoint" user property, or category 0 -> prop +0x49
    unsigned char LightGroup;   // +0x40 entity +0x40 byte -> RoomObject +0x15
    unsigned char Layer;        // +0x41 entity +0x41 byte -> RoomObject +0x16
    unsigned RenderType;        // +0x44 ThornGetRenderType(entity, 0) -> RoomObject +0x18
    char const* Loadout;        // +0x48 "loadout" user property, or ""
    unsigned PriorityId;        // +0x4C CreateID("priority"), 0 without one; RtcSlider gate
    bool bRunningLights;        // +0x50 "running_lights" user property -> RoomObject +0x14
};

// operator= (0x062612b0) block-copies 0x15 dwords.
static_assert(sizeof(RoomPropInfo) == 0x54, "RoomPropInfo sizeof 0x54");

// ExhaustNozzleInfo — engine exhaust nozzle offset.
struct COMMON_API ExhaustNozzleInfo
{
    ExhaustNozzleInfo();
    ExhaustNozzleInfo& operator=(ExhaustNozzleInfo const& Other);

    bool GetHardpointOffset_NS(Transform& Out) const;

    // Fields as CEEngine's ctor fills each appended element (.text:06295d73-06295dad).
    unsigned short SubObjId;     // +0x00  SUBOBJ_ID_NONE (0, .rdata:063a4aa4) = no hardpoint bound
    unsigned short Pad02;        // +0x02  never written (retail inserts an uninitialised element)
    int Index;                   // +0x04  N of "HpEngine0<N>" (.text:06295d7a)
    long ParentInstance;         // +0x08  FindHardpoint_OS out handle (.text:06295d81)
    Transform Offset;            // +0x0C  hardpoint Orient then Position
};

static_assert(offsetof(ExhaustNozzleInfo, Offset) == 0x0C, "ExhaustNozzleInfo+0xC transform");
static_assert(sizeof(ExhaustNozzleInfo) == 0x3C, "ExhaustNozzleInfo sizeof 0x3C");

// ---------------------------------------------------------------------------
// HardpointSummary — runtime hardpoint enumeration from compound instances
// ---------------------------------------------------------------------------
class COMMON_API HardpointSummary
{
public:
    HardpointSummary();
    HardpointSummary(HardpointSummary const& Other);
    virtual ~HardpointSummary();
    HardpointSummary& operator=(HardpointSummary const& Other);

    void clear();
    void destroy();
    int analyze_instance(long Instance);
    void expire_instance();

    int get_count() const;
    char const* get_name(int Index) const;
    HardpointInfo const& get_info(int Index) const;
    long get_instance(int Index) const;
    int find_name(char const* Name) const;
    long find_name_instance(char const* Name) const;
    HardpointInfo const* get_info_by_name(char const* Name) const;

private:
    void count_hardpoints(long Instance);
    void fill_hardpoints(long Instance);
    static void __cdecl count_hardpoints_callback(long Instance, void* Context);
    static void __cdecl fill_hardpoints_callback(long Instance, void* Context);
    static void __cdecl hardpoint_callback1(char const* Name, void* Context);
    static void __cdecl hardpoint_callback2(char const* Name, void* Context);

    // +0x00 is the vftable pointer (virtual destructor).
    int Count;                  // +0x04
    char const** Names;         // +0x08  array[Count] of malloc'd strings
    HardpointInfo* Infos;       // +0x0C  array[Count] of HardpointInfo
    long* Instances;            // +0x10  array[Count] of instance handles
    // +0x14/+0x18: scratch of fill_hardpoints()'s walk. fill_hardpoints_callback stores the
    // current instance and its archetype (part) handle; hardpoint_callback2 records the
    // instance in Instances[cursor] and queries the part's hardpoints. Both are -1 whenever
    // no fill is running (ctor / clear / analyze_instance's end).
    long FillInstance;           // +0x14  formerly FillIndex
    void* FillPart;              // +0x18  IEngine archetype handle; formerly CachedInstance
    int FillCursor;              // +0x1C  array-fill cursor, 0..Count-1
};

// freelancer.exe FUN_00457f10 allocates operator new(0x20) for one.
static_assert(sizeof(HardpointSummary) == 0x20, "HardpointSummary sizeof 0x20");

// ---------------------------------------------------------------------------
// MissionVendorProperties / NewsBroadcastProperties / CharacterProperties
// Network-serializable property bags. Use FmtStr (common_text.h).
// ---------------------------------------------------------------------------

// Property bag description structs
struct MissionVendorOfferDescription
{
    // Content.dll FUN_06f04b10 (06f04b10-06f04b89, thiscall; the end of each loop pass of
    // FUN_06f047e0 at 06f049cf and its EH state 5 funclet 06faccc2): FmtStr::destroy on +0x10,
    // +0x28, +0x40, then the members' ~FmtStr (+0x40, +0x28, +0x10).
    ~MissionVendorOfferDescription()
    {
        Title.destroy();
        Description.destroy();
        Briefing.destroy();
    }

    // +0x00. Renamed 2026-09-24 from MissionType: init() copies it to
    // MissionVendorProperties+0x04, which Server.dll's BaseDB::create_mission_vendor
    // (FUN_06d14dc0, .text:06d14dcb `MOV EAX,[EDI+0x4]`) looks up in the BASE map.
    unsigned BaseId;
    unsigned TargetId;
    unsigned SystemId;
    unsigned MissionClass;
    FmtStr Title;
    FmtStr Description;
    FmtStr Briefing;
    unsigned IconTextureId;
    // +0x5c. pub::GF::MissionVendorOfferCreate reads it (.text:06d1834a
    // `MOV EAX,[ESI+0x5c]`) and hands it to BaseDB as the record's session
    // filter (0 = every session). init() does not touch it.
    unsigned Filter;
};

struct NewsBroadcastDescription
{
    // +0x00. Renamed 2026-09-24 from IconId: init() copies it to
    // NewsBroadcastProperties+0x04, the key BaseDB::create_news_broadcast
    // (FUN_06d15140, .text:06d1514b) looks up in the BASE map.
    unsigned BaseId;
    bool bFlag1;
    bool bFlag2;
    unsigned Category;
    FmtStr Headline;
    FmtStr Text;
    FmtStr Audio;
    char const* LogoTexture;
    char const* AudioName;
    // +0x5c. pub::GF::NewsBroadcastCreate's session filter (.text:06d1846a
    // `MOV EAX,[ESI+0x5c]`); init() does not touch it.
    unsigned Filter;
};

struct CharacterDescription
{
    // NAMING NOTE (2026-09-17, Server.dll lane wave 8): this first dword is NOT
    // a system id. CharacterProperties::init copies it to Properties+0x08
    // (Common.dll .text:0632dc20), and Server.dll's FUN_06d14870 reads exactly
    // that field at .text:06d1487b and uses it as the key into BaseDB's
    // LOCATION tree (BaseDB+0x1c, the one BaseDB::find_location uses). It is a
    // room/location id, exactly like AmbientScriptDescription::RoomId above.
    // Renamed 2026-09-24 (with CharacterProperties' Key / SessionId / RoomId).
    unsigned RoomId;
    Costume Cost;
    // +0x38 / +0x44 / +0x4c renamed 2026-10-14 (were VoiceId / Field_0x44 / Field_0x4c):
    // init copies them to CharacterProperties' NameIds / BehaviorId / VoiceId
    // (.text:0632dc25, 0632dc95, 0632dc9e). Content's FBarCharacter::Create (06ea7d20) stores
    // the individual name into +0x38, -1 into +0x44 and the voice into +0x4c.
    unsigned NameIds;
    unsigned FactionId;
    char const* Script;
    unsigned BehaviorId;
    char const* Location;
    unsigned VoiceId;
    // +0x50. Added 2026-09-17: the struct used to stop at +0x4c, but retail's
    // pub::GF::CharacterCreate reads one dword past that --
    //   .text:06d17f6a  8b4650  MOV EAX,[ESI + 0x50]
    //   .text:06d17f6d  50      PUSH EAX
    // -- and hands it to FUN_06d14870, which passes it straight to FUN_06d120b0,
    // which stores it as the new node's SESSION FILTER at node+0x04
    // (.text:06d12205). FUN_06d11b50 tests that filter before every per-session
    // dispatch; 0 means "every session". CharacterProperties::init never touches
    // it, which is why it is a description field and not a properties field.
    unsigned Filter;
};

class COMMON_API MissionVendorProperties
{
public:
    MissionVendorProperties();
    MissionVendorProperties(MissionVendorProperties const& Other);
    ~MissionVendorProperties();
    MissionVendorProperties& operator=(MissionVendorProperties const& Other);

    void clear();
    void destroy();
    void init(MissionVendorOfferDescription const& Desc);
    void marshal(class MarshalBuffer* Buf) const;
    void unmarshal(class MarshalBuffer* Buf);
    unsigned long get_key() const;  // retail ?get_key@...@@QBEKXZ (K = unsigned long)
    char const* get_icon_texture() const;

    // Public since 2026-09-24: Server.dll's BaseDB stamps the handle into Key
    // (FUN_06d13070 .text:06d131c3 `MOV [EDI],ECX`) and keys the offer by
    // BaseId; retail has no setter or getter for either.
    int Key;                    // +0x00  default -1
    unsigned BaseId;            // +0x04  (was MissionType; see the description)
    unsigned TargetId;          // +0x08
    unsigned SystemId;          // +0x0C
    unsigned MissionClass;      // +0x10  1=destroy,2=destroyinstall,3=assassinate,...
    FmtStr Title;               // +0x14  sizeof 0x18 (6 DWORDs)
    FmtStr Description;         // +0x2C
    FmtStr Briefing;            // +0x44
    // +0x5C. The name is a guess nothing confirms: freelancer.exe's NN_MissionVendor reads it
    // as the reward in credits (0x0048ca94..0x0048cab7: nonzero -> FmtStr mad-lib 0x732 +
    // append_int(value), zero -> mad-lib 0x741). The icon comes from +0x10 instead.
    unsigned IconTextureId;
};

// 0x14 header + three FmtStr at 0x18 each = 0x5C, plus the icon id.
static_assert(sizeof(MissionVendorProperties) == 0x60, "MissionVendorProperties sizeof 0x60");

// Retail Common.dll class carrying a client's accept/decline response (with an optional
// FmtStr reason) to a mission vendor offer -- re-derived from its ctor/dtor/clear/destroy/
// marshal/unmarshal/operator= (Common.dll.c 162870-163088). Not to be confused with the
// unrelated free function pub::GF::MissionVendorAcceptance (server_pub.cpp), which sends
// this same data (handle/accepted/reason/session) over the wire but does not use this
// class's marshal format directly.
class COMMON_API MissionVendorAcceptance
{
public:
    MissionVendorAcceptance();
    MissionVendorAcceptance(MissionVendorAcceptance const& Other);
    ~MissionVendorAcceptance();
    MissionVendorAcceptance& operator=(MissionVendorAcceptance const& Other);

    void clear();
    void destroy();
    void marshal(class MarshalBuffer* Buf) const;
    void unmarshal(class MarshalBuffer* Buf);
    // 0x06261d90 ?get_key@MissionVendorAcceptance@@QBEKXZ: MOV EAX,[ECX] / RET (ICF-shared
    // with AmbientScriptProperties::get_key and other "return the first dword" getters).
    unsigned long get_key() const;

    // Public since 2026-09-24: Server.dll's FUN_06d13270 fills a local one
    // field by field (.text:06d1329f .. .text:06d132bc) before marshalling it.
    int Key;              // +0x00  default -1
    unsigned TargetId;     // +0x04
    bool bAccepted;        // +0x08
    FmtStr Reason;         // +0x0C  sizeof 0x18
};

static_assert(sizeof(MissionVendorAcceptance) == 0x24, "MissionVendorAcceptance sizeof 0x24");

class COMMON_API NewsBroadcastProperties
{
public:
    NewsBroadcastProperties();
    NewsBroadcastProperties(NewsBroadcastProperties const& Other);
    ~NewsBroadcastProperties();
    NewsBroadcastProperties& operator=(NewsBroadcastProperties const& Other);

    void clear();
    void destroy();
    void init(NewsBroadcastDescription const& Desc);
    void marshal(class MarshalBuffer* Buf) const;
    void unmarshal(class MarshalBuffer* Buf);
    unsigned long get_key() const;  // retail ?get_key@...@@QBEKXZ (K = unsigned long)
    char const* get_logo_texture() const;

    // Public since 2026-09-24 (Server.dll's BaseDB stamps Key, FUN_06d13350
    // .text:06d134a3, and keys the broadcast by BaseId).
    // Re-derived from retail's ctor/dtor/clear/destroy/marshal/unmarshal/init (Common.dll.c
    // 163092-163539): the previous field list here (SourceId/Priority/Flags as three plain
    // dwords, Headline at +0x14) did not match retail at all -- the real layout only has one
    // extra dword before the FmtStrs, split as two bools (mirroring NewsBroadcastDescription's
    // bFlag1/bFlag2) plus one dword, and there is a genuine 5th data member (a heap-owned
    // override string at +0x58, freed with malloc_strfree / copied with malloc_strcpy /
    // (un)marshalled with Put/GetString) that get_logo_texture() must consult -- previously
    // missing entirely, which is why get_logo_texture() always returned the "genericnews"
    // fallback and ignored any real per-broadcast logo texture.
    int Key;                    // +0x00  default -1
    unsigned BaseId;            // +0x04  (was NewsType; see the description)
    bool bFlag1;                // +0x08  from NewsBroadcastDescription::bFlag1
    bool bFlag2;                // +0x09  from NewsBroadcastDescription::bFlag2 (+2 bytes padding follows)
    unsigned Category;          // +0x0C  from NewsBroadcastDescription::Category
    FmtStr Headline;            // +0x10
    FmtStr Body;                // +0x28
    FmtStr Logo;                // +0x40
    char* LogoTexture;          // +0x58  heap-owned (malloc_strcpy/malloc_strfree); nullptr => fall back to "genericnews"
    unsigned LogoTextureId;     // +0x5C  CreateID() of NewsBroadcastDescription::AudioName
};

static_assert(sizeof(NewsBroadcastProperties) == 0x60, "NewsBroadcastProperties sizeof 0x60");

class COMMON_API CharacterProperties
{
public:
    CharacterProperties();
    ~CharacterProperties();
    CharacterProperties& operator=(CharacterProperties const& Other);

    void destroy();
    void init(CharacterDescription const& Desc);
    void marshal(class MarshalBuffer* Buf) const;
    void unmarshal(class MarshalBuffer* Buf);
    unsigned long get_key() const;  // retail ?get_key@...@@QBEKXZ (K = unsigned long)

    // Public since 2026-09-24: Server.dll's BaseDB writes these directly
    // (FUN_06d11ee0 / FUN_06d120b0 / FUN_06d14ac0); retail has no setters.
    // Key, SessionId, RoomId and BehaviorId renamed the same day from RepId,
    // BaseId, SystemId and Field5C:
    //   +0x00 the pool handle FUN_06d120b0 / FUN_06d11ee0 store (get_key()),
    //   +0x04 the owning player's session (FUN_06d11ee0 .text:06d11f51, matched
    //         by BaseDB::exit_location, FUN_06d143e0),
    //   +0x08 the room (FUN_06d14870 .text:06d1487b keys the LOCATION map by it),
    //   +0x5c the script-behaviour handle (FUN_06d14ac0 .text:06d14b5f).
    unsigned Key;               // +0x00
    unsigned SessionId;         // +0x04
    unsigned RoomId;            // +0x08
    // +0x0C / +0x10 / +0x58 renamed 2026-09-26 from ShipArchId / Rank / Affiliation: init
    // fills them from CharacterDescription's VoiceId / FactionId / Script, and freelancer.exe's
    // CharacterObject reads +0x0C as the ids of the name it shows (FUN_00444290 0x004447f6,
    // FUN_0041c770), +0x10 as the faction marked met on a click (FUN_00445c10 0x00445c32 and
    // FUN_00445720 0x004459ee), and +0x58 as the idle script it plays (0x00444812).
    unsigned NameIds;           // +0x0C
    unsigned FactionId;         // +0x10
    wchar_t* NameWide;          // +0x14  wide string
    // +0x1c / +0x20 / +0x60 renamed 2026-10-14 from Field1C / Field20 / Field60. Server's
    // BaseDB stores CharType 1 for a player (.text:06d1201a) and 0 for a placed character
    // (.text:06d121fb), with the placement's posture (.text:06d12202); freelancer.exe's
    // RoomControl matches both against the placement's char type and posture, and
    // CharacterObject treats CharType 1 as the shared-deform player. VoiceId is
    // CreateID(voice) on the server (.text:06d12005) and Content's voice on a bar character.
    char* NameAnsi;             // +0x18  heap-owned (malloc_strcpy); the character-placement name
    unsigned CharType;          // +0x1C
    unsigned Posture;           // +0x20
    Costume Cost;               // +0x24  4+8+1 dwords = 0x34 bytes
    char* Script;               // +0x58  heap-owned (malloc_strcpy); the idle script (Thorn file)
    unsigned BehaviorId;        // +0x5C
    unsigned VoiceId;           // +0x60
};

// AmbientScriptDescription — input to pub::GF::AmbientScriptCreate.
// Retail: ?AmbientScriptCreate@GF@pub@@YAKABUAmbientScriptDescription@@@Z (Server.dll 0x384e0).
// Layout re-derived from AmbientScriptProperties::init (Common.dll.c 163688-163710): init()
// reads offsets +0x00/+0x04/+0x08 as three plain dwords and +0x0c as a raw `char const*` (NOT
// an FmtStr -- that field was previously misnamed/mistyped as one). Separately,
// pub::GF::AmbientScriptCreate itself reads one dword at +0x10 and passes it on as the
// record's session filter (see the field). No field beyond it is read by anyone.
// Lives here (not common_server.h) because it pairs with the other pub::GF
// property-bag classes below.
struct AmbientScriptDescription
{
    unsigned RoomId;      // +0x00  BaseDB room/location key (BaseDB::find_location)
    unsigned ScriptId;    // +0x04
    unsigned Flags;       // +0x08  only the low byte is read (AmbientScriptProperties::init)
    char const* Name;     // +0x0C
    // +0x10. CORRECTED 2026-09-24: this is not an FmtStr. pub::GF::AmbientScriptCreate
    // reads one dword here (.text:06d1851a `MOV EAX,[ESI+0x10]`) and FUN_06d15390 /
    // FUN_06d12520 store it as the new record's session filter (record+0x04,
    // .text:06d1267f), the value FUN_06d11b50 compares against each session id.
    unsigned Filter;
};

// Common.dll.c 163560-163710 (ctor 0xbb7a0, dtor/destroy 0xbb7c0, clear 0xbb7f0,
// marshal 0xbb810, unmarshal 0xbb880, operator= 0xbb8e0, init 0xbb930). Backs
// Server.dll's pub::GF::AmbientScriptCreate (see server_pub.cpp / BaseDB::create_ambient_script).
// Same small "network property bag" shape as MissionVendorProperties/NewsBroadcastProperties
// above, just simpler: no FmtStr members -- Name is a plain heap string copied out of
// AmbientScriptDescription::Name's raw head pointer.
class COMMON_API AmbientScriptProperties
{
public:
    AmbientScriptProperties();
    ~AmbientScriptProperties();
    AmbientScriptProperties& operator=(AmbientScriptProperties const& Other);

    void clear();
    void destroy();
    void init(AmbientScriptDescription const& Desc);
    void marshal(class MarshalBuffer* Buf) const;
    void unmarshal(class MarshalBuffer* Buf);
    unsigned long get_key() const;  // retail ?get_key@...@@QBEKXZ (K = unsigned long)

    // Re-derived from retail's ctor/dtor/clear/operator=/init (Common.dll.c 163560-163710).
    // Key defaults to -1 (never constructed via a description); init() resets it to 0 instead
    // of -1 (a real, deliberate difference in retail -- verified byte-for-byte), since the
    // handle id itself isn't known until the object is copied onto the heap and linked into
    // its room's ambient-script list by the registration helper (Server.dll FUN_06d12520,
    // FRoomEntry::create_ambient_script), which overwrites Key with the allocated handle
    // by a direct store (.text:06d1266f CALL init / .text:06d1267d MOV [EDI],ECX with
    // EDI = &Props): the members are public, there is no setter.
    int Key;                // +0x00  default -1; 0 right after init(); handle id once registered
    unsigned RoomId;        // +0x04  from AmbientScriptDescription::RoomId
    unsigned ScriptId;      // +0x08  from AmbientScriptDescription::ScriptId
    bool bFlag;             // +0x0C  from AmbientScriptDescription::Flags (low byte); default true
    char* Name;             // +0x10  heap-owned (malloc_strcpy/malloc_strfree)
};

static_assert(sizeof(AmbientScriptProperties) == 0x14, "AmbientScriptProperties sizeof 0x14");

// ---------------------------------------------------------------------------
// AmbientScriptInfo -- retail Common.dll exports four members and nothing else
// (ctor 0x06274130, dtor and destroy 0x06274140 -- one body, clear 0x062ee230).
// No module of the retail game imports any of them (grep of every EXE\Disasm
// listing), and inside Common.dll the ctor/dtor/destroy have no caller at all.
// The two in-DLL CALLs to 0x062ee230 (.text:0628bda8 in
// PhySys::CalcClosestPointOnSynapse, .text:062ed9d0 in the SystemManager ambient
// gate) are /OPT:ICF folds of other types' "zero three dwords" members onto this
// address: the first zeroes a Vector, the second a 3-int flag buffer whose +4
// receives 1/2/3 -- not an owned string -- so neither object is an
// AmbientScriptInfo.
//
// Layout: three dwords -- the one record of a room's "scene =" line. RoomData keeps
// them behind pointers in its +0x1b4 vector: FUN_06279920 news 0xC bytes and zeroes the
// three dwords (the ctor inlined), read_ambient_script (062753d9..062753e4) writes the
// type to +0x00, malloc_strcpy's the script to +0x04 and stores the priority id at +0x08,
// and the purge FUN_062798b0 frees +0x04 then the node (the dtor). freelancer.exe reads
// the three fields directly (RoomControl scene start, FUN_0044a54d..0x0044a65f).
// ---------------------------------------------------------------------------
struct COMMON_API AmbientScriptInfo
{
    AmbientScriptInfo();
    ~AmbientScriptInfo();
    // Retail exports no operator= (dumpbin EXE\Common.dll: ctor, dtor, clear, destroy only),
    // and nothing in src/ assigns an AmbientScriptInfo. Without this the implicit copy
    // assignment of a COMMON_API struct is exported. The implicit copy ctor is not exported
    // (absent from dumpbin bin\Common.dll), so it is left as is.
    AmbientScriptInfo& operator=(AmbientScriptInfo const&) = delete;

    void clear();
    void destroy();

    int Type;               // +0x00  1 = ambient, 2 = traffic (obsolete), 3 = once
    char* Path;             // +0x04  the .thn script (malloc_strcpy; malloc_strfree in destroy)
    unsigned Priority;      // +0x08  CreateID(priority token), 0 without one; RtcSlider gate
};

static_assert(sizeof(AmbientScriptInfo) == 0x0c, "AmbientScriptInfo sizeof 0x0c");

// ---------------------------------------------------------------------------
// CostumeHint / BaseHint — serializable hint structs
// ---------------------------------------------------------------------------
// Common.dll.c 0xb11e0/0xb11f0: BaseHint's own ctor/copy-ctor only ever touch the
// vtable ptr and this one dword at +0x04 -- confirmed BOTH by BaseHint's own copy
// ctor (`*(this+4) = *(param+4)`) AND by CostumeHint's copy ctor (0xb1280), which
// inlines BaseHint's copy first (`this+4 = param+4`) before looping its own 13
// dwords starting at +8. So BaseHint is sizeof 8 (vtable + one dword), and
// CostumeHint : public BaseHint puts CostumeHint's own Data[13] at +0x08, not +0x04.
struct COMMON_API BaseHint
{
    BaseHint();
    BaseHint(BaseHint const& Other);
    virtual ~BaseHint();
    BaseHint& operator=(BaseHint const& Other);

    // ??_7BaseHint@@6B@ (.rdata:063a3700): slot 0 = scalar deleting dtor (06311210),
    // slots 1..3 = 06391fb0 `JMP [->MSVCRT::_purecall]`. Order matches
    // ??_7CostumeHint@@6B@ (.rdata:063a3714): Serialize, UnSerialize, CleanUp.
    virtual unsigned Serialize(unsigned char** OutBuf) = 0;
    virtual void UnSerialize(unsigned char* Buf, unsigned Size) = 0;
    virtual void CleanUp(unsigned char* Buf) = 0;

    // Retail's own BaseHint ctor/copy-ctor never initialize this (copy ctor only
    // copies whatever the source has); CostumeHint::CostumeHint() (0xb1370) sets it
    // to 1 explicitly, consistent with a type-tag/kind discriminator for the
    // BaseHint-derived hint family (CostumeHint is the only known subclass).
    unsigned Kind;              // +0x04
};

static_assert(sizeof(BaseHint) == 0x08, "BaseHint sizeof 0x08 (vtable + Kind)");
static_assert(offsetof(BaseHint, Kind) == 0x04, "BaseHint+0x04 kind");

struct COMMON_API CostumeHint : public BaseHint
{
    CostumeHint();
    CostumeHint(CostumeHint const& Other);
    virtual ~CostumeHint();
    CostumeHint& operator=(CostumeHint const& Other);

    unsigned Serialize(unsigned char** OutBuf) override;
    void UnSerialize(unsigned char* Buf, unsigned Size) override;
    void CleanUp(unsigned char* Buf) override;

    // The hinted costume (was `unsigned Data[13]`, retyped 2026-10-14). Every member treats it
    // as one: the ctor (06311370) is the inline Costume ctor (part ids and count zeroed), the
    // copy ctor (06311280) copies all thirteen dwords, operator= (063112b0) is Costume's
    // count-bounded operator=, and Serialize / UnSerialize move exactly sizeof(Costume) = 0x34
    // bytes. Content's FUN_06eba720 fills it from a dialog Costume.
    Costume Cost;               // +0x08..+0x3B
};

static_assert(sizeof(CostumeHint) == 0x3C, "CostumeHint sizeof 0x3C (BaseHint + Costume)");
static_assert(offsetof(CostumeHint, Cost) == 0x08, "CostumeHint+0x08 costume");

// ---------------------------------------------------------------------------
// CBase — lightweight CObject-holding base. Retail ctor/copy-ctor (0x48250/0x48260)
// only ever touch this+4 (Object); sizeof is vtable+Object == 8 bytes, confirmed by
// IObjInspectImpl (common_iface.h) embedding it inline at +0x0C (CBase vtable) /
// +0x10 (CObject* delegate) with no room for anything past Object. advise()/
// notify_of_destruction() are real CBase methods but retail ICF-folds them to a
// shared empty-body stub (ICF group @ 0x8d730) — ports as no-ops.
struct COMMON_API CBase
{
    CBase();
    CBase(CBase const& Other);
    virtual ~CBase();
    CBase& operator=(CBase const& Other);

    void advise(bool bFlag);
    void cobject(CObject* Obj);
    CObject* cobject() const;
    long get_index() const;  // retail ?get_index@CBase@@QBEJXZ (J = long)
    void notify_of_destruction(void* Ptr);

    CObject* Object;            // +0x04
};

struct IObjRW;

// ---------------------------------------------------------------------------
// BaseWatcher — intrusive watcher node for Watchable objects
// ---------------------------------------------------------------------------
// Retail Common.dll exports exactly five BaseWatcher members: ??0 (default), ??1, ??4, set,
// set_pointer. Its other members below (the IObjRW* / Watchable* const* ctors, target(),
// get_watched_id()) are unexported retail code (FUN_062c2920 / FUN_062ed070 / FUN_062c2ab0) or
// inline, so the export is function-level, not class-level.
struct BaseWatcher
{
    COMMON_API BaseWatcher();
    // Common.dll.c FUN_062c2920 (0x62920): retail overload that watches Target's
    // Watchable sub-object (at IObjRW+8) immediately, in one step. Used both as the
    // per-element constructor for ScanList's BaseWatcher[256] array (called with a
    // null Target there, degenerating to the default ctor) and directly elsewhere
    // (e.g. 0x062d47bb) with a real IObjRW*.
    explicit BaseWatcher(IObjRW* Target);
    // Common.dll.c FUN_062ed070 (0x62ed070): a second constructor overload that
    // dereferences a Watchable* const* directly (no +8 IObjRW adjustment, unlike
    // the IObjRW* overload above) and additionally guards against a literal 0x8
    // sentinel value alongside null. Verified against raw disasm (Common.dll.txt
    // 253086-253125): zero Watched/Next, then `Watchable* V = *Src; if (V != nullptr
    // && V != (Watchable*)8) set_pointer(V);`. Sole call site (0x062e8720) sits in a
    // still-unprocessed cluster, but the constructor's own logic is fully verified
    // regardless of the caller.
    explicit BaseWatcher(Watchable* const* Src);
    COMMON_API ~BaseWatcher();
    COMMON_API BaseWatcher& operator=(BaseWatcher const& Other);

    COMMON_API void set(Watchable const* Target);
    Watchable const* target() const { return Watched; }
    // Common.dll.c FUN_062c2ab0 (0x62ab0): 0 if nothing watched, else the watched
    // IObjRW's unique id (vtable slot +0x20, the same "GetId" slot used throughout
    // this cluster -- see CEScanner::add_obj / ScanList::add_in_range).
    unsigned int get_watched_id() const;

protected:
    COMMON_API void set_pointer(Watchable const* Target);

private:
    // Watchable::unwatch (.text:063415e0) walks the chain and clears both fields of every node.
    friend struct Watchable;

    Watchable const* Watched;   // +0x00
    BaseWatcher* Next;          // +0x04  next in watcher chain
};

static_assert(sizeof(BaseWatcher) == 0x08, "BaseWatcher sizeof 0x08 (two 4-byte pointers)");

// ---------------------------------------------------------------------------
// EquipDesc — sizeof 0x20. ctor 0xa9ae0. Hardpoint "BAY" = internal cargo.
// ---------------------------------------------------------------------------
struct COMMON_API EquipDesc
{
    EquipDesc();
    // 06263c60 -- a plain member-by-member copy. Worth having explicitly because retail
    // exports it, and because the copy reads +0x00 as a BYTE and +0x02 as a WORD, which is
    // how we know Cached is one byte with a pad behind it rather than a short.
    EquipDesc(EquipDesc const& Other);
    // 06263ca0 -- retail exports an explicit operator= too, but it's the same plain
    // member-by-member copy as the copy ctor (no owned resources here to worry about).
    EquipDesc& operator=(EquipDesc const& Other) = default;

    // ?CARGO_BAY_HP_NAME@EquipDesc@@2UCacheString@@B (.data:063fcf0c): the hardpoint name
    // of the internal cargo bay. Set by the static initialiser at .text:06309bc0
    // (StringAlloc("BAY", false)); read by the ctor, make_internal, is_internal,
    // get_cargo_space_occupied and CECargo::Init.
    static CacheString const CARGO_BAY_HP_NAME;

    // 06263be0/06263c00/06263c20/06263c40 -- retail compares only the Id field for all
    // four relational operators.
    bool operator==(EquipDesc const& Other) const { return Id == Other.Id; }
    bool operator!=(EquipDesc const& Other) const { return Id != Other.Id; }
    bool operator<(EquipDesc const& Other) const { return Id < Other.Id; }
    bool operator>(EquipDesc const& Other) const { return Id > Other.Id; }

    unsigned short get_id() const;
    CacheString const& get_hardpoint() const;
    // ADDED 2026-09-17 (wave 14). Retail exports all six of these by name and
    // the port had none of them; each one is a single load off the offset named
    // in its comment, read from Common.dll's own listing:
    //   ?get_arch_id@EquipDesc@@QBEIXZ    0xa9ad0 -> [this+0x04]
    //   ?is_equipped@EquipDesc@@QBE_NXZ   0x3b90  -> [this+0x0c] (byte)
    //   ?get_count@EquipDesc@@QBEHXZ      0x3bb0  -> [this+0x14]
    //   ?set_temporary@EquipDesc@@QAEX_N@Z 0x3bd0 -> [this+0x18] = arg (byte)
    //   ?get_owner@EquipDesc@@QBEHXZ      0x29e0  -> [this+0x1c]
    //   ?set_owner@EquipDesc@@QAEXH@Z     (same one-line shape)
    // Several of those addresses are /OPT:ICF folds shared with unrelated
    // classes; the MANGLED NAME is what binds each body to EquipDesc, not the
    // address, so the fold does not make the receiver ambiguous.
    unsigned get_arch_id() const;
    unsigned char is_equipped() const;
    int get_count() const;
    void set_temporary(bool bOn);
    int get_owner() const;
    void set_owner(int NewOwner);
    void set_id(unsigned short NewId);
    void set_arch_id(unsigned Arch);
    // 0xdbad0 ?set_count@EquipDesc@@QAEXH@Z -- `MOV EAX,[ESP+4] ; MOV [ECX+0x14],EAX ;
    // RET 4`, ICF-folded onto several other one-line "[this+0x14] = arg" setters. Added
    // 2026-09-17 because retail rpclocal.dll imports it by name for ch2 op 0x55
    // FLPACKET_SERVER_OBJECTCARGOUPDATE (rpclocal IAT .rdata:06db3084); it was the only
    // one of that receiver's seven EquipDesc entry points the port was missing.
    void set_count(int NewCount);
    void set_hardpoint(CacheString const& Hp);
    void set_equipped(unsigned char bOn);
    float get_status() const;
    void set_status(float HealthValue);
    void make_internal();
    bool is_internal() const;
    bool get_temporary() const;
    // 0xa9b90 ?get_cargo_space_occupied@EquipDesc@@QBEMXZ (Common.dll). Only counts
    // toward cargo space when the item sits in the internal cargo-bay hardpoint;
    // externally-mounted equipment (guns, shields, etc.) always returns 0. Returns double
    // (retail mangling M = float): the FILD Count * FMUL Volume product is left unrounded
    // in ST0 and freelancer.exe's callers consume it there (0x0047dd88 ftol, 0x004c1ca7
    // FSTP double).
    double get_cargo_space_occupied() const;

    unsigned short Cached;          // +0x00
    unsigned short Id;              // +0x02
    unsigned ArchId;                // +0x04
    char const* Hardpoint;          // +0x08  CacheString
    unsigned char bEquipped;        // +0x0C; native raw BYTE, preserved by RPC
    float Health;                   // +0x10  default 1
    int Count;                      // +0x14  default 1
    bool bMission;                  // +0x18
    unsigned Owner;                 // +0x1C
};

static_assert(sizeof(EquipDesc) == 0x20, "EquipDesc sizeof 0x20");

// Retail Common.dll exports every member of this STL instantiation (VC6 std::list, 55
// members, imported by name by FreeLancer.exe, Server.dll and rpclocal.dll). The port
// exports the modern MSVC instantiation instead: the definition is in common_equipdesc.cpp; other modules
// import it through this declaration. It must precede the first use that would implicitly
// instantiate the class, which is why it sits directly after the element type.
#ifndef COMMON_EXPORTS
extern template class COMMON_API std::list<EquipDesc>;
#endif
// Retail Common.dll exports every member of this STL instantiation (VC6 std::vector, 47
// members, imported by name by FreeLancer.exe, Server.dll and rpclocal.dll). The port
// exports the modern MSVC instantiation instead: the definition is in common_equipdesc.cpp; other modules
// import it through this declaration. It must precede the first use that would implicitly
// instantiate the class, which is why it sits directly after the element type.
#ifndef COMMON_EXPORTS
extern template class COMMON_API std::vector<EquipDesc>;
#endif

// Retail VC6 vector: allocator byte + 3 pointers. Stride 0x20. Not std::vector.
// Default ctor (0x06266310, ??0EquipDescVector@@QAE@XZ) just zeroes all four dwords;
// retail's linker /OPT:ICF-folded it onto the identical weighted_vector<unsigned>
// default ctor (??0?$weighted_vector@I@@QAE@XZ) since both are "zero four dwords".
struct COMMON_API EquipDescVector
{
    EquipDescVector();
    // 06263cf0 -- deep copy: allocates a buffer sized to Other's element count and copies
    // each EquipDesc by value.
    EquipDescVector(EquipDescVector const& Other);

    // 0x06269110 ??1EquipDescVector@@QAE@XZ (ICF-shared with the VC6 vector dtors and
    // ??1?$weighted_vector@I@@QAE@XZ): operator delete(Begin), then Begin/End/Cap = 0.
    // The buffer always comes from ::operator new (this struct's own members, Server's
    // pub::GetLoadout, rpclocal's RpcEquipVectorFromHandle / push-back).
    ~EquipDescVector();

    // 0x06263d90 -- VC6 vector assignment: reuses the buffer when Other fits in size()
    // or capacity() (element copy, End = Begin + n), otherwise frees it and copies into
    // an exactly-sized new buffer. AllocPad (+0) is not copied.
    EquipDescVector& operator=(EquipDescVector const& Other);

    // 0xa9e40 -- builds a vector holding a copy of every item in a List, in the
    // List's own (insertion) order.
    explicit EquipDescVector(EquipDescList const& Other);

    // 0xa9be0 -- appends a copy of every element of Other after this vector's
    // existing elements (reallocating if needed).
    void append(EquipDescVector const& Other);

    // 0xaa0b0. When bCombinable and an existing element matches Desc's
    // ArchId+bMission+Owner+Hardpoint, folds Desc into it (Health-aware: a
    // full-health existing element just absorbs Desc's Count and takes Desc's
    // Health; otherwise Health/Count are summed, rolling over into "-1 count,
    // -Health 1.0" once combined Health reaches 1.0) and returns 1. Otherwise
    // appends Desc as a new element and returns 0.
    int add_equipment_item(EquipDesc const& Desc, bool bCombinable);

    // 0xaa350 ?traverse_equipment_type@EquipDescVector@@QAEPAUEquipDesc@@IPBU2@@Z.
    // Linear scan for the element whose ArchId == Type. Pass Prev == nullptr to get
    // the first match; pass a previously-returned pointer as Prev to get the NEXT
    // match after it (retail: once the scan reaches the element == Prev, it clears
    // its internal "looking for this one" flag and keeps scanning for the next hit).
    // Returns nullptr once no further match exists.
    EquipDesc* traverse_equipment_type(unsigned Type, EquipDesc const* Prev);

    unsigned AllocPad;   // +0x00
    EquipDesc* Begin;    // +0x04
    EquipDesc* End;      // +0x08
    EquipDesc* Cap;      // +0x0C
};

static_assert(sizeof(EquipDescVector) == 0x10, "EquipDescVector sizeof 0x10");
static_assert(offsetof(EquipDescVector, Begin) == 4, "EquipDescVector+4 begin");

// ---------------------------------------------------------------------------
// EquipDescList — retail Common.dll class, imported by freelancer.exe as a
// single client-side static singleton (freelancer.exe FUN_004c3ba0/LAB_004c3bc0
// construct/destroy it at static init; FUN_004c4170/FUN_004c4260 call
// add_equipment_item/find_equipment_item on it). Retail's container is a VC6
// std::list<EquipDesc>, and so is the port's (current-CRT std::list, not the VC6
// node layout). The list matters: element addresses never move, and callers keep
// raw EquipDesc pointers into it across inserts and erases of other elements --
// freelancer.exe NN_Trade holds its selected item (+0x428, and CTradeEquipList
// +0x58) while it adds to the same list (AddTargetItem 0x004c2380), and the trade
// rows (CTradeEquipListLine +0x24) point into it between refreshes.
// ---------------------------------------------------------------------------
// A class in retail: Common.dll exports ??0EquipDescList@@QAE@ABV0@@Z and
// ?append@EquipDescList@@QAEXABV1@@Z, Server.dll/rpclocal ?ReqCargo@IServerImpl@@UAEXABVEquipDescList@@I@Z.
class COMMON_API EquipDescList
{
public:
    EquipDescList();
    // 06263f40/06263f70 -- retail's copy ctor deep-copies its VC6 std::list<EquipDesc>
    // node-by-node. std::list<EquipDesc>'s own copy ctor does the same deep copy in
    // the same order, so the implicit memberwise copy matches; declared `= default` explicitly
    // since EquipDescList also declares a destructor.
    EquipDescList(EquipDescList const& Other) = default;
    ~EquipDescList();

    // Retail exports no clear(): callers empty the list with inlined std::list code
    // (freelancer.exe FUN_004c4070, .text:004c4071-004c40ac), which is Items.clear() here.

    // 0xaa4d0 ?add_equipment_item@EquipDescList@@QAEHABUEquipDesc@@_N@Z. When
    // bCombinable and an existing element matches Desc's
    // ArchId+bMission+Owner+Hardpoint, folds Desc into it (same Health-aware
    // rule as EquipDescVector::add_equipment_item) and returns 1; otherwise
    // appends a copy of Desc verbatim (retail does NOT assign it a fresh Id
    // here -- Desc.Id is copied as-is) and returns 0. Does not touch NextId.
    int add_equipment_item(EquipDesc const& Desc, bool bCombinable);

    // Linear search by Id; returns nullptr if not found.
    EquipDesc* find_equipment_item(unsigned short Id);
    EquipDesc const* find_equipment_item(unsigned short Id) const;

    // 0xaa6e0 ?find_equipment_item@EquipDescList@@Q[AB]EP[AB]UEquipDesc@@ABUCacheString@@@Z
    // (const/non-const overloads ICF-folded onto one retail body). Linear search by
    // Hardpoint identity: retail compares the node's Hardpoint pointer directly
    // against Hp.Value (CacheString values are interned in retail, so pointer
    // identity is meaningful there); the port's Hardpoint field already stores that
    // same kind of pointer (see EquipDesc::is_internal's CARGO_BAY_HP_NAME pointer
    // comparison), so this is a faithful 1:1 port, not the content-based _stricmp
    // used by add_equipment_item's separate combinable-merge matching rule.
    EquipDesc* find_equipment_item(CacheString const& Hp);
    EquipDesc const* find_equipment_item(CacheString const& Hp) const;

    // 0xaa710 (non-const) / 0xaa750 (const) ?traverse_equipment_type@EquipDescList@@...
    // Same "first match, or next match after Prev" semantics as
    // EquipDescVector::traverse_equipment_type, over Items instead of a raw buffer.
    EquipDesc* traverse_equipment_type(unsigned Type, EquipDesc const* Prev);
    EquipDesc const* traverse_equipment_type(unsigned Type, EquipDesc const* Prev) const;

    // 0xaa790 ?find_matching_cargo@EquipDescList@@QBEPBUEquipDesc@@IHM@Z. Looks for
    // an existing cargo item of Type to combine into (or repair-match against):
    //  - if arch_is_combinable(Type): returns the first non-mission item of Type
    //    whose Count is already >= NeedCount, or nullptr.
    //  - otherwise: returns the non-mission, non-equipped item of Type whose Health
    //    is closest to (but not over) TargetHealth (smallest TargetHealth - Health,
    //    capped at retail's 2.0f "nothing found yet" sentinel), or nullptr.
    EquipDesc const* find_matching_cargo(unsigned Type, int NeedCount, float TargetHealth) const;

    // 0xaa660 ?remove_equipment_item@EquipDescList@@QAEHGH@Z. CountToRemove == -1
    // removes the whole item; otherwise Count -= CountToRemove and Health resets
    // to a full 1.0. Returns -1 if Id isn't found, 0 if Count reached 0 (item
    // erased), 1 if the item still has Count left.
    int remove_equipment_item(unsigned short Id, int CountToRemove);

    // 0xaa8e0 ?get_cargo_space_occupied@EquipDescList@@QBEMXZ (Common.dll). Sum of
    // EquipDesc::get_cargo_space_occupied() over every item (retail walks its VC6
    // list inline; the port just folds over Items).
    float get_cargo_space_occupied() const;

    // 0xaa460 -- appends a copy of every element of Other, in Other's order.
    void append(EquipDescList const& Other);

    // 0xaa380 -- builds a list holding a copy of every item in a Vector, in the
    // vector's order.
    explicit EquipDescList(EquipDescVector const& Other);

    // Retail's only member is a VC6 std::list<EquipDesc> (0x0c bytes: allocator byte,
    // head, size -- freelancer.exe DAT_00672960/64/68). There is no id counter.
    // Walk it with iterators; element pointers stay valid until that element is erased.
    std::list<EquipDesc> Items;
};

// ---------------------------------------------------------------------------
// GoodDesc / GoodDescList — retail Common.dll GOODS batch (trade negotiation
// item records; consumed by TradeResponseInfo below and MarshalBuffer).
//
// GoodDesc is a flat 0x10-byte record (ctor 0x1bdc0, ??0GoodDesc@@QAE@XZ):
// defaults {0, -1, 1.0f, 1.0f}. Field semantics recovered from add_good_item
// (Common.dll 0xaa9d0): GoodId is the match key when combining two entries,
// Count accumulates on combine. Price/Field0C are never observed set
// independently of each other in any traced caller; named conservatively.
//
// Retail's type hierarchy: MetaList<GoodDesc> (non-owning) <- OwnerList<GoodDesc>
// (frees each Data, .text:0627ca20) <- GoodDescList (no members of its own). All
// three are the templates in common_metalist.h; Common.dll exports the GoodDesc
// instantiations of MetaList and OwnerList.
// ---------------------------------------------------------------------------
struct COMMON_API GoodDesc
{
    // User-declared so that no implicit move members are generated and exported:
    // VC6 had none, and retail exports exactly the copy assignment.
    GoodDesc& operator=(GoodDesc const&) = default;

    GoodDesc() : GoodId(0), Count(-1), Price(1.0f), Field0C(1.0f) {}

    // ??2/??3GoodDesc: class-scope allocation, a bare jump to msvcrt malloc/free
    // (listing 0x9d560 / 0x9d570, ICF-shared with CEquip's).
    static void* operator new(size_t Size);
    static void operator delete(void* Ptr);

    unsigned GoodId;   // +0x00  default 0; match key for add_good_item's combine check
    int Count;         // +0x04  default -1; accumulated by add_good_item when combining
    float Price;       // +0x08  default 1.0
    float Field0C;     // +0x0C  default 1.0 -- uncertain; always tracks Price's default
};

static_assert(sizeof(GoodDesc) == 0x10, "GoodDesc sizeof 0x10");

// OwnerList<GoodDesc>::free releases each element through ??3GoodDesc (.text:0627be93).
template <>
inline void OwnerListDeleteData<GoodDesc>(GoodDesc* Data)
{
    delete Data;
}

// Importing modules take both instantiations from Common.dll; inside Common the explicit
// instantiation definitions in common_goodinfo.cpp export them (dllexport + extern is C4910).
#ifndef COMMON_EXPORTS
extern template class COMMON_API MetaList<GoodDesc>;
extern template class COMMON_API OwnerList<GoodDesc>;
#endif

struct COMMON_API GoodDescList : public OwnerList<GoodDesc>
{
    // .text:0627be30: OwnerList<GoodDesc>() inlined, then the GoodDescList vptr.
    GoodDescList();

    // .text:0630e350: bUseHeap copied from Other, then a copy of every GoodDesc of
    // Other appended.
    GoodDescList(GoodDescList const& Other);

    // .text:0630e450: appends a copy of every GoodDesc of Other WITHOUT clearing this
    // list first, and returns a const reference.
    GoodDescList const& operator=(GoodDescList const& Other);
};

// 0xaa9d0. Adds a GoodDesc{GoodId, Count, Price, Field0C} to List, merging into an
// existing entry with the same GoodId (accumulating Count) when bCombinable is set
// and a match exists. Returns 1 on merge, 0 on a fresh append.
COMMON_API int __cdecl add_good_item(unsigned int GoodId, int Count, float Price, float Field0C,
                                     bool bCombinable, GoodDescList* List);
// 0xaaab0. Finds the entry with GoodId in List and subtracts Count from it (Count
// of -1 zeroes it outright); once its count reaches zero the entry is unlinked and
// freed. Returns -1 if not found, 1 if the entry survives with a reduced count, 0
// if it was removed.
COMMON_API int __cdecl remove_good_item(unsigned int GoodId, int Count, GoodDescList* List);

// 0xaa990. Finds the entry with GoodId in List. Retail keeps overwriting its result on
// every match while walking the whole list rather than stopping at the first hit, so
// this returns the LAST matching node/GoodDesc, not the first. Optionally reports the
// owning node via OutNode (left untouched if List has no match and OutNode is null;
// set to nullptr on no match otherwise). Does not null-check List, matching retail.
COMMON_API GoodDesc* __cdecl find_good_item(GoodDescList* List, unsigned int GoodId,
                                            MetaNode<GoodDesc>** OutNode);

// ---------------------------------------------------------------------------
// TradeResponseInfo — retail Common.dll GOODS batch network message
// (Common.dll.c ~0x1bf70..0x1c400). Used by IServerImpl::TradeResponse
// (retail 0x65840): unmarshals a wire buffer into one of these, holding the
// two sides of a trade negotiation (goods offered / goods requested).
//
// Field00/Field04/bFlag08 are marshaled (retail's marshal(): put_void x2 then
// put_bool) but Field04 is notably left uninitialized by the default
// constructor in retail (0x1bfe0) -- only Field00/bFlag08 are zeroed there,
// matching a retail quirk we replicate rather than "fix". Named
// conservatively since no caller in Common.dll sets them with a named
// accessor (real semantics -- likely requester/responder session ids and an
// accept/reject flag -- live in the Server-side trade logic, out of scope
// for Common.dll).
// ---------------------------------------------------------------------------
class COMMON_API TradeResponseInfo
{
public:
    // .text:0627bfe0: Field00 and bFlag08 zeroed, Field04 not written, both lists
    // default-constructed.
    TradeResponseInfo() : Field00(0), bFlag08(false) {}
    // .text:0627bf70: memberwise -- the three scalars, then GoodDescList's copy
    // constructor for each list.
    TradeResponseInfo(TradeResponseInfo const& Other) = default;
    // .text:0627c010: destroy() inlined, then the two GoodDescList destructors.
    ~TradeResponseInfo() { destroy(); }

    // .text:0627c4c0: destroy() inlined, the three scalars copied, then
    // GoodDescList::operator= (which appends) for each list. No self-assignment
    // check.
    TradeResponseInfo& operator=(TradeResponseInfo const& Other)
    {
        destroy();
        Field00 = Other.Field00;
        Field04 = Other.Field04;
        bFlag08 = Other.bFlag08;
        OfferedGoods = Other.OfferedGoods;
        RequestedGoods = Other.RequestedGoods;
        return *this;
    }

    // .text:0627c1e0 -- retail's clear() is surprisingly shallow: it only resets
    // Field00/bFlag08 and does NOT touch the goods lists or Field04.
    void clear()
    {
        Field00 = 0;
        bFlag08 = false;
    }

    // .text:0627c1f0 -- OwnerList<GoodDesc>::free() on both lists (inlined), then
    // clear() (Field00 = 0, bFlag08 = 0). Field04 is left alone.
    void destroy()
    {
        OfferedGoods.free();
        RequestedGoods.free();
        clear();
    }

    void marshal(class MarshalBuffer* Buf) const;
    void unmarshal(class MarshalBuffer* Buf);

    int Field00;                    // +0x00  default 0
    int Field04;                    // +0x04  retail leaves this uninitialized by the default ctor
    bool bFlag08;                   // +0x08  default false
    GoodDescList OfferedGoods;      // +0x0C
    GoodDescList RequestedGoods;    // +0x20
};

static_assert(sizeof(GoodDescList) == 0x14, "GoodDescList sizeof 0x14");
static_assert(sizeof(TradeResponseInfo) == 0x34, "TradeResponseInfo sizeof 0x34 (retail layout)");

// ---------------------------------------------------------------------------
// GoodInfo — retail operator_new(0xC0). Port is not layout-compatible (no VC6
// list<> of addons). Lookups key on Id / ArchId only.
// ---------------------------------------------------------------------------
enum EGoodCategory
{
    GoodCategoryCommodity = 0,
    GoodCategoryEquipment = 1,
    GoodCategoryShipHull = 2,
    GoodCategoryShip = 3,
    GoodCategoryUnknown = 0xFFFFFFFF,
};

// Retail GoodInfo derives from CommReferrable at +0x00: the ctor (.text:0626ff6e/0627001c) stores
// CommReferrable's then GoodInfo's vptr, ??_7GoodInfo@@6B@ (.rdata:063993f8) is CommReferrable's
// three slots unchanged, read_Good_block's last fallback is its slot 2 (.text:06270d5f), and
// Server.dll pub::TranslateGoodToMsgIdPrefix calls slot 1 on the GoodInfo* (.text:06d5e068).
struct COMMON_API GoodInfo : public CommReferrable
{
    GoodInfo();
    ~GoodInfo();

    // .text:06270230: true when AttachmentArchetype (+0x84) is a non-empty string.
    bool has_visual_attachment() const { return AttachmentArchetype.Value[0] != '\0'; }

    unsigned Id;                    // nickname CreateID
    unsigned Category;              // EGoodCategory
    unsigned ArchId;                // equipment / hull arch
    unsigned ShipArchId;
    float Price;
    float GoodBuyPrice;
    float BadBuyPrice;
    float GoodSellPrice;
    float BadSellPrice;
    int JumpDist;                   // default 50
    bool bCombinable;
    // +0x74..+0x84: CacheStrings (the ctor CacheString::clear's each, 0626ff79-0626ffa2;
    // read_Good_block StringAlloc's into them).
    CacheString ShopArchetype;          // +0x74
    CacheString ItemIcon;               // +0x78
    CacheString MaterialLibrary;        // +0x7c
    CacheString HpChild;                // +0x80
    CacheString AttachmentArchetype;    // +0x84
    int IdsName;
    int IdsInfo;
    unsigned HullGoodId;                // +0x90
    // Retail +0x94 / +0xa0 / +0xac: three std::list<EquipDesc> filled by "addon" (no slot or
    // anything else / "mid" / "high"). Kept as vectors; only the order of the elements matters.
    std::vector<EquipDesc> AddonLow;
    std::vector<EquipDesc> AddonMid;
    std::vector<EquipDesc> AddonHigh;
    unsigned FreeAmmoArchId;            // +0xb8
    int FreeAmmoCount;                  // +0xbc
};

// Retail layout (ctor 0x06270250, destroy 0x062704a0, find_by_* 0x062711d0..0x06271300):
// +0x00 std::list<GoodInfo*> (insertion order; get_list returns `this`), +0x0c
// std::map<unsigned, GoodInfo*> keyed by GoodInfo::Id, +0x20 the same keyed by ArchId.
class COMMON_API GoodInfoList
{
public:
    GoodInfoList();
    ~GoodInfoList();

    // 0x062645e0 / 0x06264730: retail's compiler-generated memberwise copies -- the
    // std::list<GoodInfo*> is copied node by node (the pointers, not the GoodInfos) and
    // both maps are copied as trees. Shallow, exactly like retail: the copy shares the
    // GoodInfo objects with the original.
    GoodInfoList(GoodInfoList const& Other) = default;
    GoodInfoList& operator=(GoodInfoList const& Other) = default;

    void load(char const* RelPath);
    void destroy();

    GoodInfo const* find_by_id(unsigned Id) const;
    // 0x06271290 ?find_by_name@GoodInfoList@@QBEPBUGoodInfo@@PBD@Z: CreateID(Name), then
    // the ById lookup of find_by_id.
    GoodInfo const* find_by_name(char const* Name) const;
    GoodInfo const* find_by_archetype(unsigned ArchId) const;
    GoodInfo const* find_by_ship_arch(unsigned ArchId) const;

    // 0x06261260 ?get_list@GoodInfoList@@QBEPBV?$list@PAUGoodInfo@@...@std@@XZ:
    // `MOV EAX,ECX ; RET` -- the list sits at offset 0.
    std::list<GoodInfo*> const* get_list() const;

private:
    // ?read_Good_block@GoodInfoList@@AAEXPAVINI_Reader@@PAUGoodInfo@@@Z (0x06270620).
    void read_Good_block(INI_Reader* Reader, GoodInfo* Info);

    std::list<GoodInfo*> Goods;             // +0x00
    std::map<unsigned, GoodInfo*> ById;     // +0x0c
    std::map<unsigned, GoodInfo*> ByArch;   // +0x20
};

COMMON_API void __cdecl GoodList_load(char const* RelPath);
COMMON_API void __cdecl GoodList_destroy();
COMMON_API GoodInfoList* __cdecl GoodList_get();

namespace GoodList
{
    COMMON_API GoodInfo const* __cdecl find_by_id(unsigned Id);
    COMMON_API GoodInfo const* __cdecl find_by_nickname(char const* Name);
    COMMON_API GoodInfo const* __cdecl find_by_archetype(unsigned ArchId);
}

COMMON_API unsigned int __cdecl Good2Arch(unsigned int GoodId);
COMMON_API unsigned int __cdecl Arch2Good(unsigned int ArchId);
COMMON_API bool __cdecl arch_is_combinable(unsigned int ArchId);

// ---------------------------------------------------------------------------
// Market / bases
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// TransactionType -- retail's own `enum TransactionType`, proved real by the
// mangling of BaseData::set_market_good,
//   ?set_market_good@BaseData@@QAEXIHHW4TransactionType@@MMM@Z
// where `W4TransactionType@@` is a global-scope enum passed by value as the
// fourth parameter.
//
// The VALUES come from Server.dll's two filters over the market map, not from
// any header. Both read the same dword, map-node+0x20, which is mapped
// value +0x10:
//   for sale  : .text:06d12ec4 MOV EAX,[ESI+0x20] / TEST EAX,EAX / JZ accept /
//               .text:06d12ecb CMP EAX,0x2 / JNZ reject        -> 0 or 2
//               (also .text:06d12f84/.text:06d12f8b, and .text:06d12dc9 with
//                `CMP ESI,0x2` in GetNumCommoditiesForSale's helper)
//   in demand : .text:06d12e24 MOV EAX,[ESI+0x20] / TEST EAX,EAX / JZ accept /
//               .text:06d12e2b CMP EAX,0x1 / JNZ reject        -> 0 or 1
//               (also .text:06d12d70/.text:06d12d77 behind pub::Market::
//                IsGoodInDemand)
// So 0 means both directions, 1 demand-only, 2 sale-only, and anything else is
// neither. The previous port had `TransactionBuy = 0, TransactionSell = 1`,
// which is an invented pair that gets both the count and the meaning wrong.
//
// TODO: retail's own enumerator spellings are in no table we hold; these names
// are ours. The numeric values are not.
// ---------------------------------------------------------------------------
enum TransactionType
{
    TransactionBuyAndSell = 0,      // base both sells it and buys it
    TransactionDemandOnly = 1,      // base buys it only
    TransactionSaleOnly = 2,        // base sells it only
};

// ---------------------------------------------------------------------------
// MarketGoodInfo -- the mapped value of BaseData::Market. 0x1C bytes.
//
// The size and the field order are read off Common.dll's own bytes, not
// inferred. BaseDataList::load_market_data (0x06265900) builds the record on
// the stack at [ESP+0x30] and copies it with `MOV ECX,0x7` + `MOVSD.REP`
// (.text:06265c95 and again at .text:06265cbf), 7 dwords = 0x1C:
//
//   +0x00  .text:06265c2b  MOV [ESP+0x30],ESI   ESI = GoodInfo+0x48, the good id
//   +0x04  .text:06265c21  FLD  [EAX+0x58]      GoodList::find_by_id(id)->Price
//          .text:06265c24  FMUL [ESP+0x20]      INI index 6, the price multiplier
//          .text:06265c2f  CALL MSVCRT ftol     <-- TRUNCATED to an integer
//          .text:06265c40  FILD / .text:06265c50 FSTP [ESP+0x34]
//   +0x08  .text:06265c58  MOV [ESP+0x38],EAX   INI index 3 (int)
//   +0x0c  .text:06265ca8  MOV [ESP+0x44],EBP   INI index 4 (int); ESP is 8 lower
//                                               there after two pushes, so the
//                                               displacement 0x44 is record+0x0c
//   +0x10  .text:06265c54  MOV [ESP+0x40],EDI   INI index 5 (TransactionType)
//   +0x14  .text:06265c48  MOV [ESP+0x44],ECX   INI index 1 (float)
//   +0x18  .text:06265c97  MOV [ESP+0x48],EDX   INI index 2 (float)
//
// INI index 7 is read and thrown away (.text:06265bbb get_value_float,
// .text:06265bc0 `ddd8 FSTP ST0`). Index 6 is never stored; it only produces
// the price at +0x04.
//
// Four independent confirmations of the 0x1C size, all on bytes:
//   1. both `MOV ECX,0x7` + REP MOVSD above;
//   2. the mangled return type of ?get_market@BaseData@@QBEPBV?$map@IUMarket
//      GoodInfo@@U?$less@I@std@@V?$allocator@UMarketGoodInfo@@@3@@std@@XZ --
//      the mapped type IS MarketGoodInfo;
//   3. Server.dll walks the tree nodes directly and tests `_Isnil` at node+0x2d
//      (.text:06d12ee4, .text:06d12fa4), so the node's value spans 0x0c..0x2b =
//      0x20 bytes = key(4) + mapped(0x1c), and the key sits at node+0x0c
//      (.text:06d12d50 CMP EDX,[EAX+0xc]) with the mapped value at node+0x10
//      (.text:06d12cfb ADD EAX,0x10);
//   4. BaseData::set_market_good (0x06264b60) builds the identical 7-dword
//      record and copies it into node+0x10 with the same `MOV ECX,7` + REP MOVSD
//      (.text:06264d60 .. .text:06264d69).
//
// Field naming, and how sure each name is:
//   GoodId / Price          -- certain (see above).
//   MinInventory            -- certain: pub::Market::GetMinInventory's whole
//                              body is a map find that returns [mapped+0x8]
//                              (.text:06d12cbe MOV EAX,[EAX+0x8]).
//   Field_0x0c              -- INI index 4. NO reader found: Server.dll exports
//                              exactly seven pub::Market functions and none of
//                              them touches +0x0c, and neither does any market
//                              path in Common.dll. Placeholder name on purpose.
//                              TODO: name it once a reader turns up.
//   Type                    -- certain as an `enum TransactionType` from the
//                              set_market_good mangling; see the enum below.
//   MinLevel / MinRep       -- INI indices 1 and 2. Also unread by anything in
//                              Server.dll. The names come from the defaults the
//                              ctor installs (0.0 and -1.0; a -1.0 default is a
//                              Freelancer reputation floor), which is weaker
//                              evidence than the rest of this comment.
//                              TODO: confirm against a real reader.
// ---------------------------------------------------------------------------
struct COMMON_API MarketGoodInfo
{
    // User-declared so that no implicit move members are generated and exported:
    // VC6 had none, and retail exports exactly the copy assignment.
    MarketGoodInfo& operator=(MarketGoodInfo const&) = default;

    MarketGoodInfo();

    unsigned GoodId;                // +0x00
    float Price;                    // +0x04  nominal * INI index 6, ftol'd
    int MinInventory;               // +0x08  INI index 3, default 0
    // +0x0c renamed 2026-10-14 from Field_0x0c: the MarketGood value after the minimum
    // stock (index 3 -> +0x08), and freelancer.exe stocks a market entry only when the
    // minimum is nonzero and this is positive (0x00437ebd..0x00437ecb).
    int MaxInventory;               // +0x0c  INI index 4, default 0
    TransactionType Type;           // +0x10  INI index 5, default 0
    float MinLevel;                 // +0x14  INI index 1, default 0.0
    float MinRep;                   // +0x18  INI index 2, default -1.0
};

// weighted_vector<T> lives in common_weighted_vector.h (included above).

// thorn.dll IScriptEngine: declared in thorn_iface.h (included above).

// Thorn glue -- E:\FL\Scratch\Source\Common\ThornScript.cpp.
// All five accessors below share one shape: entity name from slot 7 (+0x1c), user property
// from slot 33 (+0x84), 0 meaning "found".
COMMON_API char const* __cdecl ThornGetUserString(IScriptEngine* Engine, int Index, char const* Key);
// 0x1bd00 / 0x1bca0 -- the same lookup parsed with atol / atof.
COMMON_API bool __cdecl ThornGetUserInt(IScriptEngine* Engine, int Index, char const* Key,
                                        int* Out);
COMMON_API bool __cdecl ThornGetUserFloat(IScriptEngine* Engine, int Index, char const* Key,
                                          float* Out);
// 0x1bd60. Reads the flag byte at entity +0x42: bit 0 -> 1, bit 1 -> 2 (bit 1 wins), else 0.
// The second parameter is accepted and ignored, exactly as in retail.
COMMON_API unsigned long __cdecl ThornGetRenderType(ThornEntity const* Entity, char const* Unused);
// 0x1b8f0. Creates the "IScriptEngine" component through DALib::Dacom, queries
// "1.11_IScriptEngine" off it, then feeds it the script file opened as a "DOS" filesystem.
COMMON_API IScriptEngine* __cdecl ThornScriptLoad(char const* Path);
// 0x1ba10 -- folded onto DAEngine_release_filesystem in retail: release through slot 2 and null.
COMMON_API void __cdecl ThornScriptDestroy(IScriptEngine** Engine);
// 0x1ba30. The first type-2 entity whose "actor" user property equals ActorName wins (with a
// null ActorName: no actor property, or any actor except "player"); its Csys is
// {x from +0x08, y from +0x44, z from +0x10} with the 3x3 at +0x14. Identity on a miss.
COMMON_API Csys __cdecl ThornScriptGetCsys(IScriptEngine* Engine, char const* ActorName);

class RoomData;

// FlashLightSetInfo -- Common.dll.c 0x13bb0 ctor / 0x13bf0 dtor / 0x13c30 clear /
// 0x13d70 add_light_csys / 0x14120 unfixup. Per-object runtime table of resolved
// flashlight-hardpoint frames (NOT RoomLightInfo -- that's a separate, unrelated
// retail class at 0x13410 for static Thorn light entities in common_goods.h).
//
// Layout confirmed by reading clear()/dtor byte-for-byte: the two 64-slot Csys banks
// (LightFrames/FinalFrames) are literally Csys::set_identity() (Common.dll.c 0x1170)
// repeated inline -- same (0,0,0)/identity-3x3 bit pattern, field for field.
struct COMMON_API FlashLightSetInfo
{
public:
    FlashLightSetInfo();
    ~FlashLightSetInfo();

    void clear();
    // .text:06273bf0: the exported ?destroy@FlashLightSetInfo@@QAEXXZ and the destructor are
    // one ICF-folded body (free both string banks, then tail-jump into clear()).
    void destroy();
    void unfixup();

    // 0x06273db0 ?fixup@FlashLightSetInfo@@QAEXJPBVHardpointSummary@@PBVRoomData@@@Z.
    // Resolves HardpointNames[]/SetpointNames[] (hardpoints of the room's compound instance,
    // then room setpoints) into LightFrames[0..NumLights), then appends to FinalFrames:
    // Mode 1 copies LightFrames verbatim (add_light_csys inlined), Mode 2 interpolates
    // NumLightsAlt steps between LightFrames[0] and LightFrames[1]. Called by
    // RoomData::fixup_FlashLight_list (0x06277000) and RoomData::fixup (0x06277ce5). The
    // Instance argument is not read.
    void fixup(long Instance, HardpointSummary const* HpSum, RoomData const* Room);

private:
    // Private in retail (?add_light_csys@FlashLightSetInfo@@AAEXABVCsys@@@Z). fixup() inlines
    // the same append rather than calling it.
    void add_light_csys(Csys const& LightFrame);

public:

    // Common.dll.c 0x13c30 (clear): 7 unconfirmed float params + a mode flag, defaulting
    // to 1.0f, 1.0f, 1.0f, 2.0f, 0.1f, 0.2f, 1.0f, 0. Plausibly a spotlight-ish
    // color/range/cone-angle/intensity set given the class name, but nothing in the 4
    // ported functions reads them back and fixup() (see above) is unported, so that
    // reading is NOT confirmed. Hex constants decoded exactly; offsets verified.
    float Param0;   // +0x00  0x3f800000 = 1.0f
    float Param1;   // +0x04  0x3f800000 = 1.0f
    float Param2;   // +0x08  0x3f800000 = 1.0f
    float Param3;   // +0x0C  0x40000000 = 2.0f
    float Param4;   // +0x10  0x3dcccccd = 0.1f
    float Param5;   // +0x14  0x3e4ccccd = 0.2f
    float Param6;   // +0x18  0x3f800000 = 1.0f
    long  Mode;     // +0x1C  0 -- fixup() branches on this: 1 = copy LightFrames verbatim,
                    // 2 = interpolate LightFrames[0]/[1] over NumLightsAlt steps

    // +0x020/+0x120: 64 parallel malloc'd string slots. HardpointNames[i]/SetpointNames[i]
    // form a pair per light slot (fixup() resolves HardpointNames[i] against a
    // HardpointSummary, then overrides that Csys from a room setpoint lookup when
    // SetpointNames[i] is also set). Freed with malloc_strfree, same as the rest of the
    // port's malloc'd char* fields.
    char const* HardpointNames[64];  // +0x020
    char const* SetpointNames[64];   // +0x120

    // +0x220: bank of 64 Csys, default-identity (see class comment). Populated by
    // fixup() from HardpointNames/SetpointNames; left at identity until that exists.
    Csys LightFrames[64];            // +0x220

    long NumLights;      // +0xE20 -- count of valid HardpointNames/LightFrames slots
    long NumLightsAlt;   // +0xE24 -- second count, only consulted by fixup() when Mode == 2
    long Cursor;         // +0xE28 -- write index into FinalFrames; reset to 0 by unfixup()

    // +0xE2C: second bank of 64 Csys, also default-identity. add_light_csys() appends to
    // FinalFrames[Cursor++].
    Csys FinalFrames[64];            // +0xE2C
};

static_assert(offsetof(FlashLightSetInfo, Mode) == 0x1C, "FlashLightSetInfo Mode at +0x1C");
static_assert(offsetof(FlashLightSetInfo, HardpointNames) == 0x20, "FlashLightSetInfo HardpointNames at +0x20");
static_assert(offsetof(FlashLightSetInfo, SetpointNames) == 0x120, "FlashLightSetInfo SetpointNames at +0x120");
static_assert(offsetof(FlashLightSetInfo, LightFrames) == 0x220, "FlashLightSetInfo LightFrames at +0x220");
static_assert(offsetof(FlashLightSetInfo, NumLights) == 0xE20, "FlashLightSetInfo NumLights at +0xE20");
static_assert(offsetof(FlashLightSetInfo, Cursor) == 0xE28, "FlashLightSetInfo Cursor at +0xE28");
static_assert(offsetof(FlashLightSetInfo, FinalFrames) == 0xE2C, "FlashLightSetInfo FinalFrames at +0xE2C");
static_assert(sizeof(FlashLightSetInfo) == 0x1A2C, "FlashLightSetInfo sizeof 0x1A2C");

// ---------------------------------------------------------------------------
// RoomData -- one room of a base (E:\FL\Scratch\Source\Common\RoomData.cpp). Retail
// sizeof 0x208; every field below is at its retail offset in retail order (the vectors are
// 0x10 bytes in VC6, the port's std::vector differs in size, so the offsets after +0x134
// are documentation, not layout). Records are owned through pointers in the vectors;
// destroy() deletes them, clear() only drops them.
//   ctor 06274150, dtor 06274300, copy ctor 06261330 and operator= 06261780 (both the
//   compiler-generated memberwise copies: strings and record pointers are SHARED), clear
//   06274540, destroy 062748d0.
// freelancer.exe and Server.dll read the fields directly.
// ---------------------------------------------------------------------------
class COMMON_API RoomData
{
public:
    RoomData();
    // 0x06261330 / 0x06261780: memberwise. Strings and record pointers are copied as
    // pointers (the copy shares them with Other), Animations by a 0x20-dword block copy, the
    // camera by RoomCameraInfo's implicit copy (06261762 REP MOVSD 0x10) in the copy ctor
    // and by RoomCameraInfo::operator= (06261d59) in the assignment.
    RoomData(RoomData const& Other) = default;
    ~RoomData();
    RoomData& operator=(RoomData const& Other) = default;

    // 06277c50: read_from_ini(File) unless bLoaded, then bLoaded = true.
    void load();
    void unload();
    // Retail 0x17c10 stores param_1 at +0x00 and param_2 at +0x04, and its only caller
    // (BaseData::read_Room_block, .text:0626504f..06265058) passes (BaseData+0 = base id,
    // CreateID("%X_%s") = room id, nickname, file). Parameter order fixed 2026-09-24.
    void init(unsigned NewBaseId, unsigned NewRoomId, char const* NewNickname, char const* NewFile);
    void clear();
    void destroy();
    void fixup(long Instance);
    void unfixup();

    RoomCameraInfo const* find_camera(char const* Name, char const* ScriptName) const;
    RoomCameraInfo const* find_camera_generic(char const* Pattern, char const* ScriptName) const;
    SetpointInfo const* find_setpoint(char const* Name, char const* ScriptName) const;
    SetpointInfo const* find_setpoint_generic(char const* Pattern, char const* ScriptName) const;

    void enumerate_CharacterPlacement_ini(void(__cdecl* Callback)(int, INI_Reader*, void*),
                                          void* Context) const;

    RoomCameraInfo const* get_camera_info() const;

private:
    // Everything below is private in retail (`AAE` / `CAX` in the mangled names, e.g.
    // ?read_from_ini@RoomData@@AAEXPBD@Z, ?make_setpoint_name_generic@RoomData@@CAXPBDPAD@Z).
    // FlashLightSetInfo::fixup (Common.dll.c 0x13db0) calls apply_setpoint, so it is a friend.
    friend struct FlashLightSetInfo;

    void warn(INI_Reader* Reader, bool bError);

    void read_from_ini(char const* RelPath);
    void read_set_script(char const* RelPath);
    void read_ambient_script(INI_Reader* Reader);
    void read_RoomInfo_block(INI_Reader* Reader);
    void read_Camera_block(INI_Reader* Reader);
    void read_Sound_block(INI_Reader* Reader);
    void read_CharacterPlacement_block(INI_Reader* Reader);
    void read_PlayerShipPlacement_block(INI_Reader* Reader);
    void read_ForSaleShipPlacement_block(INI_Reader* Reader);
    void read_Hotspot_block(INI_Reader* Reader);
    void read_FlashLightSet_block(INI_Reader* Reader);
    void read_FlashLightLine_block(INI_Reader* Reader);

    void add_set_script_light(unsigned long Id, Csys const& Frame,
                              ThornLightProperties const* Props, unsigned char Type);
    // The parameters in RoomPropInfo field order (+0x00 .. +0x50).
    void add_set_script_Prop(char const* Template, char const* Name, int Category,
                             Csys const& Frame, bool bVisible, bool bLit, bool bFog,
                             bool bRoomMain, unsigned char LightGroup, signed char Layer,
                             unsigned long RenderType, char const* Loadout,
                             unsigned long PriorityId, bool bRunningLights);
    void add_set_script_Camera(unsigned long Id, Csys const& Frame, char const* Name,
                               ThornCameraProperties const* Props);
    void add_set_script_Setpoint(unsigned long Id, Csys const& Frame, char const* Name,
                                 ThornEntity const* Entity);

    // 06275bb0, called from fixup() only.
    void setup_Camera();
    void unsetup_Camera();

    CharPlaceInfo* find_CharacterPlacement(char const* Name);
    SetpointInfo const* apply_setpoint(char const* Name, char** OutName, Csys* OutFrame,
                                       char const* ScriptName);
    static void __cdecl make_setpoint_name_generic(char const* Name, char* OutGeneric);

    void fixup_FlashLight_list(long Instance, HardpointSummary const* HpSum);
    void unfixup_FlashLight_list();

public:

    // Retail field map, in retail order. init (06277c10) writes +0x00..+0x10, read_RoomInfo_block
    // (06275490) +0x14..+0xc8, read_Sound_block (06275860) +0xcc..+0xd5, read_set_script
    // (06274b40) +0xd8..+0x130 and the record vectors, read_Camera_block (06275aa0) +0x1c4,
    // setup_Camera (06275bb0) +0x1c8.
    unsigned BaseId;                    // +0x00
    unsigned RoomId;                    // +0x04
    // +0x08: set by init (0) and load (1). Neither the ctor nor clear() writes it (06274150,
    // 06274540): it stays uninitialised until init, its only creator (BaseData::read_Room_block)
    // calling init right after the ctor.
    bool bLoaded;
    char* Nickname;                     // +0x0c  malloc_strcpy (init)
    char* File;                         // +0x10  malloc_strcpy (init)
    // +0x14 "animation" / "animation_oneshot" names, +0x94 their loop flags (1 / 0), +0xb4 the
    // count. read_RoomInfo_block indexes both arrays by the count with no bound check.
    char* Animations[0x20];             // +0x14
    bool AnimationLoop[0x20];           // +0x94
    int AnimationCount;                 // +0xb4
    char* GoodscartScript;              // +0xb8  "goodscart_script" value 0
    unsigned GoodscartPriorityId;       // +0xbc  CreateID(value 1), 0 without one; RtcSlider gate
    bool bCutoffShadowZ;                // +0xc0
    float CutoffShadowZ;                // +0xc4
    float ExtraShadowHeightOffset;      // +0xc8
    ID_String Music;                    // +0xcc  [Sound] music = id [, oneshot]
    ID_String Ambient;                  // +0xd0  [Sound] ambient = id [, oneshot]
    bool bMusicLoop;                    // +0xd4  1 unless "oneshot"
    bool bAmbientLoop;                  // +0xd5  1 unless "oneshot"
    // +0xd8: the set_script's ambient entity (Thorn type 9): three ints / 255.0. clear (1,1,1).
    Vector AmbientColor;
    // +0xe4..+0x108: the set_script light named "LtShadowSource"; +0x10c..+0x130 the one named
    // "LtGlareSource". The flag at +0xe5 / +0x10d is 1 when the light is directional (D3D light
    // type 3 in the light blob): then the direction (+0xf4 / +0x11c) is the blob's direction
    // turned by the entity's orientation (3DMathEngine slot 11) and normalized; for any other
    // light type only the position (+0xe8 / +0x110, the entity's own) is written.
    bool bShadowLight;                  // +0xe4
    bool bShadowLightWorldSpaceDir;     // +0xe5  clear: 1
    Vector ShadowLightPosition;         // +0xe8
    Vector ShadowLightDirection;        // +0xf4  clear: (0, 1, 0)
    Vector ShadowLightColor;            // +0x100 the blob's diffuse colour
    bool bGlareLight;                   // +0x10c
    bool bGlareLightWorldSpaceDir;      // +0x10d clear: 1
    Vector GlareLightPosition;          // +0x110
    Vector GlareLightDirection;         // +0x11c clear: (0, 1, 0)
    Vector GlareLightColor;             // +0x128
    // The owned record vectors. destroy() deletes every element; clear() only empties them.
    std::vector<RoomCameraInfo*> Cameras;           // +0x134 set_script cameras (type 3)
    std::vector<RoomLightInfo*> Lights;             // +0x144 set_script lights (type 5, on)
    std::vector<RoomPropInfo*> Props;               // +0x154 set_script compounds (type 1)
    std::vector<SetpointInfo*> Setpoints;           // +0x164 set_script setpoints (types 3, 7)
    std::vector<CharPlaceInfo*> CharacterPlacements; // +0x174 one per zs/zg setpoint
    // +0x184: [PlayerShipPlacement] and [ForSaleShipPlacement] in file order; the bytes +0x34 /
    // +0x35 of the record tell them apart. freelancer.exe FUN_00448a60 indexes it directly.
    std::vector<ShipPlaceInfo*> ShipPlacements;
    std::vector<RoomButtonInfo*> Hotspots;          // +0x194 [Hotspot]
    std::vector<FlashLightSetInfo*> FlashLightSets; // +0x1a4 [FlashlightSet] / [FlashlightLine]
    std::vector<AmbientScriptInfo*> AmbientScripts; // +0x1b4 [Room] scene =
    char* CameraName;                   // +0x1c4 [Camera] name
    RoomCameraInfo Camera;              // +0x1c8 the camera setup_Camera resolved
};

// Retail sizeof 0x30: +0x00 BaseId, +0x04 start room, +0x08 price_variance, +0x0c
// ship_repair_cost, +0x10 market map, +0x24 list<RoomData*>. The copy ctor (06261de0) and
// operator= (06261f20) are the compiler-generated memberwise ones: the room list is copied
// pointer by pointer (FUN_0630cd00 is `*dst = *src`), so a copy SHARES the RoomData objects
// and assigning deletes none of the old ones.
class COMMON_API BaseData
{
public:
    BaseData();
    BaseData(BaseData const& Other) = default;
    ~BaseData();
    BaseData& operator=(BaseData const& Other) = default;

    unsigned get_base_id() const;
    float get_price_variance() const;
    float get_ship_repair_cost() const;
    unsigned get_start_location() const;
    // .text:06261dc0: LEA EAX,[this+0x10] -- the market map.
    std::map<unsigned, MarketGoodInfo> const* get_market() const;
    // .text:06261dd0 (both names, ICF): LEA EAX,[this+0x24] -- the room list.
    std::list<RoomData*>* get_room_data_list();
    std::list<RoomData*> const* get_const_room_data_list() const;
    void read_from_ini(char const* RelPath, unsigned BaseId);
    // 0x06264b60 ?set_market_good@BaseData@@QAEXIHHW4TransactionType@@MMM@Z.
    // Parameter order is retail's, read off the writes into the record, NOT
    // guessed from the struct: param 5 is the PRICE MULTIPLIER
    // (.text:06264bdb FMUL [ESP+param_5], the same multiply the INI path does),
    // param 6 lands at record+0x14 (.text:06264c71 MOV [ESP+0x68],EDX) and
    // param 7 at record+0x18 (.text:06264c35 MOV [ESP+0x6c],EAX). The previous
    // declaration had (MinLevel, MinRank, PriceMult) in those three slots,
    // i.e. the multiplier last instead of first.
    void set_market_good(unsigned GoodId, int MinInventory, int MaxInventory,
                         TransactionType Type, float PriceMult, float MinLevel,
                         float MinRep);

private:
    // 0x06264d80 -- [Base]/[BaseInfo] block: start_room, price_variance, ship_repair_cost.
    void read_Base_block(INI_Reader* Reader);
    // 0x06264ea0 -- [Room] block: allocates and appends one RoomData from nickname/file.
    void read_Room_block(INI_Reader* Reader);

public:

    unsigned BaseId;                // +0x00
    unsigned StartRoomId;           // +0x04
    float PriceVariance;            // +0x08
    float ShipRepairCost;           // +0x0c default 0.33
    std::map<unsigned, MarketGoodInfo> Market; // +0x10
    // Retail +0x24: std::list<RoomData*> (the mangled return type of get_room_data_list).
    std::list<RoomData*> Rooms;
};

// Retail sizeof 0x34: +0x00 list<BaseData*>, +0x0c map base id -> BaseData*, +0x20 map room
// id -> RoomData*. Both maps are filled by load() after every base has been read. Copy ctor
// (06262150) and operator= (062622a0) are memberwise: the BaseData pointers are shared.
class COMMON_API BaseDataList
{
public:
    BaseDataList();
    BaseDataList(BaseDataList const& Other) = default;
    ~BaseDataList();
    BaseDataList& operator=(BaseDataList const& Other) = default;

    void load();
    void load_market_data(char const* RelPath);
    BaseData* get_base_data(unsigned BaseId) const;
    RoomData* get_room_data(unsigned RoomId) const;
    RoomData* get_unloaded_room_data(unsigned RoomId) const;
    // .text:06261260 (both names, ICF): MOV EAX,this -- the base list sits at +0x00.
    std::list<BaseData*>* get_base_data_list();
    std::list<BaseData*> const* get_const_base_data_list() const;

private:
    // Retail +0x00: std::list<BaseData*> (the mangled return type of get_base_data_list).
    std::list<BaseData*> Bases;
    std::map<unsigned, BaseData*> ById;      // +0x0c
    std::map<unsigned, RoomData*> RoomById;  // +0x20
};

COMMON_API void __cdecl BaseDataList_load();
COMMON_API void __cdecl BaseDataList_load_market_data(char const* RelPath);
COMMON_API void __cdecl BaseDataList_destroy();
COMMON_API BaseDataList* __cdecl BaseDataList_get();

// ---------------------------------------------------------------------------
// Loadout
// ---------------------------------------------------------------------------
namespace Loadout
{
    struct COMMON_API Map
    {
        // User-declared so that no implicit move members are generated and exported:
        // VC6 had none, and retail exports exactly the copy assignment.
        Map& operator=(Map const&) = default;

        unsigned NicknameId;
        std::vector<EquipDesc> Items;
        // +0x14 (was `unsigned short NextEquipId`): Loadout::Load constructs it with
        // EquipIdMaker() (.text:063116dc), resets it (06311785) and draws every item's id
        // from CreateEquipID (063117d9 / 0631185a).
        SubObjectID::EquipIdMaker IdMaker;
    };

    COMMON_API int __cdecl Load(char const* Path, bool bReload);
    COMMON_API void __cdecl Free();
    COMMON_API Map const* __cdecl Get(unsigned int Id);
    COMMON_API unsigned int __cdecl GetID(char const* Name);

    COMMON_API bool __cdecl ReadEquipLine(INI_Reader& Reader, EquipDesc& Desc);
    COMMON_API bool __cdecl ReadCargoLine(INI_Reader& Reader, EquipDesc& Desc);
}

// ---------------------------------------------------------------------------
// ReadConstants — globals use retail namespace names for mangles.
// ---------------------------------------------------------------------------
COMMON_API bool __cdecl ReadConstants(char const* RelPath);

COMMON_API extern float COLLISION_DAMAGE_FACTOR;
COMMON_API extern float LOOT_OWNER_SAFE_TIME;
COMMON_API extern float LOOT_UNSEEN_LIFE_TIME;
COMMON_API extern float LOOT_UNSEEN_RADIUS;
COMMON_API extern float MUSIC_CROSS_FADE_DELAY;
COMMON_API extern float JETTISONED_CARGO_VELOCITY;
// SND_CARGO_JETTISONED and FIRE_FAILED_SOUND are ?<name>@@3UID_String@@A in retail, not
// unsigned, and they are declared with the rest of the ID_String globals in common_misc.h.
// They used to be declared here as plain unsigned as well, which gave the port two globals of
// the same name and different type in two translation units.
COMMON_API extern float MUZZLE_CONE_ANGLE;
COMMON_API extern float FIRE_FAILED_DELAY;
COMMON_API extern float PLAYER_COLLISION_GROUP_HIT_PTS_SCALE;
COMMON_API extern float PLAYER_ATTACHED_EQUIP_HIT_PTS_SCALE;
COMMON_API extern int MAX_PLAYER_AMMO;
COMMON_API extern float COMM_PLAYER_FAR_DIST;
COMMON_API extern float COMM_PLAYER_FAR_DIST_ATTEN;
COMMON_API extern float CHATTER_MAX_DIST;
COMMON_API extern float CHATTER_MAX_DIST_ATTEN;
COMMON_API extern float CHATTER_START_ATTEN;
COMMON_API extern int COMM_CONFLICT_PRIORITY_CUTOFF;
COMMON_API extern float WALLA_MAX_DIST;
COMMON_API extern float WALLA_MAX_DIST_ATTEN;
COMMON_API extern float WALLA_START_ATTEN;
COMMON_API extern int WALLA_PRIORITY_CUTOFF;

namespace EngineEquipConsts
{
    COMMON_API extern float CRUISING_SPEED;
    COMMON_API extern float CRUISE_ACCEL_TIME;
    COMMON_API extern float CRUISE_DRAG;
    COMMON_API extern float CRUISE_DISRUPT_TIME;
    COMMON_API extern float MAX_DELTA_FX_THROTTLE;
    COMMON_API extern float MAX_ENGINE_FX_THROTTLE;
    COMMON_API extern float THROTTLE_STEADY_TIME;
    COMMON_API extern float THROTTLE_ATTEN_MOD_RANGE;
    COMMON_API extern float DELTA_THROTTLE_ATTEN_MOD_CHANGING;
    COMMON_API extern float DELTA_THROTTLE_ATTEN_MOD_STEADY;
    COMMON_API extern float CRUISE_STEADY_TIME;
    COMMON_API extern float DELTA_CRUISE_ATTEN_MOD_STEADY;
    COMMON_API extern float CRUISE_ATTEN_MOD_RANGE;
}

namespace ShieldEquipConsts
{
    COMMON_API extern float HULL_DAMAGE_FACTOR;
}

namespace ThrusterEquipConsts
{
    // ?INTERIOR_SOUND_NAME@ThrusterEquipConsts@@3UID_String@@A -- an ID_String, not a bare id.
    COMMON_API extern ID_String INTERIOR_SOUND_NAME;
    COMMON_API extern ID_String EXTERIOR_SOUND_NAME;
    COMMON_API extern float MAX_VOLUME_FORCE;
    COMMON_API extern float MIN_VOLUME_FORCE;
    COMMON_API extern float INSIDE_CONE_ANGLE;
    COMMON_API extern float OUTSIDE_CONE_ANGLE;
    COMMON_API extern float OUTSIDE_CONE_ATTENUATION;
}

namespace Collision
{
    COMMON_API bool __cdecl Load(INI_Reader& Reader);
}
