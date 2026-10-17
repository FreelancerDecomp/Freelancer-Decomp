// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "dacom.h"

// Common.dll MISC batch: global utilities, File I/O, DamageList/Entry, culling helpers
// Wave 5 catch-all for remaining exports not covered by other batches

#include <cstddef>
#include <cstdio>
#include <list>
#include "common_ini.h"  // Vector, StringAlloc declaration
#include "common_id.h"   // ID_String (the four sound/fuse id globals below)
#include "common_csys.h" // Transform (Geometry::Frustum below)

// Forward declarations
struct IFileSystem;
struct FmtStr;
// Geometry::Sphere / Geometry::Frustum -- the argument types of SphereCull / FrustumCull
// (exported) and of the zone spatial partition queries. Server.dll pub::System::InZones builds
// both on its stack (FUN_06cfb640 / FUN_06cfb760), so they are public.
namespace Geometry
{
    struct Sphere
    {
        Vector Center; // +0x00
        float Radius;  // +0x0C
    };
    static_assert(sizeof(Sphere) == 0x10, "Geometry::Sphere size is 0x10");

    struct Frustum
    {
        Transform Xform; // +0x00
        float AngleX;    // +0x30
        float AngleY;    // +0x34
        float Distance;  // +0x38
    };
    static_assert(sizeof(Frustum) == 0x3C, "Geometry::Frustum size is 0x3C");
}
namespace Universe { struct IZone; }
struct CObject;

// ============================================================================
// CRT-heap string helpers (malloc_strcpy family; StringAlloc lives in common_ini.h)
// ============================================================================

// Retail exports only ??4option_spec@@QAEAAU0@ABU0@@Z (ord 882, .text:06280ee0, the
// two-dword copy ICF-shared with ActionDB/CollisionGroupDesc/... operator=).
struct COMMON_API option_spec
{
    // User-declared so that no implicit move members are generated and exported:
    // VC6 had none, and retail exports exactly the copy assignment.
    option_spec& operator=(option_spec const&) = default;

    char const* Name;     // +0x00
    unsigned long Value;  // +0x04
};

COMMON_API unsigned long __cdecl select_option_from_string(char* String, option_spec const* const Options, int Count, char const* DefaultName);

// ?malloc_strcpy@@YAPBDPAPADPBD@Z  RVA 0xd4400
COMMON_API char const* __cdecl malloc_strcpy(char** Dest, char const* Src);

// ?malloc_strfree@@YAXPAPAD@Z  RVA 0xd4430
COMMON_API void __cdecl malloc_strfree(char** Str);

// Retail ?malloc_wstrcpy@@YAPBGPAPAGPBG@Z  RVA 0xd4450 (VC6 spells wchar_t `unsigned short`, G;
// ours is the same function over wchar_t, _W).
COMMON_API wchar_t const* __cdecl malloc_wstrcpy(wchar_t** Dest, wchar_t const* Src);

// Retail ?malloc_wstrfree@@YAXPAPAG@Z  RVA 0xd4430 (ICF-folded)
COMMON_API void __cdecl malloc_wstrfree(wchar_t** Str);

// ============================================================================
// Client/server run-permission handshake (Common.dll.txt 0x631d970 .. 0x631da4c)
// Two named manual-reset events, both created already signalled. Only the client waits with
// a 5 s budget; the server side gets 500 ms. A wait that times out signals its own event
// again, so a dead peer degrades to "everyone runs" instead of deadlocking.
// ============================================================================

// ?WaitForClientRunPermission@@YAXXZ  RVA 0xbd9b0
COMMON_API void __cdecl WaitForClientRunPermission();

// ?WaitForServerRunPermission@@YAXXZ  RVA 0xbd9f0
COMMON_API void __cdecl WaitForServerRunPermission();

// ?GiveClientRunPermission@@YAXXZ  RVA 0xbda30
COMMON_API void __cdecl GiveClientRunPermission();

// ?GiveServerRunPermission@@YAXXZ  RVA 0xbda40
COMMON_API void __cdecl GiveServerRunPermission();

// ?SafeWaitForSingleObject@@YAKPAXK@Z  RVA 0x9d4c0. WaitForSingleObject that keeps pumping the
// message queue. Returns WAIT_TIMEOUT (0x102) when WM_QUIT shows up.
COMMON_API unsigned long __cdecl SafeWaitForSingleObject(HANDLE Handle, unsigned long Timeout);

// ============================================================================
// Hex <-> wide-string and GUID text parsing (Common.dll.txt 0x63032b0 / 0x6303260 / 0x631b980)
// ============================================================================

// Retail ?HexToWide@@YAXPBDPAG@Z  RVA 0xa32b0 (G = VC6 wchar_t). If every character of Src is a hex digit the string is
// read back four characters at a time as UTF-16 code units; otherwise it is converted with
// mbstowcs. Dst is NUL-terminated in the hex path only.
COMMON_API void __cdecl HexToWide(char const* Src, wchar_t* Dst);

// Retail ?WideToHex@@YAXPBGPAD@Z  RVA 0xa3260. The inverse of the hex path: "%04x" per code unit.
COMMON_API void __cdecl WideToHex(wchar_t const* Src, char* Dst);

// Retail ?filetime_to_wstring@@YAPBGABU_FILETIME@@@Z  RVA 0xa3360. Short date, a space, the
// 24-hour time, in one shared static buffer (DAT_063fcc78) that every call overwrites.
struct _FILETIME;
COMMON_API wchar_t const* __cdecl filetime_to_wstring(_FILETIME const& Time);

// ?ParseGUID@@YAPAU_GUID@@PBDPAU1@@Z  RVA 0xbb980. Parses "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}"
// into Out. Returns Out, or NULL when the text is the null GUID (Out is still filled with zeros).
struct _GUID;
COMMON_API _GUID* __cdecl ParseGUID(char const* Text, _GUID* Out);

// ============================================================================
// Math utilities
// ============================================================================

// FUN_062df7f0 (the unexported fast inverse square root) is ported next to its only two
// callers as FastInvSqrtX87 in common_airaycast.cpp.

// ============================================================================
// Frustum/sphere culling (Universe::IZone intersection tests)
// ============================================================================

// ?FrustumCull@@YA_NABUFrustum@Geometry@@PBUIZone@Universe@@@Z  RVA 0xdc6d0
COMMON_API bool __cdecl FrustumCull(Geometry::Frustum const& Frustum, Universe::IZone const* Zone);

// ?SphereCull@@YA_NABUSphere@Geometry@@PBUIZone@Universe@@@Z  RVA 0xdd770
COMMON_API bool __cdecl SphereCull(Geometry::Sphere const& Sphere, Universe::IZone const* Zone);

// ============================================================================
// File — simple file I/O wrapper (not IFileSystem)
// ============================================================================

// Retail layout, sizeof 0x8C: operator= (06262e10) copies 0x23 dwords; File::File
// (0630b270) zeroes Name[0], +0x80, +0x84, +0x88. The ErrorHandler log node embeds one at
// +0x14 (common_errorhandler.cpp). The members are public because the ErrorHandler code in the
// same module reads Handle (+0x80 of the File, node +0x94) directly.
class COMMON_API File
{
public:
    File();             // 0630b270
    ~File();            // 0630b290: JMP close

    File(File const&) = delete;

    // 06262e10: raw 0x8C-byte copy (MOVSD x 0x23) - the copy shares the source's FILE*.
    File& operator=(File const&);

    // 0630cdc0: a null or empty Path skips the body; otherwise the path is copied into
    // Name (unbounded in retail), fopen(Name, "rb"), and on success Length = file size,
    // Offset = 0. A handle already open is overwritten, not closed (as retail).
    // Returns Handle != null.
    bool open(char const* Path);
    void close();                                         // 0630cd40
    int read(void* Buffer, int Bytes);                    // 0630cec0: fread, no Offset update
    // 06262de0: seek(OffsetFromStart), then read(Buffer, Bytes) - the offset is the
    // SECOND parameter (?read@File@@QAEHPAXHH@Z, param_2 goes to seek).
    int read(void* Buffer, int OffsetFromStart, int Bytes);
    int read_line(char* Line, int MaxLen);                // 0630cef0
    bool is_open();                                       // 06262d80
    int eof();                                            // 06262d60: Offset >= Length
    int tell() const;                                     // 06262dd0: Offset
    // 0630ce50: -1 is a pure query; otherwise Offset changes only for a different value in
    // [0, Length]; then fseek to Offset, returning Offset, or 0 without a handle or on failure.
    int seek(int NewOffset);
    int get_length();                                     // 06262d50: Length
    void get_info();                                      // 0630cd70
    void set_name(char const* NewName);                   // 06262d90: copy into Name
    int set_offset(int NewOffset);                        // 06262db0
    char const* get_name() const;                         // 06261260: returns this (Name)

    char Name[0x80];    // +0x00  path; open() fopen()s this buffer
    FILE* Handle;       // +0x80
    int Offset;         // +0x84  cached position (seek / set_offset / read_line / get_info)
    int Length;         // +0x88  cached size (open / get_info)
};

static_assert(sizeof(File) == 0x8C, "File sizeof 0x8C");

// ============================================================================
// Heap — tiny fixed-size-block free-list pool allocator (0xae580/0xae5a0/0xae600).
// Used by CObject::Alloc's per-class object pools (6 static instances, one per
// CObject subclass size). Not ported here: CObject::Alloc itself (avoided
// region -- see the CObject/CEqObj cluster note in the marathon's memory).
// ============================================================================
// Retail exports the ctor, Malloc, Free and Shrink only (no class-level export: no operator=).
class Heap
{
public:
    // Common.dll.c 80679-80690 (0x4d450, ??0Heap@@QAE@IH@Z): a real exported ctor
    // does exist -- {ElementSize, FreeList=nullptr, MaxFreeCount, CurrentFreeCount=0}.
    // Only XREF is Entry Point + an rdata table slot (no traced call site), so the
    // 6 static per-class pools are presumably still built through this ctor at
    // static-init time rather than via a traced call.
    COMMON_API Heap(unsigned int ElementSize, int MaxFreeCount);

    COMMON_API void* Malloc();
    COMMON_API void Free(void* Ptr);
    COMMON_API void Shrink();

private:
    // A pooled block: its first 4 bytes are the link to the next free block (the rest of the
    // block keeps the 0xCD fill written by Free).
    struct FFreeBlock
    {
        FFreeBlock* Next;
    };

    unsigned int ElementSize; // +0x00  size of one pooled block, passed to malloc() on a miss
    FFreeBlock* FreeList;     // +0x04  head of the free-block linked list
    int MaxFreeCount;         // +0x08  pool cap; negative means "unlimited" (always pool)
    int CurrentFreeCount;     // +0x0C  number of blocks currently on FreeList
};

static_assert(sizeof(Heap) == 0x10, "Heap sizeof 0x10");

// ============================================================================
// DamageEntry — single damage record
// ============================================================================

// WHAT IS AND IS NOT SOURCED HERE (settled 2026-09-17, server lane, wave 15).
//
// Retail names not one of these values. DamageList::DmgCauseToString
// (.text:062bbce0) is `MOV EAX,.rdata:0639ed24 / RET` and that .rdata reads
// 3f 3f 3f 00 -- the string "???". There is no table. DamageList::set_cause
// (.text:06332f50) is ICF-folded with EquipDesc::set_status,
// TRANode::set_default_mask and StateMachineMessage::set_delivery_state, and
// all six of its call sites inside Common.dll turn out to be one of those three
// (they push 0x3f800000, an INI float, a list element -- .text:06270d00,
// .text:0629578a, .text:0629ae3f, .text:06311c20, .text:06311d15,
// .text:0631312c), so no caller can be attributed to set_cause at all.
// DamageList::get_cause (.text:06263a70) is a one-liner folded with four more
// getters. "DamageCause" appears in exactly four places across every listing in
// EXE\Disasm, all four inside that one mangled name.
//
// So the six enumerators below are UNSOURCED -- carried over from an earlier
// pass, not read off any byte. They are kept because they compile and nothing
// depends on their spelling, not because they are confirmed.
//
// The one cause value that IS on the bytes is 0x19, and it is the only non-zero
// cause either DLL ever writes. Both writers are Server.dll callers of
// GameObject::get_damage_list, building a DamageList on the stack:
//   .text:06cf61d2  MOV dword ptr [ESP + 0x84],0x19
//   .text:06d07f8a  MOV dword ptr [ESP + 0x74],0x19
// with the record bases at S+0x6c and S+0x64 respectively, so both land on
// +0x10 -- the field get_cause reads. Everywhere else the field keeps the 0 the
// constructor leaves (including StarSystem::damage_object's reduced list,
// .text:06d0ba93). 0x19 therefore cannot be a decode error and cannot be named
// from these two binaries; it would have to come out of freelancer.exe or
// Content.dll, and neither carries the symbol.
enum class DamageCause : int
{
    Unknown = 0,
    Gun = 1,
    Missile = 2,
    Mine = 3,
    Collision = 4,
    Explosion = 5,
};

// Retail is 0xC bytes: {ushort SubObjId; float HitPts; SubObjFate Fate;} -- confirmed via
// DamageEntry::DamageEntry (Common.dll.c 0x20f00: zeroes a ushort at +0, sets a float at +4
// to -1.0f, zeroes a dword at +8) and DamageEntry::operator= (Common.dll.c 0x20f20: copies
// exactly 3 dwords). operator== (0x5bb20) compares all 3 fields; operator</> (0x5bb90/0x5bbb0)
// compare only SubObjId. A previous port revision fabricated extra InflictorId/Time/Cause/
// InflictorOwner fields (0x18 sizeof) that don't exist on this type in retail -- those fields
// actually belong to DamageList itself (see below), not to each per-subobject entry.
struct COMMON_API DamageEntry
{
    enum class SubObjFate : int
    {
        None = 0,
        Destroyed = 1,
        Separated = 2,
        HealthProxy = 3,
    };

    DamageEntry();
    // Retail exports operator= (0x06280f20) but no copy ctor; the implicit trivial copy
    // ctor is not exported. The user-declared operator= also keeps move members away.
    DamageEntry& operator=(DamageEntry const&) = default;

    bool operator==(DamageEntry const& Other) const;
    bool operator!=(DamageEntry const& Other) const;
    bool operator<(DamageEntry const& Other) const;
    bool operator>(DamageEntry const& Other) const;

    static char const* FateToString(SubObjFate Fate);

    unsigned short SubObjId;   // +0x00
    float HitPts;              // +0x04  default -1.0f
    SubObjFate Fate;           // +0x08  default None
};

static_assert(sizeof(DamageEntry) == 0xC, "DamageEntry sizeof 0xC (retail, see comment above)");

// Retail Common.dll exports every member of this STL instantiation (VC6 std::list, 55
// members, imported by name by FreeLancer.exe, Server.dll and rpclocal.dll). The port
// exports the modern MSVC instantiation instead: the definition is in common_damagelist.cpp; other modules
// import it through this declaration. It must precede the first use that would implicitly
// instantiate the class, which is why it sits directly after the element type.
#ifndef COMMON_EXPORTS
extern template class COMMON_API std::list<DamageEntry>;
#endif

// ============================================================================
// DamageList — list of damage entries for an object
// ============================================================================

// Retail is a real (unbounded) std::list<DamageEntry>, not a fixed 8-slot array: ctor
// (Common.dll.c 0x20f70) allocates a VC6 std::list sentinel/proxy node pair at +0x00/+0x04,
// zeroes an int Count at +0x08, a bool bDestroyed at +0x0c, and three more dwords at
// +0x10/+0x14/+0x18 identified via their accessors' ICF-shared bodies: get_cause reads
// +0x10 (DamageList@@get_cause ICF'd with IObjInspectImpl::cobject-family @ Common.dll.c
// 0x3a70), get_inflictor_id reads +0x14 (ICF'd with CEquip::GetType-family @ 0x3bb0),
// get_inflictor_owner_player / is_inflictor_a_player read +0x18 (ICF'd with
// CArchGroup::GetFate-family @ 0x40160). Total retail sizeof is 0x1C, matching the ctor's
// last write at +0x18. add_damage_entry (0x5bc00) dedups by SubObjId instead of appending
// unconditionally: an existing entry gets HitPts replaced by min(old, new) and Fate replaced
// by max(old, new) (i.e. keep the more severe fate); only a genuinely new SubObjId gets a new
// list node, with no 8-entry cap. It also auto-sets bDestroyed when SubObjId==1 (the
// root/hull entry) reaches HitPts<=0. get_hit_pts_left (0x5bbd0) returns the first (only)
// matching entry's HitPts, or -1.0f (not 0) if the SubObjId isn't present. Per this
// codebase's "no retail ABI" convention (see dalib_net.h CDPMsgList, engine_character.h) the list is
// ported as std::list<DamageEntry> rather than reproducing the VC6 node layout.
struct COMMON_API DamageList
{
    DamageList();
    DamageList(DamageList const&);
    DamageList& operator=(DamageList const&);
    ~DamageList();

    void add_damage_entry(unsigned short SubObjId, float HitPts, DamageEntry::SubObjFate Fate);
    float get_hit_pts_left(unsigned short SubObjId) const;
    DamageCause get_cause() const;
    void set_cause(DamageCause Cause);
    // Retail: ?get_inflictor_id@DamageList@@QBEIXZ, ?set_inflictor_id@DamageList@@QAEXI@Z and
    // the owner-player pair alike -- unsigned int (I), not unsigned long.
    unsigned int get_inflictor_id() const;
    void set_inflictor_id(unsigned int Id);
    unsigned int get_inflictor_owner_player() const;
    void set_inflictor_owner_player(unsigned int Player);
    bool is_destroyed() const;
    void set_destroyed(bool bDestroyed);
    bool is_inflictor_a_player() const;

    static char const* DmgCauseToString(DamageCause Cause);

    std::list<DamageEntry> Entries;   // +0x00/+0x04  retail VC6 std::list head+proxy
    int Count;                        // +0x08
    bool bDestroyed;                  // +0x0c
    DamageCause Cause;                 // +0x10
    unsigned int InflictorId;          // +0x14
    unsigned int InflictorOwner;       // +0x18
};

// ============================================================================
// Math/utility globals
// ============================================================================

COMMON_API extern float CHATTER_MAX_DIST;          // DAT_063ed1a4
COMMON_API extern float CHATTER_MAX_DIST_ATTEN;    // DAT_063ed1ac
COMMON_API extern float CHATTER_START_ATTEN;       // DAT_063ed1a8
COMMON_API extern float COLLISION_DAMAGE_FACTOR;   // DAT_063ed184
COMMON_API extern int COMM_CONFLICT_PRIORITY_CUTOFF; // DAT_063ed1b0
COMMON_API extern float COMM_PLAYER_FAR_DIST;      // DAT_063ed19c
COMMON_API extern float COMM_PLAYER_FAR_DIST_ATTEN;// DAT_063ed1a0
COMMON_API extern double DaysPerSecond;            // DAT_063edb78
COMMON_API extern float FIRE_FAILED_DELAY;         // DAT_063ed1c8
COMMON_API extern float JETTISONED_CARGO_VELOCITY; // DAT_063ed194
COMMON_API extern float LOOT_OWNER_SAFE_TIME;      // DAT_063ed188
COMMON_API extern float LOOT_UNSEEN_LIFE_TIME;     // DAT_063ed18c
COMMON_API extern float LOOT_UNSEEN_RADIUS;        // DAT_063ed190
COMMON_API extern int MAX_PLAYER_AMMO;             // DAT_063ed1d4
COMMON_API extern float MUSIC_CROSS_FADE_DELAY;    // DAT_063ed198
COMMON_API extern float MUZZLE_CONE_ANGLE;         // DAT_063ed1c4
COMMON_API extern float PLAYER_ATTACHED_EQUIP_HIT_PTS_SCALE; // DAT_063ed1d0
COMMON_API extern float PLAYER_COLLISION_GROUP_HIT_PTS_SCALE;// DAT_063ed1cc
COMMON_API extern float WALLA_MAX_DIST;            // DAT_063ed1b4
COMMON_API extern float WALLA_MAX_DIST_ATTEN;      // DAT_063ed1b8
COMMON_API extern int WALLA_PRIORITY_CUTOFF;       // DAT_063ed1c0
COMMON_API extern float WALLA_START_ATTEN;         // DAT_063ed1bc

#ifdef COMMON_EXPORTS
// DAT_063ed17c (.data byte, init 01h). Retail does not export it: other modules go through
// SinglePlayer()/SetMultiPlayer(). Visible only inside Common.dll (MPCritSec::lock/unlock and
// the SinglePlayer family read it directly, as retail does).
extern bool GbSinglePlayer;
#endif
COMMON_API extern bool PlayerTrailAvailable; // DAT_063ece0d

// ID_String constants. The real definition, not a local forward declaration: these four are
// ?<name>@@3UID_String@@A in retail, and a bare "struct ID_String;" here would disagree with
// common_id.h's dllimport-decorated one (C2371).
COMMON_API extern ID_String DEATH_COMM_FUSE;
COMMON_API extern ID_String FIRE_FAILED_SOUND;
COMMON_API extern ID_String SND_CARGO_JETTISONED;
COMMON_API extern ID_String SpaceflightLocationID;

// Constants
// Values: NONE=0, ROOT=1, POWER=2 -- see common_misc.cpp for the retail-address verification note.
COMMON_API extern unsigned short const SUBOBJ_ID_NONE;   // .rdata:063a4aa4
COMMON_API extern unsigned short const SUBOBJ_ID_ROOT;   // .rdata:063a4aa6
COMMON_API extern unsigned short const SUBOBJ_ID_POWER;  // .rdata:063a4aa8

// State-machine message-type ids, NOT tuning values. Retail declares both as
// `unsigned int const` (?SMM_TIMEOUT@@3IB ord 1949, ?SMM_CHANGE_STATE@@3IB ord 1948) but leaves
// the .data slots uninitialised in the image and fills them from a CRT static initialiser that
// inlines StateMachineMessage::GenerateUniqueMessageType() (.text:06332f70). Values are pinned
// to 1 and 2 by CRT-table slot order -- see the derivation note in common_misc.cpp, where the
// definition order is load-bearing.
COMMON_API extern unsigned const SMM_TIMEOUT;            // .data:0640a764 (assigned 2)
COMMON_API extern unsigned const SMM_CHANGE_STATE;       // .data:0640a768 (assigned 1)

// ============================================================================
// Utility functions
// ============================================================================

// Random generation
COMMON_API Vector __cdecl RandomVector(float Radius);
COMMON_API Vector __cdecl random_on_unit_sphere();
COMMON_API Vector __cdecl random_inside_sphere(float Radius);
COMMON_API Vector __cdecl random_inside_box(float X, float Y, float Z);
COMMON_API Vector __cdecl random_inside_cylinder(float Radius, float HalfHeight);   // axis y
COMMON_API Vector __cdecl random_inside_ellipsoid(float A, float B, float C);
COMMON_API Vector __cdecl random_inside_ring(float Outer, float Inner, float HalfHeight); // axis y
COMMON_API Vector __cdecl random_sphere_constrained(Vector const& Dir, float Angle);
COMMON_API Vector __cdecl random_sphere_constrained_2(Vector const& Dir, float MinAngle, float MaxAngle);

// Safe string ops. Size is sizeof(Dest) in BYTES for all three (safe_wcscpy divides by 2
// internally to get a wchar_t count, matching Common.dll.txt:0xa4840's `Size >> 1`).
COMMON_API char* __cdecl safe_strcpy(char* Dest, int Size, char const* Src);
COMMON_API char* __cdecl safe_strcat(char* Dest, int Size, char const* Src);
COMMON_API wchar_t* __cdecl safe_wcscpy(wchar_t* Dest, int Size, wchar_t const* Src);

// String equality
COMMON_API bool __cdecl strequal(char const* A, char const* B);

// Build directory helper
COMMON_API void __cdecl BuildDirectory(char const* Path);

// Debug output
COMMON_API void __cdecl DebugPrint(char const* Format, ...);

// CmnMisc.cpp file-system paths, single-player flag and CleanUpDatabases, defined in
// common_misc.cpp. (SetIsMPServer / IsMPServer are declared in common_net.h.)
struct IFileSystem;

// Common.dll.c @ 0xa3220 / 0xa3210 — not Timing. Default GbSinglePlayer = true (.data 01h).
COMMON_API bool __cdecl SinglePlayer();
COMMON_API void __cdecl SetMultiPlayer(bool bMultiPlayer);
// Common.dll.c @ 0xa4870 — JMP Reputation::Vibe::FreeDynamic (not CObjDB / not class lists).
// Connect calls this *before* spawn — must not run DataConfig::shutdown.
COMMON_API void __cdecl CleanUpDatabases();

// Common.dll.c GetFileSysPath @ 0xa2df0
// ?GetFileSysPath@@YAXPADPBDPAUIFileSystem@@@Z
COMMON_API void __cdecl GetFileSysPath(char* Dest, char const* RelOrAbs, IFileSystem* Filesys);

// Common.dll.c GetFullFilename @ 0xa2ea0
// ?GetFullFilename@@YAPBDPBD@Z — returns static 0x104 buffer (DAT_063fccf8).
COMMON_API char const* __cdecl GetFullFilename(char const* RelOrAbs);

// SHGetFolderPath + _mkdir. Mangle QAD = char * const. No GetUserPath export.
// ?GetUserLocalDataPath@@YA_NQAD@Z  CSIDL_LOCAL_APPDATA|CREATE + "Freelancer"
// ?GetUserDataPath@@YA_NQAD@Z       CSIDL_PERSONAL|CREATE + "My Games" + "Freelancer"
// ?GetScreenShotPath@@YA_NQAD@Z     CSIDL_MYPICTURES|CREATE + "FreelancerShots"
COMMON_API bool __cdecl GetUserLocalDataPath(char* const Dest);
COMMON_API bool __cdecl GetUserDataPath(char* const Dest);
COMMON_API bool __cdecl GetScreenShotPath(char* const Dest);

// Common.dll.c DAEngine_create_filesystem @ 0xa3140 / release @ 0x1ba10
// ?DAEngine_create_filesystem@@YAXPBDPAPAUIFileSystem@@0@Z
// ?DAEngine_release_filesystem@@YAXPAPAUIFileSystem@@@Z
COMMON_API void __cdecl DAEngine_create_filesystem(char const* Path, IFileSystem** Out,
                                        char const* Implementation);
COMMON_API void __cdecl DAEngine_release_filesystem(IFileSystem** Filesys);

// "1.11_ICamera" -- the client camera. Two classes in freelancer.exe implement it:
//   * the 20-slot table PTR_LAB_005c8944 (0x005c8944..0x005c8990), which is the one
//     src/freelancer/Client/space_camera.h models as FSpaceCamera, and whose slot roles are
//     recovered one by one from their own bodies -- that is where the names below come from;
//   * the 22-slot table 0x005e12a4 (and its sibling 0x005e1304), whose slot 0 LAB_00558600 is
//     the QueryInterface that strcmps "1.11_ICamera". Its field layout is different, and only
//     slots 3, 4 and 16 are settled there, from the sound mixer's call sites at 0042816e and
//     004281a2.
//
// The declaration lives here because Common.dll's get_projected_bounding_sphere/box below take
// one, and this header is where the forward declaration already was.
//
// TODO: slot 5 is the one place the two classes look inconsistent. FSpaceCamera's slot 5 is
// LAB_0040f470 (`this + 0xac`, the ViewRect) while the 22-slot class puts LAB_0040f380
// (`this + 0x04`) there -- and LAB_0040f380 is FSpaceCamera's slot 4. Either it is a shared
// `return this + 4` stub landing on a different field, or one of the two tables is not this
// interface. Nothing in the tree calls slot 5 on the 22-slot class, so the name below is
// asserted from FSpaceCamera alone.
struct ICamera : IDAComponent
{
    // 3 +0x0C -- LAB_0040f370 `this + 0x28`; the other class LAB_00556a80 `this + 0x80`.
    virtual float const* DACOM_API GetPosition() = 0;
    // 4 +0x10 -- LAB_0040f380 `this + 0x04`; the other class LAB_00556a90 `this + 0x5C`.
    // Nine floats, row-major axes. The sound mixer feeds this straight into
    // ISoundManager::SetOrientationFromMatrix, which is what proves it is the 3x3.
    virtual float const* DACOM_API GetOrientation() = 0;
    // 5 +0x14 -- LAB_0040f470, the ViewRect (left, top, right, bottom), inclusive edges.
    virtual int const* DACOM_API GetViewport() = 0;
    virtual float DACOM_API GetHalfFovXDeg() = 0;      // 6  +0x18 LAB_0040f480
    virtual float DACOM_API GetFovYDeg() = 0;          // 7  +0x1C LAB_0040f490
    virtual float DACOM_API GetZNear() = 0;            // 8  +0x20 LAB_0040f4a0
    virtual float DACOM_API GetZFar() = 0;             // 9  +0x24 LAB_0040f4b0
    virtual float DACOM_API GetAspect() = 0;           // 10 +0x28 LAB_0040f4c0
    // 11 / 12: every implementer returns an x87 quotient or product that no FSTP rounds
    // (LAB_0040f4d0 / LAB_0040f4e0 FLD / FDIV / RET 4, room camera LAB_00556680 / 00556690,
    // flmaterials LAB_066fdd40 / FUN_066fdd80 FMUL after the inner call), so the value the
    // caller sees is wider than a float: double here (project /arch:IA32 convention).
    virtual double DACOM_API GetXScaleOverNear() = 0;  // 11 +0x2C LAB_0040f4d0
    virtual double DACOM_API GetYScaleOverNear() = 0;  // 12 +0x30 LAB_0040f4e0
    // 13 +0x34  0x0040ff50 — Project(out x, out y, out z, world point). RET 0x14.
    virtual int DACOM_API Project(float* OutX, float* OutY, float* OutZ,
                                  float const* World) = 0;
    // 14 +0x38  0x0040f520 — Unproject(out dir, screenX, screenY). RET 0x10.
    virtual void DACOM_API Unproject(float* OutWorld3, float ScreenX, float ScreenY) = 0;
    // 15 +0x3C  0x0040ffe0 — sphere-vs-frustum (1=out, 3=straddle, 4=in). RET 0xC.
    virtual int DACOM_API ClassifySphere(float const* Center, float Radius) = 0;
    // 16 +0x40 -- LAB_0040f390, the packed view transform, rebuilt lazily; the other class's
    // LAB_00556aa0 builds the same 12 floats.
    virtual float const* DACOM_API GetViewTransform() = 0;
    // 17 +0x44  0x0040f4f0 — project-to-vector (out3, world). RET 0xC. The body tail-calls
    // slot 13 Project and returns its result; HUD_Target tests it (0x004e3f60, 0x004e507f).
    virtual int DACOM_API ProjectVec(float* Out3, float const* World) = 0;
    // 18 +0x48  0x0040f5d0 — velocity out-vector. Retail __thiscall, one stack arg, RET 0x4.
    virtual void GetVelocity(float* Out3) = 0;
    // 19 +0x4C  0x0040f5f0 — SpaceCamera "fovx"/"fovy"/"znear"/"zfar". Retail __thiscall,
    // two stack args, RET 0x8 (this in ECX). Room camera's same slot is SetPosition(Vector*)
    // (FUN_00556b70, stdcall RET 0x8) — different body, same cleanup width.
    virtual int SetProperty(char const* Key, char const* Value) = 0;

};

// Common.dll 06304320: the intersection volume of two spheres whose centres are Dist apart.
COMMON_API float __cdecl GetSphereIntersectVol(float Dist, float RadiusA, float RadiusB);
COMMON_API long __cdecl get_projected_bounding_sphere(ICamera* Camera, Vector const& Center, float Radius, float& Out1, float& Out2, float& Out3, float& Out4);
struct ViewRect;
COMMON_API long __cdecl get_projected_bounding_box(ICamera* Camera, Vector const& Center, float Radius, ViewRect* Rect, float& OutZ);
COMMON_API void __cdecl ComputeExplosiveImpulses(long EngineHandle, Vector& OutLinear, Vector& OutAngular1, Vector& OutAngular2);

